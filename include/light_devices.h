// ============================================================================
// Light Devices — discovery, tracking, and provisioning
//
// Discovers WLED (mDNS), WiZ (UDP), Hue (bridge API).
// Maintains a cached device list in /lights.json on SD.
// Provisions WLED devices with presence presets on demand.
// ============================================================================

#ifndef LIGHT_DEVICES_H
#define LIGHT_DEVICES_H

#include <Arduino.h>
#include <vector>
#include "light_control.h"

// A discovered or configured light device
struct LightDevice {
    String    name;                    // Human-readable (e.g. "Office WLED")
    LightType type       = LIGHT_NONE;
    String    ip;                      // IP address
    String    id;                      // Hue light/group/room ID, or empty
    bool      provisioned = false;     // WLED: presets uploaded?
    bool      responding  = true;      // Last contact succeeded?
};

// Get the cached device list (in-memory)
std::vector<LightDevice>& lightDevicesGet();

// Save current device list to /lights.json on SD
bool lightDevicesSave();

// Load device list from /lights.json on SD
bool lightDevicesLoad();

// ---- Discovery (call after WiFi is connected) ----

// Discover WLED devices via mDNS (_wled._tcp).
// Adds new devices; updates IP of known devices.  Returns count found.
int lightDiscoverWLED();

// Discover WiZ devices via UDP broadcast on port 38899.
// Returns count found.
int lightDiscoverWiZ();

// Query a Hue bridge for lights, groups, and rooms.
// bridgeIp = bridge IP, apiKey = Hue username/API key.
// Returns count of items enumerated.
int lightDiscoverHue(const String& bridgeIp, const String& apiKey);

// Run all applicable discovery based on current light type config.
// Returns total new devices found.
int lightDiscoverAll(const LightConfig& cfg);

// ---- WLED Preset Control ----

// Activate a WLED preset by ID on a single device.
// Returns true on success.
bool wledActivatePreset(const String& ip, int presetId);

// Activate a WLED preset on ALL tracked WLED devices.
void wledActivatePresetAll(int presetId);

// Map a presence availability string to a WLED preset ID.
// Available=1, Away=2, Busy=3, DND=4, Call/Meeting=5, Offline=6
int wledPresetForPresence(const char* availability);

// ---- WLED Provisioning ----

// Upload the standard 6-preset pack to a single WLED device.
// Does NOT overwrite LED config — only presets.
bool wledProvisionDevice(const String& ip);

// Provision all un-provisioned WLED devices.
int wledProvisionAll();

// ---- Utility ----

// Check if a known device is still responding (quick HTTP ping).
bool lightDevicePing(const LightDevice& dev);

// Verify all tracked devices; update `responding` flags.
void lightDevicesVerify();

#endif
