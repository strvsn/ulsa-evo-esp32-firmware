/**
 * @file cmd_sys.cpp
 * @brief システム情報コマンド実装
 * 
 * TaskStatsAnalyzerライブラリを使用してFreeRTOSタスク統計を取得
 * ESP32標準APIでシステム情報を取得
 */

#include <Arduino.h>
#include <Esp.h>

// タスク統計（独自実装）
#include "task_stats.h"
#include "uart_bridge.h"

// 外部変数: TaskStatsManagerインスタンス（main.cppで定義）
extern TaskStatsManager* g_taskStats;
extern UartBridge uartBridge;

static const char* bootloaderCommandName(uint8_t command) {
    switch (command) {
        case 0x00: return "GET";
        case 0x01: return "GET_VERSION";
        case 0x02: return "GET_ID";
        case 0x11: return "READ_MEMORY";
        case 0x21: return "GO";
        case 0x31: return "WRITE_MEMORY";
        case 0x43: return "ERASE";
        case 0x44: return "EXT_ERASE";
        case 0x50: return "SPECIAL";
        case 0x51: return "EXT_SPECIAL";
        case 0x63: return "WRITE_PROTECT";
        case 0x73: return "WRITE_UNPROTECT";
        case 0x82: return "READOUT_PROTECT";
        case 0x92: return "READOUT_UNPROTECT";
        case 0xA1: return "GET_CHECKSUM";
        default: return "UNKNOWN";
    }
}

/**
 * @brief システム情報コマンドハンドラ
 * 
 * サブコマンド:
 *   info   - チップ情報、周波数、アップタイム
 *   memory - ヒープメモリ使用状況
 *   tasks  - FreeRTOSタスク一覧とスタック情報（自動出力）
 *   reboot - システム再起動
 * 
 * @param argc 引数の数
 * @param argv 引数配列 (argv[0]="sys", argv[1]=サブコマンド)
 * @return 成功時true、失敗時false
 */
