/**
 * @file wind_parser.cpp
 * @brief ULSA EVO風速計データパーサー実装
 * @date 2025-12-03
 */

#include "wind_parser.h"
#include "uart_bridge.h"
#include <M5Unified.h>
#include <string.h>

WindParser::WindParser()
  : _serial(nullptr)
  , _bridge(nullptr)
  , _hasNewData(false)
  , _discardingLine(false)
  , _lineIndex(0) {
  memset(_lineBuffer, 0, sizeof(_lineBuffer));
  memset(_lastErrorLine, 0, sizeof(_lastErrorLine));
}

void WindParser::begin(HardwareSerial* serial) {
  _serial = serial;
  _bridge = nullptr;
  _hasNewData = false;
  _discardingLine = false;
  _lineIndex = 0;
  resetStats();
}

void WindParser::begin(UartBridge* bridge) {
  _serial = nullptr;
  _bridge = bridge;
  _hasNewData = false;
  _discardingLine = false;
  _lineIndex = 0;
  resetStats();
}

int WindParser::readByte() {
  if (_bridge != nullptr) {
    return _bridge->readForWindSensor();
  } else if (_serial != nullptr) {
    return _serial->read();
  }
  return -1;
}

int WindParser::available() {
  if (_bridge != nullptr) {
    return _bridge->availableForWindSensor();
  } else if (_serial != nullptr) {
    return _serial->available();
  }
  return 0;
}

bool WindParser::update() {
  if (_serial == nullptr && _bridge == nullptr) return false;
  
  _hasNewData = false;
  
  // Serial1またはUartBridgeから受信データを処理
  while (available() > 0) {
    int data = readByte();
    if (data < 0) break;
    
    char c = (char)data;
    
    // 改行コード検出（\nまたは\r）
    if (c == '\n' || c == '\r') {
      if (_discardingLine) {
        _discardingLine = false;
        _lineIndex = 0;
        continue;
      }
      if (_lineIndex > 0) {
        // NULL終端
        _lineBuffer[_lineIndex] = '\0';
        
        // パース実行
        WindData tempData;
        _stats.totalReceived++;
        
        if (parseLine(_lineBuffer, tempData)) {
          // バリデーション
          if (tempData.isDataValid()) {
            tempData.timestamp = millis();
            _latestData = tempData;
            _stats.parseSuccess++;
            _hasNewData = true;
          } else {
            _stats.validationErrors++;
            _stats.lastErrorTime = millis();
            // バリデーションエラーの場合もエラー行を保存
            strncpy(_lastErrorLine, _lineBuffer, sizeof(_lastErrorLine) - 1);
            _lastErrorLine[sizeof(_lastErrorLine) - 1] = '\0';
            // 詳細なバリデーションエラー情報を出力
            M5.Log.printf("[Validation Error] Data: %s\n", _lineBuffer);
            M5.Log.printf("  Dir:%d (max:%d) Speed:%.2f (%.2f-%.2f) Head:%.2f (%.2f-%.2f)\n",
                          tempData.windDirection, WIND_DIR_MAX,
                          tempData.windSpeed, WIND_SPEED_MIN, WIND_SPEED_MAX,
                          tempData.headingSpeed, HEADING_SPEED_MIN, HEADING_SPEED_MAX);
            M5.Log.printf("  Sound:%.2f (%.2f-%.2f) Temp:%.2f (%.2f-%.2f)\n",
                          tempData.soundSpeed, SOUND_SPEED_MIN, SOUND_SPEED_MAX,
                          tempData.temperature, TEMPERATURE_MIN, TEMPERATURE_MAX);
          }
        } else {
          _stats.parseErrors++;
          _stats.lastErrorTime = millis();
          // パースエラーの行を保存
          strncpy(_lastErrorLine, _lineBuffer, sizeof(_lastErrorLine) - 1);
          _lastErrorLine[sizeof(_lastErrorLine) - 1] = '\0';
          // パースエラーのログ出力は削除（CORE_DEBUG_LEVEL=0で出力制限のため）
        }
        
        // バッファリセット
        _lineIndex = 0;
        
        // 新データがあれば即座に返す
        if (_hasNewData) {
          return true;
        }
      }
    }
    else if (_discardingLine) {
      continue;
    }
    // 通常文字の追加（バッファオーバーフロー対策）
    else if (_lineIndex < UartWindFrameParser::MAX_LINE_LENGTH - 1) {
      _lineBuffer[_lineIndex++] = c;
    }
    // バッファオーバーフロー時はリセット
    else {
      _lineIndex = 0;
      _discardingLine = true;
      _stats.parseErrors++;
      _stats.lastErrorTime = millis();
    }
  }
  
  return false;
}

bool WindParser::parseLine(const char* line, WindData& data) {
  return UartWindFrameParser::parse(line, data);
}

const WindData& WindParser::getLatestData() const {
  return _latestData;
}

const ParseStats& WindParser::getStats() const {
  return _stats;
}

void WindParser::resetStats() {
  _stats.totalReceived = 0;
  _stats.parseSuccess = 0;
  _stats.parseErrors = 0;
  _stats.validationErrors = 0;
  _stats.lastErrorTime = 0;
}

bool WindParser::hasNewData() const {
  return _hasNewData;
}

void WindParser::clearNewDataFlag() {
  _hasNewData = false;
}

const char* WindParser::getLastError() const {
  return _lastErrorLine;
}
