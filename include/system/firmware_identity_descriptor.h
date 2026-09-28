#ifndef ULSA_EVO_ESP32_FIRMWARE_IDENTITY_DESCRIPTOR_H
#define ULSA_EVO_ESP32_FIRMWARE_IDENTITY_DESCRIPTOR_H

#include <stdint.h>

#define ULSA_EVO_ESP32_IDENTITY_DESCRIPTOR_SCHEMA 1U
#define ULSA_EVO_ESP32_IDENTITY_FLAG_DIRTY 0x0001U
#define ULSA_EVO_ESP32_PROFILE_DEMO 1U
#define ULSA_EVO_ESP32_PROFILE_INITIAL 2U

#pragma pack(push, 1)
struct Esp32FirmwareIdentityDescriptor {
  char magic[8];
  uint8_t schemaVersion;
  uint8_t profileCode;
  uint16_t flags;
  uint32_t versionCode;
  uint32_t revision;
  char sourceCommit[41];
  char version[12];
  char buildContractSha256[65];
};
#pragma pack(pop)

static_assert(sizeof(Esp32FirmwareIdentityDescriptor) == 138U,
              "ESP32 firmware identity descriptor layout changed");

#endif  // ULSA_EVO_ESP32_FIRMWARE_IDENTITY_DESCRIPTOR_H
