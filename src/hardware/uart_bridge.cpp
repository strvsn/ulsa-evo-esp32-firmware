/**
 * @file uart_bridge.cpp
 * @brief USB-CDC ⇔ UART1 ブリッジモジュール実装
 * @date 2025-12-03
 */

#include "uart_bridge.h"
// BOOTコマンド文字列
static const char BOOT_COMMAND[] = "BOOT";

// 通常ブリッジ用の処理上限。
// 片方向の連続ストリームで反対方向の入力を飢餓状態にしないため、
// 1回のprocess()で扱う量を区切り、PC入力を優先的に拾う。
static const size_t NORMAL_USB_TO_UART_BURST_LIMIT = 96;
static const size_t NORMAL_UART_TO_USB_BURST_LIMIT = 192;
static const size_t NORMAL_USB_INTERLEAVE_INTERVAL = 16;
static const size_t NORMAL_USB_INTERLEAVE_LIMIT = 16;
static const uint8_t STM32_BOOTLOADER_SYNC = 0x7F;
static const uint8_t STM32_BOOTLOADER_ACK = 0x79;
static const uint8_t STM32_BOOTLOADER_NACK = 0x1F;
static const uint8_t STM32_BOOTLOADER_GET_COMMAND = 0x00;
static const uint8_t STM32_BOOTLOADER_GET_VERSION_COMMAND = 0x01;
static const uint8_t STM32_BOOTLOADER_GET_ID_COMMAND = 0x02;
static const uint8_t STM32_BOOTLOADER_READ_COMMAND = 0x11;
static const uint8_t STM32_BOOTLOADER_GO_COMMAND = 0x21;
static const uint8_t STM32_BOOTLOADER_WRITE_COMMAND = 0x31;
static const uint8_t STM32_BOOTLOADER_ERASE_COMMAND = 0x43;
static const uint8_t STM32_BOOTLOADER_EXT_ERASE_COMMAND = 0x44;
static const uint8_t STM32_BOOTLOADER_SPECIAL_COMMAND = 0x50;
static const uint8_t STM32_BOOTLOADER_EXT_SPECIAL_COMMAND = 0x51;
static const uint8_t STM32_BOOTLOADER_WRITE_PROTECT_COMMAND = 0x63;
static const uint8_t STM32_BOOTLOADER_WRITE_UNPROTECT_COMMAND = 0x73;
static const uint8_t STM32_BOOTLOADER_READOUT_PROTECT_COMMAND = 0x82;
static const uint8_t STM32_BOOTLOADER_READOUT_UNPROTECT_COMMAND = 0x92;
static const uint8_t STM32_BOOTLOADER_GET_CHECKSUM_COMMAND = 0xA1;

UartBridge::UartBridge()
  : _usb(nullptr)
  , _uart(nullptr)
  , _hasActivity(false)
  , _bootloaderClearTimeout(0)
  , _bootloaderFirstByte(true)
  , _totalBytes(0)
  , _bootCommandReceived(false)
  , _enabled(false)
  , _bootloaderSnifferStats()
  , _bootloaderSnifferPhase(BOOTLOADER_SNIFFER_IDLE)
  , _bootloaderPendingCommand(0)
  , _bootloaderFrameIndex(0)
  , _bootloaderPayloadRemaining(0)
  , _bootloaderEraseLength(0)
  , _bootloaderGoComplete(false)
  // , _debugger(nullptr)  // D入力デバッグ機能を無効化
  , _bootCmdIndex(0)
  , _windBufferHead(0)
  , _windBufferTail(0) {
  memset(_bootCmdBuffer, 0, sizeof(_bootCmdBuffer));
  memset(_bootloaderFrame, 0, sizeof(_bootloaderFrame));
  memset(_windBuffer, 0, sizeof(_windBuffer));
}

