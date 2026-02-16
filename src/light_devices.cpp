// ============================================================================
// Light Devices — discovery, tracking, and provisioning
// ============================================================================

#include "light_devices.h"
#include "sd_storage.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

// ============================================================================
// In-memory device list
// ============================================================================

static std::vector<LightDevice> g_devices;

std::vector<LightDevice>& lightDevicesGet() { return g_devices; }

// ============================================================================
// SD persistence — /lights.json
// ============================================================================

static const char* LIGHTS_PATH = "/lights.json";

bool lightDevicesSave() {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();

    for (auto& d : g_devices) {
        JsonObject obj = arr.createNestedObject();
        obj["name"]        = d.name;
        obj["type"]        = (int)d.type;
        obj["ip"]          = d.ip;
        obj["id"]          = d.id;
        obj["provisioned"] = d.provisioned;
    }

    String json;
    serializeJsonPretty(doc, json);
    return sdWriteText(LIGHTS_PATH, json);
}

bool lightDevicesLoad() {
    String json = sdReadText(LIGHTS_PATH);
    if (json.isEmpty()) return false;

    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, json)) {
        Serial.println("[Lights] Failed to parse lights.json");
        return false;
    }

    g_devices.clear();
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        LightDevice d;
        d.name        = obj["name"]        | "Unknown";
        d.type        = (LightType)(obj["type"] | (int)LIGHT_NONE);
        d.ip          = obj["ip"]          | "";
        d.id          = obj["id"]          | "";
        d.provisioned = obj["provisioned"] | false;
        d.responding  = true;  // assume responding until verified
        g_devices.push_back(d);
    }

    Serial.printf("[Lights] Loaded %d devices from SD\n", g_devices.size());
    return true;
}

// ============================================================================
// Helper: find existing device by IP to avoid duplicates
// ============================================================================

static LightDevice* findByIP(const String& ip) {
    for (auto& d : g_devices) {
        if (d.ip == ip) return &d;
    }
    return nullptr;
}

// ============================================================================
// mDNS WLED Discovery — _wled._tcp
// ============================================================================

int lightDiscoverWLED() {
    Serial.println("[Lights] mDNS: scanning for WLED devices...");

    if (!MDNS.begin("statuspod")) {
        Serial.println("[Lights] mDNS init failed");
        return 0;
    }

    int n = MDNS.queryService("wled", "tcp");
    int added = 0;

    Serial.printf("[Lights] mDNS: found %d WLED service(s)\n", n);

    for (int i = 0; i < n; i++) {
        String ip   = MDNS.IP(i).toString();
        String name = MDNS.hostname(i);
        if (name.isEmpty()) name = "WLED-" + ip;

        Serial.printf("[Lights]   %s @ %s\n", name.c_str(), ip.c_str());

        LightDevice* existing = findByIP(ip);
        if (existing) {
            existing->name = name;
            existing->type = LIGHT_WLED;
            existing->responding = true;
        } else {
            LightDevice d;
            d.name        = name;
            d.type        = LIGHT_WLED;
            d.ip          = ip;
            d.provisioned = false;
            d.responding  = true;
            g_devices.push_back(d);
            added++;
        }
    }

    MDNS.end();
    Serial.printf("[Lights] WLED discovery: %d new device(s)\n", added);
    return added;
}

// ============================================================================
// WiZ UDP Discovery — broadcast on port 38899
// ============================================================================

