/**
 * @file stm32_package_payload.cpp
 * @brief Helpers for reading verified STM32 package payload chunks
 */

#include "stm32_update/stm32_package_payload.h"

#include "stm32_package_internal.h"

#include <string.h>

namespace stm32_update {

PackageError readPackageChunkEntry(const esp_partition_t* partition,
                                   size_t chunkTableOffset,
                                   uint32_t chunkIndex,
                                   PackageChunkEntry& out) {
  if (partition == nullptr) {
    return PackageError::NullInput;
  }

  uint8_t raw[STM32_PACKAGE_CHUNK_TABLE_ENTRY_SIZE];
  const size_t offset =
    chunkTableOffset + (size_t)chunkIndex * STM32_PACKAGE_CHUNK_TABLE_ENTRY_SIZE;
  if (esp_partition_read(partition, offset, raw, sizeof(raw)) != ESP_OK) {
    return PackageError::PartitionReadFailed;
  }

  out.index = package_internal::readLe32(raw);
  out.plainOffset = package_internal::readLe32(raw + 4);
  out.plainSize = package_internal::readLe32(raw + 8);
  out.frameOffset = package_internal::readLe32(raw + 12);
  out.frameSize = package_internal::readLe32(raw + 16);
  memcpy(out.hash, raw + 20, STM32_PACKAGE_SHA256_SIZE);
  return PackageError::Ok;
}

PackageError readPackagePayloadBytes(const esp_partition_t* partition,
                                     size_t payloadOffset,
                                     uint32_t frameOffset,
                                     size_t readOffset,
                                     uint8_t* buffer,
                                     size_t length) {
  if (length == 0) {
    return PackageError::Ok;
  }
  if (partition == nullptr || buffer == nullptr) {
    return PackageError::NullInput;
  }

  const size_t offset = payloadOffset + (size_t)frameOffset + readOffset;
  return esp_partition_read(partition, offset, buffer, length) == ESP_OK
    ? PackageError::Ok
    : PackageError::PartitionReadFailed;
}

PackageError readPackagePlainChunk(const esp_partition_t* partition,
                                   size_t payloadOffset,
                                   const PackageManifest& manifest,
                                   const PackageChunkEntry& entry,
                                   uint8_t* out,
                                   size_t outCapacity) {
  if (partition == nullptr || out == nullptr) {
    return PackageError::NullInput;
  }
  if (entry.plainSize == 0 ||
      entry.plainSize > STM32_PACKAGE_MAX_CHUNK_SIZE ||
      outCapacity < entry.plainSize) {
    return PackageError::ChunkTableInvalid;
  }

  if (manifest.payloadEncoding != "plain" || entry.frameSize != entry.plainSize) {
    return PackageError::InvalidFieldValue;
  }

  PackageError error = readPackagePayloadBytes(
    partition,
    payloadOffset,
    entry.frameOffset,
    0,
    out,
    entry.plainSize);
  if (error != PackageError::Ok) {
    return error;
  }

  uint8_t digest[STM32_PACKAGE_SHA256_SIZE];
  package_internal::sha256(out, entry.plainSize, digest);
  return package_internal::digestEquals(digest, entry.hash)
    ? PackageError::Ok
    : PackageError::ChunkHashMismatch;
}

}  // namespace stm32_update
