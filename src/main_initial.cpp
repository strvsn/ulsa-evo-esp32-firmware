/**
 * @file main_initial.cpp
 * @brief ULSA EVO Initial profile composition root
 *
 * Factory setup, measurement, maintenance, recovery, and firmware
 * update runtime. Profile-specific behavior is kept in src/profile while
 * hardware, sensor, OTA, and STM32 update modules are shared with Demo.
 */

#include <M5Unified.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>

#include "commands/command_handler.h"
#include "config/pin_config.h"
#include "hardware/led_controller.h"
#include "hardware/system_ui_led_adapter.h"
#include "hardware/rtc_manager.h"
#include "hardware/stm32_bootloader.h"
#include "hardware/uart_bridge.h"
#include "ota/ota_manager.h"
#include "profile/firmware_profile.h"
#include "profile/initial_factory_boot_health.h"
#include "profile/initial_button_handler.h"
#include "profile/initial_command_mode.h"
#include "sensor/node_identity_synchronizer.h"
#include "sensor/wind_sensor.h"
#include "system/task_stats.h"
#include "system/update_coordinator.h"
#include "system/system_ui_presentation.h"
#include "system/system_ui_runtime_adapter.h"

#if !ULSA_PROFILE_IS_INITIAL
#error "main_initial.cpp belongs to the initial profile"
#endif

extern bool cmdRtc(int argc, const String* argv);
extern bool cmdLed(int argc, const String* argv);
extern bool cmdSys(int argc, const String* argv);
extern bool cmdWind(int argc, const String* argv);
extern bool cmdHelp(int argc, const String* argv);
extern bool cmdExit(int argc, const String* argv);

#define WDT_TIMEOUT_SEC 10
#define WDT_PANIC_ENABLED true

LedController ledCtrl;
STM32Bootloader stm32;
WindSensor windSensor;
UartBridge uartBridge;
RtcManager rtc;
OtaManager otaManager;
ulsa_update::UpdateCoordinator updateCoordinator;
NodeIdentitySynchronizer nodeIdentitySynchronizer;
CommandHandler cmdHandler;
initial_factory::InitialFactoryBootHealth initialFactoryBootHealth;

TaskStatsManager* g_taskStats = nullptr;
WindSensor* g_pWindSensor = &windSensor;

static const uint32_t BOOTLOADER_GO_COMPLETE_DELAY_MS = 250;
static const uint32_t BOOTLOADER_CUBEPROG_COMPLETE_QUIET_MS = 1000;
static bool bootloaderGoCompletePending = false;
static uint32_t bootloaderGoCompleteAtMs = 0;
static uint32_t bootloaderLastSnifferBytes = 0;
static uint32_t bootloaderLastSnifferActivityMs = 0;
static bool physicalAuthorizationHoldConfirmed = false;
static bool i2cReturnHoldConfirmed = false;

static void configureI2cDriveStrength() {
  gpio_set_drive_capability((gpio_num_t)I2C_SCL_PIN, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t)I2C_SDA_PIN, GPIO_DRIVE_CAP_0);
}

static bool isStm32UpdateModeActive() {
  const auto phase = otaManager.getStm32UpdatePhase();
  const bool updatePortalPurpose =
    otaManager.getPortalPurpose() == OtaPortalPurpose::Stm32Update;
  const bool updatePortalActive =
    otaManager.isPortalActive() && updatePortalPurpose;

  return updatePortalActive ||
         otaManager.isStm32BootloaderSessionActive() ||
         phase == stm32_update::UpdatePhase::Receiving ||
         phase == stm32_update::UpdatePhase::PackageStored ||
         phase == stm32_update::UpdatePhase::Verifying ||
         phase == stm32_update::UpdatePhase::Writing ||
         phase == stm32_update::UpdatePhase::VerifyingFlash ||
         phase == stm32_update::UpdatePhase::RecoveryRequired ||
         ((phase == stm32_update::UpdatePhase::ReadyToWrite ||
           phase == stm32_update::UpdatePhase::Complete ||
           phase == stm32_update::UpdatePhase::Error) &&
          updatePortalPurpose);
}

