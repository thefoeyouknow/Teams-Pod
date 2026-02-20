// ============================================================================
// Status Pod — main firmware
//
// State machine:
//   BOOT → (no creds) → SETUP_BLE        — wait for BLE provisioning
//   BOOT → (creds)    → CONNECTING_WIFI   — join stored AP
//        Teams:       → AUTH_DEVICE_CODE   — show QR, poll for token
//        Zoom:        → RUNNING (auto S2S) — token fetched automatically
//                     → RUNNING            — poll presence API
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
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <time.h>
#include <driver/rtc_io.h>

#include "ble_setup.h"
#include "display_ui.h"
#include "teams_auth.h"
#include "teams_presence.h"
#include "zoom_auth.h"
#include "zoom_presence.h"
#include "battery.h"
#include "settings.h"
#include "sd_storage.h"
#include "audio.h"
#include "light_control.h"
#include "light_devices.h"

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

// ---- Power management ----
static unsigned long       g_lastBatteryCheck   = 0;
static const int           BATTERY_WARN_PCT     = 15;
static const int           BATTERY_SHUTDOWN_PCT = 5;
static bool                g_serialDisabled     = false;

// ---- Deep sleep state (RTC memory — survives deep sleep) ----
RTC_DATA_ATTR static bool    rtc_deepSleepActive = false;
RTC_DATA_ATTR static uint8_t rtc_stableCount     = 0;
RTC_DATA_ATTR static char    rtc_lastAvailability[32] = "";

static const int DEEP_SLEEP_THRESHOLD = 3;  // unchanged polls before deep sleep

// ============================================================================
// Forward declarations
// ============================================================================
void initializeHardware();
bool connectWiFi(unsigned long timeoutMs = 15000);
void checkPowerOff();
void updateAndDisplayPresence();
void handleMenu();
void handleSettings();
void waitForAnyButton();
void checkBattery();
void enterDeepSleep(int intervalSec);
void syncNTP();
bool isOfficeHours();
int  secondsUntilOfficeStart();

