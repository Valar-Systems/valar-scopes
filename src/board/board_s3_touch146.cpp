// Board support for the Blipscope 1.46 (Waveshare ESP32-S3-Touch-LCD-1.46B).
//
// Gated on the variant so it compiles to nothing for every other SKU (call sites use the board::
// no-ops from Board.h / variant::BoardPreInit() defined elsewhere).
//
// What's here, and why it can't live in the variant header:
//   - variant::BoardPreInit(): the TCA9554 IO-expander bring-up that MUST run before tft.init().
//     The panel reset (EXIO2) and touch reset (EXIO1) are on the expander, not GPIOs. We pulse both.
//     Unlike the S3-2.1 there is NO held-low-CS hack here: LCD_CS is a real GPIO driven by the QSPI
//     bus. The expander also gates the (unused) SD card CS on EXIO3, which we leave deasserted.
//   - board::ImuRead(): the QMI8658 accelerometer, for the Stats-screen tilt readout (same part and
//     registers as the S3-2.1).
//   - board::BuzzerChirp()/BuzzerUpdate(): a short, non-blocking "chirp" -- a tone burst written to
//     the PCM5101 I2S DAC. (The S3-2.1's chirp is a one-GPIO buzzer; this board has a real speaker.)
//
// All board I2C goes through LovyanGFX's own lgfx::i2c (same owner as the touch driver) on the loop
// task, so the shared bus (touch + expander + IMU + RTC) serializes naturally -- no Arduino Wire.

#if defined(BLIPSCOPE_VARIANT_S3_146)

#include <Arduino.h>
#include <LovyanGFX.hpp>        // lgfx::i2c
#include <driver/i2s_std.h>     // PCM5101 I2S speaker

#include "Board.h"
#include "LGFX.h"
#include "variants/Variant.h"

namespace {

// Shared I2C bus (touch + expander + IMU + RTC), from the variant's touch macros.
constexpr int      I2C_PORT = BLIPSCOPE_TOUCH_I2C_PORT;
constexpr int      I2C_SDA  = BLIPSCOPE_TOUCH_PIN_SDA;
constexpr int      I2C_SCL  = BLIPSCOPE_TOUCH_PIN_SCL;
constexpr uint32_t I2C_FREQ = 400000;

// TCA9554PWR IO expander.
constexpr uint8_t TCA_ADDR       = 0x20;
constexpr uint8_t TCA_OUTPUT_REG = 0x01;
constexpr uint8_t TCA_CONFIG_REG = 0x03; // 1 = input, 0 = output
// EXIO pin -> output-register bit (EXIOn == bit n-1).
constexpr uint8_t EXIO_TP_RST  = 1 << 0; // EXIO1 -> touch reset
constexpr uint8_t EXIO_LCD_RST = 1 << 1; // EXIO2 -> panel reset
constexpr uint8_t EXIO_SD_CS   = 1 << 2; // EXIO3 -> SD card CS (unused; held deasserted)
// EXIO4/EXIO5 are the QMI8658 INT lines -> inputs. Config: outputs on bits 0..2, inputs elsewhere.
constexpr uint8_t TCA_CONFIG    = 0xF8;  // 0b11111000

// QMI8658 6-axis IMU (same part/registers as the S3-2.1).
constexpr uint8_t QMI_ADDR  = 0x6B;
constexpr uint8_t QMI_CTRL1 = 0x02;
constexpr uint8_t QMI_CTRL2 = 0x03;
constexpr uint8_t QMI_CTRL7 = 0x08;
constexpr uint8_t QMI_AX_L  = 0x35;
constexpr float   QMI_ACC_LSB_PER_G = 16384.0f; // ±2 g full scale

// PCM5101 I2S speaker pins.
constexpr int PIN_I2S_DIN  = 47; // Speak_DIN
constexpr int PIN_I2S_LRCK = 38; // Speak_LRCK
constexpr int PIN_I2S_BCK  = 48; // Speak_BCK
constexpr uint32_t AUDIO_RATE = 16000;
constexpr int      CHIRP_HZ   = 2000;
constexpr int16_t  CHIRP_AMP  = 6000;

uint8_t g_exioOut = 0;
i2s_chan_handle_t g_i2sTx = nullptr;

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

void exioFlush() { i2cWriteReg(TCA_ADDR, TCA_OUTPUT_REG, g_exioOut); }

void imuInit()
{
    i2cWriteReg(QMI_ADDR, QMI_CTRL1, 0x40); // SIM=I2C, ADDR auto-increment
    delay(2);
    i2cWriteReg(QMI_ADDR, QMI_CTRL2, 0x05); // accelerometer: ±2 g, ODR ~250 Hz
    i2cWriteReg(QMI_ADDR, QMI_CTRL7, 0x01); // enable accelerometer
    delay(2);
}

void audioInit()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 6;
    chan_cfg.dma_frame_num = 240; // 6*240 = 1440 frames of DMA ring -> a chirp burst fits without blocking
    chan_cfg.auto_clear    = true; // output silence (zeros) when the ring underruns, so no buzzing
    if (i2s_new_channel(&chan_cfg, &g_i2sTx, nullptr) != ESP_OK) { g_i2sTx = nullptr; return; }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_I2S_BCK,
            .ws   = (gpio_num_t)PIN_I2S_LRCK,
            .dout = (gpio_num_t)PIN_I2S_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    if (i2s_channel_init_std_mode(g_i2sTx, &std_cfg) != ESP_OK) { g_i2sTx = nullptr; return; }
    i2s_channel_enable(g_i2sTx);
}

} // namespace

