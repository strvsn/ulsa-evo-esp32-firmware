/**
 * @file ble_config.h
 * @brief BLEサービス/キャラクタリスティック UUID定義
 * @date 2025-12-03
 * 
 * BLE SIG標準サービス + カスタムサービスのUUID定義
 */

#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

// ============================================
// デバイス名設定
// ============================================
#define BLE_DEVICE_NAME_PREFIX  "ULSA EVO #"   // デバイス名プレフィックス
#define BLE_DEVICE_NAME_MAX_LEN 20            // デバイス名最大長

// ============================================
// BLE接続パラメータ
// ============================================
#define BLE_ADV_INTERVAL_MIN    100   // 最小アドバタイズ間隔 [ms]
#define BLE_ADV_INTERVAL_MAX    200   // 最大アドバタイズ間隔 [ms]
#define BLE_UPDATE_RATE_MS      100   // データ更新レート [ms] (10Hz)

// Product BLE PHY policy is 1M only. Coded PHY selection, status reporting,
// and range claims are not part of the released firmware contract.

// ============================================
// BLEセキュリティ/ボンディング設定
// ============================================
#define BLE_BONDING_ENABLED     false // ボンディング（ペアリング情報保存）無効
#define BLE_MITM_PROTECTION     false // MITM保護（PINコード入力）無効 → Just Works方式
#define BLE_SECURE_CONNECTION   false // LE Secure Connections無効

// ============================================
// 標準BLEサービス UUID (16bit)
// ============================================

// Environmental Sensing Service (0x181A)
// 風向・風速・温度などの環境データ
#define UUID_SERVICE_ENVIRONMENTAL    0x181A

// Current Time Service (0x1805)
// セントラルからの時刻同期用
#define UUID_SERVICE_CURRENT_TIME     0x1805

// Device Information Service (0x180A)
// ファームウェアバージョン等のデバイス情報
#define UUID_SERVICE_DEVICE_INFO      0x180A

// ============================================
// 標準キャラクタリスティック UUID (16bit)
// ============================================

// Current Time (0x2A2B) - 現在時刻
// Format: 10バイト構造体
//   [0-1] Year (uint16_t, little endian)
//   [2]   Month (1-12)
//   [3]   Day (1-31)
//   [4]   Hour (0-23)
//   [5]   Minute (0-59)
//   [6]   Second (0-59)
//   [7]   Day of Week (1=月曜, 7=日曜, 0=不明)
//   [8]   Fractions256 (1/256秒、通常0)
//   [9]   Adjust Reason (ビットフラグ、通常0)
#define UUID_CHAR_CURRENT_TIME        0x2A2B
#define CTS_DATA_SIZE                 10

// Local Time Information (0x2A0F), read only.
// [0] signed UTC timezone in 15-minute units; -128 means unknown.
// [1] DST offset: 0=standard, 2=30min, 4=60min, 8=120min, 0xff=unknown.
#define UUID_CHAR_LOCAL_TIME_INFORMATION 0x2A0F
#define CTS_LOCAL_TIME_INFO_SIZE      2

// Firmware Revision String (0x2A26) - ファームウェアバージョン
// Format: UTF-8文字列
#define UUID_CHAR_FIRMWARE_REV        0x2A26

// Software Revision String (0x2A28) - revision / commit / profile
#define UUID_CHAR_SOFTWARE_REV        0x2A28

// Manufacturer Name String (0x2A29) - メーカー名
#define UUID_CHAR_MANUFACTURER_NAME   0x2A29

// Model Number String (0x2A24) - モデル番号
#define UUID_CHAR_MODEL_NUMBER        0x2A24

// Apparent Wind Direction (0x2A73) - 見かけの風向
// Format: uint16, 単位: 0.01度
#define UUID_CHAR_WIND_DIRECTION      0x2A73

// Apparent Wind Speed (0x2A72) - 見かけの風速  
// Format: uint16, 単位: 0.01 m/s
#define UUID_CHAR_WIND_SPEED          0x2A72

// Temperature (0x2A6E) - 温度
// Format: int16, 単位: 0.01℃
#define UUID_CHAR_TEMPERATURE         0x2A6E

// ============================================
// カスタムサービス UUID (128bit)
// ULSA EVO専用サービス
// ============================================

// Base UUID: xxxxxxxx-0000-1000-8000-00805F9B34FB (BLE SIG Base)
// カスタムUUID: 独自のベースUUIDを使用

// ULSA Wind Service
// Product UUID namespace: E147A12A-67FF-4249-930B-C35D372BA0xx
#define UUID_SERVICE_ULSA_WIND        "E147A12A-67FF-4249-930B-C35D372BA000"

// 音速 [m/s] - uint16, 単位: 0.01 m/s
#define UUID_CHAR_SOUND_SPEED         "E147A12A-67FF-4249-930B-C35D372BA001"

// 機首風速 [m/s] - int16, 単位: 0.01 m/s (正負あり)
#define UUID_CHAR_HEADING_SPEED       "E147A12A-67FF-4249-930B-C35D372BA002"

