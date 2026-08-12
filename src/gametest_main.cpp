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

// FULL-SCREEN BACKBUFFER. The first bench build drew straight to the panel, and
// every repaint began with fillScreen -- so at the 10 Hz repaint rate the whole
// screen visibly flickered 3-5 times a second, which on a HOLD test is actively
// harmful: the thing being measured is a finger held still while the operator
// watches the HUD, and a strobing screen is both unreadable and unusable on the
// bench video this exists to produce.
//
// The product solves this with BandCanvas; the harness gets the simpler version
// because this board has PSRAM to spare -- 240x240x16bpp is 115 KB, which would
// not fit internal RAM but is nothing against 8 MB of PSRAM. Falls back to
// direct drawing if the sprite cannot be created, so a PSRAM-less board still
// runs the tests (flickering, but running beats not running on a bench tool).
LGFX_Sprite canvas(&tft);
bool haveCanvas = false;

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

// ---- gesture state ---------------------------------------------------------
// A SWIPE IS CLASSIFIED FROM MOVEMENT DURING CONTACT, NOT FROM THE RELEASE
// SAMPLE. The first bench session was lost to the obvious version of this:
// dx was computed at release from getTouch()'s output, and getTouch() zeroes
// its out-params when it returns false. So every lift produced dx = 0 - pressX
// -- a 100+ px "swipe" on a 40 px threshold -- and 33 consecutive hold runs
// voided as screen_changed with not one run_end among them. The arm-cycle
// branch sat behind that swipe test as an `else if` and was unreachable, so the
// banner never changed either and all 33 runs recorded as arm A.
//
// Two independent guards now, because either alone still fails:
//   1. track the largest displacement seen WHILE contact is live, so a zeroed
//      release sample can never enter the delta at all;
//   2. require the contact to have been SHORT. A deliberate swipe is a flick;
//      a 10 s hold that drifts 40 px across the glass is not a swipe, and
//      classifying it as one would void exactly the runs we came here to get.
bool wasTouched = false;
int  pressX = 0, pressY = 0;
int  maxMovePx = 0;              // peak |displacement| from the press point, live contact only
unsigned long pressMs = 0;
bool pressInBanner = false;      // press began on the arm chip -> cycles the arm, never a run
constexpr int  SWIPE_MIN_PX  = 40;
constexpr unsigned long SWIPE_MAX_MS = 800;   // longer than this is a hold, whatever it did
constexpr int  BANNER_H_PX   = 50;            // top band: tap here to change arm

// ---- poll cadence ----------------------------------------------------------
// Logged because sampling is the obvious confound: a 60 ms poll cannot resolve a
// 40 ms dropout, and without this number a clean result is unfalsifiable.
unsigned long lastPollMs = 0;
unsigned long pollGapMaxMs = 0, pollGapSumMs = 0, pollCount = 0;
// WHEN the worst gap happened, because an unexplained number is not a result.
// The first session reported poll_max_ms=6036 against a 5 ms loop and there was
// no way to tell a one-off startup stall from a recurring 6 s freeze -- and the
// difference between those two is the difference between "fine" and "this board
// cannot host a millisecond-scored game at all".
unsigned long pollGapMaxAtMs = 0;
// The gap of the poll we are currently in, so a dropout can report how much of
// its own measured duration was just this loop not looking. See the artifact
// note on HoldTick's dropout branch.
unsigned long curGapMs = 0;

// ---- HOLD TEST -------------------------------------------------------------
// A DROPOUT is a release the operator did not make: contact returns within
// REJOIN_MS, so the finger never actually left. A real lift ends the run.
constexpr unsigned long HOLD_TARGET_MS = 10000;
constexpr unsigned long REJOIN_MS      = 100; // "release <100 ms not user-initiated"
// Arm C only: consecutive-NACK ceiling before the run is voided as a bus failure
// rather than scored. See the source-selection block in loop() for why an
// unbounded hold-previous-state is worse than no arm C at all.
constexpr unsigned long NACK_VOID_MS   = 100;
unsigned long busFailRuns = 0;

// KEY-WINDOW POLL INTERVAL -- the fourth number this harness produces. Scoring
// granularity is max(NTP uncertainty, INPUT latency), and the input floor that
// matters is the one inside a focused high-rate poll window (what the launch
// key-turn will actually run), NOT the idle product loop. Measured separately
// from the general poll cadence for exactly that reason: the idle figure is the
// wrong floor and would set the leaderboard bucket an order of magnitude coarse.
unsigned long keyPollGapMaxMs = 0, keyPollSumMs = 0, keyPollCount = 0;

