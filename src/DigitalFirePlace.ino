#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SSD1306.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Wire.h>
#include <math.h>

#include "fireplace_config.h"
#include "fire_animation.h"

#ifdef ARDUINO_ARCH_ESP32
#include <WebServer.h>
#include <WiFi.h>
#endif

struct ButtonState {
  uint8_t pin;
  bool stablePressed;
  uint32_t lastChangeMs;
  uint32_t lastRepeatMs;
  bool repeating;
};

enum class ButtonEvent { kNone, kPressed, kRepeat };

template <typename T>
T clampValue(T value, T minValue, T maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

static ButtonState buttonTempUp{FireplaceConfig::kButtonTempUp, false, 0, 0, false};
static ButtonState buttonTempDown{FireplaceConfig::kButtonTempDown, false, 0, 0, false};
static ButtonState buttonBrightUp{FireplaceConfig::kButtonBrightUp, false, 0, 0, false};
static ButtonState buttonBrightDown{FireplaceConfig::kButtonBrightDown, false, 0, 0, false};
static ButtonState buttonMode{FireplaceConfig::kButtonMode, false, 0, 0, false};

static Adafruit_SSD1306 display(128, 64, &Wire, FireplaceConfig::kOledResetPin);
static Adafruit_NeoPixel strip(FireplaceConfig::kNeoPixelCount, FireplaceConfig::kNeoPixelPin,
                               NEO_GRB + NEO_KHZ800);
static OneWire oneWire(FireplaceConfig::kTemperatureSensorPin);
static DallasTemperature temperatureSensor(&oneWire);
static bool displayReady = false;
static float targetTemperatureC = 21.0f;
static uint8_t targetBrightness = 160;
static uint8_t targetColorPercent = 0;
static FireAnimation::State fireState{targetBrightness, 0};
enum class OperatingMode { kFireOnly, kFireAndHeat, kHeatOnly };

static bool heaterActive = false;
static OperatingMode operatingMode = OperatingMode::kFireAndHeat;
static float lastTemperatureC = NAN;

#ifdef ARDUINO_ARCH_ESP32
static WebServer server(80);
#endif

// ----------------- Helpers -----------------

static const char *modeLabel() {
  switch (operatingMode) {
    case OperatingMode::kFireOnly:
      return "Fire";
    case OperatingMode::kFireAndHeat:
      return "Fire+Heat";
    case OperatingMode::kHeatOnly:
      return "Heat";
  }
  return "Fire+Heat";
}

static void cycleMode() {
  switch (operatingMode) {
    case OperatingMode::kFireOnly:
      operatingMode = OperatingMode::kFireAndHeat;
      break;
    case OperatingMode::kFireAndHeat:
      operatingMode = OperatingMode::kHeatOnly;
      break;
    case OperatingMode::kHeatOnly:
      operatingMode = OperatingMode::kFireOnly;
      break;
  }
}

static uint8_t effectiveBrightness() {
  return operatingMode == OperatingMode::kHeatOnly ? 0 : targetBrightness;
}

static uint8_t brightnessToPercent(uint8_t brightness) {
  const float percent =
      (static_cast<float>(brightness) / static_cast<float>(FireplaceConfig::kMaxBrightness)) * 100.0f;
  return static_cast<uint8_t>(round(percent));
}

static uint8_t brightnessFromPercent(int percent) {
  const int clamped = clampValue(percent, 0, 100);
  const float brightness =
      (static_cast<float>(clamped) / 100.0f) * static_cast<float>(FireplaceConfig::kMaxBrightness);
  return static_cast<uint8_t>(round(brightness));
}

static uint16_t hueDegreesFromPercent(uint8_t colorPercent) {
  const float hue = (static_cast<float>(colorPercent) / 100.0f) * 360.0f;
  return static_cast<uint16_t>(round(hue));
}

// Approximate human-readable colour name for flame
static const char* colorNameFromPercent(uint8_t p) {
  if (p < 12)  return "Red";
  if (p < 28)  return "Orange";
  if (p < 48)  return "Green";
  if (p < 68)  return "Blue";
  if (p < 88)  return "Violet";
  return "Red";
}

static ButtonEvent updateButton(ButtonState &button) {
  const uint32_t now = millis();
  const bool reading = digitalRead(button.pin) == LOW;  // Active-low buttons

  if (reading != button.stablePressed) {
    if (now - button.lastChangeMs >= FireplaceConfig::kDebounceDelayMs) {
      button.stablePressed = reading;
      button.lastChangeMs = now;
      button.repeating = false;
      button.lastRepeatMs = now;
      if (button.stablePressed) {
        return ButtonEvent::kPressed;
      }
    }
  }

  if (button.stablePressed) {
    const uint32_t threshold =
        button.repeating ? FireplaceConfig::kHoldRepeatRateMs
                          : FireplaceConfig::kHoldRepeatDelayMs;
    if (now - button.lastRepeatMs >= threshold) {
      button.lastRepeatMs = now;
      button.repeating = true;
      return ButtonEvent::kRepeat;
    }
  }

  return ButtonEvent::kNone;
}

static float readTemperatureCelsius() {
  temperatureSensor.requestTemperatures();
  const float reading = temperatureSensor.getTempCByIndex(0);
  if (reading == DEVICE_DISCONNECTED_C) {
    return NAN;
  }
  return reading;
}

static void updateDisplay(float currentTemperatureC) {
  if (!displayReady) {
    return;
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Room:");
  display.setCursor(0, 10);
  display.print(currentTemperatureC, 1);
  display.cp437(true);
  // display.write(247);  // Degree symbol
  display.println("C");

  display.setCursor(0, 25);
  display.println("Set:");
  display.print(targetTemperatureC, 1);
  // display.write(247);
  display.println("C");

  display.setTextSize(1);
  display.setCursor(70, 0);
  display.print("Mode:");
  display.setCursor(70, 10);
  display.print(modeLabel());

  display.setCursor(0, 50);
  display.print("Bright:");
  display.print(effectiveBrightness());

  display.setCursor(80, 50);
  display.print(heaterActive ? "HEAT ON" : "HEAT OFF");

  display.display();
}

static float smoothTemperature(float measurementC) {
  if (isnan(measurementC)) {
    return lastTemperatureC;
  }
  if (isnan(lastTemperatureC)) {
    return measurementC;
  }
  const float alpha = FireplaceConfig::kTemperatureSmoothAlpha;
  return (alpha * measurementC) + ((1.0f - alpha) * lastTemperatureC);
}

// ----------------- Web UI (ESP32) -----------------
#ifdef ARDUINO_ARCH_ESP32

static String htmlHeader() {
  String h;
  h  = "<!doctype html><html><head><meta charset='utf-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>Digital Fireplace</title><style>";

  // LIGHT MODE
  h += "@media (prefers-color-scheme: light){";
  h += "body{margin:0;font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;";
  h += "background:#f5f5f5;color:#111;display:flex;justify-content:center;align-items:center;";
  h += "min-height:100vh;}";
  h += ".card{background:#ffffff;border-radius:16px;padding:20px 18px;max-width:420px;width:100%;";
  h += "box-sizing:border-box;box-shadow:0 10px 30px rgba(0,0,0,0.12);border:1px solid #e0e0e0;";
  h += "overflow:hidden;}";
  h += ".title{font-size:1.2rem;font-weight:600;margin:0 0 4px 0;color:#222;}";
  h += ".subtitle{font-size:0.85rem;color:#666;margin:0 0 16px 0;}";
  h += ".stat-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-bottom:14px;}";
  h += ".stat-label{font-size:0.7rem;text-transform:uppercase;letter-spacing:0.06em;color:#888;margin-bottom:2px;}";
  h += ".stat-value{font-size:1.15rem;font-weight:600;}";
  h += ".stat-value-heat{font-size:0.9rem;font-weight:600;}";
  h += ".stat-pill{font-size:0.75rem;border-radius:999px;padding:4px 8px;background:#fff;";
  h += "border:1px solid #ddd;display:inline-flex;align-items:center;gap:6px;margin-top:2px;}";
  h += ".color-chip{display:inline-block;width:12px;height:12px;border-radius:999px;border:1px solid #ccc;}";
  h += ".section{margin-top:10px;}";
  h += "label{display:block;font-size:0.8rem;margin-bottom:4px;color:#555;}";
  h += "input[type=range]{width:100%;box-sizing:border-box;margin:0;}";
  h += "input,button{font-family:inherit;font-size:0.85rem;border-radius:999px;border:1px solid #ccc;";
  h += "padding:6px 10px;background:#fafafa;color:#111;}";
  h += ".mode-buttons{display:flex;gap:6px;flex-wrap:wrap;margin-top:4px;}";
  h += ".mode-buttons button{flex:1 1 auto;background:#f3f4f6;border-color:#d1d5db;}";
  h += ".mode-buttons button.active{background:#0ea5e9;color:#fff;border-color:#0ea5e9;}";
  h += ".status-line{font-size:0.75rem;color:#666;margin-top:10px;}";
  h += ".scale-row{display:flex;justify-content:space-between;font-size:0.7rem;color:#777;margin-top:2px;}";
  h += "}";

  // DARK MODE
  h += "@media (prefers-color-scheme: dark){";
  h += "body{margin:0;font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;";
  h += "background:#020617;color:#e5e7eb;display:flex;justify-content:center;align-items:center;";
  h += "min-height:100vh;}";
  h += ".card{background:#020617;border-radius:16px;padding:20px 18px;max-width:420px;width:100%;";
  h += "box-sizing:border-box;box-shadow:0 20px 40px rgba(0,0,0,0.7);border:1px solid rgba(148,163,184,0.35);";
  h += "overflow:hidden;}";
  h += ".title{font-size:1.2rem;font-weight:600;margin:0 0 4px 0;color:#f9fafb;}";
  h += ".subtitle{font-size:0.8rem;color:#9ca3af;margin:0 0 16px 0;}";
  h += ".stat-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-bottom:14px;}";
  h += ".stat-label{font-size:0.7rem;text-transform:uppercase;letter-spacing:0.06em;color:#9ca3af;margin-bottom:2px;}";
  h += ".stat-value{font-size:1.15rem;font-weight:600;}";
  h += ".stat-value-heat{font-size:0.9rem;font-weight:600;}";
  h += ".stat-pill{font-size:0.75rem;border-radius:999px;padding:4px 8px;";
  h += "background:rgba(15,23,42,0.9);border:1px solid rgba(148,163,184,0.5);display:inline-flex;align-items:center;gap:6px;margin-top:2px;}";
  h += ".color-chip{display:inline-block;width:12px;height:12px;border-radius:999px;border:1px solid #64748b;}";
  h += ".section{margin-top:10px;}";
  h += "label{display:block;font-size:0.8rem;margin-bottom:4px;color:#e5e7eb;}";
  h += "input[type=range]{width:100%;box-sizing:border-box;margin:0;}";
  h += "input,button{font-family:inherit;font-size:0.85rem;border-radius:999px;border:1px solid #334155;";
  h += "padding:6px 10px;background:#020617;color:#e5e7eb;}";
  h += ".mode-buttons{display:flex;gap:6px;flex-wrap:wrap;margin-top:4px;}";
  h += ".mode-buttons button{flex:1 1 auto;background:#020617;border-color:#334155;}";
  h += ".mode-buttons button.active{background:#0ea5e9;color:#fff;border-color:#0ea5e9;}";
  h += ".status-line{font-size:0.75rem;color:#9ca3af;margin-top:10px;}";
  h += ".scale-row{display:flex;justify-content:space-between;font-size:0.7rem;color:#9ca3af;margin-top:2px;}";
  h += "}";

  h += "</style></head><body>";
  return h;
}

static String htmlFooter() {
  return "</body></html>";
}

static String renderPage() {
  String page = htmlHeader();

  page += "<div class='card'>";
  page += "<h1 class='title'>Digital Fireplace</h1>";
  page += "<p class='subtitle'>Virtual flame with thermostat control</p>";

  // Live status block
  page += "<div class='stat-grid'>";
  page += "  <div>";
  page += "    <div class='stat-label'>Room</div>";
  page += "    <div id='roomTemp' class='stat-value'>";
  if (isnan(lastTemperatureC)) {
    page += "--.- &deg;C";
  } else {
    page += String(lastTemperatureC, 1);
    page += " &deg;C";
  }
  page += "    </div>";
  page += "  </div>";

  page += "  <div>";
  page += "    <div class='stat-label'>Set</div>";
  page += "    <div id='setTemp' class='stat-value'>";
  page += String(targetTemperatureC, 1);
  page += " &deg;C</div>";
  page += "  </div>";

  page += "  <div>";
  page += "    <div class='stat-label'>Mode</div>";
  page += "    <div id='modeLabel' class='stat-value'>";
  page += modeLabel();
  page += "    </div>";
  page += "  </div>";

  page += "  <div>";
  page += "    <div class='stat-label'>Heat</div>";
  page += "    <div id='heatStatus' class='stat-value-heat'>";
  page += heaterActive ? "HEAT ON" : "HEAT OFF";
  page += "    </div>";
  page += "  </div>";
  page += "</div>";

  page += "<div class='stat-pill'>";
  page += "Brightness: <span id='brightLabel'>";
  page += brightnessToPercent(effectiveBrightness());
  page += "</span>%";
  page += "</div> ";

  page += "<div class='stat-pill'>";
  page += "Colour: <span id='colorLabel'>";
  page += colorNameFromPercent(targetColorPercent);
  page += "</span>";
  page += "<span id='colorChip' class='color-chip'></span>";
  page += "</div>";

  // Controls: temperature
  page += "<div class='section'>";
  page += "<label for='temp'>Target temperature (&deg;C)</label>";
  page += "<input type='range' id='temp' name='temp' step='0.5' min='15' max='30' list='temp-scale' value='";
  page += String(targetTemperatureC, 1);
  page += "'>";
  page += "<datalist id='temp-scale'><option value='15'><option value='18'><option value='21'><option value='24'><option value='27'><option value='30'></datalist>";
  page += "<div class='scale-row'><span>15</span><span>18</span><span>21</span><span>24</span><span>27</span><span>30</span></div>";
  page += "<div class='status-line'>Current target: <span id='temp-value'>";
  page += String(targetTemperatureC, 1);
  page += "</span> &deg;C</div>";
  page += "</div>";

  // Controls: brightness
  page += "<div class='section'>";
  page += "<label for='bright'>Brightness (0–100%)</label>";
  page += "<input type='range' id='bright' name='bright' min='0' max='100' step='1' list='bright-scale' value='";
  page += brightnessToPercent(targetBrightness);
  page += "'>";
  page += "<datalist id='bright-scale'><option value='0'><option value='25'><option value='50'><option value='75'><option value='100'></datalist>";
  page += "<div class='scale-row'><span>0</span><span>25</span><span>50</span><span>75</span><span>100</span></div>";
  page += "<div class='status-line'>Current brightness: <span id='bright-value'>";
  page += brightnessToPercent(targetBrightness);
  page += "</span>%</div>";
  page += "</div>";

  // Controls: colour
  page += "<div class='section'>";
  page += "<label for='color'>Flame colour (full spectrum)</label>";
  page += "<input type='range' id='color' name='color' min='0' max='100' step='1' list='color-scale' value='";
  page += String(targetColorPercent);
  page += "'>";
  page += "<datalist id='color-scale'><option value='0'><option value='25'><option value='50'><option value='75'><option value='100'></datalist>";
  page += "<div class='scale-row'><span>Red</span><span>Green</span><span>Blue</span><span>Violet</span><span>Red</span></div>";
  page += "<div class='status-line'>Colour: <span id='color-value'>";
  page += colorNameFromPercent(targetColorPercent);
  page += "</span></div>";
  page += "</div>";

  // Controls: mode
  page += "<div class='section'>";
  page += "<label>Mode</label>";
  page += "<div class='mode-buttons'>";
  page += "<button type='button' data-mode='0";
  page += (operatingMode == OperatingMode::kFireOnly ? "' class='active'>" : "'>");
  page += "Fire only</button>";
  page += "<button type='button' data-mode='1";
  page += (operatingMode == OperatingMode::kFireAndHeat ? "' class='active'>" : "'>");
  page += "Fire + Heat</button>";
  page += "<button type='button' data-mode='2";
  page += (operatingMode == OperatingMode::kHeatOnly ? "' class='active'>" : "'>");
  page += "Heat only</button>";
  page += "</div>";
  page += "</div>";

  page += "<div id='status' class='status-line'>Syncing with fireplace...</div>";

  // Script: auto-refresh + controls + colour chip
  page += "<script>";
  page += "const tempInput=document.getElementById('temp');";
  page += "const tempValue=document.getElementById('temp-value');";
  page += "const brightInput=document.getElementById('bright');";
  page += "const brightValue=document.getElementById('bright-value');";
  page += "const colorInput=document.getElementById('color');";
  page += "const colorValue=document.getElementById('color-value');";
  page += "const modeButtons=document.querySelectorAll('.mode-buttons button');";
  page += "const roomEl=document.getElementById('roomTemp');";
  page += "const setEl=document.getElementById('setTemp');";
  page += "const modeLabelEl=document.getElementById('modeLabel');";
  page += "const brightLabelEl=document.getElementById('brightLabel');";
  page += "const colorLabelEl=document.getElementById('colorLabel');";
  page += "const heatEl=document.getElementById('heatStatus');";
  page += "const statusEl=document.getElementById('status');";
  page += "const colorChip=document.getElementById('colorChip');";

  page += "const colorNameFromPercent=p=>{";
  page += " const n=Number(p);";
  page += " if(n<12) return 'Red';";
  page += " if(n<28) return 'Orange';";
  page += " if(n<48) return 'Green';";
  page += " if(n<68) return 'Blue';";
  page += " if(n<88) return 'Violet';";
  page += " return 'Red';";
  page += "};";

  page += "const colorChipFromPercent=p=>{";
  page += " const n=Number(p);";
  page += " const hue=Math.round((n/100)*360);";
  page += " const color=`hsl(${hue},90%,60%)`;";  // red (0) through blue back to red (360)
  page += " return color;";
  page += "};";

  page += "function updateChip(p){";
  page += " const c=colorChipFromPercent(p);";
  page += " if(colorChip){";
  page += "   colorChip.style.backgroundColor=c;";
  page += "   colorChip.style.boxShadow=`0 0 6px ${c}`;";
  page += " }";
  page += "}";

  page += "function sendUpdate(params){";
  page += " const url=new URL('/apply',window.location.href);";
  page += " Object.keys(params).forEach(k=>url.searchParams.set(k,params[k]));";
  page += " fetch(url.toString()).catch(console.error);";
  page += "}";

  page += "tempInput.addEventListener('input',()=>{";
  page += " tempValue.textContent=tempInput.value;";
  page += " setEl.textContent=tempInput.value+' \\u00B0C';";
  page += " sendUpdate({temp:tempInput.value});";
  page += "});";

  page += "brightInput.addEventListener('input',()=>{";
  page += " brightValue.textContent=brightInput.value;";
  page += " brightLabelEl.textContent=brightInput.value;";
  page += " sendUpdate({bright:brightInput.value});";
  page += "});";

  page += "colorInput.addEventListener('input',()=>{";
  page += " const name=colorNameFromPercent(colorInput.value);";
  page += " colorValue.textContent=name;";
  page += " colorLabelEl.textContent=name;";
  page += " updateChip(colorInput.value);";
  page += " sendUpdate({color:colorInput.value});";
  page += "});";

  page += "function modeLabelFromInt(m){";
  page += " if(m===0) return 'Fire';";
  page += " if(m===2) return 'Heat';";
  page += " return 'Fire+Heat';";
  page += "}";

  page += "function applyState(state){";
  page += " if(state.temp!==undefined){";
  page += "   tempInput.value=state.temp;";
  page += "   tempValue.textContent=state.temp;";
  page += "   setEl.textContent=state.temp+' \\u00B0C';";
  page += " }";
  page += " if(state.bright!==undefined){";
  page += "   brightInput.value=state.bright;";
  page += "   brightValue.textContent=state.bright;";
  page += "   brightLabelEl.textContent=state.bright;";
  page += " }";
  page += " if(state.color!==undefined){";
  page += "   colorInput.value=state.color;";
  page += "   const name=colorNameFromPercent(state.color);";
  page += "   colorValue.textContent=name;";
  page += "   colorLabelEl.textContent=name;";
  page += "   updateChip(state.color);";
  page += " }";
  page += " if(state.mode!==undefined){";
  page += "   modeButtons.forEach(btn=>{";
  page += "     const active=btn.getAttribute('data-mode')===String(state.mode);";
  page += "     btn.classList.toggle('active',active);";
  page += "   });";
  page += "   modeLabelEl.textContent=modeLabelFromInt(state.mode);";
  page += " }";
  page += " if(state.room!==undefined){";
  page += "   if(state.room===null){";
  page += "     roomEl.textContent='--.- \\u00B0C';";
  page += "   }else{";
  page += "     roomEl.textContent=Number(state.room).toFixed(1)+' \\u00B0C';";
  page += "   }";
  page += " }";
  page += " if(state.heat!==undefined){";
  page += "   heatEl.textContent=state.heat? 'HEAT ON':'HEAT OFF';";
  page += " }";
  page += "}";

  page += "async function refreshState(){";
  page += " try{";
  page += "   const resp=await fetch('/state',{cache:'no-store'});";
  page += "   if(!resp.ok){statusEl.textContent='Sync error: '+resp.status;return;}";
  page += "   const data=await resp.json();";
  page += "   applyState(data);";
  page += "   statusEl.textContent='Last sync: '+new Date().toLocaleTimeString();";
  page += " }catch(err){";
  page += "   statusEl.textContent='Sync failed';";
  page += " }";
  page += "}";

  page += "modeButtons.forEach(btn=>btn.addEventListener('click',()=>{";
  page += " modeButtons.forEach(b=>b.classList.remove('active'));";
  page += " btn.classList.add('active');";
  page += " const m=btn.getAttribute('data-mode');";
  page += " sendUpdate({mode:m});";
  page += "}));";

  page += "updateChip(colorInput.value);";  // initial chip
  page += "refreshState();";
  page += "setInterval(refreshState,2000);";
  page += "</script>";

  page += "</div>";
  page += htmlFooter();
  return page;
}

static void handleRoot() {
  server.send(200, "text/html", renderPage());
}

static void handleApply() {
  if (server.hasArg("temp")) {
    const float requested = server.arg("temp").toFloat();
    targetTemperatureC = clampValue(requested, FireplaceConfig::kMinTargetTemperature,
                                    FireplaceConfig::kMaxTargetTemperature);
  }

  if (server.hasArg("bright")) {
    const int requestedPercent = server.arg("bright").toInt();
    targetBrightness = brightnessFromPercent(requestedPercent);
  }

  if (server.hasArg("color")) {
    const int requestedColor = server.arg("color").toInt();
    targetColorPercent = static_cast<uint8_t>(clampValue(requestedColor, 0, 100));
  }

  if (server.hasArg("mode")) {
    const int requestedMode = server.arg("mode").toInt();
    switch (requestedMode) {
      case 0: operatingMode = OperatingMode::kFireOnly; break;
      case 1: operatingMode = OperatingMode::kFireAndHeat; break;
      case 2: operatingMode = OperatingMode::kHeatOnly; break;
      default: break;
    }
  }

  server.send(200, "text/plain", "OK");
}

static String renderStateJson() {
  String json = "{";
  json += "\"temp\":";
  json += String(targetTemperatureC, 1);
  json += ",\"bright\":";
  json += String(brightnessToPercent(targetBrightness));
  json += ",\"color\":";
  json += String(targetColorPercent);
  json += ",\"mode\":";
  json += String(static_cast<int>(operatingMode));
  json += ",\"room\":";
  if (isnan(lastTemperatureC)) {
    json += "null";
  } else {
    json += String(lastTemperatureC, 1);
  }
  json += ",\"heat\":";
  json += heaterActive ? "1" : "0";
  json += "}";
  return json;
}

static void handleState() {
  server.send(200, "application/json", renderStateJson());
}

static void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

static void startWifiAndServer() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(FireplaceConfig::kWifiSsid, FireplaceConfig::kWifiPassword);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < FireplaceConfig::kWifiConnectTimeoutMs) {
    delay(100);
  }

  server.on("/", handleRoot);
  server.on("/apply", handleApply);
  server.on("/state", handleState);
  server.onNotFound(handleNotFound);
  server.begin();
}

