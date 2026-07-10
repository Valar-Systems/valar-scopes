#pragma once

// Blipscope Kit S3 -- "jxl" ESP32-S3R8 + 1.28" round GC9A01 LCD + CST816 capacitive touch.
//
// Candidate replacement for the retired C3 Kit: same panel/touch family on a dual-core
// S3R8 with PSRAM (plus PCF85063 RTC, QMI8658 IMU, spk/mic, TF, 2x WS2812, 5 buttons --
// none of which this scaffold integrates yet; the board exists first to A/B the CST816
// wedge against the C3 under the same watchdog ledger).
//
// PIN MAP STATUS: 100% VERIFIED -- hardware-derived (JTAG dump of the stock demo, probe
// sweep + phase-2 hunt/pulse tests) AND cross-confirmed against the vendor doc pack
// ("ESP32S3-NxxRxx-128SPIT_开发板 V1.0": SimpleSchematic.pdf + FullFunctionTest demo
// source), which matches on every pin. Vendor facts not yet consumed by this variant
// (recorded for future integration): side buttons SW_UP=14 / SW_PW=15 / SW_Down=16
// (power latch also senses on 15), USB VBUS detect P_ST=17, battery ADC on GPIO1
// (divider x2, 4095 @ 3.3 V), PCF85063 RTC INT=45 (RTC unpopulated on base SKU),
// TF card MISO=48 SCK=41 MOSI=47 CS=40, speaker MAX98357 / mic ICS-43434 are I2S.
// RF: chip antenna vs u.FL is a SOLDER_TERMINATION/FEED_TERMINATION link by the
// matching network (schematic ANT1/RF1) -- factory-selectable; we require the u.FL
// configuration for production units (chip antenna failed association at -64 dBm).
//
// (2026-07-09 incoming inspection, probe-s3-128 sweep on the actual unit):
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

// ---- display SPI bus (GC9A01) -- pins read from the LIVE stock demo via JTAG ----
// SCLK/MOSI from the GPIO matrix (SPI3_CLK/SPI3_D routed there), RST by targeted
// low-pulse kill test, BL by matrix re-route blink test (LEDC-driven in the demo).
// DC/CS: the demo's plain-GPIO output set left {2,18} for these two roles (GPIO0 is
// the strap pin -> touch reset, GPIO40 clusters with the TF pins -> TF_CS); DC=18/CS=2
// CONFIRMED 2026-07-09 by firmware trial (splash + setup portal render correctly).
#define BLIPSCOPE_DISP_SPI_HOST   SPI3_HOST   // the demo used SPI3 for the panel (FSPI is the TF card)
#define BLIPSCOPE_DISP_FREQ_WRITE 40000000
#define BLIPSCOPE_DISP_PIN_MOSI   10
#define BLIPSCOPE_DISP_PIN_SCLK   3
#define BLIPSCOPE_DISP_PIN_DC     18
#define BLIPSCOPE_DISP_PIN_CS     2
#define BLIPSCOPE_DISP_PIN_RST    21
#define BLIPSCOPE_DISP_PIN_BUSY   (-1)
#define BLIPSCOPE_DISP_INVERT     true

// ---- backlight (PWM, verified by the GPIO42 matrix-blink test) ----
#define BLIPSCOPE_BL_PIN    42
#define BLIPSCOPE_BL_INVERT false
#define BLIPSCOPE_BL_FREQ   40000

// ---- capacitive touch (CST816T, VERIFIED by the pin sweep on this unit) ----
#define BLIPSCOPE_TOUCH_I2C_PORT 0
#define BLIPSCOPE_TOUCH_PIN_SDA  8
#define BLIPSCOPE_TOUCH_PIN_SCL  9
#define BLIPSCOPE_TOUCH_PIN_INT  11   // VERIFIED 2026-07-09 (phase-2 hunt: 1700+ falling edges
                                      // under a finger drag, all 14 other candidates flat).
                                      // INT gating is REQUIRED on this chip revision, not a
                                      // nicety: polled between report pulses, the touch regs
                                      // intermittently read 0 mid-touch -- the phantom-release
                                      // double-tap bug seen on the blind-polling build.
#define BLIPSCOPE_TOUCH_PIN_RST  0     // VERIFIED 2026-07-09 (probe phase 2: three low-pulse
                                       // runs, chip NACKed while low + clean 450 ms recovery
                                       // every time) -- the watchdog's hard rung is armed.
                                       // Strap pin: safe at runtime; never hold low across an
                                       // ESP reset (that's download mode).
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
