#pragma once
// The request headers a board sends to the proxy: ONE builder for every edition that talks to it.
//
// Weatherscope's Ask A (docs/weatherscope-stage4.md S4.32): the radar built these in CloudFeed::Headers, which a scoped
// build may not compile (it would send the bare slug as its model). A second, hand-copied builder would be a second
// path that drifts; this is the one path. CloudFeed::Headers calls it with variant::SLUG; a scoped edition calls it
// with the identity ota::ModelString fills (include/OtaIdentity.h).
//
// `model` is a const char*, not a String, so the radar's pair is built from a const char* exactly as it always was:
// always_inline, no extra copy, and the radar's image does not change for it (S1, the registration's S1-A).

#include <Arduino.h>
#include <utility>
#include <vector>

#include "DeviceIdentity.h"  // LeaderboardId() for the X-Blip-Device header
#include "OtaUpdater.h"      // FW_VERSION for the X-Blip-FW header

namespace devheaders {

inline __attribute__((always_inline)) std::vector<std::pair<String, String>> Build(const String& key, const char* model,
                                                                                  const String& otaMem,
                                                                                  const String& usage,
                                                                                  const String& bootReason)
{
    std::vector<std::pair<String, String>> h = {
        { "X-Blip-Key", key },
        { "X-Blip-Model", model },
        { "X-Blip-FW", String(FW_VERSION) },
        // Device identity, so the proxy can use its PER-DEVICE key path. LeaderboardId() is the salted SHA-256 of the
        // MAC, already published on the leaderboard, and is not a secret. It is NOT the raw MAC -- that never leaves
        // the device. (The history of this header is in CloudFeed.cpp, where it was first sent.)
        { "X-Blip-Device", DeviceIdentity::LeaderboardId() },
    };
    // One-shot OTA memory report, present only on the first check-in after an update attempt (OtaUpdater.h, README
    // "Telemetry"). It rides a request the device was making anyway: no extra traffic, no endpoint of its own.
    if (!otaMem.isEmpty())
        h.push_back({ "X-Blip-OTA-Mem", otaMem });
    // Anonymous feature-use counts, at most once an hour (README "Telemetry"). Counts only: digits and commas by
    // construction, asserted in test/host/test_usage_report.cpp.
    if (!usage.isEmpty())
        h.push_back({ "X-Blip-Usage", usage });
    // Why this boot happened, on the first check-in after it and never again (OtaUpdater.h, TakeBootReasonReport).
    if (!bootReason.isEmpty())
        h.push_back({ "X-Blip-Boot", bootReason });
    return h;
}

}  // namespace devheaders
