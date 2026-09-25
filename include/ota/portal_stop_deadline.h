/**
 * @file portal_stop_deadline.h
 * @brief Host-testable grace period for stopping a portal after an HTTP reply
 */

#ifndef PORTAL_STOP_DEADLINE_H
#define PORTAL_STOP_DEADLINE_H

#include <stdint.h>

class PortalStopDeadline {
public:
  void schedule(uint32_t nowMs, uint32_t graceMs) {
    _deadlineMs = nowMs + graceMs;
    _pending = true;
  }

  bool pending() const { return _pending; }

  bool due(uint32_t nowMs) const {
    return _pending && (int32_t)(nowMs - _deadlineMs) >= 0;
  }

  void clear() {
    _pending = false;
    _deadlineMs = 0;
  }

private:
  bool _pending = false;
  uint32_t _deadlineMs = 0;
};

#endif  // PORTAL_STOP_DEADLINE_H
