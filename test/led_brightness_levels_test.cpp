#include <stdint.h>
#include <stdio.h>

#include "../include/hardware/led_brightness_levels.h"

static int failures = 0;

static void expect(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAILED: %s\n", message);
    ++failures;
  }
}

int main() {
  expect(LedBrightnessLevels::kCount == 8U, "brightness policy must expose eight levels");
  expect(LedBrightnessLevels::valueForLevel(0U) == 0U, "level zero must turn the LED off");
  expect(LedBrightnessLevels::valueForLevel(3U) == 50U, "level three must preserve the former default");
  expect(LedBrightnessLevels::valueForLevel(7U) == 255U, "level seven must reach full brightness");
  expect(LedBrightnessLevels::normalize(0U) == 0U, "off must remain off");
  expect(LedBrightnessLevels::normalize(61U) == 50U, "legacy values must choose the nearest level");
  expect(LedBrightnessLevels::normalize(250U) == 255U, "high legacy values must choose the final level");
  expect(LedBrightnessLevels::levelForValue(50U) == 3U, "raw value must resolve back to its level");

  if (failures != 0) {
    return 1;
  }

  puts("led_brightness_levels_test: passed");
  return 0;
}
