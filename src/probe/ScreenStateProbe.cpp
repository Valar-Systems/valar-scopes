/**
 * ScreenStateProbe -- log what a draw call DECIDED, so a render can be asserted
 * without a human looking at the panel.
 *
 * ===========================================================================
 * THE PROBLEM IT SOLVES, stated plainly: this project has never been able to
 * assert on a rendered frame. Every claim about what the device shows has been
 * "somebody looked at it and it seemed right", which is why the eleven Draw*
 * functions in src/eam/EamScreens.cpp were allowed to interleave state with
 * rendering in the first place. D1 split the drill's state out; this is the
 * other half -- the device-side answer to "assert on observable output".
 *
 * WHAT IT LOGS. Not pixels. A frame of pixels is not an assertion, it is a
 * second thing to eyeball, and diffing screenshots on a round display makes
 * every antialiased edge a failure. What a renderer DECIDES is a small set of
 * facts, and those are assertable:
 *
 *   frame,phase,text:<id>=<string>,rect:<id>=x,y,w,h,arc:<id>=deg,flag:<id>=0|1
 *
 * One line per frame, key=value, sorted. The renderer calls Note* instead of
 * (or alongside) drawing; a real DrawDrill compiled with -DSCREEN_STATE_PROBE
 * emits both, so what is asserted is the SAME code path that ships, not a
 * parallel description of it. That is the whole design constraint: a probe that
 * logs what a renderer meant to draw, rather than what it drew, is a
 * restatement -- the mirror this project has been caught in three times.
 *
 * SO THE RULE IS: Note* calls sit BESIDE the draw call they describe, taking
 * the same arguments, on the same line. A reviewer can see that a divergence
 * would be a visible edit rather than a silent drift.
 *
 *   canvas.drawString(label, x, y);  ScreenState::Text("hdr", label);
 *
 * It is compiled out entirely without the flag, so the shipping image carries
 * none of it.
 * ===========================================================================
 *
 * WHAT THIS PROBE ITSELF DOES. It drives DrillMachine through a scripted drill
 * with no human present -- the one thing the bench cannot do by hand at
 * microsecond precision -- and prints the screen-state line for each frame. The
 * output is a fixture: the frames a correct drill produces, captured from the
 * real board, which D4's host tests can then assert against.
 *
 * NOTE WHAT IT CANNOT ANSWER. It shows what the renderer decided, not whether
 * the panel displayed it. A dead backlight, a wrong-SKU flash, an SPI stall
 * mid-push -- all produce a perfect log and a black screen. That is a real
 * limit and the runbook says so: this replaces "did it draw the right thing",
 * not "is anything on the screen".
 *
 * Runbook: docs/bench-runbook-device-game.md, station 2.
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

#include "LGFX.h"
#include "Layout.h"
#include "ProbeProvenance.h"
#include "BandCanvas.h"
#include "game/DrawDrill.h"
#include "game/DrillMachine.h"

namespace {

LGFX tft;

/// The real renderer, on a real backbuffer.
///
/// THE STAND-IN IS GONE. This probe shipped with a minimal DrawAndLog so it
/// could be landed and flashed before D4 existed -- the bench session must not
/// wait on the software queue. D4 landed, the log format did not move, and the
/// probe now drives the SAME function the device draws with. A fixture captured
/// against a stand-in would have graded the stand-in.
LGFX_Sprite g_back(&tft);
uint64_t g_now = 0;

/// The config the scripted drills run under.
///
/// The bucket is set to what /config serves TODAY, and is written here rather
/// than left at its zero default because a capture in which every deviation
/// renders "TIMING RECORDED" would exercise the wrong branch and produce a
/// fixture that says nothing about the figure. It is a probe-local stand-in for
/// a fetch, and the value is stamped into the provenance block so a reader can
/// tell it apart from a served one.
game::Config g_cfg = [] {
  game::Config c;
  c.bucket_us = 200000u;  // 0.2 s, per test/fixtures/game-config.json
  return c;
}();

void DrawAndLog(uint32_t frame, const game::State& st) {
  (void)frame;  // DrawDrill carries its own log sequence.
  BandCanvas c(g_back, 0);
  // The bucket comes from the machine's Config exactly as it will on a real
  // device -- the probe must not hand the renderer a value the firmware would
  // not have, or the capture stops describing the shipping path.
  game::DrawDrill(c, eam::PaletteGreen(), st, g_cfg, g_now);
  g_back.pushSprite(0, 0);
}

/// One scripted drill, driven at a fixed frame cadence with a key turn at a
/// chosen offset from T. No human, exact microseconds -- which is the one thing
/// a bench session cannot produce by hand.
void RunScript(const char* label, int64_t key_offset_us, bool key_at_all) {
  Serial.printf("\n# ---- script: %s (key %s%lld us from T) ----\n",
                label, key_at_all ? "" : "NOT PRESSED, ", (long long)key_offset_us);
  Serial.println("frame,phase,fields...");

  game::DrillMachine m(g_cfg);
  const uint64_t T = 5000000ull;  // 5 s in, so the countdown is visible
  m.SetT(T);

  const uint64_t kFrame = 40000ull;  // 25 fps
  uint32_t frame = 0;
  bool keyed = false;
  for (uint64_t now = 0; now <= T + 4000000ull; now += kFrame, frame += 1) {
    if (frame == 1) m.Step(game::Event::MessageArrived, now);
    if (frame == 10) m.Step(game::Event::PlayerOpen, now);
    if (frame == 45) m.Step(game::Event::PlayerAck, now);
    if (frame == 50) m.Step(game::Event::PlayerConfirmWarPlan, now);
    if (frame == 55) m.Step(game::Event::PlayerEnable, now);
    m.Step(game::Event::Tick, now);
    g_now = now;
    if (key_at_all && !keyed && (int64_t)now >= (int64_t)T + key_offset_us) {
      m.Step(game::Event::PlayerKeyTurn, now);
      keyed = true;
    }
    DrawAndLog(frame, m.Get());
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  probe::PrintProvenance("screen-state");

  Serial.println("# station 2 of 4 -- screen state (D6, fixture for D4)");
  Serial.println("# one line per FRAME: what the renderer decided, not pixels.");
  Serial.println("# LIMIT: this cannot see the panel. A dead backlight, a wrong-SKU");
  Serial.println("#   flash or an SPI stall all produce a perfect log and a black");
  Serial.println("#   screen. Look at the board at least once during this run.");

  variant::BoardPreInit();
  tft.init();
  tft.setRotation(0);
  // A PSRAM backbuffer, exactly as the app uses: a frame log captured against a
  // sprite in internal RAM would be a fixture about a machine we do not ship.
  g_back.setPsram(true);
  g_back.setColorDepth(16);
  if (!g_back.createSprite(SCREEN_SIZE, SCREEN_SIZE)) {
    Serial.println("# FATAL: backbuffer alloc failed -- this run is not evidence");
    return;
  }

  RunScript("clean execution", 300000, true);
  RunScript("perfect execution", 0, true);
  RunScript("late, inside the window", 1900000, true);
  RunScript("no key at all", 0, false);

  Serial.println("\n# ---- done. four scripts captured. ----");
}

void loop() {
  delay(1000);
}

#endif // PROBE_SKETCH
