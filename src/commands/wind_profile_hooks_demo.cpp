/**
 * @file wind_profile_hooks_demo.cpp
 * @brief Demo profile hooks that expose application wind diagnostics.
 */

#include "wind_profile_hooks.h"

void printWindProfileStats() {}

void printWindProfileStatsUsage(bool nested) {
    Serial.println(nested ? "       wind i2c stats" : "Usage: wind i2c stats");
}

bool handleWindProfileStatsAction(const String&) { return false; }
