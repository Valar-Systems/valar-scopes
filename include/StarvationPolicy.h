#pragma once

#include <stdint.h>

/**
 * WHEN SHOULD THE BOARD SAY IT IS STARVED?
 *
 * Pure, so it is host-tested against real numbers captured off two boards. The
 * I/O -- printing, the recovery attempt -- stays in AircraftManager. Same split
 * as ConfigMigration and GameFormat: put the RULE in a predicate, test the rule.
 *
 * =========================================================================
 * WHY THIS EXISTS: THE FIRST VERSION NEVER FIRED, ON A BOARD THAT WAS STARVED
 * =========================================================================
 *
 * The 2026-08-24 A/B soak put a control (no PSRAM routing) and a treatment
 * (PSRAM routing) on the same sky for 24 h. The control's photos died. Tapping
 * the SAME aircraft type on both boards thirteen seconds apart:
 *
 *     control    08:03:45   R44   hasPhoto=0
 *     treatment  08:03:58   R44   hasPhoto=1
 *
 * The watch printed ENRICHMENT STARVED exactly zero times on the control.
 *
 * Its predicate was:
 *
 *     starved = (tlsOk == 0) && !BallastHeld()
 *
 * and tlsOk is CanHandshake(), which is `BallastHeld() || CanAllocate(...)`.
 * So whenever the ballast is held, tlsOk is 1 BY CONSTRUCTION and the whole
 * expression is false. The second clause could never change the outcome; it was
 * dead. The control ran the entire soak at ball=1 and was therefore invisible to
 * its own alarm.
 *
 * THE CONFLATION, which is the actual bug: "may a handshake proceed right now"
 * is not "is this heap healthy". The ballast is a MITIGATION -- one block, held
 * back from the pool for exactly one handshake. A board down to its last
 * reserved block, unable to serve anything else, is the definition of degraded,
 * and the old predicate read it as fine.
 *
 * So the input here is CanAllocate(TLS_HANDSHAKE_BYTES) -- can the heap serve a
 * handshake ON ITS OWN MERITS -- and the ballast is deliberately NOT consulted.
 * It is still taken as a parameter so the host test can assert it makes no
 * difference, which pins the defect rather than trusting a comment.
 *
 * =========================================================================
 * WHY NOT `largest`, WHICH IS WHAT BUDGET BROKEN USES
 * =========================================================================
 *
 * The obvious move after the above is to key the watch to the same condition the
 * [health] line's "BUDGET BROKEN: largest block N < 20000" already detects --
 * it fired 2,612 times on the control and once on the treatment, which looks
 * like a ready-made signal.
 *
 * It is the wrong signal, for the reason HeapHealth.h was written:
 * heap_caps_get_largest_free_block is a MAX ACROSS REGIONS, and this chip has
 * regions nothing allocates from, so the metric latches onto an untouched
 * reserve and stops tracking the pool that is actually draining. Both plateau
 * values named in that header showed up verbatim in this very soak --
 *
 *     treatment  largest=31732   (the 32,768 B reserve)
 *     control    largest=7668    (the  8,152 B reserve)
 *
 * -- so neither number is a measurement of the pool either board allocates from.
 * Building the alarm on it would reintroduce the deleted ENRICH_TLS_HEAP_FLOOR
 * mistake under a new name. Ask the allocator; believe the answer.
 *
 * =========================================================================
 * WHY A RUN OF TICKS, AND WHY FOUR
 * =========================================================================
 *
 * A single refusal must not fire the alarm. The treatment -- a healthy board,
 * photos working all day -- broke the largest-block budget ONCE, and the
 * control's own heap oscillates rather than decaying monotonically. From its
 * capture:
 *
 *     07:27:23  largest=7668      07:27:55  largest=7924    (recovered, 1 tick)
 *     08:01:25  largest=17396     08:01:56  largest=7668    (above the 16,717
 *                                                            handshake size,
 *                                                            then back under)
 *
 * So one-tick and two-tick dips are the NORMAL behaviour of a working board, and
 * a threshold of 1 or 2 would fire on the treatment.
 *
 * CONFIRM_TICKS = 4, at the health line's ~30 s cadence, is about two minutes of
 * uninterrupted refusal. The argument for that number, rather than a smaller
 * one that would also have caught this soak:
 *
 *   - It clears the observed transients with margin. Every recovery seen in the
 *     capture happened within one or two ticks; four is double the worst.
 *   - Two minutes is already user-visible. Enrichment is paced around 5 s, so a
 *     board that cannot handshake for two minutes has failed to fill a dozen
 *     cards. Below that, nobody would have noticed anything to report.
 *   - The margin it has to discriminate is enormous -- 2,612 breaks against 1 --
 *     so buying quiet with a slightly later alarm costs nothing here. On the
 *     control the condition held for over twenty hours; four ticks late is
 *     0.03 % of the episode.
 *
 * THE ERROR DIRECTIONS ARE NOT SYMMETRIC, and that is why this is not tuned any
 * higher: a false STARVED is one noisy log line, while a missed STARVED is what
 * actually happened -- twenty-four hours of dead photos with the device
 * reporting itself healthy. When in doubt, fire.
 */
namespace starvation {

/** Consecutive refusing health ticks before the board declares itself starved. */
constexpr uint8_t CONFIRM_TICKS = 4;

/**
 * Advance the consecutive-refusal run for one health tick.
 *
 * `canAllocateHandshake` is CanAllocate(TLS_HANDSHAKE_BYTES) -- the trial
 * allocation, NOT CanHandshake(), because the ballast arm is what made the old
 * predicate blind. Any success resets the run to zero: this measures an
 * UNINTERRUPTED inability, so a board that recovers for one tick starts over.
 *
 * Saturates rather than wrapping. A run of 255 ticks is over two hours, and a
 * counter that wrapped there would silently clear the alarm on a board that had
 * been broken all day -- the precise failure this file exists to fix.
 */
inline uint8_t NextRun(uint8_t run, bool canAllocateHandshake)
{
    if (canAllocateHandshake)
        return 0;
    return (run >= 255) ? (uint8_t)255 : (uint8_t)(run + 1);
}

/**
 * Is the board starved, given the current run?
 *
 * `ballastHeld` is accepted and DELIBERATELY IGNORED. It is in the signature so
 * the host test can assert the answer does not depend on it -- the old predicate
 * consulted it and that is exactly why it never fired.
 */
inline bool IsStarved(uint8_t run, bool /*ballastHeld*/)
{
    return run >= CONFIRM_TICKS;
}

} // namespace starvation
