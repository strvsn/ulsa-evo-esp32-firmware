/**
 * @file demo_ota_boot_health.cpp
 * @brief Delay ESP-IDF OTA validity until the Demo BLE surface is usable.
 */

#include "system/demo_ota_boot_health.h"

#include <Arduino.h>
#include <esp_ota_ops.h>

#include "system/firmware_identity.h"

namespace {
constexpr uint32_t DEMO_OTA_BOOT_HEALTH_TIMEOUT_MS = 30000U;
bool pendingVerification = false;
bool healthFailed = false;
uint32_t verificationDeadlineMs = 0;

bool deadlinePassed(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}
}  // namespace

extern "C" bool verifyRollbackLater() {
  return true;
}

void beginDemoOtaBootHealth() {
  pendingVerification = false;
  healthFailed = false;
  verificationDeadlineMs = 0;

  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (running != nullptr &&
      esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    pendingVerification = true;
    verificationDeadlineMs = millis() + DEMO_OTA_BOOT_HEALTH_TIMEOUT_MS;
  }
}

void serviceDemoOtaBootHealth(bool bleAdvertisingReady) {
  if (!pendingVerification || healthFailed) return;

  const Esp32FirmwareIdentityDescriptor& identity = getEsp32FirmwareIdentity();
  const bool identityValid =
    identity.schemaVersion == ULSA_EVO_ESP32_IDENTITY_DESCRIPTOR_SCHEMA &&
    identity.profileCode == ULSA_EVO_ESP32_PROFILE_DEMO &&
    (identity.flags & ULSA_EVO_ESP32_IDENTITY_FLAG_DIRTY) == 0U;

  if (bleAdvertisingReady && identityValid) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      pendingVerification = false;
      verificationDeadlineMs = 0;
    } else {
      healthFailed = true;
    }
    return;
  }

  if (!deadlinePassed(millis(), verificationDeadlineMs)) return;
  const esp_err_t rollbackResult = esp_ota_mark_app_invalid_rollback_and_reboot();
  if (rollbackResult != ESP_OK) {
    healthFailed = true;
    pendingVerification = false;
  }
}

bool demoOtaBootHealthFailed() {
  return healthFailed;
}
