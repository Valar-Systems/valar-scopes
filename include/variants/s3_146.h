#pragma once

// Blipscope 1.46 -- Waveshare ESP32-S3-Touch-LCD-1.46B: ESP32-S3R8 + 1.46" round 412x412 IPS panel
// driven by an SPD2010 over QSPI, with SPD2010 capacitive touch over I2C.
//
// The SPD2010 is a TDDI chip: the same controller is both the display (QSPI) and the touch (I2C). It
// is not in LovyanGFX, so this SKU ships two small custom drivers selected by the macros below:
//   - BLIPSCOPE_PANEL_SPD2010 -> include/Panel_SPD2010.hpp (subclasses lgfx::Panel_AMOLED; the QSPI
//     command/pixel framing is shared with CO5300/SH8601, only the init blob differs).
//   - BLIPSCOPE_TOUCH_SPD2010 -> include/Touch_SPD2010.hpp (an lgfx::ITouch; tft.getTouch() unchanged).
//
// Like the S3-2.1 this board hangs the panel + touch RESET on a TCA9554 I2C IO expander, so
// variant::BoardPreInit() (src/board/board_s3_touch146.cpp) pulses them before tft.init(). Unlike the
// S3-2.1, LCD_CS is a real GPIO driven by the QSPI bus -- no held-low-CS hack. It also has a QMI8658
// IMU and a PCM5101 I2S speaker (the chirp is a short I2S tone, not a buzzer GPIO). Dual-core + 8 MB
// PSRAM, so full-frame render and always-on enrichment.
//
// Pins are from the Waveshare ESP32-S3-Touch-LCD-1.46B wiki pin table.

// ---- driver selection (consumed by LGFX.h) ----
#define BLIPSCOPE_PANEL_SPD2010 1
#define BLIPSCOPE_TOUCH_SPD2010 1

// ---- display QSPI bus (SPD2010; 4 data lines, in-band command framing so no DC) ----
#define BLIPSCOPE_DISP_SPI_HOST   SPI3_HOST
#define BLIPSCOPE_DISP_FREQ_WRITE 40000000   // conservative QSPI clock; raise after bring-up if stable
#define BLIPSCOPE_DISP_PIN_SCLK   40
#define BLIPSCOPE_DISP_PIN_CS     21
#define BLIPSCOPE_DISP_PIN_IO0    46         // LCD_SDA0
#define BLIPSCOPE_DISP_PIN_IO1    45         // LCD_SDA1
#define BLIPSCOPE_DISP_PIN_IO2    42         // LCD_SDA2
#define BLIPSCOPE_DISP_PIN_IO3    41         // LCD_SDA3
#define BLIPSCOPE_DISP_PIN_RST    (-1)       // LCD_RST is on TCA9554 EXIO2 (pulsed in BoardPreInit)
#define BLIPSCOPE_DISP_INVERT     false      // SPD2010 init leaves the panel non-inverted

// ---- backlight (PWM, real GPIO) ----
#define BLIPSCOPE_BL_PIN    5
#define BLIPSCOPE_BL_INVERT false
#define BLIPSCOPE_BL_FREQ   20000

// ---- capacitive touch (SPD2010 on the shared I2C bus; RST is on expander EXIO1, so -1) ----
#define BLIPSCOPE_TOUCH_I2C_PORT 0
#define BLIPSCOPE_TOUCH_PIN_SDA  11
#define BLIPSCOPE_TOUCH_PIN_SCL  10
#define BLIPSCOPE_TOUCH_PIN_INT  4
#define BLIPSCOPE_TOUCH_PIN_RST  (-1)
#define BLIPSCOPE_TOUCH_I2C_ADDR 0x53
#define BLIPSCOPE_TOUCH_FREQ     400000

namespace variant {
    // Screen geometry (round; square bounding box).
    constexpr int SCREEN_SIZE = 412;

    // Capabilities.
    constexpr bool BANDED_RENDER = false; // dual-core + PSRAM: render the whole frame at once
    constexpr bool ENRICH_ALWAYS = true;  // ample heap: never skip the adsbdb TLS enrichment
    constexpr bool HAS_AUDIO     = true;  // PCM5101 I2S speaker (chirp = short I2S tone)
    constexpr bool HAS_IMU       = true;  // QMI8658 6-axis accel/gyro on the shared I2C bus

    // OTA + identity. SLUG names the per-SKU release asset (firmware-<SLUG>.bin / version-<SLUG>.txt).
    constexpr char SLUG[] = "s3-146";
    constexpr char NAME[] = "Blipscope 1.46";

    // Board pre-init: drives the TCA9554 expander (panel reset EXIO2, touch reset EXIO1) and brings up
    // the IMU + I2S audio, before tft.init(). Implemented in src/board/board_s3_touch146.cpp.
    void BoardPreInit();
}
