#pragma once
// When the radar asks for its basemap, and what each answer means (docs/blipscope-map-client-steps.md, step A, with the
// rules registered on 2026-11-11 before the code; F1, F2, F5, F6 and F8 in docs/blipscope-basemap-client.md). Pure
// C++11, no Arduino: the loop task calls these, test/host/test_map_fetch.cpp grades CA4-CA11 against fixed clocks, and
// test/host/sabotage_map_fetch.py plants one break per property into a copy of this file.
//
//   CA4  a 202 or 429 with a usable Retry-After (1-4 ASCII digits, 1-3,600) asks again exactly that long after the reply
//   CA5  a 202 whose Retry-After is refused (absent, or anything else) waits kRetryFallbackS: refused, never clamped
//   CA6  404, 422 and 400: no package for this config, and nothing asked until the location or radius changes
//   CA7  every other failure waits 60, 120, 240 ... s, to 3,600, from the reply; only a package that passes resets it
//   CA7b 401 and 403 wait an hour, leaving that level alone: a credential, not a build
//   CA8  a 200 that yields no package counts against its config, at most 3, then no package
//   CA9  a spread boot (the nightly reboot, the network watchdog's, the new image's first) waits D before asking
//   CA10 a change of the request's key drops the package and asks at once, whatever was pending; a zoom is no change
//   CA11 an answer for a config that has since changed is dropped
//   F8   once a package is held, nothing more is asked for that config: it lives in RAM, once per boot
// Every time comparison is a wrap-safe SIGNED difference, as in WxPolicy (WD5 addendum 7): a time later than the pass's
// `now` is not due, rather than underflowing to "late".
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "MapQuery.h"
#include "RebootCause.h"

