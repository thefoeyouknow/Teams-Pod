// ============================================================================
// Teams Presence — Microsoft Graph /me/presence poller
// ============================================================================

#include "teams_presence.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

bool getPresence(const String& accessToken, PresenceState& state) {
    state.valid = false;

    WiFiClientSecure client;
    client.setInsecure();       // TODO: pin root CA
    HTTPClient http;

    if (!http.begin(client,
                    "https://graph.microsoft.com/v1.0/me/presence")) {
        Serial.println("[Presence] http.begin failed");
        return false;
    }
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("Accept", "application/json");

    int httpCode = http.GET();
    String payload = http.getString();
    http.end();

    Serial.printf("[Presence] HTTP %d\n", httpCode);

    if (httpCode == 401) {
        Serial.println("[Presence] 401 — token expired");
        return false;
    }
    if (httpCode != 200) {
        Serial.printf("[Presence] Error: %s\n", payload.c_str());
        return false;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError jsonErr = deserializeJson(doc, payload);
    if (jsonErr) {
        Serial.printf("[Presence] JSON error: %s (payload %d bytes)\n",
                      jsonErr.c_str(), payload.length());
        Serial.printf("[Presence] Payload: %.200s\n", payload.c_str());
        return false;
    }

    state.availability = doc["availability"].as<String>();
    state.activity     = doc["activity"].as<String>();
    state.valid        = true;

    Serial.printf("[Presence] %s (%s)\n",
                  state.availability.c_str(), state.activity.c_str());
    return true;
}

const char* availabilityLabel(const String& a) {
    if (a == "Available")       return "Available";
    if (a == "Busy")            return "Busy";
    if (a == "DoNotDisturb")    return "Do Not Disturb";
    if (a == "Away")            return "Away";
    if (a == "BeRightBack")     return "Be Right Back";
    if (a == "Offline")         return "Offline";
    return "Unknown";
}
