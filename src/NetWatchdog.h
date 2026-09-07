#pragma once
#include <stdint.h>
#include "NetWatchPolicy.h"

/*
 * The reachability watchdog: the Arduino-side glue around netwatch::Step().
 *
 * WHY THERE ARE TWO FILES. Everything that DECIDES lives in the pure header
 * (include/NetWatchPolicy.h) so it can be proved on a host over case counts and
 * timings a bench cannot reach in an afternoon. Everything that ACTS lives here,
 * where it needs WiFi, NVS and a reboot. The split is what makes the rehearsal
 * in docs/ota-control-plan.md a test of the ACTIONS rather than of the logic.
 *
 * THREADING. RecordOutcome() is called from whichever FreeRTOS task made the
 * request -- the fetch task, the enrich task, the MQTT task or the loop -- and
 * touches only two atomics. Tick() runs on the LOOP TASK and owns all state,
 * which is the same rule the rest of this codebase follows: background tasks
 * hand back numbers, never pointers into shared state.
 */
namespace netwatch {

/**
 * Record one completed request attempt.
 *
 * `transportOk` MUST be `responseCode > 0`, not `HttpResult.success`. They are
 * not the same thing and the difference is the whole safety of this feature:
 *
 *   Get()/Post()   success == (responseCode > 0)          -- same thing
 *   GetSecure()    success == (responseCode == 200)       -- a 404 is a FAILURE
 *   GetJson()      success also requires the parse to work
 *
 * A 404 from adsbdb and a malformed body are both round trips that REACHED the
 * network. Counting them as unreachability would let an enrichment workload
 * full of legitimate misses walk a healthy board up the ladder to a reboot.
 * HTTPClient returns a negative code for transport failures and an HTTP status
 * otherwise, so the sign of the response code is exactly the question this asks.
 */
void RecordOutcome(bool transportOk);

/**
 * Print the ladder at boot, and report a stage recovered from NVS.
 *
 * The ladder is printed IN FULL, every stage, every time -- so a stage that has
 * never fired can be told from a stage that cannot fire. An escalation ladder is
 * only legible in a log if the log says what the ladder CONTAINS as well as what
 * it did; otherwise "we never saw radio-reset" and "radio-reset was compiled out
 * six weeks ago" are the same observation. Same reason the reconcile script
 * prints the fw: table on every run.
 */
void Begin();

/** One tick. Loop task only. Performs at most one escalation per call. */
void Tick();

/**
 * The stage this boot was rebooted BY, or Stage::Healthy if it was not.
 *
 * Read from NVS once at Begin(). Feeds the reset-reason string that already
 * flows to Analytics Engine, so "how often does this fire in real homes" is a
 * fleet-wide question rather than a bench one.
 */
Stage RebootedByStage();

/** Current stage, for the [health] line. */
Stage CurrentStage();

/** Consecutive transport failures right now, for the [health] line. */
uint16_t FailRun();

} // namespace netwatch
