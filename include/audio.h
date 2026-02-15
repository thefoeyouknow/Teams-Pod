// ============================================================================
// Audio — ES8311 codec + I2S tone generation
//
// Hardware (Waveshare ESP32-S3-ePaper-1.54 V2):
//   ES8311 codec via I2C (SCL=47, SDA=48)
//   I2S output: MCLK=14, BCLK=15, WS=38, DOUT=45, DIN=16
//   PA enable: GPIO 46 (active HIGH)
//   Audio power: GPIO 42 (active LOW)
// ============================================================================

#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>

// Initialise I2C to ES8311, configure codec, set up I2S output.
void audioInit();

// Enable/disable the power amplifier and audio power rail.
void audioEnable();
void audioDisable();

// --- Tone primitives ---

// Play a single sine tone at `freqHz` for `durationMs` milliseconds.
void audioTone(int freqHz, int durationMs);

// --- Canned sound effects ---

// Short click/beep for button presses (~50ms, 2kHz)
void audioBeep();

// Confirmation tone (two ascending beeps)
void audioConfirm();

// Error/warning tone (low buzz)
void audioError();

// Urgent repeating chirp for re-auth required.
// Plays `repeats` chirps with gaps.  Blocking.
void audioChirp(int repeats = 3);

#endif
