/**
 * @file debug_config.h
 * @brief デバッグ出力制御設定
 * @date 2025-12-07
 *
 * ログ出力の有効/無効をマクロで制御
 */

#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

// ============================================
// デバッグ出力制御マクロ
// ============================================

// 全体ログ出力制御
// 定義されている場合のみログ出力有効
#define ENABLE_DEBUG_OUTPUT

// 個別モジュールログ制御
#define ENABLE_RTC_DEBUG       // RTC関連ログ
#define ENABLE_SD_DEBUG        // SDカード関連ログ
#define ENABLE_BLE_DEBUG       // BLE関連ログ
#define ENABLE_OTA_DEBUG       // OTA関連ログ
#define ENABLE_BUTTON_DEBUG    // ボタン関連ログ
#define ENABLE_WIND_DEBUG      // 風速センサー関連ログ
#define ENABLE_LED_DEBUG       // LED関連ログ

// ============================================
// ログ出力マクロ定義
// ============================================

#ifdef ENABLE_DEBUG_OUTPUT

// 汎用ログマクロ
#define LOG_DEBUG(module, fmt, ...)    M5.Log.printf("[" module "] " fmt, ##__VA_ARGS__)
#define LOG_INFO(module, fmt, ...)     M5.Log.printf("[" module "] " fmt, ##__VA_ARGS__)
#define LOG_WARN(module, fmt, ...)     M5.Log.printf("[" module "] WARN: " fmt, ##__VA_ARGS__)
#define LOG_ERROR(module, fmt, ...)    M5.Log.printf("[" module "] ERROR: " fmt, ##__VA_ARGS__)

// モジュール別ログマクロ
#ifdef ENABLE_RTC_DEBUG
#define LOG_RTC(fmt, ...)              LOG_DEBUG("RTC", fmt, ##__VA_ARGS__)
#else
#define LOG_RTC(fmt, ...)
#endif

#ifdef ENABLE_SD_DEBUG
#define LOG_SD(fmt, ...)               LOG_DEBUG("SD", fmt, ##__VA_ARGS__)
#else
#define LOG_SD(fmt, ...)
#endif

#ifdef ENABLE_BLE_DEBUG
#define LOG_BLE(fmt, ...)              LOG_DEBUG("BLE", fmt, ##__VA_ARGS__)
#else
#define LOG_BLE(fmt, ...)
#endif

#ifdef ENABLE_OTA_DEBUG
#define LOG_OTA(fmt, ...)              LOG_DEBUG("OTA", fmt, ##__VA_ARGS__)
#else
#define LOG_OTA(fmt, ...)
#endif

#ifdef ENABLE_BUTTON_DEBUG
#define LOG_BUTTON(fmt, ...)           LOG_DEBUG("Button", fmt, ##__VA_ARGS__)
#else
#define LOG_BUTTON(fmt, ...)
#endif

#ifdef ENABLE_WIND_DEBUG
#define LOG_WIND(fmt, ...)             LOG_DEBUG("Wind", fmt, ##__VA_ARGS__)
#define LOG_STATS(fmt, ...)            LOG_DEBUG("Stats", fmt, ##__VA_ARGS__)
#else
#define LOG_WIND(fmt, ...)
#define LOG_STATS(fmt, ...)
#endif

#ifdef ENABLE_LED_DEBUG
#define LOG_LED(fmt, ...)              LOG_DEBUG("LED", fmt, ##__VA_ARGS__)
#else
#define LOG_LED(fmt, ...)
#endif

#else // !ENABLE_DEBUG_OUTPUT

// デバッグ出力無効時は空マクロ
#define LOG_DEBUG(module, fmt, ...)
#define LOG_INFO(module, fmt, ...)
#define LOG_WARN(module, fmt, ...)
#define LOG_ERROR(module, fmt, ...)

#define LOG_RTC(fmt, ...)
#define LOG_SD(fmt, ...)
#define LOG_BLE(fmt, ...)
#define LOG_OTA(fmt, ...)
#define LOG_BUTTON(fmt, ...)
#define LOG_WIND(fmt, ...)
#define LOG_STATS(fmt, ...)
#define LOG_LED(fmt, ...)

#endif // ENABLE_DEBUG_OUTPUT

#endif // DEBUG_CONFIG_H