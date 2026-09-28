/**
 * @file ulsa_evo_i2c_client.h
 * @brief ULSA EVO I2Cスレーブ読み取り/設定クライアント
 */

#ifndef ULSA_EVO_I2C_CLIENT_H
#define ULSA_EVO_I2C_CLIENT_H

#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include "i2c_sequence_tracker.h"
#include "i2c_retry_deadline.h"
#include "i2c_source_timestamp_clock.h"
#include "wind_data.h"
#include "pin_config.h"

enum UlsaEvoI2cClientError : uint8_t {
  ULSA_I2C_ERR_NONE = 0,
  ULSA_I2C_ERR_NO_WIRE,
  ULSA_I2C_ERR_NACK,
  ULSA_I2C_ERR_SHORT_READ,
  ULSA_I2C_ERR_INVALID_WHOAMI,
  ULSA_I2C_ERR_UNSUPPORTED_VERSION,
  ULSA_I2C_ERR_DATA_NOT_READY,
  ULSA_I2C_ERR_INVALID_DATA,
  ULSA_I2C_ERR_DIAG_UNSUPPORTED,
  ULSA_I2C_ERR_BACKOFF,
  ULSA_I2C_ERR_CONFIG_UNSUPPORTED,
  ULSA_I2C_ERR_INVALID_CONFIG_VALUE,
  ULSA_I2C_ERR_COMMAND_FAILED,
  ULSA_I2C_ERR_COMMAND_TIMEOUT,
  ULSA_I2C_ERR_SUSPENDED
};

struct UlsaEvoI2cStats {
  uint32_t probeCount;
  uint32_t probeSuccess;
  uint32_t readAttempts;
  uint32_t readSuccess;
  uint32_t readErrors;
  uint32_t staleSeq;
  uint32_t sequenceGapCount;
  uint32_t missedSequenceCount;
  uint32_t sequenceResetCount;
  uint32_t shortReads;
  uint32_t nackErrors;
  uint32_t validationErrors;
  uint32_t backoffSkips;
  uint32_t lastSuccessTime;
  uint32_t lastErrorTime;
  uint32_t observedIntervalMs;
  uint32_t pollIntervalMs;
  uint16_t lastSeq;
  uint16_t lastSequenceDelta;
  uint16_t maxSequenceGap;
  uint8_t lastRequestedLength;
  uint8_t lastReceivedLength;
  uint8_t lastStatus;
  uint8_t lastServiceStatus;
  uint8_t lastActiveCause;
  uint8_t lastNtcReadingStatus;
  uint8_t lastRemoteError;
  uint8_t lastWireStatus;
  uint8_t lastError;
  uint8_t configuredOutputRateHz;
  uint8_t i2cOutputRateHz;
  uint16_t i2cOutputIntervalMs;

  UlsaEvoI2cStats()
    : probeCount(0)
    , probeSuccess(0)
    , readAttempts(0)
    , readSuccess(0)
    , readErrors(0)
    , staleSeq(0)
    , sequenceGapCount(0)
    , missedSequenceCount(0)
    , sequenceResetCount(0)
    , shortReads(0)
    , nackErrors(0)
    , validationErrors(0)
    , backoffSkips(0)
    , lastSuccessTime(0)
    , lastErrorTime(0)
    , observedIntervalMs(0)
    , pollIntervalMs(ULSA_EVO_I2C_POLL_INTERVAL_DEFAULT_MS)
    , lastSeq(0)
    , lastSequenceDelta(0)
    , maxSequenceGap(0)
    , lastRequestedLength(0)
    , lastReceivedLength(0)
    , lastStatus(0)
    , lastServiceStatus(0)
    , lastActiveCause(WIND_CAUSE_NONE)
    , lastNtcReadingStatus(WIND_NTC_READING_NOT_SAMPLED)
    , lastRemoteError(0)
    , lastWireStatus(0)
    , lastError(ULSA_I2C_ERR_NONE)
    , configuredOutputRateHz(0)
    , i2cOutputRateHz(0)
    , i2cOutputIntervalMs(0) {
  }
};