// ---------------------------------------------------------------------------
// THE PUBLISHED CLOCK FLOOR, mirrored here so the harness can notice when the
// world exceeds it.
//
// 199 ms = ceil(198.504), the worst correction in the 26.8 h session of
// 2026-08-05 0855. It is the authority for `scoring.clockFloorMs` in
// valar-eam-feed (src/game/config.ts), whose evidence is committed at
// test/fixtures/ntp-corrections-2026-08.json.
//
// MIRRORED, NOT SHARED, and that is a real seam: this is a bench harness with
// no network config, and the server value is an env-overridable runtime
// constant. If one moves and the other does not, the alarm fires at the wrong
// threshold. The fixture is the tie-breaker in that argument — it is the thing
// both numbers are supposed to describe.
static constexpr int CLOCK_FLOOR_MS  = 199;
static constexpr int CLOCK_RERULE_MS = 250;
unsigned long clockAlarms = 0;

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
// THREE ARMS. A and B are the sleep A/B (see the header note on why its polarity
// is inverted on this board). C exists because of finding (b): the driver and the
// chip disagree on a static contact, and the chip is the one that keeps the truth.
//
//   A  factory      0xFE=1 (sleep OFF)   contact source: the DRIVER (getTouch)
//   B  sleep armed  0xFE=0 (sleep ON)    contact source: the DRIVER
//   C  chip poll    0xFE=0 (sleep ON)    contact source: the CHIP (TouchNum, 0x02)
//
// C deliberately runs under the HOSTILE sleep config rather than the benign one.
// If direct register polling holds clean with the sleep engine armed, it holds
// clean everywhere, and that is one run instead of two. It is also the result
// that matters most: if C is clean, the deputy gesture is solved by a game-mode
// poll window that ignores INT entirely, and the sleep engine never has to be
// touched in the shipping product -- no register writes, no departure from the
// factory config the incoming-inspection gate checks for.
enum class Arm : uint8_t { FactoryNoSleep, SleepArmed, ChipPoll, COUNT };
const char* ArmName(Arm a)
{
    switch (a) {
        case Arm::FactoryNoSleep: return "A_factory_nosleep";
        case Arm::SleepArmed:     return "B_sleep_armed";
        case Arm::ChipPoll:       return "C_chip_poll";
        default:                  return "?";
    }
}
ArmStats armStats[(int)Arm::COUNT];
Arm arm = Arm::FactoryNoSleep;

// C reads the chip; A and B read the driver.
bool ArmUsesChipSource(Arm a) { return a == Arm::ChipPoll; }
// C wants the sleep engine armed (hostile); B does too; A does not.
uint8_t ArmWantsDisAutoSleep(Arm a) { return a == Arm::FactoryNoSleep ? 1 : 0; }

ArmStats& CurArm() { return armStats[(int)arm]; }

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
// Returns -1 for UNMEASURED, which is not the same as good. The first session
// read `syncs,1  worst_us,0  uncertainty_ms,6036` -- and every part of that was
// misleading: worst_us was 0 because a drift correction needs TWO syncs to
// exist at all, and the 6036 came entirely from the poll floor, so the headline
// figure was a startup stall wearing a clock number's clothes. A harness whose
// job is to produce one number must never let "no data yet" render as data.
long UncertaintyMs()
{
    if (syncCount < 2) return -1;
    const long adjMs = (worstAdjustUs < 0 ? -worstAdjustUs : worstAdjustUs) / 1000;
    const long pollMs = (long)pollGapMaxMs;
    return adjMs > pollMs ? adjMs : pollMs;
}

// ---- drawing ---------------------------------------------------------------
// One accessor so every draw call is written once and lands on the backbuffer
// when there is one, or on the panel when there is not.
inline LovyanGFX& gfx() { return haveCanvas ? static_cast<LovyanGFX&>(canvas) : static_cast<LovyanGFX&>(tft); }
void Banner(const char* title)
{
    gfx().fillScreen(TFT_BLACK);
    gfx().setTextDatum(textdatum_t::top_center);
    gfx().setTextColor(TFT_DARKGREY, TFT_BLACK);
    gfx().setTextSize(1);
    gfx().drawString(title, C, 6);
}

// Runs wanted per arm before the deltas mean anything. Shown on glass so the
// operator can see the denominator filling instead of guessing at "a bunch".
constexpr unsigned long RUNS_PER_ARM = 10;

