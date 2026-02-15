// Custom GxEPD2 panel driver for Waveshare ESP32-S3-ePaper-1.54 V2
// Based on GxEPD2_154_GDEY0154D67 by Jean-Marc Zingg, with Waveshare-correct
// init sequence, custom waveform LUT, driving voltages, and update command.
//
// Key differences from stock GxEPD2 driver:
//   - _InitDisplay(): full hardware reset + BUSY wait, GD=1, data entry 0x01,
//     border 0x01, temp sensor load, then custom 159-byte LUT with voltages
//   - _Update_Full(): 0xC7 (use loaded LUT) instead of 0xF7 (built-in)
//   - _setPartialRamArea(): data entry mode 0x01 (X inc, Y dec) + matching window/cursor

#include "WS_EPD154V2.h"

// Custom waveform LUT from Waveshare factory firmware
// Bytes 0-152: waveform phases  |  153: gate voltage  |  154: source voltage
// 155-157: VCOM  |  158: VCOM register
static const uint8_t WF_Full_1IN54[159] PROGMEM = {
    0x80, 0x48, 0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x40, 0x48, 0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x80, 0x48, 0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x40, 0x48, 0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0xA,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x8,  0x1,  0x0,  0x8,  0x1,  0x0,  0x2,
    0xA,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0,  0x0,  0x0,
    0x22, 0x17, 0x41, 0x0,  0x32, 0x20
};

// Partial refresh waveform LUT from Waveshare factory firmware
static const uint8_t WF_Partial_1IN54[159] PROGMEM = {
    0x0,  0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x80, 0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x40, 0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0xF,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x1,  0x1,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0,  0x0,  0x0,
    0x02, 0x17, 0x41, 0xB0, 0x32, 0x28
};

WS_EPD154V2::WS_EPD154V2(int16_t cs, int16_t dc, int16_t rst, int16_t busy) :
  GxEPD2_EPD(cs, dc, rst, busy, HIGH, 10000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate, hasFastPartialUpdate)
{
}

// ============================================================================
// Screen buffer operations
// ============================================================================

void WS_EPD154V2::clearScreen(uint8_t value)
{
  _writeScreenBuffer(0x26, value); // set previous
  _writeScreenBuffer(0x24, value); // set current
  refresh(false); // full refresh
  _initial_write = false;
}

void WS_EPD154V2::writeScreenBuffer(uint8_t value)
{
  if (_initial_write) return clearScreen(value);
  _writeScreenBuffer(0x24, value);
}

void WS_EPD154V2::writeScreenBufferAgain(uint8_t value)
{
  _writeScreenBuffer(0x24, value);
  _writeScreenBuffer(0x26, value);
}

void WS_EPD154V2::_writeScreenBuffer(uint8_t command, uint8_t value)
{
  if (!_init_display_done) _InitDisplay();
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(command);
  _startTransfer();
  for (uint32_t i = 0; i < uint32_t(WIDTH) * uint32_t(HEIGHT) / 8; i++)
  {
    _transfer(value);
  }
  _endTransfer();
}

// ============================================================================
// Image write operations
// ============================================================================

void WS_EPD154V2::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x24, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void WS_EPD154V2::writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x26, bitmap, x, y, w, h, invert, mirror_y, pgm);
  _writeImage(0x24, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void WS_EPD154V2::writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x26, bitmap, x, y, w, h, invert, mirror_y, pgm);
  _writeImage(0x24, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void WS_EPD154V2::_writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  delay(1);
  int16_t wb = (w + 7) / 8;
  x -= x % 8;
  w = wb * 8;
  int16_t x1 = x < 0 ? 0 : x;
  int16_t y1 = y < 0 ? 0 : y;
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x;
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  if (!_init_display_done) _InitDisplay();
  if (_initial_write) writeScreenBuffer();
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t data;
      int16_t idx = mirror_y ? j + dx / 8 + ((h - 1 - (i + dy))) * wb : j + dx / 8 + (i + dy) * wb;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      _transfer(data);
    }
  }
  _endTransfer();
  delay(1);
}

