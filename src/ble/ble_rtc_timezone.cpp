/**
 * @file ble_rtc_timezone.cpp
 * @brief Queued RTC timezone control and CTS write processing.
 */
#include "ble_manager.h"

#include "hardware/rtc_timezone_database.h"

namespace {

uint32_t getU32Le(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
      (static_cast<uint32_t>(data[1]) << 8) |
      (static_cast<uint32_t>(data[2]) << 16) |
      (static_cast<uint32_t>(data[3]) << 24);
}

int64_t getI64Le(const uint8_t* data) {
  uint64_t value = 0;
  for (uint8_t index = 0; index < 8; ++index) {
    value |= static_cast<uint64_t>(data[index]) << (index * 8U);
  }
  return static_cast<int64_t>(value);
}

void putU16Le(uint8_t* data, size_t offset, uint16_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void putU32Le(uint8_t* data, size_t offset, uint32_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
  data[offset + 2] = static_cast<uint8_t>(value >> 16);
  data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void putI16Le(uint8_t* data, size_t offset, int16_t value) {
  putU16Le(data, offset, static_cast<uint16_t>(value));
}

void putI64Le(uint8_t* data, size_t offset, int64_t signedValue) {
  const uint64_t value = static_cast<uint64_t>(signedValue);
  for (uint8_t index = 0; index < 8; ++index) {
    data[offset + index] = static_cast<uint8_t>(value >> (index * 8U));
  }
}

uint8_t bleResultForRtcResult(RtcOperationResult result) {
  switch (result) {
    case RtcOperationResult::Ok:
      return BLE_RTC_TIMEZONE_RESULT_OK;
    case RtcOperationResult::UnsupportedZone:
    case RtcOperationResult::TimezoneNotConfigured:
    case RtcOperationResult::LocalTimeAmbiguous:
      return BLE_RTC_TIMEZONE_RESULT_UNSUPPORTED_ZONE;
    case RtcOperationResult::TimeOutOfRange:
      return BLE_RTC_TIMEZONE_RESULT_TIME_OUT_OF_RANGE;
    case RtcOperationResult::NvsFailed:
      return BLE_RTC_TIMEZONE_RESULT_NVS_FAILED;
    case RtcOperationResult::RtcWriteFailed:
      return BLE_RTC_TIMEZONE_RESULT_RTC_WRITE_FAILED;
    case RtcOperationResult::ReadbackFailed:
      return BLE_RTC_TIMEZONE_RESULT_READBACK_FAILED;
    default:
      return BLE_RTC_TIMEZONE_RESULT_READBACK_FAILED;
  }
}

}  // namespace

void BleManager::clearRtcTimezoneRequest() {
  portENTER_CRITICAL(&_rtcTimezoneMux);
  _rtcTimezoneRequestPending = false;
  _rtcTimezoneProcessing = false;
  _rtcTimezoneRequest = BleRtcTimezoneRequest{};
  portEXIT_CRITICAL(&_rtcTimezoneMux);
}

bool BleManager::enqueueRtcTimezoneRequest(
    const BleRtcTimezoneRequest& request) {
  bool queued = false;
  portENTER_CRITICAL(&_rtcTimezoneMux);
  if (!_rtcTimezoneRequestPending && !_rtcTimezoneProcessing) {
    _rtcTimezoneRequest = request;
    _rtcTimezoneRequestPending = true;
    queued = true;
  }
  portEXIT_CRITICAL(&_rtcTimezoneMux);
  return queued;
}

void BleManager::publishRtcTimezoneStatus(uint8_t op, uint8_t result,
                                          bool notify,
                                          bool incrementGeneration,
                                          const RtcTimezoneSnapshot* cached) {
  if (!_pRtcTimezoneChar) return;
  (void)notify;  // UUID contract is Read/Write, not Notify.

  RtcTimezoneSnapshot snapshot{};
  if (cached) {
    snapshot = *cached;
  } else if (_pRtc) {
    (void)_pRtc->getTimezoneSnapshot(snapshot);
  }

  uint8_t status[BLE_RTC_TIMEZONE_STATUS_SIZE]{};
  status[0] = BLE_RTC_TIMEZONE_PROTOCOL_VERSION;
  if (snapshot.status.detected) {
    status[1] |= BLE_RTC_TIMEZONE_FLAG_RTC_DETECTED;
  }
  if (snapshot.status.busReadable) {
    status[1] |= BLE_RTC_TIMEZONE_FLAG_RTC_READABLE;
  }
  if (snapshot.status.utcValid) status[1] |= BLE_RTC_TIMEZONE_FLAG_UTC_VALID;
  if (snapshot.status.zoneConfigured) {
    status[1] |= BLE_RTC_TIMEZONE_FLAG_ZONE_CONFIGURED;
  }
  if (snapshot.status.nvsPersisted) status[1] |= BLE_RTC_TIMEZONE_FLAG_NVS_SAVED;
  if (snapshot.localValid && snapshot.dstOffsetMinutes != 0) {
    status[1] |= BLE_RTC_TIMEZONE_FLAG_DST_ACTIVE;
  }
  if (_rtcTimezoneProcessing) status[1] |= BLE_RTC_TIMEZONE_FLAG_PROCESSING;
  if (result != BLE_RTC_TIMEZONE_RESULT_OK ||
      (snapshot.status.zoneConfigured && !snapshot.status.zoneResolved)) {
    status[1] |= BLE_RTC_TIMEZONE_FLAG_ERROR;
  }
  status[2] = op;
  status[3] = result;
  putU32Le(status, 4, snapshot.zoneId);
  putI64Le(status, 8, snapshot.status.utcValid ? snapshot.unixSeconds : 0);
  putI16Le(status, 16, snapshot.localValid ? snapshot.totalOffsetMinutes : 0);
  putI16Le(status, 18, snapshot.localValid ? snapshot.standardOffsetMinutes : 0);
  putI16Le(status, 20, snapshot.localValid ? snapshot.dstOffsetMinutes : 0);

  putU16Le(status, 24, RtcTimezoneDatabase::TZDB_YEAR);
  status[26] = static_cast<uint8_t>(RtcTimezoneDatabase::TZDB_REVISION);
  status[27] = 0;

  portENTER_CRITICAL(&_rtcTimezoneMux);
  if (incrementGeneration) ++_rtcTimezoneOperationGeneration;
  _rtcTimezoneLastOp = op;
  _rtcTimezoneLastResult = result;
  putU16Le(status, 22, _rtcTimezoneOperationGeneration);
  memcpy(_rtcTimezoneLastStatus, status, sizeof(status));
  portEXIT_CRITICAL(&_rtcTimezoneMux);
  _pRtcTimezoneChar->setValue(status, sizeof(status));
}

void BleManager::publishRtcTimezoneBusyFromCallback(uint8_t op) {
  if (!_pRtcTimezoneChar) return;
  uint8_t status[BLE_RTC_TIMEZONE_STATUS_SIZE]{};
  portENTER_CRITICAL(&_rtcTimezoneMux);
  memcpy(status, _rtcTimezoneLastStatus, sizeof(status));
  ++_rtcTimezoneOperationGeneration;
  _rtcTimezoneLastOp = op;
  _rtcTimezoneLastResult = BLE_RTC_TIMEZONE_RESULT_BUSY;
  status[1] |= BLE_RTC_TIMEZONE_FLAG_PROCESSING |
               BLE_RTC_TIMEZONE_FLAG_ERROR;
  status[2] = op;
  status[3] = BLE_RTC_TIMEZONE_RESULT_BUSY;
  putU16Le(status, 22, _rtcTimezoneOperationGeneration);
  memcpy(_rtcTimezoneLastStatus, status, sizeof(status));
  portEXIT_CRITICAL(&_rtcTimezoneMux);
  _pRtcTimezoneChar->setValue(status, sizeof(status));
}

void BleManager::onRtcTimezoneRead() {
  // The characteristic already contains the main-loop cache.
}

void BleManager::onRtcTimezoneWrite(const uint8_t* data, size_t length) {
  BleRtcTimezoneRequest request{};
  request.kind = BleRtcRequestKind::TimezoneControl;
  request.op = data && length > 0 ? data[0] : 0;

  if (!data || (length != BLE_RTC_TIMEZONE_SET_ZONE_SIZE &&
                length != BLE_RTC_TIMEZONE_SYNC_SIZE)) {
    request.validationResult = BLE_RTC_TIMEZONE_RESULT_INVALID_LENGTH;
  } else if (data[1] != BLE_RTC_TIMEZONE_PROTOCOL_VERSION) {
    request.validationResult = BLE_RTC_TIMEZONE_RESULT_INVALID_VERSION;
  } else if (request.op != BLE_RTC_TIMEZONE_OP_SET_ZONE &&
             request.op != BLE_RTC_TIMEZONE_OP_SYNC_UTC_AND_ZONE) {
    request.validationResult = BLE_RTC_TIMEZONE_RESULT_INVALID_OP;
  } else if ((request.op == BLE_RTC_TIMEZONE_OP_SET_ZONE &&
              length != BLE_RTC_TIMEZONE_SET_ZONE_SIZE) ||
             (request.op == BLE_RTC_TIMEZONE_OP_SYNC_UTC_AND_ZONE &&
              length != BLE_RTC_TIMEZONE_SYNC_SIZE)) {
    request.validationResult = BLE_RTC_TIMEZONE_RESULT_INVALID_LENGTH;
  } else {
    request.zoneId = getU32Le(data + 2);
    if (request.op == BLE_RTC_TIMEZONE_OP_SYNC_UTC_AND_ZONE) {
      request.unixSeconds = getI64Le(data + 6);
    }
  }

  if (!enqueueRtcTimezoneRequest(request)) {
    publishRtcTimezoneBusyFromCallback(request.op);
  }
}

void BleManager::onLocalTimeInfoRead() {
  // The characteristic already contains the main-loop cache.
}

void BleManager::serviceRtcTimeCaches() {
  if (!_running || !_pRtc || _rtcTimezoneProcessing) return;
  const uint32_t now = millis();
  if (_rtcTimeCacheLastRefreshMs != 0 &&
      static_cast<uint32_t>(now - _rtcTimeCacheLastRefreshMs) < 1000U) {
    return;
  }

  RtcTimezoneSnapshot snapshot{};
  (void)_pRtc->getTimezoneSnapshot(snapshot);
  updateCurrentTimeChar(snapshot);
  updateLocalTimeInfoChar(snapshot);
  publishRtcTimezoneStatus(_rtcTimezoneLastOp, _rtcTimezoneLastResult,
                           false, false, &snapshot);
  _rtcTimeCacheLastRefreshMs = now;
}

void BleManager::processRtcTimezoneRequest() {
  BleRtcTimezoneRequest request{};
  portENTER_CRITICAL(&_rtcTimezoneMux);
  if (!_rtcTimezoneRequestPending || _rtcTimezoneProcessing) {
    portEXIT_CRITICAL(&_rtcTimezoneMux);
    return;
  }
  request = _rtcTimezoneRequest;
  _rtcTimezoneRequestPending = false;
  _rtcTimezoneProcessing = true;
  portEXIT_CRITICAL(&_rtcTimezoneMux);

  uint8_t result = request.validationResult;
  if (request.kind == BleRtcRequestKind::TimezoneControl &&
      result == BLE_RTC_TIMEZONE_RESULT_OK) {
    publishRtcTimezoneStatus(request.op, BLE_RTC_TIMEZONE_RESULT_BUSY,
                             true, false);
    RtcOperationResult rtcResult = RtcOperationResult::RtcWriteFailed;
    if (!_pRtc) {
      rtcResult = RtcOperationResult::RtcWriteFailed;
    } else if (request.op == BLE_RTC_TIMEZONE_OP_SET_ZONE) {
      rtcResult = _pRtc->setTimezoneZoneId(request.zoneId);
    } else {
      rtcResult = _pRtc->syncUtcAndZone(request.unixSeconds, request.zoneId);
    }
    result = bleResultForRtcResult(rtcResult);
  } else if (request.kind == BleRtcRequestKind::CtsLocalTime) {
    if (result == BLE_RTC_TIMEZONE_RESULT_OK && _pRtc) {
      if (!_pRtc->isTimezoneConfigured() || !_pRtc->isTimezoneResolved()) {
        result = BLE_RTC_TIMEZONE_RESULT_UNSUPPORTED_ZONE;
      } else {
        const RtcOperationResult rtcResult =
            _pRtc->syncLocalDateTime(request.local);
        if (rtcResult == RtcOperationResult::LocalTimeAmbiguous) {
          result = BLE_RTC_TIMEZONE_RESULT_TIME_OUT_OF_RANGE;
        } else {
          result = bleResultForRtcResult(rtcResult);
        }
      }
    }
  }

  portENTER_CRITICAL(&_rtcTimezoneMux);
  _rtcTimezoneProcessing = false;
  portEXIT_CRITICAL(&_rtcTimezoneMux);

  RtcTimezoneSnapshot snapshot{};
  if (_pRtc) (void)_pRtc->getTimezoneSnapshot(snapshot);
  updateCurrentTimeChar(snapshot);
  updateLocalTimeInfoChar(snapshot);
  updateDeviceHealthStatus(true, true);
  if (_pCurrentTimeChar && _connectionCount > 0) _pCurrentTimeChar->notify();

  publishRtcTimezoneStatus(
      request.kind == BleRtcRequestKind::TimezoneControl ? request.op : 0,
      result, false, true, &snapshot);
  _rtcTimeCacheLastRefreshMs = millis();
}
