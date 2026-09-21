/**
 * @file ble_advertising.cpp
 * @brief BLEアドバタイジング・データ更新モジュール
 * @date 2025-12-07
 * 
 * アドバタイジングデータの更新とキャラクタリスティック更新
 */

#include "ble_manager.h"
#include "sd_logger.h"
#include "debug_config.h"
#include "sensor/ulsa_evo_i2c_client.h"
#include "stm32_update/stm32_version_identity.h"
#include <M5Unified.h>
#include <cstring>

// ble_callbacks.cppで定義されているコールバック取得関数
extern NimBLECharacteristicCallbacks* getCurrentTimeCallbacks();

static const uint32_t SAMPLE_METADATA_STALE_MS = 3000;
static const uint32_t RTC_HEALTH_REFRESH_INTERVAL_MS = 1000;

static void putU16LE(uint8_t* data, size_t offset, uint16_t value) {
  data[offset] = (uint8_t)(value & 0xFF);
  data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
}

static void putI16LE(uint8_t* data, size_t offset, int16_t value) {
  putU16LE(data, offset, (uint16_t)value);
}

static void putU32LE(uint8_t* data, size_t offset, uint32_t value) {
  data[offset] = (uint8_t)(value & 0xFF);
  data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
  data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
  data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint16_t clampU32ToU16(uint32_t value) {
  return value > 0xFFFF ? 0xFFFF : (uint16_t)value;
}

// ============================================
// アドバタイジング開始
// ============================================
void BleManager::startAdvertising() {
  _pAdvertising = NimBLEDevice::getAdvertising();
  
  // すでにアドバタイジングがアクティブな場合はスキップ
  if (_pAdvertising->isAdvertising()) {
    LOG_BLE("Advertising already active, skipping start\n");
    return;
  }
  
  // サービスUUIDをアドバタイジングに追加
  _pAdvertising->addServiceUUID((uint16_t)UUID_SERVICE_ENVIRONMENTAL);
  
  // アドバタイズ間隔設定
  _pAdvertising->setMinInterval(BLE_ADV_INTERVAL_MIN / 0.625);  // 0.625ms単位
  _pAdvertising->setMaxInterval(BLE_ADV_INTERVAL_MAX / 0.625);
  
  // 初期のService Data設定
  _serviceData[0] = BLE_ENV_SENSING_UUID & 0xFF;         // Service UUID Low
  _serviceData[1] = (BLE_ENV_SENSING_UUID >> 8) & 0xFF;  // Service UUID High
  _serviceData[2] = _nodeId;                             // Node ID
  _serviceData[3] = 0;                                   // Valid flag + reserved
  // [4-13] は updateAdvertisingData() で設定
  
  _pAdvertising->setServiceData((uint16_t)UUID_SERVICE_ENVIRONMENTAL, std::string((char*)_serviceData, BLE_SERVICE_DATA_SIZE));
  
  // アドバタイジング開始
  _pAdvertising->start();
  LOG_BLE("Advertising started\n");
}

// ============================================
// アドバタイジングデータ更新
// ============================================
void BleManager::updateAdvertisingData(const WindData& data) {
  // Service Data構築
  // [0-1] Service UUID (既に設定済み)
  // [2] Node ID
  _serviceData[2] = _nodeId;
  
  // [3] Valid flag
  _serviceData[3] = data.isValid ? 0x01 : 0x00;
  
  // [4-5] Wind Direction (Little Endian)
  uint16_t windDir = WIND_DIR_TO_BLE(data.windDirection);
  _serviceData[4] = windDir & 0xFF;         // Low byte
  _serviceData[5] = (windDir >> 8) & 0xFF; // High byte
  
  // [6-7] Wind Speed (Little Endian)
  uint16_t windSpeed = WIND_SPEED_TO_BLE(data.windSpeed);
  _serviceData[6] = windSpeed & 0xFF;         // Low byte
  _serviceData[7] = (windSpeed >> 8) & 0xFF; // High byte
  
  // [8-9] Temperature (Little Endian, signed)
  int16_t temp = TEMP_TO_BLE(data.temperature);
  _serviceData[8] = temp & 0xFF;         // Low byte
  _serviceData[9] = (temp >> 8) & 0xFF; // High byte
  
  // [10-11] Sound Speed (Little Endian)
  uint16_t soundSpeed = SOUND_SPEED_TO_BLE(data.soundSpeed);
  _serviceData[10] = soundSpeed & 0xFF;         // Low byte
  _serviceData[11] = (soundSpeed >> 8) & 0xFF; // High byte
  
  // [12-13] Heading Speed (Little Endian, signed)
  int16_t headingSpeed = HEADING_TO_BLE(data.headingSpeed);
  _serviceData[12] = headingSpeed & 0xFF;         // Low byte
  _serviceData[13] = (headingSpeed >> 8) & 0xFF; // High byte
  
  // アドバタイジングデータ更新
  _pAdvertising->setServiceData((uint16_t)UUID_SERVICE_ENVIRONMENTAL, std::string((char*)_serviceData, BLE_SERVICE_DATA_SIZE));
  
  // 接続中でもアドバタイジングを継続
  // これにより接続していないデバイスもセンサーデータを参照可能
  if (!_pAdvertising->isAdvertising() && !_maintenancePeerLocked) {
    _pAdvertising->start();
    LOG_BLE("Advertising restarted with updated data\n");
  }
}

// ============================================
// キャラクタリスティック更新（接続中のクライアント向け）
// ============================================
void BleManager::updateCharacteristics(const WindData& data) {
  uint16_t windDir = WIND_DIR_TO_BLE(data.windDirection);
  _pWindDirChar->setValue(windDir);

  uint16_t windSpeed = WIND_SPEED_TO_BLE(data.windSpeed);
  _pWindSpeedChar->setValue(windSpeed);

  int16_t temp = TEMP_TO_BLE(data.temperature);
  _pTempChar->setValue(temp);

  uint16_t soundSpeed = SOUND_SPEED_TO_BLE(data.soundSpeed);
  _pSoundSpeedChar->setValue(soundSpeed);

  int16_t headingSpeed = HEADING_TO_BLE(data.headingSpeed);
  _pHeadingSpeedChar->setValue(headingSpeed);

  uint8_t axisSpeeds[BLE_WIND_AXIS_SPEEDS_SIZE];
  putI16LE(axisSpeeds, 0, WIND_AXIS_SPEED_TO_BLE(data.windSpeedA));
  putI16LE(axisSpeeds, 2, WIND_AXIS_SPEED_TO_BLE(data.windSpeedB));
  _pWindAxisSpeedsChar->setValue(axisSpeeds, BLE_WIND_AXIS_SPEEDS_SIZE);

  uint8_t status[BLE_SENSOR_STATUS_SIZE] = {
      _nodeId,
      data.isValid ? (uint8_t)1 : (uint8_t)0,
      data.statusProtocolVersion,
      data.status,
      data.serviceStatus,
      data.activeCause,
      data.ntcReadingStatus,
  };
  _pStatusChar->setValue(status, BLE_SENSOR_STATUS_SIZE);

  // Prioritize the three standard measurements. The app commits a live sample
  // on Temperature, so optional diagnostics must never be able to starve it.
  if (_sensorStatusNotifyPending ||
      memcmp(_sensorStatusLastNotified, status, BLE_SENSOR_STATUS_SIZE) != 0) {
    _pStatusChar->notify();
    memcpy(_sensorStatusLastNotified, status, BLE_SENSOR_STATUS_SIZE);
    _sensorStatusNotifyPending = false;
  }
  _pWindDirChar->notify();
  _pWindSpeedChar->notify();
  _pTempChar->notify();
  _pSoundSpeedChar->notify();
  _pHeadingSpeedChar->notify();
  _pWindAxisSpeedsChar->notify();
}

void BleManager::updateSampleMetadata(const WindData& data, bool notify) {
  if (!_pSampleMetadataChar) return;

  uint8_t metadata[BLE_SAMPLE_METADATA_STATUS_SIZE];
  memset(metadata, 0, sizeof(metadata));
  metadata[0] = BLE_SAMPLE_METADATA_PROTOCOL_VERSION;

  if (data.isValid) {
    metadata[1] |= BLE_SAMPLE_METADATA_FLAG_VALID;
  }

  uint16_t seq = ++_localSampleSeq;
  uint8_t source = BLE_SAMPLE_SOURCE_UNKNOWN;
  uint8_t remoteStatus = 0;
  uint8_t remoteError = 0;
  uint8_t localError = ULSA_I2C_ERR_NONE;

  const uint8_t mode = _deviceModeLastStatus[1];
  const uint8_t modeFlags = _deviceModeLastStatus[2];
  const bool i2cMode =
    mode == BLE_DEVICE_MODE_I2C_MEASURE ||
    (modeFlags & BLE_DEVICE_MODE_FLAG_I2C_MEASURE) != 0;
  const bool uartMode =
    mode == BLE_DEVICE_MODE_UART_MEASURE ||
    mode == BLE_DEVICE_MODE_UART_BRIDGE ||
    (modeFlags & BLE_DEVICE_MODE_FLAG_UART_BRIDGE) != 0;

  if (i2cMode && _pI2cConfigClient) {
    const UlsaEvoI2cStats& stats = _pI2cConfigClient->getStats();
    source = BLE_SAMPLE_SOURCE_I2C;
    metadata[1] |= BLE_SAMPLE_METADATA_FLAG_SOURCE_I2C;
    if (stats.lastSeq != 0) {
      seq = stats.lastSeq;
    }
    remoteStatus = stats.lastStatus;
    remoteError = stats.lastRemoteError;
    localError = stats.lastError;
  } else if (uartMode) {
    source = BLE_SAMPLE_SOURCE_UART;
    metadata[1] |= BLE_SAMPLE_METADATA_FLAG_SOURCE_UART;
  }

  const uint32_t now = millis();
  const uint32_t timestamp = data.timestamp != 0 ? data.timestamp : now;
  if (data.timestamp != 0 && (uint32_t)(now - data.timestamp) > SAMPLE_METADATA_STALE_MS) {
    metadata[1] |= BLE_SAMPLE_METADATA_FLAG_STALE;
  }

  putU16LE(metadata, 2, seq);
  putU32LE(metadata, 4, timestamp);
  metadata[8] = source;
  metadata[9] = remoteStatus;
  metadata[10] = remoteError;
  metadata[11] = localError;

  if (memcmp(_sampleMetadataLastStatus, metadata, BLE_SAMPLE_METADATA_STATUS_SIZE) == 0) {
    return;
  }

  memcpy(_sampleMetadataLastStatus, metadata, BLE_SAMPLE_METADATA_STATUS_SIZE);
  _pSampleMetadataChar->setValue(metadata, BLE_SAMPLE_METADATA_STATUS_SIZE);
  if (notify && _connectionCount > 0) {
    _pSampleMetadataChar->notify();
  }
  updateDeviceHealthStatus(false);
}


// ============================================
// Current Time Characteristicを更新
// ============================================
void BleManager::updateCurrentTimeChar() {
  if (!_pRtc || !_pCurrentTimeChar) return;
  RtcTimezoneSnapshot snapshot{};
  (void)_pRtc->getTimezoneSnapshot(snapshot);
  updateCurrentTimeChar(snapshot);
}

void BleManager::updateCurrentTimeChar(const RtcTimezoneSnapshot& snapshot) {
  if (!_pCurrentTimeChar) return;
  if (!snapshot.localValid) {
    // VL/STOP/I2C異常後に以前のCTS値を返さない。空値にして、アプリ側は
    // Device Healthの理由を表示して明示同期へ導く。
    const uint8_t emptyValue = 0;
    _pCurrentTimeChar->setValue(&emptyValue, 0);
    return;
  }
  const RtcDateTime& dt = snapshot.local;
  
  // CTS標準フォーマットに変換
  uint8_t ctsData[CTS_DATA_SIZE];
  ctsData[0] = dt.year & 0xFF;          // Year low
  ctsData[1] = (dt.year >> 8) & 0xFF;   // Year high
  ctsData[2] = dt.month;
  ctsData[3] = dt.day;
  ctsData[4] = dt.hour;
  ctsData[5] = dt.minute;
  ctsData[6] = dt.second;
  
  // PCF8563: 0=日曜...6=土曜 → BLE CTS: 1=月曜...7=日曜
  if (dt.weekday == 0) {
    ctsData[7] = 7;  // 日曜 → 7
  } else {
    ctsData[7] = dt.weekday;  // 1(月)...6(土)はそのまま
  }
  
  ctsData[8] = 0;  // Fractions256
  ctsData[9] = 0;  // Adjust Reason
  
  _pCurrentTimeChar->setValue(ctsData, CTS_DATA_SIZE);
}

void BleManager::updateLocalTimeInfoChar() {
  if (!_pRtc || !_pLocalTimeInfoChar) return;
  RtcTimezoneSnapshot snapshot{};
  (void)_pRtc->getTimezoneSnapshot(snapshot);
  updateLocalTimeInfoChar(snapshot);
}

void BleManager::updateLocalTimeInfoChar(
    const RtcTimezoneSnapshot& snapshot) {
  if (!_pLocalTimeInfoChar) return;
  uint8_t value[CTS_LOCAL_TIME_INFO_SIZE] = {0x80, 0xff};
  if (snapshot.localValid &&
      snapshot.standardOffsetMinutes >= -720 &&
      snapshot.standardOffsetMinutes <= 840 &&
      (snapshot.standardOffsetMinutes % 15) == 0) {
    value[0] = static_cast<uint8_t>(
        static_cast<int8_t>(snapshot.standardOffsetMinutes / 15));
    switch (snapshot.dstOffsetMinutes) {
      case 0: value[1] = 0x00; break;
      case 30: value[1] = 0x02; break;
      case 60: value[1] = 0x04; break;
      case 120: value[1] = 0x08; break;
      default: value[1] = 0xff; break;
    }
  }
  _pLocalTimeInfoChar->setValue(value, sizeof(value));
}

// ============================================
// SDステータスを更新
// ============================================
void BleManager::updateSdStatus(bool notify) {
  if (!_pSdLogger || !_pSdStatusChar) return;
  
  // SDステータスデータを構築
  // [0] state: SD状態
  // [1] usage%: 使用率
  // [2-3] freeSpace MB (uint16 LE)
  // [4-5] totalSpace MB (uint16 LE)
  // [6] cardType
  // [7] reserved
  uint8_t sdData[SD_STATUS_DATA_SIZE];
  
  sdData[0] = (uint8_t)_pSdLogger->getState();
  sdData[1] = _pSdLogger->getUsagePercent();
  
  uint16_t freeMB = clampU32ToU16(_pSdLogger->getFreeSpaceMB());
  sdData[2] = freeMB & 0xFF;
  sdData[3] = (freeMB >> 8) & 0xFF;
  
  uint16_t totalMB = clampU32ToU16(_pSdLogger->getTotalSpaceMB());
  sdData[4] = totalMB & 0xFF;
  sdData[5] = (totalMB >> 8) & 0xFF;
  sdData[6] = _pSdLogger->getCardTypeCode();
  sdData[7] = 0;
  
  _pSdStatusChar->setValue(sdData, SD_STATUS_DATA_SIZE);
  if (notify) {
    _pSdStatusChar->notify();
  }
  updateSdLogDetailStatus(notify);
  updateDeviceHealthStatus(notify);
}

void BleManager::updateSdLogDetailStatus(bool notify) {
  if (!_pSdLogger || !_pSdLogDetailChar) return;

  uint8_t detail[BLE_SD_LOG_DETAIL_STATUS_SIZE];
  memset(detail, 0, sizeof(detail));
  detail[0] = BLE_SD_LOG_DETAIL_PROTOCOL_VERSION;

  uint8_t flags = 0;
  if (_pSdLogger->isAvailable()) {
    flags |= BLE_SD_LOG_DETAIL_FLAG_CARD_AVAILABLE;
  }
  if (_pSdLogger->isLoggingEnabled()) {
    flags |= BLE_SD_LOG_DETAIL_FLAG_LOGGING_ENABLED;
  }
  if (_pSdLogger->canLog()) {
    flags |= BLE_SD_LOG_DETAIL_FLAG_CAN_LOG;
  }
  if (_pSdLogger->isFileOpen()) {
    flags |= BLE_SD_LOG_DETAIL_FLAG_FILE_OPEN;
  }
  if (_pSdLogger->isRtcTimestampingAvailable()) {
    flags |= BLE_SD_LOG_DETAIL_FLAG_RTC_TIMESTAMP;
  }
  if (SdLogWritePolicy::isSlowWriteAdvisory(_pSdLogger->getLastWriteDurationMs())) {
    flags |= BLE_SD_LOG_DETAIL_FLAG_SLOW_WRITE;
  }

  const SdLoggerStopReason stopReason = _pSdLogger->getStopReason();
  if (_pSdLogger->getState() == SD_STATE_MOUNT_ERROR ||
      stopReason == SD_STOP_SLOW_WRITE ||
      stopReason == SD_STOP_WRITE_ERROR ||
      stopReason == SD_STOP_FILE_ERROR || stopReason == SD_STOP_CAPACITY ||
      stopReason == SD_STOP_RETRY_EXHAUSTED) {
    flags |= BLE_SD_LOG_DETAIL_FLAG_ERROR_STOP;
  }

  const uint32_t logCount = _pSdLogger->getLogCount();
  const uint32_t lastLogMillis = _pSdLogger->getLastLogMillis();
  uint16_t lastLogAgeSec = 0xFFFF;
  if (logCount > 0 && lastLogMillis != 0) {
    lastLogAgeSec = clampU32ToU16((millis() - lastLogMillis) / 1000);
  }

  detail[1] = flags;
  detail[2] = (uint8_t)_pSdLogger->getState();
  detail[3] = (uint8_t)stopReason;
  detail[4] = _pSdLogger->getLogRate();
  detail[5] = (_pSdLogger->isRecordingRequested() ? 1U : 0U) |
              (_pSdLogger->isInputPaused() ? 2U : 0U) |
              (_pSdLogger->isRecovering() ? 4U : 0U) |
              (_pSdLogger->isStorageQuiescent() ? 8U : 0U);
  putU32LE(detail, 6, logCount);
  putU32LE(detail, 10, _pSdLogger->getFlushCount());
  putU16LE(detail, 14, _pSdLogger->getBufferedBytes());
  putU16LE(detail, 16, _pSdLogger->getLastWriteDurationMs());
  putU16LE(detail, 18, lastLogAgeSec);
  putU32LE(detail, 20, _pSdLogger->getSyncedLogCount());
  putU32LE(detail, 24, _pSdLogger->getDroppedLogCount());
  putU32LE(detail, 28, _pSdLogger->getUncertainLogCount());
  putU16LE(detail, 32, _pSdLogger->getQueueDepth());

  if (memcmp(_sdLogDetailLastStatus, detail, BLE_SD_LOG_DETAIL_STATUS_SIZE) == 0) {
    return;
  }

  memcpy(_sdLogDetailLastStatus, detail, BLE_SD_LOG_DETAIL_STATUS_SIZE);
  _pSdLogDetailChar->setValue(detail, BLE_SD_LOG_DETAIL_STATUS_SIZE);
  if (notify && _connectionCount > 0) {
    _pSdLogDetailChar->notify(detail, 20);
  }
}

void BleManager::updateCapabilitiesStatus() {
  if (!_pCapabilitiesChar) return;

  uint8_t capabilities[BLE_CAPABILITIES_STATUS_SIZE];
  memset(capabilities, 0, sizeof(capabilities));
  capabilities[0] = BLE_CAPABILITIES_PROTOCOL_VERSION;
  capabilities[1] =
    BLE_CAP_FLAG_DEVICE_INFO |
    BLE_CAP_FLAG_DEVICE_MODE |
    BLE_CAP_FLAG_STM32_FW_VERSION |
    BLE_CAP_FLAG_I2C_CONFIG |
    BLE_CAP_FLAG_SAMPLE_METADATA;
  capabilities[2] =
    BLE_CAP_FLAG2_DEVICE_HEALTH |
    BLE_CAP_FLAG2_CAPABILITIES |
    BLE_CAP_FLAG2_WIND_NOTIFY |
    BLE_CAP_FLAG2_I2C_CONFIG_WRITE;
  capabilities[6] = BLE_CAP_FLAG3_DEVICE_IDENTIFY |
                    BLE_CAP_FLAG3_LED_BRIGHTNESS |
                    BLE_CAP_FLAG3_LED_WIND_REACTIVE |
                    BLE_CAP_FLAG3_OTA_CONTROL |
                    BLE_CAP_FLAG3_RESET_CONTROL |
                    BLE_CAP_FLAG3_STM32_UPDATE_CONTROL;

  if (_pRtc && _pRtc->isAvailable()) {
    capabilities[1] |= BLE_CAP_FLAG_CURRENT_TIME;
    capabilities[2] |= BLE_CAP_FLAG2_RTC_READ_WRITE;
  }
  if (_pRtcTimezoneChar) {
    capabilities[7] |= BLE_CAP_FLAG4_TIMEZONE_CONFIG;
  }

  if (_pSdLogger) {
    capabilities[1] |= BLE_CAP_FLAG_SD_STATUS | BLE_CAP_FLAG_SD_LOG_CONTROL;
    capabilities[2] |= BLE_CAP_FLAG2_SD_LOG_DETAIL |
                       BLE_CAP_FLAG2_SD_LOG_WRITE |
                       BLE_CAP_FLAG2_SD_LOG_SETTINGS;
  }

  capabilities[3] = BLE_INTERFACE_REVISION;
  capabilities[4] = 1000 / BLE_UPDATE_RATE_MS;
  capabilities[5] = BLE_DIAGNOSTIC_POLL_HINT_SEC;

  memcpy(_capabilitiesLastStatus, capabilities, BLE_CAPABILITIES_STATUS_SIZE);
  _pCapabilitiesChar->setValue(capabilities, BLE_CAPABILITIES_STATUS_SIZE);
}

// ============================================
// ESP32動作モードステータスを更新
// ============================================
void BleManager::updateDeviceModeStatus(uint8_t mode, uint8_t flags) {
  if (!_pDeviceModeChar) return;

  uint8_t modeData[BLE_DEVICE_MODE_STATUS_SIZE];
  modeData[0] = BLE_DEVICE_MODE_PROTOCOL_VERSION;
  modeData[1] = mode;
  modeData[2] = flags;
  if (_connectionCount > 0) {
    modeData[2] |= BLE_DEVICE_MODE_FLAG_CONNECTED;
  }
  // Protocol v1 keeps byte 3 for existing app consumers. It reports the fixed
  // released PHY policy; it is not a live controller PHY readback.
  modeData[3] = BLE_DEVICE_MODE_PHY_1M;

  if (memcmp(_deviceModeLastStatus, modeData, BLE_DEVICE_MODE_STATUS_SIZE) == 0) {
    return;
  }

  memcpy(_deviceModeLastStatus, modeData, BLE_DEVICE_MODE_STATUS_SIZE);
  _pDeviceModeChar->setValue(modeData, BLE_DEVICE_MODE_STATUS_SIZE);
  if (_connectionCount > 0) {
    _pDeviceModeChar->notify();
  }
  updateDeviceHealthStatus(true);
}

// ============================================
// STM32 FWバージョンステータスを更新
// ============================================
void BleManager::updateStm32FirmwareVersion(bool notify) {
  memset(_stm32FirmwareVersionLastStatus, 0, sizeof(_stm32FirmwareVersionLastStatus));
  _stm32FirmwareVersionLastStatus[0] = BLE_STM32_FW_VERSION_PROTOCOL_VERSION_V2;

  uint32_t firmwareVersion = 0;
  uint32_t firmwareRevision = 0;
  uint8_t flags = 0;
  uint8_t localError = ULSA_I2C_ERR_NO_WIRE;
  uint8_t regVersion = 0;
  const size_t statusSize = BLE_STM32_FW_VERSION_STATUS_SIZE;

  if (_pI2cConfigClient) {
    flags |= BLE_STM32_FW_VERSION_FLAG_CLIENT_PRESENT;
    const bool versionOk =
        _pI2cConfigClient->readFirmwareVersion(firmwareVersion);
    regVersion = _pI2cConfigClient->getRegVersion();

    stm32_update::Stm32SemanticVersion semanticVersion = {};
    const bool isSemVer = versionOk &&
        stm32_update::decodeStm32VersionCode(firmwareVersion,
                                             semanticVersion);
    const bool identityOk = isSemVer &&
        _pI2cConfigClient->readFirmwareRevision(firmwareRevision) &&
        firmwareRevision >= stm32_update::STM32_MIN_REVISION;
    localError = _pI2cConfigClient->getStats().lastError;

    if (_pI2cConfigClient->isDetected()) {
      flags |= BLE_STM32_FW_VERSION_FLAG_DETECTED;
    }
    if (identityOk) {
      flags |= BLE_STM32_FW_VERSION_FLAG_READ_OK;
    }
  }

  _stm32FirmwareVersionLastStatus[1] = flags;
  _stm32FirmwareVersionLastStatus[2] = localError;
  _stm32FirmwareVersionLastStatus[3] = regVersion;
  _stm32FirmwareVersionLastStatus[4] = (uint8_t)(firmwareVersion & 0xFF);
  _stm32FirmwareVersionLastStatus[5] = (uint8_t)((firmwareVersion >> 8) & 0xFF);
  _stm32FirmwareVersionLastStatus[6] = (uint8_t)((firmwareVersion >> 16) & 0xFF);
  _stm32FirmwareVersionLastStatus[7] = (uint8_t)((firmwareVersion >> 24) & 0xFF);
  putU32LE(_stm32FirmwareVersionLastStatus, 8, firmwareRevision);

  if (_pStm32FirmwareVersionChar) {
    _pStm32FirmwareVersionChar->setValue(_stm32FirmwareVersionLastStatus,
                                         statusSize);
    if (notify && _connectionCount > 0) {
      _pStm32FirmwareVersionChar->notify();
    }
  }
  updateDeviceHealthStatus(notify);
}

void BleManager::updateDeviceHealthStatus(bool notify, bool forceRtcRefresh) {
  if (!_pDeviceHealthChar) return;

  uint8_t health[BLE_DEVICE_HEALTH_STATUS_SIZE];
  memset(health, 0, sizeof(health));
  health[0] = BLE_DEVICE_HEALTH_PROTOCOL_VERSION;

  uint8_t flags = 0;
  if (_connectionCount > 0) {
    flags |= BLE_DEVICE_HEALTH_FLAG_BLE_CONNECTED;
  }

  health[2] = _deviceModeLastStatus[1];
  health[4] = _pI2cConfigClient ? _pI2cConfigClient->getRegVersion() : 0;
  health[5] = _pI2cConfigClient ? _pI2cConfigClient->getStats().lastStatus : 0;
  health[6] = _pI2cConfigClient ? _pI2cConfigClient->getStats().lastRemoteError : 0;
  health[7] = _pI2cConfigClient ? _pI2cConfigClient->getStats().lastError : ULSA_I2C_ERR_NO_WIRE;

  if (_pI2cConfigClient && _pI2cConfigClient->isDetected()) {
    flags |= BLE_DEVICE_HEALTH_FLAG_I2C_DETECTED;
  }
  if ((_i2cConfigLastStatus[5] & UlsaEvoI2cClient::CFG_FLAG_DIRTY) != 0) {
    flags |= BLE_DEVICE_HEALTH_FLAG_CONFIG_DIRTY;
  }
  if ((_i2cConfigLastStatus[5] & UlsaEvoI2cClient::CFG_FLAG_REBOOT_REQUIRED) != 0 ||
      (_i2cConfigLastStatus[15] & 0x01) != 0) {
    flags |= BLE_DEVICE_HEALTH_FLAG_REBOOT_REQUIRED;
  }

  if (_pRtc && _pRtc->isAvailable()) {
    // presentは起動時にPCF8563を検出できたこと、RTC_AVAILABLEは今回の
    // I2C snapshotが読めたことを表す。これで配線未検出と一時的なI2C障害を
    // アプリ側で区別できる。
    health[10] |= BLE_DEVICE_HEALTH_RTC_FLAG_PRESENT;
    const uint32_t now = millis();
    const bool shouldRefreshRtc = forceRtcRefresh || !_rtcHealthStatusInitialized ||
      (uint32_t)(now - _rtcHealthLastReadMs) >= RTC_HEALTH_REFRESH_INTERVAL_MS;
    if (shouldRefreshRtc) {
      RtcStatus rtcStatus{};
      (void)_pRtc->getStatus(rtcStatus);
      _rtcHealthLastStatus = rtcStatus;
      _rtcHealthLastReadMs = now;
      _rtcHealthStatusInitialized = true;
    }
    const RtcStatus& rtcStatus = _rtcHealthLastStatus;
    if (rtcStatus.busReadable) {
      flags |= BLE_DEVICE_HEALTH_FLAG_RTC_AVAILABLE;
    }
    if (rtcStatus.busReadable && !rtcStatus.clockStopped) {
      health[10] |= BLE_DEVICE_HEALTH_RTC_FLAG_RUNNING;
    }
    if (rtcStatus.timeValid) {
      health[10] |= BLE_DEVICE_HEALTH_RTC_FLAG_TIME_VALID;
    }
    if (rtcStatus.voltageLow) {
      health[10] |= BLE_DEVICE_HEALTH_RTC_FLAG_VOLTAGE_LOW;
    }
    if (rtcStatus.clockStopped) {
      health[10] |= BLE_DEVICE_HEALTH_RTC_FLAG_CLOCK_STOPPED;
    }
  }

  if (_pSdLogger) {
    health[8] = (uint8_t)_pSdLogger->getState();
    health[9] = (uint8_t)_pSdLogger->getStopReason();
    if (_pSdLogger->isAvailable()) {
      flags |= BLE_DEVICE_HEALTH_FLAG_SD_AVAILABLE;
    }
    if (_pSdLogger->isLoggingEnabled()) {
      flags |= BLE_DEVICE_HEALTH_FLAG_LOGGING_ENABLED;
    }
    if (_pSdLogger->getState() == SD_STATE_MOUNT_ERROR ||
        _pSdLogger->getStopReason() == SD_STOP_SLOW_WRITE ||
        _pSdLogger->getStopReason() == SD_STOP_WRITE_ERROR ||
        _pSdLogger->getStopReason() == SD_STOP_FILE_ERROR) {
      flags |= BLE_DEVICE_HEALTH_FLAG_ERROR_ACTIVE;
    }
  }

  const bool remoteDataInvalid =
      (health[5] & UlsaEvoI2cClient::STATUS_DATA_READY) != 0 &&
      (health[5] & UlsaEvoI2cClient::STATUS_DATA_VALID) == 0;
  if (remoteDataInvalid ||
      health[6] != 0 ||
      (health[7] != ULSA_I2C_ERR_NONE &&
       health[7] != ULSA_I2C_ERR_DATA_NOT_READY &&
       health[7] != ULSA_I2C_ERR_BACKOFF)) {
    flags |= BLE_DEVICE_HEALTH_FLAG_ERROR_ACTIVE;
  }

  health[1] = flags;

  if (memcmp(_deviceHealthLastStatus, health, BLE_DEVICE_HEALTH_STATUS_SIZE) == 0) {
    return;
  }

  memcpy(_deviceHealthLastStatus, health, BLE_DEVICE_HEALTH_STATUS_SIZE);
  _pDeviceHealthChar->setValue(health, BLE_DEVICE_HEALTH_STATUS_SIZE);
  if (notify && _connectionCount > 0) {
    _pDeviceHealthChar->notify();
  }
}

void BleManager::refreshDeviceModeConnectionFlag() {
  if (!_pDeviceModeChar) return;

  const uint8_t mode = _deviceModeLastStatus[1];
  const uint8_t flags = _deviceModeLastStatus[2] & ~BLE_DEVICE_MODE_FLAG_CONNECTED;
  updateDeviceModeStatus(mode, flags);
}
