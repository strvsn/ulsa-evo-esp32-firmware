#include <assert.h>
#include <stdio.h>

#include "../include/hardware/sd_logging_led_effect.h"

static void rendersHeartbeat() {
  SdLoggingLedEffect effect;
  assert(!effect.frame(10).active);
  assert(effect.setLoggingState(true, false, 100));

  const SdLoggingLedFrame low = effect.frame(100);
  const SdLoggingLedFrame peak = effect.frame(100 + 900);
  const SdLoggingLedFrame nextCycle = effect.frame(100 + 1800);
  assert(low.active && low.ledOn && low.red > 0);
  assert(peak.red == 255U);
  assert(nextCycle.red == low.red);
}

static void confirmsUserStopWithExactlyThreeFlashes() {
  SdLoggingLedEffect effect;
  assert(effect.setLoggingState(true, false, 0));
  assert(effect.setLoggingState(false, true, 1000));

  const uint32_t sampleTimes[] = {1000, 1100, 1200, 1300, 1400, 1500};
  const bool expectedOn[] = {true, false, true, false, true, false};
  uint8_t onCount = 0;
  for (uint8_t i = 0; i < 6U; ++i) {
    const SdLoggingLedFrame frame = effect.frame(sampleTimes[i]);
    assert(frame.active);
    assert(frame.ledOn == expectedOn[i]);
    if (frame.ledOn) {
      ++onCount;
    }
  }
  assert(onCount == 3U);

  const SdLoggingLedFrame complete = effect.frame(1600);
  assert(!complete.active && complete.completed);
  assert(effect.getPhase() == SD_LOG_LED_INACTIVE);
}

static void doesNotConfirmFaultOrRepeatedStop() {
  SdLoggingLedEffect effect;
  assert(effect.setLoggingState(true, false, 10));
  assert(effect.setLoggingState(false, false, 20));
  assert(!effect.frame(20).active);
  assert(!effect.setLoggingState(false, true, 30));
  assert(!effect.frame(30).active);
}

static void restartSupersedesStopConfirmation() {
  SdLoggingLedEffect effect;
  assert(effect.setLoggingState(true, false, 0));
  assert(effect.setLoggingState(false, true, 100));
  assert(effect.getPhase() == SD_LOG_LED_STOP_CONFIRMATION);
  assert(effect.setLoggingState(true, false, 150));
  assert(effect.getPhase() == SD_LOG_LED_HEARTBEAT);
  assert(effect.frame(150).active);
}

static void handlesMillisWrap() {
  SdLoggingLedEffect effect;
  assert(effect.setLoggingState(true, false, 0xfffffff0U));
  const SdLoggingLedFrame frame = effect.frame(0x00000054U);
  assert(frame.active && frame.ledOn);
}

int main() {
  rendersHeartbeat();
  confirmsUserStopWithExactlyThreeFlashes();
  doesNotConfirmFaultOrRepeatedStop();
  restartSupersedesStopConfirmation();
  handlesMillisWrap();
  puts("sd_logging_led_effect_test: passed");
  return 0;
}
