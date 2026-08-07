// Boot-hold-to-forget probe for the jxl S3-128 (CST816T) -- issue #173.
//
// The question: with a finger ALREADY on the glass at power-on, why does the CST816
// report zero touches for the whole 1.2 s detect window (`[wifi-reset] boot touch
// window: driver=1 polls=60 touched=0`) when the same finger, applied a second later,
// works perfectly?
//
// What this probe is NOT testing, because reading the driver already settled it:
//   * INT gating is NOT the cause. Touch_CSTxxx.cpp gates on the INT level only inside
//     `if (diff_msec < 10 && _wait_cycle)`; BootHoldToForget polls at delay(20), so
//     diff_msec >= 10 and the gate is bypassed. All 60 polls performed a real I2C read
//     of register 0x02 and the chip answered "0 touches" 60 times. Panel_Device::getTouch
//     is a bare passthrough to getTouchRaw (no debounce, no multi-sample), so there is
//     no driver-layer filter to blame either. The fault is in what the CHIP believes,
//     not in how we ask it.
//   * "Pulse TP_RST so the held finger re-reports" is ALREADY IN THE SHIPPING BUILD,
//     unlabelled: Touch_CST816S::init() pulses pin_rst (low 10 ms, high, 10 ms) inside
//     tft.init(), immediately before the boot window. The reset the customer's thumb is
//     held across already happens every boot, and the boot-hold still fails. So the
//     simple form of that candidate is falsified by evidence we already had.
//
// The live hypothesis (H2): a capacitive controller calibrates its baseline at power-on.
// A finger present during that calibration is absorbed INTO the baseline, so the chip
// reads "no touch" -- correctly, by its own lights -- until the finger lifts. If true,
// pulsing TP_RST is not merely useless, it is the mechanism of the bug, and no amount of
// waiting or re-reading fixes it.
//
// So each cycle below runs four windows against one continuously-held thumb:
//   hold   -- chip as the ESP found it, no reset from us  (is it already absorbed?)
//   rst450 -- reset + our VERIFIED 450 ms boot wait       (candidate A, generous)
//   rst10  -- reset + LovyanGFX's actual 10 ms wait       (what ships today)
//   lift   -- prompt to lift and re-touch                 (CONTROL: proves the read path)
// If `lift` reports touches and the other three do not, H2 is confirmed and the boot-hold
// feature cannot be rescued by any change to how we read the chip.
//
// Raw I2C only (same lgfx::i2c call path as src/TouchWatchdog.cpp), no display: register
// reads are strictly more informative than getTouch(), which we just proved is a
// passthrough. After every reset the probe replays LovyanGFX's own _check_init() writes
// so the chip sits in exactly the state the product leaves it in.

#ifdef PROBE_SKETCH

#include <Arduino.h>
#include <LovyanGFX.hpp>

#ifndef PROBE_TP_SDA
#define PROBE_TP_SDA 8
#endif
#ifndef PROBE_TP_SCL
#define PROBE_TP_SCL 9
#endif
#ifndef PROBE_TP_RST
#define PROBE_TP_RST 0
#endif
#ifndef PROBE_TP_INT
#define PROBE_TP_INT 11
#endif
#ifndef PROBE_TP_ADDR
#define PROBE_TP_ADDR 0x15
#endif

