/**
 * @file ble_reset_control.cpp
 * @brief BLE経由のESP32/STM32個別リセット制御
 */

#include "ble_manager.h"
#include <string.h>

static bool resetOpIsValid(uint8_t op) {
  return op == BLE_RESET_OP_READ ||
         op == BLE_RESET_OP_ESP32 ||
         op == BLE_RESET_OP_STM32;
}

static uint8_t resetTargetForOp(uint8_t op) {
  if (op == BLE_RESET_OP_ESP32) {
    return BLE_RESET_TARGET_ESP32;
  }
  if (op == BLE_RESET_OP_STM32) {
    return BLE_RESET_TARGET_STM32;
  }
  return BLE_RESET_TARGET_NONE;
}

void BleManager::clearResetControlRequest() {
  portENTER_CRITICAL(&_resetControlMux);
  _resetControlRequestPending = false;
  _resetControlRequestOp = 0;
  portEXIT_CRITICAL(&_resetControlMux);
}

bool BleManager::consumeResetControlRequest(uint8_t& op) {
  op = 0;
  bool hasRequest = false;

  portENTER_CRITICAL(&_resetControlMux);
  if (_resetControlRequestPending) {
    op = _resetControlRequestOp;
    _resetControlRequestPending = false;
    _resetControlRequestOp = 0;
    hasRequest = true;
  }
  portEXIT_CRITICAL(&_resetControlMux);

  return hasRequest;
}

void BleManager::onResetControlRead() {
  publishResetControlStatus(BLE_RESET_OP_READ, BLE_RESET_RESULT_OK, 0, false);
}

void BleManager::onResetControlWrite(const uint8_t* data, size_t length) {
  if (!_running || !_pResetControlChar) {
    return;
  }

  uint8_t op = (data != nullptr && length > 0) ? data[0] : BLE_RESET_OP_READ;
  if (data == nullptr || length != 1) {
    publishResetControlStatus(op, BLE_RESET_RESULT_INVALID_LENGTH);
    return;
  }

  if (!resetOpIsValid(op)) {
    publishResetControlStatus(op, BLE_RESET_RESULT_INVALID_OP);
    return;
  }

  if (op == BLE_RESET_OP_READ) {
    publishResetControlStatus(op, BLE_RESET_RESULT_OK);
    return;
  }

  bool queued = false;
  portENTER_CRITICAL(&_resetControlMux);
  if (!_resetControlRequestPending) {
    _resetControlRequestPending = true;
    _resetControlRequestOp = op;
    queued = true;
  }
  portEXIT_CRITICAL(&_resetControlMux);

  publishResetControlStatus(op, queued ? BLE_RESET_RESULT_QUEUED
                                       : BLE_RESET_RESULT_BUSY);
}

void BleManager::publishResetControlStatus(uint8_t op,
                                           uint8_t result,
                                           uint8_t flags,
                                           bool notify) {
  if (!_pResetControlChar) {
    return;
  }

  bool pending = false;
  uint8_t pendingOp = 0;
  portENTER_CRITICAL(&_resetControlMux);
  pending = _resetControlRequestPending;
  pendingOp = _resetControlRequestOp;
  portEXIT_CRITICAL(&_resetControlMux);

  if (pending) {
    if (pendingOp == BLE_RESET_OP_ESP32) {
      flags |= BLE_RESET_FLAG_ESP32_PENDING;
    } else if (pendingOp == BLE_RESET_OP_STM32) {
      flags |= BLE_RESET_FLAG_STM32_PENDING;
    }
  }

  uint8_t status[BLE_RESET_CONTROL_STATUS_SIZE];
  memset(status, 0, sizeof(status));
  status[0] = BLE_RESET_CONTROL_PROTOCOL_VERSION;
  status[1] = op;
  status[2] = result;
  status[3] = flags;
  status[4] = resetTargetForOp(op);

  memcpy(_resetControlLastStatus, status, BLE_RESET_CONTROL_STATUS_SIZE);
  _pResetControlChar->setValue(_resetControlLastStatus, BLE_RESET_CONTROL_STATUS_SIZE);
  if (notify && _connectionCount > 0) {
    _pResetControlChar->notify();
  }
}
