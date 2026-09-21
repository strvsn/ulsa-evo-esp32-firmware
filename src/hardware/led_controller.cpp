/**
 * @file led_controller.cpp
 * @brief RGB LED制御モジュール実装（Adafruit NeoPixel版）
 * @date 2025-12-07
 * 
 * Adafruit NeoPixelライブラリを使用してM5Stamp C3Uの内蔵LEDを制御
 * ESP32-C3のRMTペリフェラルを使用して安定動作
 */

#include "led_controller.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

// NVS設定キー
#define NVS_NAMESPACE "led_cfg"
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_WIND_REACTIVE "wind_reactive"

// モード名
const char* LedController::_modeNames[] = {
  "BLE Disconnected",
  "BLE Connected (1M)",
  "WiFi Portal",
  "WiFi Portal Ready",
  "Bootloader",
  "OTA Updating",
  "Command Mode",
  "UART Bridge",
  "I2C Measure",
  "I2C Measure Connected",
  "STM32 Update Portal",
  "STM32 Update Transfer",
  "STM32 Update Ready",
  "STM32 Update Writing",
  "STM32 Update Error",
  "ESP32 Authorization Pending",
  "ESP32 Authorization Granted",
  "ESP32 Portal Active",
  "ESP32 Update Error",
  "ESP32 Recovery",
  "STM32 Authorization Pending",
  "STM32 Authorization Granted",
  "STM32 Portal Active",
  "STM32 Recovery",
  "Initial Factory Checking",
  "Initial Setup Ready"
};

LedController::LedController() 
  : _pixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800)
  , _currentMode(MODE_BLE_DISCONNECTED)
  , _lastBlinkTime(0)
  , _blinkState(true)
  , _currentColor(0)
  , _brightness(LED_BRIGHTNESS)
  , _identifyActive(false)
  , _identifyStartedAtMs(0)
  , _identifyLastToggleMs(0)
  , _identifyLedOn(false)
  , _otaProgress(0)
  , _windReactiveEnabled(false)
  , _windReactiveTheme(WIND_REACTIVE_LED_THEME_TIDE)
  , _windReactiveWindValid(false)
  , _windReactiveWindSpeedMps(0.0f)
  , _windReactiveOverProductLimit(false)
  , _sdLoggingLastFrameMs(0) {
}

void LedController::begin() {
  _pixel.begin();
  
  // NVSから保存された輝度を読み込み（失敗時はデフォルト値を使用）
  if (!loadBrightness()) {
    _brightness = LED_BRIGHTNESS;
  }
  loadWindReactiveConfig();
  _pixel.setBrightness(_brightness);
  
  // 初期化直後は色設定せず、setMode()で即座に設定されるのを待つ
  // これにより消灯→点灯の遅延を最小化
  
  _lastBlinkTime = millis();
  _blinkState = true;
}

/**
 * @brief LEDに色を設定
 */
void LedController::setColor(uint8_t r, uint8_t g, uint8_t b) {
  _currentColor = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  _pixel.setPixelColor(0, _pixel.Color(r, g, b));
  _pixel.show();
}

/**
 * @brief LED消灯
 */
void LedController::setColorOff() {
  _currentColor = 0;
  _pixel.setPixelColor(0, 0);
  _pixel.show();
}

