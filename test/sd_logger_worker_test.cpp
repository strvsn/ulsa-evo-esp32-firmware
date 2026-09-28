#include "storage/sd_logger.h"
#include <assert.h>
#include <fstream>
#include <iostream>
#include <functional>
#include <filesystem>
#include "storage/sd_fat_timestamp.h"
extern "C" uint32_t __wrap_get_fattime(void);

static std::atomic<bool> holdWrites{false}, writeEntered{false}, failSync{false};
static std::atomic<bool> failWriteNoSpace{false};
static std::atomic<unsigned> delayedWriteAfterReleaseMs{0};
static std::atomic<unsigned> openedFiles{0}, closedFiles{0};
static int openFile(const char* path, int flags, int mode) {
  const int fd = ::open(path, flags, mode);
  if (fd >= 0) ++openedFiles;
  return fd;
}
static int closeFile(int fd) { ++closedFiles; return ::close(fd); }
static ssize_t writeFile(int fd, const void* data, size_t size) {
  if (failWriteNoSpace.exchange(false)) { errno = ENOSPC; return -1; }
  writeEntered = true;
  while (holdWrites) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const unsigned delayMs = delayedWriteAfterReleaseMs.exchange(0);
  if (delayMs) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
  return ::write(fd, data, size);
}
static int syncFile(int fd) {
  if (failSync.exchange(false)) { errno = EIO; return -1; }
  return ::fsync(fd);
}
static bool waitFor(const std::function<bool()>& condition) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!condition()) {
    if (std::chrono::steady_clock::now() >= deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}
static WindData sample() {
  WindData value;
  value.timestamp = millis();
  value.sourceTimestampValid = false;
  value.isValid = true;
  value.windSpeed = 1.25;
  return value;
}

int main() {
  char root[] = "/tmp/ulsa-sd-worker-XXXXXX";
  assert(mkdtemp(root)); sdTestRoot = root;
  static const SdFileOperations operations = {openFile, writeFile, syncFile, closeFile};
  auto* rtc = new RtcManager;
  rtc->valid = false;
  auto* logger = new SdLogger(false, &operations);
  assert(logger->begin(rtc));
  assert(!logger->isRecordingRequested());
  assert(logger->resumeLogging());

  holdWrites = true;
  assert(logger->log(sample()));
  assert(waitFor([] { return writeEntered.load(); }));
  unsigned accepted = 1;
  const auto started = std::chrono::steady_clock::now();
  for (unsigned n = 0; n < 300; ++n) {
    sdTestClockOffset += 20;
    if (logger->log(sample())) ++accepted;
  }
  const auto elapsed = std::chrono::steady_clock::now() - started;
  assert(elapsed < std::chrono::milliseconds(200));
  assert(logger->getQueueDepth() == 256);
  assert(logger->getDroppedLogCount() == 301 - accepted);
  // A saturated sample queue must still accept the durability command after
  // the current slow write finishes; the command then drains accepted rows.
  delayedWriteAfterReleaseMs = 250;
  holdWrites = false;
  assert(logger->flush());
  assert(logger->getSyncedLogCount() == accepted);
  assert(logger->log(sample()));
  assert(logger->resumeLogging()); // duplicate START cannot discard pending sync
  assert(logger->flush());
  assert(logger->getSyncedLogCount() == accepted + 1);
  const std::string firstPath = logger->getCurrentFilePath();
  assert(firstPath.find("/boot_logs/") == 0);
  assert(__wrap_get_fattime() == SD_FAT_TIME_UNSET);

  sdTestClockOffset += (1ULL << 32);
  assert(logger->log(sample()) && logger->flush());
  assert(firstPath != logger->getCurrentFilePath());
  {
    std::ifstream file(sdTestRoot + logger->getCurrentFilePath());
    std::string header, row; getline(file, header); getline(file, row);
    assert(header.find("record_index,dropped_total,uncertain_total") != std::string::npos);
    assert(row.find("BOOT+") == 0);
    const auto firstComma = row.find(',');
    assert(row[firstComma + 1] == ',');
    assert(std::stoull(row.substr(5, firstComma - 5)) > (1ULL << 32));
  }

  rtc->valid = true; rtc->date.minute = 10;
  assert(logger->log(sample()) && logger->flush());
  const std::string firstRtcPath = logger->getCurrentFilePath();
  assert(firstRtcPath.find("/20260905/20260905_121000.csv") != std::string::npos);
  logger->stop();
  assert(waitFor([&] { return logger->isStorageQuiescent(); }));
  assert(logger->resumeLogging());
  assert(logger->log(sample()) && logger->flush());
  const std::string duplicateRtcPath = logger->getCurrentFilePath();
  assert(duplicateRtcPath.find("/20260905/20260905_121000_01.csv") != std::string::npos);
  const std::string beforeRollback = duplicateRtcPath;
  rtc->date.minute = 5;
  assert(logger->log(sample()) && logger->flush());
  assert(beforeRollback != logger->getCurrentFilePath());
  rtc->date.second = 30;
  assert(logger->log(sample()) && logger->flush());
  const std::string beforeSecondRollback = logger->getCurrentFilePath();
  rtc->date.second = 20;
  assert(logger->log(sample()) && logger->flush());
  assert(beforeSecondRollback != logger->getCurrentFilePath());
  sdTestClockOffset += 3100;
  assert(logger->log(sample()) && logger->flush());
  assert(!logger->isRtcTimestampingAvailable());
  rtc->date.second = 21;
  assert(logger->log(sample()) && logger->flush());
  assert(logger->isRtcTimestampingAvailable());
  assert(__wrap_get_fattime() != SD_FAT_TIME_UNSET);

  const auto synchronized = logger->getSyncedLogCount();
  assert(logger->log(sample()));
  failSync = true;
  assert(!logger->flush());
  assert(logger->getSyncedLogCount() == synchronized);
  assert(logger->getUncertainLogCount() >= 1);
  assert(waitFor([&] { return logger->isRecovering(); }));
  sdTestClockOffset += 1100;
  assert(waitFor([&] { return !logger->isRecovering() && logger->isLoggingEnabled(); }));
  assert(logger->log(sample()) && logger->flush());

  logger->setInputActive(false);
  assert(logger->isInputPaused() && !logger->isLoggingEnabled());
  assert(!logger->shouldLogSample(millis()));
  logger->setInputActive(true);
  assert(logger->isRecordingRequested());
  assert(logger->resumeLogging());
  assert(logger->log(sample()));
  sdTestPresent = false;
  failSync = true;
  assert(!logger->flush());
  assert(waitFor([&] { return logger->isRecovering(); }));
  const unsigned beforeFailedMounts = sdTestMountAttempts;
  for (unsigned attempt = 0; attempt < 5; ++attempt) {
    sdTestClockOffset += 32000;
    assert(waitFor([&] { return sdTestMountAttempts >= beforeFailedMounts + attempt + 1; }));
    delay(25);
  }
  assert(waitFor([&] { return !logger->isRecordingRequested(); }));
  assert(logger->getStopReason() == SD_STOP_RETRY_EXHAUSTED);
  sdTestClockOffset += 3600000;
  delay(30);
  assert(sdTestMountAttempts == beforeFailedMounts + 5);
  sdTestPresent = true;
  assert(logger->resumeLogging());
  logger->stop();
  assert(waitFor([&] { return logger->isStorageQuiescent(); }));

  rtc->valid = false;
  assert(logger->resumeLogging());
  const unsigned openedBeforeSoak = openedFiles;
  for (unsigned segment = 0; segment < 2880; ++segment) {
    sdTestClockOffset += 1800000;
    assert(logger->log(sample()) && logger->flush());
    assert(openedFiles - closedFiles <= 1);
  }
  assert(openedFiles - openedBeforeSoak == 2880);
  logger->stop();
  assert(waitFor([&] { return logger->isStorageQuiescent(); }));
  assert(openedFiles == closedFiles);

  sdTestUsed = sdTestCapacity - 1024 * 1024;
  assert(!logger->resumeLogging());
  assert(logger->getStopReason() == SD_STOP_CAPACITY);
  assert(std::filesystem::exists(sdTestRoot + firstPath));
  logger->stop();
  assert(waitFor([&] { return logger->isStorageQuiescent(); }));
  sdTestUsed = 0;
  assert(logger->resumeLogging());
  failWriteNoSpace = true;
  assert(logger->log(sample()));
  assert(waitFor([&] { return !logger->isRecordingRequested(); }));
  assert(logger->getStopReason() == SD_STOP_CAPACITY);
  assert(!logger->isRecovering());
  logger->stop();
  assert(waitFor([&] { return logger->isStorageQuiescent(); }));
  std::filesystem::remove_all(sdTestRoot);
  std::cout << "Actual logger: bounded producer, drain, sync fault, recovery, clocks and capacity passed\n" << std::flush;
  // The firmware worker has application lifetime. Terminate the test process
  // without destructing its live static scheduler mocks.
  std::_Exit(0);
}
