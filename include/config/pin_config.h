/**
 * @file pin_config.h
 * @brief ESP32-C3 (M5Stamp C3U) ピンアサイン定義
 * @date 2025-12-03
 */

#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// ============================================
// UART1 ピンアサイン (STM32通信用)
// ============================================
#define UART1_TX_PIN    3
#define UART1_RX_PIN    10

// UART1ボーレート設定
#define UART1_BAUD_RATE 115200

// 通常モード: 8N1 (風速計データ受信用)
//   - 8bit データ
//   - No パリティ
//   - 1 ストップビット
#define UART1_CONFIG_NORMAL     SERIAL_8N1  // 8bit, No parity, 1 stop bit

// STM32 Bootloaderモード: 8E1 (AN2606/AN3155準拠)
// 重要: STM32 Bootloaderは以下の設定が必須
//   - 115200 bps
//   - 8bit データ
//   - Even パリティ (偶数パリティ)
//   - 1 ストップビット
// この設定を変更するとファームウェア書き込みに失敗します
#define UART1_CONFIG_BOOTLOADER SERIAL_8E1  // 8bit, Even parity, 1 stop bit

// ============================================
// I2C (PORT A) ピンアサイン
// ============================================
#define I2C_SDA_PIN     1
#define I2C_SCL_PIN     0
#define I2C_FREQUENCY   400000  // 400kHz (ULSA EVO I2C高速評価条件)

// ULSA EVO I2Cスレーブ設定
#define ULSA_EVO_I2C_ADDR_DEFAULT       0x50
#define ULSA_EVO_I2C_TIMEOUT_MS         50
#define ULSA_EVO_I2C_POLL_INTERVAL_DEFAULT_MS 100  // 10Hz fallback
#define ULSA_EVO_I2C_POLL_INTERVAL_MIN_MS     10
#define ULSA_EVO_I2C_POLL_INTERVAL_MAX_MS     1000
#define ULSA_EVO_I2C_POLL_OVERSAMPLE_FACTOR    2
#define ULSA_EVO_I2C_POLL_INTERVAL_MS         ULSA_EVO_I2C_POLL_INTERVAL_DEFAULT_MS

// ============================================
// SPI ピンアサイン
// ============================================
#define SPI_MOSI_PIN    7
#define SPI_MISO_PIN    6
#define SPI_CS_PIN      5
#define SPI_CLK_PIN     4
#define SPI_FREQUENCY   8000000  // 8MHz (安定性確認済み)

// ============================================
// 内蔵LED (NeoPixel)
// ============================================
#define LED_NEOPIXEL_PIN 2

// ============================================
// ボタン
// ============================================
#define BUTTON_PIN      9

// ============================================
// STM32 Bootloader制御
// ============================================
// 重要: PMOS/NMOS経由で反転ロジック！
// GPIO8  → PMOS → BOOT0 (LOW=ブートモード, HIGH=通常モード)
// GPIO20 → NMOS → NRST  (HIGH=リセット, LOW=通常動作)
#define STM32_BOOT0_PIN  8   // STM32 BOOT0制御 (PMOS経由 - 反転ロジック)
#define STM32_RESET_PIN  20  // STM32 NRST制御  (NMOS経由 - 反転ロジック)

// ============================================
// その他のGPIO (未割当)
// ============================================
// GPIO18 - USB D-
// GPIO19 - USB D+
// GPIO21 - UART0 TX (デバッグ用)

// ============================================
// デバッグ設定
// ============================================
// 風速計シミュレーションモード
// true: UARTの代わりに模擬データを生成（デバッグ用）
// false: 通常動作（UARTから実データを受信）
// 重要: リリース時はfalseに設定すること
#define WIND_SENSOR_SIMULATION_ENABLED  false

// シミュレーション設定
#define WIND_SIM_NODE_ID          0       // 模擬ノードID
#define WIND_SIM_UPDATE_INTERVAL  100     // 更新間隔 [ms] (10Hz相当)

#endif // PIN_CONFIG_H
