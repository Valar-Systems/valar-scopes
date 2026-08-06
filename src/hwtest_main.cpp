// hwtest_main.cpp -- minimal display/hardware bring-up test for the S3 1.46B.
//
// WHY THIS EXISTS. The full app boots perfectly healthy on this board -- display
// init returns 1, the 412x412 backbuffer allocates, frames render at ~39 ms, Wi-Fi
// and the cloud feed come up, allocFail=0 -- and the screen is still black. That
// combination rules out the firmware and points at the panel bring-up path, which
// the app CANNOT report on, for one specific reason:
//
//   BLIPSCOPE_DISP_PIN_RST is -1 on this variant. The panel reset is not a GPIO --
//   it hangs off EXIO2 of a TCA9554 I2C expander and is pulsed by
//   variant::BoardPreInit() BEFORE tft.init(). LovyanGFX therefore never touches
//   reset, and tft.init() returns 1 whether or not the panel ever came out of it.
//   A failed I2C write to the expander is completely invisible to the app.
//
// So this test walks the chain one layer at a time, reporting each, so the failure
// can be localised instead of guessed:
//
//   1. I2C bus scan          -- is the expander (0x20) even on the bus?
//   2. Raw backlight on GPIO5 -- does the backlight rail work at all, with
//                               LovyanGFX entirely out of the picture?
//   3. Expander config+pulse  -- does the LCD_RST write actually ACK?
//   4. tft.init + solid fills -- can the panel accept pixels once truly reset?
//
// Build/flash:  pio run -e hwtest-s3-146 -t upload -t monitor
#include <Arduino.h>
#include <lgfx/v1/platforms/common.hpp>
#include "LGFX.h"

namespace {

constexpr int      I2C_PORT = BLIPSCOPE_TOUCH_I2C_PORT; // 0
constexpr int      I2C_SDA  = BLIPSCOPE_TOUCH_PIN_SDA;  // 11
constexpr int      I2C_SCL  = BLIPSCOPE_TOUCH_PIN_SCL;  // 10
constexpr uint32_t I2C_FREQ = 400000;

// TCA9554PWR -- same constants as src/board/board_s3_touch146.cpp.
constexpr uint8_t TCA_ADDR       = 0x20;
constexpr uint8_t TCA_OUTPUT_REG = 0x01;
constexpr uint8_t TCA_CONFIG_REG = 0x03; // 1 = input, 0 = output
constexpr uint8_t TCA_CONFIG     = 0xF8; // outputs on bits 0..2
constexpr uint8_t EXIO_TP_RST    = 1 << 0;
constexpr uint8_t EXIO_LCD_RST   = 1 << 1;
constexpr uint8_t EXIO_SD_CS     = 1 << 2;

LGFX tft;

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    return lgfx::i2c::transactionWrite(I2C_PORT, addr, buf, 2, I2C_FREQ).has_value();
}

// Address probe: a 1-byte read either ACKs or it doesn't. Non-destructive.
bool i2cPresent(uint8_t addr)
{
    uint8_t b = 0;
    return lgfx::i2c::transactionRead(I2C_PORT, addr, &b, 1, I2C_FREQ).has_value();
}

