/**
 * @file cmd_wind.cpp
 * @brief 風速計データ管理コマンド実装
 * 
 * 風速計データの表示、統計情報確認、デバッグ出力制御
 */

#include <Arduino.h>
#include "wind_sensor.h"
#include "wind_data.h"
#include "ulsa_evo_i2c_client.h"
#include "wind_profile_hooks.h"
#include "rtc_manager.h"
#include <string.h>
#include <stdlib.h>

// 外部変数: WindSensorインスタンス（main.cppで定義）
extern WindSensor* g_pWindSensor;

static void printWindDataForSerial(const WindData& data) {
    Serial.printf("Node ID: %u\n", data.nodeId);
    Serial.printf("Valid: %s\n", data.isValid ? "Yes" : "No");
    Serial.printf("Wind Direction: %u deg\n", data.windDirection);
    Serial.printf("Wind Speed: %.2f m/s\n", data.windSpeed);
    Serial.printf("Wind Speed A: %.2f m/s\n", data.windSpeedA);
    Serial.printf("Wind Speed B: %.2f m/s\n", data.windSpeedB);
    Serial.printf("Heading Speed: %.2f m/s\n", data.headingSpeed);
    Serial.printf("Sound Speed: %.2f m/s\n", data.soundSpeed);
    Serial.printf("Temperature: %.2f degC\n", data.temperature);
    Serial.printf("Timestamp: %lu ms\n", (unsigned long)data.timestamp);
}

