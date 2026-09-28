/**
 * @file button_gesture_classifier.h
 * @brief Hardware-independent release-time button gesture classifier.
 */

#ifndef HARDWARE_BUTTON_GESTURE_CLASSIFIER_H
#define HARDWARE_BUTTON_GESTURE_CLASSIFIER_H

#include <stdint.h>

#include "button_click_gesture.h"

enum ButtonGestureAction {
  BUTTON_GESTURE_NONE = 0,
  BUTTON_GESTURE_SINGLE,
  BUTTON_GESTURE_DOUBLE,
  BUTTON_GESTURE_TRIPLE,
  BUTTON_GESTURE_LONG_RETURN,
  BUTTON_GESTURE_LONG_PRIMARY,
  BUTTON_GESTURE_LONG_SECONDARY,
};

enum ButtonGesturePreview {
  BUTTON_PREVIEW_NONE = 0,
  BUTTON_PREVIEW_LONG_RETURN,
  BUTTON_PREVIEW_LONG_PRIMARY,
  BUTTON_PREVIEW_LONG_SECONDARY,
};

struct ButtonGestureTiming {
  uint32_t multiClickWindowMs;
  uint32_t shortReleaseMaxExclusiveMs;
  uint32_t returnLongMinInclusiveMs;
  uint32_t primaryLongMinInclusiveMs;
  uint32_t primaryLongMaxExclusiveMs;
  uint32_t secondaryLongMinInclusiveMs;
};

class ButtonGestureClassifier {
public:
  explicit ButtonGestureClassifier(const ButtonGestureTiming& timing)
    : _timing(timing)
    , _clickGesture(timing.multiClickWindowMs)
    , _pressStartedMs(0)
    , _pressed(false) {}

  void onPress(uint32_t nowMs) {
    if (_pressed) {
      return;
    }
    _pressStartedMs = nowMs;
    _pressed = true;
    _clickGesture.onPress(nowMs);
  }

  ButtonGesturePreview preview(uint32_t nowMs) const {
    if (!_pressed) {
      return BUTTON_PREVIEW_NONE;
    }

    const uint32_t durationMs = elapsedMs(nowMs, _pressStartedMs);
    if (durationMs >= _timing.secondaryLongMinInclusiveMs) {
      return BUTTON_PREVIEW_LONG_SECONDARY;
    }
    if (durationMs >= _timing.primaryLongMinInclusiveMs &&
        durationMs < _timing.primaryLongMaxExclusiveMs) {
      return BUTTON_PREVIEW_LONG_PRIMARY;
    }
    if (durationMs >= _timing.returnLongMinInclusiveMs &&
        durationMs < _timing.primaryLongMinInclusiveMs) {
      return BUTTON_PREVIEW_LONG_RETURN;
    }
    return BUTTON_PREVIEW_NONE;
  }

  ButtonGestureAction onRelease(uint32_t nowMs) {
    if (!_pressed) {
      return BUTTON_GESTURE_NONE;
    }

    const uint32_t durationMs = elapsedMs(nowMs, _pressStartedMs);
    _pressed = false;

    if (durationMs < _timing.shortReleaseMaxExclusiveMs) {
      return fromClickAction(_clickGesture.onShortRelease(nowMs));
    }

    _clickGesture.cancel();
    if (durationMs >= _timing.secondaryLongMinInclusiveMs) {
      return BUTTON_GESTURE_LONG_SECONDARY;
    }
    if (durationMs >= _timing.primaryLongMinInclusiveMs &&
        durationMs < _timing.primaryLongMaxExclusiveMs) {
      return BUTTON_GESTURE_LONG_PRIMARY;
    }
    if (durationMs >= _timing.returnLongMinInclusiveMs &&
        durationMs < _timing.primaryLongMinInclusiveMs) {
      return BUTTON_GESTURE_LONG_RETURN;
    }
    return BUTTON_GESTURE_NONE;
  }

  ButtonGestureAction poll(uint32_t nowMs) {
    return fromClickAction(_clickGesture.poll(nowMs));
  }

  void cancel() {
    _pressed = false;
    _pressStartedMs = 0;
    _clickGesture.cancel();
  }

  bool isPressed() const {
    return _pressed;
  }

private:
  static uint32_t elapsedMs(uint32_t nowMs, uint32_t thenMs) {
    return nowMs - thenMs;
  }

  static ButtonGestureAction fromClickAction(ButtonClickAction action) {
    switch (action) {
      case BUTTON_CLICK_SINGLE:
        return BUTTON_GESTURE_SINGLE;
      case BUTTON_CLICK_DOUBLE:
        return BUTTON_GESTURE_DOUBLE;
      case BUTTON_CLICK_TRIPLE:
        return BUTTON_GESTURE_TRIPLE;
      case BUTTON_CLICK_NONE:
      default:
        return BUTTON_GESTURE_NONE;
    }
  }

  ButtonGestureTiming _timing;
  ButtonClickGesture _clickGesture;
  uint32_t _pressStartedMs;
  bool _pressed;
};

#endif  // HARDWARE_BUTTON_GESTURE_CLASSIFIER_H
