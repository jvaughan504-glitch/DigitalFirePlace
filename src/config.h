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

// NeoPixel configuration
static constexpr uint16_t kNeoPixelCount = 14;

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
static constexpr uint8_t kBrightnessStep = 5;
static constexpr float kMinTargetTemperature = 15.0f;
static constexpr float kMaxTargetTemperature = 30.0f;
static constexpr float kHysteresis = 0.5f; // Degrees Celsius

// Button debounce timings
static constexpr uint32_t kDebounceDelayMs = 50;
static constexpr uint32_t kHoldRepeatDelayMs = 400;
static constexpr uint32_t kHoldRepeatRateMs = 150;

// Display configuration
static constexpr uint8_t kOledResetPin = 16;
static constexpr uint8_t kOledAddress = 0x3C;

// Fire animation timings
static constexpr uint32_t kAnimationFrameMs = 35;
}

