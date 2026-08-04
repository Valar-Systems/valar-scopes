// gametest_main.cpp -- bench harness for the three HARDWARE questions that gate
// the Missileer game design (docs/missileer-game-design.md §13, "Remaining --
// build tasks"). Defines its own setup()/loop(); only [env:gametest-s3-128]
// re-includes it, exactly like hwtest_main.cpp.
//
// Board: the S3 1.28" Kit (GC9A01 240x240 + CST816D, variant s3_128.h) -- the
// default SKU, and the board Missileer ships on.
//
// It answers three numbers and nothing else. No cloud calls, no game logic, no
// product code touched:
//
//   1. HOLD TEST   -> dropout-free % of a 10 s static hold (gates the deputy's
//                     two-hands crew layer: if the panel cannot hold a finger
//                     for 10 s, the mechanic does not exist)
//   2. MULTITOUCH  -> max simultaneous points this path reports (design assumes 1)
//   3. NTP TEST    -> a conservative clock-sync uncertainty in ms, which becomes
//                     the deviation leaderboard's scoring granularity (§4)
//
// ---------------------------------------------------------------------------
// TWO FINDINGS THAT RESHAPE TEST 1. Read before interpreting any run.
//
// (a) THE AUTO-SLEEP A/B IS INVERTED ON THIS BOARD. The brief asks for "current
//     product config" vs "auto-sleep held off". On the RETIRED C3 Kit's CST816T
//     that was the right polarity -- 0xFE (DisAutoSleep) was unreachable there,
//     sleep could not be turned off, and the wedge program's step 1 was DOA. But
//     this board is a CST816**D**, and INCOMING-INSPECTION.md records that it
//     ships with 0xFE=1 -- auto-sleep ALREADY DISABLED from the factory. So
//     "current product config" is the no-sleep arm, and running the brief's two
//     arms verbatim would compare a state against itself and report a null delta
//     that looks like a clean result.
//
//     To get a real A/B the control arm must ARM the sleep engine: write 0xFE=0.
//     That is deliberately the value INCOMING-INSPECTION calls REJECT, held only
//     for the duration of a bench run and restored after. AutoSleepTime (0xF9)
//     sits at 2 s underneath, so a 10 s static hold against a 2 s sleep timer is
//     exactly the collision worth measuring.
//
// (b) A DROPOUT MAY NOT BE SLEEP AT ALL. IrqCtl (0xFA) is 0x60 = EnTouch|EnChange,
//     and the inspection doc notes registers are only valid at INT pulses. A
//     finger that does not MOVE may generate no change interrupt, so a static
//     hold can read as "no touch" at the driver while the chip still has the
//     finger. That is a different defect with a different fix, and the two are
//     indistinguishable from getTouch() alone. So every poll logs BOTH the
//     driver's answer and the chip's own TouchNum register, and the CSV carries
//     both columns.
// ---------------------------------------------------------------------------
//
// Build/flash (pin the port -- a second board is usually attached):
//   pio run -e gametest-s3-128 -t upload --upload-port COM119 -t monitor
//
// Every line is CSV, tagged for bench-capture: HOLD, / TOUCH, / NTP, / REG,.

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>

#include "LGFX.h"
#include "variants/Variant.h"

