/**
 * @file ota_manager.cpp
 * @brief WiFi OTAアップデートモジュール - コアロジック
 * @date 2025-12-07
 * 
 * OtaManagerクラスの初期化・SoftAP Web OTA状態処理
 * ※ポータル lifecycle は ota_arduino.cpp に分離（legacy filename）
 * ※HTMLテンプレートは web_portal_html.cpp に分離
 * ※ポータルハンドラは web_portal_handlers.cpp に分離
 */

#include "ota_manager.h"
#include "debug_config.h"
#include "sensor/ulsa_evo_i2c_client.h"
#include "system/firmware_identity.h"
#include "web_portal_html.h"
#include <M5Unified.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <string.h>

// ============================================
// コンストラクタ
// ============================================
OtaManager::OtaManager()
  : _state(OTA_STATE_DISABLED)
  , _nodeId(0)
  , _progress(0)
  , _initialized(false)
  , _portalActive(false)
  , _portalStartTime(0)
  , _uploadedBytes(0)
  , _totalBytes(0)
  , _lastProgressPercent(0)
  , _webOtaRejected(false)
  , _stm32UploadAccepted(false)
  , _stm32UploadRejected(false)
  , _stm32UploadRejectStatus(0)
  , _initialImageShaActive(false)
  , _appDrivenPortal(false)
  , _recoveryPortal(false)
  , _initialSetupPortal(false)
  , _initialSessionClaimed(false)
  , _initialExpectedRevision(0)
  , _portalPurpose(OtaPortalPurpose::Esp32Ota)
  , _stm32UpdateSessionBound(false)
  , _stm32UpdateSessionHasExpectedNodeId(false)
  , _stm32UpdateSessionExpectedNodeId(0)
  , _watchdogDetachedForWebOta(false)
  , _pStm32Bootloader(nullptr)
  , _pI2cClient(nullptr)
  , _pWebServer(nullptr)
  , _pDnsServer(nullptr)
  , onStartCallback(nullptr)
  , onEndCallback(nullptr)
  , onProgressCallback(nullptr)
  , onErrorCallback(nullptr) {
  memset(_portalSsid, 0, sizeof(_portalSsid));
  memset(_portalPassword, 0, sizeof(_portalPassword));
  memset(_portalIpString, 0, sizeof(_portalIpString));
  memset(_portalToken, 0, sizeof(_portalToken));
  memset(_lastPortalSessionSuffix, 0, sizeof(_lastPortalSessionSuffix));
  memset(_lastError, 0, sizeof(_lastError));
  memset(_initialClientNonce, 0, sizeof(_initialClientNonce));
  memset(_initialExpectedSha256, 0, sizeof(_initialExpectedSha256));
  memset(_initialExpectedVersion, 0, sizeof(_initialExpectedVersion));
  memset(_initialExpectedCommit, 0, sizeof(_initialExpectedCommit));
  memset(_stm32UpdateSessionTarget, 0, sizeof(_stm32UpdateSessionTarget));
  memset(_stm32UpdateSessionReleaseTag, 0, sizeof(_stm32UpdateSessionReleaseTag));
  mbedtls_sha256_init(&_initialImageShaContext);
}

// ============================================
// 初期化
// ============================================
void OtaManager::begin(uint8_t nodeId,
                       STM32Bootloader* stm32Bootloader,
                       UlsaEvoI2cClient* i2cClient) {
  _nodeId = nodeId;
  _pStm32Bootloader = stm32Bootloader;
  _pI2cClient = i2cClient;
  updatePortalIdentity();
  snprintf(_portalIpString, sizeof(_portalIpString), "%d.%d.%d.%d", PORTAL_IP);
  _stm32UpdateManager.setI2cClient(i2cClient);
  _stm32UpdateManager.begin();
  
  _state = OTA_STATE_WIFI_DISCONNECTED;
  _initialized = true;
}

