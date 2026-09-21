/**
 * @file ble_manager.cpp
 * @brief BLE通信管理モジュール - コアロジック
 * @date 2025-12-07
 * 
 * BleManagerクラスの初期化・停止・データ更新ロジック
 * ※コールバックは ble_callbacks.cpp に分離
 * ※サービス構築は ble_services.cpp に分離
 */

#include "ble_manager.h"
#include "sd_logger.h"
#include "debug_config.h"
#include <M5Unified.h>

#if !defined(MYNEWT_VAL_BLE_LL_CFG_FEAT_LE_2M_PHY) || \
    !defined(MYNEWT_VAL_BLE_LL_CFG_FEAT_LE_CODED_PHY)
#error "NimBLE PHY feature configuration is unavailable"
#endif

#if MYNEWT_VAL_BLE_LL_CFG_FEAT_LE_2M_PHY || \
    MYNEWT_VAL_BLE_LL_CFG_FEAT_LE_CODED_PHY
#error "ULSA EVO release firmware requires the NimBLE 1M-only PHY contract"
#endif

// ble_callbacks.cppで定義されているコールバック取得関数
extern NimBLEServerCallbacks* getServerCallbacks();
extern NimBLESecurityCallbacks* getSecurityCallbacks();

// ============================================
// BleManager 実装
// ============================================

