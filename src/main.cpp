// ============================================================================
// Teams Puck — main firmware
//
// State machine:
//   BOOT → (no creds) → SETUP_BLE        — wait for BLE provisioning
//   BOOT → (creds)    → CONNECTING_WIFI   — join stored AP
//                     → AUTH_DEVICE_CODE   — show QR, poll for token
//                     → RUNNING            — poll Graph /me/presence
//   any  →             ERROR               — hold BOOT 3 s to restart
//
// Factory reset: hold BOOT during power-on to clear NVS.
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <WS_EPD154V2.h>
#include <WiFi.h>

#include "ble_setup.h"
#include "display_ui.h"
#include "teams_auth.h"
#include "teams_presence.h"

// ============================================================================
// Hardware pins  (Waveshare ESP32-S3-ePaper-1.54 V2)
// ============================================================================
#define EPD_DC_PIN    10
#define EPD_CS_PIN    11
#define EPD_SCK_PIN   12
#define EPD_MOSI_PIN  13
#define EPD_RST_PIN   9
#define EPD_BUSY_PIN  8
#define EPD_PWR_PIN   6    // ACTIVE LOW — LOW = on

#define BOOT_BUTTON   0
#define PWR_BUTTON    18

// ============================================================================
// Display object  (referenced via extern in display_ui.h)
// ============================================================================
GxEPD2_BW<WS_EPD154V2, WS_EPD154V2::HEIGHT> display(
    WS_EPD154V2(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN)
);

// ============================================================================
// Application state
// ============================================================================
enum AppState {
    STATE_BOOT,
    STATE_SETUP_BLE,
    STATE_CONNECTING_WIFI,
    STATE_AUTH_DEVICE_CODE,
    STATE_RUNNING,
    STATE_ERROR
};

static AppState            g_state              = STATE_BOOT;
static DeviceCodeResponse  g_deviceCode;
static PresenceState       g_currentPresence;
static String              g_lastAvailability   = "";
static unsigned long       g_lastPollTime       = 0;
static unsigned long       g_lastPresenceCheck  = 0;
static unsigned long       g_authStartTime      = 0;

static const unsigned long PRESENCE_INTERVAL    = 30000;  // 30 s

// ============================================================================
// Forward declarations
// ============================================================================
void initializeHardware();
bool connectWiFi(unsigned long timeoutMs = 15000);
void updateAndDisplayPresence();

// ============================================================================
// setup()
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== Teams Puck v0.51 ===\n");

    // --- Factory reset: BOOT held on power-up → wipe NVS and reboot ---
    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    if (digitalRead(BOOT_BUTTON) == LOW) {
        Serial.println("[Main] BOOT held — factory reset");
        clearStoredCredentials();
        clearAuthNVS();
        delay(1000);
        ESP.restart();
    }

    initializeHardware();
    drawBootScreen();
    delay(2000);

    // --- BLE always initialised (also opens NVS) ---
    initializeBLE();

    // --- Credential check ---
    if (!hasStoredCredentials()) {
        Serial.println("[Main] No credentials — Setup Mode");
        g_state = STATE_SETUP_BLE;
        startBLEAdvertising();
        drawSetupScreen();
        return;
    }
    loadCredentialsFromNVS();
    Serial.printf("[Main] SSID: %s  Client: %s  Tenant: %s\n",
                  g_ssid.c_str(), g_client_id.c_str(), g_tenant_id.c_str());

    // --- WiFi ---
    g_state = STATE_CONNECTING_WIFI;
    if (!connectWiFi()) {
        g_state = STATE_ERROR;
        drawErrorScreen("WiFi Failed", "Check SSID / password");
        return;
    }

    // --- Auth: try refresh first, then device-code ---
    loadAuthFromNVS();
    if (hasStoredRefreshToken()) {
        Serial.println("[Main] Attempting token refresh...");
        if (refreshAccessToken(g_client_id, g_tenant_id)) {
            g_state = STATE_RUNNING;
            g_lastPresenceCheck = millis() - PRESENCE_INTERVAL;
            updateAndDisplayPresence();
            return;
        }
        Serial.println("[Main] Refresh failed — need device code auth");
    }

    if (startDeviceCodeFlow(g_client_id, g_tenant_id, g_deviceCode)) {
        g_state         = STATE_AUTH_DEVICE_CODE;
        g_authStartTime = millis();
        g_lastPollTime  = millis();
        drawQRAuthScreen(g_deviceCode.user_code.c_str(),
                         g_deviceCode.qr_url.c_str());
    } else {
        g_state = STATE_ERROR;
        drawErrorScreen("Auth Error", "Device code request failed");
    }
}

