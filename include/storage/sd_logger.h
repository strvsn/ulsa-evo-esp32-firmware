/**
 * @file sd_logger.h
 * @brief SDカードロギングモジュール
 * @date 2025-12-03
 * 
 * 風速計データのSDカードへのログ保存を担当
 * - SPI接続のmicroSDカード対応
 * - バッファリング書込みによる効率化
 * - 30分ごとのファイル分割
 * - 日付ベースのディレクトリ構造
 */

#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "sd_log_interval_policy.h"
#include "sd_log_sample_gate.h"
#include "sd_log_timestamp_clock.h"
#include "sd_log_write_policy.h"
#include "sd_checked_file.h"
#include "sd_log_lifetime_policy.h"
#include "wind_parser.h"
#include "rtc_manager.h"
#include "pin_config.h"

// ============================================
// SDロガー設定
// ============================================

// バッファサイズ（書込み効率化のため）
#define SD_BUFFER_SIZE          512   // 1セクタ分

// ファイル分割間隔（分）
#define SD_FILE_SPLIT_MINUTES   30

// 最大パス長
#define SD_MAX_PATH_LENGTH      64

// CSVヘッダー（単位付き）
#define SD_CSV_HEADER "timestamp_iso,timestamp_epoch_ms,uptime_ms,node_id,valid,status_protocol_version,status_flags,service_status,active_cause,ntc_reading_status,direction[deg],speed[m/s],head_speed[m/s],sound_speed[m/s],sonic_temp[degC],source_seq,record_index,dropped_total,uncertain_total\r\n"

// SDログ周期設定
#define SD_LOG_INTERVAL_DEFAULT_MS 100UL
#define SD_LOG_INTERVAL_MIN_MS     20UL
#define SD_LOG_INTERVAL_MAX_MS     600000UL

/**
 * @brief SDロガー状態
 */
enum SdLoggerState {
  SD_STATE_UNINITIALIZED,   // 未初期化
  SD_STATE_NO_CARD,         // カードなし
  SD_STATE_MOUNT_ERROR,     // マウントエラー
  SD_STATE_READY,           // 準備完了
  SD_STATE_LOGGING          // ログ中
};

/**
 * @brief SDロガー停止理由
 */
enum SdLoggerStopReason {
  SD_STOP_NONE,             // 停止なし
  SD_STOP_NO_CARD,          // カードなし
  SD_STOP_SLOW_WRITE,       // 書き込み遅延による停止
  SD_STOP_WRITE_ERROR,      // 書き込み失敗による停止
  SD_STOP_FILE_ERROR,       // ファイル作成/オープン失敗による停止
  SD_STOP_USER_DISABLED,    // ユーザー操作による停止
  SD_STOP_CAPACITY,         // 保存領域のreserve到達
  SD_STOP_RETRY_EXHAUSTED   // 自動復旧回数超過
};

/**
 * @brief BLE公開用SDカード形式コード
 */
enum SdCardTypeCode {
  SD_CARD_TYPE_NONE = 0,
  SD_CARD_TYPE_UNKNOWN = 1,
  SD_CARD_TYPE_MMC = 2,
  SD_CARD_TYPE_SD = 3,
  SD_CARD_TYPE_SDHC = 4
};

/**
 * @brief SDカードロギングクラス
 * 
 * 風速計データをCSV形式でSDカードに保存
 * バッファリングにより書込み回数を削減
 */
class SdLogger {
public:
  explicit SdLogger(bool workerCore = false, const SdFileOperations* operations = nullptr);
  bool isRecordingRequested() const;
  bool isStorageQuiescent() const;
  void setInputActive(bool active);
  uint32_t getSyncedLogCount() const;
  uint32_t getDroppedLogCount() const;
  uint32_t getUncertainLogCount() const;
  uint16_t getQueueDepth() const;
  bool isRecovering() const;
  bool isInputPaused() const;
  