void WS_EPD154V2::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                  int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImagePart(0x24, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void WS_EPD154V2::writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImagePart(0x26, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  _writeImagePart(0x24, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void WS_EPD154V2::_writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                   int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  delay(1);
  if ((w_bitmap < 0) || (h_bitmap < 0) || (w < 0) || (h < 0)) return;
  if ((x_part < 0) || (x_part >= w_bitmap)) return;
  if ((y_part < 0) || (y_part >= h_bitmap)) return;
  int16_t wb_bitmap = (w_bitmap + 7) / 8;
  x_part -= x_part % 8;
  w = w_bitmap - x_part < w ? w_bitmap - x_part : w;
  h = h_bitmap - y_part < h ? h_bitmap - y_part : h;
  x -= x % 8;
  w = 8 * ((w + 7) / 8);
  int16_t x1 = x < 0 ? 0 : x;
  int16_t y1 = y < 0 ? 0 : y;
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x;
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  if (!_init_display_done) _InitDisplay();
  if (_initial_write) writeScreenBuffer();
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t data;
      int16_t idx = mirror_y ? x_part / 8 + j + dx / 8 + ((h_bitmap - 1 - (y_part + i + dy))) * wb_bitmap : x_part / 8 + j + dx / 8 + (y_part + i + dy) * wb_bitmap;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      _transfer(data);
    }
  }
  _endTransfer();
  delay(1);
}

void WS_EPD154V2::writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) writeImage(black, x, y, w, h, invert, mirror_y, pgm);
}

void WS_EPD154V2::writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                  int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) writeImagePart(black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void WS_EPD154V2::writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (data1) writeImage(data1, x, y, w, h, invert, mirror_y, pgm);
}

// ============================================================================
// Draw operations (write + refresh)
// ============================================================================

void WS_EPD154V2::drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
  writeImageAgain(bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void WS_EPD154V2::drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                 int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
  writeImagePartAgain(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void WS_EPD154V2::drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) drawImage(black, x, y, w, h, invert, mirror_y, pgm);
}

void WS_EPD154V2::drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                 int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black) drawImagePart(black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void WS_EPD154V2::drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (data1) drawImage(data1, x, y, w, h, invert, mirror_y, pgm);
}

// ============================================================================
// Refresh
// ============================================================================

void WS_EPD154V2::refresh(bool partial_update_mode)
{
  if (partial_update_mode) refresh(0, 0, WIDTH, HEIGHT);
  else
  {
    _Update_Full();
    _initial_refresh = false;
  }
}

void WS_EPD154V2::refresh(int16_t x, int16_t y, int16_t w, int16_t h)
{
  if (_initial_refresh) return refresh(false);
  int16_t w1 = x < 0 ? w + x : w;
  int16_t h1 = y < 0 ? h + y : h;
  int16_t x1 = x < 0 ? 0 : x;
  int16_t y1 = y < 0 ? 0 : y;
  w1 = x1 + w1 < int16_t(WIDTH) ? w1 : int16_t(WIDTH) - x1;
  h1 = y1 + h1 < int16_t(HEIGHT) ? h1 : int16_t(HEIGHT) - y1;
  if ((w1 <= 0) || (h1 <= 0)) return;
  w1 += x1 % 8;
  if (w1 % 8 > 0) w1 += 8 - w1 % 8;
  x1 -= x1 % 8;
  _setPartialRamArea(x1, y1, w1, h1);
  _Update_Part();
}

// ============================================================================
// Power management
// ============================================================================

void WS_EPD154V2::powerOff()
{
  _PowerOff();
}

void WS_EPD154V2::hibernate()
{
  _PowerOff();
  if (_rst >= 0)
  {
    _writeCommand(0x10); // deep sleep mode
    _writeData(0x1);
    _hibernating = true;
    _init_display_done = false;
  }
}

// ============================================================================
// Private: RAM area setup — Waveshare-compatible (data entry mode 0x01)
// ============================================================================

void WS_EPD154V2::_setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  // Data entry mode: X increment, Y decrement (matches Waveshare factory)
  _writeCommand(0x11);
  _writeData(0x03);    // x increase, y increase for GxEPD2 coordinate system
  _writeCommand(0x44);
  _writeData(x / 8);
  _writeData((x + w - 1) / 8);
  _writeCommand(0x45);
  _writeData(y % 256);
  _writeData(y / 256);
  _writeData((y + h - 1) % 256);
  _writeData((y + h - 1) / 256);
  _writeCommand(0x4e);
  _writeData(x / 8);
  _writeCommand(0x4f);
  _writeData(y % 256);
  _writeData(y / 256);
}

