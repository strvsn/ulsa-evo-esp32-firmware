/**
 * @file ble_services.cpp
 * @brief BLE GATTサービス構築
 * @date 2025-12-07
 * 
 * Environmental Sensing, ULSA Wind, Device Info, CTS サービスの設定
 * ※アドバタイジング・データ更新は ble_advertising.cpp に分離
 */

#include "ble_manager.h"
#include "sd_logger.h"
#include "system/firmware_identity.h"
#include <M5Unified.h>
#include <cstring>

// ble_callbacks.cppで定義されているコールバック取得関数
extern NimBLECharacteristicCallbacks* getCurrentTimeCallbacks();
extern NimBLECharacteristicCallbacks* getLocalTimeInfoCallbacks();
extern NimBLECharacteristicCallbacks* getRtcTimezoneCallbacks();
extern NimBLECharacteristicCallbacks* getI2cConfigCallbacks();
extern NimBLECharacteristicCallbacks* getSdLogControlCallbacks();
extern NimBLECharacteristicCallbacks* getSdLogDetailCallbacks();
extern NimBLECharacteristicCallbacks* getSdLogSettingsCallbacks();
extern NimBLECharacteristicCallbacks* getSdStatusCallbacks();
extern NimBLECharacteristicCallbacks* getStm32FirmwareVersionCallbacks();
extern NimBLECharacteristicCallbacks* getDeviceIdentifyCallbacks();
extern NimBLECharacteristicCallbacks* getLedBrightnessCallbacks();
extern NimBLECharacteristicCallbacks* getLedWindReactiveCallbacks();
extern NimBLECharacteristicCallbacks* getOtaControlCallbacks();
extern NimBLECharacteristicCallbacks* getStm32UpdateControlCallbacks();
extern NimBLECharacteristicCallbacks* getResetControlCallbacks();

