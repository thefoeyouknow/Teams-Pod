// ============================================================================
// Display UI — all screen-rendering functions for Teams Pod
// ============================================================================

#include "display_ui.h"
#include "battery.h"
#include <qrcode.h>

// Adafruit-GFX FreeFont headers (bundled with GxEPD2's dependency)
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

// URL to the Web Bluetooth setup page.
// Host web/setup.html on GitHub Pages (or any HTTPS host) and set this:
static const char* SETUP_URL = "https://thefoeyouknow.github.io/Teams-Pod/web/setup.html";

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// Centre a string horizontally at the given baseline-y
static void centerText(const char* text, int y) {
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((200 - w) / 2 - x1, y);
    display.print(text);
}

// Pick the smallest QR version that can hold `len` bytes at ECC_HIGH.
static int selectQRVersion(int len) {
    // byte-mode capacity at ECC_HIGH per version
    static const int cap[] = {0,7,10,15,26,36,46,58,74,86,119};
    for (int v = 1; v <= 10; v++) {
        if (len <= cap[v]) return v;
    }
    return 10;
}

// "Busy" and "DoNotDisturb" get an inverted (black-fill) screen.
static bool isInvertedStatus(const char* avail) {
    return (strcmp(avail, "Busy") == 0 ||
            strcmp(avail, "DoNotDisturb") == 0);
}

// ============================================================================
// Battery Icon — lower-right corner
//
//   Draws a small battery outline with proportional fill and optional
//   voltage text.  Works on both white and black backgrounds.
//
//   Layout (26×12 px):
//   ┌──────────────────┐
//   │ ████████         │╸  <- tip
//   └──────────────────┘
//
// ============================================================================
static void drawBatteryIcon(uint16_t fg, uint16_t bg) {
    float voltage = batteryReadVoltage();
    int   pct     = batteryPercent(voltage);
    bool  usb     = batteryOnUSB(voltage);

    // Icon geometry
    const int bw = 22, bh = 11;          // body width/height
    const int tw = 3,  th = 5;           // tip width/height
    const int ix = 200 - bw - tw - 4;   // x origin (lower-right, 4px margin)
    const int iy = 200 - bh - 4;         // y origin

    // Body outline (2px border)
    display.drawRect(ix, iy, bw, bh, fg);
    display.drawRect(ix + 1, iy + 1, bw - 2, bh - 2, fg);

    // Tip (positive terminal)
    int tipX = ix + bw;
    int tipY = iy + (bh - th) / 2;
    display.fillRect(tipX, tipY, tw, th, fg);

    // Fill level (inside body, 2px inset)
    int innerW = bw - 4;
    int innerH = bh - 4;
    int fillW  = (innerW * pct) / 100;
    if (fillW > 0) {
        display.fillRect(ix + 2, iy + 2, fillW, innerH, fg);
    }

    // Voltage text to the left of icon
    char buf[8];
    if (usb) {
        snprintf(buf, sizeof(buf), "USB");
    } else {
        snprintf(buf, sizeof(buf), "%.1fV", voltage);
    }
    display.setFont(NULL);
    display.setTextSize(1);
    display.setTextColor(fg);

    int16_t x1, y1;
    uint16_t tw2, th2;
    display.getTextBounds(buf, 0, 0, &x1, &y1, &tw2, &th2);
    display.setCursor(ix - tw2 - 3, iy + (bh - th2) / 2);
    display.print(buf);

    Serial.printf("[Batt] %.2fV  %d%%  %s\n", voltage, pct, usb ? "USB" : "BATT");
}

// ---------------------------------------------------------------------------
// Gear icon — represents the BOOT button (has ⚙ symbol on PCB)
// Draws a simple gear at (cx,cy) with given radius and color.
// ---------------------------------------------------------------------------
static void drawGearIcon(int cx, int cy, int r, uint16_t fg, uint16_t bg) {
    // Gear body
    display.fillCircle(cx, cy, r, fg);
    // Centre hole
    display.fillCircle(cx, cy, r / 3, bg);
    // 6 teeth — small filled squares on the perimeter
    const int toothW = max(2, r / 3);
    for (int i = 0; i < 6; i++) {
        float angle = i * PI / 3.0f;
        int tx = cx + (int)(cos(angle) * (r + toothW / 2));
        int ty = cy + (int)(sin(angle) * (r + toothW / 2));
        display.fillRect(tx - toothW / 2, ty - toothW / 2,
                         toothW, toothW, fg);
    }
}

