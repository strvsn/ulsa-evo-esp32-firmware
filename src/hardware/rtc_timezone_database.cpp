/**
 * @file rtc_timezone_database.cpp
 * @brief Full AceTime zonedbx Zone+Link registry adapter.
 */
#include "hardware/rtc_timezone_database.h"

#include <AceTime.h>
#include <ace_time/ZonedExtra.h>
#include <zonedbx/zone_infos.h>
#include <zonedbx/zone_registry.h>

#include "hardware/rtc_manager.h"

namespace {

using ace_time::Disambiguate;
using ace_time::ExtendedZoneManager;
using ace_time::ExtendedZoneProcessorCache;
using ace_time::Resolved;
using ace_time::TimeZone;
using ace_time::ZonedDateTime;
using ace_time::ZonedExtra;

ExtendedZoneProcessorCache<2> gZoneProcessorCache;
ExtendedZoneManager gZoneManager(
    ace_time::zonedbx::kZoneAndLinkRegistrySize,
    ace_time::zonedbx::kZoneAndLinkRegistry,
    gZoneProcessorCache);

class BufferPrint final : public Print {
public:
  BufferPrint(char* buffer, size_t size)
      : _buffer(buffer), _size(size), _length(0), _ok(buffer && size > 0) {
    if (_ok) _buffer[0] = '\0';
  }

  size_t write(uint8_t value) override {
    if (!_ok || _length + 1 >= _size) {
      _ok = false;
      return 0;
    }
    _buffer[_length++] = static_cast<char>(value);
    _buffer[_length] = '\0';
    return 1;
  }

  bool ok() const { return _ok; }

private:
  char* _buffer;
  size_t _size;
  size_t _length;
  bool _ok;
};

bool unixSecondsInPcfRange(int64_t unixSeconds) {
  const auto utc = ace_time::PlainDateTime::forUnixSeconds64(unixSeconds);
  return !utc.isError() && utc.year() >= 2000 && utc.year() <= 2099;
}

RtcDateTime fromAceComponents(int16_t year, uint8_t month, uint8_t day,
                              uint8_t hour, uint8_t minute, uint8_t second,
                              uint8_t isoDayOfWeek) {
  RtcDateTime result{};
  result.year = static_cast<uint16_t>(year);
  result.month = month;
  result.day = day;
  result.hour = hour;
  result.minute = minute;
  result.second = second;
  result.weekday = static_cast<uint8_t>(isoDayOfWeek % 7U);
  return result;
}

}  // namespace

bool RtcTimezoneDatabase::zoneIdForName(const char* name, uint32_t& zoneId) {
  if (!name || !name[0]) return false;
  const uint16_t index = gZoneManager.indexForZoneName(name);
  if (index == ace_time::ZoneManager::kInvalidIndex) return false;
  zoneId = gZoneManager.getZoneForIndex(index).zoneId();
  return zoneId != 0;
}

bool RtcTimezoneDatabase::zoneNameForId(uint32_t zoneId, char* buffer,
                                        size_t bufferSize) {
  if (!buffer || bufferSize == 0) return false;
  buffer[0] = '\0';
  const uint16_t index = gZoneManager.indexForZoneId(zoneId);
  if (index == ace_time::ZoneManager::kInvalidIndex) return false;
  BufferPrint output(buffer, bufferSize);
  gZoneManager.getZoneForIndex(index).printNameTo(output);
  return output.ok() && buffer[0] != '\0';
}

bool RtcTimezoneDatabase::isSupportedZone(uint32_t zoneId) {
  return zoneId != 0 &&
      gZoneManager.indexForZoneId(zoneId) != ace_time::ZoneManager::kInvalidIndex;
}

bool RtcTimezoneDatabase::utcDateTimeToUnixSeconds(const RtcDateTime& utc,
                                                   int64_t& unixSeconds) {
  if (!utc.isValid() || utc.year < 2000 || utc.year > 2099) return false;
  const auto dateTime = ace_time::PlainDateTime::forComponents(
      utc.year, utc.month, utc.day, utc.hour, utc.minute, utc.second);
  if (dateTime.isError()) return false;
  unixSeconds = dateTime.toUnixSeconds64();
  return unixSecondsInPcfRange(unixSeconds);
}

bool RtcTimezoneDatabase::unixSecondsToUtcDateTime(int64_t unixSeconds,
                                                   RtcDateTime& utc) {
  if (!unixSecondsInPcfRange(unixSeconds)) return false;
  const auto dateTime = ace_time::PlainDateTime::forUnixSeconds64(unixSeconds);
  if (dateTime.isError()) return false;
  utc = fromAceComponents(dateTime.year(), dateTime.month(), dateTime.day(),
                          dateTime.hour(), dateTime.minute(), dateTime.second(),
                          dateTime.dayOfWeek());
  return utc.isValid();
}

bool RtcTimezoneDatabase::unixSecondsToLocal(
    int64_t unixSeconds, uint32_t zoneId, RtcDateTime& local,
    int16_t& standardOffsetMinutes, int16_t& dstOffsetMinutes,
    int16_t& totalOffsetMinutes) {
  if (!unixSecondsInPcfRange(unixSeconds)) return false;
  TimeZone zone = gZoneManager.createForZoneId(zoneId);
  if (zone.isError()) return false;
  const ZonedDateTime dateTime =
      ZonedDateTime::forUnixSeconds64(unixSeconds, zone);
  if (dateTime.isError()) return false;
  const ZonedExtra extra = ZonedExtra::forEpochSeconds(
      dateTime.toEpochSeconds(), zone);
  if (extra.isError()) return false;
  const int32_t standardSeconds = extra.stdOffset().toSeconds();
  const int32_t dstSeconds = extra.dstOffset().toSeconds();
  const int32_t totalSeconds = dateTime.timeOffset().toSeconds();
  if ((standardSeconds % 60) != 0 || (dstSeconds % 60) != 0 ||
      (totalSeconds % 60) != 0) {
    return false;
  }
  standardOffsetMinutes = static_cast<int16_t>(standardSeconds / 60);
  dstOffsetMinutes = static_cast<int16_t>(dstSeconds / 60);
  totalOffsetMinutes = static_cast<int16_t>(totalSeconds / 60);
  local = fromAceComponents(dateTime.year(), dateTime.month(), dateTime.day(),
                            dateTime.hour(), dateTime.minute(), dateTime.second(),
                            dateTime.dayOfWeek());
  return local.isValid();
}

bool RtcTimezoneDatabase::localDateTimeToUnixSeconds(
    const RtcDateTime& local, uint32_t zoneId, int64_t& unixSeconds,
    bool* utcOutOfRange) {
  if (utcOutOfRange) *utcOutOfRange = false;
  if (!local.isValid() || local.year < 2000 || local.year > 2099) return false;
  TimeZone zone = gZoneManager.createForZoneId(zoneId);
  if (zone.isError()) return false;
  const ZonedExtra extra = ZonedExtra::forComponents(
      local.year, local.month, local.day, local.hour, local.minute, local.second,
      zone, Disambiguate::kCompatible);
  if (extra.isError() || extra.resolved() != Resolved::kUnique) return false;
  const ZonedDateTime dateTime = ZonedDateTime::forComponents(
      local.year, local.month, local.day, local.hour, local.minute, local.second,
      zone, Disambiguate::kCompatible);
  if (dateTime.isError() || dateTime.resolved() != Resolved::kUnique) return false;
  unixSeconds = dateTime.toUnixSeconds64();
  if (!unixSecondsInPcfRange(unixSeconds)) {
    if (utcOutOfRange) *utcOutOfRange = true;
    return false;
  }
  return true;
}
