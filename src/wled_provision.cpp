// ============================================================================
// WLED Zero-Config Provisioning
//
// Pushes the Pod's home WiFi credentials to a factory-fresh WLED device
// that is broadcasting its default AP (WLED-AP / wled1234).
//
// WLED default AP details (hardcoded — covers all stock firmware):
//   SSID:     WLED-AP
//   Password: wled1234
//   Gateway:  4.3.2.1
//   Config:   POST http://4.3.2.1/json/cfg   (WLED 0.14+)
// ============================================================================

#include "wled_provision.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WLED factory AP defaults
static const char* WLED_AP_SSID = "WLED-AP";
static const char* WLED_AP_PASS = "wled1234";
static const char* WLED_AP_GW   = "4.3.2.1";
static const unsigned long AP_CONNECT_TIMEOUT_MS = 15000;
static const unsigned long HOME_RECONNECT_TIMEOUT_MS = 15000;

// ---------------------------------------------------------------------------
WledProvResult wledZeroConfig(const String& ssid, const String& password) {
    Serial.println("[WLEDProv] Starting zero-config provisioning");

    // ---- Step 1: Disconnect from home WiFi ----
    Serial.println("[WLEDProv] Disconnecting from home WiFi...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500);

    // ---- Step 2: Connect to WLED-AP ----
    Serial.printf("[WLEDProv] Connecting to %s...\n", WLED_AP_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WLED_AP_SSID, WLED_AP_PASS);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < AP_CONNECT_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WLEDProv] FAILED to connect to WLED-AP");
        // Clean up — try to get back to a sane state
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(200);
        return WLED_PROV_AP_FAIL;
    }
    Serial.printf("[WLEDProv] Connected to WLED-AP, IP: %s\n",
                  WiFi.localIP().toString().c_str());

    // ---- Step 3: POST WiFi credentials to WLED config API ----
    // WLED 0.14+ JSON config endpoint
    String url = String("http://") + WLED_AP_GW + "/json/cfg";

    DynamicJsonDocument doc(512);
    JsonObject iface = doc.createNestedObject("if");
    JsonObject wifi  = iface.createNestedObject("wifi");
    wifi["ssid"] = ssid;
    wifi["psk"]  = password;

    String body;
    serializeJson(doc, body);
    Serial.printf("[WLEDProv] POST %s\n", url.c_str());
    Serial.printf("[WLEDProv] Body: %s\n", body.c_str());

    HTTPClient http;
    bool httpOk = false;

    if (http.begin(url)) {
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(10000);
        int code = http.POST(body);
        String resp = http.getString();
        http.end();

        Serial.printf("[WLEDProv] HTTP %d — %s\n", code, resp.c_str());
        httpOk = (code == 200);
    }

    if (!httpOk) {
        Serial.println("[WLEDProv] HTTP config POST failed");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(200);
        return WLED_PROV_HTTP_FAIL;
    }

    Serial.println("[WLEDProv] WiFi credentials sent to WLED device!");

    // ---- Step 4: Disconnect from WLED-AP, rejoin home WiFi ----
    Serial.println("[WLEDProv] Disconnecting from WLED-AP...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(1000);  // give WLED device time to start rebooting

    Serial.printf("[WLEDProv] Rejoining home WiFi (%s)...\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < HOME_RECONNECT_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WLEDProv] WARNING: Failed to rejoin home WiFi");
        return WLED_PROV_REJOIN_FAIL;
    }

    Serial.printf("[WLEDProv] Back on home WiFi, IP: %s\n",
                  WiFi.localIP().toString().c_str());

    // ---- Step 5: Brief delay for WLED to boot onto network ----
    Serial.println("[WLEDProv] Waiting for WLED device to join network...");
    delay(5000);  // WLED reboot takes ~3-5 seconds

    Serial.println("[WLEDProv] Zero-config complete!");
    return WLED_PROV_OK;
}
