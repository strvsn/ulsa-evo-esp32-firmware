/**
 * @file sd_logging_led_effect.h
 * @brief SD logging heartbeat and stop-confirmation timing state machine.
 */

#ifndef SD_LOGGING_LED_EFFECT_H
#define SD_LOGGING_LED_EFFECT_H

#include <stdint.h>

enum SdLoggingLedEffectPhase : uint8_t {
  SD_LOG_LED_INACTIVE = 0,
  SD_LOG_LED_HEARTBEAT,
  SD_LOG_LED_STOP_CONFIRMATION,
};

struct SdLoggingLedFrame {
  bool active;
  bool ledOn;
  uint8_t red;
  bool completed;
};

class SdLoggingLedEffect {
public:
  enum : uint32_t {
    HEARTBEAT_PERIOD_MS = 1800U,
    STOP_FLASH_SLOT_MS = 100U,
    STOP_FLASH_SLOT_COUNT = 6U,
  };

  SdLoggingLedEffect()
    : _phase(SD_LOG_LED_INACTIVE)
    , _phaseStartedMs(0) {
  }

  /**
   * @return true when the visible effect phase changed.
   */
  bool setLoggingState(bool loggingActive,
                       bool userStopConfirmed,
                       uint32_t nowMs) {
    if (loggingActive) {
      if (_phase == SD_LOG_LED_HEARTBEAT) {
        return false;
      }
      _phase = SD_LOG_LED_HEARTBEAT;
      _phaseStartedMs = nowMs;
      return true;
    }

    if (_phase != SD_LOG_LED_HEARTBEAT) {
      return false;
    }

    _phase = userStopConfirmed
      ? SD_LOG_LED_STOP_CONFIRMATION
      : SD_LOG_LED_INACTIVE;
    _phaseStartedMs = nowMs;
    return true;
  }

  SdLoggingLedFrame frame(uint32_t nowMs) {
    if (_phase == SD_LOG_LED_HEARTBEAT) {
      return {
        true,
        true,
        heartbeatRed(nowMs - _phaseStartedMs),
        false,
      };
    }

    if (_phase == SD_LOG_LED_STOP_CONFIRMATION) {
      const uint32_t elapsedMs = nowMs - _phaseStartedMs;
      if (elapsedMs >= STOP_FLASH_SLOT_MS * STOP_FLASH_SLOT_COUNT) {
        _phase = SD_LOG_LED_INACTIVE;
        return {false, false, 0, true};
      }

      const bool ledOn = ((elapsedMs / STOP_FLASH_SLOT_MS) & 1U) == 0U;
      return {true, ledOn, static_cast<uint8_t>(ledOn ? 255U : 0U), false};
    }

    return {false, false, 0, false};
  }

  bool isActive() const {
    return _phase != SD_LOG_LED_INACTIVE;
  }

  SdLoggingLedEffectPhase getPhase() const {
    return _phase;
  }

private:
  SdLoggingLedEffectPhase _phase;
  uint32_t _phaseStartedMs;

  static uint8_t heartbeatRed(uint32_t elapsedMs) {
    const uint32_t phaseMs = elapsedMs % HEARTBEAT_PERIOD_MS;
    const uint32_t halfPeriodMs = HEARTBEAT_PERIOD_MS / 2U;
    const uint32_t triangle = phaseMs <= halfPeriodMs
      ? (phaseMs * 255U) / halfPeriodMs
      : ((HEARTBEAT_PERIOD_MS - phaseMs) * 255U) / halfPeriodMs;

    // Integer smoothstep keeps the fade gentle without pulling floating-point
    // math into the time-critical LED service path.
    const uint32_t smooth =
      (triangle * triangle * (765U - (2U * triangle))) / 65025U;
    return static_cast<uint8_t>(12U + ((243U * smooth) / 255U));
  }
};

#endif  // SD_LOGGING_LED_EFFECT_H
