#ifndef TEAMS_AUTH_H
#define TEAMS_AUTH_H

#include <Arduino.h>

// Response from the Microsoft Device Code endpoint
struct DeviceCodeResponse {
    String device_code;
    String user_code;
    String verification_uri;
    String qr_url;          // full URL for QR code (includes ?otc=code)
    int    expires_in;       // seconds until code expires
    int    interval;         // polling interval in seconds
    bool   valid;
};

// --- Device Code Flow ---
bool startDeviceCodeFlow(const String& clientId, const String& tenantId,
                         DeviceCodeResponse& response);

// Poll for token.  Returns: 1 = success, 0 = pending, -1 = error/expired
int  pollForToken(const String& clientId, const String& tenantId,
                  const String& deviceCode);

// --- Token management ---
bool   refreshAccessToken(const String& clientId, const String& tenantId);
String getAccessToken();
bool   hasValidToken();
bool   hasStoredRefreshToken();
bool   isTokenExpiringSoon();

// --- NVS persistence ---
void loadAuthFromNVS();
void saveAuthToNVS();
void clearAuthNVS();

#endif
