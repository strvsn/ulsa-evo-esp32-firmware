/**
 * @file wind_reactive_led_palette.h
 * @brief Wind-speed ordered RGB palettes for the connected I2C measurement LED.
 */

#ifndef WIND_REACTIVE_LED_PALETTE_H
#define WIND_REACTIVE_LED_PALETTE_H

#include <stdint.h>

enum WindReactiveLedTheme : uint8_t {
  WIND_REACTIVE_LED_THEME_TIDE = 0,
  WIND_REACTIVE_LED_THEME_CIVIDIS = 1,
  WIND_REACTIVE_LED_THEME_VIRIDIS = 2,
  WIND_REACTIVE_LED_THEME_EMBER = 3,
  WIND_REACTIVE_LED_THEME_AURORA = 4,
  WIND_REACTIVE_LED_THEME_COUNT = 5,
};

struct WindReactiveLedColor {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

class WindReactiveLedPalette {
public:
  static float maxWindSpeedMps() {
    return 25.0f;
  }

  // The product-scale maximum, not a sensor validity limit. Values above this
  // retain the high-end palette color and are rendered as an LED warning.
  static bool isOverProductWindSpeedLimit(float windSpeedMps) {
    return windSpeedMps > maxWindSpeedMps();
  }

  static bool isValidTheme(uint8_t theme) {
    return theme < WIND_REACTIVE_LED_THEME_COUNT;
  }

  static WindReactiveLedColor colorForSpeed(uint8_t theme, float windSpeedMps) {
    const WindReactiveLedColor* palette = colorsForTheme(theme);
    float normalized = windSpeedMps / maxWindSpeedMps();
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;

    if (normalized <= 0.5f) {
      return interpolate(palette[0], palette[1], normalized * 2.0f);
    }
    return interpolate(palette[1], palette[2], (normalized - 0.5f) * 2.0f);
  }

private:
  static WindReactiveLedColor interpolate(
      const WindReactiveLedColor& from,
      const WindReactiveLedColor& to,
      float amount) {
    return {
      static_cast<uint8_t>(from.red + (to.red - from.red) * amount),
      static_cast<uint8_t>(from.green + (to.green - from.green) * amount),
      static_cast<uint8_t>(from.blue + (to.blue - from.blue) * amount),
    };
  }

  static const WindReactiveLedColor* colorsForTheme(uint8_t theme) {
    static const WindReactiveLedColor kThemes[WIND_REACTIVE_LED_THEME_COUNT][3] = {
      {{0, 100, 210}, {0, 215, 202}, {255, 205, 77}},   // Tide
      {{0, 32, 76}, {87, 102, 111}, {250, 232, 82}},     // Cividis
      {{68, 1, 84}, {33, 145, 140}, {253, 231, 37}},     // Viridis
      {{30, 58, 138}, {192, 57, 143}, {255, 185, 82}},   // Ember
      {{0, 80, 137}, {0, 184, 169}, {218, 255, 98}},     // Aurora
    };
    return kThemes[isValidTheme(theme) ? theme : WIND_REACTIVE_LED_THEME_TIDE];
  }
};

#endif  // WIND_REACTIVE_LED_PALETTE_H
