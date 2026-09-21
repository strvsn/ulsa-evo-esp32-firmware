/**
 * @file system_ui_state.h
 * @brief Hardware-independent state to LED-presentation resolver.
 */

#ifndef SYSTEM_SYSTEM_UI_STATE_H
#define SYSTEM_SYSTEM_UI_STATE_H

#include <stdint.h>

enum class SystemUiBaseMode : uint8_t {
  I2cMeasure = 0,
  Command,
  UartBridge,
  UartMeasure,
};

enum class SystemUiUpdatePurpose : uint8_t {
  None = 0,
  Esp32,
  Stm32,
};

enum class SystemUiUpdateStage : uint8_t {
  Idle = 0,
  AuthorizationPending,
  AuthorizationHoldConfirmed,
  AuthorizationGranted,
  Portal,
  Transfer,
  Ready,
  Writing,
  Error,
  Recovery,
};

enum class LedPresentation : uint8_t {
  BaseI2cDisconnected = 0,
  InitialReady,
  BaseI2cConnected,
  BaseCommand,
  BaseUartBridge,
  BaseUartDisconnected,
  BaseUartConnected,
  ManualBootloader,
  UpdateAuthorizationPending,
  UpdateAuthorizationGranted,
  UpdatePortal,
  UpdateTransfer,
  UpdateReady,
  UpdateWriting,
  UpdateError,
  UpdateRecovery,
};

struct SystemUiState {
  SystemUiBaseMode baseMode;
  bool initialProfile;
  bool initialFactoryReady;
  bool initialFactoryError;
  bool bleConnected;
  bool manualBootloader;
  bool i2cReturnHoldConfirmed;
  SystemUiUpdatePurpose updatePurpose;
  SystemUiUpdateStage updateStage;

  SystemUiState()
    : baseMode(SystemUiBaseMode::I2cMeasure), initialProfile(false),
      initialFactoryReady(false), initialFactoryError(false), bleConnected(false),
      manualBootloader(false), i2cReturnHoldConfirmed(false),
      updatePurpose(SystemUiUpdatePurpose::None),
      updateStage(SystemUiUpdateStage::Idle) {}
};

/**
 * Apply the update authorization button preview without changing hardware.
 * Both update purposes use the same yellow pending presentation and advance to
 * the same semantic HoldConfirmed stage at the threshold. Initial has no BLE
 * prepare request, so only its idle hold may create an ESP32 preview.
 */
inline void applyUpdateAuthorizationButtonPresentation(
    SystemUiState& state,
    bool holdConfirmed,
    bool allowIdleEsp32HoldPreview) {
  if (state.updatePurpose != SystemUiUpdatePurpose::None &&
      state.updateStage == SystemUiUpdateStage::AuthorizationPending) {
    if (holdConfirmed) {
      state.updateStage = SystemUiUpdateStage::AuthorizationHoldConfirmed;
    }
    return;
  }
  if (holdConfirmed && allowIdleEsp32HoldPreview &&
      state.updateStage == SystemUiUpdateStage::Idle &&
      !state.manualBootloader) {
    state.updatePurpose = SystemUiUpdatePurpose::Esp32;
    state.updateStage = SystemUiUpdateStage::AuthorizationHoldConfirmed;
  }
}

/**
 * Keep the confirmed color visible while an app-driven SoftAP is waiting for
 * the phone to finish its Wi-Fi transition. Portal color begins only after a
 * station is associated. This is presentation-only; token and HTTP checks
 * remain independent security gates.
 */
inline void applyPortalClientConnectionPresentation(
    SystemUiState& state,
    bool appDrivenPortal,
    bool portalClientConnected) {
  if (appDrivenPortal && !portalClientConnected &&
      state.updatePurpose != SystemUiUpdatePurpose::None &&
      state.updateStage == SystemUiUpdateStage::Portal) {
    state.updateStage = SystemUiUpdateStage::AuthorizationGranted;
  }
}

/**
 * A retained STM32 write-recovery state owns the LED only while no STM32
 * portal is serving it. Once that portal is active, the normal portal
 * association presentation takes priority: white before association and
 * orange after association.
 */
inline SystemUiUpdateStage stm32RecoveryPresentationStage(bool portalActive) {
  return portalActive ? SystemUiUpdateStage::Portal
                      : SystemUiUpdateStage::Error;
}

inline LedPresentation resolveLedPresentation(const SystemUiState& state) {
  // A failed factory gate must remain visible and can never fall through to
  // the green InitialReady presentation or a transient update presentation.
  if (state.initialProfile && state.initialFactoryError) {
    return LedPresentation::UpdateError;
  }

  if (state.updateStage != SystemUiUpdateStage::Idle &&
      state.updatePurpose != SystemUiUpdatePurpose::None) {
    switch (state.updateStage) {
      case SystemUiUpdateStage::AuthorizationPending:
        return LedPresentation::UpdateAuthorizationPending;
      case SystemUiUpdateStage::AuthorizationHoldConfirmed:
      case SystemUiUpdateStage::AuthorizationGranted:
        return LedPresentation::UpdateAuthorizationGranted;
      case SystemUiUpdateStage::Portal:
        return LedPresentation::UpdatePortal;
      case SystemUiUpdateStage::Transfer:
        return LedPresentation::UpdateTransfer;
      case SystemUiUpdateStage::Ready:
        return LedPresentation::UpdateReady;
      case SystemUiUpdateStage::Writing:
        return LedPresentation::UpdateWriting;
      case SystemUiUpdateStage::Error:
        return LedPresentation::UpdateError;
      case SystemUiUpdateStage::Recovery:
        return LedPresentation::UpdateRecovery;
      case SystemUiUpdateStage::Idle:
      default:
        break;
    }
  }

  if (state.manualBootloader) return LedPresentation::ManualBootloader;
  if (state.i2cReturnHoldConfirmed) return LedPresentation::BaseI2cDisconnected;
  if (state.initialProfile &&
      state.initialFactoryReady &&
      state.baseMode == SystemUiBaseMode::I2cMeasure) {
    return LedPresentation::InitialReady;
  }
  switch (state.baseMode) {
    case SystemUiBaseMode::Command:
      return LedPresentation::BaseCommand;
    case SystemUiBaseMode::UartBridge:
      return LedPresentation::BaseUartBridge;
    case SystemUiBaseMode::UartMeasure:
      return state.bleConnected ? LedPresentation::BaseUartConnected
                                : LedPresentation::BaseUartDisconnected;
    case SystemUiBaseMode::I2cMeasure:
    default:
      return state.bleConnected ? LedPresentation::BaseI2cConnected
                                : LedPresentation::BaseI2cDisconnected;
  }
}

#endif  // SYSTEM_SYSTEM_UI_STATE_H