bool cmdSys(int argc, const String* argv) {
    if (argc < 2) {
        Serial.println("Usage: sys <info|memory|tasks|bootloader|reboot>");
        return false;
    }

    String subcmd = argv[1];
    subcmd.toLowerCase();

    if (subcmd == "info") {
        // === チップ情報 ===
        Serial.println("=== System Info ===");
        Serial.printf("Chip Model: %s\n", ESP.getChipModel());
        Serial.printf("Chip Cores: %d\n", ESP.getChipCores());
        Serial.printf("Chip Revision: %d\n", ESP.getChipRevision());
        Serial.printf("CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
        Serial.printf("Flash Size: %d KB\n", ESP.getFlashChipSize() / 1024);
        Serial.printf("Flash Speed: %d MHz\n", ESP.getFlashChipSpeed() / 1000000);
        
        // アップタイム（ミリ秒→日時分秒変換）
        unsigned long uptime_ms = millis();
        unsigned long uptime_sec = uptime_ms / 1000;
        unsigned long days = uptime_sec / 86400;
        unsigned long hours = (uptime_sec % 86400) / 3600;
        unsigned long mins = (uptime_sec % 3600) / 60;
        unsigned long secs = uptime_sec % 60;
        Serial.printf("Uptime: %lud %02lu:%02lu:%02lu\n", days, hours, mins, secs);
        
        return true;
    }
    else if (subcmd == "memory") {
        // === メモリ情報 ===
        Serial.println("=== Memory Info ===");
        Serial.printf("Heap Size: %d bytes\n", ESP.getHeapSize());
        Serial.printf("Free Heap: %d bytes (%.1f%%)\n", 
                      ESP.getFreeHeap(),
                      100.0 * ESP.getFreeHeap() / ESP.getHeapSize());
        Serial.printf("Min Free Heap: %d bytes\n", ESP.getMinFreeHeap());
        Serial.printf("Max Alloc Heap: %d bytes\n", ESP.getMaxAllocHeap());
        
        // PSRAM情報（ESP32-C3には非搭載だが将来対応用）
        if (ESP.getPsramSize() > 0) {
            Serial.printf("PSRAM Size: %d bytes\n", ESP.getPsramSize());
            Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
        }
        
        return true;
    }
    else if (subcmd == "tasks") {
        // === タスク統計 ===
        Serial.println("=== Task Statistics ===");
        if (g_taskStats == nullptr) {
            Serial.println("ERROR: TaskStats not initialized");
            return false;
        }
        // 即時出力（ランタイム統計とスタック残量）
        g_taskStats->printNow();
        return true;
    }
    else if (subcmd == "bootloader") {
        // === STM32 ROM bootloader passive sniffer ===
        const UartBridge::BootloaderSnifferStats& stats =
            uartBridge.getBootloaderSnifferStats();

        Serial.println("=== STM32 Bootloader Sniffer ===");
        Serial.printf("Bytes: host->stm32=%lu, stm32->host=%lu\n",
                      (unsigned long)stats.hostToTargetBytes,
                      (unsigned long)stats.targetToHostBytes);
        Serial.printf("Sync=%lu, ACK=%lu, NACK=%lu, checksum_errors=%lu\n",
                      (unsigned long)stats.syncCount,
                      (unsigned long)stats.ackCount,
                      (unsigned long)stats.nackCount,
                      (unsigned long)stats.checksumErrorCount);
        Serial.printf("Commands: total=%lu, ack=%lu, last=0x%02X (%s), phase=%u\n",
                      (unsigned long)stats.commandCount,
                      (unsigned long)stats.commandAckCount,
                      stats.lastCommand,
                      bootloaderCommandName(stats.lastCommand),
                      stats.lastSnifferPhase);
        Serial.printf("Info: get=%lu, get_version=%lu, get_id=%lu\n",
                      (unsigned long)stats.getCommandCount,
                      (unsigned long)stats.getVersionCommandCount,
                      (unsigned long)stats.getIdCommandCount);
        Serial.printf("Write: cmd=%lu, final_ack=%lu\n",
                      (unsigned long)stats.writeCommandCount,
                      (unsigned long)stats.writeFinalAckCount);
        Serial.printf("Read/verify: cmd=%lu, final_ack=%lu\n",
                      (unsigned long)stats.readCommandCount,
                      (unsigned long)stats.readFinalAckCount);
        Serial.printf("Checksum verify: cmd=%lu, final_ack=%lu\n",
                      (unsigned long)stats.getChecksumCommandCount,
                      (unsigned long)stats.getChecksumFinalAckCount);
        Serial.printf("Erase: cmd=%lu, final_ack=%lu\n",
                      (unsigned long)stats.eraseCommandCount,
                      (unsigned long)stats.eraseFinalAckCount);
        Serial.printf("GO: cmd=%lu, final_ack=%lu, complete_seen=%s, last_go_addr=0x%08lX\n",
                      (unsigned long)stats.goCommandCount,
                      (unsigned long)stats.goFinalAckCount,
                      stats.goCompleteSeen ? "yes" : "no",
                      (unsigned long)stats.lastGoAddress);
        Serial.printf("Protect/Special: wp=%lu, wun=%lu, rdp=%lu, rdu=%lu, sp=%lu, esp=%lu\n",
                      (unsigned long)stats.writeProtectCommandCount,
                      (unsigned long)stats.writeUnprotectCommandCount,
                      (unsigned long)stats.readoutProtectCommandCount,
                      (unsigned long)stats.readoutUnprotectCommandCount,
                      (unsigned long)stats.specialCommandCount,
                      (unsigned long)stats.extendedSpecialCommandCount);
        Serial.printf("Other: cmd=%lu, last=0x%02X (%s)\n",
                      (unsigned long)stats.otherCommandCount,
                      stats.lastOtherCommand,
                      bootloaderCommandName(stats.lastOtherCommand));
        Serial.printf("Last address=0x%08lX\n",
                      (unsigned long)stats.lastAddress);
        Serial.printf("CubeProgrammer complete candidate=%s\n",
                      uartBridge.isBootloaderCubeProgrammerCompleteCandidate() ? "yes" : "no");
        Serial.println("Note: auto-return is triggered by GO final ACK, or by a conservative CubeProgrammer completion candidate after a quiet window.");
        return true;
    }
    else if (subcmd == "reboot") {
        // === システム再起動 ===
        Serial.println("Rebooting...");
        delay(100); // シリアル出力完了を待つ
        ESP.restart();
        return true; // ここには到達しない
    }
    else {
        Serial.printf("Unknown subcommand: %s\n", subcmd.c_str());
        Serial.println("Available: info, memory, tasks, bootloader, reboot");
        return false;
    }
}
