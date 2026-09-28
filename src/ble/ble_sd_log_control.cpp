/**
 * @file ble_sd_log_control.cpp
 * @brief BLE経由のSDログ開始/停止制御
 */

#include "ble_manager.h"
#include "button_handler.h"
#include "sd_logger.h"
#include <string.h>

static bool sdLogOpIsValid(uint8_t op) {
  return op == BLE_SD_LOG_OP_READ ||
         op == BLE_SD_LOG_OP_START ||
         op == BLE_SD_LOG_OP_STOP;
}

static uint8_t sdLogFailureResult(SdLogger* logger) {
  if (!logger) {
    return BLE_SD_LOG_RESULT_UNAVAILABLE;
  }

  SdLoggerState state = logger->getState();
  if (state == SD_STATE_UNINITIALIZED || state == SD_STATE_NO_CARD) {
    return BLE_SD_LOG_RESULT_UNAVAILABLE;
  }
  return BLE_SD_LOG_RESULT_FAILED;
}

void BleManager::clearSdLogControlRequest() {
  portENTER_CRITICAL(&_sdLogControlMux);
  _sdLogControlRequestPending = false;
  _sdLogControlRequestOp = 0;
  portEXIT_CRITICAL(&_sdLogControlMux);
}

void BleManager::publishSdLogControlStatus(uint8_t op, uint8_t result) {
  memset(_sdLogControlLastStatus, 0, sizeof(_sdLogControlLastStatus));
  _sdLogControlLastStatus[0] = BLE_SD_LOG_CONTROL_PROTOCOL_VERSION;
  _sdLogControlLastStatus[1] = op;
  _sdLogControlLastStatus[2] = result;

  if (_pSdLogger) {
    _sdLogControlLastStatus[3] = (uint8_t)_pSdLogger->getState();
    if (_pSdLogger->isAvailable()) {
      _sdLogControlLastStatus[4] |= BLE_SD_LOG_FLAG_CARD_AVAILABLE;
    }
    if (_pSdLogger->isRecordingRequested()) {
      _sdLogControlLastStatus[4] |= BLE_SD_LOG_FLAG_LOGGING_ENABLED;
    }
    if (_pSdLogger->canLog()) {
      _sdLogControlLastStatus[4] |= BLE_SD_LOG_FLAG_CAN_LOG;
    }
    _sdLogControlLastStatus[5] = (uint8_t)_pSdLogger->getStopReason();
  }

  if (_pSdLogControlChar) {
    _pSdLogControlChar->setValue(_sdLogControlLastStatus, BLE_SD_LOG_CONTROL_STATUS_SIZE);
    if (_connectionCount > 0) {
      _pSdLogControlChar->notify();
    }
  }
  updateSdLogDetailStatus(true);
  updateDeviceHealthStatus(true);
}

void BleManager::onSdLogControlWrite(const uint8_t* data, size_t length) {
  if (!_running || !_pSdLogControlChar) {
    return;
  }

  uint8_t op = (data != nullptr && length > 0) ? data[0] : BLE_SD_LOG_OP_READ;
  if (data == nullptr || length != 1) {
    publishSdLogControlStatus(op, BLE_SD_LOG_RESULT_INVALID_LENGTH);
    return;
  }

  if (!sdLogOpIsValid(op)) {
    publishSdLogControlStatus(op, BLE_SD_LOG_RESULT_INVALID_OP);
    return;
  }

  if (_pSdLogger == nullptr) {
    publishSdLogControlStatus(op, BLE_SD_LOG_RESULT_UNAVAILABLE);
    return;
  }

  bool queued = false;
  portENTER_CRITICAL(&_sdLogControlMux);
  // STOP is a safety operation. It may supersede a queued read/start so a
  // client can always drive the logger to a terminal state.
  if (!_sdLogControlRequestPending || op == BLE_SD_LOG_OP_STOP) {
    _sdLogControlRequestPending = true;
    _sdLogControlRequestOp = op;
    queued = true;
  }
  portEXIT_CRITICAL(&_sdLogControlMux);

  publishSdLogControlStatus(op, queued ? BLE_SD_LOG_RESULT_QUEUED
                                       : BLE_SD_LOG_RESULT_BUSY);
}

void BleManager::processSdLogControlRequest() {
  if (!_running || _pSdLogger == nullptr) {
    return;
  }

  uint8_t op = 0;
  bool hasRequest = false;

  portENTER_CRITICAL(&_sdLogControlMux);
  if (_sdLogControlRequestPending) {
    op = _sdLogControlRequestOp;
    _sdLogControlRequestPending = false;
    _sdLogControlRequestOp = 0;
    hasRequest = true;
  }
  portEXIT_CRITICAL(&_sdLogControlMux);

  if (!hasRequest) {
    return;
  }

  uint8_t result = BLE_SD_LOG_RESULT_OK;
  switch (op) {
    case BLE_SD_LOG_OP_READ:
      break;
    case BLE_SD_LOG_OP_START:
      if (getButtonMode() != BTN_MODE_I2C_MEASURE) {
        result = BLE_SD_LOG_RESULT_WRONG_MODE;
      } else if (!_pSdLogger->resumeLogging()) {
        result = sdLogFailureResult(_pSdLogger);
      }
      break;
    case BLE_SD_LOG_OP_STOP:
      // Stop is idempotent and must be accepted even after an SD failure.
      // The detail status keeps the original failure reason for diagnosis.
      _pSdLogger->disableLogging(SD_STOP_USER_DISABLED);
      if (!_pSdLogger->isStorageQuiescent()) result = BLE_SD_LOG_RESULT_BUSY;
      break;
    default:
      result = BLE_SD_LOG_RESULT_INVALID_OP;
      break;
  }

  publishSdLogControlStatus(op, result);
  updateSdStatus(true);
}
