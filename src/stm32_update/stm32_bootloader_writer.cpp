/**
 * @file stm32_bootloader_writer.cpp
 * @brief STM32 ROM bootloader UART helper
 */

#include "stm32_update/stm32_bootloader_writer.h"

#include "hardware/stm32_bootloader.h"

#include <string.h>

namespace stm32_update {

BootloaderSyncResult Stm32BootloaderWriter::probeSync(STM32Bootloader* bootloader,
                                                      HardwareSerial* serial) {
  BootloaderSyncResult result = beginSession(bootloader, serial);
  endSession();
  return result;
}

BootloaderSyncResult Stm32BootloaderWriter::beginSession(STM32Bootloader* bootloader,
                                                         HardwareSerial* serial) {
  BootloaderSyncResult result;
  if (bootloader == nullptr || serial == nullptr) {
    result.error = BootloaderSyncError::InvalidArgument;
    return result;
  }

  _bootloader = bootloader;
  _serial = serial;
  _bootloader->resetToBootloader();
  clearReceiveBuffer(_serial);

  for (uint8_t attempt = 1; attempt <= MAX_SYNC_ATTEMPTS; ++attempt) {
    result.attempts = attempt;
    result.lastResponse = 0;

    const uint8_t sync = SYNC_BYTE;
    if (writeExact(&sync, 1) != BootloaderSyncError::Ok) {
      result.error = BootloaderSyncError::SerialWriteFailed;
      endSession();
      return result;
    }

    const BootloaderSyncError error = waitForSyncResponse(_serial, &result.lastResponse);
    if (error == BootloaderSyncError::Ok) {
      result.error = BootloaderSyncError::Ok;
      return result;
    }

    result.error = error;
    clearReceiveBuffer(_serial);
    if (attempt < MAX_SYNC_ATTEMPTS) {
      delay(SYNC_RETRY_DELAY_MS);
    }
  }

  endSession();
  return result;
}

void Stm32BootloaderWriter::endSession() {
  if (_bootloader != nullptr) {
    _bootloader->resetToNormal();
  }
  _bootloader = nullptr;
  _serial = nullptr;
}

BootloaderSyncError Stm32BootloaderWriter::getInfo(BootloaderInfo& info) {
  memset(&info, 0, sizeof(info));

  BootloaderSyncError error = sendCommand(CMD_GET);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }

  uint8_t lengthByte = 0;
  error = readExact(&lengthByte, 1, 1500);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  const size_t payloadLength = (size_t)lengthByte + 1;
  uint8_t payload[64];
  if (payloadLength == 0 || payloadLength > sizeof(payload)) {
    return BootloaderSyncError::UnexpectedResponse;
  }
  error = readExact(payload, payloadLength, 1500);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  error = expectAck(1500);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }

  info.bootloaderVersion = payload[0];
  info.supportedCommandCount = (uint8_t)((payloadLength - 1) > sizeof(info.supportedCommands)
    ? sizeof(info.supportedCommands)
    : (payloadLength - 1));
  memcpy(info.supportedCommands, payload + 1, info.supportedCommandCount);

  error = sendCommand(CMD_GET_ID);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  error = readExact(&lengthByte, 1, 1500);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  const size_t idLength = (size_t)lengthByte + 1;
  uint8_t idBytes[8];
  if (idLength < 2 || idLength > sizeof(idBytes)) {
    return BootloaderSyncError::UnexpectedResponse;
  }
  error = readExact(idBytes, idLength, 1500);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  error = expectAck(1500);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }

  info.chipId = ((uint16_t)idBytes[0] << 8) | idBytes[1];
  info.valid = true;
  return BootloaderSyncError::Ok;
}

BootloaderSyncError Stm32BootloaderWriter::massErase(const BootloaderInfo* info) {
  const bool canUseExtended = commandSupported(info, CMD_EXTENDED_ERASE);
  const bool canUseStandard = commandSupported(info, CMD_ERASE);
  BootloaderSyncError extendedError = BootloaderSyncError::UnsupportedErase;

  if (canUseExtended) {
    extendedError = extendedMassErase();
    if (extendedError == BootloaderSyncError::Ok) {
      return BootloaderSyncError::Ok;
    }
    if (extendedError != BootloaderSyncError::CommandNack &&
        extendedError != BootloaderSyncError::DataNack) {
      return extendedError;
    }
  }

  if (canUseStandard) {
    return standardMassErase();
  }

  return extendedError == BootloaderSyncError::UnsupportedErase
    ? BootloaderSyncError::UnsupportedErase
    : extendedError;
}

