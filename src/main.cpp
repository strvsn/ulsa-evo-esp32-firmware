/**
 * @file main.cpp
 * @brief ULSA EVO - 超音波風速計デモファームウェア
 * @date 2025-12-03
 *
 * 各機能は専用モジュールに分離:
 * - WindSensor: 風速計データ受信・パース
 * - UartBridge: STM32書き込み用ブリッジ
 * - LedController: LED制御
 * - STM32Bootloader: STM32モード切替
 * - BleManager: NimBLE BLE通信
 * - OtaManager: 一時SoftAP HTTP OTAアップデート
 * - button_handler: ボタン入力処理
 */

#include <M5Unified.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>
#include "config/debug_config.h"
#include "config/pin_config.h"
#include "hardware/led_controller.h"
#include "hardware/stm32_bootloader.h"
#include "sensor/node_identity_synchronizer.h"
#include "sensor/wind_sensor.h"
#include "hardware/uart_bridge.h"
#include "ble/ble_manager.h"
#include "hardware/rtc_manager.h"
#include "storage/sd_logger.h"
#include "ota/ota_manager.h"
#include "hardware/button_handler.h"
#include "hardware/boot_recovery_hold.h"
#include "hardware/system_ui_led_adapter.h"
#include "commands/command_handler.h"
#include "system/task_stats.h"  // タスク統計（独自実装）
#include "system/update_coordinator.h"
#include "system/demo_ota_boot_health.h"
#include "system/system_ui_presentation.h"
#include "system/system_ui_runtime_adapter.h"
// コマンド関数の外部参照
extern bool cmdRtc(int argc, const String* argv);
extern bool cmdLed(int argc, const String* argv);
extern bool cmdSys(int argc, const String* argv);
extern bool cmdSd(int argc, const String* argv);
extern bool cmdWind(int argc, const String* argv);
extern bool cmdBle(int argc, const String* argv);
extern bool cmdHelp(int argc, const String* argv);
extern bool cmdExit(int argc, const String* argv);

// ============================================
// ウォッチドッグタイマー設定
// ============================================
#define WDT_TIMEOUT_SEC     10    // タイムアウト秒数（10秒でリセット）
#define WDT_PANIC_ENABLED   true  // タイムアウト時にパニック（再起動）

// ============================================
// モジュールインスタンス
// ============================================
LedController ledCtrl;
STM32Bootloader stm32;
WindSensor windSensor;
UartBridge uartBridge;
BleManager bleManager;
RtcManager rtc;
SdLogger sdLogger;
OtaManager otaManager;
ulsa_update::UpdateCoordinator updateCoordinator;
NodeIdentitySynchronizer nodeIdentitySynchronizer;
CommandHandler cmdHandler;  // コマンドハンドラー
// BootloaderDebugger bootDebugger;  // D入力デバッグ機能を無効化

// TaskStats（コマンド経由でCPU使用率表示）
TaskStatsManager* g_taskStats = nullptr;

// コマンドハンドラーから参照されるグローバルポインタ
SdLogger* g_pSdLogger = &sdLogger;
WindSensor* g_pWindSensor = &windSensor;
// g_pBleManagerはble_callbacks.cppで定義済み
extern BleManager* g_pBleManager;

// LED デバッグ出力タイマー
static uint32_t lastLedDebugTime = 0;
static const uint32_t LED_DEBUG_INTERVAL_MS = 5000; // 5秒ごとに LED 状態を出力
static const uint32_t BOOTLOADER_GO_COMPLETE_DELAY_MS = 250;
static const uint32_t BOOTLOADER_CUBEPROG_COMPLETE_QUIET_MS = 1000;
static bool bootloaderGoCompletePending = false;
static uint32_t bootloaderGoCompleteAtMs = 0;
static uint32_t bootloaderLastSnifferBytes = 0;
static uint32_t bootloaderLastSnifferActivityMs = 0;
static bool physicalAuthorizationHoldConfirmed = false;
static bool i2cReturnHoldConfirmed = false;

static void restoreUpdateAuthorizationPresentation();

static void applyStartupRecoveryPresentation(SystemUiUpdatePurpose purpose,
                                             SystemUiUpdateStage stage) {
  SystemUiState state;
  state.updatePurpose = purpose;
  state.updateStage = stage;
  applyLedPresentation(ledCtrl, state);
}

static bool detectBootRecoveryRequest(SystemUiUpdatePurpose purpose) {
  M5.update();
  if (!M5.BtnA.isPressed()) return false;

  BootRecoveryHold hold(5000U);
  applyStartupRecoveryPresentation(purpose, SystemUiUpdateStage::Recovery);
  (void)hold.sample(true, millis());
  while (true) {
    delay(10);
    M5.update();
    const BootRecoveryHoldEvent event = hold.sample(M5.BtnA.isPressed(), millis());
    if (event == BootRecoveryHoldEvent::HoldConfirmed) {
      applyStartupRecoveryPresentation(
        purpose, SystemUiUpdateStage::AuthorizationGranted);
    } else if (event == BootRecoveryHoldEvent::EnterRecovery) {
      return true;
    } else if (event == BootRecoveryHoldEvent::Cancelled) {
      return false;
    }
    ledCtrl.update();
  }
}

static void serviceLedController() {
  const bool loggingActive = sdLogger.isLoggingEnabled();
  const bool userStopConfirmed =
    !loggingActive && sdLogger.getStopReason() == SD_STOP_USER_DISABLED;
  ledCtrl.updateSdLoggingState(loggingActive, userStopConfirmed);
  ledCtrl.update();
}

