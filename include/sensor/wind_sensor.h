/**
 * @file wind_sensor.h
 * @brief 風速計データ処理モジュール
 * @date 2025-12-03
 * 
 * WindParserを使用した風速計データの処理とデバッグ出力を担当
 */

#ifndef WIND_SENSOR_H
#define WIND_SENSOR_H

#include <Arduino.h>
#include "wind_parser.h"
#include "ulsa_evo_i2c_client.h"

// 前方宣言
class UartBridge;

enum WindSensorSource {
  WIND_SOURCE_UART = 0,
  WIND_SOURCE_I2C,
  WIND_SOURCE_SIMULATION
};

/**
 * @brief 風速計データ処理クラス
 * 
 * UARTからのデータ受信、パース、デバッグ出力を一括管理
 */
class WindSensor {
public:
  WindSensor();
  
  /**
   * @brief 初期化
   * @param serial UARTシリアルポート（Serial1等）
   */
  void begin(HardwareSerial* serial);
  
  /**
   * @brief 初期化（UartBridge経由）
   * @param bridge UartBridgeインスタンス
   */
  void begin(UartBridge* bridge);

  /**
   * @brief I2C入力の初期化
   * @param wire I2Cバス
   * @param address ULSA EVOの7bit I2Cアドレス
   */
  void beginI2c(TwoWire* wire = &Wire, uint8_t address = ULSA_EVO_I2C_ADDR_DEFAULT);
  
  /**
   * @brief 更新処理（毎ループ呼び出し）
   * @return true: 新しいデータを受信した
   */
  bool update();
  
  /**
   * @brief 最新のデータを取得
   * @return WindData構造体の参照
   */
  const WindData& getData() const;
  
  /**
   * @brief 風速計データを1行フォーマットで表示
   */
  void printDataOneLine() const;
  
  /**
   * @brief パース統計情報を取得
   * @return ParseStats構造体の参照
   */
  const ParseStats& getParseStats() const;
  
  /**
   * @brief 最後のパースエラー内容を取得
   * @return エラーメッセージ（エラーがない場合は空文字列）
   */
  const char* getLastParseError() const;
  
  /**
   * @brief パーサーを取得（統計情報アクセス用）
   * @return WindParser参照
   */
  const WindParser& getParser() const;

  /**
   * @brief I2Cクライアントを取得
   * @return UlsaEvoI2cClient参照
   */
  UlsaEvoI2cClient& getI2cClient();
  const UlsaEvoI2cClient& getI2cClient() const;
  
  /**
   * @brief デバッグ出力の有効/無効を設定
   * @param enable true: 有効
   */
  void setDebugOutput(bool enable);

  /**
   * @brief 入力ソースを設定
   * @param source UART/I2C/SIMULATION
   */
  void setSource(WindSensorSource source);

  /**
   * @brief 現在の入力ソースを取得
   */
  WindSensorSource getSource() const;

  /**
   * @brief 現在の入力ソース名を取得
   */
  const char* getSourceName() const;

  /**
   * @brief シミュレーションモードかどうかを取得
   * @return true: シミュレーション中
   */
  bool isSimulationMode() const;

private:
  WindParser _parser;
  UlsaEvoI2cClient _i2cClient;
  bool _debugOutput;
  uint32_t _lastStatsTime;
  WindSensorSource _source;
  
  static const uint32_t STATS_INTERVAL_MS = 10000;  // 統計表示間隔（10秒）
  
  void initializeSimulationData();
  void printWindData(const WindData& data);
  void printStats();
  
  // シミュレーション関連
  bool _simulationMode;           // シミュレーションモードフラグ
  WindData _simulatedData;        // シミュレーション用データ
  uint32_t _lastSimUpdateTime;    // 最後のシミュレーション更新時刻
  float _simWindDirPhase;         // 風向シミュレーション用位相
  float _simWindSpeedPhase;       // 風速シミュレーション用位相
  
  /**
   * @brief シミュレーションデータを更新
   * @return true: データが更新された
   */
  bool updateSimulation();
};

#endif // WIND_SENSOR_H
