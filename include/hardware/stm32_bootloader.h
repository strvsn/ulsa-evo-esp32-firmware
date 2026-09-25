/**
 * @file stm32_bootloader.h
 * @brief STM32 Bootloaderモード制御モジュール
 * @date 2025-12-03
 */

#ifndef STM32_BOOTLOADER_H
#define STM32_BOOTLOADER_H

#include <Arduino.h>
#include "pin_config.h"

// 前方宣言
class UartBridge;

/**
 * @brief STM32 Bootloader制御クラス
 * 
 * ハードウェア構成（重要！）:
 * - ESP32 GPIO8  → PMOS → STM32 BOOT0 (反転ロジック)
 * - ESP32 GPIO20 → NMOS → STM32 NRST  (反転ロジック)
 * 
 * 反転ロジックの動作:
 * [BOOT0制御]
 * - GPIO8=LOW  → PMOS ON  → BOOT0=HIGH (ブートローダーモード)
 * - GPIO8=HIGH → PMOS OFF → BOOT0=LOW  (通常モード)
 * 
 * [NRST制御]
 * - GPIO20=HIGH → NMOS ON  → NRST=LOW  (リセット状態)
 * - GPIO20=LOW  → NMOS OFF → NRST=HIGH (通常動作)
 * 
 * モード切り替え:
 * - BOOT0=LOW  + RESET → 通常起動（Flash実行）
 * - BOOT0=HIGH + RESET → Bootloaderモード（UART書き込み可能）
 */
class STM32Bootloader {
public:
  STM32Bootloader();
  
  /**
   * @brief 初期化（ピンモード設定）
   */
  void begin();
  
  /**
   * @brief STM32を通常モードでリセット
   */
  void resetToNormal();
  
  /**
   * @brief STM32をBootloaderモードでリセット
   */
  void resetToBootloader();

  /** BOOT0をLOWのまま、ULSA custom bootloader受付時間内へresetする。 */
  void resetToCustomBootloader();

  /** custom bootloader完了後、追加resetせず通常UART ownershipへ戻す。 */
  void finishCustomBootloaderSession();
  
  /**
   * @brief STM32をリセット（現在のBOOT0状態を維持）
   */
  void reset();
  
  /**
   * @brief BOOT0ピンの状態を設定
   * @param state true: HIGH (Bootloaderモード), false: LOW (通常モード)
   */
  void setBoot0(bool state);
  
  /**
   * @brief 現在Bootloaderモードかどうか
   * @return true: Bootloaderモード
   */
  bool isBootloaderMode() const;
  
  /**
   * @brief Bootloaderモードフラグを直接設定（ペリフェラル停止前に使用）
   * @param mode true: Bootloaderモード, false: 通常モード
   */
  void setBootloaderModeFlag(bool mode);
  
  /**
   * @brief UARTブリッジへの参照を設定（resetToBootloader内でブリッジを開始するため）
   * @param bridge UartBridgeインスタンスへのポインタ
   */
  void setUartBridge(UartBridge* bridge);

private:
  bool _bootloaderMode;
  UartBridge* _uartBridge;
  
  // STM32 リセットパルス制御
  // 参考: AN2606 - STM32 Microcontroller System Memory Boot Mode
  //
  // リセットパルス幅: 最小 5ms（10ms は安全マージン）
  // リセット後の待機: AN2606推奨は最小300ms、実測では1500msで安定
  // BOOT0 安定化: GPIO 容量充放電には 10ms 必要
  static const uint32_t RESET_PULSE_MS = 10;    // リセットパルス幅（5ms 以上推奨）
  static const uint32_t RESET_WAIT_MS = 1500;   // リセット後の待機時間（STM32F4完全起動+バッファ安定化）
  static const uint32_t BOOT0_STABLE_MS = 10;   // BOOT0 安定化時間（GPIO 容量充放電）
};

#endif // STM32_BOOTLOADER_H
