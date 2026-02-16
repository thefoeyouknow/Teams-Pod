// ============================================================================
// Audio — ES8311 codec + I2S tone generation
//
// Uses the ESP-IDF I2S driver to push PCM samples through the ES8311 DAC.
// The ES8311 is configured for I2S slave mode via I2C registers.
//
// IMPORTANT: I2S must be started BEFORE ES8311 init so that MCLK is running
// when the codec configures its internal clock tree.
//
// I2S clocks run continuously after init.  Light sleep naturally pauses them;
// on wake they resume automatically.  This avoids the ES8311 losing its
// clock state machine between tones.
//
// Register sequence derived from Espressif's official esp-adf ES8311 driver.
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
#define I2C_SDA_PIN       47
#define I2C_SCL_PIN       48

// ---- ES8311 I2C ----
#define ES8311_ADDR       0x18   // 7-bit address

// ---- I2S config ----
#define I2S_PORT          I2S_NUM_0
#define SAMPLE_RATE       16000
#define DMA_BUF_COUNT     4
#define DMA_BUF_LEN       256      // samples per DMA buffer
#define TONE_AMPLITUDE    24000    // ~73% of int16 max — loud enough for tiny speaker

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

// ---- ES8311 codec init (Espressif esp-adf proven sequence) ----
//
// Prerequisites: MCLK must already be running (I2S started first).
// Config: Slave mode, 16kHz, 16-bit I2S, DAC-only playback.

// ---- Register dump for debugging ----

static void es8311_dump_regs() {
    Serial.println("[Audio] === ES8311 Register Dump ===");
    const uint8_t regs[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x1B, 0x1C,
        0x32, 0x37, 0x44, 0x45, 0xFD
    };
    for (int i = 0; i < (int)(sizeof(regs)/sizeof(regs[0])); i++) {
        Serial.printf("  REG 0x%02X = 0x%02X\n", regs[i], es8311_read(regs[i]));
    }
    Serial.println("[Audio] === End Dump ===");
}

// ---- ES8311 codec init (matches Waveshare factory firmware exactly) ----
//
// Source: waveshareteam/ESP32-S3-ePaper-1.54  08_Audio_Test
//         esp_codec_dev/device/es8311/es8311.c
//
// Prerequisites: MCLK must already be running (I2S started first).
// Config: Slave mode, 16kHz, 16-bit I2S, 32-bit slots, DAC playback.
//
// Clock coefficient table for MCLK=4096000, Fs=16000:
//   pre_div=1, pre_multi=×1, adc_div=1, dac_div=1
//   fs_mode=0, lrck_h=0x00, lrck_l=0xFF, bclk_div=4
//   adc_osr=0x10, dac_osr=0x20
//
// CRITICAL encoding notes (ESP-ADF driver):
//   REG 0x02: pre_div is encoded as (value-1)<<5, pre_multi ×1=0,×2=1,×4=2,×8=3 shifted <<3
//   REG 0x06: bclk_div is encoded as (value-1) in bits[4:0]
//   So bclk_div=4 → register = 0x03, NOT 0x04
//   And pre_multi=×1 → register bits[4:3] = 00, NOT 01

