#include "fire_animation.h"

#include "config.h"

namespace FireAnimation {
namespace {
uint8_t clampBrightness(int value) {
  if (value < FireplaceConfig::kMinBrightness) {
    return FireplaceConfig::kMinBrightness;
  }
  if (value > FireplaceConfig::kMaxBrightness) {
    return FireplaceConfig::kMaxBrightness;
  }
  return static_cast<uint8_t>(value);
}
}

void begin(Adafruit_NeoPixel &strip, uint8_t initialBrightness) {
  strip.begin();
  strip.setBrightness(clampBrightness(initialBrightness));
  strip.show();
}

void update(Adafruit_NeoPixel &strip, State &state, uint8_t targetBrightness) {
  const uint32_t now = millis();
  if (now - state.lastFrameMs < FireplaceConfig::kAnimationFrameMs) {
    return;
  }
  state.lastFrameMs = now;

  // Ease brightness toward target to avoid abrupt jumps.
  if (state.baseBrightness < targetBrightness) {
    const int next = state.baseBrightness + 2;
    state.baseBrightness = next > targetBrightness ? targetBrightness : next;
  } else if (state.baseBrightness > targetBrightness) {
    const int next = state.baseBrightness - 2;
    state.baseBrightness = next < targetBrightness ? targetBrightness : next;
  }
  strip.setBrightness(clampBrightness(state.baseBrightness));

  for (uint16_t i = 0; i < strip.numPixels(); ++i) {
    const uint8_t flicker = random(80, 140);
    int red = (state.baseBrightness * flicker) / 100;
    if (red > 255) {
      red = 255;
    }
    int green = (state.baseBrightness * flicker) / 180;
    if (green > 180) {
      green = 180;
    }
    int blue = (state.baseBrightness * flicker) / 400;
    if (blue > 100) {
      blue = 100;
    }
    if (red < 0) red = 0;
    if (green < 0) green = 0;
    if (blue < 0) blue = 0;
    strip.setPixelColor(i, strip.Color(red, green, blue));
  }
  strip.show();
}

}  // namespace FireAnimation

