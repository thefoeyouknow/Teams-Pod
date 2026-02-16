// ============================================================================
// Light Control — WLED, Tasmota, Philips Hue, WiZ Connected
// ============================================================================

#include "light_control.h"
#include "light_devices.h"
#include "sd_storage.h"
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <math.h>

static const char* LIGHT_NVS_NS  = "pod_light";
static const char* LIGHT_SD_PATH = "/light_config.json";

// ============================================================================
// Type name
// ============================================================================

const char* lightTypeName(LightType t) {
    switch (t) {
        case LIGHT_WLED: return "WLED";
        case LIGHT_BULB: return "Tasmota";
        case LIGHT_HUE:  return "Hue";
        case LIGHT_WIZ:  return "WiZ";
        default:         return "None";
    }
}

// ============================================================================
// NVS persistence
// ============================================================================

// ============================================================================
// Persistence — SD primary, NVS read-only fallback
// ============================================================================

void loadLightConfig(LightConfig& cfg) {
    // Try SD first
    if (sdMounted()) {
        String json = sdReadText(LIGHT_SD_PATH);
        if (json.length() > 0) {
            StaticJsonDocument<256> doc;
            if (!deserializeJson(doc, json)) {
                cfg.type       = (LightType)(doc["type"] | (int)LIGHT_NONE);
                cfg.ip         = doc["ip"]     | "";
                cfg.brightness = doc["bright"] | 128;
                cfg.key        = doc["key"]    | "";
                cfg.aux        = doc["aux"]    | "1";
                Serial.printf("[Light] Config from SD: type=%s ip=%s\n",
                              lightTypeName(cfg.type), cfg.ip.c_str());
                return;
            }
        }
    }

    // Fallback: NVS (legacy / no SD)
    Preferences prefs;
    if (prefs.begin(LIGHT_NVS_NS, true)) {
        cfg.type       = (LightType)prefs.getInt("type", LIGHT_NONE);
        cfg.ip         = prefs.getString("ip", "");
        cfg.brightness = prefs.getInt("bright", 128);
        cfg.key        = prefs.getString("key", "");
        cfg.aux        = prefs.getString("aux", "1");
        prefs.end();
    }
    Serial.printf("[Light] Config from NVS fallback: type=%s ip=%s bright=%d\n",
                  lightTypeName(cfg.type), cfg.ip.c_str(), cfg.brightness);
}