namespace {

LGFX tft;

constexpr int      TP_PORT = BLIPSCOPE_TOUCH_I2C_PORT;
constexpr int      TP_ADDR = BLIPSCOPE_TOUCH_I2C_ADDR;
constexpr uint32_t TP_FREQ = BLIPSCOPE_TOUCH_FREQ;
constexpr int      C       = variant::SCREEN_SIZE / 2;

// CST816 registers used here. 0x02 is the live touch count; the config space is
// documented in INCOMING-INSPECTION.md.
constexpr uint8_t REG_TOUCHNUM     = 0x02;
constexpr uint8_t REG_CHIPID       = 0xA7;
constexpr uint8_t REG_DISAUTOSLEEP = 0xFE;
constexpr uint8_t REG_AUTOSLEEPTIME= 0xF9;
constexpr uint8_t REG_IRQCTL       = 0xFA;

int ReadReg(uint8_t reg)
{
    const auto r = lgfx::i2c::readRegister8(TP_PORT, TP_ADDR, reg, TP_FREQ);
    return r.has_value() ? (int)r.value() : -1; // -1 = NACK
}
bool WriteReg(uint8_t reg, uint8_t val)
{
    return lgfx::i2c::writeRegister8(TP_PORT, TP_ADDR, reg, val, 0, TP_FREQ).has_value();
}

// ---- screens ---------------------------------------------------------------
enum class Screen : uint8_t { Hold, Multi, Ntp, COUNT };
Screen screen = Screen::Hold;
bool needsRepaint = true;

// ---- gesture state (swipe to advance; same shape as EamManager::HandleTouch) -
bool wasTouched = false;
int  pressX = 0, pressY = 0;
unsigned long pressMs = 0;
constexpr int SWIPE_MIN_PX = 40;

// ---- poll cadence ----------------------------------------------------------
// Logged because sampling is the obvious confound: a 60 ms poll cannot resolve a
// 40 ms dropout, and without this number a clean result is unfalsifiable.
unsigned long lastPollMs = 0;
unsigned long pollGapMaxMs = 0, pollGapSumMs = 0, pollCount = 0;

// ---- HOLD TEST -------------------------------------------------------------
// A DROPOUT is a release the operator did not make: contact returns within
// REJOIN_MS, so the finger never actually left. A real lift ends the run.
constexpr unsigned long HOLD_TARGET_MS = 10000;
constexpr unsigned long REJOIN_MS      = 100; // "release <100 ms not user-initiated"

bool holdActive = false;
unsigned long holdStartMs = 0, holdContactMs = 0;
unsigned long lastDownMs = 0, lastUpMs = 0;
bool inDropout = false;
unsigned long runDropouts = 0, runLongestDropoutMs = 0;

// accumulated across runs, per arm
struct ArmStats {
    unsigned long runs = 0, cleanRuns = 0, dropouts = 0, longestMs = 0;
    unsigned long hist[5] = {0,0,0,0,0}; // <10, <25, <50, <100, >=100 ms
};
ArmStats armFactory;  // 0xFE = 1 (auto-sleep DISABLED -- this board's factory state)
ArmStats armSleepOn;  // 0xFE = 0 (auto-sleep ARMED -- deliberately the reject value)
bool sleepArmed = false; // which arm is live

ArmStats& CurArm() { return sleepArmed ? armSleepOn : armFactory; }

void Bucket(ArmStats& a, unsigned long ms)
{
    if (ms < 10)       a.hist[0]++;
    else if (ms < 25)  a.hist[1]++;
    else if (ms < 50)  a.hist[2]++;
    else if (ms < 100) a.hist[3]++;
    else               a.hist[4]++;
}

// ---- MULTITOUCH ------------------------------------------------------------
int maxPointsSeen = 0;
int livePoints = 0;
int liveX = 0, liveY = 0;

// ---- NTP -------------------------------------------------------------------
volatile bool  ntpSyncFlag = false;
unsigned long  bootMs = 0;
unsigned long  firstSyncMs = 0;      // millis() at first sync (0 = not yet)
unsigned long  syncCount = 0;
int64_t        lastSyncEpochUs = 0;
unsigned long  lastSyncMillis = 0;
long           lastAdjustUs = 0;
long           worstAdjustUs = 0;

void OnNtpSync(struct timeval* /*tv*/) { ntpSyncFlag = true; }

// The honest uncertainty: the largest correction NTP has had to apply since the
// previous sync. Predicting forward from the last sync with the local clock and
// measuring how wrong that prediction was is exactly the error a device would
// carry into a deviation score. Reported pessimistically (worst, not mean), and
// floored by the poll cadence -- we cannot claim tighter than we can sample.
long UncertaintyMs()
{
    const long adjMs = (worstAdjustUs < 0 ? -worstAdjustUs : worstAdjustUs) / 1000;
    const long pollMs = (long)pollGapMaxMs;
    return adjMs > pollMs ? adjMs : pollMs;
}

// ---- drawing ---------------------------------------------------------------
void Banner(const char* title)
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(textdatum_t::top_center);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString(title, C, 6);
}

