/**
 * @file ulsa_evo_i2c_client.cpp
 * @brief ULSA EVO I2Cスレーブ読み取り/設定クライアント実装
 */

#include "ulsa_evo_i2c_client.h"
#include "i2c_poll_cadence_policy.h"
#include <string.h>

UlsaEvoI2cClient::UlsaEvoI2cClient()
  : _wire(nullptr)
  , _address(ULSA_EVO_I2C_ADDR_DEFAULT)
  , _regVersion(0)
  , _lastCommandResultSeq(0)
  , _detected(false)
  , _hasData(false)
  , _suspended(false)
  , _lastPollTime(0)
  , _pollIntervalMs(ULSA_EVO_I2C_POLL_INTERVAL_DEFAULT_MS)
  , _consecutiveErrors(0) {
}

void UlsaEvoI2cClient::begin(TwoWire* wire, uint8_t address) {
  _wire = wire;
  _address = address;
  _regVersion = 0;
  _lastCommandResultSeq = 0;
  _detected = false;
  _hasData = false;
  _suspended = false;
  _lastPollTime = 0;
  _retryDeadline.clear();
  _pollIntervalMs = ULSA_EVO_I2C_POLL_INTERVAL_DEFAULT_MS;
  _consecutiveErrors = 0;
  _latestData = WindData();
  _stats = UlsaEvoI2cStats();
  _sequenceTracker.reset();
  _sourceTimestampClock.reset();
  _stats.pollIntervalMs = _pollIntervalMs;
  configureBus();
}

void UlsaEvoI2cClient::setAddress(uint8_t address) {
  if (address < 0x08 || address > 0x77) {
    return;
  }
  begin(_wire, address);
}

void UlsaEvoI2cClient::configureBus() {
  if (_wire == nullptr) return;
  _wire->setClock(I2C_FREQUENCY);
  _wire->setTimeOut(ULSA_EVO_I2C_TIMEOUT_MS);
}

bool UlsaEvoI2cClient::probe() {
  if (blockIfSuspended()) {
    return false;
  }

  _stats.probeCount++;
  configureBus();

  uint8_t whoami = 0;
  if (!readRegister8(REG_WHOAMI, whoami)) {
    _detected = false;
    scheduleBackoff();
    return false;
  }

  if (whoami != DEVICE_ID) {
    _detected = false;
    _stats.lastWireStatus = 0;
    recordError(ULSA_I2C_ERR_INVALID_WHOAMI);
    scheduleBackoff();
    return false;
  }

  uint8_t version = 0;
  if (!readRegister8(REG_VERSION, version)) {
    _detected = false;
    scheduleBackoff();
    return false;
  }

  if (version < SUPPORTED_REG_VERSION_MIN) {
    _detected = false;
    _regVersion = version;
    recordError(ULSA_I2C_ERR_UNSUPPORTED_VERSION);
    scheduleBackoff();
    return false;
  }

  _regVersion = version;
  _detected = true;
  _stats.probeSuccess++;
  _consecutiveErrors = 0;
  _retryDeadline.clear();
  readStatus();
  readOutputRateRegisters();
  clearError();
  return true;
}

bool UlsaEvoI2cClient::probeWithStop() {
  if (blockIfSuspended()) {
    return false;
  }

  _stats.probeCount++;
  configureBus();

  uint8_t whoami = 0;
  if (!readRegister8Stop(REG_WHOAMI, whoami)) {
    _detected = false;
    scheduleBackoff();
    return false;
  }

  if (whoami != DEVICE_ID) {
    _detected = false;
    _stats.lastWireStatus = 0;
    recordError(ULSA_I2C_ERR_INVALID_WHOAMI);
    scheduleBackoff();
    return false;
  }

  uint8_t version = 0;
  if (!readRegister8Stop(REG_VERSION, version)) {
    _detected = false;
    scheduleBackoff();
    return false;
  }

  if (version < SUPPORTED_REG_VERSION_MIN) {
    _detected = false;
    _regVersion = version;
    recordError(ULSA_I2C_ERR_UNSUPPORTED_VERSION);
    scheduleBackoff();
    return false;
  }

  _regVersion = version;
  _detected = true;
  _stats.probeSuccess++;
  _consecutiveErrors = 0;
  _retryDeadline.clear();

  uint8_t status = 0;
  if (readRegister8Stop(REG_STATUS, status)) {
    _stats.lastStatus = status;
  }
  uint8_t remoteError = 0;
  if (readRegister8Stop(REG_LAST_ERROR, remoteError)) {
    _stats.lastRemoteError = remoteError;
  }

  readOutputRateRegisters();

  clearError();
  return true;
}

bool UlsaEvoI2cClient::update() {
  if (blockIfSuspended()) {
    return false;
  }

  uint32_t now = millis();
  if (isBackoffActive(now)) {
    _stats.backoffSkips++;
    _stats.lastError = ULSA_I2C_ERR_BACKOFF;
    return false;
  }

  if (now - _lastPollTime < _pollIntervalMs) {
    return false;
  }
  _lastPollTime = I2cPollCadencePolicy::advanceScheduledPoll(
      _lastPollTime, now, _pollIntervalMs);

  if (!_detected && !probe()) {
    return false;
  }

  WindData data;
  return readOnce(data, true);
}