BleManager::BleManager()
  : _pServer(nullptr)
  , _pAdvertising(nullptr)
  , _pEnvService(nullptr)
  , _pWindDirChar(nullptr)
  , _pWindSpeedChar(nullptr)
  , _pTempChar(nullptr)
  , _pUlsaService(nullptr)
  , _pSoundSpeedChar(nullptr)
  , _pHeadingSpeedChar(nullptr)
  , _pWindAxisSpeedsChar(nullptr)
  , _pStatusChar(nullptr)
  , _pCtsService(nullptr)
  , _pCurrentTimeChar(nullptr)
  , _pLocalTimeInfoChar(nullptr)
  , _pDevInfoService(nullptr)
  , _pFirmwareRevChar(nullptr)
  , _pSoftwareRevChar(nullptr)
  , _pManufacturerChar(nullptr)
  , _pModelNumberChar(nullptr)
  , _pSdStatusChar(nullptr)
  , _pSdLogControlChar(nullptr)
  , _pSdLogDetailChar(nullptr)
  , _pSdLogSettingsChar(nullptr)
  , _pDeviceModeChar(nullptr)
  , _pStm32FirmwareVersionChar(nullptr)
  , _pI2cConfigChar(nullptr)
  , _pSampleMetadataChar(nullptr)
  , _pDeviceHealthChar(nullptr)
  , _pCapabilitiesChar(nullptr)
  , _pDeviceIdentifyChar(nullptr)
  , _pLedBrightnessChar(nullptr)
  , _pLedWindReactiveChar(nullptr)
  , _pOtaControlChar(nullptr)
  , _pStm32UpdateControlChar(nullptr)
  , _pResetControlChar(nullptr)
  , _pRtcTimezoneChar(nullptr)
  , _pRtc(nullptr)
  , _rtcHealthLastStatus{}
  , _rtcHealthLastReadMs(0)
  , _rtcHealthStatusInitialized(false)
  , _pSdLogger(nullptr)
  , _pI2cConfigClient(nullptr)
  , _pOtaManager(nullptr)
  , _i2cConfigRequestPending(false)
  , _i2cConfigRequestOp(0)
  , _i2cConfigRequestValue(0)
  , _i2cConfigRequestOperationSeq(0)
  , _i2cConfigOperationSeq(0)
  , _sdLogControlRequestPending(false)
  , _sdLogControlRequestOp(0)
  , _sdLogSettingsRequestPending(false)
  , _sdLogSettingsRequestOp(0)
  , _sdLogSettingsRequestIntervalMs(0)
  , _sdLogSettingsRequestAutoStartEnabled(false)
  , _deviceIdentifyRequestPending(false)
  , _deviceIdentifyRequestConnectionSequence(0)
  , _connectionEventSequence(0)
  , _ledBrightnessRequestPending(false)
  , _ledBrightnessRequestValue(0)
  , _ledWindReactiveRequestPending(false)
  , _ledWindReactiveRequestEnabled(false)
  , _ledWindReactiveRequestTheme(0)
  , _otaControlRequestPending(false)
  , _otaControlRequestOp(0)
  , _otaControlRequestPeerHandle(0xffffU)
  , _otaControlLastStatusSize(0)
  , _stm32UpdateControlRequestPending(false)
  , _stm32UpdateControlRequest()
  , _stm32UpdateControlLastStatusSize(0)
  , _resetControlRequestPending(false)
  , _resetControlRequestOp(0)
  , _rtcTimezoneRequestPending(false)
  , _rtcTimezoneProcessing(false)
  , _rtcTimezoneRequest()
  , _rtcTimezoneLastOp(0)
  , _rtcTimezoneLastResult(BLE_RTC_TIMEZONE_RESULT_OK)
  , _rtcTimezoneOperationGeneration(0)
  , _rtcTimeCacheLastRefreshMs(0)
  , _localSampleSeq(0)
  , _nodeId(0)
  , _connectionCount(0)
  , _maintenancePeerLocked(false)
  , _maintenancePeerHandle(0xffffU)
  , _disconnectedPeerPending(false)
  , _disconnectedPeerHandle(0xffffU)
  , _running(false) {
  memset(_deviceName, 0, sizeof(_deviceName));
  memset(_serviceData, 0, sizeof(_serviceData));
  memset(_i2cConfigLastStatus, 0, sizeof(_i2cConfigLastStatus));
  memset(_sdLogControlLastStatus, 0, sizeof(_sdLogControlLastStatus));
  memset(_sdLogDetailLastStatus, 0, sizeof(_sdLogDetailLastStatus));
  memset(_sdLogSettingsLastStatus, 0, sizeof(_sdLogSettingsLastStatus));
  memset(_capabilitiesLastStatus, 0, sizeof(_capabilitiesLastStatus));
  memset(_deviceModeLastStatus, 0, sizeof(_deviceModeLastStatus));
  memset(_stm32FirmwareVersionLastStatus, 0, sizeof(_stm32FirmwareVersionLastStatus));
  memset(_sampleMetadataLastStatus, 0, sizeof(_sampleMetadataLastStatus));
  memset(_sensorStatusLastNotified, 0, sizeof(_sensorStatusLastNotified));
  _sensorStatusNotifyPending = true;
  memset(_deviceHealthLastStatus, 0, sizeof(_deviceHealthLastStatus));
  _rtcHealthLastStatus = {};
  _rtcHealthLastReadMs = 0;
  _rtcHealthStatusInitialized = false;
  memset(_otaControlLastStatus, 0, sizeof(_otaControlLastStatus));
  memset(_stm32UpdateControlLastStatus, 0, sizeof(_stm32UpdateControlLastStatus));
  memset(_resetControlLastStatus, 0, sizeof(_resetControlLastStatus));
  memset(_rtcTimezoneLastStatus, 0, sizeof(_rtcTimezoneLastStatus));
  portMUX_INITIALIZE(&_i2cConfigMux);
  portMUX_INITIALIZE(&_sdLogControlMux);
  portMUX_INITIALIZE(&_sdLogSettingsMux);
  portMUX_INITIALIZE(&_deviceIdentifyMux);
  portMUX_INITIALIZE(&_ledBrightnessMux);
  portMUX_INITIALIZE(&_ledWindReactiveMux);
  portMUX_INITIALIZE(&_otaControlMux);
  portMUX_INITIALIZE(&_stm32UpdateControlMux);
  portMUX_INITIALIZE(&_resetControlMux);
  portMUX_INITIALIZE(&_rtcTimezoneMux);
}

