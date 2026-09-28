/**
 * @file initial_factory_boot_health.cpp
 * @brief Initial-only factory layout validation and inactive-slot cleanup.
 */

#include "profile/initial_factory_boot_health.h"

#include "system/firmware_identity.h"

#include <Preferences.h>
#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <nvs.h>

#include <cstring>

namespace initial_factory {
namespace {

constexpr char FACTORY_NVS_NAMESPACE[] = "initial_factory";
constexpr char CLEANUP_STATE_KEY[] = "cleanup_state";
constexpr char CLEANED_ADDRESS_KEY[] = "cleaned_addr";
constexpr uint8_t CLEANUP_NOT_STARTED = 0;
constexpr uint8_t CLEANUP_IN_PROGRESS = 1;
constexpr uint8_t CLEANUP_COMPLETE = 2;
constexpr size_t ERASE_VERIFY_CHUNK_BYTES = 256;

struct CleanupMarker {
  uint8_t state;
  uint32_t cleanedAddress;
};

bool samePartition(const esp_partition_t* partition,
                   const PartitionDescriptor& expected) {
  if (partition == nullptr) return false;
  const PartitionDescriptor actual = {
    partition->label,
    static_cast<uint8_t>(partition->type),
    static_cast<uint8_t>(partition->subtype),
    partition->address,
    partition->size,
    partition->encrypted,
  };
  return partitionMatches(actual, expected);
}

bool readExactLayout() {
  PartitionDescriptor actual[EXPECTED_PARTITION_COUNT] = {};
  size_t count = 0;
  bool overflow = false;
  esp_partition_iterator_t iterator = esp_partition_find(
    ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (iterator != nullptr) {
    const esp_partition_t* partition = esp_partition_get(iterator);
    if (partition == nullptr) {
      esp_partition_iterator_release(iterator);
      return false;
    }
    if (count < EXPECTED_PARTITION_COUNT) {
      actual[count] = {
        partition->label,
        static_cast<uint8_t>(partition->type),
        static_cast<uint8_t>(partition->subtype),
        partition->address,
        partition->size,
        partition->encrypted,
      };
    } else {
      overflow = true;
    }
    ++count;
    iterator = esp_partition_next(iterator);
  }
  return !overflow && exactPartitionLayout(actual, count);
}

bool legacyBridgeNvsIsAbsent() {
  nvs_handle_t handle = 0;
  const esp_err_t result = nvs_open("espnow_br", NVS_READONLY, &handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return true;
  if (result == ESP_OK) nvs_close(handle);
  // The factory Bridge creates this namespace before pairing. Its presence
  // proves the mandatory post-OTA NVS erase did not complete. Other NVS
  // failures are also rejected rather than allowing a green factory result.
  return false;
}

bool runningIdentityIsCleanInitial() {
  const Esp32FirmwareIdentityDescriptor& identity = getEsp32FirmwareIdentity();
  uint32_t magicHash = 2166136261U;
  for (size_t index = 0; index < sizeof(identity.magic); ++index) {
    magicHash ^= static_cast<uint8_t>(identity.magic[index]);
    magicHash *= 16777619U;
  }
  // Hashing avoids embedding a second literal ULSA identity magic sequence in
  // the image; release validation requires exactly one descriptor occurrence.
  return magicHash == 0x413165A8U &&
         identity.schemaVersion == ULSA_EVO_ESP32_IDENTITY_DESCRIPTOR_SCHEMA &&
         identity.profileCode == ULSA_EVO_ESP32_PROFILE_INITIAL &&
         identity.flags == 0U &&
         identity.versionCode == ULSA_EVO_ESP32_FIRMWARE_VERSION_CODE &&
         identity.revision == ULSA_EVO_ESP32_FIRMWARE_REVISION &&
         std::strcmp(identity.sourceCommit, ULSA_SOURCE_COMMIT_SHA) == 0 &&
         std::strcmp(identity.version,
                     ULSA_EVO_ESP32_FIRMWARE_VERSION_NAME) == 0 &&
         std::strcmp(identity.buildContractSha256,
                     ULSA_BUILD_CONTRACT_SHA256) == 0;
}

bool readMarker(CleanupMarker& marker) {
  Preferences preferences;
  if (!preferences.begin(FACTORY_NVS_NAMESPACE, false)) return false;
  marker.state = preferences.getUChar(CLEANUP_STATE_KEY, CLEANUP_NOT_STARTED);
  marker.cleanedAddress = preferences.getUInt(CLEANED_ADDRESS_KEY, 0);
  preferences.end();
  return true;
}

bool persistMarker(uint8_t state, uint32_t cleanedAddress) {
  Preferences preferences;
  if (!preferences.begin(FACTORY_NVS_NAMESPACE, false)) return false;
  const bool addressWritten =
    preferences.putUInt(CLEANED_ADDRESS_KEY, cleanedAddress) == sizeof(uint32_t);
  const bool stateWritten = addressWritten &&
    preferences.putUChar(CLEANUP_STATE_KEY, state) == sizeof(uint8_t);
  preferences.end();
  if (!stateWritten) return false;

  CleanupMarker verified = {};
  if (!readMarker(verified)) return false;
  return verified.state == state && verified.cleanedAddress == cleanedAddress;
}

bool markRunningImageValidIfPending(const esp_partition_t* running) {
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t result = esp_ota_get_state_partition(running, &state);
  if (result == ESP_ERR_NOT_SUPPORTED) return true;
  if (result != ESP_OK) return false;
  if (state != ESP_OTA_IMG_PENDING_VERIFY) return true;
  return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}

bool partitionIsFullyErased(const esp_partition_t* partition) {
  uint8_t buffer[ERASE_VERIFY_CHUNK_BYTES];
  for (size_t offset = 0; offset < partition->size;
       offset += sizeof(buffer)) {
    const size_t remaining = partition->size - offset;
    const size_t length = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    if (esp_partition_read(partition, offset, buffer, length) != ESP_OK) {
      return false;
    }
    for (size_t index = 0; index < length; ++index) {
      if (buffer[index] != 0xFFU) return false;
    }
  }
  return true;
}

}  // namespace

// Keep a newly installed Initial image rollback-pending until its factory
// partition and manager checks have passed.
extern "C" bool verifyRollbackLater() {
  return true;
}

InitialFactoryBootHealth::InitialFactoryBootHealth()
  : _ready(false), _error(BootHealthError::None) {}

bool InitialFactoryBootHealth::fail(BootHealthError error) {
  _ready = false;
  _error = error;
  return false;
}

bool InitialFactoryBootHealth::run(bool requiredManagerAvailable) {
  _ready = false;
  _error = BootHealthError::None;

  if (!readExactLayout()) return fail(BootHealthError::PartitionLayout);
  if (!runningIdentityIsCleanInitial()) {
    return fail(BootHealthError::FirmwareIdentity);
  }
  if (!requiredManagerAvailable) {
    return fail(BootHealthError::RequiredManagerUnavailable);
  }
  if (!legacyBridgeNvsIsAbsent()) {
    return fail(BootHealthError::LegacyBridgeNvsPresent);
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) return fail(BootHealthError::RunningPartition);
  const int inactiveIndex = inactiveOtaIndexForRunningAddress(running->address);
  if (inactiveIndex < 0 ||
      !samePartition(running,
                     EXPECTED_PARTITIONS[inactiveIndex == 2 ? 3 : 2])) {
    return fail(BootHealthError::RunningPartition);
  }

  const esp_partition_t* boot = esp_ota_get_boot_partition();
  if (boot == nullptr || boot->address != running->address ||
      boot->size != running->size) {
    return fail(BootHealthError::BootPartition);
  }

  const PartitionDescriptor& inactiveExpected = EXPECTED_PARTITIONS[inactiveIndex];
  const esp_partition_t* inactive = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP,
    static_cast<esp_partition_subtype_t>(inactiveExpected.subtype),
    inactiveExpected.label);
  if (!samePartition(inactive, inactiveExpected) || inactive == running) {
    return fail(BootHealthError::RunningPartition);
  }

  CleanupMarker marker = {};
  if (!readMarker(marker)) return fail(BootHealthError::MarkerStorage);
  if (marker.state > CLEANUP_COMPLETE) {
    return fail(BootHealthError::MarkerInvalid);
  }
  if (marker.state == CLEANUP_COMPLETE) {
    if (marker.cleanedAddress != inactive->address ||
        !partitionIsFullyErased(inactive)) {
      return fail(BootHealthError::InactiveVerify);
    }
    if (!markRunningImageValidIfPending(running)) {
      return fail(BootHealthError::OtaValidation);
    }
    _ready = true;
    return true;
  }

  if (marker.state == CLEANUP_IN_PROGRESS &&
      marker.cleanedAddress != inactive->address) {
    return fail(BootHealthError::MarkerInvalid);
  }

  // Blank factory-only data is required before the one-time destructive
  // cleanup. After completion these regions may legitimately hold a recovery
  // package or crash dump and must not suppress Initial status on later boots.
  const PartitionDescriptor& stm32PackageExpected = EXPECTED_PARTITIONS[4];
  const esp_partition_t* stm32Package = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    static_cast<esp_partition_subtype_t>(stm32PackageExpected.subtype),
    stm32PackageExpected.label);
  if (!samePartition(stm32Package, stm32PackageExpected) ||
      !partitionIsFullyErased(stm32Package)) {
    return fail(BootHealthError::Stm32PackageNotBlank);
  }

