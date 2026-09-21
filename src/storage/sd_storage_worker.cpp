/**
 * @file sd_storage_worker.cpp
 * @brief Bounded SD work queue, single storage owner, and recovery service.
 */
#include "storage/sd_logger.h"
#include <esp_timer.h>
#include <new>
#include <type_traits>

namespace {
constexpr uint64_t kReserveBytes = 1024ULL * 1024ULL;
constexpr uint32_t kCapacityRefreshMs = 30000U;
constexpr unsigned kMaximumRecoveryAttempts = 5;
// A durability command is queued behind every sample already accepted.  Share
// one bounded deadline between waiting for a full queue slot and draining the
// backlog, instead of failing after an unrelated 100 ms enqueue timeout.
constexpr uint32_t kStorageCommandTimeoutMs = 2000U;
constexpr uint64_t kStorageCommandTimeoutUs = 2000000ULL;
}

bool SdLogger::startStorageWorker(RtcManager* rtc) {
  static_assert(std::is_trivially_copyable<StorageMessage>::value,
                "The FreeRTOS queue requires value-only records");
  if (_storageTask) return isAvailable();
  _rtc = rtc;
  loadLogIntervalSetting();
  _sampleGate.reset(_logIntervalMs);
  _storage = new (std::nothrow) SdLogger(true, _fileOperations);
  if (!_storage) return false;
  const bool mounted = _storage->begin(rtc);
  _storageQueue = xQueueCreate(kQueueLength, sizeof(StorageMessage));
  if (!_storageQueue) { delete _storage; _storage = nullptr; return false; }
  const uint64_t total = mounted ? SD.totalBytes() : 0;
  const uint64_t used = mounted ? SD.usedBytes() : 0;
  publishStorageSnapshot(false, used <= total ? total - used : 0, total);
  if (xTaskCreate(storageTaskEntry, "sd_writer", 6144, this, 1, &_storageTask) != pdPASS) {
    vQueueDelete(_storageQueue);
    _storageQueue = nullptr;
    delete _storage;
    _storage = nullptr;
    return false;
  }
  return mounted;
}

SdLogger::StorageSnapshot SdLogger::storageSnapshot() const {
  portENTER_CRITICAL(&_snapshotMux);
  const auto snapshot = _snapshot;
  portEXIT_CRITICAL(&_snapshotMux);
  return snapshot;
}

void SdLogger::publishStorageSnapshot(bool recovering, uint64_t freeBytes, uint64_t totalBytes) {
  StorageSnapshot value;
  value.state = _storage->_state;
  value.reason = _storage->_stopReason;
  value.enabled = _storage->_loggingEnabled;
  value.fileOpen = static_cast<bool>(_storage->_logFile);
  value.rtcValid = _storage->_rtcAvailable;
  value.recovering = recovering;
  value.rows = _storage->_logCount;
  value.syncedRows = _storage->_syncedLogCount;
  value.uncertainRows = _storage->_uncertainRows;
  value.flushes = _storage->_flushCount;
  value.lastLog = _storage->_lastLogTime;
  value.bufferedBytes = _storage->_bufferPos;
  value.writeMs = _storage->_lastWriteDurationMs;
  value.freeMB = freeBytes / (1024ULL * 1024ULL);
  value.totalMB = totalBytes / (1024ULL * 1024ULL);
  value.usage = totalBytes ? (totalBytes - freeBytes) * 100ULL / totalBytes : 0;
  value.cardType = _storage->getCardTypeCode();
  memcpy(value.path, _storage->_currentPath, sizeof(value.path));
  portENTER_CRITICAL(&_snapshotMux);
  _snapshot = value;
  portEXIT_CRITICAL(&_snapshotMux);
}

