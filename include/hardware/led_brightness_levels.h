/**
 * @file led_brightness_levels.h
 * @brief Shared eight-step LED brightness policy.
 */

#ifndef LED_BRIGHTNESS_LEVELS_H
#define LED_BRIGHTNESS_LEVELS_H

#include <stdint.h>

class LedBrightnessLevels {
public:
  static const uint8_t kCount = 8U;

  // Step zero is an explicit off state. The former default brightness of 50
  // remains at step three.
  static uint8_t valueForLevel(uint8_t level) {
    if (level >= kCount) {
      level = kCount - 1U;
    }
    return values()[level];
  }

  static uint8_t normalize(uint8_t brightness) {
    uint8_t closest = values()[0];
    uint16_t closestDistance = distance(brightness, closest);
    for (uint8_t index = 1U; index < kCount; ++index) {
      const uint8_t candidate = values()[index];
      const uint16_t candidateDistance = distance(brightness, candidate);
      if (candidateDistance < closestDistance) {
        closest = candidate;
        closestDistance = candidateDistance;
      }
    }
    return closest;
  }

  static uint8_t levelForValue(uint8_t brightness) {
    const uint8_t normalized = normalize(brightness);
    for (uint8_t index = 0U; index < kCount; ++index) {
      if (values()[index] == normalized) {
        return index;
      }
    }
    return 0U;
  }

private:
  static const uint8_t* values() {
    static const uint8_t kValues[kCount] = {
      0U, 16U, 32U, 50U, 75U, 110U, 170U, 255U,
    };
    return kValues;
  }

  static uint16_t distance(uint8_t left, uint8_t right) {
    return left >= right
      ? static_cast<uint16_t>(left - right)
      : static_cast<uint16_t>(right - left);
  }
};

#endif  // LED_BRIGHTNESS_LEVELS_H
