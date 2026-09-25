// Host tests for the decisions EamManager makes around the drill
// (src/game/DrillPolicy.h) and the key-turn recogniser (src/game/KeyTurn.h).
//
// EamManager is an Arduino TU and cannot run here, which is exactly why these
// decisions were pulled out of it: whether a message is offered, whether the
// clock is good enough to arm, where T lands on the monotonic clock, and what a
// touch means. Every rule is a ruling of Fable, 2026-09-23.

#include "../../src/game/DrillPolicy.h"
#include "../../src/game/KeyTurn.h"

#include <cstdio>
#include <cstring>

using namespace game;

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

static const uint64_t S = 1000000ull;

static void Clock() {
  CASE("clock gate: a sync of known age, younger than the served maximum");
  CHECK(ClockFresh(true, 100 * S, 40 * S, 3600), "a 60 s old sync is fresh");
  CHECK(ClockFresh(true, 3700 * S, 100 * S, 3600), "exactly the max age is fresh");
  CHECK(!ClockFresh(true, 3700 * S + 1, 100 * S, 3600), "one microsecond older is not");
  CHECK(!ClockFresh(false, 100 * S, 99 * S, 3600), "no sync at all is never fresh");
  CHECK(!ClockFresh(true, 100 * S, 99 * S, 0), "an unserved max age is never fresh");
  CHECK(!ClockFresh(true, 10 * S, 20 * S, 3600), "a sync 'from the future' is not trusted");

  CASE("T onto the monotonic clock");
  CHECK(MonoForUtcMs(1000000, 1000000, 50 * S) == 50 * S, "now is now");
  CHECK(MonoForUtcMs(1000000 + 90000, 1000000, 50 * S) == 140 * S, "90 s ahead");
  CHECK(MonoForUtcMs(1000000 - 20000, 1000000, 50 * S) == 30 * S, "20 s ago");
  CHECK(MonoForUtcMs(0, 1000000, 50 * S) == 1, "before boot is 1, never 0 (0 means no T)");
}

static Derivation Derived(MsgClass c, int64_t t_ms = -1, DeriveStatus st = DeriveStatus::Ok) {
  Derivation d;
  d.status = st;
  d.cls = c;
  d.t_at_ms = t_ms;
  return d;
}

static OfferInput Ready(const Derivation& d) {
  OfferInput in;
  in.drill_idle = true;
  in.have_config = true;
  in.derivation = d;
  in.clock_fresh = true;
  in.now_utc_ms = 1000000;
  in.ack_cutoff_s = 60;
  return in;
}