  /**
   * @brief 初期化
   * @param rtc RTCマネージャーへの参照
   * @return true: 初期化成功（カード検出）
   */
  bool begin(RtcManager* rtc);
  
  /**
   * @brief SDカードの再マウントを試行
   * @return true: マウント成功
   */
  bool remount();

  /**
   * @brief 停止中のSDロギングを再開
   * @return true: 再開成功
   */
  bool resumeLogging();

  /**
   * @brief SDロギングを停止理由付きで無効化
   * @param reason 停止理由
   */
  void disableLogging(SdLoggerStopReason reason);
  
  /**
   * @brief 風速計データをログに記録
   * @param data 風速計データ
   * @return true: 記録成功
   */
  bool log(const WindData& data);
  
  /**
   * @brief バッファをフラッシュ（強制書込み）
   * @return true: 書込み成功
   */
  bool flush();
  
  /**
   * @brief ロギングを停止してファイルを閉じる
   */
  void stop();
  
  /**
   * @brief 現在の状態を取得
   * @return SdLoggerState
   */
  SdLoggerState getState() const;
  
  /**
   * @brief 状態を文字列で取得
   * @return 状態文字列
   */
  const char* getStateString() const;
  
  /**
   * @brief SDカードが利用可能か
   * @return true: 利用可能
   */
  bool isAvailable() const;

  /**
   * @brief SDロギングが有効か
   * @return true: ログ記録を試行可能
   */
  bool isLoggingEnabled() const;

  /**
   * @brief 電源投入時のSDログ自動開始設定を取得
   * @return true: 次回起動時、カード検出後にログ開始
   */
  bool isAutoStartEnabled() const;

  /**
   * @brief 電源投入時のSDログ自動開始設定をNVSへ保存
   * @param enabled true: 有効、false: 無効
   * @return true: 保存成功。失敗時は現在の設定を維持
   */
  bool setAutoStartEnabled(bool enabled);

  /**
   * @brief ロギング停止理由を取得
   * @return SdLoggerStopReason
   */
  SdLoggerStopReason getStopReason() const;

  /**
   * @brief ロギング停止理由を文字列で取得
   * @return 停止理由文字列
   */
  const char* getStopReasonString() const;
  
  /**
   * @brief 現在のログファイルパスを取得
   * @return ファイルパス（未オープン時は空文字列）
   */
  const char* getCurrentFilePath() const;
  
  /**
   * @brief 書込み済み行数を取得
   * @return 行数
   */
  uint32_t getLogCount() const;

  /**
   * @brief バッファflush回数を取得
   * @return flush回数
   */
  uint32_t getFlushCount() const;

  /**
   * @brief 未flushバッファ量を取得
   * @return バッファ使用量 [byte]
   */
  uint16_t getBufferedBytes() const;

  /**
   * @brief 直近のSD書込み所要時間を取得
   * @return 書込み所要時間 [ms]
   */
  uint16_t getLastWriteDurationMs() const;

  /**
   * @brief 直近のログ記録millis()を取得
   * @return millis()。まだ記録がなければ0
   */
  uint32_t getLastLogMillis() const;

  /**
   * @brief 現在ログファイルが開いているか
   * @return true: open
   */
  bool isFileOpen() const;

  /**
   * @brief 現在のログがRTC由来の時刻でtimestampを生成しているか
   * @return true: 有効なRTC読出しを使うログ方式
   */
  bool isRtcTimestampingAvailable() const;
  
  /**
   * @brief ログレート設定（1-10Hz, 後方互換API）
   * @param rateHz ログ頻度（Hz）
   */
  void setLogRate(uint8_t rateHz);
  
  /**
   * @brief 現在のログレートを取得
   * @return Hz
   */
  uint8_t getLogRate() const;

