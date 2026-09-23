// DrillPolicy — the decisions EamManager makes AROUND the drill, kept pure.
//
// ===========================================================================
// EamManager is an Arduino TU and cannot be host-tested. So every decision it
// makes about the drill -- whether a message is offered, whether the clock is
// good enough to arm, where T falls on the monotonic clock, what a tap hit --
// lives here as a plain function of plain values, graded by
// test/host/test_drill_policy.cpp. EamManager only gathers the inputs and
// feeds the outputs to DrillMachine.
//
// Pure like DrillMachine: <stdint.h>, <stddef.h>, and the pure game headers.
// Every rule below is a ruling of Fable, 2026-09-23 unless it says otherwise.
// ===========================================================================

#ifndef BLIPSCOPE_GAME_DRILLPOLICY_H
#define BLIPSCOPE_GAME_DRILLPOLICY_H

#include <stddef.h>
#include <stdint.h>

#include "Derive.h"

namespace game {

// ---------------------------------------------------------------------------
// The clock gate
// ---------------------------------------------------------------------------

/// True when the device holds an SNTP sync younger than the served maximum.
/// "Post-2020" (EamManager's old `utc > 1600000000`) is NOT "synced": arming,
/// and voting, need a sync of KNOWN age. A max age of 0 (not served) is never fresh.
inline bool ClockFresh(bool have_sync, uint64_t now_mono_us, uint64_t last_sync_mono_us,
                       uint32_t max_age_s) {
  if (!have_sync || max_age_s == 0 || last_sync_mono_us > now_mono_us) return false;
  return now_mono_us - last_sync_mono_us <= static_cast<uint64_t>(max_age_s) * 1000000ull;
}

/// Where the Zulu instant `t_ms` falls on the monotonic clock, given one
/// simultaneous reading of both clocks. Never 0 (0 means "no T" to the drill):
/// an instant before boot maps to 1, which the drill then treats as long past.
inline uint64_t MonoForUtcMs(int64_t t_ms, int64_t now_utc_ms, uint64_t now_mono_us) {
  const int64_t delta_us = (t_ms - now_utc_ms) * 1000;
  const int64_t mono = static_cast<int64_t>(now_mono_us) + delta_us;
  return mono <= 0 ? 1u : static_cast<uint64_t>(mono);
}

// ---------------------------------------------------------------------------
// Whether a new message is offered
// ---------------------------------------------------------------------------

enum class OfferDecision : uint8_t {
  /// Idle -> Offered.
  Offer = 0,
  /// A drill is live: this is OtherMessageArrived. NOT queued -- it stays in
  /// the ticker and the logbook, worked or not.
  Busy,
  /// The served HOLD flag is set: no game surfaces at all (§1.2).
  Held,
  /// No /config in hand: the device refuses to derive, and the message is an
  /// ordinary EAM.
  NoConfig,
  /// Derive() refused (unsound params, unparseable heard_at): ordinary EAM.
  NotDerivable,
  /// An execution with no fresh clock: whether its ack cutoff has passed cannot
  /// be known, and "never Offered past the cutoff" is absolute, so it is not
  /// offered. (An interpretation: the ruling gates ARMING on the clock; this
  /// applies the same gate one step earlier because the cutoff rule needs it.)
  ClockUnsynced,
  /// An execution whose ack cutoff (ackCutoffS before T) has passed.
  PastCutoff,
};

struct OfferInput {
  bool drill_idle = true;
  bool hold = false;
  bool have_config = false;
  Derivation derivation;
  bool clock_fresh = false;
  int64_t now_utc_ms = 0;
  uint32_t ack_cutoff_s = 0;
};

inline OfferDecision DecideOffer(const OfferInput& in) {
  if (in.hold) return OfferDecision::Held;
  // One drill at a time. Checked before derivability: a message arriving
  // mid-drill is OtherMessageArrived whatever it is.
  if (!in.drill_idle) return OfferDecision::Busy;
  if (!in.have_config) return OfferDecision::NoConfig;
  if (in.derivation.status != DeriveStatus::Ok) return OfferDecision::NotDerivable;
  if (in.derivation.cls != MsgClass::Execution) return OfferDecision::Offer;
  if (!in.clock_fresh) return OfferDecision::ClockUnsynced;
  const int64_t cutoff_ms = in.derivation.t_at_ms - static_cast<int64_t>(in.ack_cutoff_s) * 1000;
  if (in.now_utc_ms >= cutoff_ms) return OfferDecision::PastCutoff;
  return OfferDecision::Offer;
}

// ---------------------------------------------------------------------------
// Offered's auto-decode, and the finished drill's return to Idle
// ---------------------------------------------------------------------------

/// §5 "Unattended messages auto-decode after a few minutes": a DISPLAY property
/// of the Offered banner, not a phase. True once the banner may show the class.
/// A served value of 0 (not served) never auto-decodes.
inline bool AutoDecoded(uint64_t offered_at_us, uint64_t now_us, uint32_t auto_decode_s) {
  if (auto_decode_s == 0 || now_us < offered_at_us) return false;
  return now_us - offered_at_us >= static_cast<uint64_t>(auto_decode_s) * 1000000ull;
}

/// Complete/Aborted return to Idle on a dismiss tap or after 60 s, whichever first.
constexpr uint64_t kEndedDwellUs = 60ull * 1000000ull;
inline bool EndedDwellOver(uint64_t ended_at_us, uint64_t now_us) {
  return now_us >= ended_at_us && now_us - ended_at_us >= kEndedDwellUs;
}

// ---------------------------------------------------------------------------
// Touch targets. One definition, read by the renderer AND the touch handler,
// so what is drawn is what is hit. Proportional to the (square, round) screen.
// ---------------------------------------------------------------------------

struct Rect {
  int x, y, w, h;
  bool Contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

/// The Offered banner: a band across the upper screen, clear of the round edge.
inline Rect BannerRect(int screen) {
  return Rect{screen * 18 / 100, screen * 14 / 100, screen * 64 / 100, screen * 15 / 100};
}

/// The ABORT target: visible during every drill phase, no hidden gesture.
inline Rect AbortRect(int screen) {
  return Rect{screen * 36 / 100, screen * 80 / 100, screen * 28 / 100, screen * 10 / 100};
}

}  // namespace game

#endif  // BLIPSCOPE_GAME_DRILLPOLICY_H