void saveLightConfig(const LightConfig& cfg) {
    // Write to SD only
    if (sdMounted()) {
        StaticJsonDocument<256> doc;
        doc["type"]   = (int)cfg.type;
        doc["ip"]     = cfg.ip;
        doc["bright"] = cfg.brightness;
        doc["key"]    = cfg.key;
        doc["aux"]    = cfg.aux;

        String json;
        serializeJsonPretty(doc, json);
        sdWriteText(LIGHT_SD_PATH, json);
    } else {
        Serial.println("[Light] WARNING: SD not mounted, light config not saved");
    }
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
// Smart Bulb — Tasmota HTTP colour API
// ============================================================================

static bool bulb_setColor(const char* ip, uint8_t r, uint8_t g, uint8_t b) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    char url[128];

    if (r == 0 && g == 0 && b == 0) {
        snprintf(url, sizeof(url), "http://%s/cm?cmnd=Power%%20Off", ip);
    } else {
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
// Philips Hue — Bridge REST API
//
// PUT http://<bridge>/api/<key>/lights/<id>/state
// Colour via CIE xy coordinates for accuracy.
// ============================================================================

static void rgbToHueXY(uint8_t r8, uint8_t g8, uint8_t b8,
                       float& x, float& y, uint8_t& bri) {
    // sRGB → linear
    float rf = r8 / 255.0f;
    float gf = g8 / 255.0f;
    float bf = b8 / 255.0f;
    rf = (rf > 0.04045f) ? powf((rf + 0.055f) / 1.055f, 2.4f) : rf / 12.92f;
    gf = (gf > 0.04045f) ? powf((gf + 0.055f) / 1.055f, 2.4f) : gf / 12.92f;
    bf = (bf > 0.04045f) ? powf((bf + 0.055f) / 1.055f, 2.4f) : bf / 12.92f;

    // Wide gamut D65 conversion
    float X = rf * 0.664511f + gf * 0.154324f + bf * 0.162028f;
    float Y = rf * 0.283881f + gf * 0.668433f + bf * 0.047685f;
    float Z = rf * 0.000088f + gf * 0.072310f + bf * 0.986039f;

    float sum = X + Y + Z;
    if (sum > 0.0f) {
        x = X / sum;
        y = Y / sum;
    } else {
        x = 0.3227f;
        y = 0.3290f;   // D65 white point
    }
    bri = (uint8_t)(Y * 254.0f);
    if (bri < 1 && (r8 > 0 || g8 > 0 || b8 > 0)) bri = 1;
}

static bool hue_setColor(const char* ip, const char* apiKey, const char* lightId,
                         uint8_t r, uint8_t g, uint8_t b) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    char url[192];
    snprintf(url, sizeof(url), "http://%s/api/%s/lights/%s/state", ip, apiKey, lightId);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(3000);

    char payload[96];
    if (r == 0 && g == 0 && b == 0) {
        snprintf(payload, sizeof(payload), "{\"on\":false}");
    } else {
        float x, y;
        uint8_t bri;
        rgbToHueXY(r, g, b, x, y, bri);
        snprintf(payload, sizeof(payload),
                 "{\"on\":true,\"bri\":%d,\"xy\":[%.4f,%.4f]}", bri, x, y);
    }

    Serial.printf("[Hue] PUT %s  %s\n", url, payload);
    int code = http.PUT(payload);
    http.end();

    if (code == 200) {
        Serial.println("[Hue] OK");
        return true;
    }
    Serial.printf("[Hue] Failed: HTTP %d\n", code);
    return false;
}

// ============================================================================
// WiZ Connected — UDP control (port 38899)
//
// No hub required — direct UDP to bulb IP.
// {"method":"setPilot","params":{"r":R,"g":G,"b":B,"dimming":D}}
// ============================================================================

static WiFiUDP wizUdp;

static bool wiz_setColor(const char* ip, uint8_t r, uint8_t g, uint8_t b, int brightness) {
    if (WiFi.status() != WL_CONNECTED) return false;

    char payload[128];
    if (r == 0 && g == 0 && b == 0) {
        snprintf(payload, sizeof(payload),
                 "{\"method\":\"setPilot\",\"params\":{\"state\":false}}");
    } else {
        int dim = map(brightness, 0, 255, 10, 100);
        snprintf(payload, sizeof(payload),
                 "{\"method\":\"setPilot\",\"params\":{\"r\":%d,\"g\":%d,\"b\":%d,\"dimming\":%d}}",
                 r, g, b, dim);
    }

    Serial.printf("[WiZ] UDP %s:38899  %s\n", ip, payload);

    IPAddress addr;
    if (!addr.fromString(ip)) {
        Serial.println("[WiZ] Invalid IP");
        return false;
    }

    wizUdp.beginPacket(addr, 38899);
    wizUdp.print(payload);
    bool ok = wizUdp.endPacket();

    Serial.printf("[WiZ] %s\n", ok ? "OK" : "Send failed");
    return ok;
}

// ============================================================================
// Public API
// ============================================================================

void lightSetPresence(const LightConfig& cfg, const char* availability) {
    if (cfg.type == LIGHT_NONE) return;

    // WLED: use preset swarm if any WLED devices are in the device list
    if (cfg.type == LIGHT_WLED) {
        auto& devs = lightDevicesGet();
        bool hasSwarm = false;
        for (auto& d : devs) {
            if (d.type == LIGHT_WLED) { hasSwarm = true; break; }
        }
        if (hasSwarm) {
            int preset = wledPresetForPresence(availability);
            wledActivatePresetAll(preset);
            return;
        }
    }

    // Legacy path: direct RGB for configured single device
    if (cfg.ip.isEmpty()) return;
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
        case LIGHT_HUE:
            hue_setColor(cfg.ip.c_str(), cfg.key.c_str(),
                         cfg.aux.isEmpty() ? "1" : cfg.aux.c_str(), r, g, b);
            break;
        case LIGHT_WIZ:
            wiz_setColor(cfg.ip.c_str(), r, g, b, cfg.brightness);
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
