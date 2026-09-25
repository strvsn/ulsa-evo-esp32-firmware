/**
 * @file cmd_sd.cpp
 * @brief SDカード管理コマンド実装
 * 
 * SDカードの状態確認、ログレート設定、統計情報表示
 */

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <string.h>
#include "button_handler.h"
#include "sd_logger.h"

// 外部変数: SdLoggerインスタンス（main.cppで定義）
extern SdLogger* g_pSdLogger;

static void keepWatchdogAlive() {
    esp_task_wdt_reset();
    yield();
}

static String normalizeSdPath(const char* path) {
    String normalized = String(path ? path : "");
    if (normalized.length() > 0 && normalized[0] != '/') {
        normalized = "/" + normalized;
    }
    return normalized;
}

static bool deleteSdEntry(const String& path, int& deletedDirs, int& deletedFiles, int& errors) {
    keepWatchdogAlive();

    File entry = SD.open(path);
    if (!entry) {
        Serial.printf("ERROR: Failed to open %s\n", path.c_str());
        errors++;
        return false;
    }

    bool ok = true;
    if (entry.isDirectory()) {
        File child = entry.openNextFile();
        while (child) {
            String childPath = normalizeSdPath(child.name());
            child.close();

            if (!deleteSdEntry(childPath, deletedDirs, deletedFiles, errors)) {
                ok = false;
            }

            keepWatchdogAlive();
            child = entry.openNextFile();
        }
        entry.close();

        if (path != "/") {
            Serial.printf("Removing directory: %s\n", path.c_str());
            if (SD.rmdir(path)) {
                deletedDirs++;
            } else {
                Serial.printf("ERROR: Failed to remove directory %s\n", path.c_str());
                errors++;
                ok = false;
            }
        }
    } else {
        entry.close();

        if (SD.remove(path)) {
            Serial.printf("Deleted file: %s\n", path.c_str());
            deletedFiles++;
        } else {
            Serial.printf("ERROR: Failed to delete %s\n", path.c_str());
            errors++;
            ok = false;
        }
    }

    return ok;
}

static bool deleteSdRootContents(int& deletedDirs, int& deletedFiles, int& errors) {
    File root = SD.open("/");
    if (!root) {
        Serial.println("ERROR: Failed to open root directory");
        errors++;
        return false;
    }

    if (!root.isDirectory()) {
        Serial.println("ERROR: Root is not a directory");
        root.close();
        errors++;
        return false;
    }

    bool ok = true;
    File entry = root.openNextFile();
    while (entry) {
        String entryPath = normalizeSdPath(entry.name());
        entry.close();

        if (!deleteSdEntry(entryPath, deletedDirs, deletedFiles, errors)) {
            ok = false;
        }

        keepWatchdogAlive();
        entry = root.openNextFile();
    }

    root.close();
    return ok;
}

/**
 * @brief SDカード管理コマンドハンドラ
 * 
 * サブコマンド:
 *   status - カード状態、容量、使用率を表示
 *   rate [Hz] - ログレート設定・表示（1-10Hz）
 *   interval [ms] - ログ周期設定・表示（20-600000ms）
 *   info - カード詳細情報
 *   stats - ログ統計（記録行数）
 *   resume - 停止中のロギングを再開
 *   remount - SDカードを再マウント
 *   format YES - typed confirmation付きSDカード全消去（ログファイル削除）
 * 
 * @param argc 引数の数
 * @param argv 引数配列 (argv[0]="sd", argv[1]=サブコマンド)
 * @return 成功時true、失敗時false
 */