// A/B方向風速 [m/s] - int16 x 2, 単位: 0.01 m/s (正負あり)
// [0-1] A direction speed, [2-3] B direction speed
#define UUID_CHAR_WIND_AXIS_SPEEDS    "E147A12A-67FF-4249-930B-C35D372BA015"
#define BLE_WIND_AXIS_SPEEDS_SIZE     4

// Device Status v2 - uint8[7]
// nodeId, dataValid, version, status, serviceStatus, activeCause, ntcReadingStatus
#define UUID_CHAR_SENSOR_STATUS       "E147A12A-67FF-4249-930B-C35D372BA003"
#define BLE_SENSOR_STATUS_SIZE        7

// Sample Metadata - 12bytes
// [0] protocol version
// [1] flags: bit0 valid, bit1 sourceI2c, bit2 sourceUart, bit3 stale
// [2-3] sample sequence (STM32 DATA_SEQ or ESP32 local seq, uint16 LE)
// [4-7] ESP32 receive timestamp millis (uint32 LE)
// [8] source: 0=unknown, 1=I2C, 2=UART, 3=simulation
// [9] remote status
// [10] remote error
// [11] local error
#define UUID_CHAR_SAMPLE_METADATA     "E147A12A-67FF-4249-930B-C35D372BA009"
#define BLE_SAMPLE_METADATA_PROTOCOL_VERSION 0x01
#define BLE_SAMPLE_METADATA_STATUS_SIZE 12

#define BLE_SAMPLE_METADATA_FLAG_VALID       0x01
#define BLE_SAMPLE_METADATA_FLAG_SOURCE_I2C  0x02
#define BLE_SAMPLE_METADATA_FLAG_SOURCE_UART 0x04
#define BLE_SAMPLE_METADATA_FLAG_STALE       0x08

#define BLE_SAMPLE_SOURCE_UNKNOWN      0x00
#define BLE_SAMPLE_SOURCE_I2C          0x01
#define BLE_SAMPLE_SOURCE_UART         0x02
#define BLE_SAMPLE_SOURCE_SIMULATION   0x03

// BLE Capabilities - 8bytes
// [0] protocol version
// [1] feature flags 0: bit0 currentTime, bit1 deviceInfo, bit2 sdStatus,
//                    bit3 sdLogControl, bit4 deviceMode, bit5 stm32FwVersion,
//                    bit6 i2cConfigControl, bit7 sampleMetadata
// [2] feature flags 1: bit0 deviceHealth, bit1 sdLogDetail,
//                    bit2 capabilities, bit3 windNotifications,
//                    bit4 rtcReadWrite, bit5 i2cConfigWrite, bit6 sdLogWrite,
//                    bit7 sdLogSettings
// [3] BLE interface revision
// [4] max measurement notify rate Hz
// [5] diagnostic poll hint seconds
// [6] feature flags 2: bit0 deviceIdentify, bit1 ledBrightness,
//                    bit2 otaControl, bit3 resetControl,
//                    bit4 stm32UpdateControl
// [7] feature flags 3: bit0 timezoneConfig
#define UUID_CHAR_CAPABILITIES         "E147A12A-67FF-4249-930B-C35D372BA00B"
#define BLE_CAPABILITIES_PROTOCOL_VERSION 0x01
#define BLE_CAPABILITIES_STATUS_SIZE   8

#define BLE_CAP_FLAG_CURRENT_TIME      0x01
#define BLE_CAP_FLAG_DEVICE_INFO       0x02
#define BLE_CAP_FLAG_SD_STATUS         0x04
#define BLE_CAP_FLAG_SD_LOG_CONTROL    0x08
#define BLE_CAP_FLAG_DEVICE_MODE       0x10
#define BLE_CAP_FLAG_STM32_FW_VERSION  0x20
#define BLE_CAP_FLAG_I2C_CONFIG        0x40
#define BLE_CAP_FLAG_SAMPLE_METADATA   0x80

#define BLE_CAP_FLAG2_DEVICE_HEALTH    0x01
#define BLE_CAP_FLAG2_SD_LOG_DETAIL    0x02
#define BLE_CAP_FLAG2_CAPABILITIES     0x04
#define BLE_CAP_FLAG2_WIND_NOTIFY      0x08
#define BLE_CAP_FLAG2_RTC_READ_WRITE   0x10
#define BLE_CAP_FLAG2_I2C_CONFIG_WRITE 0x20
#define BLE_CAP_FLAG2_SD_LOG_WRITE     0x40
#define BLE_CAP_FLAG2_SD_LOG_SETTINGS  0x80

#define BLE_CAP_FLAG3_DEVICE_IDENTIFY  0x01
#define BLE_CAP_FLAG3_LED_BRIGHTNESS   0x02
#define BLE_CAP_FLAG3_OTA_CONTROL      0x04
#define BLE_CAP_FLAG3_RESET_CONTROL    0x08
#define BLE_CAP_FLAG3_STM32_UPDATE_CONTROL 0x10
#define BLE_CAP_FLAG3_LED_WIND_REACTIVE 0x20

#define BLE_CAP_FLAG4_TIMEZONE_CONFIG 0x01