static void printI2cStats(const UlsaEvoI2cClient& i2c) {
    const UlsaEvoI2cStats& stats = i2c.getStats();
    Serial.printf("I2C Address: 0x%02X\n", i2c.getAddress());
    Serial.printf("Host Suspended: %s\n", i2c.isSuspended() ? "Yes" : "No");
    Serial.printf("Detected: %s\n", i2c.isDetected() ? "Yes" : "No");
    Serial.printf("Reg Version: 0x%02X\n", i2c.getRegVersion());
    Serial.printf("Last Error: %s\n", i2c.getLastErrorName());
    Serial.printf("Last Wire Status: %u (%s)\n",
                  stats.lastWireStatus,
                  UlsaEvoI2cClient::wireStatusName(stats.lastWireStatus));
    Serial.printf("Last Request: %u byte, Received: %u byte\n",
                  stats.lastRequestedLength,
                  stats.lastReceivedLength);
    Serial.printf("Remote LAST_ERROR: 0x%02X (%s)\n",
                  stats.lastRemoteError,
                  UlsaEvoI2cClient::remoteErrorName(stats.lastRemoteError));
    Serial.printf("Last STATUS: 0x%02X\n", stats.lastStatus);
    Serial.printf("Last SERVICE_STATUS: 0x%02X\n", stats.lastServiceStatus);
    Serial.printf("Last ACTIVE_CAUSE: %u\n", stats.lastActiveCause);
    Serial.printf("Last NTC_READING_STATUS: %u\n", stats.lastNtcReadingStatus);
    Serial.printf("  DATA_READY: %s\n", (stats.lastStatus & UlsaEvoI2cClient::STATUS_DATA_READY) ? "Yes" : "No");
    Serial.printf("  DATA_VALID: %s\n", (stats.lastStatus & UlsaEvoI2cClient::STATUS_DATA_VALID) ? "Yes" : "No");
    Serial.printf("  HIGH_WIND_CLIPPED: %s\n", (stats.lastStatus & UlsaEvoI2cClient::STATUS_HIGH_WIND_CLIPPED) ? "Yes" : "No");
    Serial.printf("  HV_INHIBITED: %s\n", (stats.lastStatus & UlsaEvoI2cClient::STATUS_HV_INHIBITED) ? "Yes" : "No");
    Serial.printf("  SAFETY_LATCHED: %s\n", (stats.lastStatus & UlsaEvoI2cClient::STATUS_SAFETY_LATCHED) ? "Yes" : "No");
    Serial.printf("  CALIB_ACTIVE: %s\n", (stats.lastStatus & UlsaEvoI2cClient::STATUS_CALIB_ACTIVE) ? "Yes" : "No");
    Serial.printf("  CONFIG_ACTIVE: %s\n", (stats.lastStatus & UlsaEvoI2cClient::STATUS_CONFIG_ACTIVE) ? "Yes" : "No");
    Serial.printf("  EEPROM_BUSY: %s\n", (stats.lastStatus & UlsaEvoI2cClient::STATUS_EEPROM_BUSY) ? "Yes" : "No");
    Serial.printf("Last DATA_SEQ: %u\n", stats.lastSeq);
    Serial.printf("DATA_SEQ Continuity: gaps=%lu missed=%lu max_gap=%u resets=%lu last_delta=%u\n",
                  (unsigned long)stats.sequenceGapCount,
                  (unsigned long)stats.missedSequenceCount,
                  stats.maxSequenceGap,
                  (unsigned long)stats.sequenceResetCount,
                  stats.lastSequenceDelta);
    Serial.printf("I2C Poll Interval: %lu ms\n", (unsigned long)i2c.getPollIntervalMs());
    if (stats.i2cOutputIntervalMs > 0) {
        Serial.printf("I2C Output: %u Hz / %u ms (configured %u Hz)\n",
                      stats.i2cOutputRateHz,
                      stats.i2cOutputIntervalMs,
                      stats.configuredOutputRateHz);
    }
    if (stats.observedIntervalMs > 0) {
        Serial.printf("Observed DATA_SEQ Interval: %lu ms\n",
                      (unsigned long)stats.observedIntervalMs);
    }
    Serial.printf("Probe: %lu success / %lu total\n",
                  (unsigned long)stats.probeSuccess,
                  (unsigned long)stats.probeCount);
    Serial.printf("Reads: %lu success / %lu attempts\n",
                  (unsigned long)stats.readSuccess,
                  (unsigned long)stats.readAttempts);
    Serial.printf("Poll repeats: stale DATA_SEQ=%lu\n",
                  (unsigned long)stats.staleSeq);
    Serial.printf("Errors: read=%lu nack=%lu short=%lu validation=%lu backoff=%lu\n",
                  (unsigned long)stats.readErrors,
                  (unsigned long)stats.nackErrors,
                  (unsigned long)stats.shortReads,
                  (unsigned long)stats.validationErrors,
                  (unsigned long)stats.backoffSkips);
    if (stats.lastSuccessTime > 0) {
        Serial.printf("Last Success Age: %lu ms\n",
                      (unsigned long)(millis() - stats.lastSuccessTime));
    }
    if (stats.lastErrorTime > 0) {
        Serial.printf("Last Error Age: %lu ms\n",
                      (unsigned long)(millis() - stats.lastErrorTime));
    }
    printWindProfileStats();
}

static bool parseI2cAddressArg(const char* text, uint8_t& address) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    char* end = nullptr;
    unsigned long value = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || value < 0x08 || value > 0x77) {
        return false;
    }

    address = (uint8_t)value;
    return true;
}

static bool parseUint8Arg(const char* text, uint8_t minValue, uint8_t maxValue, uint8_t& valueOut) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    char* end = nullptr;
    unsigned long value = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || value < minValue || value > maxValue) {
        return false;
    }

    valueOut = (uint8_t)value;
    return true;
}


static bool parseWindInstallModeArg(const char* text, uint8_t& mode) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    String value(text);
    value.toLowerCase();
    if (value == "0" || value == "normal") {
        mode = 0;
        return true;
    }
    if (value == "1" || value == "inverted" || value == "invert") {
        mode = 1;
        return true;
    }
    return false;
}

static bool parseOnOffArg(const String& text, bool& enabled) {
    String value(text);
    value.toLowerCase();
    if (value == "on" || value == "1" || value == "true" || value == "yes") {
        enabled = true;
        return true;
    }
    if (value == "off" || value == "0" || value == "false" || value == "no") {
        enabled = false;
        return true;
    }
    return false;
}


