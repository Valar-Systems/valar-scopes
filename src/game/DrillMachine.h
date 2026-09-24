// DrillMachine — the six-step REACT drill, as state and nothing else.
//
// ===========================================================================
// THIS TRANSLATION UNIT IS PURE, AND THAT IS THE POINT OF IT.
//
// The eleven Draw* functions in src/eam/EamScreens.cpp interleave state with
// rendering, which is why no firmware behaviour in this project has ever been
// testable without a human looking at a panel. The drill must not repeat that.
//
// So: no LVGL, no BandCanvas, no Arduino.h, no ESP-IDF. No millis(), no Serial,
// no String, no heap. Time arrives as a parameter. The only includes are
// <stdint.h> and <stddef.h>, and test/host/no_arduino.cpp exists to make the
// build FAIL if that ever stops being true.
//
// BOTH of them, explicitly. <stddef.h> was missing for one commit: MinGW pulls
// size_t in through <stdint.h> transitively and the xtensa toolchain does not,
// so the host rig was green and the device build failed. The host rig proves
// the logic; it does not prove the TU compiles for the board, and nothing
// should read it as doing so.
//
// Inputs are (event, monotonic_us). Outputs are a phase plus the plain values a
// renderer would draw. A renderer consumes State and holds nothing.
// ===========================================================================
//
// RAIL 1 — NOTHING ANIMATES WITHOUT A HUMAN.
//
// Design principle #1, and the failure the whole tone posture exists to
// prevent: "the device auto-plays a launch animation during a real-world crisis
// traffic spike". Every transition into a phase that moves is caused by a
// player event. A message ARRIVING moves the machine to Offered, which is
// static and says only that something is waiting.
//
// The rule is enforced structurally rather than by review: Step() rejects every
// phase advance that is not carried by a player event, and IsAnimating() names
// the phases that move so a test can assert none is ever entered without one.
//
// RAIL 2 — SOLO PATH ONLY. Step 3's cooperative enable is a two-person
// split-knowledge minigame, gated on a hardware measurement (can the panel hold
// a finger for 10 s?) that has not been taken. §13: "if the panel cannot hold a
// finger for 10 s, the mechanic does not exist." The solo path treats the
// enable as the player's own act. There is no deputy in this file, and adding
// one before the arm runs land would be writing against a measurement that does
// not exist.
//
// RAIL 3 — THE DEVICE DOES NOT INVENT SCORING, OR ITS PRESENTATION. There is no
// bucketS, metresPerBucket, shackFloorM, maxMissM or clockFloorMs here, and no
// deviation curve. The machine records WHEN the key turned relative to T and
// reports the signed offset in microseconds. Scoring happens on the server. Two
// implementations of the deviation curve is how the fiction drifts.
//
// EXTENDED after the renderer shipped a second implementation of the
// QUANTIZATION while obeying the constants perfectly. There was no `0.2`
// anywhere in it; it divided by 100000 and produced tenths, so one sortie read
// `+0.3 s` here and `0.4` on the leaderboard. Rounding direction, bucket width
// and decimal places are not constants, so sharing constants cannot detect a
// disagreement about any of them.
//
// A shared constant proves the two sides agree about a NUMBER. Only a shared
// OUTPUT proves they agree about what to do with it — which is why the figure
// is graded against strings the server itself produced (src/game/GameFormat.h,
// test/fixtures/). See valar-eam-feed docs/verification-ledger.md entry 38.
//
// The 2-second cooperative window IS a published constant (§12, Nuclear
// Companion) — but it is passed in through Config rather than baked here, so
// the device displays the rule the server is playing by. The default in
// Config's initialiser cites its source and exists so a test can construct one
// without ceremony.

#ifndef BLIPSCOPE_GAME_DRILLMACHINE_H
#define BLIPSCOPE_GAME_DRILLMACHINE_H

#include <stddef.h>
#include <stdint.h>

#include "Derive.h"  // MsgClass. Pure, like this file.