void LedController::renderCurrentModeColor() {
  switch (_currentMode) {
    case MODE_BLE_DISCONNECTED:
      setColor(255, 100, 150);  // ピンク
      break;
    case MODE_BLE_CONNECTED_1M:
      setColor(0, 0, 255);  // 青
      break;
    case MODE_WIFI_PORTAL:
    case MODE_WIFI_PORTAL_READY:
      setColor(255, 100, 0);  // オレンジ
      break;
    case MODE_BOOTLOADER:
      setColor(255, 0, 0);  // 赤
      break;
    case MODE_OTA_UPDATING:
    case MODE_STM32_UPDATE_TRANSFER:
      setColor(255, 100, 0);  // オレンジ: 共通transfer
      break;
    case MODE_COMMAND:
      setColor(0, 191, 165);  // 青緑: Command
      break;
    case MODE_INITIAL_READY:
      setColor(0, 255, 0);  // 緑: Initial ready
      break;
    case MODE_INITIAL_CHECKING:
      setColor(0, 255, 0);  // 緑常時点灯: factory boot-health検査中
      break;
    case MODE_BRIDGE:
      setColor(155, 70, 255);  // 紫: UART Bridge
      break;
    case MODE_I2C_MEASURE:
      setColor(0, 180, 255);  // 水色
      break;

    case MODE_I2C_MEASURE_CONNECTED:
      if (shouldRenderWindReactiveColor()) {
        renderWindReactiveColor();
      } else {
        setColor(0, 180, 255);  // 水色
      }
      break;
    case MODE_STM32_UPDATE_PORTAL:
      setColor(255, 100, 0);  // オレンジ: 旧SoftAP modeも共通portal色
      break;
    case MODE_STM32_UPDATE_READY:
      setColor(255, 255, 255);  // 白: 共通ready/確認済み
      break;
    case MODE_STM32_UPDATE_WRITING:
      setColor(255, 40, 160);  // マゼンタ: STM32書込み中
      break;
    case MODE_STM32_UPDATE_ERROR:
    case MODE_ESP32_UPDATE_ERROR:
      setColor(255, 20, 80);  // 赤紫: 共通update error/recovery required
      break;
    case MODE_ESP32_AUTH_PENDING:
    case MODE_STM32_AUTH_PENDING:
      setColor(255, 220, 0);  // 黄色: 物理認可待ち
      break;
    case MODE_ESP32_AUTH_GRANTED:
    case MODE_STM32_AUTH_GRANTED:
      setColor(255, 255, 255);  // 白: ボタンを離す合図、認可済み
      break;
    case MODE_ESP32_PORTAL_ACTIVE:
      setColor(255, 100, 0);  // オレンジ: SoftAP active
      break;
    case MODE_ESP32_RECOVERY:
    case MODE_STM32_RECOVERY:
      setColor(255, 180, 60);  // 淡橙: boot-held Recovery
      break;
    case MODE_STM32_PORTAL_ACTIVE:
      setColor(255, 100, 0);  // オレンジ: SoftAPへ端末がassociation済み
      break;
    default:
      setColorOff();
      break;
  }
}

/**
 * @brief モード設定
 */
void LedController::setMode(LedMode mode) {
  if (_currentMode == mode) {
    return;
  }

  _currentMode = mode;
  _lastBlinkTime = millis();
  _blinkState = true;
  if (mode == MODE_OTA_UPDATING || mode == MODE_STM32_UPDATE_TRANSFER) {
    _otaProgress = 0;
  }

  // 即座に初期色を表示
  if (!_identifyActive && !_sdLoggingEffect.isActive()) {
    renderCurrentModeColor();
  }
}

void LedController::blinkBootloaderComplete(uint8_t count) {
  if (_sdLoggingEffect.isActive()) {
    return;
  }

  for (uint8_t i = 0; i < count; i++) {
    setColorOff();
    delay(120);
    setColor(255, 0, 0);
    delay(120);
  }
}

void LedController::startIdentifyBlink() {
  if (_sdLoggingEffect.isActive()) {
    return;
  }

  _identifyActive = true;
  _identifyStartedAtMs = millis();
  _identifyLastToggleMs = _identifyStartedAtMs;
  _identifyLedOn = true;
  setColor(255, 176, 0);  // アンバー
}

void LedController::stopIdentifyBlink() {
  if (!_identifyActive) return;
  _identifyActive = false;
  _lastBlinkTime = millis();
  _blinkState = true;
  renderCurrentModeColor();
}

bool LedController::isIdentifyBlinkActive() const {
  return _identifyActive;
}

void LedController::setOtaProgress(uint8_t progress) {
  _otaProgress = progress > 100 ? 100 : progress;
}

uint32_t LedController::getOtaBlinkIntervalMs() const {
  if (_otaProgress >= 75) {
    return OTA_BLINK_FAST_MS;
  }
  if (_otaProgress >= 25) {
    return OTA_BLINK_MED_MS;
  }
  return OTA_BLINK_SLOW_MS;
}

