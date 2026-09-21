/**
 * @file rtc_config_blob.h
 * @brief Stable on-flash codec for the rtc_cfg NVS blob.
 *
 * The byte layout is deliberately independent from C++ struct padding and the
 * compiler ABI. All multi-byte values are little-endian and the CRC covers the
 * first 12 bytes.
 */
#ifndef RTC_CONFIG_BLOB_H
#define RTC_CONFIG_BLOB_H

#include <stddef.h>
#include <stdint.h>

enum class RtcConfigState : uint8_t {
  Unconfigured = 0,
  Pending = 1,
  Valid = 2,
  ZoneOnly = 3,
};

struct RtcStoredConfig {
  RtcConfigState state = RtcConfigState::Unconfigured;
  uint32_t zoneId = 0;
};

namespace ulsa {
namespace rtc {

constexpr size_t RTC_CONFIG_BLOB_SIZE = 16;
constexpr uint8_t RTC_CONFIG_SCHEMA_VERSION = 1;

inline uint32_t rtcConfigCrc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xffffffffUL;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1) ^ (0xedb88320UL & mask);
    }
  }
  return ~crc;
}

inline void putU32Le(uint8_t* data, size_t offset, uint32_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
  data[offset + 2] = static_cast<uint8_t>(value >> 16);
  data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

inline uint32_t getU32Le(const uint8_t* data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) |
      (static_cast<uint32_t>(data[offset + 1]) << 8) |
      (static_cast<uint32_t>(data[offset + 2]) << 16) |
      (static_cast<uint32_t>(data[offset + 3]) << 24);
}

inline bool isRtcConfigStateValue(uint8_t value) {
  return value == static_cast<uint8_t>(RtcConfigState::Pending) ||
      value == static_cast<uint8_t>(RtcConfigState::Valid) ||
      value == static_cast<uint8_t>(RtcConfigState::ZoneOnly);
}

inline void encodeRtcConfigBlob(const RtcStoredConfig& config,
                                uint8_t (&blob)[RTC_CONFIG_BLOB_SIZE]) {
  blob[0] = 'U';
  blob[1] = 'R';
  blob[2] = 'T';
  blob[3] = 'C';
  blob[4] = RTC_CONFIG_SCHEMA_VERSION;
  blob[5] = static_cast<uint8_t>(config.state);
  blob[6] = 0;
  blob[7] = 0;
  putU32Le(blob, 8, config.zoneId);
  putU32Le(blob, 12, rtcConfigCrc32(blob, 12));
}

inline bool decodeRtcConfigBlob(const uint8_t* blob, size_t length,
                                RtcStoredConfig& config) {
  if (!blob || length != RTC_CONFIG_BLOB_SIZE) return false;
  if (blob[0] != 'U' || blob[1] != 'R' || blob[2] != 'T' || blob[3] != 'C') {
    return false;
  }
  if (blob[4] != RTC_CONFIG_SCHEMA_VERSION || blob[6] != 0 || blob[7] != 0 ||
      !isRtcConfigStateValue(blob[5])) {
    return false;
  }
  if (getU32Le(blob, 12) != rtcConfigCrc32(blob, 12)) return false;
  const uint32_t zoneId = getU32Le(blob, 8);
  if (zoneId == 0) return false;
  config.state = static_cast<RtcConfigState>(blob[5]);
  config.zoneId = zoneId;
  return true;
}

}  // namespace rtc
}  // namespace ulsa

#endif  // RTC_CONFIG_BLOB_H