namespace game {

/// Where the drill is. Ordered by progress through §3's six steps.
enum class Phase : uint8_t {
  /// No message is being worked. The resting state.
  Idle = 0,
  /// A message is present and untouched. STATIC — this is what a traffic burst
  /// produces, and it must never be mistaken for the drill having started.
  Offered,
  /// §3 step 1 — the paper strip printing. Animates; entered only on PlayerOpen.
  Printing,
  /// §3 step 1 for a NAM or FDM: the print IS the decode, and this is the class
  /// reveal (§5 "Banner tap runs the decoder ... then the class reveal").
  /// STATIC. Left only by PlayerAck (§5's CONFIRM COPY) or PlayerAbort.
  /// Fable, 2026-09-23.
  Decoded,
  /// §3 step 1 — the padlocked SAS safe. Awaiting the player's ack.
  Authenticate,
  /// §3 step 2 — confirm or adjust the war plan. Locks at T-X.
  WarPlan,
  /// §3 step 3 — enable. Solo path: the player's own act (rail 2).
  Enable,
  /// Armed and waiting for the window to open. The countdown runs here.
  Armed,
  /// The published 2-second window is OPEN. The key turn counts now.
  Window,
  /// §3 step 5 — the execution registered; the fleet can second or inhibit.
  Committed,
  /// §3 step 6 — the published 30-second terminal countdown.
  Terminal,
  /// The drill finished. `deviation_us` is the answer.
  Complete,
  /// The player aborted, or the window closed with no key turn.
  Aborted,
};

/// What can happen to the drill. Everything a human does is a Player* event.
enum class Event : uint8_t {
  /// Time passed. Carries no intent and can never start an animation.
  Tick = 0,
  /// A message arrived from the feed. Not a human act — moves Idle -> Offered.
  MessageArrived,
  /// The player opened the message. §3 step 1's print begins here.
  PlayerOpen,
  /// The paper strip finished printing. Not a human act; the human started it.
  PrintFinished,
  /// The player acked. §4: "Acking commits you."
  PlayerAck,
  /// The player confirmed the war plan. §3 step 2.
  PlayerConfirmWarPlan,
  /// The player enabled. §3 step 3, solo path.
  PlayerEnable,
  /// The key turn is CONFIRMED (the hold after the arc completed). §3 step 4.
  /// The instant SCORED is the arc's completion if a PlayerKeyArc preceded
  /// this, else this event's own instant (Fable, 2026-09-23: "arc completes").
  PlayerKeyTurn,
  /// The player abandoned the drill.
  PlayerAbort,
  /// The feed dropped and came back. The drill must survive it.
  FeedReconnected,
  /// A different message arrived mid-drill. Must not disturb this one.
  OtherMessageArrived,
  /// The press-drag along the bezel reached the end of its arc, finger still
  /// down. Records the SCORED instant; nothing is decided until the hold
  /// confirms it (PlayerKeyTurn) or the finger lifts (PlayerKeyRelease).
  PlayerKeyArc,
  /// The finger lifted before the hold confirmed the turn. The turn is void and
  /// the player may try again while the window is open.
  PlayerKeyRelease,
  /// The server resolved this drill's vote (polled from /votes/live). Carries
  /// the outcome in EventArgs. Valid ONLY from Committed; ignored everywhere
  /// else (Fable, 2026-09-23). Not a player event: rail 1 holds because
  /// Committed is reachable only through PlayerKeyTurn.
  VoteResolved,
  /// The served HOLD flag was set (§1.2). Any live drill -> Aborted, reason "hold"
  /// (Fable, 2026-09-23: one bit reverts the fleet to pure monitoring; a pause
  /// would imply resumption). Not a player event, and it animates nothing.
  Hold,
};

/// How the server resolved the vote. Only the four the ruling names.
enum class VoteOutcome : uint8_t {
  None = 0,
  /// A crew seconded it: the launch proceeds. -> Terminal.
  Seconded,
  /// Launched (seconded, or the dead-man timer expired unopposed). -> Terminal.
  Launched,
  /// A squadron-mate inhibited it. -> Aborted, with the reason.
  Inhibited,
  /// The execution failed (missed or outside the window). -> Aborted, with the reason.
  Failed,
  /// The crew stopped the drill (the server's ABORTED). -> Aborted, and NEVER shown as a
  /// failure: a player who stops a drill is never shown FAILED (Fable, 2026-09-24).
  Aborted,
};

/// What an event carries, for the two events that carry anything.
struct EventArgs {
  /// MessageArrived: the class DERIVED for the message (src/game/Derive.h).
  /// Defaults to NAM, the class that cannot reach an execution, so a caller
  /// that forgets to say gets "nothing happens" rather than a launch drill.
  MsgClass cls = MsgClass::Nam;
  /// MessageArrived: the monotonic instant the offer is WITHDRAWN -- the ack
  /// cutoff (served ackCutoffS before T). 0 = never (NAM/FDM carry no T).
  uint64_t withdraw_at_us = 0;
  /// VoteResolved: the outcome, and the server's reason text (may be null).
  VoteOutcome outcome = VoteOutcome::None;
  const char* reason = nullptr;
};

/// The published rules, supplied rather than invented (rail 3).
struct Config {
  /// §12, Nuclear Companion: the cooperative concurrency window, 2 s.
  uint32_t window_us = 2000000u;
  /// §12, Nuclear Companion: the terminal countdown, 30 s.
  uint32_t terminal_us = 30000000u;
  /// How long the paper strip takes to print. Cosmetic; not a published rule.
  uint32_t print_us = 1200000u;
  /// How long the hold after the arc must last to confirm the key turn.
  /// §13-D TUNABLE (gestures are tunables; Fable, 2026-09-23), not a published
  /// rule. Here only so a pending turn can be expired if its release is lost.
  uint32_t key_confirm_us = 300000u;