void DrawHold()
{
    Banner(sleepArmed ? "HOLD  [sleep ARMED]" : "HOLD  [factory]");

    // The switch arc the deputy holds.
    const int r = C - 22;
    tft.drawArc(C, C, r, r - 8, 200, 340, holdActive ? TFT_GREEN : TFT_DARKGREY);

    tft.setTextDatum(textdatum_t::middle_center);
    if (holdActive) {
        const unsigned long held = millis() - holdStartMs;
        const bool ok = held >= HOLD_TARGET_MS;
        tft.setTextColor(inDropout ? TFT_RED : (ok ? TFT_GREEN : TFT_YELLOW), TFT_BLACK);
        tft.setTextSize(4);
        tft.drawString(inDropout ? "DROPPED" : "HELD", C, C - 18);
        tft.setTextSize(3);
        char b[16];
        snprintf(b, sizeof(b), "%lu.%lus", held / 1000, (held % 1000) / 100);
        tft.drawString(b, C, C + 22);
        tft.setTextSize(2);
        tft.setTextColor(TFT_ORANGE, TFT_BLACK);
        snprintf(b, sizeof(b), "drops %lu", runDropouts);
        tft.drawString(b, C, C + 56);
    } else {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(2);
        tft.drawString("PRESS &", C, C - 30);
        tft.drawString("HOLD 10s", C, C - 6);
        const ArmStats& a = CurArm();
        tft.setTextSize(1);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        char b[40];
        snprintf(b, sizeof(b), "runs %lu  clean %lu%%", a.runs,
                 a.runs ? (a.cleanRuns * 100 / a.runs) : 0);
        tft.drawString(b, C, C + 34);
        snprintf(b, sizeof(b), "swipe: next   long-press: arm A/B");
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawString(b, C, variant::SCREEN_SIZE - 14);
    }
}

void DrawMulti()
{
    Banner("MULTITOUCH");
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString("MAX POINTS SEEN", C, C - 44);
    tft.setTextSize(6);
    tft.setTextColor(maxPointsSeen > 1 ? TFT_ORANGE : TFT_GREEN, TFT_BLACK);
    char b[24];
    snprintf(b, sizeof(b), "%d", maxPointsSeen);
    tft.drawString(b, C, C - 6);
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(b, sizeof(b), "live %d", livePoints);
    tft.drawString(b, C, C + 34);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(b, sizeof(b), "%d,%d", liveX, liveY);
    tft.drawString(b, C, C + 56);
}

void DrawNtp()
{
    Banner("NTP SYNC");
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("UNCERTAINTY (ms)", C, C - 48);
    tft.setTextSize(6);
    char b[24];
    if (firstSyncMs == 0) {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawString("--", C, C - 8);
    } else {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        snprintf(b, sizeof(b), "%ld", UncertaintyMs());
        tft.drawString(b, C, C - 8);
    }
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(b, sizeof(b), "first sync %lums", firstSyncMs ? firstSyncMs - bootMs : 0);
    tft.drawString(b, C, C + 34);
    snprintf(b, sizeof(b), "syncs %lu  worst adj %ldms", syncCount, worstAdjustUs / 1000);
    tft.drawString(b, C, C + 48);
    snprintf(b, sizeof(b), "wifi %s", WiFi.status() == WL_CONNECTED ? "up" : "DOWN");
    tft.setTextColor(WiFi.status() == WL_CONNECTED ? TFT_DARKGREY : TFT_RED, TFT_BLACK);
    tft.drawString(b, C, C + 62);
}

