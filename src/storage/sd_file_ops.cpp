/**
 * @file sd_file_ops.cpp
 * @brief SDカードファイル操作モジュール
 * @date 2025-01-06
 * 
 * CSVフォーマット、ディレクトリ作成、ファイル作成などの
 * ファイル操作関連機能を提供
 * 
 * 主な機能:
 * - 日付ディレクトリの作成
 * - ログファイルの作成（RTC有/無両対応）
 * - CSV行のフォーマット
 * - エラーログ記録
 */

#include "sd_logger.h"
#include <M5Unified.h>
#include <string.h>
#include <sys/time.h>   // utime()用
#include <utime.h>      // struct utimbuf用
#include <esp_timer.h>
#include <esp_system.h>
#include "storage/sd_fat_timestamp.h"
#ifndef SD_LOG_MOUNT_POINT
#define SD_LOG_MOUNT_POINT "/sd"
#endif

// FATファイルシステム用のグローバルRTCポインタ（get_fattime()で使用）
static std::atomic<uint32_t> g_sdFatTime{SD_FAT_TIME_UNSET};

static void formatTimezoneOffset(int16_t offsetMinutes, char* buffer,
                                 size_t bufferSize) {
  const char sign = offsetMinutes >= 0 ? '+' : '-';
  const uint16_t absolute = static_cast<uint16_t>(
      offsetMinutes >= 0 ? offsetMinutes : -offsetMinutes);
  snprintf(buffer, bufferSize, "%c%02u:%02u", sign,
           absolute / 60U, absolute % 60U);
}

// FATファイルシステムのタイムスタンプコールバック関数
// FATファイルシステムがファイル/ディレクトリ作成時に呼び出す
// --wrap=get_fattimeリンカオプションにより、元の実装をラップして上書き
extern "C" uint32_t __wrap_get_fattime(void) {
  return g_sdFatTime.load();
}

void SdLogger::prepareFatTimestamp(const RtcDateTime* date) {
  uint32_t result = SD_FAT_TIME_UNSET;
  if (date && date->year >= 1980 && date->year <= 2107) {
    result = ((uint32_t)(date->year - 1980) << 25) |
      ((uint32_t)date->month << 21) | ((uint32_t)date->day << 16) |
      ((uint32_t)date->hour << 11) | ((uint32_t)date->minute << 5) |
      ((uint32_t)date->second / 2);
  }
  g_sdFatTime = result;
}

// === 日付ディレクトリ作成 ===
/**
 * @brief RTCの日時情報を元に日付ディレクトリを作成
 * 
 * ディレクトリ名形式: "20251203(Wed)"
 * ルートフォルダ直下に作成される
 * 
 * @param dt RTC日時情報
 * @return true: 成功
 */
bool SdLogger::createDateDirectory(const RtcDateTime& dt) {
  // ディレクトリ名を生成: "20251203(Wed)"
  char dirName[32];
  RtcManager::formatDateForFilename(dt, dirName);
  
  snprintf(_currentDir, sizeof(_currentDir), "/%s", dirName);
  
  // ディレクトリが存在しない場合は作成
  if (!SD.exists(_currentDir)) {
    if (!SD.mkdir(_currentDir)) {
      // M5.Log.printf("SD: Failed to create dir: %s\n", _currentDir);
      return false;
    }
    // M5.Log.printf("SD: Created dir: %s\n", _currentDir);
    // ディレクトリのタイムスタンプはget_fattime()コールバックで自動設定される
  }
  
  return true;
}

// === 新規ログファイル作成 ===
/**
 * @brief RTC使用時の新規ログファイル作成
 * 
 * ファイル名形式: "YYYYMMDD_hhmmss.csv"
 * 30分単位でファイルが分割される
 * 
 * @param dt RTC日時情報
 * @return true: 成功
 */
bool SdLogger::createNewLogFile(const RtcDateTime& dt) {
  // FATタイムスタンプコールバック用にRTCポインタを設定
  
  // 日付ディレクトリを作成
  if (!createDateDirectory(dt)) {
    return false;
  }
  
  char rtcTimestampStem[16];
  RtcManager::formatDateTimeForFilename(dt, rtcTimestampStem);
  if (!openUniqueLogFile(_currentDir, rtcTimestampStem)) return false;
  
  // 現在のファイル情報を更新（30分単位インデックスを保存）
  _currentFileHalfHour = getHalfHourIndex(dt);
  _currentFileDate = dt;
  
  return true;
}

// === RTC timestampを使えない時の新規ログファイル作成 ===
/**
 * @brief RTC timestampを使えない時のログファイル作成
 * 
 * "/boot_logs" ディレクトリにランダムなセッションIDとsegment番号を含む
 * ファイル名で保存し、既存fileへは追記しない
 * 
 * @return true: 成功
 */
