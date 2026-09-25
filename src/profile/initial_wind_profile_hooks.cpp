/**
 * @file initial_wind_profile_hooks.cpp
 * @brief Initial profile implementation of shared wind command hooks.
 */

#include "wind_profile_hooks.h"

void printWindProfileStats() {}

void printWindProfileStatsUsage(bool nested) {
    Serial.println(nested ? "       wind i2c stats" : "Usage: wind i2c stats");
}

bool handleWindProfileStatsAction(const String&) {
    return false;
}
