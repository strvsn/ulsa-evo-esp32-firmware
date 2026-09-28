/**
 * @file rtc_timezone_database.h
 * @brief AceTime zonedbx adapter used by both Demo and Initial profiles.
 */
#ifndef RTC_TIMEZONE_DATABASE_H
#define RTC_TIMEZONE_DATABASE_H

#include <stddef.h>
#include <stdint.h>

struct RtcDateTime;

class RtcTimezoneDatabase {
public:
  static constexpr uint16_t TZDB_YEAR = 2025;
  static constexpr char TZDB_REVISION = 'b';
  static constexpr const char* TZDB_VERSION = "2025b";

  static bool zoneIdForName(const char* name, uint32_t& zoneId);
  static bool zoneNameForId(uint32_t zoneId, char* buffer, size_t bufferSize);
  static bool isSupportedZone(uint32_t zoneId);

  static bool utcDateTimeToUnixSeconds(const RtcDateTime& utc,
                                       int64_t& unixSeconds);
  static bool unixSecondsToUtcDateTime(int64_t unixSeconds,
                                       RtcDateTime& utc);
  static bool unixSecondsToLocal(int64_t unixSeconds, uint32_t zoneId,
                                 RtcDateTime& local,
                                 int16_t& standardOffsetMinutes,
                                 int16_t& dstOffsetMinutes,
                                 int16_t& totalOffsetMinutes);

  /**
   * Convert a unique local civil time to Unix seconds. Gaps and folds are
   * rejected instead of applying AceTime's compatibility disambiguation.
   */
  static bool localDateTimeToUnixSeconds(const RtcDateTime& local,
                                         uint32_t zoneId,
                                         int64_t& unixSeconds,
                                         bool* utcOutOfRange = nullptr);
};

#endif  // RTC_TIMEZONE_DATABASE_H