// ============================================================================
// loop()
// ============================================================================
void loop() {
    switch (g_state) {

    // ---- BLE setup: just wait for callbacks ----------------------------
    case STATE_SETUP_BLE:
        delay(100);
        break;

    // ---- Device-code auth: poll at interval ----------------------------
    case STATE_AUTH_DEVICE_CODE: {
        if (millis() - g_authStartTime >
            (unsigned long)g_deviceCode.expires_in * 1000UL) {
            g_state = STATE_ERROR;
            drawErrorScreen("Auth Timeout", "Code expired — restart");
            break;
        }
        if (millis() - g_lastPollTime >=
            (unsigned long)g_deviceCode.interval * 1000UL) {
            g_lastPollTime = millis();
            Serial.println("[Main] Polling for token...");
            int r = pollForToken(g_client_id, g_tenant_id,
                                 g_deviceCode.device_code);
            if (r == 1) {
                saveAuthToNVS();
                g_state = STATE_RUNNING;
                g_lastPresenceCheck = millis() - PRESENCE_INTERVAL;
                updateAndDisplayPresence();
            } else if (r < 0) {
                g_state = STATE_ERROR;
                drawErrorScreen("Auth Error", "Token request denied");
            }
        }
        break;
    }

    // ---- Normal operation: poll presence --------------------------------
    case STATE_RUNNING: {
        if (millis() - g_lastPresenceCheck >= PRESENCE_INTERVAL) {
            g_lastPresenceCheck = millis();
            if (isTokenExpiringSoon())
                refreshAccessToken(g_client_id, g_tenant_id);
            updateAndDisplayPresence();
        }
        // manual refresh on short press
        if (digitalRead(BOOT_BUTTON) == LOW) {
            delay(200);
            if (digitalRead(BOOT_BUTTON) == LOW) {
                Serial.println("[Main] Manual refresh");
                updateAndDisplayPresence();
                while (digitalRead(BOOT_BUTTON) == LOW) delay(50);
            }
        }
        delay(100);
        break;
    }

    // ---- Error: hold BOOT 3 s to restart --------------------------------
    case STATE_ERROR:
        if (digitalRead(BOOT_BUTTON) == LOW) {
            delay(3000);
            if (digitalRead(BOOT_BUTTON) == LOW) ESP.restart();
        }
        delay(100);
        break;

    default:
        delay(100);
    }
}

// ============================================================================
// Hardware init
// ============================================================================
void initializeHardware() {
    if (psramFound())
        Serial.printf("[HW] PSRAM: %d MB\n",
                      ESP.getPsramSize() / (1024 * 1024));

    // E-Paper power — ACTIVE LOW
    pinMode(EPD_PWR_PIN, OUTPUT);
    digitalWrite(EPD_PWR_PIN, LOW);
    delay(200);

    // GxEPD2 display
    display.init(115200, true, 20, false, SPI,
                 SPISettings(10000000, MSBFIRST, SPI_MODE0));
    SPI.end();
    SPI.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, -1);
    Serial.printf("[HW] Display: %dx%d\n", display.width(), display.height());
}

// ============================================================================
// WiFi
// ============================================================================
bool connectWiFi(unsigned long timeoutMs) {
    Serial.printf("[WiFi] Connecting to %s", g_ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_ssid.c_str(), g_password.c_str());

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] ✓ IP %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    Serial.println("[WiFi] ✗ Failed");
    return false;
}

// ============================================================================
// Presence fetch + display update
// ============================================================================
void updateAndDisplayPresence() {
    if (!hasValidToken()) {
        Serial.println("[Main] Token invalid — refreshing");
        if (!refreshAccessToken(g_client_id, g_tenant_id)) {
            g_state = STATE_ERROR;
            drawErrorScreen("Token Expired", "Restart to re-auth");
            return;
        }
    }

    PresenceState st;
    if (getPresence(getAccessToken(), st)) {
        if (st.availability != g_lastAvailability) {
            drawStatusScreen(st.availability.c_str(),
                             st.activity.c_str());
            g_lastAvailability = st.availability;
        } else {
            Serial.printf("[Main] Unchanged: %s\n",
                          st.availability.c_str());
        }
        g_currentPresence = st;
    } else if (!hasValidToken()) {
        if (!refreshAccessToken(g_client_id, g_tenant_id)) {
            g_state = STATE_ERROR;
            drawErrorScreen("Auth Lost", "Restart to re-auth");
        }
    }
}