bool UlsaEvoI2cClient::readOnce(WindData& data, bool requireNewSeq) {
  if (blockIfSuspended()) {
    return false;
  }

  _stats.readAttempts++;
  configureBus();

  // The phase-stable oversample cadence probes only DATA_SEQ first. A full
  // snapshot remains tied to a newly published source sample, which preserves
  // the standard I2C load while avoiding a miss at a source-period edge.
  uint8_t sequenceRaw[2] = {0};
  if (!readBlock(REG_SNAPSHOT_START, sequenceRaw, sizeof(sequenceRaw))) {
    scheduleBackoff();
    return false;
  }
  const uint16_t probedSequence = readU16LE(sequenceRaw);
  if (requireNewSeq && _hasData && probedSequence == _stats.lastSeq &&
      (_latestData.status & STATUS_DATA_READY) != 0) {
    _stats.staleSeq++;
    _stats.lastSequenceDelta = 0;
    clearError();
    return false;
  }

  uint8_t info[STATUS_INFO_LENGTH] = {0};
  if (!readBlock(REG_STATUS, info, sizeof(info))) {
    scheduleBackoff();
    return false;
  }
  _stats.lastStatus = info[0];
  _stats.lastActiveCause = info[1];
  _stats.lastServiceStatus = info[STATUS_INFO_SERVICE_OFFSET];
  _stats.lastNtcReadingStatus = info[STATUS_INFO_NTC_OFFSET];

  uint8_t remoteError = 0;
  if (readRegister8(REG_LAST_ERROR, remoteError)) {
    _stats.lastRemoteError = remoteError;
  }

  if ((info[0] & STATUS_DATA_READY) == 0) {
    WindData statusOnly;
    statusOnly.nodeId = info[2];
    statusOnly.statusProtocolVersion = WIND_STATUS_PROTOCOL_VERSION;
    statusOnly.status = info[0];
    statusOnly.serviceStatus = info[STATUS_INFO_SERVICE_OFFSET];
    statusOnly.activeCause = info[1];
    statusOnly.ntcReadingStatus = info[STATUS_INFO_NTC_OFFSET];
    statusOnly.isValid = false;
    statusOnly.sourceSequence = probedSequence;
    statusOnly.sourceSequenceValid = true;
    statusOnly.timestamp = millis();
    if (!statusOnly.isDataValid()) {
      _stats.validationErrors++;
      recordError(ULSA_I2C_ERR_INVALID_DATA);
      return false;
    }

    const bool statusChanged =
      !_hasData || _latestData.status != statusOnly.status ||
      _latestData.serviceStatus != statusOnly.serviceStatus ||
      _latestData.activeCause != statusOnly.activeCause ||
      _latestData.ntcReadingStatus != statusOnly.ntcReadingStatus ||
      _latestData.nodeId != statusOnly.nodeId;
    if (!statusChanged) {
      clearError();
      return false;
    }

    _latestData = statusOnly;
    data = statusOnly;
    _hasData = true;
    _stats.lastSeq = probedSequence;
    _stats.lastSequenceDelta = 0;
    _stats.readSuccess++;
    _stats.lastSuccessTime = statusOnly.timestamp;
    _consecutiveErrors = 0;
    clearError();
    return true;
  }

  uint8_t snapshot[SNAPSHOT_LENGTH] = {0};
  if (!readBlock(REG_SNAPSHOT_START, snapshot, sizeof(snapshot))) {
    scheduleBackoff();
    return false;
  }

  const uint16_t seq = readU16LE(&snapshot[0]);
  if (seq != probedSequence) {
    // STATUS and measurement data are separate I2C reads. If publication
    // advanced between them, discard only this mixed-generation observation
    // and retry on the next poll without entering transport backoff.
    _stats.staleSeq++;
    _stats.lastSequenceDelta = 0;
    clearError();
    return false;
  }
  const I2cSequenceObservation sequenceObservation = _sequenceTracker.observe(seq);
  if (requireNewSeq && sequenceObservation.duplicate) {
    _stats.staleSeq++;
    _stats.lastSeq = seq;
    _stats.lastSequenceDelta = 0;
    clearError();
    return false;
  }

  _stats.lastSequenceDelta = sequenceObservation.delta;
  if (sequenceObservation.gap) {
    _stats.sequenceGapCount++;
    _stats.missedSequenceCount += sequenceObservation.missingCount;
    if (sequenceObservation.missingCount > _stats.maxSequenceGap) {
      _stats.maxSequenceGap = sequenceObservation.missingCount;
    }
  } else if (sequenceObservation.reset) {
    _stats.sequenceResetCount++;
  }

  if (!decodeSnapshot(info, snapshot, data)) {
    _stats.validationErrors++;
    recordError(ULSA_I2C_ERR_INVALID_DATA);
    return false;
  }

  const bool sequenceContinuous = !sequenceObservation.reset &&
                                  !sequenceObservation.duplicate;
  data.sourceTimestamp = _sourceTimestampClock.timestampFor(
      data.timestamp,
      sequenceObservation.delta,
      _stats.i2cOutputIntervalMs,
      sequenceContinuous);
  data.sourceTimestampValid = true;


  const uint32_t now = millis();
  if (_hasData && _stats.lastSuccessTime != 0 && seq != _stats.lastSeq) {
    _stats.observedIntervalMs = now - _stats.lastSuccessTime;
  }

  _latestData = data;
  _hasData = true;
  _stats.lastSeq = seq;
  _stats.readSuccess++;
  _stats.lastSuccessTime = now;
  _consecutiveErrors = 0;
  _retryDeadline.clear();
  clearError();
  return true;
}

bool UlsaEvoI2cClient::readStatus() {
  if (blockIfSuspended()) {
    return false;
  }

  uint8_t info[STATUS_INFO_LENGTH] = {0};
  if (!readBlock(REG_STATUS, info, sizeof(info))) {
    return false;
  }
  _stats.lastStatus = info[0];
  _stats.lastActiveCause = info[1];
  _stats.lastServiceStatus = info[STATUS_INFO_SERVICE_OFFSET];
  _stats.lastNtcReadingStatus = info[STATUS_INFO_NTC_OFFSET];

  uint8_t remoteError = 0;
  if (readRegister8(REG_LAST_ERROR, remoteError)) {
    _stats.lastRemoteError = remoteError;
  }
  return true;
}

