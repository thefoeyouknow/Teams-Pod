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

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[SD] JSON parse error: %s\n", err.c_str());
        return false;
    }

    cfg.invertDisplay    = doc["invertDisplay"]    | cfg.invertDisplay;
    cfg.audioAlerts      = doc["audioAlerts"]      | cfg.audioAlerts;
    cfg.presenceInterval = doc["presenceInterval"] | cfg.presenceInterval;
    cfg.fullRefreshEvery = doc["fullRefreshEvery"] | cfg.fullRefreshEvery;
    cfg.timezone         = doc["timezone"]         | cfg.timezone.c_str();

    Serial.printf("[SD] Config loaded: invert=%d audio=%d interval=%d fullEvery=%d tz=%s\n",
                  cfg.invertDisplay, cfg.audioAlerts,
                  cfg.presenceInterval, cfg.fullRefreshEvery,
                  cfg.timezone.c_str());
    return true;
}

bool sdSaveConfig(const SdConfig& cfg) {
    if (!g_sd_mounted) return false;

    StaticJsonDocument<512> doc;
    doc["invertDisplay"]    = cfg.invertDisplay;
    doc["audioAlerts"]      = cfg.audioAlerts;
    doc["presenceInterval"] = cfg.presenceInterval;
    doc["fullRefreshEvery"] = cfg.fullRefreshEvery;
    doc["timezone"]         = cfg.timezone;

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
