#include <assert.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>

#include "ota/esp32_ota_image_validator.h"

static Esp32FirmwareIdentityDescriptor descriptor(uint8_t profile = ULSA_EVO_ESP32_PROFILE_DEMO,
                                                  uint16_t flags = 0) {
  Esp32FirmwareIdentityDescriptor value{};
  memcpy(value.magic, "ULSAE32V", 8);
  value.schemaVersion = ULSA_EVO_ESP32_IDENTITY_DESCRIPTOR_SCHEMA;
  value.profileCode = profile;
  value.flags = flags;
  value.versionCode = 0x7e010000;
  value.revision = 123;
  strcpy(value.sourceCommit, "0123456789abcdef0123456789abcdef01234567");
  strcpy(value.version, "1.0.0");
  strcpy(value.buildContractSha256,
         "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  return value;
}

static Esp32OtaImageValidator validator() {
  Esp32OtaImageValidator value;
  value.begin("1.0.0", 123,
              "0123456789abcdef0123456789abcdef01234567");
  return value;
}

static void acceptsOneDemoDescriptorAcrossChunks() {
  auto value = descriptor();
  auto subject = validator();
  const uint8_t prefix[] = {0xe9, 0x01, 0x02, 0x03};
  subject.feed(prefix, sizeof(prefix));
  subject.feed(reinterpret_cast<const uint8_t*>(&value), 5);
  subject.feed(reinterpret_cast<const uint8_t*>(&value) + 5, 17);
  subject.feed(reinterpret_cast<const uint8_t*>(&value) + 22,
               sizeof(value) - 22);
  assert(subject.finish());
}

static void rejectsWrongProfileDirtyAndMissingIdentity() {
  for (const auto& value : {
         descriptor(ULSA_EVO_ESP32_PROFILE_INITIAL),
         descriptor(ULSA_EVO_ESP32_PROFILE_DEMO,
                    ULSA_EVO_ESP32_IDENTITY_FLAG_DIRTY)}) {
    auto subject = validator();
    subject.feed(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
    assert(!subject.finish());
  }

  auto missing = validator();
  const uint8_t bytes[] = {0xe9, 0x00, 0x01};
  missing.feed(bytes, sizeof(bytes));
  assert(!missing.finish());
}

static void rejectsMismatchedAndDuplicateIdentity() {
  auto wrong = descriptor();
  wrong.revision = 124;
  auto mismatch = validator();
  mismatch.feed(reinterpret_cast<const uint8_t*>(&wrong), sizeof(wrong));
  assert(!mismatch.finish());

  auto good = descriptor();
  auto duplicate = validator();
  duplicate.feed(reinterpret_cast<const uint8_t*>(&good), sizeof(good));
  duplicate.feed(reinterpret_cast<const uint8_t*>(&good), sizeof(good));
  assert(!duplicate.finish());
  assert(strcmp(duplicate.error(), "multiple_firmware_identities") == 0);
}

int main() {
  acceptsOneDemoDescriptorAcrossChunks();
  rejectsWrongProfileDirtyAndMissingIdentity();
  rejectsMismatchedAndDuplicateIdentity();
  puts("esp32_ota_image_validator_test: passed");
  return 0;
}