// ============================================================================
// Splash Screen — shown at power-on, requires button press to proceed
// ============================================================================

void drawSplashScreen() {
    float voltage = batteryReadVoltage();
    int   pct     = batteryPercent(voltage);
    bool  usb     = batteryOnUSB(voltage);

    char battStr[24];
    if (usb) {
        snprintf(battStr, sizeof(battStr), "USB  %d%%", pct);
    } else {
        snprintf(battStr, sizeof(battStr), "%.2fV  %d%%", voltage, pct);
    }

    char verStr[16];
    snprintf(verStr, sizeof(verStr), "v%s", FW_VERSION);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Double border
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);
        display.drawRect(2, 2, 196, 196, GxEPD_BLACK);

        // Title
        display.setFont(&FreeSansBold18pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("Teams", 60);
        centerText("Pod", 95);

        // Version
        display.setFont(&FreeSans9pt7b);
        centerText(verStr, 120);

        // Battery bar (centred, larger than corner icon)
        const int barW = 40, barH = 16;
        const int barX = 60, barY = 135;
        const int tipW = 4, tipH = 8;
        display.drawRect(barX, barY, barW, barH, GxEPD_BLACK);
        display.drawRect(barX + 1, barY + 1, barW - 2, barH - 2, GxEPD_BLACK);
        display.fillRect(barX + barW, barY + (barH - tipH) / 2,
                         tipW, tipH, GxEPD_BLACK);
        int fillW = ((barW - 4) * pct) / 100;
        if (fillW > 0)
            display.fillRect(barX + 2, barY + 2, fillW, barH - 4, GxEPD_BLACK);

        // Battery text to right of bar
        display.setFont(NULL);
        display.setTextSize(1);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(barX + barW + tipW + 6, barY + 4);
        display.print(battStr);

        // "Press ⚙ to start" with drawn gear icon
        display.setFont(NULL);
        display.setTextSize(1);
        const char* pressMsg = "Press     to start";
        int16_t x1, y1;
        uint16_t tw, th;
        display.getTextBounds(pressMsg, 0, 0, &x1, &y1, &tw, &th);
        int msgX = (200 - tw) / 2 - x1;
        int msgY = 180;
        display.setCursor(msgX, msgY);
        display.print("Press ");
        int gearX = display.getCursorX() + 5;
        int gearY = msgY + 3;
        drawGearIcon(gearX, gearY, 4, GxEPD_BLACK, GxEPD_WHITE);
        display.setCursor(gearX + 8, msgY);
        display.print(" to start");

    } while (display.nextPage());

    Serial.printf("[UI] Splash: %s  batt=%s\n", verStr, battStr);
}

// ============================================================================
// Setup-Mode Screen (waiting for BLE credentials)
// ============================================================================

void drawSetupScreen() {
    Serial.printf("[Setup] QR URL (%d chars): %s\n", strlen(SETUP_URL), SETUP_URL);

    int qrVersion = selectQRVersion(strlen(SETUP_URL));
    uint8_t qrcodeData[qrcode_getBufferSize(10)];
    QRCode qrcode;
    int rc = -1;
    while (rc != 0 && qrVersion <= 10) {
        rc = qrcode_initText(&qrcode, qrcodeData, qrVersion, ECC_MEDIUM,
                             SETUP_URL);
        if (rc != 0) qrVersion++;
    }
    if (rc != 0) {
        drawErrorScreen("QR Error", "Setup URL too long");
        return;
    }

    int modules = qrcode.size;
    // Leave 24px top for title, 24px bottom for hint
    int availH  = 200 - 24 - 24;
    int scale   = availH / modules;
    if (scale < 1) scale = 1;
    int totalPx = modules * scale;
    int offsetX = (200 - totalPx) / 2;
    int topY    = 24;
    int offsetY = topY + (availH - totalPx) / 2;

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Title
        display.setFont(&FreeSansBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("Scan to Setup", 17);

        // QR code
        for (int y = 0; y < modules; y++) {
            for (int x = 0; x < modules; x++) {
                if (qrcode_getModule(&qrcode, x, y)) {
                    display.fillRect(offsetX + x * scale,
                                     offsetY + y * scale,
                                     scale, scale, GxEPD_BLACK);
                }
            }
        }

        // Bottom hint
        display.setFont(NULL);
        display.setTextSize(1);
        centerText("Open link, tap Connect", 190);
    } while (display.nextPage());

    Serial.println("[Setup] QR setup screen drawn");
}