namespace mappolicy {

constexpr uint32_t kRetryFallbackS = 60;      // CA5
constexpr uint32_t kRetryAfterMaxS = 3600;    // CA4: a larger value is refused, not clamped
constexpr uint32_t kBackoffFirstS = 60;       // CA7
constexpr uint32_t kBackoffMaxS = 3600;
constexpr uint32_t kCredentialS = 3600;       // CA7b
constexpr uint8_t kGateRefusalsMax = 3;       // CA8
constexpr uint32_t kSpreadS = 7200;           // CA9: F6's two hours
constexpr uint32_t kReplyTimeoutMs = 60000;   // a reply this late is a failed attempt (the package can near 1 MB)

/** What a request answers: the wire's lat/lon at 2 dp (x100) and every other parameter, as sent. */
struct Key {
    int32_t lat100 = 0, lon100 = 0;
    char params[mapquery::kParamsMax] = {0};
    bool operator==(const Key& o) const
    {
        return lat100 == o.lat100 && lon100 == o.lon100 && strcmp(params, o.params) == 0;
    }
    bool operator!=(const Key& o) const { return !(*this == o); }
};

/** What the fetch task's gate (maplayer::Load) said of a 200's body. Any 200 that yields no accepted package -- a CRC
 *  or section 8 refusal, a body over the cap, short or stalled -- is Refused. */
enum class Gate : uint8_t { NotRun, Passed, Refused };
enum class Outcome : uint8_t { Ignored, Dropped, Accepted, Wait, RetryAfterRefused, NoPackage, Refused, Credential, Error };

struct State {
    Key key;
    bool haveKey = false;
    uint32_t gen = 0;                     // CA11: the config's number; every change of the key moves it
    bool held = false;                    // F8: a package passed the gate for this config
    bool stopped = false;                 // CA6, CA8: no package for this config
    uint8_t gateRefusals = 0;             // CA8, per config
    uint32_t backoffS = kBackoffFirstS;   // CA7: the next failure's wait
    bool waiting = false;                 // nothing is asked before notBeforeMs
    uint32_t notBeforeMs = 0;
    uint32_t lastWaitS = 0;               // the wait a reply just set, for the log line (never the spread)
    bool inFlight = false;
    uint32_t sentMs = 0;
    uint32_t seq = 0;
    uint32_t sentGen = 0;
};

inline void Wait(State& s, uint32_t nowMs, uint32_t secs)
{
    s.waiting = true;
    s.notBeforeMs = nowMs + secs * 1000u;
    s.lastWaitS = secs;
}

/** CA7 and CA7b: how long a failure waits. 401 and 403 wait the hour and leave the level; anything else waits the
 *  level and doubles it, to the cap. */
inline uint32_t FailureWaitS(State& s, bool credential)
{
    if (credential) return kCredentialS;
    const uint32_t w = s.backoffS;
    s.backoffS = w * 2 > kBackoffMaxS ? kBackoffMaxS : w * 2;
    return w;
}

/** CA4 and CA5: Retry-After as whole seconds, 1-4 ASCII digits whose value is 1-3,600. False -- refused -- for anything
 *  else: absent, empty, 0, a sign, a space, a fraction, five digits, an HTTP date, more than an hour. Never clamped. */
inline bool RetryAfterS(const char* v, uint32_t* out)
{
    uint32_t n = 0;
    bool ok = v != nullptr && v[0] != 0;
    for (size_t i = 0; ok && v[i] != 0; ++i) {
        if (i >= 4 || v[i] < '0' || v[i] > '9') ok = false;
        else n = n * 10 + (uint32_t)(v[i] - '0');
    }
    ok = ok && n >= 1 && n <= kRetryAfterMaxS;
    *out = ok ? n : 0;
    return ok;   // refused is never a value
}

/** CA9 (F6; Q-MA2): whether this boot waits before its first request. The deferred reboot's cause is
 *  ConsumeDeferredRebootCause(); `followsUpdate` is OtaUpdater's BootFollowsUpdate(), the new image's first boot. */
inline bool Spreads(uint8_t deferredCause, bool followsUpdate)
{
    return followsUpdate || deferredCause == rebootcause::OTA_CHECK || deferredCause == rebootcause::NET_WEDGE;
}

/** CA9: D, in seconds, FNV-1a over a domain-separated string, mod two hours. Neither the id, the hash nor D is ever
 *  printed. */
inline uint32_t SpreadS(const char* id)
{
    uint32_t h = 2166136261u;
    for (const char* p = "map-spread|"; *p; ++p) h = (h ^ (uint8_t)*p) * 16777619u;
    for (const char* p = id; p != nullptr && *p; ++p) h = (h ^ (uint8_t)*p) * 16777619u;
    return h % kSpreadS;
}

/** Once per boot, at the first Initialise. A spread boot waits spreadS before its first request. lastWaitS is left
 *  alone, so no log line can print D. */
inline void Begin(State& s, bool spread, uint32_t spreadS, uint32_t nowMs)
{
    if (!spread) return;
    s.waiting = true;
    s.notBeforeMs = nowMs + spreadS * 1000u;
}

/** The request's key, built from the stored config at every Initialise. True when it CHANGED: the caller drops the
 *  package it holds. The boot's first key is not a change, so a pending spread stands. */
inline bool OnQuery(State& s, const Key& k)
{
    if (!s.haveKey) {
        s.key = k;
        s.haveKey = true;
        return false;
    }
    if (s.key == k) return false;
    s.key = k;
    ++s.gen;                 // CA11: an answer in flight now answers an older config
    s.held = false;          // CA10: the package belongs to the old view
    s.stopped = false;       // CA6, CA8: a new config earns a new attempt
    s.gateRefusals = 0;
    s.waiting = false;       // CA10: at once, whatever was pending
    return true;
}

/** Whether to send a request now. Ends a request unanswered for kReplyTimeoutMs as a failed attempt. */
inline bool ShouldFetch(State& s, bool enabled, uint32_t nowMs)
{
    if (s.inFlight) {
        if ((int32_t)(nowMs - s.sentMs) < (int32_t)kReplyTimeoutMs) return false;   // one in flight
        s.inFlight = false;   // the reply never came: a failed attempt (CA7)
        Wait(s, nowMs, FailureWaitS(s, false));
    }
    if (!enabled || !s.haveKey || s.held || s.stopped) return false;
    return !s.waiting || (int32_t)(nowMs - s.notBeforeMs) >= 0;
}

/** A request is being sent now. Returns its number for OnResponse. */
inline uint32_t OnSend(State& s, uint32_t nowMs)
{
    s.inFlight = true;
    s.sentMs = nowMs;
    s.sentGen = s.gen;
    s.waiting = false;
    return ++s.seq;
}

/** The reply to request `seq`, arriving at `nowMs`. status <= 0 is a transport failure; `retryAfter` is the header's
 *  value or nullptr; `gate` is what maplayer::Load said of a 200's body. */
inline Outcome OnResponse(State& s, uint32_t seq, int status, const char* retryAfter, Gate gate, uint32_t nowMs)
{
    if (!s.inFlight || seq != s.seq) return Outcome::Ignored;   // late (it timed out) or stray: changes nothing
    s.inFlight = false;
    if (s.sentGen != s.gen) return Outcome::Dropped;             // CA11: it answers a config that has since changed
    if (status == 200) {
        if (gate == Gate::Passed) {
            s.held = true;
            s.backoffS = kBackoffFirstS;   // CA7: the reset
            return Outcome::Accepted;
        }
        if (++s.gateRefusals >= kGateRefusalsMax) {
            s.stopped = true;   // CA8: a body this config keeps failing to read
            return Outcome::NoPackage;
        }
        Wait(s, nowMs, FailureWaitS(s, false));
        return Outcome::Refused;
    }
    if (status == 202 || status == 429) {
        uint32_t ra = 0;
        if (RetryAfterS(retryAfter, &ra)) {
            Wait(s, nowMs, ra);   // CA4
            return Outcome::Wait;
        }
        if (status == 202) {
            Wait(s, nowMs, kRetryFallbackS);   // CA5
            return Outcome::RetryAfterRefused;
        }
        Wait(s, nowMs, FailureWaitS(s, false));   // a 429 without a usable Retry-After (CA7)
        return Outcome::Error;
    }
    if (status == 404 || status == 422 || status == 400) {
        s.stopped = true;   // CA6 (F5): no package for this config
        return Outcome::NoPackage;
    }
    if (status == 401 || status == 403) {
        Wait(s, nowMs, FailureWaitS(s, true));   // CA7b
        return Outcome::Credential;
    }
    Wait(s, nowMs, FailureWaitS(s, false));   // 5xx, a transport failure, and any other status (CA7)
    return Outcome::Error;
}

}  // namespace mappolicy
