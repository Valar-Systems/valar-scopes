#pragma once
#include <stdint.h>

/* ===========================================================================
 * REACHABILITY IS NOT ASSOCIATION, AND THE OLD WATCHDOG ONLY WATCHED THE ONE
 * THAT CANNOT SEE THIS FAILURE.
 *
 * THE MEASUREMENT. 2026-09-03, COM16, OTA_FAULT_AT_PCT rehearsal. After the
 * injected disconnect the board logged 75 consecutive
 *
 *     start_ssl_client(): connect on fd 48, errno: 118, "Host is unreachable"
 *
 * from 19:52:50Z to 20:02:27Z -- 9 min 37 s of every single request failing at
 * the transport layer. Throughout that window `WiFi.status()` returned
 * WL_CONNECTED, the existing 10-minute supervisor in main.cpp never armed
 * (it arms on `status() != WL_CONNECTED`, which never happened), zero reboots
 * occurred, and uptime kept climbing to 14.3 min. The device was associated
 * with an access point and could not reach anything through it.
 *
 * So the instrument that was supposed to catch a dead network is measuring a
 * property the dead network does not disturb. That is this repo's oldest shape:
 * a guard that runs on every path and establishes something narrower than its
 * name suggests -- see `getMaxAllocHeap` in CLAUDE.md, which also never fired,
 * also over thousands of samples, also while the thing it guarded was failing.
 *
 * THEREFORE: WL_CONNECTED IS NOT AN INPUT TO ANY DECISION HERE.
 *
 * It is accepted by Step() and recorded, because "associated but unroutable"
 * and "not associated at all" are different diagnoses and the fleet should be
 * able to tell them apart. It is never consulted. test_net_watch_policy.cpp
 * asserts the entire Action sequence is IDENTICAL for associated=true and
 * associated=false -- the same technique StarvationPolicy::IsStarved uses to
 * pin that its answer does not depend on `ballastHeld`, and for the same
 * reason: an input a predicate must not use is worth proving unused rather
 * than commenting that it is.
 *
 * THE EVIDENCE IS TRAFFIC. HttpResult.success is `responseCode > 0` -- i.e.
 * the round trip completed, whatever the server said. A 404 from adsbdb is a
 * SUCCESS here (the network worked; the answer was "no"), and only a transport
 * failure counts against us. That distinction is what stops a quiet upstream
 * from being read as a dead LAN.
 *
 * BOTH CONDITIONS, NEVER EITHER. A run of N failures alone fires on a busy
 * device inside seconds; a wall-clock window alone fires on a device that made
 * two requests. Requiring N failures AND a minimum elapsed window means a brief
 * ISP blip or a single upstream outage cannot trip it, and the rehearsal's 75
 * failures over 9.6 min clears both by a wide margin.
 *
 * THE ERROR DIRECTIONS ARE NOT SYMMETRIC, but they point the OTHER WAY from
 * StarvationPolicy's, so the thresholds are set the other way too. A false
 * STARVED there costs a log line. A false wedge HERE costs a radio reset and
 * eventually a reboot of a healthy device -- so when in doubt, DO NOT fire.
 * ======================================================================== */

namespace netwatch {

/** Consecutive failed round trips before the ladder may start. */
constexpr uint16_t MIN_FAIL_RUN = 10;

/** ...and the run must ALSO have lasted this long. Both, never either. */
constexpr uint32_t MIN_WEDGE_MS = 5UL * 60UL * 1000UL;

/**
 * How long to wait after acting before deciding the action did not work.
 *
 * A DHCP lease plus a fresh association is a few seconds; 90 s is generous on
 * purpose, because escalating early spends the expensive stages on a network
 * that was already coming back.
 */
constexpr uint32_t STAGE_GRACE_MS = 90UL * 1000UL;

/**
 * Retry cadence once the whole ladder has been walked without recovery.
 *
 * This is the "the router has no upstream" case, and the board cannot fix it.
 * Hammering a dead network costs power and log noise and achieves nothing, so
 * the ladder re-arms slowly and the display keeps running on whatever data it
 * has. 30 min is roughly an ISP outage's order of magnitude.
 */
constexpr uint32_t BACKOFF_RETRY_MS = 30UL * 60UL * 1000UL;

/**
 * THE LADDER, CHEAPEST FIRST. Declared as an enum with a printable name (see
 * StageName) rather than three bare `if`s, because a stage that NEVER FIRED and
 * a stage that CANNOT fire must be distinguishable in the field -- and the only
 * way to tell those apart is to be able to enumerate what the ladder contains
 * independently of what it has done. NetWatchdog logs the full ladder at boot
 * for exactly that reason.
 */
enum class Stage : uint8_t {
    Healthy    = 0,
    Reconnect  = 1, ///< WiFi.disconnect()/reconnect() -- keeps the radio up
    RadioReset = 2, ///< radio off/on + fresh begin() -- drops the whole stack
    Reboot     = 3, ///< deferred-reboot NVS path, capped at one per 24 h
    Backoff    = 4, ///< ladder exhausted; slow retry, display keeps running
};

/** What the caller should DO this tick. */
enum class Action : uint8_t {
    None = 0,
    Reconnect,
    RadioReset,
    Reboot,
    EnterBackoff,
    Recovered, ///< traffic returned while the ladder was walking; stand down
};

inline const char* StageName(Stage s)
{
    switch (s) {
        case Stage::Healthy:    return "healthy";
        case Stage::Reconnect:  return "reconnect";
        case Stage::RadioReset: return "radio-reset";
        case Stage::Reboot:     return "reboot";
        case Stage::Backoff:    return "backoff";
    }
    return "?";
}

inline const char* ActionName(Action a)
{
    switch (a) {
        case Action::None:         return "none";
        case Action::Reconnect:    return "reconnect";
        case Action::RadioReset:   return "radio-reset";
        case Action::Reboot:       return "reboot";
        case Action::EnterBackoff: return "backoff";
        case Action::Recovered:    return "recovered";
    }
    return "?";
}

struct State {
    uint32_t okSeen   = 0;      ///< last observed cumulative success count
    uint32_t failSeen = 0;      ///< last observed cumulative failure count
    uint16_t failRun  = 0;      ///< consecutive failures since the last success
    uint32_t runStartMs = 0;    ///< when the current failure run began
    Stage    stage = Stage::Healthy;
    uint32_t stageAtMs = 0;     ///< when the current stage was entered
    uint8_t  cycles = 0;        ///< full ladders walked without recovery

