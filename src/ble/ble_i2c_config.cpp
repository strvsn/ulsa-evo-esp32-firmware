/**
 * @file ble_i2c_config.cpp
 * @brief BLE経由のULSA EVO I2C config制御
 */

#include "ble_manager.h"
#include "ota/ota_manager.h"
#include "sensor/ulsa_evo_i2c_client.h"
#include <string.h>

static bool i2cConfigOpNeedsValue(uint8_t op) {
  return op == BLE_I2C_CONFIG_OP_SET_NODE_ID ||
         op == BLE_I2C_CONFIG_OP_SET_AVG_CYCLE ||
         op == BLE_I2C_CONFIG_OP_SET_WIND_MODE ||
         op == BLE_I2C_CONFIG_OP_SET_I2C_ADDR;
}

static bool i2cConfigOpIsValid(uint8_t op) {
  return op == BLE_I2C_CONFIG_OP_READ_CONFIG ||
         op == BLE_I2C_CONFIG_OP_SET_NODE_ID ||
         op == BLE_I2C_CONFIG_OP_SET_AVG_CYCLE ||
         op == BLE_I2C_CONFIG_OP_SET_WIND_MODE ||
         op == BLE_I2C_CONFIG_OP_SET_I2C_ADDR ||
         op == BLE_I2C_CONFIG_OP_SAVE_CONFIG ||
         op == BLE_I2C_CONFIG_OP_DISCARD_CONFIG ||
         op == BLE_I2C_CONFIG_OP_RESTORE_DEFAULTS ||
         op == BLE_I2C_CONFIG_OP_CLEAR_ERROR;
}

static uint8_t resultFromClientError(uint8_t error) {
  if (error == ULSA_I2C_ERR_CONFIG_UNSUPPORTED) {
    return BLE_I2C_CONFIG_RESULT_UNSUPPORTED;
  }
  if (error == ULSA_I2C_ERR_NONE) {
    return BLE_I2C_CONFIG_RESULT_OK;
  }
  return BLE_I2C_CONFIG_RESULT_I2C_FAILED;
}

static void applyConfirmedI2cStageValue(uint8_t* status, uint8_t op,
                                        uint8_t value) {
  if (status == nullptr) {
    return;
  }
  switch (op) {
    case BLE_I2C_CONFIG_OP_SET_NODE_ID:
      status[6] = value;
      break;
    case BLE_I2C_CONFIG_OP_SET_AVG_CYCLE:
      status[7] = value;
      break;
    case BLE_I2C_CONFIG_OP_SET_WIND_MODE:
      status[8] = value;
      break;
    case BLE_I2C_CONFIG_OP_SET_I2C_ADDR:
      status[9] = value;
      break;
    default:
      break;
  }
}

void BleManager::clearI2cConfigRequest() {
  portENTER_CRITICAL(&_i2cConfigMux);
  _i2cConfigRequestPending = false;
  _i2cConfigRequestOp = 0;
  _i2cConfigRequestValue = 0;
  _i2cConfigRequestOperationSeq = 0;
  portEXIT_CRITICAL(&_i2cConfigMux);
}

void BleManager::publishI2cConfigStatus(uint8_t op, uint8_t result,
                                        uint8_t operationSeq) {
  uint8_t preservedRemoteAndConfig[9] = {0};
  memcpy(preservedRemoteAndConfig, &_i2cConfigLastStatus[3],
         sizeof(preservedRemoteAndConfig));
  const uint8_t preservedRebootFlag = _i2cConfigLastStatus[15] & 0x01;
  memset(_i2cConfigLastStatus, 0, sizeof(_i2cConfigLastStatus));
  memcpy(&_i2cConfigLastStatus[3], preservedRemoteAndConfig,
         sizeof(preservedRemoteAndConfig));
  _i2cConfigLastStatus[0] = BLE_I2C_CONFIG_PROTOCOL_VERSION;
  _i2cConfigLastStatus[1] = op;
  _i2cConfigLastStatus[2] = result;
  _i2cConfigLastStatus[12] =
    _pI2cConfigClient ? _pI2cConfigClient->getStats().lastError : ULSA_I2C_ERR_NO_WIRE;
  _i2cConfigLastStatus[13] =
    _pI2cConfigClient ? _pI2cConfigClient->getRegVersion() : 0;
  _i2cConfigLastStatus[14] =
    _pI2cConfigClient ? _pI2cConfigClient->getAddress() : 0;
  _i2cConfigLastStatus[15] = preservedRebootFlag;
  _i2cConfigLastStatus[16] = operationSeq;
  _i2cConfigLastStatus[17] =
    _pI2cConfigClient ? _pI2cConfigClient->getCommandResultSeq() : 0;
  if (_pI2cConfigClient) {
    if (_pI2cConfigClient->isDetected()) {
      _i2cConfigLastStatus[15] |= 0x02;
    }
    if (_pI2cConfigClient->supportsConfigWrite()) {
      _i2cConfigLastStatus[15] |= 0x04;
    }
  }

  if (_pI2cConfigChar) {
    _pI2cConfigChar->setValue(_i2cConfigLastStatus, BLE_I2C_CONFIG_STATUS_SIZE);
    if (_connectionCount > 0) {
      _pI2cConfigChar->notify();
    }
  }
  updateDeviceHealthStatus(true);
}

