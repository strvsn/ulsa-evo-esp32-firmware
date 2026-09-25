/**
 * @file initial_factory_boot_health.h
 * @brief Initial-only factory layout validation and inactive-slot cleanup.
 */

#ifndef PROFILE_INITIAL_FACTORY_BOOT_HEALTH_H
#define PROFILE_INITIAL_FACTORY_BOOT_HEALTH_H

#include <stddef.h>
#include <stdint.h>

namespace initial_factory {

constexpr uint8_t PARTITION_TYPE_APP = 0x00;
constexpr uint8_t PARTITION_TYPE_DATA = 0x01;
constexpr uint8_t PARTITION_SUBTYPE_DATA_OTA = 0x00;
constexpr uint8_t PARTITION_SUBTYPE_DATA_NVS = 0x02;
constexpr uint8_t PARTITION_SUBTYPE_DATA_COREDUMP = 0x03;
constexpr uint8_t PARTITION_SUBTYPE_APP_OTA_0 = 0x10;
constexpr uint8_t PARTITION_SUBTYPE_APP_OTA_1 = 0x11;
constexpr uint8_t PARTITION_SUBTYPE_STM32_PACKAGE = 0x40;

struct PartitionDescriptor {
  const char* label;
  uint8_t type;
  uint8_t subtype;
  uint32_t address;
  uint32_t size;
  bool encrypted;
};

constexpr size_t EXPECTED_PARTITION_COUNT = 6;
static constexpr PartitionDescriptor EXPECTED_PARTITIONS[EXPECTED_PARTITION_COUNT] = {
  {"nvs", PARTITION_TYPE_DATA, PARTITION_SUBTYPE_DATA_NVS,
   0x00009000U, 0x00005000U, false},
  {"otadata", PARTITION_TYPE_DATA, PARTITION_SUBTYPE_DATA_OTA,
   0x0000E000U, 0x00002000U, false},
  {"app0", PARTITION_TYPE_APP, PARTITION_SUBTYPE_APP_OTA_0,
   0x00010000U, 0x001B0000U, false},
  {"app1", PARTITION_TYPE_APP, PARTITION_SUBTYPE_APP_OTA_1,
   0x001C0000U, 0x001B0000U, false},
  {"stm32pkg", PARTITION_TYPE_DATA, PARTITION_SUBTYPE_STM32_PACKAGE,
   0x00370000U, 0x00080000U, false},
  {"coredump", PARTITION_TYPE_DATA, PARTITION_SUBTYPE_DATA_COREDUMP,
   0x003F0000U, 0x00010000U, false},
};

inline bool partitionLabelEquals(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) return false;
  while (*left != '\0' && *right != '\0') {
    if (*left++ != *right++) return false;
  }
  return *left == *right;
}

inline bool partitionMatches(const PartitionDescriptor& actual,
                             const PartitionDescriptor& expected) {
  return partitionLabelEquals(actual.label, expected.label) &&
         actual.type == expected.type &&
         actual.subtype == expected.subtype &&
         actual.address == expected.address &&
         actual.size == expected.size &&
         actual.encrypted == expected.encrypted;
}

inline bool exactPartitionLayout(const PartitionDescriptor* actual,
                                 size_t count) {
  if (actual == nullptr || count != EXPECTED_PARTITION_COUNT) return false;
  for (size_t expectedIndex = 0;
       expectedIndex < EXPECTED_PARTITION_COUNT;
       ++expectedIndex) {
    size_t matches = 0;
    for (size_t actualIndex = 0; actualIndex < count; ++actualIndex) {
      if (partitionMatches(actual[actualIndex],
                           EXPECTED_PARTITIONS[expectedIndex])) {
        ++matches;
      }
    }
    if (matches != 1) return false;
  }
  return true;
}

inline int inactiveOtaIndexForRunningAddress(uint32_t runningAddress) {
  if (runningAddress == EXPECTED_PARTITIONS[2].address) return 3;
  if (runningAddress == EXPECTED_PARTITIONS[3].address) return 2;
  return -1;
}

enum class BootHealthError : uint8_t {
  None = 0,
  PartitionLayout,
  FirmwareIdentity,
  RequiredManagerUnavailable,
  LegacyBridgeNvsPresent,
  RunningPartition,
  BootPartition,
  MarkerStorage,
  MarkerInvalid,
  OtaValidation,
  Stm32PackageNotBlank,
  CoredumpNotBlank,
  InactiveErase,
  InactiveVerify,
  CompletionMarker,
};

class InitialFactoryBootHealth {
public:
  InitialFactoryBootHealth();

  bool run(bool requiredManagerAvailable);
  bool isReady() const { return _ready; }
  bool hasPersistentError() const { return _error != BootHealthError::None; }
  BootHealthError getError() const { return _error; }
  const char* getErrorString() const;

private:
  bool fail(BootHealthError error);

  bool _ready;
  BootHealthError _error;
};

}  // namespace initial_factory

#endif  // PROFILE_INITIAL_FACTORY_BOOT_HEALTH_H
