// ============================================================================
// Display UI — all screen-rendering functions for Status Pod
// ============================================================================

#include "display_ui.h"
#include "battery.h"
#include "sd_storage.h"
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

// Pick the smallest QR version that can hold `len` bytes.
// ecLevel: 0=ECC_LOW, 1=ECC_MEDIUM, 2=ECC_QUARTILE, 3=ECC_HIGH
static int selectQRVersion(int len, int ecLevel = 3) {
    // byte-mode capacity per version (indices 1-10)
    static const int capHigh[] = {0,  7, 10, 15, 26, 36, 46, 58, 74,  86, 119};
    static const int capLow[]  = {0, 17, 32, 53, 78,106,134,154,192, 230, 271};
    static const int capMed[]  = {0, 14, 26, 42, 62, 84,106,122,152, 180, 213};
    const int* cap = (ecLevel == 0) ? capLow : (ecLevel == 1) ? capMed : capHigh;
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
static void drawBatteryIcon(uint16_t fg, uint16_t bg, bool large = false) {
    float voltage = batteryReadVoltage();
    int   pct     = batteryPercent(voltage);
    bool  usb     = batteryOnUSB(voltage);

    if (large) {
        // --- Large vertical battery icon for status screen ---
        //   Tip on top, fill from bottom
        //        ╺╸       <- tip
        //   ┌──────────┐
        //   │          │
        //   │ ████████ │
        //   └──────────┘
        const int bw = 18, bh = 36;          // body (vertical)
        const int tipW = 8, tipH = 5;        // tip (horizontal nub on top)
        const int ix = 200 - bw - 6;         // x (lower-right, 6px margin)
        const int iy = 200 - bh - tipH - 6;  // y (leave room for tip above)

        // Tip (centred on top)
        int tipX = ix + (bw - tipW) / 2;
        int tipY = iy;
        display.fillRect(tipX, tipY, tipW, tipH, fg);

        // Body outline (2px border, below tip)
        int bodyY = iy + tipH;
        display.drawRect(ix, bodyY, bw, bh, fg);
        display.drawRect(ix + 1, bodyY + 1, bw - 2, bh - 2, fg);

        // Fill level (inside body, 2px inset, fills from bottom)
        int innerW = bw - 4;
        int innerH = bh - 4;
        int fillH  = (innerH * pct) / 100;
        if (fillH > 0)
            display.fillRect(ix + 2, bodyY + 2 + (innerH - fillH), innerW, fillH, fg);

        // Percent text above icon (size 2 = 12px tall)
        char buf[8];
        if (usb) snprintf(buf, sizeof(buf), "USB");
        else     snprintf(buf, sizeof(buf), "%d%%", pct);

        display.setFont(NULL);
        display.setTextSize(2);
        display.setTextColor(fg);

        int16_t x1, y1;
        uint16_t tw2, th2;
        display.getTextBounds(buf, 0, 0, &x1, &y1, &tw2, &th2);
        // Centre text horizontally over the icon, place above tip
        display.setCursor(ix + (bw - (int)tw2) / 2 - x1, iy - th2 - 3);
        display.print(buf);
        display.setTextSize(1);  // reset after large battery text
    } else {
        // --- Compact vertical battery icon for menu screens ---
        const int bw = 11, bh = 22;
        const int tipW = 5, tipH = 3;
        const int ix = 200 - bw - 4;
        const int iy = 200 - bh - tipH - 4;

        // Tip (centred on top)
        int tipX = ix + (bw - tipW) / 2;
        int tipY = iy;
        display.fillRect(tipX, tipY, tipW, tipH, fg);

        // Body outline
        int bodyY = iy + tipH;
        display.drawRect(ix, bodyY, bw, bh, fg);
        display.drawRect(ix + 1, bodyY + 1, bw - 2, bh - 2, fg);

        // Fill level (from bottom)
        int innerW = bw - 4;
        int innerH = bh - 4;
        int fillH  = (innerH * pct) / 100;
        if (fillH > 0)
            display.fillRect(ix + 2, bodyY + 2 + (innerH - fillH), innerW, fillH, fg);

        char buf[8];
        if (usb) snprintf(buf, sizeof(buf), "USB");
        else     snprintf(buf, sizeof(buf), "%d%%", pct);

        display.setFont(NULL);
        display.setTextSize(1);
        display.setTextColor(fg);

        int16_t x1, y1;
        uint16_t tw2, th2;
        display.getTextBounds(buf, 0, 0, &x1, &y1, &tw2, &th2);
        // Text above icon
        display.setCursor(ix + (bw - (int)tw2) / 2 - x1, iy - th2 - 2);
        display.print(buf);
    }

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

void drawSplashScreen(const char* platformLabel) {
    // --- Try BMP splash from SD card first ---
    const char* splashPath = nullptr;
    if (platformLabel) {
        if (strcmp(platformLabel, "Zoom") == 0)
            splashPath = "/graphics/zoom_splash.bmp";
        else
            splashPath = "/graphics/teams_splash.bmp";
    }
    if (splashPath && sdMounted() && sdFileExists(splashPath)) {
        static uint8_t bmpBuf[5000];
        if (sdLoadBMP(splashPath, bmpBuf, sizeof(bmpBuf))) {
            display.setFullWindow();
            display.firstPage();
            do {
                display.fillScreen(GxEPD_WHITE);
                display.drawBitmap(0, 0, bmpBuf, 200, 200, GxEPD_WHITE, GxEPD_BLACK);
                drawBatteryIcon(GxEPD_BLACK, GxEPD_WHITE, true);
            } while (display.nextPage());
            Serial.printf("[UI] BMP Splash: %s\n", splashPath);
            return;
        }
    }

    // --- Fallback: programmatic splash ---
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
        display.setTextSize(1);

        // Double border
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);
        display.drawRect(2, 2, 196, 196, GxEPD_BLACK);

        // Title
        display.setFont(&FreeSansBold18pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("Status", 55);
        centerText("Pod", 88);

        // Platform label (e.g. "for Teams" or "for Zoom")
        if (platformLabel && platformLabel[0]) {
            char plat[24];
            snprintf(plat, sizeof(plat), "for %s", platformLabel);
            display.setFont(&FreeSans9pt7b);
            centerText(plat, 110);
        }

        // Version
        display.setFont(NULL);
        display.setTextSize(1);
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

    int qrVersion = selectQRVersion(strlen(SETUP_URL), 1);  // 1 = ECC_MEDIUM
    uint8_t qrcodeData[qrcode_getBufferSize(10)];
    QRCode qrcode;
    int rc = qrcode_initText(&qrcode, qrcodeData, qrVersion, ECC_MEDIUM,
                             SETUP_URL);
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
        display.setTextSize(1);

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

    // --- generate QR code (ECC_LOW = smallest version = biggest modules) ---
    uint8_t qrcodeData[qrcode_getBufferSize(10)];
    QRCode qrcode;
    // Must compute correct minimum version ourselves — QRCode lib v0.0.1
    // has a bug where qrcode_initText() returns 0 even when data overflows.
    int qrVersion = selectQRVersion(strlen(qrUrl), 0);  // 0 = ECC_LOW
    int rc = qrcode_initText(&qrcode, qrcodeData, qrVersion, ECC_LOW, qrUrl);
    if (rc != 0) {
        Serial.println("[QR] FATAL: could not generate QR code");
        drawErrorScreen("QR Error", "URL too long for QR");
        return;
    }
    Serial.printf("[QR] Version %d, %dx%d modules\n",
                  qrVersion, qrcode.size, qrcode.size);

    // --- layout: maximize QR, code text in remaining space below ---
    // Use full 200px width with 4-module quiet zone each side.
    // Text goes below the QR in whatever space remains.
    int modules = qrcode.size;
    int scale   = 200 / (modules + 8);   // +8 = 4-module quiet zone each side
    if (scale < 2) scale = 2;
    int totalPx = modules * scale;
    int quietPx = scale * 4;             // quiet zone in pixels
    int offsetX = (200 - totalPx) / 2;
    // Push QR to top with quiet zone as top margin.
    // Bottom quiet zone is provided by the white text area below.
    int offsetY = quietPx;
    int qrBottom = offsetY + totalPx + 2;  // 2px gap, text area = quiet zone

    Serial.printf("[QR] scale=%d totalPx=%d offset=(%d,%d) qrBottom=%d\n",
                  scale, totalPx, offsetX, offsetY, qrBottom);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);

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

        // --- Auth code + gear hint below QR ---
        int textSpace = 200 - qrBottom;  // ~33px for V3@scale5

        // Auth code centred in available space, leaving 12px for gear hint
        const GFXfont* font = &FreeSansBold9pt7b;
        int16_t x1, y1;
        uint16_t w, h;
        display.setFont(font);
        display.getTextBounds(userCode, 0, 0, &x1, &y1, &w, &h);
        display.setTextColor(GxEPD_BLACK);
        int codeY = qrBottom + (textSpace - 12 + h) / 2;
        display.setCursor((200 - w) / 2 - x1, codeY);
        display.print(userCode);

        // Gear hint at very bottom: ⚙ = Code
        display.setFont(NULL);
        display.setTextSize(1);
        int hintW = 6 * 6 + 8 + 3;  // "= Code" width + gear + gap
        int hintX = (200 - hintW) / 2;
        drawGearIcon(hintX + 3, 195, 3, GxEPD_BLACK, GxEPD_WHITE);
        display.setCursor(hintX + 9, 192);
        display.print("= Code");

    } while (display.nextPage());

    Serial.printf("[QR] Auth screen drawn.  User code: %s\n", userCode);
}

