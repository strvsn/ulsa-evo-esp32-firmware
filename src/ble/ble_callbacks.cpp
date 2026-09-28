/**
 * @file ble_callbacks.cpp
 * @brief BLE コールバッククラス実装
 * @date 2025-12-07
 * 
 * NimBLEのサーバー/セキュリティ/キャラクタリスティックコールバックを定義
 */

#include "ble_manager.h"
#include "rtc_manager.h"
#include <M5Unified.h>

// ============================================
// グローバルインスタンス（コールバック用）
// ============================================
BleManager* g_pBleManager = nullptr;

// ============================================
// NimBLE サーバーコールバッククラス
// ============================================
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
    if (g_pBleManager) {
      g_pBleManager->onConnect(pServer, desc ? desc->conn_handle : 0xffffU);
    }
  }
  
  void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
    if (g_pBleManager) {
      g_pBleManager->onDisconnect(pServer, desc ? desc->conn_handle : 0xffffU);
    }
  }
};

// ============================================
// Current Time Characteristic コールバッククラス
// セントラルからの時刻書き込みを処理
// ============================================
class CurrentTimeCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) {
      g_pBleManager->onTimeRead();
    }
  }

  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    if (g_pBleManager) {
      NimBLEAttValue value = pCharacteristic->getValue();
      g_pBleManager->onTimeWrite(value.data(), value.size());
    }
  }
};

class LocalTimeInfoCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) g_pBleManager->onLocalTimeInfoRead();
  }
};

class RtcTimezoneCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) g_pBleManager->onRtcTimezoneRead();
  }

  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    if (!g_pBleManager) return;
    const NimBLEAttValue value = pCharacteristic->getValue();
    g_pBleManager->onRtcTimezoneWrite(value.data(), value.size());
  }
};

// ============================================
// I2C Config Control Characteristic コールバッククラス
// BLEタスク内では要求をキュー投入するだけにする
// ============================================
class I2cConfigCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    if (g_pBleManager) {
      NimBLEAttValue value = pCharacteristic->getValue();
      g_pBleManager->onI2cConfigWrite(value.data(), value.size());
    }
  }
};

// ============================================
// SD Status Characteristic コールバッククラス
// セントラルからのRead直前に容量情報を更新
// ============================================
class SdStatusCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) {
      g_pBleManager->onSdStatusRead();
    }
  }
};

// ============================================
// SD Log Control Characteristic コールバッククラス
// BLEタスク内では要求をキュー投入するだけにする
// ============================================
class SdLogControlCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) {
      g_pBleManager->onSdLogControlRead();
    }
  }

  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    if (g_pBleManager) {
      NimBLEAttValue value = pCharacteristic->getValue();
      g_pBleManager->onSdLogControlWrite(value.data(), value.size());
    }
  }
};

// ============================================
// SD Log Detail Characteristic コールバッククラス
// セントラルからのRead直前にログ統計を更新
// ============================================
class SdLogDetailCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) {
      g_pBleManager->onSdLogDetailRead();
    }
  }
};

// ============================================
// SD Log Settings Characteristic コールバッククラス
// BLEタスク内では要求をキュー投入するだけにする
// ============================================
class SdLogSettingsCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) {
      g_pBleManager->onSdLogSettingsRead();
    }
  }

  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    if (g_pBleManager) {
      NimBLEAttValue value = pCharacteristic->getValue();
      g_pBleManager->onSdLogSettingsWrite(value.data(), value.size());
    }
  }
};

// ============================================
// STM32 Firmware Version Characteristic コールバッククラス
// セントラルからのRead直前にSTM32 I2CからFWバージョンを更新
// ============================================
class Stm32FirmwareVersionCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) {
      g_pBleManager->onStm32FirmwareVersionRead();
    }
  }
};

// ============================================
// Device Identify Characteristic コールバッククラス
// BLEタスク内ではLED識別要求をキュー投入するだけにする
// ============================================
class DeviceIdentifyCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    if (g_pBleManager) {
      NimBLEAttValue value = pCharacteristic->getValue();
      g_pBleManager->onDeviceIdentifyWrite(value.data(), value.size());
    }
  }
};

// ============================================
// LED Brightness Characteristic コールバッククラス
// BLEタスク内では輝度変更要求をキュー投入するだけにする
// ============================================
class LedBrightnessCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) {
      g_pBleManager->onLedBrightnessRead();
    }
  }

  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    if (g_pBleManager) {
      NimBLEAttValue value = pCharacteristic->getValue();
      g_pBleManager->onLedBrightnessWrite(value.data(), value.size());
    }
  }
};

// ============================================
// LED Wind Reactive Characteristic コールバッククラス
// BLEタスク内では設定要求をキュー投入するだけにする
// ============================================
class LedWindReactiveCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) {
      g_pBleManager->onLedWindReactiveRead();
    }
  }

  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    if (g_pBleManager) {
      NimBLEAttValue value = pCharacteristic->getValue();
      g_pBleManager->onLedWindReactiveWrite(value.data(), value.size());
    }
  }
};

