#ifndef ULSA_I2C_RETRY_DEADLINE_H
#define ULSA_I2C_RETRY_DEADLINE_H
#include <stdint.h>

// Durations are bounded well below half the 32-bit clock period. Zero is a
// valid deadline; a separate flag distinguishes it from an inactive timer.
class I2cRetryDeadline {
public:
  void clear() { _active = false; }
  void schedule(uint32_t now, uint32_t delayMs) {
    _started = now;
    _delay = delayMs;
    _active = true;
  }
  bool active(uint32_t now) {
    if (_active && static_cast<uint32_t>(now - _started) >= _delay) clear();
    return _active;
  }
private:
  bool _active = false;
  uint32_t _started = 0;
  uint32_t _delay = 0;
};
#endif