// ============================================================================
// Auth Code Screen — large text display of the user code
// ============================================================================

void drawAuthCodeScreen(const char* userCode) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Title
        display.setFont(&FreeSansBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("Enter code at", 30);

        display.setFont(NULL);
        display.setTextSize(1);
        centerText("microsoft.com/devicelogin", 48);

        // Separator
        display.drawLine(10, 58, 190, 58, GxEPD_BLACK);

        // Auth code — 12pt fits codes like "ABCD1234" in one row
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(userCode, 0, 0, &x1, &y1, &w, &h);
        // If still too wide, fall back to 9pt
        if (w > 190) {
            display.setFont(&FreeSansBold9pt7b);
            display.getTextBounds(userCode, 0, 0, &x1, &y1, &w, &h);
        }
        display.setCursor((200 - w) / 2 - x1, 110);
        display.print(userCode);

        // Hint at bottom
        display.setFont(NULL);
        display.setTextSize(2);
        display.setTextColor(GxEPD_BLACK);
        drawGearIcon(14, 188, 5, GxEPD_BLACK, GxEPD_WHITE);
        display.setCursor(24, 183);
        display.print("= QR");
        display.setTextSize(1);  // reset after hint

    } while (display.nextPage());

    Serial.printf("[UI] Auth code screen: %s\n", userCode);
}

