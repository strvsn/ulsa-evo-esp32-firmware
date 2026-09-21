#include <assert.h>
#include <stdio.h>

#include "../include/hardware/button_click_gesture.h"

static void expectsSingleClickAfterWindow() {
  ButtonClickGesture gesture(350);

  gesture.onPress(10);
  assert(gesture.onShortRelease(40) == BUTTON_CLICK_NONE);
  assert(gesture.poll(389) == BUTTON_CLICK_NONE);
  assert(gesture.poll(390) == BUTTON_CLICK_SINGLE);
  assert(gesture.poll(400) == BUTTON_CLICK_NONE);
}

static void waitsForTheLastClickBeforeSelectingDoubleOrTriple() {
  ButtonClickGesture gesture(350);

  gesture.onPress(10);
  assert(gesture.onShortRelease(40) == BUTTON_CLICK_NONE);
  gesture.onPress(170);
  assert(gesture.poll(390) == BUTTON_CLICK_NONE);
  assert(gesture.onShortRelease(210) == BUTTON_CLICK_NONE);
  assert(gesture.poll(559) == BUTTON_CLICK_NONE);
  gesture.onPress(300);
  assert(gesture.onShortRelease(330) == BUTTON_CLICK_NONE);
  assert(gesture.poll(679) == BUTTON_CLICK_NONE);
  assert(gesture.poll(680) == BUTTON_CLICK_TRIPLE);

  gesture.onPress(800);
  assert(gesture.onShortRelease(820) == BUTTON_CLICK_NONE);
  gesture.onPress(900);
  assert(gesture.onShortRelease(920) == BUTTON_CLICK_NONE);
  assert(gesture.poll(1270) == BUTTON_CLICK_DOUBLE);
}

static void ignoresFourOrMoreClicksAsOneSequence() {
  ButtonClickGesture gesture(350);
  for (uint32_t i = 0; i < 5; ++i) {
    gesture.onPress(10 + i * 100);
    assert(gesture.onShortRelease(40 + i * 100) == BUTTON_CLICK_NONE);
  }
  assert(gesture.poll(789) == BUTTON_CLICK_NONE);
  assert(gesture.poll(790) == BUTTON_CLICK_NONE);
}

static void cancelsPendingSingleClickForLongPress() {
  ButtonClickGesture gesture(350);

  gesture.onPress(10);
  assert(gesture.onShortRelease(40) == BUTTON_CLICK_NONE);
  gesture.onPress(100);
  gesture.cancel();
  assert(gesture.poll(1000) == BUTTON_CLICK_NONE);
}

static void handlesMillisWrap() {
  ButtonClickGesture gesture(350);

  gesture.onPress(0xfffffff0U);
  assert(gesture.onShortRelease(0xfffffff8U) == BUTTON_CLICK_NONE);
  assert(gesture.poll(0x00000155U) == BUTTON_CLICK_NONE);
  assert(gesture.poll(0x00000156U) == BUTTON_CLICK_SINGLE);
}

int main() {
  expectsSingleClickAfterWindow();
  waitsForTheLastClickBeforeSelectingDoubleOrTriple();
  ignoresFourOrMoreClicksAsOneSequence();
  cancelsPendingSingleClickForLongPress();
  handlesMillisWrap();
  puts("button_click_gesture_test: passed");
  return 0;
}