bool cmdSd(int argc, const String* argv) {
    if (argc < 2) {
        Serial.println("Usage: sd <status|rate|interval|info|stats|resume|stop|remount|format>");
        return false;
    }

    // SdLoggerが初期化されているか確認
    if (g_pSdLogger == nullptr) {
        Serial.println("ERROR: SD Logger not initialized");
        return false;
    }

    String subcmd = argv[1];
    subcmd.toLowerCase();

    if (subcmd == "status") {
        // === カード状態と容量情報 ===
        Serial.println("=== SD Card Status ===");
        Serial.printf("State: %s\n", g_pSdLogger->getStateString());
        Serial.printf("Available: %s\n", g_pSdLogger->isAvailable() ? "Yes" : "No");
        Serial.printf("Logging Enabled: %s\n", g_pSdLogger->isLoggingEnabled() ? "Yes" : "No");
        Serial.printf("Stop Reason: %s\n", g_pSdLogger->getStopReasonString());
        
        if (g_pSdLogger->isAvailable()) {
            uint32_t totalMB = g_pSdLogger->getTotalSpaceMB();
            uint32_t freeMB = g_pSdLogger->getFreeSpaceMB();
            uint8_t usage = g_pSdLogger->getUsagePercent();
            
            Serial.printf("Total: %lu MB\n", totalMB);
            Serial.printf("Free: %lu MB\n", freeMB);
            Serial.printf("Used: %lu MB (%u%%)\n", totalMB - freeMB, usage);
            Serial.printf("Log Interval: %lu ms\n", g_pSdLogger->getLogIntervalMs());
            Serial.printf("Log Rate: %u Hz\n", g_pSdLogger->getLogRate());
            Serial.printf("Can Log Now: %s\n", g_pSdLogger->canLog() ? "Yes" : "No");
            Serial.printf("Last SD I/O: %u ms\n", g_pSdLogger->getLastWriteDurationMs());
            
            const char* currentPath = g_pSdLogger->getCurrentFilePath();
            if (strlen(currentPath) > 0) {
                Serial.printf("Current File: %s\n", currentPath);
            }
        }

        if (!g_pSdLogger->isLoggingEnabled()) {
            Serial.println("Recovery: use 'sd resume' or 'sd remount'");
        }
        return true;
    }
    else if (subcmd == "rate") {
        // === ログレート設定・表示 ===
        if (argc < 3) {
            // レート表示のみ
            Serial.printf("Current log rate: %u Hz\n", g_pSdLogger->getLogRate());
            Serial.printf("Current log interval: %lu ms\n", g_pSdLogger->getLogIntervalMs());
            Serial.println("Usage: sd rate <1-10>");
            return true;
        }
        
        // レート設定
        int newRate = argv[2].toInt();
        if (newRate < 1 || newRate > 10) {
            Serial.println("ERROR: Rate must be 1-10 Hz");
            return false;
        }
        
        g_pSdLogger->setLogRate((uint8_t)newRate);
        Serial.printf("Log rate set to %d Hz (%lu ms)\n", newRate, g_pSdLogger->getLogIntervalMs());
        return true;
    }
    else if (subcmd == "interval") {
        // === ログ周期設定・表示 ===
        if (argc < 3) {
            Serial.printf("Current log interval: %lu ms\n", g_pSdLogger->getLogIntervalMs());
            Serial.printf("Current log rate: %u Hz\n", g_pSdLogger->getLogRate());
            Serial.println("Usage: sd interval <20-600000>");
            return true;
        }

        uint32_t intervalMs = (uint32_t)argv[2].toInt();
        if (!g_pSdLogger->setLogIntervalMs(intervalMs, SD_LOG_INTERVAL_MIN_MS, true)) {
            Serial.println("ERROR: Interval must be 20-600000 ms");
            return false;
        }

        Serial.printf("Log interval set to %lu ms\n", g_pSdLogger->getLogIntervalMs());
        return true;
    }
    else if (subcmd == "info") {
        // === カード詳細情報 ===
        Serial.println("=== SD Card Info ===");
        
        if (!g_pSdLogger->isAvailable()) {
            Serial.println("SD card not available");
            return true;
        }
        
        // SD.hのカード情報取得
        uint8_t cardType = SD.cardType();
        const char* typeStr = "UNKNOWN";
        switch (cardType) {
            case CARD_MMC: typeStr = "MMC"; break;
            case CARD_SD: typeStr = "SD"; break;
            case CARD_SDHC: typeStr = "SDHC"; break;
            default: break;
        }
        
        Serial.printf("Card Type: %s\n", typeStr);
        Serial.printf("Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
        Serial.printf("Total Space: %llu MB\n", SD.totalBytes() / (1024 * 1024));
        Serial.printf("Used Space: %llu MB\n", SD.usedBytes() / (1024 * 1024));
        Serial.printf("Num Sectors: %llu\n", SD.numSectors());
        Serial.printf("Sector Size: %u bytes\n", SD.sectorSize());
        
        return true;
    }
    else if (subcmd == "stats") {
        // === ログ統計 ===
        Serial.println("=== SD Logging Statistics ===");
        
        Serial.printf("State: %s\n", g_pSdLogger->getStateString());
        Serial.printf("Logging Enabled: %s\n", g_pSdLogger->isLoggingEnabled() ? "Yes" : "No");
        Serial.printf("Stop Reason: %s\n", g_pSdLogger->getStopReasonString());
        Serial.printf("Log Count: %lu lines\n", g_pSdLogger->getLogCount());
        Serial.printf("Synchronized Rows: %lu\n", g_pSdLogger->getSyncedLogCount());
        Serial.printf("Dropped Rows: %lu\n", g_pSdLogger->getDroppedLogCount());
        Serial.printf("Uncertain Rows: %lu\n", g_pSdLogger->getUncertainLogCount());
        Serial.printf("Queued Rows: %u\n", g_pSdLogger->getQueueDepth());
        Serial.printf("Input Paused: %s\n", g_pSdLogger->isInputPaused() ? "Yes" : "No");
        Serial.printf("Log Interval: %lu ms\n", g_pSdLogger->getLogIntervalMs());
        Serial.printf("Log Rate: %u Hz\n", g_pSdLogger->getLogRate());
        Serial.printf("Can Log: %s\n", g_pSdLogger->canLog() ? "Yes" : "No");
        Serial.printf("Source Samples: %lu\n", g_pSdLogger->getSourceSampleCount());
        Serial.printf("Rate-Limited Samples: %lu\n", g_pSdLogger->getRateLimitedSampleCount());
        Serial.printf("Last Source Interval: %lu ms\n", g_pSdLogger->getLastSourceSampleIntervalMs());
        Serial.printf("Last Logged Sample Interval: %lu ms\n",
                      g_pSdLogger->getLastLoggedSampleIntervalMs());
        Serial.printf("Durable Sync Count: %lu\n", g_pSdLogger->getFlushCount());
        Serial.printf("Last SD I/O: %u ms\n", g_pSdLogger->getLastWriteDurationMs());
        
        const char* currentPath = g_pSdLogger->getCurrentFilePath();
        if (strlen(currentPath) > 0) {
            Serial.printf("Current File: %s\n", currentPath);
        }
        
        return true;
    }
    else if (subcmd == "resume") {
        // === ロギング再開 ===
        if (getButtonMode() != BTN_MODE_I2C_MEASURE) {
            Serial.println("ERROR: Return to I2C measurement mode before starting SD logging");
            return false;
        }
        if (g_pSdLogger->resumeLogging()) {
            Serial.println("SD logging resumed");
            return true;
        }

        Serial.printf("ERROR: Failed to resume SD logging (%s)\n", g_pSdLogger->getStateString());
        return false;
    }
    else if (subcmd == "remount") {
        // === SDカード再マウント ===
        Serial.println("Remounting SD card...");
        if (g_pSdLogger->remount()) {
            Serial.println("SD card remounted");
            return true;
        }

        Serial.printf("ERROR: Failed to remount SD card (%s)\n", g_pSdLogger->getStateString());
        return false;
    }
    else if (subcmd == "stop") {
        g_pSdLogger->disableLogging(SD_STOP_USER_DISABLED);
        if (!g_pSdLogger->isStorageQuiescent()) {
            Serial.println("SD stop pending; writer is still draining");
            return false;
        }
        Serial.println("SD logging stopped");
        return true;
    }
    else if (subcmd == "format") {
        // === SDカード全消去（ルートディレクトリのログファイル削除） ===
        SdLoggerState state = g_pSdLogger->getState();
        if (state == SD_STATE_UNINITIALIZED || state == SD_STATE_NO_CARD) {
            Serial.println("ERROR: SD card not available");
            return false;
        }
        
        if (argc != 3 || argv[2] != "YES") {
            Serial.println("WARNING: This deletes all log files on the SD card.");
            Serial.println("Format cancelled. To confirm, enter exactly: sd format YES");
            return false;
        }

        g_pSdLogger->stop();
        if (!g_pSdLogger->isStorageQuiescent()) {
            Serial.println("ERROR: SD writer is still draining; retry after it stops");
            return false;
        }
        
        Serial.println("Deleting files...");
        int deletedDirs = 0;
        int deletedFiles = 0;
        int errors = 0;
        
        bool ok = deleteSdRootContents(deletedDirs, deletedFiles, errors);
        
        Serial.println("=== Format Complete ===");
        Serial.printf("Directories deleted: %d\n", deletedDirs);
        Serial.printf("Files deleted: %d\n", deletedFiles);
        Serial.printf("Errors: %d\n", errors);
        
        return ok && errors == 0;
    }
    else {
        Serial.printf("Unknown subcommand: %s\n", subcmd.c_str());
        Serial.println("Available: status, rate, interval, info, stats, resume, stop, remount, format");
        return false;
    }
}
