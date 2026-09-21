/**
 * @file ble_sd_log_settings.cpp
 * @brief BLE経由のSDログ周期設定
 */

#include "ble_manager.h"
#include "sd_logger.h"
#include "sensor/ulsa_evo_i2c_client.h"
#include <string.h>

static uint32_t readU32LE(const uint8_t* data) {
  return ((uint32_t)data[0]) |
         ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) |
         ((uint32_t)data[3] << 24);
}

static void putU32LE(uint8_t* data, size_t offset, uint32_t value) {
  data[offset] = (uint8_t)(value & 0xFF);
  data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
  data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
  data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

static bool sdLogSettingsOpIsValid(uint8_t op) {
  return op == BLE_SD_LOG_SETTINGS_OP_READ ||
         op == BLE_SD_LOG_SETTINGS_OP_SET_INTERVAL_MS ||
         op == BLE_SD_LOG_SETTINGS_OP_RESTORE_DEFAULT ||
         op == BLE_SD_LOG_SETTINGS_OP_SET_AUTO_START;
}

static bool sdLogSettingsOpNeedsInterval(uint8_t op) {
  return op == BLE_SD_LOG_SETTINGS_OP_SET_INTERVAL_MS;
}

static bool sdLogSettingsOpNeedsAutoStartValue(uint8_t op) {
  return op == BLE_SD_LOG_SETTINGS_OP_SET_AUTO_START;
}

static size_t sdLogSettingsExpectedLength(uint8_t op) {
  if (sdLogSettingsOpNeedsInterval(op)) {
    return 5;
  }
  return sdLogSettingsOpNeedsAutoStartValue(op) ? 2 : 1;
}

void BleManager::clearSdLogSettingsRequest() {
  portENTER_CRITICAL(&_sdLogSettingsMux);
  _sdLogSettingsRequestPending = false;
  _sdLogSettingsRequestOp = 0;
  _sdLogSettingsRequestIntervalMs = 0;
  _sdLogSettingsRequestAutoStartEnabled = false;
  portEXIT_CRITICAL(&_sdLogSettingsMux);
}

uint32_t BleManager::getSdLogStm32IntervalMs() const {
  uint32_t intervalMs = 0;

  if (_pI2cConfigClient) {
    const UlsaEvoI2cStats& stats = _pI2cConfigClient->getStats();
    if (stats.i2cOutputIntervalMs >= SD_LOG_INTERVAL_MIN_MS &&
        stats.i2cOutputIntervalMs <= SD_LOG_INTERVAL_MAX_MS) {
      intervalMs = stats.i2cOutputIntervalMs;
    }

    if (intervalMs == 0) {
      UlsaEvoI2cConfig config;
      if (_pI2cConfigClient->readConfig(config) &&
          config.i2cOutputIntervalMs >= SD_LOG_INTERVAL_MIN_MS &&
          config.i2cOutputIntervalMs <= SD_LOG_INTERVAL_MAX_MS) {
        intervalMs = config.i2cOutputIntervalMs;
      }
    }

    const uint32_t observedMs = stats.observedIntervalMs;
    if (intervalMs == 0 &&
        observedMs >= SD_LOG_INTERVAL_MIN_MS && observedMs <= SD_LOG_INTERVAL_MAX_MS &&
        stats.lastSuccessTime != 0) {
      const uint32_t ageMs = millis() - stats.lastSuccessTime;
      const uint32_t staleAfterMs = observedMs < 5000UL ? 5000UL : observedMs * 4UL;
      if (ageMs <= staleAfterMs) {
        intervalMs = observedMs;
      }
    }
  }

  return intervalMs;
}

uint32_t BleManager::getSdLogMinIntervalMs() const {
  const uint32_t stm32IntervalMs = getSdLogStm32IntervalMs();
  if (stm32IntervalMs > SD_LOG_INTERVAL_MIN_MS) {
    return stm32IntervalMs;
  }
  if (stm32IntervalMs == 0) {
    return SD_LOG_INTERVAL_DEFAULT_MS;
  }
  return SD_LOG_INTERVAL_MIN_MS;
}

void BleManager::publishSdLogSettingsStatus(uint8_t op, uint8_t result) {
  memset(_sdLogSettingsLastStatus, 0, sizeof(_sdLogSettingsLastStatus));
  _sdLogSettingsLastStatus[0] = BLE_SD_LOG_SETTINGS_PROTOCOL_VERSION;
  _sdLogSettingsLastStatus[1] = op;
  _sdLogSettingsLastStatus[2] = result;

  const uint32_t stm32IntervalMs = getSdLogStm32IntervalMs();
  const uint32_t minIntervalMs = getSdLogMinIntervalMs();
  putU32LE(_sdLogSettingsLastStatus, 8, minIntervalMs);
  putU32LE(_sdLogSettingsLastStatus, 12, SD_LOG_INTERVAL_MAX_MS);
  putU32LE(_sdLogSettingsLastStatus, 16, stm32IntervalMs);

  if (_pSdLogger) {
    const uint32_t currentIntervalMs = _pSdLogger->getLogIntervalMs();
    putU32LE(_sdLogSettingsLastStatus, 4, currentIntervalMs);
    if (_pSdLogger->isLogIntervalPersisted()) {
      _sdLogSettingsLastStatus[3] |= BLE_SD_LOG_SETTINGS_FLAG_PERSISTED;
    }
    if (currentIntervalMs == SD_LOG_INTERVAL_DEFAULT_MS) {
      _sdLogSettingsLastStatus[3] |= BLE_SD_LOG_SETTINGS_FLAG_DEFAULT_INTERVAL;
    }
    if (_pSdLogger->isAutoStartEnabled()) {
      _sdLogSettingsLastStatus[3] |= BLE_SD_LOG_SETTINGS_FLAG_AUTO_START_ENABLED;
    }
  }
  if (stm32IntervalMs != 0) {
    _sdLogSettingsLastStatus[3] |= BLE_SD_LOG_SETTINGS_FLAG_STM_INTERVAL_KNOWN;
  }

  if (_pSdLogSettingsChar) {
    _pSdLogSettingsChar->setValue(_sdLogSettingsLastStatus, BLE_SD_LOG_SETTINGS_STATUS_SIZE);
    if (_connectionCount > 0) {
      _pSdLogSettingsChar->notify();
    }
  }
}

void BleManager::onSdLogSettingsWrite(const uint8_t* data, size_t length) {
  if (!_running || !_pSdLogSettingsChar) {
    return;
  }

  uint8_t op = (data != nullptr && length > 0) ? data[0] : BLE_SD_LOG_SETTINGS_OP_READ;
  if (data == nullptr || length == 0) {
    publishSdLogSettingsStatus(op, BLE_SD_LOG_SETTINGS_RESULT_INVALID_LENGTH);
    return;
  }

  if (!sdLogSettingsOpIsValid(op)) {
    publishSdLogSettingsStatus(op, BLE_SD_LOG_SETTINGS_RESULT_INVALID_OP);
    return;
  }

  const size_t expectedLength = sdLogSettingsExpectedLength(op);
  if (length != expectedLength) {
    publishSdLogSettingsStatus(op, BLE_SD_LOG_SETTINGS_RESULT_INVALID_LENGTH);
    return;
  }

  if (sdLogSettingsOpNeedsAutoStartValue(op) && data[1] > 1U) {
    publishSdLogSettingsStatus(op, BLE_SD_LOG_SETTINGS_RESULT_INVALID_VALUE);
    return;
  }

  if (_pSdLogger == nullptr) {
    publishSdLogSettingsStatus(op, BLE_SD_LOG_SETTINGS_RESULT_UNAVAILABLE);
    return;
  }

  const uint32_t intervalMs = sdLogSettingsOpNeedsInterval(op) ? readU32LE(&data[1]) : 0;
  const bool autoStartEnabled = sdLogSettingsOpNeedsAutoStartValue(op) && data[1] == 1U;
  bool queued = false;
  portENTER_CRITICAL(&_sdLogSettingsMux);
  if (!_sdLogSettingsRequestPending) {
    _sdLogSettingsRequestPending = true;
    _sdLogSettingsRequestOp = op;
    _sdLogSettingsRequestIntervalMs = intervalMs;
    _sdLogSettingsRequestAutoStartEnabled = autoStartEnabled;
    queued = true;
  }
  portEXIT_CRITICAL(&_sdLogSettingsMux);

  publishSdLogSettingsStatus(op, queued ? BLE_SD_LOG_SETTINGS_RESULT_QUEUED
                                        : BLE_SD_LOG_SETTINGS_RESULT_BUSY);
}

void BleManager::processSdLogSettingsRequest() {
  if (!_running || _pSdLogger == nullptr) {
    return;
  }

  uint8_t op = 0;
  uint32_t intervalMs = 0;
  bool autoStartEnabled = false;
  bool hasRequest = false;

  portENTER_CRITICAL(&_sdLogSettingsMux);
  if (_sdLogSettingsRequestPending) {
    op = _sdLogSettingsRequestOp;
    intervalMs = _sdLogSettingsRequestIntervalMs;
    autoStartEnabled = _sdLogSettingsRequestAutoStartEnabled;
    _sdLogSettingsRequestPending = false;
    _sdLogSettingsRequestOp = 0;
    _sdLogSettingsRequestIntervalMs = 0;
    _sdLogSettingsRequestAutoStartEnabled = false;
    hasRequest = true;
  }
  portEXIT_CRITICAL(&_sdLogSettingsMux);

  if (!hasRequest) {
    return;
  }

  uint8_t result = BLE_SD_LOG_SETTINGS_RESULT_OK;
  switch (op) {
    case BLE_SD_LOG_SETTINGS_OP_READ:
      break;
    case BLE_SD_LOG_SETTINGS_OP_SET_INTERVAL_MS: {
      const uint32_t sourceIntervalMs = getSdLogStm32IntervalMs();
      const uint32_t minIntervalMs = getSdLogMinIntervalMs();
      if (sourceIntervalMs == 0U) {
        result = BLE_SD_LOG_SETTINGS_RESULT_SOURCE_INTERVAL_UNKNOWN;
      } else if (intervalMs < minIntervalMs || intervalMs > SD_LOG_INTERVAL_MAX_MS) {
        result = BLE_SD_LOG_SETTINGS_RESULT_OUT_OF_RANGE;
      } else if (!SdLogIntervalPolicy::isExactSourceMultiple(intervalMs, sourceIntervalMs)) {
        result = BLE_SD_LOG_SETTINGS_RESULT_INTERVAL_NOT_ALIGNED;
      } else if (!_pSdLogger->setLogIntervalMs(intervalMs, minIntervalMs, true)) {
        result = BLE_SD_LOG_SETTINGS_RESULT_FAILED;
      }
      break;
    }
    case BLE_SD_LOG_SETTINGS_OP_RESTORE_DEFAULT: {
      const uint32_t sourceIntervalMs = getSdLogStm32IntervalMs();
      const uint32_t minIntervalMs = getSdLogMinIntervalMs();
      if (sourceIntervalMs == 0U) {
        result = BLE_SD_LOG_SETTINGS_RESULT_SOURCE_INTERVAL_UNKNOWN;
      } else {
        const uint32_t defaultIntervalMs = SdLogIntervalPolicy::alignAtOrAbove(
            SD_LOG_INTERVAL_DEFAULT_MS, sourceIntervalMs, SD_LOG_INTERVAL_MAX_MS);
        if (defaultIntervalMs == 0U ||
            !_pSdLogger->setLogIntervalMs(defaultIntervalMs, minIntervalMs, true)) {
          result = BLE_SD_LOG_SETTINGS_RESULT_FAILED;
        }
      }
      break;
    }
    case BLE_SD_LOG_SETTINGS_OP_SET_AUTO_START:
      if (!_pSdLogger->setAutoStartEnabled(autoStartEnabled)) {
        result = BLE_SD_LOG_SETTINGS_RESULT_FAILED;
      }
      break;
    default:
      result = BLE_SD_LOG_SETTINGS_RESULT_INVALID_OP;
      break;
  }

  publishSdLogSettingsStatus(op, result);
  updateSdLogDetailStatus(true);
}