void OtaManager::updatePortalIdentity() {
  if (_initialSetupPortal) {
    strncpy(_portalSsid, INITIAL_SETUP_SSID, sizeof(_portalSsid) - 1);
    _portalSsid[sizeof(_portalSsid) - 1] = '\0';
    return;
  }
  const char* prefix = PORTAL_SSID_PREFIX;
  if (_recoveryPortal) {
    prefix = RECOVERY_ESP32_SSID_PREFIX;
  }
  const char* suffix = _portalToken[0] != '\0'
    ? _portalToken
    : (_lastPortalSessionSuffix[0] != '\0' ? _lastPortalSessionSuffix : "000");
  snprintf(_portalSsid, sizeof(_portalSsid), "%s%.*s",
           prefix, PORTAL_SESSION_SUFFIX_LENGTH, suffix);
}

bool OtaManager::updateNodeId(uint8_t nodeId) {
  if (nodeId == _nodeId) {
    return true;
  }

  if (_portalActive || isUpdating() || _portalToken[0] != '\0') {
    return false;
  }

  _nodeId = nodeId;
  updatePortalIdentity();
  return true;
}

bool OtaManager::refreshNodeIdentityFromI2c() {
  if (!_initialized || _pI2cClient == nullptr || isNodeLabelFrozen()) {
    return false;
  }

  uint8_t nodeId = 0;
  if (!_pI2cClient->readNodeId(nodeId)) {
    return false;
  }

  return updateNodeId(nodeId);
}

bool OtaManager::isNodeLabelFrozen() const {
  return _portalActive || isUpdating() || _portalToken[0] != '\0';
}

// ============================================
// OTA処理
// ============================================
void OtaManager::update() {
  // ポータルモード処理（Web OTA）
  if (_portalActive) {
    if (_pDnsServer) {
      _pDnsServer->processNextRequest();
    }
    if (_pWebServer) {
      _pWebServer->handleClient();
    }

    // `/stm32/write` first reserves an immutable package context and returns
    // HTTP 202. Start the RTOS writer only after that response had a grace
    // window to leave the TCP stack.
    if (_stm32WriteStartDeadline.pending()) {
      if (_stm32WriteStartDeadline.due(millis())) {
        _stm32WriteStartDeadline.clear();
        (void)_stm32UpdateManager.startPendingWrite();
      }
      return;
    }

    // A successful cancel or terminal STM32 status response must leave the TCP
    // stack before SoftAP and its WebServer are destroyed. A pending grace also
    // wins over the generic five-minute timeout in this loop.
    if (_portalStopDeadline.pending()) {
      if (_portalStopDeadline.due(millis())) {
        _portalStopDeadline.clear();
        stopPortal();
      }
      return;
    }
    
    // STM32 pre-write work may be abandoned by a broken HTTP client. Let the
    // portal deadline discard those phases; only an active writer is durable
    // recovery state and must suppress automatic portal shutdown.
    const bool stm32PortalHasActiveWriter =
      _portalPurpose == OtaPortalPurpose::Stm32Update &&
      stm32_update::updatePhaseHasActiveWriter(_stm32UpdateManager.getPhase());

    // タイムアウトチェック
    if (!isUpdating() &&
        (!_stm32UpdateManager.isBusy() ||
         (_portalPurpose == OtaPortalPurpose::Stm32Update &&
          !stm32PortalHasActiveWriter)) &&
        millis() - _portalStartTime > PORTAL_TIMEOUT * 1000) {
      // LOG_OTA("Portal timeout, stopping...");
      stopPortal();
    }
  }
}

void OtaManager::requestPortalStopAfterResponse() {
  if (!_portalActive || isUpdating()) return;
  _portalStopDeadline.schedule(millis(), PORTAL_RESPONSE_GRACE_MS);
}

void OtaManager::requestStm32WriteStartAfterResponse() {
  if (!_portalActive ||
      _portalPurpose != OtaPortalPurpose::Stm32Update ||
      !_stm32UpdateManager.hasPendingWriteStart()) {
    return;
  }
  _stm32WriteStartDeadline.schedule(
    millis(), STM32_WRITE_RESPONSE_GRACE_MS);
}

// ============================================
// 状態取得
// ============================================
OtaState OtaManager::getState() const {
  return _state;
}

