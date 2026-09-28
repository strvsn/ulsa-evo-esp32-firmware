#include <cassert>
#include <cstdio>
#include <cstring>

#include "system/firmware_version.h"

int main() {
  char expectedVersionName[16] = {};
  const int written = std::snprintf(
      expectedVersionName,
      sizeof(expectedVersionName),
      "%u.%u.%u",
      static_cast<unsigned int>(ULSA_EVO_ESP32_FIRMWARE_SEMVER_MAJOR),
      static_cast<unsigned int>(ULSA_EVO_ESP32_FIRMWARE_SEMVER_MINOR),
      static_cast<unsigned int>(ULSA_EVO_ESP32_FIRMWARE_SEMVER_PATCH));
  assert(written > 0);
  assert(static_cast<unsigned int>(written) < sizeof(expectedVersionName));
  assert(std::strcmp(ULSA_EVO_ESP32_FIRMWARE_VERSION_NAME, expectedVersionName) == 0);

  const Esp32SemanticVersion configuredVersion = {
      ULSA_EVO_ESP32_FIRMWARE_SEMVER_MAJOR,
      ULSA_EVO_ESP32_FIRMWARE_SEMVER_MINOR,
      ULSA_EVO_ESP32_FIRMWARE_SEMVER_PATCH,
  };
  assert(ULSA_EVO_ESP32_FIRMWARE_VERSION_CODE ==
         packEsp32FirmwareVersionCode(configuredVersion));
  assert(ULSA_EVO_ESP32_FIRMWARE_REVISION > 0UL);

  Esp32SemanticVersion version = {1U, 12U, 3U};
  assert(packEsp32FirmwareVersionCode(version) == 0x7E010C03UL);

  Esp32SemanticVersion decoded = {};
  assert(decodeEsp32FirmwareVersionCode(0x7EFFFFFFUL, decoded));
  assert(decoded.major == 255U);
  assert(decoded.minor == 255U);
  assert(decoded.patch == 255U);
  assert(!decodeEsp32FirmwareVersionCode(20260703UL, decoded));
  return 0;
}
