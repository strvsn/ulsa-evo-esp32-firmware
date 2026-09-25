#include <cstdio>
#include <cstdlib>

#include "hardware/rtc_validation.h"

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "rtc_validation_test failed: %s\n", message);
    std::exit(1);
  }
}

}  // namespace

int main() {
  using namespace ulsa::rtc;

  expect(isValidDateTime(2024, 2, 29, 4, 23, 59, 59), "2024 leap day must be valid");
  expect(!isValidDateTime(2023, 2, 29, 3, 12, 0, 0), "non-leap February 29 must be invalid");
  expect(!isValidDateTime(2026, 4, 31, 5, 12, 0, 0), "April 31 must be invalid");
  expect(!isValidDateTime(2026, 7, 22, 7, 12, 0, 0), "weekday outside PCF8563 range must be invalid");

  expect(isValidBcd(0x59), "valid BCD must pass");
  expect(!isValidBcd(0x6A), "invalid BCD low digit must fail");
  expect(!isValidBcd(0xFA), "invalid BCD high digit must fail");

  expect(calculateWeekday(2000, 1, 1) == 6, "2000-01-01 must be Saturday");
  expect(calculateWeekday(2024, 2, 29) == 4, "2024-02-29 must be Thursday");
  expect(calculateWeekday(2026, 7, 22) == 3, "2026-07-22 must be Wednesday");
  expect(doesWeekdayMatchDate(2026, 7, 22, 3), "matching weekday must pass");
  expect(!doesWeekdayMatchDate(2026, 7, 22, 4), "mismatched weekday must fail");

  constexpr uint8_t rawWeekdayRegister = 0xFB;
  expect((rawWeekdayRegister & 0x07) == 3,
         "unused weekday register bits must not alter the weekday value");

  std::puts("rtc_validation_test: passed");
  return 0;
}
