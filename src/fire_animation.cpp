#include "fire_animation.h"

#include "fireplace_config.h"

namespace FireAnimation {
namespace {
uint8_t clampBrightness(int value) {
  if (value <= 0) {
    return 0;
  }
  if (value < FireplaceConfig::kMinBrightness) {
    return FireplaceConfig::kMinBrightness;
  }
  if (value > FireplaceConfig::kMaxBrightness) {
    return FireplaceConfig::kMaxBrightness;
  }
  return static_cast<uint8_t>(value);
}

struct ColorWeights {
  float red;
  float green;
  float blue;
};

ColorWeights colorFromPercent(uint8_t colorPercent) {
  const float t = static_cast<float>(colorPercent) / 100.0f;
  const ColorWeights warm{1.0f, 0.7f, 0.25f};
  const ColorWeights cool{0.5f, 0.7f, 1.0f};

  return {warm.red + (cool.red - warm.red) * t,
          warm.green + (cool.green - warm.green) * t,
          warm.blue + (cool.blue - warm.blue) * t};
}
}

void begin(Adafruit_NeoPixel &strip, uint8_t initialBrightness) {
  strip.begin();
  strip.setBrightness(clampBrightness(initialBrightness));
  strip.show();
}

void update(Adafruit_NeoPixel &strip, State &state, uint8_t targetBrightness,
            uint8_t colorPercent) {
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

  const ColorWeights weights = colorFromPercent(colorPercent);

  for (uint16_t i = 0; i < strip.numPixels(); ++i) {
    const uint8_t flicker = random(80, 140);
    const int brightnessWithFlicker = (state.baseBrightness * flicker) / 100;
    int red = static_cast<int>(brightnessWithFlicker * weights.red);
    int green = static_cast<int>(brightnessWithFlicker * weights.green);
    int blue = static_cast<int>(brightnessWithFlicker * weights.blue);
    if (red > 255) red = 255;
    if (green > 255) green = 255;
    if (blue > 255) blue = 255;
    if (red < 0) red = 0;
    if (green < 0) green = 0;
    if (blue < 0) blue = 0;
    strip.setPixelColor(i, strip.Color(red, green, blue));
  }
  strip.show();
}

}  // namespace FireAnimation

