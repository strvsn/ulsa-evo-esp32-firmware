#pragma once
#include <stdint.h>
struct Preferences {
  bool begin(const char*, bool) { return true; }
  bool isKey(const char*) { return false; }
  uint32_t getUInt(const char*, uint32_t fallback) { return fallback; }
  bool getBool(const char*, bool fallback) { return fallback; }
  unsigned putUInt(const char*, uint32_t) { return 4; }
  unsigned putBool(const char*, bool) { return 1; }
  void end() {}
};