const char* OtaManager::getStateString() const {
  switch (_state) {
    case OTA_STATE_DISABLED:          return "Disabled";
    case OTA_STATE_WIFI_DISCONNECTED: return "WiFi Disconnected";
    case OTA_STATE_RESERVED_STA_CONNECTING: return "Reserved";
    case OTA_STATE_PORTAL_ACTIVE:     return "Portal Active";
    case OTA_STATE_RESERVED_LAN_READY: return "Reserved";
    case OTA_STATE_RESERVED_LAN_UPDATING: return "Reserved";
    case OTA_STATE_WEB_OTA_UPDATING:  return "Web OTA Updating";
    case OTA_STATE_ERROR:             return "Error";
    default:                          return "Unknown";
  }
}

bool OtaManager::isUpdating() const {
  return _state == OTA_STATE_WEB_OTA_UPDATING;
}

uint8_t OtaManager::getProgress() const {
  return _progress;
}

size_t OtaManager::getUploadedBytes() const {
  return _uploadedBytes;
}

size_t OtaManager::getTotalBytes() const {
  return _totalBytes;
}

uint32_t OtaManager::getPortalRemainingSeconds() const {
  if (!_portalActive) {
    return 0;
  }

  const uint32_t elapsedSec = (millis() - _portalStartTime) / 1000;
  if (elapsedSec >= PORTAL_TIMEOUT) {
    return 0;
  }

  return PORTAL_TIMEOUT - elapsedSec;
}

const char* OtaManager::getLastError() const {
  return _lastError;
}

const char* OtaManager::getPortalToken() const {
  return _portalToken;
}

const char* OtaManager::getPortalSsid() const {
  return _portalSsid;
}

const char* OtaManager::getPortalPassword() const {
  return _portalPassword[0] == '\0' ? PORTAL_PASSWORD : _portalPassword;
}

const char* OtaManager::getPortalIpString() const {
  return _portalIpString;
}

bool OtaManager::isStm32BootloaderSessionActive() const {
  return _stm32UpdateManager.isBootloaderSessionActive();
}

bool OtaManager::isPortalTokenValid(const String& token) const {
  return _portalToken[0] != '\0' && token.equals(_portalToken);
}

bool OtaManager::preparePortalSession(OtaPortalPurpose purpose) {
  if (!_initialized || _portalActive || isUpdating()) {
    return false;
  }

  // Best-effort label refresh only. Node ID is not an OTA identity or gate.
  (void)refreshNodeIdentityFromI2c();

  clearLastError();
  resetWebOtaProgress();
  clearStm32UpdateSession();
  generatePortalToken();
  generatePortalPassword();
  _portalPurpose = purpose;
  _appDrivenPortal = true;
  _recoveryPortal = false;
  _initialSetupPortal = false;
  _initialSessionClaimed = false;
  updatePortalIdentity();
  snprintf(_portalIpString, sizeof(_portalIpString), "%d.%d.%d.%d", PORTAL_IP);
  return true;
}

bool OtaManager::prepareRecoveryPortalSession(OtaPortalPurpose purpose) {
  if (!_initialized || _portalActive || isUpdating() ||
      purpose != OtaPortalPurpose::Esp32Ota) {
    return false;
  }
  // Recovery must remain available even when the user label cannot be read.
  (void)refreshNodeIdentityFromI2c();

  clearLastError();
  resetWebOtaProgress();
  clearStm32UpdateSession();
  generatePortalToken();
  memset(_portalPassword, 0, sizeof(_portalPassword));
  _portalPurpose = purpose;
  _appDrivenPortal = false;
  _recoveryPortal = true;
  _initialSetupPortal = false;
  _initialSessionClaimed = false;
  updatePortalIdentity();
  snprintf(_portalIpString, sizeof(_portalIpString), "%d.%d.%d.%d", PORTAL_IP);
  return true;
}

