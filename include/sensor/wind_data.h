/**
 * @file wind_data.h
 * @brief 風速計データ構造定義
 * @date 2025-12-03
 * 
 * ULSA EVO風速計からのUARTデータフォーマット:
 * #,0,1,359,12.12,24.13,340.13,21.12\r\n
 * |A|B|C|-D-|--E--|--F--|---G--|--H--|
 */

#ifndef WIND_DATA_H
#define WIND_DATA_H

#include <Arduino.h>
#include <math.h>

// ============================================
// バリデーション範囲設定（ここで一括管理）
// ============================================

// UART CSV整数field
#define WIND_NODE_ID_MIN       0
#define WIND_NODE_ID_MAX       255

// STM32 Device Status v2
#define WIND_STATUS_PROTOCOL_VERSION       2
#define WIND_STATUS_DATA_READY             0x80
#define WIND_STATUS_DATA_VALID             0x40
#define WIND_STATUS_HIGH_WIND_CLIPPED      0x20
#define WIND_STATUS_HV_INHIBITED           0x10
#define WIND_STATUS_SAFETY_LATCHED         0x08
#define WIND_STATUS_CALIB_ACTIVE           0x04
#define WIND_STATUS_CONFIG_ACTIVE          0x02
#define WIND_STATUS_EEPROM_BUSY            0x01

#define WIND_SERVICE_I2C_WRITE_LOCKED      0x80
#define WIND_SERVICE_COMMAND_ERROR         0x40
#define WIND_SERVICE_PERSISTENCE_PENDING   0x20
#define WIND_SERVICE_REBOOT_REQUIRED       0x10
#define WIND_SERVICE_BOOT_FAULT            0x08
#define WIND_SERVICE_NTC_READING_VALID     0x04
#define WIND_SERVICE_SAFETY_MONITOR_ACTIVE 0x02
#define WIND_SERVICE_PERSISTENCE_FAILED    0x01

#define WIND_CAUSE_NONE                     0
#define WIND_CAUSE_HIGH_WIND_LIMIT          1
#define WIND_CAUSE_LOW_TEMPERATURE          2
#define WIND_CAUSE_OVER_TEMPERATURE         3
#define WIND_CAUSE_TEMPERATURE_SENSOR_FAULT 4
#define WIND_CAUSE_MEASUREMENT_FAULT        5
#define WIND_CAUSE_CONFIGURATION_FAULT      6
#define WIND_CAUSE_STARTUP_HARDWARE_FAULT   7
#define WIND_CAUSE_EEPROM_FAULT             8
#define WIND_CAUSE_INTEGRITY_FAULT          9
#define WIND_CAUSE_IDENTITY_FAULT          10
#define WIND_CAUSE_PERSISTENCE_FAULT       11
#define WIND_CAUSE_RESTORED_UNKNOWN_LATCH  12

#define WIND_NTC_READING_VALID          0
#define WIND_NTC_READING_ADC_LOW_RAIL   1
#define WIND_NTC_READING_ADC_HIGH_RAIL  2
#define WIND_NTC_READING_MATH_FAULT     3
#define WIND_NTC_READING_NOT_SAMPLED    0xFF

#define WIND_OUTPUT_NOT_READY                 0
#define WIND_OUTPUT_OK                        1
#define WIND_OUTPUT_HIGH_WIND_CLIPPED         2
#define WIND_OUTPUT_LOW_TEMPERATURE           3
#define WIND_OUTPUT_OVER_TEMPERATURE          4
#define WIND_OUTPUT_TEMPERATURE_SENSOR_FAULT  5
#define WIND_OUTPUT_MEASUREMENT_INVALID       6
#define WIND_OUTPUT_CONFIGURATION_FAULT       7
#define WIND_OUTPUT_STARTUP_HARDWARE_FAULT    8
#define WIND_OUTPUT_EEPROM_FAULT               9
#define WIND_OUTPUT_INTEGRITY_FAULT           10
#define WIND_OUTPUT_IDENTITY_FAULT            11
#define WIND_OUTPUT_PERSISTENCE_FAULT         12
#define WIND_OUTPUT_REBOOT_REQUIRED           13
#define WIND_OUTPUT_RESTORED_UNKNOWN_LATCH    14