static void configureI2cDriveStrength() {
  // I2CのLOW引き込みを弱め、SCL/SDA立下り時のアンダーシュートを抑える。
  gpio_set_drive_capability((gpio_num_t)I2C_SCL_PIN, GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability((gpio_num_t)I2C_SDA_PIN, GPIO_DRIVE_CAP_0);
}

static void configureSpiDriveStrength() {
  // MISOはSDカード側が駆動するため、ESP32が駆動するSCLK/MOSI/CSだけを対象にする。
  const gpio_drive_cap_t spiDriveCap = GPIO_DRIVE_CAP_DEFAULT;
  gpio_set_drive_capability((gpio_num_t)SPI_CLK_PIN, spiDriveCap);
  gpio_set_drive_capability((gpio_num_t)SPI_MOSI_PIN, spiDriveCap);
  gpio_set_drive_capability((gpio_num_t)SPI_CS_PIN, spiDriveCap);
}

static bool synchronizeBleNodeIdentity(bool force = false) {
  const bool labelRefreshed = nodeIdentitySynchronizer.synchronize(
    windSensor.getI2cClient(), otaManager, force);
  if (!bleManager.isRunning()) {
    const uint8_t nodeLabel = labelRefreshed
      ? nodeIdentitySynchronizer.getNodeId()
      : otaManager.getNodeId();
    return bleManager.begin(nodeLabel, &rtc, &sdLogger);
  }

  if (!labelRefreshed) {
    return false;
  }
  return bleManager.updateNodeId(nodeIdentitySynchronizer.getNodeId());
}

static void refreshInformationalNodeLabelForPortalSession() {
  // Node ID is a user label and may be zero, duplicated, changed, or
  // temporarily unreadable. Refresh it for display/SSID when possible, but do
  // not make OTA authorization depend on it.
  (void)synchronizeBleNodeIdentity(true);
}

static_assert(BLE_OTA_OP_READ == BLE_STM32_UPDATE_OP_READ,
              "ESP32 and STM32 update read op must stay aligned");
static_assert(BLE_OTA_OP_PREPARE_PORTAL == BLE_STM32_UPDATE_OP_PREPARE_PORTAL,
              "ESP32 and STM32 update prepare op must stay aligned");
static_assert(BLE_OTA_OP_ACTIVATE_PORTAL == BLE_STM32_UPDATE_OP_ACTIVATE_PORTAL,
              "ESP32 and STM32 update activate op must stay aligned");
static_assert(BLE_OTA_OP_STOP_OR_CANCEL == BLE_STM32_UPDATE_OP_STOP_OR_CANCEL,
              "ESP32 and STM32 update cancel op must stay aligned");
static_assert(BLE_OTA_RESULT_BUSY == BLE_STM32_UPDATE_RESULT_BUSY,
              "ESP32 and STM32 update busy result must stay aligned");
static_assert(BLE_OTA_RESULT_UNAVAILABLE == BLE_STM32_UPDATE_RESULT_UNAVAILABLE,
              "ESP32 and STM32 update unavailable result must stay aligned");
static_assert(BLE_OTA_RESULT_FAILED == BLE_STM32_UPDATE_RESULT_FAILED,
              "ESP32 and STM32 update failed result must stay aligned");
static_assert(BLE_OTA_RESULT_AUTHORIZATION_REQUIRED ==
                BLE_STM32_UPDATE_RESULT_AUTHORIZATION_REQUIRED,
              "ESP32 and STM32 authorization-required result must stay aligned");
static_assert(BLE_OTA_RESULT_AUTHORIZATION_EXPIRED ==
                BLE_STM32_UPDATE_RESULT_AUTHORIZATION_EXPIRED,
              "ESP32 and STM32 authorization-expired result must stay aligned");
static_assert(BLE_OTA_RESULT_PEER_CONFLICT ==
                BLE_STM32_UPDATE_RESULT_PEER_CONFLICT,
              "ESP32 and STM32 peer-conflict result must stay aligned");

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
    bleManager.getConnectionCount(),
    stm32.isBootloaderMode());
}

static ulsa_update::Operation coordinatorOperation(uint8_t op) {
  switch (op) {
    case BLE_OTA_OP_PREPARE_PORTAL:
      return ulsa_update::Operation::Prepare;
    case BLE_OTA_OP_ACTIVATE_PORTAL:
      return ulsa_update::Operation::Activate;
    case BLE_OTA_OP_STOP_OR_CANCEL:
      return ulsa_update::Operation::StopOrCancel;
    case BLE_OTA_OP_READ:
    default:
      return ulsa_update::Operation::Read;
  }
}

static uint8_t otaResultForDecision(const ulsa_update::Decision& decision) {
  switch (decision.result) {
    case ulsa_update::Result::Busy:
      return BLE_OTA_RESULT_BUSY;
    case ulsa_update::Result::Unavailable:
      return BLE_OTA_RESULT_UNAVAILABLE;
    case ulsa_update::Result::Failed:
      return BLE_OTA_RESULT_FAILED;
    case ulsa_update::Result::AuthorizationRequired:
      return BLE_OTA_RESULT_AUTHORIZATION_REQUIRED;
    case ulsa_update::Result::AuthorizationExpired:
      return BLE_OTA_RESULT_AUTHORIZATION_EXPIRED;
    case ulsa_update::Result::PeerConflict:
      return BLE_OTA_RESULT_PEER_CONFLICT;
    case ulsa_update::Result::Ok:
    default:
      return BLE_OTA_RESULT_OK;
  }
}