void DrawHold()
{
    char b[44];

    // ---- arm chip: the control sits on the label it changes ----
    gfx().fillScreen(TFT_BLACK);
    gfx().setTextDatum(textdatum_t::top_center);
    gfx().setTextSize(1);
    gfx().setTextColor(TFT_BLACK, TFT_CYAN);
    snprintf(b, sizeof(b), " %s ", ArmName(arm));
    gfx().drawString(b, C, 8);
    gfx().setTextColor(TFT_DARKGREY, TFT_BLACK);
    gfx().drawString(holdActive ? "" : "tap here to change arm", C, 24);

    // ---- progress ring: fills to the 10 s target ----
    const int r = C - 8;
    const unsigned long held = holdActive ? millis() - holdStartMs : 0;
    gfx().drawArc(C, C, r, r - 6, 0, 360, 0x2104); // unfilled track
    if (holdActive) {
        const float frac = held >= HOLD_TARGET_MS ? 1.0f : (float)held / HOLD_TARGET_MS;
        const int sweep = (int)(360.0f * frac);
        if (sweep > 0)
            gfx().drawArc(C, C, r, r - 6, 0, sweep,
                          inDropout ? TFT_RED : (frac >= 1.0f ? TFT_GREEN : TFT_YELLOW));
    }

    gfx().setTextDatum(textdatum_t::middle_center);
    if (holdActive) {
        const bool ok = held >= HOLD_TARGET_MS;
        gfx().setTextColor(inDropout ? TFT_RED : (ok ? TFT_GREEN : TFT_YELLOW), TFT_BLACK);
        gfx().setTextSize(3);
        gfx().drawString(inDropout ? "DROPPED" : (ok ? "OK - LIFT" : "HOLD..."), C, C - 30);
        gfx().setTextSize(5);
        snprintf(b, sizeof(b), "%lu.%lu", held / 1000, (held % 1000) / 100);
        gfx().drawString(b, C, C + 6);
        gfx().setTextSize(1);
        gfx().setTextColor(TFT_DARKGREY, TFT_BLACK);
        gfx().drawString(ok ? "target met - lift to bank it" : "of 10.0 s - keep still", C, C + 38);
        if (runDropouts) {
            gfx().setTextColor(TFT_ORANGE, TFT_BLACK);
            snprintf(b, sizeof(b), "drops %lu", runDropouts);
            gfx().drawString(b, C, C + 54);
        }
    } else {
        // Idle: say the instruction, then the denominator, then the last result.
        gfx().setTextColor(TFT_WHITE, TFT_BLACK);
        gfx().setTextSize(2);
        gfx().drawString("PRESS &", C, C - 40);
        gfx().drawString("HOLD", C, C - 18);
        gfx().setTextSize(1);
        gfx().setTextColor(TFT_DARKGREY, TFT_BLACK);
        gfx().drawString("anywhere below the chip, 10 s", C, C + 2);

        const ArmStats& a = CurArm();
        gfx().setTextSize(2);
        const bool enough = a.runs >= RUNS_PER_ARM;
        gfx().setTextColor(enough ? TFT_GREEN : TFT_CYAN, TFT_BLACK);
        snprintf(b, sizeof(b), "%lu / %lu runs", a.runs, RUNS_PER_ARM);
        gfx().drawString(b, C, C + 26);
        gfx().setTextSize(1);
        gfx().setTextColor(TFT_CYAN, TFT_BLACK);
        snprintf(b, sizeof(b), "clean %lu%%   this arm", a.runs ? (a.cleanRuns * 100 / a.runs) : 0);
        gfx().drawString(b, C, C + 46);

        gfx().setTextColor(TFT_DARKGREY, TFT_BLACK);
        gfx().drawString("swipe = next screen", C, variant::SCREEN_SIZE - 22);
    }
}

void DrawMulti()
{
    Banner("MULTITOUCH");
    gfx().setTextDatum(textdatum_t::middle_center);
    gfx().setTextColor(TFT_WHITE, TFT_BLACK);
    gfx().setTextSize(1);
    gfx().drawString("MAX POINTS SEEN", C, C - 44);
    gfx().setTextSize(6);
    gfx().setTextColor(maxPointsSeen > 1 ? TFT_ORANGE : TFT_GREEN, TFT_BLACK);
    char b[24];
    snprintf(b, sizeof(b), "%d", maxPointsSeen);
    gfx().drawString(b, C, C - 6);
    gfx().setTextSize(2);
    gfx().setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(b, sizeof(b), "live %d", livePoints);
    gfx().drawString(b, C, C + 34);
    gfx().setTextSize(1);
    gfx().setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(b, sizeof(b), "%d,%d", liveX, liveY);
    gfx().drawString(b, C, C + 56);
}

