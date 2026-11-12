#pragma once

// Blipscope on the ESP32-P4 4" -- Waveshare ESP32-P4-WIFI6-Touch-LCD-XC, 4-inch version ("4C").
// BENCH-ONLY BRING-UP (docs/p4-40-bringup.md): no slug in skus.yml, no release asset, nothing ships.
//
// ESP32-P4NRW32 (dual-core RISC-V, 32 MB flash, 32 MB PSRAM) + 4" round 720x720 IPS on a JD9365
// over 2-lane MIPI-DSI + GT911 touch + an ESP32-C6 for Wi-Fi over ESP-Hosted (SDIO). Unlike every
// S3 SKU:
//   1. The panel is MIPI-DSI, driven by LovyanGFX's Bus_DSI/Panel_DSI (esp32p4 platform) with the
//      JD9365 init table from Waveshare's board package -- see include/Panel_JD9365.hpp.
//   2. The P4 has no radio. Wi-Fi is the C6's, reached through esp_wifi_remote/ESP-Hosted, which
//      the Arduino core brings up by itself on the first WiFi call. The SDIO pins are the core's
//      esp32p4 variant defaults (CLK 18, CMD 19, D0-D3 14-17, C6 reset 54); Waveshare does not
//      document them for this board, so they are a bench row (W1), not a fact.
//   3. The P4 has no Wi-Fi MAC of its own: esp_read_mac(ESP_MAC_WIFI_STA) is unsupported here,
//      so DeviceIdentity reads zeros on every P4 (docs/p4-40-bringup.md section 2, ruling Q-ID).
//
// Every pin below is from the Waveshare BSP waveshare/esp32_p4_wifi6_touch_lcd_xc v2.0.0
// (include/bsp/esp32_p4_wifi6_touch_lcd_xc.h, display.h). None is verified on hardware yet.

// ---- driver selection (consumed by LGFX.h) ----
#define BLIPSCOPE_PANEL_JD9365 1
#define BLIPSCOPE_TOUCH_GT911  1

// ---- MIPI-DSI link (BSP display.h) ----
#define BLIPSCOPE_DSI_LANES       2
#define BLIPSCOPE_DSI_LANE_MBPS   1500
#define BLIPSCOPE_DSI_LDO_CHAN    3      // LDO_VO3 feeds VDD_MIPI_DPHY
#define BLIPSCOPE_DSI_LDO_MV      2500

// ---- DPI video timing (BSP bsp_display_new_with_handles) ----
// 80 MHz over (720+20+20+40) x (720+12+4+24) = 800 x 760 is a ~132 Hz scan-out.
#define BLIPSCOPE_DPI_FREQ_MHZ    80
#define BLIPSCOPE_DPI_HSYNC_BACK  20
#define BLIPSCOPE_DPI_HSYNC_PULSE 20
#define BLIPSCOPE_DPI_HSYNC_FRONT 40
#define BLIPSCOPE_DPI_VSYNC_BACK  12
#define BLIPSCOPE_DPI_VSYNC_PULSE 4
#define BLIPSCOPE_DPI_VSYNC_FRONT 24

#define BLIPSCOPE_DISP_PIN_RST    27     // BSP_LCD_RST, active low
#define BLIPSCOPE_DISP_INVERT     false  // main.cpp's tft.invertDisplay(): the JD9365 table sends no INVON (0x21)

// ---- backlight (LEDC PWM on a GPIO) ----
// The BSP drives it with output_invert = 1, so 0 % duty is full brightness. It runs 5 kHz; this
// runs [common]'s 20 kHz so a dimmed backlight does not whine. Row B3 checks the driver dims at
// 20 kHz, and the BSP's 5 kHz is the pre-registered fallback.
#define BLIPSCOPE_BL_PIN    26
#define BLIPSCOPE_BL_INVERT true
#define BLIPSCOPE_BL_FREQ   20000

// ---- capacitive touch (GT911 on I2C1, shared with the ES8311 codec; INT and RST not wired) ----
// LovyanGFX's Touch_GT911 tries 0x5D and 0x14 in turn, so the address the chip latched at power-up
// does not matter; row B4 records which one answered. With no INT pin it is polled over I2C.
#define BLIPSCOPE_TOUCH_I2C_PORT 1
#define BLIPSCOPE_TOUCH_PIN_SDA  7
#define BLIPSCOPE_TOUCH_PIN_SCL  8
#define BLIPSCOPE_TOUCH_PIN_INT  (-1)
#define BLIPSCOPE_TOUCH_PIN_RST  (-1)
#define BLIPSCOPE_TOUCH_I2C_ADDR 0x5D
#define BLIPSCOPE_TOUCH_FREQ     400000

namespace variant {
    // Screen geometry (round; square bounding box).
    constexpr int SCREEN_SIZE = 720;

    // Capabilities.
    constexpr bool BANDED_RENDER = false; // 32 MB PSRAM: render the whole frame at once
    constexpr bool HAS_AUDIO     = false; // ES8311 codec + amp exist on-board; no driver here
    constexpr bool HAS_IMU       = false; // the board has none (BSP_CAPS_IMU 0)
    constexpr bool TOUCH_WATCHDOG = false;      // the supervisor exists for the CST816; this is a GT911

    // OTA + identity. No release publishes firmware-p4-40.bin, so an OTA check finds nothing to install.
    constexpr char SLUG[] = "p4-40";
    constexpr char NAME[] = "Blipscope P4 4.0 (bench)";

    // Prints the board facts bench rows B0/B1 read, and arms the one-shot ESP-Hosted version line
    // (row W5) for the first Wi-Fi start. Implemented in src/board/board_p4_40.cpp.
    void BoardPreInit();
}
