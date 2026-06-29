// Board support for the Blipscope Pro 2.1 (Waveshare ESP32-S3-Touch-LCD-2.1).
//
// The whole file is gated on the variant so it compiles to nothing for every other SKU
// (the call sites use board:: no-ops from Board.h / variant::BoardPreInit() from c3_128.h).
//
// What's here, and why it can't just live in the variant header:
//   - variant::BoardPreInit(): the TCA9554 IO-expander bring-up that MUST run before
//     tft.init(). The ST7701's init-SPI chip-select (EXIO3), the panel reset (EXIO1) and
//     the touch reset (EXIO2) are all on the expander, not GPIOs. We pulse the resets and
//     then hold LCD_CS low for the whole session so LovyanGFX's bit-banged ST7701 init
//     reaches the panel (LGFX.h passes pin_cs = -1). The buzzer is on EXIO8.
//   - board::ImuRead(): the QMI8658 accelerometer, for the Stats-screen tilt readout.
//   - board::BuzzerChirp()/BuzzerUpdate(): a non-blocking beep on the active buzzer.
//
// All I2C goes through LovyanGFX's own lgfx::i2c (the same owner as the touch driver) so we
// don't fight Arduino Wire for I2C port 0. Every call here runs on the loop task, in lockstep
// with the touch poll, so the bus is naturally serialized -- no extra mutex needed.

#if defined(BLIPSCOPE_VARIANT_S3_21)

#include <Arduino.h>
#include <LovyanGFX.hpp> // lgfx::i2c

#include "Board.h"
#include "LGFX.h"
#include "variants/Variant.h"

namespace {

// Shared I2C bus (touch + expander + IMU), from the variant's touch macros.
constexpr int      I2C_PORT = BLIPSCOPE_TOUCH_I2C_PORT;
constexpr int      I2C_SDA  = BLIPSCOPE_TOUCH_PIN_SDA;
constexpr int      I2C_SCL  = BLIPSCOPE_TOUCH_PIN_SCL;
constexpr uint32_t I2C_FREQ = 400000;

// TCA9554PWR IO expander.
constexpr uint8_t TCA_ADDR       = 0x20;
constexpr uint8_t TCA_OUTPUT_REG = 0x01;
constexpr uint8_t TCA_CONFIG_REG = 0x03; // 1 = input, 0 = output
// EXIO pin -> output-register bit (EXIOn == bit n-1).
constexpr uint8_t EXIO_LCD_RST = 1 << 0; // EXIO1
constexpr uint8_t EXIO_TP_RST  = 1 << 1; // EXIO2
constexpr uint8_t EXIO_LCD_CS  = 1 << 2; // EXIO3
constexpr uint8_t EXIO_BUZZER  = 1 << 7; // EXIO8

// QMI8658 6-axis IMU.
constexpr uint8_t QMI_ADDR   = 0x6B;
constexpr uint8_t QMI_CTRL1  = 0x02;
constexpr uint8_t QMI_CTRL2  = 0x03;
constexpr uint8_t QMI_CTRL7  = 0x08;
constexpr uint8_t QMI_AX_L   = 0x35;
constexpr float   QMI_ACC_LSB_PER_G = 16384.0f; // ±2 g full scale

// Cached expander output latch + buzzer timing (loop-task only, no locking).
uint8_t       g_exioOut = 0;
bool          g_buzzerOn = false;
unsigned long g_buzzerStopMs = 0;

bool i2cWriteBytes(uint8_t addr, const uint8_t* data, size_t len)
{
    bool ok = lgfx::i2c::beginTransaction(I2C_PORT, addr, I2C_FREQ, false).has_value();
    if (ok) ok = lgfx::i2c::writeBytes(I2C_PORT, data, len).has_value();
    lgfx::i2c::endTransaction(I2C_PORT);
    return ok;
}

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    return i2cWriteBytes(addr, buf, 2);
}

// Push the cached output latch to the expander.
void exioFlush() { i2cWriteReg(TCA_ADDR, TCA_OUTPUT_REG, g_exioOut); }

