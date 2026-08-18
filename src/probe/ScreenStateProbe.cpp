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

#include <Arduino.h>

#include "LGFX.h"
#include "Layout.h"
#include "ProbeProvenance.h"
#include "game/DrillMachine.h"

namespace {

LGFX tft;

/// The screen-state recorder. Fixed buffers, no heap: this runs in a draw path.
class ScreenState {
 public:
  static void Begin(uint32_t frame, const char* phase) {
    len_ = 0;
    Serial.printf("%u,%s", (unsigned)frame, phase);
  }
  static void Text(const char* id, const char* value) {
    Serial.printf(",text:%s=%s", id, value);
  }
  static void Num(const char* id, long value) {
    Serial.printf(",num:%s=%ld", id, value);
  }
  static void Rect(const char* id, int x, int y, int w, int h) {
    Serial.printf(",rect:%s=%d,%d,%d,%d", id, x, y, w, h);
  }
  static void Arc(const char* id, int deg) {
    Serial.printf(",arc:%s=%d", id, deg);
  }
  static void Flag(const char* id, bool on) {
    Serial.printf(",flag:%s=%d", id, on ? 1 : 0);
  }
  static void End() { Serial.println(); }

 private:
  static size_t len_;
};
size_t ScreenState::len_ = 0;

const char* PhaseName(game::Phase p) {
  switch (p) {
    case game::Phase::Idle: return "idle";
    case game::Phase::Offered: return "offered";
    case game::Phase::Printing: return "printing";
    case game::Phase::Authenticate: return "authenticate";
    case game::Phase::WarPlan: return "warplan";
    case game::Phase::Enable: return "enable";
    case game::Phase::Armed: return "armed";
    case game::Phase::Window: return "window";
    case game::Phase::Committed: return "committed";
    case game::Phase::Terminal: return "terminal";
    case game::Phase::Complete: return "complete";
    case game::Phase::Aborted: return "aborted";
  }
  return "?";
}

/// A stand-in for D4's DrawDrill, deliberately minimal.
///
/// It exists so this probe can be landed and exercised BEFORE D4 -- the
/// runbook's whole purpose is that the bench session does not wait on the
/// software queue. When DrawDrill lands it replaces this body and the log
/// format does not move, so the fixture captured today still grades it.
void DrawAndLog(uint32_t frame, const game::State& st) {
  const int c = SCREEN_SIZE / 2;
  ScreenState::Begin(frame, PhaseName(st.phase));

  tft.fillScreen(TFT_BLACK);                      ScreenState::Rect("bg", 0, 0, SCREEN_SIZE, SCREEN_SIZE);

  // EXERCISE marking, on every frame, in every phase. Tone rail, absolute.
  tft.setTextDatum(middle_center);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("EXERCISE", c, 18);              ScreenState::Text("exercise", "EXERCISE");

  if (st.phase == game::Phase::Armed) {
    const long secs = (long)(st.until_window_us / 1000000ull);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(secs).c_str(), c, c);   ScreenState::Num("countdown_s", secs);
  }
  if (st.phase == game::Phase::Window) {
    tft.drawCircle(c, c, c - 6, TFT_GREEN);       ScreenState::Arc("window_ring", 360);
  }
  if (st.phase == game::Phase::Printing) {
    ScreenState::Num("print_permille", st.progress_permille);
  }
  ScreenState::Flag("committed", st.committed);
  ScreenState::Flag("executed", st.executed);
  if (st.executed) ScreenState::Num("deviation_us", (long)st.deviation_us);
  if (st.note[0] != '\0') ScreenState::Text("note", st.note);
  ScreenState::End();
}

/// One scripted drill, driven at a fixed frame cadence with a key turn at a
/// chosen offset from T. No human, exact microseconds -- which is the one thing
/// a bench session cannot produce by hand.
void RunScript(const char* label, int64_t key_offset_us, bool key_at_all) {
  Serial.printf("\n# ---- script: %s (key %s%lld us from T) ----\n",
                label, key_at_all ? "" : "NOT PRESSED, ", (long long)key_offset_us);
  Serial.println("frame,phase,fields...");

  game::DrillMachine m;
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
  tft.setTextDatum(middle_center);

  RunScript("clean execution", 300000, true);
  RunScript("perfect execution", 0, true);
  RunScript("late, inside the window", 1900000, true);
  RunScript("no key at all", 0, false);

  Serial.println("\n# ---- done. four scripts captured. ----");
}

void loop() {
  delay(1000);
}