namespace {

constexpr int      PORT = 0;
constexpr uint32_t FREQ = 400000;

int ReadReg(uint8_t reg)
{
    const auto r = lgfx::i2c::readRegister8(PORT, PROBE_TP_ADDR, reg, FREQ);
    return r.has_value() ? (int)r.value() : -1;
}

// Register 0x02 is TouchNum; 0x03..0x06 carry the X/Y nibbles+bytes. Reading the block
// in one transaction is what the driver does, so a NACK here means the same thing there.
bool ReadTouchBlock(uint8_t* out6)
{
    uint8_t reg = 0x02;
    return lgfx::i2c::transactionWriteRead(PORT, PROBE_TP_ADDR, &reg, 1, out6, 6, FREQ)
        .has_value();
}

// LovyanGFX's Touch_CST816S::init() reset, with the wait as the variable under test.
//
// TP_RST is GPIO0 -- the ESP's boot strap. Held low across an ESP reset it means
// "download mode", and driven push-pull it fights the USB bridge's IO0 line, which is
// exactly how this probe put the board into ROM download mode on its first long run.
// So the pin is only an output for the 10 ms of the pulse and is parked on the internal
// pull-up the rest of the time: high, but not fighting anyone.
void ResetPulse(uint32_t settleMs)
{
    pinMode(PROBE_TP_RST, OUTPUT);
    digitalWrite(PROBE_TP_RST, LOW);
    delay(10);
    digitalWrite(PROBE_TP_RST, HIGH);
    delayMicroseconds(50);
    pinMode(PROBE_TP_RST, INPUT_PULLUP);
    delay(settleMs);
}

// Replay Touch_CST816S::_check_init() so the chip is configured exactly as the product
// leaves it (change-detect INT, 2 ms low pulse) -- otherwise we would be measuring a
// chip in its factory INT mode and the comparison would not transfer.
void ReplayDriverInit()
{
    lgfx::i2c::writeRegister8(PORT, PROBE_TP_ADDR, 0x00, 0x00, 0, FREQ);
    uint8_t id[3] = {0};
    uint8_t reg   = 0xA7;
    lgfx::i2c::transactionWriteRead(PORT, PROBE_TP_ADDR, &reg, 1, id, 3, FREQ);
    lgfx::i2c::writeRegister8(PORT, PROBE_TP_ADDR, 0xFA, 0x20, 0, FREQ);
    lgfx::i2c::writeRegister8(PORT, PROBE_TP_ADDR, 0xED, 20, 0, FREQ);
}

// A continuous timeline instead of labelled windows.
//
// The windowed version needed the operator's actions to line up with windows they cannot
// see -- no display on this build, and the prompts go to serial. That made the first run
// ambiguous: contact turned up in two windows and I had to infer which of it was a hold
// and which was a tap. So: no windows. One character per 100 ms, forever, with resets
// marked in the stream. Whatever the finger does, the timeline shows it in place.
//
//   .  no contact in that 100 ms       #  contact reported
//   x  the chip NACKed                 R  TP_RST pulse (450 ms settle) starts here
//
// Sampling stays at 20 ms -- the cadence BootHoldToForget uses -- and five samples are
// folded into each character, so the chip is polled exactly as the product polls it.
constexpr uint32_t SAMPLE_MS   = 20;
constexpr int      PER_CHAR    = 5;    // 5 x 20 ms = 100 ms per character
constexpr int      CHARS_LINE  = 50;   // 50 x 100 ms = 5 s per line
constexpr uint32_t RESET_EVERY = 30000;

uint32_t gStart      = 0;
uint32_t gNextReset  = RESET_EVERY;
char     gLine[CHARS_LINE + 1];
int      gCol        = 0;
uint32_t gLineTouch  = 0;
int      gLastN      = -1;

void FlushLine()
{
    if (gCol == 0) return;
    gLine[gCol] = '\0';
    Serial.printf("[probe] t=%3lus |%-*s| touched=%lu\n",
                  (unsigned long)((millis() - gStart) / 1000), CHARS_LINE, gLine,
                  (unsigned long)gLineTouch);
    gCol       = 0;
    gLineTouch = 0;
}

void PutChar(char c)
{
    gLine[gCol++] = c;
    if (gCol >= CHARS_LINE) FlushLine();
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(400);

    Serial.println();
    Serial.println("[probe] ============================================================");
    Serial.println("[probe] BOOT-HOLD PROBE v2 (#173) -- CST816T on jxl S3-128");
    Serial.printf("[probe] SDA=%d SCL=%d RST=%d INT=%d addr=0x%02X\n",
                  PROBE_TP_SDA, PROBE_TP_SCL, PROBE_TP_INT, PROBE_TP_RST, PROBE_TP_ADDR);
    Serial.println("[probe] legend: '.' no contact  '#' contact  'x' NACK  'R' TP_RST pulse");
    Serial.println("[probe] one char = 100 ms (5 polls @20 ms); one line = 5 s");
    Serial.println("[probe] ============================================================");

    if (!lgfx::i2c::init(PORT, PROBE_TP_SDA, PROBE_TP_SCL).has_value()) {
        Serial.println("[probe] FATAL: i2c init failed");
        return;
    }
    pinMode(PROBE_TP_INT, INPUT_PULLUP);

    Serial.printf("[probe] chip id=0x%02X proj=0x%02X fw=0x%02X | 0xFA=0x%02X 0xED=%d 0xFE=0x%02X\n",
                  ReadReg(0xA7), ReadReg(0xA8), ReadReg(0xA9),
                  ReadReg(0xFA), ReadReg(0xED), ReadReg(0xFE));

    gStart = millis();
}

void loop()
{
    // A reset every 30 s, marked in place. If a held contact only reports just after a
    // reset, that shows up as '#' clustered right of an 'R' and nowhere else. If the
    // contact fades as the chip re-baselines, that shows up as '#' decaying to '.' with
    // the finger still down. Both are visible without knowing what the operator did when.
    if (millis() - gStart >= gNextReset) {
        gNextReset += RESET_EVERY;
        FlushLine();
        Serial.printf("[probe] t=%3lus -- TP_RST pulse, 450 ms settle --\n",
                      (unsigned long)((millis() - gStart) / 1000));
        ResetPulse(450);
        ReplayDriverInit();
        gLastN = -1;
        PutChar('R');
        return;
    }

    bool sawTouch = false, sawNack = false;
    for (int i = 0; i < PER_CHAR; ++i) {
        uint8_t buf[6] = {0};
        if (!ReadTouchBlock(buf)) {
            sawNack = true;
        } else {
            const int n = buf[0] & 0x0F;
            if (n > 0) {
                sawTouch = true;
                // Coordinates only on a change, so a resting finger does not flood the
                // log -- but a finger that MOVES is exactly what we need to see, since
                // movement is the thing that appears to wake the report.
                if (n != gLastN) {
                    const int x = buf[2] | ((buf[1] & 0x0F) << 8);
                    const int y = buf[4] | ((buf[3] & 0x0F) << 8);
                    Serial.printf("[probe]   contact @t=%lums x=%d y=%d\n",
                                  (unsigned long)(millis() - gStart), x, y);
                }
            }
            gLastN = n;
        }
        delay(SAMPLE_MS);
    }
    if (sawTouch) ++gLineTouch;
    PutChar(sawNack && !sawTouch ? 'x' : (sawTouch ? '#' : '.'));
}

#endif // PROBE_SKETCH
