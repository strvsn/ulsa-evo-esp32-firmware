/**
 * @file rtc_manager.cpp
 * @brief PCF8563 RTC管理モジュール実装
 * @date 2025-12-03
 */

#include "rtc_manager.h"
#include "rtc_timezone_database.h"
#include <M5Unified.h>
#include <string.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>
#endif

namespace {
constexpr size_t RTC_FILENAME_DATE_BUFFER_SIZE = 14;  // 20251203(Wed) + NUL
constexpr size_t RTC_FILENAME_DATETIME_BUFFER_SIZE = 16; // YYYYMMDD_hhmmss + NUL
constexpr size_t RTC_LOG_DATETIME_BUFFER_SIZE = 20;   // YYYY-MM-DD HH:MM:SS + NUL
constexpr char RTC_CONFIG_NAMESPACE[] = "rtc_cfg";
constexpr char RTC_CONFIG_KEY[] = "state";
}

// ============================================
// コンストラクタ
// ============================================
RtcManager::RtcManager()
  : _wire(nullptr)
  , _available(false)
  , _configState(RtcConfigState::Unconfigured)
  , _zoneId(0)
  , _nvsPersisted(false)
  , _configGeneration(0)
#if !defined(ARDUINO_ARCH_ESP32)
  , _hostConfigPresent(false)
  , _hostConfigBlob{}
#endif
{
}

// ============================================
// 初期化
// ============================================
bool RtcManager::begin(TwoWire* wire) {
  _wire = wire;
  (void)loadPersistedConfig();
  if (!_wire) {
    _available = false;
    return false;
  }
  
  // I2C通信テスト
  _wire->beginTransmission(PCF8563_I2C_ADDRESS);
  if (_wire->endTransmission() != 0) {
    M5.Log.println("[RTC] PCF8563 not found");
    _available = false;
    return false;
  }
  
  _available = true;
  M5.Log.println("[RTC] PCF8563 initialized");
  
  RtcStatus status{};
  if (!getStatus(status)) {
    M5.Log.println("[RTC] Warning: initial status read failed");
  } else if (!status.timeValid) {
    M5.Log.println("[RTC] Warning: stored UTC is invalid; explicit sync required");
  }
  
  return true;
}

// ============================================
// 状態確認
// ============================================
bool RtcManager::isAvailable() const {
  return _available;
}

bool RtcManager::isRunning() const {
  RtcStatus status{};
  if (!const_cast<RtcManager*>(this)->getStatus(status)) {
    return false;
  }
  return status.busReadable && !status.clockStopped;
}

bool RtcManager::getStatus(RtcStatus& status) {
  return readSnapshot(status, nullptr);
}

// ============================================
// UTC / timezone snapshot
// ============================================
bool RtcManager::getUtcDateTime(RtcDateTime& dt) {
  RtcStatus status{};
  return readSnapshot(status, &dt) && status.timeValid;
}

bool RtcManager::getLocalDateTime(RtcDateTime& local) {
  RtcTimezoneSnapshot snapshot{};
  return getTimezoneSnapshot(snapshot) && snapshot.localValid &&
      ((local = snapshot.local), true);
}

bool RtcManager::getTimezoneSnapshot(RtcTimezoneSnapshot& snapshot) {
  snapshot = {};
  snapshot.zoneId = _zoneId;
  snapshot.configGeneration = _configGeneration;
  if (!readSnapshot(snapshot.status, &snapshot.utc)) return false;
  if (!snapshot.status.utcValid ||
#if defined(ARDUINO_ARCH_ESP32)
      !RtcTimezoneDatabase::utcDateTimeToUnixSeconds(
          snapshot.utc, snapshot.unixSeconds)
#else
      true
#endif
  ) {
    return true;
  }

#if defined(ARDUINO_ARCH_ESP32)
  if (snapshot.status.zoneResolved &&
      RtcTimezoneDatabase::unixSecondsToLocal(
          snapshot.unixSeconds, _zoneId, snapshot.local,
          snapshot.standardOffsetMinutes, snapshot.dstOffsetMinutes,
          snapshot.totalOffsetMinutes)) {
    snapshot.localValid = true;
    return true;
  }

  // TZDB upgrades may remove/rename an ID. Keep the proven UTC timeline and
  // explicitly mark the local conversion unavailable. SD logging can continue
  // in UTC, while CTS and the app require the user to select a supported zone.
  snapshot.local = snapshot.utc;
  snapshot.standardOffsetMinutes = 0;
  snapshot.dstOffsetMinutes = 0;
  snapshot.totalOffsetMinutes = 0;
  snapshot.utcFallback = true;
#endif
  return true;
}

