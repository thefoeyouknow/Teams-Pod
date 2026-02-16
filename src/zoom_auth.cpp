// ============================================================================
// Zoom Auth — Server-to-Server OAuth token management
//
// POST https://zoom.us/oauth/token
// Authorization: Basic base64(client_id:client_secret)
// Body: grant_type=account_credentials&account_id=<account_id>
//
// Returns access_token valid for ~1 hour (3600s).  No refresh token —
// just call zoomFetchToken() again when expiring.
// ============================================================================

#include "zoom_auth.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>

// ---- internal state -------------------------------------------------------
static String       s_zoom_token   = "";
static unsigned long s_zoom_expiry = 0;   // millis() when token expires

// ============================================================================
// Base64 encode client_id:client_secret for Basic auth
// ============================================================================
static String makeBasicAuth(const String& clientId, const String& clientSecret) {
    String raw = clientId + ":" + clientSecret;
    return base64::encode(raw);
}

// ============================================================================
// Fetch new S2S token
// ============================================================================
bool zoomFetchToken(const String& accountId,
                    const String& clientId,
                    const String& clientSecret)
{
    Serial.println("[Zoom] Fetching S2S token...");

    WiFiClientSecure client;
    client.setInsecure();   // TODO: pin root CA for prod
    HTTPClient http;

    if (!http.begin(client, "https://zoom.us/oauth/token")) {
        Serial.println("[Zoom] http.begin failed");
        return false;
    }

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Authorization", "Basic " + makeBasicAuth(clientId, clientSecret));

    String body = "grant_type=account_credentials&account_id=" + accountId;

    int httpCode = http.POST(body);
    String payload = http.getString();
    http.end();

    Serial.printf("[Zoom] HTTP %d\n", httpCode);

    if (httpCode != 200) {
        Serial.printf("[Zoom] Token request failed: %s\n", payload.c_str());
        s_zoom_token = "";
        return false;
    }

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload)) {
        Serial.println("[Zoom] JSON parse error");
        return false;
    }

    s_zoom_token  = doc["access_token"].as<String>();
    int expiresIn = doc["expires_in"] | 3600;
    s_zoom_expiry = millis() + (unsigned long)expiresIn * 1000UL;

    Serial.printf("[Zoom] ✓ Token acquired (expires in %ds)\n", expiresIn);
    return true;
}

// ============================================================================
// Accessors
// ============================================================================
String zoomGetAccessToken()       { return s_zoom_token; }
bool   zoomHasValidToken()        { return !s_zoom_token.isEmpty() && millis() < s_zoom_expiry; }
bool   zoomIsTokenExpiringSoon()  {
    if (s_zoom_expiry == 0) return false;
    unsigned long now = millis();
    return (now >= s_zoom_expiry) || (s_zoom_expiry - now < 300000UL);  // 5 min
}
long   zoomGetTokenExpirySeconds() {
    if (s_zoom_expiry == 0) return 0;
    return (long)(s_zoom_expiry - millis()) / 1000L;
}
