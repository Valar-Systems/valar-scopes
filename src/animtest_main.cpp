// animtest_main.cpp -- bench rig for the Missileer flight animation.
//
// Iterates the launch sequence on REAL GLASS, ahead of the game loop consuming
// it. Defines its own setup()/loop(); only [env:animtest-s3-128] re-includes it,
// exactly like hwtest_main.cpp and gametest_main.cpp.
//
// Board: the S3 1.28" Kit (GC9A01 240x240 round + CST816D, variant s3_128.h) --
// the default SKU and the board Missileer ships on.
//
// The animation itself is NOT in this file. It is src/anim/FlightAnimation.*,
// written to be lifted into the real flight director; this TU is the harness
// that drives it and measures it, and it owns three things the module must not:
// the panel, the touch controls, and the clock.
//
// The look being iterated toward is docs/reference/missileer-launch-animation-
// preview.html -- open it next to the board. That is the comparison this rig
// exists to make cheap.
//
// ---------------------------------------------------------------------------
// PERFORMANCE IS A DELIVERABLE, NOT A FOOTNOTE.
//
// The point of putting this on glass early is that art complexity gets tuned to
// what the SPI bus actually sustains. A 240x240 16bpp frame is 115 KB and the
// GC9A01 is on a 40 MHz SPI bus, so the push alone has a floor of roughly 23 ms
// -- about 43 fps before a single pixel is composed. Any beat that draws a
// full-screen fill on top of that (the detonation does, deliberately) is going
// to cost more, and the only honest way to know how much is to measure it per
// beat and write the number down.
//
// So every frame is timed and tagged with its beat, and the summary is a
// per-beat worst case. docs/animtest-results-template.md is the table those
// numbers go into.
// ---------------------------------------------------------------------------
//
// Build/flash (PIN THE PORT -- a second board is usually attached, and
// auto-detection has already put an image on the wrong one once):
//   pio run -e animtest-s3-128 -t upload --upload-port COM119 -t monitor
//   pio run -e animtest-s3-128-truetime -t upload --upload-port COM119 -t monitor
//
// Serial is CSV, tagged for bench-capture: FRAME, / BEAT, / FPS, / TOUCH,.

#include <Arduino.h>

#include "LGFX.h"
#include "anim/FlightAnimation.h"
#include "variants/Variant.h"