// ============================================
// UTC register write
// ============================================
RtcOperationResult RtcManager::writeUtcDateTime(const RtcDateTime& dt) {
  if (!_available) return RtcOperationResult::RtcWriteFailed;
  if (!dt.isValid() || dt.year < 2000 || dt.year > 2099) {
    return RtcOperationResult::TimeOutOfRange;
  }

  // CTSのweekday=0（不明）やCLI入力の誤った曜日をRTCに残さない。
  RtcDateTime normalized = dt;
  normalized.weekday = calculateWeekday(dt.year, dt.month, dt.day);
  
  // PCF8563の推奨手順に従い、一度STOPしてから日時を書き込み、最後に
  // STOPを解除する。これにより停止状態のまま時刻だけが更新される事態を防ぐ。
  if (!writeRegister(PCF8563_REG_CONTROL1, PCF8563_CONTROL1_STOP)) {
    return RtcOperationResult::RtcWriteFailed;
  }

  // 10進 → BCD変換してバッファに格納
  uint8_t buffer[7];
  buffer[0] = decToBcd(normalized.second) & 0x7F;   // VL=0
  buffer[1] = decToBcd(normalized.minute);
  buffer[2] = decToBcd(normalized.hour);
  buffer[3] = decToBcd(normalized.day);
  buffer[4] = normalized.weekday & 0x07;
  buffer[5] = decToBcd(normalized.month);
  buffer[6] = decToBcd(normalized.year - 2000);
  
  // 転送失敗時は途中まで日時レジスタへ到達した可能性がある。ここで時計を
  // 再開すると、もっともらしいが誤った時刻を有効扱いする危険があるため、
  // STOPのまま明示同期を要求する。
  if (!writeRegisters(PCF8563_REG_SECONDS, buffer, 7)) {
    M5.Log.println("[RTC] Time write failed; RTC remains stopped until resync");
    return RtcOperationResult::RtcWriteFailed;
  }
  if (!writeRegister(PCF8563_REG_CONTROL1, 0x00)) {
    M5.Log.println("[RTC] Failed to restart RTC after time write");
    return RtcOperationResult::RtcWriteFailed;
  }

  RtcDateTime readback{};
  RtcStatus status{};
  if (!readSnapshot(status, &readback) || !status.hardwareTimeValid ||
      !isReadbackConfirmed(normalized, readback)) {
    M5.Log.println("[RTC] Time set readback verification failed");
    return RtcOperationResult::ReadbackFailed;
  }
  
  M5.Log.printf("[RTC] Time set: %04d-%02d-%02d %02d:%02d:%02d\n",
                normalized.year, normalized.month, normalized.day,
                normalized.hour, normalized.minute, normalized.second);
  
  return RtcOperationResult::Ok;
}

bool RtcManager::setUtcDateTime(const RtcDateTime& utc) {
  return writeUtcDateTime(utc) == RtcOperationResult::Ok;
}