void DrawNtp()
{
    Banner("NTP SYNC");
    gfx().setTextDatum(textdatum_t::middle_center);
    gfx().setTextSize(1);
    gfx().setTextColor(TFT_WHITE, TFT_BLACK);
    gfx().drawString("UNCERTAINTY (ms)", C, C - 48);
    gfx().setTextSize(6);
    char b[24];
    if (UncertaintyMs() < 0) {
        gfx().setTextColor(TFT_DARKGREY, TFT_BLACK);
        gfx().drawString("--", C, C - 8);
    } else {
        gfx().setTextColor(TFT_GREEN, TFT_BLACK);
        snprintf(b, sizeof(b), "%ld", UncertaintyMs());
        gfx().drawString(b, C, C - 8);
    }
    gfx().setTextSize(1);
    gfx().setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(b, sizeof(b), "first sync %lums", firstSyncMs ? firstSyncMs - bootMs : 0);
    gfx().drawString(b, C, C + 34);
    snprintf(b, sizeof(b), "syncs %lu  worst adj %ldms", syncCount, worstAdjustUs / 1000);
    gfx().drawString(b, C, C + 48);
    snprintf(b, sizeof(b), "wifi %s", WiFi.status() == WL_CONNECTED ? "up" : "DOWN");
    gfx().setTextColor(WiFi.status() == WL_CONNECTED ? TFT_DARKGREY : TFT_RED, TFT_BLACK);
    gfx().drawString(b, C, C + 62);
}

// ---- arm switching ---------------------------------------------------------
/* ---------------------------------------------------------------------------
 * FORCED ARM SELECTION — bench only, behind a build flag.
 *
 * WHY THIS EXISTS. The arm cycles on a tap inside the top 50 px. Across the
 * 26.8 h corrected session of 2026-08-05 0855, every one of the 14 driver-
 * touched samples landed at y >= 77 and `TOUCH,screen` was 0 — so arms B and C
 * logged `runs,0` and the whole A/B/C comparison was never re-measured on the
 * fixed instrument. Waiting for a tap that organic use produced ZERO of in 26.8
 * hours is an unbounded wait, not a slow one.
 *
 * A COMMAND, NOT A UI AFFORDANCE, and the distinction is the point: making the
 * banner easier to hit would change what the harness measures about reachability
 * while pretending to change only convenience. This path cannot be reached by a
 * finger at all.
 *
 * FORCED RUNS ARE MARKED AS FORCED in the ledger (`forced,1`). A forced arm is
 * fine for measuring THE ARM; it would not be fine for measuring how often a
 * user reaches it, and the flag is what stops a later reader conflating the two.
 *
 *   pio run -e gametest-s3-128 -t upload \
 *     --build-flag="-DGAMETEST_FORCE_ARM" --build-flag="-DGAMETEST_ARM=1"
 *
 * GAMETEST_ARM: 0=A, 1=B, 2=C. With the flag set, serial 'a'/'b'/'c' also
 * switches live, so one flash covers all three arms.
 * ------------------------------------------------------------------------- */
#ifdef GAMETEST_FORCE_ARM
static constexpr bool ARM_FORCED = true;
#  ifndef GAMETEST_ARM
#    define GAMETEST_ARM 0
#  endif
static_assert(GAMETEST_ARM >= 0 && GAMETEST_ARM < 3, "GAMETEST_ARM must be 0 (A), 1 (B) or 2 (C)");
#else
static constexpr bool ARM_FORCED = false;
#endif