bool UlsaEvoI2cClient::readNodeId(uint8_t& nodeId) {
  nodeId = 0;

  if (blockIfSuspended()) {
    return false;
  }

  if (!_detected && !probe()) {
    return false;
  }

  if (!readRegister8(REG_NODE_ID, nodeId)) {
    scheduleBackoff();
    return false;
  }

  _consecutiveErrors = 0;
  _retryDeadline.clear();
  clearError();
  return true;
}

bool UlsaEvoI2cClient::readFirmwareVersion(uint32_t& firmwareVersion) {
  firmwareVersion = 0;

  if (blockIfSuspended()) {
    return false;
  }

  if (!_detected || _regVersion == 0) {
    if (!probe()) {
      return false;
    }
  }

  uint8_t raw[4] = {0};
  if (!readBlock(REG_FIRMVER_START, raw, sizeof(raw))) {
    scheduleBackoff();
    return false;
  }

  firmwareVersion = readU32LE(raw);
  _consecutiveErrors = 0;
  _retryDeadline.clear();
  clearError();
  return true;
}

bool UlsaEvoI2cClient::readFirmwareRevision(uint32_t& firmwareRevision) {
  firmwareRevision = 0;

  if (blockIfSuspended()) return false;
  if (!_detected || _regVersion == 0) {
    if (!probe()) return false;
  }
  if (_regVersion < FIRMWARE_REVISION_REG_VERSION_MIN) {
    recordError(ULSA_I2C_ERR_UNSUPPORTED_VERSION);
    return false;
  }

  uint8_t raw[4] = {0};
  if (!readBlock(REG_FWREV_START, raw, sizeof(raw))) {
    scheduleBackoff();
    return false;
  }

  firmwareRevision = readU32LE(raw);
  _consecutiveErrors = 0;
  _retryDeadline.clear();
  clearError();
  return true;
}


bool UlsaEvoI2cClient::readConfig(UlsaEvoI2cConfig& config) {
  config = UlsaEvoI2cConfig();

  if (blockIfSuspended()) {
    return false;
  }

  if (!ensureConfigWriteSupported()) {
    return false;
  }

  uint8_t raw[11] = {0};
  const uint8_t readLength = (_regVersion >= OUTPUT_RATE_REG_VERSION_MIN) ? 11 : 7;
  bool configBlockRead = false;
  for (uint8_t attempt = 0; attempt < CONFIG_READ_ATTEMPTS; ++attempt) {
    if (readBlock(REG_CFG_NODE_ID, raw, readLength)) {
      configBlockRead = true;
      break;
    }
    if ((attempt + 1) < CONFIG_READ_ATTEMPTS) {
      delay(CONFIG_READ_RETRY_DELAY_MS);
    }
  }
  if (!configBlockRead) {
    scheduleBackoff();
    return false;
  }

  config.nodeId = raw[0];
  config.avgCycle = raw[1];
  config.windDirInstallMode = raw[2];
  config.i2cAddr = raw[3];
  config.i2cSlaveEnabled = raw[4];
  config.measIntervalMs = raw[5];
  config.flags = raw[6];
  if (readLength >= 11) {
    config.configuredOutputRateHz = raw[7];
    config.i2cOutputRateHz = raw[8];
    config.i2cOutputIntervalMs = readU16LE(&raw[9]);
    _stats.configuredOutputRateHz = config.configuredOutputRateHz;
    _stats.i2cOutputRateHz = config.i2cOutputRateHz;
    _stats.i2cOutputIntervalMs = config.i2cOutputIntervalMs;
    applyOutputIntervalMs(config.i2cOutputIntervalMs);
  }

  if (_regVersion >= COMMAND_RESULT_SEQ_REG_VERSION_MIN) {
    config.commandResultSeq = _lastCommandResultSeq;
    (void)readCommandResultSeq(config.commandResultSeq);
  }

  for (uint8_t attempt = 0; attempt < CONFIG_READ_ATTEMPTS; ++attempt) {
    if (readCommandStatus(config.commandStatus, config.lastError)) {
      _consecutiveErrors = 0;
      _retryDeadline.clear();
      clearError();
      return true;
    }
    if ((attempt + 1) < CONFIG_READ_ATTEMPTS) {
      delay(CONFIG_READ_RETRY_DELAY_MS);
    }
  }

  // The writable config block is coherent and is the source of truth for a
  // read operation. CMD_STATUS/LAST_ERROR are diagnostics; do not discard a
  // confirmed config snapshot only because those follow-up reads were lost.
  // Keep the local I2C error intact for the BLE status and mark remote values
  // unknown rather than presenting stale command diagnostics as current.
  config.commandStatus = 0xFF;
  config.lastError = 0xFF;
  return true;
}

bool UlsaEvoI2cClient::readCommandStatus(uint8_t& commandStatus, uint8_t& lastError) {
  commandStatus = 0;
  lastError = 0;

  if (blockIfSuspended()) {
    return false;
  }

  if (!ensureConfigWriteSupported()) {
    return false;
  }

  if (!readRegister8(REG_CMD_STATUS, commandStatus)) {
    return false;
  }
  if (!readRegister8(REG_LAST_ERROR, lastError)) {
    return false;
  }

  _stats.lastRemoteError = lastError;
  return true;
}

