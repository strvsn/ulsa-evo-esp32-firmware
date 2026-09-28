#include <assert.h>
#include <stdio.h>

#include "system/system_ui_state.h"

static SystemUiState update(SystemUiUpdatePurpose purpose,
                            SystemUiUpdateStage stage) {
  SystemUiState state;
  state.updatePurpose = purpose;
  state.updateStage = stage;
  return state;
}

static void updateAuthorizationButtonPresentation() {
  SystemUiState demoPending = update(
    SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::AuthorizationPending);
  applyUpdateAuthorizationButtonPresentation(demoPending, false, false);
  assert(demoPending.updatePurpose == SystemUiUpdatePurpose::Esp32);
  assert(demoPending.updateStage == SystemUiUpdateStage::AuthorizationPending);
  assert(resolveLedPresentation(demoPending) ==
    LedPresentation::UpdateAuthorizationPending);

  SystemUiState demoConfirmed = update(
    SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::AuthorizationPending);
  applyUpdateAuthorizationButtonPresentation(demoConfirmed, true, false);
  assert(demoConfirmed.updateStage ==
    SystemUiUpdateStage::AuthorizationHoldConfirmed);
  assert(resolveLedPresentation(demoConfirmed) ==
    LedPresentation::UpdateAuthorizationGranted);

  SystemUiState initialIdle;
  applyUpdateAuthorizationButtonPresentation(initialIdle, true, true);
  assert(initialIdle.updatePurpose == SystemUiUpdatePurpose::Esp32);
  assert(initialIdle.updateStage ==
    SystemUiUpdateStage::AuthorizationHoldConfirmed);
  assert(resolveLedPresentation(initialIdle) ==
    LedPresentation::UpdateAuthorizationGranted);

  SystemUiState stm32Pending = update(
    SystemUiUpdatePurpose::Stm32,
    SystemUiUpdateStage::AuthorizationPending);
  applyUpdateAuthorizationButtonPresentation(stm32Pending, false, false);
  assert(resolveLedPresentation(stm32Pending) ==
    LedPresentation::UpdateAuthorizationPending);

  SystemUiState stm32Confirmed = update(
    SystemUiUpdatePurpose::Stm32,
    SystemUiUpdateStage::AuthorizationPending);
  applyUpdateAuthorizationButtonPresentation(stm32Confirmed, true, false);
  assert(stm32Confirmed.updatePurpose == SystemUiUpdatePurpose::Stm32);
  assert(stm32Confirmed.updateStage ==
    SystemUiUpdateStage::AuthorizationHoldConfirmed);
  assert(resolveLedPresentation(stm32Confirmed) ==
    LedPresentation::UpdateAuthorizationGranted);
}

static void portalClientConnectionPresentation() {
  SystemUiState esp32Waiting = update(
    SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::Portal);
  applyPortalClientConnectionPresentation(esp32Waiting, true, false);
  assert(esp32Waiting.updateStage == SystemUiUpdateStage::AuthorizationGranted);
  assert(resolveLedPresentation(esp32Waiting) ==
    LedPresentation::UpdateAuthorizationGranted);

  SystemUiState esp32Connected = update(
    SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::Portal);
  applyPortalClientConnectionPresentation(esp32Connected, true, true);
  assert(resolveLedPresentation(esp32Connected) == LedPresentation::UpdatePortal);

  SystemUiState stm32Waiting = update(
    SystemUiUpdatePurpose::Stm32,
    SystemUiUpdateStage::Portal);
  applyPortalClientConnectionPresentation(stm32Waiting, true, false);
  assert(resolveLedPresentation(stm32Waiting) ==
    LedPresentation::UpdateAuthorizationGranted);

  SystemUiState recoveryPortal = update(
    SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::Portal);
  applyPortalClientConnectionPresentation(recoveryPortal, false, false);
  assert(resolveLedPresentation(recoveryPortal) == LedPresentation::UpdatePortal);
}