static void handleHttp() {
  server.handleClient();
}

#else   // not ESP32
static void startWifiAndServer() {}
static void handleHttp() {}
#endif  // ARDUINO_ARCH_ESP32

// ----------------- Heater / Buttons / Display / Loop -----------------

static void updateHeater(float currentTemperatureC) {
  const bool heatEnabled = operatingMode != OperatingMode::kFireOnly;
  if (!heatEnabled) {
    heaterActive = false;
  } else {
    const float lowerThreshold =
        targetTemperatureC - (FireplaceConfig::kHysteresis / 2.0f);
    const float upperThreshold =
        targetTemperatureC + (FireplaceConfig::kHysteresis / 2.0f);

    if (!heaterActive && currentTemperatureC <= lowerThreshold) {
      heaterActive = true;
    } else if (heaterActive && currentTemperatureC >= upperThreshold) {
      heaterActive = false;
    }
  }
  digitalWrite(FireplaceConfig::kRelayPin, heaterActive ? HIGH : LOW);
}

static void handleButtons() {
  const ButtonEvent tempUpEvent = updateButton(buttonTempUp);
  const ButtonEvent tempDownEvent = updateButton(buttonTempDown);
  const ButtonEvent brightUpEvent = updateButton(buttonBrightUp);
  const ButtonEvent brightDownEvent = updateButton(buttonBrightDown);
  const ButtonEvent modeEvent = updateButton(buttonMode);

  if (tempUpEvent != ButtonEvent::kNone) {
    targetTemperatureC = clampValue(targetTemperatureC + FireplaceConfig::kTemperatureStep,
                                    FireplaceConfig::kMinTargetTemperature,
                                    FireplaceConfig::kMaxTargetTemperature);
  }
  if (tempDownEvent != ButtonEvent::kNone) {
    targetTemperatureC = clampValue(targetTemperatureC - FireplaceConfig::kTemperatureStep,
                                    FireplaceConfig::kMinTargetTemperature,
                                    FireplaceConfig::kMaxTargetTemperature);
  }
  if (brightUpEvent != ButtonEvent::kNone) {
    const int next = targetBrightness + FireplaceConfig::kBrightnessStep;
    targetBrightness = static_cast<uint8_t>(
        clampValue(next, static_cast<int>(FireplaceConfig::kMinBrightness),
                   static_cast<int>(FireplaceConfig::kMaxBrightness)));
  }
  if (brightDownEvent != ButtonEvent::kNone) {
    const int next = static_cast<int>(targetBrightness) - FireplaceConfig::kBrightnessStep;
    targetBrightness = static_cast<uint8_t>(
        clampValue(next, static_cast<int>(FireplaceConfig::kMinBrightness),
                   static_cast<int>(FireplaceConfig::kMaxBrightness)));
  }
  if (modeEvent == ButtonEvent::kPressed) {
    cycleMode();
  }
}

