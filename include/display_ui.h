#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <WS_EPD154V2.h>
#include "settings.h"
#include "light_control.h"
#include "light_devices.h"

// Display object — defined in main.cpp, used by display_ui.cpp
extern GxEPD2_BW<WS_EPD154V2, WS_EPD154V2::HEIGHT> display;

// Firmware version — single source of truth
#define FW_VERSION "0.15.000"

// ---- Screen-drawing functions ----
void drawSplashScreen(const char* platformLabel = nullptr);  // boot splash
void drawSetupScreen();
void drawQRAuthScreen(const char* userCode, const char* qrUrl);
void drawAuthCodeScreen(const char* userCode);
void drawStatusScreen(const char* availability, const char* activity);
void drawErrorScreen(const char* title, const char* detail);
void drawShutdownScreen();
void drawLowBatteryScreen(int percent, bool critical);

// ---- Menu system ----
enum MenuItem {
    MENU_DEVICE_INFO = 0,
    MENU_AUTH_STATUS,
    MENU_LIGHTS,
    MENU_SETTINGS,
    MENU_REFRESH,
    MENU_EXIT,
    MENU_COUNT
};

enum SettingsItem {
    SET_LIGHT_TYPE = 0,
    SET_LIGHT_TEST,
    SET_INVERT,
    SET_AUDIO,
    SET_BLE_SETUP,
    SET_BACK,
    SET_COUNT
};

void drawMenuScreen(int selected, const PodSettings& settings,
                    const LightConfig& light, bool partial = false);
void drawSettingsScreen(int selected, const PodSettings& settings,
                       const LightConfig& light, bool partial = false);
void drawDeviceInfoScreen(const char* ssid, const char* ip,
                          const char* clientId, const char* tenantId,
                          float battV, int battPct,
                          const char* sdInfo = nullptr,
                          bool partial = false);
void drawAuthInfoScreen(bool tokenValid, long expirySeconds,
                        const char* lastStatus,
                        bool partial = false);

// ---- Lights screen ----
enum LightsItem {
    LIGHTS_DISCOVER = 0,
    LIGHTS_PROVISION_ALL,
    LIGHTS_FIRST_DEVICE,   // dynamic entries start here
};

void drawLightsScreen(int selected, const std::vector<LightDevice>& devs,
                      int scrollOffset, bool partial = false);

// Device-action screen for a single light
enum LightAction {
    LACT_TEST = 0,
    LACT_PROVISION,
    LACT_BACK,
    LACT_COUNT
};

void drawLightActionScreen(const LightDevice& dev, int selected,
                           bool partial = false);

#endif