bool UlsaEvoI2cClient::stageNodeId(uint8_t nodeId) {
  return stageConfigRegister(REG_CFG_NODE_ID, nodeId);
}

bool UlsaEvoI2cClient::stageAvgCycle(uint8_t avgCycle) {
  if (!isValidAvgCycle(avgCycle)) {
    recordError(ULSA_I2C_ERR_INVALID_CONFIG_VALUE);
    return false;
  }
  return stageConfigRegister(REG_CFG_AVG_CYCLE, avgCycle);
}

bool UlsaEvoI2cClient::stageWindDirInstallMode(uint8_t mode) {
  if (!isValidWindDirInstallMode(mode)) {
    recordError(ULSA_I2C_ERR_INVALID_CONFIG_VALUE);
    return false;
  }
  return stageConfigRegister(REG_CFG_WIND_DIR_INSTALL_MODE, mode);
}

bool UlsaEvoI2cClient::stageI2cAddress(uint8_t address) {
  if (!isValidI2cAddress(address)) {
    recordError(ULSA_I2C_ERR_INVALID_CONFIG_VALUE);
    return false;
  }
  return stageConfigRegister(REG_CFG_I2C_ADDR, address);
}

bool UlsaEvoI2cClient::saveConfig(UlsaEvoI2cConfigResult* result) {
  return runCommand(CMD_SAVE_CONFIG, true, result);
}

bool UlsaEvoI2cClient::discardConfig(UlsaEvoI2cConfigResult* result) {
  return runCommand(CMD_DISCARD_CONFIG, false, result);
}

bool UlsaEvoI2cClient::restoreConfigDefaults(UlsaEvoI2cConfigResult* result) {
  return runCommand(CMD_RESTORE_CONFIG_DEFAULTS, true, result);
}

bool UlsaEvoI2cClient::clearRemoteError(UlsaEvoI2cConfigResult* result) {
  return runCommand(CMD_CLEAR_ERROR, false, result);
}

const WindData& UlsaEvoI2cClient::getData() const {
  return _latestData;
}

const UlsaEvoI2cStats& UlsaEvoI2cClient::getStats() const {
  return _stats;
}

uint8_t UlsaEvoI2cClient::getAddress() const {
  return _address;
}

uint8_t UlsaEvoI2cClient::getRegVersion() const {
  return _regVersion;
}

uint8_t UlsaEvoI2cClient::getCommandResultSeq() const {
  return _lastCommandResultSeq;
}

bool UlsaEvoI2cClient::isDetected() const {
  return _detected;
}

bool UlsaEvoI2cClient::hasData() const {
  return _hasData;
}

bool UlsaEvoI2cClient::supportsConfigWrite() const {
  return _detected && _regVersion >= CONFIG_WRITE_REG_VERSION_MIN;
}

void UlsaEvoI2cClient::setSuspended(bool suspended) {
  if (_suspended == suspended) {
    return;
  }

  _suspended = suspended;
  _lastPollTime = 0;
  _retryDeadline.clear();
  if (_suspended) {
    _stats.lastRequestedLength = 0;
    _stats.lastReceivedLength = 0;
    _stats.lastWireStatus = 0;
    recordError(ULSA_I2C_ERR_SUSPENDED);
  } else {
    clearError();
  }
}

bool UlsaEvoI2cClient::isSuspended() const {
  return _suspended;
}

uint32_t UlsaEvoI2cClient::getPollIntervalMs() const {
  return _pollIntervalMs;
}

const char* UlsaEvoI2cClient::getLastErrorName() const {
  return errorName(_stats.lastError);
}

bool UlsaEvoI2cClient::blockIfSuspended() {
  if (!_suspended) {
    return false;
  }

  _stats.lastRequestedLength = 0;
  _stats.lastReceivedLength = 0;
  _stats.lastWireStatus = 0;
  _stats.lastError = ULSA_I2C_ERR_SUSPENDED;
  _stats.lastErrorTime = millis();
  return true;
}

bool UlsaEvoI2cClient::readRegister8(uint8_t reg, uint8_t& value) {
  uint8_t buffer[1] = {0};
  if (!readBlock(reg, buffer, 1)) {
    return false;
  }
  value = buffer[0];
  return true;
}

bool UlsaEvoI2cClient::readRegister8Stop(uint8_t reg, uint8_t& value) {
  uint8_t buffer[1] = {0};
  if (!readBlockStop(reg, buffer, 1)) {
    return false;
  }
  value = buffer[0];
  return true;
}

bool UlsaEvoI2cClient::readCommandResultSeq(uint8_t& resultSeq) {
  if (_regVersion < COMMAND_RESULT_SEQ_REG_VERSION_MIN) {
    resultSeq = 0;
    return false;
  }
  if (!readRegister8(REG_CMD_RESULT_SEQ, resultSeq)) {
    return false;
  }
  _lastCommandResultSeq = resultSeq;
  return true;
}

bool UlsaEvoI2cClient::writeRegister8(uint8_t reg, uint8_t value) {
  if (blockIfSuspended()) {
    return false;
  }

  if (_wire == nullptr) {
    recordError(ULSA_I2C_ERR_NO_WIRE);
    return false;
  }

  configureBus();
  _stats.lastRequestedLength = 2;
  _stats.lastReceivedLength = 0;

  _wire->beginTransmission(_address);
  _wire->write(reg);
  _wire->write(value);
  uint8_t result = _wire->endTransmission(true);
  _stats.lastWireStatus = result;
  if (result != 0) {
    _stats.nackErrors++;
    recordError(ULSA_I2C_ERR_NACK);
    return false;
  }

  return true;
}

