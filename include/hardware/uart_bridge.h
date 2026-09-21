/**
 * @file uart_bridge.h
 * @brief USB-CDC ⇔ UART1 ブリッジモジュール
 * @date 2025-12-03
 * 
 * STM32 Bootloaderモード時のデータ中継を担当
 */

#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

#include <Arduino.h>

// 前方宣言 - D入力デバッグ機能を無効化
// class BootloaderDebugger;

/**
 * @brief UARTブリッジクラス
 * 
 * USB-CDCとUART1間のデータ転送を管理
 */
class UartBridge {
public:
  UartBridge();
  
  /**
   * @brief 初期化
   * @param usbSerial USB-CDCシリアル（Serial）
   * @param uartSerial UART1シリアル（Serial1）
   */
  void begin(Stream* usbSerial, HardwareSerial* uartSerial);
  
  /**
   * @brief ブリッジ処理 - 通常モード用（毎ループ呼び出し）
   * 利用可能な全バイトを高速処理
   * WindSensor用バッファにも保存
   */
  void process();
  
  /**
   * @brief ブリッジ処理 - STM32ブートローダーモード専用
   * 厳密な1バイトずつ処理（STM32ブートローダープロトコル準拠）
   * UART→USB優先で即座にreturn
   */
  void processBootloaderMode();

  /**
   * @brief STM32 bootloader GO完了検知フラグを取得してクリア
   * 転送済みbyte列を受動監視した結果のみ返す。転送データは消費しない。
   */
  bool consumeBootloaderGoComplete();

  struct BootloaderSnifferStats {
    uint32_t hostToTargetBytes;
    uint32_t targetToHostBytes;
    uint32_t syncCount;
    uint32_t ackCount;
    uint32_t nackCount;
    uint32_t commandCount;
    uint32_t commandAckCount;
    uint32_t getCommandCount;
    uint32_t getVersionCommandCount;
    uint32_t getIdCommandCount;
    uint32_t writeCommandCount;
    uint32_t writeFinalAckCount;
    uint32_t readCommandCount;
    uint32_t readFinalAckCount;
    uint32_t eraseCommandCount;
    uint32_t eraseFinalAckCount;
    uint32_t goCommandCount;
    uint32_t goFinalAckCount;
    uint32_t getChecksumCommandCount;
    uint32_t getChecksumFinalAckCount;
    uint32_t writeProtectCommandCount;
    uint32_t writeUnprotectCommandCount;
    uint32_t readoutProtectCommandCount;
    uint32_t readoutUnprotectCommandCount;
    uint32_t specialCommandCount;
    uint32_t extendedSpecialCommandCount;
    uint32_t otherCommandCount;
    uint32_t checksumErrorCount;
    uint8_t lastCommand;
    uint8_t lastOtherCommand;
    uint8_t lastSnifferPhase;
    uint32_t lastAddress;
    uint32_t lastGoAddress;
    bool goCompleteSeen;
  };

  /**
   * @brief 直近bootloaderセッションの受動スニファ統計を取得
   * bootloader中は出力せず、通常コマンドモードでの事後確認に使う。
   */
  const BootloaderSnifferStats& getBootloaderSnifferStats() const;

  /**
   * @brief CubeProgrammer風のGOなし書き込み完了候補かを取得
   * 受動スニファ統計だけを評価し、転送データは消費しない。
   */
  bool isBootloaderCubeProgrammerCompleteCandidate() const;

  /**
   * @brief bootloaderスニファが観測した累積byte数を取得
   * main側の静穏時間判定に使う。転送処理には影響しない。
   */
  uint32_t getBootloaderSnifferByteCount() const;
  
  /**
   * @brief ブートローダーモード用タイマーリセット
   * resetToBootloader()から呼び出される
   */
  // ブートローダーモード用バッファクリア時間リセット
  // ブートローダーモード移行時に呼び出される
  // 100ms = STM32のBOOT0/RESET切替完了に必要な時間
  void resetBootloaderState();
  
  /**
   * @brief デバッグ用：BootloaderDebuggerを設定 - D入力デバッグ機能を無効化
   */
  // void setDebugger(BootloaderDebugger* debugger) { _debugger = debugger; }
  
  /**
   * @brief アクティビティがあったかチェック
   * @return true: 最近データ転送があった
   */
  bool hasActivity() const;
  
  /**
   * @brief アクティビティフラグをクリア
   */
  void clearActivity();
  
  /**
   * @brief 累積転送バイト数を取得
   * @return 転送バイト数
   */
  uint32_t getTotalBytes() const;
  
  /**
   * @brief USB-CDCからBOOTコマンドを受信したかチェック
   * @return true: BOOTコマンドを受信した
   */
  bool isBootCommandReceived() const;
  
  /**
   * @brief BOOTコマンド受信フラグをクリア
   */
  void clearBootCommand();
  
  /**
   * @brief USB-CDCからのデータを監視してBOOTコマンドを検出
   * ブリッジ無効時のみUSB入力を消費する。ブリッジ有効時は通常CLIを
   * STM32へ透過させるため、BOOT検出は行わない。
   */
  void checkBootCommand();
  
