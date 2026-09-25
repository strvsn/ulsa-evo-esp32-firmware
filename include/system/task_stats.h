/**
 * @file task_stats.h
 * @brief ESP32-C3向けFreeRTOSタスク統計（ランタイム/スタック）簡易マネージャ
 */

#pragma once

#include <Arduino.h>

class TaskStatsManager {
public:
    explicit TaskStatsManager(uint32_t intervalMs);
    void begin();
    void printNow();

private:
    static void taskEntry(void* arg);
    void printRuntimeStats();
    void printStackWatermarks();

    uint32_t _intervalMs;
    TaskHandle_t _taskHandle;
};