#define BLE_INTERFACE_REVISION         0x0E
#define BLE_DIAGNOSTIC_POLL_HINT_SEC   2

// RTC Timezone Control - strict request and 28-byte status.
#define UUID_CHAR_RTC_TIMEZONE_CONTROL "E147A12A-67FF-4249-930B-C35D372BA019"
#define BLE_RTC_TIMEZONE_PROTOCOL_VERSION 0x01
#define BLE_RTC_TIMEZONE_SET_ZONE_SIZE 6
#define BLE_RTC_TIMEZONE_SYNC_SIZE 14
#define BLE_RTC_TIMEZONE_STATUS_SIZE 28

#define BLE_RTC_TIMEZONE_OP_SET_ZONE 0x01
#define BLE_RTC_TIMEZONE_OP_SYNC_UTC_AND_ZONE 0x02

#define BLE_RTC_TIMEZONE_RESULT_OK 0x00
#define BLE_RTC_TIMEZONE_RESULT_INVALID_LENGTH 0x01
#define BLE_RTC_TIMEZONE_RESULT_INVALID_VERSION 0x02
#define BLE_RTC_TIMEZONE_RESULT_INVALID_OP 0x03
#define BLE_RTC_TIMEZONE_RESULT_UNSUPPORTED_ZONE 0x04
#define BLE_RTC_TIMEZONE_RESULT_TIME_OUT_OF_RANGE 0x05
#define BLE_RTC_TIMEZONE_RESULT_NVS_FAILED 0x06
#define BLE_RTC_TIMEZONE_RESULT_RTC_WRITE_FAILED 0x07
#define BLE_RTC_TIMEZONE_RESULT_READBACK_FAILED 0x08
#define BLE_RTC_TIMEZONE_RESULT_BUSY 0x09

#define BLE_RTC_TIMEZONE_FLAG_RTC_DETECTED 0x01
#define BLE_RTC_TIMEZONE_FLAG_RTC_READABLE 0x02
#define BLE_RTC_TIMEZONE_FLAG_UTC_VALID 0x04
#define BLE_RTC_TIMEZONE_FLAG_ZONE_CONFIGURED 0x08
#define BLE_RTC_TIMEZONE_FLAG_NVS_SAVED 0x10
#define BLE_RTC_TIMEZONE_FLAG_DST_ACTIVE 0x20
#define BLE_RTC_TIMEZONE_FLAG_PROCESSING 0x40
#define BLE_RTC_TIMEZONE_FLAG_ERROR 0x80

// Device Identify - 1byte write request
// Write request:
//   [0] op: 1=blink yellow for 30 seconds
#define UUID_CHAR_DEVICE_IDENTIFY      "E147A12A-67FF-4249-930B-C35D372BA00E"
#define BLE_DEVICE_IDENTIFY_OP_BLINK   0x01

// LED Brightness - 1byte read/write/notify
// Value:
//   [0] brightness: one of 0,16,32,50,75,110,170,255. Arbitrary legacy
//       values are normalized to the nearest step and persisted to ESP32 NVS.
#define UUID_CHAR_LED_BRIGHTNESS       "E147A12A-67FF-4249-930B-C35D372BA00F"

// LED Wind Reactive - Read/Write/Notify status, 1 or 3byte write request.
// Write: [0] op (0=read, 1=set config), [1] enabled 0/1, [2] theme 0..4.
// Status: [0] protocol, [1] last op, [2] result, [3] flags,
//         [4] theme, [5] reserved.
#define UUID_CHAR_LED_WIND_REACTIVE    "E147A12A-67FF-4249-930B-C35D372BA018"
#define BLE_LED_WIND_REACTIVE_PROTOCOL_VERSION 0x01
#define BLE_LED_WIND_REACTIVE_STATUS_SIZE 6
#define BLE_LED_WIND_REACTIVE_OP_READ 0x00
#define BLE_LED_WIND_REACTIVE_OP_SET_CONFIG 0x01
#define BLE_LED_WIND_REACTIVE_RESULT_OK 0x00
#define BLE_LED_WIND_REACTIVE_RESULT_QUEUED 0x01
#define BLE_LED_WIND_REACTIVE_RESULT_BUSY 0x02
#define BLE_LED_WIND_REACTIVE_RESULT_INVALID_LENGTH 0x03
#define BLE_LED_WIND_REACTIVE_RESULT_INVALID_OP 0x04
#define BLE_LED_WIND_REACTIVE_RESULT_INVALID_VALUE 0x05
#define BLE_LED_WIND_REACTIVE_RESULT_FAILED 0x06
#define BLE_LED_WIND_REACTIVE_FLAG_ENABLED 0x01
#define BLE_LED_WIND_REACTIVE_FLAG_ACTIVE 0x02
#define BLE_LED_WIND_REACTIVE_FLAG_PERSISTED 0x04

