/**
 * @file sd_logger.cpp
 * @brief SDカードロギングモジュール - コア機能
 * @date 2025-12-03
 * 
 * このファイルにはSDロガーのコア機能が含まれる:
 * - 初期化/終了処理
 * - ログ記録ロジック
 * - 状態管理
 * - レート制御
 * 
 * ファイル操作関連: sd_file_ops.cpp
 * バッファ/容量操作: sd_buffer_ops.cpp
 */

#include "sd_logger.h"
#include <M5Unified.h>
#include <Preferences.h>
#include <sys/time.h>   // utime()用
#include <utime.h>      // struct utimbuf用
#include <esp_timer.h>

static const char* SD_LOG_NVS_NAMESPACE = "sd_log_cfg";
static const char* SD_LOG_NVS_INTERVAL_KEY = "interval_ms";
static const char* SD_LOG_NVS_AUTO_START_KEY = "auto_start";

static uint32_t normalizeLogIntervalMin(uint32_t minAllowedMs) {
  if (minAllowedMs < SD_LOG_INTERVAL_MIN_MS) {
    return SD_LOG_INTERVAL_MIN_MS;
  }
  if (minAllowedMs > SD_LOG_INTERVAL_MAX_MS) {
    return SD_LOG_INTERVAL_MAX_MS;
  }
  return minAllowedMs;
}

static bool isSdFaultStopReason(SdLoggerStopReason reason) {
  return reason == SD_STOP_NO_CARD ||
         reason == SD_STOP_SLOW_WRITE ||
         reason == SD_STOP_WRITE_ERROR ||
         reason == SD_STOP_FILE_ERROR;
}

// ============================================
// コンストラクタ
// ============================================
SdLogger::SdLogger(bool workerCore, const SdFileOperations* operations)
  : _workerCore(workerCore)
  , _fileOperations(operations)
  , _rtc(nullptr)
  , _state(SD_STATE_UNINITIALIZED)
  , _logFile(operations)
  , _bufferPos(0)
  , _currentFileHalfHour(255)  // 無効値
  , _rtcAvailable(false)
  , _bootLogFileIndex(0)
  , _logCount(0)
  , _flushCount(0)
  , _lastWriteDurationMs(0)
  , _lastSyncMillis(0)
  , _syncPending(false)
  , _stopReason(SD_STOP_NONE)
  , _loggingEnabled(true)
  , _lastLogTime(0)
  , _logIntervalMs(SD_LOG_INTERVAL_DEFAULT_MS)
  , _logIntervalPersisted(false)
  , _autoStartEnabled(false)
  , _canLog(true)
  , _sampleGate(SD_LOG_INTERVAL_DEFAULT_MS)
  , _sourceSampleCount(0)
  , _rateLimitedSampleCount(0)
  , _lastSourceSampleTimestampMs(0)
  , _lastSourceSampleIntervalMs(0)
  , _lastLoggedSampleTimestampMs(0)
  , _lastLoggedSampleIntervalMs(0)
  , _hasSourceSampleTimestamp(false)
  , _hasLoggedSampleTimestamp(false)
{
  memset(_buffer, 0, sizeof(_buffer));
  memset(_currentPath, 0, sizeof(_currentPath));
  memset(_currentDir, 0, sizeof(_currentDir));
  memset(&_fileCreationTime, 0, sizeof(_fileCreationTime));
}

// ============================================
// 初期化
// ============================================
bool SdLogger::begin(RtcManager* rtc) {
  if (!_workerCore) return startStorageWorker(rtc);
  _rtc = rtc;
  loadLogIntervalSetting();
  _sampleGate.reset(_logIntervalMs);
  _rtcTimestampClock.reset();
  _rtcAvailable = false;
  _lastWriteDurationMs = 0;
  _lastSyncMillis = millis();
  _syncPending = false;
  
  // SDカードのマウントを試行
  if (!SD.begin(SPI_CS_PIN, SPI, SPI_FREQUENCY)) {
    _state = SD_STATE_NO_CARD;
    _loggingEnabled = false;
    _stopReason = SD_STOP_NO_CARD;
    // M5.Log.println("SD: Card not found");
    return false;
  }
  
  // カードタイプを確認
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    _state = SD_STATE_NO_CARD;
    _loggingEnabled = false;
    _stopReason = SD_STOP_NO_CARD;
    // M5.Log.println("SD: No card attached");
    return false;
  }
  
  // カード情報をログ出力
  const char* typeStr = "UNKNOWN";
  switch (cardType) {
    case CARD_MMC:  typeStr = "MMC"; break;
    case CARD_SD:   typeStr = "SD"; break;
    case CARD_SDHC: typeStr = "SDHC"; break;
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  M5.Log.printf("SD: %s card, %llu MB\n", typeStr, cardSize);
  
  _state = SD_STATE_READY;
  // Mounting only makes the card available. The boot sequence may opt in to
  // one automatic start; remounts and normal runtime control stay explicit.
  _loggingEnabled = false;
  _stopReason = SD_STOP_USER_DISABLED;
  _lastLogTime = 0;
  _canLog = false;
  return true;
}

