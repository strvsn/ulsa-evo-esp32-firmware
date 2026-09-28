/**
 * @file ble_manager.h
 * @brief BLE通信管理モジュール (NimBLE使用)
 * @date 2025-12-03
 * 
 * 風速計データのBLEアドバタイジング・GATT送信を管理
 */

#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include "wind_data.h"
#include "ble_config.h"
#include "rtc_manager.h"

// Forward declaration
class SdLogger;
class UlsaEvoI2cClient;
class OtaManager;

/**
 * @brief BLE接続状態
 */
enum BleConnectionState {
  BLE_DISCONNECTED = 0,   // 未接続（アドバタイジング中）
  BLE_CONNECTED           // 接続中
};

/**
 * @brief Frozen bootloader-return source compatibility values
 *
 * getCurrentPhy() always returns BLE_PHY_1M. BLE_PHY_CODED remains only so the
 * hardware-validated handleStm32ModeToggle() body can stay byte-for-byte
 * unchanged; it is not an implemented or reported PHY state.
 */
enum BlePhyType {
  BLE_PHY_1M = 0,
  BLE_PHY_CODED = 1,
};


struct BleStm32UpdateControlRequest {
  uint8_t op;
  uint16_t peerHandle;
  // Legacy wire ABI. This is an optional user-label hint, not an identity gate.
  bool hasExpectedNodeId;
  uint8_t expectedNodeId;
  bool hasSessionBinding;
  char target[BLE_STM32_UPDATE_CONTROL_TARGET_MAX_LEN + 1];
  char releaseTag[BLE_STM32_UPDATE_CONTROL_RELEASE_TAG_MAX_LEN + 1];

  BleStm32UpdateControlRequest()
    : op(0)
    , peerHandle(0xffffU)
    , hasExpectedNodeId(false)
    , expectedNodeId(0)
    , hasSessionBinding(false) {
    memset(target, 0, sizeof(target));
    memset(releaseTag, 0, sizeof(releaseTag));
  }
};

enum class BleRtcRequestKind : uint8_t {
  TimezoneControl,
  CtsLocalTime,
};

struct BleRtcTimezoneRequest {
  BleRtcRequestKind kind = BleRtcRequestKind::TimezoneControl;
  uint8_t op = 0;
  uint8_t validationResult = BLE_RTC_TIMEZONE_RESULT_OK;
  uint32_t zoneId = 0;
  int64_t unixSeconds = 0;
  RtcDateTime local{};
};

/**
 * @brief BLE通信管理クラス
 * 
 * NimBLEを使用してBLEペリフェラルとして動作
 * - アドバタイジングデータに計測値を含める
 * - GATT接続時はNotificationで計測値を送信
 */
class BleManager {
public:
  BleManager();
  
  /**
   * @brief BLE初期化
   * @param nodeId ユーザー設定Node label（デバイス名に使用）
   * @param pRtc RtcManager（時刻同期用、nullptrで無効）
   * @param pSdLogger SdLogger（SDステータス通知用、nullptrで無効）
   * @return true: 初期化成功
   */
  bool begin(uint8_t nodeId, RtcManager* pRtc = nullptr, SdLogger* pSdLogger = nullptr);

  /**
   * @brief 停止前のlast-known Node labelでBLEを再開する
   *
   * STM32の再起動直後はI2Cの公開NODE_IDがまだ読めないことがある。
   * その間も、直前のlabelを使って広告を再開し、
   * 通常のI2C同期で後から最新値へ更新する。
   */
  bool restartWithLastVerifiedNodeId();
  
  /**
   * @brief BLE停止（WiFiポータル使用時などリソース解放用）
   */
  void stop();
  
  /**
   * @brief BLEが動作中かどうか
   * @return true: 動作中
   */
  bool isRunning() const;
  
  /**
   * @brief 風速計データを更新してBLE送信
   * @param data 風速計データ
   */
  void updateWindData(const WindData& data);
  
  /**
   * @brief Node labelを更新（BLEデバイス名も更新）
   * @param nodeId 新しいuser label
   * @return true: 更新成功または変更なし
   */
  bool updateNodeId(uint8_t nodeId);
  
  /**
   * @brief 現在のノードIDを取得
   * @return ノードID
   */
  uint8_t getNodeId() const { return _nodeId; }
  
  /**
   * @brief SDカードステータスをBLEで更新
   */
  void updateSdStatus(bool notify = true);