// Map availability/activity to BMP filename on SD card.
static const char* statusToBmpPath(const char* availability, const char* activity) {
    // Activity-specific overrides
    if (activity && strlen(activity) > 0) {
        if (strcmp(activity, "InACall") == 0 || strcmp(activity, "InAMeeting") == 0)
            return "/graphics/status_call.bmp";
        if (strcmp(activity, "Presenting") == 0)
            return "/graphics/status_presenting.bmp";
    }
    if (strcmp(availability, "Available") == 0)     return "/graphics/status_available.bmp";
    if (strcmp(availability, "Away") == 0)          return "/graphics/status_away.bmp";
    if (strcmp(availability, "BeRightBack") == 0)   return "/graphics/status_brb.bmp";
    if (strcmp(availability, "Busy") == 0)          return "/graphics/status_busy.bmp";
    if (strcmp(availability, "DoNotDisturb") == 0)  return "/graphics/status_dnd.bmp";
    if (strcmp(availability, "Offline") == 0)       return "/graphics/status_offline.bmp";
    if (strcmp(availability, "OutOfOffice") == 0)   return "/graphics/status_OoO.bmp";
    return nullptr;
}

// ============================================================================
// Presence-Status Screen
//   Available / Away / BeRightBack  →  white background, black text
//   Busy / DoNotDisturb             →  black background, white text
// ============================================================================

