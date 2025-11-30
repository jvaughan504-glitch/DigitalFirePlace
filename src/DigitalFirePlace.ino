#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <math.h>

#include "config.h"
#include "fire_animation.h"

namespace {
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

ButtonState buttonTempUp{FireplaceConfig::kButtonTempUp, false, 0, 0, false};
ButtonState buttonTempDown{FireplaceConfig::kButtonTempDown, false, 0, 0, false};
ButtonState buttonBrightUp{FireplaceConfig::kButtonBrightUp, false, 0, 0, false};
ButtonState buttonBrightDown{FireplaceConfig::kButtonBrightDown, false, 0, 0, false};


Adafruit_SSD1306 display(128, 64, &Wire, FireplaceConfig::kOledResetPin);
Adafruit_NeoPixel strip(FireplaceConfig::kNeoPixelCount, FireplaceConfig::kNeoPixelPin,
                        NEO_GRB + NEO_KHZ800);
float targetTemperatureC = 21.0f;
uint8_t targetBrightness = 160;
FireAnimation::State fireState{targetBrightness, 0};
bool heaterActive = false;

ButtonEvent updateButton(ButtonState &button) {
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

float readThermistorCelsius() {
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

void updateDisplay(float currentTemperatureC) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print("Room:");
  display.setCursor(0, 18);
  display.print(currentTemperatureC, 1);
  display.cp437(true);
  display.write(247);  // Degree symbol
  display.print("C");

  display.setCursor(0, 40);
  display.print("Set:");
  display.print(targetTemperatureC, 1);
  display.write(247);
  display.print("C");

  display.setTextSize(1);
  display.setCursor(0, 58);
  display.print("Bright:");
  display.print(targetBrightness);

  display.setCursor(80, 58);
  display.print(heaterActive ? "HEAT ON" : "HEAT OFF");

  display.display();
}

void updateHeater(float currentTemperatureC) {
  const float lowerThreshold = targetTemperatureC - (FireplaceConfig::kHysteresis / 2.0f);
  const float upperThreshold = targetTemperatureC + (FireplaceConfig::kHysteresis / 2.0f);

  if (!heaterActive && currentTemperatureC <= lowerThreshold) {
    heaterActive = true;
  } else if (heaterActive && currentTemperatureC >= upperThreshold) {
    heaterActive = false;
  }
  digitalWrite(FireplaceConfig::kRelayPin, heaterActive ? HIGH : LOW);
}

void handleButtons() {
  const ButtonEvent tempUpEvent = updateButton(buttonTempUp);
  const ButtonEvent tempDownEvent = updateButton(buttonTempDown);
  const ButtonEvent brightUpEvent = updateButton(buttonBrightUp);
  const ButtonEvent brightDownEvent = updateButton(buttonBrightDown);

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
}

void drawSplash() {
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

}  // namespace

void setup() {
  pinMode(FireplaceConfig::kRelayPin, OUTPUT);
  digitalWrite(FireplaceConfig::kRelayPin, LOW);

  pinMode(FireplaceConfig::kButtonTempUp, INPUT_PULLUP);
  pinMode(FireplaceConfig::kButtonTempDown, INPUT_PULLUP);
  pinMode(FireplaceConfig::kButtonBrightUp, INPUT_PULLUP);
  pinMode(FireplaceConfig::kButtonBrightDown, INPUT_PULLUP);

#ifdef ARDUINO_ARCH_ESP32
  analogReadResolution(12);
#endif

  Wire.begin();
  display.begin(SSD1306_SWITCHCAPVCC, FireplaceConfig::kOledAddress);
  drawSplash();

  randomSeed(analogRead(FireplaceConfig::kThermistorPin));

  FireAnimation::begin(strip, targetBrightness);
  fireState.baseBrightness = targetBrightness;
  fireState.lastFrameMs = millis();
}

void loop() {
  handleButtons();
  const float currentTemperatureC = readThermistorCelsius();
  updateHeater(currentTemperatureC);
  updateDisplay(currentTemperatureC);
  FireAnimation::update(strip, fireState, targetBrightness);
}