  /**
   * @brief ESP32動作モードステータスをBLEで更新
   * @param mode BLE_DEVICE_MODE_* のいずれか
   * @param flags BLE_DEVICE_MODE_FLAG_* のOR
   */
  void updateDeviceModeStatus(uint8_t mode, uint8_t flags);

  /**
   * @brief STM32 FWバージョンステータスをBLEで更新
   */
  void updateStm32FirmwareVersion(bool notify = true);

  /**
   * @brief BLE I2C config制御で使用するI2Cクライアントを設定
   */
  void setI2cConfigClient(UlsaEvoI2cClient* client);

  /**
   * @brief BLEから受け取ったI2C config要求をメインループ側で処理
   */
  void processI2cConfigRequest();

  /**
   * @brief BLEから受け取ったSDログ制御要求をメインループ側で処理
   */
  void processSdLogControlRequest();

  /**
   * @brief BLEから受け取ったSDログ設定要求をメインループ側で処理
   */
  void processSdLogSettingsRequest();

  /**
   * @brief BLEから受け取ったDevice Identify要求を消費
   * @return true: LED識別点滅を実行する要求あり
   */
  bool consumeDeviceIdentifyRequest(uint32_t& connectionEventSequence);
  // The identify command arrives after its temporary BLE connection is made.
  // A later connection event can therefore identify a normal app connection.
  uint32_t getConnectionEventSequence();

  /**
   * @brief BLEから受け取ったLED輝度変更要求をメインループ側で処理
   */
  void processLedBrightnessRequest();

  /**
   * @brief BLEから受け取った風速連動LED設定要求をメインループ側で処理
   */
  void processLedWindReactiveRequest();

  /** BLE callbackでqueueされたRTC/timezone writeをmain側で直列実行する。 */
  void processRtcTimezoneRequest();

  /** main loopから呼び、RTC系Read characteristicのcacheを更新する。 */
  void serviceRtcTimeCaches();

  /**
   * @brief BLE OTA controlで使用するOtaManagerを設定
   */
  void setOtaManager(OtaManager* manager);

  /**
   * @brief BLEから受け取ったReset Control要求をメインループ側で消費
   * @param op BLE_RESET_OP_* を格納
   * @return true: 要求あり
   */
  bool consumeResetControlRequest(uint8_t& op);

  /**
   * @brief Reset Control statusをBLEで更新
   */
  void publishResetControlStatus(uint8_t op,
                                 uint8_t result,
                                 uint8_t flags = 0,
                                 bool notify = true);

  /**
   * @brief BLEから受け取ったOTA要求をメインループ側で消費
   * @param op BLE_OTA_OP_* を格納
   * @return true: 要求あり
   */
  bool consumeOtaControlRequest(uint8_t& op, uint16_t& peerHandle);

  /**
   * @brief OTA control statusをBLEで更新
   */
  void publishOtaControlStatus(uint8_t op, uint8_t result, bool notify = true);

  /**
   * @brief BLEから受け取ったSTM32 update要求をメインループ側で消費
   * @param request BLE_STM32_UPDATE_OP_* と任意のsession bindingを格納
   * @return true: 要求あり
   */
  bool consumeStm32UpdateControlRequest(BleStm32UpdateControlRequest& request);

  /**
   * @brief STM32 update control statusをBLEで更新
   */
  void publishStm32UpdateControlStatus(uint8_t op, uint8_t result, bool notify = true);
  
  /**
   * @brief 接続状態を取得
   * @return BLE_CONNECTED or BLE_DISCONNECTED
   */
  BleConnectionState getConnectionState() const;
  
  /**
   * @brief 接続中かどうか
   * @return true: 接続中
   */
  bool isConnected() const;
  
  /**
   * @brief 接続数を取得
   * @return 接続中のクライアント数
   */
  uint8_t getConnectionCount() const;

  bool lockMaintenancePeer(uint16_t peerHandle);
  void unlockMaintenancePeer();
  bool consumeDisconnectedPeerHandle(uint16_t& peerHandle);


  BlePhyType getCurrentPhy() const;

  // NimBLEコールバック用（内部使用）
  void onConnect(NimBLEServer* pServer, uint16_t peerHandle);
  void onDisconnect(NimBLEServer* pServer, uint16_t peerHandle);
  