void variant::BoardPreInit()
{
    lgfx::i2c::init(I2C_PORT, I2C_SDA, I2C_SCL);

    // Expander pin directions. Whether this ACKs tells us if the bus + TCA9554 are alive -- if it
    // NAKs, neither reset reaches the panel/touch and the screen shows uninitialised garbage.
    const bool tcaOk = i2cWriteReg(TCA_ADDR, TCA_CONFIG_REG, TCA_CONFIG);
    Serial.printf("[board] TCA9554 @0x%02X %s\n", TCA_ADDR, tcaOk ? "ACK (expander alive)" : "NAK -- not responding!");

    // Resets de-asserted (high), SD CS deasserted (high).
    g_exioOut = EXIO_LCD_RST | EXIO_TP_RST | EXIO_SD_CS;
    exioFlush();
    delay(20);

    // Reset the panel (EXIO2) then the touch controller (EXIO1): low pulse, then release.
    g_exioOut &= ~EXIO_LCD_RST; exioFlush(); delay(10);
    g_exioOut |=  EXIO_LCD_RST; exioFlush(); delay(50);
    g_exioOut &= ~EXIO_TP_RST;  exioFlush(); delay(10);
    g_exioOut |=  EXIO_TP_RST;  exioFlush(); delay(50);

    imuInit();
    audioInit();

    // WHO_AM_I confirms the IMU (and the shared I2C bus) is talking; expect 0x05 for a QMI8658.
    const auto who = lgfx::i2c::readRegister8(I2C_PORT, QMI_ADDR, 0x00, I2C_FREQ);
    Serial.printf("[board] QMI8658 WHO_AM_I=0x%02X (expect 0x05); I2S speaker %s\n",
                  who.has_value() ? who.value() : 0xFF, g_i2sTx ? "ready" : "init FAILED");
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
    if (!g_i2sTx) return;
    if (ms > 80) ms = 80; // cap so the burst always fits the DMA ring (stays non-blocking)

    // Build a square-wave tone: stereo 16-bit, both channels the same.
    const size_t frames = (size_t)ms * AUDIO_RATE / 1000;
    const int half = AUDIO_RATE / (CHIRP_HZ * 2); // samples per half period
    static int16_t buf[2 * 80 * AUDIO_RATE / 1000]; // worst-case 80 ms stereo
    for (size_t i = 0; i < frames; ++i) {
        const int16_t s = (half > 0 && ((int)i / half) & 1) ? CHIRP_AMP : (int16_t)-CHIRP_AMP;
        buf[2 * i]     = s; // L
        buf[2 * i + 1] = s; // R
    }
    size_t written = 0;
    // timeout 0: copy into the DMA ring and return immediately (ring is sized to hold the burst).
    i2s_channel_write(g_i2sTx, buf, frames * 2 * sizeof(int16_t), &written, 0);
}

void board::BuzzerUpdate()
{
    // Nothing to do: the DMA drains the burst on its own and auto_clear emits silence afterward.
}

void board::DisplayFlush(LGFX&)
{
    // QSPI panel writes are immediate (no back framebuffer to present), so there's nothing to flush.
}

#endif // BLIPSCOPE_VARIANT_S3_146
