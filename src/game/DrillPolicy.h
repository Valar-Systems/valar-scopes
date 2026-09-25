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
#include "DrillMachine.h"  // Phase. Pure, like this file.

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

/// Whole seconds since the last SNTP sync, for the drill face's seven-segment
/// age readout. -1 when there has been no sync (the face then shows unlit digits).
/// Saturates at 9999: four digits is what the face has room for, and a sync
/// that old is stale under any served maximum.
inline int32_t ClockAgeS(bool have_sync, uint64_t now_mono_us, uint64_t last_sync_mono_us) {
  if (!have_sync || last_sync_mono_us > now_mono_us) return -1;
  const uint64_t s = (now_mono_us - last_sync_mono_us) / 1000000ull;
  return s > 9999u ? 9999 : static_cast<int32_t>(s);
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
  /// No sync younger than the served maxClockSyncAgeS: NOT OFFERED, whatever the
  /// class (Fable, 2026-09-23, game-client PR: "refuse to enter Offered if the
  /// clock's last sync is older than maxClockSyncAgeS"). This widens the earlier
  /// gate, which applied only to executions (their cutoff needs the clock) and
  /// let a NAM or FDM through on any clock.
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
  if (!in.clock_fresh) return OfferDecision::ClockUnsynced;
  if (in.derivation.cls != MsgClass::Execution) return OfferDecision::Offer;
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
// The USB "open on computer" long press
// ---------------------------------------------------------------------------

/// The long press types a URL into the host over USB HID. It is a monitor feature
/// and stays OUTSIDE a drill, but from Offered through Terminal it is not armed at
/// all (Fable, 2026-09-24): a player holding the key arc -- or resting a finger on
/// the Offered banner -- must never type into a host. Idle, Complete and Aborted
/// are outside the drill.
inline bool UsbLongPressArmed(Phase p) {
  return !(p >= Phase::Offered && p <= Phase::Terminal);
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

// ---------------------------------------------------------------------------
// The /config poll cadence
// ---------------------------------------------------------------------------

/// Seconds until /config is fetched again, from its Cache-Control header:
/// `max-age` honoured (Fable, 2026-09-23), clamped to [10 s, 1 h] so a typo on
/// the server can neither hammer it nor strand the HOLD kill switch for a day.
/// Absent or unparseable: 300 s.
inline uint32_t ConfigPollIntervalS(const char* cache_control) {
  const uint32_t kDefault = 300, kMin = 10, kMax = 3600;
  if (cache_control == nullptr) return kDefault;
  const char* key = "max-age=";
  const size_t key_len = 8;
  for (const char* p = cache_control; *p; p += 1) {
    size_t i = 0;
    // Case-insensitive: only letters fold (c | 0x20), so '-' and '=' match exactly.
    while (i < key_len && p[i] != 0
           && (p[i] == key[i] || (key[i] >= 'a' && key[i] <= 'z' && (p[i] | 0x20) == key[i]))) {
      i += 1;
    }
    if (i != key_len) continue;
    const char* d = p + i;
    if (*d < '0' || *d > '9') return kDefault;
    uint32_t v = 0;
    while (*d >= '0' && *d <= '9' && v < 100000u) v = v * 10 + static_cast<uint32_t>(*d++ - '0');
    return v < kMin ? kMin : v > kMax ? kMax : v;
  }
  return kDefault;
}

}  // namespace game

#endif  // BLIPSCOPE_GAME_DRILLPOLICY_H