int lightDiscoverWiZ() {
    Serial.println("[Lights] UDP: scanning for WiZ devices...");

    WiFiUDP udp;
    if (!udp.begin(38899)) {
        Serial.println("[Lights] UDP bind failed");
        return 0;
    }

    // Send registration broadcast
    const char* probe = "{\"method\":\"registration\",\"params\":{\"phoneMac\":\"aabbccddeeff\",\"register\":false,\"phoneIp\":\"1.2.3.4\",\"id\":\"1\"}}";
    IPAddress broadcast = WiFi.localIP();
    broadcast[3] = 255;  // simple /24 broadcast

    udp.beginPacket(broadcast, 38899);
    udp.print(probe);
    udp.endPacket();

    int added = 0;
    unsigned long start = millis();

    // Listen for 2 seconds for responses
    while (millis() - start < 2000) {
        int packetSize = udp.parsePacket();
        if (packetSize > 0) {
            char buf[512];
            int len = udp.read(buf, sizeof(buf) - 1);
            buf[len] = 0;

            String ip = udp.remoteIP().toString();

            // Parse the response to get module name
            StaticJsonDocument<512> doc;
            String devName = "WiZ-" + ip;
            if (!deserializeJson(doc, buf)) {
                const char* mn = doc["result"]["moduleName"];
                if (mn) devName = String(mn);
            }

            Serial.printf("[Lights]   WiZ: %s @ %s\n", devName.c_str(), ip.c_str());

            LightDevice* existing = findByIP(ip);
            if (existing) {
                existing->name = devName;
                existing->type = LIGHT_WIZ;
                existing->responding = true;
            } else {
                LightDevice d;
                d.name        = devName;
                d.type        = LIGHT_WIZ;
                d.ip          = ip;
                d.provisioned = true;  // WiZ needs no provisioning
                d.responding  = true;
                g_devices.push_back(d);
                added++;
            }
        }
        delay(10);
    }

    udp.stop();
    Serial.printf("[Lights] WiZ discovery: %d new device(s)\n", added);
    return added;
}

// ============================================================================
// Hue Bridge Enumeration — lights, groups, rooms
// ============================================================================

