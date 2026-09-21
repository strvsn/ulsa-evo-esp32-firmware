#ifndef STM32_UPDATE_STM32_SECURE_BOOTLOADER_WRITER_H
#define STM32_UPDATE_STM32_SECURE_BOOTLOADER_WRITER_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class STM32Bootloader;

namespace stm32_update {

static const size_t STM32_SECURE_LOADER_MANIFEST_SIZE = 96;
static const size_t STM32_SECURE_LOADER_NONCE_SIZE = 12;
static const size_t STM32_SECURE_LOADER_TAG_SIZE = 16;

enum class SecureLoaderError : uint8_t {
  Ok = 0,
  InvalidArgument,
  Timeout,
  Transport,
  BadFrame,
  BadCrc,
  LoaderRejected,
};

struct SecureLoaderStatus {
  uint32_t error = 0;
  uint32_t stage = 0;
  uint32_t nextChunk = 0;
  uint32_t writtenBytes = 0;
  uint32_t deviceId = 0;
  uint32_t versionCode = 0;
  uint32_t revision = 0;
  bool applicationValid = false;
};

class Stm32SecureBootloaderWriter {
public:
  SecureLoaderError beginSession(STM32Bootloader* bootloader, HardwareSerial* serial,
                                 SecureLoaderStatus& status);
  void endSession(bool resetTarget);
  SecureLoaderError beginUpdate(const uint8_t manifest[STM32_SECURE_LOADER_MANIFEST_SIZE],
                                const uint8_t nonce[STM32_SECURE_LOADER_NONCE_SIZE],
                                const uint8_t tag[STM32_SECURE_LOADER_TAG_SIZE],
                                SecureLoaderStatus& status);
  SecureLoaderError writeChunk(uint32_t index, uint32_t plainOffset,
                               uint32_t plainSize, const uint8_t* frame,
                               size_t frameSize, SecureLoaderStatus& status);
  SecureLoaderError finish(SecureLoaderStatus& status);
  static const char* errorToString(SecureLoaderError error);

private:
  STM32Bootloader* _bootloader = nullptr;
  HardwareSerial* _serial = nullptr;
  uint32_t _sequence = 1;

  SecureLoaderError command(uint8_t command, const uint8_t* payload,
                            size_t payloadLength, uint32_t timeoutMs,
                            SecureLoaderStatus& status);
  SecureLoaderError commandParts(uint8_t command,
                                 const uint8_t* prefix,
                                 size_t prefixLength,
                                 const uint8_t* payload,
                                 size_t payloadLength,
                                 uint32_t timeoutMs,
                                 SecureLoaderStatus& status);
  SecureLoaderError readExact(uint8_t* output, size_t length, uint32_t timeoutMs);
  bool writeExact(const uint8_t* data, size_t length);
};

}  // namespace stm32_update

#endif
