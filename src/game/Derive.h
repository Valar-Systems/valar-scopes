// Derive — a message's class, and for an execution its T, from its id.
//
// ===========================================================================
// A PORT, AND GRADED AS ONE.
//
// The specification is valar-eam-feed docs/game-derivation.md and the reference
// is that repo's src/game/derive.ts -- the same function POST /votes re-derives
// with, refusing any vote whose class or T disagrees (`derivation_mismatch`).
// So a drift here is not a cosmetic difference: it is a device that cannot
// commit a single vote.
//
// This file is graded against test/fixtures/derivation_fixture.h, which the
// SERVER emitted by calling its own function (scripts/emit-derivation-fixture.ts).
// Whole-value equality on every draw, class, tier, offset and T. Nothing in the
// expected values was typed by somebody reading derive.ts.
//
// PURE, like DrillMachine: <stdint.h> and <stddef.h>, no Arduino, no heap, no
// clock. Every rule below is a ruling (2026-09-22) cited in the spec; none was
// chosen here.
// ===========================================================================

#ifndef BLIPSCOPE_GAME_DERIVE_H
#define BLIPSCOPE_GAME_DERIVE_H

#include <stddef.h>
#include <stdint.h>

namespace game {

enum class MsgClass : uint8_t { Nam = 0, Fdm, Execution };
enum class Tier : uint8_t { None = 0, Normal, Snap };

/// One entry of /config's ORDERED decode.weights, as served: integer ppm.
struct ClassWeight {
  MsgClass cls;
  uint32_t ppm;
};

/// One entry of /config's ORDERED timing.tOffset. min_s inclusive, max_s EXCLUSIVE.
struct TierParam {
  Tier tier;
  uint32_t ppm;
  uint32_t min_s;
  uint32_t max_s;
};

constexpr size_t kMaxWeights = 8;
constexpr size_t kMaxTiers = 8;

/// The served parameters. Order is part of the function.
struct DeriveParams {
  uint32_t epoch = 0;
  ClassWeight weights[kMaxWeights] = {};
  size_t n_weights = 0;
  TierParam tiers[kMaxTiers] = {};
  size_t n_tiers = 0;
};

enum class DeriveStatus : uint8_t {
  Ok = 0,
  /// The served parameters do not sum to exactly 1e6 (or a tier has max <= min).
  /// REFUSED, not normalised: the device does no normalisation (ruling), and a
  /// fixed-up walk would derive a class the server will reject.
  BadParams,
  /// heard_at is not an instant this parser accepts. No T is guessed.
  BadHeardAt,
};

struct Derivation {
  DeriveStatus status = DeriveStatus::BadParams;
  /// SHA-256(msg_id) bytes 0-3, 4-7, 8-11, big-endian.
  uint32_t class_u32 = 0;
  uint32_t tier_u32 = 0;
  uint32_t offset_u32 = 0;
  /// (u64(u32) * 1_000_000) >> 32.
  uint32_t class_ppm = 0;
  uint32_t tier_ppm = 0;
  MsgClass cls = MsgClass::Nam;
  /// Tier::None unless cls is Execution (T is for execution only).
  Tier tier = Tier::None;
  /// Whole seconds; -1 unless Execution.
  int32_t offset_s = -1;
  /// T as Unix milliseconds, on a whole Zulu minute; -1 unless Execution.
  int64_t t_at_ms = -1;
};

/// The served parameters are sound: every ppm list sums to exactly 1e6 and
/// every tier has min_s < max_s. The server refuses to boot otherwise; the
/// device refuses to derive.
bool ParamsSound(const DeriveParams& p);

/// THE FUNCTION. `msg_id` is `id_len` UTF-8 bytes (the feed's Msg.id, as served);
/// `heard_at` is the feed's served heard_at, NUL-terminated.
Derivation Derive(const char* msg_id, size_t id_len, const char* heard_at, const DeriveParams& p);

/// Parse `YYYY-MM-DDTHH:MM:SS[.f{1,3}]Z` -- what the feed serves
/// (JavaScript's toISOString) -- to Unix milliseconds. Anything else is false.
bool ParseIsoUtcMs(const char* s, int64_t* out_ms);

/// Render Unix milliseconds as `YYYY-MM-DDTHH:MM:SS.sssZ` (24 chars + NUL), the
/// server's rendering. Returns the length written, or 0 if `cap` < 25.
size_t FormatIsoUtcMs(int64_t ms, char* out, size_t cap);

/// SHA-256. Exposed for the host test's known-answer vectors.
void Sha256(const uint8_t* data, size_t len, uint8_t out[32]);

}  // namespace game

#endif  // BLIPSCOPE_GAME_DERIVE_H
