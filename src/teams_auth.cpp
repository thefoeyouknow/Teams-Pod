// ============================================================================
// Teams Auth — Microsoft Device Code flow + token management
// ============================================================================

#include "teams_auth.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "sd_storage.h"

// ---- internal state -------------------------------------------------------
static String  s_access_token  = "";
static String  s_refresh_token = "";
static unsigned long s_token_expiry = 0;   // millis() when access token dies

static Preferences auth_prefs;
static const char* AUTH_NS      = "puck_auth";
static const char* KEY_REFRESH  = "refresh_tok";

// Scope: Presence.Read + User.Read (baseline) + offline_access for refresh tokens
static const char* SCOPE_ENC =
    "https%3A%2F%2Fgraph.microsoft.com%2FPresence.Read"
    "+https%3A%2F%2Fgraph.microsoft.com%2FUser.Read"
    "+offline_access";

// ---- endpoint helpers -----------------------------------------------------
static String deviceCodeEndpoint(const String& tid) {
    return "https://login.microsoftonline.com/" + tid +
           "/oauth2/v2.0/devicecode";
}
static String tokenEndpoint(const String& tid) {
    return "https://login.microsoftonline.com/" + tid +
           "/oauth2/v2.0/token";
}

// ============================================================================
// Device Code Flow — step 1: request a code
// ============================================================================

bool startDeviceCodeFlow(const String& clientId, const String& tenantId,
                         DeviceCodeResponse& response)
{
    response.valid = false;
    Serial.println("[Auth] Starting Device Code Flow...");

    WiFiClientSecure client;
    client.setInsecure();                       // TODO: pin root CA for prod
    HTTPClient http;

    String url  = deviceCodeEndpoint(tenantId);
    String body = "client_id=" + clientId + "&scope=" + String(SCOPE_ENC);

    Serial.printf("[Auth] POST %s\n", url.c_str());
    if (!http.begin(client, url)) {
        Serial.println("[Auth] http.begin failed");
        return false;
    }
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int code = http.POST(body);
    Serial.printf("[Auth] HTTP %d\n", code);

    if (code != 200) {
        String errPayload = http.getString();
        Serial.println(errPayload);
        http.end();

        // Parse Azure error for a user-friendly message
        DynamicJsonDocument errDoc(1024);
        if (!deserializeJson(errDoc, errPayload) && errDoc.containsKey("error")) {
            response.user_code = errDoc["error"].as<String>();  // reuse field for error detail
        }
        return false;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, payload)) {
        Serial.println("[Auth] JSON parse error");
        return false;
    }

    response.device_code      = doc["device_code"].as<String>();
    response.user_code         = doc["user_code"].as<String>();
    response.verification_uri  = doc["verification_uri"].as<String>();
    response.expires_in        = doc["expires_in"] | 900;
    response.interval          = doc["interval"]   | 5;

    // QR URL — just the base verification URI (code shown as text below QR)
    response.qr_url = response.verification_uri;

    response.valid = true;

    Serial.printf("[Auth] user_code : %s\n", response.user_code.c_str());
    Serial.printf("[Auth] QR URL    : %s\n", response.qr_url.c_str());
    Serial.printf("[Auth] expires   : %ds, interval: %ds\n",
                  response.expires_in, response.interval);
    return true;
}

// ============================================================================
// Device Code Flow — step 2: poll for token
// ============================================================================

int pollForToken(const String& clientId, const String& tenantId,
                 const String& deviceCode)
{
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url  = tokenEndpoint(tenantId);
    String body = "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code"
                  "&client_id=" + clientId +
                  "&device_code=" + deviceCode;

    if (!http.begin(client, url)) return -1;
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.POST(body);
    String payload = http.getString();
    http.end();

    if (httpCode == 200) {
        DynamicJsonDocument doc(16384);  // MS token JWTs can exceed 4KB
        DeserializationError jsonErr = deserializeJson(doc, payload);
        if (jsonErr) {
            Serial.printf("[Auth] Token JSON parse FAILED: %s (payload %d bytes)\n",
                          jsonErr.c_str(), payload.length());
            return -1;
        }

        s_access_token  = doc["access_token"].as<String>();
        s_refresh_token = doc["refresh_token"].as<String>();
        int expiresIn   = doc["expires_in"] | 3600;
        s_token_expiry  = millis() + (unsigned long)expiresIn * 1000UL;

        Serial.println("[Auth] ✓ Token acquired!");
        return 1;   // success
    }

    if (httpCode == 400) {
        DynamicJsonDocument doc(2048);
        DeserializationError jsonErr = deserializeJson(doc, payload);
        if (!jsonErr) {
            String err = doc["error"].as<String>();
            Serial.printf("[Auth] Poll response: %s\n", err.c_str());
            if (err == "authorization_pending" || err == "slow_down")
                return 0;   // keep waiting
            String errDesc = doc["error_description"].as<String>();
            Serial.printf("[Auth] Fatal: %s\n", err.c_str());
            Serial.printf("[Auth] Detail: %.300s\n", errDesc.c_str());
            return -1;  // genuinely rejected
        }
        // JSON parse failed on 400 — log it and treat as transient
        Serial.printf("[Auth] 400 JSON parse failed: %s\n", jsonErr.c_str());
        Serial.printf("[Auth] Payload (%d bytes): %.200s\n", payload.length(), payload.c_str());
        return 0;
    }

    // Transient HTTP errors (5xx, network issues) — don't give up
    Serial.printf("[Auth] Unexpected HTTP %d — treating as transient\n", httpCode);
    return -1;
}

