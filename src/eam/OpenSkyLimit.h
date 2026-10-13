#pragma once

// OpenSky's rate-limit answer, and what the command-post watch does with it. Pure (no Arduino),
// so test/host/test_abncp_identity.cpp grades the shipping code as-is.
//
// THE HEADERS ARE OPENSKY'S OWN NAMES. REST API docs, "API Credits"
// (https://openskynetwork.github.io/opensky-api/rest.html; source pinned at
// https://github.com/openskynetwork/opensky-api/blob/c4af9c9e5aba25fc6ee0852b54a91b8a7c02f76b/docs/free/rest.rst):
//   "When credits are available, X-Rate-Limit-Remaining shows your remaining balance. When
//    exhausted, the API returns 429 Too Many Requests and X-Rate-Limit-Retry-After-Seconds
//    indicates how many seconds to wait."
//
// RULED (Daniel, 2026-10-12): respect them; back off until the reset; show "OpenSky limit
// reached, resuming HH:MM" on the ABNCP screen instead of a blank. The device's clock is Zulu
// (UTC) everywhere -- the Missileer has no local-time offset (no tz setting; the clock screen
// is 24 h UTC) -- so the time is shown as UTC with a "Z".
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace osklimit {

constexpr const char* kRemainingHeader = "X-Rate-Limit-Remaining";
constexpr const char* kRetryAfterHeader = "X-Rate-Limit-Retry-After-Seconds";
constexpr int kTooManyRequests = 429;
// A sanity bound on the server's number, not a policy: the longest refill period in the docs
// is daily, so anything past two days is treated as two days.
constexpr long kMaxRetryAfterS = 2L * 24 * 3600;

// A header value as whole non-negative seconds (digits, surrounding spaces allowed); -1 when it
// is absent, empty, signed, fractional or anything else.
inline long ParseSeconds(const char* v)
{
    if (!v) return -1;
    while (*v == ' ' || *v == '\t') ++v;
    if (*v < '0' || *v > '9') return -1;
    long n = 0;
    for (; *v >= '0' && *v <= '9'; ++v) {
        n = n * 10 + (*v - '0');
        if (n > 100000000L) return -1;  // absurd: not a wait anyone meant
    }
    while (*v == ' ' || *v == '\t') ++v;
    return *v == '\0' ? n : -1;
}

// After a 429, how long until the next poll, in ms: the server's Retry-After (bounded by
// kMaxRetryAfterS), never less than the normal interval. When the header is missing or not a
// number, `fallbackMs` -- the caller's ordinary failure back-off. Not a 429: 0 (no limit wait).
inline uint32_t LimitDelayMs(int status, const char* retryAfter, uint32_t intervalMs, uint32_t fallbackMs)
{
    if (status != kTooManyRequests) return 0;
    long s = ParseSeconds(retryAfter);
    if (s < 0) return fallbackMs < intervalMs ? intervalMs : fallbackMs;
    if (s > kMaxRetryAfterS) s = kMaxRetryAfterS;
    const uint32_t ms = (uint32_t)s * 1000u;
    return ms < intervalMs ? intervalMs : ms;
}

// The screen's second line. With the clock set (resumeEpochUtc > 0): "resuming HH:MMZ", the
// minute ROUNDED UP so the shown time is never earlier than the actual retry. Without it:
// "resuming in N min" from the remaining ms, also rounded up.
inline void FormatResume(char* buf, size_t n, long resumeEpochUtc, uint32_t msLeft)
{
    if (!buf || n == 0) return;
    if (resumeEpochUtc > 0) {
        const long minutes = (resumeEpochUtc + 59) / 60;  // ceil to the minute
        const int hh = (int)((minutes / 60) % 24);
        const int mm = (int)(minutes % 60);
        snprintf(buf, n, "resuming %02d:%02dZ", hh, mm);
    } else {
        const unsigned long mins = (msLeft + 59999UL) / 60000UL;
        snprintf(buf, n, "resuming in %lu min", mins);
    }
}

} // namespace osklimit
