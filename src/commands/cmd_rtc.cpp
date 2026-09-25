/**
 * @file cmd_rtc.cpp
 * @brief UTC/IANA timezone RTC commands shared by Demo and Initial.
 */
#include <M5Unified.h>
#include <errno.h>
#include <stdlib.h>

#include "rtc_manager.h"
#include "rtc_timezone_database.h"

extern RtcManager rtc;

namespace {

const char* configStateName(RtcConfigState state) {
  switch (state) {
    case RtcConfigState::Pending: return "pending";
    case RtcConfigState::Valid: return "valid";
    case RtcConfigState::ZoneOnly: return "zone-only";
    case RtcConfigState::Unconfigured:
    default: return "unconfigured";
  }
}

const char* operationResultName(RtcOperationResult result) {
  switch (result) {
    case RtcOperationResult::Ok: return "ok";
    case RtcOperationResult::UnsupportedZone: return "unsupported zone";
    case RtcOperationResult::TimeOutOfRange: return "time out of range (2000-2099)";
    case RtcOperationResult::NvsFailed: return "NVS write/readback failed";
    case RtcOperationResult::RtcWriteFailed: return "RTC write failed";
    case RtcOperationResult::ReadbackFailed: return "RTC readback failed";
    case RtcOperationResult::LocalTimeAmbiguous: return "DST gap or fold";
    case RtcOperationResult::TimezoneNotConfigured: return "timezone not configured";
    default: return "unknown";
  }
}

bool parseUnixSeconds(const String& text, int64_t& value) {
  if (text.length() == 0) return false;
  errno = 0;
  char* end = nullptr;
  const char* begin = text.c_str();
  const long long parsed = strtoll(begin, &end, 10);
  if (errno == ERANGE || end == begin || !end || *end != '\0') return false;
  value = static_cast<int64_t>(parsed);
  return true;
}

void printDateTime(const char* label, const RtcDateTime& value) {
  M5.Log.printf("%-12s: %04u-%02u-%02u %02u:%02u:%02u\n",
                label, value.year, value.month, value.day,
                value.hour, value.minute, value.second);
}

bool parseLocalDateTime(int argc, const String* argv, RtcDateTime& local) {
  if (argc != 8) return false;
  long values[6]{};
  for (size_t index = 0; index < 6; ++index) {
    errno = 0;
    char* end = nullptr;
    const char* begin = argv[index + 2].c_str();
    values[index] = strtol(begin, &end, 10);
    if (errno == ERANGE || end == begin || !end || *end != '\0') return false;
  }
  if (values[0] < 2000 || values[0] > 2099 ||
      values[1] < 1 || values[1] > 12 ||
      values[2] < 1 || values[2] > 31 ||
      values[3] < 0 || values[3] > 23 ||
      values[4] < 0 || values[4] > 59 ||
      values[5] < 0 || values[5] > 59) {
    return false;
  }
  local.year = static_cast<uint16_t>(values[0]);
  local.month = static_cast<uint8_t>(values[1]);
  local.day = static_cast<uint8_t>(values[2]);
  local.hour = static_cast<uint8_t>(values[3]);
  local.minute = static_cast<uint8_t>(values[4]);
  local.second = static_cast<uint8_t>(values[5]);
  local.weekday = ulsa::rtc::calculateWeekday(local.year, local.month, local.day);
  return local.isValid();
}

void printTimezone() {
  if (!rtc.isTimezoneConfigured()) {
    M5.Log.println("Timezone    : NOT CONFIGURED");
    return;
  }
  char name[64]{};
  if (rtc.getZoneName(name, sizeof(name))) {
    M5.Log.printf("Timezone    : %s (0x%08lX)\n", name,
                  static_cast<unsigned long>(rtc.getZoneId()));
  } else {
    M5.Log.printf("Timezone    : UNSUPPORTED ID 0x%08lX (reconfigure required)\n",
                  static_cast<unsigned long>(rtc.getZoneId()));
  }
}

void cmdRtcGet() {
  RtcTimezoneSnapshot snapshot{};
  if (!rtc.getTimezoneSnapshot(snapshot) || !snapshot.status.utcValid) {
    M5.Log.println("Error: UTC is not valid; run rtc sync <unix-seconds> <Area/City>");
    return;
  }
  printDateTime("UTC", snapshot.utc);
  M5.Log.printf("Unix seconds: %lld\n", static_cast<long long>(snapshot.unixSeconds));
  if (snapshot.localValid) {
    printDateTime("Local", snapshot.local);
    M5.Log.printf("UTC offset  : total=%+d min, standard=%+d min, DST=%+d min\n",
                  snapshot.totalOffsetMinutes, snapshot.standardOffsetMinutes,
                  snapshot.dstOffsetMinutes);
  } else {
    M5.Log.println("Local       : unavailable (timezone reconfiguration required)");
  }
}

void cmdRtcStatus() {
  RtcTimezoneSnapshot snapshot{};
  const bool read = rtc.getTimezoneSnapshot(snapshot);
  const RtcStatus& status = snapshot.status;
  M5.Log.println("=== RTC Status ===");
  M5.Log.printf("Detected    : %s\n", status.detected ? "Yes" : "No");
  M5.Log.printf("I2C Read    : %s\n", status.busReadable ? "OK" : "Failed");
  M5.Log.printf("Clock       : %s\n", status.clockStopped ? "Stopped" : "Running");
  M5.Log.printf("Voltage Low : %s\n", status.voltageLow ? "Detected" : "No");
  M5.Log.printf("RTC Calendar: %s\n", status.hardwareTimeValid ? "Valid" : "Invalid");
  M5.Log.printf("UTC Valid   : %s\n", status.utcValid ? "Yes" : "No");
  M5.Log.printf("NVS State   : %s (%s)\n", configStateName(rtc.getConfigState()),
                status.nvsPersisted ? "saved" : "not committed");
  printTimezone();
  M5.Log.printf("TZDB        : %s\n", RtcTimezoneDatabase::TZDB_VERSION);
  if (read && status.utcValid) {
    printDateTime("UTC", snapshot.utc);
    if (snapshot.localValid) {
      printDateTime("Local", snapshot.local);
      M5.Log.printf("UTC offset  : total=%+d min, standard=%+d min, DST=%+d min\n",
                    snapshot.totalOffsetMinutes, snapshot.standardOffsetMinutes,
                    snapshot.dstOffsetMinutes);
    }
  }
  if (!status.utcValid) {
    M5.Log.println("Status      : SYNC REQUIRED");
  } else if (!status.zoneResolved) {
    M5.Log.println("Status      : TIMEZONE RECONFIGURATION REQUIRED (UTC retained)");
  } else {
    M5.Log.println("Status      : OK");
  }
}

void cmdRtcTimezone(int argc, const String* argv) {
  if (argc == 3 && argv[2].equalsIgnoreCase("get")) {
    printTimezone();
    return;
  }
  if (argc == 4 && argv[2].equalsIgnoreCase("set")) {
    const RtcOperationResult result = rtc.setTimezoneName(argv[3].c_str());
    if (result == RtcOperationResult::Ok) {
      M5.Log.println("OK: timezone saved; UTC RTC was not changed");
      printTimezone();
    } else {
      M5.Log.printf("Error: %s\n", operationResultName(result));
    }
    return;
  }
  M5.Log.println("Usage: rtc timezone get");
  M5.Log.println("       rtc timezone set <Area/City>");
}

void cmdRtcSync(int argc, const String* argv) {
  if (argc != 4) {
    M5.Log.println("Usage: rtc sync <unix-seconds> <Area/City>");
    return;
  }
  int64_t unixSeconds = 0;
  uint32_t zoneId = 0;
  if (!parseUnixSeconds(argv[2], unixSeconds)) {
    M5.Log.println("Error: invalid Unix seconds");
    return;
  }
  if (!RtcTimezoneDatabase::zoneIdForName(argv[3].c_str(), zoneId)) {
    M5.Log.println("Error: unsupported IANA timezone");
    return;
  }
  const RtcOperationResult result = rtc.syncUtcAndZone(unixSeconds, zoneId);
  if (result == RtcOperationResult::Ok) {
    M5.Log.println("OK: UTC and timezone synchronized transactionally");
    cmdRtcGet();
  } else {
    M5.Log.printf("Error: %s\n", operationResultName(result));
  }
}

void cmdRtcSet(int argc, const String* argv) {
  RtcDateTime local{};
  if (!parseLocalDateTime(argc, argv, local)) {
    M5.Log.println("Usage: rtc set YYYY MM DD HH MM SS");
    M5.Log.println("The value is interpreted in the configured IANA timezone.");
    return;
  }
  const RtcOperationResult result = rtc.syncLocalDateTime(local);
  if (result == RtcOperationResult::LocalTimeAmbiguous) {
    M5.Log.println("Error: local time is in a DST gap/fold; use rtc sync <unix-seconds> <Area/City>");
  } else if (result == RtcOperationResult::Ok) {
    M5.Log.println("OK: local time converted to UTC and synchronized");
  } else {
    M5.Log.printf("Error: %s\n", operationResultName(result));
  }
}

void printUsage() {
  M5.Log.println("RTC commands:");
  M5.Log.println("  rtc get");
  M5.Log.println("  rtc timezone get");
  M5.Log.println("  rtc timezone set <Area/City>");
  M5.Log.println("  rtc sync <unix-seconds> <Area/City>");
  M5.Log.println("  rtc set YYYY MM DD HH MM SS");
  M5.Log.println("  rtc status");
}

}  // namespace

bool cmdRtc(int argc, const String* argv) {
  if (argc < 2) {
    printUsage();
    return false;
  }
  String subCommand = argv[1];
  subCommand.toLowerCase();
  if (subCommand == "get") {
    cmdRtcGet();
  } else if (subCommand == "timezone") {
    cmdRtcTimezone(argc, argv);
  } else if (subCommand == "sync") {
    cmdRtcSync(argc, argv);
  } else if (subCommand == "set") {
    cmdRtcSet(argc, argv);
  } else if (subCommand == "status") {
    cmdRtcStatus();
  } else {
    printUsage();
    return false;
  }
  return true;
}
