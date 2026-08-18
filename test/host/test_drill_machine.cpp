// Host tests for DrillMachine. No framework: a header-only check macro, because
// pulling in a test framework would be the first thing to drag a dependency
// into a build whose entire purpose is having none.
//
// Built and run by test/host/run.sh. See test/host/README.md.

#include "../../src/game/DrillMachine.h"

#include <cstdio>
#include <cstring>

using game::Config;
using game::DrillMachine;
using game::Event;
using game::Phase;

static int g_failures = 0;
static int g_checks = 0;
static const char* g_case = "";

#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    g_checks += 1;                                                              \
    if (!(cond)) {                                                              \
      g_failures += 1;                                                          \
      std::printf("  FAIL %s\n       %s\n       at %s:%d\n", g_case, msg,       \
                  __FILE__, __LINE__);                                          \
    }                                                                           \
  } while (0)

#define CASE(name)                                                              \
  g_case = name;                                                                \
  std::printf("  ---- %s\n", name);

static const uint64_t S = 1000000ull;  // one second in microseconds

/// A machine armed and ready, with T at `t`. Every step driven by a player.
static DrillMachine ArmedAt(uint64_t t, uint64_t now = 0) {
  DrillMachine m;
  m.SetT(t);
  m.Step(Event::MessageArrived, now);
  m.Step(Event::PlayerOpen, now);
  m.Step(Event::PrintFinished, now);
  m.Step(Event::PlayerAck, now);
  m.Step(Event::PlayerConfirmWarPlan, now);
  m.Step(Event::PlayerEnable, now);
  return m;
}

// ---------------------------------------------------------------------------

/// RAIL 1. The pin the work order asks for by name: feed a burst of real
/// traffic with no input and assert no animation state is ever entered.
static void RailNothingAnimatesWithoutAHuman() {
  CASE("rail 1: a traffic burst enters no animating phase");
  DrillMachine m;
  m.SetT(10 * S);
  uint64_t now = 0;
  for (int i = 0; i < 500; i += 1) {
    // Everything the world can do, and nothing a person can.
    m.Step(Event::MessageArrived, now);
    m.Step(Event::OtherMessageArrived, now);
    m.Step(Event::FeedReconnected, now);
    m.Step(Event::Tick, now);
    m.Step(Event::PrintFinished, now);
    now += 40000;  // 25 Hz, ~20 s of traffic
    CHECK(!DrillMachine::IsAnimating(m.Get().phase),
          "an animating phase was entered with no player input");
    CHECK(m.Get().phase == Phase::Offered,
          "a burst moved the drill somewhere other than Offered");
  }
  CHECK(!m.Get().committed, "a burst committed a sortie with no ack");

  // THE POSITIVE CONTROL, and the half that makes the loop above mean
  // something: a machine that never advanced at all would pass it perfectly.
  CASE("rail 1: and a human DOES move it");
  m.Step(Event::PlayerOpen, now);
  CHECK(m.Get().phase == Phase::Printing, "PlayerOpen did not start the print");
  CHECK(DrillMachine::IsAnimating(m.Get().phase), "Printing is not counted as animating");
}

/// The happy path, end to end, and the deviation it reports.
static void HappyPath() {
  CASE("the six steps, in order, each on a player act");
  const uint64_t t = 60 * S;
  DrillMachine m;
  m.SetT(t);
  m.Step(Event::MessageArrived, 0);
  CHECK(m.Get().phase == Phase::Offered, "arrival did not offer");
  m.Step(Event::PlayerOpen, 1 * S);
  CHECK(m.Get().phase == Phase::Printing, "open did not print");
  m.Step(Event::PrintFinished, 2 * S);
  CHECK(m.Get().phase == Phase::Authenticate, "print did not reach authenticate");
  CHECK(!m.Get().committed, "committed before the ack");
  m.Step(Event::PlayerAck, 3 * S);
  CHECK(m.Get().phase == Phase::WarPlan, "ack did not reach the war plan");
  CHECK(m.Get().committed, "the ack did not commit the sortie");
  m.Step(Event::PlayerConfirmWarPlan, 4 * S);
  CHECK(m.Get().phase == Phase::Enable, "war plan did not reach enable");
  m.Step(Event::PlayerEnable, 5 * S);
  CHECK(m.Get().phase == Phase::Armed, "enable did not arm");

  // The countdown runs on ticks, and only ticks.
  m.Step(Event::Tick, 50 * S);
  CHECK(m.Get().phase == Phase::Armed, "armed too early");
  CHECK(m.Get().until_window_us == 10 * S, "the countdown does not read 10 s");

  m.Step(Event::Tick, t);
  CHECK(m.Get().phase == Phase::Window, "the window did not open at T");

  m.Step(Event::PlayerKeyTurn, t + 300000);
  CHECK(m.Get().phase == Phase::Committed, "the key turn did not commit");
  CHECK(m.Get().executed, "the execution was not recorded");
  CHECK(m.Get().deviation_us == 300000, "deviation is not +300 ms");
}

