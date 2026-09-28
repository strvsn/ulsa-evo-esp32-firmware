#ifndef ULSA_SD_FAT_TIMESTAMP_H
#define ULSA_SD_FAT_TIMESTAMP_H
#include <stdint.h>

// FAT starts at 1980 and cannot represent Unix epoch or an absent date. This
// reserved value means unknown, never a measured wall-clock timestamp.
static constexpr uint32_t SD_FAT_TIME_UNSET = (1UL << 21U) | (1UL << 16U);
#endif