void step(const char* s) { Serial.printf("\n=== %s ===\n", s); }

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(2500); // native USB-CDC needs a beat before the host is listening
    Serial.println("\n\n########## Blipscope S3-1.46B hardware test ##########");
    Serial.printf("chip=%s rev=%d cores=%d  heap=%u psram=%u\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    Serial.printf("pins: BL=%d  I2C sda=%d scl=%d  QSPI cs=%d sclk=%d\n",
                  BLIPSCOPE_BL_PIN, I2C_SDA, I2C_SCL,
                  BLIPSCOPE_DISP_PIN_CS, BLIPSCOPE_DISP_PIN_SCLK);

    // ---- 0. RAW GPIO test of the two I2C lines ----------------------------
    // Runs BEFORE lgfx takes the pins. The backlight works, so the assembly has
    // power and the ribbon carries at least that line -- which means the fault is
    // specific to SDA/SCL. This separates the three things that look identical
    // from the app's point of view:
    //
    //   both idle HIGH + toggle cleanly -> lines and ESP32 pins are electrically
    //       fine; the devices simply aren't answering (ribbon contacts for those
    //       two pins, or dead slaves). Board is likely repairable.
    //   one stuck LOW                   -> a short, or a slave clamping the bus.
    //       A held bus also explains a phantom scan hit.
    //   cannot be driven at all         -> the ESP32's GPIO 10/11 are damaged;
    //       that is a MAIN-BOARD fault, and swapping the display won't help.
    //
    // A healthy idle I2C bus sits HIGH on both lines via its pull-ups.
    step("0. Raw GPIO check of SDA/SCL (before I2C is initialised)");
    {
        const int pins[2] = { I2C_SDA, I2C_SCL };
        const char* names[2] = { "SDA", "SCL" };
        for (int i = 0; i < 2; i++) {
            pinMode(pins[i], INPUT_PULLUP);
            delay(5);
            const int idle = digitalRead(pins[i]);
            pinMode(pins[i], OUTPUT);
            digitalWrite(pins[i], LOW);  delay(2);
            pinMode(pins[i], INPUT_PULLUP); delay(2);
            const int afterLow = digitalRead(pins[i]);   // pull-up should restore HIGH
            Serial.printf("  %s (GPIO %2d): idle=%s  recovers-after-pulled-low=%s  %s\n",
                          names[i], pins[i],
                          idle ? "HIGH" : "*** LOW ***",
                          afterLow ? "YES" : "*** NO ***",
                          (idle && afterLow) ? "line looks healthy"
                                             : "*** LINE FAULT ***");
        }
        Serial.println("  (both HIGH + recovering = wiring/pins fine, so the slaves are the problem)");
    }

    // ---- 1. I2C scan ------------------------------------------------------
    // THE decisive check. 0x20 missing => the expander is unreachable => the panel
    // can never be pulsed out of reset, and every downstream symptom follows.
    step("1. I2C bus scan (expect 0x20 expander, 0x53 touch, 0x6B IMU)");
    if (!lgfx::i2c::init(I2C_PORT, I2C_SDA, I2C_SCL).has_value()) {
        Serial.println("  !! lgfx::i2c::init FAILED -- bus could not be brought up");
    }
    int found = 0;
    bool sawExpander = false;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (!i2cPresent(a)) continue;
        found++;
        if (a == TCA_ADDR) sawExpander = true;
        const char* who = (a == 0x20) ? "TCA9554 expander (panel+touch RESET)"
                        : (a == 0x53) ? "SPD2010 touch"
                        : (a == 0x6B) ? "QMI8658 IMU"
                                      : "unknown";
        Serial.printf("  0x%02X  %s\n", a, who);
    }
    Serial.printf("  -> %d device(s). expander %s\n", found,
                  sawExpander ? "PRESENT" : "*** MISSING -- this is the fault ***");

    // ---- 2. raw backlight -------------------------------------------------
    // LovyanGFX is not involved here: straight GPIO. If the panel does not visibly
    // flash during this, the backlight rail or its ribbon is the problem and no
    // amount of panel init will help.
    step("2. RAW backlight blink on GPIO 5 -- WATCH THE SCREEN for 6 flashes");
    pinMode(BLIPSCOPE_BL_PIN, OUTPUT);
    for (int i = 0; i < 6; i++) {
        digitalWrite(BLIPSCOPE_BL_PIN, (i % 2) ? LOW : HIGH);
        Serial.printf("  BL = %s\n", (i % 2) ? "LOW (off)" : "HIGH (on)");
        delay(700);
    }
    digitalWrite(BLIPSCOPE_BL_PIN, HIGH);
    Serial.println("  BL left ON");

    // ---- 3. expander config + panel reset pulse ---------------------------
    step("3. TCA9554 config + LCD_RST pulse (EXIO2)");
    Serial.printf("  config  reg 0x%02X <- 0x%02X : %s\n", TCA_CONFIG_REG, TCA_CONFIG,
                  i2cWriteReg(TCA_ADDR, TCA_CONFIG_REG, TCA_CONFIG) ? "ACK" : "*** NO ACK ***");
    // Deassert everything, then pulse both resets low and back high.
    uint8_t out = EXIO_TP_RST | EXIO_LCD_RST | EXIO_SD_CS;
    Serial.printf("  output  reg 0x%02X <- 0x%02X : %s (all high)\n", TCA_OUTPUT_REG, out,
                  i2cWriteReg(TCA_ADDR, TCA_OUTPUT_REG, out) ? "ACK" : "*** NO ACK ***");
    delay(10);
    out &= ~(EXIO_TP_RST | EXIO_LCD_RST);
    Serial.printf("  output  reg 0x%02X <- 0x%02X : %s (resets LOW)\n", TCA_OUTPUT_REG, out,
                  i2cWriteReg(TCA_ADDR, TCA_OUTPUT_REG, out) ? "ACK" : "*** NO ACK ***");
    delay(20);
    out |= (EXIO_TP_RST | EXIO_LCD_RST);
    Serial.printf("  output  reg 0x%02X <- 0x%02X : %s (resets HIGH)\n", TCA_OUTPUT_REG, out,
                  i2cWriteReg(TCA_ADDR, TCA_OUTPUT_REG, out) ? "ACK" : "*** NO ACK ***");
    delay(120); // SPD2010 needs time after reset before it accepts init

    // ---- 4. panel init + solid fills --------------------------------------
    step("4. tft.init() + solid colour fills");
    const bool ok = tft.init();
    Serial.printf("  tft.init = %d   %dx%d\n", (int)ok, tft.width(), tft.height());
    // The GC9A01 boots inverted (src/main.cpp:137 does this on the product). It
    // matters MORE here than anywhere: the whole point of step 4 is that a human
    // reads the fill colours back, and without this the RED fill is cyan and the
    // BLACK fill is white -- a panel that is working looks broken.
    tft.invertDisplay(BLIPSCOPE_DISP_INVERT);
    Serial.printf("  invertDisplay(%d)  <- GC9A01 boots inverted\n", (int)BLIPSCOPE_DISP_INVERT);
    tft.setBrightness(255);
    Serial.println("  brightness = 255");
    Serial.println("  -> cycling RED / GREEN / BLUE / WHITE / BLACK, 1.5 s each, forever");
}

