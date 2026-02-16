// ============================================================================
// Zoom Presence — GET /v2/users/me/presence_status
//
// Zoom returns: Available, Away, Do_Not_Disturb, In_Calendar_Event,
//               Presenting, In_A_Zoom_Meeting, On_A_Call, Out_of_Office,
//               Busy, Offline
//
// We map these to Teams-compatible strings so the display and light
// control code works without modification.
// ============================================================================

#include "zoom_presence.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Map Zoom status → Teams-compatible availability string
static const char* mapZoomStatus(const String& status) {
    if (status == "Available")           return "Available";
    if (status == "Away")                return "Away";
    if (status == "Do_Not_Disturb")      return "DoNotDisturb";
    if (status == "Busy")                return "Busy";
    if (status == "In_A_Zoom_Meeting")   return "Busy";
    if (status == "On_A_Call")           return "Busy";
    if (status == "Presenting")          return "Busy";
    if (status == "In_Calendar_Event")   return "Busy";
    if (status == "Out_of_Office")       return "Away";
    if (status == "Offline")             return "Offline";
    return "PresenceUnknown";
}

// Map Zoom status → activity string for display
static const char* mapZoomActivity(const String& status) {
    if (status == "In_A_Zoom_Meeting")   return "In a Meeting";
    if (status == "On_A_Call")           return "On a Call";
    if (status == "Presenting")          return "Presenting";
    if (status == "In_Calendar_Event")   return "Calendar Event";
    if (status == "Out_of_Office")       return "Out of Office";
    if (status == "Do_Not_Disturb")      return "Do Not Disturb";
    return "";  // no specific activity
}

bool getZoomPresence(const String& accessToken, PresenceState& state) {
    state.valid = false;

    WiFiClientSecure client;
    client.setInsecure();       // TODO: pin root CA
    HTTPClient http;

    if (!http.begin(client,
                    "https://api.zoom.us/v2/users/me/presence_status")) {
        Serial.println("[Zoom] http.begin failed");
        return false;
    }
    http.addHeader("Authorization", "Bearer " + accessToken);

    int httpCode = http.GET();
    String payload = http.getString();
    http.end();

    Serial.printf("[Zoom] HTTP %d\n", httpCode);

    if (httpCode == 401) {
        Serial.println("[Zoom] 401 — token expired");
        return false;
    }
    if (httpCode != 200) {
        Serial.printf("[Zoom] Error: %s\n", payload.c_str());
        return false;
    }

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, payload)) {
        Serial.printf("[Zoom] JSON error\n");
        return false;
    }

    String zoomStatus = doc["status"].as<String>();
    state.availability = mapZoomStatus(zoomStatus);
    state.activity     = mapZoomActivity(zoomStatus);
    state.valid        = true;

    Serial.printf("[Zoom] %s → %s (%s)\n",
                  zoomStatus.c_str(),
                  state.availability.c_str(),
                  state.activity.c_str());
    return true;
}