  /**
   * @brief ログ周期を設定
   * @param intervalMs ログ周期 [ms]
   * @param minAllowedMs 現在の計測周期に基づく最短許可周期 [ms]
   * @param persist trueならNVSへ保存
   * @return true: 設定成功
   */
  bool setLogIntervalMs(uint32_t intervalMs,
                        uint32_t minAllowedMs = SD_LOG_INTERVAL_MIN_MS,
                        bool persist = true);

  /**
   * @brief 現在のログ周期を取得
   * @return ログ周期 [ms]
   */
  uint32_t getLogIntervalMs() const;

  /**
   * @brief 現在のログ周期がNVS保存済みか
   * @return true: 保存済み
   */
  bool isLogIntervalPersisted() const;

  /**
   * @brief ログ周期設定を既定値へ戻す
   * @param minAllowedMs 現在の計測周期に基づく最短許可周期 [ms]
   * @return true: 設定成功
   */
  bool restoreDefaultLogInterval(uint32_t minAllowedMs = SD_LOG_INTERVAL_MIN_MS);

  /**
   * @brief ログ周期をI2C新規サンプル周期の整数倍へ正規化する
   * @return true: sourceIntervalMsが有効で、現在値または正規化後の値が整合する
   * @note 旧NVS設定も、速くならない方向へ一度だけ補正して保存する。
   */
  bool synchronizeLogIntervalToSource(uint32_t sourceIntervalMs);

  /**
   * @brief 新規センササンプルをSDログ周期へ照合する
   * @param sampleTimestampMs WindDataの受信時刻 [ms]
   * @return true: このサンプルをログに保存する
   * @note 新規サンプルごとに1回だけ呼ぶ。SD書込み完了時刻ではなく、
   *       サンプル時刻を基準に周期を維持する。
   */
  bool shouldLogSample(uint32_t sampleTimestampMs);
  
  /**
   * @brief 更新処理（レート制限用）
   * 
   * 内部タイマーを更新し、次のログが許可されるかを管理
   */
  void update();
  
  /**
   * @brief ログが許可されているかチェック
   * @return true: ログ可能
   */
  bool canLog() const;

  /** @brief 起動後にSDログへ渡された新規センササンプル数 */
  uint32_t getSourceSampleCount() const;

  /** @brief 設定周期より早く到着し、保存対象外となったサンプル数 */
  uint32_t getRateLimitedSampleCount() const;

  /** @brief 直近の新規センササンプル間隔 [ms]。未計測時は0 */
  uint32_t getLastSourceSampleIntervalMs() const;

  /** @brief 直近の保存サンプル間隔 [ms]。未計測時は0 */
  uint32_t getLastLoggedSampleIntervalMs() const;
  
  /**
   * @brief SDカードの空き容量を取得
   * @return 空き容量 [MB]
   */
  uint32_t getFreeSpaceMB() const;
  
  /**
   * @brief SDカードの総容量を取得
   * @return 総容量 [MB]
   */
  uint32_t getTotalSpaceMB() const;
  
  /**
   * @brief SDカードの使用率を取得
   * @return 使用率 [%]
   */
  uint8_t getUsagePercent() const;

  /**
   * @brief SDカード形式コードを取得
   * @return SdCardTypeCode
   */
  uint8_t getCardTypeCode() const;

  /**
   * @brief SDカード形式を文字列で取得
   * @return 形式文字列
   */
  const char* getCardTypeString() const;
  
