/**
 * @file sd_buffer_ops.cpp
 * @brief SDカードバッファ操作モジュール
 * @date 2025-12-07
 * 
 * SDロガーのバッファ管理機能:
 * - バッファへのデータ追加
 * - バッファからファイルへの書込み
 * - ファイル分割判定
 * - SDカード容量取得
 */

#include "sd_logger.h"
#include <M5Unified.h>

// ============================================
// プライベート: ファイル分割判定
// ============================================
/**
 * @brief 新しいログファイルが必要かを判定
 * 
 * 以下の条件で新しいファイルを作成:
 * - ファイルが開いていない
 * - 日付が変わった
 * - ファイル作成から30分経過した
 * 
 * @param dt 現在のRTC日時
 * @return true: 新しいファイルが必要
 */
bool SdLogger::needsNewFile(const RtcDateTime& dt) {
  // ファイルが開いていない
  if (!_logFile) {
    return true;
  }
  
  // 日付が変わった
  if (dt.year != _currentFileDate.year ||
      dt.month != _currentFileDate.month ||
      dt.day != _currentFileDate.day) {
    return true;
  }
  
  // ファイル作成時刻から30分経過したかを判定
  // ファイル作成時刻との時間差を分単位で計算
  int minutesDiff = (dt.hour * 60 + dt.minute) - 
                    (_fileCreationTime.hour * 60 + _fileCreationTime.minute);
  
  // 30分以上経過していれば新しいファイルが必要
  if (minutesDiff < 0 || minutesDiff >= SD_FILE_SPLIT_MINUTES) {
    return true;
  }
  
  return false;
}

// ============================================
// プライベート: バッファ追加
// ============================================
/**
 * @brief バッファに文字列を追加
 * 
 * バッファサイズを超える場合はfalseを返す
 * 
 * @param str 追加する文字列
 * @return true: 成功, false: バッファフル
 */
bool SdLogger::appendToBuffer(const char* str) {
  size_t len = strlen(str);
  
  // バッファに収まるかチェック
  if (_bufferPos + len >= SD_BUFFER_SIZE) {
    return false;  // バッファフル
  }
  
  memcpy(_buffer + _bufferPos, str, len);
  _bufferPos += len;
  
  return true;
}

// ============================================
// プライベート: バッファ書込み
// ============================================
/**
 * @brief バッファ内容をファイルに書き込み
 * 
 * 書込み後、バッファをクリアしフラッシュカウントを更新
 * 
 * @return true: 成功
 */
bool SdLogger::writeBuffer(bool forceSync) {
  if (!_logFile) {
    return false;
  }

  const size_t bytesToWrite = _bufferPos;
  if (bytesToWrite == 0 && !_syncPending) {
    return true;
  }

  const uint32_t writeStartTime = millis();
  if (bytesToWrite > 0) {
    const size_t written = _logFile.write((uint8_t*)_buffer, bytesToWrite);
    if (written != bytesToWrite) {
      // M5.Log.printf("SD: Write error: %d/%d\n", written, bytesToWrite);
      failStorageWrite();
      return false;
    }

    _bufferPos = 0;
    _totalStorageBytes += written;
    _pendingSyncRows += _bufferedRows;
    _bufferedRows = 0;
    _syncPending = true;
  }

  const uint32_t now = millis();
  if (SdLogWritePolicy::shouldSync(_syncPending, _lastSyncMillis, now, forceSync)) {
    // Checked POSIX VFS synchronization propagates media errors to the caller.
    if (!_logFile.flush()) {
      failStorageWrite();
      return false;
    }
    _syncedLogCount += _pendingSyncRows;
    _pendingSyncRows = 0;
    _syncPending = false;
    _lastSyncMillis = millis();
    _flushCount++;
  }

  const uint32_t writeDuration = millis() - writeStartTime;
  _lastWriteDurationMs = writeDuration > 0xFFFF ? 0xFFFF : (uint16_t)writeDuration;
  if (SdLogWritePolicy::isSlowWriteAdvisory(writeDuration)) {
    M5.Log.printf("[SD] Slow committed write: %lums\n", (unsigned long)writeDuration);
  }

  return true;
}

void SdLogger::failStorageWrite() {
  _uncertainRows += _bufferedRows + _pendingSyncRows;
  _state = SD_STATE_MOUNT_ERROR;
  _loggingEnabled = false;
  _stopReason = _logFile.error() == ENOSPC ? SD_STOP_CAPACITY : SD_STOP_WRITE_ERROR;
  _canLog = false;
  _bufferPos = 0;
  _bufferedRows = 0;
  _pendingSyncRows = 0;
  _syncPending = false;
  (void)_logFile.close();
}

bool SdLogger::closeCheckedLogFile() {
  if (_logFile.close()) return true;
  failStorageWrite();
  return false;
}

// ============================================
// SDカード容量取得
// ============================================
/**
 * @brief SDカードの空き容量を取得
 * @return 空き容量 [MB]
 */
uint32_t SdLogger::getFreeSpaceMB() const {
  if (!_workerCore) return storageSnapshot().freeMB;
  if (_state == SD_STATE_NO_CARD || _state == SD_STATE_UNINITIALIZED) {
    return 0;
  }
  
  uint64_t totalBytes = SD.totalBytes();
  uint64_t usedBytes = SD.usedBytes();
  uint64_t freeBytes = totalBytes - usedBytes;
  
  return (uint32_t)(freeBytes / (1024 * 1024));
}

/**
 * @brief SDカードの総容量を取得
 * @return 総容量 [MB]
 */
uint32_t SdLogger::getTotalSpaceMB() const {
  if (!_workerCore) return storageSnapshot().totalMB;
  if (_state == SD_STATE_NO_CARD || _state == SD_STATE_UNINITIALIZED) {
    return 0;
  }
  
  return (uint32_t)(SD.totalBytes() / (1024 * 1024));
}

/**
 * @brief SDカードの使用率を取得
 * @return 使用率 [%]
 */
uint8_t SdLogger::getUsagePercent() const {
  if (!_workerCore) return storageSnapshot().usage;
  if (_state == SD_STATE_NO_CARD || _state == SD_STATE_UNINITIALIZED) {
    return 0;
  }
  
  uint64_t totalBytes = SD.totalBytes();
  if (totalBytes == 0) return 0;
  
  uint64_t usedBytes = SD.usedBytes();
  return (uint8_t)((usedBytes * 100) / totalBytes);
}

uint8_t SdLogger::getCardTypeCode() const {
  if (!_workerCore) return storageSnapshot().cardType;
  if (_state == SD_STATE_NO_CARD || _state == SD_STATE_UNINITIALIZED) {
    return SD_CARD_TYPE_NONE;
  }

  switch (SD.cardType()) {
    case CARD_NONE: return SD_CARD_TYPE_NONE;
    case CARD_MMC:  return SD_CARD_TYPE_MMC;
    case CARD_SD:   return SD_CARD_TYPE_SD;
    case CARD_SDHC: return SD_CARD_TYPE_SDHC;
    default:        return SD_CARD_TYPE_UNKNOWN;
  }
}

const char* SdLogger::getCardTypeString() const {
  switch (getCardTypeCode()) {
    case SD_CARD_TYPE_NONE:    return "None";
    case SD_CARD_TYPE_MMC:     return "MMC";
    case SD_CARD_TYPE_SD:      return "SD";
    case SD_CARD_TYPE_SDHC:    return "SDHC/SDXC";
    case SD_CARD_TYPE_UNKNOWN:
    default:                   return "Unknown";
  }
}
