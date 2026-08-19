#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Can THIS allocation succeed, right now?
 *
 * WHY THIS EXISTS, AND WHY IT IS NOT A NUMBER. Every heap gate in this codebase
 * used to read ESP.getMaxAllocHeap() and compare it to a threshold. Bench-proved
 * on 2026-08-07 (issue #163, src/probe/HeapProbe.cpp) that this cannot work on
 * this board: `heap_caps_get_largest_free_block` is a MAX ACROSS REGIONS, and the
 * S3 carries regions nothing ever allocates from --
 *
 *     0x3fcf0000  32,768 B   0 allocated, min_free never moved  -> plateau 31,732
 *     0x600fe000   8,152 B   0 allocated, min_free never moved  -> plateau  7,668
 *
 * -- so the instant the main pool's largest block falls below one of those, the
 * metric latches onto the untouched reserve and STOPS TRACKING the pool that is
 * actually draining. Over a 54 h soak the reported figure took five distinct
 * values across 6,466 samples, 97.4 % of them the identical 31,732, while free
 * heap over the same samples took 3,361. The gate keyed to it never fired once,
 * including at the two moments an allocation genuinely failed.
 *
 * The replacement is not a better number, because no number answers the question
 * a gate is asking. `free_size` tracks continuously and still cannot tell you
 * whether one 16 KB contiguous request will be served. So: ASK THE ALLOCATOR.
 * Trial-allocate the real size, free it immediately, and believe the answer. It
 * cannot be fooled by a region the caller will never be served from, because it
 * is the caller's own allocation path.
 *
 * COST: a malloc+free pair, single-digit microseconds. None of the call sites is
 * per-frame -- the three enrichment gates run before starting an HTTPS lookup and
 * the health line runs every 15-30 s -- so there is no hot path to trade away.
 */
namespace heaphealth {

/**
 * The largest SINGLE contiguous block an mbedTLS handshake was observed to need,
 * measured rather than estimated: it is the exact size the OTA version check
 * failed on, twice, 24 h apart, in bench-logs/s3-128-2026-08-04-1436.log --
 *
 *     [health] ALLOC FAILED: 16717 B (caps 0x804) in heap_caps_calloc
 *
 * Deliberately NOT padded with a safety margin. A margin here is guesswork
 * layered on a measurement, and it would make the gate refuse handshakes that
 * would in fact have succeeded -- which is the failure mode the old floor's own
 * comment was worried about ("Raising it starves enrichment off"). If a future
 * mbedTLS or CA-bundle change moves this, the [health] line's tlsOk flag will
 * start disagreeing with observed handshake outcomes, and that is the signal to
 * re-measure it rather than to inflate it.
 */
constexpr size_t TLS_HANDSHAKE_BYTES = 16717;

/** Trial-allocate `bytes` of the same memory a TLS handshake draws from. */
bool CanAllocate(size_t bytes);

/**
 * True when the reserved handshake block (fix 4, below) is currently in hand.
 *
 * Declared up here rather than with the rest of the ballast API because the
 * inline CanHandshake() immediately below consults it, and a name used in an
 * inline body has to be visible at that point.
 */
bool BallastHeld();

/**
 * Can a TLS handshake proceed right now?
 *
 * Reads as two questions and is one: either the allocator can serve the block,
 * or we are already holding one reserved for exactly this. The ballast arm is
 * checked FIRST and without allocating -- asking the allocator for a block we
 * have ourselves removed from the pool is how a reservation turns into a denial.
 */
inline bool CanHandshake() { return BallastHeld() || CanAllocate(TLS_HANDSHAKE_BYTES); }

/**
 * True when the CALLING TASK is inside CanAllocate().
 *
 * The alloc-failure callback is registered globally, so a trial that correctly
 * says "no" would otherwise increment allocFailures and print ALLOC FAILED --
 * i.e. the gate working would be indistinguishable in the logs from the failure
 * it just prevented, and the soak's outcome counters would fill with its own
 * successes. Scoped to the task handle rather than a bare flag so a GENUINE
 * failure on another task during the same few microseconds still counts.
 */
bool InTrial();

/** How many times a gate has said no. These are refusals, not failures. */
uint32_t TrialRejectionCount();

/**
 * FIX 1 -- CanHandshake() for call sites that run EVERY LOOP.
 *
 * The detail-card enrichment gate is reached once per loop iteration, so at a
 * 41-43 ms frame it trials 16,717 B about 23 times a second, and while the heap
 * is low every one of those fails. Measured on COM119: 23 rejections/s, indefinitely,
 * achieving nothing. A failing heap_caps_malloc still walks every region before
 * it can answer, so this is not free -- it is the device working hard to be told
 * "no" it already knew.
 *
 * After a refusal this stops trialling for RETRY_BACKOFF_MS and answers false
 * from the cached verdict. The saving is 22 of every 23 trials; the cost is that
 * enrichment can resume up to a second later than it strictly could.
 *
 * THE DIRECTION OF THE ERROR IS THE POINT: a throttled gate can only ever say NO
 * more often than the true gate, never yes. It cannot let a handshake through
 * that CanHandshake() would have refused, so nothing downstream needs to change.
 *
 * NOT used by the [health] line or the OTA path. Those want the instantaneous
 * truth, and a reporting number smoothed by a backoff would be a worse number --
 * the whole reason this file exists is that a metric which stops tracking the
 * thing it names is how the previous gate got it wrong for months.
 */
bool CanHandshakeThrottled();

/** Backoff after a refusal, before the next real trial. */
constexpr uint32_t RETRY_BACKOFF_MS = 1000;

// ---------------------------------------------------------------------------
// FIX 4 -- the reserved handshake block ("ballast").
//
// UNPROVEN ON HARDWARE AT THE TIME OF WRITING. It is built, it is correct by
// inspection, and it has host tests -- and none of that is evidence about an
// allocator. It needs a bench soak against the COM119 numbers in
// docs/heap-fragmentation-2026-08-17.md before anyone calls it fixed.
//
// The problem it addresses: `free` stays healthy while the LARGEST CONTIGUOUS
// block erodes ~10 KB in 12 idle minutes, and a normal fetch needs ~7 KB of
// clearance on top of the floor. So the handshake block is not lost to exhaustion,
// it is lost to the pool being carved up while nobody is looking.
//
// The mechanism: claim one TLS_HANDSHAKE_BYTES block at boot, while the heap is
// pristine and contiguous, and hold it. Release it in the instant before a fresh
// TLS connect, so the block the handshake needs demonstrably exists at the one
// moment it is needed. Re-take it later, opportunistically, when there is slack.
//
// WHAT THIS TRADES: 16,717 B permanently unavailable to the tracked set. That is
// a real cost and it is why this is fix 4 and not fix 1 -- it makes an
// intermittent failure into a fixed, known one, which is better but is not free.
//
// WHY THE GATE HAS TO KNOW ABOUT IT, or this makes things worse: while the
// ballast is held, the pool is missing exactly the block a trial asks for -- so a
// naive CanAllocate() would start refusing handshakes BECAUSE we reserved memory
// for handshakes. CanHandshake() therefore answers true when the ballast is held,
// since releasing it is precisely what the caller is about to do.
// ---------------------------------------------------------------------------

/**
 * Claim the ballast if it is not already held. Safe to call repeatedly; a call
 * while it is held is a no-op. Call at boot and then on a slow cadence -- a
 * re-take that fails is the normal state while a TLS session is live, not an
 * error, and it will succeed on a later attempt when the session ends.
 */
void ReserveHandshakeBallast();

/**
 * Hand the reserved block back to the allocator because a handshake is imminent.
 * No-op when not held. Called from the request path on the network task.
 */
void ReleaseBallastForHandshake();

/** Times a re-take was attempted and refused. Reported, not acted on. */
uint32_t BallastReacquireFailures();

/**
 * Free 8-bit-capable internal bytes. NOT a gate -- reported alongside the trial
 * result so the plateau stays visible in the field. This is the number that
 * moved (3,361 distinct values where largest had 5), so it is the one worth
 * watching for a trend; it just cannot answer a contiguity question.
 */
uint32_t FreeInternal8Bit();

} // namespace heaphealth
