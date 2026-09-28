/**
 * @file i2c_sequence_tracker.h
 * @brief Tracks continuity of the STM32 snapshot DATA_SEQ counter.
 */

#ifndef I2C_SEQUENCE_TRACKER_H
#define I2C_SEQUENCE_TRACKER_H

#include <stdint.h>

struct I2cSequenceObservation {
  bool first;
  bool duplicate;
  bool gap;
  bool reset;
  uint16_t delta;
  uint16_t missingCount;

  I2cSequenceObservation()
      : first(false)
      , duplicate(false)
      , gap(false)
      , reset(false)
      , delta(0)
      , missingCount(0) {
  }
};

class I2cSequenceTracker {
public:
  // Any larger backward-looking jump is treated as an STM32 restart, not as
  // tens of thousands of lost records after the 16-bit counter wraps.
  static const uint16_t kMaximumPlausibleDelta = 1024U;

  I2cSequenceTracker()
      : _hasSequence(false)
      , _lastSequence(0U) {
  }

  void reset() {
    _hasSequence = false;
    _lastSequence = 0U;
  }

  I2cSequenceObservation observe(uint16_t sequence) {
    I2cSequenceObservation result;
    if (!_hasSequence) {
      _hasSequence = true;
      _lastSequence = sequence;
      result.first = true;
      return result;
    }

    const uint16_t delta = static_cast<uint16_t>(sequence - _lastSequence);
    _lastSequence = sequence;
    result.delta = delta;
    if (delta == 0U) {
      result.duplicate = true;
      return result;
    }
    if (delta > kMaximumPlausibleDelta) {
      result.reset = true;
      return result;
    }
    if (delta > 1U) {
      result.gap = true;
      result.missingCount = static_cast<uint16_t>(delta - 1U);
    }
    return result;
  }

private:
  bool _hasSequence;
  uint16_t _lastSequence;
};

#endif  // I2C_SEQUENCE_TRACKER_H
