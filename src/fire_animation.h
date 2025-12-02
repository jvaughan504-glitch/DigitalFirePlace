#pragma once

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

namespace FireAnimation {
struct State {
  uint8_t baseBrightness;
  uint32_t lastFrameMs;
};

void begin(Adafruit_NeoPixel &strip, uint8_t initialBrightness);
void update(Adafruit_NeoPixel &strip, State &state, uint8_t targetBrightness);
}

