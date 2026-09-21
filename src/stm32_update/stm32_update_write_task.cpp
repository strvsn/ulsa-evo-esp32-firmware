/**
 * @file stm32_update_write_task.cpp
 * @brief Background STM32 ROM bootloader write task
 */

#include "stm32_update/stm32_update_manager.h"

#include "stm32_update/stm32_bootloader_writer.h"
#include "stm32_update/stm32_package_payload.h"
#include "stm32_update/stm32_target_identity.h"

#include <algorithm>
#include <esp_task_wdt.h>
#include <stdlib.h>
#include <string.h>

namespace stm32_update {
namespace {

static const uint32_t STM32_FLASH_BASE = 0x08000000UL;
static const size_t STM32_UART_BLOCK_SIZE = 256;
// ESP-IDF's xTaskCreate stack depth is bytes. Keep ample headroom for UART,
// Preferences, and protocol call frames even though the 4 KiB secure payload
// buffers no longer live on this stack.
static const uint32_t WRITE_TASK_STACK_BYTES = 16384;
static const UBaseType_t WRITE_TASK_PRIORITY = 1;
// A normal updated device is ready soon after resetToNormal() returns. Older
// plaintext EEPROMs can take about 12 seconds to migrate during STM32 setup.
static const uint32_t STM32_POST_WRITE_I2C_CONFIRM_TIMEOUT_MS = 15000;
static const uint32_t STM32_POST_WRITE_I2C_RETRY_INTERVAL_MS = 250;

bool attachWriteTaskWatchdog() {
  return esp_task_wdt_add(NULL) == ESP_OK;
}

void feedWriteTaskWatchdog(bool attached) {
  if (attached) {
    esp_task_wdt_reset();
  }
}

void waitWithWatchdog(uint32_t durationMs, bool watchdogAttached) {
  while (durationMs > 0) {
    const uint32_t sliceMs = durationMs > 100 ? 100 : durationMs;
    delay(sliceMs);
    feedWriteTaskWatchdog(watchdogAttached);
    durationMs -= sliceMs;
  }
}

bool parseBaseAddress(const String& value, uint32_t& out) {
  if (!value.startsWith("0x") && !value.startsWith("0X")) {
    return false;
  }
  char* end = nullptr;
  const unsigned long parsed = strtoul(value.c_str(), &end, 16);
  if (end == nullptr || *end != '\0' || parsed != STM32_FLASH_BASE) {
    return false;
  }
  out = (uint32_t)parsed;
  return true;
}

bool validateChunkEntry(const PackageChunkEntry& entry,
                        uint32_t expectedIndex,
                        uint32_t expectedOffset,
                        size_t payloadBytes) {
  return entry.index == expectedIndex &&
         entry.plainOffset == expectedOffset &&
         entry.plainSize > 0 &&
         entry.plainSize <= STM32_PACKAGE_MAX_CHUNK_SIZE &&
         entry.frameOffset <= payloadBytes &&
         entry.frameSize <= payloadBytes - entry.frameOffset;
}

const char* writeErrorString(BootloaderSyncError error) {
  return Stm32BootloaderWriter::errorToString(error);
}

}  // namespace

bool Stm32UpdateManager::startWrite(STM32Bootloader* bootloader, HardwareSerial* serial) {
  if (bootloader == nullptr || serial == nullptr) {
    setError("STM32 writer unavailable");
    return false;
  }
  if (_writeStartPending || _writeTaskHandle != nullptr || _bootloaderSessionActive) {
    setError("STM32 write already running");
    return false;
  }
  if (!canWrite() || !_hasVerifiedPackage) {
    setError("STM32 package is not ready to write");
    return false;
  }
  if (!captureWriteContext()) {
    setError("STM32 package is not ready to write");
    return false;
  }

  if (!saveWriteRecoveryMarker(WriteRecoveryStage::WriteScheduled)) {
    clearWriteContext();
    setError("STM32 recovery marker write failed");
    return false;
  }

  clearError();
  _writeBootloader = bootloader;
  _writeSerial = serial;
  _writeEraseStarted = false;
  _writeStartPending = true;
  _bootloaderSessionActive = false;
  _phase = UpdatePhase::Writing;
  _writtenBytes = 0;
  _verifiedBytes = 0;
  resetPostWriteVersionCheck();

  return true;
}

bool Stm32UpdateManager::startPendingWrite() {
  if (!_writeStartPending || _phase != UpdatePhase::Writing ||
      !_writeContext.valid || _writeBootloader == nullptr ||
      _writeSerial == nullptr || _writeTaskHandle != nullptr ||
      _bootloaderSessionActive) {
    return false;
  }
  if (!_hasVerifiedPackage ||
      _writeContext.scratchBytes != _scratchBytes ||
      _writeContext.firmwareBytes != _packageFirmwareBytes ||
      _writeContext.manifest.target != _verifiedManifest.target ||
      _writeContext.manifest.releaseTag != _verifiedManifest.releaseTag ||
      memcmp(_writeContext.packageSha256,
             _packageSha256,
             sizeof(_packageSha256)) != 0) {
    _writeStartPending = false;
    _writeBootloader = nullptr;
    _writeSerial = nullptr;
    clearWriteContext();
    _phase = UpdatePhase::Error;
    setError("STM32 reserved package changed");
    return false;
  }
  if (!saveWriteRecoveryMarker(WriteRecoveryStage::WriterStarting)) {
    _writeStartPending = false;
    _writeBootloader = nullptr;
    _writeSerial = nullptr;
    clearWriteContext();
    _phase = UpdatePhase::ReadyToWrite;
    setError("STM32 recovery marker write failed");
    return false;
  }

  _bootloaderSessionActive = true;

  const BaseType_t created = xTaskCreate(
    Stm32UpdateManager::writeTaskEntry,
    "stm32_writer",
    WRITE_TASK_STACK_BYTES,
    this,
    WRITE_TASK_PRIORITY,
    &_writeTaskHandle);

  if (created != pdPASS) {
    _writeStartPending = false;
    _writeTaskHandle = nullptr;
    _bootloaderSessionActive = false;
    _writeBootloader = nullptr;
    _writeSerial = nullptr;
    clearWriteContext();
    _phase = UpdatePhase::ReadyToWrite;
    (void)saveWriteRecoveryMarker(WriteRecoveryStage::None);
    setError("Failed to start STM32 write task");
    return false;
  }

  _writeStartPending = false;
  return true;
}

void Stm32UpdateManager::writeTaskEntry(void* arg) {
  Stm32UpdateManager* manager = static_cast<Stm32UpdateManager*>(arg);
  if (manager != nullptr) {
    if (manager->writeTaskHasSafeStackHeadroom()) {
      manager->runWriteTask();
    } else {
      manager->finishWriteTask(
        false, "STM32 writer stack headroom too low", false);
    }
  }
  // resetToNormal()/finishCustomBootloaderSession() may subscribe this task;
  // avoid deleting an unregistered task after an early failure.
  if (esp_task_wdt_status(nullptr) == ESP_OK) {
    esp_task_wdt_delete(nullptr);
  }
  vTaskDelete(nullptr);
}

void Stm32UpdateManager::runWriteTask() {
  if (_writeContext.valid && _writeContext.manifest.isV3 &&
      _writeContext.manifest.payloadEncoding == "aes-256-gcm-chunked") {
    runSecureWriteTask();
    return;
  }
  uint32_t baseAddress = 0;
  if (_partition == nullptr ||
      !_writeContext.valid ||
      !parseBaseAddress(_writeContext.manifest.baseAddress, baseAddress)) {
    finishWriteTask(false, "STM32 package is not writable", false);
    return;
  }

  if (_writeContext.manifest.payloadEncoding != "plain") {
    finishWriteTask(false, "STM32 package encoding unsupported", false);
    return;
  }

  Stm32BootloaderWriter writer;
  const BootloaderSyncResult sync = writer.beginSession(_writeBootloader, _writeSerial);
  _hasBootloaderSyncResult = true;
  _lastBootloaderSyncAttempts = sync.attempts;
  _lastBootloaderSyncResponse = sync.lastResponse;
  _lastBootloaderSyncOk = sync.error == BootloaderSyncError::Ok;
  if (!_lastBootloaderSyncOk) {
    setBootloaderSyncError(writeErrorString(sync.error));
    finishWriteTask(false, writeErrorString(sync.error), false);
    return;
  }

  BootloaderInfo info;
  const BootloaderSyncError infoError = writer.getInfo(info);
  if (infoError != BootloaderSyncError::Ok || !info.valid) {
    writer.endSession();
    finishWriteTask(
        false,
        infoError == BootloaderSyncError::Ok
          ? "bootloader_info_invalid"
          : writeErrorString(infoError),
        false);
    return;
  }
  uint16_t expectedChipId = 0;
  if (!expectedChipIdForStm32Target(
          _writeContext.manifest.target.c_str(), expectedChipId) ||
      info.chipId != expectedChipId) {
    writer.endSession();
    finishWriteTask(false, "target_chip_id_mismatch", false);
    return;
  }

  if (!writeTaskHasSafeStackHeadroom()) {
    writer.endSession();
    finishWriteTask(false, "STM32 writer stack headroom too low", false);
    return;
  }

  if (!saveWriteRecoveryMarker(WriteRecoveryStage::DestructiveCommandSent)) {
    writer.endSession();
    finishWriteTask(false, "STM32 recovery marker write failed", false);
    return;
  }
  BootloaderSyncError error = writer.massErase(&info);
  if (error != BootloaderSyncError::Ok) {
    writer.endSession();
    const bool uncertainErase = error == BootloaderSyncError::DataTimeout;
    finishWriteTask(false, writeErrorString(error), uncertainErase);
    return;
  }
  _writeEraseStarted = true;
  const bool writeWatchdogAttached = attachWriteTaskWatchdog();
  feedWriteTaskWatchdog(writeWatchdogAttached);

  uint8_t chunkBuffer[STM32_PACKAGE_MAX_CHUNK_SIZE];
  uint32_t expectedOffset = 0;

  for (uint32_t index = 0; index < _writeContext.manifest.chunkCount; ++index) {
    PackageChunkEntry entry;
    PackageError packageError =
      readPackageChunkEntry(_partition, _writeContext.chunkTableOffset, index, entry);
    if (packageError != PackageError::Ok ||
        !validateChunkEntry(entry, index, expectedOffset, _writeContext.payloadBytes)) {
      writer.endSession();
      finishWriteTask(
        false,
        packageError == PackageError::Ok
          ? "chunk_table_invalid"
          : packageErrorToString(packageError),
        true);
      return;
    }

    packageError = readPackagePlainChunk(
      _partition,
      _writeContext.payloadOffset,
      _writeContext.manifest,
      entry,
      chunkBuffer,
      sizeof(chunkBuffer));
    if (packageError != PackageError::Ok) {
      writer.endSession();
      finishWriteTask(false, packageErrorToString(packageError), true);
      return;
    }

    size_t offset = 0;
    while (offset < entry.plainSize) {
      feedWriteTaskWatchdog(writeWatchdogAttached);
      const size_t length = std::min((size_t)entry.plainSize - offset, (size_t)STM32_UART_BLOCK_SIZE);
      error = writer.writeMemory(baseAddress + entry.plainOffset + offset, chunkBuffer + offset, length);
      if (error != BootloaderSyncError::Ok) {
        writer.endSession();
        finishWriteTask(false, writeErrorString(error), true);
        return;
      }

      offset += length;
      _writtenBytes += length;
      feedWriteTaskWatchdog(writeWatchdogAttached);
      delay(1);
    }

    expectedOffset += entry.plainSize;
  }

  _phase = UpdatePhase::VerifyingFlash;
  _verifiedBytes = 0;
  uint8_t actual[STM32_UART_BLOCK_SIZE];
  expectedOffset = 0;

  for (uint32_t index = 0; index < _writeContext.manifest.chunkCount; ++index) {
    PackageChunkEntry entry;
    PackageError packageError =
      readPackageChunkEntry(_partition, _writeContext.chunkTableOffset, index, entry);
    if (packageError != PackageError::Ok ||
        !validateChunkEntry(entry, index, expectedOffset, _writeContext.payloadBytes)) {
      writer.endSession();
      finishWriteTask(
        false,
        packageError == PackageError::Ok
          ? "chunk_table_invalid"
          : packageErrorToString(packageError),
        true);
      return;
    }

    packageError = readPackagePlainChunk(
      _partition,
      _writeContext.payloadOffset,
      _writeContext.manifest,
      entry,
      chunkBuffer,
      sizeof(chunkBuffer));
    if (packageError != PackageError::Ok) {
      writer.endSession();
      finishWriteTask(false, packageErrorToString(packageError), true);
      return;
    }

    size_t offset = 0;
    while (offset < entry.plainSize) {
      feedWriteTaskWatchdog(writeWatchdogAttached);
      const size_t length = std::min((size_t)entry.plainSize - offset, (size_t)STM32_UART_BLOCK_SIZE);
      error = writer.readMemory(baseAddress + entry.plainOffset + offset, actual, length);
      if (error != BootloaderSyncError::Ok) {
        writer.endSession();
        finishWriteTask(false, writeErrorString(error), true);
        return;
      }
      if (memcmp(chunkBuffer + offset, actual, length) != 0) {
        writer.endSession();
        finishWriteTask(false, "verify_mismatch", true);
        return;
      }

      offset += length;
      _verifiedBytes += length;
      feedWriteTaskWatchdog(writeWatchdogAttached);
      delay(1);
    }

    expectedOffset += entry.plainSize;
  }

  writer.endSession();
  if (!saveWriteRecoveryMarker(WriteRecoveryStage::FirmwareTransferred)) {
    finishWriteTask(false, "STM32 recovery marker write failed", true);
    return;
  }
  _phase = UpdatePhase::Restarting;
  if (!saveWriteRecoveryMarker(WriteRecoveryStage::RestartConfirming)) {
    finishWriteTask(false, "STM32 recovery marker write failed", true);
    return;
  }
  if (!waitForPostWriteVersionReadback(writeWatchdogAttached)) {
    finishWriteTask(false,
                    _postWriteVersionRead
                      ? "post_write_version_mismatch"
                      : "post_write_version_unconfirmed",
                    true);
    return;
  }
  // Best-effort highest-seen revision telemetry. Failure must not turn a
  // verified user-selected downgrade into an unrecoverable write failure.
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

void Stm32UpdateManager::finishWriteTask(bool success,
                                         const char* error,
                                         bool recoveryRequired) {
  _bootloaderSessionActive = false;
  _writeEraseStarted = false;
  _writeStartPending = false;
  _writeBootloader = nullptr;
  _writeSerial = nullptr;
  _writeTaskHandle = nullptr;

  const size_t firmwareBytes = _writeContext.firmwareBytes;
  clearWriteContext();

  if (success) {
    _recoveryRestartUnknown = false;
    _recoveryStage = WriteRecoveryStage::None;
    clearError();
    _writtenBytes = firmwareBytes;
    _verifiedBytes = firmwareBytes;
    _phase = UpdatePhase::Complete;
    return;
  }

  setError(error == nullptr ? "STM32 write failed" : error);
  _phase = recoveryRequired ? UpdatePhase::RecoveryRequired : UpdatePhase::Error;
  if (!recoveryRequired) {
    _recoveryRestartUnknown = false;
    _recoveryStage = WriteRecoveryStage::None;
    (void)saveWriteRecoveryMarker(WriteRecoveryStage::None);
  }
}

bool Stm32UpdateManager::waitForPostWriteVersionReadback(bool watchdogAttached) {
  if (_i2cClient == nullptr) {
    _postWriteVersionCheckComplete = true;
    return false;
  }

  const uint32_t deadline = millis() + STM32_POST_WRITE_I2C_CONFIRM_TIMEOUT_MS;
  do {
    ++_postWriteVersionCheckAttempts;
    refreshCurrentFirmwareVersionFloor();
    feedWriteTaskWatchdog(watchdogAttached);
    if (_hasCurrentFirmwareVersion) {
      _postWriteVersionRead = true;
      _postWriteVersionMatchesPackage = postWriteIdentityMatches(
        _writeContext.manifest.versionCode,
        _writeContext.manifest.revision,
        _currentFirmwareVersionRaw,
        _currentFirmwareRevisionRaw);
      _postWriteVersionCheckComplete = true;
      return _postWriteVersionMatchesPackage;
    }
    if ((int32_t)(millis() - deadline) >= 0) {
      break;
    }
    waitWithWatchdog(STM32_POST_WRITE_I2C_RETRY_INTERVAL_MS, watchdogAttached);
  } while (true);

  _postWriteVersionCheckComplete = true;
  return false;
}

}  // namespace stm32_update
