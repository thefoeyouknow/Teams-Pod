// ============================================================================
// Battery monitoring — Waveshare ESP32-S3-ePaper-1.54 V2
//
// ADC1 Channel 3 = GPIO 4, 2:1 resistor divider on the board.
// Uses Arduino analogReadMilliVolts() for calibrated readings.
// ============================================================================

#include "battery.h"

// ---------------------------------------------------------------------------
// Board-specific constants
// ---------------------------------------------------------------------------
static const int   BATT_ADC_PIN       = 4;       // GPIO 4 = ADC1_CH3
static const float DIVIDER_RATIO      = 2.0f;    // 2:1 voltage divider
static const float BATT_FULL_V        = 4.20f;   // LiPo fully charged
static const float BATT_EMPTY_V       = 3.00f;   // LiPo cutoff

// Number of samples to average (reduces noise)
static const int   ADC_SAMPLES        = 16;

// ---------------------------------------------------------------------------
void batteryInit() {
    analogSetAttenuation(ADC_11db);   // 0 – 3.3 V input range
    pinMode(BATT_ADC_PIN, INPUT);
    // Do one throwaway read to prime the ADC
    analogReadMilliVolts(BATT_ADC_PIN);
}

// ---------------------------------------------------------------------------
float batteryReadVoltage() {
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += analogReadMilliVolts(BATT_ADC_PIN);
    }
    float mV = (float)sum / ADC_SAMPLES;
    float voltage = (mV / 1000.0f) * DIVIDER_RATIO;
    return voltage;
}

// ---------------------------------------------------------------------------
int batteryPercent(float voltage) {
    if (voltage >= BATT_FULL_V) return 100;
    if (voltage <= BATT_EMPTY_V) return 0;
    return (int)(((voltage - BATT_EMPTY_V) / (BATT_FULL_V - BATT_EMPTY_V)) * 100.0f);
}

// ---------------------------------------------------------------------------
bool batteryOnUSB(float voltage) {
    // When USB is connected and charging, the measured voltage is typically
    // at or slightly above 4.2 V.  When on battery it droops below 4.2 V
    // almost immediately.  We use a small margin.
    return voltage >= 4.25f;
}
