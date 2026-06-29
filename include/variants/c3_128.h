#pragma once

// Blipscope Kit -- ESP32-C3 + 1.28" round GC9A01 LCD + CST816 capacitive touch.
//
// The original board (Waveshare ESP32-C3-Touch-LCD-1.28 class). Single-core RISC-V and
// heap-tight, so this variant renders in horizontal bands and leaves enrichment heap-gated.
// All values below mirror the pre-refactor hardcoded config exactly -- this is the baseline.

// ---- driver selection (consumed by LGFX.h) ----
#define BLIPSCOPE_PANEL_GC9A01 1
#define BLIPSCOPE_TOUCH_CST816 1

// ---- display SPI bus (GC9A01) ----
#define BLIPSCOPE_DISP_SPI_HOST   SPI2_HOST
#define BLIPSCOPE_DISP_FREQ_WRITE 27000000
#define BLIPSCOPE_DISP_PIN_MOSI   7
#define BLIPSCOPE_DISP_PIN_SCLK   6
#define BLIPSCOPE_DISP_PIN_DC     2
#define BLIPSCOPE_DISP_PIN_CS     10
#define BLIPSCOPE_DISP_PIN_RST    (-1)
#define BLIPSCOPE_DISP_PIN_BUSY   (-1)
#define BLIPSCOPE_DISP_INVERT     true   // tft.invertDisplay(true) at boot

// ---- backlight (PWM) ----
// 40 kHz keeps the PWM above the audible band so the backlight rail doesn't whine when
// dimmed; LEDC 9-bit on the C3's 80 MHz clock supports it. See the LGFX.h note.
#define BLIPSCOPE_BL_PIN    3
#define BLIPSCOPE_BL_INVERT false
#define BLIPSCOPE_BL_FREQ   40000

// ---- capacitive touch (CST816, its own I2C bus) ----
#define BLIPSCOPE_TOUCH_I2C_PORT 0
#define BLIPSCOPE_TOUCH_PIN_SDA  4
#define BLIPSCOPE_TOUCH_PIN_SCL  5
#define BLIPSCOPE_TOUCH_PIN_INT  0
#define BLIPSCOPE_TOUCH_PIN_RST  1
#define BLIPSCOPE_TOUCH_I2C_ADDR 0x15
#define BLIPSCOPE_TOUCH_FREQ     400000

namespace variant {
    // Screen geometry (round; square bounding box).
    constexpr int SCREEN_SIZE = 240;

    // Capabilities.
    constexpr bool BANDED_RENDER = true;  // single-core/heap-tight: render in 2 half-height bands
    constexpr bool ENRICH_ALWAYS = false; // heap-gated; enrich only when free heap allows a TLS handshake
    constexpr bool HAS_AUDIO     = false; // no speaker
    constexpr bool HAS_IMU       = false; // no accelerometer/gyro

    // OTA + identity. SLUG names the per-SKU release asset (firmware-<SLUG>.bin / version-<SLUG>.txt).
    constexpr char SLUG[] = "c3-128";
    constexpr char NAME[] = "Blipscope Kit";

    // No IO-expander / pre-init dance on this board: the panel and touch reset off real GPIOs,
    // so BoardPreInit() (called unconditionally from setup()) is a no-op here. See s3_21.h for a
    // board that uses it.
    inline void BoardPreInit() {}
}
