/**
 * @file esp32_ota_image_validator.cpp
 * @brief Stream-validate the embedded identity of an Initial→Demo OTA image.
 */

#include "ota/esp32_ota_image_validator.h"

#include <string.h>

namespace {
volatile const uint8_t ENCODED_IDENTITY_MAGIC[8] = {
  (uint8_t)('U' ^ 0xa5), (uint8_t)('L' ^ 0xa5),
  (uint8_t)('S' ^ 0xa5), (uint8_t)('A' ^ 0xa5),
  (uint8_t)('E' ^ 0xa5), (uint8_t)('3' ^ 0xa5),
  (uint8_t)('2' ^ 0xa5), (uint8_t)('V' ^ 0xa5),
};

uint8_t identityMagicByte(size_t index) {
  return ENCODED_IDENTITY_MAGIC[index] ^ 0xa5U;
}

bool isLowerHex(const char* value, size_t length) {
  if (value == nullptr || value[length] != '\0') return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}
}  // namespace

Esp32OtaImageValidator::Esp32OtaImageValidator()
  : _expectedRevision(0), _magicMatched(0), _candidateLength(0),
    _validDescriptorCount(0), _capturing(false), _error("not_started") {
  memset(_expectedVersion, 0, sizeof(_expectedVersion));
  memset(_expectedCommit, 0, sizeof(_expectedCommit));
  memset(_candidate, 0, sizeof(_candidate));
}

void Esp32OtaImageValidator::begin(const char* version, uint32_t revision,
                                   const char* commit) {
  memset(_expectedVersion, 0, sizeof(_expectedVersion));
  memset(_expectedCommit, 0, sizeof(_expectedCommit));
  if (version != nullptr) strncpy(_expectedVersion, version, sizeof(_expectedVersion) - 1);
  if (commit != nullptr) strncpy(_expectedCommit, commit, sizeof(_expectedCommit) - 1);
  _expectedRevision = revision;
  _validDescriptorCount = 0;
  _error = nullptr;
  resetSearch();
}

void Esp32OtaImageValidator::resetSearch() {
  _magicMatched = 0;
  _candidateLength = 0;
  _capturing = false;
  memset(_candidate, 0, sizeof(_candidate));
}

void Esp32OtaImageValidator::feed(const uint8_t* data, size_t length) {
  if (data == nullptr || _error != nullptr) return;
  for (size_t i = 0; i < length; ++i) {
    const uint8_t byte = data[i];
    if (_capturing) {
      _candidate[_candidateLength++] = byte;
      if (_candidateLength == sizeof(_candidate)) acceptCandidate();
      continue;
    }

    if (byte == identityMagicByte(_magicMatched)) {
      _candidate[_magicMatched] = byte;
      ++_magicMatched;
      if (_magicMatched == sizeof(ENCODED_IDENTITY_MAGIC)) {
        _candidateLength = sizeof(ENCODED_IDENTITY_MAGIC);
        _capturing = true;
      }
    } else {
      _magicMatched = byte == identityMagicByte(0) ? 1U : 0U;
      if (_magicMatched == 1U) _candidate[0] = byte;
    }
  }
}

void Esp32OtaImageValidator::acceptCandidate() {
  if (validateCandidate()) {
    ++_validDescriptorCount;
    if (_validDescriptorCount > 1U) _error = "multiple_firmware_identities";
  }
  resetSearch();
}

bool Esp32OtaImageValidator::validateCandidate() const {
  Esp32FirmwareIdentityDescriptor descriptor;
  memcpy(&descriptor, _candidate, sizeof(descriptor));
  bool magicValid = true;
  for (size_t i = 0; i < sizeof(descriptor.magic); ++i) {
    magicValid = magicValid &&
                 (uint8_t)descriptor.magic[i] == identityMagicByte(i);
  }
  return magicValid &&
         descriptor.schemaVersion == ULSA_EVO_ESP32_IDENTITY_DESCRIPTOR_SCHEMA &&
         descriptor.profileCode == ULSA_EVO_ESP32_PROFILE_DEMO &&
         descriptor.flags == 0U &&
         descriptor.sourceCommit[40] == '\0' &&
         descriptor.version[11] == '\0' &&
         descriptor.buildContractSha256[64] == '\0' &&
         isLowerHex(descriptor.sourceCommit, 40) &&
         isLowerHex(descriptor.buildContractSha256, 64) &&
         strcmp(descriptor.sourceCommit, _expectedCommit) == 0 &&
         strcmp(descriptor.version, _expectedVersion) == 0 &&
         descriptor.revision == _expectedRevision;
}

bool Esp32OtaImageValidator::finish() {
  if (_error != nullptr) return false;
  if (_validDescriptorCount != 1U) {
    _error = "demo_firmware_identity_missing_or_mismatched";
    return false;
  }
  return true;
}