bool UlsaEvoI2cClient::readBlock(uint8_t reg, uint8_t* buffer, uint8_t length) {
  if (blockIfSuspended()) {
    return false;
  }

  if (_wire == nullptr || buffer == nullptr || length == 0) {
    recordError(ULSA_I2C_ERR_NO_WIRE);
    return false;
  }

  _stats.lastRequestedLength = 0;
  _stats.lastReceivedLength = 0;

  _wire->beginTransmission(_address);
  _wire->write(reg);
  uint8_t result = _wire->endTransmission(false);
  _stats.lastWireStatus = result;
  if (result != 0) {
    _stats.nackErrors++;
    recordError(ULSA_I2C_ERR_NACK);
    return false;
  }
  _stats.lastWireStatus = 0xFE;

  _stats.lastRequestedLength = length;
  uint8_t received = _wire->requestFrom(_address, length);
  _stats.lastReceivedLength = received;
  if (received != length) {
    while (_wire->available() > 0) {
      _wire->read();
    }
    _stats.shortReads++;
    recordError(ULSA_I2C_ERR_SHORT_READ);
    return false;
  }

  for (uint8_t i = 0; i < length; i++) {
    if (_wire->available() <= 0) {
      _stats.shortReads++;
      recordError(ULSA_I2C_ERR_SHORT_READ);
      return false;
    }
    buffer[i] = _wire->read();
  }

  return true;
}

bool UlsaEvoI2cClient::readBlockStop(uint8_t reg, uint8_t* buffer, uint8_t length) {
  if (blockIfSuspended()) {
    return false;
  }

  if (_wire == nullptr || buffer == nullptr || length == 0) {
    recordError(ULSA_I2C_ERR_NO_WIRE);
    return false;
  }

  _stats.lastRequestedLength = 0;
  _stats.lastReceivedLength = 0;

  _wire->beginTransmission(_address);
  _wire->write(reg);
  uint8_t result = _wire->endTransmission(true);
  _stats.lastWireStatus = result;
  if (result != 0) {
    _stats.nackErrors++;
    recordError(ULSA_I2C_ERR_NACK);
    return false;
  }

  _stats.lastRequestedLength = length;
  uint8_t received = _wire->requestFrom(_address, length);
  _stats.lastReceivedLength = received;
  if (received != length) {
    while (_wire->available() > 0) {
      _wire->read();
    }
    _stats.shortReads++;
    recordError(ULSA_I2C_ERR_SHORT_READ);
    return false;
  }

  for (uint8_t i = 0; i < length; i++) {
    if (_wire->available() <= 0) {
      _stats.shortReads++;
      recordError(ULSA_I2C_ERR_SHORT_READ);
      return false;
    }
    buffer[i] = _wire->read();
  }

  return true;
}

bool UlsaEvoI2cClient::ensureConfigWriteSupported() {
  if (blockIfSuspended()) {
    return false;
  }

  if (_wire == nullptr) {
    recordError(ULSA_I2C_ERR_NO_WIRE);
    return false;
  }

  if (!_detected || _regVersion == 0) {
    if (!probe()) {
      return false;
    }
  }

  if (!supportsConfigWrite()) {
    recordError(ULSA_I2C_ERR_CONFIG_UNSUPPORTED);
    return false;
  }

  return true;
}

bool UlsaEvoI2cClient::readOutputRateRegisters() {
  if (blockIfSuspended()) {
    return false;
  }

  if (_wire == nullptr) {
    recordError(ULSA_I2C_ERR_NO_WIRE);
    return false;
  }
  if (!_detected || _regVersion < OUTPUT_RATE_REG_VERSION_MIN) {
    return false;
  }

  uint8_t raw[4] = {0};
  if (!readBlock(REG_CFG_OUTPUT_RATE_HZ, raw, sizeof(raw))) {
    return false;
  }

  _stats.configuredOutputRateHz = raw[0];
  _stats.i2cOutputRateHz = raw[1];
  _stats.i2cOutputIntervalMs = readU16LE(&raw[2]);
  applyOutputIntervalMs(_stats.i2cOutputIntervalMs);
  return true;
}

void UlsaEvoI2cClient::applyOutputIntervalMs(uint16_t intervalMs) {
  if (intervalMs == 0) {
    return;
  }

  const uint32_t nextIntervalMs = I2cPollCadencePolicy::intervalForSource(
      intervalMs,
      ULSA_EVO_I2C_POLL_INTERVAL_MIN_MS,
      ULSA_EVO_I2C_POLL_INTERVAL_MAX_MS,
      ULSA_EVO_I2C_POLL_OVERSAMPLE_FACTOR);

  _pollIntervalMs = nextIntervalMs;
  _stats.pollIntervalMs = _pollIntervalMs;
}

