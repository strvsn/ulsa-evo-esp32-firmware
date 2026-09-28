/**
 * @file stm32_package_stream.h
 * @brief STM32 update package verifier for ESP32 scratch partitions
 */

#ifndef STM32_UPDATE_STM32_PACKAGE_STREAM_H
#define STM32_UPDATE_STM32_PACKAGE_STREAM_H

#include "stm32_update/stm32_package.h"

#include <esp_partition.h>

namespace stm32_update {

using PackageStreamProgressCallback = void (*)(size_t verifiedBytes,
                                                size_t totalBytes,
                                                void* context);

struct PackageStreamVerifyResult {
  PackageHeader header;
  PackageManifest manifest;
  size_t payloadLength;
};

PackageError verifyPackageFromPartition(const esp_partition_t* partition,
                                        size_t packageLength,
                                        const PackageVerifyOptions& options,
                                        PackageStreamVerifyResult& out,
                                        PackageStreamProgressCallback progressCallback = nullptr,
                                        void* progressContext = nullptr);

PackageError readPackageLengthFromPartition(const esp_partition_t* partition,
                                            size_t partitionCapacity,
                                            size_t& outPackageLength);

}  // namespace stm32_update

#endif  // STM32_UPDATE_STM32_PACKAGE_STREAM_H