  /**
   * @brief CTS時刻書き込みコールバック（内部使用）
   * @param data 受信データ
   * @param length データ長
   */
  void onTimeWrite(const uint8_t* data, size_t length);

  /**
   * @brief CTS時刻読み出しコールバック（内部使用）
   */
  void onTimeRead();

  /** CTS Local Time Information read callback. */
  void onLocalTimeInfoRead();

  /** RTC Timezone Control read/write callbacks. */
  void onRtcTimezoneRead();
  void onRtcTimezoneWrite(const uint8_t* data, size_t length);

  /**
   * @brief SDステータス読み出しコールバック（内部使用）
   */
  void onSdStatusRead();

  /**
   * @brief SDログ制御ステータス読み出しコールバック（内部使用）
   */
  void onSdLogControlRead();

  /**
   * @brief SDログ詳細ステータス読み出しコールバック（内部使用）
   */
  void onSdLogDetailRead();

  /**
   * @brief STM32 FWバージョン読み出しコールバック（内部使用）
   */
  void onStm32FirmwareVersionRead();

  /**
   * @brief BLE I2C config writeコールバック（内部使用）
   */
  void onI2cConfigWrite(const uint8_t* data, size_t length);

  /**
   * @brief BLE SDログ制御writeコールバック（内部使用）
   */
  void onSdLogControlWrite(const uint8_t* data, size_t length);

  /**
   * @brief BLE SDログ設定読み出しコールバック（内部使用）
   */
  void onSdLogSettingsRead();

  /**
   * @brief BLE SDログ設定writeコールバック（内部使用）
   */
  void onSdLogSettingsWrite(const uint8_t* data, size_t length);

  /**
   * @brief BLE Device Identify writeコールバック（内部使用）
   */
  void onDeviceIdentifyWrite(const uint8_t* data, size_t length);

  /**
   * @brief BLE LED輝度読み出しコールバック（内部使用）
   */
  void onLedBrightnessRead();

  /**
   * @brief BLE LED輝度writeコールバック（内部使用）
   */
  void onLedBrightnessWrite(const uint8_t* data, size_t length);

  /**
   * @brief BLE風速連動LED設定のread/writeコールバック（内部使用）
   */
  void onLedWindReactiveRead();
  void onLedWindReactiveWrite(const uint8_t* data, size_t length);

  /**
   * @brief BLE OTA control読み出しコールバック（内部使用）
   */
  void onOtaControlRead();

  /**
   * @brief BLE OTA control writeコールバック（内部使用）
   */
  void onOtaControlWrite(const uint8_t* data, size_t length, uint16_t peerHandle);

  /**
   * @brief BLE STM32 update control読み出しコールバック（内部使用）
   */
  void onStm32UpdateControlRead();

  /**
   * @brief BLE STM32 update control writeコールバック（内部使用）
   */
  void onStm32UpdateControlWrite(const uint8_t* data, size_t length,
                                 uint16_t peerHandle);

  /**
   * @brief BLE Reset Control読み出しコールバック（内部使用）
   */
  void onResetControlRead();

  /**
   * @brief BLE Reset Control writeコールバック（内部使用）
   */
  void onResetControlWrite(const uint8_t* data, size_t length);

private:
  NimBLEServer* _pServer;
  NimBLEAdvertising* _pAdvertising;
  
  // Environmental Sensing Service
  NimBLEService* _pEnvService;
  NimBLECharacteristic* _pWindDirChar;
  NimBLECharacteristic* _pWindSpeedChar;
  NimBLECharacteristic* _pTempChar;
  
  // Custom ULSA Wind Service
  NimBLEService* _pUlsaService;
  NimBLECharacteristic* _pSoundSpeedChar;
  NimBLECharacteristic* _pHeadingSpeedChar;
  NimBLECharacteristic* _pWindAxisSpeedsChar;
  NimBLECharacteristic* _pStatusChar;
  
  // Current Time Service
  NimBLEService* _pCtsService;
  NimBLECharacteristic* _pCurrentTimeChar;
  NimBLECharacteristic* _pLocalTimeInfoChar;
  
  // Device Information Service
  NimBLEService* _pDevInfoService;
  NimBLECharacteristic* _pFirmwareRevChar;
  NimBLECharacteristic* _pSoftwareRevChar;
  NimBLECharacteristic* _pManufacturerChar;
  NimBLECharacteristic* _pModelNumberChar;
  
