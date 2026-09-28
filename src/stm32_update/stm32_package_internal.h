/**
 * @file stm32_package_internal.h
 * @brief Internal helpers for STM32 update package verification
 */

#ifndef STM32_UPDATE_STM32_PACKAGE_INTERNAL_H
#define STM32_UPDATE_STM32_PACKAGE_INTERNAL_H

#include "stm32_update/stm32_package.h"

namespace stm32_update {
namespace package_internal {

extern const char PACKAGE_MAGIC[9];
extern const uint16_t PACKAGE_HEADER_VERSION_V2;
extern const uint16_t PACKAGE_HEADER_VERSION_V3;

bool isSupportedHeaderVersion(uint16_t version);
PackageError validateHeaderManifestContract(const PackageHeader& header,
                                            const PackageManifest& manifest);

uint16_t readLe16(const uint8_t* data);
uint32_t readLe32(const uint8_t* data);

void sha256(const uint8_t* data, size_t length, uint8_t out[STM32_PACKAGE_SHA256_SIZE]);

bool digestEquals(const uint8_t* a, const uint8_t* b);
bool hexDigestEquals(const String& hex, const uint8_t digest[STM32_PACKAGE_SHA256_SIZE]);

PackageError parseManifest(const uint8_t* bytes, size_t length, PackageManifest& manifest);

PackageError validateManifestPolicy(const PackageManifest& manifest,
                                    const PackageVerifyOptions& options);

PackageError verifySignature(const PackageView& view, const PackageVerifyOptions& options);

PackageError verifyChunks(const PackageView& view);

}  // namespace package_internal
}  // namespace stm32_update

#endif  // STM32_UPDATE_STM32_PACKAGE_INTERNAL_H