RtcOperationResult RtcManager::syncUtcAndZone(int64_t unixSeconds,
                                              uint32_t zoneId) {
#if !defined(ARDUINO_ARCH_ESP32)
  (void)unixSeconds;
  (void)zoneId;
  return RtcOperationResult::UnsupportedZone;
#else
  if (!RtcTimezoneDatabase::isSupportedZone(zoneId)) {
    return RtcOperationResult::UnsupportedZone;
  }
  RtcDateTime utc{};
  if (!RtcTimezoneDatabase::unixSecondsToUtcDateTime(unixSeconds, utc)) {
    return RtcOperationResult::TimeOutOfRange;
  }
  RtcDateTime local{};
  int16_t standardOffsetMinutes = 0;
  int16_t dstOffsetMinutes = 0;
  int16_t totalOffsetMinutes = 0;
  if (!RtcTimezoneDatabase::unixSecondsToLocal(
          unixSeconds, zoneId, local, standardOffsetMinutes,
          dstOffsetMinutes, totalOffsetMinutes)) {
    // PCF8563 stores a UTC year in [2000,2099], but the corresponding local
    // civil date can cross into 1999 or 2100 near the endpoints. Never commit
    // a valid UTC+zone pair that the public local/CTS contract cannot express.
    return RtcOperationResult::TimeOutOfRange;
  }
  if (!writeAndConfirmConfig(RtcConfigState::Pending, zoneId)) {
    forcePersistedConfigInvalid(zoneId);
    return RtcOperationResult::NvsFailed;
  }
  const RtcOperationResult rtcResult = writeUtcDateTime(utc);
  if (rtcResult != RtcOperationResult::Ok) return rtcResult;
  if (!writeAndConfirmConfig(RtcConfigState::Valid, zoneId)) {
    // The final write may have reached flash even when its readback failed.
    // Overwrite it with pending (or remove the key) before returning so a
    // reboot cannot accept an unverified valid record.
    forcePersistedConfigInvalid(zoneId);
    return RtcOperationResult::NvsFailed;
  }
  ++_configGeneration;
  return RtcOperationResult::Ok;
#endif
}

RtcOperationResult RtcManager::setTimezoneZoneId(uint32_t zoneId) {
#if !defined(ARDUINO_ARCH_ESP32)
  (void)zoneId;
  return RtcOperationResult::UnsupportedZone;
#else
  if (!RtcTimezoneDatabase::isSupportedZone(zoneId)) {
    return RtcOperationResult::UnsupportedZone;
  }
  const RtcConfigState previousState = _configState;
  const uint32_t previousZoneId = _zoneId;
  if (previousState == RtcConfigState::Valid) {
    RtcDateTime utc{};
    RtcStatus rtcStatus{};
    int64_t unixSeconds = 0;
    RtcDateTime local{};
    int16_t standardOffsetMinutes = 0;
    int16_t dstOffsetMinutes = 0;
    int16_t totalOffsetMinutes = 0;
    if (!readRawUtcDateTime(utc, &rtcStatus)) {
      return RtcOperationResult::ReadbackFailed;
    }
    if (!RtcTimezoneDatabase::utcDateTimeToUnixSeconds(utc, unixSeconds) ||
        !RtcTimezoneDatabase::unixSecondsToLocal(
            unixSeconds, zoneId, local, standardOffsetMinutes,
            dstOffsetMinutes, totalOffsetMinutes)) {
      return RtcOperationResult::TimeOutOfRange;
    }
  }
  const RtcConfigState nextState =
      _configState == RtcConfigState::Valid
          ? RtcConfigState::Valid : RtcConfigState::ZoneOnly;
  if (!writeAndConfirmConfig(nextState, zoneId)) {
    const bool previousCanBeRestored =
        (previousState == RtcConfigState::Valid ||
         previousState == RtcConfigState::ZoneOnly) &&
        previousZoneId != 0 &&
        writeAndConfirmConfig(previousState, previousZoneId);
    if (!previousCanBeRestored) forcePersistedConfigInvalid(zoneId);
    return RtcOperationResult::NvsFailed;
  }
  ++_configGeneration;
  return RtcOperationResult::Ok;
#endif
}

