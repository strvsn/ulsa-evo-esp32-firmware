#include "stm32_update/stm32_update_runtime_contract.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

using stm32_update::UpdatePhase;

int main() {
  // Two consecutive successful updates get a fresh Idle session each time.
  auto phase = UpdatePhase::Complete;
  for (int update = 0; update < 2; ++update) {
    assert(stm32_update::shouldResetCompletedResultForNewSession(phase, false));
    phase = UpdatePhase::Idle;
    assert(!stm32_update::updatePhaseShouldStopPortalAfterStatus(phase));
    phase = UpdatePhase::Writing;
    assert(!stm32_update::shouldResetCompletedResultForNewSession(phase, true));
    phase = UpdatePhase::Complete;
    assert(stm32_update::updatePhaseShouldStopPortalAfterStatus(phase));
  }
  for (int raw = 0; raw <= static_cast<int>(UpdatePhase::RecoveryRequired); ++raw) {
    const auto existing = static_cast<UpdatePhase>(raw);
    assert(!stm32_update::shouldResetCompletedResultForNewSession(existing, true));
    assert(stm32_update::shouldResetCompletedResultForNewSession(existing, false) ==
           (existing == UpdatePhase::Complete));
  }
  assert(stm32_update::mayStartPackageUpload(UpdatePhase::Idle, false, false));
  assert(stm32_update::mayStartPackageUpload(UpdatePhase::ReadyToWrite, false, false));
  assert(!stm32_update::mayStartPackageUpload(UpdatePhase::Receiving, false, false));
  assert(!stm32_update::mayStartPackageUpload(UpdatePhase::Writing, false, false));
  assert(!stm32_update::mayStartPackageUpload(UpdatePhase::Complete, true, false));
  assert(!stm32_update::mayStartPackageUpload(UpdatePhase::Complete, false, true));

  // HTTP Content-Length includes multipart framing. Only bytes delivered as
  // file chunks count against the package and scratch limits.
  constexpr size_t packageLimit = 384U * 1024U;
  assert(stm32_update::packageUploadSizeFits(
    packageLimit, packageLimit, packageLimit));
  assert(!stm32_update::packageUploadSizeFits(
    packageLimit + 1, packageLimit + 4096, packageLimit));
  assert(stm32_update::packageUploadChunkFits(
    0, packageLimit, packageLimit, packageLimit));
  assert(stm32_update::packageUploadChunkFits(
    packageLimit - 1, 1, packageLimit, packageLimit));
  assert(!stm32_update::packageUploadChunkFits(
    packageLimit - 1, 2, packageLimit, packageLimit));
  assert(!stm32_update::packageUploadChunkFits(
    packageLimit, 1, packageLimit, packageLimit));
  assert(!stm32_update::packageUploadChunkFits(
    packageLimit, 1, packageLimit + 4096, packageLimit));
  assert(stm32_update::packageUploadChunkFits(
    2047, 1, 2048, packageLimit));
  assert(!stm32_update::packageUploadChunkFits(
    2047, 2, 2048, packageLimit));
  assert(!stm32_update::packageUploadChunkFits(
    SIZE_MAX - 1, 4, SIZE_MAX, SIZE_MAX));

  constexpr uint32_t versionCode = 0x7E010000UL;
  assert(stm32_update::postWriteIdentityMatches(versionCode, 7, versionCode, 7));
  assert(!stm32_update::postWriteIdentityMatches(versionCode, 7, versionCode, 6));
  assert(!stm32_update::postWriteIdentityMatches(versionCode, 7, 0x7E010001UL, 7));

  // Encrypted transport overhead must not affect plaintext firmware progress.
  assert(stm32_update::updateProgressPercent(2048, 4096) == 50);
  assert(stm32_update::updateProgressPercent(4096, 4096) == 100);
  assert(stm32_update::updateProgressPercent(4124, 4096) == 100);
  assert(stm32_update::updateProgressPercent(1, 0) == 0);
  assert(stm32_update::updateStatusTotalBytes(
    UpdatePhase::Receiving, 4700, 4096) == 4700);
  assert(stm32_update::updateStatusTotalBytes(
    UpdatePhase::Writing, 4700, 4096) == 4096);
  assert(stm32_update::updateStatusTotalBytes(
    UpdatePhase::Complete, 4700, 4096) == 4096);

  uint8_t digest[32];
  for (size_t i = 0; i < sizeof(digest); ++i) {
    digest[i] = static_cast<uint8_t>(i);
  }
  char digestHex[65];
  assert(stm32_update::formatLowerHex(
    digest, sizeof(digest), digestHex, sizeof(digestHex)));
  assert(std::strcmp(
    digestHex,
    "000102030405060708090a0b0c0d0e0f"
    "101112131415161718191a1b1c1d1e1f") == 0);
  char tooSmall[64];
  assert(!stm32_update::formatLowerHex(
    digest, sizeof(digest), tooSmall, sizeof(tooSmall)));
  assert(!stm32_update::formatLowerHex(
    nullptr, sizeof(digest), digestHex, sizeof(digestHex)));
  assert(stm32_update::updateStatusIncludesPackageSha256(true));
  assert(!stm32_update::updateStatusIncludesPackageSha256(false));
  assert(stm32_update::updatePhaseRequiresPersistentRecoveryPresentation(
    UpdatePhase::RecoveryRequired));
  assert(!stm32_update::updatePhaseRequiresPersistentRecoveryPresentation(
    UpdatePhase::Error));
  assert(!stm32_update::updatePhaseRequiresPersistentRecoveryPresentation(
    UpdatePhase::ReadyToWrite));
  assert(stm32_update::updatePhaseShouldDiscardScratchOnPortalStop(
    UpdatePhase::Receiving));
  assert(stm32_update::updatePhaseShouldDiscardScratchOnPortalStop(
    UpdatePhase::PackageStored));
  assert(stm32_update::updatePhaseShouldDiscardScratchOnPortalStop(
    UpdatePhase::Verifying));
  assert(stm32_update::updatePhaseShouldDiscardScratchOnPortalStop(
    UpdatePhase::ReadyToWrite));
  assert(stm32_update::updatePhaseShouldDiscardScratchOnPortalStop(
    UpdatePhase::Error));
  assert(!stm32_update::updatePhaseShouldDiscardScratchOnPortalStop(
    UpdatePhase::RecoveryRequired));
  assert(!stm32_update::updatePhaseShouldDiscardScratchOnPortalStop(
    UpdatePhase::Writing));
  assert(stm32_update::updatePhaseShouldStopPortalAfterStatus(
    UpdatePhase::Complete));
  assert(!stm32_update::updatePhaseShouldStopPortalAfterStatus(
    UpdatePhase::Restarting));
  assert(!stm32_update::updatePhaseShouldStopPortalAfterStatus(
    UpdatePhase::Error));
  assert(!stm32_update::updatePhaseShouldStopPortalAfterStatus(
    UpdatePhase::RecoveryRequired));
  assert(stm32_update::isValidWriteRecoveryStage(
    static_cast<uint8_t>(stm32_update::WriteRecoveryStage::WriteScheduled)));
  assert(stm32_update::isValidWriteRecoveryStage(
    static_cast<uint8_t>(stm32_update::WriteRecoveryStage::RestartConfirming)));
  assert(!stm32_update::isValidWriteRecoveryStage(0xff));
  assert(stm32_update::boundedFirmwareVerifiedBytes(288262, 281520) == 281520);
  assert(stm32_update::boundedFirmwareVerifiedBytes(0, 281520) == 0);

  // Exercise overflow-safe arithmetic on the 64-bit host as well.
  const size_t largeTotal = static_cast<size_t>(UINT32_MAX) + 4096ULL;
  assert(stm32_update::updateProgressPercent(largeTotal / 2, largeTotal) == 49);
  return 0;
}
