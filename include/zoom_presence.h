// ============================================================================
// Zoom Presence — GET /v2/users/me/presence_status
//
// Zoom statuses are mapped to the same PresenceState struct used by Teams
// so the rest of the firmware (display, lights) works unchanged.
// ============================================================================

#ifndef ZOOM_PRESENCE_H
#define ZOOM_PRESENCE_H

#include "teams_presence.h"   // reuse PresenceState struct

// Fetch current presence from Zoom API.
// Maps Zoom status strings to Teams-compatible availability values.
bool getZoomPresence(const String& accessToken, PresenceState& state);

#endif
