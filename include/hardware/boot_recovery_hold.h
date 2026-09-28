/**
 * @file boot_recovery_hold.h
 * @brief Boot-only, release-exclusive recovery hold classifier.
 */

#ifndef HARDWARE_BOOT_RECOVERY_HOLD_H
#define HARDWARE_BOOT_RECOVERY_HOLD_H

#include <stdint.h>

enum class BootRecoveryHoldEvent : uint8_t {
  Waiting = 0,
  HoldConfirmed,
  EnterRecovery,
  Cancelled,
};

class BootRecoveryHold {
public:
  explicit BootRecoveryHold(uint32_t holdMs)
    : _holdMs(holdMs), _started(false), _confirmed(false), _finished(false),
      _pressedAtMs(0) {}

  BootRecoveryHoldEvent sample(bool pressed, uint32_t nowMs) {
    if (_finished) {
      return _confirmed ? BootRecoveryHoldEvent::EnterRecovery
                        : BootRecoveryHoldEvent::Cancelled;
    }

    if (!_started) {
      _started = true;
      _pressedAtMs = nowMs;
      if (!pressed) {
        _finished = true;
        return BootRecoveryHoldEvent::Cancelled;
      }
      return BootRecoveryHoldEvent::Waiting;
    }

    if (pressed) {
      if (!_confirmed && static_cast<uint32_t>(nowMs - _pressedAtMs) >= _holdMs) {
        _confirmed = true;
        return BootRecoveryHoldEvent::HoldConfirmed;
      }
      return _confirmed ? BootRecoveryHoldEvent::HoldConfirmed
                        : BootRecoveryHoldEvent::Waiting;
    }

    _finished = true;
    return _confirmed ? BootRecoveryHoldEvent::EnterRecovery
                      : BootRecoveryHoldEvent::Cancelled;
  }

  bool isConfirmed() const { return _confirmed; }
  bool isFinished() const { return _finished; }

private:
  uint32_t _holdMs;
  bool _started;
  bool _confirmed;
  bool _finished;
  uint32_t _pressedAtMs;
};

#endif  // HARDWARE_BOOT_RECOVERY_HOLD_H
