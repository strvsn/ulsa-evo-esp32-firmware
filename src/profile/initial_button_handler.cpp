/**
 * @file initial_button_handler.cpp
 * @brief Initial profile button gestures and maintenance-mode transitions.
 */

#include "profile/initial_button_handler.h"

#include <M5Unified.h>

#include "hardware/led_controller.h"
#include "hardware/button_gesture_classifier.h"
#include "hardware/stm32_bootloader.h"
#include "hardware/uart_bridge.h"
#include "ota/ota_manager.h"
#include "sensor/wind_sensor.h"
#include "system/update_coordinator.h"
#include "system/system_ui_presentation.h"

extern LedController ledCtrl;
extern STM32Bootloader stm32;
extern OtaManager otaManager;
extern UartBridge uartBridge;
extern WindSensor windSensor;
extern ulsa_update::UpdateCoordinator updateCoordinator;

static const uint32_t SHORT_RELEASE_MAX_MS = 1000;
static const uint32_t I2C_RETURN_PRESS_MS = 2000;
static const uint32_t OTA_AUTHORIZATION_PRESS_MS = 3000;
static const uint32_t LONG_HOLD_BOUNDARY_MS = 6000;
static const uint32_t MULTI_CLICK_WINDOW_MS = 350;

static ButtonMode currentMode = BTN_MODE_I2C_MEASURE;
static const ButtonGestureTiming kButtonGestureTiming = {
  MULTI_CLICK_WINDOW_MS,
  SHORT_RELEASE_MAX_MS,
  I2C_RETURN_PRESS_MS,
  OTA_AUTHORIZATION_PRESS_MS,
  LONG_HOLD_BOUNDARY_MS,
  LONG_HOLD_BOUNDARY_MS,
};
static ButtonGestureClassifier buttonGesture(kButtonGestureTiming);
static uint32_t buttonPressAuthorizationGeneration = 0;
static ButtonMode buttonPressMode = BTN_MODE_I2C_MEASURE;

enum class ButtonPressContext : uint8_t { Normal = 0, Authorization, Blocked };
static ButtonPressContext buttonPressContext = ButtonPressContext::Normal;

static LedMode modeLed(ButtonMode mode) {
  switch (mode) {
    case BTN_MODE_COMMAND:
      return MODE_COMMAND;
    case BTN_MODE_BRIDGE:
      return MODE_BRIDGE;
    case BTN_MODE_I2C_MEASURE:
    case BTN_MODE_NORMAL:
    default:
      return MODE_I2C_MEASURE;
  }
}

ButtonMode getButtonMode() {
  return currentMode;
}

void setButtonMode(ButtonMode mode) {
  currentMode = mode;
}

static void dispatchButtonGesture(ButtonGestureAction action,
                                  ButtonPressContext context,
                                  ButtonMode pressedMode) {
  if (context == ButtonPressContext::Blocked) return;
  if (context == ButtonPressContext::Normal &&
      updateCoordinator.physicalAuthorizationRequired()) return;
  if (context == ButtonPressContext::Authorization) {
    if (action == BUTTON_GESTURE_LONG_PRIMARY ||
        action == BUTTON_GESTURE_LONG_SECONDARY) {
      (void)updateCoordinator.queueButtonAuthorization(
        buttonPressAuthorizationGeneration);
    }
    return;
  }

  switch (action) {
    case BUTTON_GESTURE_SINGLE:
      // InitialにはSDロガーがない。1回クリックは共通操作の空き枠。
      break;

    case BUTTON_GESTURE_DOUBLE:
      if (!stm32.isBootloaderMode() && !otaManager.isPortalActive()) {
        handleModeSelection(BTN_MODE_BRIDGE);
      }
      break;

    case BUTTON_GESTURE_TRIPLE:
      if (!stm32.isBootloaderMode() && !otaManager.isPortalActive()) {
        handleModeSelection(BTN_MODE_COMMAND);
      }
      break;

    case BUTTON_GESTURE_LONG_RETURN:
      if ((pressedMode == BTN_MODE_BRIDGE || pressedMode == BTN_MODE_COMMAND) &&
          !stm32.isBootloaderMode() && !otaManager.isPortalActive()) {
        handleModeSelection(BTN_MODE_I2C_MEASURE);
      }
      break;

    case BUTTON_GESTURE_LONG_PRIMARY:
      if ((pressedMode == BTN_MODE_BRIDGE || pressedMode == BTN_MODE_COMMAND) &&
          !stm32.isBootloaderMode() && !otaManager.isPortalActive()) {
        handleModeSelection(BTN_MODE_I2C_MEASURE);
      } else if (pressedMode == BTN_MODE_I2C_MEASURE &&
                 !stm32.isBootloaderMode() && !otaManager.isPortalActive()) {
        (void)updateCoordinator.queueButtonManualToggle(
          ulsa_update::Purpose::Esp32Ota);
      }
      break;

    case BUTTON_GESTURE_LONG_SECONDARY:
      if ((pressedMode == BTN_MODE_BRIDGE || pressedMode == BTN_MODE_COMMAND) &&
          !stm32.isBootloaderMode() && !otaManager.isPortalActive()) {
        handleModeSelection(BTN_MODE_I2C_MEASURE);
      }
      break;

    case BUTTON_GESTURE_NONE:
    default:
      break;
  }
}

