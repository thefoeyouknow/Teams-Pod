# Teams Status Puck (v0.50)

**Version:** 0.50 (MVP - Puck Only)  
**Date:** Feb 14, 2026  
**Hardware:** Waveshare ESP32-S3-ePaper-1.54 V2  
**Role:** Portable Microsoft Teams Status Indicator

---

## 1. Core Constraints & Directives

### Scope
This phase focuses **only on the Puck firmware**. All code related to the "Smart Dock" (FM Radio, Amp, Speakers) is **DEFERRED**. Do not generate code for RDA5807M or TPA2016.

### Hardware
Target the **Waveshare ESP32-S3-ePaper-1.54 V2**. This board uses the **ESP32-S3-PICO-1-N8R8** module with OPI PSRAM (8MB) and **8MB Flash** (confirmed via Waveshare wiki, Nov 2025 V2 revision).

### Forbidden
- Do **NOT** reference "IO16" or "NeoPixel" from previous project logs
- This device uses **DotStar (APA102) LEDs** on the dock connector

### Configuration
- **NO Captive Portal**
- Using **Web Bluetooth (BLE)** for initial setup

---

## 2. Hardware Specification

### Microcontroller
**ESP32-S3-PICO-1-N8R8** (V2 module)  
- Flash: **8 MB** (Quad-SPI)  
- PSRAM: **8 MB** (OPI/Octal-SPI, Octal PSRAM)  
- Built-in 512 KB SRAM + 384 KB ROM  
- Dual-core Xtensa LX7 @ up to 240 MHz  
- Wi-Fi 802.11 b/g/n + Bluetooth 5 (BLE)

### Display
- **Type:** 1.54" BW E-Paper (200x200 resolution)
- **Driver:** GxEPD2 (SSD1681/GDEH0154D67)

### Power
- Battery powered (LiPo)
- Deep Sleep is the default state
- Wakes on Button Press or Timer

### Pinout Configuration (Strict)

#### E-Paper Bus (Fixed by Hardware — matches main.cpp #defines)
| Signal | GPIO |
|--------|------|
| BUSY   |  8   |
| RST    |  9   |
| DC     | 10   |
| CS     | 11   |
| SCK    | 12   |
| MOSI   | 13   |

#### Control Buttons
| Signal | GPIO |
|--------|------|
| BOOT button | 0 |
| PWR button  | 18 |
| VBAT latch  | 17 |

#### Dock Connector (2x6 Blind Mate Header)
- **I2C Bus:** SDA (GPIO 45), SCL (GPIO 46) - Used for future dock expansion
- **LEDs (DotStar):** Data (GPIO 47), Clock (GPIO 48) - Blind-fire signals to dock
- **VSYS:** 5V Input (Charging)
- **GND:** Common Ground

> **Note:** DotStar library (`Adafruit DotStar`) is not yet in platformio.ini — add when dock LED control is implemented.

---

## 3. Software Architecture (PlatformIO / Arduino)

### A. The Setup Flow (Web Bluetooth)

**Objective:** Zero-friction setup via phone browser.

**Mechanism:**
1. **Boot Check:** If no WiFi/Azure creds in NVS, enter Setup Mode
2. **Display:** Draw a QR Code containing the URL: `https://[user].github.io/puck-setup/`
3. **BLE Service:** Advertise a GATT Service (UUID 0x00FF)
   - **Characteristics:**
     - SSID (Write)
     - PASSWORD (Write)
     - CLIENT_ID (Write/Read)
     - TENANT_ID (Write/Read)
     - SAVE (Write - Triggers Reboot)

**UX:** User scans QR → Opens Web Client → Connects via BLE → Pushes Creds → Puck Reboots

### B. The Authentication Flow (Microsoft Device Code)

**Library:** Manual HTTP Request implementation (No heavy MSAL libraries)

**Flow:**
1. Request Device Code from MS Graph API
2. Display User Code (e.g., "A1B 2C3") and QR Link to microsoft.com/devicelogin on E-Paper
3. Poll API for Access Token & Refresh Token
4. Save Refresh Token to NVS
5. **Loop:** Use Refresh Token to get new Access Tokens silently

### C. The "Main Loop" (Deep Sleep Strategy)

1. **Wake:** (Reset reason: Timer or Button)
2. **Network:** Connect WiFi (Fast Connect using saved BSSID if possible)
3. **Update:** Use Refresh Token → Get Access Token → Get Teams Presence (`/v1.0/me/presence`)
4. **Render:**
   - If Status != Last_Status: Full/Partial Refresh E-Ink
   - Update LED Data (Blind Fire to Dock pins)
5. **Sleep:** Calculate time to next 15-min interval → Deep Sleep

---

## 4. Implementation Dependencies (platformio.ini)

```ini
[env:waveshare_epaper_s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi
board_build.flash_mode = qio
board_build.psram_type = opi
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags = 
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
    zinggjm/GxEPD2 @ ^1.5.0
    adafruit/Adafruit DotStar @ ^1.2.0
    bblanchon/ArduinoJson @ ^6.21.0
    h2zero/NimBLE-Arduino @ ^1.4.1
    ricmoo/QRCode @ ^0.0.1
```

---

## Next Steps

1. Create `platformio.ini` with the exact configuration above
2. Implement Hardware Validator sketch in `src/main.cpp`
3. Validate PSRAM, Display, and WiFi scanning
4. Proceed with BLE and Authentication flows

