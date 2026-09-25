#ifndef FIRMWARE_VERSION_H
#define FIRMWARE_VERSION_H

#include <stdint.h>

#define ULSA_EVO_ESP32_FIRMWARE_SEMVER_MAJOR 1U
#define ULSA_EVO_ESP32_FIRMWARE_SEMVER_MINOR 0U
#define ULSA_EVO_ESP32_FIRMWARE_SEMVER_PATCH 1U
#define ULSA_EVO_ESP32_FIRMWARE_VERSION_NAME "1.0.1"
#define ULSA_EVO_ESP32_FIRMWARE_VERSION_CODE 0x7E010001UL
#define ULSA_EVO_ESP32_FIRMWARE_REVISION 10UL
#define ULSA_EVO_ESP32_FIRMWARE_VERSION ULSA_EVO_ESP32_FIRMWARE_VERSION_NAME

#define ULSA_EVO_MANUFACTURER_NAME "StratoVision LLC"
#define ULSA_EVO_MODEL_NUMBER "ULSA EVO"

struct Esp32SemanticVersion {
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
};

static inline uint32_t packEsp32FirmwareVersionCode(const Esp32SemanticVersion& version) {
  return 0x7E000000UL |
         (static_cast<uint32_t>(version.major) << 16U) |
         (static_cast<uint32_t>(version.minor) << 8U) |
         static_cast<uint32_t>(version.patch);
}

static inline bool decodeEsp32FirmwareVersionCode(uint32_t code,
                                                  Esp32SemanticVersion& out) {
  if ((code & 0xFF000000UL) != 0x7E000000UL) return false;
  out.major = static_cast<uint8_t>((code >> 16U) & 0xFFU);
  out.minor = static_cast<uint8_t>((code >> 8U) & 0xFFU);
  out.patch = static_cast<uint8_t>(code & 0xFFU);
  return true;
}

static_assert(ULSA_EVO_ESP32_FIRMWARE_VERSION_CODE ==
                  (0x7E000000UL |
                   (ULSA_EVO_ESP32_FIRMWARE_SEMVER_MAJOR << 16U) |
                   (ULSA_EVO_ESP32_FIRMWARE_SEMVER_MINOR << 8U) |
                   ULSA_EVO_ESP32_FIRMWARE_SEMVER_PATCH),
              "ESP32 firmware versionCode differs from SemVer");

#endif // FIRMWARE_VERSION_H
