#pragma once

// Variant first: it defines BLIPSCOPE_PANEL_* / the pin macros that select the blocks below.
#include "variants/Variant.h"

#include <LovyanGFX.hpp>

#if defined(BLIPSCOPE_PANEL_ST7701)
// Custom ST7701 panel that drives the RGB bus via esp_lcd (LovyanGFX's own Bus_RGB doesn't bring
// up scan-out on IDF 5.5). See the header for the full rationale.
  #include "Panel_ST7701_esplcd.hpp"
#endif
#if defined(BLIPSCOPE_PANEL_SPD2010)
// Custom SPD2010 QSPI panel (LovyanGFX has no SPD2010 driver). Subclasses lgfx::Panel_AMOLED.
  #include "Panel_SPD2010.hpp"
#endif
#if defined(BLIPSCOPE_TOUCH_SPD2010)
// Custom SPD2010 touch (the touch half of the same TDDI chip; not in LovyanGFX).
  #include "Touch_SPD2010.hpp"
#endif

// Per-variant LovyanGFX device. The display/touch driver classes and the whole bring-up
// body are selected by the BLIPSCOPE_PANEL_* / BLIPSCOPE_TOUCH_* macros the active variant
// header defines; pins and bus params come from that header too. To add a new SKU's panel,
// add an `#elif defined(BLIPSCOPE_PANEL_xxx)` block here (and a touch block below) -- the
// app code above this layer never changes.

