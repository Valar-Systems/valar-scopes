// GameProtocol — what the game client says to the server, and what it hears back.
//
// ===========================================================================
// PURE, like DrillMachine: <stdint.h>, <stddef.h> and the pure game headers.
// No ArduinoJson, no String, no heap. GameClient (the Arduino half) does the
// HTTP and the JSON parse; every DECISION about a request or a reply is here,
// graded by test/host/test_game_protocol.cpp against the routes in
// valar-eam-feed src/routes/game.ts.
// ===========================================================================

#ifndef BLIPSCOPE_GAME_GAMEPROTOCOL_H
#define BLIPSCOPE_GAME_GAMEPROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "Derive.h"
#include "DrillMachine.h"

namespace game {

/// The wire name of a class: POST /votes accepts exactly these.
const char* ClassWire(MsgClass c);

/// POST /votes: {"msg_id","class","config_epoch","t_at"}. `t_at` is the derived T
/// as ISO-8601 UTC for an execution and null otherwise (the derivation gives NAM
/// and FDM no T). Returns the length written, 0 if it does not fit or the msg_id
/// holds a character that JSON would need escaping (ids are [A-Za-z0-9-] on the
/// server; anything else is not ours to guess at).
size_t BuildCommitBody(const char* msg_id, MsgClass cls, uint32_t config_epoch, int64_t t_at_ms,
                       char* out, size_t cap);

/// The device measures microseconds; the server takes whole milliseconds and
/// applies Math.round, which rounds half UP (toward +inf, so -1.5 -> -1). The
/// same rule here, so the integer sent is the integer the server would store.
int64_t DeviationMsForServer(int64_t deviation_us);

/// POST /votes/:id/execute: {"deviation_ms","enable_ok"}.
size_t BuildExecuteBody(int64_t deviation_us, bool enable_ok, char* out, size_t cap);

/// What a reply means for the client.
enum class Reply : uint8_t {
  /// 2xx: the request did what it asked.
  Ok = 0,
  /// 409 stale_config: refetch /config (the server moved to a new epoch).
  RefetchConfig,
  /// The server refused, for good. The drill ends with the reason.
  Refused,
  /// No round trip, or the server was unavailable (5xx): try again.
  Retry,
};

/// POST /votes and POST /votes/:id/execute, from the status and the `error` field.
Reply ClassifyReply(int http_status, const char* error);

/// A vote's resolution, from GET /votes/:id (or the execute reply's `outcome`).
struct Resolution {
  /// False while the vote is still PENDING: keep polling.
  bool resolved = false;
  VoteOutcome outcome = VoteOutcome::None;
  /// Static text or the `inhibit_reason` passed in; never owned.
  const char* reason = nullptr;
};

/// Map the server's vote outcome onto the four the drill knows (Fable,
/// 2026-09-23). LAUNCHED with a seconder is Seconded; LAUNCHED alone is the
/// dead-man timer. ABORTED and PREEMPTED are not in the ruling's four: both
/// end the drill, so both are Failed, with the server's word as the reason.
/// An unknown outcome string resolves nothing (keep polling) rather than
/// guessing a launch.
Resolution ResolveOutcome(const char* outcome, bool seconded, const char* inhibit_reason);

}  // namespace game

#endif  // BLIPSCOPE_GAME_GAMEPROTOCOL_H