bool BleManager::begin(uint8_t nodeId, RtcManager* pRtc, SdLogger* pSdLogger) {
  if (_running) {
    _pRtc = pRtc;
    _pSdLogger = pSdLogger;
    return updateNodeId(nodeId);
  }
  _nodeId = nodeId;
  _pRtc = pRtc;
  _pSdLogger = pSdLogger;
  clearI2cConfigRequest();
  clearSdLogControlRequest();
  clearSdLogSettingsRequest();
  clearLedBrightnessRequest();
  clearLedWindReactiveRequest();
  clearOtaControlRequest();
  clearStm32UpdateControlRequest();
  clearResetControlRequest();
  clearRtcTimezoneRequest();
  _maintenancePeerLocked = false;
  _maintenancePeerHandle = 0xffffU;
  _disconnectedPeerPending = false;
  _disconnectedPeerHandle = 0xffffU;
  _deviceIdentifyRequestPending = false;
  memset(_sdLogControlLastStatus, 0, sizeof(_sdLogControlLastStatus));
  memset(_sdLogDetailLastStatus, 0, sizeof(_sdLogDetailLastStatus));
  memset(_sdLogSettingsLastStatus, 0, sizeof(_sdLogSettingsLastStatus));
  memset(_capabilitiesLastStatus, 0, sizeof(_capabilitiesLastStatus));
  memset(_deviceModeLastStatus, 0, sizeof(_deviceModeLastStatus));
  memset(_stm32FirmwareVersionLastStatus, 0, sizeof(_stm32FirmwareVersionLastStatus));
  memset(_sampleMetadataLastStatus, 0, sizeof(_sampleMetadataLastStatus));
  memset(_sensorStatusLastNotified, 0, sizeof(_sensorStatusLastNotified));
  _sensorStatusNotifyPending = true;
  memset(_deviceHealthLastStatus, 0, sizeof(_deviceHealthLastStatus));
  _rtcHealthLastStatus = {};
  _rtcHealthLastReadMs = 0;
  _rtcHealthStatusInitialized = false;
  memset(_otaControlLastStatus, 0, sizeof(_otaControlLastStatus));
  memset(_stm32UpdateControlLastStatus, 0, sizeof(_stm32UpdateControlLastStatus));
  memset(_resetControlLastStatus, 0, sizeof(_resetControlLastStatus));
  memset(_rtcTimezoneLastStatus, 0, sizeof(_rtcTimezoneLastStatus));
  _rtcTimezoneLastOp = 0;
  _rtcTimezoneLastResult = BLE_RTC_TIMEZONE_RESULT_OK;
  _rtcTimezoneOperationGeneration = 0;
  _rtcTimeCacheLastRefreshMs = 0;
  _otaControlLastStatusSize = 0;
  _stm32UpdateControlLastStatusSize = 0;
  _localSampleSeq = 0;
  g_pBleManager = this;
  
  // デバイス名を生成 "ULSA EVO #XXX"
  snprintf(_deviceName, sizeof(_deviceName), "%s%d", BLE_DEVICE_NAME_PREFIX, nodeId);
  
  // M5.Log.printf("[BLE] Initializing as '%s'\n", _deviceName);
  // if (_pRtc) {
  //   M5.Log.println("[BLE] CTS (Current Time Service) enabled");
  // }
  // if (_pSdLogger) {
  //   M5.Log.println("[BLE] SD Status notification enabled");
  // }
  
  // NimBLE初期化
  NimBLEDevice::init(_deviceName);
  
  // ESP32-C3-MINI-1の日本向け認証範囲内で送信電力を固定する。
  NimBLEDevice::setPower(ESP_PWR_LVL_P6);  // +6 dBm
  
  // ============================================
  // セキュリティ/ボンディング設定
  // ============================================
  // セキュリティコールバック登録（ble_callbacks.cppで定義）
  if (BLE_BONDING_ENABLED) {
    NimBLEDevice::setSecurityCallbacks(getSecurityCallbacks());
  }
  
  // セキュリティ設定:
  // - BLE_SM_PAIR_AUTHREQ_BOND: ボンディング情報を保存
  // - BLE_SM_PAIR_AUTHREQ_SC: LE Secure Connections使用
  // MITM保護なし = Just Works方式（PINコード不要）
  NimBLEDevice::setSecurityAuth(BLE_BONDING_ENABLED, BLE_MITM_PROTECTION, BLE_SECURE_CONNECTION);
  
  // IO Capability: NoInputNoOutput（ディスプレイ・キーパッドなし）
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  
  M5.Log.printf("[BLE] Security: Bonding=%s, MITM=%s, SC=%s\n",
    BLE_BONDING_ENABLED ? "ON" : "OFF",
    BLE_MITM_PROTECTION ? "ON" : "OFF",
    BLE_SECURE_CONNECTION ? "ON" : "OFF");
  
  // サーバー作成（コールバックはble_callbacks.cppで定義）
  _pServer = NimBLEDevice::createServer();
  // getServerCallbacks() returns a static object. The server must not delete
  // it when a portal transition tears down the GATT database.
  _pServer->setCallbacks(getServerCallbacks(), false);
  // Our onDisconnect callback restarts advertising only while _running.
  _pServer->advertiseOnDisconnect(false);
  
  // サービス/キャラクタリスティック設定（ble_services.cppで実装）
  setupServices();
  
  // アドバタイジング開始（ble_services.cppで実装）
  _running = true;
  startAdvertising();
  M5.Log.println("[BLE] Started advertising");
  return true;
}