RtcOperationResult RtcManager::setTimezoneName(const char* zoneName) {
#if !defined(ARDUINO_ARCH_ESP32)
  (void)zoneName;
  return RtcOperationResult::UnsupportedZone;
#else
  uint32_t zoneId = 0;
  if (!RtcTimezoneDatabase::zoneIdForName(zoneName, zoneId)) {
    return RtcOperationResult::UnsupportedZone;
  }
  return setTimezoneZoneId(zoneId);
#endif
}

RtcOperationResult RtcManager::syncLocalDateTime(const RtcDateTime& local) {
#if !defined(ARDUINO_ARCH_ESP32)
  (void)local;
  return RtcOperationResult::TimezoneNotConfigured;
#else
  if (!isTimezoneConfigured() || !isTimezoneResolved()) {
    return RtcOperationResult::TimezoneNotConfigured;
  }
  int64_t unixSeconds = 0;
  bool utcOutOfRange = false;
  if (!RtcTimezoneDatabase::localDateTimeToUnixSeconds(
          local, _zoneId, unixSeconds, &utcOutOfRange)) {
    return utcOutOfRange ? RtcOperationResult::TimeOutOfRange
                         : RtcOperationResult::LocalTimeAmbiguous;
  }
  return syncUtcAndZone(unixSeconds, _zoneId);
#endif
}

bool RtcManager::isTimezoneConfigured() const {
  return _zoneId != 0 && _configState != RtcConfigState::Unconfigured;
}

bool RtcManager::isTimezoneResolved() const {
#if !defined(ARDUINO_ARCH_ESP32)
  return false;
#else
  return isTimezoneConfigured() && RtcTimezoneDatabase::isSupportedZone(_zoneId);
#endif
}

bool RtcManager::isConfigPersisted() const { return _nvsPersisted; }
RtcConfigState RtcManager::getConfigState() const { return _configState; }
uint32_t RtcManager::getZoneId() const { return _zoneId; }
uint16_t RtcManager::getConfigGeneration() const { return _configGeneration; }

bool RtcManager::getZoneName(char* buffer, size_t bufferSize) const {
#if !defined(ARDUINO_ARCH_ESP32)
  if (buffer && bufferSize) buffer[0] = '\0';
  return false;
#else
  return isTimezoneResolved() &&
      RtcTimezoneDatabase::zoneNameForId(_zoneId, buffer, bufferSize);
#endif
}

bool RtcManager::loadPersistedConfig() {
  _configState = RtcConfigState::Unconfigured;
  _zoneId = 0;
  _nvsPersisted = false;
  RtcStoredConfig stored{};
  if (!readPersistedConfig(stored)) return false;
  _configState = stored.state;
  _zoneId = stored.zoneId;
  // A persisted pending transaction is evidence of interrupted work, not a
  // completed configuration. It intentionally keeps UTC invalid.
  _nvsPersisted = stored.state == RtcConfigState::Valid ||
                  stored.state == RtcConfigState::ZoneOnly;
  return _nvsPersisted;
}

bool RtcManager::writeAndConfirmConfig(RtcConfigState state, uint32_t zoneId) {
  RtcStoredConfig requested{};
  requested.state = state;
  requested.zoneId = zoneId;
  if (!writePersistedConfig(requested)) return false;
  RtcStoredConfig confirmed{};
  if (!readPersistedConfig(confirmed) || confirmed.state != state ||
      confirmed.zoneId != zoneId) {
    return false;
  }
  _configState = state;
  _zoneId = zoneId;
  _nvsPersisted = state == RtcConfigState::Valid ||
                  state == RtcConfigState::ZoneOnly;
  return true;
}

