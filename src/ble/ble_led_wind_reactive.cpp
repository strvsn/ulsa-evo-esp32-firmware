/**
 * @file ble_led_wind_reactive.cpp
 * @brief BLE経由の風速連動LED設定
 */

#include "ble_manager.h"
#include "hardware/led_controller.h"
#include "hardware/wind_reactive_led_palette.h"

extern LedController ledCtrl;

void BleManager::clearLedWindReactiveRequest() {
  portENTER_CRITICAL(&_ledWindReactiveMux);
  _ledWindReactiveRequestPending = false;
  _ledWindReactiveRequestEnabled = false;
  _ledWindReactiveRequestTheme = WIND_REACTIVE_LED_THEME_TIDE;
  portEXIT_CRITICAL(&_ledWindReactiveMux);
}

void BleManager::publishLedWindReactiveStatus(uint8_t op, uint8_t result, bool notify) {
  if (!_pLedWindReactiveChar) {
    return;
  }

  uint8_t flags = BLE_LED_WIND_REACTIVE_FLAG_PERSISTED;
  if (ledCtrl.isWindReactiveEnabled()) {
    flags |= BLE_LED_WIND_REACTIVE_FLAG_ENABLED;
  }
  if (ledCtrl.isWindReactiveActive()) {
    flags |= BLE_LED_WIND_REACTIVE_FLAG_ACTIVE;
  }
  const uint8_t status[BLE_LED_WIND_REACTIVE_STATUS_SIZE] = {
    BLE_LED_WIND_REACTIVE_PROTOCOL_VERSION,
    op,
    result,
    flags,
    ledCtrl.getWindReactiveTheme(),
    0,
  };
  _pLedWindReactiveChar->setValue(status, sizeof(status));
  if (notify && _connectionCount > 0) {
    _pLedWindReactiveChar->notify();
  }
}

void BleManager::onLedWindReactiveRead() {
  publishLedWindReactiveStatus(BLE_LED_WIND_REACTIVE_OP_READ,
                                BLE_LED_WIND_REACTIVE_RESULT_OK,
                                false);
}

void BleManager::onLedWindReactiveWrite(const uint8_t* data, size_t length) {
  if (!_running || !_pLedWindReactiveChar || data == nullptr || length == 0U) {
    if (_pLedWindReactiveChar) {
      publishLedWindReactiveStatus(BLE_LED_WIND_REACTIVE_OP_READ,
                                    BLE_LED_WIND_REACTIVE_RESULT_INVALID_LENGTH,
                                    true);
    }
    return;
  }

  const uint8_t op = data[0];
  if (op == BLE_LED_WIND_REACTIVE_OP_READ) {
    if (length != 1U) {
      publishLedWindReactiveStatus(op, BLE_LED_WIND_REACTIVE_RESULT_INVALID_LENGTH, true);
      return;
    }
    publishLedWindReactiveStatus(op, BLE_LED_WIND_REACTIVE_RESULT_OK, true);
    return;
  }
  if (op != BLE_LED_WIND_REACTIVE_OP_SET_CONFIG) {
    publishLedWindReactiveStatus(op, BLE_LED_WIND_REACTIVE_RESULT_INVALID_OP, true);
    return;
  }
  if (length != 3U) {
    publishLedWindReactiveStatus(op, BLE_LED_WIND_REACTIVE_RESULT_INVALID_LENGTH, true);
    return;
  }
  if (data[1] > 1U || !WindReactiveLedPalette::isValidTheme(data[2])) {
    publishLedWindReactiveStatus(op, BLE_LED_WIND_REACTIVE_RESULT_INVALID_VALUE, true);
    return;
  }

  portENTER_CRITICAL(&_ledWindReactiveMux);
  _ledWindReactiveRequestPending = true;
  _ledWindReactiveRequestEnabled = data[1] == 1U;
  _ledWindReactiveRequestTheme = data[2];
  portEXIT_CRITICAL(&_ledWindReactiveMux);
  publishLedWindReactiveStatus(op, BLE_LED_WIND_REACTIVE_RESULT_QUEUED, true);
}

void BleManager::processLedWindReactiveRequest() {
  bool enabled = false;
  uint8_t theme = WIND_REACTIVE_LED_THEME_TIDE;
  bool hasRequest = false;

  portENTER_CRITICAL(&_ledWindReactiveMux);
  if (_ledWindReactiveRequestPending) {
    enabled = _ledWindReactiveRequestEnabled;
    theme = _ledWindReactiveRequestTheme;
    _ledWindReactiveRequestPending = false;
    hasRequest = true;
  }
  portEXIT_CRITICAL(&_ledWindReactiveMux);

  if (!hasRequest) {
    return;
  }

  const bool saved = ledCtrl.setWindReactiveConfig(enabled, theme);
  publishLedWindReactiveStatus(
    BLE_LED_WIND_REACTIVE_OP_SET_CONFIG,
    saved ? BLE_LED_WIND_REACTIVE_RESULT_OK : BLE_LED_WIND_REACTIVE_RESULT_FAILED,
    true);
}
