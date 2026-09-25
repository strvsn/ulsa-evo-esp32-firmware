#ifndef ULSA_SD_LOG_LIFETIME_POLICY_H
#define ULSA_SD_LOG_LIFETIME_POLICY_H
#include <stdint.h>

struct SdLogLifetimePolicy {
  static uint64_t calendarStamp(unsigned year, unsigned month, unsigned day,
                                unsigned hour, unsigned minute, unsigned second) {
    return (((((static_cast<uint64_t>(year) * 100 + month) * 100 + day) * 100 + hour)
              * 100 + minute) * 100 + second);
  }
  static constexpr uint64_t kFileDurationMs = 30ULL * 60ULL * 1000ULL;
  static constexpr uint64_t kMaxFileBytes = 16ULL * 1024ULL * 1024ULL;
  static bool rotate(uint64_t now, uint64_t openedAt, uint64_t fileBytes,
                     uint32_t nextBytes) {
    return now - openedAt >= kFileDurationMs ||
           fileBytes >= kMaxFileBytes || nextBytes > kMaxFileBytes - fileBytes;
  }
  // Match a recent 32-bit capture time to the current 64-bit boot timeline.
  static uint64_t extendCapture(uint32_t capture, uint64_t now) {
    const int64_t candidate = static_cast<int64_t>(now) +
      static_cast<int32_t>(capture - static_cast<uint32_t>(now));
    return candidate < 0 ? 0 : static_cast<uint64_t>(candidate);
  }
};
#endif