void LedController::serviceOtaIndicator() {
  if (_identifyActive || _sdLoggingEffect.isActive() ||
      (_currentMode != MODE_OTA_UPDATING &&
       _currentMode != MODE_STM32_UPDATE_TRANSFER)) {
    return;
  }

  const uint32_t now = millis();
  if (_otaProgress >= 100) {
    if (_currentColor != 0xFF6400) {
      setColor(255, 100, 0);
    }
    return;
  }

  if (now - _lastBlinkTime < getOtaBlinkIntervalMs()) {
    return;
  }

  _lastBlinkTime = now;
  _blinkState = !_blinkState;
  if (_blinkState) {
    setColor(255, 100, 0);
  } else {
    setColorOff();
  }
}

void LedController::updateSdLoggingState(bool loggingActive,
                                         bool userStopConfirmed) {
  const uint32_t now = millis();
  if (!_sdLoggingEffect.setLoggingState(loggingActive,
                                        userStopConfirmed,
                                        now)) {
    return;
  }

  if (loggingActive) {
    // The logging indicator has higher priority than a pending identify burst.
    _identifyActive = false;
  }

  _sdLoggingLastFrameMs = now;
  if (!serviceSdLoggingIndicator(now, true)) {
    _lastBlinkTime = now;
    _blinkState = true;
    renderCurrentModeColor();
  }
}

bool LedController::isSdLoggingIndicatorActive() const {
  return _sdLoggingEffect.isActive();
}

bool LedController::serviceSdLoggingIndicator(uint32_t nowMs, bool force) {
  const SdLoggingLedFrame frame = _sdLoggingEffect.frame(nowMs);
  if (frame.completed) {
    _lastBlinkTime = nowMs;
    _blinkState = true;
    renderCurrentModeColor();
    return false;
  }
  if (!frame.active) {
    return false;
  }
  if (!force && nowMs - _sdLoggingLastFrameMs < SD_LOG_LED_FRAME_MS) {
    return true;
  }

  _sdLoggingLastFrameMs = nowMs;
  if (frame.ledOn) {
    const uint32_t desiredColor = static_cast<uint32_t>(frame.red) << 16U;
    if (_currentColor != desiredColor) {
      setColor(frame.red, 0, 0);
    }
  } else if (_currentColor != 0U) {
    setColorOff();
  }
  return true;
}

/**
 * @brief LED更新（毎ループ呼び出し）
 */
