/**
 * @file stm32_package_internal.cpp
 * @brief Internal helpers for STM32 update package verification
 */

#include "stm32_package_internal.h"
#include "stm32_update/stm32_version_identity.h"
#include "stm32_update/stm32_target_identity.h"

#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <sodium.h>
#include <ctype.h>

namespace stm32_update {
namespace package_internal {
namespace {

const char PACKAGE_FORMAT_V2[] = "ulsa-stm32-fw-package-v2";
const char PACKAGE_FORMAT_V3[] = "ulsa-stm32-fw-package-v3";
const char ROM_BASE_ADDRESS[] = "0x08000000";
const char CUSTOM_BASE_ADDRESS[] = "0x08010000";

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return 0xFF;
}

bool isHexSha256(const String& value) {
  if (value.length() != STM32_PACKAGE_SHA256_SIZE * 2) {
    return false;
  }
  for (size_t i = 0; i < value.length(); ++i) {
    if (hexNibble(value[i]) > 0x0F) {
      return false;
    }
  }
  return true;
}

bool findJsonValue(const String& json, const char* key, int& valueStart, int& valueEnd) {
  const String pattern = String("\"") + key + "\":";
  const int keyPos = json.indexOf(pattern);
  if (keyPos < 0) {
    return false;
  }

  int pos = keyPos + pattern.length();
  while (pos < (int)json.length() && isspace((unsigned char)json[pos])) {
    ++pos;
  }
  if (pos >= (int)json.length()) {
    return false;
  }

  valueStart = pos;
  if (json[pos] == '"') {
    ++pos;
    bool escaped = false;
    while (pos < (int)json.length()) {
      const char c = json[pos];
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        valueEnd = pos + 1;
        return true;
      }
      ++pos;
    }
    return false;
  }

