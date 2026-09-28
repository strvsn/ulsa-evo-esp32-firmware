/**
 * @file stm32_secure_update_write_task.cpp
 * @brief Encrypted package relay to the ULSA STM32 custom bootloader
 */

#include "stm32_update/stm32_update_manager.h"

#include "stm32_update/stm32_package_payload.h"
#include "stm32_update/stm32_secure_bootloader_writer.h"
#include "stm32_update/stm32_target_identity.h"

#include <esp_task_wdt.h>
#include <string.h>

namespace stm32_update {

void Stm32UpdateManager::runSecureWriteTask() {
  recordWriteTaskStackWatermark();
  if (_partition == nullptr || !_writeContext.valid ||
      !_writeContext.manifest.isV3 ||
      _writeContext.manifest.payloadEncoding != "aes-256-gcm-chunked" ||
      _writeContext.manifest.baseAddress != "0x08010000") {
    finishWriteTask(false, "secure_package_not_writable", false);
    return;
  }

  Stm32SecureBootloaderWriter writer;
  SecureLoaderStatus status;
  SecureLoaderError error = writer.beginSession(_writeBootloader, _writeSerial, status);
  _hasBootloaderSyncResult = true;
  _lastBootloaderSyncAttempts = 1;
  _lastBootloaderSyncResponse = 0;
  _lastBootloaderSyncOk = error == SecureLoaderError::Ok;
  if (error != SecureLoaderError::Ok) {
    setBootloaderSyncError(Stm32SecureBootloaderWriter::errorToString(error));
    finishWriteTask(false, Stm32SecureBootloaderWriter::errorToString(error), false);
    return;
  }

  uint16_t expectedChipId = 0;
  if (!expectedChipIdForStm32Target(
        _writeContext.manifest.target.c_str(), expectedChipId) ||
      status.deviceId != expectedChipId) {
    writer.endSession(true);
    finishWriteTask(false, "secure_target_chip_id_mismatch", false);
    return;
  }

  if (!writeTaskHasSafeStackHeadroom()) {
    writer.endSession(true);
    finishWriteTask(false, "STM32 writer stack headroom too low", false);
    return;
  }

  // BEGIN may erase application flash before its response is returned. Make
  // the recovery stage durable before issuing the destructive command.
  if (!saveWriteRecoveryMarker(WriteRecoveryStage::DestructiveCommandSent)) {
    writer.endSession(true);
    finishWriteTask(false, "STM32 recovery marker write failed", false);
    return;
  }
  error = writer.beginUpdate(
    _writeContext.manifest.loaderManifest,
    _writeContext.manifest.loaderManifestNonce,
    _writeContext.manifest.loaderManifestTag,
    status);
  if (error != SecureLoaderError::Ok) {
    writer.endSession(true);
    finishWriteTask(false, Stm32SecureBootloaderWriter::errorToString(error), true);
    return;
  }
  _writeEraseStarted = true;
  const bool watchdogAttached = esp_task_wdt_add(NULL) == ESP_OK;
  if (watchdogAttached) esp_task_wdt_reset();

  uint32_t expectedOffset = 0;
  for (uint32_t index = 0; index < _writeContext.manifest.chunkCount; ++index) {
    PackageChunkEntry entry;
    const PackageError packageError = readPackageChunkEntry(
      _partition, _writeContext.chunkTableOffset, index, entry);
    if (packageError != PackageError::Ok || entry.index != index ||
        entry.plainOffset != expectedOffset || entry.plainSize == 0 ||
        entry.plainSize > STM32_PACKAGE_MAX_CHUNK_SIZE ||
        entry.frameSize != entry.plainSize +
          STM32_PACKAGE_AES_GCM_NONCE_SIZE + STM32_PACKAGE_AES_GCM_TAG_SIZE ||
        entry.frameOffset > _writeContext.payloadBytes ||
        entry.frameSize > _writeContext.payloadBytes - entry.frameOffset) {
      writer.endSession(true);
      finishWriteTask(false, "secure_chunk_table_invalid", true);
      return;
    }
    if (readPackagePayloadBytes(
          _partition, _writeContext.payloadOffset, entry.frameOffset, 0,
          _secureFrameBuffer, entry.frameSize) != PackageError::Ok) {
      writer.endSession(true);
      finishWriteTask(false, "secure_chunk_read_failed", true);
      return;
    }
    error = writer.writeChunk(entry.index, entry.plainOffset, entry.plainSize,
                              _secureFrameBuffer, entry.frameSize, status);
    memset(_secureFrameBuffer, 0, sizeof(_secureFrameBuffer));
    if (error != SecureLoaderError::Ok) {
      writer.endSession(true);
      finishWriteTask(false, Stm32SecureBootloaderWriter::errorToString(error), true);
      return;
    }
    if (status.nextChunk != index + 1 ||
        status.writtenBytes != entry.plainOffset + entry.plainSize) {
      writer.endSession(true);
      finishWriteTask(false, "secure_chunk_status_mismatch", true);
      return;
    }
    expectedOffset += entry.plainSize;
    _writtenBytes = status.writtenBytes;
    recordWriteTaskStackWatermark();
    if (watchdogAttached) esp_task_wdt_reset();
    delay(1);
  }

  _phase = UpdatePhase::VerifyingFlash;
  error = writer.finish(status);
  writer.endSession(false);
  if (error != SecureLoaderError::Ok) {
    finishWriteTask(false, Stm32SecureBootloaderWriter::errorToString(error), true);
    return;
  }
  if (status.stage != 5 ||
      status.writtenBytes != _writeContext.firmwareBytes) {
    finishWriteTask(false, "secure_finish_status_mismatch", true);
    return;
  }
  _verifiedBytes = status.writtenBytes;
  if (!saveWriteRecoveryMarker(WriteRecoveryStage::FirmwareTransferred)) {
    finishWriteTask(false, "STM32 recovery marker write failed", true);
    return;
  }
  _phase = UpdatePhase::Restarting;
  if (!saveWriteRecoveryMarker(WriteRecoveryStage::RestartConfirming)) {
    finishWriteTask(false, "STM32 recovery marker write failed", true);
    return;
  }
  if (!waitForPostWriteVersionReadback(watchdogAttached)) {
    finishWriteTask(false,
                    _postWriteVersionRead
                      ? "post_write_version_mismatch"
                      : "post_write_version_unconfirmed",
                    true);
    return;
  }
  commitAntiRollback(_writeContext.manifest.antiRollback);
  if (esp_partition_erase_range(_partition, 0, _partition->size) != ESP_OK) {
    finishWriteTask(false, "scratch_cleanup_failed", true);
    return;
  }
  _scratchBytes = 0;
  _packageFileBytes = 0;
  _packageVerifiedBytes = 0;
  (void)saveWriteRecoveryMarker(WriteRecoveryStage::None);
  finishWriteTask(true, nullptr, false);
}

}  // namespace stm32_update
