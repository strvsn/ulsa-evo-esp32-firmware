/**
 * @file ble_ota_control.cpp
 * @brief BLE経由のESP32 OTAポータル制御
 */

#include "ble_manager.h"
#include "ota_manager.h"
#include "system/update_coordinator.h"
#include <string.h>

extern ulsa_update::UpdateCoordinator updateCoordinator;

static_assert(BLE_OTA_CONTROL_STATUS_HEADER_SIZE +
                (sizeof(PORTAL_SSID_PREFIX) - 1) +
                PORTAL_SESSION_SUFFIX_LENGTH +
                PORTAL_PASSWORD_LENGTH +
                PORTAL_TOKEN_LENGTH +
                15 <= BLE_OTA_CONTROL_STATUS_MAX_SIZE,  // max IPv4 string length
              "BLE OTA status payload is too small for SoftAP credentials");

static bool otaOpIsValid(uint8_t op) {
  return op == BLE_OTA_OP_READ ||
         op == BLE_OTA_OP_PREPARE_PORTAL ||
         op == BLE_OTA_OP_ACTIVATE_PORTAL ||
         op == BLE_OTA_OP_STOP_OR_CANCEL;
}

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

void BleManager::setOtaManager(OtaManager* manager) {
  _pOtaManager = manager;
  publishOtaControlStatus(BLE_OTA_OP_READ,
                          manager ? BLE_OTA_RESULT_OK
                                  : BLE_OTA_RESULT_UNAVAILABLE,
                          false);
  publishStm32UpdateControlStatus(
    BLE_STM32_UPDATE_OP_READ,
    manager ? BLE_STM32_UPDATE_RESULT_OK
            : BLE_STM32_UPDATE_RESULT_UNAVAILABLE,
    false);
}

void BleManager::clearOtaControlRequest() {
  portENTER_CRITICAL(&_otaControlMux);
  _otaControlRequestPending = false;
  _otaControlRequestOp = 0;
  _otaControlRequestPeerHandle = 0xffffU;
  portEXIT_CRITICAL(&_otaControlMux);
}

bool BleManager::consumeOtaControlRequest(uint8_t& op, uint16_t& peerHandle) {
  op = 0;
  peerHandle = 0xffffU;
  bool hasRequest = false;

  portENTER_CRITICAL(&_otaControlMux);
  if (_otaControlRequestPending) {
    op = _otaControlRequestOp;
    peerHandle = _otaControlRequestPeerHandle;
    _otaControlRequestPending = false;
    _otaControlRequestOp = 0;
    _otaControlRequestPeerHandle = 0xffffU;
    hasRequest = true;
  }
  portEXIT_CRITICAL(&_otaControlMux);

  return hasRequest;
}

void BleManager::onOtaControlRead() {
  const uint8_t result =
    updateCoordinator.purpose() == ulsa_update::Purpose::Esp32Ota &&
    updateCoordinator.authorizationExpired()
      ? BLE_OTA_RESULT_AUTHORIZATION_EXPIRED
      : (updateCoordinator.purpose() == ulsa_update::Purpose::Esp32Ota &&
         updateCoordinator.physicalAuthorizationRequired()
           ? BLE_OTA_RESULT_AUTHORIZATION_REQUIRED
           : (_pOtaManager ? BLE_OTA_RESULT_OK : BLE_OTA_RESULT_UNAVAILABLE));
  publishOtaControlStatus(BLE_OTA_OP_READ,
                          result,
                          false);
}

void BleManager::onOtaControlWrite(const uint8_t* data, size_t length,
                                   uint16_t peerHandle) {
  if (!_running || !_pOtaControlChar) {
    return;
  }

  uint8_t op = (data != nullptr && length > 0) ? data[0] : BLE_OTA_OP_READ;
  if (data == nullptr || length != 1) {
    publishOtaControlStatus(op, BLE_OTA_RESULT_INVALID_LENGTH);
    return;
  }

  if (!otaOpIsValid(op)) {
    publishOtaControlStatus(op, BLE_OTA_RESULT_INVALID_OP);
    return;
  }

  if (_pOtaManager == nullptr) {
    publishOtaControlStatus(op, BLE_OTA_RESULT_UNAVAILABLE);
    return;
  }

  if (op == BLE_OTA_OP_READ) {
    onOtaControlRead();
    return;
  }

  bool queued = false;
  portENTER_CRITICAL(&_otaControlMux);
  if (!_otaControlRequestPending) {
    _otaControlRequestPending = true;
    _otaControlRequestOp = op;
    _otaControlRequestPeerHandle = peerHandle;
    queued = true;
  }
  portEXIT_CRITICAL(&_otaControlMux);

  publishOtaControlStatus(op, queued ? BLE_OTA_RESULT_QUEUED
                                     : BLE_OTA_RESULT_BUSY);
}