void RtcManager::forcePersistedConfigInvalid(uint32_t zoneId) {
  RtcStoredConfig pending{};
  pending.state = RtcConfigState::Pending;
  pending.zoneId = zoneId;
  const bool pendingWritten = writePersistedConfig(pending);
  RtcStoredConfig confirmed{};
  if (pendingWritten && readPersistedConfig(confirmed) &&
      confirmed.state == RtcConfigState::Pending &&
      confirmed.zoneId == zoneId) {
    _configState = RtcConfigState::Pending;
    _zoneId = zoneId;
    _nvsPersisted = false;
    return;
  }

  // If even pending cannot be confirmed, absence is safer than a possibly
  // committed Valid record. The runtime state remains invalid regardless of
  // the storage result, and a verified removal prevents acceptance on reboot.
  (void)removePersistedConfig();
  const bool absent = isPersistedConfigKeyAbsent();
  _configState = absent ? RtcConfigState::Unconfigured
                        : RtcConfigState::Pending;
  _zoneId = absent ? 0 : zoneId;
  _nvsPersisted = false;
  if (!absent) {
    M5.Log.println("[RTC] Critical: failed to confirm pending or remove rtc_cfg");
  }
}

bool RtcManager::readPersistedConfig(RtcStoredConfig& config) const {
  uint8_t blob[ulsa::rtc::RTC_CONFIG_BLOB_SIZE]{};
#if defined(ARDUINO_ARCH_ESP32)
  Preferences preferences;
  if (!preferences.begin(RTC_CONFIG_NAMESPACE, true)) return false;
  const size_t length = preferences.getBytesLength(RTC_CONFIG_KEY);
  const size_t read = length == sizeof(blob)
      ? preferences.getBytes(RTC_CONFIG_KEY, blob, sizeof(blob)) : 0;
  preferences.end();
  if (read != sizeof(blob)) return false;
#else
  if (!_hostConfigPresent) return false;
  memcpy(blob, _hostConfigBlob, sizeof(blob));
#endif
  return ulsa::rtc::decodeRtcConfigBlob(blob, sizeof(blob), config);
}

bool RtcManager::writePersistedConfig(const RtcStoredConfig& config) {
  uint8_t blob[ulsa::rtc::RTC_CONFIG_BLOB_SIZE]{};
  ulsa::rtc::encodeRtcConfigBlob(config, blob);
#if defined(ARDUINO_ARCH_ESP32)
  Preferences preferences;
  if (!preferences.begin(RTC_CONFIG_NAMESPACE, false)) return false;
  const size_t written = preferences.putBytes(RTC_CONFIG_KEY, blob, sizeof(blob));
  preferences.end();
  return written == sizeof(blob);
#else
  memcpy(_hostConfigBlob, blob, sizeof(blob));
  _hostConfigPresent = true;
  return true;
#endif
}

bool RtcManager::removePersistedConfig() {
#if defined(ARDUINO_ARCH_ESP32)
  Preferences preferences;
  if (!preferences.begin(RTC_CONFIG_NAMESPACE, false)) return false;
  const bool removed = preferences.remove(RTC_CONFIG_KEY);
  preferences.end();
  return removed;
#else
  _hostConfigPresent = false;
  memset(_hostConfigBlob, 0, sizeof(_hostConfigBlob));
  return true;
#endif
}

bool RtcManager::isPersistedConfigKeyAbsent() const {
#if defined(ARDUINO_ARCH_ESP32)
  Preferences preferences;
  if (!preferences.begin(RTC_CONFIG_NAMESPACE, true)) return false;
  const bool absent = !preferences.isKey(RTC_CONFIG_KEY);
  preferences.end();
  return absent;
#else
  return !_hostConfigPresent;
#endif
}

// ============================================
// 日時フォーマット
// ============================================
char* RtcManager::formatDateForFilename(const RtcDateTime& dt, char* buffer) {
  // "20251203(Wed)"
  snprintf(buffer, RTC_FILENAME_DATE_BUFFER_SIZE, "%04d%02d%02d(%s)",
           dt.year, dt.month, dt.day, dt.weekdayName());
  return buffer;
}