// ============================================
// 再マウント
// ============================================
bool SdLogger::remount() {
  if (!_workerCore) {
    _recordingRequested = false;
    return storageCommand(StorageOperation::Remount);
  }
  // 既存のファイルを閉じる
  stop();
  
  // SDをアンマウント
  SD.end();
  
  // 少し待機
  delay(100);
  
  // 再マウント
  return begin(_rtc);
}

bool SdLogger::resumeLogging() {
  if (!_workerCore) {
    _recordingRequested = true;
    _sampleGate.reset(_logIntervalMs);
    const bool ok = storageCommand(StorageOperation::Start);
    if (!ok) _recordingRequested = false;
    return ok;
  }
  if (_state == SD_STATE_UNINITIALIZED || _state == SD_STATE_NO_CARD || _state == SD_STATE_MOUNT_ERROR) {
    if (!remount()) {
      return false;
    }
  }

  if (_loggingEnabled && isAvailable()) return true;

  _loggingEnabled = true;
  _stopReason = SD_STOP_NONE;
  _lastLogTime = millis() - _logIntervalMs;
  _lastSyncMillis = millis();
  _syncPending = false;
  _canLog = true;
  _sampleGate.reset(_logIntervalMs);
  return true;
}

void SdLogger::disableLogging(SdLoggerStopReason reason) {
  if (!_workerCore) { stop(); return; }
  if (reason == SD_STOP_NONE) {
    resumeLogging();
    return;
  }

  stop();

  // A later user stop acknowledges the terminal state. Evaluate after stop()
  // because its final flush can itself be the first write failure.
  const bool preserveExistingFault =
    (reason == SD_STOP_USER_DISABLED ||
     reason == SD_STOP_FILE_ERROR || reason == SD_STOP_WRITE_ERROR) &&
    (_state == SD_STATE_UNINITIALIZED ||
     _state == SD_STATE_NO_CARD ||
     _state == SD_STATE_MOUNT_ERROR ||
     _stopReason == SD_STOP_CAPACITY ||
     isSdFaultStopReason(_stopReason));

  _loggingEnabled = false;
  _canLog = false;

  if (!preserveExistingFault) {
    _stopReason = reason;
  }

  if (reason == SD_STOP_WRITE_ERROR || reason == SD_STOP_FILE_ERROR) {
    _state = SD_STATE_MOUNT_ERROR;
  }
}

