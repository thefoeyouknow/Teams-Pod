// ============================================================================
// Zoom Auth — Server-to-Server OAuth token management
//
// Zoom S2S OAuth uses Account ID + Client ID + Client Secret.
// No user interaction needed — token is fetched automatically.
// Tokens last 1 hour; no refresh token — just re-request.
// ============================================================================

#ifndef ZOOM_AUTH_H
#define ZOOM_AUTH_H

#include <Arduino.h>

// Fetch a new access token using S2S OAuth.
// Requires accountId, clientId, clientSecret to be set (via BLE setup).
bool zoomFetchToken(const String& accountId,
                    const String& clientId,
                    const String& clientSecret);

// --- Token management ---
String zoomGetAccessToken();
bool   zoomHasValidToken();
bool   zoomIsTokenExpiringSoon();
long   zoomGetTokenExpirySeconds();

#endif
