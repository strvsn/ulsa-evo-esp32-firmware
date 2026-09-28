/**
 * @file stm32_bootloader_writer.h
 * @brief STM32 ROM bootloader UART helper
 */

#ifndef STM32_UPDATE_STM32_BOOTLOADER_WRITER_H
#define STM32_UPDATE_STM32_BOOTLOADER_WRITER_H

#include <Arduino.h>
#include <stdint.h>

class STM32Bootloader;

namespace stm32_update {

enum class BootloaderSyncError : uint8_t {
  Ok = 0,
  InvalidArgument,
  SyncTimeout,
  SyncNack,
  UnexpectedResponse,
  CommandTimeout,
  CommandNack,
  AddressTimeout,
  AddressNack,
  DataTimeout,
  DataNack,
  ReadTimeout,
  SerialWriteFailed,
  UnsupportedErase,
};

struct BootloaderSyncResult {
  BootloaderSyncError error = BootloaderSyncError::InvalidArgument;
  uint8_t attempts = 0;
  uint8_t lastResponse = 0;
};

struct BootloaderInfo {
  bool valid = false;
  uint8_t bootloaderVersion = 0;
  uint16_t chipId = 0;
  uint8_t supportedCommandCount = 0;
  uint8_t supportedCommands[32] = {};
};

class Stm32BootloaderWriter {
public:
  BootloaderSyncResult probeSync(STM32Bootloader* bootloader, HardwareSerial* serial);
  BootloaderSyncResult beginSession(STM32Bootloader* bootloader, HardwareSerial* serial);
  void endSession();

  BootloaderSyncError getInfo(BootloaderInfo& info);
  BootloaderSyncError massErase(const BootloaderInfo* info = nullptr);
  BootloaderSyncError writeMemory(uint32_t address, const uint8_t* data, size_t length);
  BootloaderSyncError readMemory(uint32_t address, uint8_t* data, size_t length);
  BootloaderSyncError go(uint32_t address);

  static const char* errorToString(BootloaderSyncError error);

private:
  static const uint8_t SYNC_BYTE = 0x7F;
  static const uint8_t ACK_BYTE = 0x79;
  static const uint8_t NACK_BYTE = 0x1F;
  static const uint8_t CMD_GET = 0x00;
  static const uint8_t CMD_GET_ID = 0x02;
  static const uint8_t CMD_READ_MEMORY = 0x11;
  static const uint8_t CMD_GO = 0x21;
  static const uint8_t CMD_WRITE_MEMORY = 0x31;
  static const uint8_t CMD_ERASE = 0x43;
  static const uint8_t CMD_EXTENDED_ERASE = 0x44;
  static const uint8_t MAX_SYNC_ATTEMPTS = 5;
  static const size_t MAX_BLOCK_SIZE = 256;
  static const uint32_t SYNC_TIMEOUT_MS = 1200;
  static const uint32_t SYNC_RETRY_DELAY_MS = 250;

  STM32Bootloader* _bootloader = nullptr;
  HardwareSerial* _serial = nullptr;

  void clearReceiveBuffer(HardwareSerial* serial);
  BootloaderSyncError waitForSyncResponse(HardwareSerial* serial,
                                          uint8_t* lastResponse);
  bool commandSupported(const BootloaderInfo* info, uint8_t command) const;
  BootloaderSyncError sendCommand(uint8_t command);
  BootloaderSyncError sendAddress(uint32_t address);
  BootloaderSyncError expectAck(uint32_t timeoutMs, uint8_t* lastResponse = nullptr);
  BootloaderSyncError readExact(uint8_t* data, size_t length, uint32_t timeoutMs);
  BootloaderSyncError writeExact(const uint8_t* data, size_t length);
  BootloaderSyncError extendedMassErase();
  BootloaderSyncError standardMassErase();
};

}  // namespace stm32_update

#endif  // STM32_UPDATE_STM32_BOOTLOADER_WRITER_H