void UartBridge::begin(Stream* usbSerial, HardwareSerial* uartSerial) {
  _usb = usbSerial;
  _uart = uartSerial;
  _hasActivity = false;
  _totalBytes = 0;
  _bootCommandReceived = false;
  _bootCmdIndex = 0;
  _windBufferHead = 0;
  _windBufferTail = 0;
  _enabled = false;  // デフォルトで無効（オフ）
  resetBootloaderSniffer();
  memset(_bootCmdBuffer, 0, sizeof(_bootCmdBuffer));
  memset(_windBuffer, 0, sizeof(_windBuffer));
}

void UartBridge::resetBootloaderState() {
  _bootloaderClearTimeout = millis() + 100;
  _bootloaderFirstByte = true;  // 初回0x00フィルタリングを有効化
  resetBootloaderSniffer();
}

void UartBridge::process() {
  if (_usb == nullptr || _uart == nullptr) return;
  
  auto transferUsbToUart = [this](size_t limit) {
    if (!_enabled) return;

    size_t processed = 0;
    while (_usb->available() > 0 && processed < limit) {
      if (_uart->availableForWrite() <= 0) {
        break;
      }

      int data = _usb->read();
      if (data < 0) {
        break;
      }

      if (_uart->write((uint8_t)data) == 0) {
        break;
      }

      _totalBytes++;
      _hasActivity = true;
      processed++;
    }
  };

  // PCからのキー入力を先に処理し、風速UARTの連続出力で入力が詰まらないようにする。
  transferUsbToUart(NORMAL_USB_TO_UART_BURST_LIMIT);

  // UART → USB（STM32 → PC）
  // WindSensor用バッファには常に保存しつつ、通常ブリッジ有効時だけUSBへ出す。
  // 連続受信中も一定量ごとにUSB→UARTを挟み、手入力コマンドの取りこぼしを防ぐ。
  size_t uartProcessed = 0;
  while (_uart->available() > 0 && uartProcessed < NORMAL_UART_TO_USB_BURST_LIMIT) {
    int data = _uart->read();
    if (data < 0) {
      break;
    }

    uint8_t byte = (uint8_t)data;

    // ブリッジが有効の場合のみUSB-CDCに転送
    if (_enabled && _usb->availableForWrite() > 0) {
      _usb->write(byte);
      _totalBytes++;
    }

    // WindSensor用バッファには常に保存（ブリッジ無効でも）
    size_t nextHead = (_windBufferHead + 1) % WIND_BUFFER_SIZE;
    if (nextHead != _windBufferTail) {  // バッファフルでなければ
      _windBuffer[_windBufferHead] = byte;
      _windBufferHead = nextHead;
    }
    // バッファフルの場合は古いデータを上書き（オーバーフロー対策）

    _hasActivity = true;
    uartProcessed++;

    if (_enabled && (uartProcessed % NORMAL_USB_INTERLEAVE_INTERVAL) == 0) {
      transferUsbToUart(NORMAL_USB_INTERLEAVE_LIMIT);
    }
  }

  // UART側処理の直後にもPC入力を拾い、次のloopまで待たせない。
  transferUsbToUart(NORMAL_USB_TO_UART_BURST_LIMIT);
}