static ulsa_update::Purpose coordinatorPurpose(OtaPortalPurpose purpose) {
  return purpose == OtaPortalPurpose::Stm32Update
           ? ulsa_update::Purpose::Stm32Update
           : ulsa_update::Purpose::Esp32Ota;
}

static ulsa_update::RuntimeSnapshot updateRuntimeSnapshot() {
  return ulsa_update::RuntimeSnapshot(
    otaManager.isPortalActive(),
    otaManager.isUpdating(),
    otaManager.getPortalToken()[0] != '\0',
    coordinatorPurpose(otaManager.getPortalPurpose()),
    0,
    stm32.isBootloaderMode());
}

static bool serviceQueuedButtonUpdateRequest() {
  ulsa_update::Request request;
  if (!updateCoordinator.takeQueuedRequest(request)) {
    return false;
  }

  const auto decision = updateCoordinator.plan(request, updateRuntimeSnapshot(), millis());
  bool succeeded = false;
  switch (decision.action) {
    case ulsa_update::Action::PrepareAndStartPortal:
      otaManager.cancelPortalSession();
      succeeded = request.source == ulsa_update::Source::PhysicalButton &&
                  request.purpose == ulsa_update::Purpose::Esp32Ota &&
                  otaManager.prepareInitialDemoSession();
      if (succeeded) {
        handleWifiPortalToggle();
        succeeded = otaManager.isPortalActive();
      }
      break;

    case ulsa_update::Action::ToggleManualBootloader: {
      const bool wasBootloaderMode = stm32.isBootloaderMode();
      handleStm32ModeToggle();
      delay(300);
      succeeded = wasBootloaderMode != stm32.isBootloaderMode();
      break;
    }

    case ulsa_update::Action::StopPortal:
      handleWifiPortalToggle();
      succeeded = !otaManager.isPortalActive();
      break;

    case ulsa_update::Action::RejectBusy:
    case ulsa_update::Action::RejectUnavailable:
    case ulsa_update::Action::RejectPeerConflict:
    case ulsa_update::Action::None:
    case ulsa_update::Action::PublishStatus:
    case ulsa_update::Action::PrepareSession:
    case ulsa_update::Action::StartPortal:
    case ulsa_update::Action::CancelPrepared:
    default:
      break;
  }

  updateCoordinator.complete(decision, succeeded, updateRuntimeSnapshot(), millis());
  return true;
}

static void restoreInitialButtonModePresentation() {
  refreshSystemUiPresentation();
}

static SystemUiBaseMode systemUiBaseMode(ButtonMode mode) {
  switch (mode) {
    case BTN_MODE_COMMAND: return SystemUiBaseMode::Command;
    case BTN_MODE_BRIDGE: return SystemUiBaseMode::UartBridge;
    case BTN_MODE_NORMAL: return SystemUiBaseMode::UartMeasure;
    case BTN_MODE_I2C_MEASURE:
    default: return SystemUiBaseMode::I2cMeasure;
  }
}

static SystemUiState currentSystemUiState() {
  SystemUiState state;
  state.initialProfile = true;
  state.initialFactoryReady = initialFactoryBootHealth.isReady();
  state.initialFactoryError = initialFactoryBootHealth.hasPersistentError();
  state.baseMode = systemUiBaseMode(getButtonMode());
  state.manualBootloader = stm32.isBootloaderMode();
  state.i2cReturnHoldConfirmed = i2cReturnHoldConfirmed;

  populateSystemUiUpdateState(state, otaManager, updateCoordinator);
  applyPortalClientConnectionPresentation(
    state, otaManager.isAppDrivenPortal(),
    otaManager.isPortalClientConnected());
  applyUpdateAuthorizationButtonPresentation(
    state, physicalAuthorizationHoldConfirmed, true);
  return state;
}

void refreshSystemUiPresentation() {
  applyLedPresentation(ledCtrl, currentSystemUiState());
}

void setPhysicalAuthorizationPreview(bool confirmed) {
  if (physicalAuthorizationHoldConfirmed == confirmed) return;
  physicalAuthorizationHoldConfirmed = confirmed;
  refreshSystemUiPresentation();
}

