#pragma once
// When the radar fetches weather, what it keeps, and when it shows it (docs/wx-device.md, WD3). Pure C++11,
// no Arduino: the fetch task and the loop call these, and test/host/test_wx_policy.cpp grades every
// property against a table and random event sequences. test/host/sabotage_wx_policy.py plants one break
// per property into a copy of this file and requires exactly that property to go red.
//
// The properties, frozen in docs/wx-device.md:
//   (a) disabled means 0 requests, ever;
//   (b) a 200 replaces the held crop only if it is <= 32,768 bytes, valid (wxcrop::Validate Ok), no older
//       than the held frame, and answers the current query -- otherwise the held crop is kept;
//   (c) never shown older than 600 s by the NTP clock, and never shown without a set clock;
//   (d) If-None-Match is sent only with an ETag earned by the current query; a query change drops both;
//   (e) every 60 s while enabled; errors back off 60 -> 120 -> 240 -> 480 -> 600 s, reset by a 200 or 304;
//   (f) a crop entirely of 14 is held but shown as "outside coverage", never as an empty (dry) overlay;
//   (g) the attempt is recorded at the SEND (OnSend, with the loop pass's own clock), one request is in flight at a
//       time, a reply never moves the interval's start and never triggers a refetch, and a reply that never comes
//       ends after kReplyTimeoutMs as a failed attempt (the backoff). WD5 addendum 6: the 2026-10-26 /wx storm was a
//       reply-time attempt compared against the pass's earlier `now`, which underflowed to "due".
#include <stdint.h>
#include <string.h>

