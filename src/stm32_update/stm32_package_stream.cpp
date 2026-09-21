/**
 * @file stm32_package_stream.cpp
 * @brief STM32 update package verifier for ESP32 scratch partitions
 */

#include "stm32_update/stm32_package_stream.h"

#include "stm32_package_internal.h"

#include <mbedtls/sha256.h>
#include <new>
#include <string.h>

namespace stm32_update {
namespace {

static const size_t STREAM_BUFFER_SIZE = 1024;
static const size_t MAX_MANIFEST_BYTES = 8192;
static const size_t MAX_CHUNK_TABLE_BYTES = 16 * 1024;

size_t minSize(size_t a, size_t b) {
  return a < b ? a : b;
}

PackageError readPartition(const esp_partition_t* partition,
                           size_t offset,
                           void* buffer,
                           size_t length) {
  if (length == 0) {
    return PackageError::Ok;
  }
  if (partition == nullptr || buffer == nullptr) {
    return PackageError::NullInput;
  }
  return esp_partition_read(partition, offset, buffer, length) == ESP_OK
    ? PackageError::Ok
    : PackageError::PartitionReadFailed;
}

PackageError readHeader(const esp_partition_t* partition,
                        PackageHeader& header,
                        size_t packageLength) {
  if (packageLength < STM32_PACKAGE_HEADER_SIZE) {
    return PackageError::TooSmall;
  }

  uint8_t raw[STM32_PACKAGE_HEADER_SIZE];
  PackageError error = readPartition(partition, 0, raw, sizeof(raw));
  if (error != PackageError::Ok) {
    return error;
  }
  if (memcmp(raw, package_internal::PACKAGE_MAGIC,
             sizeof(package_internal::PACKAGE_MAGIC) - 1) != 0) {
    return PackageError::BadMagic;
  }

  header.headerVersion = package_internal::readLe16(raw + 8);
  header.flags = package_internal::readLe16(raw + 10);
  header.manifestLength = package_internal::readLe32(raw + 12);
  header.chunkCount = package_internal::readLe32(raw + 16);
  header.chunkTableLength = package_internal::readLe32(raw + 20);
  header.packageLength = package_internal::readLe32(raw + 24);
  memcpy(header.manifestSha256, raw + 28, STM32_PACKAGE_SHA256_SIZE);
  memcpy(header.chunkTableSha256, raw + 60, STM32_PACKAGE_SHA256_SIZE);

  if (!package_internal::isSupportedHeaderVersion(header.headerVersion)) {
    return PackageError::UnsupportedHeaderVersion;
  }
  if (header.packageLength != packageLength) {
    return PackageError::LengthMismatch;
  }
  if (header.manifestLength == 0 ||
      header.manifestLength > MAX_MANIFEST_BYTES ||
      header.chunkCount == 0 ||
      header.chunkTableLength > MAX_CHUNK_TABLE_BYTES ||
      header.chunkTableLength != header.chunkCount * STM32_PACKAGE_CHUNK_TABLE_ENTRY_SIZE) {
    return PackageError::LengthMismatch;
  }

  return PackageError::Ok;
}

PackageError verifyChunkPayload(const esp_partition_t* partition,
                                const uint8_t* chunkTableBytes,
                                const PackageView& view,
                                size_t payloadStart,
                                PackageStreamProgressCallback progressCallback,
                                void* progressContext) {
  if (view.manifest.chunkCount != view.header.chunkCount ||
      view.chunkTableLength != view.manifest.chunkCount * STM32_PACKAGE_CHUNK_TABLE_ENTRY_SIZE) {
    return PackageError::ChunkTableInvalid;
  }

  uint8_t buffer[STREAM_BUFFER_SIZE];
  uint8_t payloadDigest[STM32_PACKAGE_SHA256_SIZE];
  uint8_t plainDigest[STM32_PACKAGE_SHA256_SIZE];
  PackageError error = PackageError::Ok;

  mbedtls_sha256_context payloadCtx;
  mbedtls_sha256_context plainCtx;
  const bool encrypted = view.manifest.payloadEncoding == "aes-256-gcm-chunked";
  mbedtls_sha256_init(&payloadCtx);
  mbedtls_sha256_init(&plainCtx);
  mbedtls_sha256_starts_ret(&payloadCtx, 0);
  if (!encrypted) mbedtls_sha256_starts_ret(&plainCtx, 0);

  uint32_t expectedPlainOffset = 0;
  uint32_t expectedFrameOffset = 0;
  size_t verifiedBytes = 0;

  if (progressCallback != nullptr) {
    progressCallback(0, view.payloadLength, progressContext);
  }

  for (uint32_t index = 0; index < view.manifest.chunkCount; ++index) {
    const uint8_t* entry = chunkTableBytes + index * STM32_PACKAGE_CHUNK_TABLE_ENTRY_SIZE;
    const uint32_t chunkIndex = package_internal::readLe32(entry);
    const uint32_t plainOffset = package_internal::readLe32(entry + 4);
    const uint32_t plainSize = package_internal::readLe32(entry + 8);
    const uint32_t frameOffset = package_internal::readLe32(entry + 12);
    const uint32_t frameSize = package_internal::readLe32(entry + 16);
    const uint8_t* expectedHash = entry + 20;

    if (chunkIndex != index ||
        plainOffset != expectedPlainOffset ||
        frameOffset != expectedFrameOffset ||
        frameOffset > view.payloadLength ||
        frameSize > view.payloadLength - frameOffset ||
        plainSize == 0 ||
        plainSize > STM32_PACKAGE_MAX_CHUNK_SIZE) {
      error = PackageError::ChunkTableInvalid;
      break;
    }
    if (frameSize != plainSize + (encrypted ? 28u : 0u)) {
      error = PackageError::ChunkTableInvalid;
      break;
    }

    const bool verifyFrameHash =
      encrypted && view.manifest.chunkHashEncoding == "frame-sha256";
    mbedtls_sha256_context chunkCtx;
    mbedtls_sha256_init(&chunkCtx);
    if (!encrypted || verifyFrameHash) mbedtls_sha256_starts_ret(&chunkCtx, 0);

    size_t remaining = frameSize;
    size_t readOffset = 0;
    while (remaining > 0) {
      const size_t readSize = minSize(remaining, sizeof(buffer));
      error = readPartition(partition, payloadStart + frameOffset + readOffset, buffer, readSize);
      if (error != PackageError::Ok) {
        break;
      }
      if (!encrypted || verifyFrameHash) {
        mbedtls_sha256_update_ret(&chunkCtx, buffer, readSize);
      }
      mbedtls_sha256_update_ret(&payloadCtx, buffer, readSize);
      if (!encrypted) mbedtls_sha256_update_ret(&plainCtx, buffer, readSize);
      remaining -= readSize;
      readOffset += readSize;
      verifiedBytes += readSize;
      if (progressCallback != nullptr) {
        progressCallback(verifiedBytes, view.payloadLength, progressContext);
      }
    }

    uint8_t chunkDigest[STM32_PACKAGE_SHA256_SIZE] = {};
    if (!encrypted || verifyFrameHash) {
      mbedtls_sha256_finish_ret(&chunkCtx, chunkDigest);
    }
    mbedtls_sha256_free(&chunkCtx);

    if (error != PackageError::Ok) {
      break;
    }
    if ((!encrypted || verifyFrameHash) &&
        !package_internal::digestEquals(chunkDigest, expectedHash)) {
      error = PackageError::ChunkHashMismatch;
      break;
    }

    expectedPlainOffset += plainSize;
    expectedFrameOffset += frameSize;
  }

  if (error == PackageError::Ok) {
    mbedtls_sha256_finish_ret(&payloadCtx, payloadDigest);
    if (!encrypted) mbedtls_sha256_finish_ret(&plainCtx, plainDigest);

    if (expectedPlainOffset != view.manifest.size ||
        expectedFrameOffset != view.payloadLength) {
      error = PackageError::ChunkTableInvalid;
    } else if (!package_internal::hexDigestEquals(view.manifest.payloadSha256, payloadDigest) ||
               (!encrypted && !package_internal::hexDigestEquals(view.manifest.plainSha256, plainDigest))) {
      error = PackageError::PayloadHashMismatch;
    }
  }

  mbedtls_sha256_free(&payloadCtx);
  mbedtls_sha256_free(&plainCtx);
  return error;
}

}  // namespace

PackageError verifyPackageFromPartition(const esp_partition_t* partition,
                                        size_t packageLength,
                                        const PackageVerifyOptions& options,
                                        PackageStreamVerifyResult& out,
                                        PackageStreamProgressCallback progressCallback,
                                        void* progressContext) {
  if (partition == nullptr) {
    return PackageError::NullInput;
  }
  if (packageLength > options.maxPackageSize) {
    return PackageError::SizeLimitExceeded;
  }

  PackageHeader header;
  PackageError error = readHeader(partition, header, packageLength);
  if (error != PackageError::Ok) {
    return error;
  }

  const size_t manifestStart = STM32_PACKAGE_HEADER_SIZE;
  if (header.manifestLength > packageLength - manifestStart) {
    return PackageError::LengthOverflow;
  }
  const size_t chunkTableStart = manifestStart + header.manifestLength;
  if (header.chunkTableLength > packageLength - chunkTableStart) {
    return PackageError::LengthOverflow;
  }
  const size_t payloadStart = chunkTableStart + header.chunkTableLength;
  const size_t payloadLength = packageLength - payloadStart;

  uint8_t* manifestBytes = new (std::nothrow) uint8_t[header.manifestLength];
  uint8_t* chunkTableBytes = new (std::nothrow) uint8_t[header.chunkTableLength];
  if (manifestBytes == nullptr || chunkTableBytes == nullptr) {
    delete[] manifestBytes;
    delete[] chunkTableBytes;
    return PackageError::OutOfMemory;
  }

  error = readPartition(partition, manifestStart, manifestBytes, header.manifestLength);
  if (error == PackageError::Ok) {
    error = readPartition(partition, chunkTableStart, chunkTableBytes, header.chunkTableLength);
  }

  PackageView view;
  view.header = header;
  view.manifestBytes = manifestBytes;
  view.chunkTableBytes = chunkTableBytes;
  view.payloadBytes = nullptr;
  view.manifestLength = header.manifestLength;
  view.chunkTableLength = header.chunkTableLength;
  view.payloadLength = payloadLength;

  if (error == PackageError::Ok) {
    uint8_t digest[STM32_PACKAGE_SHA256_SIZE];
    package_internal::sha256(manifestBytes, header.manifestLength, digest);
    if (!package_internal::digestEquals(digest, header.manifestSha256)) {
      error = PackageError::ManifestHashMismatch;
    }
  }
  if (error == PackageError::Ok) {
    uint8_t digest[STM32_PACKAGE_SHA256_SIZE];
    package_internal::sha256(chunkTableBytes, header.chunkTableLength, digest);
    if (!package_internal::digestEquals(digest, header.chunkTableSha256)) {
      error = PackageError::ChunkTableHashMismatch;
    }
  }
  if (error == PackageError::Ok) {
    error = package_internal::parseManifest(manifestBytes, header.manifestLength, view.manifest);
  }
  if (error == PackageError::Ok) {
    error = package_internal::validateHeaderManifestContract(header, view.manifest);
  }
  if (error == PackageError::Ok) {
    error = package_internal::validateManifestPolicy(view.manifest, options);
  }
  if (error == PackageError::Ok &&
      !package_internal::hexDigestEquals(view.manifest.chunkTableSha256, header.chunkTableSha256)) {
    error = PackageError::ChunkTableHashMismatch;
  }
  if (error == PackageError::Ok) {
    error = package_internal::verifySignature(view, options);
  }
  if (error == PackageError::Ok) {
    error = verifyChunkPayload(
      partition,
      chunkTableBytes,
      view,
      payloadStart,
      progressCallback,
      progressContext);
  }

  if (error == PackageError::Ok) {
    out.header = header;
    out.manifest = view.manifest;
    out.payloadLength = payloadLength;
  }

  delete[] manifestBytes;
  delete[] chunkTableBytes;
  return error;
}

PackageError readPackageLengthFromPartition(const esp_partition_t* partition,
                                            size_t partitionCapacity,
                                            size_t& outPackageLength) {
  outPackageLength = 0;
  if (partition == nullptr) {
    return PackageError::NullInput;
  }
  if (partitionCapacity < STM32_PACKAGE_HEADER_SIZE) {
    return PackageError::TooSmall;
  }

  uint8_t raw[STM32_PACKAGE_HEADER_SIZE];
  PackageError error = readPartition(partition, 0, raw, sizeof(raw));
  if (error != PackageError::Ok) {
    return error;
  }
  if (memcmp(raw, package_internal::PACKAGE_MAGIC,
             sizeof(package_internal::PACKAGE_MAGIC) - 1) != 0) {
    return PackageError::BadMagic;
  }

  const uint16_t headerVersion = package_internal::readLe16(raw + 8);
  const uint32_t packageLength = package_internal::readLe32(raw + 24);
  if (!package_internal::isSupportedHeaderVersion(headerVersion)) {
    return PackageError::UnsupportedHeaderVersion;
  }
  if (packageLength < STM32_PACKAGE_HEADER_SIZE || packageLength > partitionCapacity) {
    return PackageError::LengthOverflow;
  }

  outPackageLength = packageLength;
  return PackageError::Ok;
}

}  // namespace stm32_update