// ============================================================================
// QR Auth Screen — QR code with user-code text above (no overlay)
// ============================================================================

void drawQRAuthScreen(const char* userCode, const char* qrUrl) {
    Serial.printf("[QR] URL (%d chars): %s\n", strlen(qrUrl), qrUrl);

    // --- generate QR code (ECC_MEDIUM = smaller version = bigger modules) ---
    uint8_t qrcodeData[qrcode_getBufferSize(10)];
    QRCode qrcode;
    int qrVersion = 1;
    int rc = -1;
    while (rc != 0 && qrVersion <= 10) {
        rc = qrcode_initText(&qrcode, qrcodeData, qrVersion, ECC_MEDIUM,
                             qrUrl);
        if (rc != 0) qrVersion++;
    }
    if (rc != 0) {
        Serial.println("[QR] FATAL: could not generate QR code");
        drawErrorScreen("QR Error", "URL too long for QR");
        return;
    }
    Serial.printf("[QR] Version %d, %dx%d modules\n",
                  qrVersion, qrcode.size, qrcode.size);

    // --- layout: 24px top for user code, rest for QR with quiet zone ---
    const int topH = 24;                    // text area height
    int modules = qrcode.size;
    int availH  = 200 - topH;               // pixels available for QR
    int scale   = availH / (modules + 2);   // +2 for 1-module quiet zone each side
    if (scale < 2) scale = 2;
    int totalPx = modules * scale;
    int offsetX = (200 - totalPx) / 2;
    int offsetY = topH + (availH - totalPx) / 2;

    Serial.printf("[QR] scale=%d totalPx=%d offset=(%d,%d)\n",
                  scale, totalPx, offsetX, offsetY);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // --- user code text at top ---
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        String label = String("Code: ") + userCode;
        centerText(label.c_str(), 19);

        // --- QR modules ---
        for (int y = 0; y < modules; y++) {
            for (int x = 0; x < modules; x++) {
                if (qrcode_getModule(&qrcode, x, y)) {
                    display.fillRect(offsetX + x * scale,
                                     offsetY + y * scale,
                                     scale, scale, GxEPD_BLACK);
                }
            }
        }

    } while (display.nextPage());

    Serial.printf("[QR] Auth screen drawn.  User code: %s\n", userCode);
}

// ============================================================================
// Presence-Status Screen
//   Available / Away / BeRightBack  →  white background, black text
//   Busy / DoNotDisturb             →  black background, white text
// ============================================================================

void drawStatusScreen(const char* availability, const char* activity) {
    bool inverted = isInvertedStatus(availability);
    uint16_t bg = inverted ? GxEPD_BLACK : GxEPD_WHITE;
    uint16_t fg = inverted ? GxEPD_WHITE : GxEPD_BLACK;

    // pick font that fits the word (one step smaller across the board)
    const GFXfont* statusFont = &FreeSansBold18pt7b;
    if (strlen(availability) > 7)  statusFont = &FreeSansBold12pt7b;
    if (strlen(availability) > 12) statusFont = &FreeSansBold9pt7b;

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(bg);

        // --- indicator circle (top-centre) ---
        const int cx = 100, cy = 55, cr = 30;
        if (inverted) {
            display.fillCircle(cx, cy, cr, fg);
            if (strcmp(availability, "DoNotDisturb") == 0) {
                // minus bar
                display.fillRect(cx - 15, cy - 3, 30, 6, bg);
            }
        } else {
            display.drawCircle(cx, cy, cr,     fg);
            display.drawCircle(cx, cy, cr - 1, fg);
            if (strcmp(availability, "Available") == 0) {
                // tick
                display.drawLine(cx-10, cy,   cx-3, cy+8,  fg);
                display.drawLine(cx-3,  cy+8, cx+12,cy-10, fg);
                display.drawLine(cx-10, cy+1, cx-3, cy+9,  fg);
                display.drawLine(cx-3,  cy+9, cx+12,cy-9,  fg);
            } else if (strcmp(availability, "Away") == 0 ||
                       strcmp(availability, "BeRightBack") == 0) {
                // clock hands
                display.drawLine(cx, cy, cx,    cy-15, fg);
                display.drawLine(cx, cy, cx+10, cy+5,  fg);
            } else if (strcmp(availability, "Offline") == 0) {
                // X
                display.drawLine(cx-10,cy-10, cx+10,cy+10, fg);
                display.drawLine(cx+10,cy-10, cx-10,cy+10, fg);
            }
        }

        // --- primary label ---
        const char* label = availability;
        if (strcmp(availability, "DoNotDisturb")    == 0) label = "DO NOT";
        else if (strcmp(availability, "BeRightBack")== 0) label = "BRB";
        else if (strcmp(availability, "PresenceUnknown")==0) label = "UNKNOWN";

        display.setFont(statusFont);
        display.setTextColor(fg);

        String upper = String(label);
        upper.toUpperCase();

        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(upper.c_str(), 0, 0, &x1, &y1, &w, &h);
        display.setCursor((200 - w) / 2 - x1, 120);
        display.print(upper);

        // second line for "DISTURB" when DND
        if (strcmp(availability, "DoNotDisturb") == 0) {
            display.getTextBounds("DISTURB", 0, 0, &x1, &y1, &w, &h);
            display.setCursor((200 - w) / 2 - x1, 155);
            display.print("DISTURB");
        }

        // --- activity detail (smaller) ---
        if (activity && strlen(activity) > 0 &&
            strcmp(activity, availability) != 0) {
            display.setFont(&FreeSans9pt7b);
            String act(activity);
            display.getTextBounds(act.c_str(), 0, 0, &x1, &y1, &w, &h);
            display.setCursor((200 - w) / 2 - x1, 178);
            display.print(act);
        }

        // Battery icon (lower-right)
        drawBatteryIcon(fg, bg);

        // border on light screens
        if (!inverted)
            display.drawRect(0, 0, 200, 200, fg);

    } while (display.nextPage());

    Serial.printf("[UI] Status: %s (%s)\n",
                  availability, activity ? activity : "");
}