bool BleManager::restartWithLastVerifiedNodeId() {
  if (_running) {
    return true;
  }

  // _nodeId is the last-known STM32-backed user label. Keep advertising
  // available while STM32 is still booting; OTA authorization does not depend
  // on refreshing this label.
  return begin(_nodeId, _pRtc, _pSdLogger);
}

// ============================================
// ノードID動的更新
// ============================================
bool BleManager::updateNodeId(uint8_t nodeId) {
  // 変更がない場合はスキップ
  if (nodeId == _nodeId) {
    return true;
  }
  
  uint8_t oldNodeId = _nodeId;
  _nodeId = nodeId;
  
  // デバイス名を更新
  snprintf(_deviceName, sizeof(_deviceName), "%s%d", BLE_DEVICE_NAME_PREFIX, nodeId);
  
  M5.Log.printf("[BLE] Node ID updated: %d -> %d ('%s')\n", oldNodeId, nodeId, _deviceName);
  
  // BLEが動作中の場合はデバイス名とアドバタイジングを更新
  if (_running && _pAdvertising) {
    // アドバタイジング一時停止
    _pAdvertising->stop();
    
    // NimBLE内部のGAPデバイス名を更新
    NimBLEDevice::setDeviceName(std::string(_deviceName));
    
    // 広告データを完全にリセットして再構築する
    // ※ NimBLEはスキャンレスポンスの Complete Local Name を
    //    初回設定時の長さでキャッシュするため、名前の文字数が
    //    変わった場合（例: "#0"→"#10"）に切り詰めが発生する。
    //    reset()でキャッシュをリセットし、再構築することで
    //    正しい長さの名前が反映される。
    _pAdvertising->reset();
    // NimBLE reset() preserves the service UUID list, so clear it before rebuilding.
    _pAdvertising->removeServices();
    
    // サービスUUIDを再追加
    _pAdvertising->addServiceUUID((uint16_t)UUID_SERVICE_ENVIRONMENTAL);
    
    // アドバタイズ間隔を再設定
    _pAdvertising->setMinInterval(BLE_ADV_INTERVAL_MIN / 0.625);
    _pAdvertising->setMaxInterval(BLE_ADV_INTERVAL_MAX / 0.625);
    
    // デバイス名をスキャンレスポンスに明示的に設定
    _pAdvertising->setName(std::string(_deviceName));
    
    // Service Data内のノードIDを更新して再設定
    _serviceData[2] = nodeId;
    _pAdvertising->setServiceData(
      (uint16_t)UUID_SERVICE_ENVIRONMENTAL, 
      std::string((char*)_serviceData, BLE_SERVICE_DATA_SIZE)
    );
    
    // アドバタイジング再開（新しいデバイス名で発信）
    if (!_maintenancePeerLocked) {
      _pAdvertising->start();
    }
    
    M5.Log.printf("[BLE] Device name updated to '%s'\n", _deviceName);
  }

  if (_pStatusChar != nullptr) {
    const bool valid = (_serviceData[3] & 0x01) != 0;
    uint8_t status[BLE_SENSOR_STATUS_SIZE] = {
        nodeId,
        valid ? (uint8_t)1 : (uint8_t)0,
        WIND_STATUS_PROTOCOL_VERSION,
        valid ? (uint8_t)(WIND_STATUS_DATA_READY | WIND_STATUS_DATA_VALID)
              : (uint8_t)0,
        0,
        WIND_CAUSE_NONE,
        WIND_NTC_READING_NOT_SAMPLED,
    };
    _pStatusChar->setValue(status, sizeof(status));
  }
  
  return true;
}