  /**
   * @brief エラーログを記録
   * @param message エラーメッセージ
   * @return true: 記録成功
   */
  bool logError(const char* message);

private:
  enum class StorageOperation : uint8_t { Sample, Start, Stop, Flush, Remount };
  struct StorageMessage {
    StorageOperation operation = StorageOperation::Sample;
    uint32_t ticket = 0;
    WindData data;
    RtcTimezoneSnapshot rtc{};
  };
  struct StorageSnapshot {
    SdLoggerState state = SD_STATE_UNINITIALIZED;
    SdLoggerStopReason reason = SD_STOP_NONE;
    bool enabled = false, fileOpen = false, rtcValid = false, recovering = false;
    uint32_t rows = 0, syncedRows = 0, uncertainRows = 0, flushes = 0;
    uint32_t lastLog = 0, freeMB = 0, totalMB = 0;
    uint16_t bufferedBytes = 0, writeMs = 0;
    uint8_t cardType = SD_CARD_TYPE_NONE, usage = 0;
    char path[SD_MAX_PATH_LENGTH]{};
  };
  static constexpr unsigned kQueueLength = 256;
  bool _workerCore;
  const SdFileOperations* _fileOperations;
  SdLogger* _storage = nullptr;
  QueueHandle_t _storageQueue = nullptr;
  TaskHandle_t _storageTask = nullptr;
  std::atomic<bool> _recordingRequested{false};
  std::atomic<bool> _quiescent{true};
  std::atomic<bool> _inputActive{true};
  std::atomic<uint32_t> _completedTicket{0};
  std::atomic<bool> _commandSuccess{false};
  std::atomic<uint32_t> _droppedRows{0};
  uint32_t _issuedTicket = 0;
  mutable portMUX_TYPE _snapshotMux = portMUX_INITIALIZER_UNLOCKED;
  StorageSnapshot _snapshot;
  mutable char _snapshotPath[SD_MAX_PATH_LENGTH]{};
  RtcTimezoneSnapshot _capturedRtc{};
  uint32_t _uncertainRows = 0;
  uint32_t _recordOrdinal = 0;
  uint32_t _recordDropped = 0;
  uint64_t _lastRtcStamp = 0;
  int16_t _lastRtcTimezoneMinutes = 0;
  int16_t _lastRtcStandardOffsetMinutes = 0;
  int16_t _lastRtcDstOffsetMinutes = 0;
  uint32_t _lastRtcZoneId = 0;
  uint64_t _observedRtcStamp = 0;
  uint64_t _rtcChangedAtMs = 0;
  bool _rtcObserved = false;
  uint64_t _totalStorageBytes = 0;
  bool startStorageWorker(RtcManager* rtc);
  bool enqueueSample(const WindData& data);
  bool storageCommand(StorageOperation operation);
  static void storageTaskEntry(void* context);
  void storageTaskLoop();
  StorageSnapshot storageSnapshot() const;
  void publishStorageSnapshot(bool recovering, uint64_t freeBytes, uint64_t totalBytes);
  RtcManager* _rtc;
  SdLoggerState _state;
  SdCheckedFile _logFile;
  uint64_t _fileOpenedAtMs = 0;
  uint64_t _captureUptimeMs = 0;
  uint64_t _sourceUptimeMs = 0;
  uint32_t _sessionId = 0;
  bool _sessionIdInitialized = false;
  uint32_t _fileNumber = 0;
  uint32_t _syncedLogCount = 0;
  uint32_t _bufferedRows = 0;
  uint32_t _pendingSyncRows = 0;
  bool openUniqueLogFile(const char* directory,
                         const char* rtcTimestampStem = nullptr);
  void prepareFatTimestamp(const RtcDateTime* date);
  void failStorageWrite();
  bool closeCheckedLogFile();
  
  // バッファ管理
  char _buffer[SD_BUFFER_SIZE];
  size_t _bufferPos;
  
  // 現在のファイル情報
  char _currentPath[SD_MAX_PATH_LENGTH];
  char _currentDir[SD_MAX_PATH_LENGTH];
  uint8_t _currentFileHalfHour;   // 0-47 (30分単位のインデックス)
  RtcDateTime _currentFileDate;
  RtcDateTime _fileCreationTime;  // ファイル作成時のタイムスタンプ（CSV記録用）
  // 現在開いているログのtimestamp方式。PCF8563が検出済みでも、VL/STOP/I2C
  // 異常で有効な日時を読めない場合はfalseにしてBOOT+経過時間ログへ切り替える。
  bool _rtcAvailable;
  uint32_t _bootLogFileIndex;     // RTC未接続時のファイルインデックス
  
