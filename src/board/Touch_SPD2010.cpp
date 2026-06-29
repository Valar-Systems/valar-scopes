// SPD2010 touch driver implementation (declaration in include/Touch_SPD2010.hpp).
//
// Gated on the variant's touch-select macro so it compiles to nothing on SKUs that don't use it.
// See the header for the rationale; the protocol is ported from the vendor Touch_SPD2010 driver.

#include "Touch_SPD2010.hpp"

#if defined(BLIPSCOPE_TOUCH_SPD2010)

#include <cstring>
#include <algorithm>
#include <LovyanGFX.hpp> // lgfx::i2c, lgfx::gpio_in, lgfx::pinMode, timing helpers

namespace lgfx { inline namespace v1 {

namespace {
  // SPD2010 touch registers (16-bit, value chosen so the low-byte-first wire order matches the
  // vendor driver: e.g. STATUS 0x0020 -> {0x20,0x00}, HDP 0x0300 -> {0x00,0x03}).
  constexpr uint16_t REG_CLEAR_INT  = 0x0002;
  constexpr uint16_t REG_CPU_START  = 0x0004;
  constexpr uint16_t REG_TP_START   = 0x0046;
  constexpr uint16_t REG_POINT_MODE = 0x0050;
  constexpr uint16_t REG_STATUS     = 0x0020;
  constexpr uint16_t REG_HDP        = 0x0300;
  constexpr uint16_t REG_HDP_STATUS = 0xFC02;
}

bool Touch_SPD2010::_write_cmd(uint16_t reg, const uint8_t* data, size_t len)
{
  uint8_t buf[8];
  buf[0] = (uint8_t)(reg & 0xFF);
  buf[1] = (uint8_t)(reg >> 8);
  if (len > sizeof(buf) - 2) len = sizeof(buf) - 2;
  if (len) memcpy(&buf[2], data, len);
  return i2c::transactionWrite(_cfg.i2c_port, _cfg.i2c_addr, buf, len + 2, _cfg.freq).has_value();
}

bool Touch_SPD2010::_read_reg(uint16_t reg, uint8_t* data, size_t len)
{
  uint8_t ptr[2] = { (uint8_t)(reg & 0xFF), (uint8_t)(reg >> 8) };
  // Write the register pointer with a STOP, then a separate read (START) -- the vendor driver does
  // not use a repeated start here.
  if (!i2c::transactionWrite(_cfg.i2c_port, _cfg.i2c_addr, ptr, 2, _cfg.freq).has_value()) return false;
  bool ok = i2c::beginTransaction(_cfg.i2c_port, _cfg.i2c_addr, _cfg.freq, true).has_value()
         && i2c::readBytes(_cfg.i2c_port, data, len).has_value();
  i2c::endTransaction(_cfg.i2c_port);
  return ok;
}

bool Touch_SPD2010::_clear_int(void)
{
  static constexpr uint8_t ack[2]  = { 0x01, 0x00 }; // acknowledge
  static constexpr uint8_t rearm[2] = { 0x00, 0x00 }; // re-arm for the next interrupt
  bool ok = _write_cmd(REG_CLEAR_INT, ack, 2);
  lgfx::delayMicroseconds(200);
  ok = _write_cmd(REG_CLEAR_INT, rearm, 2) && ok;
  return ok;
}

bool Touch_SPD2010::_check_init(void)
{
  // The controller boots itself through BIOS -> CPU -> point-mode via the interrupt-driven state
  // machine in getTouchRaw(); there's no separate probe to do here. Reset is handled by the board's
  // TCA9554 (TP_RST on EXIO1) in variant::BoardPreInit().
  _inited = true;
  return true;
}

bool Touch_SPD2010::init(void)
{
  _inited = false;
  if (_cfg.pin_int >= 0) {
    lgfx::pinMode(_cfg.pin_int, pin_mode_t::input_pullup);
  }
  lgfx::i2c::init(_cfg.i2c_port, _cfg.pin_sda, _cfg.pin_scl);
  return true;
}

uint_fast8_t Touch_SPD2010::getTouchRaw(touch_point_t* tp, uint_fast8_t count)
{
  if (!_inited && !_check_init()) return 0;
  if (count == 0) return 0;

  // INT is asserted LOW whenever the controller wants service (BIOS/CPU ready, or touch data). When
  // it's high there is nothing to do -- skip the I2C traffic entirely.
  if (_cfg.pin_int >= 0 && lgfx::gpio_in(_cfg.pin_int)) return 0;

  uint8_t st[4];
  if (!_read_reg(REG_STATUS, st, 4)) return 0;

  const bool pt_exist    = st[0] & 0x01;
  const bool gesture     = st[0] & 0x02;
  const bool aux         = st[0] & 0x08;
  const bool tic_in_bios = st[1] & 0x40;
  const bool tic_in_cpu  = st[1] & 0x20;
  const bool cpu_run     = st[1] & 0x08;
  uint16_t read_len = (uint16_t)st[2] | ((uint16_t)st[3] << 8);
  if (read_len < 4 || read_len > 64) read_len = 0;

  // ---- bring-up / housekeeping states (no touch data yet) ----
  if (tic_in_bios) { _clear_int(); uint8_t d[2] = {0x01, 0x00}; _write_cmd(REG_CPU_START, d, 2); return 0; }
  if (tic_in_cpu)  {
    uint8_t z[2] = {0x00, 0x00};
    _write_cmd(REG_POINT_MODE, z, 2);
    _write_cmd(REG_TP_START, z, 2);
    _clear_int();
    return 0;
  }
  if (cpu_run && read_len == 0) { _clear_int(); return 0; }

  // ---- touch / gesture packet present ----
  if (pt_exist || gesture) {
    uint8_t buf[64];
    uint_fast8_t n = 0;
    if (_read_reg(REG_HDP, buf, read_len) && (buf[4] <= 0x0A) && pt_exist) {
      n = (read_len - 4) / 6;
      n = std::min<uint_fast8_t>(n, std::min<uint_fast8_t>(count, max_touch_points));
      for (uint_fast8_t i = 0; i < n; ++i) {
        const uint8_t o = i * 6;
        tp[i].id   = buf[4 + o];
        tp[i].x    = (uint16_t)(((buf[7 + o] & 0xF0) << 4) | buf[5 + o]);
        tp[i].y    = (uint16_t)(((buf[7 + o] & 0x0F) << 8) | buf[6 + o]);
        tp[i].size = buf[8 + o]; // weight
      }
    }
    _clear_int();

    // Drain any remaining HDP packets so the controller can re-arm cleanly.
    for (int guard = 0; guard < 8; ++guard) {
      uint8_t hs[8];
      if (!_read_reg(REG_HDP_STATUS, hs, 8)) break;
      const uint8_t status = hs[5];
      const uint16_t next_len = (uint16_t)hs[2] | ((uint16_t)hs[3] << 8);
      if (status == 0x82) { _clear_int(); break; }
      if (status == 0x00 && next_len && next_len <= 64) { uint8_t rem[64]; _read_reg(REG_HDP, rem, next_len); continue; }
      break;
    }
    return n;
  }

  // ---- aux event only ----
  if (cpu_run && aux) { _clear_int(); }
  return 0;
}

}}

#endif // BLIPSCOPE_TOUCH_SPD2010
