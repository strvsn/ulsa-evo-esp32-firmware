/**
 * @file rtc_manager.h
 * @brief PCF8563 RTC管理モジュール
 * @date 2025-12-03
 * 
 * PCF8563 I2C RTCの制御を行う
 * - UTC日時の取得・transactional同期
 * - IANA timezone/DST変換（暗黙の地域fallbackなし）
 * - SDカードログのタイムスタンプ用
 */

#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include "rtc_config_blob.h"
#include "rtc_validation.h"

// ============================================
// PCF8563 I2Cアドレス
// ============================================
#define PCF8563_I2C_ADDRESS   0x51

// ============================================
// PCF8563 レジスタアドレス
// ============================================
#define PCF8563_REG_CONTROL1  0x00
#define PCF8563_REG_CONTROL2  0x01
#define PCF8563_REG_SECONDS   0x02
#define PCF8563_REG_MINUTES   0x03
#define PCF8563_REG_HOURS     0x04
#define PCF8563_REG_DAYS      0x05
#define PCF8563_REG_WEEKDAYS  0x06
#define PCF8563_REG_MONTHS    0x07
#define PCF8563_REG_YEARS     0x08

// Control_status_1
#define PCF8563_CONTROL1_STOP 0x20

// ============================================
// 日時構造体
// ============================================
struct RtcDateTime {
  uint16_t year;      // 2000-2099
  uint8_t  month;     // 1-12
  uint8_t  day;       // 1-31
  uint8_t  weekday;   // 0=日曜, 1=月曜, ... 6=土曜
  uint8_t  hour;      // 0-23
  uint8_t  minute;    // 0-59
  uint8_t  second;    // 0-59
  
  /**
   * @brief 有効な日時かチェック
   */
  bool isValid() const {
    return ulsa::rtc::isValidDateTime(year, month, day, weekday, hour, minute, second);
  }
  
  /**
   * @brief 曜日名を取得（英語3文字）
   */
  const char* weekdayName() const {
    static const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return (weekday <= 6) ? names[weekday] : "???";
  }
  
  /**
   * @brief 曜日名を取得（日本語）
   */
  const char* weekdayNameJP() const {
    static const char* names[] = {"日", "月", "火", "水", "木", "金", "土"};
    return (weekday <= 6) ? names[weekday] : "?";
  }
};

/**
 * @brief PCF8563の現在の健全性
 *
 * `timeValid` は、I2Cの完全な読出し、STOP=0、VL=0、年月日と一致する曜日を含む
 * 暦日として有効な日時を
 * 全て満たす場合だけtrueになる。電源低下後に過去の時刻を推測して使わないための
 * 状態であり、時刻同期を要求するかどうかの判断に用いる。
 */
struct RtcStatus {
  bool detected;
  bool busReadable;
  bool voltageLow;
  bool clockStopped;
  bool hardwareTimeValid;
  bool timeValid;
  bool utcValid;
  bool zoneConfigured;
  bool zoneResolved;
  bool nvsPersisted;
  bool configPending;
};

enum class RtcOperationResult : uint8_t {
  Ok = 0,
  UnsupportedZone,
  TimeOutOfRange,
  NvsFailed,
  RtcWriteFailed,
  ReadbackFailed,
  LocalTimeAmbiguous,
  TimezoneNotConfigured,
};

/**
 * @brief One coherent UTC/timezone snapshot.
 *
 * Consumers such as the SD task receive this value object and must not perform
 * a second I2C read or timezone calculation. If a persisted zone can no longer
 * be resolved by the bundled TZDB, `utcFallback` keeps UTC logging available
 * with +00:00 while local-facing interfaces remain invalid.
 */
struct RtcTimezoneSnapshot {
  RtcStatus status{};
  RtcDateTime utc{};
  RtcDateTime local{};
  int64_t unixSeconds = 0;
  uint32_t zoneId = 0;
  int16_t totalOffsetMinutes = 0;
  int16_t standardOffsetMinutes = 0;
  int16_t dstOffsetMinutes = 0;
  uint16_t configGeneration = 0;
  bool localValid = false;
  bool utcFallback = false;
};

/**
 * @brief RTC管理クラス
 * 
 * PCF8563 RTCとのI2C通信を管理し、日時の取得・設定を行う
 */
class RtcManager {
public:
  RtcManager();
  
  /**
   * @brief 初期化
   * @param wire I2Cインスタンス
   * @return true: RTC検出成功
   */
  bool begin(TwoWire* wire = &Wire);
  
  /**
   * @brief RTCが利用可能か
   * @return true: RTC検出済み
   */
  bool isAvailable() const;
  
  /**
   * @brief RTCカウンタが停止していないか
   * @return true: I2C読出し成功かつSTOP=0
   *
   * VLフラグによる履歴時刻の有効性は含まない。ログや表示で時刻を使う前には
   * `getUtcDateTime()` または `getStatus()` の `utcValid` を確認する。
   */
  bool isRunning() const;

  /**
   * @brief RTC状態を読出す
   * @param status 状態の格納先
   * @return true: PCF8563の状態・日時ブロックを完全に読出せた
   */
  bool getStatus(RtcStatus& status);
  