struct UlsaEvoI2cConfig {
  uint8_t nodeId;
  uint8_t avgCycle;
  uint8_t windDirInstallMode;
  uint8_t i2cAddr;
  uint8_t i2cSlaveEnabled;
  uint8_t measIntervalMs;
  uint8_t flags;
  uint8_t configuredOutputRateHz;
  uint8_t i2cOutputRateHz;
  uint16_t i2cOutputIntervalMs;
  uint8_t commandStatus;
  uint8_t lastError;
  uint8_t commandResultSeq;

  UlsaEvoI2cConfig()
    : nodeId(0)
    , avgCycle(0)
    , windDirInstallMode(0)
    , i2cAddr(0)
    , i2cSlaveEnabled(0)
    , measIntervalMs(0)
    , flags(0)
    , configuredOutputRateHz(0)
    , i2cOutputRateHz(0)
    , i2cOutputIntervalMs(0)
    , commandStatus(0)
    , lastError(0)
    , commandResultSeq(0) {
  }
};

struct UlsaEvoI2cConfigResult {
  uint8_t commandStatus;
  uint8_t lastError;
  uint8_t flags;
  uint8_t commandResultSeq;
  bool rebootRequired;

  UlsaEvoI2cConfigResult()
    : commandStatus(0)
    , lastError(0)
    , flags(0)
    , commandResultSeq(0)
    , rebootRequired(false) {
  }
};

class UlsaEvoI2cClient {
public:
  UlsaEvoI2cClient();

  void begin(TwoWire* wire = &Wire, uint8_t address = ULSA_EVO_I2C_ADDR_DEFAULT);
  void setAddress(uint8_t address);
  bool probe();
  bool probeWithStop();
  bool update();
  bool readOnce(WindData& data, bool requireNewSeq = true);
  bool readStatus();
  /**
   * @brief STM32の実行中Node IDを読む
   *
   * CFG_NODE_IDのdraftではなく、公開I2Cレジスタ0x05の実値を返す。
   * 計測snapshotの妥当性とは独立して使う。
   */
  bool readNodeId(uint8_t& nodeId);
  bool readFirmwareVersion(uint32_t& firmwareVersion);
  bool readFirmwareRevision(uint32_t& firmwareRevision);
  bool readConfig(UlsaEvoI2cConfig& config);
  bool readCommandStatus(uint8_t& commandStatus, uint8_t& lastError);
  bool stageNodeId(uint8_t nodeId);
  bool stageAvgCycle(uint8_t avgCycle);
  bool stageWindDirInstallMode(uint8_t mode);
  bool stageI2cAddress(uint8_t address);
  bool saveConfig(UlsaEvoI2cConfigResult* result = nullptr);
  bool discardConfig(UlsaEvoI2cConfigResult* result = nullptr);
  bool restoreConfigDefaults(UlsaEvoI2cConfigResult* result = nullptr);
  bool clearRemoteError(UlsaEvoI2cConfigResult* result = nullptr);

  const WindData& getData() const;
  const UlsaEvoI2cStats& getStats() const;
  uint8_t getAddress() const;
  uint8_t getRegVersion() const;
  uint8_t getCommandResultSeq() const;
  bool isDetected() const;
  bool hasData() const;
  bool supportsConfigWrite() const;
  void setSuspended(bool suspended);
  bool isSuspended() const;
  uint32_t getPollIntervalMs() const;
  const char* getLastErrorName() const;

  static const uint8_t STATUS_DATA_READY = 0x80;
  static const uint8_t STATUS_DATA_VALID = 0x40;
  static const uint8_t STATUS_HIGH_WIND_CLIPPED = 0x20;
  static const uint8_t STATUS_HV_INHIBITED = 0x10;
  static const uint8_t STATUS_SAFETY_LATCHED = 0x08;
  static const uint8_t STATUS_CALIB_ACTIVE = 0x04;
  static const uint8_t STATUS_CONFIG_ACTIVE = 0x02;
  static const uint8_t STATUS_EEPROM_BUSY = 0x01;
  static const uint8_t SERVICE_I2C_WRITE_LOCKED = 0x80;

