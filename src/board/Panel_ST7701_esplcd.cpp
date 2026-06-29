// Implementation of Panel_ST7701_esplcd: ST7701 register init over bit-banged 3-wire SPI, then
// the RGB bus brought up via esp_lcd (LovyanGFX's own Bus_RGB is broken on IDF 5.5 -- see the
// header). Compiled only for the S3-2.1 variant; empty otherwise.
//
// Scan-out: double framebuffer + a bounce buffer. This is the combination that is BOTH drift-free
// and tear-free while WiFi/TLS hammer the PSRAM bus, and it relies on two details of the IDF RGB
// driver:
//   * The bounce buffer (fast internal SRAM) is filled by a CPU memcpy that reads the framebuffer
//     THROUGH THE CACHE (esp_lcd_panel_rgb.c). So it sees freshly-drawn pixels with no manual cache
//     writeback, and -- crucially -- it isolates the LCD scan from PSRAM-bus stalls (the CPU drawing
//     or WiFi), which is what makes the image drift/jitter when the panel is scanned straight from
//     PSRAM.
//   * In bounce mode the driver only switches which framebuffer it reads at the FRAME WRAP (it sets
//     bb_fb_index = cur_fb_index there). So calling draw_bitmap() to change the buffer is naturally
//     frame-aligned -> no mid-frame shear.
// The one subtlety (which bit us once): after presenting buffer B, the bounce keeps reading the old
// buffer A until the next wrap, so A is NOT free to draw into yet. We gate buffer reuse on the
// on_frame_buf_complete callback, which fires exactly when the wrap moves the bounce onto B and frees
// A. Drawing always lands in the truly-idle buffer -> no tearing.

#if defined(BLIPSCOPE_VARIANT_S3_21)

#include "Panel_ST7701_esplcd.hpp"

#include <Arduino.h> // delay() (yields to the RTOS; the init has a 480 ms sleep-out wait)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Bounce buffer height in scan lines (per buffer; there are two). 480 must be an integer multiple of
// this. 20 lines -> generous slack to ride out PSRAM-bus stalls from the CPU/WiFi.
static constexpr int BOUNCE_LINES = 20;

// Given from the panel's "frame buffer complete" event: fires at the frame wrap, i.e. exactly when
// the bounce switches to the just-presented framebuffer and frees the other one. display() waits on
// it before letting the next frame draw into that freed buffer. The callback must live in IRAM.
static SemaphoreHandle_t s_frameSem = nullptr;

static bool IRAM_ATTR onFrameComplete(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*)
{
  BaseType_t hpw = pdFALSE;
  if (s_frameSem) xSemaphoreGiveFromISR(s_frameSem, &hpw);
  return hpw == pdTRUE;
}

// ST7701 register init, transcribed verbatim from the vendor BSP. Format: <cmd>, <param count>,
// <params...>, repeated, terminated by the 0xFE sentinel (0xFE is not a real ST7701 command). The
// sleep-out/display-on tail with its long delays is issued imperatively after this table.
static const uint8_t ST7701_INIT[] = {
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10,           // command2 BK0 select
  0xC0, 2, 0x3B, 0x00,                             // scan line
  0xC1, 2, 0x0B, 0x02,                             // VBP
  0xC2, 2, 0x07, 0x02,
  0xCC, 1, 0x10,
  0xCD, 1, 0x08,                                   // RGB format
  0xB0, 16, 0x00, 0x11, 0x16, 0x0E, 0x11, 0x06, 0x05, 0x09,   // positive gamma
            0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18,
  0xB1, 16, 0x00, 0x11, 0x16, 0x0E, 0x11, 0x07, 0x05, 0x09,   // negative gamma
            0x09, 0x21, 0x05, 0x13, 0x11, 0x2A, 0x31, 0x18,
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x11,           // command2 BK1 select
  0xB0, 1, 0x6D,                                   // VOP
  0xB1, 1, 0x37,                                   // VCOM
  0xB2, 1, 0x81,                                   // VGH
  0xB3, 1, 0x80,
  0xB5, 1, 0x43,                                   // VGL
  0xB7, 1, 0x85,
  0xB8, 1, 0x20,
  0xC1, 1, 0x78,
  0xC2, 1, 0x78,
  0xD0, 1, 0x88,
  0xE0, 3, 0x00, 0x00, 0x02,
  0xE1, 11, 0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20,
  0xE2, 13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xE3, 4, 0x00, 0x00, 0x11, 0x00,
  0xE4, 2, 0x22, 0x00,
  0xE5, 16, 0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xE6, 4, 0x00, 0x00, 0x11, 0x00,
  0xE7, 2, 0x22, 0x00,
  0xE8, 16, 0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xEB, 7, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00,
  0xED, 16, 0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF,
            0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF,
  0xEF, 6, 0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F,
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,           // command2 BK3 select
  0xEF, 1, 0x08,
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,           // back to user command set
  0x36, 1, 0x00,                                   // MADCTL
  0x3A, 1, 0x66,                                   // COLMOD (RGB666 over the 16-bit bus)
  0xFE,                                            // terminator
};

