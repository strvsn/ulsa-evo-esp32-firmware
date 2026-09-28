/**
 * @file sd_log_write_policy.h
 * @brief SDログの同期頻度と遅延診断の共通方針
 */

#ifndef SD_LOG_WRITE_POLICY_H
#define SD_LOG_WRITE_POLICY_H

#include <stdint.h>

class SdLogWritePolicy {
public:
  // File::flush() reaches fsync() in the Arduino ESP32 filesystem layer.
  // Syncing every 512-byte buffer can make ordinary SD housekeeping look
  // like a fault, so batch durable commits while preserving a short loss
  // window for an unexpected power removal.
  static constexpr uint32_t kSyncIntervalMs = 1000U;

  // This is diagnostic only. A card can legitimately pause for a while when
  // allocating or managing flash internally; an actual write failure remains
  // the only terminal SD write condition.
  static constexpr uint16_t kSlowWriteAdvisoryMs = 250U;

  static bool shouldSync(bool syncPending,
                         uint32_t lastSyncMillis,
                         uint32_t nowMillis,
                         bool force) {
    return syncPending &&
           (force || static_cast<uint32_t>(nowMillis - lastSyncMillis) >= kSyncIntervalMs);
  }

  static bool isSlowWriteAdvisory(uint32_t durationMs) {
    return durationMs >= kSlowWriteAdvisoryMs;
  }
};

#endif  // SD_LOG_WRITE_POLICY_H