static void Offer() {
  CASE("offer: NAM and FDM are offered on a fresh clock");
  CHECK(DecideOffer(Ready(Derived(MsgClass::Nam))) == OfferDecision::Offer, "a NAM was not offered");
  CHECK(DecideOffer(Ready(Derived(MsgClass::Fdm))) == OfferDecision::Offer, "an FDM was not offered");

  CASE("offer: NO class is offered on a stale clock (game-client PR ruling)");
  // Reverses the #332 behaviour, where a NAM or FDM was offered on any clock.
  OfferInput nam = Ready(Derived(MsgClass::Nam));
  nam.clock_fresh = false;
  CHECK(DecideOffer(nam) == OfferDecision::ClockUnsynced, "a NAM was offered on a stale clock");
  OfferInput fdm = Ready(Derived(MsgClass::Fdm));
  fdm.clock_fresh = false;
  CHECK(DecideOffer(fdm) == OfferDecision::ClockUnsynced, "an FDM was offered on a stale clock");

  CASE("offer: an execution before its ack cutoff is offered");
  CHECK(DecideOffer(Ready(Derived(MsgClass::Execution, 1000000 + 61000))) == OfferDecision::Offer,
        "61 s before T was not offered");

  CASE("offer: an execution past its ack cutoff is NEVER offered");
  CHECK(DecideOffer(Ready(Derived(MsgClass::Execution, 1000000 + 60000))) == OfferDecision::PastCutoff,
        "exactly at T-60 s was offered");
  CHECK(DecideOffer(Ready(Derived(MsgClass::Execution, 1000000 - 5000))) == OfferDecision::PastCutoff,
        "a T already past was offered");

  CASE("offer: an execution with no fresh clock is not offered (the cutoff is unknowable)");
  OfferInput e = Ready(Derived(MsgClass::Execution, 1000000 + 600000));
  e.clock_fresh = false;
  CHECK(DecideOffer(e) == OfferDecision::ClockUnsynced, "offered against an unsynced clock");

  CASE("offer: one drill at a time; later arrivals are OtherMessageArrived, not queued");
  OfferInput busy = Ready(Derived(MsgClass::Nam));
  busy.drill_idle = false;
  CHECK(DecideOffer(busy) == OfferDecision::Busy, "a second message was offered mid-drill");
  busy.have_config = false;
  CHECK(DecideOffer(busy) == OfferDecision::Busy, "mid-drill is Busy whatever else is true");

  CASE("offer: no config, or an underivable message, is an ordinary EAM");
  OfferInput nc = Ready(Derived(MsgClass::Execution, 1000000 + 600000));
  nc.have_config = false;
  CHECK(DecideOffer(nc) == OfferDecision::NoConfig, "offered with no /config in hand");
  CHECK(DecideOffer(Ready(Derived(MsgClass::Nam, -1, DeriveStatus::BadParams))) == OfferDecision::NotDerivable,
        "offered on unsound params");
  CHECK(DecideOffer(Ready(Derived(MsgClass::Execution, -1, DeriveStatus::BadHeardAt))) == OfferDecision::NotDerivable,
        "offered with no heard_at");

  CASE("offer: HOLD beats everything (no game surfaces)");
  OfferInput h = Ready(Derived(MsgClass::Nam));
  h.hold = true;
  CHECK(DecideOffer(h) == OfferDecision::Held, "offered under HOLD");
  h.drill_idle = false;
  CHECK(DecideOffer(h) == OfferDecision::Held, "HOLD mid-drill was not Held");
}

static void UsbLongPress() {
  CASE("usb long press: armed outside a drill, never Offered through Terminal");
  CHECK(UsbLongPressArmed(Phase::Idle), "Idle is outside the drill");
  CHECK(UsbLongPressArmed(Phase::Complete), "Complete is outside the drill");
  CHECK(UsbLongPressArmed(Phase::Aborted), "Aborted is outside the drill");
  const Phase inDrill[] = {Phase::Offered, Phase::Printing, Phase::Decoded, Phase::Authenticate,
                           Phase::WarPlan, Phase::Enable, Phase::Armed, Phase::Window,
                           Phase::Committed, Phase::Terminal};
  for (Phase p : inDrill) CHECK(!UsbLongPressArmed(p), "armed inside the drill");
}

static void ClockAge() {
  CASE("clock age: whole seconds since the sync, -1 with none, saturating at 9999");
  CHECK(ClockAgeS(false, 100 * S, 0) == -1, "no sync did not read -1");
  CHECK(ClockAgeS(true, 100 * S, 40 * S) == 60, "a 60 s old sync did not read 60");
  CHECK(ClockAgeS(true, 100 * S + 999999, 40 * S) == 60, "the age rounded up, not down");
  CHECK(ClockAgeS(true, 40 * S, 40 * S) == 0, "a sync this instant did not read 0");
  CHECK(ClockAgeS(true, 40 * S, 41 * S) == -1, "a sync in the future read as an age");
  CHECK(ClockAgeS(true, 20000 * S, 0) == 9999, "an ancient sync did not saturate at 9999");
}

static void Timers() {
  CASE("auto-decode is a display property after the served delay");
  CHECK(!AutoDecoded(10 * S, 10 * S + 299 * S, 300), "decoded before 300 s");
  CHECK(AutoDecoded(10 * S, 10 * S + 300 * S, 300), "not decoded at 300 s");
  CHECK(!AutoDecoded(10 * S, 10000 * S, 0), "an unserved delay auto-decoded");

  CASE("a finished drill returns to Idle after 60 s");
  CHECK(!EndedDwellOver(5 * S, 64 * S), "dismissed before 60 s");
  CHECK(EndedDwellOver(5 * S, 65 * S), "not dismissed at 60 s");
}

