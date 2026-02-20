# Teams Pod — Firmware

**Version:** 0.14.003  
**Target Hardware:** Waveshare ESP32-S3-ePaper-1.54 **V2** (ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB OPI PSRAM)  
**Platform:** PlatformIO / Arduino

---

## Overview

Status Pod is a battery-powered Microsoft Teams / Zoom status indicator.  
- **1.54″ e-ink display** (200×200, black/white)
- **ES8311 audio codec** — click/beep/tone audio alerts  
- **SD card** — primary config store (`config.json`, `refresh_token.txt`)  
- **NVS** — credential/config fallback  
- **NimBLE** provisioning — initial setup via Web Bluetooth companion page  
- **Deep sleep** — wakes on timer or button; fast-poll without full boot  
- **Microsoft Device Code Flow** for Teams auth  
- **Zoom S2S OAuth** for Zoom auth  
- **Smart light integration** — WLED, Hue, WiZ

---

## Quick Start

```bash
# Build
pio run -e waveshare_epaper_s3

# Flash
pio run -e waveshare_epaper_s3 -t upload

# Serial monitor
pio device monitor -e waveshare_epaper_s3
```

---

## Project Structure

```
Teams Puck/
├── platformio.ini              # Build config, libs, flags
├── PROJECT_SEED_v0.50.md       # Design specification
├── src/
│   ├── main.cpp                # State machine, setup/loop
│   ├── ble_setup.cpp           # NimBLE provisioning
│   ├── teams_auth.cpp          # Device Code flow + token refresh
│   ├── teams_presence.cpp      # Graph /me/presence poller
│   ├── zoom_auth.cpp           # Zoom S2S OAuth
│   ├── zoom_presence.cpp       # Zoom presence poller
│   ├── display_ui.cpp          # GxEPD2 screen rendering
│   ├── audio.cpp               # ES8311 codec + I2S tones
│   ├── battery.cpp             # ADC + USB SOF detection
│   ├── light_control.cpp       # WLED / Hue / WiZ HTTP control
│   ├── light_devices.cpp       # mDNS + UDP discovery, provisioning
│   ├── sd_storage.cpp          # SDMMC + JSON config helpers
│   └── settings.cpp            # SD-primary / NVS-fallback settings
└── include/                    # Header files
```

---

## Pin Configuration

*All GPIOs verified against `main.cpp` `#define`s.*

### E-Paper Display (SPI)

| Signal | GPIO |
|--------|------|
| BUSY   | 8    |
| RST    | 9    |
| DC     | 10   |
| CS     | 11   |
| SCK    | 12   |
| MOSI   | 13   |

### Control

| Signal      | GPIO |
|-------------|------|
| BOOT button | 0    |
| PWR button  | 18   |
| VBAT latch  | 17   |
| Bat ADC     | 4    |

### Audio (I2S + I2C codec)

| Signal  | GPIO |
|---------|------|
| I2S BCLK | 15  |
| I2S LRCK | 16  |
| I2S DOUT | 14  |
| I2C SDA  | 7   |
| I2C SCL  | 6   |

### SD Card (SDMMC 1-bit)

| Signal | GPIO |
|--------|------|
| CLK    | 36   |
| CMD    | 35   |
| DATA0  | 37   |

---

## First-Time Setup

1. Power on the device  
2. If no credentials are stored, the e-ink display shows a setup QR code  
3. Scan QR → opens Web Bluetooth companion page  
4. Enter WiFi SSID/password + Azure Client ID / Tenant ID → tap **Save**  
5. Device reboots, connects to WiFi, and begins Teams Device Code auth  
6. Scan the on-screen QR on your phone → authenticate → device is live

---

## State Machine

```
BOOT → SETUP_BLE (no creds)
     → CONNECTING_WIFI
         → AUTH_DEVICE_CODE (Teams, first run)
         → RUNNING
             ↓ deep sleep threshold reached
             DEEP SLEEP (timer + button wake)
```

---

## Security Notes

- All HTTPS connections currently use `setInsecure()` — root CA pinning is a production TODO  
- WiFi credentials and OAuth secrets stored in NVS (not encrypted by default)

---

## Troubleshooting

**Board not detected**  
Use a USB-C cable that supports data (not charge-only); ensure CH340/CP210x or built-in USB drivers are installed.

**PSRAM not detected**  
Verify `build_flags` in `platformio.ini` include `-DBOARD_HAS_PSRAM` and `-mfix-esp32-psram-cache-issue`.

**Display doesn't init**  
Confirm SPI pins match the table above. Use `GxEPD2 ^1.5.0`.

**Auth fails immediately**  
Check that Client ID and Tenant ID were provisioned via BLE. Use Device Info screen (menu) to verify.

---

**Hardware:** Waveshare ESP32-S3-ePaper-1.54 V2 ([Wiki](https://www.waveshare.com/wiki/ESP32-S3-ePaper-1.54))  
**Updated:** Feb 18, 2026