static void drawSplash() {
  if (!displayReady) {
    return;
  }
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 16);
  display.print("Fireplace");
  display.setTextSize(1);
  display.setCursor(0, 44);
  display.print("Starting...");
  display.display();
}

static bool beginDisplay() {
  if (FireplaceConfig::kOledSdaPin >= 0 && FireplaceConfig::kOledSclPin >= 0) {
    Wire.begin(FireplaceConfig::kOledSdaPin, FireplaceConfig::kOledSclPin);
  } else {
    Wire.begin();
  }
  Wire.setClock(400000);

  if (display.begin(SSD1306_SWITCHCAPVCC, FireplaceConfig::kOledAddress)) {
    return true;
  }

  const uint8_t alternateAddress = FireplaceConfig::kOledAddress == 0x3C ? 0x3D : 0x3C;
  return display.begin(SSD1306_SWITCHCAPVCC, alternateAddress);
}

// ----------------- Arduino setup / loop -----------------

void setup() {
  Serial.begin(115200);
  pinMode(FireplaceConfig::kRelayPin, OUTPUT);
  digitalWrite(FireplaceConfig::kRelayPin, LOW);

  pinMode(FireplaceConfig::kButtonTempUp, INPUT_PULLUP);
  pinMode(FireplaceConfig::kButtonTempDown, INPUT_PULLUP);
  pinMode(FireplaceConfig::kButtonBrightUp, INPUT_PULLUP);
  pinMode(FireplaceConfig::kButtonBrightDown, INPUT_PULLUP);
  pinMode(FireplaceConfig::kButtonMode, INPUT_PULLUP);

  // Ensure the 1-Wire bus idles high even if the external pull-up is weak or missing.
  pinMode(FireplaceConfig::kTemperatureSensorPin, INPUT_PULLUP);

#ifdef ARDUINO_ARCH_ESP32
  analogReadResolution(12);
#endif

  temperatureSensor.begin();
  temperatureSensor.setResolution(FireplaceConfig::kSensorResolutionBits);

  displayReady = beginDisplay();
  if (!displayReady) {
    Serial.println("OLED init failed. Check SDA/SCL wiring and I2C address.");
  } else {
    drawSplash();
  }

#ifdef ARDUINO_ARCH_ESP32
  randomSeed(esp_random());
#else
  randomSeed(micros());
#endif

  FireAnimation::begin(strip, effectiveBrightness());
  fireState.baseBrightness = effectiveBrightness();
  fireState.lastFrameMs = millis();

  startWifiAndServer();
}

void loop() {
  handleButtons();
  const float measuredTemperatureC = readTemperatureCelsius();
  const float currentTemperatureC = smoothTemperature(measuredTemperatureC);
  lastTemperatureC = currentTemperatureC;
  updateHeater(currentTemperatureC);
  updateDisplay(currentTemperatureC);
  FireAnimation::update(strip, fireState, effectiveBrightness(), targetColorPercent);
  handleHttp();
}
