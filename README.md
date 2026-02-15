# Teams Pod - Firmware Project

**Version:** 0.50 (MVP - Pod Only)  
**Target Hardware:** Waveshare ESP32-S3-ePaper-1.54 V2  
**Date:** Feb 14, 2026

## Project Overview

Teams Pod is a portable Microsoft Teams status indicator built on the ESP32-S3. This MVP focuses exclusively on the pod firmware, with dock functionality (FM Radio, Amp, Speakers) deferred to future versions.

## Quick Start

### 1. Open in PlatformIO

```bash
cd "c:\Users\MikeS\Documents\Arduino\Teams Puck"
# Open in VS Code and install PlatformIO extension
```

### 2. Build the Hardware Validator

```bash
pio run -e waveshare_epaper_s3
```

### 3. Upload to Board

```bash
pio run -e waveshare_epaper_s3 -t upload
```

### 4. Monitor Serial Output

```bash
pio device monitor -e waveshare_epaper_s3
```

## Project Structure

```
Teams Pod/
├── platformio.ini             # Build configuration
├── PROJECT_SEED_v0.50.md      # Design specification
├── src/
│   └── main.cpp               # Hardware Validator Sketch
└── include/                   # Header files (future)
```

## Hardware Validator Capabilities

The `main.cpp` sketch validates:

✓ **PSRAM** - Initializes and reports 8MB PSRAM status  
✓ **Display** - Initializes GxEPD2 and renders test screen  
✓ **WiFi** - Scans and lists available networks  
✓ **Button** - Press to re-trigger WiFi scan  

### Serial Output Example

```
=== Teams Pod Hardware Validator ===
Version: 0.50 (MVP - Puck Only)
Target: Waveshare ESP32-S3-ePaper-1.54 V2

Microcontroller: ESP32-S3 (Chip ID: 0x0014)
CPU Freq: 240 MHz
Flash: 16 MB

--- PSRAM Initialization ---
✓ PSRAM Detected
  Total PSRAM: 8 MB (8388608 bytes)
  Free PSRAM:  8128.0 KB
  SRAM Heap:   254 KB / 359 KB

--- Display Initialization ---
✓ GxEPD2 Display initialized
  Resolution: 200 x 200

Drawing test screen...
Drawing QR code placeholder...
✓ Test screen rendered

--- WiFi Network Scan ---
Scanning WiFi networks...
Found 3 network(s):
  [1] SSID: HomeNetwork          | RSSI: -45 dBm | CH:  6 | SECURED
  [2] SSID: GuestWiFi            | RSSI: -72 dBm | CH: 11 | SECURED
  [3] SSID: Neighbor             | RSSI: -88 dBm | CH:  1 | SECURED
✓ WiFi scan complete (3 networks)

=== Hardware Validation Complete ===
```

## Pin Configuration

### E-Paper Display (SPI)
- **BUSY:** GPIO 25
- **RST:** GPIO 26
- **DC:** GPIO 27
- **CS:** GPIO 15
- **SCK:** GPIO 13 (SPI Clock)
- **MOSI:** GPIO 14 (SPI Data)

### Dock Connector (I2C + LEDs + Button)
- **SDA:** GPIO 17 (I2C)
- **SCL:** GPIO 18 (I2C)
- **LED_DATA:** GPIO 10 (DotStar)
- **LED_CLK:** GPIO 11 (DotStar)
- **BUTTON:** GPIO 3 (Active Low, Pull-Up)
- **VSYS:** 5V Input (Dock Charging)
- **GND:** Common Ground

## Next Steps

After validating hardware:

1. **BLE Setup Flow** - Implement Web Bluetooth for initial WiFi/Azure credential setup
2. **Microsoft Device Code Flow** - Authenticate with Microsoft Graph API
3. **Team Presence Polling** - Fetch Teams status every 15 minutes
4. **Deep Sleep Strategy** - Implement power-efficient wake/sleep cycle
5. **E-Ink Rendering** - Display Teams status with color-coded indicators

See `PROJECT_SEED_v0.50.md` for detailed architecture specifications.

## Recommended Chat Prompt

Once you've validated hardware successfully, open a new Agent chat with:

```
@workspace I have established the project context in PROJECT_SEED_v0.50.md 
and the hardware (Waveshare ESP32-S3) is connected. The Hardware Validator 
sketch is complete and functional.

Now please implement the BLE Setup Flow. Specifically:

1. Create a BEL Service with UUID 0x00FF and the following Characteristics:
   - SSID (Write)
   - PASSWORD (Write)
   - CLIENT_ID (Write/Read)
   - TENANT_ID (Write/Read)
   - SAVE (Write - Triggers Reboot)

2. When the puck boots without WiFi credentials, advertise this BLE service

3. Update the display to show a QR code with: https://[user].github.io/puck-setup/

4. Store credentials in NVS when SAVE is written

Do not implement the Microsoft authentication yet; focus on the BLE 
characteristic handling and NVS persistence.
```

## Troubleshooting

**Board Detection Issues**
- Ensure USB drivers for ESP32-S3 are installed
- Use USB-C cable that supports data transfer (not charge-only)

**PSRAM Not Detected**
- Verify `platformio.ini` has correct OPI PSRAM settings
- Check physical PSRAM chip on board

**Display Won't Initialize**
- Confirm SPI pins match hardware (GPIO 13, 14, 15, 27)
- Verify GxEPD2 library version matches (^1.5.0)

**WiFi Scanning Fails**
- Ensure antenna is connected to ESP32-S3
- Check if WiFi power domain is stable

---

**Created:** Feb 14, 2026  
**Creator:** GitHub Copilot  
**Project Status:** Hardware Validation Phase ✓
