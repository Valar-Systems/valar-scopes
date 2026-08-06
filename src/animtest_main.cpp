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

#ifdef ANIM_GLOBE_SURVEY
// ---------------------------------------------------------------------------
// GLOBE SURVEY. Not part of the sequence -- a separate mode that holds MIDCOURSE
// at 70% (past the cut, so the globe owns the frame, and far enough along that
// the track has a visible flown/unflown split) and walks a table of targets.
//
// It exists because the camera orientation is DERIVED from the launch/aim pair,
// which means every scenario is a different view of the Earth and a differently
// shaped arc. One target proves nothing: the tilt was chosen against GOLF-07,
// and the question that matters is whether the projection still reads when the
// game aims somewhere else.
//
// The scenarios are picked to break it if it can be broken -- near-meridional,
// hard east-west, trans-polar, trans-Pacific, southern hemisphere, equatorial,
// and a short one. Real sites and real aim points; the aim points stay open
// ocean or bare ground per the tone rule, except where a city is named to make
// the geometry legible on a photograph.
// ---------------------------------------------------------------------------
struct Scenario {
    const char* name;
    float launchLon, launchLat;
    float aimLon, aimLat;
};
const Scenario kScenarios[] = {
    {"GOLF-07",   -104.87f,  41.15f, -123.39f, -48.87f},  // the tuned case
    {"POLAR",     -104.87f,  41.15f,   37.62f,  55.75f},  // hard east-west, high lat
    {"TRANSPOLE", -101.30f,  48.42f,  116.40f,  39.90f},  // longest, over the top
    {"PACIFIC",   -120.57f,  34.74f,  167.73f,   8.72f},  // Vandenberg->Kwajalein
    {"ARCTIC",    -101.30f,  48.42f,   56.00f,  74.00f},  // straight over the pole
    {"S-ATL",     -104.87f,  41.15f,  -15.00f, -35.00f},  // southeast quadrant
    {"EQUATOR",    -52.77f,   5.24f,   75.00f, -25.00f},  // low latitude both ends
    {"SHORT",     -111.18f,  47.51f, -170.00f,  45.00f},  // short arc, small bow
};
constexpr int kScenarioCount = (int)(sizeof(kScenarios) / sizeof(kScenarios[0]));
constexpr unsigned long SURVEY_DWELL_MS = 4000;

int           surveyIx = 0;
unsigned long surveyAt = 0;

void SurveyApply()
{
    const Scenario& s = kScenarios[surveyIx];
    director.SetScenario(s.launchLon, s.launchLat, s.aimLon, s.aimLat);
    // Re-seek so the dot lands on the NEW great circle. Without this the map
    // redraws around a dot still sitting on the old one -- the exact "two
    // opinions about where the vehicle is" bug the match-cut rule exists to
    // prevent, reintroduced by a rig.
    director.Seek(Beat::Midcourse, 0.70f);
    Serial.printf("SURVEY,%d/%d,%s,launch,%.2f,%.2f,aim,%.2f,%.2f,range_km,%.0f\n",
                  surveyIx + 1, kScenarioCount, s.name,
                  s.launchLon, s.launchLat, s.aimLon, s.aimLat,
                  director.ScenarioRangeKm());
}
#endif

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
    uint32_t smokeWorstUs = 0;   // LIFTOFF's smoke pass alone; 0 on every other beat
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
    // smoke_worst_ms is carried on EVERY row, not only LIFTOFF's, so the column
    // is a constant width for the capture parser and the zeros are themselves the
    // assertion that no other beat pays for it.
    Serial.printf("FPS,%s,%s,frames,%lu,avg_ms,%.1f,worst_ms,%.1f,worst_at_ms,%lu,"
                  "compose_worst_ms,%.1f,push_worst_ms,%.1f,smoke_worst_ms,%.1f,"
                  "avg_fps,%.1f,worst_fps,%.1f\n",
                  ModeName(), missileer::flight::BeatName(b), (unsigned long)t.frames, avgMs,
                  t.worstUs / 1000.0f, (unsigned long)t.worstAtMs,
                  t.composeWorstUs / 1000.0f, t.pushWorstUs / 1000.0f,
                  t.smokeWorstUs / 1000.0f,
                  avgMs > 0 ? 1000.0f / avgMs : 0.0f,
                  t.worstUs > 0 ? 1000000.0f / t.worstUs : 0.0f);
}

