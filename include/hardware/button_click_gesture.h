/**
 * @file button_click_gesture.h
 * @brief Hardware-independent one/two/three-click classifier.
 */

#ifndef BUTTON_CLICK_GESTURE_H
#define BUTTON_CLICK_GESTURE_H

#include <stdint.h>

enum ButtonClickAction {
  BUTTON_CLICK_NONE,
  BUTTON_CLICK_SINGLE,
  BUTTON_CLICK_DOUBLE,
  BUTTON_CLICK_TRIPLE,
};

class ButtonClickGesture {
public:
  explicit ButtonClickGesture(uint32_t multiClickWindowMs)
    : _multiClickWindowMs(multiClickWindowMs)
    , _lastReleaseMs(0)
    , _clickCount(0)
    , _nextClickInProgress(false) {}

  void onPress(uint32_t nowMs) {
    if (_clickCount > 0 && elapsedMs(nowMs, _lastReleaseMs) < _multiClickWindowMs) {
      _nextClickInProgress = true;
    }
  }

  ButtonClickAction onShortRelease(uint32_t nowMs) {
    _clickCount = _nextClickInProgress && _clickCount > 0
      ? (_clickCount < 4 ? _clickCount + 1 : 4) : 1;
    _lastReleaseMs = nowMs;
    _nextClickInProgress = false;
    return BUTTON_CLICK_NONE;
  }

  ButtonClickAction poll(uint32_t nowMs) {
    if (_clickCount > 0 && !_nextClickInProgress &&
        elapsedMs(nowMs, _lastReleaseMs) >= _multiClickWindowMs) {
      const uint8_t count = _clickCount;
      reset();
      if (count == 1) return BUTTON_CLICK_SINGLE;
      if (count == 2) return BUTTON_CLICK_DOUBLE;
      if (count == 3) return BUTTON_CLICK_TRIPLE;
    }
    return BUTTON_CLICK_NONE;
  }

  void cancel() {
    reset();
  }

private:
  static uint32_t elapsedMs(uint32_t nowMs, uint32_t thenMs) {
    return nowMs - thenMs;
  }

  void reset() {
    _lastReleaseMs = 0;
    _clickCount = 0;
    _nextClickInProgress = false;
  }

  uint32_t _multiClickWindowMs;
  uint32_t _lastReleaseMs;
  uint8_t _clickCount;
  bool _nextClickInProgress;
};

#endif  // BUTTON_CLICK_GESTURE_H
