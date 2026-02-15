// ============================================================================
// Display UI — all screen-rendering functions for Teams Puck
//
// QR Auth Screen design note:
//   The QR code fills the entire 200×200 display.  A white strip at the
//   bottom overlays the QR modules to show the human-readable user code.
//   With ECC_HIGH (~30 % recovery), covering ~7 % of modules is safe.
//   Keeping the QR version as small as possible maximises module size
//   (easier phone scanning) and minimises the percentage of modules hidden.
// ============================================================================

#include "display_ui.h"
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
// Boot Screen
// ============================================================================

void drawBootScreen() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawRect(0, 0, 200, 200, GxEPD_BLACK);
        display.drawRect(2, 2, 196, 196, GxEPD_BLACK);

        display.setFont(&FreeSansBold18pt7b);
        display.setTextColor(GxEPD_BLACK);
        centerText("Teams", 70);
        centerText("Puck", 105);

        display.setFont(&FreeSans9pt7b);
        centerText("v0.50 MVP", 135);

        display.setFont(NULL);
        display.setTextSize(1);
        centerText("Initializing...", 170);
    } while (display.nextPage());
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
// QR Auth Screen — full-display QR code with user-code text overlay
// ============================================================================

void drawQRAuthScreen(const char* userCode, const char* qrUrl) {
    Serial.printf("[QR] URL (%d chars): %s\n", strlen(qrUrl), qrUrl);

    // --- generate QR code ---
    int qrVersion = selectQRVersion(strlen(qrUrl));
    // allocate for up to version 10 (max ~408 bytes)
    uint8_t qrcodeData[qrcode_getBufferSize(10)];
    QRCode qrcode;

    // retry with larger versions if the selected one can't fit
    int rc = -1;
    while (rc != 0 && qrVersion <= 10) {
        rc = qrcode_initText(&qrcode, qrcodeData, qrVersion, ECC_HIGH,
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

    // --- scaling ---
    int modules = qrcode.size;
    int scale   = 200 / modules;          // integer px per module
    if (scale < 1) scale = 1;
    int totalPx = modules * scale;
    int offsetX = (200 - totalPx) / 2;
    int offsetY = (200 - totalPx) / 2;

    // text strip at bottom (overlays QR — within ECC_HIGH tolerance)
    const int stripH = 22;
    const int stripY = 200 - stripH;

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

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

        // --- white backing strip + user code ---
        display.fillRect(0, stripY, 200, stripH, GxEPD_WHITE);
        display.drawLine(0, stripY, 200, stripY, GxEPD_BLACK);

        display.setFont(&FreeSansBold9pt7b);
        display.setTextColor(GxEPD_BLACK);

        String label = String("Code: ") + userCode;
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(label.c_str(), 0, 0, &x1, &y1, &w, &h);
        int textX = (200 - w) / 2 - x1;
        int textY = stripY + (stripH + h) / 2 - y1 - h;
        display.setCursor(textX, textY + h);
        display.print(label);

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

    // pick font that fits the word
    const GFXfont* statusFont = &FreeSansBold24pt7b;
    if (strlen(availability) > 7)  statusFont = &FreeSansBold18pt7b;
    if (strlen(availability) > 12) statusFont = &FreeSansBold12pt7b;

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
        centerText("Hold BOOT 3s to restart", 180);

    } while (display.nextPage());

    Serial.printf("[UI] Error: %s — %s\n", title, detail ? detail : "");
}
