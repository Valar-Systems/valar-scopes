#pragma once

#include <LovyanGFX.hpp>

#include "variants/Variant.h"

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
    lgfx::Light_PWM _light;
#else
  #error "No BLIPSCOPE_PANEL_* selected for this variant -- add a panel block to LGFX.h."
#endif

#if defined(BLIPSCOPE_TOUCH_CST816)
    lgfx::Touch_CST816S _touch;
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
        {
            auto cfg = _light.config();
            cfg.pin_bl = BLIPSCOPE_BL_PIN;
            cfg.invert = BLIPSCOPE_BL_INVERT;
            // Drive the backlight PWM well above hearing. The LovyanGFX default of 1.2 kHz
            // sits in the audible band and makes the backlight rail's inductor/caps whine
            // when partially dimmed (night auto-dim, custom brightness). See the variant's
            // BLIPSCOPE_BL_FREQ.
            cfg.freq = BLIPSCOPE_BL_FREQ;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
#endif // BLIPSCOPE_PANEL_GC9A01

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

        setPanel(&_panel);
    }
};