bool SdLogger::enqueueSample(const WindData& data) {
  if (!_storageQueue || !_recordingRequested || !_inputActive) return false;
  StorageMessage message;
  message.data = data;
  message.ticket = ++_recordOrdinal;
  // RTC belongs to the measurement task. No RTC/I2C transaction is performed
  // by the storage task or the FAT callback during a card write.
  const bool snapshotRead = _rtc && _rtc->getTimezoneSnapshot(message.rtc);
  const bool rtcTimestampAvailable = snapshotRead &&
      message.rtc.status.utcValid &&
      (message.rtc.localValid || message.rtc.utcFallback);
  if (rtcTimestampAvailable) {
    const uint64_t stamp = static_cast<uint64_t>(message.rtc.unixSeconds);
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
    if (!_rtcObserved || stamp != _observedRtcStamp) {
      _observedRtcStamp = stamp;
      _rtcChangedAtMs = nowMs;
      _rtcObserved = true;
    } else if (nowMs - _rtcChangedAtMs >= 3000U) {
      message.rtc.status.utcValid = false;
      message.rtc.localValid = false;
      message.rtc.utcFallback = false;
    }
  } else {
    _rtcObserved = false;
  }
  const uint32_t timestamp = data.sourceTimestampValid ? data.sourceTimestamp : data.timestamp;
  _sampleGate.accept(timestamp);
  if (_hasLoggedSampleTimestamp) _lastLoggedSampleIntervalMs = timestamp - _lastLoggedSampleTimestampMs;
  _lastLoggedSampleTimestampMs = timestamp;
  _hasLoggedSampleTimestamp = true;
  if (xQueueSend(_storageQueue, &message, 0) != pdTRUE) {
    ++_droppedRows;
    return false;
  }
  return true;
}

