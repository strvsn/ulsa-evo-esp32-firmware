/**
 * @file stm32_package_payload.h
 * @brief Helpers for reading verified STM32 package payload chunks
 */

#ifndef STM32_UPDATE_STM32_PACKAGE_PAYLOAD_H
#define STM32_UPDATE_STM32_PACKAGE_PAYLOAD_H

#include "stm32_update/stm32_package.h"

#include <esp_partition.h>

namespace stm32_update {

struct PackageChunkEntry {
  uint32_t index = 0;
  uint32_t plainOffset = 0;
  uint32_t plainSize = 0;
  uint32_t frameOffset = 0;
  uint32_t frameSize = 0;
  uint8_t hash[STM32_PACKAGE_SHA256_SIZE] = {};
};

PackageError readPackageChunkEntry(const esp_partition_t* partition,
                                   size_t chunkTableOffset,
                                   uint32_t chunkIndex,
                                   PackageChunkEntry& out);

PackageError readPackagePayloadBytes(const esp_partition_t* partition,
                                     size_t payloadOffset,
                                     uint32_t frameOffset,
                                     size_t readOffset,
                                     uint8_t* buffer,
                                     size_t length);

PackageError readPackagePlainChunk(const esp_partition_t* partition,
                                   size_t payloadOffset,
                                   const PackageManifest& manifest,
                                   const PackageChunkEntry& entry,
                                   uint8_t* out,
                                   size_t outCapacity);

}  // namespace stm32_update

#endif  // STM32_UPDATE_STM32_PACKAGE_PAYLOAD_H
