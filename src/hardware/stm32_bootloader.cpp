/**
 * @file stm32_bootloader.cpp
 * @brief STM32 Bootloaderモード制御モジュール実装
 * @date 2025-12-03
 */

#include "stm32_bootloader.h"
#include "uart_bridge.h"
#include <esp_task_wdt.h>

STM32Bootloader::STM32Bootloader()
  : _bootloaderMode(false)
  , _uartBridge(nullptr) {
}

void STM32Bootloader::begin() {
  // ============================================
  // ハードウェア構成:
  // GPIO8  → PMOS → STM32 BOOT0 (反転ロジック)
  // GPIO20 → NMOS → STM32 NRST  (反転ロジック)
  // ============================================
  
  // RESET ピン初期化（まず HIGH に設定してリセット状態を確保）
  // GPIO20=HIGH → NMOS ON → NRST=LOW (リセット状態)
  pinMode(STM32_RESET_PIN, OUTPUT);
  digitalWrite(STM32_RESET_PIN, HIGH);
  delay(RESET_PULSE_MS);  // リセット状態を確保
  
  // BOOT0 ピン初期化（通常モード: BOOT0=LOW）
  // GPIO8=HIGH → PMOS OFF → BOOT0=LOW (通常モード)
  pinMode(STM32_BOOT0_PIN, OUTPUT);
  digitalWrite(STM32_BOOT0_PIN, HIGH);
  delay(BOOT0_STABLE_MS);  // BOOT0 安定化待機
  
  // RESET ピンをリリース（通常状態: NRST=HIGH）
  // GPIO20=LOW → NMOS OFF → NRST=HIGH (通常動作)
  digitalWrite(STM32_RESET_PIN, LOW);
  delay(RESET_WAIT_MS);  // STM32 起動待機（AN2606準拠: 500ms）
  
  _bootloaderMode = false;
}

void STM32Bootloader::resetToNormal() {
  // watchdogを再度有効化（通常モード復帰）
  // エラーは無視（既に追加されている場合もある）
  esp_task_wdt_add(NULL);
  
  // BOOT0 を LOW に設定（通常モード）
  // GPIO8=HIGH → PMOS OFF → BOOT0=LOW
  setBoot0(false);  // false → GPIO8=HIGH (PMOS経由で反転)
  delay(BOOT0_STABLE_MS);  // BOOT0 安定化
  
  // STM32 をリセット
  reset();
  
  // UART1 を通常モード（8N1）に切り替え
  // 風速計データ受信用の設定
  Serial1.end();
  delay(100);  // UART終了待機
  Serial1.begin(UART1_BAUD_RATE, UART1_CONFIG_NORMAL, UART1_RX_PIN, UART1_TX_PIN);
  delay(100);  // UART初期化完了待機
  
  _bootloaderMode = false;
}