static bool processBleOtaControlRequest() {
  uint8_t op = 0;
  uint16_t peerHandle = ulsa_update::INVALID_PEER_HANDLE;
  if (!bleManager.consumeOtaControlRequest(op, peerHandle)) {
    return false;
  }

  if (op != BLE_OTA_OP_READ &&
      op != BLE_OTA_OP_PREPARE_PORTAL &&
      op != BLE_OTA_OP_ACTIVATE_PORTAL &&
      op != BLE_OTA_OP_STOP_OR_CANCEL) {
    bleManager.publishOtaControlStatus(op, BLE_OTA_RESULT_INVALID_OP);
    return false;
  }

  const ulsa_update::Request request(
    ulsa_update::Purpose::Esp32Ota,
    ulsa_update::Source::Ble,
    coordinatorOperation(op),
    peerHandle);
  const auto decision = updateCoordinator.plan(request, updateRuntimeSnapshot(), millis());
  uint8_t result = otaResultForDecision(decision);
  bool activatedPortal = false;
  bool succeeded = decision.result == ulsa_update::Result::Ok;
  bool statusPublished = false;

  switch (decision.action) {
    case ulsa_update::Action::BeginAuthorization:
      otaManager.cancelPortalSession();
      if (!bleManager.lockMaintenancePeer(peerHandle)) {
        result = BLE_OTA_RESULT_PEER_CONFLICT;
      } else {
        refreshInformationalNodeLabelForPortalSession();
      }
      succeeded = result == BLE_OTA_RESULT_AUTHORIZATION_REQUIRED;
      if (!succeeded) {
        bleManager.unlockMaintenancePeer();
      }
      break;

    case ulsa_update::Action::StartPortal:
      bleManager.publishOtaControlStatus(op, result);
      statusPublished = true;
      delay(120);
      handleWifiPortalToggle();
      activatedPortal = otaManager.isPortalActive();
      succeeded = activatedPortal;
      break;

    case ulsa_update::Action::StopPortal:
      handleWifiPortalToggle();
      succeeded = !otaManager.isPortalActive();
      break;

    case ulsa_update::Action::CancelPrepared:
      otaManager.cancelPortalSession();
      bleManager.unlockMaintenancePeer();
      restoreUpdateAuthorizationPresentation();
      succeeded = otaManager.getPortalToken()[0] == '\0';
      break;

    case ulsa_update::Action::PublishStatus:
      if (decision.result == ulsa_update::Result::AuthorizationExpired) {
        otaManager.cancelPortalSession();
        bleManager.unlockMaintenancePeer();
        restoreUpdateAuthorizationPresentation();
      }
      break;
    case ulsa_update::Action::RejectBusy:
    case ulsa_update::Action::RejectUnavailable:
    case ulsa_update::Action::RejectPeerConflict:
      break;

    case ulsa_update::Action::None:
    case ulsa_update::Action::PrepareSession:
    case ulsa_update::Action::PrepareAndStartPortal:
    default:
      result = BLE_OTA_RESULT_FAILED;
      succeeded = false;
      break;
  }

  updateCoordinator.complete(decision, succeeded, updateRuntimeSnapshot(), millis());
  refreshSystemUiPresentation();
  if (!statusPublished) {
    bleManager.publishOtaControlStatus(op, result);
  }
  return activatedPortal;
}

static bool processBleStm32UpdateControlRequest() {
  BleStm32UpdateControlRequest request;
  if (!bleManager.consumeStm32UpdateControlRequest(request)) {
    return false;
  }

  const uint8_t op = request.op;
  if (op != BLE_STM32_UPDATE_OP_READ &&
      op != BLE_STM32_UPDATE_OP_PREPARE_PORTAL &&
      op != BLE_STM32_UPDATE_OP_ACTIVATE_PORTAL &&
      op != BLE_STM32_UPDATE_OP_STOP_OR_CANCEL) {
    bleManager.publishStm32UpdateControlStatus(
      op, BLE_STM32_UPDATE_RESULT_INVALID_OP);
    return false;
  }

  if (op == BLE_STM32_UPDATE_OP_PREPARE_PORTAL) {
    if (!request.hasSessionBinding) {
      bleManager.publishStm32UpdateControlStatus(
        op, BLE_STM32_UPDATE_RESULT_INVALID_LENGTH);
      return false;
    }
  }

  ulsa_update::Request coordinatorRequest(
    ulsa_update::Purpose::Stm32Update,
    ulsa_update::Source::Ble,
    coordinatorOperation(op),
    request.peerHandle);
  if (op == BLE_STM32_UPDATE_OP_PREPARE_PORTAL &&
      !coordinatorRequest.setStm32Binding(
        request.hasExpectedNodeId,
        request.expectedNodeId,
        request.target,
        request.releaseTag)) {
    bleManager.publishStm32UpdateControlStatus(
      op, BLE_STM32_UPDATE_RESULT_INVALID_LENGTH);
    return false;
  }
  const auto decision = updateCoordinator.plan(
    coordinatorRequest, updateRuntimeSnapshot(), millis());
  uint8_t result = otaResultForDecision(decision);
  bool activatedPortal = false;
  bool succeeded = decision.result == ulsa_update::Result::Ok;
  bool statusPublished = false;

  switch (decision.action) {
    case ulsa_update::Action::BeginAuthorization:
      otaManager.cancelPortalSession();
      if (!bleManager.lockMaintenancePeer(request.peerHandle)) {
        result = BLE_STM32_UPDATE_RESULT_PEER_CONFLICT;
      } else {
        refreshInformationalNodeLabelForPortalSession();
      }
      succeeded = result == BLE_STM32_UPDATE_RESULT_AUTHORIZATION_REQUIRED;
      if (!succeeded) {
        bleManager.unlockMaintenancePeer();
      }
      break;

    case ulsa_update::Action::StartPortal:
      bleManager.publishStm32UpdateControlStatus(op, result);
      statusPublished = true;
      delay(120);
      handleWifiPortalToggle();
      activatedPortal = otaManager.isPortalActive();
      succeeded = activatedPortal;
      break;

    case ulsa_update::Action::StopPortal:
      handleWifiPortalToggle();
      succeeded = !otaManager.isPortalActive();
      break;

    case ulsa_update::Action::CancelPrepared:
      otaManager.cancelPortalSession();
      bleManager.unlockMaintenancePeer();
      restoreUpdateAuthorizationPresentation();
      succeeded = otaManager.getPortalToken()[0] == '\0';
      break;

    case ulsa_update::Action::PublishStatus:
      if (decision.result == ulsa_update::Result::AuthorizationExpired) {
        otaManager.cancelPortalSession();
        bleManager.unlockMaintenancePeer();
        restoreUpdateAuthorizationPresentation();
      }
      break;
    case ulsa_update::Action::RejectBusy:
    case ulsa_update::Action::RejectUnavailable:
    case ulsa_update::Action::RejectPeerConflict:
      break;

    case ulsa_update::Action::None:
    case ulsa_update::Action::PrepareSession:
    case ulsa_update::Action::PrepareAndStartPortal:
    default:
      result = BLE_STM32_UPDATE_RESULT_FAILED;
      succeeded = false;
      break;
  }

  updateCoordinator.complete(
    decision, succeeded, updateRuntimeSnapshot(), millis());
  refreshSystemUiPresentation();
  if (!statusPublished) {
    bleManager.publishStm32UpdateControlStatus(op, result);
  }
  return activatedPortal;
}