void LedController::update() {
  uint32_t now = millis();

  if (serviceSdLoggingIndicator(now)) {
    return;
  }

  if (_identifyActive) {
    if (now - _identifyStartedAtMs >= IDENTIFY_DURATION_MS) {
      _identifyActive = false;
      _lastBlinkTime = now;
      _blinkState = true;
      renderCurrentModeColor();
      return;
    }
    if (now - _identifyLastToggleMs >= IDENTIFY_BLINK_MS) {
      _identifyLastToggleMs = now;
      _identifyLedOn = !_identifyLedOn;
      if (_identifyLedOn) {
        setColor(255, 176, 0);
      } else {
        setColorOff();
      }
    }
    return;
  }
  
  switch (_currentMode) {
    case MODE_BLE_DISCONNECTED:
      if (now - _lastBlinkTime >= BLINK_INTERVAL_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) {
          setColor(255, 100, 150);
        } else {
          setColorOff();
        }
      }
      break;
      
    case MODE_WIFI_PORTAL:
      if (now - _lastBlinkTime >= BLINK_INTERVAL_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) {
          setColor(255, 100, 0);
        } else {
          setColorOff();
        }
      }
      break;

    case MODE_ESP32_AUTH_PENDING:
    case MODE_STM32_AUTH_PENDING:
      if (now - _lastBlinkTime >= BLINK_INTERVAL_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) setColor(255, 220, 0); else setColorOff();
      }
      break;

    case MODE_ESP32_PORTAL_ACTIVE:
    case MODE_STM32_PORTAL_ACTIVE:
    case MODE_STM32_UPDATE_PORTAL:
      if (now - _lastBlinkTime >= UPDATE_PORTAL_ACTIVE_BLINK_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) {
          setColor(255, 100, 0);
        } else {
          setColorOff();
        }
      }
      break;

    case MODE_ESP32_RECOVERY:
    case MODE_STM32_RECOVERY:
      if (now - _lastBlinkTime >= UPDATE_RECOVERY_BLINK_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) {
          setColor(255, 180, 60);
        } else {
          setColorOff();
        }
      }
      break;

    case MODE_ESP32_UPDATE_ERROR:
    case MODE_STM32_UPDATE_ERROR:
      if (now - _lastBlinkTime >= UPDATE_ERROR_BLINK_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) setColor(255, 20, 80); else setColorOff();
      }
      break;
      
    case MODE_OTA_UPDATING:
    case MODE_STM32_UPDATE_TRANSFER:
      serviceOtaIndicator();
      break;
      
    case MODE_COMMAND:
      if (now - _lastBlinkTime >= BLINK_INTERVAL_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) {
          setColor(0, 191, 165);  // 青緑: Command
        } else {
          setColorOff();
        }
      }
      break;

    case MODE_INITIAL_READY:
      if (now - _lastBlinkTime >= BLINK_INTERVAL_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) {
          setColor(0, 255, 0);  // 緑: Initial ready
        } else {
          setColorOff();
        }
      }
      break;
      
    case MODE_BRIDGE:
      if (now - _lastBlinkTime >= BLINK_INTERVAL_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) {
          setColor(155, 70, 255);  // 紫: UART Bridge
        } else {
          setColorOff();
        }
      }
      break;

    case MODE_I2C_MEASURE:
      if (now - _lastBlinkTime >= BLINK_INTERVAL_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) {
          setColor(0, 180, 255);  // 水色
        } else {
          setColorOff();
        }
      }
      break;

    case MODE_I2C_MEASURE_CONNECTED:
      if (shouldBlinkWindReactiveOverLimit() &&
          now - _lastBlinkTime >= WIND_REACTIVE_OVER_LIMIT_BLINK_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) {
          renderWindReactiveColor();
        } else {
          setColorOff();
        }
      }
      break;

    case MODE_STM32_UPDATE_WRITING:
      if (now - _lastBlinkTime >= STM32_UPDATE_WRITE_BLINK_MS) {
        _lastBlinkTime = now;
        _blinkState = !_blinkState;
        if (_blinkState) {
          setColor(255, 40, 160);
        } else {
          setColorOff();
        }
      }
      break;

    // 常時点灯モード
    case MODE_BLE_CONNECTED_1M:
    case MODE_WIFI_PORTAL_READY:
    case MODE_BOOTLOADER:
    case MODE_STM32_UPDATE_READY:
    case MODE_ESP32_AUTH_GRANTED:
    case MODE_STM32_AUTH_GRANTED:
    case MODE_INITIAL_CHECKING:
    default:
      break;
  }
}

LedMode LedController::getCurrentMode() const {
  return _currentMode;
}

const char* LedController::getCurrentModeName() const {
  if (_currentMode < MODE_COUNT) {
    return _modeNames[_currentMode];
  }
  return "Unknown";
}

uint32_t LedController::getCurrentColor() const {
  return _currentColor;
}

/**
 * @brief LED輝度を8段階へ正規化して設定
 * @param brightness BLE/CLI/NVSから受け取った0-255値
 */
void LedController::setBrightness(uint8_t brightness) {
  _brightness = LedBrightnessLevels::normalize(brightness);
  _pixel.setBrightness(_brightness);

  // Adafruit_NeoPixel::setBrightness() は内部のピクセルバッファを
  // 直接スケールする。特に 0 にすると色データが失われるため、保持して
  // いる未スケールのモード色を毎回描き直して、消灯後も復帰できるようにする。
  _pixel.setPixelColor(0, _currentColor);
  _pixel.show();
}

/**
 * @brief 現在の輝度を取得
 * @return 輝度値（0-255）
 */
uint8_t LedController::getBrightness() const {
  return _brightness;
}

uint8_t LedController::getBrightnessLevel() const {
  return LedBrightnessLevels::levelForValue(_brightness);
}

/**
 * @brief NVSから輝度設定を読み込み
 * @return true=成功、false=失敗（保存データなし）
 */
bool LedController::loadBrightness() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {  // read-only
    return false;
  }
  
  // 任意の旧NVS値も最寄りの8段階へ一度だけ移行する。
  const uint8_t storedBrightness = prefs.getUChar(NVS_KEY_BRIGHTNESS, LED_BRIGHTNESS);
  prefs.end();

  _brightness = LedBrightnessLevels::normalize(storedBrightness);
  if (storedBrightness != _brightness) {
    saveBrightness();
  }
  
  return true;
}

