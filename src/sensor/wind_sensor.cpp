/**
 * @file wind_sensor.cpp
 * @brief 風速計データ処理モジュール実装
 * @date 2025-12-03
 */

#include "wind_sensor.h"
#include "uart_bridge.h"
#include "pin_config.h"
#include <M5Unified.h>
#include <math.h>

WindSensor::WindSensor()
  : _debugOutput(true)
  , _lastStatsTime(0)
  , _source(WIND_SENSOR_SIMULATION_ENABLED ? WIND_SOURCE_SIMULATION : WIND_SOURCE_UART)
  , _simulationMode(WIND_SENSOR_SIMULATION_ENABLED)
  , _lastSimUpdateTime(0)
  , _simWindDirPhase(0.0f)
  , _simWindSpeedPhase(0.0f) {
}

void WindSensor::begin(HardwareSerial* serial) {
  _parser.begin(serial);
  _lastStatsTime = millis();
  
  if (_source == WIND_SOURCE_SIMULATION) {
    initializeSimulationData();
    M5.Log.println("========================================");
    M5.Log.println("[DEBUG] Wind sensor SIMULATION mode");
    M5.Log.println("[DEBUG] Generating mock data at 10Hz");
    M5.Log.println("========================================");
  } else {
    M5.Log.println("========================================");
    M5.Log.println("[INFO] Wind sensor initialized");
    M5.Log.println("[INFO] Reading REAL data from UART");
    M5.Log.println("========================================");
  }
}

void WindSensor::begin(UartBridge* bridge) {
  _parser.begin(bridge);
  _lastStatsTime = millis();
  
  if (_source == WIND_SOURCE_SIMULATION) {
    initializeSimulationData();
    M5.Log.println("========================================");
    M5.Log.println("[DEBUG] Wind sensor with UartBridge");
    M5.Log.println("[DEBUG] UART data monitoring enabled");
    M5.Log.println("========================================");
  } else {
    M5.Log.println("========================================");
    M5.Log.println("[INFO] Wind sensor initialized with UartBridge");
    M5.Log.println("[INFO] Reading REAL data from UART");
    M5.Log.println("========================================");
  }
}

void WindSensor::beginI2c(TwoWire* wire, uint8_t address) {
  _i2cClient.begin(wire, address);
}

bool WindSensor::update() {
  bool hasNew = false;
  
  if (_source == WIND_SOURCE_SIMULATION) {
    // シミュレーションモード: 模擬データを生成
    hasNew = updateSimulation();
  } else if (_source == WIND_SOURCE_I2C) {
    // I2Cモード: ULSA EVOレジスタマップからsnapshotを取得
    hasNew = _i2cClient.update();
  } else {
    // 通常モード: UARTからデータを受信
    hasNew = _parser.update();
  }
  
  if (hasNew && _debugOutput) {
    printWindData(getData());
  }
  
  // 統計情報の定期表示（シミュレーションモードでは無効）
  if (_source == WIND_SOURCE_UART && _debugOutput && (millis() - _lastStatsTime > STATS_INTERVAL_MS)) {
    printStats();
    _lastStatsTime = millis();
  }
  
  return hasNew;
}

const WindData& WindSensor::getData() const {
  if (_source == WIND_SOURCE_SIMULATION) {
    return _simulatedData;
  }
  if (_source == WIND_SOURCE_I2C) {
    return _i2cClient.getData();
  }
  return _parser.getLatestData();
}

void WindSensor::printDataOneLine() const {
  const WindData& data = getData();
  
  // 1行フォーマットで表示
  M5.Log.printf("Node:%d Valid:%d Dir:%3ddeg Speed:%5.2fm/s A:%+6.2fm/s B:%+6.2fm/s Head:%+6.2fm/s Temp:%5.1fdegC Sound:%6.2fm/s\n",
    data.nodeId,
    data.isValid ? 1 : 0,
    data.windDirection,
    data.windSpeed,
    data.windSpeedA,
    data.windSpeedB,
    data.headingSpeed,
    data.temperature,
    data.soundSpeed
  );
}

const ParseStats& WindSensor::getParseStats() const {
  return _parser.getStats();
}

const char* WindSensor::getLastParseError() const {
  return _parser.getLastError();
}

const WindParser& WindSensor::getParser() const {
  return _parser;
}

UlsaEvoI2cClient& WindSensor::getI2cClient() {
  return _i2cClient;
}

const UlsaEvoI2cClient& WindSensor::getI2cClient() const {
  return _i2cClient;
}

void WindSensor::setDebugOutput(bool enable) {
  _debugOutput = enable;
}

