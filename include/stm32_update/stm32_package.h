/**
 * @file stm32_package.h
 * @brief STM32 update package parser and verifier
 */

#ifndef STM32_UPDATE_STM32_PACKAGE_H
#define STM32_UPDATE_STM32_PACKAGE_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace stm32_update {

static const size_t STM32_PACKAGE_HEADER_SIZE = 92;
static const size_t STM32_PACKAGE_CHUNK_TABLE_ENTRY_SIZE = 52;
static const size_t STM32_PACKAGE_SHA256_SIZE = 32;
static const size_t STM32_PACKAGE_ED25519_PUBLIC_KEY_SIZE = 32;
static const size_t STM32_PACKAGE_SIGNATURE_SIZE = 64;
static const size_t STM32_PACKAGE_MAX_CHUNK_SIZE = 4096;
static const size_t STM32_PACKAGE_MAX_PACKAGE_SIZE = 512UL * 1024UL;
static const size_t STM32_PACKAGE_MAX_FIRMWARE_SIZE = 0x6FE00UL;
static const size_t STM32_PACKAGE_AES_GCM_NONCE_SIZE = 12;
static const size_t STM32_PACKAGE_AES_GCM_TAG_SIZE = 16;
static const size_t STM32_PACKAGE_LOADER_MANIFEST_SIZE = 96;

enum class PackageError : uint8_t {
  Ok = 0,
  NullInput,
  TooSmall,
  BadMagic,
  UnsupportedHeaderVersion,
  LengthMismatch,
  LengthOverflow,
  ManifestHashMismatch,
  ChunkTableHashMismatch,
  ManifestParseFailed,
  MissingRequiredField,
  InvalidFieldValue,
  PolicyRejected,
  SizeLimitExceeded,
  PayloadHashMismatch,
  ChunkHashMismatch,
  ChunkTableInvalid,
  SignatureKeyMissing,
  SignatureDecodeFailed,
  SignatureInvalid,
  PartitionReadFailed,
  OutOfMemory,
};

struct PackageHeader {
  uint16_t headerVersion;
  uint16_t flags;
  uint32_t manifestLength;
  uint32_t chunkCount;
  uint32_t chunkTableLength;
  uint32_t packageLength;
  uint8_t manifestSha256[STM32_PACKAGE_SHA256_SIZE];
  uint8_t chunkTableSha256[STM32_PACKAGE_SHA256_SIZE];
};

struct PackageManifest {
  bool isV2;
  bool isV3;
  String format;
  String target;
  String version;
  String releaseTag;
  String buildProfile;
  String rdpPolicy;
  bool requiresAdmin;
  String minEsp32Fw;
  String baseAddress;
  String payloadEncoding;
  String chunkHashEncoding;
  bool packageKeyIdIsNull;
  String packageKeyId;
  uint32_t chunkSize;
  uint32_t chunkCount;
  String chunkTableSha256;
  String payloadSha256;
  String plainSha256;
  uint32_t size;
  uint32_t antiRollback;
  uint32_t versionCode;
  uint32_t revision;
  uint32_t minEsp32UpdateContract;
  String signatureAlgorithm;
  String signatureKeyId;
  String signature;
  uint32_t customBootloaderProtocol;
  uint8_t loaderManifest[STM32_PACKAGE_LOADER_MANIFEST_SIZE];
  uint8_t loaderManifestNonce[STM32_PACKAGE_AES_GCM_NONCE_SIZE];
  uint8_t loaderManifestTag[STM32_PACKAGE_AES_GCM_TAG_SIZE];
};

struct PackageView {
  PackageHeader header;
  PackageManifest manifest;
  const uint8_t* manifestBytes;
  const uint8_t* chunkTableBytes;
  const uint8_t* payloadBytes;
  size_t manifestLength;
  size_t chunkTableLength;
  size_t payloadLength;
};

struct PackageVerifyOptions {
  const char* expectedTarget = nullptr;
  const uint8_t* signaturePublicKey = nullptr;
  size_t signaturePublicKeyLength = 0;
  const char* expectedSignatureKeyId = nullptr;
  bool allowRdp1 = false;
  size_t maxPackageSize = STM32_PACKAGE_MAX_PACKAGE_SIZE;
  size_t maxFirmwareSize = STM32_PACKAGE_MAX_FIRMWARE_SIZE;
};

const char* packageErrorToString(PackageError error);

PackageError inspectPackageBuffer(const uint8_t* data, size_t length, PackageView& out);

PackageError verifyPackageBuffer(const uint8_t* data,
                                 size_t length,
                                 const PackageVerifyOptions& options,
                                 PackageView* out = nullptr);

}  // namespace stm32_update

#endif  // STM32_UPDATE_STM32_PACKAGE_H