void STM32Bootloader::resetToBootloader() {
  // ============================================
  // 重要: UART初期化をSTM32リセット前に完了させる
  // AN2606/AN3155準拠の正しいシーケンス:
  // 1. STM32を一度リセット状態で保持（風速計データ送信を停止）
  // 2. UARTを8E1で初期化（ESP32側の準備完了）
  // 3. BOOT0=HIGHに設定
  // 4. STM32をリセット解除 → ブートローダー起動
  // 
  // 注意: ブートローダーモードではwatchdogを無効化
  //       （STM32書き込みに任意の時間がかかるため）
  // ============================================
  
  // 最初にwatchdogを無効化（長時間処理を開始する前）
  // エラーは無視（既に削除されている場合もある）
  esp_task_wdt_delete(NULL);
  
  // ステップ0: STM32を先にリセット状態で保持（風速計データ送信を完全停止）
  // GPIO20=HIGH → NMOS ON → NRST=LOW (リセット状態)
  digitalWrite(STM32_RESET_PIN, HIGH);
  delay(100);                         // リセット状態を確実に保持
  
  // ステップ1: UART1を8E1で再初期化（STM32がリセット中に実行）
  Serial1.flush();                    // 送信完了待機
  Serial.flush();                     // USB-CDC送信完了待機
  
  // 既存バッファをクリア
  while (Serial1.available()) Serial1.read();
  while (Serial.available()) Serial.read();
  
  Serial1.end();                      // UART終了
  delay(50);                          // ハードウェア停止待機
  Serial1.setRxBufferSize(1024);
  Serial1.setTxBufferSize(1024);
  
  // 8E1モードで再初期化（STM32ブートローダープロトコル準拠 - AN2606/AN3155）
  // 重要: Even parity必須 - STM32ブートローダーの仕様
  // ESP32はパリティビットを含めて9ビットとして透過的に転送
  Serial1.begin(UART1_BAUD_RATE, UART1_CONFIG_BOOTLOADER, UART1_RX_PIN, UART1_TX_PIN);
  delay(100);                         // UART初期化完了待機
  
  // UART再初期化直後に複数回バッファクリア（初期化時のゴミデータを除去）
  for (int i = 0; i < 3; i++) {
    while (Serial1.available()) Serial1.read();
    while (Serial.available()) Serial.read();
    delay(20);
  }
  
  // ステップ2: BOOT0をHIGHに設定（ブートローダーモード指定）
  // STM32はまだリセット中なので、BOOT0を安全に設定できる
  setBoot0(true);                     // GPIO8=LOW → PMOS ON → BOOT0=HIGH
  delay(BOOT0_STABLE_MS);             // BOOT0安定化（10ms）
  
  // ステップ3: STM32をリセット解除（ブートローダー起動）
  // GPIO20=LOW → NMOS OFF → NRST=HIGH (リセット解除 → STM32起動開始)
  digitalWrite(STM32_RESET_PIN, LOW);
  
  // STM32ブートローダー起動待機（最小限の500ms）
  // バッファクリアはprocessBootloaderMode()内で実施
  delay(500);
  
  _bootloaderMode = true;
  
  // UartBridgeのバッファクリアカウンタをリセット
  // processBootloaderMode()内で最初の100回のループでバッファクリアを実施
  if (_uartBridge != nullptr) {
    _uartBridge->resetBootloaderState();
  }
  
  // 能動ブリッジは実装しない
  // loop()内のuartBridge.process()が即座に実行されるように、
  // この関数は短時間で完了する
}

void STM32Bootloader::resetToCustomBootloader() {
  esp_task_wdt_delete(NULL);
  digitalWrite(STM32_RESET_PIN, HIGH);
  delay(20);
  setBoot0(false);
  delay(BOOT0_STABLE_MS);
  Serial1.flush();
  while (Serial1.available()) Serial1.read();
  Serial1.end();
  delay(20);
  // HardwareSerial rejects buffer resizing after begin(). A 1 KiB ring is
  // sufficient because requests are streamed and loader replies are 52 bytes.
  Serial1.setRxBufferSize(1024);
  Serial1.setTxBufferSize(1024);
  Serial1.begin(UART1_BAUD_RATE, UART1_CONFIG_NORMAL, UART1_RX_PIN, UART1_TX_PIN);
  delay(20);
  while (Serial1.available()) Serial1.read();
  digitalWrite(STM32_RESET_PIN, LOW);
  delay(25);
  _bootloaderMode = true;
}

void STM32Bootloader::finishCustomBootloaderSession() {
  _bootloaderMode = false;
  esp_task_wdt_add(NULL);
}

void STM32Bootloader::reset() {
  // リセットパルス（NRST: HIGH → LOW → HIGH）
  // GPIO20=HIGH → NMOS ON → NRST=LOW (リセット状態)
  digitalWrite(STM32_RESET_PIN, HIGH);
  delay(RESET_PULSE_MS);
  // GPIO20=LOW → NMOS OFF → NRST=HIGH (通常動作)
  digitalWrite(STM32_RESET_PIN, LOW);
  delay(RESET_WAIT_MS);
}

void STM32Bootloader::setBoot0(bool state) {
  // PMOS 経由の反転ロジック:
  // state=true  (BOOT0=HIGH) → GPIO8=LOW  → PMOS ON
  // state=false (BOOT0=LOW)  → GPIO8=HIGH → PMOS OFF
  digitalWrite(STM32_BOOT0_PIN, state ? LOW : HIGH);
}

bool STM32Bootloader::isBootloaderMode() const {
  return _bootloaderMode;
}

void STM32Bootloader::setBootloaderModeFlag(bool mode) {
  _bootloaderMode = mode;
}

void STM32Bootloader::setUartBridge(UartBridge* bridge) {
  _uartBridge = bridge;
}