// OTA Control - variable length read/notify status, 1byte write request
// Write request:
//   [0] op: 0=read, 1=prepare_portal, 2=activate_portal, 3=stop_or_cancel
// Status value:
//   [0] protocol version
//   [1] last op
//   [2] result
//   [3] OtaState
//   [4] progress 0..100
//   [5] flags: bit0 portalActive, bit1 updating, bit2 hasCredentials, bit3 error,
//              bit4 physicalAuthRequired, bit5 physicalAuthGranted,
//              bit6 recoveryPortal, bit7 reserved=0
//   [6-9] uploaded bytes (uint32 LE)
//   [10-13] total bytes (uint32 LE)
//   [14-17] current authorization/prepared/portal phase remaining seconds (uint32 LE)
//   [18] node ID
//   [19] ssid length
//   [20] password length
//   [21] token length
//   [22] ip length
//   [23..] ssid + password + token + ip
#define UUID_CHAR_OTA_CONTROL          "E147A12A-67FF-4249-930B-C35D372BA010"
#define BLE_OTA_CONTROL_PROTOCOL_VERSION 0x03
#define BLE_OTA_CONTROL_STATUS_HEADER_SIZE 23
#define BLE_OTA_CONTROL_STATUS_MAX_SIZE 128

#define BLE_OTA_OP_READ                0x00
#define BLE_OTA_OP_PREPARE_PORTAL      0x01
#define BLE_OTA_OP_ACTIVATE_PORTAL     0x02
#define BLE_OTA_OP_STOP_OR_CANCEL      0x03

#define BLE_OTA_RESULT_OK              0x00
#define BLE_OTA_RESULT_QUEUED          0x01
#define BLE_OTA_RESULT_BUSY            0x02
#define BLE_OTA_RESULT_INVALID_LENGTH  0x03
#define BLE_OTA_RESULT_INVALID_OP      0x04
#define BLE_OTA_RESULT_UNAVAILABLE     0x05
#define BLE_OTA_RESULT_FAILED          0x06
#define BLE_OTA_RESULT_AUTHORIZATION_REQUIRED 0x07
#define BLE_OTA_RESULT_AUTHORIZATION_EXPIRED  0x08
#define BLE_OTA_RESULT_PEER_CONFLICT          0x09

#define BLE_OTA_FLAG_PORTAL_ACTIVE     0x01
#define BLE_OTA_FLAG_UPDATING          0x02
#define BLE_OTA_FLAG_HAS_CREDENTIALS   0x04
#define BLE_OTA_FLAG_ERROR             0x08
#define BLE_OTA_FLAG_PHYSICAL_AUTH_REQUIRED 0x10
#define BLE_OTA_FLAG_PHYSICAL_AUTH_GRANTED  0x20
#define BLE_OTA_FLAG_RECOVERY_PORTAL         0x40

// STM32 Update Control - variable length read/notify status, compatible write request
// SoftAP/DNS/WebServer/token/password are shared with ESP32 OTA Control.
// Write request:
//   [0] op: 0=read, 1=prepare_portal, 2=activate_portal, 3=stop_or_cancel
// Extended prepare_portal request:
//   [0] op=1
//   [1] flags: bit0 legacy Node label present, bit1 target present, bit2 releaseTag present
//   [2] informational Node label (valid only when bit0 is set; never an OTA gate)
//   [3] target length
//   [4] releaseTag length
//   [5..] target + releaseTag UTF-8 bytes
// Status value uses the same layout as OTA Control:
//   [0] protocol version
//   [1] last op
//   [2] result
//   [3] OtaState
//   [4] progress 0..100
//   [5] flags: same v3 physical authorization layout as OTA Control
//   [6-9] uploaded bytes (uint32 LE)
//   [10-13] total bytes (uint32 LE)
//   [14-17] current authorization/prepared/portal phase remaining seconds (uint32 LE)
//   [18] node ID
//   [19] ssid length
//   [20] password length
//   [21] token length
//   [22] ip length
//   [23..] ssid + password + token + ip
#define UUID_CHAR_STM32_UPDATE_CONTROL  "E147A12A-67FF-4249-930B-C35D372BA016"
#define BLE_STM32_UPDATE_CONTROL_PROTOCOL_VERSION 0x03
#define BLE_STM32_UPDATE_CONTROL_STATUS_HEADER_SIZE 23
#define BLE_STM32_UPDATE_CONTROL_STATUS_MAX_SIZE 128
#define BLE_STM32_UPDATE_CONTROL_REQUEST_MAX_SIZE 100
#define BLE_STM32_UPDATE_CONTROL_TARGET_MAX_LEN 32
#define BLE_STM32_UPDATE_CONTROL_RELEASE_TAG_MAX_LEN 64

#define BLE_STM32_UPDATE_OP_READ                0x00
#define BLE_STM32_UPDATE_OP_PREPARE_PORTAL      0x01
#define BLE_STM32_UPDATE_OP_ACTIVATE_PORTAL     0x02
#define BLE_STM32_UPDATE_OP_STOP_OR_CANCEL      0x03

