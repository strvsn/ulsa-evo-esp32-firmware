/**
 * @file stm32_update_manager.cpp
 * @brief STM32 update scratch package manager
 */

#include "stm32_update/stm32_update_manager.h"

#include "stm32_update/stm32_bootloader_writer.h"
#include "stm32_update/stm32_secure_bootloader_writer.h"
#include "stm32_update/stm32_target_identity.h"
#include "stm32_update/stm32_package_keys.h"
#include "stm32_update/stm32_package_stream.h"
#include "stm32_update/stm32_version_identity.h"
#include "sensor/ulsa_evo_i2c_client.h"

#include <Preferences.h>
#include <ctype.h>
#include <esp_system.h>
#include <string.h>

namespace stm32_update {
namespace {

static const char STM32_PACKAGE_PARTITION_LABEL[] = "stm32pkg";
static const esp_partition_subtype_t STM32_PACKAGE_PARTITION_SUBTYPE =
  (esp_partition_subtype_t)0x40;
static const char STM32_UPDATE_NVS_NAMESPACE[] = "stm32upd";
static const char STM32_UPDATE_NVS_ANTI_ROLLBACK_KEY[] = "ar_floor";
static const char STM32_UPDATE_NVS_WRITE_STAGE_KEY[] = "wr_stage";
static const char STM32_UPDATE_NVS_WRITE_SHA_KEY[] = "wr_sha";
static const char STM32_UPDATE_NVS_BOOT_COUNT_KEY[] = "boot_count";
static const uint32_t STM32_WRITE_STACK_MIN_FREE_BYTES = 4096;

void appendJsonString(String& json, const char* value) {
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

const char* errorCodeForMessage(const char* error) {
  if (error == nullptr || error[0] == '\0') return nullptr;
  if (strcmp(error, "stm32pkg partition not found") == 0) return "scratch_partition_missing";
  if (strcmp(error, "STM32 update is busy") == 0) return "busy";
  if (strcmp(error, "STM32 package exceeds scratch partition") == 0) return "scratch_overflow";
  if (strcmp(error, "Failed to erase stm32pkg partition") == 0) return "scratch_erase_failed";
  if (strcmp(error, "STM32 package upload is not active") == 0) return "upload_inactive";
  if (strcmp(error, "Failed to write stm32pkg partition") == 0) return "scratch_write_failed";
  if (strcmp(error, "STM32 package upload did not start") == 0) return "upload_not_started";
  if (strcmp(error, "STM32 package is empty") == 0) return "package_empty";
  if (strcmp(error, "STM32 package is not ready for verification") == 0) return "package_not_ready";
  if (strcmp(error, "STM32 writer unavailable") == 0) return "writer_unavailable";
  if (strcmp(error, "STM32 package is not ready to write") == 0) return "package_not_ready";
  if (strcmp(error, "STM32 write already running") == 0) return "write_already_running";
  if (strcmp(error, "STM32 reserved package changed") == 0) return "reserved_package_changed";
  if (strcmp(error, "Failed to start STM32 write task") == 0) return "write_task_start_failed";
  if (strcmp(error, "STM32 recovery marker write failed") == 0) return "recovery_marker_write_failed";
  if (strcmp(error, "STM32 writer stack headroom too low") == 0) return "writer_stack_low";
  if (strcmp(error, "STM32 restart left update state unknown") == 0) return "restart_unknown";
  if (strcmp(error, "STM32 package is not writable") == 0) return "package_not_writable";
  if (strcmp(error, "STM32 package encoding unsupported") == 0) return "package_encoding_unsupported";
  if (strcmp(error, "stm32_revision_rollback") == 0) return "revision_rollback";
  if (strcmp(error, "STM32 write failed") == 0) return "write_failed";
  if (strcmp(error, "target_chip_id_mismatch") == 0) return "target_chip_id_mismatch";
  if (strcmp(error, "bootloader_info_invalid") == 0) return "bootloader_info_invalid";
  if (strcmp(error, "Invalid OTA token") == 0) return "invalid_token";
  if (strcmp(error, "Wrong portal purpose") == 0) return "wrong_portal_purpose";
  if (strcmp(error, "Invalid STM32 package sha256 header") == 0) return "invalid_package_sha256_header";
  if (strcmp(error, "Failed to start STM32 package sha256") == 0) return "package_sha256_start_failed";
  if (strcmp(error, "Failed to update STM32 package sha256") == 0) return "package_sha256_update_failed";
  if (strcmp(error, "Failed to finish STM32 package sha256") == 0) return "package_sha256_finish_failed";
  if (strcmp(error, "Failed to read stored STM32 package for sha256") == 0) return "stored_package_sha256_read_failed";
  if (strcmp(error, "STM32 package sha256 mismatch") == 0) return "package_sha256_mismatch";
  if (strcmp(error, "STM32 package does not match update session") == 0) return "package_session_mismatch";
  if (strcmp(error, "post_write_version_mismatch") == 0) return "post_write_version_mismatch";
  if (strcmp(error, "post_write_version_unconfirmed") == 0) return "post_write_version_unconfirmed";
  if (strcmp(error, "scratch_cleanup_failed") == 0) return "scratch_cleanup_failed";
  return error;
}

bool isHexSha256String(const char* value) {
  if (value == nullptr || value[0] == '\0') return true;
  for (size_t i = 0; i < STM32_PACKAGE_SHA256_SIZE * 2; ++i) {
    if (value[i] == '\0' || !isxdigit((unsigned char)value[i])) {
      return false;
    }
  }
  return value[STM32_PACKAGE_SHA256_SIZE * 2] == '\0';
}

uint8_t parseHexNibble(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  return 0;
}

bool digestMatchesHex(const uint8_t digest[STM32_PACKAGE_SHA256_SIZE], const char* hex) {
  if (!isHexSha256String(hex) || hex == nullptr || hex[0] == '\0') {
    return false;
  }
  uint8_t mismatch = 0;
  for (size_t i = 0; i < STM32_PACKAGE_SHA256_SIZE; ++i) {
    const uint8_t expected = (uint8_t)((parseHexNibble(hex[i * 2]) << 4) |
                                      parseHexNibble(hex[i * 2 + 1]));
    mismatch |= (uint8_t)(digest[i] ^ expected);
  }
  return mismatch == 0;
}

struct PackageVerifyProgressRelay {
  Stm32UpdateProgressCallback callback;
};

void relayPackageVerifyProgress(size_t verifiedBytes,
                                size_t totalBytes,
                                void* context) {
  PackageVerifyProgressRelay* relay =
    static_cast<PackageVerifyProgressRelay*>(context);
  if (relay == nullptr || relay->callback == nullptr) {
    return;
  }
  const uint32_t progress = totalBytes == 0
    ? 0U
    : static_cast<uint32_t>((verifiedBytes * 100UL) / totalBytes);
  relay->callback(static_cast<uint8_t>(progress > 100U ? 100U : progress));
}

}  // namespace

Stm32UpdateManager::Stm32UpdateManager()
  : _partition(nullptr)
  , _phase(UpdatePhase::Idle)
  , _scratchBytes(0)
  , _totalBytesHint(0)
  , _writtenBytes(0)
  , _verifiedBytes(0)
  , _packageVerifiedBytes(0)
  , _packageFileBytes(0)
  , _packagePayloadBytes(0)
  , _packageFirmwareBytes(0)
  , _packageChunkTableOffset(0)
  , _packagePayloadOffset(0)
  , _packageSha256{}
  , _antiRollbackFloor(0)
  , _currentFirmwareVersionRaw(0)
  , _currentFirmwareRevisionRaw(0)
  , _currentFirmwareVersionFloor(0)
  , _espResetReason(0)
  , _espBootCount(0)
  , _writeTaskStackMinFreeBytes(UINT32_MAX)
  , _postWriteVersionCheckAttempts(0)
  , _hasVerifiedPackage(false)
  , _hasExpectedPackageSha256(false)
  , _hasCurrentFirmwareVersion(false)
  , _postWriteVersionCheckComplete(false)
  , _postWriteVersionRead(false)
  , _postWriteVersionMatchesPackage(false)
  , _antiRollbackStoreReady(false)
  , _uploadSha256Active(false)
  , _hasBootloaderSyncResult(false)
  , _lastBootloaderSyncOk(false)
  , _lastBootloaderSyncAttempts(0)
  , _lastBootloaderSyncResponse(0)
  , _bootloaderSessionActive(false)
  , _writeEraseStarted(false)
  , _writeStartPending(false)
  , _recoveryRestartUnknown(false)
  , _recoveryStage(WriteRecoveryStage::None)
  , _writeTaskHandle(nullptr)
  , _writeBootloader(nullptr)
  , _writeSerial(nullptr)
  , _i2cClient(nullptr)
  , _secureFrameBuffer{} {
  memset(_lastError, 0, sizeof(_lastError));
  memset(_lastBootloaderSyncError, 0, sizeof(_lastBootloaderSyncError));
  memset(_expectedPackageSha256Hex, 0, sizeof(_expectedPackageSha256Hex));
  mbedtls_sha256_init(&_uploadSha256);
}

void Stm32UpdateManager::setI2cClient(UlsaEvoI2cClient* client) {
  _i2cClient = client;
  _hasCurrentFirmwareVersion = false;
  _currentFirmwareVersionRaw = 0;
  _currentFirmwareRevisionRaw = 0;
  _currentFirmwareVersionFloor = 0;
}

void Stm32UpdateManager::begin() {
  captureEspBootDiagnostics();
  _partition = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    STM32_PACKAGE_PARTITION_SUBTYPE,
    STM32_PACKAGE_PARTITION_LABEL);
  if (_partition == nullptr) {
    setError("stm32pkg partition not found");
    _phase = UpdatePhase::Error;
    return;
  }
  // Highest-seen revision is enforced before entering the destructive loader.
  // The same revision remains retryable; intentional old-code recovery must be
  // repackaged with a newer FWREV.
  loadAntiRollbackFloor();
  _phase = UpdatePhase::Idle;
  _writeStartPending = false;
  _recoveryRestartUnknown = false;
  _recoveryStage = WriteRecoveryStage::None;
  clearError();
  clearVerifiedPackage();
  clearBootloaderSyncResult();
  resetUploadHashState();
  resetPostWriteVersionCheck();
  refreshCurrentFirmwareVersionFloor();
  restoreStoredPackageIfPresent();
}

void Stm32UpdateManager::resetCompletedResultForNewSession() {
  if (!shouldResetCompletedResultForNewSession(
        _phase, isWriteInProgress() || _writeStartPending)) {
    return;
  }
  // Successful completion already erased scratch and committed the revision.
  // Reset only its in-memory result, never a recovery package or any Flash/NVS.
  _scratchBytes = 0;
  _totalBytesHint = 0;
  _writtenBytes = 0;
  _verifiedBytes = 0;
  clearVerifiedPackage();
  clearBootloaderSyncResult();
  resetUploadHashState();
  resetPostWriteVersionCheck();
  clearError();
  _phase = UpdatePhase::Idle;
}

bool Stm32UpdateManager::isAvailable() const {
  return _partition != nullptr;
}

bool Stm32UpdateManager::isBusy() const {
  return updatePhaseIsBusy(_phase);
}

bool Stm32UpdateManager::canCancel() const {
  return _phase == UpdatePhase::Receiving ||
         _phase == UpdatePhase::PackageStored ||
         _phase == UpdatePhase::ReadyToWrite ||
         _phase == UpdatePhase::Error;
}

bool Stm32UpdateManager::canWrite() const {
  if (!_hasVerifiedPackage || _scratchBytes == 0 || _bootloaderSessionActive || _writeTaskHandle != nullptr) {
    return false;
  }
  return _phase == UpdatePhase::ReadyToWrite ||
         _phase == UpdatePhase::RecoveryRequired;
}

bool Stm32UpdateManager::beginPackageUpload(size_t totalBytesHint,
                                            const char* expectedPackageSha256Hex) {
  if (_partition == nullptr) {
    abortPackageUpload("stm32pkg partition not found");
    return false;
  }
  // A second HTTP upload must never tear down a receiving/verifying/write
  // operation that already owns the manager. The request handler reports 409
  // without changing this state.
  if (!mayStartPackageUpload(
        _phase, _bootloaderSessionActive, _writeTaskHandle != nullptr)) {
    return false;
  }
  // WebServer::clientContentLength() describes the complete multipart HTTP
  // body, including boundaries and part headers. Keep it only as a progress
  // hint; the actual package-byte limit is enforced for every upload chunk.
  if (!isHexSha256String(expectedPackageSha256Hex)) {
    setError("Invalid STM32 package sha256 header");
    return false;
  }

  const esp_err_t eraseResult = esp_partition_erase_range(_partition, 0, _partition->size);
  if (eraseResult != ESP_OK) {
    abortPackageUpload("Failed to erase stm32pkg partition");
    return false;
  }

  // The old package can no longer be recovered after this exact-partition
  // erase. Clear any same-hash write marker before accepting replacement data.
  if (!saveWriteRecoveryMarker(WriteRecoveryStage::None)) {
    _scratchBytes = 0;
    _totalBytesHint = 0;
    clearVerifiedPackage();
    setError("STM32 recovery marker write failed");
    _phase = UpdatePhase::Error;
    return false;
  }

  clearError();
  clearVerifiedPackage();
  clearBootloaderSyncResult();
  resetPostWriteVersionCheck();
  resetUploadHashState();
  _hasExpectedPackageSha256 =
    expectedPackageSha256Hex != nullptr && expectedPackageSha256Hex[0] != '\0';
  if (_hasExpectedPackageSha256) {
    strncpy(_expectedPackageSha256Hex,
            expectedPackageSha256Hex,
            sizeof(_expectedPackageSha256Hex) - 1);
  }
  if (!startUploadHash()) {
    abortPackageUpload("Failed to start STM32 package sha256");
    return false;
  }
  _phase = UpdatePhase::Receiving;
  _scratchBytes = 0;
  _totalBytesHint = totalBytesHint;
  _writtenBytes = 0;
  _verifiedBytes = 0;
  _packageVerifiedBytes = 0;
  _packagePayloadBytes = 0;
  _packageFirmwareBytes = 0;
  _packageChunkTableOffset = 0;
  _packagePayloadOffset = 0;
  return true;
}

bool Stm32UpdateManager::writePackageChunk(const uint8_t* data, size_t length) {
  if (_partition == nullptr || _phase != UpdatePhase::Receiving) {
    if (isWriteInProgress()) {
      return false;
    }
    abortPackageUpload("STM32 package upload is not active");
    return false;
  }
  if (data == nullptr || length == 0) {
    return true;
  }
  if (!packageUploadChunkFits(
        _scratchBytes,
        length,
        _partition->size,
        STM32_PACKAGE_MAX_PACKAGE_SIZE)) {
    abortPackageUpload("STM32 package exceeds scratch partition");
    return false;
  }

  const esp_err_t writeResult = esp_partition_write(_partition, _scratchBytes, data, length);
  if (writeResult != ESP_OK) {
    abortPackageUpload("Failed to write stm32pkg partition");
    return false;
  }
  if (_uploadSha256Active && mbedtls_sha256_update_ret(&_uploadSha256, data, length) != 0) {
    abortPackageUpload("Failed to update STM32 package sha256");
    return false;
  }
  _scratchBytes += length;
  return true;
}

bool Stm32UpdateManager::finishPackageUpload(
    Stm32UpdateProgressCallback progressCallback) {
  if (_phase != UpdatePhase::Receiving) {
    if (isWriteInProgress()) {
      return false;
    }
    abortPackageUpload("STM32 package upload did not start");
    return false;
  }
  if (_scratchBytes == 0) {
    abortPackageUpload("STM32 package is empty");
    return false;
  }
  if (!packageUploadSizeFits(
        _scratchBytes,
        _partition->size,
        STM32_PACKAGE_MAX_PACKAGE_SIZE)) {
    abortPackageUpload("STM32 package exceeds scratch partition");
    return false;
  }
  if (!finishUploadHash()) {
    return false;
  }

  _phase = UpdatePhase::PackageStored;
  _writtenBytes = 0;
  _verifiedBytes = 0;
  _packageVerifiedBytes = 0;
  return verifyStoredPackage(false, progressCallback);
}

bool Stm32UpdateManager::verifyStoredPackage(
    bool recoveryRestore,
    Stm32UpdateProgressCallback progressCallback) {
  (void)recoveryRestore;
  if (_partition == nullptr) {
    abortPackageUpload("stm32pkg partition not found");
    return false;
  }
  if (_scratchBytes == 0) {
    abortPackageUpload("STM32 package is empty");
    return false;
  }
  if (_phase != UpdatePhase::PackageStored && _phase != UpdatePhase::Error) {
    abortPackageUpload("STM32 package is not ready for verification");
    return false;
  }

  clearError();
  clearVerifiedPackage();
  clearBootloaderSyncResult();
  refreshCurrentFirmwareVersionFloor();
  _phase = UpdatePhase::Verifying;
  _verifiedBytes = 0;
  _packageVerifiedBytes = 0;

  PackageVerifyOptions options;
  // F411/F446 are both accepted by the signed manifest parser. The ROM
  // bootloader GET ID result is bound to the manifest immediately before erase.
  options.expectedTarget = nullptr;
  options.signaturePublicKey = STM32_PACKAGE_SIGNATURE_PUBLIC_KEY;
  options.signaturePublicKeyLength = STM32_PACKAGE_ED25519_PUBLIC_KEY_SIZE;
  options.expectedSignatureKeyId = STM32_PACKAGE_EXPECTED_SIGNATURE_KEY_ID;
  options.allowRdp1 = false;

  PackageStreamVerifyResult result;
  PackageVerifyProgressRelay progressRelay = {progressCallback};
  const PackageError error = verifyPackageFromPartition(
    _partition,
    _scratchBytes,
    options,
    result,
    progressCallback == nullptr ? nullptr : relayPackageVerifyProgress,
    &progressRelay);
  if (error != PackageError::Ok) {
    setError(packageErrorToString(error));
    _phase = UpdatePhase::Error;
    _verifiedBytes = 0;
    _packageVerifiedBytes = 0;
    if (progressCallback != nullptr) progressCallback(0);
    return false;
  }

  const uint32_t revisionFloor = getEffectiveAntiRollbackFloor();
  if (result.manifest.revision < revisionFloor) {
    setError("stm32_revision_rollback");
    _phase = UpdatePhase::Error;
    _verifiedBytes = 0;
    _packageVerifiedBytes = 0;
    if (progressCallback != nullptr) progressCallback(0);
    return false;
  }

  _verifiedManifest = result.manifest;
  _packageFileBytes = _scratchBytes;
  _packagePayloadBytes = result.payloadLength;
  _packageFirmwareBytes = result.manifest.size;
  _packageChunkTableOffset = STM32_PACKAGE_HEADER_SIZE + result.header.manifestLength;
  _packagePayloadOffset = _packageChunkTableOffset + result.header.chunkTableLength;
  _hasVerifiedPackage = true;
  _phase = UpdatePhase::ReadyToWrite;
  _packageVerifiedBytes = _scratchBytes;
  // Package authentication is not STM32 flash verification. Keep firmware
  // verification at zero until the writer confirms device bytes.
  _verifiedBytes = 0;
  if (progressCallback != nullptr) progressCallback(100);
  return true;
}

bool Stm32UpdateManager::probeBootloaderSync(STM32Bootloader* bootloader,
                                             HardwareSerial* serial) {
  clearBootloaderSyncResult();
  if (!canWrite()) {
    _hasBootloaderSyncResult = true;
    setBootloaderSyncError("package_not_ready");
    return false;
  }

  if (_verifiedManifest.isV3) {
    Stm32SecureBootloaderWriter secureWriter;
    SecureLoaderStatus status;
    const SecureLoaderError secureError = secureWriter.beginSession(bootloader, serial, status);
    _hasBootloaderSyncResult = true;
    _lastBootloaderSyncAttempts = 1;
    _lastBootloaderSyncResponse = 0;
    _lastBootloaderSyncOk = secureError == SecureLoaderError::Ok;
    if (_lastBootloaderSyncOk) {
      uint16_t expectedChipId = 0;
      _lastBootloaderSyncOk = expectedChipIdForStm32Target(
        _verifiedManifest.target.c_str(), expectedChipId) && status.deviceId == expectedChipId;
    }
    secureWriter.endSession(true);
    if (_lastBootloaderSyncOk) return true;
    setBootloaderSyncError(
      secureError == SecureLoaderError::Ok
        ? "secure_target_chip_id_mismatch"
        : Stm32SecureBootloaderWriter::errorToString(secureError));
    return false;
  }

  Stm32BootloaderWriter writer;
  const BootloaderSyncResult result = writer.probeSync(bootloader, serial);
  _hasBootloaderSyncResult = true;
  _lastBootloaderSyncAttempts = result.attempts;
  _lastBootloaderSyncResponse = result.lastResponse;
  _lastBootloaderSyncOk = result.error == BootloaderSyncError::Ok;

  if (_lastBootloaderSyncOk) {
    return true;
  }

  setBootloaderSyncError(Stm32BootloaderWriter::errorToString(result.error));
  return false;
}

bool Stm32UpdateManager::commitVerifiedAntiRollback() {
  if (!_hasVerifiedPackage) {
    return false;
  }
  return commitAntiRollback(_verifiedManifest.antiRollback);
}

bool Stm32UpdateManager::commitAntiRollback(uint32_t antiRollback) {
  if (antiRollback <= _antiRollbackFloor) {
    return true;
  }
  return saveAntiRollbackFloor(antiRollback);
}

void Stm32UpdateManager::abortPackageUpload(const char* error) {
  // Only the request that owns an upload may reset upload state. In
  // particular, malformed/authentication-failed HTTP requests cannot clear an
  // immutable context while the background writer is programming STM32.
  if (isWriteInProgress()) {
    return;
  }
  setError(error);
  _phase = UpdatePhase::Error;
  _writtenBytes = 0;
  _verifiedBytes = 0;
  _packagePayloadBytes = 0;
  _packageFirmwareBytes = 0;
  clearVerifiedPackage();
  clearBootloaderSyncResult();
  resetUploadHashState();
}

bool Stm32UpdateManager::cancel() {
  if (!canCancel()) {
    return false;
  }
  return discardScratchBeforeWrite();
}

bool Stm32UpdateManager::shouldDiscardScratchOnPortalStop() const {
  return updatePhaseShouldDiscardScratchOnPortalStop(_phase);
}

bool Stm32UpdateManager::discardScratchBeforeWrite() {
  if (!updatePhaseShouldDiscardScratchOnPortalStop(_phase)) {
    return false;
  }
  if (_partition == nullptr) {
    setError("stm32pkg partition not found");
    _phase = UpdatePhase::Error;
    return false;
  }
  if (esp_partition_erase_range(_partition, 0, _partition->size) != ESP_OK) {
    setError("Failed to erase stm32pkg partition");
    _phase = UpdatePhase::Error;
    return false;
  }

  _phase = UpdatePhase::Idle;
  _scratchBytes = 0;
  _totalBytesHint = 0;
  _writtenBytes = 0;
  _verifiedBytes = 0;
  _packagePayloadBytes = 0;
  _packageFirmwareBytes = 0;
  clearVerifiedPackage();
  clearBootloaderSyncResult();
  resetUploadHashState();
  if (!saveWriteRecoveryMarker(WriteRecoveryStage::None)) {
    setError("STM32 recovery marker write failed");
    _phase = UpdatePhase::Error;
    return false;
  }
  clearError();
  return true;
}

const char* Stm32UpdateManager::getLastError() const {
  return _lastError;
}

bool Stm32UpdateManager::isBootloaderSessionActive() const {
  return _bootloaderSessionActive;
}

bool Stm32UpdateManager::verifiedPackageMatches(const char* expectedTarget,
                                                const char* expectedReleaseTag) const {
  if (!_hasVerifiedPackage) {
    return false;
  }
  if (expectedTarget != nullptr && expectedTarget[0] != '\0' &&
      _verifiedManifest.target != expectedTarget) {
    return false;
  }
  if (expectedReleaseTag != nullptr && expectedReleaseTag[0] != '\0' &&
      _verifiedManifest.releaseTag != expectedReleaseTag) {
    return false;
  }
  return true;
}

String Stm32UpdateManager::buildStatusJson(uint8_t nodeId) const {
  const size_t firmwareVerifiedBytes = boundedFirmwareVerifiedBytes(
    _verifiedBytes, _packageFirmwareBytes);
  uint32_t progress = 0;
  if (_phase == UpdatePhase::Receiving && _totalBytesHint > 0) {
    progress = (uint32_t)((_scratchBytes * 100UL) / _totalBytesHint);
  } else if (_phase == UpdatePhase::Verifying && _scratchBytes > 0) {
    progress = updateProgressPercent(_packageVerifiedBytes, _scratchBytes);
  } else if (_phase == UpdatePhase::Writing && _packageFirmwareBytes > 0) {
    progress = updateProgressPercent(_writtenBytes, _packageFirmwareBytes);
  } else if (_phase == UpdatePhase::VerifyingFlash && _packageFirmwareBytes > 0) {
    progress = updateProgressPercent(firmwareVerifiedBytes, _packageFirmwareBytes);
  } else if (_phase == UpdatePhase::PackageStored || _phase == UpdatePhase::ReadyToWrite) {
    progress = 100;
  } else if (_phase == UpdatePhase::Restarting || _phase == UpdatePhase::Complete) {
    progress = 100;
  }

  String json;
  json.reserve(1024);
  json += "{\"phase\":";
  appendJsonString(json, getPhaseString());
  json += ",\"packageReady\":";
  json += (_hasVerifiedPackage && _scratchBytes > 0) ? "true" : "false";
  json += ",\"packageSha256\":";
  if (updateStatusIncludesPackageSha256(
        _hasVerifiedPackage && _scratchBytes > 0)) {
    char packageSha256Hex[STM32_PACKAGE_SHA256_SIZE * 2U + 1U];
    if (formatLowerHex(
          _packageSha256,
          sizeof(_packageSha256),
          packageSha256Hex,
          sizeof(packageSha256Hex))) {
      appendJsonString(json, packageSha256Hex);
    } else {
      json += "null";
    }
  } else {
    json += "null";
  }
  json += ",\"canCancel\":";
  json += canCancel() ? "true" : "false";
  json += ",\"canWrite\":";
  json += canWrite() ? "true" : "false";
  json += ",\"scratchBytes\":";
  json += String((uint32_t)_scratchBytes);
  json += ",\"totalBytes\":";
  json += String((uint32_t)updateStatusTotalBytes(
    _phase,
    _packageFileBytes > 0 ? _packageFileBytes : _totalBytesHint,
    _packageFirmwareBytes));
  json += ",\"packageBytes\":";
  json += String((uint32_t)_packageFileBytes);
  json += ",\"writtenBytes\":";
  json += String((uint32_t)_writtenBytes);
  json += ",\"packageVerifiedBytes\":";
  json += String((uint32_t)_packageVerifiedBytes);
  json += ",\"verifiedBytes\":";
  json += String((uint32_t)firmwareVerifiedBytes);
  json += ",\"progress\":";
  json += String(progress > 100 ? 100 : progress);
  json += ",\"nodeId\":";
  json += String(nodeId);
  json += ",\"target\":";
  if (_hasVerifiedPackage) {
    appendJsonString(json, _verifiedManifest.target.c_str());
  } else {
    json += "null";
  }
  json += ",\"version\":";
  if (_hasVerifiedPackage) {
    appendJsonString(json, _verifiedManifest.version.c_str());
  } else {
    json += "null";
  }
  json += ",\"versionCode\":";
  if (_hasVerifiedPackage) {
    json += String(_verifiedManifest.versionCode);
  } else {
    json += "null";
  }
  json += ",\"revision\":";
  if (_hasVerifiedPackage) {
    json += String(_verifiedManifest.revision);
  } else {
    json += "null";
  }
  json += ",\"minEsp32UpdateContract\":";
  if (_hasVerifiedPackage) {
    json += String(_verifiedManifest.minEsp32UpdateContract);
  } else {
    json += "null";
  }
  json += ",\"releaseTag\":";
  if (_hasVerifiedPackage) {
    appendJsonString(json, _verifiedManifest.releaseTag.c_str());
  } else {
    json += "null";
  }
  json += ",\"buildProfile\":";
  if (_hasVerifiedPackage) {
    appendJsonString(json, _verifiedManifest.buildProfile.c_str());
  } else {
    json += "null";
  }
  json += ",\"rdpPolicy\":";
  if (_hasVerifiedPackage) {
    appendJsonString(json, _verifiedManifest.rdpPolicy.c_str());
  } else {
    json += "null";
  }
  json += ",\"payloadBytes\":";
  json += String((uint32_t)_packagePayloadBytes);
  json += ",\"firmwareBytes\":";
  json += String((uint32_t)_packageFirmwareBytes);
  json += ",\"antiRollback\":";
  if (_hasVerifiedPackage) {
    json += String(_verifiedManifest.antiRollback);
  } else {
    json += "null";
  }
  json += ",\"antiRollbackFloor\":";
  json += String(_antiRollbackFloor);
  json += ",\"currentFirmwareVersionRaw\":";
  if (_hasCurrentFirmwareVersion) {
    json += String(_currentFirmwareVersionRaw);
  } else {
    json += "null";
  }
  json += ",\"currentFirmwareVersionFloor\":";
  json += String(_currentFirmwareVersionFloor);
  json += ",\"currentFirmwareRevisionRaw\":";
  json += String(_currentFirmwareRevisionRaw);
  json += ",\"postWriteVersionCheckComplete\":";
  json += _postWriteVersionCheckComplete ? "true" : "false";
  json += ",\"postWriteVersionRead\":";
  json += _postWriteVersionRead ? "true" : "false";
  json += ",\"postWriteVersionMatchesPackage\":";
  json += _postWriteVersionMatchesPackage ? "true" : "false";
  json += ",\"postWriteVersionCheckAttempts\":";
  json += String(_postWriteVersionCheckAttempts);
  json += ",\"effectiveAntiRollbackFloor\":";
  json += String(getEffectiveAntiRollbackFloor());
  json += ",\"bootloaderSyncOk\":";
  if (_hasBootloaderSyncResult) {
    json += _lastBootloaderSyncOk ? "true" : "false";
  } else {
    json += "null";
  }
  json += ",\"bootloaderSyncAttempts\":";
  json += String(_lastBootloaderSyncAttempts);
  json += ",\"bootloaderSyncResponse\":";
  json += String(_lastBootloaderSyncResponse);
  json += ",\"bootloaderSyncError\":";
  if (_lastBootloaderSyncError[0] == '\0') {
    json += "null";
  } else {
    appendJsonString(json, _lastBootloaderSyncError);
  }
  json += ",\"bootloaderSessionActive\":";
  json += _bootloaderSessionActive ? "true" : "false";
  json += ",\"writeStartPending\":";
  json += _writeStartPending ? "true" : "false";
  json += ",\"writerStackMinFreeBytes\":";
  if (_writeTaskStackMinFreeBytes == UINT32_MAX) {
    json += "null";
  } else {
    json += String(_writeTaskStackMinFreeBytes);
  }
  json += ",\"espResetReason\":";
  json += String(_espResetReason);
  json += ",\"espResetReasonName\":";
  appendJsonString(json, getEspResetReasonString());
  json += ",\"espBootCount\":";
  json += String(_espBootCount);
  json += ",\"recoveryReason\":";
  if (_phase != UpdatePhase::RecoveryRequired) {
    json += "null";
  } else if (_recoveryRestartUnknown) {
    appendJsonString(json, "restart_unknown");
  } else {
    appendJsonString(json, "write_failed");
  }
  json += ",\"recoveryStage\":";
  if (_phase == UpdatePhase::RecoveryRequired) {
    appendJsonString(json, getRecoveryStageString());
  } else {
    json += "null";
  }
  json += ",\"error\":";
  if (_lastError[0] == '\0') {
    json += "null";
  } else {
    appendJsonString(json, _lastError);
  }
  json += ",\"errorCode\":";
  const char* errorCode = errorCodeForMessage(_lastError);
  if (errorCode == nullptr) {
    json += "null";
  } else {
    appendJsonString(json, errorCode);
  }
  json += "}";
  return json;
}

void Stm32UpdateManager::clearVerifiedPackage() {
  _hasVerifiedPackage = false;
  _packageVerifiedBytes = 0;
  _packageFileBytes = 0;
  _packagePayloadBytes = 0;
  _packageFirmwareBytes = 0;
  _packageChunkTableOffset = 0;
  _packagePayloadOffset = 0;
  _verifiedManifest = PackageManifest();
}

void Stm32UpdateManager::clearWriteContext() {
  _writeContext = WriteContext();
}

bool Stm32UpdateManager::captureWriteContext() {
  if (!_hasVerifiedPackage || _scratchBytes == 0 || _packageFirmwareBytes == 0) {
    clearWriteContext();
    return false;
  }
  _writeContext.manifest = _verifiedManifest;
  _writeContext.scratchBytes = _scratchBytes;
  _writeContext.firmwareBytes = _packageFirmwareBytes;
  _writeContext.payloadBytes = _packagePayloadBytes;
  _writeContext.chunkTableOffset = _packageChunkTableOffset;
  _writeContext.payloadOffset = _packagePayloadOffset;
  memcpy(_writeContext.packageSha256,
         _packageSha256,
         sizeof(_writeContext.packageSha256));
  _writeContext.valid = true;
  return true;
}

bool Stm32UpdateManager::isWriteInProgress() const {
  return _bootloaderSessionActive ||
         _writeTaskHandle != nullptr ||
         updatePhaseHasActiveWriter(_phase);
}

void Stm32UpdateManager::clearBootloaderSyncResult() {
  _hasBootloaderSyncResult = false;
  _lastBootloaderSyncOk = false;
  _lastBootloaderSyncAttempts = 0;
  _lastBootloaderSyncResponse = 0;
  memset(_lastBootloaderSyncError, 0, sizeof(_lastBootloaderSyncError));
}

void Stm32UpdateManager::resetUploadHashState() {
  if (_uploadSha256Active) {
    mbedtls_sha256_free(&_uploadSha256);
    mbedtls_sha256_init(&_uploadSha256);
    _uploadSha256Active = false;
  }
  memset(_packageSha256, 0, sizeof(_packageSha256));
  memset(_expectedPackageSha256Hex, 0, sizeof(_expectedPackageSha256Hex));
  _hasExpectedPackageSha256 = false;
}

bool Stm32UpdateManager::startUploadHash() {
  mbedtls_sha256_free(&_uploadSha256);
  mbedtls_sha256_init(&_uploadSha256);
  if (mbedtls_sha256_starts_ret(&_uploadSha256, 0) != 0) {
    return false;
  }
  _uploadSha256Active = true;
  return true;
}

bool Stm32UpdateManager::finishUploadHash() {
  if (!_uploadSha256Active) {
    abortPackageUpload("Failed to finish STM32 package sha256");
    return false;
  }
  if (mbedtls_sha256_finish_ret(&_uploadSha256, _packageSha256) != 0) {
    abortPackageUpload("Failed to finish STM32 package sha256");
    return false;
  }
  mbedtls_sha256_free(&_uploadSha256);
  mbedtls_sha256_init(&_uploadSha256);
  _uploadSha256Active = false;
  if (_hasExpectedPackageSha256 &&
      !digestMatchesHex(_packageSha256, _expectedPackageSha256Hex)) {
    abortPackageUpload("STM32 package sha256 mismatch");
    return false;
  }
  return true;
}

bool Stm32UpdateManager::calculateStoredPackageSha256() {
  resetUploadHashState();
  if (!startUploadHash()) {
    abortPackageUpload("Failed to start STM32 package sha256");
    return false;
  }

  uint8_t buffer[1024];
  size_t offset = 0;
  while (offset < _scratchBytes) {
    const size_t remaining = _scratchBytes - offset;
    const size_t length = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    if (esp_partition_read(_partition, offset, buffer, length) != ESP_OK) {
      abortPackageUpload("Failed to read stored STM32 package for sha256");
      return false;
    }
    if (mbedtls_sha256_update_ret(&_uploadSha256, buffer, length) != 0) {
      abortPackageUpload("Failed to update STM32 package sha256");
      return false;
    }
    offset += length;
  }
  return finishUploadHash();
}

void Stm32UpdateManager::setBootloaderSyncError(const char* error) {
  if (error == nullptr) {
    memset(_lastBootloaderSyncError, 0, sizeof(_lastBootloaderSyncError));
    return;
  }
  strncpy(_lastBootloaderSyncError, error, sizeof(_lastBootloaderSyncError) - 1);
  _lastBootloaderSyncError[sizeof(_lastBootloaderSyncError) - 1] = '\0';
}

bool Stm32UpdateManager::restoreStoredPackageIfPresent() {
  if (_partition == nullptr) {
    return false;
  }

  size_t packageLength = 0;
  const PackageError lengthError =
    readPackageLengthFromPartition(_partition, _partition->size, packageLength);
  if (lengthError == PackageError::BadMagic || lengthError == PackageError::TooSmall) {
    return false;
  }
  if (lengthError != PackageError::Ok) {
    setError(packageErrorToString(lengthError));
    _phase = UpdatePhase::Error;
    return false;
  }

  _scratchBytes = packageLength;
  _totalBytesHint = packageLength;
  _writtenBytes = 0;
  _verifiedBytes = 0;
  _packageVerifiedBytes = 0;
  _packagePayloadBytes = 0;
  _packageFirmwareBytes = 0;
  _packageChunkTableOffset = 0;
  _packagePayloadOffset = 0;
  _phase = UpdatePhase::PackageStored;
  if (!calculateStoredPackageSha256()) {
    return false;
  }
  if (!verifyStoredPackage(true)) {
    return false;
  }

  const WriteRecoveryStage persistedStage = loadWriteRecoveryMarker();
  _recoveryStage = persistedStage == WriteRecoveryStage::None
    ? WriteRecoveryStage::PackageVerified
    : persistedStage;
  _recoveryRestartUnknown = true;
  _writeStartPending = false;
  _bootloaderSessionActive = false;
  _writtenBytes = 0;
  _verifiedBytes = 0;
  clearBootloaderSyncResult();
  setError("STM32 restart left update state unknown");
  _phase = UpdatePhase::RecoveryRequired;
  return true;
}

uint32_t Stm32UpdateManager::getEffectiveAntiRollbackFloor() const {
  return _currentFirmwareVersionFloor > _antiRollbackFloor
    ? _currentFirmwareVersionFloor
    : _antiRollbackFloor;
}

void Stm32UpdateManager::refreshCurrentFirmwareVersionFloor() {
  _hasCurrentFirmwareVersion = false;
  _currentFirmwareVersionRaw = 0;
  _currentFirmwareRevisionRaw = 0;
  _currentFirmwareVersionFloor = 0;
  if (_i2cClient == nullptr) {
    return;
  }

  uint32_t firmwareVersion = 0;
  if (!_i2cClient->readFirmwareVersion(firmwareVersion)) {
    return;
  }
  Stm32SemanticVersion semanticVersion = {};
  if (decodeStm32VersionCode(firmwareVersion, semanticVersion)) {
    _currentFirmwareVersionRaw = firmwareVersion;
    uint32_t firmwareRevision = 0;
    if (_i2cClient->readFirmwareRevision(firmwareRevision) &&
        firmwareRevision >= STM32_MIN_REVISION) {
      _hasCurrentFirmwareVersion = true;
      _currentFirmwareRevisionRaw = firmwareRevision;
      _currentFirmwareVersionFloor = firmwareRevision;
      return;
    }

    // A tagged versionCode without a positive revision violates register contract 0x0B.
    // Block every update rather than falling back to an empty NVS floor.
    _currentFirmwareVersionFloor = 0xFFFFFFFFUL;
  }
}

void Stm32UpdateManager::resetPostWriteVersionCheck() {
  _postWriteVersionCheckAttempts = 0;
  _postWriteVersionCheckComplete = false;
  _postWriteVersionRead = false;
  _postWriteVersionMatchesPackage = false;
}

bool Stm32UpdateManager::loadAntiRollbackFloor() {
  Preferences prefs;
  if (!prefs.begin(STM32_UPDATE_NVS_NAMESPACE, true)) {
    _antiRollbackStoreReady = false;
    _antiRollbackFloor = 0;
    return false;
  }

  _antiRollbackFloor = prefs.getUInt(STM32_UPDATE_NVS_ANTI_ROLLBACK_KEY, 0);
  prefs.end();
  _antiRollbackStoreReady = true;
  return true;
}

bool Stm32UpdateManager::saveAntiRollbackFloor(uint32_t antiRollback) {
  Preferences prefs;
  if (!prefs.begin(STM32_UPDATE_NVS_NAMESPACE, false)) {
    _antiRollbackStoreReady = false;
    return false;
  }

  const size_t written = prefs.putUInt(STM32_UPDATE_NVS_ANTI_ROLLBACK_KEY, antiRollback);
  prefs.end();
  if (written == 0) {
    _antiRollbackStoreReady = false;
    return false;
  }

  _antiRollbackFloor = antiRollback;
  _antiRollbackStoreReady = true;
  return true;
}

bool Stm32UpdateManager::saveWriteRecoveryMarker(WriteRecoveryStage stage) {
  if (stage != WriteRecoveryStage::None && !_hasVerifiedPackage) {
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(STM32_UPDATE_NVS_NAMESPACE, false)) {
    return false;
  }
  bool saved = true;
  if (stage != WriteRecoveryStage::None) {
    saved = prefs.putBytes(
      STM32_UPDATE_NVS_WRITE_SHA_KEY,
      _packageSha256,
      sizeof(_packageSha256)) == sizeof(_packageSha256);
  }
  if (saved) {
    saved = prefs.putUChar(
      STM32_UPDATE_NVS_WRITE_STAGE_KEY,
      static_cast<uint8_t>(stage)) == sizeof(uint8_t);
  }
  prefs.end();
  if (saved) {
    _recoveryStage = stage;
    _recoveryRestartUnknown = false;
  }
  return saved;
}

WriteRecoveryStage Stm32UpdateManager::loadWriteRecoveryMarker() const {
  Preferences prefs;
  if (!prefs.begin(STM32_UPDATE_NVS_NAMESPACE, true)) {
    return WriteRecoveryStage::None;
  }
  const uint8_t raw = prefs.getUChar(STM32_UPDATE_NVS_WRITE_STAGE_KEY, 0);
  uint8_t packageSha256[STM32_PACKAGE_SHA256_SIZE] = {};
  const size_t shaLength = prefs.getBytesLength(STM32_UPDATE_NVS_WRITE_SHA_KEY);
  const size_t shaRead = shaLength == sizeof(packageSha256)
    ? prefs.getBytes(STM32_UPDATE_NVS_WRITE_SHA_KEY,
                     packageSha256,
                     sizeof(packageSha256))
    : 0;
  prefs.end();
  if (!isValidWriteRecoveryStage(raw) || raw == 0 ||
      shaRead != sizeof(packageSha256) ||
      memcmp(packageSha256, _packageSha256, sizeof(packageSha256)) != 0) {
    return WriteRecoveryStage::None;
  }
  return static_cast<WriteRecoveryStage>(raw);
}

const char* Stm32UpdateManager::getRecoveryStageString() const {
  switch (_recoveryStage) {
    case WriteRecoveryStage::PackageVerified: return "package_verified";
    case WriteRecoveryStage::WriteScheduled: return "write_scheduled";
    case WriteRecoveryStage::WriterStarting: return "writer_starting";
    case WriteRecoveryStage::DestructiveCommandSent: return "destructive_command_sent";
    case WriteRecoveryStage::FirmwareTransferred: return "firmware_transferred";
    case WriteRecoveryStage::RestartConfirming: return "restart_confirming";
    case WriteRecoveryStage::None:
    default: return "none";
  }
}

void Stm32UpdateManager::captureEspBootDiagnostics() {
  _espResetReason = static_cast<uint32_t>(esp_reset_reason());
  Preferences prefs;
  if (!prefs.begin(STM32_UPDATE_NVS_NAMESPACE, false)) {
    _espBootCount = 0;
    return;
  }
  const uint32_t previous = prefs.getUInt(STM32_UPDATE_NVS_BOOT_COUNT_KEY, 0);
  _espBootCount = previous == UINT32_MAX ? UINT32_MAX : previous + 1U;
  if (prefs.putUInt(STM32_UPDATE_NVS_BOOT_COUNT_KEY, _espBootCount) !=
      sizeof(_espBootCount)) {
    _espBootCount = 0;
  }
  prefs.end();
}

const char* Stm32UpdateManager::getEspResetReasonString() const {
  switch (static_cast<esp_reset_reason_t>(_espResetReason)) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_UNKNOWN:
    default: return "unknown";
  }
}

