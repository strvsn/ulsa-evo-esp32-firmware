#include <assert.h>
#include <stdio.h>

#include "profile/initial_factory_boot_health.h"

using initial_factory::EXPECTED_PARTITION_COUNT;
using initial_factory::EXPECTED_PARTITIONS;
using initial_factory::PartitionDescriptor;

static void acceptsExactLayoutInAnyOrder() {
  PartitionDescriptor reordered[EXPECTED_PARTITION_COUNT] = {
    EXPECTED_PARTITIONS[3],
    EXPECTED_PARTITIONS[0],
    EXPECTED_PARTITIONS[5],
    EXPECTED_PARTITIONS[2],
    EXPECTED_PARTITIONS[4],
    EXPECTED_PARTITIONS[1],
  };
  assert(initial_factory::exactPartitionLayout(
    reordered, EXPECTED_PARTITION_COUNT));
}

static void rejectsMissingExtraAndChangedPartitions() {
  assert(!initial_factory::exactPartitionLayout(
    EXPECTED_PARTITIONS, EXPECTED_PARTITION_COUNT - 1));

  PartitionDescriptor extra[EXPECTED_PARTITION_COUNT + 1] = {};
  for (size_t index = 0; index < EXPECTED_PARTITION_COUNT; ++index) {
    extra[index] = EXPECTED_PARTITIONS[index];
  }
  extra[EXPECTED_PARTITION_COUNT] = {
    "spiffs", 1, 0x82, 0x003D0000U, 0x00020000U, false,
  };
  assert(!initial_factory::exactPartitionLayout(
    extra, EXPECTED_PARTITION_COUNT + 1));

  PartitionDescriptor changed[EXPECTED_PARTITION_COUNT] = {};
  for (size_t index = 0; index < EXPECTED_PARTITION_COUNT; ++index) {
    changed[index] = EXPECTED_PARTITIONS[index];
  }
  changed[3].address = 0x001F0000U;
  assert(!initial_factory::exactPartitionLayout(
    changed, EXPECTED_PARTITION_COUNT));
  changed[3] = EXPECTED_PARTITIONS[3];
  changed[4].size = 0x00020000U;
  assert(!initial_factory::exactPartitionLayout(
    changed, EXPECTED_PARTITION_COUNT));
}

static void selectsOnlyTheInactiveExpectedOtaSlot() {
  assert(initial_factory::inactiveOtaIndexForRunningAddress(0x00010000U) == 3);
  assert(initial_factory::inactiveOtaIndexForRunningAddress(0x001C0000U) == 2);
  assert(initial_factory::inactiveOtaIndexForRunningAddress(0x001F0000U) == -1);
}

int main() {
  acceptsExactLayoutInAnyOrder();
  rejectsMissingExtraAndChangedPartitions();
  selectsOnlyTheInactiveExpectedOtaSlot();
  puts("initial_factory_boot_health_test: passed");
  return 0;
}