static void printI2cUsage() {
    Serial.println("Usage: wind i2c <scan|addr|probe|probe-stop|read|status|stats|config|set|save|discard|defaults|clear-error|suspend|resume>");
    Serial.println("       wind i2c config");
    printWindProfileStatsUsage(true);
    Serial.println("       wind i2c set <node|avg|wind|addr> <value>");
    Serial.println("       wind i2c save");
    Serial.println("       wind i2c discard");
    Serial.println("       wind i2c defaults");
    Serial.println("       wind i2c suspend <on|off|status>");
}

static void printI2cConfig(const UlsaEvoI2cConfig& config) {
    Serial.printf("CFG_NODE_ID: %u\n", config.nodeId);
    Serial.printf("CFG_AVG_CYCLE: %u\n", config.avgCycle);
    Serial.printf("CFG_WIND_DIR_INSTALL_MODE: %u (%s)\n",
                  config.windDirInstallMode,
                  config.windDirInstallMode == 0 ? "Normal" : "Inverted");
    Serial.printf("CFG_I2C_ADDR: 0x%02X\n", config.i2cAddr);
    Serial.printf("I2C_SLAVE_ENABLED: %u (%s, read-only)\n",
                  config.i2cSlaveEnabled,
                  config.i2cSlaveEnabled ? "Enabled" : "Disabled");
    Serial.printf("MEAS_INTERVAL_MS: %u (read-only)\n", config.measIntervalMs);
    if (config.i2cOutputIntervalMs > 0) {
        Serial.printf("CFG_OUTPUT_RATE_HZ: %u (read-only)\n", config.configuredOutputRateHz);
        Serial.printf("I2C_OUTPUT_RATE_HZ: %u (read-only)\n", config.i2cOutputRateHz);
        Serial.printf("I2C_OUTPUT_INTERVAL_MS: %u (read-only)\n", config.i2cOutputIntervalMs);
    }
    Serial.printf("CFG_FLAGS: 0x%02X\n", config.flags);
    Serial.printf("  DIRTY: %s\n", (config.flags & UlsaEvoI2cClient::CFG_FLAG_DIRTY) ? "Yes" : "No");
    Serial.printf("  REBOOT_REQUIRED: %s\n", (config.flags & UlsaEvoI2cClient::CFG_FLAG_REBOOT_REQUIRED) ? "Yes" : "No");
    Serial.printf("  EEPROM_BUSY: %s\n", (config.flags & UlsaEvoI2cClient::CFG_FLAG_EEPROM_BUSY) ? "Yes" : "No");
    Serial.printf("CMD_STATUS: 0x%02X (%s)\n",
                  config.commandStatus,
                  UlsaEvoI2cClient::commandStatusName(config.commandStatus));
    Serial.printf("LAST_ERROR: 0x%02X (%s)\n",
                  config.lastError,
                  UlsaEvoI2cClient::remoteErrorName(config.lastError));
}

static void printI2cConfigResult(const UlsaEvoI2cConfigResult& result) {
    Serial.printf("CMD_STATUS: 0x%02X (%s)\n",
                  result.commandStatus,
                  UlsaEvoI2cClient::commandStatusName(result.commandStatus));
    Serial.printf("LAST_ERROR: 0x%02X (%s)\n",
                  result.lastError,
                  UlsaEvoI2cClient::remoteErrorName(result.lastError));
    Serial.printf("CFG_FLAGS: 0x%02X\n", result.flags);
    Serial.printf("REBOOT_REQUIRED: %s\n", result.rebootRequired ? "Yes" : "No");
}

static bool printI2cConfigAfterOperation(UlsaEvoI2cClient& i2c) {
    UlsaEvoI2cConfig config;
    if (!i2c.readConfig(config)) {
        Serial.printf("Config Read Result: FAIL (%s)\n", i2c.getLastErrorName());
        return false;
    }
    printI2cConfig(config);
    return true;
}

