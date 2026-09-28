/**
 * @file stm32_target_identity.cpp
 * @brief Bind signed package targets to supported STM32 ROM chip IDs.
 */

#include "stm32_update/stm32_target_identity.h"

#include <string.h>

namespace stm32_update {

bool expectedChipIdForStm32Target(const char* target, uint16_t& chipId) {
  if (target == nullptr) return false;
  if (strcmp(target, "ULSA_EVO_STM32_F411") == 0) {
    chipId = STM32_CHIP_ID_F411;
    return true;
  }
  if (strcmp(target, "ULSA_EVO_STM32_F446") == 0) {
    chipId = STM32_CHIP_ID_F446;
    return true;
  }
  return false;
}

}  // namespace stm32_update