/**
 * @brief 現在の輝度設定をNVSに保存
 * @return true=成功、false=失敗
 */
bool LedController::saveBrightness() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {  // read-write
    return false;
  }
  
  prefs.putUChar(NVS_KEY_BRIGHTNESS, LedBrightnessLevels::normalize(_brightness));
  prefs.end();
  
  return true;
}

bool LedController::loadWindReactiveConfig() {
  _windReactiveEnabled = false;
  _windReactiveTheme = WIND_REACTIVE_LED_THEME_TIDE;

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    return false;
  }

  if (!prefs.isKey(NVS_KEY_WIND_REACTIVE)) {
    prefs.end();
    return true;
  }

  const uint8_t stored = prefs.getUChar(NVS_KEY_WIND_REACTIVE, 0);
  prefs.end();
  const uint8_t theme = (stored >> 1U) & 0x07U;
  if ((stored & 0xF0U) != 0U || !WindReactiveLedPalette::isValidTheme(theme)) {
    return false;
  }

  _windReactiveEnabled = (stored & 0x01U) != 0U;
  _windReactiveTheme = theme;
  return true;
}

bool LedController::setWindReactiveConfig(bool enabled, uint8_t theme) {
  if (!WindReactiveLedPalette::isValidTheme(theme)) {
    return false;
  }

  const uint8_t persisted = static_cast<uint8_t>((enabled ? 0x01U : 0x00U) | (theme << 1U));
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    return false;
  }
  const size_t written = prefs.putUChar(NVS_KEY_WIND_REACTIVE, persisted);
  prefs.end();
  if (written != 1U) {
    return false;
  }

  _windReactiveEnabled = enabled;
  _windReactiveTheme = theme;
  if (!_identifyActive && !_sdLoggingEffect.isActive()) {
    renderCurrentModeColor();
  }
  return true;
}

bool LedController::isWindReactiveEnabled() const {
  return _windReactiveEnabled;
}

uint8_t LedController::getWindReactiveTheme() const {
  return _windReactiveTheme;
}

bool LedController::shouldRenderWindReactiveColor() const {
  return _windReactiveEnabled &&
    _windReactiveWindValid &&
    !_sdLoggingEffect.isActive() &&
    _currentMode == MODE_I2C_MEASURE_CONNECTED;
}

bool LedController::isWindReactiveActive() const {
  return shouldRenderWindReactiveColor();
}

bool LedController::shouldBlinkWindReactiveOverLimit() const {
  return shouldRenderWindReactiveColor() && _windReactiveOverProductLimit;
}

void LedController::renderWindReactiveColor() {
  const WindReactiveLedColor color = WindReactiveLedPalette::colorForSpeed(
    _windReactiveTheme,
    _windReactiveWindSpeedMps);
  setColor(color.red, color.green, color.blue);
}

void LedController::updateWindReactiveWindSpeed(
    float windSpeedMps,
    bool valid,
    bool sourceOverProductLimit) {
  const bool nextValid = valid && isfinite(windSpeedMps) && windSpeedMps >= 0.0f;
  const bool nextOverProductLimit = nextValid &&
    (sourceOverProductLimit || WindReactiveLedPalette::isOverProductWindSpeedLimit(windSpeedMps));
  const bool overProductLimitChanged =
    _windReactiveOverProductLimit != nextOverProductLimit;
  const bool valueChanged = !nextValid ||
    !_windReactiveWindValid ||
    fabsf(_windReactiveWindSpeedMps - windSpeedMps) >= 0.01f ||
    overProductLimitChanged;
  _windReactiveWindValid = nextValid;
  _windReactiveOverProductLimit = nextOverProductLimit;
  if (nextValid) {
    _windReactiveWindSpeedMps = windSpeedMps;
  }

  if (!_identifyActive && !_sdLoggingEffect.isActive() && valueChanged &&
      _currentMode == MODE_I2C_MEASURE_CONNECTED) {
    if (overProductLimitChanged) {
      _lastBlinkTime = millis();
      _blinkState = true;
      renderCurrentModeColor();
    } else if (!shouldBlinkWindReactiveOverLimit() || _blinkState) {
      // Keep the OFF phase intact while new 10 Hz sensor samples arrive.
      renderCurrentModeColor();
    }
  }
}
