// GameProtocol — see the header. Pure.

#include "GameProtocol.h"

#include <stddef.h>
#include <stdint.h>

namespace game {

namespace {

/// Append `s` to out[*n], keeping a terminator. False if it would not fit.
bool Put(char* out, size_t cap, size_t* n, const char* s) {
  while (*s) {
    if (*n + 1 >= cap) return false;
    out[(*n)++] = *s++;
  }
  out[*n] = '\0';
  return true;
}

/// A signed decimal, appended. No printf: the formatting is the contract here.
bool PutInt(char* out, size_t cap, size_t* n, int64_t v) {
  char tmp[24];
  size_t k = 0;
  const bool neg = v < 0;
  // Negate in the unsigned domain so INT64_MIN does not overflow.
  uint64_t u = neg ? static_cast<uint64_t>(0) - static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
  do {
    tmp[k++] = static_cast<char>('0' + (u % 10));
    u /= 10;
  } while (u != 0);
  if (neg) tmp[k++] = '-';
  char rev[24];
  for (size_t i = 0; i < k; i += 1) rev[i] = tmp[k - 1 - i];
  rev[k] = '\0';
  return Put(out, cap, n, rev);
}

bool IdSafe(const char* s) {
  if (s == nullptr || *s == '\0') return false;
  size_t len = 0;
  for (; *s; s += 1) {
    const char c = *s;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '-' || c == '_' || c == '.' || c == ':';
    if (!ok) return false;
    if (++len > 128) return false;  // the server's own bound on msg_id
  }
  return true;
}

bool Eq(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  while (*a && *a == *b) {
    a += 1;
    b += 1;
  }
  return *a == *b;
}

}  // namespace

const char* ClassWire(MsgClass c) {
  switch (c) {
    case MsgClass::Nam:       return "nam";
    case MsgClass::Fdm:       return "fdm";
    case MsgClass::Execution: return "execution";
  }
  return "nam";
}

size_t BuildCommitBody(const char* msg_id, MsgClass cls, uint32_t config_epoch, int64_t t_at_ms,
                       char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (!IdSafe(msg_id)) return 0;
  size_t n = 0;
  bool ok = Put(out, cap, &n, "{\"msg_id\":\"") && Put(out, cap, &n, msg_id)
            && Put(out, cap, &n, "\",\"class\":\"") && Put(out, cap, &n, ClassWire(cls))
            && Put(out, cap, &n, "\",\"config_epoch\":") && PutInt(out, cap, &n, config_epoch)
            && Put(out, cap, &n, ",\"t_at\":");
  if (ok && cls == MsgClass::Execution) {
    // An execution without a T is not a vote the server can take: refuse here
    // rather than send a body it will 400.
    char iso[32];
    if (t_at_ms <= 0 || FormatIsoUtcMs(t_at_ms, iso, sizeof(iso)) == 0) ok = false;
    ok = ok && Put(out, cap, &n, "\"") && Put(out, cap, &n, iso) && Put(out, cap, &n, "\"");
  } else if (ok) {
    ok = Put(out, cap, &n, "null");
  }
  ok = ok && Put(out, cap, &n, "}");
  if (!ok) {
    out[0] = '\0';
    return 0;
  }
  return n;
}

int64_t DeviationMsForServer(int64_t deviation_us) {
  // floor((us + 500) / 1000), with a floor that is a floor for negatives too.
  const int64_t x = deviation_us + 500;
  int64_t q = x / 1000;
  if ((x % 1000 != 0) && (x < 0)) q -= 1;
  return q;
}

size_t BuildExecuteBody(int64_t deviation_us, bool enable_ok, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  size_t n = 0;
  const bool ok = Put(out, cap, &n, "{\"deviation_ms\":")
                  && PutInt(out, cap, &n, DeviationMsForServer(deviation_us))
                  && Put(out, cap, &n, ",\"enable_ok\":")
                  && Put(out, cap, &n, enable_ok ? "true" : "false") && Put(out, cap, &n, "}");
  if (!ok) {
    out[0] = '\0';
    return 0;
  }
  return n;
}

Reply ClassifyReply(int http_status, const char* error) {
  if (http_status >= 200 && http_status < 300) return Reply::Ok;
  if (http_status == 409 && Eq(error, "stale_config")) return Reply::RefetchConfig;
  // No round trip (<= 0), a server that is down or stood-by (5xx: game_unavailable,
  // game_hold), or a rate limit: none of these is a verdict on the vote.
  if (http_status <= 0 || http_status >= 500 || http_status == 429) return Reply::Retry;
  return Reply::Refused;
}

Resolution ResolveOutcome(const char* outcome, bool seconded, const char* inhibit_reason) {
  Resolution r;
  if (Eq(outcome, "LAUNCHED")) {
    r.resolved = true;
    r.outcome = seconded ? VoteOutcome::Seconded : VoteOutcome::Launched;
  } else if (Eq(outcome, "INHIBITED")) {
    r.resolved = true;
    r.outcome = VoteOutcome::Inhibited;
    r.reason = (inhibit_reason != nullptr && inhibit_reason[0] != '\0') ? inhibit_reason : "inhibited";
  } else if (Eq(outcome, "FAILED")) {
    r.resolved = true;
    r.outcome = VoteOutcome::Failed;
    r.reason = "execution failed";
  } else if (Eq(outcome, "ABORTED")) {
    r.resolved = true;
    r.outcome = VoteOutcome::Failed;
    r.reason = "aborted";
  } else if (Eq(outcome, "PREEMPTED")) {
    r.resolved = true;
    r.outcome = VoteOutcome::Failed;
    r.reason = "preempted";
  }
  return r;  // PENDING, or a word this build does not know: keep polling
}

}  // namespace game
