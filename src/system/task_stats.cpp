/**
 * @file task_stats.cpp
 * @brief ESP32-C3向けFreeRTOSタスク統計（CPU使用率推定実装）
 * 
 * ESP32-C3でCPU使用率が取得できない理由:
 * 1. FreeRTOS Runtime Stats (vTaskGetRunTimeStats) がESP32-C3のArduino環境で無効
 * 2. CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=1 でも ulRunTimeCounter が常に0
 * 
 * 解決策:
 * - アイドルタスクフックを使用してCPU使用率を推定
 * - 初期基準値（アイドル時）を測定してから、相対値でCPU使用率を表示
 * - ライブラリ不要（ESP32標準FreeRTOS APIのみ使用）
 */

#include "task_stats.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_freertos_hooks.h>

// 前回のアイドルタスク実行カウント
static uint32_t s_lastIdleCount = 0;
static uint32_t s_lastTotalTime = 0;

// アイドルタスクのフック（カウンタ）
static uint32_t s_idleCount = 0;
static bool idle_hook() {
    s_idleCount++;
    return true;
}

// 基準アイドルレート（キャリブレーション用）
static float s_baselineIdleRate = 0.0f;
static bool s_baselineSet = false;

TaskStatsManager::TaskStatsManager(uint32_t intervalMs)
    : _intervalMs(intervalMs), _taskHandle(nullptr) {}

void TaskStatsManager::begin() {
    if (_taskHandle) return;
    
    // アイドルタスクフックを登録
    esp_register_freertos_idle_hook(idle_hook);
    s_lastIdleCount = s_idleCount;
    s_lastTotalTime = millis();
    
    // intervalMs が 0 の場合は、定期出力タスクを作成しない（コマンド経由のみ）
    if (_intervalMs > 0) {
        xTaskCreate(
            TaskStatsManager::taskEntry,
            "TaskStats",
            4096,
            this,
            tskIDLE_PRIORITY + 1,
            &_taskHandle);
    }
}

void TaskStatsManager::printNow() {
    printRuntimeStats();
    printStackWatermarks();
}

void TaskStatsManager::taskEntry(void* arg) {
    auto* self = static_cast<TaskStatsManager*>(arg);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(self->_intervalMs));
        self->printRuntimeStats();
        self->printStackWatermarks();
    }
}

void TaskStatsManager::printRuntimeStats() {
    // アイドルカウントからCPU使用率を推定
    uint32_t currentTime = millis();
    uint32_t currentIdleCount = s_idleCount;
    
    uint32_t elapsedTime = currentTime - s_lastTotalTime;
    uint32_t idleDelta = currentIdleCount - s_lastIdleCount;
    
    if (elapsedTime > 0) {
        float currentIdleRate = (float)idleDelta / (float)elapsedTime;
        
        // 最初の測定（5秒以上経過後）で基準値を設定
        if (!s_baselineSet && elapsedTime > 5000) {
            s_baselineIdleRate = currentIdleRate;
            s_baselineSet = true;
        }
        
        Serial.println("=== CPU Usage (Estimated) ===");
        
        if (s_baselineSet) {
            // 基準値との相対値を計算（CPU使用率 = 100% - (現在のアイドルレート / 基準アイドルレート * 100%)）
            float cpuUsage = 100.0f - (currentIdleRate / s_baselineIdleRate * 100.0f);
            
            // 範囲制限
            if (cpuUsage < 0.0f) cpuUsage = 0.0f;
            if (cpuUsage > 100.0f) cpuUsage = 100.0f;
            
            Serial.printf("CPU Usage: %.1f%%\n", cpuUsage);
            Serial.printf("  (measured idle rate: %.3f calls/ms)\n", currentIdleRate);
        } else {
            // キャリブレーション中
            Serial.println("Calibrating... (wait 5+ seconds)");
            Serial.printf("  Current idle rate: %.3f calls/ms\n", currentIdleRate);
        }
        
        // 次回のため保存
        s_lastIdleCount = currentIdleCount;
        s_lastTotalTime = currentTime;
    }
}

void TaskStatsManager::printStackWatermarks() {
    Serial.println("\n=== Task Info (Simplified) ===");
    
    // タスク数を取得（この関数は利用可能）
    UBaseType_t taskCount = uxTaskGetNumberOfTasks();
    Serial.printf("Total Tasks: %d\n", taskCount);
    
    // ESP32-C3ではuxTaskGetSystemStateが利用できないため、
    // 現在実行中のタスクのスタック情報のみ表示
    TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
    if (currentTask != nullptr) {
        char* taskName = pcTaskGetName(currentTask);
        UBaseType_t priority = uxTaskPriorityGet(currentTask);
        UBaseType_t stackWatermark = uxTaskGetStackHighWaterMark(currentTask);
        
        Serial.println("\nCurrent Task:");
        Serial.printf("Name: %s\n", taskName);
        Serial.printf("Priority: %d\n", priority);
        Serial.printf("Stack Free: %d bytes\n", stackWatermark * 4);
    }
    
    // 個別タスクの情報を取得するには、タスクハンドルを保持しておく必要がある
    // 簡易実装として、ループタスクの情報のみ表示
    TaskHandle_t loopTask = xTaskGetIdleTaskHandle();
    if (loopTask != nullptr) {
        UBaseType_t idleStackWatermark = uxTaskGetStackHighWaterMark(loopTask);
        Serial.printf("\nIdle Task Stack Free: %d bytes\n", idleStackWatermark * 4);
    }
}