static void Targets() {
  CASE("touch targets sit inside the round screen and do not overlap");
  const int screens[] = {240, 412};
  for (int sz : screens) {
    const Rect b = BannerRect(sz);
    const Rect a = AbortRect(sz);
    const int corners[8][2] = {{b.x, b.y}, {b.x + b.w, b.y}, {b.x, b.y + b.h}, {b.x + b.w, b.y + b.h},
                               {a.x, a.y}, {a.x + a.w, a.y}, {a.x, a.y + a.h}, {a.x + a.w, a.y + a.h}};
    for (const auto& c : corners) {
      const long dx = c[0] - sz / 2, dy = c[1] - sz / 2;
      CHECK(dx * dx + dy * dy <= static_cast<long>(sz / 2) * (sz / 2), "a target corner is off the glass");
    }
    CHECK(b.y + b.h <= a.y, "the banner and ABORT overlap");
    CHECK(b.Contains(b.x + 1, b.y + 1) && !b.Contains(b.x + b.w, b.y), "Contains is not half-open");
  }
}

// ---------------------------------------------------------------------------
// The key turn
// ---------------------------------------------------------------------------

/// Drag along the bezel at radius r from angle a0 to a1 (degrees, clockwise
/// positive, screen y down), one sample per degree, 2 ms apart.
static KeyEvent Drag(KeyTurnGesture& g, float a0, float a1, uint64_t& now, int r = 110,
                     KeyEvent* first_nonzero = nullptr, uint64_t* at = nullptr) {
  const float step = a1 > a0 ? 1.0f : -1.0f;
  KeyEvent last = KeyEvent::None;
  for (float a = a0; step > 0 ? a <= a1 : a >= a1; a += step) {
    const float rad = a / 57.29578f;
    const int x = 120 + static_cast<int>(r * __builtin_cosf(rad));
    const int y = 120 + static_cast<int>(r * __builtin_sinf(rad));
    const KeyEvent e = g.Sample(true, x, y, now);
    if (e != KeyEvent::None) {
      last = e;
      if (first_nonzero && *first_nonzero == KeyEvent::None) {
        *first_nonzero = e;
        if (at) *at = now;
      }
    }
    now += 2000;
  }
  return last;
}

/// Hold still (same point) for `us`, sampling every 5 ms.
static KeyEvent Hold(KeyTurnGesture& g, uint64_t us, uint64_t& now, uint64_t* at = nullptr) {
  KeyEvent got = KeyEvent::None;
  const uint64_t end = now + us;
  for (; now <= end; now += 5000) {
    const KeyEvent e = g.Sample(true, 230, 120, now);
    if (e != KeyEvent::None && got == KeyEvent::None) {
      got = e;
      if (at) *at = now;
    }
  }
  return got;
}

static KeyEvent Lift(KeyTurnGesture& g, uint64_t us, uint64_t& now) {
  KeyEvent got = KeyEvent::None;
  const uint64_t end = now + us;
  for (; now <= end; now += 5000) {
    const KeyEvent e = g.Sample(false, 0, 0, now);
    if (e != KeyEvent::None && got == KeyEvent::None) got = e;
  }
  return got;
}

