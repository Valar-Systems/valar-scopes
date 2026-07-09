#pragma once

// Blipscope Kit S3 -- "jxl" ESP32-S3R8 + 1.28" round GC9A01 LCD + CST816 capacitive touch.
//
// Candidate replacement for the retired C3 Kit: same panel/touch family on a dual-core
// S3R8 with PSRAM (plus PCF85063 RTC, QMI8658 IMU, spk/mic, TF, 2x WS2812, 5 buttons --
// none of which this scaffold integrates yet; the board exists first to A/B the CST816
// wedge against the C3 under the same watchdog ledger).
//
// PIN MAP STATUS (2026-07-09 incoming inspection, probe-s3-128 sweep on the actual unit):
//   TOUCH BUS VERIFIED: SDA=8 SCL=9, sole device 0x15 (RTC/IMU pads unpopulated on this
//   unit -- base SKU per the listing). Chip-id 0xA7=0xB6 (same family id as the C3 kit),
//   fw-ver 0xA9=0x02, proj-id 0xA8=0x02. CRITICALLY: 0xFE (DisAutoSleep) reads 1 FROM THE
//   FACTORY and is read/writable -- auto-sleep is already off (the C3's 0xFE-unreachable
//   DOA was revision-specific). TP_INT / TP_RST not yet identified (probe phase 2: watch
//   candidate pins for edges during touch / targeted low-pulse tests); -1 until then, so
//   the watchdog's hard rung is inert on this board for now.
//   DISPLAY STILL UNVERIFIED: GC9A01 SPI pins below remain Waveshare-family guesses;
//   sources: supplier schematic (requested) or a GPIO-matrix dump of the stock demo.

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

// ---- capacitive touch (CST816T, VERIFIED by the pin sweep on this unit) ----
#define BLIPSCOPE_TOUCH_I2C_PORT 0
#define BLIPSCOPE_TOUCH_PIN_SDA  8
#define BLIPSCOPE_TOUCH_PIN_SCL  9
#define BLIPSCOPE_TOUCH_PIN_INT  (-1)  // not yet identified (probe phase 2); driver polls
#define BLIPSCOPE_TOUCH_PIN_RST  (-1)  // not yet identified; watchdog hard rung inert until found
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