  /**
   * @brief UART1バッファをフラッシュ（全データを破棄）
   * ブートモード遷移時にゴミデータを除去するために使用
   */
  void flushUart() const;
  
  /**
   * @brief WindSensor用: バッファから1バイト読み取り
   * @return 読み取ったバイト（-1: データなし）
   */
  int readForWindSensor();
  
  /**
   * @brief WindSensor用: バッファ内の利用可能バイト数
   * @return 利用可能バイト数
   */
  int availableForWindSensor() const;
  
  /**
   * @brief ブリッジの有効/無効を設定
   * @param enabled true: 有効, false: 無効
   */
  void setEnabled(bool enabled);
  
  /**
   * @brief ブリッジが有効かどうかを取得
   * @return true: 有効, false: 無効
   */
  bool isEnabled() const;

private:
  enum BootloaderSnifferPhase {
    BOOTLOADER_SNIFFER_IDLE = 0,
    BOOTLOADER_SNIFFER_WAIT_SYNC_ACK,
    BOOTLOADER_SNIFFER_WAIT_COMMAND_COMPLEMENT,
    BOOTLOADER_SNIFFER_WAIT_COMMAND_ACK,
    BOOTLOADER_SNIFFER_WAIT_ADDRESS,
    BOOTLOADER_SNIFFER_WAIT_ADDRESS_ACK,
    BOOTLOADER_SNIFFER_WAIT_WRITE_LENGTH,
    BOOTLOADER_SNIFFER_WAIT_WRITE_PAYLOAD,
    BOOTLOADER_SNIFFER_WAIT_READ_LENGTH,
    BOOTLOADER_SNIFFER_WAIT_READ_ACK,
    BOOTLOADER_SNIFFER_SKIP_READ_DATA,
    BOOTLOADER_SNIFFER_WAIT_ERASE_LENGTH,
    BOOTLOADER_SNIFFER_WAIT_ERASE_PAYLOAD,
    BOOTLOADER_SNIFFER_WAIT_CHECKSUM_SIZE,
    BOOTLOADER_SNIFFER_WAIT_CHECKSUM_SIZE_ACK,
    BOOTLOADER_SNIFFER_WAIT_CHECKSUM_POLY,
    BOOTLOADER_SNIFFER_WAIT_CHECKSUM_POLY_ACK,
    BOOTLOADER_SNIFFER_WAIT_CHECKSUM_INIT,
    BOOTLOADER_SNIFFER_WAIT_FINAL_ACK
  };

  Stream* _usb;              // USB-CDCシリアル
  HardwareSerial* _uart;     // UART1シリアル
  bool _hasActivity;         // 最近の転送アクティビティ
  uint32_t _bootloaderClearTimeout; // ブートローダーモードでのバッファクリア終了時刻
  bool _bootloaderFirstByte;        // 初回0x00フィルタリング用フラグ
  uint32_t _totalBytes;
  bool _bootCommandReceived;  ///< BOOTコマンド受信フラグ
  bool _enabled;             ///< ブリッジ有効/無効フラグ
  BootloaderSnifferStats _bootloaderSnifferStats;
  BootloaderSnifferPhase _bootloaderSnifferPhase;
  uint8_t _bootloaderPendingCommand;
  uint8_t _bootloaderFrameIndex;
  uint8_t _bootloaderFrame[5];
  uint32_t _bootloaderPayloadRemaining;
  uint16_t _bootloaderEraseLength;
  bool _bootloaderGoComplete;
  // BootloaderDebugger* _debugger; ///< デバッグ用（nullptrの場合は記録なし） - D入力デバッグ機能を無効化
  
  static const size_t BUFFER_SIZE = 256;
  static const size_t BOOT_CMD_LENGTH = 4;  ///< "BOOT"の文字数
  uint8_t _buffer[BUFFER_SIZE];
  char _bootCmdBuffer[BOOT_CMD_LENGTH + 1];  ///< BOOTコマンド検出用バッファ
  uint8_t _bootCmdIndex;  ///< 現在のマッチング位置
  
  // WindSensor用リングバッファ
  static const size_t WIND_BUFFER_SIZE = 512;  ///< 風速計データ用バッファサイズ
  uint8_t _windBuffer[WIND_BUFFER_SIZE];       ///< 受信データバッファ
  volatile size_t _windBufferHead;             ///< 書込み位置
  volatile size_t _windBufferTail;             ///< 読取り位置

  void observeBootloaderUsbToUart(uint8_t data);
  void observeBootloaderUartToUsb(uint8_t data);
  void resetBootloaderSniffer();
  void resetBootloaderSnifferPhase();
  void setBootloaderSnifferPhase(BootloaderSnifferPhase phase);
  void beginBootloaderCommand(uint8_t command);
  void handleBootloaderCommandAck();
  void handleBootloaderFinalAck();
  void captureBootloaderAddressFrame();
  void validateBootloaderFiveByteXor();
};

#endif // UART_BRIDGE_H