// ============================================================================
// Private: Power control
// ============================================================================

void WS_EPD154V2::_PowerOn()
{
  if (!_power_is_on)
  {
    _writeCommand(0x22);
    _writeData(0xe0);
    _writeCommand(0x20);
    _waitWhileBusy("_PowerOn", power_on_time);
  }
  _power_is_on = true;
}

void WS_EPD154V2::_PowerOff()
{
  if (_power_is_on)
  {
    _writeCommand(0x22);
    _writeData(0x83);
    _writeCommand(0x20);
    _waitWhileBusy("_PowerOff", power_off_time);
  }
  _power_is_on = false;
  _using_partial_mode = false;
}

// ============================================================================
// Private: Display init — WAVESHARE-CORRECT SEQUENCE
// ============================================================================

void WS_EPD154V2::_LoadLUT()
{
  // Write 153 bytes of waveform LUT
  _writeCommand(0x32);
  for (int i = 0; i < 153; i++)
  {
    _writeData(pgm_read_byte(&WF_Full_1IN54[i]));
  }
  _waitWhileBusy("_LoadLUT", 100);

  // Gate driving voltage
  _writeCommand(0x3F);
  _writeData(pgm_read_byte(&WF_Full_1IN54[153])); // 0x22

  // Source driving voltage
  _writeCommand(0x03);
  _writeData(pgm_read_byte(&WF_Full_1IN54[154])); // 0x17

  // Write VCOM register
  _writeCommand(0x04);
  _writeData(pgm_read_byte(&WF_Full_1IN54[155])); // 0x41
  _writeData(pgm_read_byte(&WF_Full_1IN54[156])); // 0x00
  _writeData(pgm_read_byte(&WF_Full_1IN54[157])); // 0x32

  // VCOM voltage
  _writeCommand(0x2C);
  _writeData(pgm_read_byte(&WF_Full_1IN54[158])); // 0x20
}

void WS_EPD154V2::_InitDisplay()
{
  // Full hardware reset (matching Waveshare factory timing)
  if (_rst >= 0)
  {
    digitalWrite(_rst, HIGH);
    delay(50);
    digitalWrite(_rst, LOW);
    delay(20);
    digitalWrite(_rst, HIGH);
    delay(50);
    _waitWhileBusy("_reset", 100);
  }

  // Software reset
  _writeCommand(0x12); // SWRESET
  _waitWhileBusy("_swreset", 100);

  // Driver output control — GD=0 (GxEPD2/Adafruit GFX assumes G0→G199)
  _writeCommand(0x01);
  _writeData(0xC7); // MUX = 199
  _writeData(0x00);
  _writeData(0x00); // GD=0: gate scan G0→G199 (matches GxEPD2 coordinate system)

  // Border waveform — Waveshare uses 0x01 (stock GxEPD2 had 0x05)
  _writeCommand(0x3C);
  _writeData(0x01);

  // Temperature sensor: internal
  _writeCommand(0x18);
  _writeData(0x80);

  // Load temperature + built-in waveform first
  _writeCommand(0x22);
  _writeData(0xB1);
  _writeCommand(0x20);

  // Set up RAM area (needed before cursor set)
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _waitWhileBusy("_tempLUT", 200);

  // Load custom waveform LUT + driving voltages — THE KEY FIX
  _LoadLUT();

  _init_display_done = true;
}

// ============================================================================
// Private: Display update — WAVESHARE-CORRECT COMMANDS
// ============================================================================

void WS_EPD154V2::_Update_Full()
{
  // 0xC7 = use externally loaded LUT (not built-in 0xF7)
  _writeCommand(0x22);
  _writeData(0xC7);
  _writeCommand(0x20);
  _waitWhileBusy("_Update_Full", full_refresh_time);
  _power_is_on = false;
}

void WS_EPD154V2::_Update_Part()
{
  // 0xCF = partial update using loaded LUT (Waveshare factory uses this)
  _writeCommand(0x22);
  _writeData(0xCF);
  _writeCommand(0x20);
  _waitWhileBusy("_Update_Part", partial_refresh_time);
  _power_is_on = true;
}