bool SdLogger::createNewLogFileNoRtc() {
  // RTC timestampを使えない時は "boot_logs" ディレクトリに保存
  static const char* BOOT_LOG_DIR = "/boot_logs";
  
  // ディレクトリが存在しない場合は作成
  if (!SD.exists(BOOT_LOG_DIR)) {
    if (!SD.mkdir(BOOT_LOG_DIR)) {
      // M5.Log.printf("SD: Failed to create dir: %s\n", BOOT_LOG_DIR);
      return false;
    }
    // M5.Log.printf("SD: Created dir: %s\n", BOOT_LOG_DIR);
  }
  
  strncpy(_currentDir, BOOT_LOG_DIR, sizeof(_currentDir) - 1);
  return openUniqueLogFile(_currentDir);
}

// Every segment is exclusive, including clock rollback and restarted sessions.
bool SdLogger::openUniqueLogFile(const char* directory,
                                 const char* rtcTimestampStem) {
  if (!rtcTimestampStem && !_sessionIdInitialized) {
    _sessionId = esp_random();
    _sessionIdInitialized = true;
  }
  for (unsigned attempt = 0; attempt < 1024; ++attempt) {
    int relativePathLength = 0;
    if (rtcTimestampStem) {
      relativePathLength = attempt == 0
          ? snprintf(_currentPath, sizeof(_currentPath), "%s/%s.csv",
                     directory, rtcTimestampStem)
          : snprintf(_currentPath, sizeof(_currentPath), "%s/%s_%02u.csv",
                     directory, rtcTimestampStem, attempt);
    } else {
      relativePathLength = snprintf(_currentPath, sizeof(_currentPath),
                                    "%s/%08lx_%08lx.csv", directory,
                                    static_cast<unsigned long>(_sessionId),
                                    static_cast<unsigned long>(_fileNumber++));
    }
    if (relativePathLength < 0 ||
        static_cast<size_t>(relativePathLength) >= sizeof(_currentPath)) {
      return false;
    }
    char absolutePath[192];
    const int pathLength = snprintf(absolutePath, sizeof(absolutePath), "%s%s",
                                    SD_LOG_MOUNT_POINT, _currentPath);
    if (pathLength < 0 || static_cast<size_t>(pathLength) >= sizeof(absolutePath)) return false;
    if (!_logFile.open(absolutePath)) {
      if (_logFile.error() == EEXIST) continue;
      return false;
    }
    if (_logFile.print(SD_CSV_HEADER) != strlen(SD_CSV_HEADER) || !_logFile.flush()) {
      failStorageWrite();
      return false;
    }
    _fileOpenedAtMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
    return true;
  }
  return false;
}

// === CSV行フォーマット ===
/**
 * @brief 風速計データをCSV行にフォーマット（RTC使用時）
 * 
 * 出力形式: timestamp_iso,timestamp_epoch_ms,uptime_ms,node_id,valid,direction,speed,head_speed,sound_speed,sonic_temp,source_seq
 * 
 * @param data 風速計データ
 * @param dt RTC日時情報
 * @param buffer 出力バッファ
 * @param bufferSize バッファサイズ
 * @return 生成された文字列長（0でエラー）
 */
size_t SdLogger::formatCsvLine(const WindData& data,
                               const RtcTimezoneSnapshot& rtc,
                               char* buffer, size_t bufferSize) {
  // DATA_SEQ reconstructs a stable source-output timeline. Keep the actual
  // ESP32 receive time in uptime_ms so transport delay remains observable.
  const uint32_t captureMs = data.timestamp;
  const uint32_t sampleMs = data.sourceTimestampValid ? data.sourceTimestamp : captureMs;
  const RtcDateTime& dt = rtc.localValid ? rtc.local : rtc.utc;
  const SdLogTimestamp timestamp =
      _rtcTimestampClock.fromRtcSecond(rtc.unixSeconds, sampleMs);

  char timezone[8];
  formatTimezoneOffset(rtc.localValid ? rtc.totalOffsetMinutes : 0,
                       timezone, sizeof(timezone));

  char timestampIso[40];
  snprintf(timestampIso, sizeof(timestampIso),
           "%04u-%02u-%02uT%02u:%02u:%02u.%03lu%s",
           dt.year,
           dt.month,
           dt.day,
           dt.hour,
           dt.minute,
           dt.second,
           static_cast<unsigned long>(timestamp.subsecondMs),
           timezone);
  
  char sourceSequence[6] = {0};
  if (data.sourceSequenceValid) {
    snprintf(sourceSequence, sizeof(sourceSequence), "%u", data.sourceSequence);
  }

  // CSV行を生成
  // timestamp_iso,...,node_id,valid,status_protocol_version,status_flags,...
  int len = snprintf(buffer, bufferSize,
                     "%s,%lld,%llu,%d,%d,%u,%u,%u,%u,%u,%d,%.2f,%.2f,%.2f,%.2f,%s,%lu,%lu,%lu\r\n",
                     timestampIso,
                     static_cast<long long>(timestamp.epochMs),
                     static_cast<unsigned long long>(_captureUptimeMs),
                     data.nodeId,
                     data.isValid ? 1 : 0,
                     (unsigned int)data.statusProtocolVersion,
                     (unsigned int)data.status,
                     (unsigned int)data.serviceStatus,
                     (unsigned int)data.activeCause,
                     (unsigned int)data.ntcReadingStatus,
                     data.windDirection,
                     data.windSpeed,
                     data.headingSpeed,
                     data.soundSpeed,
                     data.temperature,
                     sourceSequence, (unsigned long)_recordOrdinal,
                     (unsigned long)_recordDropped, (unsigned long)_uncertainRows);
  
  if (len < 0 || (size_t)len >= bufferSize) {
    return 0;
  }
  
  return (size_t)len;
}