void Stm32UpdateManager::recordWriteTaskStackWatermark() {
  const uint32_t freeBytes = static_cast<uint32_t>(
    uxTaskGetStackHighWaterMark(nullptr));
  if (freeBytes < _writeTaskStackMinFreeBytes) {
    _writeTaskStackMinFreeBytes = freeBytes;
  }
}

bool Stm32UpdateManager::writeTaskHasSafeStackHeadroom() {
  recordWriteTaskStackWatermark();
  return _writeTaskStackMinFreeBytes >= STM32_WRITE_STACK_MIN_FREE_BYTES;
}

void Stm32UpdateManager::clearError() {
  memset(_lastError, 0, sizeof(_lastError));
}

void Stm32UpdateManager::setError(const char* error) {
  if (error == nullptr) {
    clearError();
    return;
  }
  strncpy(_lastError, error, sizeof(_lastError) - 1);
  _lastError[sizeof(_lastError) - 1] = '\0';
}

const char* Stm32UpdateManager::getPhaseString() const {
  switch (_phase) {
    case UpdatePhase::Idle: return "idle";
    case UpdatePhase::Receiving: return "receiving";
    case UpdatePhase::PackageStored: return "package_stored";
    case UpdatePhase::Verifying: return "verifying";
    case UpdatePhase::ReadyToWrite: return "ready_to_write";
    case UpdatePhase::Writing: return "writing";
    case UpdatePhase::VerifyingFlash: return "verifying_flash";
    case UpdatePhase::Restarting: return "restarting";
    case UpdatePhase::Complete: return "complete";
    case UpdatePhase::Error: return "error";
    case UpdatePhase::RecoveryRequired: return "recovery_required";
    default: return "error";
  }
}

}  // namespace stm32_update
