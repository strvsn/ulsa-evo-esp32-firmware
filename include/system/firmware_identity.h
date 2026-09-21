#ifndef ULSA_EVO_ESP32_FIRMWARE_IDENTITY_H
#define ULSA_EVO_ESP32_FIRMWARE_IDENTITY_H

#include <stdint.h>

#include "system/firmware_version.h"
#include "system/firmware_identity_descriptor.h"

#ifndef ULSA_SOURCE_COMMIT_SHA
#error "ULSA_SOURCE_COMMIT_SHA must be injected by configure_reproducible_build.py"
#endif

#ifndef ULSA_SOURCE_DIRTY
#error "ULSA_SOURCE_DIRTY must be injected by configure_reproducible_build.py"
#endif

#ifndef ULSA_FIRMWARE_PROFILE_NAME
#error "ULSA_FIRMWARE_PROFILE_NAME must be injected by configure_reproducible_build.py"
#endif

#ifndef ULSA_FIRMWARE_PROFILE_CODE
#error "ULSA_FIRMWARE_PROFILE_CODE must be injected by configure_reproducible_build.py"
#endif

#ifndef ULSA_BUILD_CONTRACT_SHA256
#error "ULSA_BUILD_CONTRACT_SHA256 must be injected by configure_reproducible_build.py"
#endif

static_assert(sizeof(ULSA_SOURCE_COMMIT_SHA) == 41U,
              "ESP32 source commit must be a full 40-character SHA");
static_assert(sizeof(ULSA_BUILD_CONTRACT_SHA256) == 65U,
              "ESP32 build contract must be a full SHA-256");
static_assert(ULSA_FIRMWARE_PROFILE_CODE == 1U ||
                  ULSA_FIRMWARE_PROFILE_CODE == 2U,
              "ESP32 firmware profile code is invalid");
static_assert(sizeof(ULSA_EVO_ESP32_FIRMWARE_VERSION_NAME) <= 12U,
              "ESP32 canonical version does not fit the descriptor");

extern "C" const Esp32FirmwareIdentityDescriptor g_ulsaEsp32FirmwareIdentity;

const Esp32FirmwareIdentityDescriptor& getEsp32FirmwareIdentity();

#endif  // ULSA_EVO_ESP32_FIRMWARE_IDENTITY_H