// ============================================
// ログ記録
// ============================================
bool SdLogger::log(const WindData& data) {
  if (!_workerCore) return enqueueSample(data);
  // 状態チェック
  if (!_loggingEnabled || (_state != SD_STATE_READY && _state != SD_STATE_LOGGING)) {
    return false;
  }
  
  const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
  _captureUptimeMs = SdLogLifetimePolicy::extendCapture(data.timestamp, nowMs);
  _sourceUptimeMs = data.sourceTimestampValid
    ? SdLogLifetimePolicy::extendCapture(data.sourceTimestamp, nowMs) : _captureUptimeMs;
  if (_logFile && SdLogLifetimePolicy::rotate(
        nowMs, _fileOpenedAtMs, _logFile.size() + _bufferPos, 192)) {
    if (!flush() || !closeCheckedLogFile()) return false;
  }

  // RTCが検出済みなだけでは、電圧低下(VL)・STOP・I2C短読み後の日時を
  // 信用してはいけない。日時を完全に読めた場合だけRTC timestampを使う。
  const RtcTimezoneSnapshot rtcSnapshot = _capturedRtc;
  const bool rtcTimestampAvailable = rtcSnapshot.status.utcValid &&
      (rtcSnapshot.localValid || rtcSnapshot.utcFallback);
  const RtcDateTime dt = rtcSnapshot.localValid
      ? rtcSnapshot.local : rtcSnapshot.utc;
  prepareFatTimestamp(rtcTimestampAvailable ? &dt : nullptr);
  const uint64_t rtcStamp = rtcTimestampAvailable
      ? static_cast<uint64_t>(rtcSnapshot.unixSeconds) : 0;
  if (_logFile && rtcTimestampAvailable && _rtcAvailable &&
      (rtcStamp < _lastRtcStamp ||
       rtcSnapshot.totalOffsetMinutes != _lastRtcTimezoneMinutes ||
       rtcSnapshot.standardOffsetMinutes != _lastRtcStandardOffsetMinutes ||
       rtcSnapshot.dstOffsetMinutes != _lastRtcDstOffsetMinutes ||
       rtcSnapshot.zoneId != _lastRtcZoneId)) {
    if (!closeLogFileForTimestampModeChange()) return false;
  }
  _lastRtcStamp = rtcStamp;
  _lastRtcTimezoneMinutes = rtcSnapshot.totalOffsetMinutes;
  _lastRtcStandardOffsetMinutes = rtcSnapshot.standardOffsetMinutes;
  _lastRtcDstOffsetMinutes = rtcSnapshot.dstOffsetMinutes;
  _lastRtcZoneId = rtcSnapshot.zoneId;
  if (_logFile && rtcTimestampAvailable != _rtcAvailable) {
    if (!closeLogFileForTimestampModeChange()) {
      return false;
    }
  }
  _rtcAvailable = rtcTimestampAvailable;
  
  char line[256];
  size_t len = 0;
  
  if (_rtcAvailable) {
    // RTCが利用可能な場合: 通常のログ処理
    // 新しいファイルが必要かチェック
    if (needsNewFile(dt)) {
      // 既存のバッファをフラッシュ
      if (_logFile) {
        if (!flush()) {
          disableLogging(SD_STOP_WRITE_ERROR);
          return false;
        }
        if (!closeCheckedLogFile()) return false;
      }
      
      // 新しいファイルを作成
      if (!createNewLogFile(dt)) {
        disableLogging(SD_STOP_FILE_ERROR);
        return false;
      }
      
      // ファイル作成時のタイムスタンプをキャッシュ（30分判定用）
      _fileCreationTime = dt;
    }
    
    // CSV行を生成（現在時刻を記録）
    len = formatCsvLine(data, rtcSnapshot, line, sizeof(line));
  } else {
    // RTC timestampを使えない場合: millis()ベースのログ
    // ファイルが開いていない場合は新規作成
    if (!_logFile) {
      if (!createNewLogFileNoRtc()) {
        disableLogging(SD_STOP_FILE_ERROR);
        return false;
      }
    }
    
    // millis()ベースのCSV行を生成
    len = formatCsvLineNoRtc(data, line, sizeof(line));
  }
  if (len == 0) {
    return false;
  }
  
  // バッファに追加
  if (!appendToBuffer(line)) {
    // バッファが一杯の場合はフラッシュして再試行
    if (!writeBuffer(false)) {
      disableLogging(SD_STOP_WRITE_ERROR);
      return false;
    }
    if (!appendToBuffer(line)) {
      disableLogging(SD_STOP_WRITE_ERROR);
      return false;
    }
  }
  
  _logCount++;
  _bufferedRows++;
  _state = SD_STATE_LOGGING;
  const uint32_t sampleTimestampMs = data.sourceTimestampValid
    ? data.sourceTimestamp
    : data.timestamp;
  if (_hasLoggedSampleTimestamp) {
    _lastLoggedSampleIntervalMs = sampleTimestampMs - _lastLoggedSampleTimestampMs;
  }
  _lastLoggedSampleTimestampMs = sampleTimestampMs;
  _hasLoggedSampleTimestamp = true;
  _sampleGate.accept(sampleTimestampMs);
  _lastLogTime = millis();
  _canLog = false;
  
  return true;
}

