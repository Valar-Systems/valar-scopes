#pragma once

// Blipscope Pro 1.75 -- Waveshare ESP32-S3-Touch-AMOLED-1.75: ESP32-S3R8 + 1.75" round 466x466
// AMOLED driven by a CO5300 over QSPI, with FT3168 capacitive touch over I2C, a QMI8658 IMU, an
// RTC, and a MEMS microphone (the "+ mic" of the premium tier). Dual-core + PSRAM.
//
// Unlike the 1.46" (SPD2010, a TDDI chip with no LovyanGFX driver), BOTH controllers here are
// built into LovyanGFX: the CO5300 AMOLED panel (lgfx::Panel_CO5300, the same QSPI in-band command
// framing as Panel_AMOLED) and the FT3168 touch (FT5x06-compatible -> lgfx::Touch_FT5x06). So this
// SKU needs NO custom driver -- only this variant header, an LGFX.h block, and an env.
//
// =====================================================================================
//  STATUS: BENCH-ONLY SCAFFOLD. The pin map below is NOT YET VERIFIED against the
//  Waveshare ESP32-S3-Touch-AMOLED-1.75 wiki pin table -- it is seeded from the AMOLED
//  family and WILL need confirmation on the physical board before flashing. Do not add a
//  CI row or ship this SKU until the pins are checked and the board is brought up
//  (a WRONG pin is a bug, per the Airports.h/first-article discipline).
// =====================================================================================

// ---- driver selection (consumed by LGFX.h) ----
#define BLIPSCOPE_PANEL_CO5300  1
#define BLIPSCOPE_TOUCH_FT5X06  1

// ---- display QSPI bus (CO5300; 4 data lines, in-band command framing so no DC) ----
// PINS UNVERIFIED -- confirm against the ESP32-S3-Touch-AMOLED-1.75 wiki.
#define BLIPSCOPE_DISP_SPI_HOST   SPI3_HOST
#define BLIPSCOPE_DISP_FREQ_WRITE 40000000   // conservative QSPI clock; raise after bring-up if stable
#define BLIPSCOPE_DISP_PIN_SCLK   10
#define BLIPSCOPE_DISP_PIN_CS     9
#define BLIPSCOPE_DISP_PIN_IO0    11         // LCD_SDA0
#define BLIPSCOPE_DISP_PIN_IO1    12         // LCD_SDA1
#define BLIPSCOPE_DISP_PIN_IO2    13         // LCD_SDA2
#define BLIPSCOPE_DISP_PIN_IO3    14         // LCD_SDA3
#define BLIPSCOPE_DISP_PIN_RST    21         // LCD_RST (plain GPIO on this board; pulsed by LovyanGFX)
#define BLIPSCOPE_DISP_INVERT     false

// ---- backlight ----
// AMOLED has no separate backlight rail (each pixel self-emits); brightness is a panel command.
// LovyanGFX's Panel_CO5300 maps setBrightness() to the panel's own dimming, so the shared
// Light_PWM is driven on a spare/no-op pin -- brightness still works via the panel path.
#define BLIPSCOPE_BL_PIN    (-1)
#define BLIPSCOPE_BL_INVERT false
#define BLIPSCOPE_BL_FREQ   20000

// ---- capacitive touch (FT3168 on the shared I2C bus) ----
// PINS UNVERIFIED -- confirm against the wiki.
#define BLIPSCOPE_TOUCH_I2C_PORT 0
#define BLIPSCOPE_TOUCH_PIN_SDA  15
#define BLIPSCOPE_TOUCH_PIN_SCL  14
#define BLIPSCOPE_TOUCH_PIN_INT  8
#define BLIPSCOPE_TOUCH_PIN_RST  (-1)
#define BLIPSCOPE_TOUCH_I2C_ADDR 0x38        // FT3168 / FT5x06 family default
#define BLIPSCOPE_TOUCH_FREQ     400000

namespace variant {
    // Screen geometry (round; square bounding box).
    constexpr int SCREEN_SIZE = 466;

    // Capabilities.
    constexpr bool BANDED_RENDER = false; // dual-core + PSRAM: render the whole frame at once
    constexpr bool ENRICH_ALWAYS = true;  // ample heap: never skip the adsbdb TLS enrichment
    constexpr bool HAS_AUDIO     = true;  // I2S path (chirp = short I2S tone), plus the premium mic
    constexpr bool HAS_IMU       = true;  // QMI8658 6-axis on the shared I2C bus
    constexpr bool SERIALIZE_TOUCH_BUS = false; // dual-core: touch and network on separate cores
    constexpr bool TOUCH_WATCHDOG = false;      // FT3168 has shown no bus wedge (CST816 supervisor off)

    // OTA + identity. SLUG names the per-SKU release asset (firmware-<SLUG>.bin / version-<SLUG>.txt).
    constexpr char SLUG[] = "s3-175-amoled";
    constexpr char NAME[] = "Blipscope Pro 1.75";

    // Panel/touch reset are plain GPIOs on this board (no IO expander): no pre-init dance.
    inline void BoardPreInit() {}
}
