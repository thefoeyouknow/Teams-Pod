// ============================================================================
// SD Card Storage — SDMMC 1-wire interface for Waveshare ESP32-S3-ePaper-1.54
// ============================================================================

#include "sd_storage.h"
#include <FS.h>
#include <SD_MMC.h>
#include <ArduinoJson.h>

// SDMMC GPIO pins (Waveshare factory assignments)
#define SDMMC_CLK_PIN   39
#define SDMMC_CMD_PIN   41
#define SDMMC_D0_PIN    40

static bool g_sd_mounted = false;

static const char* CONFIG_PATH = "/config.json";

// ============================================================================
// Init / deinit
// ============================================================================

bool sdInit() {
    if (g_sd_mounted) return true;

    // Configure 1-wire SDMMC on custom GPIOs
    SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN);

    if (!SD_MMC.begin("/sdcard", true /* mode1bit */)) {
        Serial.println("[SD] Mount failed — no card or bad format");
        return false;
    }

    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[SD] No card detected");
        SD_MMC.end();
        return false;
    }

    g_sd_mounted = true;

    const char* typeStr = "UNKNOWN";
    if (cardType == CARD_MMC)       typeStr = "MMC";
    else if (cardType == CARD_SD)   typeStr = "SD";
    else if (cardType == CARD_SDHC) typeStr = "SDHC";

    Serial.printf("[SD] Mounted: %s  %llu MB\n",
                  typeStr, SD_MMC.cardSize() / (1024 * 1024));

    // Create standard directory structure
    const char* dirs[] = { "/audio", "/graphics", "/user" };
    for (auto d : dirs) {
        if (!SD_MMC.exists(d)) {
            SD_MMC.mkdir(d);
            Serial.printf("[SD] Created %s\n", d);
        }
    }

    return true;
}

bool sdMounted() {
    return g_sd_mounted;
}

void sdDeinit() {
    if (g_sd_mounted) {
        SD_MMC.end();
        g_sd_mounted = false;
        Serial.println("[SD] Unmounted");
    }
}

String sdCardInfo() {
    if (!g_sd_mounted) return "";
    uint8_t cardType = SD_MMC.cardType();
    const char* typeStr = "UNKNOWN";
    if (cardType == CARD_MMC)       typeStr = "MMC";
    else if (cardType == CARD_SD)   typeStr = "SD";
    else if (cardType == CARD_SDHC) typeStr = "SDHC";

    char buf[32];
    snprintf(buf, sizeof(buf), "%s %lluMB",
             typeStr, SD_MMC.cardSize() / (1024 * 1024));
    return String(buf);
}

// ============================================================================
// JSON config file
// ============================================================================

bool sdLoadConfig(SdConfig& cfg) {
    if (!g_sd_mounted) return false;

    File f = SD_MMC.open(CONFIG_PATH, FILE_READ);
    if (!f) {
        Serial.println("[SD] No config.json — using defaults");
        return false;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[SD] JSON parse error: %s\n", err.c_str());
        return false;
    }

    cfg.platform         = doc["platform"]         | cfg.platform;
    cfg.invertDisplay    = doc["invertDisplay"]    | cfg.invertDisplay;
    cfg.audioAlerts      = doc["audioAlerts"]      | cfg.audioAlerts;
    cfg.presenceInterval = doc["presenceInterval"] | cfg.presenceInterval;
    cfg.fullRefreshEvery = doc["fullRefreshEvery"] | cfg.fullRefreshEvery;
    cfg.timezone         = doc["timezone"]         | cfg.timezone.c_str();
    cfg.officeHoursEnabled = doc["officeHoursEnabled"] | cfg.officeHoursEnabled;
    cfg.officeStartHour    = doc["officeStartHour"]    | cfg.officeStartHour;
    cfg.officeStartMin     = doc["officeStartMin"]     | cfg.officeStartMin;
    cfg.officeEndHour      = doc["officeEndHour"]      | cfg.officeEndHour;
    cfg.officeEndMin       = doc["officeEndMin"]       | cfg.officeEndMin;
    cfg.officeDays         = doc["officeDays"]         | cfg.officeDays;

    Serial.printf("[SD] Config loaded: platform=%d invert=%d audio=%d interval=%d fullEvery=%d tz=%s\n",
                  cfg.platform, cfg.invertDisplay, cfg.audioAlerts,
                  cfg.presenceInterval, cfg.fullRefreshEvery,
                  cfg.timezone.c_str());
    return true;
}