namespace WindDataContract {

inline bool isSupportedCauseCode(int value) {
  return value >= WIND_CAUSE_NONE &&
         value <= WIND_CAUSE_RESTORED_UNKNOWN_LATCH;
}

inline bool isSupportedNtcReadingStatus(int value) {
  return (value >= WIND_NTC_READING_VALID &&
          value <= WIND_NTC_READING_MATH_FAULT) ||
         value == WIND_NTC_READING_NOT_SAMPLED;
}

inline bool projectOutputStatusCode(int code, uint8_t& status,
                                    uint8_t& serviceStatus,
                                    uint8_t& activeCause,
                                    uint8_t& ntcReadingStatus,
                                    bool& dataValid) {
  status = code == WIND_OUTPUT_NOT_READY ? 0 : WIND_STATUS_DATA_READY;
  serviceStatus = 0;
  activeCause = WIND_CAUSE_NONE;
  ntcReadingStatus = WIND_NTC_READING_NOT_SAMPLED;
  dataValid = false;
  switch (code) {
  case WIND_OUTPUT_NOT_READY:
    break;
  case WIND_OUTPUT_OK:
    status |= WIND_STATUS_DATA_VALID;
    dataValid = true;
    break;
  case WIND_OUTPUT_HIGH_WIND_CLIPPED:
    status |= WIND_STATUS_HIGH_WIND_CLIPPED;
    activeCause = WIND_CAUSE_HIGH_WIND_LIMIT;
    break;
  case WIND_OUTPUT_LOW_TEMPERATURE:
    status |= WIND_STATUS_HV_INHIBITED;
    activeCause = WIND_CAUSE_LOW_TEMPERATURE;
    ntcReadingStatus = WIND_NTC_READING_VALID;
    break;
  case WIND_OUTPUT_OVER_TEMPERATURE:
    status |= WIND_STATUS_HV_INHIBITED | WIND_STATUS_SAFETY_LATCHED;
    activeCause = WIND_CAUSE_OVER_TEMPERATURE;
    ntcReadingStatus = WIND_NTC_READING_VALID;
    break;
  case WIND_OUTPUT_TEMPERATURE_SENSOR_FAULT:
    status |= WIND_STATUS_HV_INHIBITED | WIND_STATUS_SAFETY_LATCHED;
    activeCause = WIND_CAUSE_TEMPERATURE_SENSOR_FAULT;
    break;
  case WIND_OUTPUT_MEASUREMENT_INVALID:
    activeCause = WIND_CAUSE_MEASUREMENT_FAULT;
    break;
  case WIND_OUTPUT_CONFIGURATION_FAULT:
    status |= WIND_STATUS_HV_INHIBITED;
    activeCause = WIND_CAUSE_CONFIGURATION_FAULT;
    break;
  case WIND_OUTPUT_STARTUP_HARDWARE_FAULT:
    status |= WIND_STATUS_HV_INHIBITED;
    serviceStatus |= WIND_SERVICE_BOOT_FAULT;
    activeCause = WIND_CAUSE_STARTUP_HARDWARE_FAULT;
    break;
  case WIND_OUTPUT_EEPROM_FAULT:
    status |= WIND_STATUS_HV_INHIBITED;
    serviceStatus |= WIND_SERVICE_BOOT_FAULT;
    activeCause = WIND_CAUSE_EEPROM_FAULT;
    break;
  case WIND_OUTPUT_INTEGRITY_FAULT:
    status |= WIND_STATUS_HV_INHIBITED;
    serviceStatus |= WIND_SERVICE_BOOT_FAULT;
    activeCause = WIND_CAUSE_INTEGRITY_FAULT;
    break;
  case WIND_OUTPUT_IDENTITY_FAULT:
    status |= WIND_STATUS_HV_INHIBITED;
    serviceStatus |= WIND_SERVICE_BOOT_FAULT;
    activeCause = WIND_CAUSE_IDENTITY_FAULT;
    break;
  case WIND_OUTPUT_PERSISTENCE_FAULT:
    status |= WIND_STATUS_HV_INHIBITED | WIND_STATUS_SAFETY_LATCHED;
    serviceStatus |= WIND_SERVICE_PERSISTENCE_PENDING |
                     WIND_SERVICE_PERSISTENCE_FAILED;
    activeCause = WIND_CAUSE_PERSISTENCE_FAULT;
    break;
  case WIND_OUTPUT_REBOOT_REQUIRED:
    status |= WIND_STATUS_HV_INHIBITED;
    serviceStatus |= WIND_SERVICE_REBOOT_REQUIRED;
    break;
  case WIND_OUTPUT_RESTORED_UNKNOWN_LATCH:
    status |= WIND_STATUS_HV_INHIBITED | WIND_STATUS_SAFETY_LATCHED;
    activeCause = WIND_CAUSE_RESTORED_UNKNOWN_LATCH;
    break;
  default:
    return false;
  }
  return true;
}

}  // namespace WindDataContract

