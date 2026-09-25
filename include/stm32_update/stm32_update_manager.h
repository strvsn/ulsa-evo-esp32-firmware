/**
 * @file stm32_update_manager.h
 * @brief STM32 update scratch package manager
 */

#ifndef STM32_UPDATE_MANAGER_H
#define STM32_UPDATE_MANAGER_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include "stm32_update/stm32_package.h"
#include "stm32_update/stm32_update_runtime_contract.h"

class STM32Bootloader;
class UlsaEvoI2cClient;

namespace stm32_update {

using Stm32UpdateProgressCallback = void (*)(uint8_t progress);

class Stm32UpdateManager {
public:
  Stm32UpdateManager();

  void setI2cClient(UlsaEvoI2cClient* client);
  void begin();
  void resetCompletedResultForNewSession();
  bool isAvailable() const;
  bool isBusy() const;
  bool canCancel() const;
  bool canWrite() const;
  UpdatePhase getPhase() const { return _phase; }

  bool beginPackageUpload(size_t totalBytesHint,
                          const char* expectedPackageSha256Hex = nullptr);
  bool writePackageChunk(const uint8_t* data, size_t length);
  bool finishPackageUpload(Stm32UpdateProgressCallback progressCallback = nullptr);
  bool verifyStoredPackage(
    bool recoveryRestore = false,
    Stm32UpdateProgressCallback progressCallback = nullptr);
  bool probeBootloaderSync(STM32Bootloader* bootloader, HardwareSerial* serial);
  // Reserves an immutable package/write context. The RTOS writer is started
  // separately after the HTTP 202 response grace has elapsed.
  bool startWrite(STM32Bootloader* bootloader, HardwareSerial* serial);
  bool startPendingWrite();
  bool hasPendingWriteStart() const { return _writeStartPending; }
  bool commitVerifiedAntiRollback();
  void abortPackageUpload(const char* error);
  bool cancel();
  bool discardScratchBeforeWrite();
  bool shouldDiscardScratchOnPortalStop() const;

  const char* getLastError() const;
  bool isBootloaderSessionActive() const;
  bool verifiedPackageMatches(const char* expectedTarget,
                              const char* expectedReleaseTag) const;
  String buildStatusJson(uint8_t nodeId = 0) const;

private:
  const esp_partition_t* _partition;
  UpdatePhase _phase;
  size_t _scratchBytes;
  size_t _totalBytesHint;
  size_t _writtenBytes;
  size_t _verifiedBytes;
  size_t _packageVerifiedBytes;
  size_t _packageFileBytes;
  size_t _packagePayloadBytes;
  size_t _packageFirmwareBytes;
  size_t _packageChunkTableOffset;
  size_t _packagePayloadOffset;
  uint8_t _packageSha256[STM32_PACKAGE_SHA256_SIZE];
  uint32_t _antiRollbackFloor;
  uint32_t _currentFirmwareVersionRaw;
  uint32_t _currentFirmwareRevisionRaw;
  uint32_t _currentFirmwareVersionFloor;
  uint32_t _espResetReason;
  uint32_t _espBootCount;
  uint32_t _writeTaskStackMinFreeBytes;
  uint8_t _postWriteVersionCheckAttempts;
  bool _hasVerifiedPackage;
  bool _hasExpectedPackageSha256;
  bool _hasCurrentFirmwareVersion;
  bool _postWriteVersionCheckComplete;
  bool _postWriteVersionRead;
  bool _postWriteVersionMatchesPackage;
  bool _antiRollbackStoreReady;
  bool _uploadSha256Active;
  bool _hasBootloaderSyncResult;
  bool _lastBootloaderSyncOk;
  uint8_t _lastBootloaderSyncAttempts;
  uint8_t _lastBootloaderSyncResponse;
  bool _bootloaderSessionActive;
  bool _writeEraseStarted;
  bool _writeStartPending;
  bool _recoveryRestartUnknown;
  WriteRecoveryStage _recoveryStage;
  TaskHandle_t _writeTaskHandle;
  STM32Bootloader* _writeBootloader;
  HardwareSerial* _writeSerial;
  UlsaEvoI2cClient* _i2cClient;
  PackageManifest _verifiedManifest;
  struct WriteContext {
    bool valid = false;
    size_t scratchBytes = 0;
    size_t firmwareBytes = 0;
    size_t payloadBytes = 0;
    size_t chunkTableOffset = 0;
    size_t payloadOffset = 0;
    uint8_t packageSha256[STM32_PACKAGE_SHA256_SIZE] = {};
    PackageManifest manifest;
  } _writeContext;
  // One manager-owned buffer avoids nesting a 4 KiB caller frame with a
  // second 4 KiB command payload on the RTOS task stack.
  uint8_t _secureFrameBuffer[
    STM32_PACKAGE_MAX_CHUNK_SIZE +
    STM32_PACKAGE_AES_GCM_NONCE_SIZE +
    STM32_PACKAGE_AES_GCM_TAG_SIZE];
  char _lastError[96];
  char _lastBootloaderSyncError[64];
  char _expectedPackageSha256Hex[STM32_PACKAGE_SHA256_SIZE * 2 + 1];
  mbedtls_sha256_context _uploadSha256;

  void clearVerifiedPackage();
  void clearWriteContext();
  bool captureWriteContext();
  bool isWriteInProgress() const;
  void clearBootloaderSyncResult();
  void setBootloaderSyncError(const char* error);
  void resetUploadHashState();
  bool startUploadHash();
  bool finishUploadHash();
  bool calculateStoredPackageSha256();
  bool saveWriteRecoveryMarker(WriteRecoveryStage stage);
  WriteRecoveryStage loadWriteRecoveryMarker() const;
  const char* getRecoveryStageString() const;
  void captureEspBootDiagnostics();
  const char* getEspResetReasonString() const;
  void recordWriteTaskStackWatermark();
  bool writeTaskHasSafeStackHeadroom();
  bool restoreStoredPackageIfPresent();
  uint32_t getEffectiveAntiRollbackFloor() const;
  void refreshCurrentFirmwareVersionFloor();
  void resetPostWriteVersionCheck();
  bool waitForPostWriteVersionReadback(bool watchdogAttached);
  void runWriteTask();
  void runSecureWriteTask();
  void finishWriteTask(bool success, const char* error, bool recoveryRequired);
  static void writeTaskEntry(void* arg);
  bool loadAntiRollbackFloor();
  bool saveAntiRollbackFloor(uint32_t antiRollback);
  bool commitAntiRollback(uint32_t antiRollback);
  void clearError();
  void setError(const char* error);
  const char* getPhaseString() const;
};

}  // namespace stm32_update

#endif  // STM32_UPDATE_MANAGER_H
