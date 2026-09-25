/**
 * @file stm32_version_identity.cpp
 * @brief Canonical STM32 SemVer versionCode codec.
 */

#include "stm32_update/stm32_version_identity.h"

namespace stm32_update {
namespace {

bool parseComponent(const char*& cursor, uint8_t& out) {
  if (cursor == nullptr || *cursor < '0' || *cursor > '9') return false;
  if (*cursor == '0' && cursor[1] >= '0' && cursor[1] <= '9') return false;
  unsigned int value = 0;
  do {
    value = value * 10U + static_cast<unsigned int>(*cursor - '0');
    if (value > 255U) return false;
    ++cursor;
  } while (*cursor >= '0' && *cursor <= '9');
  out = static_cast<uint8_t>(value);
  return true;
}

bool appendComponent(uint8_t value, char* output, size_t outputSize, size_t& cursor) {
  char reversed[3];
  size_t count = 0;
  do {
    reversed[count++] = static_cast<char>('0' + value % 10U);
    value = static_cast<uint8_t>(value / 10U);
  } while (value != 0U && count < sizeof(reversed));
  if (cursor + count >= outputSize) return false;
  while (count > 0U) output[cursor++] = reversed[--count];
  return true;
}

}  // namespace

bool parseCanonicalStm32Version(const char* text, Stm32SemanticVersion& out) {
  if (text == nullptr) return false;
  const char* cursor = text;
  Stm32SemanticVersion parsed = {};
  if (!parseComponent(cursor, parsed.major) || *cursor++ != '.' ||
      !parseComponent(cursor, parsed.minor) || *cursor++ != '.' ||
      !parseComponent(cursor, parsed.patch) || *cursor != '\0') {
    return false;
  }
  out = parsed;
  return true;
}

bool formatCanonicalStm32Version(const Stm32SemanticVersion& version,
                                 char* output,
                                 size_t outputSize) {
  if (output == nullptr || outputSize == 0U) return false;
  size_t cursor = 0;
  if (!appendComponent(version.major, output, outputSize, cursor) ||
      cursor + 1U >= outputSize) {
    output[0] = '\0';
    return false;
  }
  output[cursor++] = '.';
  if (!appendComponent(version.minor, output, outputSize, cursor) ||
      cursor + 1U >= outputSize) {
    output[0] = '\0';
    return false;
  }
  output[cursor++] = '.';
  if (!appendComponent(version.patch, output, outputSize, cursor) ||
      cursor >= outputSize) {
    output[0] = '\0';
    return false;
  }
  output[cursor] = '\0';
  return true;
}

uint32_t packStm32VersionCode(const Stm32SemanticVersion& version) {
  return STM32_VERSION_CODE_MARKER |
         (static_cast<uint32_t>(version.major) << 16U) |
         (static_cast<uint32_t>(version.minor) << 8U) |
         static_cast<uint32_t>(version.patch);
}

bool decodeStm32VersionCode(uint32_t code, Stm32SemanticVersion& out) {
  if ((code & 0xFF000000UL) != STM32_VERSION_CODE_MARKER) return false;
  out.major = static_cast<uint8_t>((code >> 16U) & 0xFFU);
  out.minor = static_cast<uint8_t>((code >> 8U) & 0xFFU);
  out.patch = static_cast<uint8_t>(code & 0xFFU);
  return true;
}

int compareStm32Versions(const Stm32SemanticVersion& left,
                         const Stm32SemanticVersion& right) {
  if (left.major != right.major) return left.major < right.major ? -1 : 1;
  if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
  if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
  return 0;
}

}  // namespace stm32_update
