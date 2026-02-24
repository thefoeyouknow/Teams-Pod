// ============================================================================
// WLED Zero-Config Provisioning
//
// Connects to a factory-fresh WLED device in AP mode (WLED-AP / wled1234),
// pushes the Pod's home WiFi credentials, then rejoins the home network.
// After the WLED device reboots onto the home network, mDNS discovery
// finds it automatically.
// ============================================================================

#ifndef WLED_PROVISION_H
#define WLED_PROVISION_H

#include <Arduino.h>

// Result codes for wledZeroConfig()
enum WledProvResult {
    WLED_PROV_OK = 0,           // Success — device configured + Pod back on WiFi
    WLED_PROV_AP_FAIL,          // Could not connect to WLED-AP
    WLED_PROV_HTTP_FAIL,        // Connected to AP but HTTP config POST failed
    WLED_PROV_REJOIN_FAIL       // Config sent but Pod failed to rejoin home WiFi
};

// Run the full zero-config provisioning flow:
//   1. Disconnect from home WiFi
//   2. Connect to WLED-AP (default password)
//   3. POST home SSID + password to WLED config API
//   4. Reconnect to home WiFi
//   5. Run mDNS discovery
//
// ssid/password = the home WiFi credentials to push to the WLED device.
// Returns a WledProvResult code.
//
// This function is blocking and takes 15-30 seconds.
WledProvResult wledZeroConfig(const String& ssid, const String& password);

#endif