void BleManager::publishOtaControlStatus(uint8_t op, uint8_t result, bool notify) {
  if (!_pOtaControlChar) {
    return;
  }

  uint8_t status[BLE_OTA_CONTROL_STATUS_MAX_SIZE];
  memset(status, 0, sizeof(status));
  status[0] = BLE_OTA_CONTROL_PROTOCOL_VERSION;
  status[1] = op;
  status[2] = result;

  const char* ssid = "";
  const char* password = "";
  const char* token = "";
  const char* ip = "";

  if (_pOtaManager) {
    status[3] = (uint8_t)_pOtaManager->getState();
    status[4] = _pOtaManager->getProgress();
    if (_pOtaManager->isPortalActive()) {
      status[5] |= BLE_OTA_FLAG_PORTAL_ACTIVE;
    }
    if (_pOtaManager->isUpdating()) {
      status[5] |= BLE_OTA_FLAG_UPDATING;
    }
    if (_pOtaManager->getPortalToken()[0] != '\0') {
      status[5] |= BLE_OTA_FLAG_HAS_CREDENTIALS;
    }
    if (_pOtaManager->getLastError()[0] != '\0') {
      status[5] |= BLE_OTA_FLAG_ERROR;
    }
    if (updateCoordinator.purpose() == ulsa_update::Purpose::Esp32Ota) {
      if (updateCoordinator.physicalAuthorizationRequired()) {
        status[5] |= BLE_OTA_FLAG_PHYSICAL_AUTH_REQUIRED;
      }
      if (updateCoordinator.physicalAuthorizationGranted()) {
        status[5] |= BLE_OTA_FLAG_PHYSICAL_AUTH_GRANTED;
      }
    }
    if (_pOtaManager->isRecoveryPortal()) {
      status[5] |= BLE_OTA_FLAG_RECOVERY_PORTAL;
    }

    putU32LE(status, 6, (uint32_t)_pOtaManager->getUploadedBytes());
    putU32LE(status, 10, (uint32_t)_pOtaManager->getTotalBytes());
    const uint32_t authorizationRemaining =
      updateCoordinator.purpose() == ulsa_update::Purpose::Esp32Ota
        ? updateCoordinator.remainingSeconds(millis()) : 0U;
    putU32LE(status, 14, authorizationRemaining > 0U
      ? authorizationRemaining : _pOtaManager->getPortalRemainingSeconds());
    status[18] = _pOtaManager->getNodeId();
    ssid = _pOtaManager->getPortalSsid();
    password = _pOtaManager->getPortalPassword();
    token = _pOtaManager->getPortalToken();
    ip = _pOtaManager->getPortalIpString();
  }

  size_t offset = BLE_OTA_CONTROL_STATUS_HEADER_SIZE;
  const uint8_t ssidLen = copyStatusString(status, offset, sizeof(status), ssid);
  const uint8_t passwordLen = copyStatusString(status, offset, sizeof(status), password);
  const uint8_t tokenLen = copyStatusString(status, offset, sizeof(status), token);
  const uint8_t ipLen = copyStatusString(status, offset, sizeof(status), ip);
  status[19] = ssidLen;
  status[20] = passwordLen;
  status[21] = tokenLen;
  status[22] = ipLen;

  memcpy(_otaControlLastStatus, status, offset);
  _otaControlLastStatusSize = offset;
  _pOtaControlChar->setValue(_otaControlLastStatus, _otaControlLastStatusSize);
  if (notify && _connectionCount > 0) {
    _pOtaControlChar->notify();
  }
}
