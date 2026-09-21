/**
 * @file button_handler.cpp
 * @brief ボタン入力処理モジュール
 * @date 2025-12-07
 * 
 * M5Stamp C3Uのボタン入力処理:
 * - 1回: I2C計測中のSDログ開始/停止
 * - 2回: UART Bridge、3回: Command
 * - Bridge/Command中の2秒以上: I2C計測へ復帰
 * - OTA request pending中は3秒以上でrelease: requestの物理認可
 *
 * 長押しはrelease時に一つだけ確定し、押下中はhardware状態を変更しない。
 */

#include "button_handler.h"
#include <M5Unified.h>
#include "system/update_coordinator.h"
#include "system/system_ui_presentation.h"
#include "uart_bridge.h"
#include "wind_sensor.h"
// 外部モジュール参照（main.cppで定義）
extern LedController ledCtrl;
extern STM32Bootloader stm32;
extern BleManager bleManager;
extern RtcManager rtc;
extern SdLogger sdLogger;
extern OtaManager otaManager;
extern UartBridge uartBridge;
extern ulsa_update::UpdateCoordinator updateCoordinator;
// extern BootloaderDebugger bootDebugger;  // D入力デバッグ機能を無効化

// ボタン状態管理
static ButtonMode currentMode = BTN_MODE_I2C_MEASURE;  // デフォルトはI2C計測モード
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

static void publishDeviceModeBeforeBleStop(uint8_t mode, uint8_t flags) {
  if (!bleManager.isRunning()) {
    return;
  }
  bleManager.updateDeviceModeStatus(mode, flags);
  delay(20);
}

static void restoreButtonBasePresentation() {
  if (stm32.isBootloaderMode() || otaManager.isPortalActive()) {
    return;
  }
  refreshSystemUiPresentation();
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
      if (pressedMode == BTN_MODE_I2C_MEASURE &&
          !stm32.isBootloaderMode() && !otaManager.isPortalActive()) {
        handleSdLoggingToggle();
      }
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
    case BUTTON_GESTURE_LONG_PRIMARY:
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

// ============================================
// ボタン処理メイン
// ============================================
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
      buttonPressContext == ButtonPressContext::Authorization &&
      (preview == BUTTON_PREVIEW_LONG_PRIMARY ||
       preview == BUTTON_PREVIEW_LONG_SECONDARY) &&
      updateCoordinator.physicalAuthorizationRequired() &&
      buttonPressAuthorizationGeneration != 0U &&
      buttonPressAuthorizationGeneration ==
        updateCoordinator.authorizationGeneration());
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
    restoreButtonBasePresentation();
  }
}

// ============================================
// SDログ開始/停止切替（I2C計測中の物理単押し）
// ============================================
void handleSdLoggingToggle() {
  if (sdLogger.isRecordingRequested()) {
    sdLogger.disableLogging(SD_STOP_USER_DISABLED);
  } else if (currentMode == BTN_MODE_I2C_MEASURE) {
    (void)sdLogger.resumeLogging();
  }

  // The app already consumes these status characteristics, so physical
  // controls stay visible through the same BLE state path as BLE commands.
  bleManager.updateSdStatus(true);
}

// ============================================
// WiFiポータル起動/停止切替
// ============================================
void handleWifiPortalToggle() {
  if (otaManager.isPortalActive()) {
    // ポータル停止 → BLE再開
    otaManager.stopPortal();
    // M5.Log.println("WiFi Portal: Stopped, restarting BLE...");
    if (otaManager.refreshNodeIdentityFromI2c()) {
      bleManager.begin(otaManager.getNodeId(), &rtc, &sdLogger);
    } else {
      bleManager.restartWithLastVerifiedNodeId();
    }
    refreshSystemUiPresentation();
  } else {
    // BLE停止 → ポータル起動（リソース競合回避）
    // M5.Log.println("WiFi Portal: Stopping BLE for portal...");
    publishDeviceModeBeforeBleStop(BLE_DEVICE_MODE_WIFI_PORTAL,
                                   BLE_DEVICE_MODE_FLAG_WIFI_PORTAL);
    bleManager.stop();
    
    // WiFi ポータル起動時に STM32 ブートローダーモードを強制的に OFF にする
    if (stm32.isBootloaderMode()) {
      // M5.Log.println("WiFi Portal: Forcing STM32 to normal mode");
      stm32.resetToNormal();
      delay(500);
    }
    
    refreshSystemUiPresentation();
    
    delay(100);
    if (otaManager.startPortal()) {
      refreshSystemUiPresentation();
    } else if (otaManager.refreshNodeIdentityFromI2c()) {
      // A failed portal start must not leave measurement BLE disabled.
      bleManager.begin(otaManager.getNodeId(), &rtc, &sdLogger);
      refreshSystemUiPresentation();
    } else {
      // STM32が起動途中でも、直前のSTM32確認済みIDでBLEを復帰する。
      bleManager.restartWithLastVerifiedNodeId();
      refreshSystemUiPresentation();
    }
    // M5.Log.println("WiFi Portal: Started");
  }
}

