// Incoming-inspection probe for the "jxl" ESP32-S3R8 1.28" round CTP board (2026-07-11).
//
// Answers, in order, the questions that gate everything else about this board:
//   1. What actually ACKs on the touch I2C bus (census, with known-ID annotations:
//      CST816 family 0x15, PCF85063 RTC 0x51, QMI8658 IMU 0x6A/0x6B)?
//   2. What CST816 revision is it (chip id 0xA7, project id 0xA8, fw 0xA9)?
//   3. Is register 0xFE (DisAutoSleep) reachable -- read AND write -- in each access
//      window: right after reset, while awake, after auto-sleep, and DURING a touch?
//      The C3 kit's CST816T NACKs 0xFE in every window we could produce (the step-1
//      DOA finding); whether this board's part does too decides if the no-autosleep
//      config is even a candidate here.
//
// Methodology note: uses the SAME lgfx::i2c call path as src/TouchWatchdog.cpp
// (readRegister8/writeRegister8), so ACK/NACK results compare 1:1 with the C3 ledger.
//
// Pins default to the Waveshare ESP32-S3-Touch-LCD-1.28 family map (a common clone
// ancestor); override via build flags if the jxl vendor doc differs:
//   -DPROBE_TP_SDA=.. -DPROBE_TP_SCL=.. -DPROBE_TP_RST=.. -DPROBE_TP_INT=..
// Output is [probe]-tagged over native USB-CDC at 115200. Touch the panel while it
// runs: the INT edge triggers the "during-touch" 0xFE attempt (the one window the C3
// bench never exercised, since synthetic gestures bypass the chip).

#ifdef PROBE_SKETCH

#include <Arduino.h>
#include <LovyanGFX.hpp>

#ifndef PROBE_TP_SDA
#define PROBE_TP_SDA 6
#endif
#ifndef PROBE_TP_SCL
#define PROBE_TP_SCL 7
#endif
#ifndef PROBE_TP_RST
#define PROBE_TP_RST 13
#endif
#ifndef PROBE_TP_INT
#define PROBE_TP_INT 5
#endif
#ifndef PROBE_TP_ADDR
#define PROBE_TP_ADDR 0x15
#endif

namespace {

    constexpr int PORT = 0;
    constexpr uint32_t FREQ = 400000;
    constexpr uint32_t SCAN_FREQ = 100000; // gentler for the census on an unknown bus

    volatile bool intSeen = false;
    void IRAM_ATTR OnTouchInt() { intSeen = true; }

    const char* Annotate(uint8_t addr)
    {
        switch (addr) {
        case 0x15: return " <- CST816 family CTP";
        case 0x51: return " <- PCF85063 RTC";
        case 0x6A: case 0x6B: return " <- QMI8658 IMU";
        default:   return "";
        }
    }

    void Scan()
    {
        Serial.print("[probe] census:");
        int n = 0;
        for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
            uint8_t dummy = 0;
            if (lgfx::i2c::transactionWrite(PORT, addr, &dummy, 0, SCAN_FREQ).has_value()) {
                Serial.printf(" 0x%02X%s", addr, Annotate(addr));
                n++;
            }
        }
        Serial.printf("  (%d device%s)\n", n, n == 1 ? "" : "s");
    }

    void ReadReg(const char* name, uint8_t reg)
    {
        const auto r = lgfx::i2c::readRegister8(PORT, PROBE_TP_ADDR, reg, FREQ);
        if (r.has_value())
            Serial.printf("[probe]   %s (0x%02X) = 0x%02X\n", name, reg, r.value());
        else
            Serial.printf("[probe]   %s (0x%02X) = NACK\n", name, reg);
    }

    // The headline experiment, printed in the C3 finding's exact vocabulary.
    void Try0xFE(const char* window)
    {
        const auto pre = lgfx::i2c::readRegister8(PORT, PROBE_TP_ADDR, 0xFE, FREQ);
        const bool wrote = lgfx::i2c::writeRegister8(PORT, PROBE_TP_ADDR, 0xFE, 0x01, 0, FREQ).has_value();
        const auto back = lgfx::i2c::readRegister8(PORT, PROBE_TP_ADDR, 0xFE, FREQ);
        Serial.printf("[probe] 0xFE @%s: pre=%s write=%s readback=%s\n",
                      window,
                      pre.has_value() ? String(pre.value(), HEX).c_str() : "NACK",
                      wrote ? "ok" : "NACK",
                      back.has_value() ? String(back.value(), HEX).c_str() : "NACK");
    }

    void Census(const char* window)
    {
        Serial.printf("[probe] ---- census @%s ----\n", window);
        Scan();
        ReadReg("chip-id ", 0xA7); // C3 kit's CST816T answers 0xB6
        ReadReg("proj-id ", 0xA8);
        ReadReg("fw-ver  ", 0xA9);
        Try0xFE(window);
    }

    void PulseReset()
    {
        pinMode(PROBE_TP_RST, OUTPUT);
        digitalWrite(PROBE_TP_RST, LOW);
        delay(10);
        digitalWrite(PROBE_TP_RST, HIGH);
    }

    // Post-reset schedule bracketing the CST816 auto-sleep onset (~2-5 s).
    constexpr unsigned long CENSUS_AT_MS[] = { 450, 1000, 2500, 6000 };
    constexpr size_t CENSUS_STEPS = sizeof(CENSUS_AT_MS) / sizeof(CENSUS_AT_MS[0]);
    size_t censusStep = 0;
    unsigned long resetAtMs = 0;
    unsigned long lastPeriodicMs = 0;

} // namespace

