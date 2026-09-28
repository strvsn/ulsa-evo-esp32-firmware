#pragma once
#include "Arduino.h"

static int64_t mockDaysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear =
    (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra =
    yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

struct RtcDateTime {
  uint16_t year = 2026;
  uint8_t month = 9, day = 5, hour = 12, minute = 0, second = 0, weekday = 6;
};

static int64_t mockUnixSeconds(const RtcDateTime& value) {
  return mockDaysFromCivil(value.year, value.month, value.day) * 86400LL +
    static_cast<int64_t>(value.hour) * 3600LL +
    static_cast<int64_t>(value.minute) * 60LL + value.second;
}

struct RtcStatus {
  bool detected = true, busReadable = true, voltageLow = false;
  bool clockStopped = false, hardwareTimeValid = true, timeValid = true;
  bool utcValid = true, zoneConfigured = true, zoneResolved = true;
  bool nvsPersisted = true, configPending = false;
};
struct RtcTimezoneSnapshot {
  RtcStatus status{};
  RtcDateTime utc{};
  RtcDateTime local{};
  int64_t unixSeconds = 0;
  uint32_t zoneId = 1;
  int16_t totalOffsetMinutes = 540;
  int16_t standardOffsetMinutes = 540;
  int16_t dstOffsetMinutes = 0;
  uint16_t configGeneration = 1;
  bool localValid = true;
  bool utcFallback = false;
};
class RtcManager {
public:
  bool valid = true;
  RtcDateTime date;
  bool getTimezoneSnapshot(RtcTimezoneSnapshot& value) {
    value.local = date;
    value.utc = date;
    // Keep the host snapshot's absolute time consistent with the mutable
    // calendar fields used by rollback tests. The production manager derives
    // this value from the RTC's UTC registers and timezone database.
    value.unixSeconds = mockUnixSeconds(date);
    value.status.utcValid = valid;
    value.status.timeValid = valid;
    value.localValid = valid;
    return true;
  }
  bool isAvailable() { return valid; }
  static void formatDateForFilename(const RtcDateTime& d, char* out) {
    snprintf(out, 32, "%04u%02u%02u", d.year, d.month, d.day);
  }
  static void formatDateTimeForFilename(const RtcDateTime& d, char* out) {
    snprintf(out, 16, "%04u%02u%02u_%02u%02u%02u",
             static_cast<unsigned>(d.year), static_cast<unsigned>(d.month),
             static_cast<unsigned>(d.day), static_cast<unsigned>(d.hour),
             static_cast<unsigned>(d.minute), static_cast<unsigned>(d.second));
  }
  static void formatDateTimeForLog(const RtcDateTime&, char* out) { out[0] = 0; }
};
