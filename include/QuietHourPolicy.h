#pragma once
#include <stdint.h>

/* ===========================================================================
 * WHEN TO REBOOT FOR THE DAILY UPDATE CHECK.
 *
 * THE PROBLEM. The check is currently `millis() - lastOtaCheck > 24h`, which is
 * uptime-based and therefore drifts: a board that boots at 14:00 reboots at
 * 14:00 every day thereafter, in front of the customer. The v10 rollout fired at
 * 20:04Z and 20:34Z for exactly this reason. A reboot is about 60 s of black
 * screen (measured: 58 s and 59 s, defer to running-v10) -- fine at 03:00,
 * not fine at 20:00.
 *
 * THIS DEVICE HAS NO DST, AND THAT IS THE WHOLE REASON THIS HEADER IS SHORT.
 *
 * main.cpp calls `configTime(0, 0, "pool.ntp.org")` -- zero offset, zero DST
 * offset -- so the system clock is pure UTC and every local rendering in the
 * firmware is `utc + utcOffsetSec`, a fixed number from the `tz-offset` config.
 * There is no zoneinfo, no POSIX TZ string, and no rule that ever moves that
 * number on its own.
 *
 * So a DST transition is NOT A CLOCK EVENT HERE. It is the customer editing a
 * config field by hand, twice a year. "DST changed" and "the user changed their
 * timezone" are the SAME INPUT, which collapses the two classic hazards of a
 * local-time scheduler into one case -- and makes that case more likely rather
 * than less, since it now includes everyone in a DST region twice a year plus
 * anyone who travels or moves.
 *
 * IF THAT EVER CHANGES -- if someone sets a real TZ string so libc applies DST
 * -- this analysis is void: the offset would then move underneath this policy
 * without a config save, and the seeding below would need to survive it. The
 * check is one grep: `configTime(` in main.cpp with anything but 0, 0.
 *
 * THE TWO HAZARDS, AND WHY NEITHER SURVIVES.
 *
 *   DOUBLE-FIRE. Shift the offset back two hours at 05:00 local and 03:00
 *   happens again the same evening. Keying on the local hour ALONE would fire
 *   twice. So the trigger also requires the local DAY to differ from the last
 *   one fired, AND at least MIN_INTERVAL_S of real time to have passed -- the
 *   second because a backwards shift across midnight decrements the local day
 *   and would otherwise look like a fresh one.
 *
 *   SKIP. Shift the offset forward two hours at 02:59 local and 03:00 never
 *   occurs. The hour is simply missed, and with an hour-only trigger the board
 *   would wait a further day. CATCHUP_S closes it: once more than a day has
 *   passed since the last fire, the next tick fires regardless of the hour.
 *   A late check beats a missed one, and the cap still bounds it.
 *
 * WHAT THIS POLICY DOES NOT DECIDE. Whether the reboot actually happens.
 * DeferRebootWithCause owns the 24 h cap and refuses on an unsynced clock, and
 * it stays the single implementation of both -- this file proposes, that one
 * disposes. Same split as the reachability ladder's rung 3, and for the same
 * reason: two guards on one rule is two rules.
 * ======================================================================== */

