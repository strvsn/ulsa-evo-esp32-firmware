#pragma once
#include "FreeRTOS.h"
#include <thread>
using TaskHandle_t = void*;
inline int xTaskCreate(void (*entry)(void*), const char*, unsigned, void* arg,
                       unsigned, TaskHandle_t* handle) {
  *handle = arg;
  std::thread(entry, arg).detach();
  return pdPASS;
}
inline void vTaskDelay(uint32_t ticks) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}
