#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <Arduino.h>

// ============================================================================
// SD Card Storage — SDMMC 1-wire interface
//
// Waveshare ESP32-S3-ePaper-1.54 V2 SD card pins:
//   CLK = GPIO 39,  CMD = GPIO 41,  D0 = GPIO 40
//
// Mount point: /sdcard
// Filesystem:  FAT32
//
// Directory layout:
//   /sdcard/config.json       — settings (invertDisplay, audioAlerts, etc.)
//   /sdcard/assets/           — future: images, audio files
// ============================================================================

// Initialise SDMMC and mount filesystem.  Returns true if card mounted OK.
bool sdInit();

// True if SD card is currently mounted and accessible.
bool sdMounted();

// Unmount and de-initialise.
void sdDeinit();

// Get card info string (type + size).  Returns empty string if no card.
String sdCardInfo();

// ---- JSON config file (/sdcard/config.json) ----

struct SdConfig {
    bool   invertDisplay = false;
    bool   audioAlerts   = false;
    int    presenceInterval = 30;       // seconds between presence polls
    int    fullRefreshEvery = 10;       // do full refresh every N partial updates
    String timezone       = "UTC";
};

// Load config from SD.  Returns true on success; fills `cfg` with values.
// Missing keys keep their defaults.
bool sdLoadConfig(SdConfig& cfg);

// Save config to SD.  Returns true on success.
bool sdSaveConfig(const SdConfig& cfg);

// ---- Asset helpers ----

// Check if a file exists on SD card.
bool sdFileExists(const char* path);

// Get file size in bytes.  Returns -1 if file not found.
int32_t sdFileSize(const char* path);

// Read an entire file into a heap-allocated buffer.
// Caller must free() the returned pointer.  Sets `outLen` to bytes read.
// Returns nullptr on failure.
uint8_t* sdReadFile(const char* path, size_t& outLen);

// Read a raw monochrome bitmap (200×200 / 8 = 5000 bytes) for e-paper.
// Returns true and fills `buf` (must be ≥ 5000 bytes).
bool sdLoadBitmap(const char* path, uint8_t* buf, size_t bufLen);

#endif
