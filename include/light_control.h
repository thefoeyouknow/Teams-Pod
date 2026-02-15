// ============================================================================
// Light Control — WLED and Smart Bulb output
//
// Supported types:
//   LIGHT_NONE   — disabled
//   LIGHT_WLED   — Waveshare/generic WLED via JSON API (HTTP)
//   LIGHT_BULB   — Generic smart bulb via HTTP API (Tasmota, Shelly, etc.)
//
// Each type uses HTTP to set colour based on Teams presence.
// Config (IP, type) stored in NVS + SD.
// ============================================================================

#ifndef LIGHT_CONTROL_H
#define LIGHT_CONTROL_H

#include <Arduino.h>

// Light output type
enum LightType {
    LIGHT_NONE = 0,
    LIGHT_WLED = 1,
    LIGHT_BULB = 2,
    LIGHT_TYPE_COUNT = 3
};

// Light configuration (persisted in NVS + SD)
struct LightConfig {
    LightType type      = LIGHT_NONE;
    String    ip        = "";           // device IP, e.g. "192.168.1.100"
    int       brightness = 128;         // 0–255
};

// Get human-readable name for light type
const char* lightTypeName(LightType t);

// Load/save light config from/to NVS
void loadLightConfig(LightConfig& cfg);
void saveLightConfig(const LightConfig& cfg);

// Set the light colour from a Teams availability string.
// Maps: Available→green, Busy/DND→red, Away/BRB→yellow, Offline→off
void lightSetPresence(const LightConfig& cfg, const char* availability);

// Set an arbitrary RGB colour (0–255 each)
void lightSetColor(const LightConfig& cfg, uint8_t r, uint8_t g, uint8_t b);

// Turn the light off
void lightOff(const LightConfig& cfg);

// Test the light with a brief colour sequence
void lightTest(const LightConfig& cfg);

#endif