void BleManager::onI2cConfigWrite(const uint8_t* data, size_t length) {
  if (!_running || !_pI2cConfigChar) {
    return;
  }

  uint8_t op = (data != nullptr && length > 0) ? data[0] : BLE_I2C_CONFIG_OP_READ_CONFIG;
  if (data == nullptr || length == 0 || length > 2) {
    publishI2cConfigStatus(op, BLE_I2C_CONFIG_RESULT_INVALID_LENGTH,
                           _i2cConfigOperationSeq);
    return;
  }

  if (!i2cConfigOpIsValid(op)) {
    publishI2cConfigStatus(op, BLE_I2C_CONFIG_RESULT_INVALID_OP,
                           _i2cConfigOperationSeq);
    return;
  }

  uint8_t expectedLength = i2cConfigOpNeedsValue(op) ? 2 : 1;
  if (length != expectedLength) {
    publishI2cConfigStatus(op, BLE_I2C_CONFIG_RESULT_INVALID_LENGTH,
                           _i2cConfigOperationSeq);
    return;
  }

  uint8_t operationSeq = 0;
  portENTER_CRITICAL(&_i2cConfigMux);
  _i2cConfigOperationSeq = (uint8_t)(_i2cConfigOperationSeq + 1U);
  operationSeq = _i2cConfigOperationSeq;
  portEXIT_CRITICAL(&_i2cConfigMux);

  if (_pOtaManager != nullptr && _pOtaManager->isNodeLabelFrozen() &&
      op != BLE_I2C_CONFIG_OP_READ_CONFIG &&
      op != BLE_I2C_CONFIG_OP_CLEAR_ERROR) {
    publishI2cConfigStatus(op, BLE_I2C_CONFIG_RESULT_BUSY, operationSeq);
    return;
  }

  if (_pI2cConfigClient == nullptr) {
    publishI2cConfigStatus(op, BLE_I2C_CONFIG_RESULT_UNAVAILABLE,
                           operationSeq);
    return;
  }

  bool queued = false;
  portENTER_CRITICAL(&_i2cConfigMux);
  if (!_i2cConfigRequestPending) {
    _i2cConfigRequestPending = true;
    _i2cConfigRequestOp = op;
    _i2cConfigRequestValue = (length > 1) ? data[1] : 0;
    _i2cConfigRequestOperationSeq = operationSeq;
    queued = true;
  }
  portEXIT_CRITICAL(&_i2cConfigMux);

  publishI2cConfigStatus(op, queued ? BLE_I2C_CONFIG_RESULT_QUEUED
                                    : BLE_I2C_CONFIG_RESULT_BUSY,
                         operationSeq);
}