void UartBridge::processBootloaderMode() {
  if (_usb == nullptr || _uart == nullptr) return;
  
  // ブートローダーモード移行後100msは、バッファクリアのみ実行
  // resetToBootloader()完了後の短時間に蓄積したゴミデータを除去
  // 100msで十分（STM32のBOOT0/RESET切替完了に必要な時間）
  if (millis() < _bootloaderClearTimeout) {
    // 高速クリア（delayなし）
    while (_uart->available()) _uart->read();
    while (_usb->available()) _usb->read();
    return;  // 100ms経過するまではデータ転送しない
  }
  
  // UART → USB（STM32 → PC）優先
  if (_uart->available()) {
    uint8_t data = _uart->read();
    
    // 初回0x00フィルタリング: ブートローダーモード開始直後の1回のみ
    // STM32リセット直後の不安定な状態で発生する0x00を除去
    if (_bootloaderFirstByte && data == 0x00) {
      _bootloaderFirstByte = false;  // 次回からフィルタリング無効
      // デバッグログには記録（問題追跡のため） - D入力デバッグ機能を無効化
      // if (_debugger != nullptr) {
      //   _debugger->logData(DIR_UART_TO_USB, data);
      // }
      return;  // 0x00をPCに送信せずに破棄
    }
    _bootloaderFirstByte = false;  // 初回データ通過後はフィルタ無効
    
    _usb->write(data);
    observeBootloaderUartToUsb(data);
    
    // デバッグログ記録 - D入力デバッグ機能を無効化
    // if (_debugger != nullptr) {
    //   _debugger->logData(DIR_UART_TO_USB, data);
    // }
    
    _hasActivity = true;
    return;
  }
  
  // USB → UART（PC → STM32）
  if (_usb->available()) {
    uint8_t data = _usb->read();
    _uart->write(data);
    observeBootloaderUsbToUart(data);
    
    // デバッグログ記録 - D入力デバッグ機能を無効化
    // if (_debugger != nullptr) {
    //   _debugger->logData(DIR_USB_TO_UART, data);
    // }
    
    _hasActivity = true;
    return;
  }
}

bool UartBridge::consumeBootloaderGoComplete() {
  bool complete = _bootloaderGoComplete;
  if (complete) {
    _bootloaderGoComplete = false;
  }
  return complete;
}

const UartBridge::BootloaderSnifferStats& UartBridge::getBootloaderSnifferStats() const {
  return _bootloaderSnifferStats;
}

bool UartBridge::isBootloaderCubeProgrammerCompleteCandidate() const {
  const BootloaderSnifferStats& stats = _bootloaderSnifferStats;

  if (_bootloaderSnifferPhase != BOOTLOADER_SNIFFER_IDLE) return false;
  if (stats.commandCount == 0 || stats.commandCount != stats.commandAckCount) return false;

  // Conservative CubeProgrammer pattern observed in practice:
  // erase + write + read/verify all ACKed, followed by GET_ID, without GO.
  if (stats.eraseCommandCount == 0 || stats.eraseCommandCount != stats.eraseFinalAckCount) return false;
  if (stats.writeCommandCount == 0 || stats.writeCommandCount != stats.writeFinalAckCount) return false;
  if (stats.readCommandCount == 0 || stats.readCommandCount != stats.readFinalAckCount) return false;
  if (stats.getChecksumCommandCount != stats.getChecksumFinalAckCount) return false;
  if (stats.getIdCommandCount == 0 || stats.lastCommand != STM32_BOOTLOADER_GET_ID_COMMAND) return false;

  if (stats.goCommandCount != 0 || stats.goFinalAckCount != 0 || stats.goCompleteSeen) return false;
  if (stats.nackCount != 0 || stats.checksumErrorCount != 0 || stats.otherCommandCount != 0) return false;

  if (stats.writeProtectCommandCount != 0 ||
      stats.writeUnprotectCommandCount != 0 ||
      stats.readoutProtectCommandCount != 0 ||
      stats.readoutUnprotectCommandCount != 0 ||
      stats.specialCommandCount != 0 ||
      stats.extendedSpecialCommandCount != 0) {
    return false;
  }

  return true;
}

uint32_t UartBridge::getBootloaderSnifferByteCount() const {
  return _bootloaderSnifferStats.hostToTargetBytes +
         _bootloaderSnifferStats.targetToHostBytes;
}

void UartBridge::resetBootloaderSniffer() {
  memset(&_bootloaderSnifferStats, 0, sizeof(_bootloaderSnifferStats));
  resetBootloaderSnifferPhase();
  _bootloaderGoComplete = false;
}

void UartBridge::resetBootloaderSnifferPhase() {
  _bootloaderPendingCommand = 0;
  _bootloaderFrameIndex = 0;
  _bootloaderPayloadRemaining = 0;
  _bootloaderEraseLength = 0;
  memset(_bootloaderFrame, 0, sizeof(_bootloaderFrame));
  setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_IDLE);
}