// ============================================================================
// Error Screen
// ============================================================================

void drawErrorScreen(const char* title, const char* detail) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // warning triangle
        display.fillTriangle(100,20, 70,70, 130,70, GxEPD_BLACK);
        display.fillTriangle(100,30, 78,65, 122,65, GxEPD_WHITE);
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(93, 62);
        display.print("!");

        // title
        display.setFont(&FreeSansBold12pt7b);
        centerText(title, 105);

        // detail
        if (detail && strlen(detail) > 0) {
            display.setFont(&FreeSans9pt7b);
            centerText(detail, 135);
        }

        // restart hint
        display.setFont(NULL);
        display.setTextSize(1);
        centerText("Hold BOOT 3s to restart", 168);

        // Battery icon (lower-right)
        drawBatteryIcon(GxEPD_BLACK, GxEPD_WHITE);

    } while (display.nextPage());

    Serial.printf("[UI] Error: %s — %s\n", title, detail ? detail : "");
}

// ============================================================================
// Menu Screen
// ============================================================================

void drawMenuScreen(int selected, const PodSettings& settings,
                    const LightConfig& light, bool partial) {
    const char* labels[MENU_COUNT];
    static char lightLabel[24];
    snprintf(lightLabel, sizeof(lightLabel), "Light: %s", lightTypeName(light.type));

    labels[MENU_DEVICE_INFO] = "Device Info";
    labels[MENU_AUTH_STATUS] = "Auth Status";
    labels[MENU_LIGHT_TYPE]  = lightLabel;
    labels[MENU_LIGHT_TEST]  = "Test Light";
    labels[MENU_INVERT]      = settings.invertDisplay ? "Invert: ON"  : "Invert: OFF";
    labels[MENU_AUDIO]       = settings.audioAlerts   ? "Audio: ON"   : "Audio: OFF";
    labels[MENU_BLE_SETUP]   = "BLE Setup";
    labels[MENU_REFRESH]     = "Refresh Now";
    labels[MENU_EXIT]        = "< Exit";

    if (partial)
        display.setPartialWindow(0, 0, 200, 200);
    else
        display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Title
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("MENU", 25);

        // Separator
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        // Menu items
        display.setFont(&FreeSans9pt7b);
        for (int i = 0; i < MENU_COUNT; i++) {
            int y = 50 + i * 17;
            if (i == selected) {
                // Highlight bar
                display.fillRect(5, y - 12, 190, 16, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            } else {
                display.setTextColor(GxEPD_BLACK);
            }
            display.setCursor(15, y);
            display.print(labels[i]);
        }

        // Button hints at bottom
        display.setFont(NULL);
        display.setTextSize(1);
        display.setTextColor(GxEPD_BLACK);

        // Gear icon for BOOT button
        drawGearIcon(12, 190, 4, GxEPD_BLACK, GxEPD_WHITE);
        display.setCursor(20, 187);
        display.print("=Next");

        display.setCursor(110, 187);
        display.print("PWR=Select");

        // Battery icon (lower-right)
        drawBatteryIcon(GxEPD_BLACK, GxEPD_WHITE);

    } while (display.nextPage());

    Serial.printf("[UI] Menu drawn, selected=%d\n", selected);
}