namespace quiet {

/** Local hour at which the daily reboot is preferred. */
constexpr int QUIET_HOUR = 3;

/**
 * Minimum real time between fires, independent of what local time says.
 *
 * The structural defence against a double-fire: no arrangement of offset edits
 * can produce two proposals inside this window. 20 h rather than 24 h so a
 * legitimate next-day fire is never suppressed by a few minutes of jitter --
 * consecutive quiet hours are 24 h apart, and the margin absorbs an offset edit
 * of up to four hours without eating the following day.
 */
constexpr uint32_t MIN_INTERVAL_S = 20UL * 3600UL;

/**
 * After this long without a fire, go regardless of the local hour.
 *
 * This is the skip catcher. 25 h: longer than any legitimate gap between quiet
 * hours (24 h), short enough that a skipped day costs about an hour rather than
 * another whole day.
 */
constexpr uint32_t CATCHUP_S = 25UL * 3600UL;

/** The pre-clock fallback interval, matching today's uptime-based behaviour. */
constexpr uint32_t FALLBACK_MS = 24UL * 3600UL * 1000UL;

/** Below this, time() has not been set by NTP and local time is meaningless. */
constexpr uint32_t CLOCK_SANE_EPOCH = 1735689600UL; // 2025-01-01T00:00:00Z

enum class Decision : uint8_t {
    None = 0,
    QuietHour, ///< the local quiet hour arrived
    Catchup,   ///< a quiet hour was missed (offset edit); going late
    Fallback,  ///< no usable clock; the old uptime timer
};

inline const char* DecisionName(Decision d)
{
    switch (d) {
        case Decision::None:      return "none";
        case Decision::QuietHour: return "quiet-hour";
        case Decision::Catchup:   return "catchup";
        case Decision::Fallback:  return "fallback";
    }
    return "?";
}

struct State {
    bool     seeded = false;
    int32_t  lastFiredDay = -2147483647; ///< local day index of the last fire
    uint32_t lastFiredEpoch = 0;         ///< 0 = has not fired this boot
    uint32_t lastFallbackMs = 0;
};

/** Local day index (days since the epoch, in local time). Negative-offset safe. */
inline int32_t LocalDay(uint32_t utcEpoch, long offsetSec)
{
    // int64 because a negative offset can push a small epoch below zero, and
    // C++ integer division truncates TOWARD ZERO -- so a naive `/86400` on a
    // negative value lands a day out. Clamped rather than floored because a
    // pre-1970 local time cannot occur on a board whose clock passed
    // CLOCK_SANE_EPOCH; the clamp is belt-and-braces on an impossible input.
    int64_t local = (int64_t)utcEpoch + (int64_t)offsetSec;
    if (local < 0) local = 0;
    return (int32_t)(local / 86400);
}

/** Local hour, 0..23. */
inline int LocalHour(uint32_t utcEpoch, long offsetSec)
{
    int64_t local = (int64_t)utcEpoch + (int64_t)offsetSec;
    if (local < 0) local = 0;
    return (int)((local / 3600) % 24);
}

/**
 * Decide whether to propose a reboot now.
 *
 * `utcEpoch` is time(nullptr); `offsetSec` is AircraftManager's utcOffsetSec;
 * `uptimeMs` is millis(). Records its own fire, because DeferRebootWithCause
 * does not return when it succeeds, so there is no "afterwards" in which the
 * caller could record it. A refusal therefore also counts as a fire -- which is
 * correct, because the only thing that refuses is the 24 h cap, and the cap
 * refuses precisely when a reboot has already happened.
 */
inline Decision Step(State& s, uint32_t utcEpoch, long offsetSec, uint32_t uptimeMs)
{
    if (utcEpoch < CLOCK_SANE_EPOCH) {
        // No usable clock. Behave exactly as the firmware does today: propose on
        // the uptime timer. NOTE this proposal is then REFUSED by
        // DeferRebootWithCause, which declines on an unsynced clock -- so an
        // unsynced board does not update, and that is pre-existing behaviour
        // this policy preserves rather than a regression it introduces. Stated
        // because "the fallback fires" and "the board updates" are different
        // claims and only the first is true.
        if (uptimeMs - s.lastFallbackMs >= FALLBACK_MS) {
            s.lastFallbackMs = uptimeMs;
            return Decision::Fallback;
        }
        return Decision::None;
    }

    const int32_t day  = LocalDay(utcEpoch, offsetSec);
    const int     hour = LocalHour(utcEpoch, offsetSec);

    // SEEDED ONLY WHEN WE BOOT INSIDE THE WINDOW. A board that comes up at 03:30
    // must not immediately propose the reboot it has just finished doing; a
    // board that comes up at 02:00 must still fire at 03:00 the same day.
    // Seeding on the day unconditionally would get the second case wrong, which
    // is why this is conditional on the hour rather than on having run at all.
    if (!s.seeded) {
        s.seeded = true;
        if (hour == QUIET_HOUR) s.lastFiredDay = day;
    }

    const bool everFired = s.lastFiredEpoch != 0;
    const uint32_t sinceFire = everFired ? (utcEpoch - s.lastFiredEpoch) : 0;

    // The skip catcher runs FIRST: once a day has been missed, the hour no
    // longer matters and waiting for it is the bug being fixed.
    if (everFired && sinceFire >= CATCHUP_S) {
        s.lastFiredDay = day;
        s.lastFiredEpoch = utcEpoch;
        return Decision::Catchup;
    }

    if (hour != QUIET_HOUR) return Decision::None;
    if (day == s.lastFiredDay) return Decision::None;
    if (everFired && sinceFire < MIN_INTERVAL_S) return Decision::None;

    s.lastFiredDay = day;
    s.lastFiredEpoch = utcEpoch;
    return Decision::QuietHour;
}

} // namespace quiet
