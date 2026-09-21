#ifndef ULSA_EVO_STM32_TARGET_IDENTITY_H
#define ULSA_EVO_STM32_TARGET_IDENTITY_H

#include <stdint.h>

namespace stm32_update {

static const uint16_t STM32_CHIP_ID_F411 = 0x0431U;
static const uint16_t STM32_CHIP_ID_F446 = 0x0421U;

bool expectedChipIdForStm32Target(const char* target, uint16_t& chipId);

}  // namespace stm32_update

#endif  // ULSA_EVO_STM32_TARGET_IDENTITY_H