// ============================================
// GATTサービス構築
// ============================================
void BleManager::setupServices() {
  // ============================================
  // Environmental Sensing Service (0x181A)
  // ============================================
  _pEnvService = _pServer->createService((uint16_t)UUID_SERVICE_ENVIRONMENTAL);
  
  // Wind Direction (0x2A73) - Notify対応
  _pWindDirChar = _pEnvService->createCharacteristic(
    (uint16_t)UUID_CHAR_WIND_DIRECTION,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  _pWindDirChar->setValue((uint16_t)0);
  
  // Wind Speed (0x2A72) - Notify対応
  _pWindSpeedChar = _pEnvService->createCharacteristic(
    (uint16_t)UUID_CHAR_WIND_SPEED,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  _pWindSpeedChar->setValue((uint16_t)0);
  
  // Temperature (0x2A6E) - Notify対応
  _pTempChar = _pEnvService->createCharacteristic(
    (uint16_t)UUID_CHAR_TEMPERATURE,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  _pTempChar->setValue((int16_t)0);
  
  _pEnvService->start();
  
  // ============================================
  // Custom ULSA Wind Service
  // ============================================
  _pUlsaService = _pServer->createService(UUID_SERVICE_ULSA_WIND);
  
  // Sound Speed - Notify対応
  _pSoundSpeedChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_SOUND_SPEED,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  _pSoundSpeedChar->setValue((uint16_t)0);
  
  // Heading Speed - Notify対応
  _pHeadingSpeedChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_HEADING_SPEED,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  _pHeadingSpeedChar->setValue((int16_t)0);

  // Wind Axis Speeds - Notify対応
  _pWindAxisSpeedsChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_WIND_AXIS_SPEEDS,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  uint8_t axisSpeeds[BLE_WIND_AXIS_SPEEDS_SIZE] = {0, 0, 0, 0};
  _pWindAxisSpeedsChar->setValue(axisSpeeds, BLE_WIND_AXIS_SPEEDS_SIZE);
  
  // Sensor Status - Read only
  _pStatusChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_SENSOR_STATUS,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  uint8_t status[BLE_SENSOR_STATUS_SIZE] = {
      _nodeId, 0, WIND_STATUS_PROTOCOL_VERSION, 0, 0,
      WIND_CAUSE_NONE, WIND_NTC_READING_NOT_SAMPLED};
  _pStatusChar->setValue(status, BLE_SENSOR_STATUS_SIZE);

  // Sample Metadata - Read/Notify対応
  _pSampleMetadataChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_SAMPLE_METADATA,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  memset(_sampleMetadataLastStatus, 0, sizeof(_sampleMetadataLastStatus));
  _sampleMetadataLastStatus[0] = BLE_SAMPLE_METADATA_PROTOCOL_VERSION;
  _pSampleMetadataChar->setValue(_sampleMetadataLastStatus,
                                 BLE_SAMPLE_METADATA_STATUS_SIZE);

  
  // SD Status - Read/Notify対応
  if (_pSdLogger) {
    _pSdStatusChar = _pUlsaService->createCharacteristic(
      UUID_CHAR_SD_STATUS,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    _pSdStatusChar->setCallbacks(getSdStatusCallbacks());
    // 初期値を設定
    updateSdStatus(false);

    // SD Log Control - Read/Write/Notify対応
    _pSdLogControlChar = _pUlsaService->createCharacteristic(
      UUID_CHAR_SD_LOG_CONTROL,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    _pSdLogControlChar->setCallbacks(getSdLogControlCallbacks());
    publishSdLogControlStatus(BLE_SD_LOG_OP_READ,
                              BLE_SD_LOG_RESULT_OK);

    // SD Log Detail - Read/Notify対応
    _pSdLogDetailChar = _pUlsaService->createCharacteristic(
      UUID_CHAR_SD_LOG_DETAIL,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    _pSdLogDetailChar->setCallbacks(getSdLogDetailCallbacks());
    updateSdLogDetailStatus(false);

    // SD Log Settings - Read/Write/Notify対応
    _pSdLogSettingsChar = _pUlsaService->createCharacteristic(
      UUID_CHAR_SD_LOG_SETTINGS,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    _pSdLogSettingsChar->setCallbacks(getSdLogSettingsCallbacks());
    publishSdLogSettingsStatus(BLE_SD_LOG_SETTINGS_OP_READ,
                               BLE_SD_LOG_SETTINGS_RESULT_OK);
  }

  // ESP32 Device Mode - Read/Notify対応
  _pDeviceModeChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_DEVICE_MODE,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  updateDeviceModeStatus(BLE_DEVICE_MODE_I2C_MEASURE,
                         BLE_DEVICE_MODE_FLAG_I2C_MEASURE);

  // STM32 Firmware Version - Read/Notify対応
  _pStm32FirmwareVersionChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_STM32_FIRMWARE_VERSION,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  _pStm32FirmwareVersionChar->setCallbacks(getStm32FirmwareVersionCallbacks());
  updateStm32FirmwareVersion(false);

  // I2C Config Control/Status - Read/Write/Notify対応
  _pI2cConfigChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_I2C_CONFIG_CONTROL,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  _pI2cConfigChar->setCallbacks(getI2cConfigCallbacks());
  publishI2cConfigStatus(BLE_I2C_CONFIG_OP_READ_CONFIG,
                         _pI2cConfigClient ? BLE_I2C_CONFIG_RESULT_OK
                                           : BLE_I2C_CONFIG_RESULT_UNAVAILABLE,
                         _i2cConfigOperationSeq);

  // Device Health Summary - Read/Notify対応
  _pDeviceHealthChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_DEVICE_HEALTH,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  updateDeviceHealthStatus(false);

  // RTC Timezone Control is a Demo-only GATT surface. RtcManager itself is
  // shared with Initial, but Initial does not compile any BLE source.
  if (_pRtc) {
    _pRtcTimezoneChar = _pUlsaService->createCharacteristic(
      UUID_CHAR_RTC_TIMEZONE_CONTROL,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    _pRtcTimezoneChar->setCallbacks(getRtcTimezoneCallbacks());
    publishRtcTimezoneStatus(0, BLE_RTC_TIMEZONE_RESULT_OK, false, false);
  }

  // BLE Capabilities - Read only
  _pCapabilitiesChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_CAPABILITIES,
    NIMBLE_PROPERTY::READ
  );
  updateCapabilitiesStatus();

  // Device Identify - Write only
  _pDeviceIdentifyChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_DEVICE_IDENTIFY,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  _pDeviceIdentifyChar->setCallbacks(getDeviceIdentifyCallbacks());

  // LED Brightness - Read/Write/Notify対応
  _pLedBrightnessChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_LED_BRIGHTNESS,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  _pLedBrightnessChar->setCallbacks(getLedBrightnessCallbacks());
  publishLedBrightnessStatus(false);

  // LED Wind Reactive - Read/Write/Notify対応
  _pLedWindReactiveChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_LED_WIND_REACTIVE,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  _pLedWindReactiveChar->setCallbacks(getLedWindReactiveCallbacks());
  publishLedWindReactiveStatus(BLE_LED_WIND_REACTIVE_OP_READ,
                                BLE_LED_WIND_REACTIVE_RESULT_OK,
                                false);

  // OTA Control - Read/Write/Notify対応
  _pOtaControlChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_OTA_CONTROL,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  _pOtaControlChar->setCallbacks(getOtaControlCallbacks());
  publishOtaControlStatus(BLE_OTA_OP_READ, BLE_OTA_RESULT_OK, false);

  // STM32 Update Control - Read/Write/Notify対応
  _pStm32UpdateControlChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_STM32_UPDATE_CONTROL,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  _pStm32UpdateControlChar->setCallbacks(getStm32UpdateControlCallbacks());
  publishStm32UpdateControlStatus(
    BLE_STM32_UPDATE_OP_READ,
    BLE_STM32_UPDATE_RESULT_OK,
    false);

  // Reset Control - Read/Write/Notify対応
  _pResetControlChar = _pUlsaService->createCharacteristic(
    UUID_CHAR_RESET_CONTROL,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  _pResetControlChar->setCallbacks(getResetControlCallbacks());
  publishResetControlStatus(BLE_RESET_OP_READ, BLE_RESET_RESULT_OK, 0, false);
  
  _pUlsaService->start();
  
  // ============================================
  // Device Information Service (0x180A)
  // ファームウェアバージョン等のデバイス情報
  // ============================================
  _pDevInfoService = _pServer->createService((uint16_t)UUID_SERVICE_DEVICE_INFO);
  
  // Firmware Revision String (0x2A26)
  _pFirmwareRevChar = _pDevInfoService->createCharacteristic(
    (uint16_t)UUID_CHAR_FIRMWARE_REV,
    NIMBLE_PROPERTY::READ
  );
  const Esp32FirmwareIdentityDescriptor& firmwareIdentity = getEsp32FirmwareIdentity();
  _pFirmwareRevChar->setValue(
    reinterpret_cast<const uint8_t*>(firmwareIdentity.version),
    std::strlen(firmwareIdentity.version)
  );

  // Software Revision String (0x2A28)
  _pSoftwareRevChar = _pDevInfoService->createCharacteristic(
    (uint16_t)UUID_CHAR_SOFTWARE_REV,
    NIMBLE_PROPERTY::READ
  );
  char softwareRevision[80];
  snprintf(softwareRevision, sizeof(softwareRevision),
           "r%lu.g%.12s.%s%s",
           (unsigned long)firmwareIdentity.revision,
           firmwareIdentity.sourceCommit,
           ULSA_FIRMWARE_PROFILE_NAME,
           (firmwareIdentity.flags & ULSA_EVO_ESP32_IDENTITY_FLAG_DIRTY)
               ? ".dirty"
               : "");
  _pSoftwareRevChar->setValue(
    reinterpret_cast<const uint8_t*>(softwareRevision),
    std::strlen(softwareRevision)
  );
  
  // Manufacturer Name String (0x2A29)
  _pManufacturerChar = _pDevInfoService->createCharacteristic(
    (uint16_t)UUID_CHAR_MANUFACTURER_NAME,
    NIMBLE_PROPERTY::READ
  );
  _pManufacturerChar->setValue(
    reinterpret_cast<const uint8_t*>(ULSA_EVO_MANUFACTURER_NAME),
    std::strlen(ULSA_EVO_MANUFACTURER_NAME)
  );
  
  // Model Number String (0x2A24)
  _pModelNumberChar = _pDevInfoService->createCharacteristic(
    (uint16_t)UUID_CHAR_MODEL_NUMBER,
    NIMBLE_PROPERTY::READ
  );
  _pModelNumberChar->setValue(
    reinterpret_cast<const uint8_t*>(ULSA_EVO_MODEL_NUMBER),
    std::strlen(ULSA_EVO_MODEL_NUMBER)
  );
  
  _pDevInfoService->start();
  // M5.Log.println("[BLE] Device Info Service configured");
  
  // ============================================
  // Current Time Service (0x1805)
  // セントラルからの時刻同期用
  // ============================================
  // RTCが物理的に検出できた場合だけCTSを公開する。時刻が無効な場合も
  // serviceは残し、アプリからの明示同期で復旧できるようにする。
  if (_pRtc && _pRtc->isAvailable()) {
    _pCtsService = _pServer->createService((uint16_t)UUID_SERVICE_CURRENT_TIME);
    
    // Current Time (0x2A2B) - Read/Write/Notify対応
    // セントラルから書き込まれた時刻をRTCに設定
    _pCurrentTimeChar = _pCtsService->createCharacteristic(
      (uint16_t)UUID_CHAR_CURRENT_TIME,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    
    // 初期値をRTCから読み取って設定
    updateCurrentTimeChar();
    
    // 書き込みコールバック設定
    _pCurrentTimeChar->setCallbacks(getCurrentTimeCallbacks());

    _pLocalTimeInfoChar = _pCtsService->createCharacteristic(
      (uint16_t)UUID_CHAR_LOCAL_TIME_INFORMATION,
      NIMBLE_PROPERTY::READ
    );
    _pLocalTimeInfoChar->setCallbacks(getLocalTimeInfoCallbacks());
    updateLocalTimeInfoChar();
    
    _pCtsService->start();
    // M5.Log.println("[BLE] CTS Service configured");
  }
  
  // M5.Log.println("[BLE] Services configured");
}