#define BLE_STM32_UPDATE_RESULT_OK              0x00
#define BLE_STM32_UPDATE_RESULT_QUEUED          0x01
#define BLE_STM32_UPDATE_RESULT_BUSY            0x02
#define BLE_STM32_UPDATE_RESULT_INVALID_LENGTH  0x03
#define BLE_STM32_UPDATE_RESULT_INVALID_OP      0x04
#define BLE_STM32_UPDATE_RESULT_UNAVAILABLE     0x05
#define BLE_STM32_UPDATE_RESULT_FAILED          0x06
#define BLE_STM32_UPDATE_RESULT_AUTHORIZATION_REQUIRED 0x07
#define BLE_STM32_UPDATE_RESULT_AUTHORIZATION_EXPIRED  0x08
#define BLE_STM32_UPDATE_RESULT_PEER_CONFLICT          0x09

#define BLE_STM32_UPDATE_FLAG_PORTAL_ACTIVE     0x01
#define BLE_STM32_UPDATE_FLAG_UPDATING          0x02
#define BLE_STM32_UPDATE_FLAG_HAS_CREDENTIALS   0x04
#define BLE_STM32_UPDATE_FLAG_ERROR             0x08
#define BLE_STM32_UPDATE_FLAG_PHYSICAL_AUTH_REQUIRED 0x10
#define BLE_STM32_UPDATE_FLAG_PHYSICAL_AUTH_GRANTED  0x20
#define BLE_STM32_UPDATE_FLAG_RECOVERY_PORTAL         0x40

// Reset Control - 6bytes status, 1byte write request
// Write request:
//   [0] op: 0=read, 1=reset_esp32, 2=reset_stm32
// Status value:
//   [0] protocol version
//   [1] last op
//   [2] result
//   [3] flags: bit0 esp32ResetPending, bit1 stm32ResetPending,
//              bit2 esp32Rebooting, bit3 stm32Resetting
//   [4] target: 0=none, 1=esp32, 2=stm32
//   [5] reserved
#define UUID_CHAR_RESET_CONTROL        "E147A12A-67FF-4249-930B-C35D372BA013"
#define BLE_RESET_CONTROL_PROTOCOL_VERSION 0x01
#define BLE_RESET_CONTROL_STATUS_SIZE  6

#define BLE_RESET_OP_READ              0x00
#define BLE_RESET_OP_ESP32             0x01
#define BLE_RESET_OP_STM32             0x02

#define BLE_RESET_RESULT_OK             0x00
#define BLE_RESET_RESULT_QUEUED         0x01
#define BLE_RESET_RESULT_BUSY           0x02
#define BLE_RESET_RESULT_INVALID_LENGTH 0x03
#define BLE_RESET_RESULT_INVALID_OP     0x04
#define BLE_RESET_RESULT_UNAVAILABLE    0x05
#define BLE_RESET_RESULT_FAILED         0x06

#define BLE_RESET_TARGET_NONE          0x00
#define BLE_RESET_TARGET_ESP32         0x01
#define BLE_RESET_TARGET_STM32         0x02

#define BLE_RESET_FLAG_ESP32_PENDING   0x01
#define BLE_RESET_FLAG_STM32_PENDING   0x02
#define BLE_RESET_FLAG_ESP32_REBOOTING 0x04
#define BLE_RESET_FLAG_STM32_RESETTING 0x08

// SDカードステータス - 8bytes
// [0] state: 0=未初期化, 1=カードなし, 2=エラー, 3=準備完了, 4=ログ中
// [1] usage%: 0-100
// [2-3] freeSpace MB (uint16 LE)
// [4-5] totalSpace MB (uint16 LE)
// [6] cardType: 0=None, 1=Unknown, 2=MMC, 3=SD, 4=SDHC/SDXC
// [7] reserved
#define UUID_CHAR_SD_STATUS           "E147A12A-67FF-4249-930B-C35D372BA004"
#define SD_STATUS_DATA_SIZE           8

// SD Log Control - 6bytes status, 1byte write request
// Write request:
//   [0] op: 0=read, 1=start, 2=stop
// Status value:
//   [0] protocol version
//   [1] last op
//   [2] result
//   [3] SdLoggerState
//   [4] flags: bit0 cardAvailable, bit1 loggingEnabled, bit2 canLog
//   [5] SdLoggerStopReason
#define UUID_CHAR_SD_LOG_CONTROL      "E147A12A-67FF-4249-930B-C35D372BA008"
#define BLE_SD_LOG_CONTROL_PROTOCOL_VERSION 0x01
#define BLE_SD_LOG_CONTROL_STATUS_SIZE 6

#define BLE_SD_LOG_OP_READ            0x00
#define BLE_SD_LOG_OP_START           0x01
#define BLE_SD_LOG_OP_STOP            0x02

#define BLE_SD_LOG_RESULT_OK             0x00
#define BLE_SD_LOG_RESULT_QUEUED         0x01
#define BLE_SD_LOG_RESULT_BUSY           0x02
#define BLE_SD_LOG_RESULT_INVALID_LENGTH 0x03
#define BLE_SD_LOG_RESULT_INVALID_OP     0x04
#define BLE_SD_LOG_RESULT_UNAVAILABLE    0x05
#define BLE_SD_LOG_RESULT_FAILED         0x06
#define BLE_SD_LOG_RESULT_WRONG_MODE     0x07