void drawStatusScreen(const char* availability, const char* activity) {
    // --- Try BMP image from SD card first ---
    const char* bmpPath = statusToBmpPath(availability, activity);
    if (bmpPath && sdMounted() && sdFileExists(bmpPath)) {
        static uint8_t bmpBuf[5000];  // 200×200 / 8
        if (sdLoadBMP(bmpPath, bmpBuf, sizeof(bmpBuf))) {
            display.setFullWindow();
            display.firstPage();
            do {
                display.fillScreen(GxEPD_WHITE);
                // bit 1 = white pixel, bit 0 = black pixel (normalised by sdLoadBMP)
                display.drawBitmap(0, 0, bmpBuf, 200, 200, GxEPD_WHITE, GxEPD_BLACK);
                // Battery icon overlay in clear bottom-right area
                drawBatteryIcon(GxEPD_BLACK, GxEPD_WHITE, true);
            } while (display.nextPage());
            Serial.printf("[UI] BMP Status: %s (%s) -> %s\n",
                          availability, activity ? activity : "", bmpPath);
            return;
        }
    }

    // --- Fallback: programmatic rendering ---
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
        display.setTextSize(1);

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

        // --- activity detail ---
        if (activity && strlen(activity) > 0 &&
            strcmp(activity, availability) != 0) {
            display.setFont(&FreeSansBold12pt7b);
            String act(activity);
            display.getTextBounds(act.c_str(), 0, 0, &x1, &y1, &w, &h);
            // Fall back to 9pt bold if too wide
            if (w > 190) {
                display.setFont(&FreeSansBold9pt7b);
                display.getTextBounds(act.c_str(), 0, 0, &x1, &y1, &w, &h);
            }
            display.setCursor((200 - w) / 2 - x1, 168);
            display.print(act);
        }

        // Battery icon (lower-right, large on status screen)
        drawBatteryIcon(fg, bg, true);

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
        display.setTextSize(1);
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
// Shutdown Screen — displayed before power off
// ============================================================================

void drawShutdownScreen() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Power icon — circle with line
        const int cx = 100, cy = 65, cr = 25;
        display.drawCircle(cx, cy, cr, GxEPD_BLACK);
        display.drawCircle(cx, cy, cr - 1, GxEPD_BLACK);
        display.fillRect(cx - 2, cy - cr - 5, 5, 20, GxEPD_WHITE);
        display.fillRect(cx - 1, cy - cr - 3, 3, 18, GxEPD_BLACK);

        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("Powered Off", 125);

        display.setFont(&FreeSans9pt7b);
        centerText("Press PWR to start", 155);

    } while (display.nextPage());

    Serial.println("[UI] Shutdown screen drawn");
}

// ============================================================================
// Low Battery Warning Screen
// ============================================================================

void drawLowBatteryScreen(int percent, bool critical) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Warning triangle
        display.fillTriangle(100, 20, 70, 70, 130, 70, GxEPD_BLACK);
        display.fillTriangle(100, 30, 78, 65, 122, 65, GxEPD_WHITE);
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(93, 62);
        display.print("!");

        // Title
        display.setFont(&FreeSansBold12pt7b);
        if (critical) {
            centerText("SHUTDOWN", 105);
            display.setFont(&FreeSans9pt7b);
            centerText("Battery critical", 130);
        } else {
            centerText("LOW BATTERY", 105);
        }

        // Large percentage
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", percent);
        display.setFont(&FreeSansBold18pt7b);
        centerText(buf, critical ? 175 : 160);

    } while (display.nextPage());

    Serial.printf("[UI] Low battery: %d%% %s\n", percent,
                  critical ? "CRITICAL" : "warning");
}

