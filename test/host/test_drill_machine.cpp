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

/// MessageArrived's payload for execution traffic. The machine's default is
/// NAM, so every execution-path test says which class it is working.
static game::EventArgs Exec() {
  game::EventArgs a;
  a.cls = game::MsgClass::Execution;
  return a;
}

/// A machine armed and ready, with T at `t`. Every step driven by a player.
static DrillMachine ArmedAt(uint64_t t, uint64_t now = 0) {
  DrillMachine m;
  m.SetT(t);
  m.Step(Event::MessageArrived, now, Exec());
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
  m.Step(Event::MessageArrived, 0, Exec());
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
    m.Step(Event::MessageArrived, 0, Exec());
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
  m.Step(Event::MessageArrived, 0, Exec());
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

// ---------------------------------------------------------------------------
// Fable, 2026-09-23: Decoded, the key arc, VoteResolved, arming needs a T.
// ---------------------------------------------------------------------------

static game::EventArgs Cls(game::MsgClass c) {
  game::EventArgs a;
  a.cls = c;
  return a;
}

static game::EventArgs Resolved(game::VoteOutcome o, const char* reason = nullptr) {
  game::EventArgs a;
  a.outcome = o;
  a.reason = reason;
  return a;
}

/// A machine in Window at T, with the execution path walked by a player.
static DrillMachine WindowAt(uint64_t t) {
  DrillMachine m = ArmedAt(t);
  m.Step(Event::Tick, t);
  return m;
}

/// A machine committed with a clean key at T + 100 ms.
static DrillMachine CommittedAt(uint64_t t) {
  DrillMachine m = WindowAt(t);
  m.Step(Event::PlayerKeyTurn, t + 100000);
  return m;
}

static void DecodedPath() {
  const game::MsgClass kinds[] = {game::MsgClass::Nam, game::MsgClass::Fdm};
  for (game::MsgClass k : kinds) {
    CASE(k == game::MsgClass::Nam ? "NAM: the print is the decode, then the static reveal"
                                  : "FDM: the print is the decode, then the static reveal");
    DrillMachine m;
    m.Step(Event::MessageArrived, 0, Cls(k));
    CHECK(m.Get().phase == Phase::Offered, "arrival did not offer");
    m.Step(Event::PlayerOpen, 1 * S);
    CHECK(m.Get().phase == Phase::Printing, "open did not print");
    m.Step(Event::Tick, 1 * S + 2 * S);  // past print_us: the print finishes on ticks
    CHECK(m.Get().phase == Phase::Decoded, "the print did not end at the class reveal");
    CHECK(!DrillMachine::IsAnimating(Phase::Decoded), "Decoded must be static");
    for (int i = 0; i < 100; i += 1) {
      m.Step(Event::Tick, (10 + i) * S);
      m.Step(Event::PrintFinished, (10 + i) * S);
      m.Step(Event::PlayerKeyTurn, (10 + i) * S);
      m.Step(Event::PlayerEnable, (10 + i) * S);
    }
    CHECK(m.Get().phase == Phase::Decoded, "something other than the ack moved the reveal");
    CHECK(!m.Get().committed, "a NAM/FDM committed a sortie");
    m.Step(Event::PlayerAck, 200 * S);
    CHECK(m.Get().phase == Phase::Complete, "CONFIRM COPY did not complete it");
  }

  CASE("execution traffic still goes to Authenticate");
  DrillMachine e;
  e.Step(Event::MessageArrived, 0, Cls(game::MsgClass::Execution));
  e.Step(Event::PlayerOpen, 0);
  e.Step(Event::PrintFinished, 0);
  CHECK(e.Get().phase == Phase::Authenticate, "an execution stopped at the reveal");

  CASE("a caller that forgets the class gets NAM, never a launch drill");
  DrillMachine d;
  d.Step(Event::MessageArrived, 0);
  d.Step(Event::PlayerOpen, 0);
  d.Step(Event::PrintFinished, 0);
  CHECK(d.Get().phase == Phase::Decoded, "the default class reached Authenticate");
}

static void ArmingNeedsAT() {
  CASE("no T (no synced clock): enable is refused in place, with the reason");
  DrillMachine m;
  m.Step(Event::MessageArrived, 0, Exec());
  m.Step(Event::PlayerOpen, 0);
  m.Step(Event::PrintFinished, 0);
  m.Step(Event::PlayerAck, 0);
  m.Step(Event::PlayerConfirmWarPlan, 0);
  m.Step(Event::PlayerEnable, 1 * S);
  CHECK(m.Get().phase == Phase::Enable, "armed with no T");
  CHECK(std::strstr(m.Get().note, "clock not synced") != nullptr, "the refusal is not explained");
  m.Step(Event::Tick, 100 * S);
  CHECK(m.Get().phase == Phase::Enable, "a tick armed it");
  m.SetT(200 * S);
  m.Step(Event::PlayerEnable, 101 * S);
  CHECK(m.Get().phase == Phase::Armed, "with a T, enable did not arm");
  CHECK(std::strlen(m.Get().note) == 0, "the stale refusal note survived arming");
}

static void KeyArc() {
  const uint64_t t = 100 * S;

  CASE("key: the ARC is scored, not the hold that confirms it");
  {
    DrillMachine m = WindowAt(t);
    m.Step(Event::PlayerKeyArc, t + 300000);
    CHECK(m.Get().phase == Phase::Window, "the arc alone decided something");
    m.Step(Event::Tick, t + 450000);
    m.Step(Event::PlayerKeyTurn, t + 600000);
    CHECK(m.Get().phase == Phase::Committed, "the confirmed turn did not commit");
    CHECK(m.Get().deviation_us == 300000, "scored at the confirm, not the arc");
  }

  CASE("key: an arc inside the window holds it open for its hold");
  {
    DrillMachine m = WindowAt(t);
    m.Step(Event::PlayerKeyArc, t + 1900000);
    m.Step(Event::Tick, t + 2100000);  // past the close, hold not yet done
    CHECK(m.Get().phase == Phase::Window, "the window shut on a pending arc");
    m.Step(Event::PlayerKeyTurn, t + 2200000);
    CHECK(m.Get().phase == Phase::Committed, "an on-time arc confirmed late was refused");
    CHECK(m.Get().deviation_us == 1900000, "not scored at the arc");
  }

  CASE("key: a release voids the turn, and the window then closes normally");
  {
    DrillMachine m = WindowAt(t);
    m.Step(Event::PlayerKeyArc, t + 500000);
    m.Step(Event::PlayerKeyRelease, t + 600000);
    CHECK(!m.Get().key_pending, "the release left the turn pending");
    m.Step(Event::Tick, t + 2 * S + 1);
    CHECK(m.Get().phase == Phase::Aborted, "a released turn kept the window open");
    CHECK(!m.Get().executed, "a released turn was recorded as an execution");
  }

  CASE("key: after a release the player may try again; the second arc is scored");
  {
    DrillMachine m = WindowAt(t);
    m.Step(Event::PlayerKeyArc, t + 200000);
    m.Step(Event::PlayerKeyRelease, t + 300000);
    m.Step(Event::PlayerKeyArc, t + 900000);
    m.Step(Event::PlayerKeyTurn, t + 1200000);
    CHECK(m.Get().phase == Phase::Committed, "the retry did not commit");
    CHECK(m.Get().deviation_us == 900000, "the retry was scored at the wrong arc");
  }

  CASE("key: an arc before T confirmed after T is EARLY, because it was");
  {
    DrillMachine m = ArmedAt(t);
    m.Step(Event::PlayerKeyArc, t - 100000);
    m.Step(Event::Tick, t);
    CHECK(m.Get().phase == Phase::Window, "the pending arc stopped the window opening");
    m.Step(Event::PlayerKeyTurn, t + 200000);
    CHECK(m.Get().phase == Phase::Aborted, "an early arc was scored as on time");
    CHECK(m.Get().deviation_us == -100000, "the early arc was not recorded at -100 ms");
    CHECK(std::strstr(m.Get().note, "before the window") != nullptr, "not explained");
  }

  CASE("key: a pending arc whose release was lost expires; the window closes");
  {
    DrillMachine m = WindowAt(t);
    m.Step(Event::PlayerKeyArc, t + 500000);
    m.Step(Event::Tick, t + 3 * S);
    CHECK(m.Get().phase == Phase::Aborted, "a lost release held the window open forever");
  }
}

static void VoteResolution() {
  const uint64_t t = 100 * S;

  CASE("resolved from Committed: launched or seconded -> Terminal");
  {
    DrillMachine a = CommittedAt(t);
    a.Step(Event::VoteResolved, t + 60 * S, Resolved(game::VoteOutcome::Launched));
    CHECK(a.Get().phase == Phase::Terminal, "a launch did not start the terminal countdown");
    DrillMachine b = CommittedAt(t);
    b.Step(Event::VoteResolved, t + 60 * S, Resolved(game::VoteOutcome::Seconded));
    CHECK(b.Get().phase == Phase::Terminal, "a second did not start the terminal countdown");
    b.Step(Event::Tick, t + 60 * S + 31 * S);
    CHECK(b.Get().phase == Phase::Complete, "the terminal countdown did not run out");
  }

  CASE("resolved from Committed: inhibited or failed -> Aborted, with the reason");
  {
    DrillMachine a = CommittedAt(t);
    a.Step(Event::VoteResolved, t + 60 * S, Resolved(game::VoteOutcome::Inhibited, "not our call"));
    CHECK(a.Get().phase == Phase::Aborted, "an inhibit did not end the drill");
    CHECK(std::strcmp(a.Get().note, "not our call") == 0, "the inhibit's reason was not kept");
    CHECK(a.Get().executed, "the inhibit erased the measured execution");
    DrillMachine b = CommittedAt(t);
    b.Step(Event::VoteResolved, t + 60 * S, Resolved(game::VoteOutcome::Failed));
    CHECK(b.Get().phase == Phase::Aborted, "a failure did not end the drill");
    CHECK(std::strstr(b.Get().note, "failed") != nullptr, "the failure is not explained");
  }

  CASE("VoteResolved from any phase but Committed is rejected");
  {
    // Every other phase, reached by a player, then offered every outcome.
    DrillMachine phases[12];
    int n = 0;
    phases[n++] = DrillMachine();  // Idle
    { DrillMachine m; m.Step(Event::MessageArrived, 0, Exec()); phases[n++] = m; }  // Offered
    { DrillMachine m; m.Step(Event::MessageArrived, 0, Exec()); m.Step(Event::PlayerOpen, 0); phases[n++] = m; }  // Printing
    { DrillMachine m; m.Step(Event::MessageArrived, 0); m.Step(Event::PlayerOpen, 0); m.Step(Event::PrintFinished, 0); phases[n++] = m; }  // Decoded
    { DrillMachine m; m.SetT(t); m.Step(Event::MessageArrived, 0, Exec()); m.Step(Event::PlayerOpen, 0); m.Step(Event::PrintFinished, 0); phases[n++] = m; }  // Authenticate
    { DrillMachine m = phases[n - 1]; m.Step(Event::PlayerAck, 0); phases[n++] = m; }  // WarPlan
    { DrillMachine m = phases[n - 1]; m.Step(Event::PlayerConfirmWarPlan, 0); phases[n++] = m; }  // Enable
    phases[n++] = ArmedAt(t);  // Armed
    phases[n++] = WindowAt(t);  // Window
    { DrillMachine m = CommittedAt(t); m.Step(Event::VoteResolved, t + S, Resolved(game::VoteOutcome::Launched)); phases[n++] = m; }  // Terminal
    { DrillMachine m; m.Step(Event::MessageArrived, 0); m.Step(Event::PlayerOpen, 0); m.Step(Event::PrintFinished, 0); m.Step(Event::PlayerAck, 0); phases[n++] = m; }  // Complete
    { DrillMachine m = ArmedAt(t); m.Step(Event::PlayerAbort, 0); phases[n++] = m; }  // Aborted
    const game::VoteOutcome outs[] = {game::VoteOutcome::Seconded, game::VoteOutcome::Launched,
                                      game::VoteOutcome::Inhibited, game::VoteOutcome::Failed};
    int checked = 0;
    bool seen[16] = {false};
    for (int i = 0; i < n; i += 1) {
      CHECK(phases[i].Get().phase != Phase::Committed, "the setup reached Committed by accident");
      seen[static_cast<int>(phases[i].Get().phase)] = true;
      for (game::VoteOutcome o : outs) {
        DrillMachine m = phases[i];
        const Phase before = m.Get().phase;
        m.Step(Event::VoteResolved, t + 2 * S, Resolved(o, "x"));
        CHECK(m.Get().phase == before, "VoteResolved moved a drill that was not Committed");
        checked += 1;
      }
    }
    int distinct = 0;
    for (bool b : seen) distinct += b ? 1 : 0;
    CHECK(distinct == 12, "the setup did not reach all twelve non-Committed phases");
    CHECK(checked == 12 * 4, "not every phase/outcome pair was tried");
  }

  CASE("Committed outlives a feed reconnect and other traffic, then resolves");
  {
    DrillMachine m = CommittedAt(t);
    for (int i = 0; i < 20; i += 1) {
      m.Step(Event::FeedReconnected, t + (10 + i) * S);
      m.Step(Event::OtherMessageArrived, t + (10 + i) * S);
      m.Step(Event::MessageArrived, t + (10 + i) * S);
      m.Step(Event::Tick, t + (10 + i) * S);
    }
    CHECK(m.Get().phase == Phase::Committed, "Committed did not survive the world happening");
    m.Step(Event::VoteResolved, t + 40 * S, Resolved(game::VoteOutcome::Launched));
    CHECK(m.Get().phase == Phase::Terminal, "the resolution after a reconnect was lost");
  }
}

/// RAIL 1, EXHAUSTIVELY. Random sequences of EVERY event (player and world,
/// every outcome, every class) at random instants, from a fixed seed. Each
/// transition is checked against the rail:
///   into Printing   only by PlayerOpen
///   into Committed  only by PlayerKeyTurn
///   into Terminal   only by VoteResolved, from Committed
/// and Terminal is never reached in a sequence with no PlayerKeyTurn in it.
static uint32_t g_seed = 0x5eed1234u;
static uint32_t Rnd(uint32_t n) {
  g_seed = g_seed * 1664525u + 1013904223u;
  return (g_seed >> 8) % n;
}

static void RailExhaustive() {
  CASE("rail 1, randomised: animating phases only ever follow a human");
  const Event events[] = {
      Event::Tick, Event::MessageArrived, Event::PlayerOpen, Event::PrintFinished,
      Event::PlayerAck, Event::PlayerConfirmWarPlan, Event::PlayerEnable,
      Event::PlayerKeyTurn, Event::PlayerAbort, Event::FeedReconnected,
      Event::OtherMessageArrived, Event::PlayerKeyArc, Event::PlayerKeyRelease,
      Event::VoteResolved};
  const uint32_t kEv = static_cast<uint32_t>(sizeof(events) / sizeof(events[0]));
  int transitions = 0, terminals = 0, printings = 0, committeds = 0, violations = 0;
  for (int seq = 0; seq < 20000; seq += 1) {
    DrillMachine m;
    if (Rnd(4) != 0) m.SetT((5 + Rnd(30)) * S);
    uint64_t now = 0;
    bool keyed = false;
    for (int i = 0; i < 40; i += 1) {
      Event ev = events[Rnd(kEv)];
      game::EventArgs a;
      a.cls = static_cast<game::MsgClass>(Rnd(3));
      a.outcome = static_cast<game::VoteOutcome>(Rnd(5));
      now += Rnd(4) == 0 ? Rnd(3000000) : Rnd(200000);
      // HALF THE STEPS ARE GUIDED toward the next phase, or the sweep never
      // gets deep enough for its checks to mean anything (the first version
      // reached Committed 3 times in 20,000 sequences and Terminal never). The
      // guided event is still an ordinary event, checked by the same rules.
      if (Rnd(2) == 0) {
        switch (m.Get().phase) {
          case Phase::Idle: ev = Event::MessageArrived; break;
          case Phase::Offered: ev = Event::PlayerOpen; break;
          case Phase::Printing: ev = Event::PrintFinished; break;
          case Phase::Decoded: case Phase::Authenticate: ev = Event::PlayerAck; break;
          case Phase::WarPlan: ev = Event::PlayerConfirmWarPlan; break;
          case Phase::Enable: ev = Event::PlayerEnable; break;
          case Phase::Armed:
            ev = Event::Tick;
            if (now < m.Get().t_at_us) now = m.Get().t_at_us;
            break;
          case Phase::Window: ev = Rnd(2) ? Event::PlayerKeyTurn : Event::PlayerKeyArc; break;
          case Phase::Committed: ev = Event::VoteResolved; break;
          default: break;
        }
      }
      const Phase before = m.Get().phase;
      m.Step(ev, now, a);
      const Phase after = m.Get().phase;
      if (ev == Event::PlayerKeyTurn) keyed = true;
      if (after == before) continue;
      transitions += 1;
      if (after == Phase::Printing) {
        printings += 1;
        if (ev != Event::PlayerOpen) violations += 1;
      }
      if (after == Phase::Committed) {
        committeds += 1;
        if (ev != Event::PlayerKeyTurn) violations += 1;
      }
      if (after == Phase::Terminal) {
        terminals += 1;
        if (ev != Event::VoteResolved || before != Phase::Committed || !keyed) violations += 1;
      }
      if (DrillMachine::IsAnimating(after) && ev == Event::MessageArrived) violations += 1;
    }
  }
  CHECK(violations == 0, "an animating phase or Committed was entered without the rail's human act");
  // THE CONTROLS: a sweep that never reached these phases proves nothing.
  CHECK(printings > 100, "the sweep never printed");
  CHECK(committeds > 10, "the sweep never committed");
  CHECK(terminals > 1, "the sweep never reached Terminal");
  std::printf("       %d transitions: %d printing, %d committed, %d terminal\n", transitions,
              printings, committeds, terminals);
}

int main() {
  std::printf("DrillMachine host tests\n");
  RailNothingAnimatesWithoutAHuman();
  HappyPath();
  TheWindow();
  TAlreadyPast();
  Interruptions();
  RenderableValues();
  DecodedPath();
  ArmingNeedsAT();
  KeyArc();
  VoteResolution();
  RailExhaustive();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  if (g_checks < 40) {
    std::printf("FAIL: only %d checks ran; the suite did not execute\n", g_checks);
    return 2;
  }
  return g_failures == 0 ? 0 : 1;
}
