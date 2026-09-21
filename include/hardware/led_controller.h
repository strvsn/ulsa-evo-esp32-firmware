/**
 * @file led_controller.h
 * @brief RGB LED制御モジュール（Adafruit NeoPixel版）
 * @date 2025-12-07
 * 
 * Adafruit NeoPixelライブラリを使用してM5Stamp C3Uの内蔵LEDを制御
 * ESP32-C3のRMTペリフェラルを使用して安定動作
 */

#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Adafruit_NeoPixel.h>
#include "led_brightness_levels.h"
#include "pin_config.h"
#include "sd_logging_led_effect.h"
#include "wind_reactive_led_palette.h"

// LED設定
#define LED_PIN LED_NEOPIXEL_PIN  // GPIO2
#define NUM_LEDS 1
#define LED_BRIGHTNESS 50  // Eight-step default: level 3 (0 is off)

// 点滅設定
#define BLINK_INTERVAL_MS 500   // 1秒点滅 = 500ms ON/OFF
#define FAST_BLINK_MS     100   // 高速点滅
#define WRITE_BLINK_MS    200   // 書込み中点滅
#define IDENTIFY_BLINK_MS  300   // BLE識別点滅（300ms ON / 300ms OFF）
#define IDENTIFY_DURATION_MS 30000U // BLE識別表示は接続状態に関係なく30秒間
#define OTA_BLINK_SLOW_MS  260   // OTA開始直後
#define OTA_BLINK_MED_MS   160   // OTA中盤
#define OTA_BLINK_FAST_MS   90   // OTA終盤
#define STM32_UPDATE_WRITE_BLINK_MS 90 // STM32 update write/verify
#define UPDATE_PORTAL_ACTIVE_BLINK_MS 250 // Prepared SoftAP is active
#define UPDATE_RECOVERY_BLINK_MS 900 // Boot-held recovery portal
#define UPDATE_ERROR_BLINK_MS 500 // Common update/session error
#define WIND_REACTIVE_OVER_LIMIT_BLINK_MS 120 // Product wind-speed limit warning
#define SD_LOG_LED_FRAME_MS 20 // Smooth heartbeat and responsive stop flashes

/**
 * @brief LEDモード
 */
enum LedMode {
  MODE_BLE_DISCONNECTED = 0, // BLE未接続（ピンク点滅）
  MODE_BLE_CONNECTED_1M,     // BLE接続中（1M PHY: 青色常時点灯）
  MODE_WIFI_PORTAL,          // WiFi設定ポータル起動中/エラー（オレンジ点滅）
  MODE_WIFI_PORTAL_READY,    // WiFi設定ポータル起動完了（オレンジ常時点灯）
  MODE_BOOTLOADER,           // STM32ブートローダーモード（赤色常時点灯）
  MODE_OTA_UPDATING,         // OTAアップデート中（オレンジ進捗点滅）
  MODE_COMMAND,              // コマンドモード（青緑色点滅）
  MODE_BRIDGE,               // UARTブリッジモード（紫色点滅）
  MODE_I2C_MEASURE,          // I2C計測モード（水色点滅）
  MODE_I2C_MEASURE_CONNECTED,// I2C計測モード BLE接続中（水色常時点灯）
  MODE_STM32_UPDATE_PORTAL,  // 旧STM32 SoftAP表示（オレンジ中速点滅）
  MODE_STM32_UPDATE_TRANSFER,// legacy name; common update transfer (orange progress)
  MODE_STM32_UPDATE_READY,   // legacy name; common update ready (white steady)
  MODE_STM32_UPDATE_WRITING, // common update writing (magenta blink)
  MODE_STM32_UPDATE_ERROR,   // common update error/recovery (red-purple blink)
  MODE_ESP32_AUTH_PENDING,   // common update authorization pending (yellow blink)
  MODE_ESP32_AUTH_GRANTED,   // common confirmed/ready (white steady)
  MODE_ESP32_PORTAL_ACTIVE,  // common update portal (orange blink)
  MODE_ESP32_UPDATE_ERROR,   // legacy name; common update error/recovery
  MODE_ESP32_RECOVERY,       // common recovery (pale orange blink)
  MODE_STM32_AUTH_PENDING,   // legacy name; common authorization pending (yellow blink)
  MODE_STM32_AUTH_GRANTED,   // legacy name; common confirmed/ready
  MODE_STM32_PORTAL_ACTIVE,  // legacy name; common update portal
  MODE_STM32_RECOVERY,       // legacy name; Recovery portal (pale orange blink)
  MODE_INITIAL_CHECKING,     // Initial factory boot-health検査中（緑常時点灯）
  MODE_INITIAL_READY,        // Initial profileセットアップ待ち（緑点滅）
  MODE_COUNT                 // モード数
};