static bool serviceQueuedButtonUpdateRequest() {
  ulsa_update::Request request;
  if (!updateCoordinator.takeQueuedRequest(request)) {
    return false;
  }

  const auto decision = updateCoordinator.plan(request, updateRuntimeSnapshot(), millis());
  bool succeeded = false;
  switch (decision.action) {
    case ulsa_update::Action::PrepareSession:
      otaManager.cancelPortalSession();
      if (decision.request.purpose == ulsa_update::Purpose::Stm32Update) {
        succeeded = otaManager.prepareStm32UpdateSession(
          decision.request.hasNodeLabel,
          decision.request.nodeLabel,
          decision.request.target,
          decision.request.releaseTag);
      } else {
        succeeded = otaManager.preparePortalSession(OtaPortalPurpose::Esp32Ota);
      }
      updateCoordinator.complete(
        decision, succeeded, updateRuntimeSnapshot(), millis());
      if (decision.request.purpose == ulsa_update::Purpose::Stm32Update) {
        bleManager.publishStm32UpdateControlStatus(
          BLE_STM32_UPDATE_OP_PREPARE_PORTAL,
          succeeded ? BLE_STM32_UPDATE_RESULT_OK
                    : BLE_STM32_UPDATE_RESULT_FAILED);
      } else {
        bleManager.publishOtaControlStatus(
          BLE_OTA_OP_PREPARE_PORTAL,
          succeeded ? BLE_OTA_RESULT_OK : BLE_OTA_RESULT_FAILED);
      }
      if (!succeeded) bleManager.unlockMaintenancePeer();
      refreshSystemUiPresentation();
      return true;

    case ulsa_update::Action::PrepareAndStartPortal:
      otaManager.cancelPortalSession();
      if (request.source == ulsa_update::Source::BootRecovery) {
        succeeded = otaManager.prepareRecoveryPortalSession(
          request.purpose == ulsa_update::Purpose::Stm32Update
            ? OtaPortalPurpose::Stm32Update : OtaPortalPurpose::Esp32Ota);
      } else if (request.purpose == ulsa_update::Purpose::Stm32Update) {
        succeeded = otaManager.preparePortalSession(OtaPortalPurpose::Stm32Update);
      } else {
        succeeded = true;
      }
      if (succeeded) {
        handleWifiPortalToggle();
        succeeded = otaManager.isPortalActive();
      }
      break;

    case ulsa_update::Action::StopPortal:
      handleWifiPortalToggle();
      succeeded = !otaManager.isPortalActive();
      break;

    case ulsa_update::Action::ToggleManualBootloader: {
      const bool wasBootloaderMode = stm32.isBootloaderMode();
      handleStm32ModeToggle();
      if (wasBootloaderMode && !stm32.isBootloaderMode()) {
        bleManager.restartWithLastVerifiedNodeId();
      }
      delay(300);
      succeeded = wasBootloaderMode != stm32.isBootloaderMode();
      break;
    }

    case ulsa_update::Action::RejectBusy:
    case ulsa_update::Action::RejectUnavailable:
    case ulsa_update::Action::RejectPeerConflict:
    case ulsa_update::Action::None:
    case ulsa_update::Action::PublishStatus:
    case ulsa_update::Action::BeginAuthorization:
    case ulsa_update::Action::StartPortal:
    case ulsa_update::Action::CancelPrepared:
    default:
      break;
  }

  updateCoordinator.complete(decision, succeeded, updateRuntimeSnapshot(), millis());
  return true;
}

static void serviceUpdateAuthorizationGuards() {
  uint16_t disconnectedPeer = ulsa_update::INVALID_PEER_HANDLE;
  if (bleManager.consumeDisconnectedPeerHandle(disconnectedPeer) &&
      updateCoordinator.clearIfPeerDisconnected(disconnectedPeer)) {
    otaManager.cancelPortalSession();
    bleManager.unlockMaintenancePeer();
    restoreUpdateAuthorizationPresentation();
  }

  if (updateCoordinator.state() != ulsa_update::State::PendingPhysicalAuthorization &&
      updateCoordinator.state() != ulsa_update::State::Prepared) {
    return;
  }
  const ulsa_update::Purpose purpose = updateCoordinator.purpose();
  const ulsa_update::Request readRequest(
    purpose, ulsa_update::Source::Ble, ulsa_update::Operation::Read,
    updateCoordinator.peerHandle());
  const auto decision = updateCoordinator.plan(
    readRequest, updateRuntimeSnapshot(), millis());
  if (decision.result != ulsa_update::Result::AuthorizationExpired) {
    return;
  }

  otaManager.cancelPortalSession();
  bleManager.unlockMaintenancePeer();
  restoreUpdateAuthorizationPresentation();
  if (purpose == ulsa_update::Purpose::Stm32Update) {
    bleManager.publishStm32UpdateControlStatus(
      BLE_STM32_UPDATE_OP_PREPARE_PORTAL,
      BLE_STM32_UPDATE_RESULT_AUTHORIZATION_EXPIRED);
  } else {
    bleManager.publishOtaControlStatus(
      BLE_OTA_OP_PREPARE_PORTAL,
      BLE_OTA_RESULT_AUTHORIZATION_EXPIRED);
  }
}

