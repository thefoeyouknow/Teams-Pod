// ============================================================================
// Audio — ES8311 codec + I2S tone generation
//
// Uses the ESP-IDF I2S driver to push PCM samples through the ES8311 DAC.
// The ES8311 is configured for I2S slave mode via I2C registers.
// ============================================================================

#include "audio.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

// ---- Hardware pins ----
#define AUDIO_PWR_PIN     42    // Audio power rail — ACTIVE LOW
#define PA_EN_PIN         46    // Power amplifier enable — ACTIVE HIGH
#define I2S_MCLK_PIN      14
#define I2S_BCLK_PIN      15
#define I2S_WS_PIN        38
#define I2S_DOUT_PIN      45
#define I2S_DIN_PIN       16
#define I2C_SDA_PIN       48
#define I2C_SCL_PIN       47

// ---- ES8311 I2C ----
#define ES8311_ADDR       0x18   // 7-bit address

// ES8311 register map (subset we need)
#define ES8311_REG00_RESET      0x00
#define ES8311_REG01_CLK_MGR    0x01
#define ES8311_REG02_CLK_MGR    0x02
#define ES8311_REG03_CLK_MGR    0x03
#define ES8311_REG04_CLK_MGR    0x04
#define ES8311_REG05_CLK_MGR    0x05
#define ES8311_REG06_CLK_MGR    0x06
#define ES8311_REG07_CLK_MGR    0x07
#define ES8311_REG08_CLK_MGR    0x08
#define ES8311_REG09_SDPIN      0x09
#define ES8311_REG0A_SDPOUT     0x0A
#define ES8311_REG0B_SYSTEM     0x0B
#define ES8311_REG0C_SYSTEM     0x0C
#define ES8311_REG0D_SYSTEM     0x0D
#define ES8311_REG0E_SYSTEM     0x0E
#define ES8311_REG0F_SDPOUT     0x0F
#define ES8311_REG10_SYSTEM     0x10
#define ES8311_REG11_SYSTEM     0x11
#define ES8311_REG12_SYSTEM     0x12
#define ES8311_REG13_SYSTEM     0x13
#define ES8311_REG14_SYSTEM     0x14
#define ES8311_REG32_DAC        0x32

// ---- I2S config ----
#define I2S_PORT          I2S_NUM_0
#define SAMPLE_RATE       16000
#define TONE_AMPLITUDE    8000    // 16-bit signed amplitude

static bool g_audioInitialized = false;
static bool g_audioEnabled     = false;

// ---- ES8311 I2C helpers ----

static bool es8311_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static uint8_t es8311_read(uint8_t reg) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

// ---- ES8311 codec init ----

static bool es8311_init() {
    // Soft reset
    es8311_write(ES8311_REG00_RESET, 0x1F);
    delay(20);
    es8311_write(ES8311_REG00_RESET, 0x80);  // normal mode, power up

    // Clock manager — I2S slave mode, MCLK from master
    es8311_write(ES8311_REG01_CLK_MGR, 0x3F);  // MCLK on, all clocks on
    es8311_write(ES8311_REG02_CLK_MGR, 0x00);  // MCLK/BCLK = auto
    es8311_write(ES8311_REG03_CLK_MGR, 0x10);  // MCLK divider
    es8311_write(ES8311_REG04_CLK_MGR, 0x10);  // BCLK divider
    es8311_write(ES8311_REG05_CLK_MGR, 0x00);  // clock select
    es8311_write(ES8311_REG06_CLK_MGR, 0x03);  // ADC/DAC OSR
    es8311_write(ES8311_REG07_CLK_MGR, 0x00);  // ADC OSR
    es8311_write(ES8311_REG08_CLK_MGR, 0xFF);  // DAC OSR

    // SDP (Serial Data Port) — I2S, 16-bit, slave
    es8311_write(ES8311_REG09_SDPIN,  0x0C);   // I2S mode, 16-bit input
    es8311_write(ES8311_REG0A_SDPOUT, 0x0C);   // I2S mode, 16-bit output

    // System — power on DAC + analog
    es8311_write(ES8311_REG0B_SYSTEM, 0x00);   // power on
    es8311_write(ES8311_REG0C_SYSTEM, 0x00);   // power on
    es8311_write(ES8311_REG0D_SYSTEM, 0x01);   // enable DAC
    es8311_write(ES8311_REG0E_SYSTEM, 0x02);   // DAC enabled
    es8311_write(ES8311_REG0F_SDPOUT, 0x00);   // unmute
    es8311_write(ES8311_REG10_SYSTEM, 0x1F);   // enable analog
    es8311_write(ES8311_REG11_SYSTEM, 0x7F);   // analog gain
    es8311_write(ES8311_REG12_SYSTEM, 0x00);   // DAC unmute
    es8311_write(ES8311_REG13_SYSTEM, 0x10);   // output mixer
    es8311_write(ES8311_REG14_SYSTEM, 0x1A);   // power on analog

    // DAC volume
    es8311_write(ES8311_REG32_DAC, 0xBF);      // ~75% volume

    uint8_t chipId = es8311_read(0xFD);
    Serial.printf("[Audio] ES8311 chip ID: 0x%02X\n", chipId);

    return chipId != 0xFF;
}

