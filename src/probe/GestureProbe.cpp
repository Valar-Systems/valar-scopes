/**
 * GestureProbe -- record real fingers, so D3 is graded against a corpus that
 * exists.
 *
 * ===========================================================================
 * WHY THIS COMES BEFORE THE RECOGNIZER, NOT AFTER IT.
 *
 * D3 is the key-turn recognizer: press-drag arc on the bezel, hold (§3 step 4).
 * It must be graded against a RECORDED corpus, not an imagined one, and the
 * work order says so in one line: "The espeak lesson stands: synthetic input
 * produced a fake bug, a fake priority, and hid the real one."
 *
 * A recognizer written first and a corpus captured second is a recognizer whose
 * thresholds were chosen from a picture in somebody's head. So this ships
 * first, and D3 is graded on what it produces.
 *
 * WHAT IT DOES. Polls the CST816D as fast as the bus allows and prints one CSV
 * row per sample -- raw, uninterpreted, no smoothing, no gesture register, no
 * dead-zone. Interpretation is D3's job and doing any of it here would bake the
 * recognizer's assumptions into its own evidence.
 *
 *   seq,t_us,dt_us,state,x,y,r,theta_deci,band
 *
 *   seq        monotonic sample counter. GAPS ARE THE POINT -- see below.
 *   t_us       micros() at read. The device has no RTC; the runbook stamps
 *              wall-clock into the provenance header.
 *   dt_us      microseconds since the previous sample, so a stall is visible
 *              as a number rather than inferred from missing rows.
 *   state      1 = a finger is reported, 0 = none.
 *   x,y        raw panel coordinates, top-left origin, as the driver gives them.
 *   r          radius from centre, in pixels. Precomputed because it is
 *              integer-cheap here and every consumer wants it.
 *   theta_deci angle from centre in tenths of a degree, 0 = 12 o'clock,
 *              increasing clockwise. Tenths, not degrees: a slow turn moves
 *              less than a degree per sample and integer degrees would quantise
 *              the very thing being measured.
 *   band       1 when r is inside the bezel band the key turn lives in, else 0.
 *              Recorded rather than filtered on, so a drag that LEAVES the band
 *              is in the corpus -- that is one of D3's required cases.
 *
 * WHAT THIS CAPTURE GATES, which is more than it looks like.
 *
 * §13 B frames the 10 s hold as the gate on the DEPUTY's switches, so it has
 * been read as a crew-layer question. It is not only that. §3 step 4 makes the
 * COMMANDER's input a press-drag arc WITH A HOLD -- so if this panel drops
 * static touches, the SOLO key-turn is affected too, and D3's recognizer has to
 * become motion-sustained rather than hold-sustained. That is a different
 * design, not a tuned threshold.
 *
 * So D3's spec waits on this corpus rather than assuming a hold is available.
 *
 * THE SAMPLE GAP IS A FIRST-CLASS MEASUREMENT, not noise to be cleaned up.
 * §13's open question is whether this panel can hold a finger for 10 s, and the
 * failure mode the corrected gametest is measuring is a hold that DROPS a
 * sample mid-way. If this probe smoothed over gaps, the corpus would be missing
 * exactly the defect the recognizer has to be explicit about.
 *
 * WHAT IT DELIBERATELY DOES NOT DO:
 *
 *   - no TouchWatchdog. It re-arms the chip's DisAutoSleep after a silent
 *     internal reset, which is correct in production and would silently repair
 *     a dropout mid-capture -- the corpus would then show a clean hold on a
 *     chip that had in fact reset. Off here, and the runbook says a dropout is
 *     a finding rather than a bad run.
 *   - no gesture register. The CST816 reports its own swipe/long-press
 *     verdicts; using them would grade D3 against the chip's opinion instead of
 *     against fingers.
 *   - no network, no display drawing beyond a static target ring.
 * ===========================================================================
 *
 * Runbook: docs/bench-runbook-device-game.md, station 1.
 */

// PROBE_SKETCH GUARD -- NOT OPTIONAL, AND NOT STYLE.
//
// This file defines setup() and loop(). Its env selects it with a
// build_src_filter, but a filter is what puts a file IN a build; nothing keeps
// it out of the others. The radar env's filter is `+<*>` plus a list of
// exclusions, and `src/probe/` is not on that list -- so without this guard the
// TU lands in every product build and the link dies on duplicate setup()/loop().
//
// Every other probe in this directory has carried this guard since the
// directory existed. These two shipped without it and broke the default SKU's
// link for the whole of PR #228, which nobody saw because D6 and D4 were each
// verified by building THEIR OWN env. The shipping env was never built.
// CLAUDE.md lists that exact asymmetry -- "the shipping env vs the CI matrix" --
// as a known way this project breaks things silently. It did it again.
#ifdef PROBE_SKETCH

#include <Arduino.h>
#include <math.h>

#include "LGFX.h"
#include "Layout.h"
#include "ProbeProvenance.h"