bool OtaManager::prepareInitialDemoSession() {
  if (!_initialized || _portalActive || isUpdating()) {
    return false;
  }

  clearLastError();
  resetWebOtaProgress();
  clearStm32UpdateSession();
  generatePortalToken();
  strncpy(_portalPassword, INITIAL_SETUP_PASSWORD, sizeof(_portalPassword) - 1);
  _portalPassword[sizeof(_portalPassword) - 1] = '\0';
  _portalPurpose = OtaPortalPurpose::Esp32Ota;
  _appDrivenPortal = true;
  _recoveryPortal = false;
  _initialSetupPortal = true;
  _initialSessionClaimed = false;
  memset(_initialClientNonce, 0, sizeof(_initialClientNonce));
  memset(_initialExpectedSha256, 0, sizeof(_initialExpectedSha256));
  memset(_initialExpectedVersion, 0, sizeof(_initialExpectedVersion));
  _initialExpectedRevision = 0;
  memset(_initialExpectedCommit, 0, sizeof(_initialExpectedCommit));
  updatePortalIdentity();
  snprintf(_portalIpString, sizeof(_portalIpString), "%d.%d.%d.%d", PORTAL_IP);
  return true;
}

bool OtaManager::claimInitialDemoSession(const char* clientNonce,
                                         const char* artifactSha256,
                                         const char* version,
                                         uint32_t revision,
                                         const char* commit) {
  if (!_portalActive || !_initialSetupPortal || isUpdating() ||
      clientNonce == nullptr || artifactSha256 == nullptr ||
      version == nullptr || commit == nullptr) {
    return false;
  }

  if (_initialSessionClaimed) {
    return strcmp(_initialClientNonce, clientNonce) == 0 &&
           strcmp(_initialExpectedSha256, artifactSha256) == 0 &&
           strcmp(_initialExpectedVersion, version) == 0 &&
           _initialExpectedRevision == revision &&
           strcmp(_initialExpectedCommit, commit) == 0;
  }

  strncpy(_initialClientNonce, clientNonce, sizeof(_initialClientNonce) - 1);
  strncpy(_initialExpectedSha256, artifactSha256,
          sizeof(_initialExpectedSha256) - 1);
  strncpy(_initialExpectedVersion, version,
          sizeof(_initialExpectedVersion) - 1);
  _initialExpectedRevision = revision;
  strncpy(_initialExpectedCommit, commit, sizeof(_initialExpectedCommit) - 1);
  _initialSessionClaimed = true;
  return true;
}

bool OtaManager::prepareStm32UpdateSession(bool hasNodeLabel,
                                           uint8_t nodeLabel,
                                           const char* target,
                                           const char* releaseTag) {
  if (!_initialized || _portalActive || isUpdating()) {
    setLastError("STM32 update is busy");
    return false;
  }
  if (target == nullptr || releaseTag == nullptr ||
      target[0] == '\0' || releaseTag[0] == '\0' ||
      strlen(target) > STM32_UPDATE_SESSION_TARGET_MAX_LEN ||
      strlen(releaseTag) > STM32_UPDATE_SESSION_RELEASE_TAG_MAX_LEN) {
    setLastError("Invalid STM32 update session binding");
    return false;
  }

  if (!preparePortalSession(OtaPortalPurpose::Stm32Update)) {
    setLastError("STM32 update is busy");
    return false;
  }

  _stm32UpdateManager.resetCompletedResultForNewSession();
  _stm32UpdateSessionBound = true;
  // Preserve the legacy fields as an informational request-label echo only.
  _stm32UpdateSessionHasExpectedNodeId = hasNodeLabel;
  _stm32UpdateSessionExpectedNodeId = nodeLabel;
  strncpy(_stm32UpdateSessionTarget, target, sizeof(_stm32UpdateSessionTarget) - 1);
  strncpy(_stm32UpdateSessionReleaseTag, releaseTag, sizeof(_stm32UpdateSessionReleaseTag) - 1);
  return true;
}

void OtaManager::cancelPortalSession() {
  if (_portalActive || isUpdating()) {
    return;
  }

  resetWebOtaProgress();
  clearPortalSession();
}