#define BLE_SD_LOG_FLAG_CARD_AVAILABLE   0x01
#define BLE_SD_LOG_FLAG_LOGGING_ENABLED  0x02
#define BLE_SD_LOG_FLAG_CAN_LOG          0x04

// SD Log Detail - 20bytes
// [0] protocol version
// [1] flags: bit0 cardAvailable, bit1 loggingEnabled, bit2 canLog,
//            bit3 fileOpen, bit4 rtcTimestamping, bit5 slowWrite,
//            bit6 errorStop
// [2] SdLoggerState
// [3] SdLoggerStopReason
// [4] log rate Hz
// [5] reserved
// [6-9] log count (uint32 LE)
// [10-13] flush count (uint32 LE)
// [14-15] buffered bytes (uint16 LE)
// [16-17] last SD write duration ms (uint16 LE)
// [18-19] last log age seconds (uint16 LE, 0xFFFF=never)
#define UUID_CHAR_SD_LOG_DETAIL       "E147A12A-67FF-4249-930B-C35D372BA00C"
#define BLE_SD_LOG_DETAIL_PROTOCOL_VERSION 0x02
#define BLE_SD_LOG_DETAIL_STATUS_SIZE 36
// v2 retains the first 20 bytes; byte 5 bits: requested/paused/recovering/quiescent.
// Read suffix: [20..23] synced rows, [24..27] dropped rows,
// [28..31] uncertain rows, [32..33] queue depth, [34..35] reserved.
// Notifications carry only the 20-byte prefix, including at the minimum MTU.

#define BLE_SD_LOG_DETAIL_FLAG_CARD_AVAILABLE  0x01
#define BLE_SD_LOG_DETAIL_FLAG_LOGGING_ENABLED 0x02
#define BLE_SD_LOG_DETAIL_FLAG_CAN_LOG         0x04
#define BLE_SD_LOG_DETAIL_FLAG_FILE_OPEN       0x08
#define BLE_SD_LOG_DETAIL_FLAG_RTC_TIMESTAMP   0x10
#define BLE_SD_LOG_DETAIL_FLAG_SLOW_WRITE      0x20
#define BLE_SD_LOG_DETAIL_FLAG_ERROR_STOP      0x40

// SD Log Settings - 20bytes status, 1, 2, or 5bytes write request
// Write request:
//   [0] op: 0=read, 1=set_interval_ms, 2=restore_default, 3=set_auto_start
//   [1-4] interval ms (uint32 LE, set_interval_ms only)
//   [1] auto start enabled: 0=false, 1=true (set_auto_start only)
// Status value:
//   [0] protocol version
//   [1] last op
//   [2] result
//   [3] flags: bit0 persisted, bit1 stmIntervalKnown, bit2 defaultInterval,
//              bit3 autoStartEnabled
//   [4-7] current interval ms (uint32 LE)
//   [8-11] minimum allowed interval ms (uint32 LE)
//   [12-15] maximum allowed interval ms (uint32 LE)
//   [16-19] STM32 observed/effective interval ms (uint32 LE, 0=unknown)
#define UUID_CHAR_SD_LOG_SETTINGS     "E147A12A-67FF-4249-930B-C35D372BA00D"
#define BLE_SD_LOG_SETTINGS_PROTOCOL_VERSION 0x03
#define BLE_SD_LOG_SETTINGS_STATUS_SIZE 20

#define BLE_SD_LOG_SETTINGS_OP_READ             0x00
#define BLE_SD_LOG_SETTINGS_OP_SET_INTERVAL_MS  0x01
#define BLE_SD_LOG_SETTINGS_OP_RESTORE_DEFAULT  0x02
#define BLE_SD_LOG_SETTINGS_OP_SET_AUTO_START   0x03

#define BLE_SD_LOG_SETTINGS_RESULT_OK             0x00
#define BLE_SD_LOG_SETTINGS_RESULT_QUEUED         0x01
#define BLE_SD_LOG_SETTINGS_RESULT_BUSY           0x02
#define BLE_SD_LOG_SETTINGS_RESULT_INVALID_LENGTH 0x03
#define BLE_SD_LOG_SETTINGS_RESULT_INVALID_OP     0x04
#define BLE_SD_LOG_SETTINGS_RESULT_UNAVAILABLE    0x05
#define BLE_SD_LOG_SETTINGS_RESULT_FAILED         0x06
#define BLE_SD_LOG_SETTINGS_RESULT_OUT_OF_RANGE   0x07
#define BLE_SD_LOG_SETTINGS_RESULT_SOURCE_INTERVAL_UNKNOWN 0x08
#define BLE_SD_LOG_SETTINGS_RESULT_INTERVAL_NOT_ALIGNED    0x09
#define BLE_SD_LOG_SETTINGS_RESULT_INVALID_VALUE 0x0A

