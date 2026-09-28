/**
 * @file cmd_ble.cpp
 * @brief BLE通信管理コマンド実装
 * 
 * BLE接続状態確認、開始/停止制御、ノードID変更
 */

#include <Arduino.h>
#include "ble_manager.h"

// 外部変数: BleManagerインスタンス（main.cppで定義）
extern BleManager* g_pBleManager;

/**
 * @brief BLE通信管理コマンドハンドラ
 * 
 * サブコマンド:
 *   status - 接続状態を表示
 *   info - デバイス名、ノードIDを表示
 *   start - BLE通信を開始
 *   stop - BLE通信を停止
 *   node <id> - ノードID変更（0-255）
 * 
 * @param argc 引数の数
 * @param argv 引数配列 (argv[0]="ble", argv[1]=サブコマンド)
 * @return 成功時true、失敗時false
 */
bool cmdBle(int argc, const String* argv) {
    if (argc < 2) {
        Serial.println("Usage: ble <status|info|start|stop|node>");
        return false;
    }

    // BleManagerが初期化されているか確認
    if (g_pBleManager == nullptr) {
        Serial.println("ERROR: BLE Manager not initialized");
        return false;
    }

    String subcmd = argv[1];
    subcmd.toLowerCase();

    if (subcmd == "status") {
        // === BLE接続状態 ===
        Serial.println("=== BLE Status ===");
        
        Serial.printf("Running: %s\n", g_pBleManager->isRunning() ? "Yes" : "No");
        
        if (g_pBleManager->isRunning()) {
            Serial.printf("Connected: %s\n", g_pBleManager->isConnected() ? "Yes" : "No");
            Serial.printf("Connections: %u\n", g_pBleManager->getConnectionCount());
            Serial.println("PHY Policy: 1M only");
            
            BleConnectionState state = g_pBleManager->getConnectionState();
            Serial.printf("State: %s\n", 
                          (state == BLE_CONNECTED) ? "Connected" : "Advertising");
        }
        
        return true;
    }
    else if (subcmd == "info") {
        // === BLEデバイス情報 ===
        Serial.println("=== BLE Device Info ===");
        
        Serial.printf("Node ID: %u\n", g_pBleManager->getNodeId());
        Serial.printf("Device Name: ULSA EVO #%u\n", g_pBleManager->getNodeId());
        Serial.printf("Running: %s\n", g_pBleManager->isRunning() ? "Yes" : "No");
        
        return true;
    }
    else if (subcmd == "start") {
        // === BLE開始 ===
        if (g_pBleManager->isRunning()) {
            Serial.println("BLE is already running");
            return true;
        }
        
        Serial.println("Starting BLE...");
        // begin()は既に初期化済みの場合は再開始
        // 実装上、stop()後の再begin()が必要な場合を想定
        Serial.println("Note: BLE restart requires reboot. Use 'sys reboot'");
        
        return true;
    }
    else if (subcmd == "stop") {
        // === BLE停止 ===
        if (!g_pBleManager->isRunning()) {
            Serial.println("BLE is already stopped");
            return true;
        }
        
        Serial.println("Stopping BLE...");
        g_pBleManager->stop();
        Serial.println("BLE stopped");
        
        return true;
    }
    else if (subcmd == "node") {
        Serial.println("Node ID is controlled by STM32. Use the BLE I2C config and save flow.");
        return false;
    }
    else {
        Serial.printf("Unknown subcommand: %s\n", subcmd.c_str());
        Serial.println("Available: status, info, start, stop, node");
        return false;
    }
}