static bool es8311_init() {
    // ── Phase 1: Open — basic codec setup ──
    es8311_write(0x44, 0x08);   // I2C noise filter (write twice for reliability)
    es8311_write(0x44, 0x08);
    es8311_write(0x01, 0x30);   // Clock manager: disable all clocks initially
    es8311_write(0x02, 0x00);   // Clock divider defaults
    es8311_write(0x03, 0x10);   // ADC OSR = 0x10
    es8311_write(0x16, 0x24);   // MIC gain 36dB (recording, harmless)
    es8311_write(0x04, 0x10);   // DAC OSR initial
    es8311_write(0x05, 0x00);   // ADC/DAC clock divider = 1
    es8311_write(0x0B, 0x00);   // System power ref
    es8311_write(0x0C, 0x00);   // System power ref
    es8311_write(0x10, 0x1F);   // Enable analog block
    es8311_write(0x11, 0x7F);   // DAC bias / analog settings
    es8311_write(0x00, 0x80);   // CSM power-up enable, slave mode

    // ── Phase 2: Slave mode + clocks on ──
    es8311_write(0x00, 0x80);   // Confirm: CSM on (bit7=1), slave (bit6=0)
    es8311_write(0x01, 0x3F);   // Enable all clocks, MCLK from pin
    es8311_write(0x06, 0x00);   // SCLK not inverted (clear before clock config)
    es8311_write(0x13, 0x10);   // VMID reference config
    es8311_write(0x1B, 0x0A);   // ALC / ADC HPF stage 1
    es8311_write(0x1C, 0x6A);   // ADC HPF stage 2
    es8311_write(0x44, 0x08);   // Keep I2C noise filter (factory ends open with 0x08)

    // ── Phase 3: SDP format — 16-bit I2S (data in upper bits of 32-bit slot) ──
    es8311_write(0x09, 0x0C);   // SDP In:  16-bit (bits[4:2]=011), I2S fmt (bits[1:0]=00)
    es8311_write(0x0A, 0x0C);   // SDP Out: 16-bit, I2S format, unmuted

    // ── Phase 4: Clock coefficients for MCLK=4.096MHz, Fs=16kHz ──
    //   pre_div=1 → (1-1)<<5 = 0x00   pre_multi=×1 → 0<<3 = 0x00
    es8311_write(0x02, 0x00);   // pre_div=1, pre_multi=×1
    es8311_write(0x05, 0x00);   // adc_div=1, dac_div=1
    es8311_write(0x03, 0x10);   // fs_mode=single speed, adc_osr=0x10
    es8311_write(0x04, 0x20);   // dac_osr=0x20
    es8311_write(0x07, 0x00);   // lrck_h=0x00
    es8311_write(0x08, 0xFF);   // lrck_l=0xFF → LRCK = MCLK/256 = 16kHz
    //   bclk_div=4 → register = (4-1) = 0x03
    es8311_write(0x06, 0x03);   // bclk_div=4 (encoded as value-1)

    // ── Phase 5: Power up & enable DAC ──
    es8311_write(0x00, 0x80);   // CSM on, slave
    es8311_write(0x01, 0x3F);   // All clocks enabled
    es8311_write(0x09, 0x0C);   // DAC SDP unmuted (bit6=0)
    es8311_write(0x17, 0xBF);   // ADC/DAC analog ref voltage
    es8311_write(0x0E, 0x02);   // Power up analog circuitry
    es8311_write(0x12, 0x00);   // Enable DAC (0x00=enabled)
    es8311_write(0x14, 0x1A);   // Analog PGA gain, DMIC disabled
    es8311_write(0x0D, 0x01);   // Power up digital core
    es8311_write(0x15, 0x40);   // ADC ramp rate
    es8311_write(0x37, 0x08);   // DAC ramp rate (prevents pops)
    es8311_write(0x45, 0x00);   // GP control: normal operation

    // ── Phase 6: Volume ──
    es8311_write(0x32, 0xBF);   // DAC volume 0dB

    // Verify chip is responding
    uint8_t chipId = es8311_read(0xFD);
    Serial.printf("[Audio] ES8311 chip ID: 0x%02X\n", chipId);

    // Dump all registers for debugging
    es8311_dump_regs();

    return chipId != 0xFF;
}

// ---- I2S init ----

static bool i2s_init_output() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;  // 32-bit slot to match ES8311 SDP config
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = DMA_BUF_COUNT;
    cfg.dma_buf_len = DMA_BUF_LEN;
    cfg.use_apll = true;           // APLL for accurate MCLK generation
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = SAMPLE_RATE * 256;  // MCLK = 4.096 MHz for 16kHz

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

// ---- DMA flush: write silence to push remaining tone data through pipeline --