  // 統計
  uint32_t _logCount;
  uint32_t _flushCount;
  uint16_t _lastWriteDurationMs;
  uint32_t _lastSyncMillis;
  bool _syncPending;
  SdLoggerStopReason _stopReason;
  bool _loggingEnabled;
  
  // レート制限
  uint32_t _lastLogTime;
  uint32_t _logIntervalMs;
  bool _logIntervalPersisted;
  bool _autoStartEnabled;
  bool _canLog;

  // Source sample cadence is intentionally independent of SD/CSV completion.
  SdLogSampleGate _sampleGate;
  uint32_t _sourceSampleCount;
  uint32_t _rateLimitedSampleCount;
  uint32_t _lastSourceSampleTimestampMs;
  uint32_t _lastSourceSampleIntervalMs;
  uint32_t _lastLoggedSampleTimestampMs;
  uint32_t _lastLoggedSampleIntervalMs;
  bool _hasSourceSampleTimestamp;
  bool _hasLoggedSampleTimestamp;

  // RTC秒精度 + source sample millis() によるログ用ミリ秒補間
  SdLogTimestampClock _rtcTimestampClock;

  bool loadLogIntervalSetting();
  bool saveLogIntervalSetting() const;
  bool saveAutoStartSetting(bool enabled) const;
  
  /**
   * @brief 日付ディレクトリを作成
   * @param dt 日時
   * @return true: 成功
   */
  bool createDateDirectory(const RtcDateTime& dt);
  
  /**
   * @brief 新しいログファイルを作成
   * @param dt 日時
   * @return true: 成功
   */
  bool createNewLogFile(const RtcDateTime& dt);
  
  /**
   * @brief ファイル分割が必要かチェック
   * @param dt 現在日時
   * @return true: 分割必要
   */
  bool needsNewFile(const RtcDateTime& dt);

  /**
   * @brief RTC timestampとBOOT timestampの切替前に現在のファイルを確定する
   *
   * 同じCSVへ異なるtimestamp意味論を混在させないため、RTCの有効/無効遷移時は
   * 必ずファイルを閉じ、次のsampleで適切な形式の新規ファイルを作成する。
   */
  bool closeLogFileForTimestampModeChange();
  
  /**
   * @brief バッファにデータを追加
   * @param str 追加する文字列
   * @return true: 成功
   */
  bool appendToBuffer(const char* str);
  
  /**
   * @brief バッファをファイルに書込み
   * @return true: 成功
   */
  bool writeBuffer(bool forceSync = false);
  
  /**
   * @brief 風速計データをCSV行に変換
   * @param data 風速計データ
   * @param dt タイムスタンプ
   * @param buffer 出力バッファ
   * @param bufferSize バッファサイズ
   * @return 生成された文字列長
   */
  size_t formatCsvLine(const WindData& data,
                       const RtcTimezoneSnapshot& rtc,
                       char* buffer, size_t bufferSize);
  
  /**
   * @brief RTC timestampを使えない時のCSV行フォーマット（millis使用）
   * @param data 風速計データ
   * @param buffer 出力バッファ
   * @param bufferSize バッファサイズ
   * @return 生成された文字列長
   */
  size_t formatCsvLineNoRtc(const WindData& data, char* buffer, size_t bufferSize);
  
  /**
   * @brief RTC timestampを使えない時の新規ログファイル作成
   * @return true: 成功
   */
  bool createNewLogFileNoRtc();
  
  /**
   * @brief 30分単位のインデックスを計算
   * @param dt 日時
   * @return 0-47
   */
  static uint8_t getHalfHourIndex(const RtcDateTime& dt);
};

#endif // SD_LOGGER_H