// ============================================
// バッファフラッシュ
// ============================================
bool SdLogger::flush() {
  if (!_workerCore) return storageCommand(StorageOperation::Flush);
  // 停止・ファイル分割では、バッファが空でも前回writeの保留同期を
  // 完了する。これによりCSVの終端を確定する。
  return writeBuffer(true);
}

bool SdLogger::closeLogFileForTimestampModeChange() {
  if (!_logFile) {
    return true;
  }
  if (!flush()) {
    disableLogging(SD_STOP_WRITE_ERROR);
    return false;
  }
  if (!closeCheckedLogFile()) return false;
  _currentFileHalfHour = 255;
  _rtcTimestampClock.reset();
  return true;
}

// ============================================
// ロギング停止
// ============================================
void SdLogger::stop() {
  if (!_workerCore) {
    _recordingRequested = false;
    (void)storageCommand(StorageOperation::Stop);
    return;
  }
  // Stop accepting samples before touching the card. This leaves the logger
  // terminal even when the SD driver has already reported a fault.
  _loggingEnabled = false;
  _canLog = false;

  if (_logFile) {
    // A previous write/file failure has already made the pending bytes
    // unrecoverable. Do not issue another SD write while closing that handle.
    if (_state == SD_STATE_READY || _state == SD_STATE_LOGGING) {
      flush();
    }
    (void)closeCheckedLogFile();
  }

  _rtcAvailable = false;
  _rtcTimestampClock.reset();

  // Either flush() committed the bytes or the SD failure made them
  // unrecoverable. Never carry a stale buffer into a later remount/session.
  _bufferPos = 0;
  
  _currentFileHalfHour = 255;
  memset(_currentPath, 0, sizeof(_currentPath));
  
  if (_state == SD_STATE_LOGGING) {
    _state = SD_STATE_READY;
  }
}

// ============================================
// 状態取得
// ============================================
SdLoggerState SdLogger::getState() const {
  if (!_workerCore) return storageSnapshot().state;
  return _state;
}

const char* SdLogger::getStateString() const {
  switch (getState()) {
    case SD_STATE_UNINITIALIZED: return "Uninitialized";
    case SD_STATE_NO_CARD:       return "No Card";
    case SD_STATE_MOUNT_ERROR:   return "Mount Error";
    case SD_STATE_READY:         return "Ready";
    case SD_STATE_LOGGING:       return "Logging";
    default:                     return "Unknown";
  }
}

bool SdLogger::isAvailable() const {
  if (!_workerCore) {
    const auto state = storageSnapshot().state;
    return state == SD_STATE_READY || state == SD_STATE_LOGGING;
  }
  return (_state == SD_STATE_READY || _state == SD_STATE_LOGGING);
}

bool SdLogger::isLoggingEnabled() const {
  if (!_workerCore) return _recordingRequested && _inputActive && storageSnapshot().enabled;
  return _loggingEnabled;
}

bool SdLogger::isAutoStartEnabled() const {
  return _autoStartEnabled;
}

bool SdLogger::setAutoStartEnabled(bool enabled) {
  if (!saveAutoStartSetting(enabled)) {
    return false;
  }

  _autoStartEnabled = enabled;
  return true;
}

SdLoggerStopReason SdLogger::getStopReason() const {
  if (!_workerCore) return storageSnapshot().reason;
  return _stopReason;
}

const char* SdLogger::getStopReasonString() const {
  switch (getStopReason()) {
    case SD_STOP_NONE:        return "None";
    case SD_STOP_NO_CARD:     return "No Card";
    case SD_STOP_SLOW_WRITE:  return "Slow Write";
    case SD_STOP_WRITE_ERROR: return "Write Error";
    case SD_STOP_FILE_ERROR:  return "File Error";
    case SD_STOP_USER_DISABLED: return "User Disabled";
    case SD_STOP_CAPACITY: return "Capacity Reserve";
    case SD_STOP_RETRY_EXHAUSTED: return "Recovery Exhausted";
    default:                  return "Unknown";
  }
}

const char* SdLogger::getCurrentFilePath() const {
  if (!_workerCore) {
    const auto snapshot = storageSnapshot();
    memcpy(_snapshotPath, snapshot.path, sizeof(_snapshotPath));
    return _snapshotPath;
  }
  return _currentPath;
}

uint32_t SdLogger::getLogCount() const {
  if (!_workerCore) return storageSnapshot().rows;
  return _logCount;
}