// 風向 [°] - コンパス方位角
#define WIND_DIR_MIN        0       // 最小値（北）
#define WIND_DIR_MAX        359     // 最大値

// 風速 [m/s] - 絶対速度
#define WIND_SPEED_MIN      0.0f    // 最小値（無風）
#define WIND_SPEED_MAX      100.0f  // 最大値（台風クラス）

// 機首風速 [m/s] - 筐体0°方向成分（正負あり）
#define HEADING_SPEED_MIN   -100.0f // 最小値（後方から）
#define HEADING_SPEED_MAX   100.0f  // 最大値（前方から）

// A/B方向風速 [m/s] - センサー軸方向成分（正負あり）
#define WIND_AXIS_SPEED_MIN -100.0f
#define WIND_AXIS_SPEED_MAX 100.0f

// 音速 [m/s] - 気温依存
// 注: センサーから異常値が報告される場合があるため範囲を広めに設定
#define SOUND_SPEED_MIN     250.0f  // 最小値（異常値考慮）
#define SOUND_SPEED_MAX     400.0f  // 最大値（約+50℃相当）

// 温度 [℃] - 音仮温度
// 注: センサーから異常値が報告される場合があるため範囲を広めに設定
#define TEMPERATURE_MIN     -100.0f  // 最小値（異常値考慮）
#define TEMPERATURE_MAX     100.0f   // 最大値（異常値考慮）

// 基板温度 [℃] - STM32 NTC診断値
#define BOARD_TEMPERATURE_MIN -100.0f
#define BOARD_TEMPERATURE_MAX 150.0f

// ============================================

/**
 * @brief 風速計データ構造体
 * 
 * UARTから受信した1行分のデータを格納
 */
struct WindData {
  uint8_t nodeId;           // センサーノードID (B)
  bool isValid;             // Status v2 DATA_READY && DATA_VALID の派生bool
  uint8_t statusProtocolVersion;
  uint8_t status;
  uint8_t serviceStatus;
  uint8_t activeCause;
  uint8_t ntcReadingStatus;
  uint16_t windDirection;   // 風向 [0-359°] (D)
  float windSpeed;          // 風速 [m/s] (E)
  float windSpeedA;         // A方向風速 [m/s]
  float windSpeedB;         // B方向風速 [m/s]
  float headingSpeed;       // 機首風速(筐体0°方向) [m/s] (F)
  float soundSpeed;         // 音速 [m/s] (G)
  float temperature;        // 音仮温度 [℃] (H)
  float boardTemperature;   // STM32基板温度 [℃]（診断用）
  bool boardTemperatureValid; // 基板温度がI2C snapshotから取得済み
  uint32_t timestamp;       // 受信時刻 [ms] (millis()による)
  bool sourceSequenceValid; // I2C snapshot DATA_SEQが取得済み
  uint16_t sourceSequence;  // I2C snapshot DATA_SEQ（UART/Simulationでは未使用）
  bool sourceTimestampValid; // DATA_SEQと公開出力周期から復元した出力時刻
  uint32_t sourceTimestamp;  // 復元済みSTM32出力時刻 [ms]（ESP32 millis領域）
  
