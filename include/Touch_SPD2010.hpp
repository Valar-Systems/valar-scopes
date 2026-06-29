#pragma once

#include "variants/Variant.h" // defines BLIPSCOPE_TOUCH_SPD2010

#if defined(BLIPSCOPE_TOUCH_SPD2010)

#include <cstdint>
#include <cstddef>

#include <lgfx/v1/Touch.hpp> // base lgfx::ITouch + touch_point_t

// SPD2010 capacitive touch (the touch half of the SPD2010 TDDI chip on the round 1.46" panel).
//
// LovyanGFX has no SPD2010 touch driver, so this is a minimal lgfx::ITouch implementation modeled on
// Touch_CST816S / Touch_CST226: it plugs into the device via _panel.setTouch() and the app keeps using
// tft.getTouch() unchanged. The SPD2010 protocol is more involved than CST816 -- it is interrupt
// driven and boots through a BIOS -> CPU -> point-mode handshake -- so getTouchRaw() runs that small
// state machine (only when INT is asserted) and parses the "HDP" point packet. Ported from the vendor
// Touch_SPD2010 driver (see SPD2010Touch.cpp / esp_lcd_touch_spd2010).
//
// All I2C uses lgfx::i2c on the shared bus (same owner as the board IMU/expander), so it serializes
// naturally on the loop task -- no Arduino Wire.
namespace lgfx { inline namespace v1 {

  struct Touch_SPD2010 : public ITouch
  {
    Touch_SPD2010(void)
    {
      _cfg.i2c_addr = 0x53;
      _cfg.x_min = 0;
      _cfg.x_max = 411;
      _cfg.y_min = 0;
      _cfg.y_max = 411;
    }

    bool init(void) override;
    void wakeup(void) override {}
    void sleep(void) override {}
    uint_fast8_t getTouchRaw(touch_point_t* tp, uint_fast8_t count) override;

  private:
    enum { max_touch_points = 5 };

    bool _check_init(void);

    // 16-bit register addressing, low byte first on the wire (matches the vendor driver). Writes are
    // one transaction (STOP); reads are write-pointer (STOP) then read (START), not a repeated start.
    bool _write_cmd(uint16_t reg, const uint8_t* data, size_t len);
    bool _read_reg(uint16_t reg, uint8_t* data, size_t len);

    bool _clear_int(void);
  };

}}

#endif // BLIPSCOPE_TOUCH_SPD2010