// ============================================================================
// setup()
// ============================================================================
void setup() {
    // Release ALL GPIO holds from deep sleep so pins can be driven again
    gpio_hold_dis((gpio_num_t)VBAT_PWR_PIN);
    gpio_deep_sleep_hold_dis();

    // Latch battery power ON immediately (must be first)
    pinMode(VBAT_PWR_PIN, OUTPUT);
    digitalWrite(VBAT_PWR_PIN, HIGH);

    Serial.begin(115200);
    delay(1000);
    Serial.printf("\n=== Status Pod v%s ===\n\n", FW_VERSION);

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

    // ========================================================================
    // Deep sleep fast-path — minimal wake, poll, return to sleep
    // Skips splash, BLE, audio init, discovery to minimise wake time & power
    // ========================================================================
    if (reason == ESP_RST_DEEPSLEEP && rtc_deepSleepActive) {
        esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();

        if (wakeup == ESP_SLEEP_WAKEUP_TIMER) {
            Serial.println("[DeepSleep] Timer wake — fast poll");
            setCpuFrequencyMhz(80);

            // --- Battery check first (may shutdown before spending power) ---
            batteryInit();
            float voltage = batteryReadVoltage();
            int   pct     = batteryPercent(voltage);

            if (batteryOnUSB(voltage)) {
                // USB plugged in — exit to full normal boot (WOT mode)
                Serial.println("[DeepSleep] USB detected — full-power boot");
                rtc_deepSleepActive = false;
                rtc_stableCount     = 0;
                // Fall through to normal boot below
                goto normalBoot;
            }

            // Critical battery → shutdown
            if (pct <= BATTERY_SHUTDOWN_PCT) {
                Serial.printf("[DeepSleep] CRITICAL %d%% — shutdown\n", pct);
                initializeHardware();
                audioInit(false);
                audioAttention(3);   // forced beep
                drawLowBatteryScreen(pct, true);
                delay(3000);
                checkPowerOff();
                return;
            }

            // Low battery → forced beep every wake (regardless of settings)
            if (pct <= BATTERY_WARN_PCT) {
                Serial.printf("[DeepSleep] Low battery %d%% — forced beep\n", pct);
                audioInit(false);
                audioAttention(1);
            }

            // --- Load config + credentials ---
            if (sdInit()) Serial.println("[DeepSleep] SD mounted");
            loadSettings(g_settings);
            loadLightConfig(g_lightCfg);
            loadCredentialsFromNVS();
            g_lightCfg.type     = (LightType)g_light_type.toInt();
            g_lightCfg.ip       = g_light_ip;
            g_settings.platform = (Platform)g_platform.toInt();

            // --- WiFi connect (need 240 MHz for radio) ---
            setCpuFrequencyMhz(240);
            if (!connectWiFi()) {
                Serial.println("[DeepSleep] WiFi failed — back to sleep");
                setCpuFrequencyMhz(80);
                enterDeepSleep(g_settings.presenceInterval);
                return;
            }

            // --- NTP sync + office hours check ---
            syncNTP();
            if (!isOfficeHours()) {
                Serial.println("[DeepSleep] Outside office hours — sleeping");
                int sleepSec = secondsUntilOfficeStart();
                if (sleepSec < 60) sleepSec = 60;
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                setCpuFrequencyMhz(80);
                enterDeepSleep(sleepSec);
                return;
            }

            // --- Token + presence poll ---
            PresenceState st;
            bool gotPresence = false;

            if (g_settings.platform == PLATFORM_ZOOM) {
                if (zoomFetchToken(g_tenant_id, g_client_id, g_client_secret))
                    gotPresence = getZoomPresence(zoomGetAccessToken(), st);
            } else {
                loadAuthFromNVS();
                if (refreshAccessToken(g_client_id, g_tenant_id))
                    gotPresence = getPresence(getAccessToken(), st);
            }

            bool changed = gotPresence &&
                            strcmp(st.availability.c_str(), rtc_lastAvailability) != 0;

            if (!changed) {
                // UNCHANGED — back to deep sleep
                rtc_stableCount++;
                Serial.printf("[DeepSleep] Unchanged (%s), stable=%d — sleeping\n",
                              rtc_lastAvailability, rtc_stableCount);
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                setCpuFrequencyMhz(80);
                enterDeepSleep(g_settings.presenceInterval);
                return;
            }

            // STATUS CHANGED — update display & lights, enter normal mode
            Serial.printf("[DeepSleep] Changed: %s → %s\n",
                          rtc_lastAvailability, st.availability.c_str());
            strncpy(rtc_lastAvailability, st.availability.c_str(), 31);
            rtc_lastAvailability[31] = '\0';
            rtc_deepSleepActive = false;
            rtc_stableCount     = 0;

            initializeHardware();
            drawStatusScreen(st.availability.c_str(), st.activity.c_str());
            lightDevicesLoad();
            lightSetPresence(g_lightCfg, st.availability.c_str());

            g_lastAvailability  = st.availability;
            g_currentPresence   = st;
            g_state             = STATE_RUNNING;
            g_lastPresenceCheck = millis();
            g_lastBatteryCheck  = millis();

            audioInit(false);  // safe to call again — has internal guard

            if (!batteryOnUSB(batteryReadVoltage())) setCpuFrequencyMhz(80);
            return;  // enter loop() in STATE_RUNNING

        } else {
            // Button wake from deep sleep — full boot
            Serial.println("[DeepSleep] Button wake — full boot");
            rtc_deepSleepActive = false;
            rtc_stableCount     = 0;
            // Fall through to normalBoot
        }
    }

normalBoot:
    bool skipSplash = (reason == ESP_RST_DEEPSLEEP);

    initializeHardware();

    // Mount SD card (before settings, so SD config is preferred)
    if (sdInit()) {
        Serial.printf("[Main] SD card: %s\n", sdCardInfo().c_str());
    } else {
        Serial.println("[Main] No SD card — using NVS for settings");
    }

    loadSettings(g_settings);
    loadLightConfig(g_lightCfg);

    // Audio init (ES8311 + I2S) — skip test tone on deep sleep resume
    audioInit(!skipSplash);

    if (!skipSplash)
        drawSplashScreen(platformName(g_settings.platform));

    // --- Splash gate: wait for BOOT press (short = continue, hold 3s = reset)
    if (!skipSplash) {
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

    // BLE no longer needed — free ~60 KB of RAM
    deinitBLE();

    // Sync BLE light globals → LightConfig (BLE saves to puck_creds namespace)
    g_lightCfg.type = (LightType)g_light_type.toInt();
    g_lightCfg.ip   = g_light_ip;
    // Sync platform from BLE credential store into settings
    g_settings.platform = (Platform)g_platform.toInt();
    Serial.printf("[Main] Platform: %s  SSID: %s  Client: %s  Tenant: %s\n",
                  platformName(g_settings.platform),
                  g_ssid.c_str(), g_client_id.c_str(), g_tenant_id.c_str());

    // --- WiFi ---
    g_state = STATE_CONNECTING_WIFI;
    if (!connectWiFi()) {
        g_state = STATE_ERROR;
        drawErrorScreen("WiFi Failed", "Check SSID / password");
        return;
    }

    // --- NTP time sync ---
    syncNTP();

    // --- Office hours check (battery only) ---
    if (!batteryOnUSB(batteryReadVoltage()) && !isOfficeHours()) {
        Serial.println("[Power] Outside office hours at boot — deep sleep");
        int sleepSec = secondsUntilOfficeStart();
        if (sleepSec < 60) sleepSec = 60;
        rtc_deepSleepActive = true;
        enterDeepSleep(sleepSec);
        return;
    }

    // --- Light devices: load cache then discover ---
    lightDevicesLoad();
    // Skip discovery on battery if we have cached devices
    if (batteryOnUSB(batteryReadVoltage()) || lightDevicesGet().empty()) {
        lightDiscoverAll(g_lightCfg);
    } else {
        Serial.println("[Main] Battery mode — using cached light devices");
    }

    // --- Auth: platform-specific ---
    if (g_settings.platform == PLATFORM_ZOOM) {
        // Zoom S2S OAuth — automatic, no user interaction
        Serial.println("[Main] Zoom S2S — fetching token...");
        if (zoomFetchToken(g_tenant_id, g_client_id, g_client_secret)) {
            g_state = STATE_RUNNING;
            g_lastPresenceCheck = 0;
            updateAndDisplayPresence();
        } else {
            g_state = STATE_ERROR;
            drawErrorScreen("Zoom Auth Failed", "Check credentials");
        }
    } else {
        // Teams Device Code Flow
        loadAuthFromNVS();
        if (hasStoredRefreshToken()) {
            Serial.println("[Main] Attempting token refresh...");
            if (refreshAccessToken(g_client_id, g_tenant_id)) {
                g_state = STATE_RUNNING;
                g_lastPresenceCheck = 0;
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
            const char* detail = g_deviceCode.user_code.isEmpty()
                ? "Device code request failed"
                : g_deviceCode.user_code.c_str();
            drawErrorScreen("Auth Error", detail);
        }
    }

    // Drop to 80 MHz on battery for idle power savings
    if (!batteryOnUSB(batteryReadVoltage())) setCpuFrequencyMhz(80);
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
        static bool showingQR = true;  // track which view is displayed

        if (millis() - g_authStartTime >
            (unsigned long)g_deviceCode.expires_in * 1000UL) {
            g_state = STATE_ERROR;
            drawErrorScreen("Auth Timeout", "Code expired — restart");
            break;
        }

        // BOOT button toggles between QR and code text
        if (digitalRead(BOOT_BUTTON) == LOW) {
            delay(200);  // debounce
            while (digitalRead(BOOT_BUTTON) == LOW) delay(50);
            showingQR = !showingQR;
            if (showingQR) {
                drawQRAuthScreen(g_deviceCode.user_code.c_str(),
                                 g_deviceCode.qr_url.c_str());
            } else {
                drawAuthCodeScreen(g_deviceCode.user_code.c_str());
            }
        }

        if (millis() - g_lastPollTime >=
            (unsigned long)g_deviceCode.interval * 1000UL) {
            g_lastPollTime = millis();
            Serial.println("[Main] Polling for token...");
            int r = pollForToken(g_client_id, g_tenant_id,
                                 g_deviceCode.device_code);
            if (r == 1) {
                showingQR = true;  // reset for next time
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
        bool onUSB = batteryOnUSB(batteryReadVoltage());
        batteryUpdateChargeLED(onUSB);

        // --- Charging = WOT: full speed, serial on, no sleep ---
        if (onUSB) {
            if (g_serialDisabled) {
                Serial.begin(115200);
                g_serialDisabled = false;
                Serial.println("[Power] USB — full-power mode");
            }
            rtc_stableCount = 0;  // reset deep sleep counter while charging
        } else {
            // On battery: disable Serial to save ~2-3 mA (one-shot)
            if (!g_serialDisabled) {
                Serial.println("[Power] Battery — disabling Serial");
                Serial.flush();
                Serial.end();
                g_serialDisabled = true;
            }
        }

        // --- Presence poll ---
        bool needPoll = (millis() - g_lastPresenceCheck >=
                         (unsigned long)g_settings.presenceInterval * 1000UL);
        if (needPoll) {
            // Office hours check (on battery only)
            if (!onUSB && !isOfficeHours()) {
                Serial.println("[Power] Outside office hours — deep sleep");
                int sleepSec = secondsUntilOfficeStart();
                if (sleepSec < 60) sleepSec = 60;  // minimum 1 min
                strncpy(rtc_lastAvailability, g_lastAvailability.c_str(), 31);
                rtc_lastAvailability[31] = '\0';
                rtc_deepSleepActive = true;
                enterDeepSleep(sleepSec);
                return;  // never reached
            }

            // Boost CPU for WiFi + HTTPS
            if (!onUSB) setCpuFrequencyMhz(240);

            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[Main] Reconnecting WiFi for poll...");
                if (!connectWiFi()) {
                    Serial.println("[Main] WiFi failed, will retry next cycle");
                    g_lastPresenceCheck = millis();
                    if (!onUSB) setCpuFrequencyMhz(80);
                    delay(1000);
                    break;
                }
            }
            g_lastPresenceCheck = millis();

            // Platform-aware token refresh
            if (g_settings.platform == PLATFORM_ZOOM) {
                if (zoomIsTokenExpiringSoon())
                    zoomFetchToken(g_tenant_id, g_client_id, g_client_secret);
            } else {
                if (isTokenExpiringSoon())
                    refreshAccessToken(g_client_id, g_tenant_id);
            }

            // Track status change for deep sleep decision
            String oldAvail = g_lastAvailability;
            updateAndDisplayPresence();

            if (!onUSB) {
                // Track consecutive unchanged polls
                if (g_lastAvailability == oldAvail && !oldAvail.isEmpty()) {
                    rtc_stableCount++;
                } else {
                    rtc_stableCount = 0;
                }

                // Battery check (merged into wake cycle — no separate timer)
                checkBattery();

                setCpuFrequencyMhz(80);  // back to low speed
            }
        }

        // --- BOOT = manual refresh ---
        if (digitalRead(BOOT_BUTTON) == LOW) {
            delay(200);
            if (digitalRead(BOOT_BUTTON) == LOW) {
                if (g_settings.audioAlerts) audioClick();
                Serial.println("[Main] Manual refresh");
                if (!onUSB) setCpuFrequencyMhz(240);
                if (WiFi.status() != WL_CONNECTED) connectWiFi();
                updateAndDisplayPresence();
                g_lastPresenceCheck = millis();
                rtc_stableCount = 0;  // user activity resets deep sleep
                if (!onUSB) setCpuFrequencyMhz(80);
                while (digitalRead(BOOT_BUTTON) == LOW) delay(50);
            }
        }

        // --- PWR = short press opens menu, long press powers off ---
        if (digitalRead(PWR_BUTTON) == LOW) {
            unsigned long pressStart = millis();
            while (digitalRead(PWR_BUTTON) == LOW) {
                if (millis() - pressStart >= 3000) {
                    checkPowerOff();
                    break;
                }
                delay(50);
            }
            if (millis() - pressStart < 3000) {
                if (g_settings.audioAlerts) audioClick();
                delay(100);
                rtc_stableCount = 0;  // user activity resets deep sleep
                handleMenu();
            }
            break;  // re-evaluate state after menu
        }

        // --- Power management ---
        if (onUSB) {
            // USB: full power, no sleep, WiFi stays up
            delay(100);
        } else if (rtc_stableCount >= DEEP_SLEEP_THRESHOLD) {
            // Stable long enough — enter deep sleep
            Serial.printf("[Power] %d stable polls — deep sleep\n",
                          rtc_stableCount);
            strncpy(rtc_lastAvailability, g_lastAvailability.c_str(), 31);
            rtc_lastAvailability[31] = '\0';
            rtc_deepSleepActive = true;
            enterDeepSleep(g_settings.presenceInterval);
            // Never returns
        } else {
            // Light sleep until next poll (building toward deep sleep)
            unsigned long now      = millis();
            unsigned long nextPoll = g_lastPresenceCheck +
                                     (unsigned long)g_settings.presenceInterval * 1000UL;

            if (nextPoll > now + 1000) {
                unsigned long sleepMs = nextPoll - now - 500;

                // Suspend WiFi for sleep
                if (WiFi.getMode() != WIFI_OFF) {
                    WiFi.disconnect(true);
                    WiFi.mode(WIFI_OFF);
                }

                // Configure GPIO wake for button presses
                gpio_wakeup_enable((gpio_num_t)BOOT_BUTTON, GPIO_INTR_LOW_LEVEL);
                gpio_wakeup_enable((gpio_num_t)PWR_BUTTON,  GPIO_INTR_LOW_LEVEL);
                esp_sleep_enable_gpio_wakeup();
                esp_sleep_enable_timer_wakeup((uint64_t)sleepMs * 1000ULL);

                Serial.printf("[Power] Light sleep %lu ms\n", sleepMs);
                Serial.flush();
                esp_light_sleep_start();

                // --- Woke up ---
                esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
                Serial.printf("[Power] Woke: %s\n",
                              cause == ESP_SLEEP_WAKEUP_GPIO ? "button" : "timer");
                delay(50);  // debounce
            } else {
                delay(100);
            }
        }
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
        Serial.printf("[WiFi] ✓ IP %s\n", WiFi.localIP().toString().c_str());        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // modem sleep between polls
        Serial.println("[WiFi] Modem sleep enabled");        return true;
    }
    Serial.println("[WiFi] ✗ Failed");
    return false;
}

// ============================================================================
// Presence fetch + display update
// ============================================================================
void updateAndDisplayPresence() {
    // Platform-aware token validation and presence fetch
    if (g_settings.platform == PLATFORM_ZOOM) {
        if (!zoomHasValidToken()) {
            Serial.println("[Main] Zoom token invalid — re-fetching");
            if (!zoomFetchToken(g_tenant_id, g_client_id, g_client_secret)) {
                g_state = STATE_ERROR;
                drawErrorScreen("Zoom Auth Lost", "Check credentials");
                if (g_settings.audioAlerts) audioAttention(3);
                lightOff(g_lightCfg);
                return;
            }
        }
        PresenceState st;
        if (getZoomPresence(zoomGetAccessToken(), st)) {
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
        }
    } else {
        // Teams flow
        if (!hasValidToken()) {
            Serial.println("[Main] Token invalid — refreshing");
            if (!refreshAccessToken(g_client_id, g_tenant_id)) {
                g_state = STATE_ERROR;
                drawErrorScreen("Token Expired", "Scan QR to re-auth");
                if (g_settings.audioAlerts) audioAttention(3);
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
                if (g_settings.audioAlerts) audioAttention(3);
                lightOff(g_lightCfg);
            }
        }
    }
}

// ============================================================================
// NTP + Office Hours helpers
// ============================================================================
void syncNTP() {
    if (g_settings.timezone.length() == 0) return;
    Serial.printf("[NTP] Syncing with TZ: %s\n", g_settings.timezone.c_str());
    configTzTime(g_settings.timezone.c_str(), "pool.ntp.org", "time.nist.gov");
    struct tm t;
    if (getLocalTime(&t, 5000)) {
        Serial.printf("[NTP] Time: %04d-%02d-%02d %02d:%02d:%02d (wday=%d)\n",
                      t.tm_year+1900, t.tm_mon+1, t.tm_mday,
                      t.tm_hour, t.tm_min, t.tm_sec, t.tm_wday);
    } else {
        Serial.println("[NTP] Failed to get time");
    }
}

bool isOfficeHours() {
    if (!g_settings.officeHoursEnabled) return true;  // disabled = always on
    struct tm t;
    if (!getLocalTime(&t, 100)) return true;  // can't tell = assume on
    // tm_wday: 0=Sun,1=Mon..6=Sat → remap to bit0=Mon..bit6=Sun
    int dayBit = (t.tm_wday == 0) ? 6 : (t.tm_wday - 1);
    if (!(g_settings.officeDays & (1 << dayBit))) return false;
    int nowMin   = t.tm_hour * 60 + t.tm_min;
    int startMin = g_settings.officeStartHour * 60 + g_settings.officeStartMin;
    int endMin   = g_settings.officeEndHour   * 60 + g_settings.officeEndMin;
    return (nowMin >= startMin && nowMin < endMin);
}

int secondsUntilOfficeStart() {
    struct tm t;
    if (!getLocalTime(&t, 100)) return 3600;  // fallback 1h
    int startMin = g_settings.officeStartHour * 60 + g_settings.officeStartMin;
    // Check remaining days this week + wrap
    for (int ahead = 0; ahead < 8; ahead++) {
        int wday = (t.tm_wday + ahead) % 7;
        int dayBit = (wday == 0) ? 6 : (wday - 1);
        if (!(g_settings.officeDays & (1 << dayBit))) continue;
        int nowMin = (ahead == 0) ? (t.tm_hour * 60 + t.tm_min) : 0;
        if (ahead == 0 && nowMin >= startMin) continue;  // already past start today
        int secsToday = (startMin - nowMin) * 60 - t.tm_sec;
        return ahead * 86400 + secsToday;
    }
    return 3600;  // fallback
}

// ============================================================================
// Battery check — warn at low %, auto-shutdown at critical %
// ============================================================================
void checkBattery() {
    float voltage = batteryReadVoltage();
    int pct = batteryPercent(voltage);

    if (batteryOnUSB(voltage))
        return;  // USB powered — no concern

    // Critical — force shutdown to protect battery
    if (pct <= BATTERY_SHUTDOWN_PCT) {
        Serial.printf("[Power] CRITICAL %d%% — auto-shutdown\n", pct);
        audioAttention(3);   // forced beep regardless of settings
        drawLowBatteryScreen(pct, true);
        delay(3000);
        checkPowerOff();
        return;
    }

    // Low warning — forced beep every poll cycle at ≤15%
    if (pct <= BATTERY_WARN_PCT) {
        Serial.printf("[Power] Low battery: %d%%\n", pct);
        audioAttention(1);   // forced beep
    }
}

// ============================================================================
// Power off — graceful shutdown, release power latch
// ============================================================================
void checkPowerOff() {
    Serial.println("[Main] Powering off...");
    // Graceful cleanup — turn off peripherals before power cut
    batteryUpdateChargeLED(false);
    lightOff(g_lightCfg);
    audioShutdown();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    drawShutdownScreen();
    // Wait for button release
    while (digitalRead(PWR_BUTTON) == LOW) delay(50);
    delay(500);  // let user see the screen
    // Release GPIO holds from deep sleep before driving pin LOW
    gpio_hold_dis((gpio_num_t)VBAT_PWR_PIN);
    gpio_deep_sleep_hold_dis();
    // Release power latch — device will lose power
    digitalWrite(VBAT_PWR_PIN, LOW);
    // If still running (USB powered), enter deep sleep with no wake sources
    delay(100);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();
}

// ============================================================================
// enterDeepSleep — hold power latch, set wake sources, sleep
// ============================================================================
void enterDeepSleep(int intervalSec) {
    Serial.printf("[DeepSleep] Sleeping %d s\n", intervalSec);
    Serial.flush();

    // Turn off charge LED before sleeping
    batteryUpdateChargeLED(false);

    // Turn off WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Suspend audio codec to save power
    audioSuspend();

    // Hold power latch HIGH during deep sleep
    gpio_hold_en((gpio_num_t)VBAT_PWR_PIN);
    gpio_deep_sleep_hold_en();

    // Wake on either button (ext1 — ANY_LOW)
    uint64_t buttonMask = (1ULL << BOOT_BUTTON) | (1ULL << PWR_BUTTON);
    esp_sleep_enable_ext1_wakeup(buttonMask, ESP_EXT1_WAKEUP_ANY_LOW);

    // Wake on timer
    esp_sleep_enable_timer_wakeup((uint64_t)intervalSec * 1000000ULL);

    esp_deep_sleep_start();
    // Never returns
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
// Lights submenu — scrollable device list + per-device actions
// ============================================================================
void handleLightAction(LightDevice& dev) {
    int sel = 0;
    drawLightActionScreen(dev, sel);

    while (true) {
        if (digitalRead(BOOT_BUTTON) == LOW) {
            delay(200);
            if (g_settings.audioAlerts) audioClick();
            sel = (sel + 1) % LACT_COUNT;
            drawLightActionScreen(dev, sel, true);
            while (digitalRead(BOOT_BUTTON) == LOW) delay(50);
        }
        if (digitalRead(PWR_BUTTON) == LOW) {
            delay(200);
            if (g_settings.audioAlerts) audioClick();
            while (digitalRead(PWR_BUTTON) == LOW) delay(50);

            switch (sel) {
            case LACT_TEST:
                if (dev.type == LIGHT_WLED) {
                    // cycle through presets 1-5 quickly, end on 6 (off)
                    for (int p = 1; p <= 5; p++) {
                        wledActivatePreset(dev.ip, p);
                        delay(700);
                    }
                    wledActivatePreset(dev.ip, 6);  // off
                } else {
                    // For other types, use legacy RGB test via config
                    LightConfig tmp;
                    tmp.type = dev.type;
                    tmp.ip   = dev.ip;
                    tmp.key  = "";
                    tmp.aux  = dev.id;
                    lightTest(tmp);
                }
                drawLightActionScreen(dev, sel, true);
                break;

            case LACT_PROVISION:
                if (dev.type == LIGHT_WLED) {
                    Serial.printf("[Lights] Provisioning %s\n", dev.name.c_str());
                    wledProvisionDevice(dev.ip);
                    dev.provisioned = true;
                }
                drawLightActionScreen(dev, sel, true);
                break;

            case LACT_BACK:
                return;
            }
        }
        delay(50);
    }
}

void handleLights() {
    Serial.println("[Lights] Entering lights submenu");
    auto& devs = lightDevicesGet();

    int selected = 0;
    int scrollOffset = 0;
    const int maxVisible = 6;

    drawLightsScreen(selected, devs, scrollOffset);

    while (true) {
        int totalItems = 2 + (int)devs.size() + 1;  // Discover + Provision All + devices + Back

        if (digitalRead(BOOT_BUTTON) == LOW) {
            delay(200);
            if (g_settings.audioAlerts) audioClick();
            selected = (selected + 1) % totalItems;

            // Adjust scroll to keep selected visible
            if (selected < scrollOffset) scrollOffset = selected;
            if (selected >= scrollOffset + maxVisible) scrollOffset = selected - maxVisible + 1;

            drawLightsScreen(selected, devs, scrollOffset, true);
            while (digitalRead(BOOT_BUTTON) == LOW) delay(50);
        }

        if (digitalRead(PWR_BUTTON) == LOW) {
            delay(200);
            if (g_settings.audioAlerts) audioClick();
            while (digitalRead(PWR_BUTTON) == LOW) delay(50);

            if (selected == 0) {
                // Discover
                Serial.println("[Lights] Running discovery...");
                lightDiscoverAll(g_lightCfg);
                // Reset selection
                selected = 0;
                scrollOffset = 0;
                drawLightsScreen(selected, devs, scrollOffset, true);
            } else if (selected == 1) {
                // Provision All
                Serial.println("[Lights] Provisioning all WLED devices...");
                int count = wledProvisionAll();
                Serial.printf("[Lights] Provisioned %d device(s)\n", count);
                drawLightsScreen(selected, devs, scrollOffset, true);
            } else if (selected == totalItems - 1) {
                // Back
                Serial.println("[Lights] Back to main menu");
                return;
            } else {
                // Device entry — open action screen
                int devIdx = selected - 2;
                if (devIdx >= 0 && devIdx < (int)devs.size()) {
                    handleLightAction(devs[devIdx]);
                    drawLightsScreen(selected, devs, scrollOffset, true);
                }
            }
        }
        delay(50);
    }
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
            if (g_settings.audioAlerts) audioClick();
            selected = (selected + 1) % MENU_COUNT;
            drawMenuScreen(selected, g_settings, g_lightCfg, true);  // partial
            while (digitalRead(BOOT_BUTTON) == LOW) delay(50);
        }

        // PWR = select
        if (digitalRead(PWR_BUTTON) == LOW) {
            delay(200);
            if (g_settings.audioAlerts) audioClick();
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
                bool tokenOk;
                long expSec;
                if (g_settings.platform == PLATFORM_ZOOM) {
                    tokenOk    = zoomHasValidToken();
                    expSec     = zoomGetTokenExpirySeconds();
                } else {
                    tokenOk    = hasValidToken();
                    expSec     = getTokenExpirySeconds();
                }
                drawAuthInfoScreen(tokenOk, expSec,
                                   g_lastAvailability.c_str(), true);
                // BOOT = back to menu, PWR = factory reset
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
            case MENU_LIGHTS:
                handleLights();
                drawMenuScreen(selected, g_settings, g_lightCfg, true);
                break;

            case MENU_SETTINGS:
                handleSettings();
                drawMenuScreen(selected, g_settings, g_lightCfg, true);
                break;

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

// ============================================================================
// handleSettings() — Settings submenu
// ============================================================================
void handleSettings() {
    Serial.println("[Settings] Entering settings submenu");

    int selected = 0;
    drawSettingsScreen(selected, g_settings, g_lightCfg);  // full refresh

    while (true) {
        // BOOT = next item
        if (digitalRead(BOOT_BUTTON) == LOW) {
            delay(200);
            if (g_settings.audioAlerts) audioClick();
            selected = (selected + 1) % SET_COUNT;
            drawSettingsScreen(selected, g_settings, g_lightCfg, true);
            while (digitalRead(BOOT_BUTTON) == LOW) delay(50);
        }

        // PWR = select
        if (digitalRead(PWR_BUTTON) == LOW) {
            delay(200);
            if (g_settings.audioAlerts) audioClick();
            while (digitalRead(PWR_BUTTON) == LOW) delay(50);

            switch (selected) {
            case SET_LIGHT_TYPE: {
                int t = (int)g_lightCfg.type + 1;
                if (t >= (int)LIGHT_TYPE_COUNT) t = (int)LIGHT_NONE;
                g_lightCfg.type = (LightType)t;
                saveLightConfig(g_lightCfg);
                Serial.printf("[Settings] Light type → %d\n", t);
                drawSettingsScreen(selected, g_settings, g_lightCfg, true);
                break;
            }
            case SET_LIGHT_TEST:
                Serial.println("[Settings] Testing light");
                lightTest(g_lightCfg);
                drawSettingsScreen(selected, g_settings, g_lightCfg, true);
                break;

            case SET_INVERT:
                g_settings.invertDisplay = !g_settings.invertDisplay;
                saveSettings(g_settings);
                drawSettingsScreen(selected, g_settings, g_lightCfg, true);
                break;

            case SET_AUDIO:
                g_settings.audioAlerts = !g_settings.audioAlerts;
                saveSettings(g_settings);
                if (g_settings.audioAlerts) audioBeep();  // confirm audio is now ON
                drawSettingsScreen(selected, g_settings, g_lightCfg, true);
                break;

            case SET_BLE_SETUP: {
                Serial.println("[Settings] Starting BLE setup");
                drawSetupScreen();
                startBLEAdvertising();
                waitForAnyButton();
                loadLightConfig(g_lightCfg);
                drawSettingsScreen(selected, g_settings, g_lightCfg, true);
                break;
            }

            case SET_BACK:
                Serial.println("[Settings] Back to main menu");
                return;
            }
        }
        delay(50);
    }
}