bool sdSaveConfig(const SdConfig& cfg) {
    if (!g_sd_mounted) return false;

    StaticJsonDocument<1024> doc;
    doc["platform"]         = cfg.platform;
    doc["invertDisplay"]    = cfg.invertDisplay;
    doc["audioAlerts"]      = cfg.audioAlerts;
    doc["presenceInterval"] = cfg.presenceInterval;
    doc["fullRefreshEvery"] = cfg.fullRefreshEvery;
    doc["timezone"]           = cfg.timezone;
    doc["officeHoursEnabled"] = cfg.officeHoursEnabled;
    doc["officeStartHour"]    = cfg.officeStartHour;
    doc["officeStartMin"]     = cfg.officeStartMin;
    doc["officeEndHour"]      = cfg.officeEndHour;
    doc["officeEndMin"]       = cfg.officeEndMin;
    doc["officeDays"]         = cfg.officeDays;

    File f = SD_MMC.open(CONFIG_PATH, FILE_WRITE);
    if (!f) {
        Serial.println("[SD] Failed to open config.json for writing");
        return false;
    }

    size_t written = serializeJsonPretty(doc, f);
    f.close();

    Serial.printf("[SD] Config saved (%d bytes)\n", written);
    return written > 0;
}

// ============================================================================
// Plain text file helpers
// ============================================================================

bool sdWriteText(const char* path, const String& content) {
    if (!g_sd_mounted) {
        Serial.printf("[SD] Write failed (not mounted): %s\n", path);
        return false;
    }

    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[SD] Failed to open for writing: %s\n", path);
        return false;
    }

    size_t written = f.print(content);
    f.close();

    if (written != content.length()) {
        Serial.printf("[SD] Short write: %d/%d bytes to %s\n",
                      written, content.length(), path);
        return false;
    }

    Serial.printf("[SD] Wrote %d bytes to %s\n", written, path);
    return true;
}

String sdReadText(const char* path) {
    if (!g_sd_mounted) return "";

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return "";

    String content = f.readString();
    f.close();
    return content;
}

// ============================================================================
// Asset helpers
// ============================================================================

bool sdFileExists(const char* path) {
    if (!g_sd_mounted) return false;
    return SD_MMC.exists(path);
}

int32_t sdFileSize(const char* path) {
    if (!g_sd_mounted) return -1;
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return -1;
    int32_t sz = f.size();
    f.close();
    return sz;
}

uint8_t* sdReadFile(const char* path, size_t& outLen) {
    outLen = 0;
    if (!g_sd_mounted) return nullptr;

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[SD] File not found: %s\n", path);
        return nullptr;
    }

    size_t sz = f.size();
    if (sz == 0) {
        f.close();
        return nullptr;
    }

    // Allocate in PSRAM if available, else heap
    uint8_t* buf = (uint8_t*)ps_malloc(sz);
    if (!buf) buf = (uint8_t*)malloc(sz);
    if (!buf) {
        Serial.printf("[SD] Failed to allocate %d bytes for %s\n", sz, path);
        f.close();
        return nullptr;
    }

    size_t read = f.read(buf, sz);
    f.close();

    if (read != sz) {
        Serial.printf("[SD] Short read: %d/%d bytes from %s\n", read, sz, path);
        free(buf);
        return nullptr;
    }

    outLen = sz;
    return buf;
}

