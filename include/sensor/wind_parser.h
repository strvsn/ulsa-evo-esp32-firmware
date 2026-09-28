/**
 * @file wind_parser.h
 * @brief ULSA EVO風速計データパーサー
 * @date 2025-12-03
 * 
 * UARTから受信したCSVデータをパースしてWindData構造体に変換
 */

#ifndef WIND_PARSER_H
#define WIND_PARSER_H

#include <Arduino.h>
#include "wind_data.h"
#include "uart_wind_frame_parser.h"

// 前方宣言
class UartBridge;

/**
 * @brief 風速計データパーサークラス
 * 
 * Serial1から受信したCSVデータを解析し、WindData構造体に変換
 * エラーハンドリング機能を内蔵
 */
class WindParser {
public:
  WindParser();
  
  /**
   * @brief パーサーの初期化
   * @param serial UARTシリアルポート（Serial1等）
   */
  void begin(HardwareSerial* serial);
  
  /**
   * @brief パーサーの初期化（UartBridge経由）
   * @param bridge UartBridgeインスタンス
   */
  void begin(UartBridge* bridge);
  
  /**
   * @brief シリアルポートからデータを読み取り、パースを試行
   * @return true: 新しい有効なデータをパース成功
   */
  bool update();
  
  /**
   * @brief 最新のパース済みデータを取得
   * @return WindData構造体の参照
   */
  const WindData& getLatestData() const;
  
  /**
   * @brief パース統計情報を取得
   * @return ParseStats構造体の参照
   */
  const ParseStats& getStats() const;
  
  /**
   * @brief 最後のパースエラー内容を取得
   * @return エラーメッセージ（エラーがない場合は空文字列）
   */
  const char* getLastError() const;
  
  /**
   * @brief 統計情報をリセット
   */
  void resetStats();
  
  /**
   * @brief 新しいデータが利用可能か
   * @return true: update()後に新しいデータあり
   */
  bool hasNewData() const;
  
  /**
   * @brief 新しいデータフラグをクリア
   */
  void clearNewDataFlag();

private:
  HardwareSerial* _serial;    // UARTシリアルポート
  UartBridge* _bridge;        // UartBridgeインスタンス（オプション）
  WindData _latestData;       // 最新のパース済みデータ
  ParseStats _stats;          // パース統計情報
  bool _hasNewData;           // 新データフラグ
  bool _discardingLine;       // 長過ぎるframeを次のEOLまで破棄中
  
  char _lineBuffer[UartWindFrameParser::MAX_LINE_LENGTH]; // 1行バッファ
  uint8_t _lineIndex;         // バッファインデックス
  char _lastErrorLine[UartWindFrameParser::MAX_LINE_LENGTH]; ///< 最後のエラー行
  
  /**
   * @brief シリアルポートまたはブリッジから1バイト読み取り
   * @return 読み取ったバイト（-1: データなし）
   */
  int readByte();
  
  /**
   * @brief シリアルポートまたはブリッジの利用可能バイト数
   * @return 利用可能バイト数
   */
  int available();
  
  /**
   * @brief 1行のCSVデータをパース
   * @param line CSVデータ文字列（NULL終端）
   * @param data パース結果を格納するWindData構造体
   * @return true: パース成功
   */
  bool parseLine(const char* line, WindData& data);
  
};

#endif // WIND_PARSER_H