    /// RECORDED, NEVER CONSULTED. The association state at the moment the
    /// ladder last acted, so the fleet can tell "associated but unroutable"
    /// (this defect) from "fell off the AP" (the old watchdog's case).
    bool associatedAtAction = false;
};

/** Saturate rather than wrap: a run of 65,535 must not silently read as zero. */
inline uint16_t SaturatingAdd(uint16_t run, uint32_t delta)
{
    const uint32_t sum = (uint32_t)run + delta;
    return sum > 0xFFFFu ? (uint16_t)0xFFFFu : (uint16_t)sum;
}

/**
 * Advance the ladder by one tick.
 *
 * `okTotal` / `failTotal` are the cumulative counters maintained by the request
 * path -- monotonic, so unsigned subtraction gives the delta correctly across
 * wraparound. Deltas rather than a shared "consecutive" counter because several
 * FreeRTOS tasks make requests and only this caller (the loop task) owns state.
 *
 * `associated` is WL_CONNECTED. It is stored and never read. See the header.
 *
 * NO EVIDENCE IS NOT FAILURE: a tick where neither counter moved changes
 * nothing at all, so an idle device can never walk the ladder on silence.
 */
inline Action Step(State& s, uint32_t okTotal, uint32_t failTotal, uint32_t nowMs, bool associated)
{
    const uint32_t dOk   = okTotal - s.okSeen;
    const uint32_t dFail = failTotal - s.failSeen;
    s.okSeen   = okTotal;
    s.failSeen = failTotal;

    if (dOk > 0) {
        // Traffic works. ANY success clears the run: this measures an
        // UNINTERRUPTED inability to reach the network, so a device that gets
        // one request through starts over.
        const bool wasEscalating = s.stage != Stage::Healthy;
        s.failRun = 0;
        s.runStartMs = 0;
        s.stage = Stage::Healthy;
        s.stageAtMs = nowMs;
        s.cycles = 0;
        return wasEscalating ? Action::Recovered : Action::None;
    }

    if (dFail > 0) {
        if (s.failRun == 0) s.runStartMs = nowMs;
        s.failRun = SaturatingAdd(s.failRun, dFail);
    }

    // BOTH conditions, and they are checked before any stage logic so that a
    // device which recovers mid-ladder cannot be escalated by a stale run.
    if (s.failRun < MIN_FAIL_RUN) return Action::None;
    if ((uint32_t)(nowMs - s.runStartMs) < MIN_WEDGE_MS) return Action::None;

    const uint32_t inStage = nowMs - s.stageAtMs;
    switch (s.stage) {
        case Stage::Healthy:
            s.stage = Stage::Reconnect;
            s.stageAtMs = nowMs;
            s.associatedAtAction = associated;
            return Action::Reconnect;

        case Stage::Reconnect:
            if (inStage < STAGE_GRACE_MS) return Action::None;
            s.stage = Stage::RadioReset;
            s.stageAtMs = nowMs;
            s.associatedAtAction = associated;
            return Action::RadioReset;

        case Stage::RadioReset:
            if (inStage < STAGE_GRACE_MS) return Action::None;
            s.stage = Stage::Reboot;
            s.stageAtMs = nowMs;
            s.associatedAtAction = associated;
            return Action::Reboot;

        case Stage::Reboot:
            // Reached only when the reboot did NOT happen -- the 24 h cap
            // refused it, or NVS was unavailable, or the clock is unsynced. A
            // successful reboot never returns here at all. So this is the
            // "capped" path, and it must lead to backoff rather than to a
            // retry, or an unreachable network becomes a reboot loop the moment
            // the cap expires.
            if (inStage < STAGE_GRACE_MS) return Action::None;
            s.stage = Stage::Backoff;
            s.stageAtMs = nowMs;
            if (s.cycles < 255) s.cycles++;
            return Action::EnterBackoff;

        case Stage::Backoff:
            if (inStage < BACKOFF_RETRY_MS) return Action::None;
            s.stage = Stage::Reconnect;
            s.stageAtMs = nowMs;
            s.associatedAtAction = associated;
            return Action::Reconnect;
    }
    return Action::None;
}

} // namespace netwatch
