#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SSD1306.h>
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
static bool displayReady = false;
static float targetTemperatureC = 21.0f;
static uint8_t targetBrightness = 160;
static FireAnimation::State fireState{targetBrightness, 0};
enum class OperatingMode { kFireOnly, kFireAndHeat, kHeatOnly };

static bool heaterActive = false;
static OperatingMode operatingMode = OperatingMode::kFireAndHeat;
static float lastTemperatureC = NAN;

#ifdef ARDUINO_ARCH_ESP32
static WebServer server(80);
#endif

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

static float readThermistorCelsius() {
  const int raw = analogRead(FireplaceConfig::kThermistorPin);
  if (raw <= 0) {
    return -40.0f;
  }

  float resistance = FireplaceConfig::kSeriesResistor *
                     ((FireplaceConfig::kAdcMax / static_cast<float>(raw)) - 1.0f);
  if (resistance <= 0.0f) {
    resistance = FireplaceConfig::kSeriesResistor;
  }

  float steinhart;
  steinhart = resistance / FireplaceConfig::kNominalResistance;
  steinhart = log(steinhart);
  steinhart /= FireplaceConfig::kBCoefficient;
  steinhart += 1.0f / (FireplaceConfig::kNominalTemperatureC + 273.15f);
  steinhart = 1.0f / steinhart;
  steinhart -= 273.15f;
  return steinhart;
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
  //display.write(247);  // Degree symbol
  display.println("C");

  display.setCursor(0, 25);
  display.println("Set:");
  display.print(targetTemperatureC, 1);
  //display.write(247);
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

#ifdef ARDUINO_ARCH_ESP32
static String htmlHeader() {
  return R"(<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Fireplace</title><style>
body{font-family:Arial,Helvetica,sans-serif;background:#111;color:#eee;margin:0;padding:1rem;}
header{font-size:1.4rem;font-weight:700;margin-bottom:.5rem;}
section{margin-bottom:1rem;}
label{display:block;margin:.4rem 0 .2rem;}
input[type=range]{width:100%;}
input,button{padding:.4rem;border-radius:4px;border:1px solid #555;background:#222;color:#eee;}
button{cursor:pointer;}
small{color:#aaa;display:block;margin-top:.4rem;}
.mode-buttons{display:flex;gap:.4rem;flex-wrap:wrap;margin-top:.2rem;}
.mode-buttons .active{background:#0a84ff;border-color:#0a84ff;color:#fff;}
</style></head><body><header>Digital Fireplace</header><section>)";
}

static String htmlFooter() { return "</section></body></html>"; }

static String renderPage() {
  String page = htmlHeader();
  page += "<div>Room: ";
  if (isnan(lastTemperatureC)) {
    page += "--";
  } else {
    page += String(lastTemperatureC, 1);
    page += " &deg;C";
  }
  page += "<br/>Set: ";
  page += String(targetTemperatureC, 1);
  page += " &deg;C<br/>Mode: ";
  page += modeLabel();
  page += "<br/>Brightness: ";
  page += effectiveBrightness();
  page += heaterActive ? "<br/><strong>HEAT ON</strong>" : "<br/>HEAT OFF";
  page += "</div>";

  page += R"(<form action='/apply' method='get'><section><label for='temp'>Target Temp (&deg;C)</label><input type='range' step='0.5' min='15' max='30' id='temp' name='temp' value='";
  page += String(targetTemperatureC, 1);
  page += R"('><small>Current target: )";
  page += String(targetTemperatureC, 1);
  page += R"(</small></section><section><label for='bright'>Brightness (0-255)</label><input type='range' min='0' max='255' id='bright' name='bright' value='";
  page += targetBrightness;
  page += R"('><small>Current: )";
  page += effectiveBrightness();
  page += "</small></section><section><label>Mode</label><div class='mode-buttons'>";
  page += "<button type='submit' name='mode' value='0";
  page += operatingMode == OperatingMode::kFireOnly ? "' class='active'" : "'";
  page += ">Fire only</button>";
  page += "<button type='submit' name='mode' value='1";
  page += operatingMode == OperatingMode::kFireAndHeat ? "' class='active'" : "'";
  page += ">Fire + Heat</button>";
  page += "<button type='submit' name='mode' value='2";
  page += operatingMode == OperatingMode::kHeatOnly ? "' class='active'" : "'";
  page += ">Heat only</button>";
  page += R"(</div></section><button type='submit'>Apply</button></form>)";
  page += htmlFooter();
  return page;
}

static void handleRoot() { server.send(200, "text/html", renderPage()); }

static void handleApply() {
  if (server.hasArg("temp")) {
    const float requested = server.arg("temp").toFloat();
    targetTemperatureC = clampValue(requested, FireplaceConfig::kMinTargetTemperature,
                                    FireplaceConfig::kMaxTargetTemperature);
  }

  if (server.hasArg("bright")) {
    const int requested = server.arg("bright").toInt();
    targetBrightness = static_cast<uint8_t>(
        clampValue(requested, 0, static_cast<int>(FireplaceConfig::kMaxBrightness)));
  }

  if (server.hasArg("mode")) {
    const int requestedMode = server.arg("mode").toInt();
    switch (requestedMode) {
      case 0:
        operatingMode = OperatingMode::kFireOnly;
        break;
      case 1:
        operatingMode = OperatingMode::kFireAndHeat;
        break;
      case 2:
        operatingMode = OperatingMode::kHeatOnly;
        break;
      default:
        break;
    }
  }

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Redirecting...");
}

static void handleNotFound() { server.send(404, "text/plain", "Not found"); }

static void startWifiAndServer() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(FireplaceConfig::kWifiSsid, FireplaceConfig::kWifiPassword);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start <
                                              FireplaceConfig::kWifiConnectTimeoutMs) {
    delay(100);
  }

  server.on("/", handleRoot);
  server.on("/apply", handleApply);
  server.onNotFound(handleNotFound);
  server.begin();
}

static void handleHttp() { server.handleClient(); }
#else
static void startWifiAndServer() {}
static void handleHttp() {}
#endif

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

void setup() {
  Serial.begin(115200);
  pinMode(FireplaceConfig::kRelayPin, OUTPUT);
  digitalWrite(FireplaceConfig::kRelayPin, LOW);

  pinMode(FireplaceConfig::kButtonTempUp, INPUT_PULLUP);
  pinMode(FireplaceConfig::kButtonTempDown, INPUT_PULLUP);
  pinMode(FireplaceConfig::kButtonBrightUp, INPUT_PULLUP);
  pinMode(FireplaceConfig::kButtonBrightDown, INPUT_PULLUP);
  pinMode(FireplaceConfig::kButtonMode, INPUT_PULLUP);

#ifdef ARDUINO_ARCH_ESP32
  analogReadResolution(12);
#endif

  displayReady = beginDisplay();
  if (!displayReady) {
    Serial.println("OLED init failed. Check SDA/SCL wiring and I2C address.");
  } else {
    drawSplash();
  }

  randomSeed(analogRead(FireplaceConfig::kThermistorPin));

  FireAnimation::begin(strip, effectiveBrightness());
  fireState.baseBrightness = effectiveBrightness();
  fireState.lastFrameMs = millis();

  startWifiAndServer();
}

void loop() {
  handleButtons();
  const float currentTemperatureC = readThermistorCelsius();
  lastTemperatureC = currentTemperatureC;
  updateHeater(currentTemperatureC);
  updateDisplay(currentTemperatureC);
  FireAnimation::update(strip, fireState, effectiveBrightness());
  handleHttp();
}