void OtaManager::resetWebOtaProgress() {
  _progress = 0;
  _uploadedBytes = 0;
  _totalBytes = 0;
  _lastProgressPercent = 0;
  _webOtaRejected = false;
  abortInitialImageValidation();
}

bool OtaManager::beginInitialImageValidation() {
  if (!_initialSetupPortal || !_initialSessionClaimed) return true;
  abortInitialImageValidation();
  if (mbedtls_sha256_starts_ret(&_initialImageShaContext, 0) != 0) {
    setLastError("Firmware SHA-256 initialization failed");
    return false;
  }
  _initialImageValidator.begin(_initialExpectedVersion,
                               _initialExpectedRevision,
                               _initialExpectedCommit);
  _initialImageShaActive = true;
  return true;
}

bool OtaManager::feedInitialImageValidation(const uint8_t* data, size_t length) {
  if (!_initialSetupPortal) return true;
  if (!_initialImageShaActive || data == nullptr || length == 0) return false;
  if (mbedtls_sha256_update_ret(&_initialImageShaContext, data, length) != 0) {
    setLastError("Firmware SHA-256 update failed");
    return false;
  }
  _initialImageValidator.feed(data, length);
  return _initialImageValidator.error() == nullptr;
}

bool OtaManager::finishInitialImageValidation() {
  if (!_initialSetupPortal) return true;
  if (!_initialImageShaActive) return false;

  uint8_t digest[32];
  if (mbedtls_sha256_finish_ret(&_initialImageShaContext, digest) != 0) {
    abortInitialImageValidation();
    setLastError("Firmware SHA-256 finalization failed");
    return false;
  }
  _initialImageShaActive = false;

  static const char hex[] = "0123456789abcdef";
  char actual[INITIAL_SESSION_SHA256_HEX_LENGTH + 1];
  uint8_t mismatch = 0;
  for (size_t i = 0; i < sizeof(digest); ++i) {
    actual[i * 2] = hex[digest[i] >> 4];
    actual[i * 2 + 1] = hex[digest[i] & 0x0f];
  }
  actual[INITIAL_SESSION_SHA256_HEX_LENGTH] = '\0';
  for (size_t i = 0; i < INITIAL_SESSION_SHA256_HEX_LENGTH; ++i) {
    mismatch |= (uint8_t)(actual[i] ^ _initialExpectedSha256[i]);
  }
  if (mismatch != 0U) {
    setLastError("Demo firmware SHA-256 mismatch");
    return false;
  }
  if (!_initialImageValidator.finish()) {
    setLastError(_initialImageValidator.error());
    return false;
  }
  return true;
}

void OtaManager::abortInitialImageValidation() {
  if (_initialImageShaActive) {
    mbedtls_sha256_free(&_initialImageShaContext);
    mbedtls_sha256_init(&_initialImageShaContext);
  }
  _initialImageShaActive = false;
}

void OtaManager::generatePortalToken() {
  static const char hex[] = "0123456789abcdef";

  do {
    for (uint8_t i = 0; i < PORTAL_TOKEN_LENGTH / 2; ++i) {
      const uint8_t value = (uint8_t)(esp_random() & 0xFF);
      _portalToken[i * 2] = hex[value >> 4];
      _portalToken[i * 2 + 1] = hex[value & 0x0F];
    }
    _portalToken[PORTAL_TOKEN_LENGTH] = '\0';
  } while (_lastPortalSessionSuffix[0] != '\0' &&
           strncmp(_portalToken, _lastPortalSessionSuffix,
                   PORTAL_SESSION_SUFFIX_LENGTH) == 0);

  memcpy(_lastPortalSessionSuffix, _portalToken, PORTAL_SESSION_SUFFIX_LENGTH);
  _lastPortalSessionSuffix[PORTAL_SESSION_SUFFIX_LENGTH] = '\0';
}

void OtaManager::generatePortalPassword() {
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  const size_t alphabetLen = sizeof(alphabet) - 1;

  for (uint8_t i = 0; i < PORTAL_PASSWORD_LENGTH; ++i) {
    _portalPassword[i] = alphabet[esp_random() % alphabetLen];
  }
  _portalPassword[PORTAL_PASSWORD_LENGTH] = '\0';
}