namespace {

using missileer::flight::Beat;
using missileer::flight::Director;
using missileer::flight::TimeMode;

LGFX        tft;
LGFX_Sprite canvas(&tft);
bool        haveCanvas = false;
Director    director;

constexpr int      TP_PORT = BLIPSCOPE_TOUCH_I2C_PORT;
constexpr int      TP_ADDR = BLIPSCOPE_TOUCH_I2C_ADDR;
constexpr uint32_t TP_FREQ = BLIPSCOPE_TOUCH_FREQ;
constexpr int      SCREEN  = variant::SCREEN_SIZE;

// ---------------------------------------------------------------------------
// Time mode. COMPRESSED by default: it is the iteration mode, and iterating is
// what a rig is for. TRUE-TIME is the proof mode -- see the module header on
// why the two are not a scale factor.
// ---------------------------------------------------------------------------
#ifdef ANIM_TRUE_TIME
TimeMode mode = TimeMode::TrueTime;
#else
TimeMode mode = TimeMode::Compressed;
#endif

bool paused = false;

// ---------------------------------------------------------------------------
// Per-beat frame timing. Worst case is the number that matters: an average
// hides exactly the frame that drops, and a sequence that averages 40 fps while
// stalling 180 ms on every separation flash is a sequence with a visible hitch.
// ---------------------------------------------------------------------------
struct BeatTiming {
    uint32_t frames = 0;
    uint32_t sumUs  = 0;
    uint32_t worstUs = 0;
    uint32_t worstAtMs = 0;   // when, so a one-off stall is distinguishable from a pattern
    uint32_t composeWorstUs = 0; // draw only, excluding the panel push
    uint32_t pushWorstUs = 0;    // the SPI push alone -- the floor art cannot beat
};
BeatTiming timings[(int)Beat::COUNT];
bool summaryPrinted = false;

// ---------------------------------------------------------------------------
// Gesture state.
//
// LIFTED WHOLESALE FROM gametest_main.cpp, INCLUDING ITS SCARS. That harness
// lost a full bench session because dx was computed at release and getTouch()
// zeroes its out-params when it returns false -- so every lift read as a
// 100+ px swipe on a 40 px threshold, and 33 consecutive runs voided. The two
// guards that fixed it are both here:
//   1. track the largest displacement seen WHILE contact is live, so a zeroed
//      release sample can never enter the delta;
//   2. require the contact to have been SHORT -- a deliberate swipe is a flick,
//      and a long press that drifts is a hold whatever it did.
// Re-deriving this from scratch would have re-derived the bug.
// ---------------------------------------------------------------------------
bool wasTouched = false;
int  pressX = 0, pressY = 0;
int  maxMoveX = 0, maxMovePx = 0;
unsigned long pressMs = 0;
bool pressInBanner = false;
constexpr int           SWIPE_MIN_PX  = 40;
constexpr unsigned long SWIPE_MAX_MS  = 800;
constexpr unsigned long LONG_HOLD_MS  = 900;
constexpr int           BANNER_H_PX   = 46;   // top band: long-hold here toggles time mode

unsigned long lastFrameMs = 0;

const char* ModeName() { return mode == TimeMode::TrueTime ? "TRUE" : "COMP"; }

void LogBeatSummary(Beat b)
{
    const BeatTiming& t = timings[(int)b];
    if (t.frames == 0) return;
    const float avgMs = (t.sumUs / (float)t.frames) / 1000.0f;
    Serial.printf("FPS,%s,%s,frames,%lu,avg_ms,%.1f,worst_ms,%.1f,worst_at_ms,%lu,"
                  "compose_worst_ms,%.1f,push_worst_ms,%.1f,avg_fps,%.1f,worst_fps,%.1f\n",
                  ModeName(), missileer::flight::BeatName(b), (unsigned long)t.frames, avgMs,
                  t.worstUs / 1000.0f, (unsigned long)t.worstAtMs,
                  t.composeWorstUs / 1000.0f, t.pushWorstUs / 1000.0f,
                  avgMs > 0 ? 1000.0f / avgMs : 0.0f,
                  t.worstUs > 0 ? 1000000.0f / t.worstUs : 0.0f);
}

void PrintFullSummary()
{
    Serial.println("FPS,---- per-beat summary ----");
    for (int i = 0; i < (int)Beat::COUNT; ++i) LogBeatSummary((Beat)i);
    // The one line to copy into the results doc's verdict row.
    uint32_t worst = 0; Beat worstBeat = Beat::Ignition;
    for (int i = 0; i < (int)Beat::COUNT; ++i) {
        if (timings[i].worstUs > worst) { worst = timings[i].worstUs; worstBeat = (Beat)i; }
    }
    Serial.printf("FPS,VERDICT,%s,worst_beat,%s,worst_ms,%.1f,worst_fps,%.1f\n",
                  ModeName(), missileer::flight::BeatName(worstBeat), worst / 1000.0f,
                  worst > 0 ? 1000000.0f / worst : 0.0f);
}

void ResetTimings()
{
    for (int i = 0; i < (int)Beat::COUNT; ++i) timings[i] = BeatTiming{};
    summaryPrinted = false;
}

/**
 * Rig chrome. SMALL AND DELIBERATELY UGLY -- it is bench instrumentation, not a
 * design surface, and anything prettier here would start getting mistaken for
 * the product's flight HUD (which §7 has not specified yet). It uses only §11's
 * accents; amber appears nowhere, on the chrome or in the art.
 *
 * ALL OF IT LIVES AT THE TOP. The bottom of the frame is the animation's own
 * lower third -- the reference video's captions, drawn by the module -- and rig
 * chrome sitting on top of the art it is measuring would be instrumentation that
 * changes the thing observed. Hex values are the look target's.
 */
void DrawHud(LovyanGFX& g)
{
    // THE FACE IS ROUND, AND THE TOP OF IT IS NARROW. This chrome first sat at
    // x=8 on rows 4 and 16, which put both lines partly outside the glass -- the
    // chord of a 240 px circle is only 61 px at y=4 and 120 px at y=16, so text
    // starting at x=8 does not exist until x~90. On the bench it read as
    // "T+1:03" with the beat name simply missing, which looks like a truncation
    // bug rather than a placement one.
    //
    // So: CENTRED, and low enough to have room.
    //     y=16 -> 120 px chord, holds the beat name (12 chars = 72 px)
    //     y=28 -> 154 px chord, holds "TRUE PAUSE 16/16 T+31:56" (24 = 144 px)
    // Same arithmetic as the module's caption rows; see DrawCaption.
    const float u = SCREEN / 240.0f;
    g.setTextSize(1);
    g.setTextDatum(textdatum_t::top_center);

    char buf[48];
    g.setTextColor(lgfx::color888(0xC9, 0xA1, 0x5C)); // BRASS
    g.drawString(missileer::flight::BeatName(director.CurrentBeat()),
                 SCREEN / 2, (int)(16 * u));

    // Mode, beat position, and the TRUE T+ mark -- the number the art is judged
    // against even in compressed mode (see Director::TPlusMs).
    const uint32_t tp = director.TPlusMs();
    snprintf(buf, sizeof(buf), "%s%s %d/%d T+%lu:%02lu", ModeName(), paused ? " PAUSE" : "",
             (int)director.CurrentBeat() + 1, (int)Beat::COUNT,
             (unsigned long)(tp / 60000), (unsigned long)((tp / 1000) % 60));
    g.setTextColor(paused ? lgfx::color888(0xFF, 0x3B, 0x30)    // RED
                          : lgfx::color888(0x1D, 0x7A, 0x4A));  // GREEN_DIM
    g.drawString(buf, SCREEN / 2, (int)(28 * u));

    g.setTextDatum(textdatum_t::top_left);
}

void HandleTouch()
{
    int32_t tx = 0, ty = 0;
    const bool down = tft.getTouch(&tx, &ty);

    if (down && !wasTouched) {
        pressX = tx; pressY = ty; pressMs = millis();
        maxMoveX = 0; maxMovePx = 0;
        pressInBanner = (ty < BANNER_H_PX);
    } else if (down) {
        const int dx = tx - pressX, dy = ty - pressY;
        if (abs(dx) > abs(maxMoveX)) maxMoveX = dx;
        const int d = (int)sqrtf((float)(dx * dx + dy * dy));
        if (d > maxMovePx) maxMovePx = d;
    } else if (!down && wasTouched) {
        const unsigned long heldMs = millis() - pressMs;

        if (heldMs >= LONG_HOLD_MS) {
            if (pressInBanner) {
                // TIME MODE TOGGLE. The banner is the mode chip, matching
                // gametest's arm-cycle placement so the two rigs share a
                // vocabulary. A gesture rather than only a build flag because
                // the comparison that matters -- does this beat feel right at
                // the real mark -- is one you make by flipping back and forth
                // on the same board, not by reflashing.
                mode = (mode == TimeMode::Compressed) ? TimeMode::TrueTime : TimeMode::Compressed;
                director.SetMode(mode);
                ResetTimings();
                Serial.printf("TOUCH,mode,%s\n", ModeName());
            } else {
                director.Seek(director.CurrentBeat()); // replay current beat
                paused = false;
                Serial.printf("TOUCH,replay,%s\n", missileer::flight::BeatName(director.CurrentBeat()));
            }
        } else if (abs(maxMoveX) >= SWIPE_MIN_PX && heldMs <= SWIPE_MAX_MS) {
            const int dir = maxMoveX < 0 ? 1 : -1; // swipe left -> next beat
            director.StepBeat(dir);
            paused = false;
            Serial.printf("TOUCH,jump,%s\n", missileer::flight::BeatName(director.CurrentBeat()));
        } else if (maxMovePx < SWIPE_MIN_PX) {
            // Tap: pause when running, step to the next beat when already paused.
            if (!paused) {
                paused = true;
                Serial.printf("TOUCH,pause,%s\n", missileer::flight::BeatName(director.CurrentBeat()));
            } else {
                director.StepBeat(1);
                Serial.printf("TOUCH,step,%s\n", missileer::flight::BeatName(director.CurrentBeat()));
            }
        }
    }
    wasTouched = down;
}

} // namespace

