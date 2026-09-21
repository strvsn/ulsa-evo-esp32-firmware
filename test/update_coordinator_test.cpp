#include <assert.h>
#include <stdio.h>

#include "../include/system/update_coordinator.h"

using namespace ulsa_update;

static RuntimeSnapshot bleIdle() {
  return RuntimeSnapshot(false, false, false, Purpose::Esp32Ota, 1);
}

static Request bleRequest(Purpose purpose, Operation operation, uint16_t peer = 7) {
  return Request(purpose, Source::Ble, operation, peer);
}

static Request buttonAuthorization(const UpdateCoordinator& coordinator) {
  Request request(Purpose::None, Source::PhysicalButton,
                  Operation::AuthorizePending);
  request.authorizationGeneration = coordinator.authorizationGeneration();
  return request;
}

static void requiresPhysicalAuthorizationBeforeCredentials() {
  UpdateCoordinator coordinator;
  const Request prepare = bleRequest(Purpose::Esp32Ota, Operation::Prepare);
  Decision pending = coordinator.plan(prepare, bleIdle(), 1000);
  assert(pending.action == Action::BeginAuthorization);
  assert(pending.result == Result::AuthorizationRequired);
  coordinator.complete(pending, true, bleIdle(), 1000);
  assert(coordinator.state() == State::PendingPhysicalAuthorization);
  const uint32_t generation = coordinator.authorizationGeneration();
  assert(generation != 0U);
  assert(coordinator.remainingSeconds(1000) == 60);

  Decision duplicate = coordinator.plan(prepare, bleIdle(), 2000);
  assert(duplicate.action == Action::PublishStatus);
  assert(duplicate.result == Result::AuthorizationRequired);
  assert(coordinator.authorizationGeneration() == generation);
  assert(coordinator.remainingSeconds(2000) == 59);

  Decision authorize = coordinator.plan(
    buttonAuthorization(coordinator),
    bleIdle(), 3000);
  assert(authorize.action == Action::PrepareSession);
  assert(authorize.request.peerHandle == 7);
  coordinator.complete(
    authorize, true,
    RuntimeSnapshot(false, false, true, Purpose::Esp32Ota, 1), 3000);
  assert(coordinator.state() == State::Prepared);
  assert(coordinator.physicalAuthorizationGranted());
}

static void bindsStm32PurposePeerAndRelease() {
  UpdateCoordinator coordinator;
  Request prepare = bleRequest(Purpose::Stm32Update, Operation::Prepare, 12);
  assert(prepare.setStm32Binding(
    true, 0, "ULSA_EVO_STM32_F411", "stm32-fw-v1.2.3"));
  Decision pending = coordinator.plan(
    prepare, RuntimeSnapshot(false, false, false, Purpose::Stm32Update, 1), 10);
  assert(pending.action == Action::BeginAuthorization);

  // Node ID is an optional, mutable, duplicate user label. Changing or
  // omitting it does not create a different authorization request.
  Request relabeled = prepare;
  assert(relabeled.setStm32Binding(
    true, 255, "ULSA_EVO_STM32_F411", "stm32-fw-v1.2.3"));
  Decision relabeledDecision = coordinator.plan(
    relabeled, RuntimeSnapshot(false, false, false, Purpose::Stm32Update, 1), 15);
  assert(relabeledDecision.action == Action::PublishStatus);
  assert(relabeledDecision.result == Result::AuthorizationRequired);

  Request labelOmitted = prepare;
  assert(labelOmitted.setStm32Binding(
    false, 0, "ULSA_EVO_STM32_F411", "stm32-fw-v1.2.3"));
  Decision labelOmittedDecision = coordinator.plan(
    labelOmitted, RuntimeSnapshot(false, false, false, Purpose::Stm32Update, 1), 16);
  assert(labelOmittedDecision.action == Action::PublishStatus);
  assert(labelOmittedDecision.result == Result::AuthorizationRequired);

  Request changed = prepare;
  assert(changed.setStm32Binding(
    true, 0, "ULSA_EVO_STM32_F411", "stm32-fw-v1.2.4"));
  Decision changedDecision = coordinator.plan(
    changed, RuntimeSnapshot(false, false, false, Purpose::Stm32Update, 1), 20);
  assert(changedDecision.action == Action::RejectBusy);

  Request duplicateLabelWrongPeer = bleRequest(
    Purpose::Stm32Update, Operation::Prepare, 13);
  assert(duplicateLabelWrongPeer.setStm32Binding(
    true, 0, "ULSA_EVO_STM32_F411", "stm32-fw-v1.2.3"));
  Decision wrongPeer = coordinator.plan(
    duplicateLabelWrongPeer,
    RuntimeSnapshot(false, false, false, Purpose::Stm32Update, 1), 20);
  assert(wrongPeer.action == Action::RejectPeerConflict);
}