#define BLE_SD_LOG_SETTINGS_FLAG_PERSISTED          0x01
#define BLE_SD_LOG_SETTINGS_FLAG_STM_INTERVAL_KNOWN 0x02
#define BLE_SD_LOG_SETTINGS_FLAG_DEFAULT_INTERVAL   0x04
#define BLE_SD_LOG_SETTINGS_FLAG_AUTO_START_ENABLED 0x08

// ESP32動作モード - 4bytes (protocol, mode, flags, PHY compatibility value)
// [0] protocol version
// [1] mode: 0=UART計測, 1=I2C計測, 2=COMMAND, 3=UARTブリッジ, 4=WiFiポータル, 5=STM32 bootloader, 6=STM32 update
// [2] flags: bit0 BLE connected, bit1 UART bridge, bit2 I2C measure, bit3 command, bit4 bootloader, bit5 WiFi portal, bit6 STM32 update
// [3] PHY compatibility value: always 0=1M. Kept for protocol v1 consumers.
#define UUID_CHAR_DEVICE_MODE         "E147A12A-67FF-4249-930B-C35D372BA006"
#define BLE_DEVICE_MODE_PROTOCOL_VERSION 0x01
#define BLE_DEVICE_MODE_STATUS_SIZE   4
#define BLE_DEVICE_MODE_PHY_1M        0x00

#define BLE_DEVICE_MODE_UART_MEASURE  0x00
#define BLE_DEVICE_MODE_I2C_MEASURE   0x01
#define BLE_DEVICE_MODE_COMMAND       0x02
#define BLE_DEVICE_MODE_UART_BRIDGE   0x03
#define BLE_DEVICE_MODE_WIFI_PORTAL   0x04
#define BLE_DEVICE_MODE_BOOTLOADER    0x05
#define BLE_DEVICE_MODE_STM32_UPDATE  0x06

#define BLE_DEVICE_MODE_FLAG_CONNECTED    0x01
#define BLE_DEVICE_MODE_FLAG_UART_BRIDGE  0x02
#define BLE_DEVICE_MODE_FLAG_I2C_MEASURE  0x04
#define BLE_DEVICE_MODE_FLAG_COMMAND      0x08
#define BLE_DEVICE_MODE_FLAG_BOOTLOADER   0x10
#define BLE_DEVICE_MODE_FLAG_WIFI_PORTAL  0x20
#define BLE_DEVICE_MODE_FLAG_STM32_UPDATE 0x40

// Device Health Summary - 12bytes
// [0] protocol version
// [1] system flags: bit0 bleConnected, bit1 i2cDetected, bit2 rtcAvailable,
//                   bit3 sdAvailable, bit4 loggingEnabled, bit5 configDirty,
//                   bit6 rebootRequired, bit7 errorActive
// [2] ESP32 device mode
// [3] ESP32 last error (currently 0)
// [4] STM32 REG_VERSION
// [5] STM32 STATUS
// [6] STM32 LAST_ERROR
// [7] ESP32 local I2C error
// [8] SD state
// [9] SD stop reason
// [10] RTC flags: bit0 present, bit1 running, bit2 timeValid,
//                  bit3 voltageLow, bit4 clockStopped
// [11] reserved
#define UUID_CHAR_DEVICE_HEALTH       "E147A12A-67FF-4249-930B-C35D372BA00A"
#define BLE_DEVICE_HEALTH_PROTOCOL_VERSION 0x02
#define BLE_DEVICE_HEALTH_STATUS_SIZE 12

#define BLE_DEVICE_HEALTH_FLAG_BLE_CONNECTED   0x01
#define BLE_DEVICE_HEALTH_FLAG_I2C_DETECTED    0x02
#define BLE_DEVICE_HEALTH_FLAG_RTC_AVAILABLE   0x04
#define BLE_DEVICE_HEALTH_FLAG_SD_AVAILABLE    0x08
#define BLE_DEVICE_HEALTH_FLAG_LOGGING_ENABLED 0x10
#define BLE_DEVICE_HEALTH_FLAG_CONFIG_DIRTY    0x20
#define BLE_DEVICE_HEALTH_FLAG_REBOOT_REQUIRED 0x40
#define BLE_DEVICE_HEALTH_FLAG_ERROR_ACTIVE    0x80

#define BLE_DEVICE_HEALTH_RTC_FLAG_PRESENT     0x01
#define BLE_DEVICE_HEALTH_RTC_FLAG_RUNNING     0x02
#define BLE_DEVICE_HEALTH_RTC_FLAG_TIME_VALID  0x04
#define BLE_DEVICE_HEALTH_RTC_FLAG_VOLTAGE_LOW 0x08
#define BLE_DEVICE_HEALTH_RTC_FLAG_CLOCK_STOPPED 0x10

// STM32 firmware identity - protocol v2, 12 bytes
// [0] protocol version
// [1] flags: bit0 i2cClientPresent, bit1 detected, bit2 readOk
// [2] local I2C client error
// [3] remote REG_VERSION
// [4-7] versionCode (uint32 LE)
// [8-11] positive FWREV (uint32 LE)
#define UUID_CHAR_STM32_FIRMWARE_VERSION "E147A12A-67FF-4249-930B-C35D372BA007"
#define BLE_STM32_FW_VERSION_PROTOCOL_VERSION_V2 0x02
#define BLE_STM32_FW_VERSION_STATUS_SIZE 12
#define BLE_STM32_FW_VERSION_FLAG_CLIENT_PRESENT 0x01
#define BLE_STM32_FW_VERSION_FLAG_DETECTED 0x02
#define BLE_STM32_FW_VERSION_FLAG_READ_OK 0x04