static void printI2cScan(TwoWire& wire) {
    wire.setClock(I2C_FREQUENCY);
    wire.setTimeOut(ULSA_EVO_I2C_TIMEOUT_MS);

    int found = 0;
    Serial.println("=== I2C Bus Scan ===");
    for (uint8_t address = 0x08; address <= 0x77; address++) {
        wire.beginTransmission(address);
        uint8_t status = wire.endTransmission(true);
        if (status == 0) {
            Serial.printf("  ACK: 0x%02X", address);
            if (address == ULSA_EVO_I2C_ADDR_DEFAULT) {
                Serial.print(" (ULSA EVO default)");
            }
            if (address == PCF8563_I2C_ADDRESS) {
                Serial.print(" (RTC PCF8563)");
            }
            Serial.println();
            found++;
        }
    }

    if (found == 0) {
        Serial.println("  No ACKed I2C devices found.");
    }
    Serial.printf("Found: %d device(s)\n", found);
}


/**
 * @brief 風速計データ管理コマンドハンドラ
 * 
 * サブコマンド:
 *   status - データ有効性、パース統計を表示
 *   data - 現在の計測値（風向、風速、温度等）を表示
 *   debug [on|off] - デバッグ出力の有効/無効を切り替え
 *   stats - 詳細なパース統計を表示
 * 
 * @param argc 引数の数
 * @param argv 引数配列 (argv[0]="wind", argv[1]=サブコマンド)
 * @return 成功時true、失敗時false
 */