void UartBridge::setBootloaderSnifferPhase(BootloaderSnifferPhase phase) {
  _bootloaderSnifferPhase = phase;
  _bootloaderSnifferStats.lastSnifferPhase = (uint8_t)phase;
}

void UartBridge::beginBootloaderCommand(uint8_t command) {
  _bootloaderPendingCommand = command;
  _bootloaderSnifferStats.commandCount++;
  _bootloaderSnifferStats.lastCommand = command;

  if (command == STM32_BOOTLOADER_GET_COMMAND) {
    _bootloaderSnifferStats.getCommandCount++;
  } else if (command == STM32_BOOTLOADER_GET_VERSION_COMMAND) {
    _bootloaderSnifferStats.getVersionCommandCount++;
  } else if (command == STM32_BOOTLOADER_GET_ID_COMMAND) {
    _bootloaderSnifferStats.getIdCommandCount++;
  } else if (command == STM32_BOOTLOADER_WRITE_COMMAND) {
    _bootloaderSnifferStats.writeCommandCount++;
  } else if (command == STM32_BOOTLOADER_READ_COMMAND) {
    _bootloaderSnifferStats.readCommandCount++;
  } else if (command == STM32_BOOTLOADER_ERASE_COMMAND ||
             command == STM32_BOOTLOADER_EXT_ERASE_COMMAND) {
    _bootloaderSnifferStats.eraseCommandCount++;
  } else if (command == STM32_BOOTLOADER_GO_COMMAND) {
    _bootloaderSnifferStats.goCommandCount++;
  } else if (command == STM32_BOOTLOADER_GET_CHECKSUM_COMMAND) {
    _bootloaderSnifferStats.getChecksumCommandCount++;
  } else if (command == STM32_BOOTLOADER_WRITE_PROTECT_COMMAND) {
    _bootloaderSnifferStats.writeProtectCommandCount++;
  } else if (command == STM32_BOOTLOADER_WRITE_UNPROTECT_COMMAND) {
    _bootloaderSnifferStats.writeUnprotectCommandCount++;
  } else if (command == STM32_BOOTLOADER_READOUT_PROTECT_COMMAND) {
    _bootloaderSnifferStats.readoutProtectCommandCount++;
  } else if (command == STM32_BOOTLOADER_READOUT_UNPROTECT_COMMAND) {
    _bootloaderSnifferStats.readoutUnprotectCommandCount++;
  } else if (command == STM32_BOOTLOADER_SPECIAL_COMMAND) {
    _bootloaderSnifferStats.specialCommandCount++;
  } else if (command == STM32_BOOTLOADER_EXT_SPECIAL_COMMAND) {
    _bootloaderSnifferStats.extendedSpecialCommandCount++;
  } else {
    _bootloaderSnifferStats.otherCommandCount++;
    _bootloaderSnifferStats.lastOtherCommand = command;
  }
}

void UartBridge::handleBootloaderCommandAck() {
  _bootloaderSnifferStats.commandAckCount++;

  if (_bootloaderPendingCommand == STM32_BOOTLOADER_GO_COMMAND ||
      _bootloaderPendingCommand == STM32_BOOTLOADER_READ_COMMAND ||
      _bootloaderPendingCommand == STM32_BOOTLOADER_WRITE_COMMAND ||
      _bootloaderPendingCommand == STM32_BOOTLOADER_GET_CHECKSUM_COMMAND) {
    _bootloaderFrameIndex = 0;
    memset(_bootloaderFrame, 0, sizeof(_bootloaderFrame));
    setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_ADDRESS);
    return;
  }

  if (_bootloaderPendingCommand == STM32_BOOTLOADER_ERASE_COMMAND ||
      _bootloaderPendingCommand == STM32_BOOTLOADER_EXT_ERASE_COMMAND) {
    _bootloaderFrameIndex = 0;
    _bootloaderEraseLength = 0;
    setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_ERASE_LENGTH);
    return;
  }

  resetBootloaderSnifferPhase();
}