BootloaderSyncError Stm32BootloaderWriter::writeMemory(uint32_t address,
                                                       const uint8_t* data,
                                                       size_t length) {
  if (data == nullptr || length == 0 || length > MAX_BLOCK_SIZE) {
    return BootloaderSyncError::InvalidArgument;
  }

  BootloaderSyncError error = sendCommand(CMD_WRITE_MEMORY);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  error = sendAddress(address);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }

  uint8_t frame[MAX_BLOCK_SIZE + 2];
  frame[0] = (uint8_t)(length - 1);
  uint8_t checksum = frame[0];
  for (size_t i = 0; i < length; ++i) {
    frame[i + 1] = data[i];
    checksum ^= data[i];
  }
  frame[length + 1] = checksum;

  error = writeExact(frame, length + 2);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  error = expectAck(5000);
  return error == BootloaderSyncError::CommandTimeout
    ? BootloaderSyncError::DataTimeout
    : error == BootloaderSyncError::CommandNack
      ? BootloaderSyncError::DataNack
      : error;
}

BootloaderSyncError Stm32BootloaderWriter::readMemory(uint32_t address,
                                                      uint8_t* data,
                                                      size_t length) {
  if (data == nullptr || length == 0 || length > MAX_BLOCK_SIZE) {
    return BootloaderSyncError::InvalidArgument;
  }

  BootloaderSyncError error = sendCommand(CMD_READ_MEMORY);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  error = sendAddress(address);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }

  const uint8_t n = (uint8_t)(length - 1);
  const uint8_t frame[2] = { n, (uint8_t)(n ^ 0xFF) };
  error = writeExact(frame, sizeof(frame));
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  error = expectAck(2000);
  if (error != BootloaderSyncError::Ok) {
    return error == BootloaderSyncError::CommandTimeout
      ? BootloaderSyncError::DataTimeout
      : error == BootloaderSyncError::CommandNack
        ? BootloaderSyncError::DataNack
        : error;
  }

  return readExact(data, length, 5000);
}

BootloaderSyncError Stm32BootloaderWriter::go(uint32_t address) {
  BootloaderSyncError error = sendCommand(CMD_GO);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  return sendAddress(address);
}

const char* Stm32BootloaderWriter::errorToString(BootloaderSyncError error) {
  switch (error) {
    case BootloaderSyncError::Ok: return "ok";
    case BootloaderSyncError::InvalidArgument: return "invalid_argument";
    case BootloaderSyncError::SyncTimeout: return "sync_timeout";
    case BootloaderSyncError::SyncNack: return "sync_nack";
    case BootloaderSyncError::UnexpectedResponse: return "unexpected_response";
    case BootloaderSyncError::CommandTimeout: return "command_timeout";
    case BootloaderSyncError::CommandNack: return "command_nack";
    case BootloaderSyncError::AddressTimeout: return "address_timeout";
    case BootloaderSyncError::AddressNack: return "address_nack";
    case BootloaderSyncError::DataTimeout: return "data_timeout";
    case BootloaderSyncError::DataNack: return "data_nack";
    case BootloaderSyncError::ReadTimeout: return "read_timeout";
    case BootloaderSyncError::SerialWriteFailed: return "serial_write_failed";
    case BootloaderSyncError::UnsupportedErase: return "unsupported_erase";
    default: return "unknown";
  }
}

void Stm32BootloaderWriter::clearReceiveBuffer(HardwareSerial* serial) {
  if (serial == nullptr) {
    return;
  }
  while (serial->available()) {
    serial->read();
  }
}

BootloaderSyncError Stm32BootloaderWriter::waitForSyncResponse(
  HardwareSerial* serial,
  uint8_t* lastResponse) {
  const uint32_t startedAt = millis();
  bool sawUnexpected = false;

  while (millis() - startedAt < SYNC_TIMEOUT_MS) {
    while (serial->available()) {
      const int value = serial->read();
      if (value < 0) {
        continue;
      }

      const uint8_t byte = (uint8_t)value;
      if (lastResponse != nullptr) {
        *lastResponse = byte;
      }
      if (byte == ACK_BYTE) {
        return BootloaderSyncError::Ok;
      }
      if (byte == NACK_BYTE) {
        return BootloaderSyncError::SyncNack;
      }
      sawUnexpected = true;
    }
    delay(5);
  }

  return sawUnexpected ? BootloaderSyncError::UnexpectedResponse
                       : BootloaderSyncError::SyncTimeout;
}

bool Stm32BootloaderWriter::commandSupported(const BootloaderInfo* info,
                                             uint8_t command) const {
  if (info == nullptr || !info->valid) {
    return true;
  }
  for (uint8_t i = 0; i < info->supportedCommandCount; ++i) {
    if (info->supportedCommands[i] == command) {
      return true;
    }
  }
  return false;
}

