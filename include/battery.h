// ============================================================================
// Battery monitoring — Waveshare ESP32-S3-ePaper-1.54 V2
//
// Uses ADC1 Channel 3 (GPIO 4) with on-board 2:1 voltage divider.
// Portable: only the ADC pin and divider ratio are board-specific.
// ============================================================================

#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

// Initialise the ADC pin for battery reading
void batteryInit();

// Read the current battery voltage (returns volts, e.g. 3.85)
float batteryReadVoltage();

// Convert voltage to percentage (LiPo: 4.20V = 100%, 3.00V = 0%)
int batteryPercent(float voltage);

// Returns true when running on USB power (voltage >= 4.25V typical with
// simultaneous charge), false on battery alone.  Approximate — the charging
// IC keeps voltage near 4.2V so this detects the small overshoot.
bool batteryOnUSB(float voltage);

#endif