void setI2cReturnPreview(bool confirmed) {
  if (i2cReturnHoldConfirmed == confirmed) return;
  i2cReturnHoldConfirmed = confirmed;
  refreshSystemUiPresentation();
}

static void restoreDefaultModeAfterBootloaderComplete() {
  ledCtrl.blinkBootloaderComplete(3);
  stm32.resetToNormal();
  uartBridge.setEnabled(false);
  setButtonMode(BTN_MODE_I2C_MEASURE);
  windSensor.setSource(WIND_SOURCE_I2C);
  windSensor.setDebugOutput(false);
  ledCtrl.setMode(MODE_I2C_MEASURE);
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.fallback_board = m5::board_t::board_M5StampC3U;
  cfg.output_power = false;
  M5.begin(cfg);

  ledCtrl.begin();
  // Solid green means the Initial factory gate is still running. Only the
  // green 500 ms blink produced by the resolver means factory-ready.
  ledCtrl.setMode(MODE_INITIAL_CHECKING);
  M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_NONE);

  Serial.setRxBufferSize(1024);
  Serial.setTxBufferSize(1024);
  Serial1.setRxBufferSize(1024);
  Serial1.setTxBufferSize(1024);
  Serial1.begin(UART1_BAUD_RATE, UART1_CONFIG_NORMAL, UART1_RX_PIN, UART1_TX_PIN);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQUENCY);
  Wire.setClock(I2C_FREQUENCY);
  Wire.setTimeOut(ULSA_EVO_I2C_TIMEOUT_MS);
  configureI2cDriveStrength();

  stm32.begin();
  windSensor.begin(&uartBridge);
  windSensor.beginI2c(&Wire, ULSA_EVO_I2C_ADDR_DEFAULT);
  windSensor.setSource(WIND_SOURCE_I2C);
  windSensor.setDebugOutput(false);
  uartBridge.begin(&Serial, &Serial1);
  uartBridge.setEnabled(false);
  stm32.setUartBridge(&uartBridge);

  // Initial profileでも、RTC異常をコンパイル時刻で隠さない。
  // 復旧には`rtc set`または製品アプリの明示的なCTS同期を使用する。
  (void)rtc.begin();

  g_taskStats = new TaskStatsManager(0);
  g_taskStats->begin();

  cmdHandler.registerCommand("rtc", cmdRtc, "UTC RTC and IANA timezone control");
  cmdHandler.registerCommand("led", cmdLed, "LED brightness control (get/brightness)");
  cmdHandler.registerCommand("sys", cmdSys, "System info (info/memory/tasks/bootloader/reboot)");
  cmdHandler.registerCommand("wind", cmdWind, "Wind sensor data and I2C diagnostics");
  cmdHandler.registerCommand("help", cmdHelp, "Show available commands");
  cmdHandler.registerCommand("exit", cmdExit, "Exit command mode");

  otaManager.begin(0, &stm32, &windSensor.getI2cClient());
  if (!initialFactoryBootHealth.run(otaManager.isStm32UpdateFactoryReady())) {
    Serial.printf("INITIAL_FACTORY_ERROR:%s\n",
                  initialFactoryBootHealth.getErrorString());
  }
  refreshSystemUiPresentation();
  nodeIdentitySynchronizer.synchronize(windSensor.getI2cClient(), otaManager, true);
  otaManager.onStartCallback = []() {
    refreshSystemUiPresentation();
    ledCtrl.setOtaProgress(0);
    ledCtrl.update();
  };
  otaManager.onProgressCallback = [](uint8_t progress) {
    refreshSystemUiPresentation();
    ledCtrl.setOtaProgress(progress);
    ledCtrl.update();
  };
  otaManager.onEndCallback = []() {};
  otaManager.onErrorCallback = [](const char*) {
    refreshSystemUiPresentation();
  };

  esp_task_wdt_init(WDT_TIMEOUT_SEC, WDT_PANIC_ENABLED);
  esp_task_wdt_add(NULL);
}

