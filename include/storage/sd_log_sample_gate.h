/**
 * @file sd_log_sample_gate.h
 * @brief Source-sample time based cadence gate for SD logging.
 *
 * The gate deliberately uses the timestamp of a newly received sensor sample,
 * not the time at which CSV formatting or SD I/O finishes.  This prevents a
 * fixed-period source from being deterministically decimated when logging
 * work takes a few milliseconds after every sample.
 */

#ifndef SD_LOG_SAMPLE_GATE_H
#define SD_LOG_SAMPLE_GATE_H

#include <stdint.h>

class SdLogSampleGate {
public:
  static const uint32_t kEarlyToleranceMs = 2U;

  explicit SdLogSampleGate(uint32_t intervalMs = 1U)
      : _intervalMs(intervalMs == 0U ? 1U : intervalMs)
      , _nextDueMs(0U)
      , _armed(false) {
  }

  void reset(uint32_t intervalMs) {
    _intervalMs = intervalMs == 0U ? 1U : intervalMs;
    _nextDueMs = 0U;
    _armed = false;
  }

  bool isDue(uint32_t sampleTimestampMs) const {
    if (!_armed) {
      return true;
    }

    // A small tolerance absorbs normal millis()/I2C scheduling jitter without
    // turning a 100 ms source into an every-other-sample logger.
    return static_cast<int32_t>(sampleTimestampMs - _nextDueMs) >=
           -static_cast<int32_t>(kEarlyToleranceMs);
  }

  void accept(uint32_t sampleTimestampMs) {
    if (!_armed) {
      _nextDueMs = sampleTimestampMs + _intervalMs;
      _armed = true;
      return;
    }

    const int32_t latenessMs = static_cast<int32_t>(sampleTimestampMs - _nextDueMs);
    if (latenessMs > static_cast<int32_t>(kEarlyToleranceMs)) {
      // A source gap occurred.  Do not create a catch-up burst from stale
      // deadlines; resume cadence from the sample that was actually logged.
      _nextDueMs = sampleTimestampMs + _intervalMs;
      return;
    }

    // Keep the original phase for on-time (or slightly early) samples.
    _nextDueMs += _intervalMs;
  }

  bool isArmed() const {
    return _armed;
  }

  uint32_t getNextDueMs() const {
    return _nextDueMs;
  }

private:
  uint32_t _intervalMs;
  uint32_t _nextDueMs;
  bool _armed;
};

#endif  // SD_LOG_SAMPLE_GATE_H
