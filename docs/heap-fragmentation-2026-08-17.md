# Contiguous heap erodes on an idle board — s3-128, 40-aircraft sky

**Open.** Found 2026-08-17 on bench board `.55` while preparing the A2 rotation. Distinct
from — and initially confused with — the [fetch stall](fetch-stall-2026-08-17.md). Treated
as separate until something links them.

## ANNOTATION 2026-08-19 — why this could not be reproduced, and how it becomes reproducible

**The starved state did not reproduce on COM119**, which is why heap fixes 1 and 4 (PR #234,
merged) shipped **measured only by the compiler**. COM119 under the same nominal load —
`ac=40/40`, 41 airports — reported a completely healthy heap:

```
n=41 ap=41  heap free=92060 largest=44020 free8=92060 tlsOk=1 rej=0  tls=2/889
```

`largest=44020` against a 20,000 budget, and **zero** trial rejections where `.55` produced 23
a second.

**The reason is now known, and it is not that the defect went away.** COM119 had been factory
reset that morning. Every `needsLookup` info field shipped `defaultOn=false`, so a
factory-fresh board set `metadataNeeded=false`, `ProcessMetadataLookups()` returned at its
first gate, and **enrichment never ran at all** — `enrichReqs=0` on every `[perf]` line. A
board doing no enrichment cannot starve its heap on enrichment, and cannot exercise either
fix.

So the two boards were never comparable: `.55` was enriching and COM119 was not. The 23/s
figure is not in doubt — it is simply not a thing COM119 could have produced in that state.

**This is the first chance at a real before/after.** `info-type` and `info-operator` now ship
`defaultOn=true`, which makes `metadataNeeded` true out of the box — the state **every shipped
unit will be in**, and the honest bench control this defect has never had. The soak to run:

1. a board on the new defaults, untouched, 40-aircraft sky, `[health]` captured
2. `rej`/s under a reproduced starvation — `.55` said ~23/s; fix 1 should give ~1/s
3. `ball=1` from boot, dropping to 0 only across a live TLS session
4. `largest` holding its floor rather than eroding ~10 KB in 12 minutes

Until that runs, **fixes 1 and 4 are in main and unproven**, and this document stays open.

## The finding that makes this a defect rather than a curiosity

```
11:49:32  boot
11:55:03  [health] BUDGET BROKEN: largest block 18420 < 20000
```

**Six minutes from boot, on a board nobody touched.** `touchIdle` climbs 17 s → 737 s
monotonically across the run; there is not a single `[touch]` line. This is the shipping
image's behaviour in an ordinary configuration (41 airports / 100 km, `ac=40/40`), not a
stress test.

`LARGEST_BLOCK_BUDGET` is 20,000 ([AircraftManager.cpp:1542](../src/AircraftManager.cpp#L1542)).

## What it looks like

12 minutes idle, instrumented build (`-DFETCH_TRACE`), production backend:

| | |
|---|---|
| contiguous headroom lost | **10,240 B** (31,732 → 21,492) |
| `free` over the same window | 45,912 → ~38,000, **oscillating, no real trend** |
| TLS handshakes | **2** — both at boot |
| requests | **196**, every one `reuse=1` |
| `rej` | 0 → 2 |

`free` holding while `largest` falls is what makes this **fragmentation, not a leak**.

## Ruled out

**Per-handshake allocation (mbedTLS record buffers).** `tls=H/R` shows `H=2` across the
entire run — two handshakes at boot and never another, with every request reusing the
connection. There were no handshakes to allocate anything. Whatever churns memory here
does so on a **live** connection.

**Enrichment volume**, as the primary driver. `enrichReqs` fell from 10–12/min to 1–5/min
at ~11:53 and the decline continued at the same rate. This was the original hypothesis,
formed from a `.55`-vs-`.32` comparison, and this run weakens it: the two boards differ in
more than enrichment.

**Tapping.** The first observation (2026-08-17 ~10:47) followed a 40-card tapping burst and
was attributed to it. It reproduces with zero touch events.

## The signature: 1 KB quantisation, in both directions

Every change in `largest`, ordered:

```
-3072  -1024  -2048  -7168  +7168  +2048  -2048  -1024  -2048  -1024
```

**Ten changes, all exact multiples of 1024, ups as clean as downs.** That is a pool
growing and partially reclaiming in 1 KB units — not general heap churn, which would
produce arbitrary sizes.

Two of them are different events and should not be averaged together:

- **−7168 / +7168** (11:55:03 → 11:55:33) is a **matched pair**, taken with `inFlight=1`.
  The fetch's transient working set, ~7 KB, returned in full. Normal.
- **+2048** (11:57:33) is **unmatched** — a net return with no preceding equal take. This
  is the reclaim half of the pool and is the most informative single event in the run.

There is **no 1024-byte allocation in our tree's fetch path**; the only literal 1024 is
`ASYNC_RESPONCE_BUFF_SIZE` (the async web server), and nothing hit the config page during
this run. The candidate is therefore below our code: **lwIP pbufs**, which churn per packet
on a persistent connection, come from a pool, and grow/shrink — fitting where mbedTLS does
not. **Unconfirmed.**

## The mechanism, which is why the number matters

The budget did not break at steady state. It broke when the **7 KB fetch transient landed
on an already-declined floor**: steady state 25,588 → transient 18,420 → budget 20,000.

> **The fetch needs ~7 KB of contiguous headroom, and the floor descends toward it.**

So the erosion matters not because it exhausts memory — `free` is fine — but because it
eats the clearance a normal fetch requires. `rej` ticking at 11:58 and 12:01 is that
clearance going negative, and `CanHandshake()` correctly refusing.

Both `LOW HEAP` transient lows are **exactly 16,372 = 16,384 − 12** — a 16 KB block minus
its header, the same block surviving as largest on each dip.

## Customer impact — bounded, and that is load-bearing

Both `CanHandshake()` gate sites are **enrichment-only**
([:4077](../src/AircraftManager.cpp#L4077) detail card,
[:5361](../src/AircraftManager.cpp#L5361) background sweep). **Neither gates the position
fetch.** So at the floor:

- ✅ the radar keeps drawing — positions are unaffected
- ❌ cards degrade — type / operator / photo go missing
- ✅ no stale ladder from this cause

That bound is why the [fetch stall](fetch-stall-2026-08-17.md) outranks it: the stall stops
the radar, this does not. A config mitigation (smaller radius) exists if needed.

## Next test, and why it discriminates

**Reduce `.55`'s radius**, record the value before and after. Radius cuts the tracked
aircraft count but **not** the poll rate, so the two live hypotheses separate cleanly:

| if the decline… | the driver is |
|---|---|
| slows | **per-aircraft** — the 40-strong tracked set, its Strings, its map churn |
| is unchanged | **per-fetch/per-packet** — the parse/decode cycle or the pbuf pool, both fixed-rate |

`.32` stays pinned as the negative control on the shipping image (it held `largest=31,732`
flat for 12 minutes under a 25-airport / 48 km sky). The cross-over — pushing `.32` to
`.55`'s sky — comes **last**, because it consumes the control.

Weak signal already in hand, offered as a hint not a result: at 11:59 the poll rate dropped
3× (5 s → 15 s idle transition) and the decline did not visibly slow. Weak because the
series is not monotonic — it recovers to 27,636 at 11:57 before falling again.
