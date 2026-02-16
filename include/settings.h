// ============================================================================
// Pod Settings — SD card primary, NVS fallback
// ============================================================================

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

// Platform — determines auth flow, presence API, and branding
enum Platform {
    PLATFORM_TEAMS = 0,
    PLATFORM_ZOOM  = 1,
    PLATFORM_COUNT = 2
};

const char* platformName(Platform p);

struct PodSettings {
    Platform platform     = PLATFORM_TEAMS;
    bool invertDisplay    = false;   // false = normal (white bg)
    bool audioAlerts      = false;   // false = silent
    int  presenceInterval = 120;     // seconds between presence polls
    int  fullRefreshEvery = 10;      // full refresh every N partial updates
};

// Load settings: tries SD card first, then NVS fallback.
void loadSettings(PodSettings& s);

// Save settings: writes to SD card (if available) and NVS.
void saveSettings(const PodSettings& s);

#endif