// ============================================
// BLE停止（リソース解放）
// ============================================
void BleManager::stop() {
  if (!_running) return;
  
  // 最初に停止フラグを立てる（他のタスクからの呼び出しを防ぐ）
  _running = false;
  delay(10);  // 他のタスクが停止を認識するまで待つ
  
  // M5.Log.println("[BLE] Stopping...");
  
  // 接続中のクライアントを切断
  if (_pServer && _pServer->getConnectedCount() > 0) {
    // M5.Log.println("[BLE] Disconnecting clients...");
    // 全てのクライアントを切断
    const auto peers = _pServer->getPeerDevices();
    for (const uint16_t peerHandle : peers) {
      _pServer->disconnect(peerHandle);
    }
    delay(100);  // 切断処理の完了を待つ
  }
  
  // アドバタイジング停止
  if (_pAdvertising) {
    _pAdvertising->stop();
    delay(50);  // 停止処理の完了を待つ
  }
  
  // Every begin() builds a fresh GATT graph, so stop() must destroy the old
  // graph. deinit(false) retained duplicate UUIDs and left clients subscribed
  // to old characteristics which updateWindData() no longer updated.
  // Security callbacks are also borrowed static objects; NimBLE 1.x otherwise
  // deletes them during clearAll. They are installed again by begin() if used.
  NimBLEDevice::setSecurityCallbacks(nullptr);
  NimBLEDevice::deinit(true);
  delay(100);  // deinit処理の完了を待つ
  
  // 全GATT objectが解放されたので借用pointerをすべて無効化する。
  _pServer = nullptr;
  _pAdvertising = nullptr;
  _pEnvService = nullptr;
  _pWindDirChar = nullptr;
  _pWindSpeedChar = nullptr;
  _pTempChar = nullptr;
  _pUlsaService = nullptr;
  _pSoundSpeedChar = nullptr;
  _pHeadingSpeedChar = nullptr;
  _pWindAxisSpeedsChar = nullptr;
  _pStatusChar = nullptr;
  _pCtsService = nullptr;
  _pCurrentTimeChar = nullptr;
  _pLocalTimeInfoChar = nullptr;
  _pDevInfoService = nullptr;
  _pFirmwareRevChar = nullptr;
  _pSoftwareRevChar = nullptr;
  _pManufacturerChar = nullptr;
  _pModelNumberChar = nullptr;
  _pSdStatusChar = nullptr;
  _pSdLogControlChar = nullptr;
  _pSdLogDetailChar = nullptr;
  _pSdLogSettingsChar = nullptr;
  _pDeviceModeChar = nullptr;
  _pStm32FirmwareVersionChar = nullptr;
  _pI2cConfigChar = nullptr;
  _pSampleMetadataChar = nullptr;
  _pDeviceHealthChar = nullptr;
  _pCapabilitiesChar = nullptr;
  _pDeviceIdentifyChar = nullptr;
  _pLedBrightnessChar = nullptr;
  _pLedWindReactiveChar = nullptr;
  _pOtaControlChar = nullptr;
  _pStm32UpdateControlChar = nullptr;
  _pResetControlChar = nullptr;
  _pRtcTimezoneChar = nullptr;
  clearI2cConfigRequest();
  clearSdLogControlRequest();
  clearSdLogSettingsRequest();
  clearLedBrightnessRequest();
  clearLedWindReactiveRequest();
  clearOtaControlRequest();
  clearStm32UpdateControlRequest();
  clearResetControlRequest();
  clearRtcTimezoneRequest();
  portENTER_CRITICAL(&_deviceIdentifyMux);
  _deviceIdentifyRequestPending = false;
  portEXIT_CRITICAL(&_deviceIdentifyMux);
  memset(_sdLogControlLastStatus, 0, sizeof(_sdLogControlLastStatus));
  memset(_sdLogDetailLastStatus, 0, sizeof(_sdLogDetailLastStatus));
  memset(_capabilitiesLastStatus, 0, sizeof(_capabilitiesLastStatus));
  memset(_deviceModeLastStatus, 0, sizeof(_deviceModeLastStatus));
  memset(_stm32FirmwareVersionLastStatus, 0, sizeof(_stm32FirmwareVersionLastStatus));
  memset(_sampleMetadataLastStatus, 0, sizeof(_sampleMetadataLastStatus));
  memset(_deviceHealthLastStatus, 0, sizeof(_deviceHealthLastStatus));
  _rtcHealthLastStatus = {};
  _rtcHealthLastReadMs = 0;
  _rtcHealthStatusInitialized = false;
  memset(_otaControlLastStatus, 0, sizeof(_otaControlLastStatus));
  memset(_stm32UpdateControlLastStatus, 0, sizeof(_stm32UpdateControlLastStatus));
  memset(_resetControlLastStatus, 0, sizeof(_resetControlLastStatus));
  memset(_rtcTimezoneLastStatus, 0, sizeof(_rtcTimezoneLastStatus));
  _rtcTimezoneLastOp = 0;
  _rtcTimezoneLastResult = BLE_RTC_TIMEZONE_RESULT_OK;
  _rtcTimeCacheLastRefreshMs = 0;
  _otaControlLastStatusSize = 0;
  _stm32UpdateControlLastStatusSize = 0;
  _localSampleSeq = 0;
  
  _connectionCount = 0;
  _maintenancePeerLocked = false;
  _maintenancePeerHandle = 0xffffU;
  _disconnectedPeerPending = false;
  _disconnectedPeerHandle = 0xffffU;
  // _running = false;  // 既に最初に設定済み
  
  M5.Log.println("[BLE] Stopped");
}

