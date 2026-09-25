/**
 * @file ble_stm32_update_control.cpp
 * @brief BLE経由のSTM32 update用共通SoftAP session制御
 */

#include "ble_manager.h"
#include "ota_manager.h"
#include "system/update_coordinator.h"

#include <string.h>

extern ulsa_update::UpdateCoordinator updateCoordinator;

static_assert(BLE_STM32_UPDATE_CONTROL_STATUS_HEADER_SIZE +
                (sizeof(PORTAL_SSID_PREFIX) - 1) +
                PORTAL_SESSION_SUFFIX_LENGTH +
                PORTAL_PASSWORD_LENGTH +
                PORTAL_TOKEN_LENGTH +
                15 <= BLE_STM32_UPDATE_CONTROL_STATUS_MAX_SIZE,
              "BLE STM32 update status payload is too small for SoftAP credentials");

static bool stm32UpdateOpIsValid(uint8_t op) {
  return op == BLE_STM32_UPDATE_OP_READ ||
         op == BLE_STM32_UPDATE_OP_PREPARE_PORTAL ||
         op == BLE_STM32_UPDATE_OP_ACTIVATE_PORTAL ||
         op == BLE_STM32_UPDATE_OP_STOP_OR_CANCEL;
}

static const uint8_t STM32_UPDATE_REQUEST_FLAG_EXPECTED_NODE_ID = 0x01;
static const uint8_t STM32_UPDATE_REQUEST_FLAG_TARGET = 0x02;
static const uint8_t STM32_UPDATE_REQUEST_FLAG_RELEASE_TAG = 0x04;