uint32_t SdLogger::getFlushCount() const {
  if (!_workerCore) return storageSnapshot().flushes;
  return _flushCount;
}

uint16_t SdLogger::getBufferedBytes() const {
  if (!_workerCore) return storageSnapshot().bufferedBytes;
  return _bufferPos > 0xFFFF ? 0xFFFF : (uint16_t)_bufferPos;
}

uint16_t SdLogger::getLastWriteDurationMs() const {
  if (!_workerCore) return storageSnapshot().writeMs;
  return _lastWriteDurationMs;
}

uint32_t SdLogger::getLastLogMillis() const {
  if (!_workerCore) return storageSnapshot().lastLog;
  return _lastLogTime;
}

bool SdLogger::isFileOpen() const {
  if (!_workerCore) return storageSnapshot().fileOpen;
  return (bool)_logFile;
}

bool SdLogger::isRtcTimestampingAvailable() const {
  if (!_workerCore) return storageSnapshot().rtcValid;
  return _rtcAvailable;
}

// ============================================
// レート制御
// ============================================
void SdLogger::setLogRate(uint8_t rateHz) {
  if (rateHz < 1) rateHz = 1;
  if (rateHz > 10) rateHz = 10;
  setLogIntervalMs(1000UL / rateHz, SD_LOG_INTERVAL_MIN_MS, true);
}

uint8_t SdLogger::getLogRate() const {
  if (_logIntervalMs == 0 || _logIntervalMs > 1000UL) {
    return 0;
  }

  uint32_t rateHz = 1000UL / _logIntervalMs;
  if (rateHz > 255UL) {
    return 255;
  }
  return (uint8_t)rateHz;
}

bool SdLogger::setLogIntervalMs(uint32_t intervalMs, uint32_t minAllowedMs, bool persist) {
  const uint32_t minIntervalMs = normalizeLogIntervalMin(minAllowedMs);
  if (intervalMs < minIntervalMs || intervalMs > SD_LOG_INTERVAL_MAX_MS) {
    return false;
  }

  const uint32_t previousIntervalMs = _logIntervalMs;
  const bool previousPersisted = _logIntervalPersisted;
  _logIntervalMs = intervalMs;
  _logIntervalPersisted = false;

  if (persist && !saveLogIntervalSetting()) {
    _logIntervalMs = previousIntervalMs;
    _logIntervalPersisted = previousPersisted;
    return false;
  }

  _logIntervalPersisted = persist ? true : previousPersisted;
  _lastLogTime = millis() - _logIntervalMs;
  _canLog = true;
  _sampleGate.reset(_logIntervalMs);
  return true;
}

uint32_t SdLogger::getLogIntervalMs() const {
  return _logIntervalMs;
}

bool SdLogger::isLogIntervalPersisted() const {
  return _logIntervalPersisted;
}

bool SdLogger::restoreDefaultLogInterval(uint32_t minAllowedMs) {
  const uint32_t minIntervalMs = normalizeLogIntervalMin(minAllowedMs);
  const uint32_t intervalMs = SD_LOG_INTERVAL_DEFAULT_MS < minIntervalMs
                                ? minIntervalMs
                                : SD_LOG_INTERVAL_DEFAULT_MS;
  return setLogIntervalMs(intervalMs, minIntervalMs, true);
}

bool SdLogger::synchronizeLogIntervalToSource(uint32_t sourceIntervalMs) {
  if (sourceIntervalMs < SD_LOG_INTERVAL_MIN_MS ||
      sourceIntervalMs > SD_LOG_INTERVAL_MAX_MS) {
    return false;
  }

  if (SdLogIntervalPolicy::isExactSourceMultiple(_logIntervalMs, sourceIntervalMs)) {
    return true;
  }

  // Preserve the user's maximum-rate intent: an old unaligned NVS value is
  // rounded toward a longer interval, never toward a faster log rate.
  const uint32_t alignedIntervalMs = SdLogIntervalPolicy::alignAtOrAbove(
      _logIntervalMs, sourceIntervalMs, SD_LOG_INTERVAL_MAX_MS);
  if (alignedIntervalMs == 0U) {
    return false;
  }

  return setLogIntervalMs(alignedIntervalMs, sourceIntervalMs, true);
}