namespace wxpolicy {

constexpr uint32_t kMaxBytes = 32768;
constexpr uint32_t kMaxAgeS = 600;
constexpr uint32_t kPollMs = 60000;
constexpr uint32_t kMaxBackoffMs = 600000;
constexpr size_t kEtagMax = 96;
constexpr uint32_t kReplyTimeoutMs = 30000;   // (g) a request unanswered this long is a failed attempt

/** The query a crop answers: the wire's lat/lon at 2 dp (as integers x100) and r in whole km. */
struct Query {
    int32_t lat100 = 0, lon100 = 0, rKm = 0;
    bool operator==(const Query& o) const { return lat100 == o.lat100 && lon100 == o.lon100 && rKm == o.rKm; }
    bool operator!=(const Query& o) const { return !(*this == o); }
};

struct State {
    Query query;                  // the query the held crop and ETag answer
    bool haveCrop = false;
    uint32_t frameEpoch = 0;      // the held crop's frame time (UTC s)
    bool allNoCoverage = false;   // every cell of the held crop is 14
    char etag[kEtagMax] = {0};    // "" = none
    uint32_t intervalMs = kPollMs;
    uint32_t lastAttemptMs = 0;   // (g) the SEND of the latest request, never its reply
    bool attempted = false;
    bool inFlight = false;        // (g) a request has been sent and not yet answered or timed out
    uint32_t sentMs = 0;          // (g) when the in-flight request was sent
    uint32_t seq = 0;             // (g) the in-flight request's number; a reply carrying another is ignored
    uint32_t lastReplyMs = 0;     // diagnostics only: when the latest accepted reply arrived
};

enum class Show : uint8_t { Nothing, Crop, OutsideCoverage };
enum class Outcome : uint8_t { Replaced, NotModified, Kept, Error };

/** The current query has changed (a location or range save): what is held answers another query. */
inline void OnQuery(State& s, const Query& q)
{
    if (s.query == q) return;
    s.query = q;
    s.haveCrop = false;                                   // (d) the crop answered the old query
    s.frameEpoch = 0;
    s.allNoCoverage = false;
    s.etag[0] = 0;                                        // (d) and so did its ETag
}

inline void Backoff(State& s)
{
    const uint32_t next = s.intervalMs * 2;               // (e)
    s.intervalMs = next > kMaxBackoffMs ? kMaxBackoffMs : next;
}

/** A request is being sent now, at `nowMs` (the loop pass's own clock). Returns its number for OnResponse. */
inline uint32_t OnSend(State& s, uint32_t nowMs)
{
    s.attempted = true;                                   // (g) the attempt is the send
    s.lastAttemptMs = nowMs;                              // (g) the interval runs from here
    s.inFlight = true;                                    // (g) one at a time
    s.sentMs = nowMs;
    return ++s.seq;
}

/** Whether to make a request now. Ends a request unanswered for kReplyTimeoutMs. Both elapsed times are a
 *  wrap-safe SIGNED difference (WD5 addendum 7): a recorded time later than `nowMs` -- the 2026-10-26 storm's shape,
 *  an attempt stamped after the pass's `now` -- is still in flight, and not due, instead of underflowing to "late". */
inline bool ShouldFetch(State& s, bool enabled, uint32_t nowMs)
{
    if (!enabled) return false;                           // (a)
    if (s.inFlight) {
        if ((int32_t)(nowMs - s.sentMs) < (int32_t)kReplyTimeoutMs) return false;   // (g) one in flight
        s.inFlight = false;                               // (g) the reply never came: a failed attempt
        Backoff(s);
    }
    if (!s.attempted) return true;
    return (int32_t)(nowMs - s.lastAttemptMs) >= (int32_t)s.intervalMs;   // (e), and (g): later than now is not due
}

/** The If-None-Match value for a request answering `q`, or nullptr. */
inline const char* IfNoneMatch(const State& s, const Query& q)
{
    if (s.etag[0] == 0) return nullptr;
    if (s.query != q) return nullptr;                     // (d) never an ETag earned by another query
    return s.etag;
}

/** The reply to request `seq`, made for `asked`. status <= 0 is a network failure. `valid` is
 *  wxcrop::Validate()==Ok on the body; `frameEpoch`/`allNoCoverage` come from that body. `nowMs` is the reply's
 *  arrival, kept for diagnostics only: the interval runs from the send (g). */
inline Outcome OnResponse(State& s, uint32_t seq, const Query& asked, int status, uint32_t bytes, bool valid,
                          uint32_t frameEpoch, bool allNoCoverage, const char* etag, uint32_t nowMs)
{
    if (!s.inFlight || seq != s.seq) return Outcome::Kept;   // (g) late or stray: changes nothing
    s.inFlight = false;                                   // (g) answered
    s.lastReplyMs = nowMs;                                // (g) never lastAttemptMs
    if (status == 304) {
        s.intervalMs = kPollMs;                           // (e) reset
        return Outcome::NotModified;
    }
    if (status != 200) {
        Backoff(s);
        return Outcome::Error;
    }
    s.intervalMs = kPollMs;                               // (e) reset: the server answered
    if (asked != s.query) return Outcome::Kept;           // (b) answers a query that is no longer current
    if (bytes > kMaxBytes) return Outcome::Kept;          // (b) over the cap
    if (!valid) return Outcome::Kept;                     // (b) refused by the strict decoder
    if (s.haveCrop && frameEpoch < s.frameEpoch) return Outcome::Kept;   // (b) never backwards
    s.haveCrop = true;
    s.frameEpoch = frameEpoch;
    s.allNoCoverage = allNoCoverage;
    s.etag[0] = 0;
    if (etag) { strncpy(s.etag, etag, kEtagMax - 1); s.etag[kEtagMax - 1] = 0; }
    return Outcome::Replaced;
}

/** What the radar shows now. `clockSet` = NTP has set the clock; `nowEpoch` = UTC seconds. */
inline Show WhatToShow(const State& s, bool enabled, bool clockSet, uint32_t nowEpoch)
{
    if (!enabled || !s.haveCrop) return Show::Nothing;
    if (!clockSet) return Show::Nothing;                  // (c) never without a set clock
    if (nowEpoch > s.frameEpoch && nowEpoch - s.frameEpoch > kMaxAgeS) return Show::Nothing;   // (c)
    if (s.allNoCoverage) return Show::OutsideCoverage;    // (f) the note, never an empty overlay
    return Show::Crop;
}

}  // namespace wxpolicy