// ============================================================================
// Menu Screen
// ============================================================================

void drawMenuScreen(int selected, const PodSettings& settings,
                    const LightConfig& light, bool partial) {
    const char* labels[MENU_COUNT];

    labels[MENU_DEVICE_INFO] = "Device Info";
    labels[MENU_AUTH_STATUS] = "Auth Status";
    labels[MENU_LIGHTS]     = "Lights >";
    labels[MENU_SETTINGS]   = "Settings >";
    labels[MENU_REFRESH]    = "Refresh Now";
    labels[MENU_EXIT]       = "< Exit";

    if (partial)
        display.setPartialWindow(0, 0, 200, 200);
    else
        display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Title
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("MENU", 25);

        // Separator
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        // Menu items — 6 items, 22px spacing
        display.setFont(&FreeSansBold9pt7b);
        for (int i = 0; i < MENU_COUNT; i++) {
            int y = 55 + i * 22;
            if (i == selected) {
                display.fillRect(5, y - 13, 190, 18, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            } else {
                display.setTextColor(GxEPD_BLACK);
            }
            display.setCursor(15, y);
            display.print(labels[i]);
        }

        // Button hints at bottom
        display.setFont(NULL);
        display.setTextSize(2);
        display.setTextColor(GxEPD_BLACK);

        drawGearIcon(14, 188, 5, GxEPD_BLACK, GxEPD_WHITE);
        display.setCursor(24, 183);
        display.print("Next");

        display.setCursor(110, 183);
        display.print("PWR=Sel");

        drawBatteryIcon(GxEPD_BLACK, GxEPD_WHITE);

    } while (display.nextPage());

    Serial.printf("[UI] Menu drawn, selected=%d\n", selected);
}

// ============================================================================
// Settings Screen (submenu)
// ============================================================================

void drawSettingsScreen(int selected, const PodSettings& settings,
                        const LightConfig& light, bool partial) {
    const char* labels[SET_COUNT];
    static char lightLabel[24];
    snprintf(lightLabel, sizeof(lightLabel), "Light: %s", lightTypeName(light.type));

    labels[SET_LIGHT_TYPE]  = lightLabel;
    labels[SET_LIGHT_TEST]  = "Test Light";
    labels[SET_INVERT]      = settings.invertDisplay ? "Invert: ON"  : "Invert: OFF";
    labels[SET_AUDIO]       = settings.audioAlerts   ? "Audio: ON"   : "Audio: OFF";
    labels[SET_BLE_SETUP]   = "BLE Setup";
    labels[SET_BACK]        = "< Back";

    if (partial)
        display.setPartialWindow(0, 0, 200, 200);
    else
        display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Title
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("SETTINGS", 25);

        // Separator
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        // Settings items — 6 items, 22px spacing
        display.setFont(&FreeSansBold9pt7b);
        for (int i = 0; i < SET_COUNT; i++) {
            int y = 55 + i * 22;
            if (i == selected) {
                display.fillRect(5, y - 13, 190, 18, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            } else {
                display.setTextColor(GxEPD_BLACK);
            }
            display.setCursor(15, y);
            display.print(labels[i]);
        }

        // Button hints
        display.setFont(NULL);
        display.setTextSize(2);
        display.setTextColor(GxEPD_BLACK);

        drawGearIcon(14, 188, 5, GxEPD_BLACK, GxEPD_WHITE);
        display.setCursor(24, 183);
        display.print("Next");

        display.setCursor(110, 183);
        display.print("PWR=Sel");

        drawBatteryIcon(GxEPD_BLACK, GxEPD_WHITE);

    } while (display.nextPage());

    Serial.printf("[UI] Settings drawn, selected=%d\n", selected);
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

    if (partial)
        display.setPartialWindow(0, 0, 200, 200);
    else
        display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Title
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("DEVICE INFO", 25);
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        // Info rows (7 rows, size 2, 19px spacing)
        display.setFont(NULL);
        display.setTextSize(2);
        display.setTextColor(GxEPD_BLACK);
        int y = 42;
        const int lineH = 19;

        display.setCursor(6, y); display.printf("SSID:%s", ssid);
        y += lineH;
        display.setCursor(6, y); display.printf("IP:%s", ip);
        y += lineH;
        display.setCursor(6, y); display.printf("Cli:%s", clientShort);
        y += lineH;
        display.setCursor(6, y); display.printf("Ten:%s", tenantShort);
        y += lineH;
        display.setCursor(6, y); display.printf("Batt:%s", battBuf);
        y += lineH;
        display.setCursor(6, y); display.printf("SD:%s", sdInfo ? sdInfo : "No card");
        y += lineH;

        char verBuf[16];
        snprintf(verBuf, sizeof(verBuf), "v%s", FW_VERSION);
        display.setCursor(6, y); display.printf("FW:%s", verBuf);

        // Footer
        display.drawLine(10, 172, 190, 172, GxEPD_BLACK);
        display.setTextSize(2);
        display.setCursor(6, 178);
        display.print("Boot:X PWR:Rst");
        display.setTextSize(1);  // reset after footer

    } while (display.nextPage());

    Serial.println("[UI] Device info screen drawn");
}

