#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>
#include "storage/sd_checked_file.h"
#include "storage/sd_fat_timestamp.h"
#include "storage/sd_log_lifetime_policy.h"
#include "storage/sd_log_sample_gate.h"
#include "sensor/i2c_retry_deadline.h"

static int syncResult = 0, closeResult = 0, openFlags = 0;
static ssize_t writeResult = 3;
static int fakeOpen(const char*, int flags, int) { openFlags = flags; return 42; }
static ssize_t fakeWrite(int, const void*, size_t) { errno = ENOSPC; return writeResult; }
static int fakeSync(int) { errno = EIO; return syncResult; }
static int fakeClose(int) { errno = EIO; return closeResult; }

int main() {
  assert((SD_FAT_TIME_UNSET >> 25) == 0U);
  assert(((SD_FAT_TIME_UNSET >> 21) & 15U) == 1U);
  assert(((SD_FAT_TIME_UNSET >> 16) & 31U) == 1U);
  assert((SD_FAT_TIME_UNSET & 65535U) == 0U);
  I2cRetryDeadline retry;
  retry.schedule(0xfffffc18U, 1000U);
  assert(retry.active(0xffffffffU));
  assert(!retry.active(0U));
  retry.schedule(0xfffffc16U, 1000U);
  assert(!retry.active(10U));
  retry.schedule(10U, 1000U);
  assert(retry.active(1009U));
  assert(!retry.active(1010U));
  retry.clear();
  assert(!retry.active(0U));

  const SdFileOperations operations = {fakeOpen, fakeWrite, fakeSync, fakeClose};
  SdCheckedFile file(&operations);
  assert(file.open("unused"));
  assert((openFlags & O_EXCL) != 0 && (openFlags & O_TRUNC) == 0);
  assert(file.print("abc") == 3);
  assert(file.size() == 3);
  syncResult = -1;
  assert(!file.flush() && file.error() == EIO);
  syncResult = 0;
  assert(file.flush());
  writeResult = 1;
  assert(file.print("abc") == 1 && file.error() == EIO);
  writeResult = -1;
  assert(file.print("abc") == 0 && file.error() == ENOSPC);
  closeResult = -1;
  assert(!file.close() && !file);

  char directory[] = "/tmp/ulsa-sd-storage-XXXXXX";
  assert(mkdtemp(directory));
  char path[128];
  snprintf(path, sizeof(path), "%s/log.csv", directory);
  SdCheckedFile actual;
  assert(actual.open(path));
  assert(actual.print("retained\n") == 9 && actual.flush() && actual.close());
  assert(!actual.open(path) && actual.error() == EEXIST);
  FILE* readback = fopen(path, "rb");
  char buffer[16] = {};
  assert(readback && fread(buffer, 1, 9, readback) == 9);
  assert(strcmp(buffer, "retained\n") == 0);
  fclose(readback);
  unlink(path);
  rmdir(directory);

  const uint64_t wrap = 1ULL << 32;
  assert(SdLogLifetimePolicy::extendCapture(0xfffffff0U, wrap + 10) == wrap - 16);
  assert(SdLogLifetimePolicy::extendCapture(0U, wrap + 10) == wrap);
  assert(SdLogLifetimePolicy::extendCapture(100U, wrap + 100) == wrap + 100);
  assert(!SdLogLifetimePolicy::rotate(1799999, 0, 0, 192));
  assert(SdLogLifetimePolicy::rotate(1800000, 0, 0, 192));
  assert(SdLogLifetimePolicy::rotate(1, 0, 16ULL * 1024 * 1024 - 100, 192));
  assert(SdLogLifetimePolicy::rotate(1, 0, UINT64_MAX, 192));
  for (uint32_t interval : {20U, 100U, 1000U}) {
    SdLogSampleGate gate(interval);
    uint64_t openedAt = 0, count = 0, segments = 1;
    for (uint64_t t = 0; t < 60ULL * 86400000ULL; t += interval) {
      assert(gate.isDue(static_cast<uint32_t>(t)));
      gate.accept(static_cast<uint32_t>(t));
      assert(SdLogLifetimePolicy::extendCapture(static_cast<uint32_t>(t), t) == t);
      if (SdLogLifetimePolicy::rotate(t, openedAt, 0, 192)) {
        openedAt = t;
        ++segments;
      }
      ++count;
    }
    assert(segments == 2880);
    printf("60 days: %u ms, %llu samples, %llu segments passed\n", interval,
           (unsigned long long)count, (unsigned long long)segments);
  }
  puts("SD storage failures, exclusive creation, rollover and rotation passed");
}