  const PartitionDescriptor& coredumpExpected = EXPECTED_PARTITIONS[5];
  const esp_partition_t* coredump = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    static_cast<esp_partition_subtype_t>(coredumpExpected.subtype),
    coredumpExpected.label);
  if (!samePartition(coredump, coredumpExpected) ||
      !partitionIsFullyErased(coredump)) {
    return fail(BootHealthError::CoredumpNotBlank);
  }

  if (!persistMarker(CLEANUP_IN_PROGRESS, inactive->address)) {
    return fail(BootHealthError::MarkerStorage);
  }
  if (!markRunningImageValidIfPending(running)) {
    return fail(BootHealthError::OtaValidation);
  }
  if (esp_partition_erase_range(inactive, 0, inactive->size) != ESP_OK) {
    return fail(BootHealthError::InactiveErase);
  }
  if (!partitionIsFullyErased(inactive)) {
    return fail(BootHealthError::InactiveVerify);
  }
  if (!persistMarker(CLEANUP_COMPLETE, inactive->address)) {
    return fail(BootHealthError::CompletionMarker);
  }

  _ready = true;
  return true;
}

const char* InitialFactoryBootHealth::getErrorString() const {
  switch (_error) {
    case BootHealthError::PartitionLayout: return "partition_layout";
    case BootHealthError::FirmwareIdentity: return "firmware_identity";
    case BootHealthError::RequiredManagerUnavailable: return "manager_unavailable";
    case BootHealthError::LegacyBridgeNvsPresent: return "bridge_nvs_present";
    case BootHealthError::RunningPartition: return "running_partition";
    case BootHealthError::BootPartition: return "boot_partition";
    case BootHealthError::MarkerStorage: return "marker_storage";
    case BootHealthError::MarkerInvalid: return "marker_invalid";
    case BootHealthError::OtaValidation: return "ota_validation";
    case BootHealthError::Stm32PackageNotBlank: return "stm32pkg_not_blank";
    case BootHealthError::CoredumpNotBlank: return "coredump_not_blank";
    case BootHealthError::InactiveErase: return "inactive_erase";
    case BootHealthError::InactiveVerify: return "inactive_verify";
    case BootHealthError::CompletionMarker: return "completion_marker";
    case BootHealthError::None:
    default: return "none";
  }
}

}  // namespace initial_factory