// ============================================================================
// Device Info Screen
// ============================================================================

void drawDeviceInfoScreen(const char* ssid, const char* ip,
                          const char* clientId, const char* tenantId,
                          float battV, int battPct,
                          const char* sdInfo, bool partial)
{
    // Truncate long IDs
    char clientShort[20], tenantShort[20];
    snprintf(clientShort, sizeof(clientShort), "%.8s...", clientId);
    snprintf(tenantShort, sizeof(tenantShort), "%.8s...", tenantId);

    char battBuf[16];
    snprintf(battBuf, sizeof(battBuf), "%.2fV  %d%%", battV, battPct);

    char heapBuf[16];
    snprintf(heapBuf, sizeof(heapBuf), "%dKB", ESP.getFreeHeap() / 1024);

    if (partial)
        display.setPartialWindow(0, 0, 200, 200);
    else
        display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Title
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("DEVICE INFO", 25);
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        // Info rows
        display.setFont(NULL);
        display.setTextSize(1);
        display.setTextColor(GxEPD_BLACK);
        int y = 48;
        const int lineH = 16;

        display.setCursor(10, y); display.printf("SSID: %s", ssid);
        y += lineH;
        display.setCursor(10, y); display.printf("IP:   %s", ip);
        y += lineH;
        display.setCursor(10, y); display.printf("Client: %s", clientShort);
        y += lineH;
        display.setCursor(10, y); display.printf("Tenant: %s", tenantShort);
        y += lineH;
        display.setCursor(10, y); display.printf("Heap:   %s free", heapBuf);
        y += lineH;
        display.setCursor(10, y); display.printf("Batt:   %s", battBuf);
        y += lineH;
        display.setCursor(10, y); display.printf("SD:     %s", sdInfo ? sdInfo : "No card");
        y += lineH;

        char verBuf[16];
        snprintf(verBuf, sizeof(verBuf), "v%s", FW_VERSION);
        display.setCursor(10, y); display.printf("FW:     %s", verBuf);

        // Footer
        display.drawLine(10, 172, 190, 172, GxEPD_BLACK);
        display.setCursor(10, 180);
        display.print("BOOT:Close  PWR:Reboot");

    } while (display.nextPage());

    Serial.println("[UI] Device info screen drawn");
}

// ============================================================================
// Auth Info Screen
// ============================================================================

void drawAuthInfoScreen(bool tokenValid, long expirySeconds,
                        bool hasRefresh, const char* lastStatus,
                        bool partial)
{
    char expiryBuf[32];
    if (!tokenValid) {
        snprintf(expiryBuf, sizeof(expiryBuf), "Expired");
    } else if (expirySeconds > 3600) {
        snprintf(expiryBuf, sizeof(expiryBuf), "%ldh %ldm",
                 expirySeconds / 3600, (expirySeconds % 3600) / 60);
    } else if (expirySeconds > 60) {
        snprintf(expiryBuf, sizeof(expiryBuf), "%ld min", expirySeconds / 60);
    } else {
        snprintf(expiryBuf, sizeof(expiryBuf), "%ld sec", expirySeconds);
    }

    if (partial)
        display.setPartialWindow(0, 0, 200, 200);
    else
        display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Title
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("AUTH STATUS", 25);
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        // Info rows
        display.setFont(NULL);
        display.setTextSize(1);
        display.setTextColor(GxEPD_BLACK);
        int y = 55;
        const int lineH = 22;

        display.setCursor(10, y);
        display.printf("Token:   %s", tokenValid ? "Valid" : "INVALID");
        y += lineH;

        display.setCursor(10, y);
        display.printf("Expires: %s", expiryBuf);
        y += lineH;

        display.setCursor(10, y);
        display.printf("Refresh: %s", hasRefresh ? "Present" : "None");
        y += lineH;

        display.setCursor(10, y);
        display.printf("Status:  %s", lastStatus ? lastStatus : "Unknown");

        // Footer
        display.drawLine(10, 160, 190, 160, GxEPD_BLACK);
        display.setCursor(10, 170);
        display.print("BOOT: Close");
        display.setCursor(10, 184);
        display.print("PWR:  Factory Reset");

    } while (display.nextPage());

    Serial.println("[UI] Auth info screen drawn");
}