namespace {

LGFX tft;

/// The bezel band the key turn lives in, as a fraction of the radius.
///
/// RECORDED, NOT ENFORCED. Samples outside it are still printed with band=0 --
/// D3 needs "a drag that leaves the bezel band" in its corpus, and a probe that
/// dropped those rows would delete the case.
constexpr float kBandInner = 0.72f;
constexpr float kBandOuter = 1.02f;  // past the edge: fingers overhang the glass

uint32_t g_seq = 0;
uint32_t g_last_us = 0;
bool g_was_down = false;
uint32_t g_strokes = 0;

/// A static target so the operator has something to trace. Drawn ONCE: a
/// redraw mid-capture would compete with the I2C poll for the loop task and
/// show up as a sample gap that is the probe's fault rather than the panel's.
void DrawTarget() {
  tft.fillScreen(TFT_BLACK);
  const int c = SCREEN_SIZE / 2;
  const int rOuter = static_cast<int>(c * kBandOuter);
  const int rInner = static_cast<int>(c * kBandInner);
  tft.drawCircle(c, c, rOuter - 1, TFT_DARKGREY);
  tft.drawCircle(c, c, rInner, TFT_DARKGREY);
  // 12 o'clock mark: the zero the angles are measured from, so the operator
  // and the CSV agree about where a turn started.
  tft.fillRect(c - 1, 2, 3, 10, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(middle_center);
  tft.drawString("TRACE THE BAND", c, c - 8);
  tft.drawString("serial is recording", c, c + 8);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  probe::PrintProvenance("gesture-capture");

  Serial.println("# station 1 of 4 -- gesture capture (D6, corpus for D3)");
  Serial.println("# every row is one raw sample. no smoothing, no gesture register.");
  Serial.println("# a GAP in t_us is a measurement, not noise: see the file header.");
  Serial.println("#");
  Serial.println("# what to do: trace the ring slowly, then quickly; a short arc;");
  Serial.println("#   a wrong-direction arc; a drag that wanders out of the band;");
  Serial.println("#   a 10 s hold without moving; two fingers. Say each one aloud");
  Serial.println("#   into the log with a blank line between strokes.");
  Serial.println("#");
  Serial.println("seq,t_us,dt_us,state,x,y,r,theta_deci,band");

  variant::BoardPreInit();
  tft.init();
  tft.setRotation(0);
  DrawTarget();
  g_last_us = micros();
}

void loop() {
  int32_t x = 0;
  int32_t y = 0;
  // The DRIVER's raw read, not ReadTouch(): ReadTouch carries the production
  // poll's own debouncing and bus arbitration, and a corpus is supposed to
  // record the panel rather than the app's treatment of it.
  const bool down = tft.getTouch(&x, &y);
  const uint32_t now = micros();
  const uint32_t dt = now - g_last_us;
  g_last_us = now;

  // A BLANK LINE BETWEEN STROKES, so the corpus segments without a heuristic.
  // The alternative is D3 inferring stroke boundaries from gaps, which is the
  // same gap it must treat as a defect inside a stroke -- one signal, two
  // meanings, and the recognizer would have to guess which.
  if (g_was_down && !down) {
    Serial.println();
    g_strokes += 1;
    Serial.printf("# --- stroke %u ended ---\n", (unsigned)g_strokes);
  }
  g_was_down = down;

  if (down) {
    const int c = SCREEN_SIZE / 2;
    const float dx = static_cast<float>(x - c);
    const float dy = static_cast<float>(y - c);
    const float r = sqrtf(dx * dx + dy * dy);
    // 0 = 12 o'clock, clockwise. atan2(dx, -dy) puts north at zero and turns
    // the sign the way a clock face does, which is how the operator will
    // describe the stroke.
    float deg = atan2f(dx, -dy) * 57.2957795f;
    if (deg < 0.0f) deg += 360.0f;
    const int band = (r >= c * kBandInner && r <= c * kBandOuter) ? 1 : 0;
    Serial.printf("%u,%u,%u,1,%d,%d,%d,%d,%d\n",
                  (unsigned)g_seq++, (unsigned)now, (unsigned)dt,
                  (int)x, (int)y, (int)(r + 0.5f), (int)(deg * 10.0f + 0.5f), band);
  } else {
    // IDLE ROWS ARE RECORDED TOO, at a slow cadence. A corpus that only holds
    // finger-down samples cannot show that the bus was alive during a dropout,
    // so a chip that stopped reporting and a finger that lifted look identical
    // -- which is precisely the distinction §13's 10 s question turns on.
    static uint32_t last_idle = 0;
    if (now - last_idle > 100000u) {
      last_idle = now;
      Serial.printf("%u,%u,%u,0,,,,,\n",
                    (unsigned)g_seq++, (unsigned)now, (unsigned)dt);
    }
  }
  // No delay(). The poll rate IS the measurement; pacing it would put the
  // probe's cadence into the corpus in place of the panel's.
}

#endif // PROBE_SKETCH
