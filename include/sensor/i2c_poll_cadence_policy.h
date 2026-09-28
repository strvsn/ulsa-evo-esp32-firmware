/**
 * @file i2c_poll_cadence_policy.h
 * @brief Phase-stable poll cadence derived from the STM32 output period.
 */

#ifndef I2C_POLL_CADENCE_POLICY_H
#define I2C_POLL_CADENCE_POLICY_H

#include <stdint.h>

class I2cPollCadencePolicy {
public:
  // Poll at twice the source cadence. Polling once per source period can lose a
  // DATA_SEQ after only a small scheduling delay, because both clocks drift.
  static uint32_t intervalForSource(uint32_t sourceIntervalMs,
                                    uint32_t minIntervalMs,
                                    uint32_t maxIntervalMs,
                                    uint8_t oversampleFactor = 2U) {
    if (sourceIntervalMs == 0U || minIntervalMs == 0U || minIntervalMs > maxIntervalMs) {
      return 0U;
    }

    if (oversampleFactor < 2U) {
      oversampleFactor = 2U;
    }
    uint32_t intervalMs = (sourceIntervalMs + oversampleFactor - 1U) / oversampleFactor;
    if (intervalMs < minIntervalMs) {
      intervalMs = minIntervalMs;
    }
    if (intervalMs > maxIntervalMs) {
      intervalMs = maxIntervalMs;
    }
    return intervalMs;
  }

  // Advance by whole slots instead of setting the schedule to `now`. This
  // avoids one millisecond of loop jitter accumulating into a later DATA_SEQ
  // miss over a long recording session.
  static uint32_t advanceScheduledPoll(uint32_t lastScheduledMs,
                                       uint32_t nowMs,
                                       uint32_t intervalMs) {
    if (intervalMs == 0U) {
      return nowMs;
    }

    const uint32_t elapsedMs = nowMs - lastScheduledMs;
    if (elapsedMs < intervalMs) {
      return lastScheduledMs;
    }

    return lastScheduledMs + (elapsedMs / intervalMs) * intervalMs;
  }
};

#endif  // I2C_POLL_CADENCE_POLICY_H