static bool isStm32UpdateModeActive() {
  const auto phase = otaManager.getStm32UpdatePhase();
  if (stm32_update::completedUpdateReturnedToMeasurement(
        phase, otaManager.isPortalActive(),
        otaManager.isStm32BootloaderSessionActive())) {
    return false;
  }
  const bool stm32PortalPurpose =
    otaManager.getPortalPurpose() == OtaPortalPurpose::Stm32Update;
  const bool stm32PortalActive =
    otaManager.isPortalActive() && stm32PortalPurpose;

  return stm32PortalActive ||
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
          stm32PortalPurpose);
}

static uint8_t getEsp32DeviceModeCode(ButtonMode mode) {
  if (isStm32UpdateModeActive()) {
    return BLE_DEVICE_MODE_STM32_UPDATE;
  }

  const bool bootloaderActive = stm32.isBootloaderMode();
  const bool portalActive = otaManager.isPortalActive();

  if (bootloaderActive) {
    return BLE_DEVICE_MODE_BOOTLOADER;
  }
  if (portalActive) {
    return BLE_DEVICE_MODE_WIFI_PORTAL;
  }

  switch (mode) {
    case BTN_MODE_I2C_MEASURE:
      return BLE_DEVICE_MODE_I2C_MEASURE;
    case BTN_MODE_COMMAND:
      return BLE_DEVICE_MODE_COMMAND;
    case BTN_MODE_BRIDGE:
      return BLE_DEVICE_MODE_UART_BRIDGE;
    case BTN_MODE_NORMAL:
    default:
      return BLE_DEVICE_MODE_UART_MEASURE;
  }
}

static uint8_t getEsp32DeviceModeFlags(ButtonMode mode) {
  const bool bootloaderActive = stm32.isBootloaderMode();
  const bool portalActive = otaManager.isPortalActive();
  const bool stm32UpdateActive = isStm32UpdateModeActive();
  uint8_t flags = 0;
  if (uartBridge.isEnabled()) {
    flags |= BLE_DEVICE_MODE_FLAG_UART_BRIDGE;
  }
  if (mode == BTN_MODE_I2C_MEASURE) {
    flags |= BLE_DEVICE_MODE_FLAG_I2C_MEASURE;
  }
  if (mode == BTN_MODE_COMMAND) {
    flags |= BLE_DEVICE_MODE_FLAG_COMMAND;
  }
  if (bootloaderActive || otaManager.isStm32BootloaderSessionActive()) {
    flags |= BLE_DEVICE_MODE_FLAG_BOOTLOADER;
  }
  if (portalActive) {
    flags |= BLE_DEVICE_MODE_FLAG_WIFI_PORTAL;
  }
  if (stm32UpdateActive) {
    flags |= BLE_DEVICE_MODE_FLAG_STM32_UPDATE;
  }
  return flags;
}