#ifdef PROBE_SWEEP
// ---- pin-pair sweep mode (-DPROBE_SWEEP): find the touch bus empirically ----
// The fixed-map census came back empty on this board, so sweep every plausible
// SDA/SCL ordered pair for an ACK at the CST816 address. All candidate pins are
// weak-pulled high first (releases a passively-held TP_RST without ever driving
// push-pull against an unknown net). The CST816 auto-sleeps within seconds, so
// THE PANEL MUST BE TOUCHED CONTINUOUSLY while the sweep runs -- a finger keeps
// the chip awake. Excluded pins: 19/20 (USB D-/D+ -- we are talking over them),
// 26-37 (flash + octal PSRAM on the N16R8), 43/44 (UART0), 0/3/45/46 (straps).
namespace {
    constexpr int SWEEP_PINS[] = { 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                   16, 17, 18, 21, 38, 39, 40, 41, 42, 47, 48 };
    constexpr size_t SWEEP_N = sizeof(SWEEP_PINS) / sizeof(SWEEP_PINS[0]);

    // Candidate CTP addresses across families the "note the version" drift could
    // reach: CST816/CST826 0x15, FT3168/FT6x36 0x38, GT911 0x5D/0x14, CST328 0x1A.
    constexpr uint8_t CTP_ADDRS[] = { 0x15, 0x38, 0x5D, 0x14, 0x1A };

    int PairHasCtp(int sda, int scl) // returns the ACKing address, or -1
    {
        if (!lgfx::i2c::init(PORT, sda, scl).has_value())
            return -1;
        int hit = -1;
        for (uint8_t addr : CTP_ADDRS) {
            uint8_t dummy = 0;
            if (lgfx::i2c::transactionWrite(PORT, addr, &dummy, 0, SCAN_FREQ).has_value()) {
                hit = addr;
                break;
            }
        }
        lgfx::i2c::release(PORT);
        return hit;
    }

    void SweepRound(unsigned round)
    {
        for (size_t i = 0; i < SWEEP_N; ++i) {
            for (size_t j = 0; j < SWEEP_N; ++j) {
                if (i == j) continue;
                const int sda = SWEEP_PINS[i], scl = SWEEP_PINS[j];
                const int addr = PairHasCtp(sda, scl);
                if (addr < 0)
                    continue;
                Serial.printf("[probe] *** HIT: 0x%02X ACKs on SDA=%d SCL=%d ***\n", addr, sda, scl);
                lgfx::i2c::init(PORT, sda, scl);
                Serial.println("[probe] full census + identity + 0xFE on the discovered bus:");
                Census("sweep-hit");
                lgfx::i2c::release(PORT);
            }
        }
        Serial.printf("[probe] sweep round %u done (%u ordered pairs)\n", round, (unsigned)(SWEEP_N * (SWEEP_N - 1)));
    }
}
#endif // PROBE_SWEEP

void setup()
{
    Serial.begin(115200);
    const unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 4000) delay(10); // native USB-CDC settle

    Serial.println("[probe] ==================================================");
    Serial.println("[probe] jxl S3R8 1.28\" incoming inspection: I2C census + CST816 0xFE");
    Serial.printf("[probe] pins: SDA=%d SCL=%d TP_RST=%d TP_INT=%d addr=0x%02X (override with -DPROBE_TP_*)\n",
                  PROBE_TP_SDA, PROBE_TP_SCL, PROBE_TP_RST, PROBE_TP_INT, PROBE_TP_ADDR);
    Serial.println("[probe] touch the panel at any time: INT triggers the during-touch 0xFE window");
    Serial.println("[probe] ==================================================");

#ifdef PROBE_SWEEP
    // Weak-high every candidate first: releases a passively pulled-down TP_RST
    // (weak pulls can't fight any real driver, so this is safe on unknown nets).
    for (size_t i = 0; i < SWEEP_N; ++i)
        pinMode(SWEEP_PINS[i], INPUT_PULLUP);
    Serial.println("[probe] SWEEP MODE: keep a finger dragging on the panel continuously!");
    delay(1500); // let the chip boot out of any released reset
    return;
#endif

    if (!lgfx::i2c::init(PORT, PROBE_TP_SDA, PROBE_TP_SCL).has_value()) {
        Serial.println("[probe] i2c init FAILED -- wrong pins? override and reflash");
        return;
    }

    pinMode(PROBE_TP_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PROBE_TP_INT), OnTouchInt, FALLING);

    PulseReset();
    resetAtMs = millis();
    censusStep = 0;
}

void loop()
{
#ifdef PROBE_SWEEP
    static unsigned round = 0;
    SweepRound(++round);
    delay(500);
    return;
#endif

    const unsigned long now = millis();

    // Scheduled post-reset windows (450 ms boot wait ... past auto-sleep onset).
    if (censusStep < CENSUS_STEPS && now - resetAtMs >= CENSUS_AT_MS[censusStep]) {
        char tag[24];
        snprintf(tag, sizeof(tag), "reset+%lums", CENSUS_AT_MS[censusStep]);
        Census(tag);
        censusStep++;
        if (censusStep == CENSUS_STEPS)
            Serial.println("[probe] schedule done; periodic census every 10 s from here");
    }

    // The during-touch window: the chip is provably in its report/active state.
    if (intSeen) {
        intSeen = false;
        Census("during-touch");
    }

    if (censusStep == CENSUS_STEPS && now - lastPeriodicMs >= 10000) {
        lastPeriodicMs = now;
        Census("periodic");
    }

    delay(5);
}

#endif // PROBE_SKETCH