void BleManager::processI2cConfigRequest() {
  if (!_running || _pI2cConfigClient == nullptr) {
    return;
  }

  uint8_t op = 0;
  uint8_t value = 0;
  uint8_t operationSeq = 0;
  bool hasRequest = false;

  portENTER_CRITICAL(&_i2cConfigMux);
  if (_i2cConfigRequestPending) {
    op = _i2cConfigRequestOp;
    value = _i2cConfigRequestValue;
    operationSeq = _i2cConfigRequestOperationSeq;
    _i2cConfigRequestPending = false;
    _i2cConfigRequestOp = 0;
    _i2cConfigRequestValue = 0;
    _i2cConfigRequestOperationSeq = 0;
    hasRequest = true;
  }
  portEXIT_CRITICAL(&_i2cConfigMux);

  if (!hasRequest) {
    return;
  }

  UlsaEvoI2cConfigResult commandResult;
  bool commandResultValid = false;
  bool ok = false;

  switch (op) {
    case BLE_I2C_CONFIG_OP_READ_CONFIG:
      ok = true;
      break;
    case BLE_I2C_CONFIG_OP_SET_NODE_ID:
      ok = _pI2cConfigClient->stageNodeId(value);
      break;
    case BLE_I2C_CONFIG_OP_SET_AVG_CYCLE:
      ok = _pI2cConfigClient->stageAvgCycle(value);
      break;
    case BLE_I2C_CONFIG_OP_SET_WIND_MODE:
      ok = _pI2cConfigClient->stageWindDirInstallMode(value);
      break;
    case BLE_I2C_CONFIG_OP_SET_I2C_ADDR:
      ok = _pI2cConfigClient->stageI2cAddress(value);
      break;
    case BLE_I2C_CONFIG_OP_SAVE_CONFIG:
      ok = _pI2cConfigClient->saveConfig(&commandResult);
      commandResultValid = true;
      break;
    case BLE_I2C_CONFIG_OP_DISCARD_CONFIG:
      ok = _pI2cConfigClient->discardConfig(&commandResult);
      commandResultValid = true;
      break;
    case BLE_I2C_CONFIG_OP_RESTORE_DEFAULTS:
      ok = _pI2cConfigClient->restoreConfigDefaults(&commandResult);
      commandResultValid = true;
      break;
    case BLE_I2C_CONFIG_OP_CLEAR_ERROR:
      ok = _pI2cConfigClient->clearRemoteError(&commandResult);
      commandResultValid = true;
      break;
    default:
      publishI2cConfigStatus(op, BLE_I2C_CONFIG_RESULT_INVALID_OP,
                             operationSeq);
      return;
  }

  uint8_t operationError = _pI2cConfigClient->getStats().lastError;
  UlsaEvoI2cConfig config;
  bool haveConfig = _pI2cConfigClient->readConfig(config);
  uint8_t readConfigError = _pI2cConfigClient->getStats().lastError;
  uint8_t localError = ok ? readConfigError : operationError;
  uint8_t result = ok ? BLE_I2C_CONFIG_RESULT_OK : resultFromClientError(operationError);
  if (op == BLE_I2C_CONFIG_OP_READ_CONFIG && !haveConfig) {
    result = resultFromClientError(readConfigError);
    localError = readConfigError;
  }

  uint8_t preservedRemoteAndConfig[9] = {0};
  memcpy(preservedRemoteAndConfig, &_i2cConfigLastStatus[3],
         sizeof(preservedRemoteAndConfig));
  const uint8_t preservedRebootFlag = _i2cConfigLastStatus[15] & 0x01;
  memset(_i2cConfigLastStatus, 0, sizeof(_i2cConfigLastStatus));
  memcpy(&_i2cConfigLastStatus[3], preservedRemoteAndConfig,
         sizeof(preservedRemoteAndConfig));
  _i2cConfigLastStatus[0] = BLE_I2C_CONFIG_PROTOCOL_VERSION;
  _i2cConfigLastStatus[1] = op;
  _i2cConfigLastStatus[2] = result;
  _i2cConfigLastStatus[16] = operationSeq;
  _i2cConfigLastStatus[17] = _pI2cConfigClient->getCommandResultSeq();
  _i2cConfigLastStatus[15] = preservedRebootFlag;

  if (haveConfig) {
    _i2cConfigLastStatus[3] = config.commandStatus;
    _i2cConfigLastStatus[4] = config.lastError;
    _i2cConfigLastStatus[5] = config.flags;
    _i2cConfigLastStatus[6] = config.nodeId;
    _i2cConfigLastStatus[7] = config.avgCycle;
    _i2cConfigLastStatus[8] = config.windDirInstallMode;
    _i2cConfigLastStatus[9] = config.i2cAddr;
    _i2cConfigLastStatus[10] = config.i2cSlaveEnabled;
    _i2cConfigLastStatus[11] = config.measIntervalMs;
    _i2cConfigLastStatus[17] = config.commandResultSeq;
  } else if (ok) {
    // stageConfigRegister() already confirmed this value by direct readback.
    // Keep all other fields at their last confirmed values when the optional
    // full-block refresh is temporarily unavailable.
    applyConfirmedI2cStageValue(_i2cConfigLastStatus, op, value);
  }
  if (commandResultValid) {
    _i2cConfigLastStatus[3] = commandResult.commandStatus;
    _i2cConfigLastStatus[4] = commandResult.lastError;
    _i2cConfigLastStatus[5] = commandResult.flags;
    _i2cConfigLastStatus[17] = commandResult.commandResultSeq;
    if (commandResult.rebootRequired) {
      _i2cConfigLastStatus[15] |= 0x01;
    }
  }

  _i2cConfigLastStatus[12] = localError;
  _i2cConfigLastStatus[13] = _pI2cConfigClient->getRegVersion();
  _i2cConfigLastStatus[14] = _pI2cConfigClient->getAddress();
  if (_pI2cConfigClient->isDetected()) {
    _i2cConfigLastStatus[15] |= 0x02;
  }
  if (_pI2cConfigClient->supportsConfigWrite()) {
    _i2cConfigLastStatus[15] |= 0x04;
  }
  if (haveConfig && (config.flags & UlsaEvoI2cClient::CFG_FLAG_REBOOT_REQUIRED)) {
    _i2cConfigLastStatus[15] |= 0x01;
  }

  if (_pI2cConfigChar) {
    _pI2cConfigChar->setValue(_i2cConfigLastStatus, BLE_I2C_CONFIG_STATUS_SIZE);
    if (_connectionCount > 0) {
      _pI2cConfigChar->notify();
    }
  }
  updateDeviceHealthStatus(true);
}
