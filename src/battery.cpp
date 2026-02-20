// ============================================================================
// Battery monitoring — Waveshare ESP32-S3-ePaper-1.54 V2
//
// ADC1 Channel 3 = GPIO 4, 2:1 resistor divider on the board.
// Uses Arduino analogReadMilliVolts() for calibrated readings.
// ============================================================================

#include "battery.h"
#include "soc/usb_serial_jtag_reg.h"
#include <driver/gpio.h>

// ---------------------------------------------------------------------------
// Board-specific constants
// ---------------------------------------------------------------------------
static const int   BATT_ADC_PIN       = 4;       // GPIO 4 = ADC1_CH3
static const float DIVIDER_RATIO      = 2.0f;    // 2:1 voltage divider
static const float BATT_FULL_V        = 4.10f;   // Charge IC tops out ~4.10-4.15V
static const float BATT_EMPTY_V       = 3.00f;   // LiPo cutoff

// Number of samples to average (reduces noise)
static const int   ADC_SAMPLES        = 16;

// ---------------------------------------------------------------------------
void batteryInit() {
    analogSetAttenuation(ADC_11db);   // 0 – 3.3 V input range
    pinMode(BATT_ADC_PIN, INPUT);
    // Do one throwaway read to prime the ADC
    analogReadMilliVolts(BATT_ADC_PIN);

    // Green charge LED on GPIO 3 (JTAG pin — release from JTAG first)
    gpio_reset_pin(GPIO_NUM_3);
    pinMode(CHARGE_LED_PIN, OUTPUT);
    digitalWrite(CHARGE_LED_PIN, LOW);
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
    // Primary: voltage >= 4.25V (charge IC overshoot)
    if (voltage >= 4.25f) return true;

    // Secondary: USB SOF frame counter — the USB host sends Start-of-Frame
    // packets every 1ms.  If the counter changes across two reads, USB is
    // physically connected and enumerated, regardless of battery voltage.
    uint32_t sof1 = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & 0x7FF;
    delay(2);  // wait >1 SOF interval (1 ms)
    uint32_t sof2 = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & 0x7FF;
    if (sof1 != sof2) return true;

    return false;
}

// ---------------------------------------------------------------------------
void batteryUpdateChargeLED(bool usbConnected) {
    digitalWrite(CHARGE_LED_PIN, usbConnected ? HIGH : LOW);
}