// Compatibility alias used only by the frozen STM32 bootloader-return source.
// It does not add a distinct LED mode or Coded PHY behavior.
static constexpr LedMode MODE_BLE_CONNECTED_CODED = MODE_BLE_CONNECTED_1M;

/**
 * @brief LED制御クラス（Adafruit NeoPixel版）
 */
class LedController {
public:
  LedController();
  
  void begin();
  void update();              // 毎ループ呼び出し: 点滅処理
  void setMode(LedMode mode); // モード設定
  void blinkBootloaderComplete(uint8_t count = 3);
  void startIdentifyBlink();
  void stopIdentifyBlink();
  bool isIdentifyBlinkActive() const;
  void setOtaProgress(uint8_t progress);
  void serviceOtaIndicator();

  // SD logging is a temporary display overlay. A confirmed user stop emits
  // three quick red flashes before the latest base mode is restored.
  void updateSdLoggingState(bool loggingActive, bool userStopConfirmed);
  bool isSdLoggingIndicatorActive() const;
  
  LedMode getCurrentMode() const;
  const char* getCurrentModeName() const;
  uint32_t getCurrentColor() const;
  
  // 輝度調整（8段階の実効値へ正規化）
  void setBrightness(uint8_t brightness);
  uint8_t getBrightness() const;
  uint8_t getBrightnessLevel() const;
  bool loadBrightness();   // NVSから輝度を読み込み
  bool saveBrightness();   // NVSに輝度を保存

  // I2C計測中かつBLE接続中の水色定常表示だけを風速連動色に置換する。
  // 設定はNVS保存に成功した場合のみメモリへ反映する。
  bool setWindReactiveConfig(bool enabled, uint8_t theme);
  bool isWindReactiveEnabled() const;
  uint8_t getWindReactiveTheme() const;
  bool isWindReactiveActive() const;
  // sourceOverProductLimit is reserved for the future STM32/I2C over-limit
  // status bit. Until that bit exists, locally received values above 25 m/s
  // still trigger the warning.
  void updateWindReactiveWindSpeed(
    float windSpeedMps,
    bool valid,
    bool sourceOverProductLimit = false);

private:
  Adafruit_NeoPixel _pixel;
  LedMode _currentMode;
  uint32_t _lastBlinkTime;
  bool _blinkState;
  uint8_t _brightness;       // 現在の輝度（0-255）
  uint32_t _currentColor;
  bool _identifyActive;
  uint32_t _identifyStartedAtMs;
  uint32_t _identifyLastToggleMs;
  bool _identifyLedOn;
  uint8_t _otaProgress;
  bool _windReactiveEnabled;
  uint8_t _windReactiveTheme;
  bool _windReactiveWindValid;
  float _windReactiveWindSpeedMps;
  bool _windReactiveOverProductLimit;
  SdLoggingLedEffect _sdLoggingEffect;
  uint32_t _sdLoggingLastFrameMs;
  
  void setColor(uint8_t r, uint8_t g, uint8_t b);  // LEDに色を設定
  void setColorOff();                              // LED消灯
  void renderCurrentModeColor();                   // 現在モードの初期色を再表示
  bool loadWindReactiveConfig();
  bool shouldRenderWindReactiveColor() const;
  bool shouldBlinkWindReactiveOverLimit() const;
  void renderWindReactiveColor();
  bool serviceSdLoggingIndicator(uint32_t nowMs, bool force = false);
  uint32_t getOtaBlinkIntervalMs() const;
  
  static const char* _modeNames[];
};

#endif // LED_CONTROLLER_H