/// §12's published 2-second window, at every edge that matters.
static void TheWindow() {
  const uint64_t t = 100 * S;

  CASE("window: one tick before T is not open");
  {
    DrillMachine m = ArmedAt(t);
    m.Step(Event::Tick, t - 1);
    CHECK(m.Get().phase == Phase::Armed, "the window opened before T");
  }

  CASE("window: exactly T opens it");
  {
    DrillMachine m = ArmedAt(t);
    m.Step(Event::Tick, t);
    CHECK(m.Get().phase == Phase::Window, "the window did not open at exactly T");
  }

  CASE("window: a key turn at exactly T reads zero deviation");
  {
    DrillMachine m = ArmedAt(t);
    m.Step(Event::Tick, t);
    m.Step(Event::PlayerKeyTurn, t);
    CHECK(m.Get().deviation_us == 0, "a perfect key turn is not zero");
    CHECK(m.Get().phase == Phase::Committed, "a perfect key turn did not commit");
  }

  CASE("window: the last instant is still open");
  {
    // [T, T+2s]. Treating the final instant as shut makes the published
    // constant 2 s minus one tick, which is a different rule quietly.
    DrillMachine m = ArmedAt(t);
    m.Step(Event::Tick, t);
    m.Step(Event::Tick, t + 2 * S);
    CHECK(m.Get().phase == Phase::Window, "the window shut at exactly T+2s");
    m.Step(Event::PlayerKeyTurn, t + 2 * S);
    CHECK(m.Get().phase == Phase::Committed, "a key at the last instant was refused");
    CHECK(m.Get().deviation_us == 2000000, "deviation at the edge is not +2 s");
  }

  CASE("window: one tick past the close is shut");
  {
    DrillMachine m = ArmedAt(t);
    m.Step(Event::Tick, t);
    m.Step(Event::Tick, t + 2 * S + 1);
    CHECK(m.Get().phase == Phase::Aborted, "the window stayed open past its close");
    CHECK(!m.Get().executed, "an unexecuted drill claims an execution");
    CHECK(std::strstr(m.Get().note, "window closed") != nullptr,
          "the reason the drill ended is not stated");
  }

  CASE("window: no input at all ends aborted, committed, and logged");
  {
    DrillMachine m = ArmedAt(t);
    for (uint64_t now = t - 5 * S; now <= t + 10 * S; now += 100000) {
      m.Step(Event::Tick, now);
    }
    CHECK(m.Get().phase == Phase::Aborted, "silence did not end the drill");
    // §4: "Acking commits you; missing T after commitment is a logged failed
    // execution." The commitment must SURVIVE the failure or the ratio the
    // score is built on cannot count the denominator.
    CHECK(m.Get().committed, "the failed execution forgot it was committed");
    CHECK(!m.Get().executed, "a missed window recorded an execution");
  }

  CASE("window: a key turn before it opens is refused, and recorded");
  {
    DrillMachine m = ArmedAt(t);
    m.Step(Event::Tick, t - 3 * S);
    m.Step(Event::PlayerKeyTurn, t - 3 * S);
    CHECK(m.Get().phase == Phase::Aborted, "an early key turn was treated as an execution");
    CHECK(m.Get().deviation_us == -3000000, "the early turn was not recorded as -3 s");
    // A control that silently discarded the early turn's measurement failed
    // nothing until this line existed: the device would show the drill dead
    // with no record of the thing the player is certain they did.
    CHECK(m.Get().executed, "the early key turn was swallowed rather than recorded");
    CHECK(std::strstr(m.Get().note, "before the window") != nullptr,
          "the early turn is not explained");
  }

  CASE("window: a configured width is what governs, not a baked one");
  {
    // RAIL 3 in miniature. If 2 s were hardcoded, this drill would abort at
    // t+2s and never see its own window.
    Config cfg;
    cfg.window_us = 5 * S;
    DrillMachine m(cfg);
    m.SetT(t);
    m.Step(Event::MessageArrived, 0);
    m.Step(Event::PlayerOpen, 0);
    m.Step(Event::PrintFinished, 0);
    m.Step(Event::PlayerAck, 0);
    m.Step(Event::PlayerConfirmWarPlan, 0);
    m.Step(Event::PlayerEnable, 0);
    m.Step(Event::Tick, t);
    m.Step(Event::Tick, t + 4 * S);
    CHECK(m.Get().phase == Phase::Window, "a 5 s window closed at 2 s — the width is baked in");
  }
}