// ============================================================================
// Auth Info Screen
// ============================================================================

void drawAuthInfoScreen(bool tokenValid, long expirySeconds,
                        const char* lastStatus, bool partial)
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
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Title
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("AUTH STATUS", 25);
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        // Info rows (3 rows, size 2, 28px spacing)
        display.setFont(NULL);
        display.setTextSize(2);
        display.setTextColor(GxEPD_BLACK);
        int y = 50;
        const int lineH = 28;

        display.setCursor(6, y);
        display.printf("Token:%s", tokenValid ? "Valid" : "INVALID");
        y += lineH;

        display.setCursor(6, y);
        display.printf("Expiry:%s", expiryBuf);
        y += lineH;

        display.setCursor(6, y);
        display.printf("Status:%s", lastStatus ? lastStatus : "Unknown");

        // Footer
        display.drawLine(10, 160, 190, 160, GxEPD_BLACK);
        display.setTextSize(2);
        display.setCursor(6, 166);
        display.print("Boot:Close");
        display.setCursor(6, 183);
        display.print("PWR:Reset");
        display.setTextSize(1);  // reset after footer

    } while (display.nextPage());

    Serial.println("[UI] Auth info screen drawn");
}

// ============================================================================
// Lights Screen — scrollable device list
// ============================================================================