bool SdLogger::storageCommand(StorageOperation operation) {
  if (!_storageQueue || _completedTicket.load() != _issuedTicket) return false;
  _quiescent = false;
  StorageMessage message;
  message.operation = operation;
  message.ticket = _issuedTicket + 1;
  const uint64_t deadline = esp_timer_get_time() + kStorageCommandTimeoutUs;
  if (xQueueSend(_storageQueue, &message,
                 pdMS_TO_TICKS(kStorageCommandTimeoutMs)) != pdTRUE) return false;
  _issuedTicket = message.ticket;
  while (_completedTicket.load() != message.ticket) {
    if (static_cast<uint64_t>(esp_timer_get_time()) >= deadline) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return _commandSuccess.load();
}

void SdLogger::storageTaskEntry(void* context) {
  static_cast<SdLogger*>(context)->storageTaskLoop();
}

void SdLogger::storageTaskLoop() {
  uint64_t totalBytes = 0, freeBytes = 0;
  uint32_t lastCapacity = 0;
  uint64_t lastWrittenBytes = _storage->_totalStorageBytes;
  bool recovering = false;
  unsigned recoveryAttempts = 0;
  uint64_t nextRecoveryMs = 0;
  auto refreshCapacity = [&]() {
    totalBytes = _storage->isAvailable() ? SD.totalBytes() : 0;
    const uint64_t used = totalBytes ? SD.usedBytes() : 0;
    freeBytes = used <= totalBytes ? totalBytes - used : 0;
    lastCapacity = millis();
    lastWrittenBytes = _storage->_totalStorageBytes;
  };
  refreshCapacity();
  for (;;) {
    StorageMessage message;
    const bool received = xQueueReceive(_storageQueue, &message, pdMS_TO_TICKS(20)) == pdTRUE;
    if (received && message.operation != StorageOperation::Sample) {
      bool result = true;
      switch (message.operation) {
        case StorageOperation::Start:
          recovering = false;
          recoveryAttempts = 0;
          result = _recordingRequested && _storage->resumeLogging();
          refreshCapacity();
          if (result && freeBytes <= kReserveBytes) {
            _storage->disableLogging(SD_STOP_CAPACITY);
            result = false;
          }
          break;
        case StorageOperation::Stop:
          recovering = false;
          _storage->disableLogging(SD_STOP_USER_DISABLED);
          result = _storage->getStopReason() == SD_STOP_USER_DISABLED;
          break;
        case StorageOperation::Flush:
          result = !_storage->_logFile || _storage->flush();
          break;
        case StorageOperation::Remount:
          recovering = false;
          result = _storage->remount();
          refreshCapacity();
          break;
        default: break;
      }
      publishStorageSnapshot(recovering, freeBytes, totalBytes);
      _commandSuccess = result;
      _completedTicket = message.ticket;
    } else if (received) {
      if (!_storage->isLoggingEnabled() || recovering) {
        ++_droppedRows;
      } else if (freeBytes <= kReserveBytes + 192U) {
        _storage->disableLogging(SD_STOP_CAPACITY);
        _recordingRequested = false;
        ++_droppedRows;
      } else {
        const uint32_t previousFileNumber = _storage->_fileNumber;
        _storage->_capturedRtc = message.rtc;
        _storage->_recordOrdinal = message.ticket;
        _storage->_recordDropped = _droppedRows;
        if (!_storage->log(message.data)) ++_droppedRows;
        // Include allocation and directory overhead whenever a new file was
        // attempted, not just the byte count of successful payload writes.
        if (_storage->_fileNumber != previousFileNumber) refreshCapacity();
      }
    }
    _storage->update();
    if (_storage->getStopReason() == SD_STOP_CAPACITY) _recordingRequested = false;
    const uint64_t writtenDelta = _storage->_totalStorageBytes - lastWrittenBytes;
    freeBytes = writtenDelta <= freeBytes ? freeBytes - writtenDelta : 0;
    lastWrittenBytes = _storage->_totalStorageBytes;
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
    if (_recordingRequested && !_storage->isLoggingEnabled() &&
        _storage->getStopReason() != SD_STOP_CAPACITY) {
      if (!recovering) { recovering = true; nextRecoveryMs = nowMs + 1000U; }
      if (nowMs >= nextRecoveryMs) {
        if (recoveryAttempts >= kMaximumRecoveryAttempts) {
          recovering = false;
          _recordingRequested = false;
          _storage->_stopReason = SD_STOP_RETRY_EXHAUSTED;
        } else if ((++recoveryAttempts, _storage->remount()) && _storage->resumeLogging()) {
          recovering = false;
          refreshCapacity();
        } else if (recoveryAttempts >= kMaximumRecoveryAttempts) {
          recovering = false;
          _recordingRequested = false;
          _storage->_stopReason = SD_STOP_RETRY_EXHAUSTED;
        } else {
          nextRecoveryMs = nowMs + (1000ULL << recoveryAttempts);
        }
      }
    }
    // Successful recovery attempts remain charged until a new explicit start;
    // a repeatedly failing card must not retry forever after brief successes.
    if (!_recordingRequested && recovering) recovering = false;
    if (!_recordingRequested && uxQueueMessagesWaiting(_storageQueue) == 0) {
      if (_storage->_logFile) _storage->stop();
      _quiescent = true;
    }
    if (static_cast<uint32_t>(millis() - lastCapacity) >= kCapacityRefreshMs) refreshCapacity();
    publishStorageSnapshot(recovering, freeBytes, totalBytes);
  }
}

bool SdLogger::isRecordingRequested() const { return _recordingRequested; }
bool SdLogger::isStorageQuiescent() const {
  return !_recordingRequested && _quiescent;
}
void SdLogger::setInputActive(bool active) {
  const bool previous = _inputActive.exchange(active);
  if (previous && !active && _storageTask) (void)storageCommand(StorageOperation::Flush);
  if (!previous && active) _sampleGate.reset(_logIntervalMs);
}
uint32_t SdLogger::getSyncedLogCount() const { return storageSnapshot().syncedRows; }
uint32_t SdLogger::getDroppedLogCount() const { return _droppedRows; }
uint32_t SdLogger::getUncertainLogCount() const { return storageSnapshot().uncertainRows; }
uint16_t SdLogger::getQueueDepth() const {
  return _storageQueue ? uxQueueMessagesWaiting(_storageQueue) : 0;
}
bool SdLogger::isRecovering() const { return storageSnapshot().recovering; }
bool SdLogger::isInputPaused() const { return _recordingRequested && !_inputActive; }
