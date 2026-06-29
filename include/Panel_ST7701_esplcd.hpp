#pragma once

#include "variants/Variant.h" // defines BLIPSCOPE_PANEL_ST7701

#if defined(BLIPSCOPE_PANEL_ST7701)

#include <cstdint>
#include <cstddef>

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp> // base lgfx::Panel_ST7701
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>

// ST7701 480x480 RGB panel driven through the official ESP-IDF esp_lcd RGB driver.
//
// Why not stock lgfx::Panel_ST7701 + Bus_RGB? LovyanGFX 1.2.21's hand-rolled RGB bus pokes the
// LCD_CAM/GDMA registers directly and does not bring up scan-out on ESP-IDF 5.5.x (Arduino core
// 3.3): the panel powers on but never displays the framebuffer (confirmed on the Waveshare
// ESP32-S3-Touch-LCD-2.1 -- even full-screen fills don't appear). The vendor BSP uses esp_lcd,
// which works. So we keep everything LovyanGFX gives us for free by subclassing Panel_ST7701
// (a Panel_FrameBufferBase): all the drawing primitives, brightness via Light_PWM, sprites, etc.
// We swap the *bus* and the *init*: init() sends THIS panel's vendor register blob over the
// bit-banged 3-wire SPI, then creates an esp_lcd RGB panel with TWO PSRAM framebuffers and a bounce
// buffer. LovyanGFX's per-line pointers are swung to the current back buffer each frame; display()
// presents it with esp_lcd_panel_draw_bitmap() (a frame-aligned swap in bounce mode) and waits on
// the frame-complete event before the next frame reuses the freed buffer. The bounce buffer keeps
// the scan drift-free under PSRAM-bus load (WiFi/TLS); the double buffer + frame-complete handoff
// keep it tear-free. See the .cpp header comment for the full rationale.
class Panel_ST7701_esplcd : public lgfx::Panel_ST7701 {
public:
  // RGB-interface wiring + timing, supplied from the variant via LGFX.h. (The 3-wire init SPI
  // pins and pin_cs=-1 still come through the stock config_detail.)
  struct esp_rgb_cfg_t {
    int8_t   pin_data[16];
    int8_t   pin_hsync, pin_vsync, pin_de, pin_pclk;
    uint32_t pclk_hz;
    uint16_t hpw, hbp, hfp; // hsync pulse / back porch / front porch
    uint16_t vpw, vbp, vfp; // vsync pulse / back porch / front porch
  };
  void setEspRgbConfig(const esp_rgb_cfg_t& c) { _ecfg = c; }

  bool init(bool use_reset) override;
  // Presents the back buffer and swings drawing to the other framebuffer (see above).
  void display(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h) override;

private:
  esp_rgb_cfg_t          _ecfg{};
  esp_lcd_panel_handle_t _handle = nullptr;
  uint8_t*               _fbs[2]    = {nullptr, nullptr}; // esp_lcd's two framebuffers
  uint8_t**              _lineArr[2] = {nullptr, nullptr}; // per-buffer row-pointer arrays
  int                    _scan = 0;   // framebuffer currently being scanned out
  int                    _draw = 1;   // framebuffer LovyanGFX is drawing into (the back buffer)
};

#endif // BLIPSCOPE_PANEL_ST7701
