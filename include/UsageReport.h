#pragma once

// Usage counters -- the pure half. No Arduino, no NVS, no network.
//
// =============================================================================
// WHAT THIS COLLECTS, AND THE LINE IT DOES NOT CROSS
//
// Counts of feature use: how many times a detail card was opened, how many times
// each screen was switched to, how many logbook claims, whether Follow is
// configured at all, and how long the device has been up.
//
// It does NOT collect what those actions were ABOUT. Not which aircraft, not
// which callsign, not which tail number, not the follow target, not a timestamp
// per event. "How often features are used" -- never the subjects of that use.
//
// =============================================================================
// THAT LINE IS ENFORCED BY THE TYPE, NOT BY A REVIEW
//
// Everything in this file takes and returns INTEGERS. There is no String, no
// char* in, no identity of any kind that could reach the wire, because the
// report builder has no parameter capable of carrying one. A future edit that
// wanted to append a callsign would have to change the signature first, which is
// a visible act rather than an accident.
//
// This is the same move as the no-digit assertion on Follow's ocean copy (see
// CLAUDE.md, "when you decline to state a number, assert that it cannot come
// back"): the honest version has a property the dishonest version cannot have,
// so assert the property. Here the property is "carries no text", and the
// compiler is what asserts it.
//
// =============================================================================
// DELTAS, NOT TOTALS, AND WHY THE LOSS DIRECTION WAS CHOSEN
//
// Each report carries the change since the last one, so Analytics Engine can SUM
// them over any window without per-device last-value logic -- which is awkward
// in a time-bucketed store and is the reason totals were not used.
//
// A delta is committed when the request is SENT, not when a response arrives. So
// a report lost in flight is lost, and the events it carried are never counted.
// That direction is deliberate: the alternative (commit on response) turns a
// dropped response into a REPEATED delta, which invents events that did not
// happen. An undercount is a smaller lie than an overcount, and the same
// instinct as everything else here -- decline rather than state a wrong number.
//
// The bias is worth stating out loud rather than discovering later: devices on
// poor links under-report, so absolute totals are a floor, and comparisons
// between devices carry that noise. Adoption ratios are what this data is for;
// exact counts are not.

#include <cstdint>
#include <cstddef>
#include <cstdio>

