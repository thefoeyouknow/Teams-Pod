#ifndef TEAMS_PRESENCE_H
#define TEAMS_PRESENCE_H

#include <Arduino.h>

// Presence state returned by Microsoft Graph /me/presence
struct PresenceState {
    String availability;   // Available, Away, BeRightBack, Busy, DoNotDisturb, Offline, PresenceUnknown
    String activity;       // InACall, InAMeeting, Presenting, etc.
    bool   valid;
};

// Fetch current presence from Graph API
bool getPresence(const String& accessToken, PresenceState& state);

// Human-readable label for the UI
const char* availabilityLabel(const String& availability);

#endif