// I2C config control/status - 16bytes status, short write request
// Write request:
//   [0] op
//   [1] value (set ops only)
// Status value:
//   [0] protocol version
//   [1] last op
//   [2] result
//   [3] remote CMD_STATUS
//   [4] remote LAST_ERROR
//   [5] CFG_FLAGS
//   [6] CFG_NODE_ID
//   [7] CFG_AVG_CYCLE
//   [8] CFG_WIND_DIR_INSTALL_MODE
//   [9] CFG_I2C_ADDR
//   [10] I2C_SLAVE_ENABLED
//   [11] MEAS_INTERVAL_MS
//   [12] local I2C client error
//   [13] remote REG_VERSION
//   [14] current ESP32 target I2C address
//   [15] status flags: bit0 rebootRequired, bit1 detected, bit2 configWriteSupported
//   [16] ESP32 operation sequence
//   [17] STM32 CMD_RESULT_SEQ (0 when unavailable)
#define UUID_CHAR_I2C_CONFIG_CONTROL  "E147A12A-67FF-4249-930B-C35D372BA005"
#define BLE_I2C_CONFIG_PROTOCOL_VERSION 0x02
#define BLE_I2C_CONFIG_STATUS_SIZE    18

#define BLE_I2C_CONFIG_OP_READ_CONFIG       0x00
#define BLE_I2C_CONFIG_OP_SET_NODE_ID       0x10
#define BLE_I2C_CONFIG_OP_SET_AVG_CYCLE     0x11
#define BLE_I2C_CONFIG_OP_SET_WIND_MODE     0x12
#define BLE_I2C_CONFIG_OP_SET_I2C_ADDR      0x13
#define BLE_I2C_CONFIG_OP_SAVE_CONFIG       0x20
#define BLE_I2C_CONFIG_OP_DISCARD_CONFIG    0x21
#define BLE_I2C_CONFIG_OP_RESTORE_DEFAULTS  0x22
#define BLE_I2C_CONFIG_OP_CLEAR_ERROR       0x23

#define BLE_I2C_CONFIG_RESULT_OK             0x00
#define BLE_I2C_CONFIG_RESULT_QUEUED         0x01
// Terminal rejection for this request. QUEUED is the only non-terminal result.
#define BLE_I2C_CONFIG_RESULT_BUSY           0x02
#define BLE_I2C_CONFIG_RESULT_INVALID_LENGTH 0x03
#define BLE_I2C_CONFIG_RESULT_INVALID_OP     0x04
#define BLE_I2C_CONFIG_RESULT_UNAVAILABLE    0x05
#define BLE_I2C_CONFIG_RESULT_I2C_FAILED     0x06
#define BLE_I2C_CONFIG_RESULT_UNSUPPORTED    0x07


// ============================================
// アドバタイジングデータ構造
// ============================================
// Service Data (0x16) を使用 - Environmental Sensing Serviceに関連付け
// サービスUUID + 計測データ（12バイト）
//
// [0-1] Service UUID: 0x1A18 (Environmental Sensing, Little Endian)
// [2]   Node ID
// [3]   Valid flag + reserved
// [4-5] Wind Direction (0.01°, uint16 LE)
// [6-7] Wind Speed (0.01 m/s, uint16 LE)
// [8-9] Temperature (0.01°C, int16 LE)
// [10-11] Sound Speed (0.01 m/s, uint16 LE)
// [12-13] Heading Speed (0.01 m/s, int16 LE)
//
// Total: 14 bytes (2 + 12)

#define BLE_SERVICE_DATA_SIZE         14
#define BLE_ENV_SENSING_UUID          0x181A  // Environmental Sensing Service

// ============================================
// データ変換マクロ
// BLE標準フォーマット（0.01単位）への変換
// ============================================

// float [°] → uint16 [0.01°]
#define WIND_DIR_TO_BLE(deg)    ((uint16_t)((deg) * 100))

// float [m/s] → uint16 [0.01 m/s]
#define WIND_SPEED_TO_BLE(spd)  ((uint16_t)((spd) * 100))

// float [m/s] → int16 [0.01 m/s] (正負あり)
#define HEADING_TO_BLE(spd)     ((int16_t)((spd) * 100))

// float [m/s] → int16 [0.01 m/s] (正負あり)
#define WIND_AXIS_SPEED_TO_BLE(spd) ((int16_t)((spd) * 100))

// float [℃] → int16 [0.01℃]
#define TEMP_TO_BLE(temp)       ((int16_t)((temp) * 100))

// float [m/s] → uint16 [0.01 m/s]
#define SOUND_SPEED_TO_BLE(spd) ((uint16_t)((spd) * 100))

#endif // BLE_CONFIG_H
