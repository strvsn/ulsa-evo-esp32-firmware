#pragma once
struct SdTestLogger {
  template<class... T> void printf(const char*, T...) {}
};
inline struct SdTestM5 { SdTestLogger Log; } M5;