bool UlsaEvoI2cClient::stageConfigRegister(uint8_t reg, uint8_t value) {
  if (!ensureConfigWriteSupported()) {
    return false;
  }

  uint8_t status = 0;
  if (!readRegister8(REG_STATUS, status)) {
    return false;
  }
  uint8_t serviceStatus = 0;
  if (!readRegister8(REG_SERVICE_STATUS, serviceStatus)) {
    return false;
  }
  _stats.lastStatus = status;
  if ((status & (STATUS_CONFIG_ACTIVE | STATUS_EEPROM_BUSY)) != 0 ||
      (serviceStatus & SERVICE_I2C_WRITE_LOCKED) != 0) {
    recordError(ULSA_I2C_ERR_COMMAND_FAILED);
    return false;
  }

  if (!writeRegister8(reg, value)) {
    scheduleBackoff();
    return false;
  }

  uint32_t start = millis();
  uint8_t commandStatus = 0;
  uint8_t lastError = 0;
  bool haveCommandStatus = false;

  while ((uint32_t)(millis() - start) <= STAGE_TIMEOUT_MS) {
    uint8_t readback = 0;
    if (readRegister8(reg, readback) && readback == value) {
      // The STM32 has accepted the draft when its writable register reads back
      // the requested value. CMD_STATUS is useful diagnostics, but a transient
      // follow-up status read must not turn this confirmed write into a failure.
      clearError();
      return true;
    }

    if (readCommandStatus(commandStatus, lastError)) {
      haveCommandStatus = true;
      if (commandStatus == CMD_STATUS_LOCKED ||
          commandStatus == CMD_STATUS_INVALID_REGISTER ||
          commandStatus == CMD_STATUS_INVALID_VALUE ||
          commandStatus == CMD_STATUS_INVALID_COMMAND ||
          commandStatus == CMD_STATUS_EEPROM_BUSY ||
          commandStatus == CMD_STATUS_ERROR) {
        break;
      }
    }

    delay(COMMAND_POLL_INTERVAL_MS);
  }

  if (!haveCommandStatus && !readCommandStatus(commandStatus, lastError)) {
    return false;
  }

  if (commandStatus == CMD_STATUS_OK ||
      commandStatus == CMD_STATUS_REBOOT_REQUIRED) {
    recordError(ULSA_I2C_ERR_COMMAND_TIMEOUT);
    return false;
  }

  if (commandStatus == CMD_STATUS_INVALID_VALUE) {
    recordError(ULSA_I2C_ERR_INVALID_CONFIG_VALUE);
  } else {
    recordError(ULSA_I2C_ERR_COMMAND_FAILED);
  }
  return false;
}

bool UlsaEvoI2cClient::runCommand(uint8_t command, bool useLock, UlsaEvoI2cConfigResult* result) {
  if (result != nullptr) {
    *result = UlsaEvoI2cConfigResult();
  }

  if (!ensureConfigWriteSupported()) {
    return false;
  }

  uint8_t status = 0;
  if (!readRegister8(REG_STATUS, status)) {
    return false;
  }
  uint8_t serviceStatus = 0;
  if (!readRegister8(REG_SERVICE_STATUS, serviceStatus)) {
    return false;
  }
  _stats.lastStatus = status;

  uint8_t initialFlags = 0;
  if (!readRegister8(REG_CFG_FLAGS, initialFlags)) {
    return false;
  }

  uint8_t initialCommandStatus = 0;
  uint8_t initialLastError = 0;
  bool hasInitialCommandResult =
    readCommandStatus(initialCommandStatus, initialLastError);

  const bool useResultSeq = _regVersion >= COMMAND_RESULT_SEQ_REG_VERSION_MIN;
  uint8_t initialResultSeq = 0;
  if (useResultSeq && !readCommandResultSeq(initialResultSeq)) {
    return false;
  }

  if ((status & (STATUS_CONFIG_ACTIVE | STATUS_EEPROM_BUSY)) != 0 ||
      (serviceStatus & SERVICE_I2C_WRITE_LOCKED) != 0) {
    if (result != nullptr) {
      result->commandStatus =
        (status & STATUS_EEPROM_BUSY) ? CMD_STATUS_EEPROM_BUSY : CMD_STATUS_BUSY;
      result->lastError = _stats.lastRemoteError;
      result->flags = initialFlags;
      result->rebootRequired = (initialFlags & CFG_FLAG_REBOOT_REQUIRED) != 0;
    }
    recordError(ULSA_I2C_ERR_COMMAND_FAILED);
    return false;
  }

  if (useLock && !writeRegister8(REG_CMD_LOCK, CMD_LOCK_KEY)) {
    scheduleBackoff();
    return false;
  }

  if (!writeRegister8(REG_CMD, command)) {
    scheduleBackoff();
    return false;
  }

  delay(COMMAND_POLL_INTERVAL_MS);
  return waitForCommandCompletion(command, initialFlags,
                                  useResultSeq, initialResultSeq,
                                  hasInitialCommandResult,
                                  initialCommandStatus, initialLastError,
                                  COMMAND_TIMEOUT_MS, result);
}