// ============================================================================
// Token Refresh
// ============================================================================

bool refreshAccessToken(const String& clientId, const String& tenantId) {
    if (s_refresh_token.isEmpty()) {
        Serial.println("[Auth] No refresh token");
        return false;
    }
    Serial.println("[Auth] Refreshing token...");

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url  = tokenEndpoint(tenantId);
    String body = "grant_type=refresh_token"
                  "&client_id=" + clientId +
                  "&refresh_token=" + s_refresh_token +
                  "&scope=" + String(SCOPE_ENC);

    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.POST(body);
    String payload = http.getString();
    http.end();

    if (httpCode != 200) {
        Serial.printf("[Auth] Refresh failed HTTP %d\n", httpCode);
        s_refresh_token = "";   // invalidate – will force re-auth
        return false;
    }

    DynamicJsonDocument doc(16384);  // MS token JWTs can exceed 4KB
    if (deserializeJson(doc, payload)) return false;

    s_access_token = doc["access_token"].as<String>();
    if (doc.containsKey("refresh_token"))
        s_refresh_token = doc["refresh_token"].as<String>();
    int expiresIn  = doc["expires_in"] | 3600;
    s_token_expiry = millis() + (unsigned long)expiresIn * 1000UL;

    Serial.println("[Auth] ✓ Token refreshed");
    saveAuthToNVS();
    return true;
}

// ============================================================================
// Accessors
// ============================================================================

String getAccessToken()       { return s_access_token; }
bool   hasValidToken()        { return !s_access_token.isEmpty() && millis() < s_token_expiry; }
bool   hasStoredRefreshToken() { return !s_refresh_token.isEmpty(); }
bool   isTokenExpiringSoon()  {
    if (s_token_expiry == 0) return false;
    unsigned long now = millis();
    // Already expired, or within 5 minutes of expiry
    return (now >= s_token_expiry) || (s_token_expiry - now < 300000UL);
}
long   getTokenExpirySeconds() {
    if (s_token_expiry == 0) return 0;
    long diff = (long)(s_token_expiry - millis()) / 1000L;
    return diff;
}

// ============================================================================
// Token persistence — SD primary, NVS read-only fallback
// ============================================================================

static const char* SD_REFRESH_PATH = "/refresh_token.txt";

void loadAuthFromNVS() {
    // Try SD card first
    String tok = sdReadText(SD_REFRESH_PATH);
    tok.trim();
    if (tok.length() > 0) {
        s_refresh_token = tok;
        Serial.println("[Auth] Refresh token loaded from SD");
        return;
    }

    // Fallback: read from NVS (legacy / no-SD boot)
    if (auth_prefs.begin(AUTH_NS, true)) {
        s_refresh_token = auth_prefs.getString(KEY_REFRESH, "");
        auth_prefs.end();
    } else {
        s_refresh_token = "";
    }
    Serial.printf("[Auth] Refresh token from NVS fallback: %s\n",
                  s_refresh_token.isEmpty() ? "(none)" : "(present)");
}

void saveAuthToNVS() {
    // Write to SD only — no NVS writes during normal operation
    if (sdWriteText(SD_REFRESH_PATH, s_refresh_token)) {
        Serial.println("[Auth] Refresh token saved to SD");
    } else {
        Serial.println("[Auth] WARNING: SD write failed for refresh token");
    }
}

void clearAuthNVS() {
    // Clear SD token file
    if (sdMounted()) {
        sdWriteText(SD_REFRESH_PATH, "");
    }
    // Also clear legacy NVS
    auth_prefs.begin(AUTH_NS, false);
    auth_prefs.clear();
    auth_prefs.end();
    s_access_token  = "";
    s_refresh_token = "";
    s_token_expiry  = 0;
    Serial.println("[Auth] Auth cleared (SD + NVS)");
}