static void KeyTurn() {
  CASE("key: a clockwise arc on the bezel, then a hold -> Arc, then Confirm");
  {
    KeyTurnGesture g;
    uint64_t now = 0, arc_at = 0, conf_at = 0;
    KeyEvent first = KeyEvent::None;
    Drag(g, -30.0f, 40.0f, now, 110, &first, &arc_at);
    CHECK(first == KeyEvent::Arc, "a 70 degree clockwise drag did not complete the arc");
    CHECK(arc_at == 60 * 2000, "the Arc fired somewhere other than 60 degrees in");
    const KeyEvent c = Hold(g, 400000, now, &conf_at);
    CHECK(c == KeyEvent::Confirm, "the hold did not confirm");
    CHECK(conf_at - arc_at >= 300000 && conf_at - arc_at < 310000, "confirmed at the wrong time");
    CHECK(Lift(g, 200000, now) == KeyEvent::None, "a lift after the confirm emitted something");
  }

  CASE("key: lifting before the hold completes is a Release");
  {
    KeyTurnGesture g;
    uint64_t now = 0;
    Drag(g, 0.0f, 70.0f, now);
    CHECK(Hold(g, 100000, now) == KeyEvent::None, "confirmed after 100 ms");
    CHECK(Lift(g, 300000, now) == KeyEvent::Release, "an early lift was not a Release");
    // ... and the player may try again.
    KeyEvent first = KeyEvent::None;
    Drag(g, 0.0f, 70.0f, now, 110, &first);
    CHECK(first == KeyEvent::Arc, "a retry after a release was not recognised");
  }

  CASE("key: a controller dropout shorter than rejoin is not a lift");
  {
    KeyTurnGesture g;
    uint64_t now = 0;
    Drag(g, 0.0f, 70.0f, now);
    Hold(g, 100000, now);
    CHECK(Lift(g, 50000, now) == KeyEvent::None, "a 50 ms dropout released the turn");
    CHECK(Hold(g, 300000, now) == KeyEvent::Confirm, "the hold after a dropout did not confirm");
  }

  CASE("key: the release debounce is the provisional 250 ms bound");
  {
    // Bound, not measured (docs/missileer-game-design.md): bench run 2's unattributed
    // gaps (107, 214, 226 ms) are dropouts under it; a lift of 250 ms is a release.
    CHECK(KeyTurnParams().rejoin_us == 250000u, "rejoin_us is not the provisional 250 ms");
    KeyTurnGesture g;
    uint64_t now = 0;
    Drag(g, 0.0f, 70.0f, now);
    // Inside the 300 ms confirm the whole way: 10 + 226 + 5 ms after the arc.
    Hold(g, 10000, now);
    CHECK(Lift(g, 226000, now) == KeyEvent::None, "a 226 ms gap released the turn");
    Hold(g, 5000, now);
    CHECK(Lift(g, 255000, now) == KeyEvent::Release, "a 255 ms lift was not a Release");
  }

  CASE("key: counter-clockwise, too short, or off the bezel is no turn");
  {
    KeyTurnGesture ccw;
    uint64_t now = 0;
    CHECK(Drag(ccw, 70.0f, 0.0f, now) == KeyEvent::None, "a counter-clockwise drag turned the key");
    KeyTurnGesture shortArc;
    CHECK(Drag(shortArc, 0.0f, 50.0f, now) == KeyEvent::None, "a 50 degree drag turned the key");
    KeyTurnGesture inner;
    CHECK(Drag(inner, 0.0f, 90.0f, now, 50) == KeyEvent::None, "a drag in the middle turned the key");
  }

  CASE("key: the drag across the +/-180 degree seam still counts");
  {
    KeyTurnGesture g;
    uint64_t now = 0;
    KeyEvent first = KeyEvent::None;
    Drag(g, 150.0f, 220.0f, now, 110, &first);
    CHECK(first == KeyEvent::Arc, "the seam at 180 degrees broke the sweep");
  }
}

static void ConfigPoll() {
  CASE("the /config poll honours Cache-Control max-age, clamped");
  CHECK(ConfigPollIntervalS("public, max-age=30") == 30, "max-age=30 not honoured");
  CHECK(ConfigPollIntervalS("MAX-AGE=45") == 45, "case matters and should not");
  CHECK(ConfigPollIntervalS("max-age=0") == 10, "max-age=0 would hammer the server");
  CHECK(ConfigPollIntervalS("max-age=86400") == 3600, "a day would strand the HOLD switch");
  CHECK(ConfigPollIntervalS("no-store") == 300, "absent max-age is not the default");
  CHECK(ConfigPollIntervalS("") == 300, "empty header is not the default");
  CHECK(ConfigPollIntervalS(nullptr) == 300, "null header is not the default");
  CHECK(ConfigPollIntervalS("max-age=abc") == 300, "garbage is not the default");
  CHECK(ConfigPollIntervalS("max_age=30") == 300, "'_' matched '-'");
}

int main() {
  std::printf("DrillPolicy + KeyTurn host tests\n");
  ConfigPoll();
  Clock();
  Offer();
  UsbLongPress();
  ClockAge();
  Timers();
  Targets();
  KeyTurn();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  if (g_checks < 40) {
    std::printf("FAIL: only %d checks ran; the suite did not execute\n", g_checks);
    return 2;
  }
  return g_failures == 0 ? 0 : 1;
}
