#include <assert.h>
#include <stdio.h>

#include "../include/hardware/button_gesture_classifier.h"

static const ButtonGestureTiming kTiming = {
  350,
  1000,
  2000,
  3000,
  6000,
  6000,
};

static void recognizesSingleAndDoubleRelease() {
  ButtonGestureClassifier gesture(kTiming);

  gesture.onPress(10);
  assert(gesture.onRelease(40) == BUTTON_GESTURE_NONE);
  assert(gesture.poll(389) == BUTTON_GESTURE_NONE);
  assert(gesture.poll(390) == BUTTON_GESTURE_SINGLE);

  gesture.onPress(500);
  assert(gesture.onRelease(530) == BUTTON_GESTURE_NONE);
  gesture.onPress(700);
  assert(gesture.onRelease(730) == BUTTON_GESTURE_NONE);
  assert(gesture.poll(1080) == BUTTON_GESTURE_DOUBLE);

  gesture.onPress(1200);
  assert(gesture.onRelease(1230) == BUTTON_GESTURE_NONE);
  gesture.onPress(1300);
  assert(gesture.onRelease(1330) == BUTTON_GESTURE_NONE);
  gesture.onPress(1400);
  assert(gesture.onRelease(1430) == BUTTON_GESTURE_NONE);
  assert(gesture.poll(1780) == BUTTON_GESTURE_TRIPLE);
}

static void longPressHasNoActionBeforeRelease() {
  ButtonGestureClassifier gesture(kTiming);

  gesture.onPress(100);
  assert(gesture.preview(2099) == BUTTON_PREVIEW_NONE);
  assert(gesture.preview(2100) == BUTTON_PREVIEW_LONG_RETURN);
  assert(gesture.poll(2100) == BUTTON_GESTURE_NONE);
  assert(gesture.preview(3100) == BUTTON_PREVIEW_LONG_PRIMARY);
  assert(gesture.preview(6100) == BUTTON_PREVIEW_LONG_SECONDARY);
  assert(gesture.onRelease(6100) == BUTTON_GESTURE_LONG_SECONDARY);
  assert(gesture.onRelease(6200) == BUTTON_GESTURE_NONE);
}

static void returnAndOtaThresholdsAreExclusive() {
  ButtonGestureClassifier gesture(kTiming);

  gesture.onPress(0);
  assert(gesture.onRelease(999) == BUTTON_GESTURE_NONE);
  gesture.cancel();

  gesture.onPress(0);
  assert(gesture.onRelease(1000) == BUTTON_GESTURE_NONE);
  gesture.onPress(0);
  assert(gesture.onRelease(1999) == BUTTON_GESTURE_NONE);
  gesture.onPress(0);
  assert(gesture.onRelease(2000) == BUTTON_GESTURE_LONG_RETURN);
  gesture.onPress(0);
  assert(gesture.onRelease(2999) == BUTTON_GESTURE_LONG_RETURN);
  gesture.onPress(0);
  assert(gesture.onRelease(3000) == BUTTON_GESTURE_LONG_PRIMARY);
  gesture.onPress(0);
  assert(gesture.onRelease(5999) == BUTTON_GESTURE_LONG_PRIMARY);
  gesture.onPress(0);
  assert(gesture.onRelease(6000) == BUTTON_GESTURE_LONG_SECONDARY);
}

static void longSecondPressCancelsPendingSingle() {
  ButtonGestureClassifier gesture(kTiming);

  gesture.onPress(10);
  assert(gesture.onRelease(40) == BUTTON_GESTURE_NONE);
  gesture.onPress(100);
  assert(gesture.poll(1000) == BUTTON_GESTURE_NONE);
  assert(gesture.onRelease(2100) == BUTTON_GESTURE_LONG_RETURN);
  assert(gesture.poll(3000) == BUTTON_GESTURE_NONE);
}

static void handlesMillisWrap() {
  ButtonGestureClassifier gesture(kTiming);

  gesture.onPress(0xfffffff0U);
  assert(gesture.preview(0x000007bfU) == BUTTON_PREVIEW_NONE);
  assert(gesture.onRelease(0x000007c0U) == BUTTON_GESTURE_LONG_RETURN);
}

int main() {
  recognizesSingleAndDoubleRelease();
  longPressHasNoActionBeforeRelease();
  returnAndOtaThresholdsAreExclusive();
  longSecondPressCancelsPendingSingle();
  handlesMillisWrap();
  puts("button_gesture_classifier_test: passed");
  return 0;
}
