/**
 * @file update_coordinator.h
 * @brief Profile-neutral update request, authorization, and transition coordinator.
 */

#ifndef SYSTEM_UPDATE_COORDINATOR_H
#define SYSTEM_UPDATE_COORDINATOR_H

#include <stdint.h>
#include <string.h>

namespace ulsa_update {

static constexpr uint16_t INVALID_PEER_HANDLE = 0xffffU;
static constexpr uint32_t AUTHORIZATION_TIMEOUT_MS = 60000U;
static constexpr uint32_t PREPARED_SESSION_TIMEOUT_MS = 60000U;
static constexpr size_t TARGET_MAX_LEN = 32U;
static constexpr size_t RELEASE_TAG_MAX_LEN = 64U;

enum class Purpose : uint8_t { None = 0, Esp32Ota, Stm32Update };
enum class Source : uint8_t { None = 0, PhysicalButton, Ble, BootRecovery };
enum class Operation : uint8_t {
  Read = 0,
  Prepare,
  AuthorizePending,
  Activate,
  StopOrCancel,
  ManualToggle,
  ManualBootloaderToggle,
};
enum class State : uint8_t {
  Idle = 0,
  PendingPhysicalAuthorization,
  Prepared,
  AuthorizationExpired,
  PortalStarting,
  PortalActive,
  ManualBootloader,
};
enum class Action : uint8_t {
  None = 0,
  PublishStatus,
  BeginAuthorization,
  PrepareSession,
  StartPortal,
  StopPortal,
  CancelPrepared,
  PrepareAndStartPortal,
  ToggleManualBootloader,
  RejectBusy,
  RejectUnavailable,
  RejectPeerConflict,
};
enum class Result : uint8_t {
  Ok = 0,
  Busy,
  Unavailable,
  Failed,
  AuthorizationRequired,
  AuthorizationExpired,
  PeerConflict,
};

struct Request {
  Purpose purpose;
  Source source;
  Operation operation;
  uint16_t peerHandle;
  uint32_t authorizationGeneration;
  // Informational user label echoed for wire compatibility. It is deliberately
  // excluded from authorization, replay, and package identity decisions.
  bool hasNodeLabel;
  uint8_t nodeLabel;
  char target[TARGET_MAX_LEN + 1];
  char releaseTag[RELEASE_TAG_MAX_LEN + 1];

  Request(Purpose nextPurpose = Purpose::None,
          Source nextSource = Source::None,
          Operation nextOperation = Operation::Read,
          uint16_t nextPeerHandle = INVALID_PEER_HANDLE)
    : purpose(nextPurpose), source(nextSource), operation(nextOperation),
      peerHandle(nextPeerHandle), authorizationGeneration(0),
      hasNodeLabel(false), nodeLabel(0) {
    target[0] = '\0';
    releaseTag[0] = '\0';
  }

  bool setStm32Binding(bool nextHasNodeLabel,
                       uint8_t nextNodeLabel,
                       const char* nextTarget,
                       const char* nextReleaseTag) {
    if (!nextTarget || !nextReleaseTag || nextTarget[0] == '\0' ||
        nextReleaseTag[0] == '\0' || strlen(nextTarget) > TARGET_MAX_LEN ||
        strlen(nextReleaseTag) > RELEASE_TAG_MAX_LEN) {
      return false;
    }
    hasNodeLabel = nextHasNodeLabel;
    nodeLabel = nextNodeLabel;
    strncpy(target, nextTarget, sizeof(target) - 1U);
    target[sizeof(target) - 1U] = '\0';
    strncpy(releaseTag, nextReleaseTag, sizeof(releaseTag) - 1U);
    releaseTag[sizeof(releaseTag) - 1U] = '\0';
    return true;
  }
};

struct RuntimeSnapshot {
  bool portalActive;
  bool updating;
  bool hasPreparedCredentials;
  Purpose portalPurpose;
  uint8_t connectionCount;
  bool manualBootloader;

  RuntimeSnapshot(bool nextPortalActive = false,
                  bool nextUpdating = false,
                  bool nextHasPreparedCredentials = false,
                  Purpose nextPortalPurpose = Purpose::None,
                  uint8_t nextConnectionCount = 0,
                  bool nextManualBootloader = false)
    : portalActive(nextPortalActive), updating(nextUpdating),
      hasPreparedCredentials(nextHasPreparedCredentials),
      portalPurpose(nextPortalPurpose), connectionCount(nextConnectionCount),
      manualBootloader(nextManualBootloader) {}
};

struct Decision {
  Action action;
  Result result;
  Request request;
  Decision(Action nextAction = Action::None,
           Result nextResult = Result::Ok,
           const Request& nextRequest = Request())
    : action(nextAction), result(nextResult), request(nextRequest) {}
};

class UpdateCoordinator {
public:
  UpdateCoordinator();
  bool queueButtonAuthorization(uint32_t authorizationGeneration);
  bool queueBootRecovery(Purpose purpose);
  bool queueButtonManualToggle(Purpose purpose);
  bool queueButtonManualBootloaderToggle();
  bool takeQueuedRequest(Request& request);
  Decision plan(const Request& request,
                const RuntimeSnapshot& runtime,
                uint32_t nowMs);
  void complete(const Decision& decision,
                bool succeeded,
                const RuntimeSnapshot& runtimeAfter,
                uint32_t nowMs);
  void reconcile(const RuntimeSnapshot& runtime, uint32_t nowMs);
  bool clearIfPeerDisconnected(uint16_t peerHandle);
  void clearSession();
  uint32_t remainingSeconds(uint32_t nowMs) const;

  State state() const { return _state; }
  Purpose purpose() const { return _purpose; }
  Source source() const { return _source; }
  uint16_t peerHandle() const { return _boundRequest.peerHandle; }
  uint32_t authorizationGeneration() const {
    return _boundAuthorizationGeneration;
  }
  bool physicalAuthorizationRequired() const {
    return _state == State::PendingPhysicalAuthorization;
  }
  bool physicalAuthorizationGranted() const {
    return _state == State::Prepared && _source == Source::Ble;
  }
  bool authorizationExpired() const {
    return _state == State::AuthorizationExpired;
  }

private:
  static bool purposeMatches(Purpose requested, const RuntimeSnapshot& runtime);
  static bool sameRequest(const Request& left, const Request& right);
  static bool deadlinePassed(uint32_t nowMs, uint32_t deadlineMs);
  void setIdle();

  State _state;
  Purpose _purpose;
  Source _source;
  uint32_t _deadlineMs;
  Request _boundRequest;
  uint32_t _nextAuthorizationGeneration;
  uint32_t _boundAuthorizationGeneration;
  bool _queuedRequestPending;
  Request _queuedRequest;
};

}  // namespace ulsa_update

#endif  // SYSTEM_UPDATE_COORDINATOR_H
