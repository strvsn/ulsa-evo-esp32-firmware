/**
 * @file system_ui_runtime_adapter.h
 * @brief Shared OTA/coordinator state adapter for both firmware profiles.
 */

#ifndef SYSTEM_SYSTEM_UI_RUNTIME_ADAPTER_H
#define SYSTEM_SYSTEM_UI_RUNTIME_ADAPTER_H

#include "ota/ota_manager.h"
#include "system/system_ui_state.h"
#include "system/update_coordinator.h"

inline SystemUiUpdatePurpose systemUiPurpose(ulsa_update::Purpose purpose) {
  if (purpose == ulsa_update::Purpose::Stm32Update) {
    return SystemUiUpdatePurpose::Stm32;
  }
  if (purpose == ulsa_update::Purpose::Esp32Ota) {
    return SystemUiUpdatePurpose::Esp32;
  }
  return SystemUiUpdatePurpose::None;
}

inline void populateSystemUiUpdateState(
    SystemUiState& state,
    const OtaManager& otaManager,
    const ulsa_update::UpdateCoordinator& coordinator) {
  const bool recoveryPortal = otaManager.isRecoveryPortal();
  const bool stm32Purpose =
    otaManager.getPortalPurpose() == OtaPortalPurpose::Stm32Update;

  if (coordinator.physicalAuthorizationRequired()) {
    state.updatePurpose = systemUiPurpose(coordinator.purpose());
    state.updateStage = SystemUiUpdateStage::AuthorizationPending;
    return;
  }
  if (coordinator.physicalAuthorizationGranted()) {
    state.updatePurpose = systemUiPurpose(coordinator.purpose());
    state.updateStage = SystemUiUpdateStage::AuthorizationGranted;
    return;
  }

  const auto phase = otaManager.getStm32UpdatePhase();
  if (stm32_update::completedUpdateReturnedToMeasurement(
        phase, otaManager.isPortalActive(),
        otaManager.isStm32BootloaderSessionActive())) {
    return;
  }
  const bool persistentStm32Recovery =
    stm32_update::updatePhaseRequiresPersistentRecoveryPresentation(phase);
  const bool activeStm32Phase =
    otaManager.isStm32BootloaderSessionActive() ||
    phase == stm32_update::UpdatePhase::Receiving ||
    phase == stm32_update::UpdatePhase::PackageStored ||
    phase == stm32_update::UpdatePhase::Verifying ||
    phase == stm32_update::UpdatePhase::Writing ||
    phase == stm32_update::UpdatePhase::VerifyingFlash ||
    phase == stm32_update::UpdatePhase::Restarting ||
    persistentStm32Recovery ||
    (stm32Purpose &&
      (phase == stm32_update::UpdatePhase::ReadyToWrite ||
       phase == stm32_update::UpdatePhase::Complete ||
       phase == stm32_update::UpdatePhase::Error));

  if ((stm32Purpose && (activeStm32Phase ||
       otaManager.getState() == OTA_STATE_ERROR || otaManager.isPortalActive())) ||
      persistentStm32Recovery) {
    state.updatePurpose = SystemUiUpdatePurpose::Stm32;
    switch (phase) {
      case stm32_update::UpdatePhase::Receiving:
      case stm32_update::UpdatePhase::PackageStored:
      case stm32_update::UpdatePhase::Verifying:
        state.updateStage = SystemUiUpdateStage::Transfer;
        return;
      case stm32_update::UpdatePhase::ReadyToWrite:
      case stm32_update::UpdatePhase::Complete:
        state.updateStage = SystemUiUpdateStage::Ready;
        return;
      case stm32_update::UpdatePhase::Writing:
      case stm32_update::UpdatePhase::VerifyingFlash:
      case stm32_update::UpdatePhase::Restarting:
        state.updateStage = SystemUiUpdateStage::Writing;
        return;
      case stm32_update::UpdatePhase::Error:
        state.updateStage = SystemUiUpdateStage::Error;
        return;
      case stm32_update::UpdatePhase::RecoveryRequired:
        state.updateStage = stm32RecoveryPresentationStage(
          stm32Purpose && otaManager.isPortalActive());
        return;
      case stm32_update::UpdatePhase::Idle:
      default:
        state.updateStage = otaManager.getState() == OTA_STATE_ERROR
          ? SystemUiUpdateStage::Error
          : (recoveryPortal ? SystemUiUpdateStage::Recovery
                            : SystemUiUpdateStage::Portal);
        return;
    }
  }

  if (otaManager.isUpdating()) {
    state.updatePurpose = SystemUiUpdatePurpose::Esp32;
    state.updateStage = SystemUiUpdateStage::Transfer;
  } else if (recoveryPortal) {
    state.updatePurpose = stm32Purpose ? SystemUiUpdatePurpose::Stm32
                                      : SystemUiUpdatePurpose::Esp32;
    state.updateStage = SystemUiUpdateStage::Recovery;
  } else if (otaManager.isPortalActive() ||
             coordinator.state() == ulsa_update::State::PortalStarting) {
    state.updatePurpose = systemUiPurpose(coordinator.purpose());
    if (state.updatePurpose == SystemUiUpdatePurpose::None) {
      state.updatePurpose = stm32Purpose ? SystemUiUpdatePurpose::Stm32
                                        : SystemUiUpdatePurpose::Esp32;
    }
    state.updateStage = SystemUiUpdateStage::Portal;
  } else if (otaManager.getState() == OTA_STATE_ERROR) {
    state.updatePurpose = stm32Purpose ? SystemUiUpdatePurpose::Stm32
                                      : SystemUiUpdatePurpose::Esp32;
    state.updateStage = SystemUiUpdateStage::Error;
  }
}

#endif  // SYSTEM_SYSTEM_UI_RUNTIME_ADAPTER_H