static void putU32LE(uint8_t* data, size_t offset, uint32_t value) {
  data[offset] = (uint8_t)(value & 0xFF);
  data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
  data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
  data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint8_t copyStatusString(uint8_t* dest,
                                size_t& offset,
                                size_t capacity,
                                const char* value) {
  if (!value) {
    return 0;
  }

  const size_t available = capacity > offset ? capacity - offset : 0;
  const size_t rawLen = strlen(value);
  const size_t copyLen = rawLen < available ? rawLen : available;
  if (copyLen > 0) {
    memcpy(dest + offset, value, copyLen);
    offset += copyLen;
  }
  return (uint8_t)copyLen;
}

static bool parseStm32UpdateControlRequest(const uint8_t* data,
                                           size_t length,
                                           BleStm32UpdateControlRequest& request,
                                           uint8_t& result) {
  request = BleStm32UpdateControlRequest();
  result = BLE_STM32_UPDATE_RESULT_OK;

  if (data == nullptr || length == 0 || length > BLE_STM32_UPDATE_CONTROL_REQUEST_MAX_SIZE) {
    result = BLE_STM32_UPDATE_RESULT_INVALID_LENGTH;
    return false;
  }

  request.op = data[0];
  if (!stm32UpdateOpIsValid(request.op)) {
    result = BLE_STM32_UPDATE_RESULT_INVALID_OP;
    return false;
  }

  if (length == 1) {
    return true;
  }

  if (request.op != BLE_STM32_UPDATE_OP_PREPARE_PORTAL || length < 5) {
    result = BLE_STM32_UPDATE_RESULT_INVALID_LENGTH;
    return false;
  }

  const uint8_t flags = data[1];
  const uint8_t allowedFlags = STM32_UPDATE_REQUEST_FLAG_EXPECTED_NODE_ID |
                               STM32_UPDATE_REQUEST_FLAG_TARGET |
                               STM32_UPDATE_REQUEST_FLAG_RELEASE_TAG;
  if ((flags & ~allowedFlags) != 0) {
    result = BLE_STM32_UPDATE_RESULT_INVALID_LENGTH;
    return false;
  }

  const uint8_t targetLen = data[3];
  const uint8_t releaseTagLen = data[4];
  if (length != (size_t)5 + targetLen + releaseTagLen) {
    result = BLE_STM32_UPDATE_RESULT_INVALID_LENGTH;
    return false;
  }
  if (targetLen > BLE_STM32_UPDATE_CONTROL_TARGET_MAX_LEN ||
      releaseTagLen > BLE_STM32_UPDATE_CONTROL_RELEASE_TAG_MAX_LEN) {
    result = BLE_STM32_UPDATE_RESULT_INVALID_LENGTH;
    return false;
  }

  const bool hasTarget = (flags & STM32_UPDATE_REQUEST_FLAG_TARGET) != 0;
  const bool hasReleaseTag = (flags & STM32_UPDATE_REQUEST_FLAG_RELEASE_TAG) != 0;
  if (hasTarget != (targetLen > 0) || hasReleaseTag != (releaseTagLen > 0)) {
    result = BLE_STM32_UPDATE_RESULT_INVALID_LENGTH;
    return false;
  }
  if (!hasTarget || !hasReleaseTag) {
    result = BLE_STM32_UPDATE_RESULT_INVALID_LENGTH;
    return false;
  }

  request.hasExpectedNodeId = (flags & STM32_UPDATE_REQUEST_FLAG_EXPECTED_NODE_ID) != 0;
  request.expectedNodeId = data[2];
  request.hasSessionBinding = hasTarget && hasReleaseTag;
  if (request.hasSessionBinding) {
    memcpy(request.target, data + 5, targetLen);
    request.target[targetLen] = '\0';
    memcpy(request.releaseTag, data + 5 + targetLen, releaseTagLen);
    request.releaseTag[releaseTagLen] = '\0';
  }
  return true;
}

void BleManager::clearStm32UpdateControlRequest() {
  portENTER_CRITICAL(&_stm32UpdateControlMux);
  _stm32UpdateControlRequestPending = false;
  _stm32UpdateControlRequest = BleStm32UpdateControlRequest();
  portEXIT_CRITICAL(&_stm32UpdateControlMux);
}

bool BleManager::consumeStm32UpdateControlRequest(BleStm32UpdateControlRequest& request) {
  request = BleStm32UpdateControlRequest();
  bool hasRequest = false;

  portENTER_CRITICAL(&_stm32UpdateControlMux);
  if (_stm32UpdateControlRequestPending) {
    request = _stm32UpdateControlRequest;
    _stm32UpdateControlRequestPending = false;
    _stm32UpdateControlRequest = BleStm32UpdateControlRequest();
    hasRequest = true;
  }
  portEXIT_CRITICAL(&_stm32UpdateControlMux);

  return hasRequest;
}

void BleManager::onStm32UpdateControlRead() {
  const uint8_t result =
    updateCoordinator.purpose() == ulsa_update::Purpose::Stm32Update &&
    updateCoordinator.authorizationExpired()
      ? BLE_STM32_UPDATE_RESULT_AUTHORIZATION_EXPIRED
      : (updateCoordinator.purpose() == ulsa_update::Purpose::Stm32Update &&
         updateCoordinator.physicalAuthorizationRequired()
           ? BLE_STM32_UPDATE_RESULT_AUTHORIZATION_REQUIRED
           : (_pOtaManager ? BLE_STM32_UPDATE_RESULT_OK
                           : BLE_STM32_UPDATE_RESULT_UNAVAILABLE));
  publishStm32UpdateControlStatus(
    BLE_STM32_UPDATE_OP_READ,
    result,
    false);
}

void BleManager::onStm32UpdateControlWrite(const uint8_t* data, size_t length,
                                           uint16_t peerHandle) {
  if (!_running || !_pStm32UpdateControlChar) {
    return;
  }

  BleStm32UpdateControlRequest request;
  uint8_t result = BLE_STM32_UPDATE_RESULT_OK;
  if (!parseStm32UpdateControlRequest(data, length, request, result)) {
    const uint8_t op = (data != nullptr && length > 0) ? data[0] : BLE_STM32_UPDATE_OP_READ;
    publishStm32UpdateControlStatus(op, result);
    return;
  }
  request.peerHandle = peerHandle;

  const uint8_t op = request.op;
  if (!stm32UpdateOpIsValid(op)) {
    publishStm32UpdateControlStatus(op, BLE_STM32_UPDATE_RESULT_INVALID_OP);
    return;
  }

  if (_pOtaManager == nullptr) {
    publishStm32UpdateControlStatus(op, BLE_STM32_UPDATE_RESULT_UNAVAILABLE);
    return;
  }

  if (op == BLE_STM32_UPDATE_OP_READ) {
    onStm32UpdateControlRead();
    return;
  }

  bool queued = false;
  portENTER_CRITICAL(&_stm32UpdateControlMux);
  if (!_stm32UpdateControlRequestPending) {
    _stm32UpdateControlRequestPending = true;
    _stm32UpdateControlRequest = request;
    queued = true;
  }
  portEXIT_CRITICAL(&_stm32UpdateControlMux);

  publishStm32UpdateControlStatus(op, queued ? BLE_STM32_UPDATE_RESULT_QUEUED
                                             : BLE_STM32_UPDATE_RESULT_BUSY);
}

void BleManager::publishStm32UpdateControlStatus(uint8_t op, uint8_t result, bool notify) {
  if (!_pStm32UpdateControlChar) {
    return;
  }

  uint8_t status[BLE_STM32_UPDATE_CONTROL_STATUS_MAX_SIZE];
  memset(status, 0, sizeof(status));
  status[0] = BLE_STM32_UPDATE_CONTROL_PROTOCOL_VERSION;
  status[1] = op;
  status[2] = result;

  const char* ssid = "";
  const char* password = "";
  const char* token = "";
  const char* ip = "";

  if (_pOtaManager) {
    const bool stm32Purpose =
      _pOtaManager->getPortalPurpose() == OtaPortalPurpose::Stm32Update;
    status[3] = (uint8_t)_pOtaManager->getState();
    status[4] = _pOtaManager->getProgress();
    if (stm32Purpose && _pOtaManager->isPortalActive()) {
      status[5] |= BLE_STM32_UPDATE_FLAG_PORTAL_ACTIVE;
    }
    if (_pOtaManager->isUpdating()) {
      status[5] |= BLE_STM32_UPDATE_FLAG_UPDATING;
    }
    if (stm32Purpose && _pOtaManager->getPortalToken()[0] != '\0') {
      status[5] |= BLE_STM32_UPDATE_FLAG_HAS_CREDENTIALS;
      ssid = _pOtaManager->getPortalSsid();
      password = _pOtaManager->getPortalPassword();
      token = _pOtaManager->getPortalToken();
      ip = _pOtaManager->getPortalIpString();
    }
    if (stm32Purpose && _pOtaManager->getLastError()[0] != '\0') {
      status[5] |= BLE_STM32_UPDATE_FLAG_ERROR;
    }
    if (updateCoordinator.purpose() == ulsa_update::Purpose::Stm32Update) {
      if (updateCoordinator.physicalAuthorizationRequired()) {
        status[5] |= BLE_STM32_UPDATE_FLAG_PHYSICAL_AUTH_REQUIRED;
      }
      if (updateCoordinator.physicalAuthorizationGranted()) {
        status[5] |= BLE_STM32_UPDATE_FLAG_PHYSICAL_AUTH_GRANTED;
      }
    }
    if (_pOtaManager->isRecoveryPortal()) {
      status[5] |= BLE_STM32_UPDATE_FLAG_RECOVERY_PORTAL;
    }

    putU32LE(status, 6, (uint32_t)_pOtaManager->getUploadedBytes());
    putU32LE(status, 10, (uint32_t)_pOtaManager->getTotalBytes());
    const uint32_t authorizationRemaining =
      updateCoordinator.purpose() == ulsa_update::Purpose::Stm32Update
        ? updateCoordinator.remainingSeconds(millis()) : 0U;
    putU32LE(status, 14, authorizationRemaining > 0U
      ? authorizationRemaining : _pOtaManager->getPortalRemainingSeconds());
    status[18] = _pOtaManager->getNodeId();
  }

  size_t offset = BLE_STM32_UPDATE_CONTROL_STATUS_HEADER_SIZE;
  const uint8_t ssidLen = copyStatusString(status, offset, sizeof(status), ssid);
  const uint8_t passwordLen = copyStatusString(status, offset, sizeof(status), password);
  const uint8_t tokenLen = copyStatusString(status, offset, sizeof(status), token);
  const uint8_t ipLen = copyStatusString(status, offset, sizeof(status), ip);
  status[19] = ssidLen;
  status[20] = passwordLen;
  status[21] = tokenLen;
  status[22] = ipLen;

  memcpy(_stm32UpdateControlLastStatus, status, offset);
  _stm32UpdateControlLastStatusSize = offset;
  _pStm32UpdateControlChar->setValue(
    _stm32UpdateControlLastStatus,
    _stm32UpdateControlLastStatusSize);
  if (notify && _connectionCount > 0) {
    _pStm32UpdateControlChar->notify();
  }
}