  static const uint8_t CFG_FLAG_DIRTY = 0x01;
  static const uint8_t CFG_FLAG_REBOOT_REQUIRED = 0x02;
  static const uint8_t CFG_FLAG_EEPROM_BUSY = 0x04;

  static const char* errorName(uint8_t error);
  static const char* remoteErrorName(uint8_t error);
  static const char* commandStatusName(uint8_t status);
  static const char* wireStatusName(uint8_t status);

private:
  static const uint8_t REG_DEVICE_ID = 0x00;
  static const uint8_t REG_VERSION = 0x01;
  static const uint8_t REG_STATUS = 0x03;
  static const uint8_t REG_ACTIVE_CAUSE = 0x04;
  static const uint8_t REG_NODE_ID = 0x05;
  static const uint8_t REG_FIRMVER_START = 0x08;
  static const uint8_t REG_CMD_RESULT_SEQ = 0x13;
  static const uint8_t REG_FWREV_START = 0x14;
  static const uint8_t REG_SERVICE_STATUS = 0x18;
  static const uint8_t REG_NTC_READING_STATUS = 0x19;
  static const uint8_t REG_SNAPSHOT_START = 0x20;
  static const uint8_t REG_BOARD_TEMP = 0x2C;
  static const uint8_t REG_CFG_NODE_ID = 0x40;
  static const uint8_t REG_CFG_AVG_CYCLE = 0x41;
  static const uint8_t REG_CFG_WIND_DIR_INSTALL_MODE = 0x42;
  static const uint8_t REG_CFG_I2C_ADDR = 0x43;
  static const uint8_t REG_CFG_I2C_SLAVE_ENABLED = 0x44;
  static const uint8_t REG_CFG_MEAS_INTERVAL_MS = 0x45;
  static const uint8_t REG_CFG_FLAGS = 0x46;
  static const uint8_t REG_CFG_OUTPUT_RATE_HZ = 0x47;
  static const uint8_t REG_I2C_OUTPUT_RATE_HZ = 0x48;
  static const uint8_t REG_I2C_OUTPUT_INTERVAL_MS = 0x49;
  static const uint8_t REG_CMD = 0xF0;
  static const uint8_t REG_CMD_STATUS = 0xF1;
  static const uint8_t REG_CMD_LOCK = 0xF2;
  static const uint8_t REG_LAST_ERROR = 0xF3;
  static const uint8_t REG_WHOAMI = 0xFF;
  static const uint8_t DEVICE_ID = 0xEA;
  static const uint8_t SUPPORTED_REG_VERSION_MIN = 0x0E;
  static const uint8_t CONFIG_WRITE_REG_VERSION_MIN = 0x06;
  static const uint8_t OUTPUT_RATE_REG_VERSION_MIN = 0x07;
  static const uint8_t COMMAND_RESULT_SEQ_REG_VERSION_MIN = 0x08;
  static const uint8_t FIRMWARE_REVISION_REG_VERSION_MIN = 0x0B;
  static const uint8_t SNAPSHOT_LENGTH = 14;
  static const uint8_t STATUS_INFO_LENGTH =
      REG_NTC_READING_STATUS - REG_STATUS + 1;
  static const uint8_t STATUS_INFO_SERVICE_OFFSET =
      REG_SERVICE_STATUS - REG_STATUS;
  static const uint8_t STATUS_INFO_NTC_OFFSET =
      REG_NTC_READING_STATUS - REG_STATUS;
  static const uint8_t SNAPSHOT_BOARD_TEMP_OFFSET = REG_BOARD_TEMP - REG_SNAPSHOT_START;
  static const uint8_t CMD_LOCK_KEY = 0xA5;
  static const uint8_t CMD_SAVE_CONFIG = 0x01;
  static const uint8_t CMD_DISCARD_CONFIG = 0x02;
  static const uint8_t CMD_RESTORE_CONFIG_DEFAULTS = 0x03;
  static const uint8_t CMD_CLEAR_ERROR = 0x04;
  static const uint8_t CMD_STATUS_OK = 0x00;
  static const uint8_t CMD_STATUS_BUSY = 0x01;
  static const uint8_t CMD_STATUS_LOCKED = 0x02;
  static const uint8_t CMD_STATUS_INVALID_REGISTER = 0x03;
  static const uint8_t CMD_STATUS_INVALID_VALUE = 0x04;
  static const uint8_t CMD_STATUS_INVALID_COMMAND = 0x05;
  static const uint8_t CMD_STATUS_EEPROM_BUSY = 0x06;
  static const uint8_t CMD_STATUS_ERROR = 0x07;
  static const uint8_t CMD_STATUS_REBOOT_REQUIRED = 0x08;
  static const uint32_t COMMAND_TIMEOUT_MS = 5000;
  static const uint16_t COMMAND_SETTLE_MS = 200;
  static const uint16_t STAGE_TIMEOUT_MS = 200;
  static const uint16_t COMMAND_POLL_INTERVAL_MS = 20;
  static const uint8_t CONFIG_READ_ATTEMPTS = 3;
  static const uint16_t CONFIG_READ_RETRY_DELAY_MS = 4;

