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

/** Shorthand for the only size any caller currently cares about. */
inline bool CanHandshake() { return CanAllocate(TLS_HANDSHAKE_BYTES); }

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
 * Free 8-bit-capable internal bytes. NOT a gate -- reported alongside the trial
 * result so the plateau stays visible in the field. This is the number that
 * moved (3,361 distinct values where largest had 5), so it is the one worth
 * watching for a trend; it just cannot answer a contiguity question.
 */
uint32_t FreeInternal8Bit();

} // namespace heaphealth