static void publishEsp32DeviceModeStatus() {
  if (!bleManager.isRunning()) {
    return;
  }
  ButtonMode mode = getButtonMode();
  bleManager.updateDeviceModeStatus(getEsp32DeviceModeCode(mode),
                                    getEsp32DeviceModeFlags(mode));
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
  state.baseMode = systemUiBaseMode(getButtonMode());
  state.bleConnected = bleManager.isConnected();
  state.manualBootloader = stm32.isBootloaderMode();
  state.i2cReturnHoldConfirmed = i2cReturnHoldConfirmed;

  populateSystemUiUpdateState(state, otaManager, updateCoordinator);
  applyPortalClientConnectionPresentation(
    state, otaManager.isAppDrivenPortal(),
    otaManager.isPortalClientConnected());
  if (demoOtaBootHealthFailed()) {
    state.updatePurpose = SystemUiUpdatePurpose::Esp32;
    state.updateStage = SystemUiUpdateStage::Error;
  }
  applyUpdateAuthorizationButtonPresentation(
    state, physicalAuthorizationHoldConfirmed, false);
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

static void restoreUpdateAuthorizationPresentation() {
  refreshSystemUiPresentation();
}

static bool processBleResetControlRequest() {
  uint8_t op = 0;
  if (!bleManager.consumeResetControlRequest(op)) {
    return false;
  }

  switch (op) {
    case BLE_RESET_OP_ESP32:
      if (otaManager.isUpdating()) {
        bleManager.publishResetControlStatus(op, BLE_RESET_RESULT_BUSY);
        return true;
      }
      bleManager.publishResetControlStatus(op,
                                           BLE_RESET_RESULT_OK,
                                           BLE_RESET_FLAG_ESP32_REBOOTING);
      esp_task_wdt_reset();
      delay(180);
      ESP.restart();
      return true;

    case BLE_RESET_OP_STM32:
      if (otaManager.isUpdating()) {
        bleManager.publishResetControlStatus(op, BLE_RESET_RESULT_BUSY);
        return true;
      }
      bleManager.publishResetControlStatus(op,
                                           BLE_RESET_RESULT_OK,
                                           BLE_RESET_FLAG_STM32_RESETTING);
      esp_task_wdt_reset();
      delay(80);
      stm32.resetToNormal();
      if (getButtonMode() == BTN_MODE_I2C_MEASURE) {
        windSensor.setSource(WIND_SOURCE_I2C);
        refreshSystemUiPresentation();
      }
      bleManager.updateStm32FirmwareVersion(true);
      publishEsp32DeviceModeStatus();
      bleManager.publishResetControlStatus(op, BLE_RESET_RESULT_OK);
      return true;

    case BLE_RESET_OP_READ:
      bleManager.publishResetControlStatus(op, BLE_RESET_RESULT_OK);
      return true;

    default:
      bleManager.publishResetControlStatus(op, BLE_RESET_RESULT_INVALID_OP);
      return true;
  }
}

static void restoreDefaultModeAfterBootloaderComplete() {
  ledCtrl.blinkBootloaderComplete(3);

  stm32.resetToNormal();
  uartBridge.setEnabled(false);
  setButtonMode(BTN_MODE_I2C_MEASURE);
  windSensor.setSource(WIND_SOURCE_I2C);
  windSensor.setDebugOutput(false);

  // STM32 reset直後はI2Cの公開NODE_IDがまだ応答しないことがある。
  // BLEを停止したままにせず、直前にSTM32から確認済みのIDで先に復帰する。
  bleManager.restartWithLastVerifiedNodeId();
  synchronizeBleNodeIdentity(true);

  refreshSystemUiPresentation();
  publishEsp32DeviceModeStatus();
}

// ============================================
// セットアップ
// ============================================
void setup() {
  // M5Unified初期化
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.fallback_board = m5::board_t::board_M5StampC3U;
  cfg.output_power = false;  // 電源管理ログを無効化
  M5.begin(cfg);

  // LED初期化（起動直後のフィードバック）
  ledCtrl.begin();
  ledCtrl.setMode(MODE_I2C_MEASURE);  // デフォルトI2C計測状態の水色点滅
  const bool bootRecoveryRequested = detectBootRecoveryRequest(
    SystemUiUpdatePurpose::Esp32);
  if (!bootRecoveryRequested) ledCtrl.setMode(MODE_I2C_MEASURE);

  // M5Unifiedのログ出力を完全に無効化
  // USB-CDC透過ブリッジでは、ログ出力がSTM32通信を妨害する
  M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_NONE);

  // USB-CDCバッファサイズ拡大（STM32ブートローダーモード用）
  Serial.setRxBufferSize(1024);
  Serial.setTxBufferSize(1024);

  // UART1初期化（通常モード: 8N1 - 風速計データ受信用）
  Serial1.setRxBufferSize(1024);
  Serial1.setTxBufferSize(1024);
  Serial1.begin(UART1_BAUD_RATE, UART1_CONFIG_NORMAL, UART1_RX_PIN, UART1_TX_PIN);

  // I2C初期化
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQUENCY);
  Wire.setClock(I2C_FREQUENCY);
  Wire.setTimeOut(ULSA_EVO_I2C_TIMEOUT_MS);
  configureI2cDriveStrength();

  // SPI初期化
  SPI.begin(SPI_CLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CS_PIN);
  pinMode(SPI_CS_PIN, OUTPUT);
  digitalWrite(SPI_CS_PIN, HIGH);
  configureSpiDriveStrength();

  // モジュール初期化
  stm32.begin();

  windSensor.begin(&uartBridge);  // UartBridge経由でWindSensorを初期化
  windSensor.beginI2c(&Wire, ULSA_EVO_I2C_ADDR_DEFAULT);
  windSensor.setSource(WIND_SOURCE_I2C);
  windSensor.setDebugOutput(false);
  bleManager.setI2cConfigClient(&windSensor.getI2cClient());
  uartBridge.begin(&Serial, &Serial1);
  uartBridge.setEnabled(false);  // デフォルトはI2C計測モードのためUART透過は無効

  // BootloaderDebugger初期化とUartBridgeに設定 - D入力デバッグ機能を無効化
  // bootDebugger.begin();
  // uartBridge.setDebugger(&bootDebugger);

  // STM32にUARTブリッジへの参照を設定（resetToBootloader内でブリッジを即座に開始できるように）
  stm32.setUartBridge(&uartBridge);

  // RTC初期化。PCF8563のVL/STOP異常時にコンパイル時刻で自動上書きしない。
  // 時刻の回復はBLE CTSまたは`rtc set`の明示操作だけで行う。
  (void)rtc.begin();

  // SDカード初期化
  if (sdLogger.begin(&rtc)) {
    // This is the only automatic-start point. SdLogger::begin() is also used
    // by remount paths, which must remain stopped until explicitly resumed.
    if (sdLogger.isAutoStartEnabled()) {
      (void)sdLogger.resumeLogging();
    }
    // LOG_SD("Ready, logging at %dHz\n", sdLogger.getLogRate());
    // LOG_SD("%dMB free / %dMB total (%d%% used)\n",
    //        sdLogger.getFreeSpaceMB(),
    //        sdLogger.getTotalSpaceMB(),
    //        sdLogger.getUsagePercent());
  } else {
    // LOG_SD("%s\n", sdLogger.getStateString());
  }
  ledCtrl.updateSdLoggingState(
    sdLogger.isLoggingEnabled(),
    !sdLogger.isLoggingEnabled() &&
      sdLogger.getStopReason() == SD_STOP_USER_DISABLED);

  // TaskStats初期化（コマンド経由のみで表示、自動出力なし）
  g_taskStats = new TaskStatsManager(0);  // 引数0で定期出力を無効化
  g_taskStats->begin();

  // コマンド登録
  cmdHandler.registerCommand("rtc", cmdRtc, "UTC RTC and IANA timezone control");
  cmdHandler.registerCommand("led", cmdLed, "LED brightness control (get/brightness)");
  cmdHandler.registerCommand("sys", cmdSys, "System info (info/memory/tasks/bootloader/reboot)");
  cmdHandler.registerCommand("sd", cmdSd, "SD management (status/rate/info/stats/resume/remount/format YES)");
  cmdHandler.registerCommand("wind", cmdWind, "Wind sensor data and I2C diagnostics");
  cmdHandler.registerCommand("ble", cmdBle, "BLE control (status/info/start/stop)");
  cmdHandler.registerCommand("help", cmdHelp, "Show available commands");
  cmdHandler.registerCommand("exit", cmdExit, "Exit command mode");

  // OTA初期化（WiFi接続は後から可能）
  otaManager.begin(0, &stm32, &windSensor.getI2cClient());
  bleManager.setOtaManager(&otaManager);
  if (synchronizeBleNodeIdentity(true)) {
    publishEsp32DeviceModeStatus();
  }
  if (bootRecoveryRequested) {
    (void)updateCoordinator.queueBootRecovery(ulsa_update::Purpose::Esp32Ota);
  }
  beginDemoOtaBootHealth();

  // OTAコールバック設定（LED制御）
  otaManager.onStartCallback = []() {
    // OTA開始時: オレンジ進捗点滅
    refreshSystemUiPresentation();
    ledCtrl.setOtaProgress(0);
    ledCtrl.update();
  };
  otaManager.onProgressCallback = [](uint8_t progress) {
    // WebServer::handleClient()内ではESP32/STM32 uploadとpackage verificationが
    // 同期的に進むため、main loopへ戻らない間もresolverとLEDをサービスする。
    refreshSystemUiPresentation();
    ledCtrl.setOtaProgress(progress);
    ledCtrl.update();
  };
  otaManager.onEndCallback = []() {
    // OTA完了後は自動再起動するのでLED設定不要
  };
  otaManager.onErrorCallback = [](const char* error) {
    (void)error;
    refreshSystemUiPresentation();
  };

  // 起動メッセージ
  // LOG_INFO("ULSA", "\n=== ULSA EVO Wind Sensor ===");
  // LOG_INFO("ULSA", "BLE device: ULSA EVO #0 (auto-updated from sensor)");
  // LOG_INFO("ULSA", "Short press: mode toggle (BRIDGE -> COMMAND -> NORMAL)");
  // LOG_INFO("ULSA", "Long press(2s): STM32 bootloader");
  // LOG_INFO("ULSA", "Long press(5s): WiFi setup portal");

  // ウォッチドッグタイマー初期化
  // メインループがフリーズした場合に自動再起動
  esp_task_wdt_init(WDT_TIMEOUT_SEC, WDT_PANIC_ENABLED);
  esp_task_wdt_add(NULL);  // 現在のタスク（loop）を監視対象に追加
  // LOG_INFO("ULSA", "Watchdog: %d sec timeout\n", WDT_TIMEOUT_SEC);
}