// ============================================
// OTA Control Characteristic コールバッククラス
// BLEタスク内では要求をキュー投入するだけにする
// ============================================
class OtaControlCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) {
      g_pBleManager->onOtaControlRead();
    }
  }

  void onWrite(NimBLECharacteristic* pCharacteristic,
               ble_gap_conn_desc* desc) override {
    if (g_pBleManager) {
      NimBLEAttValue value = pCharacteristic->getValue();
      g_pBleManager->onOtaControlWrite(
        value.data(), value.size(), desc ? desc->conn_handle : 0xffffU);
    }
  }
};

// ============================================
// STM32 Update Control Characteristic コールバッククラス
// BLEタスク内では要求をキュー投入するだけにする
// ============================================
class Stm32UpdateControlCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) {
      g_pBleManager->onStm32UpdateControlRead();
    }
  }

  void onWrite(NimBLECharacteristic* pCharacteristic,
               ble_gap_conn_desc* desc) override {
    if (g_pBleManager) {
      NimBLEAttValue value = pCharacteristic->getValue();
      g_pBleManager->onStm32UpdateControlWrite(
        value.data(), value.size(), desc ? desc->conn_handle : 0xffffU);
    }
  }
};

// ============================================
// Reset Control Characteristic コールバッククラス
// BLEタスク内では要求をキュー投入するだけにする
// ============================================
class ResetControlCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic) override {
    (void)pCharacteristic;
    if (g_pBleManager) {
      g_pBleManager->onResetControlRead();
    }
  }

  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    if (g_pBleManager) {
      NimBLEAttValue value = pCharacteristic->getValue();
      g_pBleManager->onResetControlWrite(value.data(), value.size());
    }
  }
};

// ============================================
// セキュリティコールバッククラス
// ボンディング（ペアリング）処理用
// ============================================
class SecurityCallbacks : public NimBLESecurityCallbacks {
  // Just Works方式でのペアリング確認
  // ディスプレイやキーパッドがないデバイス向け
  uint32_t onPassKeyRequest() override {
    // M5.Log.println("[BLE] PassKey Request");
    return 123456;  // デフォルトパスキー（Just Worksでは使用されない）
  }

  void onPassKeyNotify(uint32_t pass_key) override {
    // M5.Log.printf("[BLE] PassKey Notify: %06d\n", pass_key);
  }

  bool onSecurityRequest() override {
    // M5.Log.println("[BLE] Security Request - Accepting");
    return true;  // セキュリティ要求を受け入れる
  }

  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    if (desc->sec_state.bonded) {
      // M5.Log.println("[BLE] Bonding Complete - Device Bonded");
    } else if (desc->sec_state.encrypted) {
      // M5.Log.println("[BLE] Pairing Complete - Encrypted (Not Bonded)");
    } else {
      // M5.Log.println("[BLE] Authentication Complete - No Encryption");
    }
  }

  bool onConfirmPIN(uint32_t pin) override {
    // M5.Log.printf("[BLE] Confirm PIN: %06d\n", pin);
    return true;  // PINを確認（Just Worksでは自動承認）
  }
};

// ============================================
// コールバックインスタンスを取得するヘルパー関数
// static変数でメモリリークを防止
// ============================================
NimBLEServerCallbacks* getServerCallbacks() {
  static ServerCallbacks callbacks;
  return &callbacks;
}