class LGFX : public lgfx::LGFX_Device
{
#if defined(BLIPSCOPE_PANEL_GC9A01)
    lgfx::Panel_GC9A01 _panel;
    lgfx::Bus_SPI _bus;
#elif defined(BLIPSCOPE_PANEL_ST7701)
    Panel_ST7701_esplcd _panel; // RGB bus via esp_lcd; no separate lgfx Bus needed
#elif defined(BLIPSCOPE_PANEL_SPD2010)
    Panel_SPD2010 _panel;
    lgfx::Bus_SPI _bus; // QSPI mode (selected by setting all four pin_io* below)
#else
  #error "No BLIPSCOPE_PANEL_* selected for this variant -- add a panel block to LGFX.h."
#endif
    lgfx::Light_PWM _light; // every SKU drives a PWM backlight (config is shared below)

#if defined(BLIPSCOPE_TOUCH_CST816)
    lgfx::Touch_CST816S _touch;
#elif defined(BLIPSCOPE_TOUCH_SPD2010)
    lgfx::Touch_SPD2010 _touch;
#endif

public:
    LGFX(void)
    {
#if defined(BLIPSCOPE_PANEL_GC9A01)
        {
            auto cfg = _bus.config();
            cfg.spi_host = BLIPSCOPE_DISP_SPI_HOST;
            cfg.freq_write = BLIPSCOPE_DISP_FREQ_WRITE;
            cfg.pin_miso = -1;
            cfg.pin_mosi = BLIPSCOPE_DISP_PIN_MOSI;
            cfg.pin_sclk = BLIPSCOPE_DISP_PIN_SCLK;
            cfg.pin_dc = BLIPSCOPE_DISP_PIN_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = BLIPSCOPE_DISP_PIN_CS;
            cfg.pin_rst = BLIPSCOPE_DISP_PIN_RST;
            cfg.pin_busy = BLIPSCOPE_DISP_PIN_BUSY;
            // cfg.rgb_order = true;
            _panel.config(cfg);
        }
#elif defined(BLIPSCOPE_PANEL_ST7701)
        {
            // ST7701 480x480 round panel. Geometry comes from the variant (square bounding box).
            auto cfg = _panel.config();
            cfg.memory_width  = variant::SCREEN_SIZE;
            cfg.memory_height = variant::SCREEN_SIZE;
            cfg.panel_width   = variant::SCREEN_SIZE;
            cfg.panel_height  = variant::SCREEN_SIZE;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            // cfg.rgb_order = true; // flip if red/blue come out swapped on first bring-up
            _panel.config(cfg);
        }
        {
            // The init command stream is a 3-wire SPI bit-banged on these GPIOs. CS is on the
            // TCA9554 expander (EXIO3), held low for the whole session by variant::BoardPreInit(),
            // so LovyanGFX must NOT try to toggle it -> pin_cs = -1.
            auto cfg = _panel.config_detail();
            cfg.pin_cs   = -1;
            cfg.pin_sclk = BLIPSCOPE_DISP_PIN_SCLK;
            cfg.pin_mosi = BLIPSCOPE_DISP_PIN_MOSI;
            _panel.config_detail(cfg);
        }
        {
            // RGB-interface wiring + timing -> esp_lcd (created inside the panel's init()).
            Panel_ST7701_esplcd::esp_rgb_cfg_t cfg = {};
            cfg.pin_data[0]  = BLIPSCOPE_RGB_PIN_D0;
            cfg.pin_data[1]  = BLIPSCOPE_RGB_PIN_D1;
            cfg.pin_data[2]  = BLIPSCOPE_RGB_PIN_D2;
            cfg.pin_data[3]  = BLIPSCOPE_RGB_PIN_D3;
            cfg.pin_data[4]  = BLIPSCOPE_RGB_PIN_D4;
            cfg.pin_data[5]  = BLIPSCOPE_RGB_PIN_D5;
            cfg.pin_data[6]  = BLIPSCOPE_RGB_PIN_D6;
            cfg.pin_data[7]  = BLIPSCOPE_RGB_PIN_D7;
            cfg.pin_data[8]  = BLIPSCOPE_RGB_PIN_D8;
            cfg.pin_data[9]  = BLIPSCOPE_RGB_PIN_D9;
            cfg.pin_data[10] = BLIPSCOPE_RGB_PIN_D10;
            cfg.pin_data[11] = BLIPSCOPE_RGB_PIN_D11;
            cfg.pin_data[12] = BLIPSCOPE_RGB_PIN_D12;
            cfg.pin_data[13] = BLIPSCOPE_RGB_PIN_D13;
            cfg.pin_data[14] = BLIPSCOPE_RGB_PIN_D14;
            cfg.pin_data[15] = BLIPSCOPE_RGB_PIN_D15;
            cfg.pin_hsync = BLIPSCOPE_RGB_PIN_HSYNC;
            cfg.pin_vsync = BLIPSCOPE_RGB_PIN_VSYNC;
            cfg.pin_de    = BLIPSCOPE_RGB_PIN_DE;
            cfg.pin_pclk  = BLIPSCOPE_RGB_PIN_PCLK;
            cfg.pclk_hz   = BLIPSCOPE_RGB_FREQ;
            cfg.hpw = BLIPSCOPE_RGB_HSYNC_PULSE;
            cfg.hbp = BLIPSCOPE_RGB_HSYNC_BACK;
            cfg.hfp = BLIPSCOPE_RGB_HSYNC_FRONT;
            cfg.vpw = BLIPSCOPE_RGB_VSYNC_PULSE;
            cfg.vbp = BLIPSCOPE_RGB_VSYNC_BACK;
            cfg.vfp = BLIPSCOPE_RGB_VSYNC_FRONT;
            _panel.setEspRgbConfig(cfg);
        }
#elif defined(BLIPSCOPE_PANEL_SPD2010)
        {
            // QSPI bus: setting all four pin_io* puts lgfx::Bus_SPI into quad mode. The SPD2010 carries
            // the command in-band (the 0x02 frame Panel_AMOLED emits), so there is no DC line.
            auto cfg = _bus.config();
            cfg.spi_host   = BLIPSCOPE_DISP_SPI_HOST;
            cfg.freq_write = BLIPSCOPE_DISP_FREQ_WRITE;
            cfg.pin_sclk = BLIPSCOPE_DISP_PIN_SCLK;
            cfg.pin_dc   = -1;
            cfg.pin_mosi = -1;
            cfg.pin_miso = -1;
            cfg.pin_io0  = BLIPSCOPE_DISP_PIN_IO0;
            cfg.pin_io1  = BLIPSCOPE_DISP_PIN_IO1;
            cfg.pin_io2  = BLIPSCOPE_DISP_PIN_IO2;
            cfg.pin_io3  = BLIPSCOPE_DISP_PIN_IO3;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            // SPD2010 412x412 round panel. Geometry from the variant (square bounding box). The init
            // command stream + sizes live in Panel_SPD2010; here we just attach CS and confirm geometry.
            auto cfg = _panel.config();
            cfg.pin_cs   = BLIPSCOPE_DISP_PIN_CS;
            cfg.pin_rst  = BLIPSCOPE_DISP_PIN_RST; // -1: panel reset is on the TCA9554 expander
            cfg.pin_busy = -1;
            cfg.memory_width  = variant::SCREEN_SIZE;
            cfg.memory_height = variant::SCREEN_SIZE;
            cfg.panel_width   = variant::SCREEN_SIZE;
            cfg.panel_height  = variant::SCREEN_SIZE;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            // cfg.rgb_order = true; // flip if red/blue come out swapped on first bring-up
            _panel.config(cfg);
        }
#endif // panel selection

        {
            // Backlight PWM, shared by every panel. Drive it well above hearing: the LovyanGFX
            // default of 1.2 kHz sits in the audible band and makes the backlight rail's
            // inductor/caps whine when partially dimmed (night auto-dim, custom brightness).
            // See the variant's BLIPSCOPE_BL_FREQ.
            auto cfg = _light.config();
            cfg.pin_bl = BLIPSCOPE_BL_PIN;
            cfg.invert = BLIPSCOPE_BL_INVERT;
            cfg.freq = BLIPSCOPE_BL_FREQ;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

#if defined(BLIPSCOPE_TOUCH_CST816)
        {
            // CST816 capacitive touch on its own I2C bus (not the display SPI).
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = variant::SCREEN_SIZE - 1;
            cfg.y_min = 0;
            cfg.y_max = variant::SCREEN_SIZE - 1;
            cfg.pin_int = BLIPSCOPE_TOUCH_PIN_INT;
            cfg.pin_rst = BLIPSCOPE_TOUCH_PIN_RST;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port = BLIPSCOPE_TOUCH_I2C_PORT;
            cfg.pin_sda = BLIPSCOPE_TOUCH_PIN_SDA;
            cfg.pin_scl = BLIPSCOPE_TOUCH_PIN_SCL;
            cfg.i2c_addr = BLIPSCOPE_TOUCH_I2C_ADDR;
            cfg.freq = BLIPSCOPE_TOUCH_FREQ;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
#endif // BLIPSCOPE_TOUCH_CST816

#if defined(BLIPSCOPE_TOUCH_SPD2010)
        {
            // SPD2010 capacitive touch, sharing the I2C bus with the IMU + IO expander.
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = variant::SCREEN_SIZE - 1;
            cfg.y_min = 0;
            cfg.y_max = variant::SCREEN_SIZE - 1;
            cfg.pin_int = BLIPSCOPE_TOUCH_PIN_INT;
            cfg.pin_rst = BLIPSCOPE_TOUCH_PIN_RST;
            cfg.bus_shared = true; // shared I2C (touch + IMU + TCA9554)
            cfg.offset_rotation = 0;
            cfg.i2c_port = BLIPSCOPE_TOUCH_I2C_PORT;
            cfg.pin_sda = BLIPSCOPE_TOUCH_PIN_SDA;
            cfg.pin_scl = BLIPSCOPE_TOUCH_PIN_SCL;
            cfg.i2c_addr = BLIPSCOPE_TOUCH_I2C_ADDR;
            cfg.freq = BLIPSCOPE_TOUCH_FREQ;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
#endif // BLIPSCOPE_TOUCH_SPD2010

        setPanel(&_panel);
    }
};