  /// The scoring bucket, in microseconds. ZERO MEANS UNKNOWN, and unknown is
  /// not zero-the-number -- it is "the server has not told us yet".
  ///
  /// NO DEFAULT, unlike `window_us` above, and the asymmetry is the point.
  /// The 2 s window is a §12 PUBLISHED constant: part of the fiction, and it
  /// cannot move without a design change. The bucket is an OPERATIONAL knob
  /// with a `GAME_SCORE_BUCKET_S` env override on the server, so a default
  /// baked here would be a second copy that drifts the first time it is tuned
  /// -- and it would drift into a plausible number rather than an error.
  ///
  /// A device that has not fetched /config has no figure to show. Saying so is
  /// the honest screen; guessing 0.2 is how the two sides stop agreeing.
  uint32_t bucket_us = 0;
};

/// Everything a renderer needs, and nothing it would have to compute.
struct State {
  Phase phase = Phase::Idle;

  /// Monotonic microseconds at which the window OPENS. Zero when unknown.
  uint64_t t_at_us = 0;
  /// Microseconds until the window opens; 0 once it has. For the countdown.
  uint64_t until_window_us = 0;
  /// Microseconds until the terminal countdown ends. Only meaningful in Terminal.
  uint64_t until_impact_us = 0;

  /// Progress through the current animating phase, 0..1000 (per mille).
  ///
  /// Integer rather than float: this is a device that has no business doing
  /// floating-point in a draw path, and a renderer wants a fraction of a bar.
  uint16_t progress_permille = 0;

  /// Signed microseconds between the key turn and T. Negative = early.
  ///
  /// REPORTED, NOT SCORED (rail 3). The server owns the deviation curve and the
  /// bucket; this is the raw measurement the device sends it. Displaying it
  /// finer than the clock floor supports is a §13 A.3 question and is the
  /// renderer's problem, not this file's.
  int64_t deviation_us = 0;
  /// Whether `deviation_us` holds a measurement rather than a default.
  bool executed = false;

  /// True while the drill is committed and a miss is now a logged failure (§4).
  bool committed = false;

  /// The class this drill was offered with. Decides where the print leads.
  MsgClass cls = MsgClass::Nam;
  /// While Offered: when the offer is withdrawn (0 = never). See EventArgs.
  uint64_t withdraw_at_us = 0;
  /// True once an offer has been withdrawn (the drill is then Idle again).
  bool withdrawn = false;

  /// A key arc has completed and its hold has not yet confirmed or released.
  bool key_pending = false;
  /// When that arc completed: the instant that will be scored.
  uint64_t key_arc_us = 0;
  /// Why the drill ended, when it ended badly. Empty otherwise.
  ///
  /// A fixed buffer, not a String: this TU allocates nothing.
  char note[32] = {0};
};

class DrillMachine {
 public:
  DrillMachine() {}
  explicit DrillMachine(const Config& cfg) : cfg_(cfg) {}

  /// Feed one event at one instant. The only way state changes.
  void Step(Event ev, uint64_t now_us, const EventArgs& args = EventArgs());

  /// Arm a drill against a T. Called when the message's T is known.
  ///
  /// Separate from MessageArrived because T is derived from message content and
  /// may arrive with it or shortly after; the drill is Offered either way.
  void SetT(uint64_t t_at_us) { st_.t_at_us = t_at_us; }

  const State& Get() const { return st_; }
  const Config& Cfg() const { return cfg_; }

  /// Phases that MOVE. The rail-1 pin asserts none is entered without a human.
  static bool IsAnimating(Phase p) {
    return p == Phase::Printing || p == Phase::Terminal;
  }

  /// True for events a person caused. Everything else is the world happening.
  static bool IsPlayerEvent(Event e) {
    return e == Event::PlayerOpen || e == Event::PlayerAck
        || e == Event::PlayerConfirmWarPlan || e == Event::PlayerEnable
        || e == Event::PlayerKeyTurn || e == Event::PlayerAbort
        || e == Event::PlayerKeyArc || e == Event::PlayerKeyRelease;
  }

  /// Test/reset hook. Cheap because there is no state outside `st_`.
  void Reset() { st_ = State(); }

 private:
  void Enter(Phase p, uint64_t now_us);
  void SetNote(const char* s);
  /// Settle a confirmed key turn scored at `turn_us`.
  void ResolveTurn(uint64_t turn_us, uint64_t now_us);
  /// True while a pending arc still holds the window open for its hold.
  bool PendingHolds(uint64_t now_us);

  Config cfg_;
  State st_;
  /// When the current phase began, for progress and for the terminal count.
  uint64_t phase_since_us_ = 0;
};

}  // namespace game

#endif  // BLIPSCOPE_GAME_DRILLMACHINE_H