void PrintFullSummary()
{
    Serial.println("FPS,---- per-beat summary ----");
    for (int i = 0; i < (int)Beat::COUNT; ++i) LogBeatSummary((Beat)i);
    // The one line to copy into the results doc's verdict row.
    uint32_t worst = 0; Beat worstBeat = Beat::Liftoff;
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

    // HALOED, AND NOT GREEN, for the same two reasons the flight track is not
    // green. The globe now fills the whole face, so this chrome no longer sits on
    // black -- it sits on coastlines. Green-on-green vanished, and brass read as
    // yellow against them, which on this product is the one thing chrome must
    // never do: amber means EXERCISE and a colour that turns up elsewhere stops
    // carrying that. It is a dull olive-gold, not the amber token, but at 6 px on
    // a dark green sphere that distinction is not one a viewer can make.
    //
    // So: paper and grey over a one-pixel dark halo -- the same mechanism
    // DrawCaption uses, for the same reason (no fixed row is safe when what is
    // underneath moves). The rig chrome gives up its brass to buy that; being
    // legible beats being visually distinct from the product's own lower third,
    // which is at the OTHER end of the screen anyway.
    auto hud = [&](const char* s, int row, uint32_t col) {
        static const int8_t kHalo[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        g.setTextColor(lgfx::color888(0x06, 0x08, 0x0A));
        for (int i = 0; i < 4; ++i) {
            g.drawString(s, SCREEN / 2 + kHalo[i][0], row + kHalo[i][1]);
        }
        g.setTextColor(col);
        g.drawString(s, SCREEN / 2, row);
    };
    const uint32_t kHudName = lgfx::color888(0xEC, 0xE7, 0xD6);  // PAPER
    const uint32_t kHudSub  = lgfx::color888(0x7A, 0x7D, 0x86);  // GREY

#ifdef ANIM_GLOBE_SURVEY
    // THE PHOTOGRAPH HAS TO SAY WHICH ONE IT IS. Eight globes on a camera roll
    // are eight unlabelled globes; the whole point of a survey is comparing them
    // afterwards, and a beat name is the one thing that does not vary here.
    hud(kScenarios[surveyIx].name, (int)(16 * u), kHudName);
    snprintf(buf, sizeof(buf), "%d/%d  %.0f KM", surveyIx + 1, kScenarioCount,
             director.ScenarioRangeKm());
    hud(buf, (int)(28 * u), kHudSub);
    g.setTextDatum(textdatum_t::top_left);
    return;
#endif

    hud(missileer::flight::BeatName(director.CurrentBeat()), (int)(16 * u), kHudName);

    // Mode, beat position, and the TRUE T+ mark -- the number the art is judged
    // against even in compressed mode (see Director::TPlusMs).
    const uint32_t tp = director.TPlusMs();
    snprintf(buf, sizeof(buf), "%s%s %d/%d T+%lu:%02lu", ModeName(), paused ? " PAUSE" : "",
             (int)director.CurrentBeat() + 1, (int)Beat::COUNT,
             (unsigned long)(tp / 60000), (unsigned long)((tp / 1000) % 60));
    // Red when paused, because a paused rig that looks like a running one has
    // wasted a bench session before. Grey otherwise -- it was GREEN_DIM, which is
    // invisible over the globe during MIDCOURSE.
    hud(buf, (int)(28 * u), paused ? lgfx::color888(0xFF, 0x3B, 0x30) : kHudSub);

    g.setTextDatum(textdatum_t::top_left);
}

#ifdef ANIM_PROFILE
/**
 * PRIMITIVE COSTS, measured once at boot.
 *
 * Exists because a projection change was proposed on the strength of an
 * estimate, and the estimate rested on two numbers nobody had measured on this
 * board: what a short drawLine costs, and what a full-screen PSRAM->PSRAM blit
 * costs. Everything else in an orthographic-globe budget is arithmetic on top of
 * those two, so guessing them wrong scales straight through to the answer.
 *
 * Deliberately crude -- it runs before the sequence starts, prints four CSV
 * lines and never runs again. It is not a benchmark suite; it is the two
 * constants a design decision needs.
 */
void BenchPrimitives(LGFX_Sprite& spr)
{
    if (!haveCanvas) return;

    // Short strokes, which is what a decimated coastline is made of.
    for (int len : {3, 8}) {
        const uint32_t t0 = micros();
        constexpr int kN = 2000;
        for (int i = 0; i < kN; ++i) {
            const int x = 20 + (i * 7) % (SCREEN - 40);
            const int y = 20 + (i * 13) % (SCREEN - 40);
            spr.drawLine(x, y, x + len, y + len, 0x1234u);
        }
        const uint32_t dt = micros() - t0;
        Serial.printf("BENCH,drawLine,len,%d,calls,%d,total_us,%lu,per_call_ns,%lu\n",
                      len, kN, (unsigned long)dt, (unsigned long)(dt * 1000UL / kN));
    }

    // fillScreen as a WRITE-RATE REFERENCE. Same 115 KB of destination, no
    // source read. Any blit that comes out faster than this is not copying.
    {
        constexpr int kN = 20;
        const uint32_t t0 = micros();
        for (int i = 0; i < kN; ++i) spr.fillScreen((uint16_t)(0x0800u + i));
        const uint32_t dt = micros() - t0;
        Serial.printf("BENCH,fillScreen,%dx%d,per_call_us,%.1f\n",
                      SCREEN, SCREEN, (dt / (float)kN));
    }

    // A second full-screen PSRAM sprite, blitted onto the first. This is the
    // whole cost of a cached static map, and it does not scale with vertex
    // count -- which is the entire argument for caching one.
    //
    // VERIFIED, NOT TIMED BLIND. The first version of this reported 6.2 us for
    // 115 KB of PSRAM-to-PSRAM copy -- 18 GB/s, which is roughly 500x what the
    // bus can do, so it was measuring a call that did nothing. The destination is
    // now checked pixel by pixel afterwards and the check is printed: a blit that
    // did not happen and a blit that did are otherwise indistinguishable from a
    // timing number alone.
    {
        LGFX_Sprite cache(&tft);
        cache.setPsram(true);
        cache.setColorDepth(16);
        if (cache.createSprite(SCREEN, SCREEN)) {
            spr.fillScreen(0);
            cache.fillScreen(cache.color888(0x20, 0xE0, 0x60));
            // Read the reference back OUT of the source rather than assuming the
            // literal survives colour conversion. The first version of this probe
            // compared against a raw RGB565 constant and reported 0/225 on a blit
            // that had plainly happened -- a probe that cries wolf is worse than
            // no probe, because the next real failure gets waved through.
            const uint32_t want = cache.readPixel(0, 0);
            constexpr int kN = 20;
            const uint32_t t0 = micros();
            for (int i = 0; i < kN; ++i) cache.pushSprite(&spr, 0, 0);
            const uint32_t dt = micros() - t0;
            int hits = 0;
            for (int y = 0; y < SCREEN; y += 17) {
                for (int x = 0; x < SCREEN; x += 17) {
                    if (spr.readPixel(x, y) == want) hits++;
                }
            }
            const int probes = ((SCREEN + 16) / 17) * ((SCREEN + 16) / 17);
            Serial.printf("BENCH,pushSprite,%dx%d,psram,per_call_us,%.1f,verified,%d/%d\n",
                          SCREEN, SCREEN, (dt / (float)kN), hits, probes);
            cache.deleteSprite();
        } else {
            Serial.println("BENCH,pushSprite,ALLOC_FAILED");
        }
    }

    // The globe's inner loop, with no drawing at all: int16 unit vector -> 3x3
    // rotate -> cull -> screen. This is the number the "no trig in the inner
    // loop" design rests on.
    {
        static int16_t vx[512], vy[512], vz[512];
        for (int i = 0; i < 512; ++i) {
            const float a = i * 0.34f, b = i * 0.11f;
            vx[i] = (int16_t)(cosf(b) * cosf(a) * 32767.0f);
            vy[i] = (int16_t)(cosf(b) * sinf(a) * 32767.0f);
            vz[i] = (int16_t)(sinf(b) * 32767.0f);
        }
        const float m[9] = {0.87f, -0.21f, 0.44f, 0.19f, 0.97f, 0.09f, -0.45f, 0.02f, 0.89f};
        constexpr int kReps = 40;
        volatile int sink = 0;
        const uint32_t t0 = micros();
        for (int r = 0; r < kReps; ++r) {
            for (int i = 0; i < 512; ++i) {
                const float x = vx[i] * (1.0f / 32767.0f);
                const float y = vy[i] * (1.0f / 32767.0f);
                const float z = vz[i] * (1.0f / 32767.0f);
                const float rz = m[6] * x + m[7] * y + m[8] * z;
                if (rz <= 0) continue;                       // far hemisphere
                const float rx = m[0] * x + m[1] * y + m[2] * z;
                const float ry = m[3] * x + m[4] * y + m[5] * z;
                sink += (int)(120.0f + rx * 110.0f) + (int)(120.0f - ry * 110.0f);
            }
        }
        const uint32_t dt = micros() - t0;
        Serial.printf("BENCH,vertexXform,verts,%d,total_us,%lu,per_vert_ns,%lu,sink,%d\n",
                      512 * kReps, (unsigned long)dt,
                      (unsigned long)(dt * 1000UL / (512UL * kReps)), (int)sink);
    }
    spr.fillScreen(0);
}
#endif

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

#ifdef ANIM_PROFILE
    BenchPrimitives(canvas);
#endif

    director.Begin(SCREEN, mode);
    ResetTimings();
    lastFrameMs = millis();

#ifdef ANIM_GLOBE_SURVEY
    Serial.printf("[anim] GLOBE SURVEY -- %d targets, %lu ms each, tap to advance\n",
                  kScenarioCount, (unsigned long)SURVEY_DWELL_MS);
    surveyAt = millis();
    SurveyApply();
#endif
}