bool UlsaEvoI2cClient::waitForCommandCompletion(
    uint8_t command, uint8_t initialFlags, bool useResultSeq,
    uint8_t initialResultSeq, bool hasInitialCommandResult,
    uint8_t initialCommandStatus, uint8_t initialLastError,
    uint32_t timeoutMs, UlsaEvoI2cConfigResult* result) {
  uint32_t start = millis();
  bool sawBusy = false;

  while ((uint32_t)(millis() - start) <= timeoutMs) {
    if (useResultSeq) {
      uint8_t resultSeq = 0;
      if (!readCommandResultSeq(resultSeq)) {
        delay(COMMAND_POLL_INTERVAL_MS);
        continue;
      }
      if (resultSeq == initialResultSeq) {
        delay(COMMAND_POLL_INTERVAL_MS);
        continue;
      }

      uint8_t commandStatus = 0;
      uint8_t lastError = 0;
      uint8_t flags = 0;
      if (!readCommandStatus(commandStatus, lastError) ||
          !readConfigFlags(flags)) {
        delay(COMMAND_POLL_INTERVAL_MS);
        continue;
      }

      if (result != nullptr) {
        result->commandStatus = commandStatus;
        result->lastError = lastError;
        result->flags = flags;
        result->commandResultSeq = resultSeq;
        result->rebootRequired =
          commandStatus == CMD_STATUS_REBOOT_REQUIRED ||
          (flags & CFG_FLAG_REBOOT_REQUIRED) != 0;
      }

      if (commandStatus == CMD_STATUS_OK ||
          commandStatus == CMD_STATUS_REBOOT_REQUIRED) {
        clearError();
        return true;
      }
      if (commandStatus == CMD_STATUS_INVALID_VALUE) {
        recordError(ULSA_I2C_ERR_INVALID_CONFIG_VALUE);
      } else {
        recordError(ULSA_I2C_ERR_COMMAND_FAILED);
      }
      return false;
    }

    uint8_t commandStatus = 0;
    uint8_t lastError = 0;
    uint8_t flags = 0;

    bool haveCommandStatus = readCommandStatus(commandStatus, lastError);
    bool haveFlags = readConfigFlags(flags);

    if (haveCommandStatus && haveFlags) {
      if (result != nullptr) {
        result->commandStatus = commandStatus;
        result->lastError = lastError;
        result->flags = flags;
        result->rebootRequired =
          commandStatus == CMD_STATUS_REBOOT_REQUIRED ||
          (flags & CFG_FLAG_REBOOT_REQUIRED) != 0;
      }

      bool busy =
        commandStatus == CMD_STATUS_BUSY ||
        commandStatus == CMD_STATUS_EEPROM_BUSY ||
        (flags & CFG_FLAG_EEPROM_BUSY) != 0;

      if (busy) {
        sawBusy = true;
      }

      if (!busy) {
        bool wasDirty = (initialFlags & CFG_FLAG_DIRTY) != 0;
        bool dirtyCleared = (flags & CFG_FLAG_DIRTY) == 0;
        bool completionObserved = !hasInitialCommandResult || sawBusy ||
          commandStatus != initialCommandStatus ||
          lastError != initialLastError ||
          ((command == CMD_SAVE_CONFIG || command == CMD_DISCARD_CONFIG) &&
           wasDirty && dirtyCleared);

        // The STM32 applies writes from its main loop. Do not classify an
        // unchanged terminal result from before this command as this command's
        // outcome; wait for its BUSY acknowledgement or a new result instead.
        if (!completionObserved) {
          delay(COMMAND_POLL_INTERVAL_MS);
          continue;
        }

        if (commandStatus == CMD_STATUS_OK ||
            commandStatus == CMD_STATUS_REBOOT_REQUIRED) {
          bool settled = (uint32_t)(millis() - start) >= COMMAND_SETTLE_MS;

          if (!settled ||
              ((command == CMD_SAVE_CONFIG || command == CMD_DISCARD_CONFIG) &&
               wasDirty && !dirtyCleared)) {
            delay(COMMAND_POLL_INTERVAL_MS);
            continue;
          }

          clearError();
          return true;
        }

        if ((uint32_t)(millis() - start) < COMMAND_SETTLE_MS) {
          delay(COMMAND_POLL_INTERVAL_MS);
          continue;
        }

        if (commandStatus == CMD_STATUS_INVALID_VALUE) {
          recordError(ULSA_I2C_ERR_INVALID_CONFIG_VALUE);
        } else {
          recordError(ULSA_I2C_ERR_COMMAND_FAILED);
        }
        return false;
      }
    }

    delay(COMMAND_POLL_INTERVAL_MS);
  }

  recordError(ULSA_I2C_ERR_COMMAND_TIMEOUT);
  return false;
}

bool UlsaEvoI2cClient::readConfigFlags(uint8_t& flags) {
  flags = 0;
  if (!ensureConfigWriteSupported()) {
    return false;
  }
  return readRegister8(REG_CFG_FLAGS, flags);
}

void UlsaEvoI2cClient::recordError(uint8_t error) {
  _stats.lastError = error;
  _stats.lastErrorTime = millis();
  if (error != ULSA_I2C_ERR_NONE &&
      error != ULSA_I2C_ERR_DATA_NOT_READY &&
      error != ULSA_I2C_ERR_DIAG_UNSUPPORTED &&
      error != ULSA_I2C_ERR_CONFIG_UNSUPPORTED &&
      error != ULSA_I2C_ERR_BACKOFF &&
      error != ULSA_I2C_ERR_SUSPENDED) {
    _stats.readErrors++;
    _consecutiveErrors++;
  }
}

void UlsaEvoI2cClient::clearError() {
  _stats.lastError = ULSA_I2C_ERR_NONE;
}

void UlsaEvoI2cClient::scheduleBackoff() {
  if (_consecutiveErrors >= 3) {
    _retryDeadline.schedule(millis(), 1000U);
  }
}

bool UlsaEvoI2cClient::isBackoffActive(uint32_t now) {
  return _retryDeadline.active(now);
}

bool UlsaEvoI2cClient::decodeSnapshot(const uint8_t* info, const uint8_t* snapshot, WindData& data) {
  uint16_t windDir = readU16LE(&snapshot[2]);
  int16_t windSpeedX100 = readI16LE(&snapshot[4]);
  int16_t noseVelX100 = readI16LE(&snapshot[6]);
  uint16_t soundSpeedX10 = readU16LE(&snapshot[8]);
  int16_t temperatureX100 = readI16LE(&snapshot[10]);
  int16_t boardTemperatureX100 = readI16LE(&snapshot[SNAPSHOT_BOARD_TEMP_OFFSET]);

  data.nodeId = info[2];
  data.statusProtocolVersion = WIND_STATUS_PROTOCOL_VERSION;
  data.status = info[0];
  data.serviceStatus = info[STATUS_INFO_SERVICE_OFFSET];
  data.activeCause = info[1];
  data.ntcReadingStatus = info[STATUS_INFO_NTC_OFFSET];
  data.isValid = ((data.status & STATUS_DATA_READY) != 0) &&
                 ((data.status & STATUS_DATA_VALID) != 0);
  data.windDirection = windDir;
  data.windSpeed = windSpeedX100 / 100.0f;
  data.updateAxisWindSpeedsFromPolar();
  data.headingSpeed = noseVelX100 / 100.0f;
  data.soundSpeed = soundSpeedX10 / 10.0f;
  data.temperature = temperatureX100 / 100.0f;
  data.boardTemperatureValid = false;
  data.sourceSequence = readU16LE(&snapshot[0]);
  data.sourceSequenceValid = true;
  const float boardTemperature = boardTemperatureX100 / 100.0f;
  if (boardTemperature >= BOARD_TEMPERATURE_MIN && boardTemperature <= BOARD_TEMPERATURE_MAX) {
    data.boardTemperature = boardTemperature;
    data.boardTemperatureValid = true;
  }
  data.timestamp = millis();

  return data.isDataValid();
}