  while (pos < (int)json.length() && json[pos] != ',' && json[pos] != '}') {
    ++pos;
  }
  valueEnd = pos;
  return valueEnd > valueStart;
}

bool parseJsonString(const String& json, const char* key, String& out) {
  int start = 0;
  int end = 0;
  if (!findJsonValue(json, key, start, end) || json[start] != '"' || end <= start + 1) {
    return false;
  }

  out = "";
  out.reserve(end - start - 2);
  bool escaped = false;
  for (int i = start + 1; i < end - 1; ++i) {
    const char c = json[i];
    if (escaped) {
      switch (c) {
        case '"':
        case '\\':
        case '/':
          out += c;
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        default:
          return false;
      }
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else {
      out += c;
    }
  }
  return !escaped;
}

bool parseJsonNullOrString(const String& json, const char* key, bool& isNull, String& out) {
  int start = 0;
  int end = 0;
  if (!findJsonValue(json, key, start, end)) {
    return false;
  }
  String raw = json.substring(start, end);
  raw.trim();
  if (raw == "null") {
    isNull = true;
    out = "";
    return true;
  }
  isNull = false;
  return parseJsonString(json, key, out);
}

bool parseJsonBool(const String& json, const char* key, bool& out) {
  int start = 0;
  int end = 0;
  if (!findJsonValue(json, key, start, end)) {
    return false;
  }
  String raw = json.substring(start, end);
  raw.trim();
  if (raw == "true") {
    out = true;
    return true;
  }
  if (raw == "false") {
    out = false;
    return true;
  }
  return false;
}

bool parseJsonUint32(const String& json, const char* key, uint32_t& out) {
  int start = 0;
  int end = 0;
  if (!findJsonValue(json, key, start, end)) {
    return false;
  }
  String raw = json.substring(start, end);
  raw.trim();
  if (raw.length() == 0) {
    return false;
  }

  uint64_t value = 0;
  for (size_t i = 0; i < raw.length(); ++i) {
    const char c = raw[i];
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + (uint32_t)(c - '0');
    if (value > 0xFFFFFFFFULL) {
      return false;
    }
  }
  out = (uint32_t)value;
  return true;
}

bool decodeBase64Exact(const String& encoded, uint8_t* output, size_t expectedLength) {
  size_t decodedLength = 0;
  return mbedtls_base64_decode(
           output, expectedLength, &decodedLength,
           (const uint8_t*)encoded.c_str(), encoded.length()) == 0 &&
         decodedLength == expectedLength;
}

}  // namespace

const char PACKAGE_MAGIC[9] = "ULSASTM1";
const uint16_t PACKAGE_HEADER_VERSION_V2 = 2;
const uint16_t PACKAGE_HEADER_VERSION_V3 = 3;

bool isSupportedHeaderVersion(uint16_t version) {
  return version == PACKAGE_HEADER_VERSION_V2 || version == PACKAGE_HEADER_VERSION_V3;
}

PackageError validateHeaderManifestContract(const PackageHeader& header,
                                            const PackageManifest& manifest) {
  if (header.headerVersion == PACKAGE_HEADER_VERSION_V2) {
    return manifest.isV2 && manifest.format == PACKAGE_FORMAT_V2
      ? PackageError::Ok
      : PackageError::InvalidFieldValue;
  }
  if (header.headerVersion == PACKAGE_HEADER_VERSION_V3) {
    return manifest.isV3 && manifest.format == PACKAGE_FORMAT_V3
      ? PackageError::Ok
      : PackageError::InvalidFieldValue;
  }
  return PackageError::UnsupportedHeaderVersion;
}

uint16_t readLe16(const uint8_t* data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t readLe32(const uint8_t* data) {
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) |
         ((uint32_t)data[3] << 24);
}

void sha256(const uint8_t* data, size_t length, uint8_t out[STM32_PACKAGE_SHA256_SIZE]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  mbedtls_sha256_update_ret(&ctx, data, length);
  mbedtls_sha256_finish_ret(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

bool digestEquals(const uint8_t* a, const uint8_t* b) {
  uint8_t diff = 0;
  for (size_t i = 0; i < STM32_PACKAGE_SHA256_SIZE; ++i) {
    diff |= (uint8_t)(a[i] ^ b[i]);
  }
  return diff == 0;
}

bool hexDigestEquals(const String& hex, const uint8_t digest[STM32_PACKAGE_SHA256_SIZE]) {
  if (hex.length() != STM32_PACKAGE_SHA256_SIZE * 2) {
    return false;
  }

  uint8_t diff = 0;
  for (size_t i = 0; i < STM32_PACKAGE_SHA256_SIZE; ++i) {
    const char hi = hex[i * 2];
    const char lo = hex[i * 2 + 1];
    const uint8_t expected = (uint8_t)((hexNibble(hi) << 4) | hexNibble(lo));
    diff |= (uint8_t)(expected ^ digest[i]);
  }
  return diff == 0;
}

PackageError parseManifest(const uint8_t* bytes, size_t length, PackageManifest& manifest) {
  manifest.isV2 = false;
  manifest.isV3 = false;
  manifest.versionCode = 0;
  manifest.revision = 0;
  manifest.minEsp32UpdateContract = 0;
  manifest.customBootloaderProtocol = 0;
  manifest.chunkHashEncoding = "";
  String json;
  json.reserve(length + 1);
  for (size_t i = 0; i < length; ++i) {
    json += (char)bytes[i];
  }

  if (!parseJsonString(json, "format", manifest.format) ||
      !parseJsonString(json, "target", manifest.target) ||
      !parseJsonString(json, "version", manifest.version) ||
      !parseJsonString(json, "releaseTag", manifest.releaseTag) ||
      !parseJsonString(json, "buildProfile", manifest.buildProfile) ||
      !parseJsonString(json, "rdpPolicy", manifest.rdpPolicy) ||
      !parseJsonBool(json, "requiresAdmin", manifest.requiresAdmin) ||
      !parseJsonString(json, "minEsp32Fw", manifest.minEsp32Fw) ||
      !parseJsonString(json, "baseAddress", manifest.baseAddress) ||
      !parseJsonString(json, "payloadEncoding", manifest.payloadEncoding) ||
      !parseJsonNullOrString(json, "packageKeyId", manifest.packageKeyIdIsNull, manifest.packageKeyId) ||
      !parseJsonUint32(json, "chunkSize", manifest.chunkSize) ||
      !parseJsonUint32(json, "chunkCount", manifest.chunkCount) ||
      !parseJsonString(json, "chunkTableSha256", manifest.chunkTableSha256) ||
      !parseJsonString(json, "payloadSha256", manifest.payloadSha256) ||
      !parseJsonString(json, "plainSha256", manifest.plainSha256) ||
      !parseJsonUint32(json, "size", manifest.size) ||
      !parseJsonUint32(json, "antiRollback", manifest.antiRollback) ||
      !parseJsonString(json, "signatureAlgorithm", manifest.signatureAlgorithm) ||
      !parseJsonString(json, "signatureKeyId", manifest.signatureKeyId) ||
      !parseJsonString(json, "signature", manifest.signature)) {
    return PackageError::MissingRequiredField;
  }

  if (manifest.format == PACKAGE_FORMAT_V2 || manifest.format == PACKAGE_FORMAT_V3) {
    manifest.isV2 = manifest.format == PACKAGE_FORMAT_V2;
    manifest.isV3 = manifest.format == PACKAGE_FORMAT_V3;
    if (!parseJsonUint32(json, "versionCode", manifest.versionCode) ||
        !parseJsonUint32(json, "revision", manifest.revision) ||
        !parseJsonUint32(json, "minEsp32UpdateContract",
                         manifest.minEsp32UpdateContract)) {
      return PackageError::MissingRequiredField;
    }
    if (manifest.isV3) {
      String loaderManifest;
      String loaderManifestNonce;
      String loaderManifestTag;
      const int chunkHashField = json.indexOf("\"chunkHashEncoding\"");
      if (chunkHashField >= 0 &&
          !parseJsonString(json, "chunkHashEncoding", manifest.chunkHashEncoding)) {
        return PackageError::InvalidFieldValue;
      }
      if (!parseJsonUint32(json, "customBootloaderProtocol",
                           manifest.customBootloaderProtocol) ||
          !parseJsonString(json, "loaderManifest", loaderManifest) ||
          !parseJsonString(json, "loaderManifestNonce", loaderManifestNonce) ||
          !parseJsonString(json, "loaderManifestTag", loaderManifestTag) ||
          !decodeBase64Exact(loaderManifest, manifest.loaderManifest,
                             sizeof(manifest.loaderManifest)) ||
          !decodeBase64Exact(loaderManifestNonce, manifest.loaderManifestNonce,
                             sizeof(manifest.loaderManifestNonce)) ||
          !decodeBase64Exact(loaderManifestTag, manifest.loaderManifestTag,
                             sizeof(manifest.loaderManifestTag))) {
        return PackageError::InvalidFieldValue;
      }
    }
  } else {
    return PackageError::InvalidFieldValue;
  }

  return PackageError::Ok;
}

PackageError validateManifestPolicy(const PackageManifest& manifest,
                                    const PackageVerifyOptions& options) {
  uint16_t expectedChipId = 0;
  if (!expectedChipIdForStm32Target(
          manifest.target.c_str(), expectedChipId)) {
    return PackageError::InvalidFieldValue;
  }
  const bool encrypted = manifest.isV3;
  const char* expectedBase = encrypted ? CUSTOM_BASE_ADDRESS : ROM_BASE_ADDRESS;
  if (manifest.baseAddress != expectedBase ||
      manifest.chunkSize == 0 ||
      manifest.chunkSize > STM32_PACKAGE_MAX_CHUNK_SIZE ||
      manifest.chunkCount == 0 ||
      manifest.signatureAlgorithm != "Ed25519" ||
      !isHexSha256(manifest.chunkTableSha256) ||
      !isHexSha256(manifest.payloadSha256) ||
      !isHexSha256(manifest.plainSha256)) {
    return PackageError::InvalidFieldValue;
  }
  if (encrypted && manifest.chunkHashEncoding.length() > 0 &&
      manifest.chunkHashEncoding != "frame-sha256") {
    return PackageError::InvalidFieldValue;
  }

  Stm32SemanticVersion parsed = {};
  if ((!manifest.isV2 && !manifest.isV3) ||
      !parseCanonicalStm32Version(manifest.version.c_str(), parsed) ||
      packStm32VersionCode(parsed) != manifest.versionCode ||
      manifest.revision < STM32_MIN_REVISION ||
      manifest.antiRollback != manifest.revision ||
      manifest.minEsp32UpdateContract < (encrypted ? 3u : 2u) ||
      manifest.minEsp32UpdateContract > STM32_UPDATE_CONTRACT_VERSION) {
    return PackageError::InvalidFieldValue;
  }

  if (options.expectedTarget != nullptr && manifest.target != options.expectedTarget) {
    return PackageError::InvalidFieldValue;
  }
  if (options.expectedSignatureKeyId != nullptr &&
      manifest.signatureKeyId != options.expectedSignatureKeyId) {
    return PackageError::InvalidFieldValue;
  }

  const bool validBuildProfile = manifest.buildProfile == "debug" ||
                                 manifest.buildProfile == "field" ||
                                 manifest.buildProfile == "production";
  const bool validRdpPolicy = manifest.rdpPolicy == "none" ||
                              manifest.rdpPolicy == "preserve" ||
                              manifest.rdpPolicy == "enable_rdp1";
  if (!validBuildProfile || !validRdpPolicy) {
    return PackageError::InvalidFieldValue;
  }

  if (manifest.buildProfile == "debug" && manifest.rdpPolicy == "enable_rdp1") {
    return PackageError::PolicyRejected;
  }
  if (manifest.rdpPolicy == "enable_rdp1") {
    if (manifest.buildProfile != "production" || !manifest.requiresAdmin || !options.allowRdp1) {
      return PackageError::PolicyRejected;
    }
  }

  if (encrypted) {
    if (manifest.payloadEncoding != "aes-256-gcm-chunked" ||
        manifest.packageKeyIdIsNull || manifest.packageKeyId.length() == 0 ||
        manifest.customBootloaderProtocol != 1u) {
      return PackageError::PolicyRejected;
    }
    static const uint8_t loaderMagic[8] = {'U','L','S','M','E','T','A','1'};
    if (memcmp(manifest.loaderManifest, loaderMagic, sizeof(loaderMagic)) != 0 ||
        readLe16(manifest.loaderManifest + 8) != 1u ||
        readLe16(manifest.loaderManifest + 10) != STM32_PACKAGE_LOADER_MANIFEST_SIZE ||
        readLe16(manifest.loaderManifest + 12) != expectedChipId ||
        readLe16(manifest.loaderManifest + 14) != 0u ||
        readLe32(manifest.loaderManifest + 16) != 0x08010000u ||
        readLe32(manifest.loaderManifest + 20) != manifest.size ||
        readLe32(manifest.loaderManifest + 24) != manifest.versionCode ||
        readLe32(manifest.loaderManifest + 28) != manifest.revision ||
        readLe32(manifest.loaderManifest + 32) != manifest.chunkSize ||
        readLe32(manifest.loaderManifest + 36) != manifest.chunkCount) {
      return PackageError::InvalidFieldValue;
    }
    uint8_t plainDigest[STM32_PACKAGE_SHA256_SIZE];
    uint8_t keyIdDigest[STM32_PACKAGE_SHA256_SIZE];
    bool packageIdNonZero = false;
    for (size_t i = 40; i < 56; ++i) packageIdNonZero |= manifest.loaderManifest[i] != 0;
    for (size_t i = 0; i < STM32_PACKAGE_SHA256_SIZE; ++i) {
      const int hi = hexNibble(manifest.plainSha256[i * 2]);
      const int lo = hexNibble(manifest.plainSha256[i * 2 + 1]);
      plainDigest[i] = (uint8_t)((hi << 4) | lo);
    }
    sha256((const uint8_t*)manifest.packageKeyId.c_str(), manifest.packageKeyId.length(), keyIdDigest);
    if (!packageIdNonZero ||
        memcmp(manifest.loaderManifest + 56, plainDigest, sizeof(plainDigest)) != 0 ||
        memcmp(manifest.loaderManifest + 88, keyIdDigest, 8) != 0) {
      return PackageError::InvalidFieldValue;
    }
  } else {
    if (manifest.payloadEncoding != "plain" || !manifest.packageKeyIdIsNull) {
      return PackageError::InvalidFieldValue;
    }
  }

  if (manifest.size > options.maxFirmwareSize) {
    return PackageError::SizeLimitExceeded;
  }
  return PackageError::Ok;
}

PackageError verifySignature(const PackageView& view, const PackageVerifyOptions& options) {
  if (options.signaturePublicKey == nullptr ||
      options.signaturePublicKeyLength != STM32_PACKAGE_ED25519_PUBLIC_KEY_SIZE) {
    return PackageError::SignatureKeyMissing;
  }

  uint8_t signature[STM32_PACKAGE_SIGNATURE_SIZE];
  size_t decodedLength = 0;
  const int base64Result = mbedtls_base64_decode(
    signature,
    sizeof(signature),
    &decodedLength,
    (const uint8_t*)view.manifest.signature.c_str(),
    view.manifest.signature.length());
  if (base64Result != 0 || decodedLength != STM32_PACKAGE_SIGNATURE_SIZE) {
    return PackageError::SignatureDecodeFailed;
  }

  String signedJson;
  signedJson.reserve(view.manifestLength);
  for (size_t i = 0; i < view.manifestLength; ++i) {
    signedJson += (char)view.manifestBytes[i];
  }

  int valueStart = 0;
  int valueEnd = 0;
  if (!findJsonValue(signedJson, "signature", valueStart, valueEnd) ||
      valueStart >= valueEnd ||
      signedJson[valueStart] != '"') {
    return PackageError::ManifestParseFailed;
  }

  const String signaturePayload = signedJson.substring(0, valueStart) +
                                  "null" +
                                  signedJson.substring(valueEnd);

  if (sodium_init() < 0) {
    return PackageError::SignatureInvalid;
  }

  const int ok = crypto_sign_ed25519_verify_detached(
    signature,
    (const unsigned char*)signaturePayload.c_str(),
    signaturePayload.length(),
    options.signaturePublicKey);

  return ok == 0 ? PackageError::Ok : PackageError::SignatureInvalid;
}

PackageError verifyChunks(const PackageView& view) {
  if (view.manifest.chunkCount != view.header.chunkCount) {
    return PackageError::ChunkTableInvalid;
  }
  if (view.chunkTableLength != view.manifest.chunkCount * STM32_PACKAGE_CHUNK_TABLE_ENTRY_SIZE) {
    return PackageError::ChunkTableInvalid;
  }

  uint8_t payloadDigest[STM32_PACKAGE_SHA256_SIZE];
  sha256(view.payloadBytes, view.payloadLength, payloadDigest);
  if (!hexDigestEquals(view.manifest.payloadSha256, payloadDigest)) {
    return PackageError::PayloadHashMismatch;
  }

  const bool encrypted = view.manifest.payloadEncoding == "aes-256-gcm-chunked";
  mbedtls_sha256_context plainCtx;
  mbedtls_sha256_init(&plainCtx);
  if (!encrypted) mbedtls_sha256_starts_ret(&plainCtx, 0);

  uint32_t expectedPlainOffset = 0;
  uint32_t expectedFrameOffset = 0;
  for (uint32_t index = 0; index < view.manifest.chunkCount; ++index) {
    const uint8_t* entry = view.chunkTableBytes + index * STM32_PACKAGE_CHUNK_TABLE_ENTRY_SIZE;
    const uint32_t chunkIndex = readLe32(entry);
    const uint32_t plainOffset = readLe32(entry + 4);
    const uint32_t plainSize = readLe32(entry + 8);
    const uint32_t frameOffset = readLe32(entry + 12);
    const uint32_t frameSize = readLe32(entry + 16);
    const uint8_t* expectedHash = entry + 20;

    if (chunkIndex != index ||
        plainOffset != expectedPlainOffset ||
        frameOffset != expectedFrameOffset ||
        frameOffset > view.payloadLength ||
        frameSize > view.payloadLength - frameOffset ||
        plainSize == 0 ||
        plainSize > STM32_PACKAGE_MAX_CHUNK_SIZE) {
      mbedtls_sha256_free(&plainCtx);
      return PackageError::ChunkTableInvalid;
    }
    const uint32_t expectedFrameSize = plainSize + (encrypted ? 28u : 0u);
    if (frameSize != expectedFrameSize) {
      mbedtls_sha256_free(&plainCtx);
      return PackageError::ChunkTableInvalid;
    }

    const uint8_t* frame = view.payloadBytes + frameOffset;
    if (encrypted && view.manifest.chunkHashEncoding == "frame-sha256") {
      uint8_t chunkDigest[STM32_PACKAGE_SHA256_SIZE];
      sha256(frame, frameSize, chunkDigest);
      if (!digestEquals(chunkDigest, expectedHash)) {
        mbedtls_sha256_free(&plainCtx);
        return PackageError::ChunkHashMismatch;
      }
    } else if (!encrypted) {
      uint8_t chunkDigest[STM32_PACKAGE_SHA256_SIZE];
      sha256(frame, plainSize, chunkDigest);
      if (!digestEquals(chunkDigest, expectedHash)) {
        mbedtls_sha256_free(&plainCtx);
        return PackageError::ChunkHashMismatch;
      }
      mbedtls_sha256_update_ret(&plainCtx, frame, plainSize);
    }
    expectedPlainOffset += plainSize;
    expectedFrameOffset += frameSize;
  }

  uint8_t plainDigest[STM32_PACKAGE_SHA256_SIZE] = {};
  if (!encrypted) mbedtls_sha256_finish_ret(&plainCtx, plainDigest);
  mbedtls_sha256_free(&plainCtx);

  if (expectedPlainOffset != view.manifest.size ||
      expectedFrameOffset != view.payloadLength) {
    return PackageError::ChunkTableInvalid;
  }
  if (!encrypted && !hexDigestEquals(view.manifest.plainSha256, plainDigest)) {
    return PackageError::PayloadHashMismatch;
  }

  return PackageError::Ok;
}

}  // namespace package_internal
}  // namespace stm32_update
