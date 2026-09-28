#ifndef ULSA_WIND_PROFILE_HOOKS_H
#define ULSA_WIND_PROFILE_HOOKS_H

#include <Arduino.h>

void printWindProfileStats();
void printWindProfileStatsUsage(bool nested);
bool handleWindProfileStatsAction(const String& action);

#endif