static void expiresPendingAndPreparedSessions() {
  UpdateCoordinator coordinator;
  const Request prepare = bleRequest(Purpose::Esp32Ota, Operation::Prepare);
  coordinator.plan(prepare, bleIdle(), 1000);
  Decision expired = coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Read), bleIdle(), 61000);
  assert(expired.result == Result::AuthorizationExpired);
  assert(coordinator.state() == State::AuthorizationExpired);
  assert(coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Read), bleIdle(), 62000).result ==
    Result::AuthorizationExpired);

  Decision pending = coordinator.plan(prepare, bleIdle(), 70000);
  Decision authorize = coordinator.plan(
    buttonAuthorization(coordinator),
    bleIdle(), 71000);
  coordinator.complete(
    authorize, true,
    RuntimeSnapshot(false, false, true, Purpose::Esp32Ota, 1), 71000);
  assert(pending.result == Result::AuthorizationRequired);
  expired = coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Read),
    RuntimeSnapshot(false, false, true, Purpose::Esp32Ota, 1), 131000);
  assert(expired.result == Result::AuthorizationExpired);
  assert(coordinator.state() == State::AuthorizationExpired);
}

static void rejectsMultiplePeersAndReplay() {
  UpdateCoordinator coordinator;
  Decision conflict = coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Prepare),
    RuntimeSnapshot(false, false, false, Purpose::Esp32Ota, 2), 0);
  assert(conflict.result == Result::PeerConflict);

  Decision pending = coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Prepare), bleIdle(), 100);
  assert(pending.result == Result::AuthorizationRequired);
  assert(coordinator.clearIfPeerDisconnected(7));
  assert(coordinator.state() == State::Idle);
  assert(!coordinator.clearIfPeerDisconnected(7));

  Decision activate = coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Activate),
    RuntimeSnapshot(false, false, true, Purpose::Esp32Ota, 1), 200);
  assert(activate.result == Result::Unavailable);
}

static void activatesOnlyTheBoundPeerAndPurpose() {
  UpdateCoordinator coordinator;
  const Request prepare = bleRequest(Purpose::Esp32Ota, Operation::Prepare, 4);
  coordinator.plan(prepare, bleIdle(), 0);
  Decision authorize = coordinator.plan(
    buttonAuthorization(coordinator),
    bleIdle(), 100);
  coordinator.complete(
    authorize, true,
    RuntimeSnapshot(false, false, true, Purpose::Esp32Ota, 1), 100);

  Decision wrongPeer = coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Activate, 5),
    RuntimeSnapshot(false, false, true, Purpose::Esp32Ota, 1), 200);
  assert(wrongPeer.result == Result::PeerConflict);

  Decision start = coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Activate, 4),
    RuntimeSnapshot(false, false, true, Purpose::Esp32Ota, 1), 200);
  assert(start.action == Action::StartPortal);
  coordinator.complete(
    start, true,
    RuntimeSnapshot(true, false, true, Purpose::Esp32Ota, 0), 200);
  assert(coordinator.state() == State::PortalActive);
}