int lightDiscoverHue(const String& bridgeIp, const String& apiKey) {
    if (bridgeIp.isEmpty() || apiKey.isEmpty()) return 0;
    Serial.printf("[Lights] Querying Hue bridge at %s...\n", bridgeIp.c_str());

    HTTPClient http;
    int added = 0;

    // --- Lights ---
    {
        String url = "http://" + bridgeIp + "/api/" + apiKey + "/lights";
        http.begin(url);
        http.setTimeout(5000);
        int code = http.GET();
        if (code == 200) {
            DynamicJsonDocument doc(8192);
            if (!deserializeJson(doc, http.getStream())) {
                JsonObject root = doc.as<JsonObject>();
                for (JsonPair kv : root) {
                    String lightId = kv.key().c_str();
                    const char* name = kv.value()["name"] | "Hue Light";
                    String dispName = String(name) + " (L" + lightId + ")";
                    String devIp = bridgeIp;  // all go through bridge

                    // Check if we already track this Hue target
                    String targetId = "L" + lightId;
                    bool found = false;
                    for (auto& d : g_devices) {
                        if (d.type == LIGHT_HUE && d.id == targetId) {
                            d.name = dispName;
                            d.responding = true;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        LightDevice d;
                        d.name        = dispName;
                        d.type        = LIGHT_HUE;
                        d.ip          = devIp;
                        d.id          = targetId;
                        d.provisioned = true;  // Hue needs no provisioning
                        d.responding  = true;
                        g_devices.push_back(d);
                        added++;
                    }
                    Serial.printf("[Lights]   Hue light: %s [%s]\n",
                                  dispName.c_str(), targetId.c_str());
                }
            }
        }
        http.end();
    }

    // --- Groups (includes rooms) ---
    {
        String url = "http://" + bridgeIp + "/api/" + apiKey + "/groups";
        http.begin(url);
        http.setTimeout(5000);
        int code = http.GET();
        if (code == 200) {
            DynamicJsonDocument doc(8192);
            if (!deserializeJson(doc, http.getStream())) {
                JsonObject root = doc.as<JsonObject>();
                for (JsonPair kv : root) {
                    String groupId = kv.key().c_str();
                    const char* name  = kv.value()["name"]  | "Hue Group";
                    const char* gtype = kv.value()["type"]  | "LightGroup";

                    // Prefix: R for Room, G for other groups
                    String prefix = (strcmp(gtype, "Room") == 0) ? "R" : "G";
                    String targetId = prefix + groupId;
                    String label = (strcmp(gtype, "Room") == 0) ? "Room" : "Group";
                    String dispName = String(name) + " (" + label + " " + groupId + ")";

                    bool found = false;
                    for (auto& d : g_devices) {
                        if (d.type == LIGHT_HUE && d.id == targetId) {
                            d.name = dispName;
                            d.responding = true;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        LightDevice d;
                        d.name        = dispName;
                        d.type        = LIGHT_HUE;
                        d.ip          = bridgeIp;
                        d.id          = targetId;
                        d.provisioned = true;
                        d.responding  = true;
                        g_devices.push_back(d);
                        added++;
                    }
                    Serial.printf("[Lights]   Hue %s: %s [%s]\n",
                                  label.c_str(), dispName.c_str(), targetId.c_str());
                }
            }
        }
        http.end();
    }

    Serial.printf("[Lights] Hue enumeration: %d new target(s)\n", added);
    return added;
}

// ============================================================================
// Discover all — runs appropriate discovery per configured light type
// ============================================================================

int lightDiscoverAll(const LightConfig& cfg) {
    int total = 0;

    // Always scan for WLED (mDNS is cheap)
    total += lightDiscoverWLED();

    // WiZ discovery if WiZ is configured or any WiZ devices exist
    if (cfg.type == LIGHT_WIZ) {
        total += lightDiscoverWiZ();
    } else {
        // Also discover if we have existing WiZ devices
        for (auto& d : g_devices) {
            if (d.type == LIGHT_WIZ) {
                total += lightDiscoverWiZ();
                break;
            }
        }
    }

    // Hue enumeration if bridge IP and key are configured
    if (cfg.type == LIGHT_HUE && !cfg.ip.isEmpty() && !cfg.key.isEmpty()) {
        total += lightDiscoverHue(cfg.ip, cfg.key);
    }

    lightDevicesSave();
    return total;
}

// ============================================================================
// WLED Preset Control
// ============================================================================

int wledPresetForPresence(const char* availability) {
    if (strcmp(availability, "Available") == 0)       return 1;
    if (strcmp(availability, "Away") == 0)            return 2;
    if (strcmp(availability, "BeRightBack") == 0)     return 2;
    if (strcmp(availability, "Busy") == 0)            return 3;
    if (strcmp(availability, "DoNotDisturb") == 0)    return 4;
    if (strcmp(availability, "InACall") == 0)         return 5;
    if (strcmp(availability, "InAMeeting") == 0)      return 5;
    if (strcmp(availability, "Presenting") == 0)      return 5;
    if (strcmp(availability, "InAConferenceCall") == 0) return 5;
    if (strcmp(availability, "Offline") == 0)         return 6;
    if (strcmp(availability, "PresenceUnknown") == 0) return 6;
    return 6;  // default to offline/off
}

bool wledActivatePreset(const String& ip, int presetId) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    String url = "http://" + ip + "/win&PL=" + String(presetId);
    http.begin(url);
    http.setTimeout(3000);

    Serial.printf("[WLED] GET %s\n", url.c_str());
    int code = http.GET();
    http.end();

    if (code == 200) {
        Serial.printf("[WLED] Preset %d activated on %s\n", presetId, ip.c_str());
        return true;
    }
    Serial.printf("[WLED] Failed: HTTP %d from %s\n", code, ip.c_str());
    return false;
}

void wledActivatePresetAll(int presetId) {
    for (auto& d : g_devices) {
        if (d.type == LIGHT_WLED && d.responding) {
            if (!wledActivatePreset(d.ip, presetId)) {
                d.responding = false;
            }
        }
    }
}

// ============================================================================
// WLED Provisioning — upload 6 presets (presence statuses)
//
// Stripped of segment geometry so target keeps its own LED config.
// POSTs to /json/presets with the preset pack.
// ============================================================================

// Preset JSON — derived from exported wled_presets_BusyLight.json.
// Geometry stripped (no start/stop/grp/spc/of/id, no empty segment slots)
// so the target WLED keeps its own LED layout.  Colors, effects, and
// custom params preserved exactly from the working export.
static const char* WLED_PRESET_PAYLOAD = R"({
  "0":{},
  "1":{"on":true,"bri":255,"transition":7,"mainseg":0,
       "seg":[{"on":true,"bri":255,"col":[[0,0,0],[8,255,0],[0,0,0]],
               "fx":2,"sx":94,"ix":128,"pal":2,"c1":128,"c2":128,"c3":16}],
       "n":"Available"},
  "2":{"on":true,"bri":255,"transition":7,"mainseg":0,
       "seg":[{"on":true,"bri":255,"col":[[0,0,0],[255,200,0],[0,0,0]],
               "fx":2,"sx":50,"ix":128,"pal":2,"c1":128,"c2":128,"c3":16}],
       "n":"Away"},
  "3":{"on":true,"bri":255,"transition":7,"mainseg":0,
       "seg":[{"on":true,"bri":255,"col":[[255,0,0],[255,0,0],[0,0,0]],
               "fx":0,"sx":128,"ix":128,"pal":2,"c1":128,"c2":128,"c3":16}],
       "n":"Busy"},
  "4":{"on":true,"bri":255,"transition":7,"mainseg":0,
       "seg":[{"on":true,"bri":255,"col":[[0,0,0],[255,0,255],[0,0,0]],
               "fx":2,"sx":128,"ix":128,"pal":2,"c1":128,"c2":128,"c3":16}],
       "n":"Do Not Disturb"},
  "5":{"on":true,"bri":255,"transition":7,"mainseg":0,
       "seg":[{"on":true,"bri":255,"col":[[255,0,38],[0,0,0],[0,0,0]],
               "fx":28,"sx":204,"ix":255,"pal":0,"c1":128,"c2":128,"c3":16}],
       "n":"Call/Meeting"},
  "6":{"on":true,"bri":255,"transition":7,"mainseg":0,
       "seg":[{"on":true,"bri":255,"col":[[0,0,0],[0,0,0],[0,0,0]],
               "fx":0,"sx":128,"ix":128,"pal":2,"c1":128,"c2":128,"c3":16}],
       "n":"Offline"}
})";