bool UlsaEvoI2cClient::isValidAvgCycle(uint8_t avgCycle) {
  return avgCycle == 1 ||
         avgCycle == 4 ||
         avgCycle == 8 ||
         avgCycle == 16 ||
         avgCycle == 32 ||
         avgCycle == 64;
}

bool UlsaEvoI2cClient::isValidWindDirInstallMode(uint8_t mode) {
  return mode <= 1;
}

bool UlsaEvoI2cClient::isValidI2cAddress(uint8_t address) {
  return address >= 0x08 && address <= 0x77;
}

uint16_t UlsaEvoI2cClient::readU16LE(const uint8_t* data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

int16_t UlsaEvoI2cClient::readI16LE(const uint8_t* data) {
  return (int16_t)readU16LE(data);
}

uint32_t UlsaEvoI2cClient::readU32LE(const uint8_t* data) {
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) |
         ((uint32_t)data[3] << 24);
}

const char* UlsaEvoI2cClient::errorName(uint8_t error) {
  switch (error) {
    case ULSA_I2C_ERR_NONE: return "NONE";
    case ULSA_I2C_ERR_NO_WIRE: return "NO_WIRE";
    case ULSA_I2C_ERR_NACK: return "NACK";
    case ULSA_I2C_ERR_SHORT_READ: return "SHORT_READ";
    case ULSA_I2C_ERR_INVALID_WHOAMI: return "INVALID_WHOAMI";
    case ULSA_I2C_ERR_UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
    case ULSA_I2C_ERR_DATA_NOT_READY: return "DATA_NOT_READY";
    case ULSA_I2C_ERR_INVALID_DATA: return "INVALID_DATA";
    case ULSA_I2C_ERR_DIAG_UNSUPPORTED: return "DIAG_UNSUPPORTED";
    case ULSA_I2C_ERR_BACKOFF: return "BACKOFF";
    case ULSA_I2C_ERR_CONFIG_UNSUPPORTED: return "CONFIG_UNSUPPORTED";
    case ULSA_I2C_ERR_INVALID_CONFIG_VALUE: return "INVALID_CONFIG_VALUE";
    case ULSA_I2C_ERR_COMMAND_FAILED: return "COMMAND_FAILED";
    case ULSA_I2C_ERR_COMMAND_TIMEOUT: return "COMMAND_TIMEOUT";
    case ULSA_I2C_ERR_SUSPENDED: return "SUSPENDED";
    default: return "UNKNOWN";
  }
}

const char* UlsaEvoI2cClient::remoteErrorName(uint8_t error) {
  switch (error) {
    case 0x00: return "NONE";
    case 0x01: return "READ_TIMEOUT";
    case 0x02: return "UNSUPPORTED_WRITE";
    case 0x03: return "GENERAL_CALL";
    case 0x04: return "BUS_ERROR";
    case 0x05: return "ARBITRATION_LOST";
    case 0x06: return "OVERRUN";
    case 0x07: return "TIMEOUT";
    case 0x08: return "BACKEND_INIT";
    case 0x09: return "INVALID_REGISTER";
    case 0x0A: return "INVALID_VALUE";
    case 0x0B: return "LOCKED";
    case 0x0C: return "WRITE_QUEUE_FULL";
    case 0x0D: return "EEPROM_SAVE_FAILED";
    case 0x0E: return "BOOT_EEPROM_READ";
    case 0x0F: return "BOOT_CRYPTO";
    case 0x10: return "BOOT_IDENTITY";
    default: return "UNKNOWN";
  }
}

const char* UlsaEvoI2cClient::commandStatusName(uint8_t status) {
  switch (status) {
    case CMD_STATUS_OK: return "OK";
    case CMD_STATUS_BUSY: return "BUSY";
    case CMD_STATUS_LOCKED: return "LOCKED";
    case CMD_STATUS_INVALID_REGISTER: return "INVALID_REGISTER";
    case CMD_STATUS_INVALID_VALUE: return "INVALID_VALUE";
    case CMD_STATUS_INVALID_COMMAND: return "INVALID_COMMAND";
    case CMD_STATUS_EEPROM_BUSY: return "EEPROM_BUSY";
    case CMD_STATUS_ERROR: return "ERROR";
    case CMD_STATUS_REBOOT_REQUIRED: return "REBOOT_REQUIRED";
    default: return "UNKNOWN";
  }
}

const char* UlsaEvoI2cClient::wireStatusName(uint8_t status) {
  switch (status) {
    case 0: return "OK";
    case 1: return "DATA_TOO_LONG";
    case 2: return "ADDR_NACK";
    case 3: return "DATA_NACK";
    case 4: return "OTHER_ERROR";
    case 5: return "TIMEOUT";
    case 0xFE: return "DEFERRED_NONSTOP";
    default: return "UNKNOWN";
  }
}
