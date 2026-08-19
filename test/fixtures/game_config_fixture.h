// GENERATED FILE -- DO NOT EDIT.
//
// valar-eam-feed  scripts/emit-device-fixture.ts
//   source_url    https://valar-eam-feed.onrender.com/api/v1/missileer/config
//   fetched_at    2026-08-18T22:28:04.999Z
//   server_commit NULL (/status carried no `commit` field; the deployed service predates it)
//   config_epoch  1
//
// The expected strings below were produced by the SERVER's own formatter
// (formatSortieDeviation), not by reading it. That is the whole value of the
// file: a device test asserting strings somebody transcribed would be grading
// a private replica of the rule, and would agree with itself forever.
//
// Scoring OUTPUTS (shack, miss distance) are in the .json and deliberately NOT
// here. Rail 3: the device does not invent scoring, and does not carry the
// curve's answers either.

#ifndef BLIPSCOPE_TEST_GAME_CONFIG_FIXTURE_H
#define BLIPSCOPE_TEST_GAME_CONFIG_FIXTURE_H

#include <stddef.h>
#include <stdint.h>

namespace fixture {

/// The LIVE served constants, verbatim.
constexpr uint32_t kBucketUs = 200000;
constexpr uint32_t kClockFloorMs = 199;
constexpr uint32_t kExecutionWindowUs = 2000000;

struct Case { int64_t deviation_ms; const char* expected; };
struct Table { uint32_t bucket_us; uint8_t decimals; bool live; const Case* cases; size_t n; };

// bucket 0.2 s  <-- LIVE, this is the contract
constexpr Case kCases0[] = {
    { 0, "0.0" },
    { 1, "0.2" },
    { 49, "0.2" },
    { 50, "0.2" },
    { 51, "0.2" },
    { 99, "0.2" },
    { 100, "0.2" },
    { 101, "0.2" },
    { 149, "0.2" },
    { 150, "0.2" },
    { 199, "0.2" },
    { 200, "0.2" },
    { 201, "0.4" },
    { 250, "0.4" },
    { 299, "0.4" },
    { 300, "0.4" },
    { 301, "0.4" },
    { 399, "0.4" },
    { 400, "0.4" },
    { 401, "0.6" },
    { 499, "0.6" },
    { 500, "0.6" },
    { 599, "0.6" },
    { 600, "0.6" },
    { 799, "0.8" },
    { 800, "0.8" },
    { 899, "1.0" },
    { 900, "1.0" },
    { 901, "1.0" },
    { 999, "1.0" },
    { 1000, "1.0" },
    { 1001, "1.2" },
    { 1234, "1.4" },
    { 1999, "2.0" },
    { 2000, "2.0" },
    { 2001, "2.2" },
    { 3599, "3.6" },
    { 3600, "3.6" },
    { 9999, "10.0" },
    { 60000, "60.0" },
    { -1, "0.2" },
    { -99, "0.2" },
    { -100, "0.2" },
    { -199, "0.2" },
    { -200, "0.2" },
    { -201, "0.4" },
    { -300, "0.4" },
    { -401, "0.6" },
    { -1234, "1.4" },
    { -3600, "3.6" },
};
// bucket 0.1 s  (hypothetical: GAME_SCORE_BUCKET_S overridden locally)
constexpr Case kCases1[] = {
    { 0, "0.0" },
    { 1, "0.1" },
    { 49, "0.1" },
    { 50, "0.1" },
    { 51, "0.1" },
    { 99, "0.1" },
    { 100, "0.1" },
    { 101, "0.2" },
    { 149, "0.2" },
    { 150, "0.2" },
    { 199, "0.2" },
    { 200, "0.2" },
    { 201, "0.3" },
    { 250, "0.3" },
    { 299, "0.3" },
    { 300, "0.3" },
    { 301, "0.4" },
    { 399, "0.4" },
    { 400, "0.4" },
    { 401, "0.5" },
    { 499, "0.5" },
    { 500, "0.5" },
    { 599, "0.6" },
    { 600, "0.6" },
    { 799, "0.8" },
    { 800, "0.8" },
    { 899, "0.9" },
    { 900, "0.9" },
    { 901, "1.0" },
    { 999, "1.0" },
    { 1000, "1.0" },
    { 1001, "1.1" },
    { 1234, "1.3" },
    { 1999, "2.0" },
    { 2000, "2.0" },
    { 2001, "2.1" },
    { 3599, "3.6" },
    { 3600, "3.6" },
    { 9999, "10.0" },
    { 60000, "60.0" },
    { -1, "0.1" },
    { -99, "0.1" },
    { -100, "0.1" },
    { -199, "0.2" },
    { -200, "0.2" },
    { -201, "0.3" },
    { -300, "0.3" },
    { -401, "0.5" },
    { -1234, "1.3" },
    { -3600, "3.6" },
};
// bucket 0.05 s  (hypothetical: GAME_SCORE_BUCKET_S overridden locally)
constexpr Case kCases2[] = {
    { 0, "0.00" },
    { 1, "0.05" },
    { 49, "0.05" },
    { 50, "0.05" },
    { 51, "0.10" },
    { 99, "0.10" },
    { 100, "0.10" },
    { 101, "0.15" },
    { 149, "0.15" },
    { 150, "0.15" },
    { 199, "0.20" },
    { 200, "0.20" },
    { 201, "0.25" },
    { 250, "0.25" },
    { 299, "0.30" },
    { 300, "0.30" },
    { 301, "0.35" },
    { 399, "0.40" },
    { 400, "0.40" },
    { 401, "0.45" },
    { 499, "0.50" },
    { 500, "0.50" },
    { 599, "0.60" },
    { 600, "0.60" },
    { 799, "0.80" },
    { 800, "0.80" },
    { 899, "0.90" },
    { 900, "0.90" },
    { 901, "0.95" },
    { 999, "1.00" },
    { 1000, "1.00" },
    { 1001, "1.05" },
    { 1234, "1.25" },
    { 1999, "2.00" },
    { 2000, "2.00" },
    { 2001, "2.05" },
    { 3599, "3.60" },
    { 3600, "3.60" },
    { 9999, "10.00" },
    { 60000, "60.00" },
    { -1, "0.05" },
    { -99, "0.10" },
    { -100, "0.10" },
    { -199, "0.20" },
    { -200, "0.20" },
    { -201, "0.25" },
    { -300, "0.30" },
    { -401, "0.45" },
    { -1234, "1.25" },
    { -3600, "3.60" },
};

constexpr Table kTables[] = {
    { 200000, 1, true, kCases0, sizeof(kCases0) / sizeof(kCases0[0]) },
    { 100000, 1, false, kCases1, sizeof(kCases1) / sizeof(kCases1[0]) },
    { 50000, 2, false, kCases2, sizeof(kCases2) / sizeof(kCases2[0]) },
};
constexpr size_t kTableCount = sizeof(kTables) / sizeof(kTables[0]);

}  // namespace fixture

#endif  // BLIPSCOPE_TEST_GAME_CONFIG_FIXTURE_H