void WindSensor::setSource(WindSensorSource source) {
  _source = source;
  _simulationMode = (source == WIND_SOURCE_SIMULATION);
  if (_source == WIND_SOURCE_SIMULATION) {
    initializeSimulationData();
  }
}

WindSensorSource WindSensor::getSource() const {
  return _source;
}

const char* WindSensor::getSourceName() const {
  switch (_source) {
    case WIND_SOURCE_UART:
      return "UART";
    case WIND_SOURCE_I2C:
      return "I2C";
    case WIND_SOURCE_SIMULATION:
      return "SIMULATION";
    default:
      return "UNKNOWN";
  }
}

void WindSensor::printWindData(const WindData& data) {
  M5.Log.printf("Node:%d Valid:%d Status:0x%02X Service:0x%02X Cause:%u NTC:%u Dir:%ddeg Speed:%.2fm/s A:%+.2fm/s B:%+.2fm/s Head:%.2fm/s Sound:%.2fm/s Temp:%.2fdegC\n",
                data.nodeId,
                data.isValid,
                data.status,
                data.serviceStatus,
                data.activeCause,
                data.ntcReadingStatus,
                data.windDirection,
                data.windSpeed,
                data.windSpeedA,
                data.windSpeedB,
                data.headingSpeed,
                data.soundSpeed,
                data.temperature);
}

void WindSensor::printStats() {
  // 統計情報表示を無効化
}

bool WindSensor::isSimulationMode() const {
  return _source == WIND_SOURCE_SIMULATION;
}

void WindSensor::initializeSimulationData() {
  _simulatedData.nodeId = WIND_SIM_NODE_ID;
  _simulatedData.isValid = true;
  _simulatedData.statusProtocolVersion = WIND_STATUS_PROTOCOL_VERSION;
  _simulatedData.status = WIND_STATUS_DATA_READY | WIND_STATUS_DATA_VALID;
  _simulatedData.serviceStatus = 0;
  _simulatedData.activeCause = WIND_CAUSE_NONE;
  _simulatedData.ntcReadingStatus = WIND_NTC_READING_NOT_SAMPLED;
  _simulatedData.windDirection = 0;
  _simulatedData.windSpeed = 0.0f;
  _simulatedData.updateAxisWindSpeedsFromPolar();
  _simulatedData.headingSpeed = 0.0f;
  _simulatedData.soundSpeed = 340.0f;  // 標準音速 (15°C相当)
  _simulatedData.temperature = 20.0f;  // 標準温度
  _simulatedData.timestamp = millis();
  _lastSimUpdateTime = millis();
}

bool WindSensor::updateSimulation() {
  uint32_t now = millis();
  
  // 更新間隔チェック
  if (now - _lastSimUpdateTime < WIND_SIM_UPDATE_INTERVAL) {
    return false;
  }
  _lastSimUpdateTime = now;
  
  // 時間経過を計算 (秒単位)
  float elapsed = now / 1000.0f;
  
  // ============================================
  // 風向シミュレーション
  // 0°〜359°をゆっくり回転（約60秒で1周）
  // ============================================
  _simWindDirPhase = fmodf(elapsed * 6.0f, 360.0f);  // 6°/秒
  _simulatedData.windDirection = (uint16_t)_simWindDirPhase;
  
  // ============================================
  // 風速シミュレーション
  // 0〜15 m/s の範囲で正弦波状に変化（周期約20秒）
  // ============================================
  _simWindSpeedPhase = elapsed * 0.314f;  // 2π/20秒 ≈ 0.314
  _simulatedData.windSpeed = 7.5f + 7.5f * sinf(_simWindSpeedPhase);
  _simulatedData.updateAxisWindSpeedsFromPolar();
  
  // ============================================
  // 機首風速シミュレーション
  // 風向と風速から計算（筐体0°方向成分）
  // ============================================
  float windDirRad = _simulatedData.windDirection * M_PI / 180.0f;
  _simulatedData.headingSpeed = _simulatedData.windSpeed * cosf(windDirRad);
  
  // ============================================
  // 温度シミュレーション
  // 15〜25°Cの範囲でゆっくり変化（周期約120秒）
  // ============================================
  float tempPhase = elapsed * 0.0524f;  // 2π/120秒 ≈ 0.0524
  _simulatedData.temperature = 20.0f + 5.0f * sinf(tempPhase);
  
  // ============================================
  // 音速シミュレーション
  // 温度から計算 c = 331.5 + 0.6 * T [m/s]
  // ============================================
  _simulatedData.soundSpeed = 331.5f + 0.6f * _simulatedData.temperature;
  
  // タイムスタンプ更新
  _simulatedData.timestamp = now;
  
  return true;
}
