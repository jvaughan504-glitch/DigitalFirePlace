#pragma once

#include <Arduino.h>

namespace FireplaceConfig {
// Pin assignments
static constexpr uint8_t kRelayPin = 5;
static constexpr uint8_t kThermistorPin = 34;  // ADC capable pin
static constexpr uint8_t kNeoPixelPin = 18;
static constexpr uint8_t kButtonTempUp = 25;
static constexpr uint8_t kButtonTempDown = 26;
static constexpr uint8_t kButtonBrightUp = 27;
static constexpr uint8_t kButtonBrightDown = 14;
static constexpr uint8_t kButtonMode = 32;

// NeoPixel configuration
static constexpr uint16_t kNeoPixelCount = 10;

// Thermistor calibration (Steinhart-Hart parameters)
static constexpr float kSeriesResistor = 10000.0f;
static constexpr float kNominalResistance = 10000.0f; // Resistance at 25C
static constexpr float kNominalTemperatureC = 25.0f;
static constexpr float kBCoefficient = 3950.0f;

// ADC configuration
static constexpr uint16_t kAdcMax = 4095; // 12-bit ADC
static constexpr float kAdcVoltage = 3.3f;

// Control configuration
static constexpr float kTemperatureStep = 0.5f;
static constexpr uint8_t kMinBrightness = 10;
static constexpr uint8_t kMaxBrightness = 255;
static constexpr uint8_t kBrightnessStep = 15;
static constexpr float kMinTargetTemperature = 15.0f;
static constexpr float kMaxTargetTemperature = 30.0f;
static constexpr float kHysteresis = 0.5f; // Degrees Celsius

// Button debounce timings
static constexpr uint32_t kDebounceDelayMs = 50;
static constexpr uint32_t kHoldRepeatDelayMs = 400;
static constexpr uint32_t kHoldRepeatRateMs = 150;

// Display configuration (use -1 when the OLED reset line is not exposed)
static constexpr int8_t kOledResetPin = -1;
static constexpr uint8_t kOledAddress = 0x3C;

// Fire animation timings
static constexpr uint32_t kAnimationFrameMs = 35;

// Wi-Fi configuration (used only on ESP32 builds)
static constexpr char kWifiSsid[] = "BELL728";
static constexpr char kWifiPassword[] = "9134EC94D365";
static constexpr uint32_t kWifiConnectTimeoutMs = 10000;
}

