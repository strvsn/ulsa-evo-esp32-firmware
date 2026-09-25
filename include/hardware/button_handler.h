/**
 * @file button_handler.h
 * @brief ボタン入力処理モジュールヘッダ
 * @date 2025-12-07
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <Arduino.h>
#include "led_controller.h"
#include "stm32_bootloader.h"
#include "ble_manager.h"
#include "rtc_manager.h"
#include "sd_logger.h"
#include "ota_manager.h"
#include "button_gesture_classifier.h"

// ============================================
// ボタン操作閾値
// ============================================
static const uint32_t SHORT_RELEASE_MAX_MS = 1000;
static const uint32_t I2C_RETURN_PRESS_MS = 2000;
static const uint32_t OTA_AUTHORIZATION_PRESS_MS = 3000;
static const uint32_t LONG_HOLD_BOUNDARY_MS = 6000;
static const uint32_t MULTI_CLICK_WINDOW_MS = 350; // 次の短押し開始までの間隔

// ============================================
// 通常時のモード（2回／3回クリックで直接選択）
// ============================================
enum ButtonMode {
    BTN_MODE_NORMAL,      // 通常モード（風速計データ受信、BLE送信、SD記録）
    BTN_MODE_BRIDGE,      // UARTブリッジモード（STM32書込み用）
    BTN_MODE_I2C_MEASURE, // I2C計測モード（ULSA EVO I2C読み取り）
    BTN_MODE_COMMAND      // コマンドモード（USB経由でESP32設定変更）
};

// ============================================
// 関数プロトタイプ
// ============================================

/**
 * @brief 現在のボタンモードを取得
 * @return 現在のモード
 */
ButtonMode getButtonMode();

/**
 * @brief ボタンモードを設定（外部から変更可能）
 * @param mode 設定するモード
 */
void setButtonMode(ButtonMode mode);

/**
 * @brief ボタン入力処理（毎ループ呼び出し）
 */
void processButton();

/**
 * @brief WiFiポータル起動/停止切替
 */
void handleWifiPortalToggle();

/**
 * @brief STM32ブートローダーモード切替
 */
void handleStm32ModeToggle();

/**
 * @brief 明示的にボタンモードを選択する（記録中の非I2C遷移は拒否）
 */
void handleModeSelection(ButtonMode target);

/**
 * @brief SDログを開始/停止する。
 *
 * I2C計測中の物理単押し用。BLE SD Log Controlと同じSdLogger APIを使用し、
 * 成功・失敗を含む状態をBLEへ即時通知する。
 */
void handleSdLoggingToggle();

#endif // BUTTON_HANDLER_H