/// T already past when the drill starts — §6's ~2-minute snap-execution tier
/// makes this ordinary rather than exotic.
static void TAlreadyPast() {
  CASE("T already past when the drill arms");
  const uint64_t t = 10 * S;
  DrillMachine m = ArmedAt(t, 11 * S);
  m.Step(Event::Tick, 11 * S);
  CHECK(m.Get().phase == Phase::Window, "a drill armed after T never opened its window");
  m.Step(Event::PlayerKeyTurn, 11 * S);
  CHECK(m.Get().deviation_us == 1000000, "the late execution is not +1 s");

  CASE("T long past: the window is already shut");
  DrillMachine n = ArmedAt(t, 30 * S);
  n.Step(Event::Tick, 30 * S);
  CHECK(n.Get().phase == Phase::Aborted, "a drill armed 20 s after T offered a window");
}

/// Things that happen TO the drill and must not disturb it.
static void Interruptions() {
  CASE("a new message mid-drill does not disturb this one");
  const uint64_t t = 60 * S;
  DrillMachine m = ArmedAt(t);
  m.Step(Event::Tick, 50 * S);
  const uint64_t before = m.Get().until_window_us;
  for (int i = 0; i < 50; i += 1) m.Step(Event::OtherMessageArrived, 50 * S);
  CHECK(m.Get().phase == Phase::Armed, "another message moved the drill");
  CHECK(m.Get().until_window_us == before, "another message disturbed the countdown");
  CHECK(m.Get().committed, "another message discarded the commitment");

  CASE("the drill survives a feed reconnect");
  DrillMachine n = ArmedAt(t);
  n.Step(Event::Tick, 55 * S);
  n.Step(Event::FeedReconnected, 55 * S);
  CHECK(n.Get().phase == Phase::Armed, "a reconnect dropped the drill");
  n.Step(Event::Tick, t);
  n.Step(Event::PlayerKeyTurn, t + 100000);
  CHECK(n.Get().deviation_us == 100000, "a reconnect disturbed the measurement");

  CASE("abort partway leaves the commitment logged");
  DrillMachine a = ArmedAt(t);
  a.Step(Event::PlayerAbort, 40 * S);
  CHECK(a.Get().phase == Phase::Aborted, "abort did not abort");
  CHECK(a.Get().committed, "abort erased the commitment it should log");
  CHECK(!a.Get().executed, "abort recorded an execution");

  CASE("a terminal state stays terminal");
  a.Step(Event::PlayerKeyTurn, t);
  a.Step(Event::Tick, t + 10 * S);
  CHECK(a.Get().phase == Phase::Aborted, "an aborted drill came back to life");
  CHECK(!a.Get().executed, "an aborted drill recorded a late execution");
}

/// The values a renderer draws, which is the other half of what this TU is for.
static void RenderableValues() {
  CASE("progress is monotonic and clamped");
  DrillMachine m;
  m.SetT(60 * S);
  m.Step(Event::MessageArrived, 0);
  m.Step(Event::PlayerOpen, 0);
  uint16_t last = 0;
  for (uint64_t now = 0; now <= 2 * S; now += 50000) {
    m.Step(Event::Tick, now);
    if (m.Get().phase != Phase::Printing) break;
    CHECK(m.Get().progress_permille >= last, "progress went backwards");
    CHECK(m.Get().progress_permille <= 1000, "progress exceeded 1000");
    last = m.Get().progress_permille;
  }
  CHECK(m.Get().phase == Phase::Authenticate, "the print never finished on ticks alone");

  CASE("the countdown never reads a wrapped value, even armed late");
  {
    // THE FRAME BETWEEN. Arming after T runs `Remaining(t, now)` with now > t
    // before the phase changes, and unsigned underflow there reads ~584,000
    // years -- which renders as a plausible number rather than as an error.
    // A control that removed the saturation failed nothing until this case
    // existed, because every other path overwrites the value before it is read.
    DrillMachine late = ArmedAt(10 * S, 10 * S + 500000);
    late.Step(Event::Tick, 10 * S + 500000);
    CHECK(late.Get().until_window_us == 0,
          "the countdown wrapped instead of saturating when armed after T");
  }

  CASE("the countdown saturates rather than wrapping");
  // Unsigned underflow would read ~584,000 years, which renders as a plausible
  // number rather than as an error.
  DrillMachine n = ArmedAt(10 * S);
  n.Step(Event::Tick, 9 * S);
  CHECK(n.Get().until_window_us == 1000000, "the countdown is wrong before T");
  n.Step(Event::Tick, 10 * S);
  CHECK(n.Get().until_window_us == 0, "the countdown did not reach zero");
}

int main() {
  std::printf("DrillMachine host tests\n");
  RailNothingAnimatesWithoutAHuman();
  HappyPath();
  TheWindow();
  TAlreadyPast();
  Interruptions();
  RenderableValues();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  if (g_checks < 40) {
    std::printf("FAIL: only %d checks ran; the suite did not execute\n", g_checks);
    return 2;
  }
  return g_failures == 0 ? 0 : 1;
}