  // SD Status Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pSdStatusChar;

  // SD Log Control Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pSdLogControlChar;

  // SD Log Detail Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pSdLogDetailChar;

  // SD Log Settings Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pSdLogSettingsChar;

  // ESP32 device mode Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pDeviceModeChar;

  // STM32 firmware version Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pStm32FirmwareVersionChar;

  // I2C config control/status Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pI2cConfigChar;

  // Sample metadata Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pSampleMetadataChar;

  // Device health summary Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pDeviceHealthChar;

  // BLE Capabilities Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pCapabilitiesChar;

  // Device Identify Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pDeviceIdentifyChar;

  // LED Brightness Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pLedBrightnessChar;

  // LED Wind Reactive Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pLedWindReactiveChar;

  // OTA Control Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pOtaControlChar;

  // STM32 Update Control Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pStm32UpdateControlChar;

  // Reset Control Characteristic (ULSA Serviceに追加)
  NimBLECharacteristic* _pResetControlChar;

  // RTC Timezone Control Characteristic (ULSA Service)
  NimBLECharacteristic* _pRtcTimezoneChar;

  
  // RTC連携
  RtcManager* _pRtc;
  RtcStatus _rtcHealthLastStatus;
  uint32_t _rtcHealthLastReadMs;
  bool _rtcHealthStatusInitialized;
  
  // SDロガー連携
  SdLogger* _pSdLogger;

  // I2C config連携
  UlsaEvoI2cClient* _pI2cConfigClient;

  // OTA連携
  OtaManager* _pOtaManager;
  portMUX_TYPE _i2cConfigMux;
  volatile bool _i2cConfigRequestPending;
  uint8_t _i2cConfigRequestOp;
  uint8_t _i2cConfigRequestValue;
  uint8_t _i2cConfigRequestOperationSeq;
  uint8_t _i2cConfigOperationSeq;
  uint8_t _i2cConfigLastStatus[BLE_I2C_CONFIG_STATUS_SIZE];
  portMUX_TYPE _sdLogControlMux;
  volatile bool _sdLogControlRequestPending;
  uint8_t _sdLogControlRequestOp;
  uint8_t _sdLogControlLastStatus[BLE_SD_LOG_CONTROL_STATUS_SIZE];
  uint8_t _sdLogDetailLastStatus[BLE_SD_LOG_DETAIL_STATUS_SIZE];
  portMUX_TYPE _sdLogSettingsMux;
  volatile bool _sdLogSettingsRequestPending;
  uint8_t _sdLogSettingsRequestOp;
  uint32_t _sdLogSettingsRequestIntervalMs;
  bool _sdLogSettingsRequestAutoStartEnabled;
  uint8_t _sdLogSettingsLastStatus[BLE_SD_LOG_SETTINGS_STATUS_SIZE];
  uint8_t _capabilitiesLastStatus[BLE_CAPABILITIES_STATUS_SIZE];
  uint8_t _deviceModeLastStatus[BLE_DEVICE_MODE_STATUS_SIZE];
  uint8_t _stm32FirmwareVersionLastStatus[BLE_STM32_FW_VERSION_STATUS_SIZE];
  uint8_t _sampleMetadataLastStatus[BLE_SAMPLE_METADATA_STATUS_SIZE];
  uint8_t _sensorStatusLastNotified[BLE_SENSOR_STATUS_SIZE];
  bool _sensorStatusNotifyPending;
  uint8_t _deviceHealthLastStatus[BLE_DEVICE_HEALTH_STATUS_SIZE];
  portMUX_TYPE _deviceIdentifyMux;
  volatile bool _deviceIdentifyRequestPending;
  uint32_t _deviceIdentifyRequestConnectionSequence;
  uint32_t _connectionEventSequence;
  portMUX_TYPE _ledBrightnessMux;
  volatile bool _ledBrightnessRequestPending;
  uint8_t _ledBrightnessRequestValue;
  portMUX_TYPE _ledWindReactiveMux;
  volatile bool _ledWindReactiveRequestPending;
  bool _ledWindReactiveRequestEnabled;
  uint8_t _ledWindReactiveRequestTheme;
  portMUX_TYPE _otaControlMux;
  volatile bool _otaControlRequestPending;
  uint8_t _otaControlRequestOp;
  uint16_t _otaControlRequestPeerHandle;
  uint8_t _otaControlLastStatus[BLE_OTA_CONTROL_STATUS_MAX_SIZE];
  size_t _otaControlLastStatusSize;
  portMUX_TYPE _stm32UpdateControlMux;
  volatile bool _stm32UpdateControlRequestPending;
  BleStm32UpdateControlRequest _stm32UpdateControlRequest;
  uint8_t _stm32UpdateControlLastStatus[BLE_STM32_UPDATE_CONTROL_STATUS_MAX_SIZE];
  size_t _stm32UpdateControlLastStatusSize;
  portMUX_TYPE _resetControlMux;
  volatile bool _resetControlRequestPending;
  uint8_t _resetControlRequestOp;
  uint8_t _resetControlLastStatus[BLE_RESET_CONTROL_STATUS_SIZE];
  portMUX_TYPE _rtcTimezoneMux;
  volatile bool _rtcTimezoneRequestPending;
  volatile bool _rtcTimezoneProcessing;
  BleRtcTimezoneRequest _rtcTimezoneRequest;
  uint8_t _rtcTimezoneLastStatus[BLE_RTC_TIMEZONE_STATUS_SIZE];
  uint8_t _rtcTimezoneLastOp;
  uint8_t _rtcTimezoneLastResult;
  uint16_t _rtcTimezoneOperationGeneration;
  uint32_t _rtcTimeCacheLastRefreshMs;
  uint16_t _localSampleSeq;
  
