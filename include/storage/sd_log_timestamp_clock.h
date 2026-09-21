/**
 * @file sd_log_timestamp_clock.h
 * @brief Builds CSV millisecond timestamps from a source-sample timeline.
 */

#ifndef SD_LOG_TIMESTAMP_CLOCK_H
#define SD_LOG_TIMESTAMP_CLOCK_H

#include <stdint.h>

struct SdLogTimestamp {
  int64_t epochMs;
  uint16_t subsecondMs;

  SdLogTimestamp()
      : epochMs(0)
      , subsecondMs(0) {
  }
};

class SdLogTimestampClock {
public:
  SdLogTimestampClock()
      : _valid(false)
      , _epochSeconds(0)
      , _anchorSampleMillis(0U) {
  }

  void reset() {
    _valid = false;
    _epochSeconds = 0;
    _anchorSampleMillis = 0U;
  }

  SdLogTimestamp fromRtcSecond(int64_t epochSeconds, uint32_t sampleMillis) {
    if (!_valid || _epochSeconds != epochSeconds) {
      _valid = true;
      _epochSeconds = epochSeconds;
      _anchorSampleMillis = sampleMillis;
    }

    uint32_t subsecondMs = sampleMillis - _anchorSampleMillis;
    if (subsecondMs > 999U) {
      subsecondMs = 999U;
    }

    SdLogTimestamp timestamp;
    timestamp.subsecondMs = static_cast<uint16_t>(subsecondMs);
    timestamp.epochMs = epochSeconds * 1000LL + subsecondMs;
    return timestamp;
  }

private:
  bool _valid;
  int64_t _epochSeconds;
  uint32_t _anchorSampleMillis;
};

#endif  // SD_LOG_TIMESTAMP_CLOCK_H
