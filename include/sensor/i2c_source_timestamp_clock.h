/**
 * @file i2c_source_timestamp_clock.h
 * @brief Reconstructs a stable STM32 output-sample timeline from DATA_SEQ.
 */

#ifndef I2C_SOURCE_TIMESTAMP_CLOCK_H
#define I2C_SOURCE_TIMESTAMP_CLOCK_H

#include <stdint.h>

class I2cSourceTimestampClock {
public:
  I2cSourceTimestampClock()
      : _valid(false)
      , _lastSourceTimestampMs(0U) {
  }

  void reset() {
    _valid = false;
    _lastSourceTimestampMs = 0U;
  }

  uint32_t timestampFor(uint32_t captureTimestampMs,
                        uint16_t sequenceDelta,
                        uint16_t sourceIntervalMs,
                        bool sequenceContinuous) {
    if (!_valid || !sequenceContinuous || sequenceDelta == 0U || sourceIntervalMs == 0U) {
      _valid = true;
      _lastSourceTimestampMs = captureTimestampMs;
      return _lastSourceTimestampMs;
    }

    _lastSourceTimestampMs += static_cast<uint32_t>(sequenceDelta) * sourceIntervalMs;
    return _lastSourceTimestampMs;
  }

  uint32_t lastTimestampMs() const {
    return _lastSourceTimestampMs;
  }

private:
  bool _valid;
  uint32_t _lastSourceTimestampMs;
};

#endif  // I2C_SOURCE_TIMESTAMP_CLOCK_H