void UartBridge::handleBootloaderFinalAck() {
  if (_bootloaderPendingCommand == STM32_BOOTLOADER_WRITE_COMMAND) {
    _bootloaderSnifferStats.writeFinalAckCount++;
  } else if (_bootloaderPendingCommand == STM32_BOOTLOADER_READ_COMMAND) {
    _bootloaderSnifferStats.readFinalAckCount++;
    _bootloaderPayloadRemaining = _bootloaderFrame[0] + 1U;
    setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_SKIP_READ_DATA);
    return;
  } else if (_bootloaderPendingCommand == STM32_BOOTLOADER_ERASE_COMMAND ||
             _bootloaderPendingCommand == STM32_BOOTLOADER_EXT_ERASE_COMMAND) {
    _bootloaderSnifferStats.eraseFinalAckCount++;
  } else if (_bootloaderPendingCommand == STM32_BOOTLOADER_GET_CHECKSUM_COMMAND) {
    _bootloaderSnifferStats.getChecksumFinalAckCount++;
    _bootloaderPayloadRemaining = 5; // CRC value (4 bytes) plus checksum
    setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_SKIP_READ_DATA);
    return;
  }

  resetBootloaderSnifferPhase();
}

void UartBridge::captureBootloaderAddressFrame() {
  validateBootloaderFiveByteXor();

  uint32_t address = ((uint32_t)_bootloaderFrame[0] << 24) |
                     ((uint32_t)_bootloaderFrame[1] << 16) |
                     ((uint32_t)_bootloaderFrame[2] << 8) |
                     (uint32_t)_bootloaderFrame[3];
  _bootloaderSnifferStats.lastAddress = address;
  if (_bootloaderPendingCommand == STM32_BOOTLOADER_GO_COMMAND) {
    _bootloaderSnifferStats.lastGoAddress = address;
  }
}

void UartBridge::validateBootloaderFiveByteXor() {
  uint8_t checksum = 0;
  for (size_t i = 0; i < sizeof(_bootloaderFrame); i++) {
    checksum ^= _bootloaderFrame[i];
  }
  if (checksum != 0) {
    _bootloaderSnifferStats.checksumErrorCount++;
  }
}