void BleManager::setI2cConfigClient(UlsaEvoI2cClient* client) {
  _pI2cConfigClient = client;
  publishI2cConfigStatus(BLE_I2C_CONFIG_OP_READ_CONFIG,
                         client ? BLE_I2C_CONFIG_RESULT_OK
                                : BLE_I2C_CONFIG_RESULT_UNAVAILABLE,
                         _i2cConfigOperationSeq);
  updateStm32FirmwareVersion(false);
  updateDeviceHealthStatus(false);
}

bool BleManager::consumeDeviceIdentifyRequest(uint32_t& connectionEventSequence) {
  bool pending = false;
  portENTER_CRITICAL(&_deviceIdentifyMux);
  pending = _deviceIdentifyRequestPending;
  if (pending) connectionEventSequence = _deviceIdentifyRequestConnectionSequence;
  _deviceIdentifyRequestPending = false;
  portEXIT_CRITICAL(&_deviceIdentifyMux);
  return pending;
}

uint32_t BleManager::getConnectionEventSequence() {
  portENTER_CRITICAL(&_deviceIdentifyMux);
  const uint32_t sequence = _connectionEventSequence;
  portEXIT_CRITICAL(&_deviceIdentifyMux);
  return sequence;
}

bool BleManager::isRunning() const {
  return _running;
}

// ============================================
// 風速データ更新
// ============================================
void BleManager::updateWindData(const WindData& data) {
  // BLE停止中は何もしない（クラッシュ防止）
  if (!_running) {
    return;
  }
  
  // アドバタイジングデータを更新（ble_services.cppで実装）
  updateAdvertisingData(data);

  // 最新サンプルの由来/seq/statusをRead用にも更新
  updateSampleMetadata(data, _connectionCount > 0);
  
  // 接続中の場合はキャラクタリスティックも更新（ble_services.cppで実装）
  if (_connectionCount > 0) {
    updateCharacteristics(data);
  }

}

// ============================================
// 接続状態取得メソッド
// ============================================
BleConnectionState BleManager::getConnectionState() const {
  return (_connectionCount > 0) ? BLE_CONNECTED : BLE_DISCONNECTED;
}

bool BleManager::isConnected() const {
  return _connectionCount > 0;
}

uint8_t BleManager::getConnectionCount() const {
  return _connectionCount;
}

bool BleManager::lockMaintenancePeer(uint16_t peerHandle) {
  if (!_running || !_pServer || !_pAdvertising ||
      _pServer->getConnectedCount() != 1U) {
    return false;
  }
  const std::vector<uint16_t> peers = _pServer->getPeerDevices();
  if (peers.size() != 1U || peers[0] != peerHandle) {
    return false;
  }
  _pAdvertising->stop();
  _maintenancePeerLocked = true;
  _maintenancePeerHandle = peerHandle;
  return true;
}

void BleManager::unlockMaintenancePeer() {
  _maintenancePeerLocked = false;
  _maintenancePeerHandle = 0xffffU;
  if (_running && _pAdvertising && _connectionCount < NIMBLE_MAX_CONNECTIONS) {
    _pAdvertising->start();
  }
}

bool BleManager::consumeDisconnectedPeerHandle(uint16_t& peerHandle) {
  if (!_disconnectedPeerPending) return false;
  peerHandle = _disconnectedPeerHandle;
  _disconnectedPeerPending = false;
  _disconnectedPeerHandle = 0xffffU;
  return true;
}

BlePhyType BleManager::getCurrentPhy() const {
  return BLE_PHY_1M;
}