// BLE接続状態追跡
static bool lastBleConnected = false;

// ============================================
// メインループ
// ============================================
void loop() {
  // M5とボタン処理は常に実行（モード切替のため）
  M5.update();
  processButton();
  const ButtonMode inputMode = getButtonMode();
  sdLogger.setInputActive(
    !stm32.isBootloaderMode() && !otaManager.isPortalActive() &&
    !otaManager.isStm32BootloaderSessionActive() &&
    (inputMode == BTN_MODE_NORMAL || inputMode == BTN_MODE_I2C_MEASURE));

  // アプリ主導STM32更新中はbackground writer taskがSerial1を占有する。
  // 既存の透過bootloader bridgeにACK/NACKを消費させず、HTTP statusだけ処理する。
  if (otaManager.isStm32BootloaderSessionActive()) {
    otaManager.update();
    serviceLedController();
    esp_task_wdt_reset();
    yield();
    return;
  }

  // STM32ブートローダーモード中は他の処理をスキップ
  // 完全透過UARTブリッジのみ実行
  if (stm32.isBootloaderMode()) {
    // UARTブリッジ処理（ブートローダーモード専用）
    // STM32ブートローダープロトコルに準拠した1バイトずつ処理
    uartBridge.processBootloaderMode();
    ledCtrl.update();  // LED更新も実行

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

    // watchdogリセット（念のため）
    // resetToBootloader()でwatchdogを無効化しているが、
    // 何らかの理由で無効化が失敗した場合のフェイルセーフ
    esp_task_wdt_reset();

    // yield()でタスクスケジューラに制御を返す（delayなしで最大速度で処理）
    yield();
    return;  // 他の処理は一切行わない
  }
  bootloaderGoCompletePending = false;
  bootloaderLastSnifferBytes = 0;
  bootloaderLastSnifferActivityMs = 0;

  // Manual ROM bootloader bridging is a timing-sensitive fast path. Keep
  // coordinator, OTA-health, BLE publication, and LED state resolution out of
  // that loop so every iteration remains available to the byte relay.
  serviceDemoOtaBootHealth(bleManager.isRunning());
  serviceUpdateAuthorizationGuards();
  updateCoordinator.reconcile(updateRuntimeSnapshot(), millis());
  (void)serviceQueuedButtonUpdateRequest();
  refreshSystemUiPresentation();
  publishEsp32DeviceModeStatus();

  // WiFi ポータルモード: BLEは停止中なのでBLE関連処理をスキップ
  if (otaManager.isPortalActive()) {
    const auto stateBeforeUpdate = updateCoordinator.state();
    // WiFiポータル処理のみ実行
    otaManager.update();
    updateCoordinator.reconcile(updateRuntimeSnapshot(), millis());
    if (stateBeforeUpdate == ulsa_update::State::PortalActive &&
        updateCoordinator.state() == ulsa_update::State::Idle) {
      if (!synchronizeBleNodeIdentity(true)) {
        bleManager.restartWithLastVerifiedNodeId();
      }
      refreshSystemUiPresentation();
      publishEsp32DeviceModeStatus();
    }
    serviceLedController();

    // ウォッチドッグタイマーリセット
    esp_task_wdt_reset();
    yield();
    return;
  }

  else {
    if (processBleResetControlRequest()) {
      esp_task_wdt_reset();
      yield();
      return;
    }
    if (processBleOtaControlRequest()) {
      esp_task_wdt_reset();
      yield();
      return;
    }
    if (processBleStm32UpdateControlRequest()) {
      esp_task_wdt_reset();
      yield();
      return;
    }
    bleManager.processRtcTimezoneRequest();
    bleManager.serviceRtcTimeCaches();
    bleManager.processI2cConfigRequest();
    synchronizeBleNodeIdentity();
    bleManager.processSdLogControlRequest();
    bleManager.processSdLogSettingsRequest();
    bleManager.processLedBrightnessRequest();
    bleManager.processLedWindReactiveRequest();
    static uint32_t identifyConnectionSequence = 0;
    if (bleManager.consumeDeviceIdentifyRequest(identifyConnectionSequence)) {
      ledCtrl.startIdentifyBlink();
    }
    if (ledCtrl.isIdentifyBlinkActive() && bleManager.isConnected() &&
        bleManager.getConnectionEventSequence() != identifyConnectionSequence) {
      ledCtrl.stopIdentifyBlink();
    }

    // ============================================
    // コマンドモードチェック
    // ============================================
    ButtonMode currentButtonMode = getButtonMode();
    if (currentButtonMode == BTN_MODE_COMMAND) {
      // コマンドモード: USB-CDCからコマンドを受信
      static bool welcomeShown = false;
      if (!welcomeShown) {
        cmdHandler.printWelcome();
        welcomeShown = true;
      }

      // コマンド入力処理
      cmdHandler.processUsbInput();

      serviceLedController();
      esp_task_wdt_reset();
      yield();
      return;  // コマンドモード中は他の処理をスキップ
    }

    // ============================================
    // 通常モード: フル機能動作
    // ============================================

    // USB-CDCからのデバッグコマンドをチェック - D入力デバッグ機能を無効化
    // if (Serial.available()) {
    //   char cmd = Serial.read();
    //   if (cmd == 'D' || cmd == 'd') {
    //     // 'D' キーでデバッグログ表示
    //     Serial.println("\n=== Bootloader Debug Log ===");
    //     bootDebugger.printDataLog();
    //     Serial.println("============================\n");
    //   }
    // }

    // UART1-USB CDCブリッジ処理（8N1, 115200bps）
    // 注: WindSensorと同じUART1を共有するため、
    //     ブリッジが有効な場合は風速計データ受信に影響する可能性がある
    uartBridge.process();

    // USB-CDCからのBOOTコマンドをチェック
    uartBridge.checkBootCommand();
    if (uartBridge.isBootCommandReceived()) {
      uartBridge.clearBootCommand();
      (void)updateCoordinator.queueButtonManualBootloaderToggle();
      esp_task_wdt_reset();
      return;
    }

    // 風速計データ処理
    bool shouldUpdateWind = (currentButtonMode == BTN_MODE_NORMAL ||
                             currentButtonMode == BTN_MODE_I2C_MEASURE);
    if (shouldUpdateWind && windSensor.update()) {
      // 新しいデータを受信したらBLEで更新
      const WindData& data = windSensor.getData();
      bleManager.updateWindData(data);
      ledCtrl.updateWindReactiveWindSpeed(data.windSpeed, data.isValid);

      // UARTブリッジがOFFの場合は1行フォーマットで表示
      if (!uartBridge.isEnabled()) {
        windSensor.printDataOneLine();
      }

      // I2C sourceでは、保存周期を新規DATA_SEQ周期の整数倍へ揃える。
      // 旧NVS値やCLI経由の任意周期も、実サンプルを飛ばして近似しない。
      bool sourceCadenceReady = true;
      if (windSensor.getSource() == WIND_SOURCE_I2C) {
        const uint32_t sourceIntervalMs =
            windSensor.getI2cClient().getStats().i2cOutputIntervalMs;
        sourceCadenceReady = sdLogger.synchronizeLogIntervalToSource(sourceIntervalMs);
      }

      // SD書込み完了時刻ではなく、新規サンプルの時刻で周期を判定する。
      // I2C DATA_SEQ から復元できる場合は、受信ジッタを含まないSTM32側の
      // 出力時刻を使う。UART/Simulationなどでは従来どおり受信時刻へフォールバックする。
      const uint32_t logSampleTimestampMs = data.sourceTimestampValid
        ? data.sourceTimestamp
        : data.timestamp;
      if (sourceCadenceReady &&
          sdLogger.shouldLogSample(logSampleTimestampMs)) {
        // A full queue records a dropped row; storage faults and bounded retry
        // belong to the writer and must not stop measurement acquisition.
        (void)sdLogger.log(data);
      }
    }

    // パースエラー検出時の表示（ブリッジOFF時のみ）
    if (currentButtonMode == BTN_MODE_NORMAL && !uartBridge.isEnabled()) {
      const ParseStats& stats = windSensor.getParseStats();
      static uint32_t lastErrorTime = 0;

      // エラーが発生し、かつ前回の表示から1秒以上経過していたら表示
      if (stats.lastErrorTime > lastErrorTime && (millis() - stats.lastErrorTime < 1000)) {
        // デバッグ出力は削除（CORE_DEBUG_LEVEL=0で出力制限のため）
        lastErrorTime = stats.lastErrorTime;
      }
    }

    // SDロガーのレート制限更新
    sdLogger.update();

    // BLE接続状態の変化を検出してLEDモード更新
    bool currentBleConnected = bleManager.isConnected();
    if (currentBleConnected != lastBleConnected) {
      lastBleConnected = currentBleConnected;
      refreshSystemUiPresentation();
      // LOG_BLE("%s\n", currentBleConnected ? "Connected" : "Disconnected");
    }

    serviceLedController();
  }

  // LED デバッグ出力（5秒ごと）
  uint32_t now = millis();
  if (now - lastLedDebugTime >= LED_DEBUG_INTERVAL_MS) {
    lastLedDebugTime = now;
    uint32_t currentColor = ledCtrl.getCurrentColor();
    // LOG_LED("Mode: %s | Color: 0x%06X | BLE: %s | STM32: %s | OTA: %s\n",
    //   ledCtrl.getCurrentModeName(),
    //   currentColor,
    //   bleManager.isConnected() ? "Connected" : "Disconnected",
    //   stm32.isBootloaderMode() ? "Bootloader" : "Normal",
    //   otaManager.isPortalActive() ? "Portal Active" : "Idle"
    // );
  }

  // ウォッチドッグタイマーリセット
  // 正常動作中は定期的にリセットしてタイムアウトを防止
  esp_task_wdt_reset();

  // delay不要: UARTハードウェアバッファとタスクスケジューラが自動処理
  // yield()も不要: 次のloop()が即座に実行され、最大スループットを実現
}