void UartBridge::observeBootloaderUsbToUart(uint8_t data) {
  _bootloaderSnifferStats.hostToTargetBytes++;

  switch (_bootloaderSnifferPhase) {
    case BOOTLOADER_SNIFFER_IDLE:
      if (data == STM32_BOOTLOADER_SYNC) {
        _bootloaderSnifferStats.syncCount++;
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_SYNC_ACK);
      } else {
        _bootloaderPendingCommand = data;
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_COMMAND_COMPLEMENT);
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_COMMAND_COMPLEMENT:
      if ((uint8_t)(_bootloaderPendingCommand ^ data) == 0xFF) {
        beginBootloaderCommand(_bootloaderPendingCommand);
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_COMMAND_ACK);
      } else {
        resetBootloaderSnifferPhase();
        if (data == STM32_BOOTLOADER_SYNC) {
          _bootloaderSnifferStats.syncCount++;
          setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_SYNC_ACK);
        } else {
          _bootloaderPendingCommand = data;
          setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_COMMAND_COMPLEMENT);
        }
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_ADDRESS:
      _bootloaderFrame[_bootloaderFrameIndex++] = data;
      if (_bootloaderFrameIndex >= sizeof(_bootloaderFrame)) {
        captureBootloaderAddressFrame();
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_ADDRESS_ACK);
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_WRITE_LENGTH:
      _bootloaderFrame[0] = data;
      _bootloaderPayloadRemaining = (uint32_t)data + 2U; // payload bytes plus checksum
      setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_WRITE_PAYLOAD);
      break;

    case BOOTLOADER_SNIFFER_WAIT_WRITE_PAYLOAD:
      if (_bootloaderPayloadRemaining > 0) {
        _bootloaderPayloadRemaining--;
      }
      if (_bootloaderPayloadRemaining == 0) {
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_FINAL_ACK);
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_READ_LENGTH:
      _bootloaderFrame[_bootloaderFrameIndex++] = data;
      if (_bootloaderFrameIndex >= 2) {
        if ((uint8_t)(_bootloaderFrame[0] ^ _bootloaderFrame[1]) != 0xFF) {
          _bootloaderSnifferStats.checksumErrorCount++;
        }
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_READ_ACK);
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_ERASE_LENGTH:
      if (_bootloaderPendingCommand == STM32_BOOTLOADER_EXT_ERASE_COMMAND) {
        if (_bootloaderFrameIndex == 0) {
          _bootloaderEraseLength = (uint16_t)data << 8;
          _bootloaderFrameIndex = 1;
        } else {
          _bootloaderEraseLength |= data;
          if (_bootloaderEraseLength == 0xFFFF ||
              _bootloaderEraseLength == 0xFFFE ||
              _bootloaderEraseLength == 0xFFFD) {
            _bootloaderPayloadRemaining = 1;
          } else {
            _bootloaderPayloadRemaining = ((uint32_t)_bootloaderEraseLength + 1U) * 2U + 1U;
          }
          setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_ERASE_PAYLOAD);
        }
      } else {
        _bootloaderEraseLength = data;
        if (data == 0xFF) {
          _bootloaderPayloadRemaining = 1; // mass erase checksum
        } else {
          _bootloaderPayloadRemaining = (uint32_t)data + 2U; // sector bytes plus checksum
        }
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_ERASE_PAYLOAD);
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_ERASE_PAYLOAD:
      if (_bootloaderPayloadRemaining > 0) {
        _bootloaderPayloadRemaining--;
      }
      if (_bootloaderPayloadRemaining == 0) {
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_FINAL_ACK);
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_CHECKSUM_SIZE:
      _bootloaderFrame[_bootloaderFrameIndex++] = data;
      if (_bootloaderFrameIndex >= sizeof(_bootloaderFrame)) {
        validateBootloaderFiveByteXor();
        _bootloaderFrameIndex = 0;
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_CHECKSUM_SIZE_ACK);
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_CHECKSUM_POLY:
      _bootloaderFrame[_bootloaderFrameIndex++] = data;
      if (_bootloaderFrameIndex >= sizeof(_bootloaderFrame)) {
        validateBootloaderFiveByteXor();
        _bootloaderFrameIndex = 0;
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_CHECKSUM_POLY_ACK);
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_CHECKSUM_INIT:
      _bootloaderFrame[_bootloaderFrameIndex++] = data;
      if (_bootloaderFrameIndex >= sizeof(_bootloaderFrame)) {
        validateBootloaderFiveByteXor();
        _bootloaderFrameIndex = 0;
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_FINAL_ACK);
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_SYNC_ACK:
    case BOOTLOADER_SNIFFER_WAIT_COMMAND_ACK:
    case BOOTLOADER_SNIFFER_WAIT_ADDRESS_ACK:
    case BOOTLOADER_SNIFFER_WAIT_READ_ACK:
    case BOOTLOADER_SNIFFER_WAIT_CHECKSUM_SIZE_ACK:
    case BOOTLOADER_SNIFFER_WAIT_CHECKSUM_POLY_ACK:
    case BOOTLOADER_SNIFFER_SKIP_READ_DATA:
    case BOOTLOADER_SNIFFER_WAIT_FINAL_ACK:
    default:
      resetBootloaderSnifferPhase();
      break;
  }
}