void OtaManager::clearPortalSession() {
  memset(_portalToken, 0, sizeof(_portalToken));
  memset(_portalPassword, 0, sizeof(_portalPassword));
  clearStm32UpdateSession();
  _appDrivenPortal = false;
  _recoveryPortal = false;
  _initialSetupPortal = false;
  _initialSessionClaimed = false;
  memset(_initialClientNonce, 0, sizeof(_initialClientNonce));
  memset(_initialExpectedSha256, 0, sizeof(_initialExpectedSha256));
  memset(_initialExpectedVersion, 0, sizeof(_initialExpectedVersion));
  _initialExpectedRevision = 0;
  memset(_initialExpectedCommit, 0, sizeof(_initialExpectedCommit));
  // Keep the last purpose so a terminal error remains attributable to the
  // correct ESP32/STM32 presentation until the next prepared session.
  updatePortalIdentity();
}

void OtaManager::clearStm32UpdateSession() {
  _stm32UpdateSessionBound = false;
  _stm32UpdateSessionHasExpectedNodeId = false;
  _stm32UpdateSessionExpectedNodeId = 0;
  memset(_stm32UpdateSessionTarget, 0, sizeof(_stm32UpdateSessionTarget));
  memset(_stm32UpdateSessionReleaseTag, 0, sizeof(_stm32UpdateSessionReleaseTag));
}

const char* OtaManager::getPortalPurposeString() const {
  switch (_portalPurpose) {
    case OtaPortalPurpose::Stm32Update:
      return "stm32_update";
    case OtaPortalPurpose::Esp32Ota:
    default:
      return "esp32_ota";
  }
}

void OtaManager::setLastError(const char* error) {
  if (error == nullptr) {
    clearLastError();
    return;
  }

  strncpy(_lastError, error, sizeof(_lastError) - 1);
  _lastError[sizeof(_lastError) - 1] = '\0';
}

void OtaManager::clearLastError() {
  memset(_lastError, 0, sizeof(_lastError));
}

void OtaManager::detachWatchdogForWebOta() {
  if (_watchdogDetachedForWebOta) {
    return;
  }

  if (esp_task_wdt_delete(NULL) == ESP_OK) {
    _watchdogDetachedForWebOta = true;
  }
}

void OtaManager::restoreWatchdogAfterWebOta() {
  if (!_watchdogDetachedForWebOta) {
    return;
  }

  if (esp_task_wdt_add(NULL) == ESP_OK) {
    _watchdogDetachedForWebOta = false;
  }
}

static void appendJsonString(String& json, const char* value) {
  json += '"';
  for (const char* p = value; p != nullptr && *p != '\0'; ++p) {
    switch (*p) {
      case '\\':
      case '"':
        json += '\\';
        json += *p;
        break;
      case '\n':
        json += "\\n";
        break;
      case '\r':
        json += "\\r";
        break;
      default:
        json += *p;
        break;
    }
  }
  json += '"';
}