  /**
   * @brief データ構造体の初期化
   */
  WindData() 
    : nodeId(0)
    , isValid(false)
    , statusProtocolVersion(WIND_STATUS_PROTOCOL_VERSION)
    , status(0)
    , serviceStatus(0)
    , activeCause(WIND_CAUSE_NONE)
    , ntcReadingStatus(WIND_NTC_READING_NOT_SAMPLED)
    , windDirection(0)
    , windSpeed(0.0f)
    , windSpeedA(0.0f)
    , windSpeedB(0.0f)
    , headingSpeed(0.0f)
    , soundSpeed(0.0f)
    , temperature(0.0f)
    , boardTemperature(0.0f)
    , boardTemperatureValid(false)
    , timestamp(0)
    , sourceSequenceValid(false)
    , sourceSequence(0)
    , sourceTimestampValid(false)
    , sourceTimestamp(0)
  {
  }

  /**
   * @brief 絶対風速と風向からA/Bセンサー軸方向成分を更新する
   * @note STM32 calc仕様の windDirection = atan2(A.Vair, B.Vair) と整合させる。
   */
  void updateAxisWindSpeedsFromPolar() {
    const float radians = (float)windDirection * 0.017453292519943295f;
    windSpeedA = windSpeed * sinf(radians);
    windSpeedB = windSpeed * cosf(radians);
  }
  
  /**
   * @brief データの妥当性チェック
   * @return true: データが妥当な範囲内
   * @note 範囲設定はファイル先頭の定数を参照
   */
  bool isDataValid() const {
    if (statusProtocolVersion != WIND_STATUS_PROTOCOL_VERSION) return false;
    const bool statusDataValid =
      (status & (WIND_STATUS_DATA_READY | WIND_STATUS_DATA_VALID)) ==
      (WIND_STATUS_DATA_READY | WIND_STATUS_DATA_VALID);
    if (isValid != statusDataValid) return false;
    if (isValid && activeCause != WIND_CAUSE_NONE) return false;
    if (windDirection > WIND_DIR_MAX) return false;
    if (!isfinite(windSpeed) || !isfinite(windSpeedA) ||
        !isfinite(windSpeedB) || !isfinite(headingSpeed) ||
        !isfinite(soundSpeed) || !isfinite(temperature)) return false;
    if (windSpeed < WIND_SPEED_MIN || windSpeed > WIND_SPEED_MAX) return false;
    if (windSpeedA < WIND_AXIS_SPEED_MIN || windSpeedA > WIND_AXIS_SPEED_MAX) return false;
    if (windSpeedB < WIND_AXIS_SPEED_MIN || windSpeedB > WIND_AXIS_SPEED_MAX) return false;
    if (headingSpeed < HEADING_SPEED_MIN || headingSpeed > HEADING_SPEED_MAX) return false;
    if (isValid && (soundSpeed < SOUND_SPEED_MIN || soundSpeed > SOUND_SPEED_MAX)) return false;
    if (!isValid && (soundSpeed < 0.0f || soundSpeed > SOUND_SPEED_MAX)) return false;
    if (temperature < TEMPERATURE_MIN || temperature > TEMPERATURE_MAX) return false;
    if (!WindDataContract::isSupportedCauseCode(activeCause)) return false;
    if (!WindDataContract::isSupportedNtcReadingStatus(ntcReadingStatus)) return false;
    return true;
  }
};

/**
 * @brief パース統計情報
 * デバッグ・監視用のカウンタ
 */
struct ParseStats {
  uint32_t totalReceived;    // 受信行数
  uint32_t parseSuccess;     // パース成功数
  uint32_t parseErrors;      // パースエラー数
  uint32_t validationErrors; // バリデーションエラー数
  uint32_t lastErrorTime;    // 最後のエラー発生時刻 [ms]
  
  ParseStats() 
    : totalReceived(0)
    , parseSuccess(0)
    , parseErrors(0)
    , validationErrors(0)
    , lastErrorTime(0) {
  }
  
  /**
   * @brief 成功率を計算
   * @return パース成功率 [0.0-1.0]
   */
  float getSuccessRate() const {
    if (totalReceived == 0) return 1.0f;
    return (float)parseSuccess / (float)totalReceived;
  }
};

#endif // WIND_DATA_H