// === RTC timestampを使えない時のCSV行フォーマット ===
/**
 * @brief RTC timestampを使えない時のCSV行フォーマット（millis使用）
 * 
 * RTCなしでは timestamp_iso に "BOOT+<millis_ms>"、timestamp_epoch_ms は空欄、
 * uptime_ms に起動からの経過時間を記録
 * 
 * @param data 風速計データ
 * @param buffer 出力バッファ
 * @param bufferSize バッファサイズ
 * @return 生成された文字列長（0でエラー）
 */
size_t SdLogger::formatCsvLineNoRtc(const WindData& data, char* buffer, size_t bufferSize) {
  // Use the reconstructed source time, not later CSV formatting time.
  // フォーマット: "BOOT+<millis_ms>" (起動からの経過時間)
  char sourceSequence[6] = {0};
  if (data.sourceSequenceValid) {
    snprintf(sourceSequence, sizeof(sourceSequence), "%u", data.sourceSequence);
  }
  
  // CSV行を生成
  // timestamp_iso,...,node_id,valid,status_protocol_version,status_flags,...
  int len = snprintf(buffer, bufferSize,
                     "BOOT+%llu,,%llu,%d,%d,%u,%u,%u,%u,%u,%d,%.2f,%.2f,%.2f,%.2f,%s,%lu,%lu,%lu\r\n",
                     static_cast<unsigned long long>(_sourceUptimeMs),
                     static_cast<unsigned long long>(_captureUptimeMs),
                     data.nodeId,
                     data.isValid ? 1 : 0,
                     (unsigned int)data.statusProtocolVersion,
                     (unsigned int)data.status,
                     (unsigned int)data.serviceStatus,
                     (unsigned int)data.activeCause,
                     (unsigned int)data.ntcReadingStatus,
                     data.windDirection,
                     data.windSpeed,
                     data.headingSpeed,
                     data.soundSpeed,
                     data.temperature,
                     sourceSequence, (unsigned long)_recordOrdinal,
                     (unsigned long)_recordDropped, (unsigned long)_uncertainRows);
  
  if (len < 0 || (size_t)len >= bufferSize) {
    return 0;
  }
  
  return (size_t)len;
}

// === 30分インデックス計算 ===
/**
 * @brief 時刻から30分単位のインデックスを計算
 * 
 * 0:00-0:29 = 0, 0:30-0:59 = 1, ... 23:30-23:59 = 47
 * ファイル分割の判定に使用
 * 
 * @param dt RTC日時情報
 * @return 30分インデックス (0-47)
 */
uint8_t SdLogger::getHalfHourIndex(const RtcDateTime& dt) {
  return (dt.hour * 2) + (dt.minute / 30);
}

// === エラーログ記録 ===
/**
 * @brief エラーログをSDカードに記録
 * 
 * "/error_log.txt" ファイルにタイムスタンプ付きで追記
 * RTC未接続時は記録不可
 * 
 * @param message エラーメッセージ
 * @return true: 記録成功
 */
bool SdLogger::logError(const char* message) {
  if (_state == SD_STATE_NO_CARD || _state == SD_STATE_UNINITIALIZED) {
    return false;
  }
  
  if (!_rtc || !_rtc->isAvailable()) {
    return false;
  }
  
  // エラーログファイルを開く（追記モード）
  File errorFile = SD.open("/error_log.txt", FILE_APPEND);
  if (!errorFile) {
    // M5.Log.println("SD: Failed to open error log");
    return false;
  }
  
  // タイムスタンプ取得
  RtcTimezoneSnapshot rtc{};
  if (!_rtc->getTimezoneSnapshot(rtc) || !rtc.status.utcValid ||
      (!rtc.localValid && !rtc.utcFallback)) {
    errorFile.close();
    return false;
  }
  const RtcDateTime& dt = rtc.localValid ? rtc.local : rtc.utc;
  
  // タイムスタンプ付きでエラーメッセージを記録
  char timestamp[24];
  RtcManager::formatDateTimeForLog(dt, timestamp);
  
  char offset[8];
  formatTimezoneOffset(rtc.localValid ? rtc.totalOffsetMinutes : 0,
                       offset, sizeof(offset));
  errorFile.printf("[%s%s] %s\r\n", timestamp, offset, message);
  errorFile.close();
  
  // M5.Log.printf("SD: Error logged: %s\n", message);
  return true;
}