void processButton() {
  const uint32_t nowMs = millis();
  dispatchButtonGesture(buttonGesture.poll(nowMs), ButtonPressContext::Normal,
                        currentMode);

  if (M5.BtnA.wasPressed()) {
    setPhysicalAuthorizationPreview(false);
    setI2cReturnPreview(false);
    buttonPressMode = currentMode;
    buttonPressAuthorizationGeneration =
      updateCoordinator.physicalAuthorizationRequired()
        ? updateCoordinator.authorizationGeneration() : 0U;
    buttonPressContext = otaManager.isPortalActive() || otaManager.isUpdating()
      ? ButtonPressContext::Blocked
      : (buttonPressAuthorizationGeneration != 0U
          ? ButtonPressContext::Authorization
          : ButtonPressContext::Normal);
    buttonGesture.onPress(nowMs);
  }

  if (M5.BtnA.isPressed()) {
    const ButtonGesturePreview preview = buttonGesture.preview(nowMs);
    setPhysicalAuthorizationPreview(
      ((buttonPressContext == ButtonPressContext::Authorization &&
        updateCoordinator.physicalAuthorizationRequired() &&
        buttonPressAuthorizationGeneration != 0U &&
        buttonPressAuthorizationGeneration ==
          updateCoordinator.authorizationGeneration()) ||
       (buttonPressContext == ButtonPressContext::Normal &&
        buttonPressMode == BTN_MODE_I2C_MEASURE &&
        !stm32.isBootloaderMode())) &&
      preview == BUTTON_PREVIEW_LONG_PRIMARY);
    setI2cReturnPreview(
      buttonPressContext == ButtonPressContext::Normal &&
      (buttonPressMode == BTN_MODE_BRIDGE || buttonPressMode == BTN_MODE_COMMAND) &&
      !stm32.isBootloaderMode() &&
      (preview == BUTTON_PREVIEW_LONG_RETURN ||
       preview == BUTTON_PREVIEW_LONG_PRIMARY ||
       preview == BUTTON_PREVIEW_LONG_SECONDARY));
  } else if (buttonGesture.isPressed()) {
    setPhysicalAuthorizationPreview(false);
    setI2cReturnPreview(false);
    const ButtonPressContext releaseContext = buttonPressContext;
    const ButtonGestureAction action = buttonGesture.onRelease(nowMs);
    dispatchButtonGesture(action, releaseContext, buttonPressMode);
    if (releaseContext != ButtonPressContext::Normal &&
        action != BUTTON_GESTURE_LONG_PRIMARY &&
        action != BUTTON_GESTURE_LONG_SECONDARY) {
      buttonGesture.cancel();
    }
    buttonPressAuthorizationGeneration = 0;
    buttonPressContext = ButtonPressContext::Normal;
    if (!stm32.isBootloaderMode() && !otaManager.isPortalActive()) {
      refreshSystemUiPresentation();
    }
  }
}

void handleWifiPortalToggle() {
  if (otaManager.isPortalActive()) {
    otaManager.stopPortal();
    refreshSystemUiPresentation();
    return;
  }

  if (stm32.isBootloaderMode()) {
    stm32.resetToNormal();
    delay(500);
  }

  refreshSystemUiPresentation();
  delay(100);
  if (otaManager.startPortal()) {
    refreshSystemUiPresentation();
  }
}

void handleStm32ModeToggle() {
  if (otaManager.isPortalActive()) {
    return;
  }

  if (stm32.isBootloaderMode()) {
    stm32.resetToNormal();
    ledCtrl.setMode(modeLed(currentMode));
    return;
  }

  stm32.setBootloaderModeFlag(true);
  delay(10);
  ledCtrl.setMode(MODE_BOOTLOADER);
  stm32.resetToBootloader();
}

void handleModeSelection(ButtonMode target) {
  if (currentMode == target) return;
  currentMode = target;
  uartBridge.setEnabled(target == BTN_MODE_BRIDGE);
  windSensor.setSource(target == BTN_MODE_I2C_MEASURE ? WIND_SOURCE_I2C : WIND_SOURCE_UART);
  windSensor.setDebugOutput(false);
  refreshSystemUiPresentation();
}
