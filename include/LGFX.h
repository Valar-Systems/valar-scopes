#pragma once

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_GC9A01 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;
    lgfx::Touch_CST816S _touch;

public:
    LGFX(void)
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI2_HOST;
            cfg.freq_write = 27000000;
            cfg.pin_miso = -1;
            cfg.pin_mosi = 7;
            cfg.pin_sclk = 6;
            cfg.pin_dc = 2;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = 10;
            cfg.pin_rst = -1;
            cfg.pin_busy = -1;
            // cfg.rgb_order = true;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = 3;
            cfg.invert = false;
            // Drive the backlight PWM well above hearing. The LovyanGFX default
            // of 1.2 kHz sits in the audible band and makes the backlight rail's
            // inductor/caps whine whenever the screen is partially dimmed
            // (auto-dim at night, or a custom brightness). 40 kHz minimizes the
            // mechanical excursion of those parts; LEDC's 9-bit resolution on
            // the C3's 80 MHz clock supports up to ~156 kHz, so dimming quality
            // is unaffected.
            cfg.freq = 40000;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            // CST816S capacitive touch on its own I2C bus (not the display SPI)
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = 239;
            cfg.y_min = 0;
            cfg.y_max = 239;
            cfg.pin_int = 0;
            cfg.pin_rst = 1;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port = 0;
            cfg.pin_sda = 4;
            cfg.pin_scl = 5;
            cfg.i2c_addr = 0x15;
            cfg.freq = 400000;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};