bool Panel_ST7701_esplcd::init(bool use_reset)
{
  // 1) ST7701 register init over the bit-banged 3-wire SPI. CS hangs off the TCA9554 and is held
  //    low for the whole session by variant::BoardPreInit(), so config_detail.pin_cs == -1 here.
  //    writeCommand/writeData are LovyanGFX's 9-bit soft-SPI (D/C as the 9th bit) on these pins --
  //    electrically identical to the vendor's hardware-SPI init (command_bits=1, address_bits=8).
  const int pin_mosi = _config_detail.pin_mosi;
  const int pin_sclk = _config_detail.pin_sclk;
  if (pin_mosi >= 0 && pin_sclk >= 0) {
    lgfx::gpio_lo(pin_mosi); lgfx::pinMode(pin_mosi, lgfx::pin_mode_t::output);
    lgfx::gpio_lo(pin_sclk); lgfx::pinMode(pin_sclk, lgfx::pin_mode_t::output);

    for (const uint8_t* p = ST7701_INIT; *p != 0xFE; ) {
      const uint8_t cmd = *p++;
      uint8_t n = *p++;
      writeCommand(cmd, 1);
      while (n--) writeData(*p++, 1);
    }
    // Sleep-out, inversion-off, display-on -- with the vendor's long settling delays.
    writeCommand(0x11, 1); delay(480); // sleep out
    writeCommand(0x20, 1); delay(120); // display inversion off
    writeCommand(0x29, 1);             // display on
  }

  // 2) RGB bus via esp_lcd: two PSRAM framebuffers + a bounce buffer (see the file header).
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz           = _ecfg.pclk_hz;
  cfg.timings.h_res             = _cfg.panel_width;
  cfg.timings.v_res             = _cfg.panel_height;
  cfg.timings.hsync_pulse_width = _ecfg.hpw;
  cfg.timings.hsync_back_porch  = _ecfg.hbp;
  cfg.timings.hsync_front_porch = _ecfg.hfp;
  cfg.timings.vsync_pulse_width = _ecfg.vpw;
  cfg.timings.vsync_back_porch  = _ecfg.vbp;
  cfg.timings.vsync_front_porch = _ecfg.vfp;
  cfg.timings.flags.hsync_idle_low  = 0;
  cfg.timings.flags.vsync_idle_low  = 0;
  cfg.timings.flags.de_idle_high    = 0;
  cfg.timings.flags.pclk_active_neg = 0;
  cfg.timings.flags.pclk_idle_high  = 0;
  cfg.data_width   = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs      = 2;                                  // double buffer (frame-aligned swap)
  cfg.bounce_buffer_size_px = BOUNCE_LINES * _cfg.panel_width; // isolates the scan from PSRAM stalls
  cfg.psram_trans_align = 64;
  cfg.hsync_gpio_num = _ecfg.pin_hsync;
  cfg.vsync_gpio_num = _ecfg.pin_vsync;
  cfg.de_gpio_num    = _ecfg.pin_de;
  cfg.pclk_gpio_num  = _ecfg.pin_pclk;
  cfg.disp_gpio_num  = -1;
  for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = _ecfg.pin_data[i];
  cfg.flags.fb_in_psram = 1;
  cfg.flags.double_fb   = 1;

  if (esp_lcd_new_rgb_panel(&cfg, &_handle) != ESP_OK) return false;
  esp_lcd_panel_reset(_handle);
  esp_lcd_panel_init(_handle);

  // Frame-complete event -> semaphore, so display() can tell when a presented buffer is actually
  // being scanned (and the other one is therefore free to draw into).
  s_frameSem = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_frame_buf_complete = onFrameComplete;
  esp_lcd_rgb_panel_register_event_callbacks(_handle, &cbs, nullptr);

  // Grab both framebuffers. esp_lcd starts by scanning fbs[0]; we draw the next frame into fbs[1].
  if (esp_lcd_rgb_panel_get_frame_buffer(_handle, 2, (void**)&_fbs[0], (void**)&_fbs[1]) != ESP_OK
      || _fbs[0] == nullptr || _fbs[1] == nullptr)
    return false;

  if (!Panel_FrameBufferBase::init(use_reset)) return false;

  // LovyanGFX defaults rgb565 to byte-swapped storage (it targets SPI, which is MSB-first); esp_lcd's
  // RGB DMA scans the framebuffer in natural byte order. Left swapped, every pixel's two bytes are
  // reversed and colours come out wrong -- green renders as pink (0x07E0 -> 0xE007). Forcing
  // non-swapped storage fixes it. This only flips the byte-order attribute (the high byte of the
  // color_depth_t union); _write_bits stays 16, so the stride maths below is unaffected.
  _write_depth = lgfx::color_depth_t::rgb565_nonswapped;
  _read_depth  = lgfx::color_depth_t::rgb565_nonswapped;

  // One row-pointer array per framebuffer; _lines_buffer is swung between them each frame so all
  // LovyanGFX drawing lands in the current back buffer.
  const int h = _cfg.panel_height;
  const int w = (_cfg.panel_width + 3) & ~3;        // row stride, rounded up to 4px (480 already is)
  const uint8_t bytes = _write_bits >> 3;           // rgb565 -> 2
  for (int b = 0; b < 2; ++b) {
    _lineArr[b] = (uint8_t**)lgfx::heap_alloc_dma(h * sizeof(uint8_t*));
    if (_lineArr[b] == nullptr) return false;
    uint8_t* p = _fbs[b];
    for (int i = 0; i < h; ++i) { _lineArr[b][i] = p; p += w * bytes; }
  }
  _scan = 0;
  _draw = 1;
  _lines_buffer = _lineArr[_draw];
  return true;
}

