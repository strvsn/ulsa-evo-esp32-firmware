/**
 * @file ble_led_brightness.cpp
 * @brief BLE経由のLED輝度設定
 */

#include "ble_manager.h"
#include "hardware/led_controller.h"

extern LedController ledCtrl;

void BleManager::clearLedBrightnessRequest() {
  portENTER_CRITICAL(&_ledBrightnessMux);
  _ledBrightnessRequestPending = false;
  _ledBrightnessRequestValue = 0;
  portEXIT_CRITICAL(&_ledBrightnessMux);
}

void BleManager::publishLedBrightnessStatus(bool notify) {
  if (!_pLedBrightnessChar) {
    return;
  }

  const uint8_t brightness = ledCtrl.getBrightness();
  _pLedBrightnessChar->setValue(&brightness, 1);
  if (notify && _connectionCount > 0) {
    _pLedBrightnessChar->notify();
  }
}

void BleManager::onLedBrightnessRead() {
  publishLedBrightnessStatus(false);
}

void BleManager::onLedBrightnessWrite(const uint8_t* data, size_t length) {
  if (!_running || !_pLedBrightnessChar) {
    return;
  }

  if (data == nullptr || length != 1) {
    publishLedBrightnessStatus(true);
    return;
  }

  portENTER_CRITICAL(&_ledBrightnessMux);
  // スライダー操作では短時間に複数の値が到着し得る。古い保留値を
  // そのまま処理せず、最後に受けた値で置き換える。
  _ledBrightnessRequestPending = true;
  _ledBrightnessRequestValue = data[0];
  portEXIT_CRITICAL(&_ledBrightnessMux);
}

void BleManager::processLedBrightnessRequest() {
  uint8_t brightness = 0;
  bool hasRequest = false;

  portENTER_CRITICAL(&_ledBrightnessMux);
  if (_ledBrightnessRequestPending) {
    brightness = _ledBrightnessRequestValue;
    _ledBrightnessRequestPending = false;
    _ledBrightnessRequestValue = 0;
    hasRequest = true;
  }
  portEXIT_CRITICAL(&_ledBrightnessMux);

  if (!hasRequest) {
    return;
  }

  ledCtrl.setBrightness(brightness);
  ledCtrl.saveBrightness();
  publishLedBrightnessStatus(true);
}
