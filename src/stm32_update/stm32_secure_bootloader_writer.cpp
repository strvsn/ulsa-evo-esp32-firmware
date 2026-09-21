/**
 * @file stm32_secure_bootloader_writer.cpp
 * @brief Ciphertext transport for the ULSA STM32 custom bootloader
 */

#include "stm32_update/stm32_secure_bootloader_writer.h"

#include "hardware/stm32_bootloader.h"

#include <string.h>

namespace stm32_update {
namespace {

static const uint32_t FRAME_MAGIC = 0x42534c55UL;
static const uint8_t PROTOCOL_VERSION = 1;
static const uint8_t RESPONSE_BIT = 0x80;
static const uint8_t CMD_HELLO = 0x01;
static const uint8_t CMD_BEGIN = 0x10;
static const uint8_t CMD_CHUNK = 0x11;
static const uint8_t CMD_FINISH = 0x12;
static const size_t FRAME_HEADER_SIZE = 16;
static const size_t STATUS_SIZE = 32;

void writeLe16(uint8_t* out, uint16_t value) {
  out[0] = (uint8_t)value; out[1] = (uint8_t)(value >> 8);
}

void writeLe32(uint8_t* out, uint32_t value) {
  out[0] = (uint8_t)value; out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)(value >> 16); out[3] = (uint8_t)(value >> 24);
}

uint32_t readLe32(const uint8_t* data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  while (length-- > 0) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  return crc;
}

}  // namespace

SecureLoaderError Stm32SecureBootloaderWriter::beginSession(
    STM32Bootloader* bootloader, HardwareSerial* serial, SecureLoaderStatus& status) {
  if (bootloader == nullptr || serial == nullptr) return SecureLoaderError::InvalidArgument;
  _bootloader = bootloader;
  _serial = serial;
  _sequence = 1;
  _bootloader->resetToCustomBootloader();
  while (_serial->available()) _serial->read();
  SecureLoaderError last = SecureLoaderError::Timeout;
  for (uint8_t attempt = 0; attempt < 5; ++attempt) {
    last = command(CMD_HELLO, nullptr, 0, 1500, status);
    if (last == SecureLoaderError::Ok) return last;
    delay(50);
  }
  endSession(true);
  return last;
}

void Stm32SecureBootloaderWriter::endSession(bool resetTarget) {
  if (_bootloader != nullptr) {
    if (resetTarget) _bootloader->resetToNormal();
    else _bootloader->finishCustomBootloaderSession();
  }
  _bootloader = nullptr;
  _serial = nullptr;
}

SecureLoaderError Stm32SecureBootloaderWriter::beginUpdate(
    const uint8_t manifest[96], const uint8_t nonce[12], const uint8_t tag[16],
    SecureLoaderStatus& status) {
  uint8_t payload[124];
  memcpy(payload, manifest, 96);
  memcpy(payload + 96, nonce, 12);
  memcpy(payload + 108, tag, 16);
  return command(CMD_BEGIN, payload, sizeof(payload), 75000, status);
}

SecureLoaderError Stm32SecureBootloaderWriter::writeChunk(
    uint32_t index, uint32_t plainOffset, uint32_t plainSize,
    const uint8_t* frame, size_t frameSize, SecureLoaderStatus& status) {
  if (frame == nullptr || frameSize > 4096 + 28 || plainSize + 28 != frameSize) {
    return SecureLoaderError::InvalidArgument;
  }
  uint8_t metadata[16];
  writeLe32(metadata, index);
  writeLe32(metadata + 4, plainOffset);
  writeLe32(metadata + 8, plainSize);
  writeLe32(metadata + 12, (uint32_t)frameSize);
  return commandParts(
    CMD_CHUNK, metadata, sizeof(metadata), frame, frameSize, 15000, status);
}

SecureLoaderError Stm32SecureBootloaderWriter::finish(SecureLoaderStatus& status) {
  return command(CMD_FINISH, nullptr, 0, 30000, status);
}

SecureLoaderError Stm32SecureBootloaderWriter::command(
    uint8_t commandValue, const uint8_t* payload, size_t payloadLength,
    uint32_t timeoutMs, SecureLoaderStatus& status) {
  return commandParts(
    commandValue, nullptr, 0, payload, payloadLength, timeoutMs, status);
}