static void i2s_flush_dma() {
    int32_t silence[DMA_BUF_LEN * 2] = {0};  // one full DMA buffer of stereo silence
    size_t written;
    for (int i = 0; i < DMA_BUF_COUNT; i++) {
        i2s_write(I2S_PORT, silence, sizeof(silence), &written, 200);
    }
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

    // *** CRITICAL: Start I2S FIRST so MCLK is running before ES8311 init ***
    // The ES8311 codec needs MCLK to configure its internal clock tree.
    if (!i2s_init_output()) {
        Serial.println("[Audio] I2S init failed — audio disabled");
        return;
    }
    delay(50);  // let MCLK stabilize

    if (!es8311_init()) {
        Serial.println("[Audio] ES8311 init failed — audio disabled");
        return;
    }

    // I2S clocks keep running — ES8311 needs continuous MCLK.
    // Light sleep automatically pauses/resumes I2S clocks.
    // tx_desc_auto_clear ensures silence when no data is written.

    g_audioInitialized = true;
    Serial.println("[Audio] Initialized (I2S + ES8311)");

    // ---- Startup test tone (unconditional — bypasses audioAlerts setting) ----
    // This plays regardless of settings so we can verify hardware works.
    Serial.println("[Audio] Playing startup test tone...");
    audioTone(1000, 200);  // 1kHz for 200ms — should be clearly audible
    Serial.println("[Audio] Test tone complete");
}

void audioEnable() {
    if (!g_audioInitialized) return;
    digitalWrite(PA_EN_PIN, HIGH);
    g_audioEnabled = true;
    delay(50);  // let PA + DAC output stabilize
}

void audioDisable() {
    if (!g_audioInitialized) return;
    delay(10);  // brief tail after last sample
    digitalWrite(PA_EN_PIN, LOW);
    g_audioEnabled = false;
}

// ---- Tone generation ----

void audioTone(int freqHz, int durationMs) {
    if (!g_audioInitialized) return;

    audioEnable();

    const int totalSamples = (SAMPLE_RATE * durationMs) / 1000;
    const float omega = 2.0f * M_PI * freqHz / SAMPLE_RATE;

    // Generate in small blocks (32-bit samples for ES8311 32-bit slot width)
    const int blockSize = 128;
    int32_t buf[blockSize * 2];  // stereo: L, R interleaved, 32-bit per slot
    size_t written;
    int samplesLeft = totalSamples;

    for (int offset = 0; samplesLeft > 0; ) {
        int count = min(blockSize, samplesLeft);
        for (int i = 0; i < count; i++) {
            int16_t raw = (int16_t)(sinf(omega * (offset + i)) * TONE_AMPLITUDE);
            int32_t sample = (int32_t)raw << 16;  // 16-bit data in MSB of 32-bit slot
            buf[i * 2]     = sample;  // left
            buf[i * 2 + 1] = sample;  // right
        }
        i2s_write(I2S_PORT, buf, count * 8, &written, 200);  // 8 bytes per stereo frame
        offset += count;
        samplesLeft -= count;
    }

    // Flush DMA — push all tone data through the pipeline so it actually plays
    i2s_flush_dma();

    audioDisable();
}

// ---- Canned effects ----

void audioClick() {
    audioTone(1000, 200);   // same as working startup test tone
}

void audioBeep() {
    audioTone(1000, 200);   // same as working startup test tone
}

void audioConfirm() {
    audioTone(1800, 120);
    delay(40);
    audioTone(2400, 120);
}

void audioError() {
    audioTone(400, 300);
}

void audioAttention(int repeats) {
    // Play the 1kHz test tone in repeating bursts — proven audible
    for (int i = 0; i < repeats; i++) {
        audioTone(1000, 200);   // same tone that worked at startup
        if (i < repeats - 1) delay(300);  // gap between bursts
    }
}

void audioShutdown() {
    if (!g_audioInitialized) return;
    audioDisable();
    i2s_stop(I2S_PORT);
    i2s_driver_uninstall(I2S_PORT);
    digitalWrite(AUDIO_PWR_PIN, HIGH);  // power off audio rail (active low)
    g_audioInitialized = false;
    Serial.println("[Audio] Shutdown complete");
}