char* RtcManager::formatDateTimeForFilename(const RtcDateTime& dt, char* buffer) {
  // RTCログの開始時刻を、曜日を重複させずに並び替え可能な形式へ出力する。
  snprintf(buffer, RTC_FILENAME_DATETIME_BUFFER_SIZE, "%04u%02u%02u_%02u%02u%02u",
           static_cast<unsigned>(dt.year),
           static_cast<unsigned>(dt.month),
           static_cast<unsigned>(dt.day),
           static_cast<unsigned>(dt.hour),
           static_cast<unsigned>(dt.minute),
           static_cast<unsigned>(dt.second));
  return buffer;
}

char* RtcManager::formatDateTimeForLog(const RtcDateTime& dt, char* buffer) {
  // "2025-12-03 14:30:25"
  snprintf(buffer, RTC_LOG_DATETIME_BUFFER_SIZE, "%04d-%02d-%02d %02d:%02d:%02d",
           dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
  return buffer;
}

// ============================================
// BCD変換
// ============================================
uint8_t RtcManager::bcdToDec(uint8_t bcd) {
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

uint8_t RtcManager::decToBcd(uint8_t dec) {
  return ((dec / 10) << 4) | (dec % 10);
}

bool RtcManager::readSnapshot(RtcStatus& status, RtcDateTime* dt) {
  status = {};
  status.detected = _available;
  status.zoneConfigured = isTimezoneConfigured();
  status.zoneResolved = isTimezoneResolved();
  status.nvsPersisted = _nvsPersisted;
  status.configPending = _configState == RtcConfigState::Pending;
  if (!_available || !_wire) {
    return false;
  }

  // Control_status_1〜Yearsを同じトランザクションで読む。
  // bytes[0]=Control1, [1]=Control2, [2]=Seconds, ... [8]=Years。
  uint8_t bytes[PCF8563_REG_YEARS - PCF8563_REG_CONTROL1 + 1]{};
  if (!readRegisters(PCF8563_REG_CONTROL1, bytes, sizeof(bytes))) {
    return false;
  }

  status.busReadable = true;
  status.clockStopped = (bytes[0] & PCF8563_CONTROL1_STOP) != 0;
  status.voltageLow = (bytes[2] & 0x80) != 0;

  const uint8_t seconds = bytes[2] & 0x7F;
  const uint8_t minutes = bytes[3] & 0x7F;
  const uint8_t hours = bytes[4] & 0x3F;
  const uint8_t days = bytes[5] & 0x3F;
  const uint8_t months = bytes[7] & 0x1F;
  const uint8_t years = bytes[8];
  const bool bcdValid = ulsa::rtc::isValidBcd(seconds) && ulsa::rtc::isValidBcd(minutes) &&
                        ulsa::rtc::isValidBcd(hours) && ulsa::rtc::isValidBcd(days) &&
                        ulsa::rtc::isValidBcd(months) && ulsa::rtc::isValidBcd(years);
  // Weekdaysのbit7-3は未使用で、読み値が常に0とは限らない。
  // 下位3bitだけがPCF8563の曜日値なので、予約ビットによって正常なRTCを
  // 無効扱いしない。
  const uint8_t weekday = bytes[6] & 0x07;
  const bool weekdayValid = weekday <= 6;

  RtcDateTime decoded{};
  decoded.second  = bcdToDec(seconds);
  decoded.minute  = bcdToDec(minutes);
  decoded.hour    = bcdToDec(hours);
  decoded.day     = bcdToDec(days);
  decoded.weekday = weekday;
  decoded.month   = bcdToDec(months);
  decoded.year    = 2000 + bcdToDec(years);

  const bool calendarValid = bcdValid && decoded.isValid();
  const bool weekdayMatchesCalendar = calendarValid &&
      ulsa::rtc::doesWeekdayMatchDate(decoded.year, decoded.month, decoded.day, decoded.weekday);
  status.hardwareTimeValid = !status.clockStopped && !status.voltageLow &&
      calendarValid && weekdayValid && weekdayMatchesCalendar;
  status.utcValid = status.hardwareTimeValid &&
      _configState == RtcConfigState::Valid;
  status.timeValid = status.utcValid;
  if (dt) {
    *dt = decoded;
  }
  return true;
}

bool RtcManager::readRawUtcDateTime(RtcDateTime& utc, RtcStatus* status) {
  RtcStatus localStatus{};
  if (!readSnapshot(localStatus, &utc)) {
    if (status) *status = localStatus;
    return false;
  }
  if (status) *status = localStatus;
  return localStatus.hardwareTimeValid;
}

bool RtcManager::isReadbackConfirmed(const RtcDateTime& expected, const RtcDateTime& actual) {
  const uint32_t expectedSeconds = secondsSince2000(expected);
  const uint32_t actualSeconds = secondsSince2000(actual);
  return actualSeconds >= expectedSeconds && actualSeconds <= expectedSeconds + 1;
}

uint32_t RtcManager::secondsSince2000(const RtcDateTime& dt) {
  uint32_t days = 0;
  for (uint16_t year = 2000; year < dt.year; ++year) {
    const bool leapYear = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    days += leapYear ? 366 : 365;
  }

  static const uint8_t daysPerMonth[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
  };
  for (uint8_t month = 1; month < dt.month; ++month) {
    days += daysPerMonth[month - 1];
    if (month == 2 && dt.year % 4 == 0 && (dt.year % 100 != 0 || dt.year % 400 == 0)) {
      ++days;
    }
  }
  days += dt.day - 1;
  return days * 86400UL + dt.hour * 3600UL + dt.minute * 60UL + dt.second;
}

// ============================================
// I2Cレジスタアクセス
// ============================================
bool RtcManager::readRegister(uint8_t reg, uint8_t& value) {
  if (!_wire) return false;
  _wire->beginTransmission(PCF8563_I2C_ADDRESS);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) {  // Repeated Start
    return false;
  }
  
  const size_t received = _wire->requestFrom((uint8_t)PCF8563_I2C_ADDRESS, (uint8_t)1);
  if (received != 1 || !_wire->available()) {
    return false;
  }
  value = _wire->read();
  return true;
}

bool RtcManager::writeRegister(uint8_t reg, uint8_t value) {
  if (!_wire) return false;
  _wire->beginTransmission(PCF8563_I2C_ADDRESS);
  _wire->write(reg);
  _wire->write(value);
  return _wire->endTransmission() == 0;
}

bool RtcManager::readRegisters(uint8_t startReg, uint8_t* buffer, uint8_t count) {
  if (!_wire || !buffer || count == 0) return false;
  _wire->beginTransmission(PCF8563_I2C_ADDRESS);
  _wire->write(startReg);
  if (_wire->endTransmission(false) != 0) {
    return false;
  }
  
  const size_t received = _wire->requestFrom((uint8_t)PCF8563_I2C_ADDRESS, count);
  if (received != count) {
    while (_wire->available()) {
      (void)_wire->read();
    }
    return false;
  }
  for (uint8_t i = 0; i < count; i++) {
    if (!_wire->available()) {
      return false;
    }
    buffer[i] = _wire->read();
  }
  
  return true;
}

bool RtcManager::writeRegisters(uint8_t startReg, const uint8_t* buffer, uint8_t count) {
  if (!_wire || !buffer || count == 0) return false;
  _wire->beginTransmission(PCF8563_I2C_ADDRESS);
  _wire->write(startReg);
  for (uint8_t i = 0; i < count; i++) {
    _wire->write(buffer[i]);
  }
  return _wire->endTransmission() == 0;
}

// ============================================
// 曜日計算（ツェラーの公式）
// ============================================
uint8_t RtcManager::calculateWeekday(uint16_t year, uint8_t month, uint8_t day) {
  return ulsa::rtc::calculateWeekday(year, month, day);
}