// ---- arm switching ---------------------------------------------------------
void ApplyArm()
{
    // Write, read back, and REPORT. Never assume the write landed: on the C3's
    // CST816T this register NACKs entirely, and a batch that ships 0xFE=0 is the
    // documented reject condition. If the readback disagrees with intent, the run
    // is not the arm it claims to be and the CSV must say so.
    const uint8_t want = sleepArmed ? 0 : 1;
    const int pre = ReadReg(REG_DISAUTOSLEEP);
    const bool wrote = WriteReg(REG_DISAUTOSLEEP, want);
    const int back = ReadReg(REG_DISAUTOSLEEP);
    Serial.printf("REG,arm,%s,pre,%d,write,%s,readback,%d,intended,%u,honoured,%d\n",
                  sleepArmed ? "sleep_armed" : "factory_nosleep",
                  pre, wrote ? "ok" : "NACK", back, want,
                  (back == (int)want) ? 1 : 0);
    if (back != (int)want)
        Serial.println("REG,WARNING,arm not honoured -- treat this arm's numbers as UNKNOWN state");
}

void ReportArm(const char* name, const ArmStats& a)
{
    Serial.printf("HOLD,summary,%s,runs,%lu,clean,%lu,cleanpct,%lu,dropouts,%lu,longest_ms,%lu,"
                  "hist_lt10,%lu,hist_lt25,%lu,hist_lt50,%lu,hist_lt100,%lu,hist_ge100,%lu\n",
                  name, a.runs, a.cleanRuns,
                  a.runs ? (a.cleanRuns * 100 / a.runs) : 0,
                  a.dropouts, a.longestMs,
                  a.hist[0], a.hist[1], a.hist[2], a.hist[3], a.hist[4]);
}

void ReportBoth()
{
    ReportArm("factory_nosleep", armFactory);
    ReportArm("sleep_armed", armSleepOn);
    // The delta is the whole point of running two arms: it is the auto-sleep tax.
    if (armFactory.runs && armSleepOn.runs) {
        const long f = (long)(armFactory.cleanRuns * 100 / armFactory.runs);
        const long s = (long)(armSleepOn.cleanRuns * 100 / armSleepOn.runs);
        Serial.printf("HOLD,delta,cleanpct_factory,%ld,cleanpct_sleeparmed,%ld,autosleep_tax_pts,%ld\n",
                      f, s, f - s);
    }
}

