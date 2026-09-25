/**
 * @file rtc_validation.h
 * @brief PCF8563 RTC日時の副作用を持たない検証ユーティリティ
 */

#ifndef RTC_VALIDATION_H
#define RTC_VALIDATION_H

#include <stdint.h>

namespace ulsa {
namespace rtc {

inline bool isLeapYear(uint16_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

inline uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t kDaysPerMonth[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
  };
  if (month < 1 || month > 12) {
    return 0;
  }
  return (month == 2 && isLeapYear(year)) ? 29 : kDaysPerMonth[month - 1];
}

inline bool isValidDateTime(uint16_t year, uint8_t month, uint8_t day,
                            uint8_t weekday, uint8_t hour,
                            uint8_t minute, uint8_t second) {
  return year >= 2000 && year <= 2099 &&
         day >= 1 && day <= daysInMonth(year, month) &&
         weekday <= 6 && hour <= 23 && minute <= 59 && second <= 59;
}

inline bool isValidBcd(uint8_t value) {
  return (value & 0x0F) <= 9 && ((value >> 4) & 0x0F) <= 9;
}

inline uint8_t calculateWeekday(uint16_t year, uint8_t month, uint8_t day) {
  const uint16_t adjustedYear = month <= 2 ? year - 1 : year;
  const uint8_t adjustedMonth = month <= 2 ? month + 12 : month;
  const int q = day;
  const int m = adjustedMonth;
  const int k = adjustedYear % 100;
  const int j = adjustedYear / 100;
  const int h = (q + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
  return static_cast<uint8_t>((h + 6) % 7);  // 0=Sun, ... 6=Sat
}

inline bool doesWeekdayMatchDate(uint16_t year, uint8_t month, uint8_t day,
                                 uint8_t weekday) {
  return isValidDateTime(year, month, day, weekday, 0, 0, 0) &&
         weekday == calculateWeekday(year, month, day);
}

}  // namespace rtc
}  // namespace ulsa

#endif  // RTC_VALIDATION_H