namespace usage {

/// Every counter the device keeps. Monotonic and unsigned; the report carries
/// differences of these.
///
/// ONE STRUCT, NOT AN ENUM PLUS AN ARRAY, so adding a counter is a compile error
/// everywhere it has to be handled rather than a silently-zero column.
struct Counters {
    uint32_t cardOpens     = 0;  // detail card opened
    uint32_t screenRadar   = 0;  // switched TO each screen
    uint32_t screenList    = 0;
    uint32_t screenStats   = 0;
    uint32_t screenFollow  = 0;
    uint32_t logbookClaims = 0;
};

/// What one report puts on the wire. The two non-counter fields are separated
/// from the deltas because they are a different KIND of number and summing them
/// would be meaningless.
struct Report {
    Counters delta{};              // change since the previous report
    bool     followEnabled = false; // STATE, not a count -- and a bool, never a name
    uint32_t uptimeHours   = 0;     // GAUGE: hours since boot, not since the last report
};

/// The header value's field count. Server-side validation pins the same number,
/// and the host test asserts they agree -- a parser that accepts a different
/// arity than the device emits is the classic silent-drift shape.
constexpr size_t FIELD_COUNT = 8;

/// Longest value this can ever produce: 6 u32 counters + a bool + a u32 gauge,
/// comma-separated. 7 * 10 digits + 1 + 7 commas = 78, rounded up.
constexpr size_t MAX_LEN = 96;

/// b - a, saturating at zero rather than wrapping.
///
/// A counter cannot legitimately go backwards, but NVS can be restored from an
/// older snapshot and a factory reset zeroes the totals while `reported` may
/// briefly still hold the old figure. Wrapping there would emit a delta near
/// 4.29 billion, which is not a number anyone would question in a SUM -- it
/// would simply make the week's total absurd and be blamed on the query.
inline uint32_t Since(uint32_t previous, uint32_t current)
{
    return current > previous ? (current - previous) : 0u;
}

/// The delta between two snapshots, field by field.
inline Counters Delta(const Counters& previous, const Counters& current)
{
    Counters d;
    d.cardOpens     = Since(previous.cardOpens,     current.cardOpens);
    d.screenRadar   = Since(previous.screenRadar,   current.screenRadar);
    d.screenList    = Since(previous.screenList,    current.screenList);
    d.screenStats   = Since(previous.screenStats,   current.screenStats);
    d.screenFollow  = Since(previous.screenFollow,  current.screenFollow);
    d.logbookClaims = Since(previous.logbookClaims, current.logbookClaims);
    return d;
}

/// True when a report would carry nothing anyone could learn from.
///
/// A device that sat untouched for an hour still reports, because SILENCE AND
/// ZERO ARE DIFFERENT FACTS and the whole point of the uptime gauge is to tell
/// "on but unused" from "in a drawer". So this is not used to suppress reports;
/// it exists so a test can state what an empty one looks like.
inline bool Empty(const Counters& d)
{
    return d.cardOpens == 0 && d.screenRadar == 0 && d.screenList == 0 &&
           d.screenStats == 0 && d.screenFollow == 0 && d.logbookClaims == 0;
}

/// Render the header value. Returns the length written, or 0 on a bad buffer.
///
/// TAKES NUMBERS AND NOTHING ELSE -- see the file header. The format mirrors
/// X-Blip-OTA-Mem's comma-separated fixed arity because the Worker already has a
/// validated parser of that shape and a second idiom would need a second one.
inline size_t Format(const Report& r, char* out, size_t n)
{
    if (!out || n < MAX_LEN) {
        if (out && n) out[0] = '\0';
        return 0;
    }
    const int written = snprintf(out, n, "%u,%u,%u,%u,%u,%u,%u,%u",
                                (unsigned)r.delta.cardOpens,
                                (unsigned)r.delta.screenRadar,
                                (unsigned)r.delta.screenList,
                                (unsigned)r.delta.screenStats,
                                (unsigned)r.delta.screenFollow,
                                (unsigned)r.delta.logbookClaims,
                                (unsigned)(r.followEnabled ? 1u : 0u),
                                (unsigned)r.uptimeHours);
    if (written < 0 || (size_t)written >= n) { out[0] = '\0'; return 0; }
    return (size_t)written;
}

/// Hours of uptime from a millis() value, which wraps at 49.7 days.
///
/// The wrap is not handled here and must not be: a caller that tracked uptime
/// across a wrap would be reporting a number the device cannot actually support.
/// 49 days is longer than any uptime this fleet has recorded, and a gauge that
/// silently rolls to 0 is more honest than one that claims 1,193 hours because
/// somebody added a fixup.
inline uint32_t UptimeHours(uint32_t millisNow)
{
    return millisNow / 3600000u;
}

// =============================================================================
// THE REPORTING CADENCE
//
// Hourly, and the first one deferred, both for reasons that are about the fleet
// rather than about the data:
//
//   * The carrier is the cloud check-in, which happens every 10-20 SECONDS. A
//     report on every one of those would be ~8,600 Analytics Engine points per
//     device per day for data that changes on a human timescale. Hourly is 24,
//     and at 50 units that is 1,200 points a day.
//   * The first report waits 10 minutes after boot so a device power-cycling in
//     a bring-up session does not emit a report per cycle -- and so a device
//     that is switched on and immediately off contributes nothing rather than a
//     row of zero-uptime rows.
// =============================================================================

constexpr uint32_t REPORT_INTERVAL_MS = 3600000u;  // 1 h
constexpr uint32_t FIRST_REPORT_MS    =  600000u;  // 10 min after boot

/// Is a report due? `lastReportMs` of 0 means none has been sent this boot.
///
/// Unsigned subtraction, so this is correct across the millis() wrap -- the same
/// rule as FlightStats::DurationSec, and the reason neither timestamp is ever
/// compared with `<`.
inline bool Due(uint32_t nowMs, uint32_t lastReportMs)
{
    if (lastReportMs == 0) return nowMs >= FIRST_REPORT_MS;
    return (nowMs - lastReportMs) >= REPORT_INTERVAL_MS;
}

} // namespace usage
