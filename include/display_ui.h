#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <WS_EPD154V2.h>
#include "settings.h"
#include "light_control.h"

// Display object — defined in main.cpp, used by display_ui.cpp
extern GxEPD2_BW<WS_EPD154V2, WS_EPD154V2::HEIGHT> display;

// Firmware version — single source of truth
#define FW_VERSION "0.9.000"

// ---- Screen-drawing functions ----
void drawSplashScreen();                    // boot splash, waits for button
void drawSetupScreen();
void drawQRAuthScreen(const char* userCode, const char* qrUrl);
void drawStatusScreen(const char* availability, const char* activity);
void drawErrorScreen(const char* title, const char* detail);

// ---- Menu system ----
enum MenuItem {
    MENU_DEVICE_INFO = 0,
    MENU_AUTH_STATUS,
    MENU_LIGHT_TYPE,
    MENU_LIGHT_TEST,
    MENU_INVERT,
    MENU_AUDIO,
    MENU_BLE_SETUP,
    MENU_REFRESH,
    MENU_EXIT,
    MENU_COUNT
};

void drawMenuScreen(int selected, const PodSettings& settings,
                    const LightConfig& light, bool partial = false);
void drawDeviceInfoScreen(const char* ssid, const char* ip,
                          const char* clientId, const char* tenantId,
                          float battV, int battPct,
                          const char* sdInfo = nullptr,
                          bool partial = false);
void drawAuthInfoScreen(bool tokenValid, long expirySeconds,
                        bool hasRefresh, const char* lastStatus,
                        bool partial = false);

#endif
