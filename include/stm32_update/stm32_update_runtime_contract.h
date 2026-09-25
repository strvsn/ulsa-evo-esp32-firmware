/**
 * @file stm32_update_runtime_contract.h
 * @brief Small, host-testable STM32 update state and accounting rules
 */

#ifndef STM32_UPDATE_RUNTIME_CONTRACT_H
#define STM32_UPDATE_RUNTIME_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

namespace stm32_update {

enum class UpdatePhase : uint8_t {
  Idle = 0,
  Receiving,
  PackageStored,
  Verifying,
  ReadyToWrite,
  Writing,
  VerifyingFlash,
  Restarting,
  Complete,
  Error,
  RecoveryRequired,
};

enum class WriteRecoveryStage : uint8_t {
  None = 0,
  PackageVerified,
  WriteScheduled,
  WriterStarting,
  DestructiveCommandSent,
  FirmwareTransferred,
  RestartConfirming,
};

constexpr bool isValidWriteRecoveryStage(uint8_t raw) {
  return raw <= static_cast<uint8_t>(WriteRecoveryStage::RestartConfirming);
}

constexpr bool updatePhaseIsBusy(UpdatePhase phase) {
  return phase == UpdatePhase::Receiving ||
         phase == UpdatePhase::Verifying ||
         phase == UpdatePhase::Writing ||
         phase == UpdatePhase::VerifyingFlash ||
         phase == UpdatePhase::Restarting;
}

constexpr bool updatePhaseHasActiveWriter(UpdatePhase phase) {
  return phase == UpdatePhase::Writing ||
         phase == UpdatePhase::VerifyingFlash ||
         phase == UpdatePhase::Restarting;
}

// Completion remains available as a result, but must no longer suppress the
// normal measurement presentation after its portal and writer have stopped.
constexpr bool completedUpdateReturnedToMeasurement(
    UpdatePhase phase, bool portalActive, bool bootloaderSessionActive) {
  return phase == UpdatePhase::Complete &&
         !portalActive && !bootloaderSessionActive;
}

// A result belongs to the session that produced it. Starting another session
// must not report that result or close its new portal as already complete.
constexpr bool shouldResetCompletedResultForNewSession(
    UpdatePhase phase, bool writerActive) {
  return phase == UpdatePhase::Complete && !writerActive;
}

constexpr bool updatePhaseReportsFirmwareProgress(UpdatePhase phase) {
  return phase == UpdatePhase::Writing ||
         phase == UpdatePhase::VerifyingFlash ||
         phase == UpdatePhase::Restarting ||
         phase == UpdatePhase::Complete ||
         phase == UpdatePhase::RecoveryRequired;
}

// A verified scratch package is the durable recovery journal. After an ESP32
// restart there is no active portal purpose, so this phase must claim the STM32
// system UI independently of the transient SoftAP session.
constexpr bool updatePhaseRequiresPersistentRecoveryPresentation(
    UpdatePhase phase) {
  return phase == UpdatePhase::RecoveryRequired;
}

constexpr bool updatePhaseShouldDiscardScratchOnPortalStop(
    UpdatePhase phase) {
  return phase == UpdatePhase::Receiving ||
         phase == UpdatePhase::PackageStored ||
         phase == UpdatePhase::Verifying ||
         phase == UpdatePhase::ReadyToWrite ||
         phase == UpdatePhase::Error;
}

// Stop only after a client has received the terminal success status. Error and
// recovery remain reachable so the operator can inspect or recover them.
constexpr bool updatePhaseShouldStopPortalAfterStatus(UpdatePhase phase) {
  return phase == UpdatePhase::Complete;
}

constexpr size_t updateStatusTotalBytes(UpdatePhase phase,
                                        size_t packageBytes,
                                        size_t firmwareBytes) {
  return updatePhaseReportsFirmwareProgress(phase) && firmwareBytes > 0
    ? firmwareBytes
    : packageBytes;
}

constexpr bool mayStartPackageUpload(UpdatePhase phase,
                                     bool bootloaderSessionActive,
                                     bool writeTaskActive) {
  return !updatePhaseIsBusy(phase) &&
         !bootloaderSessionActive &&
         !writeTaskActive;
}

constexpr size_t packageUploadByteLimit(size_t partitionCapacity,
                                        size_t maxPackageSize) {
  return partitionCapacity < maxPackageSize
    ? partitionCapacity
    : maxPackageSize;
}

constexpr bool packageUploadSizeFits(size_t packageBytes,
                                     size_t partitionCapacity,
                                     size_t maxPackageSize) {
  return packageBytes <= packageUploadByteLimit(
    partitionCapacity, maxPackageSize);
}

constexpr bool packageUploadChunkFits(size_t storedBytes,
                                      size_t incomingBytes,
                                      size_t partitionCapacity,
                                      size_t maxPackageSize) {
  return packageUploadSizeFits(storedBytes, partitionCapacity, maxPackageSize) &&
         incomingBytes <= packageUploadByteLimit(
           partitionCapacity, maxPackageSize) - storedBytes;
}

constexpr bool postWriteIdentityMatches(uint32_t expectedVersionCode,
                                        uint32_t expectedRevision,
                                        uint32_t actualVersionCode,
                                        uint32_t actualRevision) {
  return expectedVersionCode == actualVersionCode &&
         expectedRevision == actualRevision;
}

inline uint32_t updateProgressPercent(size_t completedBytes,
                                      size_t totalBytes) {
  if (totalBytes == 0) return 0;
  if (completedBytes >= totalBytes) return 100;
  return static_cast<uint32_t>(
    (static_cast<uint64_t>(completedBytes) * 100ULL) /
    static_cast<uint64_t>(totalBytes));
}

inline size_t boundedFirmwareVerifiedBytes(size_t verifiedBytes,
                                           size_t firmwareBytes) {
  return firmwareBytes > 0 && verifiedBytes > firmwareBytes
    ? firmwareBytes
    : verifiedBytes;
}

inline bool formatLowerHex(const uint8_t* bytes,
                           size_t byteCount,
                           char* output,
                           size_t outputCapacity) {
  static const char hex[] = "0123456789abcdef";
  if (bytes == nullptr || output == nullptr || outputCapacity == 0 ||
      byteCount > (outputCapacity - 1U) / 2U) {
    return false;
  }
  for (size_t i = 0; i < byteCount; ++i) {
    output[i * 2U] = hex[bytes[i] >> 4U];
    output[i * 2U + 1U] = hex[bytes[i] & 0x0fU];
  }
  output[byteCount * 2U] = '\0';
  return true;
}

constexpr bool updateStatusIncludesPackageSha256(bool hasVerifiedPackage) {
  return hasVerifiedPackage;
}

}  // namespace stm32_update

#endif  // STM32_UPDATE_RUNTIME_CONTRACT_H
