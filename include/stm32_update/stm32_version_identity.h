#ifndef ULSA_EVO_STM32_VERSION_IDENTITY_H
#define ULSA_EVO_STM32_VERSION_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

namespace stm32_update {

static const uint32_t STM32_VERSION_CODE_MARKER = 0x7E000000UL;
static const uint32_t STM32_INITIAL_VERSION_CODE = 0x7E010000UL;
static const uint32_t STM32_MIN_REVISION = 1UL;
static const uint16_t STM32_UPDATE_CONTRACT_VERSION = 3U;

struct Stm32SemanticVersion {
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
};

bool parseCanonicalStm32Version(const char* text, Stm32SemanticVersion& out);
bool formatCanonicalStm32Version(const Stm32SemanticVersion& version,
                                 char* output,
                                 size_t outputSize);
uint32_t packStm32VersionCode(const Stm32SemanticVersion& version);
bool decodeStm32VersionCode(uint32_t code, Stm32SemanticVersion& out);
int compareStm32Versions(const Stm32SemanticVersion& left,
                         const Stm32SemanticVersion& right);

}  // namespace stm32_update

#endif  // ULSA_EVO_STM32_VERSION_IDENTITY_H
