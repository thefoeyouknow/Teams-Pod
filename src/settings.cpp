// ============================================================================
// Pod Settings — SD card primary, NVS fallback
// ============================================================================

#include "settings.h"
#include "sd_storage.h"
#include <Preferences.h>

static const char* SETTINGS_NS = "pod_settings";

void loadSettings(PodSettings& s) {
    // Try SD card first
    if (sdMounted()) {
        SdConfig cfg;
        if (sdLoadConfig(cfg)) {
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
        s.invertDisplay    = prefs.getBool("invert",   false);
        s.audioAlerts      = prefs.getBool("audio",    false);
        s.presenceInterval = prefs.getInt("interval",  30);
        s.fullRefreshEvery = prefs.getInt("fullEvery", 10);
        prefs.end();
        Serial.println("[Settings] Loaded from NVS");
    } else {
        Serial.println("[Settings] First boot — using defaults");
        prefs.begin(SETTINGS_NS, false);
        prefs.end();
    }

    Serial.printf("[Settings] invert=%d audio=%d interval=%d fullEvery=%d\n",
                  s.invertDisplay, s.audioAlerts,
                  s.presenceInterval, s.fullRefreshEvery);
}

void saveSettings(const PodSettings& s) {
    // Write to SD card if available
    if (sdMounted()) {
        SdConfig cfg;
        cfg.invertDisplay    = s.invertDisplay;
        cfg.audioAlerts      = s.audioAlerts;
        cfg.presenceInterval = s.presenceInterval;
        cfg.fullRefreshEvery = s.fullRefreshEvery;
        sdSaveConfig(cfg);
    }

    // Always write NVS as backup
    Preferences prefs;
    prefs.begin(SETTINGS_NS, false);
    prefs.putBool("invert",   s.invertDisplay);
    prefs.putBool("audio",    s.audioAlerts);
    prefs.putInt("interval",  s.presenceInterval);
    prefs.putInt("fullEvery", s.fullRefreshEvery);
    prefs.end();

    Serial.printf("[Settings] Saved: invert=%d audio=%d interval=%d fullEvery=%d\n",
                  s.invertDisplay, s.audioAlerts,
                  s.presenceInterval, s.fullRefreshEvery);
}