// ============================================
// STM32ブートローダーモード切替
// ============================================
void handleStm32ModeToggle() {
  // WiFiポータル中はSTM32モード切替無効
  if (otaManager.isPortalActive()) {
    return;
  }
  
  // STM32モード切替
  if (stm32.isBootloaderMode()) {
    stm32.resetToNormal();
    // 通常モードに戻るときはBLE状態に応じたLEDに
    if (bleManager.isConnected()) {
      LedMode mode = (bleManager.getCurrentPhy() == BLE_PHY_CODED) ? 
                     MODE_BLE_CONNECTED_CODED : MODE_BLE_CONNECTED_1M;
      ledCtrl.setMode(mode);
    } else {
      ledCtrl.setMode(MODE_BLE_DISCONNECTED);
    }
  } else {
    // ブートローダーモードへの遷移
    // すべてのペリフェラルを完全停止（USB-CDC汚染を防ぐ）
    // M5.Log.println("[Button] Stopping all peripherals for bootloader mode...");
    
    // 1. まずブートローダーフラグを立てる（loop()での処理をスキップさせる）
    stm32.setBootloaderModeFlag(true);
    delay(10);
    
    // 2. BLE完全停止
    bleManager.stop();
    delay(100);
    
    // 3. SD書き込み停止（バッファフラッシュ）
    sdLogger.flush();
    delay(50);
    
    // 4. デバッグログをクリア（新しいセッション開始） - D入力デバッグ機能を無効化
    // bootDebugger.clearDataLog();
    
    // 5. LEDをブートローダーモード（赤色常時点灯）に設定
    ledCtrl.setMode(MODE_BOOTLOADER);
    
    // 6. BOOT0ピン設定とSTM32リセット（徹底的なバッファクリアと待機を含む）
    // resetToBootloader()完了後、即座にloop()に戻りuartBridge.process()が実行される
    stm32.resetToBootloader();
    // M5.Log.println("[Button] Bootloader mode entered - bridge only");
  }
}

// ============================================
// 現在のボタンモードを取得
// ============================================
ButtonMode getButtonMode() {
  return currentMode;
}

// ============================================
// ボタンモードを設定（外部から変更可能）
// ============================================
void setButtonMode(ButtonMode mode) {
  currentMode = mode;
}

// ============================================
// ボタンモードの直接選択
// ============================================
void handleModeSelection(ButtonMode target) {
  extern WindSensor windSensor;
  if (currentMode == target) return;
  if (target != BTN_MODE_I2C_MEASURE &&
      (sdLogger.isRecordingRequested() || !sdLogger.isStorageQuiescent())) {
    M5.Log.println("[Mode] Stop SD logging before changing mode");
    return;
  }
  currentMode = target;
  uartBridge.setEnabled(target == BTN_MODE_BRIDGE);
  windSensor.setSource(target == BTN_MODE_I2C_MEASURE ? WIND_SOURCE_I2C : WIND_SOURCE_UART);
  windSensor.setDebugOutput(false);
  refreshSystemUiPresentation();
  M5.Log.println(target == BTN_MODE_I2C_MEASURE ? "[Mode] I2C_MEASURE" :
                 target == BTN_MODE_BRIDGE ? "[Mode] BRIDGE" : "[Mode] COMMAND");
}