static void buttonOnlyAuthorizesPendingRequests() {
  UpdateCoordinator coordinator;
  Request request;
  assert(!coordinator.queueButtonAuthorization(0));
  assert(!coordinator.queueButtonAuthorization(1));
  assert(!coordinator.takeQueuedRequest(request));
  const Decision pending = coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Prepare), bleIdle(), 10);
  assert(pending.result == Result::AuthorizationRequired);
  assert(coordinator.queueButtonAuthorization(
    coordinator.authorizationGeneration()));
  assert(coordinator.takeQueuedRequest(request));
  Decision authorize = coordinator.plan(request, bleIdle(), 20);
  assert(authorize.action == Action::PrepareSession);
}

static void buttonHoldCannotAuthorizeANewerRequest() {
  UpdateCoordinator coordinator;
  coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Prepare), bleIdle(), 10);
  const uint32_t oldGeneration = coordinator.authorizationGeneration();
  assert(coordinator.clearIfPeerDisconnected(7));
  coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Prepare), bleIdle(), 20);
  assert(coordinator.authorizationGeneration() != oldGeneration);
  assert(!coordinator.queueButtonAuthorization(oldGeneration));
  assert(coordinator.state() == State::PendingPhysicalAuthorization);
}

static void bootRecoveryIsAnExplicitSource() {
  UpdateCoordinator coordinator;
  Request request;
  assert(coordinator.queueBootRecovery(Purpose::Stm32Update));
  assert(coordinator.takeQueuedRequest(request));
  assert(request.source == Source::BootRecovery);
  assert(request.purpose == Purpose::Stm32Update);
  Decision start = coordinator.plan(request, RuntimeSnapshot(), 0);
  assert(start.action == Action::PrepareAndStartPortal);
  coordinator.complete(
    start, true,
    RuntimeSnapshot(true, false, true, Purpose::Stm32Update, 0), 10);
  assert(coordinator.state() == State::PortalActive);
  assert(coordinator.source() == Source::BootRecovery);
}

static void manualBootloaderIsExclusiveWithOta() {
  UpdateCoordinator coordinator;
  Request queued;
  assert(coordinator.queueButtonManualBootloaderToggle());
  assert(coordinator.takeQueuedRequest(queued));
  Decision enter = coordinator.plan(queued, RuntimeSnapshot(), 0);
  assert(enter.action == Action::ToggleManualBootloader);
  coordinator.complete(
    enter, true,
    RuntimeSnapshot(false, false, false, Purpose::None, 0, true), 1);
  assert(coordinator.state() == State::ManualBootloader);

  Decision prepare = coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Prepare),
    RuntimeSnapshot(false, false, false, Purpose::None, 1, true), 2);
  assert(prepare.action == Action::RejectBusy);

  assert(coordinator.queueButtonManualBootloaderToggle());
  assert(coordinator.takeQueuedRequest(queued));
  Decision leave = coordinator.plan(
    queued, RuntimeSnapshot(false, false, false, Purpose::None, 0, true), 3);
  assert(leave.action == Action::ToggleManualBootloader);
  coordinator.complete(leave, true, RuntimeSnapshot(), 4);
  assert(coordinator.state() == State::Idle);

  coordinator.plan(
    bleRequest(Purpose::Esp32Ota, Operation::Prepare), bleIdle(), 5);
  assert(coordinator.queueButtonManualBootloaderToggle());
  assert(coordinator.takeQueuedRequest(queued));
  Decision blocked = coordinator.plan(queued, bleIdle(), 6);
  assert(blocked.action == Action::RejectBusy);
}

int main() {
  requiresPhysicalAuthorizationBeforeCredentials();
  bindsStm32PurposePeerAndRelease();
  expiresPendingAndPreparedSessions();
  rejectsMultiplePeersAndReplay();
  activatesOnlyTheBoundPeerAndPurpose();
  buttonOnlyAuthorizesPendingRequests();
  buttonHoldCannotAuthorizeANewerRequest();
  bootRecoveryIsAnExplicitSource();
  manualBootloaderIsExclusiveWithOta();
  puts("update_coordinator_test: passed");
  return 0;
}