NimBLESecurityCallbacks* getSecurityCallbacks() {
  static SecurityCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getCurrentTimeCallbacks() {
  static CurrentTimeCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getLocalTimeInfoCallbacks() {
  static LocalTimeInfoCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getRtcTimezoneCallbacks() {
  static RtcTimezoneCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getI2cConfigCallbacks() {
  static I2cConfigCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getSdStatusCallbacks() {
  static SdStatusCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getSdLogControlCallbacks() {
  static SdLogControlCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getSdLogDetailCallbacks() {
  static SdLogDetailCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getSdLogSettingsCallbacks() {
  static SdLogSettingsCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getStm32FirmwareVersionCallbacks() {
  static Stm32FirmwareVersionCallbacks callbacks;
  return &callbacks;
}


NimBLECharacteristicCallbacks* getDeviceIdentifyCallbacks() {
  static DeviceIdentifyCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getLedBrightnessCallbacks() {
  static LedBrightnessCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getLedWindReactiveCallbacks() {
  static LedWindReactiveCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getOtaControlCallbacks() {
  static OtaControlCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getStm32UpdateControlCallbacks() {
  static Stm32UpdateControlCallbacks callbacks;
  return &callbacks;
}

NimBLECharacteristicCallbacks* getResetControlCallbacks() {
  static ResetControlCallbacks callbacks;
  return &callbacks;
}

// ============================================
// 接続/切断コールバック実装
// ============================================
void BleManager::onConnect(NimBLEServer* pServer, uint16_t peerHandle) {
  _connectionCount++;
  portENTER_CRITICAL(&_deviceIdentifyMux);
  ++_connectionEventSequence;
  portEXIT_CRITICAL(&_deviceIdentifyMux);
  // A newly connected client must receive one current status notification.
  // Subsequent identical payloads stay quiet until a status field changes.
  _sensorStatusNotifyPending = true;
  refreshDeviceModeConnectionFlag();

  if (_maintenancePeerLocked && peerHandle != _maintenancePeerHandle) {
    pServer->disconnect(peerHandle);
    return;
  }
  
  // 接続後もアドバタイジングを継続
  // これにより接続していないデバイスもセンサーデータを参照可能
  // 複数接続もサポート（NIMBLE_MAX_CONNECTIONSまで）
  if (_connectionCount < NIMBLE_MAX_CONNECTIONS) {
    // 少し待機してからアドバタイジング再開（安定性向上）
    delay(100);
    _pAdvertising->start();
    // M5.Log.println("[BLE] Advertising continued after connection");
  }
}

void BleManager::onDisconnect(NimBLEServer* pServer, uint16_t peerHandle) {
  if (_connectionCount > 0) {
    _connectionCount--;
  }
  _disconnectedPeerHandle = peerHandle;
  _disconnectedPeerPending = true;
  if (_maintenancePeerLocked && peerHandle == _maintenancePeerHandle) {
    _maintenancePeerLocked = false;
    _maintenancePeerHandle = 0xffffU;
  }
  // M5.Log.printf("[BLE] Disconnected (count: %d)\n", _connectionCount);
  refreshDeviceModeConnectionFlag();
  
  // 再度アドバタイジング開始
  if (_running && _pAdvertising && !_maintenancePeerLocked) {
    _pAdvertising->start();
  }
}

// ============================================
// CTS 時刻書き込みコールバック
// ============================================
void BleManager::onTimeRead() {
  // The main loop refreshes this value. Never touch Wire or AceTime from the
  // NimBLE host task.
}

void BleManager::onSdStatusRead() {
  updateSdStatus(false);
}

void BleManager::onSdLogControlRead() {
  publishSdLogControlStatus(BLE_SD_LOG_OP_READ,
                            _pSdLogger ? BLE_SD_LOG_RESULT_OK
                                       : BLE_SD_LOG_RESULT_UNAVAILABLE);
}

void BleManager::onSdLogDetailRead() {
  updateSdLogDetailStatus(false);
}

void BleManager::onSdLogSettingsRead() {
  publishSdLogSettingsStatus(BLE_SD_LOG_SETTINGS_OP_READ,
                             _pSdLogger ? BLE_SD_LOG_SETTINGS_RESULT_OK
                                        : BLE_SD_LOG_SETTINGS_RESULT_UNAVAILABLE);
}

void BleManager::onStm32FirmwareVersionRead() {
  updateStm32FirmwareVersion(false);
}


void BleManager::onDeviceIdentifyWrite(const uint8_t* data, size_t length) {
  if (!data || length < 1 || data[0] != BLE_DEVICE_IDENTIFY_OP_BLINK) {
    return;
  }

  portENTER_CRITICAL(&_deviceIdentifyMux);
  _deviceIdentifyRequestPending = true;
  _deviceIdentifyRequestConnectionSequence = _connectionEventSequence;
  portEXIT_CRITICAL(&_deviceIdentifyMux);
}

void BleManager::onTimeWrite(const uint8_t* data, size_t length) {
  BleRtcTimezoneRequest request{};
  request.kind = BleRtcRequestKind::CtsLocalTime;
  if (!_pRtc || !data || length != CTS_DATA_SIZE) {
    request.validationResult = BLE_RTC_TIMEZONE_RESULT_INVALID_LENGTH;
    if (!enqueueRtcTimezoneRequest(request)) {
      publishRtcTimezoneBusyFromCallback(0);
    }
    return;
  }
  
  // CTS標準フォーマットをパース
  // [0-1] Year (uint16_t, little endian)
  // [2]   Month (1-12)
  // [3]   Day (1-31)
  // [4]   Hour (0-23)
  // [5]   Minute (0-59)
  // [6]   Second (0-59)
  // [7]   Day of Week (1=月曜...7=日曜, 0=不明)
  // [8]   Fractions256 (無視)
  // [9]   Adjust Reason (無視)
  
  RtcDateTime dt{};
  dt.year   = data[0] | (data[1] << 8);
  dt.month  = data[2];
  dt.day    = data[3];
  dt.hour   = data[4];
  dt.minute = data[5];
  dt.second = data[6];
  
  // Weekday in CTS may be zero (unknown). Validate the civil date using our
  // own calculated weekday and do not preserve an untrusted weekday in RTC.
  dt.weekday = ulsa::rtc::calculateWeekday(dt.year, dt.month, dt.day);
  if (!dt.isValid()) {
    request.validationResult = BLE_RTC_TIMEZONE_RESULT_TIME_OUT_OF_RANGE;
  }
  request.local = dt;
  if (!enqueueRtcTimezoneRequest(request)) {
    publishRtcTimezoneBusyFromCallback(0);
  }
}
