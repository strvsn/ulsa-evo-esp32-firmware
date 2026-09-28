/**
 * @file update_coordinator.cpp
 * @brief Profile-neutral update request, authorization, and transition coordinator.
 */

#include "system/update_coordinator.h"

namespace ulsa_update {

UpdateCoordinator::UpdateCoordinator()
  : _state(State::Idle), _purpose(Purpose::None), _source(Source::None),
    _deadlineMs(0), _boundRequest(), _nextAuthorizationGeneration(0),
    _boundAuthorizationGeneration(0), _queuedRequestPending(false),
    _queuedRequest() {}

bool UpdateCoordinator::queueButtonAuthorization(uint32_t authorizationGeneration) {
  if (_queuedRequestPending || _state != State::PendingPhysicalAuthorization ||
      authorizationGeneration == 0U ||
      authorizationGeneration != _boundAuthorizationGeneration) return false;
  _queuedRequest = Request(Purpose::None, Source::PhysicalButton,
                           Operation::AuthorizePending);
  _queuedRequest.authorizationGeneration = authorizationGeneration;
  _queuedRequestPending = true;
  return true;
}

bool UpdateCoordinator::queueBootRecovery(Purpose purpose) {
  if (_queuedRequestPending || purpose == Purpose::None) return false;
  _queuedRequest = Request(purpose, Source::BootRecovery, Operation::ManualToggle);
  _queuedRequestPending = true;
  return true;
}

bool UpdateCoordinator::queueButtonManualToggle(Purpose purpose) {
  if (_queuedRequestPending || purpose == Purpose::None) return false;
  _queuedRequest = Request(purpose, Source::PhysicalButton, Operation::ManualToggle);
  _queuedRequestPending = true;
  return true;
}

bool UpdateCoordinator::queueButtonManualBootloaderToggle() {
  if (_queuedRequestPending) return false;
  _queuedRequest = Request(Purpose::Stm32Update, Source::PhysicalButton,
                           Operation::ManualBootloaderToggle);
  _queuedRequestPending = true;
  return true;
}

bool UpdateCoordinator::takeQueuedRequest(Request& request) {
  if (!_queuedRequestPending) return false;
  request = _queuedRequest;
  _queuedRequest = Request();
  _queuedRequestPending = false;
  return true;
}

bool UpdateCoordinator::purposeMatches(Purpose requested,
                                       const RuntimeSnapshot& runtime) {
  return requested != Purpose::None && runtime.portalPurpose == requested;
}

bool UpdateCoordinator::sameRequest(const Request& left, const Request& right) {
  return left.purpose == right.purpose && left.source == right.source &&
         left.peerHandle == right.peerHandle &&
         strcmp(left.target, right.target) == 0 &&
         strcmp(left.releaseTag, right.releaseTag) == 0;
}

bool UpdateCoordinator::deadlinePassed(uint32_t nowMs, uint32_t deadlineMs) {
  return deadlineMs != 0U && static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

Decision UpdateCoordinator::plan(const Request& request,
                                 const RuntimeSnapshot& runtime,
                                 uint32_t nowMs) {
  if ((_state == State::PendingPhysicalAuthorization ||
       _state == State::Prepared) && deadlinePassed(nowMs, _deadlineMs)) {
    const Request expired = _boundRequest;
    _state = State::AuthorizationExpired;
    _deadlineMs = 0;
    return Decision(Action::PublishStatus, Result::AuthorizationExpired, expired);
  }

  switch (request.operation) {
    case Operation::Read:
      if (_state == State::AuthorizationExpired) {
        return Decision(Action::PublishStatus, Result::AuthorizationExpired,
                        _boundRequest);
      }
      return _state == State::PendingPhysicalAuthorization
        ? Decision(Action::PublishStatus, Result::AuthorizationRequired, _boundRequest)
        : Decision(Action::PublishStatus, Result::Ok, request);

    case Operation::Prepare:
      if (runtime.portalActive || runtime.updating || runtime.manualBootloader ||
          _state == State::PortalStarting || _state == State::ManualBootloader ||
          _state == State::PortalActive || _state == State::Prepared) {
        return Decision(Action::RejectBusy, Result::Busy, request);
      }
      if (request.source != Source::Ble ||
          request.peerHandle == INVALID_PEER_HANDLE || runtime.connectionCount != 1U) {
        return Decision(Action::RejectPeerConflict, Result::PeerConflict, request);
      }
      if (_state == State::PendingPhysicalAuthorization) {
        if (sameRequest(request, _boundRequest)) {
          return Decision(Action::PublishStatus, Result::AuthorizationRequired,
                          _boundRequest);
        }
        const bool samePeer = request.peerHandle == _boundRequest.peerHandle;
        return Decision(samePeer ? Action::RejectBusy : Action::RejectPeerConflict,
                        samePeer ? Result::Busy : Result::PeerConflict, request);
      }
      _state = State::PendingPhysicalAuthorization;
      _purpose = request.purpose;
      _source = request.source;
      _boundRequest = request;
      ++_nextAuthorizationGeneration;
      if (_nextAuthorizationGeneration == 0U) ++_nextAuthorizationGeneration;
      _boundAuthorizationGeneration = _nextAuthorizationGeneration;
      _boundRequest.authorizationGeneration = _boundAuthorizationGeneration;
      _deadlineMs = nowMs + AUTHORIZATION_TIMEOUT_MS;
      return Decision(Action::BeginAuthorization, Result::AuthorizationRequired,
                      _boundRequest);

    case Operation::AuthorizePending:
      if (_state != State::PendingPhysicalAuthorization ||
          request.authorizationGeneration == 0U ||
          request.authorizationGeneration != _boundAuthorizationGeneration) {
        return Decision(Action::RejectUnavailable, Result::Unavailable, request);
      }
      if (runtime.connectionCount != 1U ||
          _boundRequest.peerHandle == INVALID_PEER_HANDLE) {
        const Request conflicted = _boundRequest;
        setIdle();
        return Decision(Action::RejectPeerConflict, Result::PeerConflict, conflicted);
      }
      return Decision(Action::PrepareSession, Result::Ok, _boundRequest);

    case Operation::Activate:
      if (runtime.portalActive || runtime.updating) {
        return Decision(Action::RejectBusy, Result::Busy, request);
      }
      if (_state != State::Prepared || !runtime.hasPreparedCredentials ||
          !purposeMatches(request.purpose, runtime)) {
        return Decision(Action::RejectUnavailable, Result::Unavailable, request);
      }
      if (request.peerHandle != _boundRequest.peerHandle ||
          runtime.connectionCount != 1U) {
        return Decision(Action::RejectPeerConflict, Result::PeerConflict, request);
      }
      _state = State::PortalStarting;
      return Decision(Action::StartPortal, Result::Ok, _boundRequest);

    case Operation::StopOrCancel:
      if (_source == Source::Ble && request.peerHandle != _boundRequest.peerHandle) {
        return Decision(Action::RejectPeerConflict, Result::PeerConflict, request);
      }
      if (runtime.portalActive) {
        if (!purposeMatches(request.purpose, runtime)) {
          return Decision(Action::RejectBusy, Result::Busy, request);
        }
        return Decision(Action::StopPortal, Result::Ok, request);
      }
      return Decision(Action::CancelPrepared, Result::Ok, request);

    case Operation::ManualToggle:
      if (runtime.updating || runtime.manualBootloader ||
          _state == State::PendingPhysicalAuthorization ||
          _state == State::Prepared || _state == State::ManualBootloader) {
        return Decision(Action::RejectBusy, Result::Busy, request);
      }
      if (runtime.portalActive) return Decision(Action::StopPortal, Result::Ok, request);
      _state = State::PortalStarting;
      _purpose = request.purpose;
      _source = request.source;
      _boundRequest = request;
      return Decision(Action::PrepareAndStartPortal, Result::Ok, request);

    case Operation::ManualBootloaderToggle:
      if (runtime.portalActive || runtime.updating ||
          _state == State::PendingPhysicalAuthorization ||
          _state == State::Prepared || _state == State::PortalStarting ||
          _state == State::PortalActive) {
        return Decision(Action::RejectBusy, Result::Busy, request);
      }
      return Decision(Action::ToggleManualBootloader, Result::Ok, request);

    default:
      return Decision(Action::None, Result::Failed, request);
  }
}

void UpdateCoordinator::complete(const Decision& decision,
                                 bool succeeded,
                                 const RuntimeSnapshot& runtimeAfter,
                                 uint32_t nowMs) {
  if (!succeeded) {
    if (decision.action == Action::BeginAuthorization ||
        decision.action == Action::PrepareSession ||
        decision.action == Action::StartPortal) {
      setIdle();
      return;
    }
    reconcile(runtimeAfter, nowMs);
    return;
  }
  switch (decision.action) {
    case Action::BeginAuthorization:
      break;
    case Action::PrepareSession:
      _state = State::Prepared;
      _purpose = decision.request.purpose;
      _source = decision.request.source;
      _boundRequest = decision.request;
      _deadlineMs = nowMs + PREPARED_SESSION_TIMEOUT_MS;
      break;
    case Action::StartPortal:
    case Action::PrepareAndStartPortal:
      _state = State::PortalActive;
      _purpose = decision.request.purpose;
      _source = decision.request.source;
      _deadlineMs = 0;
      break;
    case Action::ToggleManualBootloader:
      if (runtimeAfter.manualBootloader) {
        _state = State::ManualBootloader;
        _purpose = Purpose::Stm32Update;
        _source = Source::PhysicalButton;
        _deadlineMs = 0;
      } else {
        setIdle();
      }
      break;
    case Action::StopPortal:
    case Action::CancelPrepared:
      setIdle();
      break;
    default:
      reconcile(runtimeAfter, nowMs);
      break;
  }
}

void UpdateCoordinator::reconcile(const RuntimeSnapshot& runtime, uint32_t nowMs) {
  if (_state == State::AuthorizationExpired) return;
  if ((_state == State::PendingPhysicalAuthorization || _state == State::Prepared) &&
      !deadlinePassed(nowMs, _deadlineMs)) return;
  if (runtime.portalActive || runtime.updating) {
    _state = State::PortalActive;
    _purpose = runtime.portalPurpose;
    _deadlineMs = 0;
    return;
  }
  if (runtime.manualBootloader) {
    _state = State::ManualBootloader;
    _purpose = Purpose::Stm32Update;
    _source = Source::PhysicalButton;
    _deadlineMs = 0;
    return;
  }
  if (runtime.hasPreparedCredentials && _state == State::Prepared) return;
  setIdle();
}

bool UpdateCoordinator::clearIfPeerDisconnected(uint16_t peerHandle) {
  if (_source != Source::Ble || _boundRequest.peerHandle != peerHandle) return false;
  setIdle();
  return true;
}

void UpdateCoordinator::clearSession() {
  setIdle();
}

uint32_t UpdateCoordinator::remainingSeconds(uint32_t nowMs) const {
  if ((_state != State::PendingPhysicalAuthorization && _state != State::Prepared) ||
      _deadlineMs == 0U || deadlinePassed(nowMs, _deadlineMs)) return 0U;
  return ((_deadlineMs - nowMs) + 999U) / 1000U;
}

void UpdateCoordinator::setIdle() {
  _state = State::Idle;
  _purpose = Purpose::None;
  _source = Source::None;
  _deadlineMs = 0;
  _boundRequest = Request();
  _boundAuthorizationGeneration = 0;
}

}  // namespace ulsa_update
