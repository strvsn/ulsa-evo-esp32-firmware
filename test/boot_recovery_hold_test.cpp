#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/boot_recovery_hold.h"

static void testNotHeldAtBoot() {
  BootRecoveryHold hold(5000U);
  assert(hold.sample(false, 100U) == BootRecoveryHoldEvent::Cancelled);
  assert(hold.isFinished());
}

static void testEarlyReleaseCancels() {
  BootRecoveryHold hold(5000U);
  assert(hold.sample(true, 100U) == BootRecoveryHoldEvent::Waiting);
  assert(hold.sample(true, 5099U) == BootRecoveryHoldEvent::Waiting);
  assert(hold.sample(false, 5099U) == BootRecoveryHoldEvent::Cancelled);
}

static void testThresholdRequiresRelease() {
  BootRecoveryHold hold(5000U);
  assert(hold.sample(true, 100U) == BootRecoveryHoldEvent::Waiting);
  assert(hold.sample(true, 5100U) == BootRecoveryHoldEvent::HoldConfirmed);
  assert(!hold.isFinished());
  assert(hold.sample(true, 9000U) == BootRecoveryHoldEvent::HoldConfirmed);
  assert(hold.sample(false, 9001U) == BootRecoveryHoldEvent::EnterRecovery);
  assert(hold.isFinished());
}

static void testMillisWrap() {
  BootRecoveryHold hold(5000U);
  const uint32_t started = UINT32_MAX - 1000U;
  assert(hold.sample(true, started) == BootRecoveryHoldEvent::Waiting);
  assert(hold.sample(true, 3998U) == BootRecoveryHoldEvent::Waiting);
  assert(hold.sample(true, 3999U) == BootRecoveryHoldEvent::HoldConfirmed);
  assert(hold.sample(false, 4000U) == BootRecoveryHoldEvent::EnterRecovery);
}

int main() {
  testNotHeldAtBoot();
  testEarlyReleaseCancels();
  testThresholdRequiresRelease();
  testMillisWrap();
  puts("boot_recovery_hold_test: passed");
  return 0;
}