void setup()
{
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    // THE HARNESS FREEZE, root-caused on gametest 2026-08-05 and inherited here
    // on purpose. With nothing draining USB-CDC, Serial.write blocks once the TX
    // ring fills -- and this rig prints a FRAME line at up to 40 Hz, so an
    // unattended session fills it in under a second and the LOOP then stalls on
    // the print. That would corrupt the frame times, which are the entire
    // deliverable: a rig that only reports slow frames when nobody is watching
    // is worse than one that reports nothing.
    Serial.setTxBufferSize(4096);
    Serial.setTxTimeoutMs(2);
#endif
    delay(300);

    Serial.println();
    Serial.printf("[build] env=%s  Missileer flight-animation rig\n",
#ifdef BUILD_ENV
                  BUILD_ENV
#else
                  "animtest"
#endif
    );
    Serial.println("[build] confirm this matches the image you intended BEFORE diagnosing any symptom");

    variant::BoardPreInit(); // no-op on this variant; kept so the sequence matches the product
    const bool panelOk = tft.init();
    tft.setRotation(0);
    // THE GC9A01 BOOTS INVERTED. src/main.cpp:137 does this and the bench TUs did
    // not, so every colour on this rig was the exact complement of what the code
    // wrote: black space came out white, the olive vehicle came out blue, the
    // Earth limb came out orange, and the Hood/Badger fireball came out cyan.
    //
    // It is invisible from the serial log and invisible from a build -- the only
    // symptom is a photograph, which is how it was found (2026-08-06). Any bench
    // rig that renders colour and skips this line is judging the complement of
    // its own palette.
    tft.invertDisplay(BLIPSCOPE_DISP_INVERT);
    tft.setBrightness(255);
    Serial.printf("[disp] tft.init=%d %dx%d invert=%d\n", panelOk ? 1 : 0,
                  (int)tft.width(), (int)tft.height(), (int)BLIPSCOPE_DISP_INVERT);

    // Full-screen backbuffer in PSRAM. 240x240x16bpp = 115 KB -- nothing against
    // 8 MB of PSRAM, and mandatory here: composing straight to the panel would
    // make every frame a visible tear, and the thing being judged is motion.
    canvas.setPsram(true);
    canvas.setColorDepth(16);
    haveCanvas = canvas.createSprite(SCREEN, SCREEN) != nullptr;
    Serial.printf("[disp] backbuffer %s (psram_free=%u)\n",
                  haveCanvas ? "ok" : "FAILED - cannot judge motion without it",
                  (unsigned)ESP.getFreePsram());

    // Wait for the touch chip to ACK before using it -- TP_RST is a real GPIO on
    // this variant and the product allows a 450 ms boot wait for the same
    // reason. Probing too early reads NACK and the controls silently do nothing.
    const unsigned long probeStart = millis();
    while (!lgfx::i2c::readRegister8(TP_PORT, TP_ADDR, 0xA7, TP_FREQ).has_value()
           && millis() - probeStart < 2000) {
        delay(25);
    }
    Serial.printf("[touch] ready after %lums\n", millis() - probeStart);

    Serial.printf("[anim] mode=%s sequence=%lums beats=%d\n", ModeName(),
                  (unsigned long)missileer::flight::SequenceDurationMs(mode), (int)Beat::COUNT);
    Serial.println("[anim] tap=pause/step  swipe=jump beat  long-hold=replay  long-hold TOP=time mode");

    director.Begin(SCREEN, mode);
    ResetTimings();
    lastFrameMs = millis();
}

