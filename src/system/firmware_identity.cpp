/**
 * @file firmware_identity.cpp
 * @brief Embed the ESP32 version, profile, source, and build contract identity.
 */

#include "system/firmware_identity.h"

extern "C" const Esp32FirmwareIdentityDescriptor
    __attribute__((used, section(".rodata.ulsa_firmware_identity")))
    g_ulsaEsp32FirmwareIdentity = {
        {'U', 'L', 'S', 'A', 'E', '3', '2', 'V'},
        ULSA_EVO_ESP32_IDENTITY_DESCRIPTOR_SCHEMA,
        ULSA_FIRMWARE_PROFILE_CODE,
        ULSA_SOURCE_DIRTY ? ULSA_EVO_ESP32_IDENTITY_FLAG_DIRTY : 0U,
        ULSA_EVO_ESP32_FIRMWARE_VERSION_CODE,
        ULSA_EVO_ESP32_FIRMWARE_REVISION,
        ULSA_SOURCE_COMMIT_SHA,
        ULSA_EVO_ESP32_FIRMWARE_VERSION_NAME,
        ULSA_BUILD_CONTRACT_SHA256,
};

const Esp32FirmwareIdentityDescriptor& getEsp32FirmwareIdentity() {
  return g_ulsaEsp32FirmwareIdentity;
}
