/**
 * @file stm32_package_keys.h
 * @brief Ed25519 public key configuration for STM32 package verification
 *
 * Public keys are not secret. The matching private signing key must stay
 * outside this repository and must never be included in ESP32 firmware.
 */

#ifndef STM32_UPDATE_STM32_PACKAGE_KEYS_H
#define STM32_UPDATE_STM32_PACKAGE_KEYS_H

#include "stm32_update/stm32_package.h"

namespace stm32_update {

static const char STM32_PACKAGE_EXPECTED_SIGNATURE_KEY_ID[] = "stm32-fw-prod-2026q3";

// Verification public key for locally stored STM32 package signing material.
// The matching private key is intentionally ignored by Git in the STM32 repo.
static const uint8_t STM32_PACKAGE_SIGNATURE_PUBLIC_KEY[STM32_PACKAGE_ED25519_PUBLIC_KEY_SIZE] = {
  0x3d, 0x09, 0x8c, 0xb9, 0xbe, 0x52, 0xe4, 0x47,
  0x70, 0xec, 0x8d, 0xba, 0xa1, 0xe0, 0xfd, 0x8d,
  0x57, 0x1e, 0xbc, 0xd8, 0xda, 0x08, 0xcf, 0x9c,
  0x4f, 0xd5, 0x19, 0x83, 0x3f, 0x40, 0xaf, 0xc9,
};

}  // namespace stm32_update

#endif  // STM32_UPDATE_STM32_PACKAGE_KEYS_H