  uint8_t _nodeId;
  uint8_t _connectionCount;
  bool _maintenancePeerLocked;
  uint16_t _maintenancePeerHandle;
  volatile bool _disconnectedPeerPending;
  uint16_t _disconnectedPeerHandle;
  bool _running;  // BLE動作中フラグ
  char _deviceName[BLE_DEVICE_NAME_MAX_LEN];
  
  // アドバタイジングデータ用バッファ
  uint8_t _serviceData[BLE_SERVICE_DATA_SIZE];
  
  void setupServices();
  void startAdvertising();
  void updateAdvertisingData(const WindData& data);
  void updateCharacteristics(const WindData& data);
  void updateCurrentTimeChar();  ///< CTS CharacteristicをRTCから更新
  void updateCurrentTimeChar(const RtcTimezoneSnapshot& snapshot);
  void updateLocalTimeInfoChar();
  void updateLocalTimeInfoChar(const RtcTimezoneSnapshot& snapshot);
  void clearI2cConfigRequest();
  void publishI2cConfigStatus(uint8_t op, uint8_t result,
                              uint8_t operationSeq);
  void clearSdLogControlRequest();
  void publishSdLogControlStatus(uint8_t op, uint8_t result);
  void clearSdLogSettingsRequest();
  void publishSdLogSettingsStatus(uint8_t op, uint8_t result);
  void clearLedBrightnessRequest();
  void publishLedBrightnessStatus(bool notify = true);
  void clearLedWindReactiveRequest();
  void publishLedWindReactiveStatus(uint8_t op, uint8_t result, bool notify = true);
  void clearOtaControlRequest();
  void clearStm32UpdateControlRequest();
  void clearResetControlRequest();
  void clearRtcTimezoneRequest();
  bool enqueueRtcTimezoneRequest(const BleRtcTimezoneRequest& request);
  void publishRtcTimezoneStatus(uint8_t op, uint8_t result,
                                bool notify = true,
                                bool incrementGeneration = false,
                                const RtcTimezoneSnapshot* snapshot = nullptr);
  void publishRtcTimezoneBusyFromCallback(uint8_t op);
  uint32_t getSdLogStm32IntervalMs() const;
  uint32_t getSdLogMinIntervalMs() const;
  void updateCapabilitiesStatus();
  void updateSdLogDetailStatus(bool notify = true);
  void refreshDeviceModeConnectionFlag();
  void updateSampleMetadata(const WindData& data, bool notify = true);
  /**
   * @brief Device Healthを更新する
   *
   * センサーフレームごとに呼ばれてもRTC I2Cを占有しないよう、通常はRTC状態を
   * 1秒ごとに再読出しする。CTS書込み直後だけはforceRtcRefreshで同期結果を即時反映する。
   */
  void updateDeviceHealthStatus(bool notify = true, bool forceRtcRefresh = false);
};

// グローバルインスタンス（コールバック用）
extern BleManager* g_pBleManager;

#endif // BLE_MANAGER_H