void loop()
{
    HandleTouch();

    const unsigned long now = millis();
    uint32_t dt = (uint32_t)(now - lastFrameMs);
    lastFrameMs = now;
    // Clamp the step so a serial stall or a breakpoint cannot teleport the
    // sequence past three beats -- the rig would then report timings for beats
    // it never actually drew.
    if (dt > 100) dt = 100;

    const Beat before = director.CurrentBeat();
    if (!paused) director.Advance(dt);

    // Beat boundary: flush the beat that just ended while its numbers are hot.
    if (director.CurrentBeat() != before) {
        LogBeatSummary(before);
        Serial.printf("BEAT,%s,%s,t_plus_ms,%lu\n", ModeName(),
                      missileer::flight::BeatName(director.CurrentBeat()),
                      (unsigned long)director.TPlusMs());
    }

    const uint32_t t0 = micros();
    if (haveCanvas) {
        director.Render(canvas, 0);
        DrawHud(canvas);
        const uint32_t t1 = micros();
        canvas.pushSprite(0, 0);
        const uint32_t t2 = micros();

        BeatTiming& bt = timings[(int)director.CurrentBeat()];
        const uint32_t total = t2 - t0, compose = t1 - t0, push = t2 - t1;
        bt.frames++;
        bt.sumUs += total;
        if (total > bt.worstUs) { bt.worstUs = total; bt.worstAtMs = now; }
        if (compose > bt.composeWorstUs) bt.composeWorstUs = compose;
        if (push > bt.pushWorstUs) bt.pushWorstUs = push;

        // Per-frame line, throttled. Every frame would be 40 lines/s of CSV and
        // the print itself would become the slowest thing in the loop --
        // instrumentation that changes the measurement.
        if (bt.frames % 15 == 0) {
            Serial.printf("FRAME,%s,%s,total_us,%lu,compose_us,%lu,push_us,%lu\n",
                          ModeName(), missileer::flight::BeatName(director.CurrentBeat()),
                          (unsigned long)total, (unsigned long)compose, (unsigned long)push);
        }
    } else {
        director.Render(tft, 0); // degraded: tears, but a rig that runs beats one that does not
    }

    if (director.Finished() && !summaryPrinted) {
        summaryPrinted = true;
        PrintFullSummary();
        Serial.println("[anim] sequence complete -- long-hold to replay, swipe to pick a beat");
    }
}