bool wledProvisionDevice(const String& ip) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    String url = "http://" + ip + "/json/presets";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);

    Serial.printf("[WLED] Provisioning presets to %s...\n", ip.c_str());
    int code = http.POST(String(WLED_PRESET_PAYLOAD));
    http.end();

    if (code == 200) {
        Serial.printf("[WLED] ✓ Presets provisioned on %s\n", ip.c_str());
        // Mark device as provisioned
        for (auto& d : g_devices) {
            if (d.type == LIGHT_WLED && d.ip == ip) {
                d.provisioned = true;
            }
        }
        lightDevicesSave();
        return true;
    }

    Serial.printf("[WLED] Provisioning failed: HTTP %d from %s\n", code, ip.c_str());
    return false;
}

int wledProvisionAll() {
    int count = 0;
    for (auto& d : g_devices) {
        if (d.type == LIGHT_WLED && !d.provisioned && d.responding) {
            if (wledProvisionDevice(d.ip)) count++;
        }
    }
    Serial.printf("[WLED] Provisioned %d device(s)\n", count);
    return count;
}

// ============================================================================
// Device verification — ping known devices
// ============================================================================

bool lightDevicePing(const LightDevice& dev) {
    if (dev.ip.isEmpty()) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    bool ok = false;

    switch (dev.type) {
    case LIGHT_WLED: {
        // WLED: GET /json/info is lightweight
        http.begin("http://" + dev.ip + "/json/info");
        http.setTimeout(2000);
        ok = (http.GET() == 200);
        http.end();
        break;
    }
    case LIGHT_HUE: {
        // Hue: GET /api/<key>/config — but we'd need the key
        // Just check bridge is reachable
        http.begin("http://" + dev.ip + "/api/config");
        http.setTimeout(2000);
        ok = (http.GET() == 200);
        http.end();
        break;
    }
    case LIGHT_WIZ: {
        // WiZ: send a quick getPilot and check for any response
        WiFiUDP udp;
        udp.begin(38899);
        IPAddress addr;
        if (addr.fromString(dev.ip)) {
            udp.beginPacket(addr, 38899);
            udp.print("{\"method\":\"getPilot\",\"params\":{}}");
            udp.endPacket();
            unsigned long t0 = millis();
            while (millis() - t0 < 1000) {
                if (udp.parsePacket() > 0) { ok = true; break; }
                delay(10);
            }
        }
        udp.stop();
        break;
    }
    default:
        break;
    }

    return ok;
}

void lightDevicesVerify() {
    Serial.println("[Lights] Verifying device connectivity...");
    for (auto& d : g_devices) {
        bool was = d.responding;
        d.responding = lightDevicePing(d);
        if (was != d.responding) {
            Serial.printf("[Lights] %s @ %s: %s → %s\n",
                          d.name.c_str(), d.ip.c_str(),
                          was ? "OK" : "DOWN",
                          d.responding ? "OK" : "DOWN");
        }
    }
}