// ---- hold-test state machine ----------------------------------------------
void HoldTick(bool touched, unsigned long now)
{
    if (touched && !holdActive && !inDropout) {
        holdActive = true;
        holdStartMs = now;
        holdContactMs = 0;
        lastDownMs = now;
        runDropouts = 0;
        runLongestDropoutMs = 0;
        Serial.printf("HOLD,run_start,%lu,arm,%s\n", now, sleepArmed ? "sleep_armed" : "factory_nosleep");
        needsRepaint = true;
        return;
    }
    if (!holdActive) return;

    if (touched && inDropout) {
        // Contact returned inside the rejoin window -> the finger never left.
        const unsigned long gap = now - lastUpMs;
        inDropout = false;
        lastDownMs = now;
        runDropouts++;
        if (gap > runLongestDropoutMs) runLongestDropoutMs = gap;
        ArmStats& a = CurArm();
        a.dropouts++;
        if (gap > a.longestMs) a.longestMs = gap;
        Bucket(a, gap);
        Serial.printf("HOLD,dropout,%lu,gap_ms,%lu,run_drops,%lu\n", now, gap, runDropouts);
        needsRepaint = true;
    } else if (!touched && !inDropout) {
        inDropout = true;
        lastUpMs = now;
        holdContactMs += now - lastDownMs;
        Serial.printf("HOLD,up,%lu\n", now);
        needsRepaint = true;
    } else if (!touched && inDropout && (now - lastUpMs) > REJOIN_MS) {
        // Stayed up past the rejoin window: the operator lifted. End the run.
        const unsigned long total = lastUpMs - holdStartMs;
        ArmStats& a = CurArm();
        a.runs++;
        if (runDropouts == 0 && total >= HOLD_TARGET_MS) a.cleanRuns++;
        Serial.printf("HOLD,run_end,%lu,total_ms,%lu,contact_ms,%lu,dropouts,%lu,longest_ms,%lu,"
                      "target_met,%d,poll_avg_ms,%lu,poll_max_ms,%lu\n",
                      now, total, holdContactMs, runDropouts, runLongestDropoutMs,
                      total >= HOLD_TARGET_MS ? 1 : 0,
                      pollCount ? pollGapSumMs / pollCount : 0, pollGapMaxMs);
        ReportBoth();
        holdActive = false;
        inDropout = false;
        needsRepaint = true;
    }
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(300);
    bootMs = millis();

    Serial.println();
    Serial.printf("[build] env=%s  Missileer game bench harness\n",
#ifdef BUILD_ENV
                  BUILD_ENV
#else
                  "gametest"
#endif
    );
    Serial.println("[build] confirm this matches the image you intended BEFORE diagnosing any symptom");

    variant::BoardPreInit(); // no-op on this variant; kept so the sequence matches the product
    const bool panelOk = tft.init();
    tft.setRotation(0);
    tft.setBrightness(255);
    Serial.printf("[disp] tft.init=%d %dx%d\n", panelOk ? 1 : 0, tft.width(), tft.height());

    // Touch identity + the config space this test's interpretation depends on.
    Serial.printf("REG,identity,chipid,0x%02X,touchnum,%d\n", ReadReg(REG_CHIPID), ReadReg(REG_TOUCHNUM));
    Serial.printf("REG,config,disautosleep_0xFE,%d,autosleeptime_0xF9,%d,irqctl_0xFA,0x%02X\n",
                  ReadReg(REG_DISAUTOSLEEP), ReadReg(REG_AUTOSLEEPTIME), ReadReg(REG_IRQCTL));
    Serial.println("REG,note,0xFE=1 means auto-sleep DISABLED (this board's factory state)");

    sleepArmed = false;
    ApplyArm();

    // WiFi from stored credentials (the board has joined before). NTP only; no
    // cloud calls of any kind are made by this firmware.
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    Serial.println("NTP,wifi,connecting");
    const unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(100);
    Serial.printf("NTP,wifi,%s,ms,%lu\n",
                  WiFi.status() == WL_CONNECTED ? "connected" : "FAILED", millis() - t0);

    sntp_set_time_sync_notification_cb(OnNtpSync);
    configTime(0, 0, "pool.ntp.org"); // same call main.cpp makes -- measure what we ship

    Serial.println("HOLD,csv,event,ms,detail...");
    Serial.println("TOUCH,csv,ms,driver_touched,chip_touchnum,x,y");
    Serial.println("NTP,csv,event,...");
    lastPollMs = millis();
}