bool SdLogger::loadLogIntervalSetting() {
  Preferences prefs;
  if (!prefs.begin(SD_LOG_NVS_NAMESPACE, true)) {
    _logIntervalMs = SD_LOG_INTERVAL_DEFAULT_MS;
    _logIntervalPersisted = false;
    _autoStartEnabled = false;
    return false;
  }

  const bool hasSavedInterval = prefs.isKey(SD_LOG_NVS_INTERVAL_KEY);
  const uint32_t intervalMs = prefs.getUInt(SD_LOG_NVS_INTERVAL_KEY, SD_LOG_INTERVAL_DEFAULT_MS);
  const bool autoStartEnabled = prefs.getBool(SD_LOG_NVS_AUTO_START_KEY, false);
  prefs.end();

  _autoStartEnabled = autoStartEnabled;

  if (intervalMs < SD_LOG_INTERVAL_MIN_MS || intervalMs > SD_LOG_INTERVAL_MAX_MS) {
    _logIntervalMs = SD_LOG_INTERVAL_DEFAULT_MS;
    _logIntervalPersisted = false;
    return false;
  }

  _logIntervalMs = intervalMs;
  _logIntervalPersisted = hasSavedInterval;
  _lastLogTime = millis() - _logIntervalMs;
  _canLog = true;
  _sampleGate.reset(_logIntervalMs);
  return true;
}

bool SdLogger::saveLogIntervalSetting() const {
  Preferences prefs;
  if (!prefs.begin(SD_LOG_NVS_NAMESPACE, false)) {
    return false;
  }

  const size_t written = prefs.putUInt(SD_LOG_NVS_INTERVAL_KEY, _logIntervalMs);
  prefs.end();
  return written > 0;
}

bool SdLogger::saveAutoStartSetting(bool enabled) const {
  Preferences prefs;
  if (!prefs.begin(SD_LOG_NVS_NAMESPACE, false)) {
    return false;
  }

  const size_t written = prefs.putBool(SD_LOG_NVS_AUTO_START_KEY, enabled);
  prefs.end();
  return written > 0;
}

void SdLogger::update() {
  if (!_workerCore) return; // worker services sync even while input is paused
  // Keep small, partially filled buffers in RAM until the same one-second
  // durability deadline as a pending file write. Calling writeBuffer() every
  // loop here would defeat buffering and reintroduce unnecessary SD traffic.
  const bool hasPendingStorage = _bufferPos > 0 || _syncPending;
  if (_loggingEnabled && (_state == SD_STATE_READY || _state == SD_STATE_LOGGING) &&
      hasPendingStorage &&
      SdLogWritePolicy::shouldSync(true, _lastSyncMillis, millis(), false)) {
    (void)writeBuffer(false);
  }
  _canLog = canLog();
}

bool SdLogger::canLog() const {
  if (!_workerCore) return _recordingRequested && _inputActive && _sampleGate.isDue(millis());
  if (!_loggingEnabled || (_state != SD_STATE_READY && _state != SD_STATE_LOGGING)) {
    return false;
  }

  return _sampleGate.isDue(millis());
}

bool SdLogger::shouldLogSample(uint32_t sampleTimestampMs) {
  _sourceSampleCount++;
  if (_hasSourceSampleTimestamp) {
    _lastSourceSampleIntervalMs = sampleTimestampMs - _lastSourceSampleTimestampMs;
  }
  _lastSourceSampleTimestampMs = sampleTimestampMs;
  _hasSourceSampleTimestamp = true;

  if ((!_workerCore && (!_recordingRequested || !_inputActive)) ||
      (_workerCore && (!_loggingEnabled || (_state != SD_STATE_READY && _state != SD_STATE_LOGGING)))) {
    return false;
  }

  if (!_sampleGate.isDue(sampleTimestampMs)) {
    _rateLimitedSampleCount++;
    return false;
  }

  return true;
}

uint32_t SdLogger::getSourceSampleCount() const {
  return _sourceSampleCount;
}

uint32_t SdLogger::getRateLimitedSampleCount() const {
  return _rateLimitedSampleCount;
}

uint32_t SdLogger::getLastSourceSampleIntervalMs() const {
  return _lastSourceSampleIntervalMs;
}

uint32_t SdLogger::getLastLoggedSampleIntervalMs() const {
  return _lastLoggedSampleIntervalMs;
}