bool cmdWind(int argc, const String* argv) {
    if (argc < 2) {
        Serial.println("Usage: wind <status|data|debug|stats|source|i2c>");
        return false;
    }

    // WindSensorが初期化されているか確認
    if (g_pWindSensor == nullptr) {
        Serial.println("ERROR: Wind Sensor not initialized");
        return false;
    }

    String subcmd = argv[1];
    subcmd.toLowerCase();

    if (subcmd == "status") {
        // === センサー状態とパース統計概要 ===
        Serial.println("=== Wind Sensor Status ===");
        
        const WindData& data = g_pWindSensor->getData();
        Serial.printf("Node ID: %u\n", data.nodeId);
        Serial.printf("Data Valid: %s\n", data.isValid ? "Yes" : "No");
        Serial.printf("Source: %s\n", g_pWindSensor->getSourceName());
        
        const ParseStats& stats = g_pWindSensor->getParseStats();
        Serial.printf("Total Received: %lu lines\n", stats.totalReceived);
        Serial.printf("Parse Success: %lu (%.1f%%)\n", 
                      stats.parseSuccess, 
                      100.0 * stats.parseSuccess / (stats.totalReceived > 0 ? stats.totalReceived : 1));
        Serial.printf("Parse Errors: %lu\n", stats.parseErrors);
        
        const char* lastError = g_pWindSensor->getLastParseError();
        if (strlen(lastError) > 0) {
            Serial.printf("Last Error: %s\n", lastError);
        }
        
        Serial.printf("Simulation Mode: %s\n", g_pWindSensor->isSimulationMode() ? "On" : "Off");
        if (g_pWindSensor->getSource() == WIND_SOURCE_I2C) {
            Serial.println();
            Serial.println("=== I2C Status ===");
            printI2cStats(g_pWindSensor->getI2cClient());
        }
        
        return true;
    }
    else if (subcmd == "data") {
        // === 現在の計測値を表示 ===
        Serial.println("=== Wind Data ===");
        
        const WindData& data = g_pWindSensor->getData();
        printWindDataForSerial(data);

        if (!data.isValid) {
            Serial.println("(No valid data available)");
        }
        
        return true;
    }
    else if (subcmd == "debug") {
        // === デバッグ出力ON/OFF ===
        if (argc < 3) {
            Serial.println("Usage: wind debug <on|off>");
            return false;
        }
        
        String mode = argv[2];
        mode.toLowerCase();
        
        if (mode == "on") {
            g_pWindSensor->setDebugOutput(true);
            Serial.println("Wind sensor debug output: ON");
            return true;
        }
        else if (mode == "off") {
            g_pWindSensor->setDebugOutput(false);
            Serial.println("Wind sensor debug output: OFF");
            return true;
        }
        else {
            Serial.println("ERROR: Use 'on' or 'off'");
            return false;
        }
    }
    else if (subcmd == "stats") {
        // === 詳細統計 ===
        Serial.println("=== Wind Parse Statistics ===");
        
        const ParseStats& stats = g_pWindSensor->getParseStats();
        
        Serial.printf("Total Received: %lu\n", stats.totalReceived);
        Serial.printf("Parse Success: %lu\n", stats.parseSuccess);
        Serial.printf("Parse Errors: %lu\n", stats.parseErrors);
        
        // エラー詳細
        Serial.printf("  - Validation Errors: %lu\n", stats.validationErrors);
        
        // 成功率
        if (stats.totalReceived > 0) {
            float successRate = 100.0f * stats.parseSuccess / stats.totalReceived;
            Serial.printf("Success Rate: %.2f%%\n", successRate);
        }
        
        const char* lastError = g_pWindSensor->getLastParseError();
        if (strlen(lastError) > 0) {
            Serial.printf("Last Error: %s\n", lastError);
        }
        
        return true;
    }
    else if (subcmd == "source") {
        Serial.printf("Wind Source: %s\n", g_pWindSensor->getSourceName());
        return true;
    }
    else if (subcmd == "i2c") {
        if (argc < 3) {
            printI2cUsage();
            return false;
        }

        String action = argv[2];
        action.toLowerCase();
        UlsaEvoI2cClient& i2c = g_pWindSensor->getI2cClient();

        if (action == "suspend") {
            if (argc >= 4) {
                String value = argv[3];
                value.toLowerCase();
                if (value == "status") {
                    // Status-only query.
                } else {
                    bool suspended = false;
                    if (!parseOnOffArg(value, suspended)) {
                        Serial.println("Usage: wind i2c suspend <on|off|status>");
                        return false;
                    }
                    i2c.setSuspended(suspended);
                }
            }

            Serial.printf("ULSA EVO STM32 I2C host traffic: %s\n",
                          i2c.isSuspended() ? "SUSPENDED" : "ACTIVE");
            if (i2c.isSuspended()) {
                Serial.println("Normal poll, probe, read, config, and BLE-backed STM32 reads are blocked.");
                Serial.println("This is a RAM-only A/B test switch; reboot or 'wind i2c suspend off' restores traffic.");
            }
            Serial.println();
            printI2cStats(i2c);
            return true;
        }

        if (action == "resume") {
            i2c.setSuspended(false);
            Serial.println("ULSA EVO STM32 I2C host traffic: ACTIVE");
            Serial.println();
            printI2cStats(i2c);
            return true;
        }

        if (i2c.isSuspended() && action != "stats") {
            Serial.printf("I2C host traffic is suspended; '%s' is blocked.\n", action.c_str());
            Serial.println("Use 'wind i2c suspend off' to restore STM32 I2C access.");
            Serial.println();
            printI2cStats(i2c);
            return false;
        }

        if (action == "scan") {
            printI2cScan(Wire);
            return true;
        }
        else if (action == "addr") {
            if (argc < 4) {
                Serial.printf("Current ULSA EVO I2C Address: 0x%02X\n", i2c.getAddress());
                Serial.println("Usage: wind i2c addr <0x08..0x77>");
                return true;
            }

            uint8_t address = 0;
            if (!parseI2cAddressArg(argv[3].c_str(), address)) {
                Serial.println("Invalid I2C address. Use 0x08..0x77.");
                return false;
            }

            i2c.setAddress(address);
            Serial.printf("ULSA EVO I2C Address set to 0x%02X\n", i2c.getAddress());
            return true;
        }
        else if (action == "probe") {
            if (argc >= 4) {
                uint8_t address = 0;
                if (!parseI2cAddressArg(argv[3].c_str(), address)) {
                    Serial.println("Invalid I2C address. Use 0x08..0x77.");
                    return false;
                }
                i2c.setAddress(address);
            }
            Serial.println("=== ULSA EVO I2C Probe ===");
            bool ok = i2c.probe();
            printI2cStats(i2c);
            Serial.printf("Probe Result: %s\n", ok ? "OK" : "FAIL");
            return ok;
        }
        else if (action == "probe-stop") {
            if (argc >= 4) {
                uint8_t address = 0;
                if (!parseI2cAddressArg(argv[3].c_str(), address)) {
                    Serial.println("Invalid I2C address. Use 0x08..0x77.");
                    return false;
                }
                i2c.setAddress(address);
            }
            Serial.println("=== ULSA EVO I2C Probe (STOP separated) ===");
            bool ok = i2c.probeWithStop();
            printI2cStats(i2c);
            Serial.printf("Probe Result: %s\n", ok ? "OK" : "FAIL");
            Serial.println("Note: This command uses STOP between register write and read for bus-state isolation.");
            return ok;
        }
        else if (action == "read") {
            Serial.println("=== ULSA EVO I2C Snapshot Read ===");
            WindData data;
            bool ok = i2c.readOnce(data, false);
            if (ok) {
                printWindDataForSerial(data);
            } else {
                Serial.printf("Read Result: FAIL (%s)\n", i2c.getLastErrorName());
            }
            Serial.println();
            printI2cStats(i2c);
            return ok;
        }
        else if (action == "status") {
            Serial.println("=== ULSA EVO I2C Status ===");
            i2c.readStatus();
            printI2cStats(i2c);
            return true;
        }
        else if (action == "stats") {
            Serial.println("=== ULSA EVO I2C Statistics ===");
            if (argc >= 4) {
                String statsAction = argv[3];
                statsAction.toLowerCase();
                if (!handleWindProfileStatsAction(statsAction)) {
                    printWindProfileStatsUsage(false);
                    return false;
                }
            }
            printI2cStats(i2c);
            return true;
        }
        else if (action == "config") {
            Serial.println("=== ULSA EVO I2C Config Draft ===");
            UlsaEvoI2cConfig config;
            bool ok = i2c.readConfig(config);
            if (ok) {
                printI2cConfig(config);
            } else {
                Serial.printf("Config Read Result: FAIL (%s)\n", i2c.getLastErrorName());
                if (i2c.isDetected() && !i2c.supportsConfigWrite()) {
                    Serial.printf("Config write unsupported by REG_VERSION 0x%02X (requires >= 0x06)\n",
                                  i2c.getRegVersion());
                }
            }
            Serial.println();
            printI2cStats(i2c);
            return ok;
        }
        else if (action == "set") {
            if (argc < 5) {
                Serial.println("Usage: wind i2c set <node|avg|wind|addr> <value>");
                Serial.println("  node: 0..255");
                Serial.println("  avg : 1,4,8,16,32,64");
                Serial.println("  wind: 0|normal, 1|inverted");
                Serial.println("  addr: 0x08..0x77");
                return false;
            }

            String field = argv[3];
            field.toLowerCase();
            bool ok = false;

            if (field == "node") {
                uint8_t nodeId = 0;
                if (!parseUint8Arg(argv[4].c_str(), 0, 255, nodeId)) {
                    Serial.println("Invalid node id. Use 0..255.");
                    return false;
                }
                ok = i2c.stageNodeId(nodeId);
            }
            else if (field == "avg") {
                uint8_t avgCycle = 0;
                if (!parseUint8Arg(argv[4].c_str(), 1, 64, avgCycle)) {
                    Serial.println("Invalid avg cycle. Use 1,4,8,16,32,64.");
                    return false;
                }
                ok = i2c.stageAvgCycle(avgCycle);
            }
            else if (field == "wind") {
                uint8_t mode = 0;
                if (!parseWindInstallModeArg(argv[4].c_str(), mode)) {
                    Serial.println("Invalid wind install mode. Use 0|normal or 1|inverted.");
                    return false;
                }
                ok = i2c.stageWindDirInstallMode(mode);
            }
            else if (field == "addr") {
                uint8_t address = 0;
                if (!parseI2cAddressArg(argv[4].c_str(), address)) {
                    Serial.println("Invalid I2C address. Use 0x08..0x77.");
                    return false;
                }
                ok = i2c.stageI2cAddress(address);
            }
            else {
                Serial.printf("Unknown config field: %s\n", field.c_str());
                Serial.println("Available fields: node, avg, wind, addr");
                return false;
            }

            Serial.printf("Stage Result: %s\n", ok ? "OK" : "FAIL");
            if (!ok) {
                Serial.printf("Last Error: %s\n", i2c.getLastErrorName());
            } else {
                Serial.println("Draft updated. Use 'wind i2c save' to persist.");
                if (field == "addr" || field == "wind") {
                    Serial.println("Note: this change is applied after SAVE_CONFIG and reboot.");
                }
            }
            Serial.println();
            printI2cConfigAfterOperation(i2c);
            return ok;
        }
        else if (action == "save") {
            Serial.println("=== ULSA EVO I2C SAVE_CONFIG ===");
            UlsaEvoI2cConfigResult result;
            bool ok = i2c.saveConfig(&result);
            Serial.printf("Save Result: %s\n", ok ? "OK" : "FAIL");
            printI2cConfigResult(result);
            if (!ok) {
                Serial.printf("Local Error: %s\n", i2c.getLastErrorName());
            }
            if (result.rebootRequired) {
                Serial.println("Note: reboot STM32 for saved I2C address or install-mode changes to take effect.");
            }
            Serial.println();
            printI2cConfigAfterOperation(i2c);
            return ok;
        }
        else if (action == "discard") {
            Serial.println("=== ULSA EVO I2C DISCARD_CONFIG ===");
            UlsaEvoI2cConfigResult result;
            bool ok = i2c.discardConfig(&result);
            Serial.printf("Discard Result: %s\n", ok ? "OK" : "FAIL");
            printI2cConfigResult(result);
            if (!ok) {
                Serial.printf("Local Error: %s\n", i2c.getLastErrorName());
            }
            Serial.println();
            printI2cConfigAfterOperation(i2c);
            return ok;
        }
        else if (action == "defaults") {
            Serial.println("=== ULSA EVO I2C RESTORE_CONFIG_DEFAULTS ===");
            UlsaEvoI2cConfigResult result;
            bool ok = i2c.restoreConfigDefaults(&result);
            Serial.printf("Restore Defaults Result: %s\n", ok ? "OK" : "FAIL");
            printI2cConfigResult(result);
            if (!ok) {
                Serial.printf("Local Error: %s\n", i2c.getLastErrorName());
            }
            if (ok) {
                Serial.println("Defaults were staged only. Use 'wind i2c save' to persist.");
            }
            Serial.println();
            printI2cConfigAfterOperation(i2c);
            return ok;
        }
        else if (action == "clear-error") {
            Serial.println("=== ULSA EVO I2C CLEAR_ERROR ===");
            UlsaEvoI2cConfigResult result;
            bool ok = i2c.clearRemoteError(&result);
            Serial.printf("Clear Error Result: %s\n", ok ? "OK" : "FAIL");
            printI2cConfigResult(result);
            if (!ok) {
                Serial.printf("Local Error: %s\n", i2c.getLastErrorName());
            }
            Serial.println();
            printI2cStats(i2c);
            return ok;
        }

        Serial.printf("Unknown i2c subcommand: %s\n", action.c_str());
        printI2cUsage();
        return false;
    }
    else {
        Serial.printf("Unknown subcommand: %s\n", subcmd.c_str());
        Serial.println("Available: status, data, debug, stats, source, i2c");
        return false;
    }
}
