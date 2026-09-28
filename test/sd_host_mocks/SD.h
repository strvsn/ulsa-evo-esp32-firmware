#pragma once
#include "Arduino.h"
#include <filesystem>
#define CARD_NONE 0
#define CARD_MMC 1
#define CARD_SD 2
#define CARD_SDHC 3
#define FILE_APPEND "a"
class File {
public:
  explicit operator bool() const { return false; }
  template<class... T> void printf(const char*, T...) {}
  void close() {}
};
inline std::atomic<uint64_t> sdTestCapacity{16ULL * 1024 * 1024 * 1024};
inline std::atomic<uint64_t> sdTestUsed{0};
inline std::atomic<bool> sdTestPresent{true};
inline std::atomic<unsigned> sdTestMountAttempts{0};
struct SdTestCard {
  bool begin(int, int, int) { ++sdTestMountAttempts; return sdTestPresent; }
  void end() {}
  uint8_t cardType() { return sdTestPresent ? CARD_SDHC : CARD_NONE; }
  uint64_t cardSize() { return sdTestCapacity; }
  uint64_t totalBytes() { return sdTestCapacity; }
  uint64_t usedBytes() { return sdTestUsed; }
  bool exists(const char* path) { return std::filesystem::exists(sdTestRoot + path); }
  bool mkdir(const char* path) { return std::filesystem::create_directories(sdTestRoot + path); }
  File open(const char*, const char*) { return File(); }
};
inline SdTestCard SD;
