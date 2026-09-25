#include "stm32_update/stm32_target_identity.h"

using namespace stm32_update;

int main() {
  uint16_t chipId = 0;
  if (!expectedChipIdForStm32Target("ULSA_EVO_STM32_F411", chipId) ||
      chipId != STM32_CHIP_ID_F411) return 1;
  if (!expectedChipIdForStm32Target("ULSA_EVO_STM32_F446", chipId) ||
      chipId != STM32_CHIP_ID_F446) return 2;
  if (expectedChipIdForStm32Target("ULSA_EVO_STM32_F429", chipId)) return 3;
  if (expectedChipIdForStm32Target(nullptr, chipId)) return 4;
  return 0;
}
