# D4: the cold build against the 2.5 s p95 -- the reads' waiting, and the CPU and bytes behind them

**Status: a proposal, predictions first.** Docs only: no code and no deploy. It answers the ~17:28Z ruling.

## The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-05 ~17:28Z, the D4 part)

> - What the reading changes for D4: CPU alone was 2.28-4.11 s per build, so parallel reads by themselves can't reach
>   the 2.5 s p95 target. Each build also read 15.2 MB to make one package. The next proposal, predictions first,
>   covers both: the reads' waiting (about 80% of wall) and the CPU and bytes behind it. If neither gets there, bring the
>   alternatives for a decision: pre-building, a longer device timeout, or a "202, retry" answer.

From ~17:50Z: "Next, as you listed: the proposal on the reads' waiting and the CPU and bytes behind each build, against
the 2.5 s p95, with predictions first."

## 1. New facts: a read-only profile of the real build (2026-11-05, 17:57-18:00Z)

**The method.**
- KDEN's build (Weatherscope's query) ran in Node on this PC, through the real `build()`.
- Its 53 objects were fetched read-only from the sealed store (`wrangler r2 object get --remote`) into a scratch folder.
- Nothing was written to R2, and nothing was deployed.

**The control: the profile reproduces the deployed build exactly.**
- **53 reads and 15,215,034 bytes**, the builder's own `bm_build` count and bytes from #541's staging reading.
- The same **214,179 B** package that staging served.

**Where the 15.2 MB goes:**

| read | count | bytes | per read |
|---|---|---|---|
| `current` | 1 | 26 | - |
| `manifest.json` | 1 | **3,867,380** | - |
| `coverage.bin` | 1 | 121,428 | - |
| `index.json` | 1 | 887,203 | - |
| cells | 12 | **8,089,216** | 674 KB on average |
| terrain tiles (z9 PNG) | 36 | 2,201,579 | 61 KB on average |
| `terrain-sources.bin` | 1 | 48,202 | - |

**A second check of the same layout.** The first three reads, `current` + manifest + coverage, come to 3,988,834 B.
That is exactly R2's figure for EGLL's 3-read `no_coverage` (V4).

**Where the CPU goes, here (V8 on this PC):**

| | ms |
|---|---|
| a whole build from memory, warm (5 runs) | 623-679 |
| a whole build in a **fresh process** (cold JIT, 3 runs), excluding 18-21 ms of disk | **583-603** |
| `JSON.parse` of the manifest | 5-6 |
| SHA-256 of the manifest (the store-id check) | 3 |
| `JSON.parse` of the index | 3 |
| decoding the 12 cells | 10 |
| decoding the 36 terrain PNGs | 93-117 |
| everything else (frames, layers, budget, encode): the remainder | about 460-500 |