void Panel_ST7701_esplcd::display(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t)
{
  // LovyanGFX calls this (via endWrite / tft.display()) with no rect -- the dirty region lives in
  // _range_mod, populated by the draw ops. An empty range means nothing was drawn since the last
  // present, so we skip (the loop's redundant second flush is a no-op).
  if (_range_mod.empty()) return;

  // Present the back buffer. In bounce mode this just sets the framebuffer the bounce-fill will pick
  // up at the next frame wrap (no cache writeback needed -- the fill reads through the cache). The
  // swap is therefore frame-aligned: tear-free.
  esp_lcd_panel_draw_bitmap(_handle, 0, 0, _cfg.panel_width, _cfg.panel_height, _fbs[_draw]);

  _range_mod.top = INT16_MAX;
  _range_mod.left = INT16_MAX;
  _range_mod.right = 0;
  _range_mod.bottom = 0;

  // Ping-pong, then wait for the swap to actually take effect before returning. on_frame_buf_complete
  // fires at the wrap that moves the bounce onto the buffer we just presented, which frees the other
  // one -- the one we're about to draw into next. Without this wait we'd race the scan and tear.
  _scan = _draw;
  _draw ^= 1;
  _lines_buffer = _lineArr[_draw];
  if (s_frameSem != nullptr) {
    xSemaphoreTake(s_frameSem, 0);                  // drop any stale token...
    xSemaphoreTake(s_frameSem, pdMS_TO_TICKS(50));  // ...then block until the freed buffer is idle
  }
}

#endif // BLIPSCOPE_VARIANT_S3_21
