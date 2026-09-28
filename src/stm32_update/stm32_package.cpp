/**
 * @file stm32_package.cpp
 * @brief STM32 update package parser and verifier
 */

#include "stm32_update/stm32_package.h"

#include "stm32_package_internal.h"

#include <string.h>

namespace stm32_update {

using namespace package_internal;

const char* packageErrorToString(PackageError error) {
  switch (error) {
    case PackageError::Ok: return "OK";
    case PackageError::NullInput: return "NULL_INPUT";
    case PackageError::TooSmall: return "TOO_SMALL";
    case PackageError::BadMagic: return "BAD_MAGIC";
    case PackageError::UnsupportedHeaderVersion: return "UNSUPPORTED_HEADER_VERSION";
    case PackageError::LengthMismatch: return "LENGTH_MISMATCH";
    case PackageError::LengthOverflow: return "LENGTH_OVERFLOW";
    case PackageError::ManifestHashMismatch: return "MANIFEST_HASH_MISMATCH";
    case PackageError::ChunkTableHashMismatch: return "CHUNK_TABLE_HASH_MISMATCH";
    case PackageError::ManifestParseFailed: return "MANIFEST_PARSE_FAILED";
    case PackageError::MissingRequiredField: return "MISSING_REQUIRED_FIELD";
    case PackageError::InvalidFieldValue: return "INVALID_FIELD_VALUE";
    case PackageError::PolicyRejected: return "POLICY_REJECTED";
    case PackageError::SizeLimitExceeded: return "SIZE_LIMIT_EXCEEDED";
    case PackageError::PayloadHashMismatch: return "PAYLOAD_HASH_MISMATCH";
    case PackageError::ChunkHashMismatch: return "CHUNK_HASH_MISMATCH";
    case PackageError::ChunkTableInvalid: return "CHUNK_TABLE_INVALID";
    case PackageError::SignatureKeyMissing: return "SIGNATURE_KEY_MISSING";
    case PackageError::SignatureDecodeFailed: return "SIGNATURE_DECODE_FAILED";
    case PackageError::SignatureInvalid: return "SIGNATURE_INVALID";
    case PackageError::PartitionReadFailed: return "PARTITION_READ_FAILED";
    case PackageError::OutOfMemory: return "OUT_OF_MEMORY";
    default: return "UNKNOWN";
  }
}

PackageError inspectPackageBuffer(const uint8_t* data, size_t length, PackageView& out) {
  if (data == nullptr) {
    return PackageError::NullInput;
  }
  if (length < STM32_PACKAGE_HEADER_SIZE) {
    return PackageError::TooSmall;
  }
  if (memcmp(data, PACKAGE_MAGIC, sizeof(PACKAGE_MAGIC) - 1) != 0) {
    return PackageError::BadMagic;
  }

  out.header.headerVersion = readLe16(data + 8);
  out.header.flags = readLe16(data + 10);
  out.header.manifestLength = readLe32(data + 12);
  out.header.chunkCount = readLe32(data + 16);
  out.header.chunkTableLength = readLe32(data + 20);
  out.header.packageLength = readLe32(data + 24);
  memcpy(out.header.manifestSha256, data + 28, STM32_PACKAGE_SHA256_SIZE);
  memcpy(out.header.chunkTableSha256, data + 60, STM32_PACKAGE_SHA256_SIZE);

  if (!isSupportedHeaderVersion(out.header.headerVersion)) {
    return PackageError::UnsupportedHeaderVersion;
  }
  if (out.header.packageLength != length) {
    return PackageError::LengthMismatch;
  }
  if (out.header.manifestLength == 0 ||
      out.header.chunkTableLength != out.header.chunkCount * STM32_PACKAGE_CHUNK_TABLE_ENTRY_SIZE) {
    return PackageError::LengthMismatch;
  }

  const size_t manifestStart = STM32_PACKAGE_HEADER_SIZE;
  const size_t manifestEnd = manifestStart + out.header.manifestLength;
  if (manifestEnd < manifestStart || manifestEnd > length) {
    return PackageError::LengthOverflow;
  }
  const size_t chunkTableEnd = manifestEnd + out.header.chunkTableLength;
  if (chunkTableEnd < manifestEnd || chunkTableEnd > length) {
    return PackageError::LengthOverflow;
  }

  out.manifestBytes = data + manifestStart;
  out.chunkTableBytes = data + manifestEnd;
  out.payloadBytes = data + chunkTableEnd;
  out.manifestLength = out.header.manifestLength;
  out.chunkTableLength = out.header.chunkTableLength;
  out.payloadLength = length - chunkTableEnd;

  uint8_t digest[STM32_PACKAGE_SHA256_SIZE];
  sha256(out.manifestBytes, out.manifestLength, digest);
  if (!digestEquals(digest, out.header.manifestSha256)) {
    return PackageError::ManifestHashMismatch;
  }
  sha256(out.chunkTableBytes, out.chunkTableLength, digest);
  if (!digestEquals(digest, out.header.chunkTableSha256)) {
    return PackageError::ChunkTableHashMismatch;
  }

  PackageError error = parseManifest(out.manifestBytes, out.manifestLength,
                                     out.manifest);
  if (error != PackageError::Ok) return error;
  return validateHeaderManifestContract(out.header, out.manifest);
}

PackageError verifyPackageBuffer(const uint8_t* data,
                                 size_t length,
                                 const PackageVerifyOptions& options,
                                 PackageView* out) {
  if (length > options.maxPackageSize) {
    return PackageError::SizeLimitExceeded;
  }

  PackageView localView;
  PackageError error = inspectPackageBuffer(data, length, localView);
  if (error != PackageError::Ok) {
    return error;
  }

  error = validateManifestPolicy(localView.manifest, options);
  if (error != PackageError::Ok) {
    return error;
  }

  if (!hexDigestEquals(localView.manifest.chunkTableSha256, localView.header.chunkTableSha256)) {
    return PackageError::ChunkTableHashMismatch;
  }

  error = verifySignature(localView, options);
  if (error != PackageError::Ok) {
    return error;
  }

  error = verifyChunks(localView);
  if (error != PackageError::Ok) {
    return error;
  }

  if (out != nullptr) {
    *out = localView;
  }
  return PackageError::Ok;
}

}  // namespace stm32_update
