#include <stdint.h>
#include <stdio.h>

#include "../include/storage/sd_log_sample_gate.h"
#include "../include/storage/sd_log_interval_policy.h"
#include "../include/storage/sd_log_timestamp_clock.h"
#include "../include/storage/sd_log_write_policy.h"
#include "../include/sensor/i2c_poll_cadence_policy.h"
#include "../include/sensor/i2c_sequence_tracker.h"
#include "../include/sensor/i2c_source_timestamp_clock.h"

static int failures = 0;

static void expect(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAILED: %s\n", message);
    failures++;
  }
}

int main() {
  SdLogSampleGate gate(100U);

  // Regression: a 10 Hz source must not be halved just because each log
  // finishes a few milliseconds after its sample was received.
  for (uint32_t timestamp = 100U; timestamp <= 1000U; timestamp += 100U) {
    expect(gate.isDue(timestamp), "10 Hz source sample must be accepted");
    gate.accept(timestamp);
  }
  expect(gate.getNextDueMs() == 1100U, "cadence phase must advance at 100 ms");

  gate.reset(100U);
  expect(gate.isDue(100U), "first sample must be accepted");
  gate.accept(100U);
  expect(gate.isDue(198U), "2 ms early sample must be accepted");
  gate.accept(198U);
  expect(!gate.isDue(297U), "3 ms early sample must not be accepted");
  expect(gate.isDue(298U), "2 ms early sample must remain accepted");

  gate.reset(100U);
  gate.accept(100U);
  expect(gate.isDue(450U), "late sample after a source gap must be accepted");
  gate.accept(450U);
  expect(!gate.isDue(451U), "late sample must not create a catch-up burst");
  expect(gate.isDue(550U), "cadence must resume from the late sample");

  gate.reset(100U);
  gate.accept(0xFFFFFFF0U);
  expect(gate.isDue(0x00000054U), "millis wrap-around must preserve cadence");

  expect(SdLogIntervalPolicy::isExactSourceMultiple(100U, 100U),
         "100 ms log must match a 10 Hz source");
  expect(SdLogIntervalPolicy::isExactSourceMultiple(300U, 100U),
         "300 ms log must keep every third 10 Hz source sample");
  expect(!SdLogIntervalPolicy::isExactSourceMultiple(150U, 100U),
         "150 ms log must not be accepted for a 10 Hz source");
  expect(SdLogIntervalPolicy::alignAtOrAbove(150U, 100U, 600000U) == 200U,
         "legacy unaligned interval must round toward a slower exact interval");
  expect(SdLogIntervalPolicy::alignAtOrAbove(100U, 20U, 600000U) == 100U,
         "100 ms remains exact for a 50 Hz source");

  expect(!SdLogWritePolicy::shouldSync(false, 0U, 1000U, false),
         "a clean SD stream must not be synced again");
  expect(!SdLogWritePolicy::shouldSync(true, 100U, 1099U, false),
         "a pending write must wait until the one-second sync interval");
  expect(SdLogWritePolicy::shouldSync(true, 100U, 1100U, false),
         "a pending write must sync at the one-second boundary");
  expect(SdLogWritePolicy::shouldSync(true, 1100U, 1101U, true),
         "stop and file rotation must force a durable sync");
  expect(SdLogWritePolicy::shouldSync(true, 0xFFFFFFF0U, 0x000003D8U, false),
         "sync scheduling must survive millis wrap-around");
  expect(!SdLogWritePolicy::isSlowWriteAdvisory(249U) &&
             SdLogWritePolicy::isSlowWriteAdvisory(250U),
         "slow-write status must remain advisory at 250 ms");

  expect(I2cPollCadencePolicy::intervalForSource(100U, 10U, 1000U) == 50U,
         "10 Hz source must be polled at 20 Hz");
  expect(I2cPollCadencePolicy::intervalForSource(20U, 10U, 1000U) == 10U,
         "50 Hz source must be polled at 100 Hz");
  expect(I2cPollCadencePolicy::advanceScheduledPoll(100U, 151U, 50U) == 150U,
         "poll schedule must keep its phase after a 1 ms delay");
  expect(I2cPollCadencePolicy::advanceScheduledPoll(150U, 201U, 50U) == 200U,
         "poll schedule must not accumulate loop jitter");

  I2cSequenceTracker sequenceTracker;
  expect(sequenceTracker.observe(65535U).first, "first DATA_SEQ must establish continuity");
  expect(sequenceTracker.observe(0U).delta == 1U,
         "DATA_SEQ wrap-around must remain contiguous");
  const I2cSequenceObservation sequenceGap = sequenceTracker.observe(2U);
  expect(sequenceGap.gap && sequenceGap.missingCount == 1U,
         "DATA_SEQ gap must identify the exact missing source sample count");
  expect(sequenceTracker.observe(2U).duplicate,
         "oversampling must classify a repeated DATA_SEQ as stale, not missing");
  expect(sequenceTracker.observe(10U).gap,
         "forward DATA_SEQ jumps must remain observable");
  expect(sequenceTracker.observe(1U).reset,
         "implausible backward DATA_SEQ jumps must be treated as a source reset");

  I2cSourceTimestampClock sourceTimestampClock;
  expect(sourceTimestampClock.timestampFor(5000U, 0U, 100U, true) == 5000U,
         "first DATA_SEQ must anchor source time at its capture time");
  expect(sourceTimestampClock.timestampFor(5799U, 1U, 100U, true) == 5100U,
         "late I2C capture must not move a contiguous source sample into a later slot");
  expect(sourceTimestampClock.timestampFor(5900U, 2U, 100U, true) == 5300U,
         "a real DATA_SEQ gap must remain visible in the source timeline");
  expect(sourceTimestampClock.timestampFor(6005U, 1U, 100U, false) == 6005U,
         "a source reset must re-anchor at the new capture time");

  SdLogTimestampClock timestampClock;
  SdLogTimestamp timestamp = timestampClock.fromRtcSecond(1000LL, 5000U);
  expect(timestamp.epochMs == 1000000LL && timestamp.subsecondMs == 0U,
         "first RTC second must anchor at the source sample time");
  timestamp = timestampClock.fromRtcSecond(1000LL, 5700U);
  expect(timestamp.epochMs == 1000700LL && timestamp.subsecondMs == 700U,
         "CSV milliseconds must preserve the source time regardless of later log work");
  timestamp = timestampClock.fromRtcSecond(1001LL, 6000U);
  expect(timestamp.epochMs == 1001000LL && timestamp.subsecondMs == 0U,
         "next RTC second must re-anchor without carrying a prior delay");

  if (failures != 0) {
    return 1;
  }

  puts("sd_log_sample_gate_test: passed");
  return 0;
}