  TwoWire* _wire;
  uint8_t _address;
  uint8_t _regVersion;
  uint8_t _lastCommandResultSeq;
  bool _detected;
  bool _hasData;
  bool _suspended;
  uint32_t _lastPollTime;
  I2cRetryDeadline _retryDeadline;
  uint32_t _pollIntervalMs;
  uint8_t _consecutiveErrors;
  WindData _latestData;
  UlsaEvoI2cStats _stats;
  I2cSequenceTracker _sequenceTracker;
  I2cSourceTimestampClock _sourceTimestampClock;

  void configureBus();
  bool blockIfSuspended();
  bool readRegister8(uint8_t reg, uint8_t& value);
  bool readRegister8Stop(uint8_t reg, uint8_t& value);
  bool readCommandResultSeq(uint8_t& resultSeq);
  bool writeRegister8(uint8_t reg, uint8_t value);
  bool readBlock(uint8_t reg, uint8_t* buffer, uint8_t length);
  bool readBlockStop(uint8_t reg, uint8_t* buffer, uint8_t length);
  bool ensureConfigWriteSupported();
  bool readOutputRateRegisters();
  void applyOutputIntervalMs(uint16_t intervalMs);
  bool stageConfigRegister(uint8_t reg, uint8_t value);
  bool runCommand(uint8_t command, bool useLock, UlsaEvoI2cConfigResult* result);
  bool waitForCommandCompletion(uint8_t command, uint8_t initialFlags,
                                bool useResultSeq,
                                uint8_t initialResultSeq,
                                bool hasInitialCommandResult,
                                uint8_t initialCommandStatus,
                                uint8_t initialLastError,
                                uint32_t timeoutMs,
                                UlsaEvoI2cConfigResult* result);
  bool readConfigFlags(uint8_t& flags);
  void recordError(uint8_t error);
  void clearError();
  void scheduleBackoff();
  bool isBackoffActive(uint32_t now);
  bool decodeSnapshot(const uint8_t* info, const uint8_t* snapshot, WindData& data);
  static bool isValidAvgCycle(uint8_t avgCycle);
  static bool isValidWindDirInstallMode(uint8_t mode);
  static bool isValidI2cAddress(uint8_t address);
  static uint16_t readU16LE(const uint8_t* data);
  static int16_t readI16LE(const uint8_t* data);
  static uint32_t readU32LE(const uint8_t* data);
};

#endif // ULSA_EVO_I2C_CLIENT_H
