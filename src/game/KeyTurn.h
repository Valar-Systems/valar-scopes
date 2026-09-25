// KeyTurn — §3 step 4's key turn, recognised from raw touch samples. Pure.
//
// ===========================================================================
// THE GESTURE: press on the bezel, drag clockwise along it through an arc, then
// hold (§3: "key-turn (press-drag arc on bezel, hold)").
//
// THE SCORED INSTANT IS THE ARC'S COMPLETION (Fable, 2026-09-23). The hold only
// confirms it: lifting before `confirm_us` voids the turn, and the player may
// try again while the window is open. So the recogniser emits three things:
//
//   Arc      the drag reached the end of the arc   -> DrillMachine PlayerKeyArc
//   Confirm  the hold lasted confirm_us            -> DrillMachine PlayerKeyTurn
//   Release  the finger lifted before the confirm  -> DrillMachine PlayerKeyRelease
//
// EVERY NUMBER HERE IS A §13-D TUNABLE, not a published rule: gestures are
// tunables (Fable, 2026-09-23). The sweep, the bezel radius and the hold length
// are chosen to be short -- §13 records the CST816's auto-sleep engine in both
// observed touch-wedge classes, and a static hold is the most auto-sleep-shaped
// input the game asks for, so the one hold the drill has is kept brief.
//
// DROPOUTS: a lift shorter than `rejoin_us` is not a lift. The CST816 reports a
// static finger as released for tens of milliseconds at a time (the gametest
// bench's finding (b), REJOIN_MS there), and a key turn voided by a dropout the
// player did not make would be the device lying about what they did.
// ===========================================================================

#ifndef BLIPSCOPE_GAME_KEYTURN_H
#define BLIPSCOPE_GAME_KEYTURN_H

#include <stddef.h>
#include <stdint.h>

namespace game {

struct KeyTurnParams {
  /// Screen centre and the inner radius of the bezel band, in pixels.
  int cx = 120;
  int cy = 120;
  int r_min = 72;
  /// Clockwise sweep that completes the turn, in degrees. §13-D tunable.
  float sweep_deg = 60.0f;
  /// Hold after the arc that confirms it. §13-D tunable.
  uint32_t confirm_us = 300000u;
  /// A lift shorter than this is a controller dropout, not a release: the release
  /// debounce. PROVISIONAL 250 ms -- BOUND, NOT MEASURED (docs/missileer-game-design.md,
  /// "Release debounce"). It covers the unattributed 107/214/226 ms gaps of bench run 2.
  /// It is replaced from the two-part bench test (run A: 30 s continuous drag, no
  /// lifts; run B: ten deliberate lifts), never from bench run 1's data.
  uint32_t rejoin_us = 250000u;
};

enum class KeyEvent : uint8_t { None = 0, Arc, Confirm, Release };

class KeyTurnGesture {
 public:
  KeyTurnGesture() {}
  explicit KeyTurnGesture(const KeyTurnParams& p) : p_(p) {}

  /// Feed one touch sample. Returns at most one event.
  KeyEvent Sample(bool touched, int x, int y, uint64_t now_us);

  void Reset() { st_ = St::Idle; }
  const KeyTurnParams& Params() const { return p_; }
  /// 0..1000 through the arc while tracking, for the renderer. 1000 once done.
  uint16_t ProgressPermille() const;

 private:
  enum class St : uint8_t { Idle, Ignored, Tracking, Holding, Done };

  KeyTurnParams p_;
  St st_ = St::Idle;
  float last_angle_ = 0.0f;
  float swept_deg_ = 0.0f;
  uint64_t arc_us_ = 0;
  bool lifted_ = false;
  uint64_t lifted_at_us_ = 0;

  bool OnBezel(int x, int y) const;
  float AngleDeg(int x, int y) const;
};

}  // namespace game

#endif  // BLIPSCOPE_GAME_KEYTURN_H
