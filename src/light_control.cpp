// ============================================================================
// Light Control — WLED and Smart Bulb HTTP control
// ============================================================================

#include "light_control.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>

static const char* LIGHT_NVS_NS = "pod_light";

// ============================================================================
// Type name
// ============================================================================

const char* lightTypeName(LightType t) {
    switch (t) {
        case LIGHT_WLED: return "WLED";
        case LIGHT_BULB: return "Smart Bulb";
        default:         return "None";
    }
}

// ============================================================================
// NVS persistence
// ============================================================================

void loadLightConfig(LightConfig& cfg) {
    Preferences prefs;
    if (prefs.begin(LIGHT_NVS_NS, true)) {
        cfg.type       = (LightType)prefs.getInt("type", LIGHT_NONE);
        cfg.ip         = prefs.getString("ip", "");
        cfg.brightness = prefs.getInt("bright", 128);
        prefs.end();
    }
    Serial.printf("[Light] Config: type=%s ip=%s bright=%d\n",
                  lightTypeName(cfg.type), cfg.ip.c_str(), cfg.brightness);
}

void saveLightConfig(const LightConfig& cfg) {
    Preferences prefs;
    prefs.begin(LIGHT_NVS_NS, false);
    prefs.putInt("type", (int)cfg.type);
    prefs.putString("ip", cfg.ip);
    prefs.putInt("bright", cfg.brightness);
    prefs.end();
    Serial.printf("[Light] Saved: type=%s ip=%s bright=%d\n",
                  lightTypeName(cfg.type), cfg.ip.c_str(), cfg.brightness);
}

// ============================================================================
// Presence → RGB colour mapping
// ============================================================================

static void presenceToRGB(const char* availability, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (strcmp(availability, "Available") == 0) {
        r = 0; g = 255; b = 0;         // green
    } else if (strcmp(availability, "Busy") == 0) {
        r = 255; g = 0; b = 0;         // red
    } else if (strcmp(availability, "DoNotDisturb") == 0) {
        r = 255; g = 0; b = 0;         // red
    } else if (strcmp(availability, "Away") == 0 ||
               strcmp(availability, "BeRightBack") == 0) {
        r = 255; g = 191; b = 0;       // amber/yellow
    } else if (strcmp(availability, "Offline") == 0) {
        r = 0; g = 0; b = 0;           // off
    } else {
        r = 80; g = 80; b = 80;        // dim white for unknown
    }
}

// ============================================================================
// WLED — JSON API (http://<ip>/json/state)
//
// Very reliable, works with any WLED firmware.
// Payload: {"on":true,"bri":128,"seg":[{"col":[[R,G,B]]}]}
// ============================================================================

static bool wled_setColor(const char* ip, uint8_t r, uint8_t g, uint8_t b, int brightness) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    String url = String("http://") + ip + "/json/state";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(3000);

    char payload[128];
    bool on = (r > 0 || g > 0 || b > 0);
    snprintf(payload, sizeof(payload),
             "{\"on\":%s,\"bri\":%d,\"seg\":[{\"col\":[[%d,%d,%d]]}]}",
             on ? "true" : "false", brightness, r, g, b);

    Serial.printf("[WLED] POST %s  %s\n", url.c_str(), payload);
    int code = http.POST(payload);
    http.end();

    if (code == 200) {
        Serial.println("[WLED] OK");
        return true;
    }
    Serial.printf("[WLED] Failed: HTTP %d\n", code);
    return false;
}

// ============================================================================
// Smart Bulb — Generic HTTP colour API
//
// Supports Tasmota-syntax by default:
//   http://<ip>/cm?cmnd=Color%20RRGGBB
// Also works with Shelly-style devices that take similar HTTP commands.
// ============================================================================

static bool bulb_setColor(const char* ip, uint8_t r, uint8_t g, uint8_t b) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    char url[128];

    if (r == 0 && g == 0 && b == 0) {
        // Turn off
        snprintf(url, sizeof(url), "http://%s/cm?cmnd=Power%%20Off", ip);
    } else {
        // Tasmota Color command
        snprintf(url, sizeof(url), "http://%s/cm?cmnd=Color%%20%02X%02X%02X", ip, r, g, b);
    }

    http.begin(url);
    http.setTimeout(3000);

    Serial.printf("[Bulb] GET %s\n", url);
    int code = http.GET();
    http.end();

    if (code == 200) {
        Serial.println("[Bulb] OK");
        return true;
    }
    Serial.printf("[Bulb] Failed: HTTP %d\n", code);
    return false;
}

// ============================================================================
// Public API
// ============================================================================

void lightSetPresence(const LightConfig& cfg, const char* availability) {
    if (cfg.type == LIGHT_NONE || cfg.ip.isEmpty()) return;

    uint8_t r, g, b;
    presenceToRGB(availability, r, g, b);
    lightSetColor(cfg, r, g, b);
}

void lightSetColor(const LightConfig& cfg, uint8_t r, uint8_t g, uint8_t b) {
    if (cfg.type == LIGHT_NONE || cfg.ip.isEmpty()) return;

    switch (cfg.type) {
        case LIGHT_WLED:
            wled_setColor(cfg.ip.c_str(), r, g, b, cfg.brightness);
            break;
        case LIGHT_BULB:
            bulb_setColor(cfg.ip.c_str(), r, g, b);
            break;
        default:
            break;
    }
}

void lightOff(const LightConfig& cfg) {
    lightSetColor(cfg, 0, 0, 0);
}

void lightTest(const LightConfig& cfg) {
    if (cfg.type == LIGHT_NONE || cfg.ip.isEmpty()) {
        Serial.println("[Light] Test skipped — no device configured");
        return;
    }
    Serial.println("[Light] Testing — R, G, B, Off");
    lightSetColor(cfg, 255, 0, 0);
    delay(700);
    lightSetColor(cfg, 0, 255, 0);
    delay(700);
    lightSetColor(cfg, 0, 0, 255);
    delay(700);
    lightOff(cfg);
}
