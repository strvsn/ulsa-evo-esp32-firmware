/**
 * @file sd_log_interval_policy.h
 * @brief Exact source-sample interval policy for SD logging.
 */

#ifndef SD_LOG_INTERVAL_POLICY_H
#define SD_LOG_INTERVAL_POLICY_H

#include <stdint.h>

class SdLogIntervalPolicy {
public:
  static bool isExactSourceMultiple(uint32_t logIntervalMs, uint32_t sourceIntervalMs) {
    return sourceIntervalMs != 0U &&
           logIntervalMs >= sourceIntervalMs &&
           (logIntervalMs % sourceIntervalMs) == 0U;
  }

  // Returns zero when no valid aligned interval can fit within maxIntervalMs.
  static uint32_t alignAtOrAbove(uint32_t requestedIntervalMs,
                                 uint32_t sourceIntervalMs,
                                 uint32_t maxIntervalMs) {
    if (sourceIntervalMs == 0U || requestedIntervalMs == 0U) {
      return 0U;
    }

    const uint32_t minimum = requestedIntervalMs < sourceIntervalMs
                                 ? sourceIntervalMs
                                 : requestedIntervalMs;
    const uint64_t multiples =
        ((uint64_t)minimum + sourceIntervalMs - 1U) / sourceIntervalMs;
    const uint64_t aligned = multiples * sourceIntervalMs;
    return aligned <= maxIntervalMs ? (uint32_t)aligned : 0U;
  }
};

#endif  // SD_LOG_INTERVAL_POLICY_H