void imuInit()
{
    i2cWriteReg(QMI_ADDR, QMI_CTRL1, 0x40); // SIM=I2C, ADDR auto-increment
    delay(2);
    i2cWriteReg(QMI_ADDR, QMI_CTRL2, 0x05); // accelerometer: ±2 g, ODR ~250 Hz
    i2cWriteReg(QMI_ADDR, QMI_CTRL7, 0x01); // enable accelerometer
    delay(2);
}

} // namespace

void variant::BoardPreInit()
{
    lgfx::i2c::init(I2C_PORT, I2C_SDA, I2C_SCL);

    // All expander pins to output mode. Whether this ACKs tells us if the I2C bus + the
    // TCA9554 are alive at all -- if it NAKs, the held-low LCD_CS never reaches the ST7701
    // and the panel shows uninitialised garbage (the classic green screen).
    const bool tcaOk = i2cWriteReg(TCA_ADDR, TCA_CONFIG_REG, 0x00);
    Serial.printf("[board] TCA9554 @0x%02X %s\n", TCA_ADDR, tcaOk ? "ACK (expander alive)" : "NAK -- not responding!");

    // Start with resets de-asserted (high), CS de-asserted (high), buzzer off (low).
    g_exioOut = EXIO_LCD_RST | EXIO_TP_RST | EXIO_LCD_CS;
    exioFlush();
    delay(20);

    // Reset the panel (EXIO1), then the touch controller (EXIO2): low pulse, then release.
    g_exioOut &= ~EXIO_LCD_RST; exioFlush(); delay(10);
    g_exioOut |=  EXIO_LCD_RST; exioFlush(); delay(50);
    g_exioOut &= ~EXIO_TP_RST;  exioFlush(); delay(10);
    g_exioOut |=  EXIO_TP_RST;  exioFlush(); delay(50);

    // Hold LCD_CS low for the rest of the session. It's the only device on the init SPI bus,
    // so leaving CS asserted lets every later panel command (e.g. invertDisplay) through;
    // RGB pixel streaming doesn't use CS at all.
    g_exioOut &= ~EXIO_LCD_CS;  exioFlush(); delay(10);

    imuInit();

    // WHO_AM_I confirms the IMU (and the shared I2C bus) is talking; expect 0x05 for a QMI8658.
    const auto who = lgfx::i2c::readRegister8(I2C_PORT, QMI_ADDR, 0x00, I2C_FREQ);
    Serial.printf("[board] QMI8658 WHO_AM_I=0x%02X (expect 0x05); LCD_CS held low for ST7701 init\n",
                  who.has_value() ? who.value() : 0xFF);
}

bool board::ImuRead(board::Imu& out)
{
    uint8_t reg = QMI_AX_L;
    uint8_t b[6];
    if (!lgfx::i2c::transactionWriteRead(I2C_PORT, QMI_ADDR, &reg, 1, b, sizeof(b), I2C_FREQ).has_value())
        return false;

    const int16_t rx = (int16_t)(b[0] | (b[1] << 8));
    const int16_t ry = (int16_t)(b[2] | (b[3] << 8));
    const int16_t rz = (int16_t)(b[4] | (b[5] << 8));
    out.ax = rx / QMI_ACC_LSB_PER_G;
    out.ay = ry / QMI_ACC_LSB_PER_G;
    out.az = rz / QMI_ACC_LSB_PER_G;
    return true;
}

void board::BuzzerChirp(uint16_t ms)
{
    g_exioOut |= EXIO_BUZZER;
    exioFlush();
    g_buzzerOn = true;
    g_buzzerStopMs = millis() + ms;
}

void board::BuzzerUpdate()
{
    if (g_buzzerOn && (long)(millis() - g_buzzerStopMs) >= 0) {
        g_exioOut &= ~EXIO_BUZZER;
        exioFlush();
        g_buzzerOn = false;
    }
}

void board::DisplayFlush(LGFX& tft)
{
    // The custom esp_lcd panel's display() writes the framebuffer back from cache so the LCD DMA
    // scans the freshly-drawn frame; tft.display() routes straight to it.
    tft.display();
}

#endif // BLIPSCOPE_VARIANT_S3_21