void loop()
{
    const unsigned long now = millis();

    // ---- poll cadence (the sampling-confound control) ----
    const unsigned long gap = now - lastPollMs;
    lastPollMs = now;
    if (gap > pollGapMaxMs) pollGapMaxMs = gap;
    pollGapSumMs += gap;
    pollCount++;

    // ---- touch: driver answer AND the chip's own count, every poll ----
    int32_t tx = 0, ty = 0;
    const bool touched = tft.getTouch(&tx, &ty);
    const int chipN = ReadReg(REG_TOUCHNUM);
    if (chipN > maxPointsSeen) maxPointsSeen = chipN;
    livePoints = chipN < 0 ? 0 : chipN;
    if (touched) { liveX = tx; liveY = ty; }

    // Disagreement between the driver and the chip is the (b) finding above: a
    // static finger that generates no change interrupt reads as no-touch at the
    // driver while the chip still holds the point. Logged only on transitions and
    // disagreements, so the CSV stays readable at a ~5 ms poll.
    static bool lastTouched = false;
    static int lastChipN = -2;
    if (touched != lastTouched || chipN != lastChipN) {
        Serial.printf("TOUCH,%lu,%d,%d,%d,%d\n", now, touched ? 1 : 0, chipN, (int)tx, (int)ty);
        lastTouched = touched;
        lastChipN = chipN;
    }

    // ---- gesture: swipe advances the screen; long-press flips the A/B arm ----
    if (touched && !wasTouched) {
        pressX = tx; pressY = ty; pressMs = now;
    } else if (!touched && wasTouched) {
        const int dx = tx - pressX, dy = ty - pressY;
        const unsigned long held = now - pressMs;
        if (abs(dx) > SWIPE_MIN_PX || abs(dy) > SWIPE_MIN_PX) {
            screen = (Screen)(((int)screen + 1) % (int)Screen::COUNT);
            needsRepaint = true;
            Serial.printf("TOUCH,screen,%d\n", (int)screen);
        } else if (held > 1500 && screen == Screen::Hold && !holdActive) {
            sleepArmed = !sleepArmed;
            ApplyArm();
            needsRepaint = true;
        }
    }
    wasTouched = touched;

    if (screen == Screen::Hold) HoldTick(touched, now);

    // ---- NTP sampling ----
    if (ntpSyncFlag) {
        ntpSyncFlag = false;
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        const int64_t nowUs = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
        syncCount++;
        if (firstSyncMs == 0) {
            firstSyncMs = now;
            Serial.printf("NTP,first_sync,ms_from_boot,%lu\n", now - bootMs);
        } else {
            // How wrong the local clock had drifted since the previous sync: the
            // correction NTP just applied. This is the error a device would carry
            // into a deviation score, so it is the honest uncertainty figure.
            const int64_t predicted = lastSyncEpochUs + (int64_t)(now - lastSyncMillis) * 1000LL;
            lastAdjustUs = (long)(nowUs - predicted);
            if (labs(lastAdjustUs) > labs(worstAdjustUs)) worstAdjustUs = lastAdjustUs;
            Serial.printf("NTP,sync,%lu,n,%lu,adjust_us,%ld,worst_us,%ld,uncertainty_ms,%ld\n",
                          now, syncCount, lastAdjustUs, worstAdjustUs, UncertaintyMs());
        }
        lastSyncEpochUs = nowUs;
        lastSyncMillis = now;
        needsRepaint = true;
    }

    // ---- periodic heartbeat so a long NTP soak has a trail ----
    static unsigned long lastBeat = 0;
    if (now - lastBeat >= 60000) {
        lastBeat = now;
        Serial.printf("NTP,beat,%lu,syncs,%lu,worst_us,%ld,uncertainty_ms,%ld,"
                      "poll_avg_ms,%lu,poll_max_ms,%lu\n",
                      now, syncCount, worstAdjustUs, UncertaintyMs(),
                      pollCount ? pollGapSumMs / pollCount : 0, pollGapMaxMs);
        needsRepaint = true;
    }

    // ---- repaint (throttled; the hold HUD needs to read as live on video) ----
    static unsigned long lastPaint = 0;
    if (needsRepaint || now - lastPaint >= 100) {
        lastPaint = now;
        needsRepaint = false;
        switch (screen) {
            case Screen::Hold:  DrawHold();  break;
            case Screen::Multi: DrawMulti(); break;
            case Screen::Ntp:   DrawNtp();   break;
            default: break;
        }
    }

    delay(5); // ~5 ms poll: fine enough to resolve the <10 ms dropout bucket
}