static void stm32RecoveryPortalPresentation() {
  SystemUiState withoutPortal = update(
    SystemUiUpdatePurpose::Stm32,
    stm32RecoveryPresentationStage(false));
  assert(resolveLedPresentation(withoutPortal) == LedPresentation::UpdateError);

  SystemUiState waitingForAssociation = update(
    SystemUiUpdatePurpose::Stm32,
    stm32RecoveryPresentationStage(true));
  applyPortalClientConnectionPresentation(
    waitingForAssociation, true, false);
  assert(resolveLedPresentation(waitingForAssociation) ==
    LedPresentation::UpdateAuthorizationGranted);

  SystemUiState associated = update(
    SystemUiUpdatePurpose::Stm32,
    stm32RecoveryPresentationStage(true));
  applyPortalClientConnectionPresentation(associated, true, true);
  assert(resolveLedPresentation(associated) == LedPresentation::UpdatePortal);
}

int main() {
  updateAuthorizationButtonPresentation();
  portalClientConnectionPresentation();
  stm32RecoveryPortalPresentation();
  SystemUiState state;
  assert(resolveLedPresentation(state) == LedPresentation::BaseI2cDisconnected);
  state.initialProfile = true;
  assert(resolveLedPresentation(state) == LedPresentation::BaseI2cDisconnected);
  state.initialFactoryReady = true;
  assert(resolveLedPresentation(state) == LedPresentation::InitialReady);
  state.initialFactoryError = true;
  assert(resolveLedPresentation(state) == LedPresentation::UpdateError);
  state.updatePurpose = SystemUiUpdatePurpose::Stm32;
  state.updateStage = SystemUiUpdateStage::Writing;
  assert(resolveLedPresentation(state) == LedPresentation::UpdateError);
  state.initialFactoryError = false;
  state.updatePurpose = SystemUiUpdatePurpose::None;
  state.updateStage = SystemUiUpdateStage::Idle;
  state.initialProfile = false;
  state.bleConnected = true;
  assert(resolveLedPresentation(state) == LedPresentation::BaseI2cConnected);
  state.manualBootloader = true;
  assert(resolveLedPresentation(state) == LedPresentation::ManualBootloader);
  state.manualBootloader = false;
  state.baseMode = SystemUiBaseMode::Command;
  state.i2cReturnHoldConfirmed = true;
  assert(resolveLedPresentation(state) == LedPresentation::BaseI2cDisconnected);
  state.updatePurpose = SystemUiUpdatePurpose::Esp32;
  state.updateStage = SystemUiUpdateStage::AuthorizationPending;
  assert(resolveLedPresentation(state) == LedPresentation::UpdateAuthorizationPending);

  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::AuthorizationPending)) ==
    LedPresentation::UpdateAuthorizationPending);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::AuthorizationHoldConfirmed)) ==
    LedPresentation::UpdateAuthorizationGranted);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::AuthorizationGranted)) ==
    LedPresentation::UpdateAuthorizationGranted);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::Portal)) == LedPresentation::UpdatePortal);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::Transfer)) == LedPresentation::UpdateTransfer);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::Error)) == LedPresentation::UpdateError);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Esp32,
    SystemUiUpdateStage::Recovery)) == LedPresentation::UpdateRecovery);

  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Stm32,
    SystemUiUpdateStage::AuthorizationPending)) ==
    LedPresentation::UpdateAuthorizationPending);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Stm32,
    SystemUiUpdateStage::AuthorizationHoldConfirmed)) ==
    LedPresentation::UpdateAuthorizationGranted);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Stm32,
    SystemUiUpdateStage::AuthorizationGranted)) ==
    LedPresentation::UpdateAuthorizationGranted);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Stm32,
    SystemUiUpdateStage::Portal)) == LedPresentation::UpdatePortal);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Stm32,
    SystemUiUpdateStage::Transfer)) == LedPresentation::UpdateTransfer);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Stm32,
    SystemUiUpdateStage::Writing)) == LedPresentation::UpdateWriting);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Stm32,
    SystemUiUpdateStage::Error)) == LedPresentation::UpdateError);
  assert(resolveLedPresentation(update(SystemUiUpdatePurpose::Stm32,
    SystemUiUpdateStage::Recovery)) == LedPresentation::UpdateRecovery);

  puts("system_ui_state_test: passed");
  return 0;
}