void ApplyArm()
{
    // Write, read back, and REPORT. Never assume the write landed: on the C3's
    // CST816T this register NACKs entirely, and a batch that ships 0xFE=0 is the
    // documented reject condition. If the readback disagrees with intent, the run
    // is not the arm it claims to be and the CSV must say so.
    const uint8_t want = ArmWantsDisAutoSleep(arm);
    const int pre = ReadReg(REG_DISAUTOSLEEP);
    const bool wrote = WriteReg(REG_DISAUTOSLEEP, want);
    const int back = ReadReg(REG_DISAUTOSLEEP);
    Serial.printf("REG,arm,%s,pre,%d,write,%s,readback,%d,intended,%u,honoured,%d,forced,%d\n",
                  ArmName(arm),
                  pre, wrote ? "ok" : "NACK", back, want,
                  (back == (int)want) ? 1 : 0,
                  ARM_FORCED ? 1 : 0);
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
    for (int i = 0; i < (int)Arm::COUNT; ++i)
        ReportArm(ArmName((Arm)i), armStats[i]);
    // Two deltas, and they answer different questions:
    //
    //   A - B  the AUTO-SLEEP TAX: what the sleep engine costs a static hold.
    //   C - B  the CHIP-POLL RESCUE: what bypassing INT recovers under the SAME
    //          hostile sleep config. If this is large and C is near 100%, the
    //          deputy gesture is solved by a game-mode poll window and the sleep
    //          engine never has to be touched in the shipping product.
    auto pct = [](const ArmStats& a) -> long {
        return a.runs ? (long)(a.cleanRuns * 100 / a.runs) : -1;
    };
    const long a = pct(armStats[(int)Arm::FactoryNoSleep]);
    const long b = pct(armStats[(int)Arm::SleepArmed]);
    const long c = pct(armStats[(int)Arm::ChipPoll]);
    Serial.printf("HOLD,delta,cleanpct_A,%ld,cleanpct_B,%ld,cleanpct_C,%ld,"
                  "autosleep_tax_pts,%ld,chippoll_rescue_pts,%ld\n",
                  a, b, c,
                  (a >= 0 && b >= 0) ? a - b : 0,
                  (c >= 0 && b >= 0) ? c - b : 0);
    if (a < 0 || b < 0 || c < 0)
        Serial.println("HOLD,note,an arm with cleanpct -1 has no runs yet -- deltas involving it are meaningless");
    // Bus failures are reported as their OWN count, never folded into dropouts:
    // a wedged bus is an equipment fault, and averaging it into a touch-quality
    // number would corrupt the very figure the gate is read from.
    Serial.printf("HOLD,busfail,runs_voided,%lu\n", busFailRuns);
    // The fourth number (see the declaration): the key-window input floor.
    Serial.printf("HOLD,keywindow,avg_ms,%lu,max_ms,%lu,samples,%lu\n",
                  keyPollCount ? keyPollSumMs / keyPollCount : 0,
                  keyPollGapMaxMs, keyPollCount);
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
        Serial.printf("HOLD,run_start,%lu,arm,%s,source,%s\n", now, ArmName(arm),
                      ArmUsesChipSource(arm) ? "chip_touchnum" : "driver_gettouch");
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
        // MEASURED DURATION IS AN UPPER BOUND, NOT THE OUTAGE. The gap is
        // wall-clock between observing contact lost and observing it back, so
        // it includes however long this loop spent not looking -- and the first
        // bench run made that concrete: all 48 dropouts across three arms fell
        // in 39-45 ms, the same band as the max poll gap, because entering a
        // dropout used to force an immediate repaint and the repaint delayed
        // the very observation that ends it. A distribution that tight is a
        // property of the instrument, not of a touch controller. poll_gap_ms is
        // logged so the two can be told apart; if it dominates gap_ms, the real
        // outage was one poll and the rest was us.
        Serial.printf("HOLD,dropout,%lu,gap_ms,%lu,poll_gap_ms,%lu,run_drops,%lu\n",
                      now, gap, curGapMs, runDropouts);
        needsRepaint = true;
    } else if (!touched && !inDropout) {
        inDropout = true;
        lastUpMs = now;
        holdContactMs += now - lastDownMs;
        Serial.printf("HOLD,up,%lu\n", now);
        // DELIBERATELY NO needsRepaint HERE. Painting on dropout ENTRY inserts
        // a full 240x240 sprite push between "contact lost" and "contact back",
        // which lands inside the interval being measured and inflates every
        // dropout to roughly the frame time. The throttled 100 ms repaint still
        // shows the state; it just no longer contaminates the measurement.
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
#if ARDUINO_USB_CDC_ON_BOOT
    // THE HARNESS FREEZE, ROOT-CAUSED 2026-08-05. This guard exists in the
    // product (main.cpp) and this TU has its own setup(), so it never got it --
    // and the omission made the board "almost unusable" for a whole bench day.
    //
    // With nothing draining the USB-CDC, Serial.write blocks once the TX ring
    // fills. This harness prints a TOUCH line on every transition, so an
    // unattended session fills it in seconds and the LOOP then stalls on the
    // print. Measured: 20 042 ms worst poll gap unattended, versus 43 ms across
    // 31 728 samples with a reader attached. The proof it was buffering and not
    // scheduling: when a recorder finally attached 17 h into a run, the first
    // lines it received were the beats from 60 s and 120 s after boot.
    //
    // A bench harness that freezes only when no one is watching is the worst
    // possible failure shape -- it corrupts poll_max_ms, which is the very
    // number the scoring floor is derived from.
    Serial.setTxBufferSize(4096);
    Serial.setTxTimeoutMs(2);
#endif
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
    // The GC9A01 boots inverted; src/main.cpp:137 undoes it and this rig did not,
    // so every colour here was the complement of what the code wrote. Harmless
    // for the hold-gesture measurement, wrong for anything judged by eye.
    // Found 2026-08-06 on animtest, which draws in colour and made it obvious.
    tft.invertDisplay(BLIPSCOPE_DISP_INVERT);
    tft.setBrightness(255);
    Serial.printf("[disp] tft.init=%d %dx%d invert=%d\n", panelOk ? 1 : 0,
                  (int)tft.width(), (int)tft.height(), (int)BLIPSCOPE_DISP_INVERT);

    // Backbuffer in PSRAM (see the note at the declaration). Reported either way:
    // if it ever falls back, the flicker is explained in the log rather than
    // rediscovered on the bench.
    canvas.setPsram(true);
    canvas.setColorDepth(16);
    haveCanvas = canvas.createSprite(variant::SCREEN_SIZE, variant::SCREEN_SIZE) != nullptr;
    Serial.printf("[disp] backbuffer %s (%dx%d, psram_free=%u)\n",
                  haveCanvas ? "ok" : "FAILED - drawing direct, expect flicker",
                  variant::SCREEN_SIZE, variant::SCREEN_SIZE,
                  (unsigned)ESP.getFreePsram());

    // WAIT FOR THE TOUCH CHIP TO ANSWER before reading anything from it. The
    // first run of this harness probed immediately after tft.init() and got
    // chipid=NACK / touchnum=-1: the CST816 is still coming out of its reset
    // (TP_RST is a real GPIO on this variant and the product's watchdog allows a
    // 450 ms boot wait for the same reason). Everything downstream inherited
    // that -- the config dump was empty and, worse, the A/B arm write went to a
    // chip that was not listening, so a run would have been attributed to an arm
    // that was never actually set. A silent NACK at setup is the one failure
    // that makes every later number wrong while looking fine.
    unsigned long probeAttempts = 0;
    const unsigned long probeStart = millis();
    while (ReadReg(REG_CHIPID) < 0 && millis() - probeStart < 2000) {
        ++probeAttempts;
        delay(25);
    }
    const int chipId = ReadReg(REG_CHIPID);
    Serial.printf("REG,probe,attempts,%lu,ms,%lu,chipid,%d\n",
                  probeAttempts, millis() - probeStart, chipId);
    if (chipId < 0) {
        Serial.println("REG,FATAL,touch chip never ACKed -- every number from this run is void");
        Serial.println("REG,FATAL,do not record results; check TP_RST / bus wiring first");
    }

    // Touch identity + the config space this test's interpretation depends on.
    Serial.printf("REG,identity,chipid,0x%02X,touchnum,%d\n", chipId, ReadReg(REG_TOUCHNUM));
    Serial.printf("REG,config,disautosleep_0xFE,%d,autosleeptime_0xF9,%d,irqctl_0xFA,0x%02X\n",
                  ReadReg(REG_DISAUTOSLEEP), ReadReg(REG_AUTOSLEEPTIME), ReadReg(REG_IRQCTL));
    Serial.println("REG,note,0xFE=1 means auto-sleep DISABLED (this board's factory state)");

#ifdef GAMETEST_FORCE_ARM
    arm = (Arm)GAMETEST_ARM;
    Serial.printf("REG,note,FORCED ARM BUILD -- arm pinned to %s at boot, serial a/b/c switches. "
                  "Runs from this build are FORCED and must be reported as such.\n", ArmName(arm));
#else
    arm = Arm::FactoryNoSleep;
#endif
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
    curGapMs = gap;
    if (gap > pollGapMaxMs) { pollGapMaxMs = gap; pollGapMaxAtMs = now; }
    pollGapSumMs += gap;
    pollCount++;
    // The key-window floor, sampled only while an arm-C hold is live -- that is
    // the focused high-rate window the launch key-turn will run in, and the only
    // cadence the scoring bucket may honestly be derived from.
    if (ArmUsesChipSource(arm) && holdActive) {
        if (gap > keyPollGapMaxMs) keyPollGapMaxMs = gap;
        keyPollSumMs += gap;
        keyPollCount++;
    }

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

    // ---- gesture: swipe advances the screen; a banner tap cycles the arm ----
    // THE ARM CANNOT BE A LONG-PRESS. On the Hold screen every press starts a
    // hold run by definition, so "press and hold to change arm" and "press and
    // hold to test" are the same gesture and the arm control is unreachable
    // behind the thing it configures. It moved to a tap on the arm chip, which
    // is also the only self-describing place to put it -- the control sits on
    // the label it changes.
    if (touched && !wasTouched) {
        pressX = tx; pressY = ty; pressMs = now;
        maxMovePx = 0;
        pressInBanner = (ty < BANNER_H_PX);
    } else if (touched) {
        // Live contact: accumulate the peak displacement. Only samples where the
        // driver reports contact are trusted, so a zeroed release never lands here.
        const int dx = abs(tx - pressX), dy = abs(ty - pressY);
        const int m = dx > dy ? dx : dy;
        if (m > maxMovePx) maxMovePx = m;
    } else if (!touched && wasTouched) {
        const unsigned long held = now - pressMs;
        if (pressInBanner && held < SWIPE_MAX_MS && maxMovePx <= SWIPE_MIN_PX) {
            arm = (Arm)(((int)arm + 1) % (int)Arm::COUNT); // A -> B -> C -> A
            ApplyArm();
            needsRepaint = true;
        } else if (held < SWIPE_MAX_MS && maxMovePx > SWIPE_MIN_PX) {
            screen = (Screen)(((int)screen + 1) % (int)Screen::COUNT);
            needsRepaint = true;
            Serial.printf("TOUCH,screen,%d,move_px,%d,held_ms,%lu\n",
                          (int)screen, maxMovePx, held);
        }
        pressInBanner = false;
    }
    wasTouched = touched;

    // A hold run is only valid while the Hold screen is up. The first bench run
    // showed why this needs saying: a swipe mid-hold moved to another screen,
    // HoldTick stopped being called, and the run sat frozen until the screen came
    // back -- then "completed" with 14 s of total time and zero dropouts. It
    // presented as a clean pass. Abort and void it instead; a discarded run is
    // recoverable, a fabricated clean one is not.
    if (screen == Screen::Hold) {
        // ARM C FEEDS FROM THE CHIP, not the driver. This is the whole point of
        // the third arm: finding (b) says the chip keeps the contact the driver
        // loses on a static finger, so arm C asks TouchNum (0x02) directly and
        // ignores INT entirely. A chipN of -1 is a NACK, not a release -- treating
        // a failed read as a lifted finger would manufacture the exact dropouts
        // this arm exists to rule out, so it holds the previous state instead.
        // BOUNDED. Holding the previous state through a single NACK is right;
        // holding it forever is not. A persistent stall (bus wedged, chip gone)
        // would freeze `contact` at true and report an eternal, flawless hold --
        // manufacturing exactly the opposite artifact this arm exists to rule
        // out, and doing it in the direction that looks like success. So a NACK
        // streak past NACK_VOID_MS voids the run outright rather than scoring it.
        static bool lastChipContact = false;
        static unsigned long nackSinceMs = 0;
        bool contact;
        if (ArmUsesChipSource(arm)) {
            if (chipN >= 0) {
                lastChipContact = chipN > 0;
                nackSinceMs = 0;
            } else {
                if (nackSinceMs == 0) nackSinceMs = now;
                if (now - nackSinceMs > NACK_VOID_MS) {
                    if (holdActive) {
                        ++busFailRuns;
                        Serial.printf("HOLD,run_void,%lu,reason,BUS_FAIL,nack_ms,%lu,arm,%s\n",
                                      now, now - nackSinceMs, ArmName(arm));
                        holdActive = false;
                        inDropout = false;
                        needsRepaint = true;
                    }
                    nackSinceMs = 0;
                    lastChipContact = false;
                }
            }
            contact = lastChipContact;
        } else {
            contact = touched;
            nackSinceMs = 0;
        }
        // A press that began on the arm chip is a control, not a run. Without
        // this, every arm change also books a ~200 ms run against the arm being
        // left -- junk denominators in the one statistic this harness exists to
        // produce.
        if (!pressInBanner) HoldTick(contact, now);
    } else if (holdActive) {
        Serial.printf("HOLD,run_void,%lu,reason,screen_changed\n", now);
        holdActive = false;
        inDropout = false;
        needsRepaint = true;
    }

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

            // ---- THE ALARM ----
            //
            // A FIELD IS NOT A REPORT. This session printed `uncertainty_ms,198`
            // in every heartbeat for 26 hours while the design doc asserted
            // 75.7 ms and §13 A.3 sat closed on it. The log and the doc
            // disagreed continuously, in the open, and neither complained --
            // the number was present and simply never read. So the condition
            // now announces itself, in the register of *** RECEIVER SILENT ***.
            //
            // Deliberately NOT an assertion or a failure: the CI guard asserts
            // the build is consistent with the evidence we hold (deterministic,
            // never goes red on its own), while this reports that the world has
            // outrun that evidence (observational). Conflating them gives a red
            // build caused by weather.
            //
            // RE-RULE TRIGGER, recorded here so it is not argued later: three
            // alarms, or any single correction above 250 ms. At that point
            // extend test/fixtures/ntp-corrections-2026-08.json in the
            // valar-eam-feed repo and re-open the ruling.
            const long adjustMs = labs(lastAdjustUs) / 1000;
            if (adjustMs > CLOCK_FLOOR_MS) {
                clockAlarms++;
                Serial.printf("\n*** CLOCK FLOOR EXCEEDED *** correction %ld ms > published floor "
                              "%d ms (alarm %lu of 3 before re-rule)\n",
                              adjustMs, CLOCK_FLOOR_MS, clockAlarms);
                if (adjustMs > CLOCK_RERULE_MS)
                    Serial.printf("*** SINGLE-SAMPLE RE-RULE TRIGGER *** %ld ms exceeds %d ms — "
                                  "re-open the A.3 ruling on this reading alone\n",
                                  adjustMs, CLOCK_RERULE_MS);
                Serial.printf("NTP,alarm,%lu,adjust_ms,%ld,floor_ms,%d,alarms,%lu\n",
                              now, adjustMs, CLOCK_FLOOR_MS, clockAlarms);
            }
        }
        lastSyncEpochUs = nowUs;
        lastSyncMillis = now;
        needsRepaint = true;
    }