// ---- I2S init ----

static bool i2s_init_output() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.use_apll = true;
    cfg.tx_desc_auto_clear = true;

    if (i2s_driver_install(I2S_PORT, &cfg, 0, NULL) != ESP_OK) {
        Serial.println("[Audio] I2S driver install failed");
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_MCLK_PIN;
    pins.bck_io_num   = I2S_BCLK_PIN;
    pins.ws_io_num    = I2S_WS_PIN;
    pins.data_out_num = I2S_DOUT_PIN;
    pins.data_in_num  = I2S_DIN_PIN;

    if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
        Serial.println("[Audio] I2S pin config failed");
        return false;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    return true;
}

// ============================================================================
// Public API
// ============================================================================

void audioInit() {
    if (g_audioInitialized) return;

    // Power on audio rail
    pinMode(AUDIO_PWR_PIN, OUTPUT);
    digitalWrite(AUDIO_PWR_PIN, LOW);  // active low
    delay(50);

    // PA off initially
    pinMode(PA_EN_PIN, OUTPUT);
    digitalWrite(PA_EN_PIN, LOW);

    // I2C for ES8311 (may already be initialised for other I2C devices)
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);

    if (!es8311_init()) {
        Serial.println("[Audio] ES8311 init failed — audio disabled");
        return;
    }

    if (!i2s_init_output()) {
        Serial.println("[Audio] I2S init failed — audio disabled");
        return;
    }

    g_audioInitialized = true;
    Serial.println("[Audio] Initialized (ES8311 + I2S)");
}

void audioEnable() {
    if (!g_audioInitialized) return;
    digitalWrite(PA_EN_PIN, HIGH);
    g_audioEnabled = true;
    delay(30);  // let PA stabilize
}

void audioDisable() {
    if (!g_audioInitialized) return;
    // Flush silence to avoid pop
    int16_t silence[64] = {0};
    size_t written;
    i2s_write(I2S_PORT, silence, sizeof(silence), &written, 50);
    delay(20);
    digitalWrite(PA_EN_PIN, LOW);
    g_audioEnabled = false;
}

// ---- Tone generation ----

void audioTone(int freqHz, int durationMs) {
    if (!g_audioInitialized) return;

    audioEnable();

    const int totalSamples = (SAMPLE_RATE * durationMs) / 1000;
    const float omega = 2.0f * M_PI * freqHz / SAMPLE_RATE;

    // Generate in small blocks
    const int blockSize = 128;
    int16_t buf[blockSize * 2];  // stereo: L, R interleaved
    size_t written;
    int samplesLeft = totalSamples;

    for (int offset = 0; samplesLeft > 0; ) {
        int count = min(blockSize, samplesLeft);
        for (int i = 0; i < count; i++) {
            int16_t sample = (int16_t)(sinf(omega * (offset + i)) * TONE_AMPLITUDE);
            buf[i * 2]     = sample;  // left
            buf[i * 2 + 1] = sample;  // right
        }
        i2s_write(I2S_PORT, buf, count * 4, &written, 100);
        offset += count;
        samplesLeft -= count;
    }

    audioDisable();
}

// ---- Canned effects ----

void audioBeep() {
    audioTone(2000, 50);
}

void audioConfirm() {
    audioTone(1800, 80);
    delay(30);
    audioTone(2400, 80);
}

void audioError() {
    audioTone(400, 200);
}

void audioChirp(int repeats) {
    for (int i = 0; i < repeats; i++) {
        audioTone(2800, 100);
        delay(60);
        audioTone(3200, 100);
        delay(200);
    }
}