**The gap, not settled.** The deployed builder's CPU was **2.28-4.11 s** (GraphQL, the three K builds). That is
**4-7 times** this PC's 0.58-0.60 s for the same build, cold.
- **The four explanations:**
  - (a) workerd counts as CPU the copying of 15.2 MB of R2 bodies into memory;
  - (b) the hardware and its sharing;
  - (c) `DecompressionStream` (the PNGs' inflate) costs more in workerd;
  - (d) GraphQL's `cpuTime` counts something the local timing does not.
- **Excluded so far:** a cold JIT, by the fresh-process runs.
- **Not separated yet:** (a), (b), (c) and (d).
- **Why it matters:** if (a) dominates, cutting bytes cuts CPU. If (b) does, only fewer instructions or no build at all
  will reach the target.

## 2. What each change can reach, predicted on wall time

**The model** for a build's wall: its CPU plus its waiting, where waiting means its dependent read stages.
- **Today:** about 12-17 s of wall, with 53 reads one after another.
- **The figures used:**
  - deployed CPU **2.3-4.1 s**;
  - waiting **10.2-13.3 s** over 53 reads, about **190-250 ms per read** (wall − CPU ÷ 53).

| change | what it does | predicted cold-build wall | reaches the 2.5 s p95? |
|---|---|---|---|
| **C1 parallel reads** | `current`, then the three store files together, then the 12 cells together, then the 36 tiles and terrain-sources together (bounded, 6 at a time) | waiting falls to about 4 dependent stages plus the tile waves: **about 1.5-3 s**. Wall **about 4-7 s** | **no:** CPU alone is 2.3-4.1 s |
| **C2 C1 plus fewer bytes** | a small per-store file table in place of the 3.87 MB manifest, and finer cells (8.1 MB for one query is coarse) | if (a) holds, CPU falls with the bytes. If (b) holds, it does not. **Wall 3-6 s, and the range is only as good as §1's unsettled gap** | **not predictable to pass** until the gap is separated |
| **C3 builder memo** | the four store files kept in a builder isolate's memory, keyed by the store id (they are immutable) | about 4.9 MB and 4 reads less, **only on a warm builder isolate**. MM4 found fresh proxy isolates every time, and the builder's reuse is unmeasured | **no**, on a cold isolate |

**What none of these address.** C2 is two **ingest** changes, and a new store needs a re-seal and the D2-style checks.
**No combination of C1-C3 can be predicted to pass 2.5 s p95 while CPU alone is 2.3-4.1 s.**

## 3. The alternatives, for a decision (as ruled: "If neither gets there")

| alternative | how a device's first request is served | cost | risks |
|---|---|---|---|
| **A1 "202, retry" with stored packages** | The proxy answers the first request at once: `202` with `Retry-After`. The build runs in the background (`ctx.waitUntil`), and its package is **stored in R2 under the store id and the query's digest**: no coordinate in the key, so MP3 holds. A retry, from any data centre, reads the stored package: one ~214 KB read. Revalidations are 304s through O1 | R2 Class A for each new query per store (writes); Class B per retry | `waitUntil`'s time limit against a 12-17 s build must be checked. A build that outlives it stores nothing, and the retry builds again. The device client, which I own, must handle 202 |
| **A2 pre-building** | Packages are built ahead for a known set of queries | as A1, ahead of time | it needs the set of device locations ahead of time, which is location data the route keeps out of every key and log |
| **A3 a longer device timeout** | The device waits through the cold build | none on the server | The shared `Get()` client holds a mutex for the whole wait: 12-17 s today. On the Blipscope, other fetches stall behind it. That is a `Get()` change, blipscope-5c's, through you |

**Recommendation: A1.**
- No device has to wait through a build.
- It keeps every route rule: no coordinate in a key, and the handler logs nothing new.
- It needs no location set.
- Its stored packages also serve repeats in **other** data centres, the case O1 leaves.
- **C1 is worth doing inside A1:** it shortens the background build, so a retry finds the package sooner.

## 4. The predictions A1 would carry, frozen at plan level (binding only if ruled)

- **The host test:** a cold query answers **202** with `Retry-After`, with no package body. The build's package is then
  stored under exactly `bm/v1/pkg/<storeId>/<digest>`, with no coordinate in the key. A second request, on a fresh
  isolate, serves the stored bytes with **0 builder calls**.
  - **Sabotages:** the store write skipped (the second request builds again); a coordinate added to the key (the
    key-shape row fails).
- **On staging, graded end to end on wall time** (the ~17:28Z ruling):
  - **The set, registered before it is read:** 10 cold queries at 10 distinct public airports, plus a retry of each
    after its `Retry-After`.
  - **Predicted:**
    - every first answer is a 202 in **under 2.5 s at p95**, with no build on the request path;
    - every retry is a 200 with the stored package in **under 2.5 s at p95**, from one R2 read;
    - **0** retries that build again.
  - **Outside the set:** stop and report.
- **The device client:** the 5 s wait is met because no request waits on a build. The client must treat 202 as "retry
  after N s", and that goes into the client's plan (§1.6 of `docs/basemap-d4-plan.md`).

## 5. Questions for the ruling

- **Q-CB1:** A1, A2 or A3? Recommended: A1, with C1 inside it.
- **Q-CB2:** may the CPU gap (§1) be separated before any change, by a staging-only probe? For example, a builder variant
  that reads the 53 objects and builds nothing, graded on its GraphQL CPU against a full build's. Or is it accepted as
  unknown, since A1 does not depend on it?
- **Q-CB3:** C2's ingest changes (a small file table, finer cells) would go to the ingest's own plan, not D4. Is that
  where they belong?
