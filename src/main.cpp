// ============================================================================
// Teams Pod — main firmware
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
#include <esp_system.h>

#include "ble_setup.h"
#include "display_ui.h"
#include "teams_auth.h"
#include "teams_presence.h"
#include "battery.h"
#include "settings.h"
#include "sd_storage.h"
#include "audio.h"
#include "light_control.h"

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

#define VBAT_PWR_PIN  17   // Battery power latch — HIGH = stay on

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
static int                 g_pollFailures       = 0;
static PodSettings         g_settings;
static LightConfig         g_lightCfg;

static const unsigned long PRESENCE_INTERVAL    = 30000;  // default 30 s, overridden by settings
static const int           MAX_POLL_FAILURES    = 5;      // allow 5 transient errors
static int                 g_partialCount       = 0;      // track partial refreshes for periodic full

// ============================================================================
// Forward declarations
// ============================================================================
void initializeHardware();
bool connectWiFi(unsigned long timeoutMs = 15000);
void updateAndDisplayPresence();
void handleMenu();
void waitForAnyButton();

// ============================================================================
// setup()
// ============================================================================
void setup() {
    // Latch battery power ON immediately (must be first)
    pinMode(VBAT_PWR_PIN, OUTPUT);
    digitalWrite(VBAT_PWR_PIN, HIGH);

    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n=== Teams Pod v%s ===\n\n", FW_VERSION);

    // Log reset reason for diagnostics
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reasonStr = "UNKNOWN";
    switch (reason) {
        case ESP_RST_POWERON:  reasonStr = "POWER_ON";   break;
        case ESP_RST_SW:       reasonStr = "SOFTWARE";    break;
        case ESP_RST_PANIC:    reasonStr = "PANIC/CRASH"; break;
        case ESP_RST_INT_WDT:  reasonStr = "INT_WDT";     break;
        case ESP_RST_TASK_WDT: reasonStr = "TASK_WDT";    break;
        case ESP_RST_WDT:      reasonStr = "OTHER_WDT";   break;
        case ESP_RST_DEEPSLEEP:reasonStr = "DEEP_SLEEP";  break;
        case ESP_RST_BROWNOUT: reasonStr = "BROWNOUT";    break;
        default: break;
    }
    Serial.printf("[Main] Reset reason: %s (%d)\n", reasonStr, (int)reason);

    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    pinMode(PWR_BUTTON,  INPUT_PULLUP);

    initializeHardware();

    // Mount SD card (before settings, so SD config is preferred)
    if (sdInit()) {
        Serial.printf("[Main] SD card: %s\n", sdCardInfo().c_str());
    } else {
        Serial.println("[Main] No SD card — using NVS for settings");
    }

    loadSettings(g_settings);
    loadLightConfig(g_lightCfg);

    // Audio init (ES8311 + I2S)
    audioInit();

    drawSplashScreen();

    // --- Splash gate: wait for BOOT press (short = continue, hold 3s = reset)
    Serial.println("[Main] Splash — press BOOT to continue, hold 3s for reset");
    while (true) {
        if (digitalRead(BOOT_BUTTON) == LOW) {
            unsigned long holdStart = millis();
            // Wait for release or 3-second hold
            while (digitalRead(BOOT_BUTTON) == LOW) {
                if (millis() - holdStart >= 3000) {
                    Serial.println("[Main] BOOT held 3s — factory reset");
                    drawErrorScreen("Factory Reset", "Clearing all data...");
                    clearStoredCredentials();
                    clearAuthNVS();
                    delay(2000);
                    ESP.restart();
                }
                delay(50);
            }
            // Short press — continue boot
            Serial.println("[Main] BOOT pressed — continuing");
            if (g_settings.audioAlerts) audioBeep();
            break;
        }
        delay(50);
    }

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
    // Sync BLE light globals → LightConfig (BLE saves to puck_creds namespace)
    g_lightCfg.type = (LightType)g_light_type.toInt();
    g_lightCfg.ip   = g_light_ip;
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
            g_lastPresenceCheck = 0;  // force immediate poll
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
        // Show Azure error code if available (e.g. "unauthorized_client")
        const char* detail = g_deviceCode.user_code.isEmpty()
            ? "Device code request failed"
            : g_deviceCode.user_code.c_str();
        drawErrorScreen("Auth Error", detail);
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
                g_lastPresenceCheck = 0;  // force immediate poll
                updateAndDisplayPresence();
            } else if (r < 0) {
                g_pollFailures++;
                Serial.printf("[Main] Poll failure %d/%d\n",
                              g_pollFailures, MAX_POLL_FAILURES);
                if (g_pollFailures >= MAX_POLL_FAILURES) {
                    g_state = STATE_ERROR;
                    drawErrorScreen("Auth Error", "Token request denied");
                }
            } else {
                g_pollFailures = 0;  // reset on successful pending response
            }
        }
        break;
    }

    // ---- Normal operation: poll presence --------------------------------
    case STATE_RUNNING: {
        // WiFi reconnect if connection dropped
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[Main] WiFi lost — reconnecting...");
            if (!connectWiFi()) {
                Serial.println("[Main] WiFi reconnect failed, will retry next loop");
                delay(5000);
                break;
            }
        }
        if (millis() - g_lastPresenceCheck >=
            (unsigned long)g_settings.presenceInterval * 1000UL) {
            g_lastPresenceCheck = millis();
            if (isTokenExpiringSoon())
                refreshAccessToken(g_client_id, g_tenant_id);
            updateAndDisplayPresence();
        }
        // BOOT = manual refresh
        if (digitalRead(BOOT_BUTTON) == LOW) {
            delay(200);
            if (digitalRead(BOOT_BUTTON) == LOW) {
                Serial.println("[Main] Manual refresh");
                updateAndDisplayPresence();
                while (digitalRead(BOOT_BUTTON) == LOW) delay(50);
            }
        }
        // PWR = open menu
        if (digitalRead(PWR_BUTTON) == LOW) {
            delay(200);
            handleMenu();
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

    // Battery ADC
    batteryInit();

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
            drawErrorScreen("Token Expired", "Scan QR to re-auth");
            if (g_settings.audioAlerts) audioChirp(3);
            lightOff(g_lightCfg);
            return;
        }
    }

    PresenceState st;
    if (getPresence(getAccessToken(), st)) {
        if (st.availability != g_lastAvailability) {
            drawStatusScreen(st.availability.c_str(),
                             st.activity.c_str());
            lightSetPresence(g_lightCfg, st.availability.c_str());
            g_lastAvailability = st.availability;
        } else {
            Serial.printf("[Main] Unchanged: %s\n",
                          st.availability.c_str());
        }
        g_currentPresence = st;
    } else if (!hasValidToken()) {
        if (!refreshAccessToken(g_client_id, g_tenant_id)) {
            g_state = STATE_ERROR;
            drawErrorScreen("Auth Lost", "Scan QR to re-auth");
            if (g_settings.audioAlerts) audioChirp(3);
            lightOff(g_lightCfg);
        }
    }
}