SecureLoaderError Stm32SecureBootloaderWriter::commandParts(
    uint8_t commandValue,
    const uint8_t* prefix,
    size_t prefixLength,
    const uint8_t* payload,
    size_t payloadLength,
    uint32_t timeoutMs,
    SecureLoaderStatus& status) {
  if (_serial == nullptr || prefixLength > 16 ||
      payloadLength > 4096 + 28 ||
      prefixLength + payloadLength > 16 + 4096 + 28 ||
      (prefixLength > 0 && prefix == nullptr) ||
      (payloadLength > 0 && payload == nullptr)) {
    return SecureLoaderError::InvalidArgument;
  }
  uint8_t header[FRAME_HEADER_SIZE] = {};
  writeLe32(header, FRAME_MAGIC);
  header[4] = PROTOCOL_VERSION;
  header[5] = commandValue;
  writeLe16(header + 6, 0);
  const uint32_t sequence = _sequence++;
  writeLe32(header + 8, sequence);
  const size_t combinedLength = prefixLength + payloadLength;
  writeLe32(header + 12, (uint32_t)combinedLength);
  uint32_t crc = crc32Update(0xFFFFFFFFUL, header, sizeof(header));
  crc = crc32Update(crc, prefix, prefixLength);
  crc = crc32Update(crc, payload, payloadLength) ^ 0xFFFFFFFFUL;
  uint8_t crcBytes[4];
  writeLe32(crcBytes, crc);
  if (!writeExact(header, sizeof(header)) ||
      (prefixLength > 0 && !writeExact(prefix, prefixLength)) ||
      (payloadLength > 0 && !writeExact(payload, payloadLength)) ||
      !writeExact(crcBytes, sizeof(crcBytes))) return SecureLoaderError::Transport;

  uint8_t responseHeader[FRAME_HEADER_SIZE];
  uint8_t responsePayload[STATUS_SIZE];
  uint8_t responseCrcBytes[4];
  if (readExact(responseHeader, sizeof(responseHeader), timeoutMs) != SecureLoaderError::Ok ||
      readLe32(responseHeader) != FRAME_MAGIC || responseHeader[4] != PROTOCOL_VERSION ||
      responseHeader[5] != (uint8_t)(commandValue | RESPONSE_BIT) ||
      readLe32(responseHeader + 8) != sequence ||
      readLe32(responseHeader + 12) != STATUS_SIZE) return SecureLoaderError::BadFrame;
  if (readExact(responsePayload, sizeof(responsePayload), timeoutMs) != SecureLoaderError::Ok ||
      readExact(responseCrcBytes, sizeof(responseCrcBytes), timeoutMs) != SecureLoaderError::Ok) {
    return SecureLoaderError::Timeout;
  }
  uint32_t responseCrc = crc32Update(0xFFFFFFFFUL, responseHeader, sizeof(responseHeader));
  responseCrc = crc32Update(responseCrc, responsePayload, sizeof(responsePayload)) ^ 0xFFFFFFFFUL;
  if (responseCrc != readLe32(responseCrcBytes)) return SecureLoaderError::BadCrc;
  status.error = readLe32(responsePayload);
  status.stage = readLe32(responsePayload + 4);
  status.nextChunk = readLe32(responsePayload + 8);
  status.writtenBytes = readLe32(responsePayload + 12);
  status.deviceId = readLe32(responsePayload + 16);
  status.versionCode = readLe32(responsePayload + 20);
  status.revision = readLe32(responsePayload + 24);
  status.applicationValid = readLe32(responsePayload + 28) != 0;
  return status.error == 0 ? SecureLoaderError::Ok : SecureLoaderError::LoaderRejected;
}

SecureLoaderError Stm32SecureBootloaderWriter::readExact(
    uint8_t* output, size_t length, uint32_t timeoutMs) {
  size_t offset = 0;
  const uint32_t started = millis();
  while (offset < length && millis() - started < timeoutMs) {
    while (offset < length && _serial->available()) {
      const int value = _serial->read();
      if (value >= 0) output[offset++] = (uint8_t)value;
    }
    if (offset < length) delay(1);
  }
  return offset == length ? SecureLoaderError::Ok : SecureLoaderError::Timeout;
}

bool Stm32SecureBootloaderWriter::writeExact(const uint8_t* data, size_t length) {
  if (_serial == nullptr || (length > 0 && data == nullptr)) return false;
  const size_t written = length == 0 ? 0 : _serial->write(data, length);
  _serial->flush();
  return written == length;
}

const char* Stm32SecureBootloaderWriter::errorToString(SecureLoaderError error) {
  switch (error) {
    case SecureLoaderError::Ok: return "ok";
    case SecureLoaderError::InvalidArgument: return "secure_invalid_argument";
    case SecureLoaderError::Timeout: return "secure_timeout";
    case SecureLoaderError::Transport: return "secure_transport";
    case SecureLoaderError::BadFrame: return "secure_bad_frame";
    case SecureLoaderError::BadCrc: return "secure_bad_crc";
    case SecureLoaderError::LoaderRejected: return "secure_loader_rejected";
    default: return "secure_unknown";
  }
}

}  // namespace stm32_update
