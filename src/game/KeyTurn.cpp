// KeyTurn — see the header.

#include "KeyTurn.h"

#include <math.h>

namespace game {

namespace {
const float kRadToDeg = 57.29577951308232f;
}  // namespace

bool KeyTurnGesture::OnBezel(int x, int y) const {
  const long dx = x - p_.cx;
  const long dy = y - p_.cy;
  return dx * dx + dy * dy >= static_cast<long>(p_.r_min) * p_.r_min;
}

/// Screen coordinates have y DOWN, so atan2(dy, dx) increases CLOCKWISE.
float KeyTurnGesture::AngleDeg(int x, int y) const {
  return atan2f(static_cast<float>(y - p_.cy), static_cast<float>(x - p_.cx)) * kRadToDeg;
}

uint16_t KeyTurnGesture::ProgressPermille() const {
  if (st_ == St::Holding || st_ == St::Done) return 1000;
  if (st_ != St::Tracking || p_.sweep_deg <= 0.0f) return 0;
  const float f = swept_deg_ / p_.sweep_deg;
  if (f <= 0.0f) return 0;
  if (f >= 1.0f) return 1000;
  return static_cast<uint16_t>(f * 1000.0f);
}

KeyEvent KeyTurnGesture::Sample(bool touched, int x, int y, uint64_t now_us) {
  // A lift is only a lift once it has lasted rejoin_us (see the header).
  bool lifted_for_real = false;
  if (touched) {
    lifted_ = false;
  } else {
    if (!lifted_) {
      lifted_ = true;
      lifted_at_us_ = now_us;
    }
    lifted_for_real = now_us - lifted_at_us_ >= p_.rejoin_us;
  }

  switch (st_) {
    case St::Idle:
      if (touched) {
        if (OnBezel(x, y)) {
          st_ = St::Tracking;
          last_angle_ = AngleDeg(x, y);
          swept_deg_ = 0.0f;
        } else {
          // A press that did not start on the bezel is not a key turn, however
          // it moves afterwards. Ignored until the finger lifts.
          st_ = St::Ignored;
        }
      }
      return KeyEvent::None;

    case St::Ignored:
      if (lifted_for_real) st_ = St::Idle;
      return KeyEvent::None;

    case St::Tracking: {
      if (!touched) {
        // An unfinished arc that lifts is simply abandoned: nothing was scored.
        if (lifted_for_real) st_ = St::Idle;
        return KeyEvent::None;
      }
      const float a = AngleDeg(x, y);
      float d = a - last_angle_;
      while (d > 180.0f) d -= 360.0f;
      while (d <= -180.0f) d += 360.0f;
      last_angle_ = a;
      swept_deg_ += d;
      // Clockwise only, as a key turns; drifting back unwinds the sweep but
      // never below zero, so a wobble at the start costs nothing.
      if (swept_deg_ < 0.0f) swept_deg_ = 0.0f;
      if (swept_deg_ >= p_.sweep_deg) {
        st_ = St::Holding;
        arc_us_ = now_us;
        return KeyEvent::Arc;
      }
      return KeyEvent::None;
    }

    case St::Holding:
      if (lifted_for_real) {
        st_ = St::Idle;
        return KeyEvent::Release;
      }
      if (touched && now_us - arc_us_ >= p_.confirm_us) {
        st_ = St::Done;
        return KeyEvent::Confirm;
      }
      return KeyEvent::None;

    case St::Done:
      if (lifted_for_real) st_ = St::Idle;
      return KeyEvent::None;
  }
  return KeyEvent::None;
}

}  // namespace game