String OtaManager::buildPortalStatusJson() const {
  String json;
  json.reserve(320);
  json += "{\"state\":";
  appendJsonString(json, getStateString());
  json += ",\"portalActive\":";
  json += _portalActive ? "true" : "false";
  json += ",\"updating\":";
  json += isUpdating() ? "true" : "false";
  json += ",\"appDriven\":";
  json += _appDrivenPortal ? "true" : "false";
  json += ",\"recoveryPortal\":";
  json += _recoveryPortal ? "true" : "false";
  json += ",\"initialSetup\":";
  json += _initialSetupPortal ? "true" : "false";
  json += ",\"sessionOrigin\":";
  appendJsonString(json, _initialSetupPortal
    ? "initial_setup" : (_recoveryPortal ? "recovery" : "ble"));
  json += ",\"targetProfile\":";
  if (_initialSetupPortal) appendJsonString(json, "demo");
  else json += "null";
  json += ",\"purpose\":";
  appendJsonString(json, getPortalPurposeString());
  json += ",\"nodeId\":";
  json += String(_nodeId);
  json += ",\"ssid\":";
  appendJsonString(json, _portalSsid);
  json += ",\"progress\":";
  json += String(_progress);
  json += ",\"uploadedBytes\":";
  json += String((uint32_t)_uploadedBytes);
  json += ",\"totalBytes\":";
  json += String((uint32_t)_totalBytes);
  json += ",\"remainingSeconds\":";
  json += String(getPortalRemainingSeconds());
  json += ",\"firmwareVersion\":";
  appendJsonString(json, getPortalFirmwareVersion());
  const Esp32FirmwareIdentityDescriptor& firmwareIdentity = getEsp32FirmwareIdentity();
  json += ",\"firmwareVersionCode\":";
  json += String(firmwareIdentity.versionCode);
  json += ",\"firmwareRevision\":";
  json += String(firmwareIdentity.revision);
  json += ",\"firmwareCommit\":";
  appendJsonString(json, firmwareIdentity.sourceCommit);
  json += ",\"firmwareProfile\":";
  appendJsonString(json,
    firmwareIdentity.profileCode == ULSA_EVO_ESP32_PROFILE_DEMO
      ? "demo" : "initial");
  json += ",\"firmwareDirty\":";
  json += ((firmwareIdentity.flags & ULSA_EVO_ESP32_IDENTITY_FLAG_DIRTY)
               ? "true"
               : "false");
  json += ",\"firmwareBuildContract\":";
  appendJsonString(json, firmwareIdentity.buildContractSha256);
  json += ",\"error\":";
  if (_lastError[0] == '\0') {
    json += "null";
  } else {
    appendJsonString(json, _lastError);
  }
  json += "}";
  return json;
}

String OtaManager::buildInitialDemoSessionJson() const {
  String json;
  json.reserve(360);
  json += "{\"contractVersion\":1,\"purpose\":\"esp32_ota\",";
  json += "\"sessionOrigin\":\"initial_setup\",\"firmwareProfile\":\"initial\",";
  json += "\"targetProfile\":\"demo\",\"ssid\":";
  appendJsonString(json, _portalSsid);
  json += ",\"password\":";
  appendJsonString(json, getPortalPassword());
  json += ",\"token\":";
  appendJsonString(json, _portalToken);
  json += ",\"ip\":";
  appendJsonString(json, _portalIpString);
  json += ",\"remainingSeconds\":";
  json += String(getPortalRemainingSeconds());
  json += "}";
  return json;
}

bool OtaManager::stm32UpdateSessionMatchesVerifiedPackage() const {
  if (!_stm32UpdateSessionBound) {
    return true;
  }
  return _stm32UpdateManager.verifiedPackageMatches(
    _stm32UpdateSessionTarget,
    _stm32UpdateSessionReleaseTag);
}

String OtaManager::buildStm32UpdateStatusJson() const {
  String json = _stm32UpdateManager.buildStatusJson(_nodeId);
  if (!json.endsWith("}")) {
    return json;
  }

  json.remove(json.length() - 1);
  json += ",\"sessionBound\":";
  json += _stm32UpdateSessionBound ? "true" : "false";
  json += ",\"sessionExpectedNodeId\":";
  if (_stm32UpdateSessionHasExpectedNodeId) {
    json += String(_stm32UpdateSessionExpectedNodeId);
  } else {
    json += "null";
  }
  json += ",\"sessionTarget\":";
  if (_stm32UpdateSessionTarget[0] == '\0') {
    json += "null";
  } else {
    appendJsonString(json, _stm32UpdateSessionTarget);
  }
  json += ",\"sessionReleaseTag\":";
  if (_stm32UpdateSessionReleaseTag[0] == '\0') {
    json += "null";
  } else {
    appendJsonString(json, _stm32UpdateSessionReleaseTag);
  }
  json += "}";
  return json;
}

// SoftAP portal lifecycleはota_arduino.cppに実装（legacy filename）
// setupPortalHandlers()はweb_portal_handlers.cppに実装
