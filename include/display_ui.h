#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <WS_EPD154V2.h>

// Display object — defined in main.cpp, used by display_ui.cpp
extern GxEPD2_BW<WS_EPD154V2, WS_EPD154V2::HEIGHT> display;

// Screen-drawing functions
void drawBootScreen();
void drawSetupScreen();
void drawQRAuthScreen(const char* userCode, const char* qrUrl);
void drawStatusScreen(const char* availability, const char* activity);
void drawErrorScreen(const char* title, const char* detail);

#endif