bool sdLoadBitmap(const char* path, uint8_t* buf, size_t bufLen) {
    if (!g_sd_mounted || !buf) return false;

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[SD] Bitmap not found: %s\n", path);
        return false;
    }

    size_t sz = f.size();
    if (sz > bufLen) {
        Serial.printf("[SD] Bitmap too large: %d > %d\n", sz, bufLen);
        f.close();
        return false;
    }

    size_t read = f.read(buf, sz);
    f.close();

    if (read != sz) {
        Serial.printf("[SD] Short bitmap read: %d/%d\n", read, sz);
        return false;
    }

    Serial.printf("[SD] Loaded bitmap %s (%d bytes)\n", path, sz);
    return true;
}

// ============================================================================
// BMP file loader — 1-bit 200×200 uncompressed
// ============================================================================

static uint32_t readLE32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t readLE16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool sdLoadBMP(const char* path, uint8_t* pixelBuf, size_t bufLen) {
    if (!g_sd_mounted || !pixelBuf) return false;

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[SD] BMP not found: %s\n", path);
        return false;
    }

    // Read BMP + DIB header (62 bytes typical for 1-bit BMP with colour table)
    uint8_t hdr[66];
    size_t hdrRead = f.read(hdr, 66);
    if (hdrRead < 54) {
        Serial.println("[SD] BMP header too short");
        f.close();
        return false;
    }

    if (hdr[0] != 'B' || hdr[1] != 'M') {
        Serial.println("[SD] Not a BMP file");
        f.close();
        return false;
    }

    uint32_t dataOffset  = readLE32(hdr + 10);
    int32_t  width       = (int32_t)readLE32(hdr + 18);
    int32_t  height      = (int32_t)readLE32(hdr + 22);
    uint16_t bpp         = readLE16(hdr + 28);
    uint32_t compression = readLE32(hdr + 30);

    if (width != 200 || (height != 200 && height != -200) || bpp != 1 || compression != 0) {
        Serial.printf("[SD] BMP format error: %dx%d %dbpp comp=%d\n",
                      width, height, bpp, compression);
        f.close();
        return false;
    }

    // Check colour table (starts at byte 54) to decide if bits need inverting.
    // Standard: entry 0 = black (0,0,0), entry 1 = white (255,255,255).
    // If entry 0 is bright, colours are swapped and we XOR the output.
    bool invertBits = false;
    if (hdrRead >= 58) {
        // Entry 0: B,G,R,A at bytes 54-57
        if (hdr[54] > 128 || hdr[55] > 128 || hdr[56] > 128)
            invertBits = true;
    }

    int absHeight   = (height > 0) ? height : -height;
    bool bottomUp   = (height > 0);
    int rowBytes    = (width + 7) / 8;                // 25 for 200px
    int rowStride   = ((rowBytes + 3) / 4) * 4;       // 28 (padded to 4)
    int outRowBytes = rowBytes;                        // 25 (no padding)

    if ((size_t)(outRowBytes * absHeight) > bufLen) {
        Serial.println("[SD] BMP pixel buffer too small");
        f.close();
        return false;
    }

    f.seek(dataOffset);

    uint8_t rowBuf[32];   // max 28 bytes per row for 200px
    for (int row = 0; row < absHeight; row++) {
        if (f.read(rowBuf, rowStride) != (size_t)rowStride) {
            Serial.printf("[SD] BMP short read at row %d\n", row);
            f.close();
            return false;
        }
        int outRow = bottomUp ? (absHeight - 1 - row) : row;
        memcpy(pixelBuf + outRow * outRowBytes, rowBuf, outRowBytes);
    }
    f.close();

    // Normalise: ensure bit 0 = black, bit 1 = white
    if (invertBits) {
        int total = outRowBytes * absHeight;
        for (int i = 0; i < total; i++) pixelBuf[i] ^= 0xFF;
    }

    Serial.printf("[SD] BMP loaded: %s (%dx%d)\n", path, width, absHeight);
    return true;
}