// Live repair aid. Two things run forever, both chosen for a board whose I2C bus is
// down and whose panel is therefore stuck in reset:
//
//   * SLOW BACKLIGHT BLINK on GPIO 5. This is the ONLY part of the display assembly
//     still reachable -- it is a real GPIO, independent of the dead expander -- so it
//     is the one thing that can be confirmed by eye. With the panel held in reset no
//     pixels can appear no matter what we draw, so "does it glow?" is the whole test.
//
//   * I2C RESCAN every cycle. Reseat the display ribbon WHILE THIS RUNS and the
//     moment the bus comes back the scan line changes and the panel is re-inited
//     automatically -- no reflash, no guessing whether the wiggle helped.
void loop()
{
    static uint32_t n = 0;
    static bool panelUp = false;

    // --- backlight: on, then off, slowly enough to be unmistakable -------------
    digitalWrite(BLIPSCOPE_BL_PIN, HIGH);
    Serial.printf("[%lu] BACKLIGHT ON   <- screen should GLOW (dark grey, not pure black)\n",
                  (unsigned long)n);
    delay(1500);
    digitalWrite(BLIPSCOPE_BL_PIN, LOW);
    Serial.printf("[%lu] backlight off  <- screen should go FULLY dark\n", (unsigned long)n);
    delay(1500);
    digitalWrite(BLIPSCOPE_BL_PIN, HIGH);

    // --- live bus rescan -------------------------------------------------------
    bool tca = i2cPresent(TCA_ADDR), tp = i2cPresent(0x53), imu = i2cPresent(0x6B);
    Serial.printf("[%lu] I2C: expander(0x20)=%s touch(0x53)=%s imu(0x6B)=%s\n",
                  (unsigned long)n++, tca ? "YES" : "--", tp ? "YES" : "--", imu ? "YES" : "--");

    if (!tca) {
        panelUp = false;
        return; // bus still down: nothing else is worth attempting
    }

    // Bus is back. Pulse the panel out of reset and re-init once, then draw.
    if (!panelUp) {
        Serial.println("  *** EXPANDER IS BACK -- pulsing LCD_RST and re-initing panel ***");
        i2cWriteReg(TCA_ADDR, TCA_CONFIG_REG, TCA_CONFIG);
        i2cWriteReg(TCA_ADDR, TCA_OUTPUT_REG, EXIO_TP_RST | EXIO_LCD_RST | EXIO_SD_CS);
        delay(10);
        i2cWriteReg(TCA_ADDR, TCA_OUTPUT_REG, EXIO_SD_CS);
        delay(20);
        i2cWriteReg(TCA_ADDR, TCA_OUTPUT_REG, EXIO_TP_RST | EXIO_LCD_RST | EXIO_SD_CS);
        delay(120);
        Serial.printf("  tft.init = %d  %dx%d\n", (int)tft.init(), tft.width(), tft.height());
        tft.setBrightness(255);
        panelUp = true;
    }

    struct { const char* name; uint16_t c; } steps[] = {
        { "RED", TFT_RED }, { "GREEN", TFT_GREEN }, { "BLUE", TFT_BLUE }, { "WHITE", TFT_WHITE },
    };
    for (auto& s : steps) {
        tft.fillScreen(s.c);
        // A centred bar too, so a partially-addressed panel (stuck window/offset ->
        // a band) is distinguishable from a fully dead one (flat wash).
        tft.fillRect(tft.width() / 4, tft.height() / 2 - 20, tft.width() / 2, 40,
                     (s.c == TFT_WHITE) ? TFT_BLACK : TFT_WHITE);
        Serial.printf("  fill %s\n", s.name);
        delay(800);
    }
}
