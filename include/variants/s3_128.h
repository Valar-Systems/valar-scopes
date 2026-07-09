#pragma once

// Blipscope Kit S3 -- "jxl" ESP32-S3R8 + 1.28" round GC9A01 LCD + CST816 capacitive touch.
//
// Candidate replacement for the retired C3 Kit: same panel/touch family on a dual-core
// S3R8 with PSRAM (plus PCF85063 RTC, QMI8658 IMU, spk/mic, TF, 2x WS2812, 5 buttons --
// none of which this scaffold integrates yet; the board exists first to A/B the CST816
// wedge against the C3 under the same watchdog ledger).
//
// !!! PIN MAP UNVERIFIED (2026-07-11) !!!
// Values below are the Waveshare ESP32-S3-Touch-LCD-1.28 family map as a starting guess;
// the jxl clone may differ. Verify against the vendor doc / the probe-s3-128 sketch
// (src/probe/TouchProbe.cpp) BEFORE flashing the firmware envs. The probe verifies the
// touch I2C half; the display half needs the vendor schematic.

// ---- driver selection (consumed by LGFX.h) ----
#define BLIPSCOPE_PANEL_GC9A01 1
#define BLIPSCOPE_TOUCH_CST816 1

// ---- display SPI bus (GC9A01) -- UNVERIFIED, Waveshare-family guess ----
#define BLIPSCOPE_DISP_SPI_HOST   SPI2_HOST
#define BLIPSCOPE_DISP_FREQ_WRITE 40000000   // S3 drives this panel at 40 MHz on the known boards
#define BLIPSCOPE_DISP_PIN_MOSI   11
#define BLIPSCOPE_DISP_PIN_SCLK   10
#define BLIPSCOPE_DISP_PIN_DC     8
#define BLIPSCOPE_DISP_PIN_CS     9
#define BLIPSCOPE_DISP_PIN_RST    14
#define BLIPSCOPE_DISP_PIN_BUSY   (-1)
#define BLIPSCOPE_DISP_INVERT     true

// ---- backlight (PWM) -- UNVERIFIED ----
#define BLIPSCOPE_BL_PIN    2
#define BLIPSCOPE_BL_INVERT false
#define BLIPSCOPE_BL_FREQ   40000

// ---- capacitive touch (CST816 family; RTC/IMU likely share this bus) -- UNVERIFIED ----
#define BLIPSCOPE_TOUCH_I2C_PORT 0
#define BLIPSCOPE_TOUCH_PIN_SDA  6
#define BLIPSCOPE_TOUCH_PIN_SCL  7
#define BLIPSCOPE_TOUCH_PIN_INT  5
#define BLIPSCOPE_TOUCH_PIN_RST  13
#define BLIPSCOPE_TOUCH_I2C_ADDR 0x15
#define BLIPSCOPE_TOUCH_FREQ     400000

namespace variant {
    // Screen geometry (round; square bounding box).
    constexpr int SCREEN_SIZE = 240;

    // Capabilities: dual-core S3R8 + 8 MB PSRAM relaxes every C3 constraint.
    constexpr bool BANDED_RENDER = false; // PSRAM: full framebuffer
    constexpr bool ENRICH_ALWAYS = true;  // heap headroom for TLS enrichment always-on
    constexpr bool HAS_AUDIO     = false; // speaker exists on-board; integration deferred
    constexpr bool HAS_IMU       = false; // QMI8658 exists on-board; integration deferred
    constexpr bool SERIALIZE_TOUCH_BUS = false; // dual-core: touch and TLS never share a core,
                                                // so the C3's overlap wedge shouldn't exist --
                                                // running WITHOUT the mutex gate is itself half
                                                // of the A/B (chip family vs platform concurrency)
    constexpr bool TOUCH_WATCHDOG = true;       // unlike the other S3 SKUs: this board exists to
                                                // A/B the CST816 wedge against the C3, so the
                                                // supervisor + its ledger run here with the same
                                                // probe cadence (side-by-side [health]/[soak] lines)

    // OTA + identity. SLUG names the per-SKU release asset (firmware-<SLUG>.bin / version-<SLUG>.txt).
    constexpr char SLUG[] = "s3-128";
    constexpr char NAME[] = "Blipscope Kit S3";

    // Panel/touch reset are plain GPIOs (no IO expander seen so far): no pre-init dance.
    inline void BoardPreInit() {}
}