void UartBridge::observeBootloaderUartToUsb(uint8_t data) {
  _bootloaderSnifferStats.targetToHostBytes++;

  bool waitingForAck =
    _bootloaderSnifferPhase == BOOTLOADER_SNIFFER_WAIT_SYNC_ACK ||
    _bootloaderSnifferPhase == BOOTLOADER_SNIFFER_WAIT_COMMAND_ACK ||
    _bootloaderSnifferPhase == BOOTLOADER_SNIFFER_WAIT_ADDRESS_ACK ||
    _bootloaderSnifferPhase == BOOTLOADER_SNIFFER_WAIT_READ_ACK ||
    _bootloaderSnifferPhase == BOOTLOADER_SNIFFER_WAIT_CHECKSUM_SIZE_ACK ||
    _bootloaderSnifferPhase == BOOTLOADER_SNIFFER_WAIT_CHECKSUM_POLY_ACK ||
    _bootloaderSnifferPhase == BOOTLOADER_SNIFFER_WAIT_FINAL_ACK;

  if (waitingForAck) {
    if (data == STM32_BOOTLOADER_ACK) {
      _bootloaderSnifferStats.ackCount++;
    } else if (data == STM32_BOOTLOADER_NACK) {
      _bootloaderSnifferStats.nackCount++;
    }
  }

  switch (_bootloaderSnifferPhase) {
    case BOOTLOADER_SNIFFER_WAIT_SYNC_ACK:
      if (data == STM32_BOOTLOADER_ACK) {
        resetBootloaderSnifferPhase();
      } else {
        resetBootloaderSnifferPhase();
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_COMMAND_ACK:
      if (data == STM32_BOOTLOADER_ACK) {
        handleBootloaderCommandAck();
      } else {
        resetBootloaderSnifferPhase();
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_ADDRESS_ACK:
      if (data == STM32_BOOTLOADER_ACK) {
        if (_bootloaderPendingCommand == STM32_BOOTLOADER_GO_COMMAND) {
          _bootloaderSnifferStats.goFinalAckCount++;
          _bootloaderSnifferStats.goCompleteSeen = true;
          _bootloaderGoComplete = true;
          resetBootloaderSnifferPhase();
        } else if (_bootloaderPendingCommand == STM32_BOOTLOADER_WRITE_COMMAND) {
          setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_WRITE_LENGTH);
        } else if (_bootloaderPendingCommand == STM32_BOOTLOADER_READ_COMMAND) {
          _bootloaderFrameIndex = 0;
          setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_READ_LENGTH);
        } else if (_bootloaderPendingCommand == STM32_BOOTLOADER_GET_CHECKSUM_COMMAND) {
          _bootloaderFrameIndex = 0;
          setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_CHECKSUM_SIZE);
        } else {
          resetBootloaderSnifferPhase();
        }
      } else {
        resetBootloaderSnifferPhase();
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_READ_ACK:
      if (data == STM32_BOOTLOADER_ACK) {
        handleBootloaderFinalAck();
      } else {
        resetBootloaderSnifferPhase();
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_FINAL_ACK:
      if (data == STM32_BOOTLOADER_ACK) {
        handleBootloaderFinalAck();
      } else {
        resetBootloaderSnifferPhase();
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_CHECKSUM_SIZE_ACK:
      if (data == STM32_BOOTLOADER_ACK) {
        _bootloaderFrameIndex = 0;
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_CHECKSUM_POLY);
      } else {
        resetBootloaderSnifferPhase();
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_CHECKSUM_POLY_ACK:
      if (data == STM32_BOOTLOADER_ACK) {
        _bootloaderFrameIndex = 0;
        setBootloaderSnifferPhase(BOOTLOADER_SNIFFER_WAIT_CHECKSUM_INIT);
      } else {
        resetBootloaderSnifferPhase();
      }
      break;

    case BOOTLOADER_SNIFFER_SKIP_READ_DATA:
      if (_bootloaderPayloadRemaining > 0) {
        _bootloaderPayloadRemaining--;
      }
      if (_bootloaderPayloadRemaining == 0) {
        resetBootloaderSnifferPhase();
      }
      break;

    case BOOTLOADER_SNIFFER_WAIT_COMMAND_COMPLEMENT:
    case BOOTLOADER_SNIFFER_WAIT_ADDRESS:
    case BOOTLOADER_SNIFFER_WAIT_WRITE_LENGTH:
    case BOOTLOADER_SNIFFER_WAIT_WRITE_PAYLOAD:
    case BOOTLOADER_SNIFFER_WAIT_READ_LENGTH:
    case BOOTLOADER_SNIFFER_WAIT_ERASE_LENGTH:
    case BOOTLOADER_SNIFFER_WAIT_ERASE_PAYLOAD:
    case BOOTLOADER_SNIFFER_WAIT_CHECKSUM_SIZE:
    case BOOTLOADER_SNIFFER_WAIT_CHECKSUM_POLY:
    case BOOTLOADER_SNIFFER_WAIT_CHECKSUM_INIT:
    case BOOTLOADER_SNIFFER_IDLE:
    default:
      break;
  }
}

bool UartBridge::hasActivity() const {
  return _hasActivity;
}

void UartBridge::clearActivity() {
  _hasActivity = false;
}

uint32_t UartBridge::getTotalBytes() const {
  return _totalBytes;
}

bool UartBridge::isBootCommandReceived() const {
  return _bootCommandReceived;
}

void UartBridge::setEnabled(bool enabled) {
  _enabled = enabled;
}

bool UartBridge::isEnabled() const {
  return _enabled;
}

void UartBridge::clearBootCommand() {
  _bootCommandReceived = false;
  _bootCmdIndex = 0;
  memset(_bootCmdBuffer, 0, sizeof(_bootCmdBuffer));
}

void UartBridge::checkBootCommand() {
  if (_usb == nullptr) return;

  // 透過ブリッジ有効中はUSB入力を必ずSTM32へ渡す。
  // ここでBOOT検出のためにread()すると、process()直後に届いた通常CLIコマンド
  // (例: exit\r\n) をESP32側が消費し、STM32へ届かない競合が起きる。
  if (_enabled) return;
  
  // USB-CDCからのデータを読み取ってBOOTコマンドを検出
  while (_usb->available() > 0) {
    char c = _usb->read();
    
    // 期待する文字と一致するかチェック
    if (c == BOOT_COMMAND[_bootCmdIndex]) {
      _bootCmdBuffer[_bootCmdIndex] = c;
      _bootCmdIndex++;
      
      // 「BOOT」が完全に一致したらフラグを立てる
      if (_bootCmdIndex >= BOOT_CMD_LENGTH) {
        _bootCommandReceived = true;
        _bootCmdIndex = 0;
        memset(_bootCmdBuffer, 0, sizeof(_bootCmdBuffer));
        return;
      }
    } else {
      // 不一致の場合はリセットして、最初の文字から再チェック
      _bootCmdIndex = 0;
      memset(_bootCmdBuffer, 0, sizeof(_bootCmdBuffer));
      
      // 今回の文字が'B'なら最初からマッチング開始
      if (c == BOOT_COMMAND[0]) {
        _bootCmdBuffer[0] = c;
        _bootCmdIndex = 1;
      }
    }
  }
}

void UartBridge::flushUart() const {
  if (_uart == nullptr) return;
  
  // UART1の受信バッファ内の全てのデータを破棄
  // これにより、ブートモード遷移時の古いデータが
  // STM32通信に干渉しないようにする
  while (_uart->available() > 0) {
    _uart->read();
  }
}

int UartBridge::readForWindSensor() {
  // リングバッファから1バイト読み取り
  if (_windBufferHead == _windBufferTail) {
    return -1;  // データなし
  }
  
  uint8_t byte = _windBuffer[_windBufferTail];
  _windBufferTail = (_windBufferTail + 1) % WIND_BUFFER_SIZE;
  return byte;
}

int UartBridge::availableForWindSensor() const {
  // バッファ内の利用可能バイト数を計算
  if (_windBufferHead >= _windBufferTail) {
    return _windBufferHead - _windBufferTail;
  } else {
    return WIND_BUFFER_SIZE - _windBufferTail + _windBufferHead;
  }
}