void drawLightsScreen(int selected, const std::vector<LightDevice>& devs,
                      int scrollOffset, bool partial) {
    // Total items = 2 fixed (Discover, Provision All) + devices + Back
    int totalItems = 2 + (int)devs.size() + 1;  // +1 for Back

    const int maxVisible = 6;   // max items visible at once
    const int itemH = 20;       // pixels per row
    const int startY = 50;      // first item baseline y

    if (partial)
        display.setPartialWindow(0, 0, 200, 200);
    else
        display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Title
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("LIGHTS", 25);
        display.drawLine(10, 32, 190, 32, GxEPD_BLACK);

        // Item list (scrolled window)
        display.setFont(&FreeSansBold9pt7b);
        int visible = (totalItems < maxVisible) ? totalItems : maxVisible;
        for (int vi = 0; vi < visible; vi++) {
            int idx = scrollOffset + vi;
            if (idx >= totalItems) break;

            int y = startY + vi * itemH;
            bool isSel = (idx == selected);

            if (isSel) {
                display.fillRect(5, y - 13, 190, 17, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            } else {
                display.setTextColor(GxEPD_BLACK);
            }

            display.setCursor(15, y);
            if (idx == 0) {
                display.print("Discover");
            } else if (idx == 1) {
                display.print("Provision All");
            } else if (idx == totalItems - 1) {
                display.print("< Back");
            } else {
                // Device entry
                int devIdx = idx - 2;
                if (devIdx >= 0 && devIdx < (int)devs.size()) {
                    const LightDevice& d = devs[devIdx];
                    String label = d.name;
                    if (label.length() > 16) label = label.substring(0, 16);
                    // Type prefix + status suffix
                    const char* typeChar = "?";
                    if (d.type == LIGHT_WLED) typeChar = "W";
                    else if (d.type == LIGHT_WIZ) typeChar = "Z";
                    else if (d.type == LIGHT_HUE) typeChar = "H";
                    const char* stat = d.responding ? (d.provisioned ? " ok" : " !") : " x";
                    display.printf("[%s] %s%s", typeChar, label.c_str(), stat);
                }
            }
        }

        // Scroll indicators
        display.setFont(NULL);
        display.setTextSize(1);
        display.setTextColor(GxEPD_BLACK);
        if (scrollOffset > 0) {
            display.setCursor(185, 40);
            display.print("^");
        }
        if (scrollOffset + maxVisible < totalItems) {
            display.setCursor(185, startY + visible * itemH - 10);
            display.print("v");
        }

        // Item count
        display.setTextSize(2);
        display.setCursor(10, 167);
        display.printf("%d dev(s)", devs.size());

        // Button hints
        display.setTextSize(2);
        drawGearIcon(14, 188, 5, GxEPD_BLACK, GxEPD_WHITE);
        display.setCursor(24, 183);
        display.print("Next");
        display.setCursor(110, 183);
        display.print("PWR=Sel");
        drawBatteryIcon(GxEPD_BLACK, GxEPD_WHITE);

    } while (display.nextPage());

    Serial.printf("[UI] Lights screen drawn, sel=%d, scroll=%d, devs=%d\n",
                  selected, scrollOffset, devs.size());
}

// ============================================================================
// Light Action Screen — per-device actions
// ============================================================================

void drawLightActionScreen(const LightDevice& dev, int selected, bool partial) {
    const char* labels[LACT_COUNT] = { "Test", "Provision", "< Back" };

    if (partial)
        display.setPartialWindow(0, 0, 200, 200);
    else
        display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(1);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);

        // Device name as title
        display.setFont(&FreeSansBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        String title = dev.name;
        if (title.length() > 18) title = title.substring(0, 18);
        centerText(title.c_str(), 22);

        // Device info
        display.setFont(NULL);
        display.setTextSize(2);
        display.setCursor(6, 36);
        display.printf("IP:%s", dev.ip.c_str());
        display.setCursor(6, 54);
        display.printf("Type:%s %s",
                       lightTypeName(dev.type),
                       dev.provisioned ? "[Prov]" : "[No]");
        display.setCursor(6, 72);
        display.printf("Stat:%s", dev.responding ? "Online" : "Offline");

        display.drawLine(10, 86, 190, 86, GxEPD_BLACK);

        // Action items
        display.setFont(&FreeSansBold9pt7b);
        for (int i = 0; i < LACT_COUNT; i++) {
            int y = 102 + i * 24;
            if (i == selected) {
                display.fillRect(5, y - 13, 190, 18, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            } else {
                display.setTextColor(GxEPD_BLACK);
            }
            display.setCursor(15, y);
            display.print(labels[i]);
        }

        // Button hints
        display.setFont(NULL);
        display.setTextSize(2);
        display.setTextColor(GxEPD_BLACK);
        drawGearIcon(14, 188, 5, GxEPD_BLACK, GxEPD_WHITE);
        display.setCursor(24, 183);
        display.print("Next");
        display.setCursor(110, 183);
        display.print("PWR=Sel");
        drawBatteryIcon(GxEPD_BLACK, GxEPD_WHITE);

    } while (display.nextPage());

    Serial.printf("[UI] Light action screen: %s, sel=%d\n",
                  dev.name.c_str(), selected);
}
