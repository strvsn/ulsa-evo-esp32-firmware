#ifndef ULSA_ESP32_OTA_IMAGE_VALIDATOR_H
#define ULSA_ESP32_OTA_IMAGE_VALIDATOR_H

#include <stddef.h>
#include <stdint.h>

#include "system/firmware_identity_descriptor.h"

class Esp32OtaImageValidator {
public:
  Esp32OtaImageValidator();
  void begin(const char* version, uint32_t revision, const char* commit);
  void feed(const uint8_t* data, size_t length);
  bool finish();
  const char* error() const { return _error; }

private:
  void resetSearch();
  void acceptCandidate();
  bool validateCandidate() const;

  char _expectedVersion[12];
  uint32_t _expectedRevision;
  char _expectedCommit[41];
  uint8_t _candidate[sizeof(Esp32FirmwareIdentityDescriptor)];
  size_t _magicMatched;
  size_t _candidateLength;
  uint8_t _validDescriptorCount;
  bool _capturing;
  const char* _error;
};

#endif  // ULSA_ESP32_OTA_IMAGE_VALIDATOR_H
