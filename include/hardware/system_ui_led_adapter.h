/**
 * @file system_ui_led_adapter.h
 * @brief Maps pure UI presentations onto the NeoPixel controller modes.
 */

#ifndef HARDWARE_SYSTEM_UI_LED_ADAPTER_H
#define HARDWARE_SYSTEM_UI_LED_ADAPTER_H

#include "hardware/led_controller.h"
#include "system/system_ui_state.h"

inline LedMode ledModeForPresentation(LedPresentation presentation) {
  switch (presentation) {
    case LedPresentation::InitialReady: return MODE_INITIAL_READY;
    case LedPresentation::BaseI2cConnected: return MODE_I2C_MEASURE_CONNECTED;
    case LedPresentation::BaseCommand: return MODE_COMMAND;
    case LedPresentation::BaseUartBridge: return MODE_BRIDGE;
    case LedPresentation::BaseUartDisconnected: return MODE_BLE_DISCONNECTED;
    case LedPresentation::BaseUartConnected: return MODE_BLE_CONNECTED_1M;
    case LedPresentation::ManualBootloader: return MODE_BOOTLOADER;
    // Update presentation is intentionally target-neutral. The same status
    // must render the same color/pattern for ESP32 and STM32 sessions.
    case LedPresentation::UpdateAuthorizationPending: return MODE_ESP32_AUTH_PENDING;
    case LedPresentation::UpdateAuthorizationGranted: return MODE_ESP32_AUTH_GRANTED;
    case LedPresentation::UpdatePortal: return MODE_ESP32_PORTAL_ACTIVE;
    case LedPresentation::UpdateTransfer: return MODE_OTA_UPDATING;
    case LedPresentation::UpdateReady: return MODE_ESP32_AUTH_GRANTED;
    case LedPresentation::UpdateWriting: return MODE_STM32_UPDATE_WRITING;
    case LedPresentation::UpdateError: return MODE_ESP32_UPDATE_ERROR;
    case LedPresentation::UpdateRecovery: return MODE_ESP32_RECOVERY;
    case LedPresentation::BaseI2cDisconnected:
    default:
      return MODE_I2C_MEASURE;
  }
}

inline void applyLedPresentation(LedController& controller,
                                 const SystemUiState& state) {
  controller.setMode(ledModeForPresentation(resolveLedPresentation(state)));
}

#endif  // HARDWARE_SYSTEM_UI_LED_ADAPTER_H
