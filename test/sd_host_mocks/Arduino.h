#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>
inline std::atomic<uint64_t> sdTestClockOffset{0};
inline const auto sdTestClockStart = std::chrono::steady_clock::now();
inline uint64_t sdTestMillis64() {
  return sdTestClockOffset + std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - sdTestClockStart).count();
}
inline uint32_t millis() { return static_cast<uint32_t>(sdTestMillis64()); }
inline void delay(uint32_t value) { std::this_thread::sleep_for(std::chrono::milliseconds(value)); }
inline std::string sdTestRoot;
inline const char* sdTestMountPoint() { return sdTestRoot.c_str(); }