// ============================================================================
// Wait for any button press (used by info sub-screens)
// ============================================================================
void waitForAnyButton() {
    // Wait for all buttons to be released first
    while (digitalRead(BOOT_BUTTON) == LOW || digitalRead(PWR_BUTTON) == LOW)
        delay(50);
    // Wait for a press
    while (digitalRead(BOOT_BUTTON) == HIGH && digitalRead(PWR_BUTTON) == HIGH)
        delay(50);
    delay(200);  // debounce
    // Wait for release
    while (digitalRead(BOOT_BUTTON) == LOW || digitalRead(PWR_BUTTON) == LOW)
        delay(50);
}

// ============================================================================
// Menu handler — blocking loop while user navigates
//   BOOT (⚙) = next item
//   PWR       = select / toggle / enter
// ============================================================================
void handleMenu() {
    Serial.println("[Menu] Entering menu");
    // Wait for PWR release from the press that opened the menu
    while (digitalRead(PWR_BUTTON) == LOW) delay(50);

    int selected = 0;
    drawMenuScreen(selected, g_settings, g_lightCfg);  // first draw: full refresh

    while (true) {
        // BOOT = next item
        if (digitalRead(BOOT_BUTTON) == LOW) {
            delay(200);
            selected = (selected + 1) % MENU_COUNT;
            drawMenuScreen(selected, g_settings, g_lightCfg, true);  // partial
            while (digitalRead(BOOT_BUTTON) == LOW) delay(50);
        }

        // PWR = select
        if (digitalRead(PWR_BUTTON) == LOW) {
            delay(200);
            while (digitalRead(PWR_BUTTON) == LOW) delay(50);

            switch (selected) {
            case MENU_DEVICE_INFO: {
                float bv = batteryReadVoltage();
                int bp = batteryPercent(bv);
                String ip = WiFi.localIP().toString();
                String sd = sdMounted() ? sdCardInfo() : String("No card");
                drawDeviceInfoScreen(g_ssid.c_str(), ip.c_str(),
                                     g_client_id.c_str(), g_tenant_id.c_str(),
                                     bv, bp, sd.c_str(), true);
                // BOOT = close, PWR = reboot
                while (true) {
                    if (digitalRead(BOOT_BUTTON) == LOW) {
                        delay(200);
                        while (digitalRead(BOOT_BUTTON) == LOW) delay(50);
                        break;  // back to menu
                    }
                    if (digitalRead(PWR_BUTTON) == LOW) {
                        delay(200);
                        Serial.println("[Menu] Rebooting...");
                        ESP.restart();
                    }
                    delay(50);
                }
                drawMenuScreen(selected, g_settings, g_lightCfg, true);
                break;
            }
            case MENU_AUTH_STATUS: {
                drawAuthInfoScreen(hasValidToken(), getTokenExpirySeconds(),
                                   hasStoredRefreshToken(),
                                   g_lastAvailability.c_str(), true);
                // BOOT = close, PWR = factory reset
                while (true) {
                    if (digitalRead(BOOT_BUTTON) == LOW) {
                        delay(200);
                        while (digitalRead(BOOT_BUTTON) == LOW) delay(50);
                        break;  // back to menu
                    }
                    if (digitalRead(PWR_BUTTON) == LOW) {
                        delay(200);
                        while (digitalRead(PWR_BUTTON) == LOW) delay(50);
                        Serial.println("[Menu] Factory reset!");
                        clearStoredCredentials();
                        delay(500);
                        ESP.restart();
                    }
                    delay(50);
                }
                drawMenuScreen(selected, g_settings, g_lightCfg, true);
                break;
            }
            case MENU_LIGHT_TYPE: {
                // Cycle: NONE → WLED → BULB → NONE
                int t = (int)g_lightCfg.type + 1;
                if (t > (int)LIGHT_BULB) t = (int)LIGHT_NONE;
                g_lightCfg.type = (LightType)t;
                saveLightConfig(g_lightCfg);
                Serial.printf("[Menu] Light type → %d\n", t);
                drawMenuScreen(selected, g_settings, g_lightCfg, true);
                break;
            }
            case MENU_LIGHT_TEST:
                Serial.println("[Menu] Testing light");
                lightTest(g_lightCfg);
                drawMenuScreen(selected, g_settings, g_lightCfg, true);
                break;

            case MENU_INVERT:
                g_settings.invertDisplay = !g_settings.invertDisplay;
                saveSettings(g_settings);
                drawMenuScreen(selected, g_settings, g_lightCfg, true);
                break;

            case MENU_AUDIO:
                g_settings.audioAlerts = !g_settings.audioAlerts;
                saveSettings(g_settings);
                drawMenuScreen(selected, g_settings, g_lightCfg, true);
                break;

            case MENU_BLE_SETUP: {
                Serial.println("[Menu] Starting BLE setup");
                drawSetupScreen();
                startBLEAdvertising();
                waitForAnyButton();
                // Reload light config in case it was changed via BLE
                loadLightConfig(g_lightCfg);
                drawMenuScreen(selected, g_settings, g_lightCfg, true);
                break;
            }

            case MENU_REFRESH:
                Serial.println("[Menu] Refresh selected — exiting menu");
                updateAndDisplayPresence();
                return;

            case MENU_EXIT:
                Serial.println("[Menu] Exit");
                if (g_lastAvailability.length() > 0) {
                    drawStatusScreen(g_currentPresence.availability.c_str(),
                                     g_currentPresence.activity.c_str());
                }
                return;
            }
        }
        delay(50);
    }
}