BootloaderSyncError Stm32BootloaderWriter::sendCommand(uint8_t command) {
  const uint8_t frame[2] = { command, (uint8_t)(command ^ 0xFF) };
  BootloaderSyncError error = writeExact(frame, sizeof(frame));
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  return expectAck(2000);
}

BootloaderSyncError Stm32BootloaderWriter::sendAddress(uint32_t address) {
  uint8_t frame[5];
  frame[0] = (uint8_t)((address >> 24) & 0xFF);
  frame[1] = (uint8_t)((address >> 16) & 0xFF);
  frame[2] = (uint8_t)((address >> 8) & 0xFF);
  frame[3] = (uint8_t)(address & 0xFF);
  frame[4] = frame[0] ^ frame[1] ^ frame[2] ^ frame[3];

  BootloaderSyncError error = writeExact(frame, sizeof(frame));
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  error = expectAck(2000);
  return error == BootloaderSyncError::CommandTimeout
    ? BootloaderSyncError::AddressTimeout
    : error == BootloaderSyncError::CommandNack
      ? BootloaderSyncError::AddressNack
      : error;
}

BootloaderSyncError Stm32BootloaderWriter::expectAck(uint32_t timeoutMs,
                                                     uint8_t* lastResponse) {
  if (_serial == nullptr) {
    return BootloaderSyncError::InvalidArgument;
  }
  const uint32_t startedAt = millis();
  while (millis() - startedAt < timeoutMs) {
    while (_serial->available()) {
      const int value = _serial->read();
      if (value < 0) {
        continue;
      }
      const uint8_t byte = (uint8_t)value;
      if (lastResponse != nullptr) {
        *lastResponse = byte;
      }
      if (byte == ACK_BYTE) {
        return BootloaderSyncError::Ok;
      }
      if (byte == NACK_BYTE) {
        return BootloaderSyncError::CommandNack;
      }
      return BootloaderSyncError::UnexpectedResponse;
    }
    delay(2);
  }
  return BootloaderSyncError::CommandTimeout;
}

BootloaderSyncError Stm32BootloaderWriter::readExact(uint8_t* data,
                                                     size_t length,
                                                     uint32_t timeoutMs) {
  if (_serial == nullptr || data == nullptr) {
    return BootloaderSyncError::InvalidArgument;
  }
  size_t offset = 0;
  const uint32_t startedAt = millis();
  while (offset < length && millis() - startedAt < timeoutMs) {
    while (offset < length && _serial->available()) {
      const int value = _serial->read();
      if (value >= 0) {
        data[offset++] = (uint8_t)value;
      }
    }
    if (offset < length) {
      delay(2);
    }
  }
  return offset == length ? BootloaderSyncError::Ok : BootloaderSyncError::ReadTimeout;
}

BootloaderSyncError Stm32BootloaderWriter::writeExact(const uint8_t* data,
                                                      size_t length) {
  if (_serial == nullptr || data == nullptr) {
    return BootloaderSyncError::InvalidArgument;
  }
  const size_t written = _serial->write(data, length);
  _serial->flush();
  return written == length ? BootloaderSyncError::Ok
                           : BootloaderSyncError::SerialWriteFailed;
}

BootloaderSyncError Stm32BootloaderWriter::extendedMassErase() {
  BootloaderSyncError error = sendCommand(CMD_EXTENDED_ERASE);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }

  const uint8_t frame[3] = { 0xFF, 0xFF, 0x00 };
  error = writeExact(frame, sizeof(frame));
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  error = expectAck(60000);
  return error == BootloaderSyncError::CommandTimeout
    ? BootloaderSyncError::DataTimeout
    : error == BootloaderSyncError::CommandNack
      ? BootloaderSyncError::DataNack
      : error;
}

BootloaderSyncError Stm32BootloaderWriter::standardMassErase() {
  BootloaderSyncError error = sendCommand(CMD_ERASE);
  if (error != BootloaderSyncError::Ok) {
    return error;
  }

  const uint8_t frame[2] = { 0xFF, 0x00 };
  error = writeExact(frame, sizeof(frame));
  if (error != BootloaderSyncError::Ok) {
    return error;
  }
  error = expectAck(60000);
  return error == BootloaderSyncError::CommandTimeout
    ? BootloaderSyncError::DataTimeout
    : error == BootloaderSyncError::CommandNack
      ? BootloaderSyncError::DataNack
      : error;
}

}  // namespace stm32_update
