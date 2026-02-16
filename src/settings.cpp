// ============================================================================
// Pod Settings — SD card primary, NVS fallback
// ============================================================================

#include "settings.h"
#include "sd_storage.h"
#include <Preferences.h>

static const char* SETTINGS_NS = "pod_settings";

const char* platformName(Platform p) {
    switch (p) {
        case PLATFORM_TEAMS: return "Teams";
        case PLATFORM_ZOOM:  return "Zoom";
        default:             return "Unknown";
    }
}

void loadSettings(PodSettings& s) {
    // Try SD card first
    if (sdMounted()) {
        SdConfig cfg;
        if (sdLoadConfig(cfg)) {
            s.platform         = (Platform)cfg.platform;
            s.invertDisplay    = cfg.invertDisplay;
            s.audioAlerts      = cfg.audioAlerts;
            s.presenceInterval = cfg.presenceInterval;
            s.fullRefreshEvery = cfg.fullRefreshEvery;
            Serial.println("[Settings] Loaded from SD card");
            return;
        }
    }

    // Fallback: NVS
    Preferences prefs;
    if (prefs.begin(SETTINGS_NS, true)) {
        s.platform         = (Platform)prefs.getInt("platform", PLATFORM_TEAMS);
        s.invertDisplay    = prefs.getBool("invert",   false);
        s.audioAlerts      = prefs.getBool("audio",    false);
        s.presenceInterval = prefs.getInt("interval",  120);
        s.fullRefreshEvery = prefs.getInt("fullEvery", 10);
        prefs.end();
        Serial.println("[Settings] Loaded from NVS");
    } else {
        Serial.println("[Settings] First boot — using defaults");
        prefs.begin(SETTINGS_NS, false);
        prefs.end();
    }

    Serial.printf("[Settings] platform=%s invert=%d audio=%d interval=%d fullEvery=%d\n",
                  platformName(s.platform), s.invertDisplay, s.audioAlerts,
                  s.presenceInterval, s.fullRefreshEvery);
}

void saveSettings(const PodSettings& s) {
    // Write to SD card (primary store)
    if (sdMounted()) {
        SdConfig cfg;
        cfg.platform         = (int)s.platform;
        cfg.invertDisplay    = s.invertDisplay;
        cfg.audioAlerts      = s.audioAlerts;
        cfg.presenceInterval = s.presenceInterval;
        cfg.fullRefreshEvery = s.fullRefreshEvery;
        sdSaveConfig(cfg);
    } else {
        Serial.println("[Settings] WARNING: SD not mounted, settings not saved");
    }

    Serial.printf("[Settings] Saved: platform=%s invert=%d audio=%d interval=%d fullEvery=%d\n",
                  platformName(s.platform), s.invertDisplay, s.audioAlerts,
                  s.presenceInterval, s.fullRefreshEvery);
}
