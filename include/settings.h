// ============================================================================
// Pod Settings — SD card primary, NVS fallback
// ============================================================================

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

struct PodSettings {
    bool invertDisplay   = false;   // false = normal (white bg)
    bool audioAlerts     = false;   // false = silent
    int  presenceInterval = 30;     // seconds between presence polls
    int  fullRefreshEvery = 10;     // full refresh every N partial updates
};

// Load settings: tries SD card first, then NVS fallback.
void loadSettings(PodSettings& s);

// Save settings: writes to SD card (if available) and NVS.
void saveSettings(const PodSettings& s);

#endif
