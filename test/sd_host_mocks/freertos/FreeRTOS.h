#pragma once
#include <stdint.h>
#include <mutex>
using BaseType_t = int;
using TickType_t = uint32_t;
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) (p)->lock()
#define portEXIT_CRITICAL(p) (p)->unlock()
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