void loop()
{
#ifdef ANIM_GLOBE_SURVEY
    // The survey does not advance the sequence at all -- it holds one frame per
    // scenario, which is what makes the globes comparable between photographs.
    // Tap steps to the next target immediately; the dwell timer does the rest.
    {
        int32_t tx = 0, ty = 0;
        const bool down = tft.getTouch(&tx, &ty);
        const unsigned long now = millis();
        if ((down && !wasTouched) || now - surveyAt >= SURVEY_DWELL_MS) {
            surveyIx = (surveyIx + 1) % kScenarioCount;
            surveyAt = now;
            SurveyApply();
        }
        wasTouched = down;

        if (haveCanvas) {
            const uint32_t t0 = micros();
            director.Render(canvas, 0);
            const uint32_t t1 = micros();
            DrawHud(canvas);
            canvas.pushSprite(0, 0);
            const uint32_t t2 = micros();
            static uint32_t frame = 0;
            if ((frame++ % 30) == 0) {
                Serial.printf("SURVEY,frame,%s,compose_us,%lu,total_us,%lu\n",
                              kScenarios[surveyIx].name,
                              (unsigned long)(t1 - t0), (unsigned long)(t2 - t0));
            }
        }
        return;
    }
#endif

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
#ifdef ANIM_PROFILE
        // THE SMOKE'S OWN WORST CASE, which is the number the particle count
        // stands or falls on. A beat average would hide it completely: the cloud
        // exists for about half of LIFTOFF and peaks for a fraction of that, so
        // averaging it across the beat reports a system that is roughly free
        // right up until the frame that drops.
        const uint32_t smoke = director.SmokeUs();
        if (smoke > bt.smokeWorstUs) bt.smokeWorstUs = smoke;
#endif

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