#ifdef GAMETEST_FORCE_ARM
    // ---- serial arm switch, so one flash covers all three arms ----
    while (Serial.available()) {
        const int ch = Serial.read();
        int want = -1;
        if (ch == 'a' || ch == 'A') want = 0;
        else if (ch == 'b' || ch == 'B') want = 1;
        else if (ch == 'c' || ch == 'C') want = 2;
        if (want >= 0 && (Arm)want != arm) {
            // Abandon any run in flight: a hold that started under one arm and
            // ended under another belongs to neither, and silently attributing
            // it to the second is how a clean-looking number stops being true.
            if (holdActive) {
                Serial.println("HOLD,run_void,reason,arm_switched_mid_run");
                holdActive = false;
            }
            arm = (Arm)want;
            ApplyArm();
            needsRepaint = true;
        }
    }
#endif

    // ---- periodic heartbeat so a long NTP soak has a trail ----
    static unsigned long lastBeat = 0;
    if (now - lastBeat >= 60000) {
        lastBeat = now;
        Serial.printf("NTP,beat,%lu,syncs,%lu,worst_us,%ld,uncertainty_ms,%ld,"
                      "poll_avg_ms,%lu,poll_max_ms,%lu,poll_max_at_ms,%lu\n",
                      now, syncCount, worstAdjustUs, UncertaintyMs(),
                      pollCount ? pollGapSumMs / pollCount : 0, pollGapMaxMs, pollGapMaxAtMs);
        if (syncCount < 2)
            Serial.println("NTP,note,uncertainty_ms=-1 means UNMEASURED -- a drift correction "
                           "needs two syncs; SNTP's default re-sync is hourly");
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
        // One transfer per repaint. Without this the fillScreen at the top of
        // every frame is visible as a full-screen flash.
        if (haveCanvas) canvas.pushSprite(0, 0);
    }

    // ~5 ms poll normally; 2 ms while an arm-C hold is live. C is the "fixed high
    // rate" arm, and its result is only as trustworthy as its sampling floor --
    // a dropout shorter than the poll gap is invisible, so the arm claiming to
    // rule dropouts out has to sample fastest. A 1-byte read at 400 kHz is ~50 us,
    // so 2 ms is nowhere near saturating the bus.
    delay((ArmUsesChipSource(arm) && holdActive) ? 2 : 5);
}
