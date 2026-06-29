#pragma once

// Blipscope Pro 2.1 -- ESP32-S3R8 + 2.1" round ST7701 480x480 RGB LCD + CST820 touch.
//
// First S3 SKU and the first RGB-bus panel (every prior board was SPI). Three things make
// this board unlike the C3 Kit:
//   1. The panel is a 16-bit parallel RGB display (ST7701), not SPI -- see the RGB block
//      below and the BLIPSCOPE_PANEL_ST7701 branch in LGFX.h.
//   2. The ST7701's 3-wire init CS (EXIO3), its reset (EXIO1), and the touch reset (EXIO2)
//      hang off a TCA9554 I2C IO expander, not GPIOs. variant::BoardPreInit() (in
//      src/board/board_s3_touch21.cpp) drives that expander before tft.init(): it pulses the
//      resets and holds LCD_CS low for the whole session so LovyanGFX's bit-banged ST7701
//      init reaches the panel. pin_cs is therefore -1 in LGFX.h, and TOUCH_PIN_RST is -1.
//   3. Dual-core + 8 MB PSRAM: full-frame render (no banding) and enrichment always-on.
//
// Pins/timings are from the Waveshare vendor demo (ESP32-S3-Touch-LCD-2.1-Demo, Display_ST7701).

// ---- driver selection (consumed by LGFX.h) ----
#define BLIPSCOPE_PANEL_ST7701 1
#define BLIPSCOPE_TOUCH_CST816 1   // CST820 is CST816S register-compatible

// ---- ST7701 3-wire init SPI (bit-banged by LovyanGFX; CS is on the expander, so -1) ----
#define BLIPSCOPE_DISP_PIN_SCLK 2
#define BLIPSCOPE_DISP_PIN_MOSI 1
#define BLIPSCOPE_DISP_INVERT   false  // ST7701 init leaves the panel non-inverted (INVOFF)

// ---- RGB bus (16-bit parallel) ----
// Pixel clock. The vendor runs 16 MHz, but we scan the PSRAM framebuffer without a bounce buffer
// (see Panel_ST7701_esplcd.cpp), so a lower pclk gives the LCD FIFO more slack to ride out PSRAM-bus
// latency spikes from the CPU/WiFi -- otherwise the image drifts left/right under network load.
// 8 MHz still refreshes at ~29 Hz (flicker-free on this IPS, the radar sweep stays smooth). Raise
// toward 16 MHz only if a bounce buffer is reinstated.
#define BLIPSCOPE_RGB_FREQ        8000000
#define BLIPSCOPE_RGB_PIN_D0      5
#define BLIPSCOPE_RGB_PIN_D1      45
#define BLIPSCOPE_RGB_PIN_D2      48
#define BLIPSCOPE_RGB_PIN_D3      47
#define BLIPSCOPE_RGB_PIN_D4      21
#define BLIPSCOPE_RGB_PIN_D5      14
#define BLIPSCOPE_RGB_PIN_D6      13
#define BLIPSCOPE_RGB_PIN_D7      12
#define BLIPSCOPE_RGB_PIN_D8      11
#define BLIPSCOPE_RGB_PIN_D9      10
#define BLIPSCOPE_RGB_PIN_D10     9
#define BLIPSCOPE_RGB_PIN_D11     46
#define BLIPSCOPE_RGB_PIN_D12     3
#define BLIPSCOPE_RGB_PIN_D13     8
#define BLIPSCOPE_RGB_PIN_D14     18
#define BLIPSCOPE_RGB_PIN_D15     17
#define BLIPSCOPE_RGB_PIN_HSYNC   38
#define BLIPSCOPE_RGB_PIN_VSYNC   39
#define BLIPSCOPE_RGB_PIN_DE      40
#define BLIPSCOPE_RGB_PIN_PCLK    41
#define BLIPSCOPE_RGB_HSYNC_PULSE 8
#define BLIPSCOPE_RGB_HSYNC_BACK  10
#define BLIPSCOPE_RGB_HSYNC_FRONT 50
#define BLIPSCOPE_RGB_VSYNC_PULSE 3
#define BLIPSCOPE_RGB_VSYNC_BACK  8
#define BLIPSCOPE_RGB_VSYNC_FRONT 8
#define BLIPSCOPE_RGB_PCLK_ACTIVE_NEG false  // vendor drives data on the rising PCLK edge

// ---- backlight (PWM, direct GPIO) ----
#define BLIPSCOPE_BL_PIN    6
#define BLIPSCOPE_BL_INVERT false
#define BLIPSCOPE_BL_FREQ   20000

// ---- capacitive touch (CST820 on the shared I2C bus; RST is on expander EXIO2, so -1) ----
#define BLIPSCOPE_TOUCH_I2C_PORT 0
#define BLIPSCOPE_TOUCH_PIN_SDA  15
#define BLIPSCOPE_TOUCH_PIN_SCL  7
#define BLIPSCOPE_TOUCH_PIN_INT  16
#define BLIPSCOPE_TOUCH_PIN_RST  (-1)
#define BLIPSCOPE_TOUCH_I2C_ADDR 0x15
#define BLIPSCOPE_TOUCH_FREQ     400000

namespace variant {
    // Screen geometry (round; square bounding box).
    constexpr int SCREEN_SIZE = 480;

    // Capabilities.
    constexpr bool BANDED_RENDER = false; // dual-core + PSRAM: render the whole frame at once
    constexpr bool ENRICH_ALWAYS = true;  // ample heap: never skip the adsbdb TLS enrichment
    constexpr bool HAS_AUDIO     = true;  // active buzzer on TCA9554 EXIO8
    constexpr bool HAS_IMU       = true;  // QMI8658 6-axis accel/gyro on the shared I2C bus

    // OTA + identity. SLUG names the per-SKU release asset (firmware-<SLUG>.bin / version-<SLUG>.txt).
    constexpr char SLUG[] = "s3-21";
    constexpr char NAME[] = "Blipscope Pro 2.1";

    // Board pre-init: drives the TCA9554 expander (panel/touch reset, hold LCD_CS low) and
    // brings up the IMU, before tft.init(). Implemented in src/board/board_s3_touch21.cpp.
    void BoardPreInit();
}