void loop() {
  M5.update();
  processButton();

  if (otaManager.isStm32BootloaderSessionActive()) {
    otaManager.update();
    ledCtrl.update();
    esp_task_wdt_reset();
    yield();
    return;
  }

  if (stm32.isBootloaderMode()) {
    uartBridge.processBootloaderMode();
    ledCtrl.update();

    uint32_t now = millis();
    uint32_t snifferBytes = uartBridge.getBootloaderSnifferByteCount();
    if (snifferBytes != bootloaderLastSnifferBytes) {
      bootloaderLastSnifferBytes = snifferBytes;
      bootloaderLastSnifferActivityMs = now;
    }

    if (uartBridge.consumeBootloaderGoComplete() && !bootloaderGoCompletePending) {
      bootloaderGoCompletePending = true;
      bootloaderGoCompleteAtMs = now + BOOTLOADER_GO_COMPLETE_DELAY_MS;
    }

    if (bootloaderGoCompletePending &&
        (int32_t)(now - bootloaderGoCompleteAtMs) >= 0) {
      bootloaderGoCompletePending = false;
      restoreDefaultModeAfterBootloaderComplete();
      esp_task_wdt_reset();
      yield();
      return;
    }

    if (!bootloaderGoCompletePending &&
        bootloaderLastSnifferActivityMs != 0 &&
        (int32_t)(now - bootloaderLastSnifferActivityMs) >=
          (int32_t)BOOTLOADER_CUBEPROG_COMPLETE_QUIET_MS &&
        uartBridge.isBootloaderCubeProgrammerCompleteCandidate()) {
      restoreDefaultModeAfterBootloaderComplete();
      esp_task_wdt_reset();
      yield();
      return;
    }

    esp_task_wdt_reset();
    yield();
    return;
  }
  bootloaderGoCompletePending = false;
  bootloaderLastSnifferBytes = 0;
  bootloaderLastSnifferActivityMs = 0;

  // Preserve the manual ROM bootloader bridge as a timing-sensitive fast
  // path. Coordinator and presentation work resumes only after that mode is
  // no longer active.
  updateCoordinator.reconcile(updateRuntimeSnapshot(), millis());
  (void)serviceQueuedButtonUpdateRequest();
  refreshSystemUiPresentation();

  if (otaManager.isPortalActive()) {
    const auto stateBeforeUpdate = updateCoordinator.state();
    otaManager.update();
    updateCoordinator.reconcile(updateRuntimeSnapshot(), millis());
    if (stateBeforeUpdate == ulsa_update::State::PortalActive &&
        updateCoordinator.state() == ulsa_update::State::Idle) {
      restoreInitialButtonModePresentation();
    }
    ledCtrl.update();
    esp_task_wdt_reset();
    yield();
    return;
  }

  const ButtonMode currentButtonMode = getButtonMode();
  if (currentButtonMode == BTN_MODE_COMMAND) {
    processInitialCommandMode();
    return;
  }

  uartBridge.process();
  nodeIdentitySynchronizer.synchronize(windSensor.getI2cClient(), otaManager);
  uartBridge.checkBootCommand();
  if (uartBridge.isBootCommandReceived()) {
    uartBridge.clearBootCommand();
    (void)updateCoordinator.queueButtonManualBootloaderToggle();
    esp_task_wdt_reset();
    return;
  }

  const bool shouldUpdateWind = currentButtonMode == BTN_MODE_NORMAL ||
                                currentButtonMode == BTN_MODE_I2C_MEASURE;
  if (shouldUpdateWind && windSensor.update()) {
    if (!uartBridge.isEnabled()) {
      windSensor.printDataOneLine();
    }
  }

  if (currentButtonMode == BTN_MODE_NORMAL && !uartBridge.isEnabled()) {
    const ParseStats& stats = windSensor.getParseStats();
    static uint32_t lastErrorTime = 0;
    if (stats.lastErrorTime > lastErrorTime && millis() - stats.lastErrorTime < 1000) {
      lastErrorTime = stats.lastErrorTime;
    }
  }

  ledCtrl.update();
  otaManager.update();
  esp_task_wdt_reset();
}
