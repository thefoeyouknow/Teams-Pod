// ============================================================================
// Audio — ES8311 codec + I2S tone generation
//
// Hardware (Waveshare ESP32-S3-ePaper-1.54 V2):
//   ES8311 codec via I2C (SDA=47, SCL=48)
//   I2S output: MCLK=14, BCLK=15, WS=38, DOUT=45, DIN=16
//   PA enable: GPIO 46 (active HIGH)
//   Audio power: GPIO 42 (active LOW)
// ============================================================================

#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>

// Initialise I2C to ES8311, configure codec, set up I2S output.
// Pass playTestTone=false to skip the startup beep (e.g. deep-sleep resume).
void audioInit(bool playTestTone = true);

// Enable/disable the power amplifier and audio power rail.
void audioEnable();
void audioDisable();

// Graceful shutdown — uninstall I2S, power off codec.
void audioShutdown();

// Suspend audio hardware to save power (power-gate codec + I2S stop).
// Call audioResume() before the next tone.  Safe to call multiple times.
void audioSuspend();
void audioResume();

// --- Tone primitives ---

// Play a single sine tone at `freqHz` for `durationMs` milliseconds.
void audioTone(int freqHz, int durationMs);

// --- Canned sound effects ---

// Short click for button presses — plays /audio/click.mp3 or fallback tone
void audioClick();

// Short beep (~100ms, 2kHz) — plays /audio/click.mp3 or fallback tone
void audioBeep();

// Confirmation tone (two ascending beeps)
void audioConfirm();

// Error/warning tone (low buzz)
void audioError();

// Status-change notification — plays /audio/notify.mp3 or fallback tone
void audioNotify();

// Attention tone — plays /audio/attention.mp3, repeated `repeats` times.
// Falls back to synthesized tone if MP3 not found.  Blocking.
void audioAttention(int repeats = 3);

// --- MP3 playback ---

// Play an MP3 file from SD card.  Returns true on success, false if
// the file is missing / unreadable (caller should fall back to a tone).
// The caller is responsible for calling audioAutoSuspend() afterwards.
bool audioPlayMP3(const char* path);

#endif