  /** RTCレジスタをUTC暦として読む。transactional設定がvalidの場合のみ成功。 */
  bool getUtcDateTime(RtcDateTime& utc);

  /** UTCとIANA zoneから生成したローカル暦を読む。 */
  bool getLocalDateTime(RtcDateTime& local);

  /** UTC、Unix秒、local暦、STD/DST/total offsetを同じ読出しから取得。 */
  bool getTimezoneSnapshot(RtcTimezoneSnapshot& snapshot);

  /**
   * NVS pending -> UTC RTC write/readback -> NVS valid の順で同期する。
   */
  RtcOperationResult syncUtcAndZone(int64_t unixSeconds, uint32_t zoneId);

  /** UTCを変更せずIANA zoneだけをtransactionalに保存する。 */
  RtcOperationResult setTimezoneZoneId(uint32_t zoneId);
  RtcOperationResult setTimezoneName(const char* zoneName);

  /** 設定済みzoneの一意なlocal暦だけをUTCへ変換して同期する。 */
  RtcOperationResult syncLocalDateTime(const RtcDateTime& local);

  bool isTimezoneConfigured() const;
  bool isTimezoneResolved() const;
  bool isConfigPersisted() const;
  RtcConfigState getConfigState() const;
  uint32_t getZoneId() const;
  uint16_t getConfigGeneration() const;
  bool getZoneName(char* buffer, size_t bufferSize) const;

  /**
   * Low-level UTC register access used by recovery/tests. It does not make an
   * unconfigured legacy RTC valid; normal callers must use syncUtcAndZone().
   */
  bool setUtcDateTime(const RtcDateTime& utc);
  
  /**
   * @brief ファイル名用の日付文字列を生成
   * @param dt 日時
   * @param buffer 出力バッファ（最低20文字）
   * @return バッファへのポインタ
   * @example "20251203(Wed)"
   */
  static char* formatDateForFilename(const RtcDateTime& dt, char* buffer);
  
  /**
   * @brief ファイル名用の時刻文字列を生成（ディレクトリに日付があるため時刻のみ）
   * @param dt 日時
   * @param buffer 出力バッファ（最低9文字: HHMMSS + 終端）
   * @return バッファへのポインタ
   * @example "143025" (FAT16 8.3形式に対応)
   */
  static char* formatDateTimeForFilename(const RtcDateTime& dt, char* buffer);
  
  /**
   * @brief ログ用の日時文字列を生成
   * @param dt 日時
   * @param buffer 出力バッファ（最低25文字）
   * @return バッファへのポインタ
   * @example "2025-12-03 14:30:25"
   */
  static char* formatDateTimeForLog(const RtcDateTime& dt, char* buffer);

private:
  TwoWire* _wire;
  bool _available;
  RtcConfigState _configState;
  uint32_t _zoneId;
  bool _nvsPersisted;
  uint16_t _configGeneration;
#if !defined(ARDUINO_ARCH_ESP32)
  bool _hostConfigPresent;
  uint8_t _hostConfigBlob[ulsa::rtc::RTC_CONFIG_BLOB_SIZE];
#endif
  
  /**
   * @brief BCDをデシマルに変換
   */
  static uint8_t bcdToDec(uint8_t bcd);
  
  /**
   * @brief デシマルをBCDに変換
   */
  static uint8_t decToBcd(uint8_t dec);

  /**
   * @brief Control_status_1から年レジスタまでを一括読出しする
   */
  bool readSnapshot(RtcStatus& status, RtcDateTime* dt);

  bool readRawUtcDateTime(RtcDateTime& utc, RtcStatus* status = nullptr);
  RtcOperationResult writeUtcDateTime(const RtcDateTime& utc);
  bool loadPersistedConfig();
  bool writeAndConfirmConfig(RtcConfigState state, uint32_t zoneId);
  void forcePersistedConfigInvalid(uint32_t zoneId);
  bool readPersistedConfig(RtcStoredConfig& config) const;
  bool writePersistedConfig(const RtcStoredConfig& config);
  bool removePersistedConfig();
  bool isPersistedConfigKeyAbsent() const;

  /**
   * @brief RTCに書込んだ直後の読戻しを確認する
   */
  static bool isReadbackConfirmed(const RtcDateTime& expected, const RtcDateTime& actual);

  /**
   * @brief 2000-01-01からの秒数へ変換する
   */
  static uint32_t secondsSince2000(const RtcDateTime& dt);
  
  /**
   * @brief 単一レジスタ読み込み
   */
  bool readRegister(uint8_t reg, uint8_t& value);
  
  /**
   * @brief 単一レジスタ書き込み
   */
  bool writeRegister(uint8_t reg, uint8_t value);
  
  /**
   * @brief 複数レジスタ読み込み
   */
  bool readRegisters(uint8_t startReg, uint8_t* buffer, uint8_t count);
  
  /**
   * @brief 複数レジスタ書き込み
   */
  bool writeRegisters(uint8_t startReg, const uint8_t* buffer, uint8_t count);
  
  /**
   * @brief 曜日を計算（ツェラーの公式）
   */
  static uint8_t calculateWeekday(uint16_t year, uint8_t month, uint8_t day);
};

#endif // RTC_MANAGER_H
