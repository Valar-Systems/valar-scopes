# Q-W4: the dense-area code, registered before any measurement (predictions first)

**A registration. No code exists, and nothing has been measured for it.** The code PR (Q-W4) will bring, in one store change: A+B's
ladder, the owner's B (P1, then D2), the sea mask, and the topology-preserving water simplifier ("tp") with its backstop. This file
registers what will be measured, on what, and what each result means, before any of it runs. The code comes only after #639's staging
rows have read.

## The rulings it carries out

- **Q-W1** (~19:08Z): A+B replaces Q26, once re-read at the client's radius. It was re-read in #620 and #622.
- **Q-W2:** the sea mask from Natural Earth as ordinary water rings, with islands joined by keyhole bridges. The design and its checks
  are `docs/basemap-sea-mask.md` (#616).
- **Q-W4:** the dense-area rung, A+B and the sea mask land in one PR.
- **Q-DP2, the owner (2026-11-11):** **B, P1 then D2.**
- **Q-DP3:** D2's steps from 162 km by × 0.8, the last at 42.5 km, then §6's 422 as now.
- **Q-DP4:** the release variant's PR first (#639, merged as `c2cf7cfcdd99`), then this. **The code waits until #639's staging rows
  have read.**
- **The owner, on the water** (relayed by the orchestrator, 19:39:40Z): "Water fix: TP, as you recommend." The backstop, its plant, and
  TP's costs named here were ruled with it.
- **This registration (the orchestrator, 22:03Z), verbatim:**

  > 1. Pin the commit: measure at 525cf7d1725da76f7647b8d64949d9dbaf0897ec, by sha, not "main". [...]
  > 2. Commit and push the predictions before any measurement runs. Include TP, the backstop and its plant (with the control that proves
  >    the plant lands), the sea mask, and B.
  > 3. Pre-register the dependency as its own outcome: "if #639's staging rows read off-prediction, the rows that rest on the release
  >    variant are VOID, not re-graded, and get re-registered". Name which rows those are.
  > 4. Machine rules: one whole-suite vitest run at a time (check that no other is live, and TIME_WAIT under 2,000), and read free disk
  >    before any large build (under 30 GB, no large build).
  > 5. Nothing deploys, and no wrangler deploy is typed, dry-run included.

## What the code PR changes

1. **The ladder** (the format's §6, which says Q26 today). Past the 32 KB cap, in order:

   | rung | step |
   |---|---|
   | 0 | ε, as requested |
   | 1 | ε × 2 for everything |
   | 2 | water at ε × 8 |
   | 3 | secondary roads dropped, shortest first |
   | 4 | water rings dropped, smallest area first |
   | 5 | rivers and roads at ε × 4 (water stays at ε × 8) |
   | 6, 7 | rungs 3 and 4 again, on rung 5's content |
   | **D2** | the whole ladder again on a package of radius r × 0.8, × 0.64, …: 129.6, 103.68, 82.94, 66.36, 53.08 and 42.47 km from 162 |
   | past D2 | **§6's 422,** and the client falls back to no package |

   - Every drop stays counted in `PROVENANCE`.
   - **The header's `radiusMm` is the radius built.** The device already places a package by it (F4, `include/MapLayer.h`).
   - The builder makes the ε × 4 and ε × 8 sets in its one pass over the cells, as Q-DR3 read it. That is the memory B costs.
2. **Water rings: tp, then the backstop,** for every water ring (TIGER's, and the sea's):
   - **tp:** Douglas–Peucker as today. Then, while the quantised ring crosses itself, each crossing edge gets back the original vertex
     farthest from its chord, until no edge crosses or none can be added. This is the cost read's tp (#645).
   - **The backstop:** a ring that still crosses itself after tp is dropped, counted in `PROVENANCE`, and never drawn. "Crosses" means a
     proper crossing in `ring_stats()`'s sense, so a keyhole bridge's two coincident edges (a contact) are not refused.
   - **tp's costs, as the cost read found them** (#645, under B, without the sea):
     - fewer secondary roads at 7 of the 9 airports outside New York: KBOS 28 → 8, KMSP 116 → 18, KSEA 700 → 342, KIND 513 → 335,
       KORD 440 → 276, KSFO 826 → 719, KDTW 428 → 386;
     - fewer water rings in New York, at about the same water share;
     - +0.13 to +0.72 MiB per build;
     - 2 rings at KMSP that tp could not fix. **These are the backstop's first catch.**
3. **The sea mask** (#616's design):
   - Natural Earth 10 m ocean, plus its lakes above a size threshold;
   - clipped to the square, with islands under 1.5 km² dropped;
   - simplified (tp), then quantised, then bridged. Each hole joins through its nearest visible vertex, and each bridge adds 2 vertices;
   - **the rings join the water section** and follow the ladder like other water.
4. **The store gains a sea layer,** read per cell. That gives a new store id, and every package rebuilds. **The store's Class A figures
   are stated before any ingest uploads anything:** how many objects the new store writes, counted from the ingest's own listing.

## Where the predictions come from: a measurement, on the pinned commit

**The builder measured is a scratch prototype of `525cf7d1`'s builder** (the shipped one, with #639's release). The code PR's changes
are added as knobs. **With every knob off, it must be `525cf7d1`'s builder, byte for byte in what it builds.**
- **The sea, in the prototype:** computed at build time from Natural Earth 10 m **land** and **lakes**, which are on this machine. The
  sea is the build square less the land, bridged as #616 says.
  - The code PR stores the sea per cell, from the ocean file. So the prototype's sea rings can differ from the code PR's at the vertex
    level.
  - **Rows that depend on the sea are graded by property, never by package hash.**
- **D2, in the prototype:** the builder itself retries at each smaller radius within one call, as the code PR will. That puts every
  step's build in one process, which Q-DR3 never measured.
- **The instrument** is Q-DR3's: each call in its own Node process, the controls C1, C2′ and C3, and settled samples at the build's
  hooks. Crossings are counted with `ring_stats()`'s definition.
- **Two steps:**
  1. this registration, then **the measurement run** at all 13 points;
  2. **its figures, re-registered as the code PR's frozen predictions,** committed before the code PR's own run:
     - per point: rung, radius, bytes, water share, secondary roads, crossings, rings the backstop refused, islands, and sea bytes;
     - memory ± 0.785 MiB;
     - the code PR's package hashes are not predicted from the prototype's.

## The rows, predicted now

| row | what | predicted |
|---|---|---|
| **QW4-0** (the control that can fail) | the prototype with every knob off, at all 13 points | **byte-identical to `525cf7d1`'s builder:** Nov 8's DR0 packages at the 9, #620's ladders at New York. **Memory within 0.785 of #639's measurement** at each point, in the ladder phase |
| **QW4-1** | B with tp and the backstop, no sea | **0 proper crossings in every package.** The 9 fit at 162 km; **New York's first fit is 129.6 km** at all 4. Every package `kOk`, at most 32,768 non-terrain bytes. The backstop's refusals are reported per point |
| **QW4-2** | B with tp, the backstop and the sea | **0 proper crossings,** and every package `kOk`. **New York's first fit is 129.6 km at all 4**, as DR6 read B with the copy's sea approximation. **KB2: no bridge crosses any other edge.** **KB3: sea bytes at most 3 KB** at KBOS, KJFK, KLGA, KSEA, KSFO and KMIA |
| **QW4-3** (the backstop's plant) | a fixture store whose one water ring is a 4-vertex bowtie, a ring that crosses itself and that tp cannot fix (it has no vertex to add) | **the control:** with the backstop off, the package carries the bowtie with 1 proper crossing, so the plant is shown to land. **With it on:** the ring is dropped and counted once in `PROVENANCE`, and the package carries 0 crossings. **Re-frozen in the 22:38Z amendment at the end: counted on `bm_build`, not in `PROVENANCE`** |
| **QW4-4** | the sea mask's own checks, KB1, KB4 and KB5, with the plants S-KB-NOBRIDGE, S-KB-CROSS and S-KB-ORDER | **as #616 registered them** |
| **QW4-D2** | New York under B, all of D2's steps in one call and one process | the call's largest sample is **at most its largest single step's, + 0.785 MiB**: no step carries memory into the next |
| **QW4-M** (memory) | every build | **no build over 96 MB** (a stop). Each B build is reported against the 77.2 MB the owner took with B, with New York's 162 km tries and KORD and KIND named. Those two points were estimated about 6 to 7 MiB above the copy's B figures, and are now measured |

**What each result means:**

| outcome | meaning | next |
|---|---|---|
| **QW4-0 off** | the prototype is not the pinned builder | **stop:** nothing below it is read |
| **QW4-1 or QW4-2 leaves a crossing** | the backstop doesn't hold | **stop and report** |
| QW4-1 or QW4-2: a New York point below 129.6 km, or a 422 anywhere | **a visible cost of tp or the sea mask** | named to the owner before the code |
| **QW4-3's control shows no crossing** | the plant didn't land, so its pass would mean nothing | **stop** |
| QW4-3 with the backstop on still carries the bowtie | the backstop doesn't hold | **stop** |
| a KB row or a sea plant off | as #616 registered | **stop and report** |
| QW4-D2 off | D2's steps carry memory, so a New York request costs more than its largest build | **report;** it goes into D4's plan |
| **QW4-M: a build over 96 MB** | the registered stop band | **stop** |
| **anything else** | | **stop and report** |

## The dependency on #639's staging rows (pre-registered as its own outcome)

**If #639's staging rows read off-prediction** (any of SG-B to SG-F at KLGA or KJFK), **the rows that rest on the release variant are
VOID, not re-graded, and get re-registered.**
- **Those rows are the memory rows:** QW4-0's memory half, QW4-D2, QW4-M, and every memory figure the re-registration freezes.
- They rest on the release variant because every one of them is a figure of the shipped builder's memory, and that is what #639
  changed.
- **The package rows don't rest on it,** because the release changes no byte. RL1 read that at all 13 points. They are QW4-0's byte half,
  QW4-1, QW4-2, QW4-3 and QW4-4.
- **SG-B in particular,** Exceeded Memory in workerd, means the release doesn't hold where it ships. D4 waits on it, whatever this
  file reads.

## Machine rules, and what does not happen

- **One whole-suite vitest run at a time.** Before each one, no other vitest is live and TIME_WAIT is under 2,000, both read and stated.
- **Free disk is read before any large build.** Under 30 GB, no large build runs.
- **Nothing deploys.** No `wrangler deploy` is typed, `--dry-run` included. The new store is built locally. Its ingest to staging, and
  its Class A figures, wait for the code PR's own ruling.

## Order

1. **This registration,** pushed before any measurement.
2. **The measurement run** on the prototype at `525cf7d1`, read into this file.
3. **The figures, re-registered as the code PR's frozen predictions,** committed.
4. **#639's staging rows read.** They wait on #646 and the orchestrator's slot. If they read off, the dependency above applies.
5. **The code PR,** graded against step 3's predictions, the plants and the KB rows. Then the store's Class A figures, before any ingest
   uploads anything.

## QW4-0, read (2026-11-11, 22:10:35Z): stopped at KJFK's memory. The prototype is not the pinned builder in memory

The registration was frozen at `882005aa9da0`. The go was the orchestrator's at 22:07Z.

**The prototype:**
- `pq4`, made by `mk-pq4.mjs` (sha256 `e428959d0bad`) from the pinned `525cf7d1725d`'s files, read from git;
- the instrument's seven hooks and the knobs added, each edit on an anchor that had to match exactly once;
- `format`, `provenance`, `query`, `store`, `terrain`, `handler` and `worker` byte-identical to the pinned commit;
- `build.ts` +26 −1, `budget.ts` +37, `frame.ts` +60, `layers.ts` +2 −2;
- the child is `dr-q4c.mts` (`5fd0b771d73d`) and the driver `dr-q4-0.mts` (`c591f0e3ae8f`);
- the child's imports were checked first: given an unknown point with the knobs off and with them on, it refused the point (exit 2).

**The read, one process each, with every knob off and reported off by the modules:**

| point | answer | memory | |
|---|---|---|---|
| KBOS | `ce5b1ea7589e`, Nov 8's DR0 | 56.02 MiB, the ladder phase, against #639's 56.70 ± 0.785 | as predicted |
| **KJFK** | 422 with #620's ladder | **61.61 MiB, the cell phase,** against #639's **58.54, the ladder phase** | **memory off: the run stopped** |

**By the registered table, QW4-0 off means the prototype is not the pinned builder, so nothing below it is read.** QW4-1 to QW4-M
did not run. The log is scratch (sha256 `bd2ba530d3ad`), and nothing ran on staging.

**What it looks like, not established:**
- **The answers are the pinned builder's** at both points read, so the byte half holds.
- **KJFK's 61.61 in the cell phase is the scratch copy's figure** (61.58 in Q-DR3, 61.19 in Q-RV5 (1), 61.32 in Q-RV6 (a)). The
  shipped builder reads 55.27 there and peaks in the ladder phase.
- So **the prototype's code, with every knob off, shifts KJFK's cell loop the way the copy's tree did.** Q-RV6 found that effect in the
  copy's tree, and Q-RV8 placed it at the end of the cell loop at KORD and KIND, but no read has named its object. The prototype's
  executed differences from the pinned builder, with every knob off, are few:
  - the knob constant;
  - two sets declared as null;
  - two conditional statements inside the cell loop;
  - one conditional after `at`;
  - the build wrapped as `buildOnce` inside a D2 caller.
- **The miss this exposes:** I registered QW4-0's memory half as if knob-off code could not move memory. Q-RV6 had already shown that the
  builder's tree, not its output, moves the cell loop at KJFK. That alternative was available, and I didn't name it.

**Proposed (Q-W4a), read-only and registered first:**
- **A bisection at KJFK:** the pinned builder with one prototype edit at a time (the cell-loop conditionals, the `at` line, the D2
  wrapper, and the knob constant with the null sets), one process each, against the pinned builder's own 58.54 in the ladder phase.
- **The edit that moves KJFK's cell loop to about 61.6 names the effect,** after four reads across Q-RV5 to Q-RV8 that left it unnamed.
- **Then decide with the orchestrator:**
  - either the prototype is written so the effect doesn't occur, and QW4-0 runs again from the start;
  - or memory predictions for Q-W4 are stated with that effect as a named bound, since the code PR's own builder may carry it too.

## The ruling (from the orchestrator, 22:13Z), verbatim

> Q-W4a is NOT approved. This would be the third investigation of the same unexplained effect (Q-RV6, Q-RV8, now this), and the retry
> cap sends a third ask to the owner. The code PR doesn't need the object named, so I'm ruling a design that doesn't ask the question
> again:
> RE-REGISTER, as a new frozen prediction committed and pushed before any re-run:
> 1. QW4-0's BYTE half is unchanged: knob-off = 525cf7d1's builder byte for byte at all 13 points.
> 2. QW4-0's MEMORY half is no longer graded against #639. The knob-off prototype's peak at each point, in each phase, is RECORDED as the
>    baseline.
> 3. Memory rows (QW4-1 to QW4-M) are graded as DIFFERENCES: knob-on minus knob-off, at the same point and phase, in the same prototype,
>    ±0.785 MiB. The 96 MB absolute ceiling stays as written.
> 4. A control the difference instrument can fail: a planted allocation of a known size (state it; 8 MiB is fine), held to the end of the
>    cell loop behind its own knob, at KJFK. Predict a difference of exactly that size ±0.785, with the plant proven to land. If it reads
>    anything else, the instrument is blind, and nothing memory-wise is read.
> 5. Name the effect as an open bound, not a prediction: "knob-off edits in the cell loop moved KJFK's cell-phase peak to 61.61 MiB,
>    against the pinned builder's 58.54 in the ladder phase; object unnamed". The code PR measures the real builder anyway, and that's
>    where it gets read.
> 6. Keep the "anything else -> stop" row.
> Then QW4-0 re-runs from the start, once. If it goes off again, it comes to me and on to the owner. Send me the re-registration's sha,
> and nothing runs until I accept it.

## The measurement, re-registered (frozen before any re-run)

**The open bound, not a prediction:** knob-off edits in the cell loop moved KJFK's cell-phase peak to 61.61 MiB, against the pinned
builder's 58.54 in the ladder phase. The object is unnamed. The code PR measures the real builder, and that is where it gets read.

**The prototype is now `pq4b`.** `mk-pq4b.mjs` (sha256 `4161a87b369d`) is `mk-pq4.mjs` with the plant added:
- the knob `Q4_PLANT_MIB`;
- an allocation of that many MiB, filled with 7, made just before the cell loop and let go just after it;
- the builder reports what it made (`q4Plant`).
- Its `build.ts` (`88f2316b2aaf`) differs from `pq4`'s in those four lines and the knob constant, and every other file is identical.
- The child is `dr-q4c2.mts` (`01157460d5e0`): `dr-q4c.mts` on `pq4b`, also reporting the plant. Its imports were checked first: given
  an unknown point with no knob, with the plant, and with every B knob, it refused the point each time (exit 2).
- The driver is `dr-q4-run.mts` (`9d0343744293`). Each call runs in its own process, and the knobs are checked as the modules read them.

| row | what | predicted |
|---|---|---|
| **QW4-0** | every knob off, at all 13 | **the byte half, unchanged:** Nov 8's DR0 packages at the 9, #620's ladders at New York. **The memory is RECORDED,** per point and phase, as the baseline, and not graded |
| **QW4-P** (the difference instrument's control) | KJFK with an **8 MiB** plant (`Q4_PLANT_MIB=8`), against QW4-0's KJFK | **+8 ± 0.785 MiB in the cell phase, and 0 ± 0.785 in tile, tiles, terrain, layers and ladder.** The plant is shown to land: the builder made 8,388,608 bytes and read them back as 7 + 7. The same answer as QW4-0's KJFK |
| **QW4-1** | B with tp and the backstop, no sea, at all 13 | **0 proper crossings** in every package; `kOk` and at most 32,768 non-terrain bytes; the 9 at 162 km and New York at 129.6 km. **Memory is RECORDED as the difference from QW4-0,** per point and phase. These differences become the code PR's frozen predictions, ± 0.785. The backstop's refusals are reported |
| **QW4-D2** | each New York point's D2 call, step by step, against the same radius built alone in the same prototype | **each in-call step's largest within 0.785 MiB of its standalone build's** |
| **QW4-M** | every build | **none over 96 MB.** Each B build's largest is reported against 77.2 MB |

| outcome | meaning | next |
|---|---|---|
| **QW4-0's bytes off** | the prototype is not the pinned builder | **stop.** It comes to the orchestrator and on to the owner |
| **QW4-P off**, or the plant not landing | **the difference instrument is blind** | **stop; no memory row is read** |
| QW4-1: a crossing, a package not `kOk`, over 32,768 bytes, or no fit at all | the backstop or the ladder doesn't hold | **stop and report** |
| QW4-1: a point at another radius | **a visible cost,** named to the owner before the code | reported; the run goes on |
| QW4-D2 off | a step carries memory into the next | reported, into D4's plan |
| **a build over 96 MB** | the registered stop band | **stop** |
| **anything else** | | **stop and report** |

**Not in this run:** QW4-2 (the sea), QW4-3 (the backstop's bowtie plant, which needs a fixture store) and QW4-4 (#616's KB rows and sea
plants). Their tooling is not built yet. They run as registered above, after an addendum here that pins their artifacts' sha256s and is
pushed before they run. **The dependency on #639's staging rows is unchanged:** the void set is the memory rows, now including QW4-P and
QW4-1's memory differences.

**This run is QW4-0's one re-run.** If it goes off again, it comes to the orchestrator and on to the owner.

### Amendment (the orchestrator's 22:17Z gap): every sample forces a GC first, so QW4-P's later-phase half can be read

**Option (a), and it is what the instrument already does.**
- **Each child runs with `--expose-gc`** (`dr-q4-run.mts` passes it to every call).
- **Every sample calls `gc()` before it reads:** `raw()` in `dr-q4c2.mts` is `gc()` followed by `process.memoryUsage()`.
- **The measure is `heapUsed + arrayBuffers`.** It is not RSS, and not `external` beyond the array buffers. The plant is a `Uint8Array`,
  so its 8 MiB backing store is counted in `arrayBuffers`.
- **A sample is settled:** `raw()` is repeated, at most four calls, until two readings agree within 0.1 MiB.
- **The plant is let go before the tile loop begins:** `PLANT = null` right after the cell loop, in `pq4b`'s `build.ts`. The tile loop's
  first sample (`MEM?.("tile")`) then forces its own `gc()`, so a dropped plant is collected before any later phase is read.
- **So QW4-P's "0 ± 0.785 in tile, tiles, terrain, layers and ladder" stands as registered.** A reading of +8 there would mean the plant
  was still reachable, not that the GC hadn't run yet. That still stops the run as blind, by the table.

## The re-run, read (2026-11-11, 22:19:24-22:21:20Z): every stop row held. QW4-D2 is reported off

The re-registration was frozen at `a04463364094` and amended at `c7282536bc02`. The go was the orchestrator's at 22:19Z. Every artifact's
sha256 read as registered first, and C: had 76 GB free. The driver exited 0, the error file is empty, and nothing ran on staging. The run
read the pinned `525cf7d1`, not main's head. The log is scratch (sha256 `4e8165f37e6d`).

**QW4-0, the byte half: as predicted at all 13** (Nov 8's DR0 packages at the 9, #620's ladders at New York). **The memory is
recorded as the baseline:**

| | KBOS | KJFK | KLGA | KEWR | Midtown | KMIA | KSFO | KSEA | KORD | KDTW | KMSP | KIND | KJAC |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| largest, MiB | 56.02 | **61.61, cell** | 60.01 | 60.81 | 58.09 | 39.39 | 39.21 | 49.18 | **39.87** | 47.13 | 47.26 | **43.98** | 38.88 |

**The open bound reaches further than it was written.** With every knob off, the prototype reads like Q-DR3's scratch copy at KORD (39.87,
against the shipped builder's 45.79) and KIND (43.98, against 50.85), as well as at KJFK. That is recorded, not investigated (ruled).

**QW4-P, the difference instrument's control: as predicted.** The builder made 8,388,608 bytes and read them back as 14. Against
QW4-0's KJFK, the difference was **+8.00 MiB in the cell phase** and **0.00 in tile, tiles, terrain, layers and ladder**, with the same
answer. **The instrument can see a difference, so the memory rows below are read.**

**QW4-1, B with tp and the backstop, no sea: as predicted at all 13.**

| point | the fit | crossings | secondary | B minus knobs-off at the ladder phase, MiB | largest, MB |
|---|---|---|---|---|---|
| KBOS | 162 km, rung 3 | 0 | 8 | +12.75 | 72.1 |
| KJFK | **129.6 km**, rung 7 | 0 | 0 | +13.24 | 75.3 |
| KLGA | **129.6 km**, rung 7 | 0 | 0 | +13.98 | **77.6** |
| KEWR | **129.6 km**, rung 7 | 0 | 0 | +13.23 | **77.6** |
| Midtown | **129.6 km**, rung 7 | 0 | 0 | +13.00 | 74.6 |
| KMIA | 162 km, rung 2 | 0 | 1,571 | +1.62 | 43.0 |
| KSFO | 162 km, rung 3 | 0 | 719 | +2.16 | 43.4 |
| KSEA | 162 km, rung 3 | 0 | 342 | +3.94 | 55.7 |
| KORD | 162 km, rung 3 | 0 | 276 | +6.81 | 48.9 |
| KDTW | 162 km, rung 3 | 0 | 386 | +5.64 | 55.3 |
| KMSP | 162 km, rung 3 | 0 | 18 | +5.09 | 54.9 |
| KIND | 162 km, rung 3 | 0 | 335 | +7.62 | 54.1 |
| KJAC | 162 km, rung 1 | 0 | 578 | +1.78 | 42.6 |

- **0 proper crossings in all 13 packages, and every package `kOk` and at most 32,768 non-terrain bytes.** The 9 fit at 162 km and New York
  at 129.6 km, so **no visible cost.** Every phase's difference is recorded in the log.
- **The backstop's first catch, as named:** at KMSP it refused exactly 2 rings, counted across the sets it builds.
  - KMSP's package still draws 176 water rings with 0 crossings, as the cost read's did, so the ε × 8 set it draws lost nothing.
  - Yet the package changed (`0b4b403134bb`, against the cost read's `4c143b450ad4`). At rung 3 its `PROVENANCE` takes
    `collapsed.water` from the ε × 2 set, so **at least one refusal was in the ε × 2 set**. Where the other was is not read.
  - **A question for the code PR:** under rungs 2 to 4, `collapsed.water` describes a set the package doesn't draw.
- **The other 12 packages equal the cost read's tp packages** (KBOS `fee6825e1fd0`, KJFK `04797ddc40a9`, and so on). This prototype is
  built on the shipped builder, and the cost read used the copy, so the two agree on what is built.

**QW4-M: no build over 96 MB.** **KLGA's and KEWR's B builds read 77.6 MB, 0.4 MB above the 77.2 the owner took with B.** Every other
build is under it.

**QW4-D2: reported off, as the table says (into D4's plan).**

| point | step 0 (162 km), in the call against alone | step 1 (129.6 km), in the call against alone |
|---|---|---|
| KJFK | 71.78 / 71.75 (+0.02) | **63.15 / 57.57 (+5.59)** |
| KLGA | 73.99 / 73.96 (+0.04) | **64.55 / 58.99 (+5.56)** |
| KEWR | 74.04 / 74.06 (−0.02) | **69.54 / 63.39 (+6.16)** |
| Midtown | 71.10 / 71.10 (0.00) | **64.73 / 59.17 (+5.56)** |

- **The step after a 422 carries about 5.6 to 6.2 MiB into the next build.** The call's largest is still its first step (the 162 km
  try), so New York's largest is not raised by it.
- **For D4's plan:** a D2 call with a third step, or a first step smaller than its second, would carry it into a peak. The object isn't
  named, and the ruling says not to investigate it here.

**For the frozen predictions (the next step):** the code PR's memory will be graded as its builder against main's, at the same point and
phase. The open bound means a tree difference alone can move KJFK, KORD and KIND by 6 to 7 MiB. **So the frozen differences at those
three points need the bound beside them, not only ± 0.785.** That goes into the re-registration of the figures, for a ruling.

## The code PR's frozen predictions (on the orchestrator's 22:25Z and 22:28Z rulings)

**Amended at 22:38Z, before anything was built:** the amendment at the end of this file re-freezes T-PROV-a, R-KMSP, the
PROVENANCE bullet in the package rows, KBOS's row and QW4-3's count, and adds T-SEA0 and the readers of `bm_build`'s new fields.
Where it and this section differ, the amendment holds.

**Frozen before any of the code exists.** It covers the code PR's memory, its packages, PROVENANCE's water counts and D2's steps.
**Points A to D at the end are mine, not ruled,** and are marked for the orchestrator's reading before anything is built.

### The rulings, verbatim

22:25Z, after accepting the re-run as read at `9196b0c7be32`:

> 1. THE OPEN BOUND: no widened band. A ±7 tolerance at exactly the points that move would blind the check where it matters. Instead:
>    (a) From data already measured, no new run: compare QW4-0's recorded knob-off baseline with #639's shipped figure at all 13
>    points. Name every point where the gap exceeds 0.785; you've seen KJFK, KORD and KIND, and the table decides the rest.
>    (b) At those points only, the code PR's memory (its builder minus main's, same point, peak to peak) gets TWO discrete named
>    outcomes, each ±0.785. O1 = QW4-1's difference ("the tree effect is absent in the real builder"). O2 = QW4-1's difference plus that
>    point's measured tree offset ("present"). Anything between or outside them stops.
>    (c) Every other point is QW4-1's difference ±0.785, one value.
> 2. PROVENANCE: collapsed.water must count the rings the package DRAWS. Backstop refusals are counted separately, never inside it.
>    The code PR carries a host test asserting provenance count == drawn rings at KMSP, with a control that a refusal is present. A count
>    that describes a set the package doesn't draw is the truncated-row shape, and it doesn't ship.
> 3. D2: into D4's plan as registered. The code PR also asserts the step order D2 relies on: the first step is the largest, and there are
>    at most 2 steps. A third step, or a smaller first, then fails a test instead of silently raising New York's peak.
> 4. QW4-M: I'm telling the owner that KLGA and KEWR read 77.6 MB, 0.4 above his 77.2, on the prototype with the open bound. That's a
>    report, not a stop.

22:28Z, on my report that 1(b) can't discriminate at KJFK:

> Register it WITH your fix. [...]
> 1. State the assumption the two outcomes rest on: the tree offset is additive with B's knobs, i.e. the same with the knobs on as off.
>    If it isn't, a reading lands between or outside O1 and O2, which stops as registered. Name that as its own outcome: "neither O1 nor
>    O2 -> the offset interacts with B, stop".
> 2. At KBOS, KLGA, KEWR and Midtown, record the cell-phase differences beside the graded peak, but don't grade them. They are the same
>    effect's evidence at points where it never reaches the peak.

### 1(a), from data already measured

Knob-off (QW4-0, `q4-run.log`, sha256 `4e8165f37e6d`) minus #639's measured builder (`rv-meas.log`, `fc43b98c419e`), peak to peak, MiB.
**Three points are over 0.785:**
- **KJFK +3.07:** 61.61 in the cell phase, against 58.54 in the ladder phase. The gap lives in the **cell phase alone** (+6.35), and every
  later phase is within 0.01.
- **KORD −5.92:** 39.87 against 45.79, both in the ladder phase. The gap runs from the tile loop to the end; the cell phase is 0.00.
- **KIND −6.87:** 43.98 against 50.85, both in the ladder phase. The gap runs from the tile loop on; the cell phase is +0.73.

The other ten are within it: KBOS −0.68, Midtown −0.02, KMSP −0.01, and 0.00 at the other seven. Four of them differ in the cell phase
only, and never at the peak: KBOS +3.65, KLGA +2.13, KEWR +6.07 and Midtown +7.43.

**Why KJFK is graded on its cell phase (my 22:27Z report, accepted at 22:28Z).** As first ruled, KJFK's O1 was +10.17 and O2 +13.24, peak
to peak. B's peak there is in the ladder phase, where the offset is 0, so the code PR reads +13.24 in both worlds: O1 is unreachable, and
O2 doesn't mean "present". The two outcomes are graded on the cell phase instead, where the offset lives. The peak is one value.

### What is measured, and on which store

- **The code PR's builder against main's.** Each is taken from its git commit and hooked by a generator like `mk-hooks.mjs`: the seven
  sampling lines in both, and D2's step line in the code PR's alone, since main has no D2 loop. The generator refuses anything else.
- **The instrument is QW4's:** one process per call with `--expose-gc`, the controls C1, C2′ and C3, settled samples, and the 13 points at
  the client's query. Each call's peak and every phase are recorded.
- **On the store the prototype read: the current one, with no sea layer.**
  - QW4-1's figures are B with tp and the backstop and **no sea**, so they predict the code PR only where it builds no sea.
  - **So the code PR's builder, given a store with no sea layer, builds no sea.** That is registered here as a property of the code. It
    is also what lets the code deploy before the new store is ingested.
  - The sea's own memory is not predicted here (point A).
- **The dependency on #639's staging rows stands.** If they read off, every memory row below is VOID and re-registered.

### The memory rows

**The assumption the two outcomes rest on: the tree offset is additive with B's knobs,** the same with the knobs on as off. If it isn't,
a reading lands between or outside O1 and O2, and that is its own outcome, below.

**The three points with two outcomes.** The code PR minus main, ± 0.785 MiB each:

| point | graded | O1: the effect absent in the real builder | O2: present |
|---|---|---|---|
| **KJFK** | **the cell phase** | **+9.94** (71.55 − 61.61) | **+16.29** (71.55 − 55.26) |
| **KORD** | the peak | **+6.81** | **+0.89** |
| **KIND** | the peak | **+7.62** | **+0.75** |

- O1 and O2 are 6.35, 5.92 and 6.87 apart, more than twice 0.785, so no reading can match both.
- **KJFK's peak is one value, +13.24** (71.78 − 58.54), in both worlds. Its phase tag isn't graded. In O2's world the cell phase (71.55 in
  the prototype) sits 0.23 below the ladder phase, so the tag may read either.

**The other ten points: one value each,** the code PR's peak minus main's, ± 0.785 MiB. These are QW4-1's differences peak to peak, from
the log. The re-run's table above gave them at the ladder phase, 0.01 apart at Midtown (+13.00) and KDTW (+5.64).

| point | graded | recorded beside it, not graded: the cell phase, if the effect is absent / present |
|---|---|---|
| KBOS | **+12.75** | +9.15 / +12.80 (and point D) |
| KLGA | **+13.98** | +8.38 / +10.51 |
| KEWR | **+13.23** | +10.13 / +16.20 |
| Midtown | **+13.01** | +9.88 / +17.31 |
| KMIA | **+1.62** | |
| KSFO | **+2.16** | |
| KSEA | **+3.94** | |
| KDTW | **+5.63** | |
| KMSP | **+5.09** | |
| KJAC | **+1.78** | |

| row | what | predicted |
|---|---|---|
| **CM-0** (main's anchor) | main's hooked builder at all 13 | **its peak and its cell phase within 0.785 of #639's measurement** at every point. Every O value above was computed from those |
| **CM-P** (the instrument's control) | KJFK on the code PR's builder. The child holds an **8 MiB** plant from before the build until the first sample past the cell loop, and drops it before that sample is read. Compared against the same build without it | **+8 ± 0.785 in the cell phase, and 0 ± 0.785 in every later phase.** The plant is shown made and read back. The code PR's builder carries no plant knob, which is why the child holds it |
| **CM-M** | every build | **none over 96 MB** (a stop). Each code PR build's largest is reported against the 77.2 MB the owner took with B |

### The package rows (the same store)

- **Every section but PROVENANCE is byte-identical to QW4-1's package** at the same point. The references are QW4-1's packages, rebuilt
  by `pq4b` at the same knobs and checked against these shas first:
  - KBOS `fee6825e1fd0`, KJFK `04797ddc40a9`, KLGA `d858d75eda6f`, KEWR `7c9d29b0e136`, Midtown `9a9f002197a4`;
  - KMIA `a39dcd96532d`, KSFO `fc6c3e960acc`, KSEA `e51bac59de43`, KORD `fe18e4224137`, KDTW `dff7ad0acedb`;
  - KMSP `0b4b403134bb`, KIND `a6943f185ad3`, KJAC `a6a475745804`.
- **So the same 0 proper crossings, `kOk`, at most 32,768 non-terrain bytes,** the same rungs, the 9 at 162 km and New York at 129.6 km.
- **PROVENANCE differs from QW4-1's in at most two places:**
  - `collapsed.water`'s value, which now counts the drawn set (ruling 2). It may move only at rungs 2 to 7, where the water drawn isn't
    the ε × 2 set;
  - the new `refused.water` line (point C).

### Ruling 2: PROVENANCE's water counts

| row | where | predicted |
|---|---|---|
| **T-PROV-a** | CI, on QW4-3's bowtie store | with the backstop on: **`refused.water: 1`, and `collapsed.water` equal to the same store's with the bowtie removed.** The control: with the backstop off, the bowtie is drawn with 1 proper crossing |
| **T-PROV-b** | CI, on a synthetic store forced past rung 1 in KMSP's shape: a refusal in the ε × 2 set and none in the drawn ε × 8 set | **`collapsed.water` equals the drawn set's count.** The control: before that, the test asserts that the ε × 2 set's count, and the drawn set's count with the refusal inside, both differ from it, so either wrong rule fails |
| **R-KMSP** | the code PR's run, on real data | **KMSP's `collapsed.water` and `refused.water` equal those of KMSP built at ε × 8 with no cap** (rung 0, where no rung swaps a set). The control: the build's own refusals, on its result, number at least 1 |

**The build-wide refusals** (every set built, drawn or not) go on the `bm_build` log line as `refused`. That way the refusals at KMSP,
which sit in sets the package doesn't draw, still reach a line someone reads.

### Ruling 3: D2's steps

| row | where | predicted |
|---|---|---|
| **T-D2** | CI, on synthetic stores | **the result, and `bm_build` as `steps`, lists D2's radii in order, strictly decreasing from the request.** A store where 162 km doesn't fit and 129.6 km does gives [162, 129.6]. The control: a store where 129.6 km doesn't fit either gives 3 steps, and the test shows the at-most-2 check failing on it |
| **R-D2** | the code PR's run, at New York's 4 | **at most 2 steps, and the call's largest sample in its first step.** A third step, or a first step smaller than its second, stops: QW4-D2 read the step after a 422 carrying 5.6 to 6.2 MiB |

### Ruling 4

The orchestrator is telling the owner that KLGA and KEWR read 77.6 MB, 0.4 above his 77.2, on the prototype with the open bound. **A
report, not a stop.** CM-M reports the code PR's own figure beside it.

### What each result means

| outcome | meaning | next |
|---|---|---|
| **CM-0 off** | main has moved under the figures | **stop** |
| **CM-P off**, or the plant not shown made | the instrument is blind | **stop; no memory row is read** |
| KJFK's cell phase, KORD's peak or KIND's peak reads its **O1** | the tree effect is absent in the real builder | as predicted |
| ... reads its **O2** | the tree effect is present in the real builder | as predicted |
| the three read a **mix** of O1 and O2 | the effect is not one thing in the real builder | as predicted at each point; reported as a mix |
| **neither O1 nor O2** at any of the three | **the offset interacts with B** | **stop** |
| a one-value point outside ± 0.785 | | **stop** |
| a section other than PROVENANCE differs from QW4-1's, or PROVENANCE differs outside its two places | the code is not the prototype | **stop** |
| a T-PROV or T-D2 control that can't fail | the test can't see what it asserts | **stop** |
| **R-KMSP or R-D2 off** | | **stop** |
| **a build over 96 MB** | the registered stop band | **stop** |
| **anything else** | | **stop and report** |

### Points A to D: mine, not ruled, for the orchestrator's reading

- **A. The sea.**
  - QW4-1 measured no sea, and the code PR carries one. So its memory can be graded against QW4-1 only on a store without the sea layer,
    as registered above.
  - The prototype's sea (QW4-2) is computed at build time from Natural Earth's land, while the code PR reads a stored sea per cell.
    QW4-2's memory therefore isn't the code PR's either.
  - **Proposed:** the sea's memory gets its own rows in QW4-2's addendum, from a measurement that reads a stored sea the way the code PR
    will.
- **B. No CI test can read KMSP's or New York's data.** The repo's basemap tests use synthetic stores and one stored package (Bend).
  - So ruling 2's KMSP test and ruling 3's two assertions are split:
    - a CI test on a synthetic store of the same shape, with its control (T-PROV-a, T-PROV-b, T-D2);
    - a stop row at the real points in the code PR's run (R-KMSP, R-D2).
  - The memory half of "the first step is the largest" can't be a CI test: vitest has no memory instrument.
  - After a deploy, `bm_build`'s `steps` is where a third step would show.
- **C. Where the refusals are counted.**
  - A `refused.water` line in PROVENANCE matches ruling 2, and QW4-3's registered "counted once in PROVENANCE". It counts the drawn
    set's refusals. It is emitted whenever water is read, as `dropped` and `collapsed` are, so 0 is written and absent never means 0.
  - **But it breaks MG0b.** MG0b requires provenance.ts's output to have the same lines as Weatherscope's stored Bend package, and the
    new line changes that count. So the line needs Weatherscope's builder to emit it too, or MG0b to change. That is another session's
    contract.
  - The alternative keeps the format: refusals only on `bm_build`. Then the package would not say it lacks a ring.
  - **I recommend the PROVENANCE line, once Weatherscope's session agrees through the orchestrator.**
- **D. KBOS's margin.** In O2's world KBOS reads +12.07: 0.68 below its one value, leaving 0.10 for noise. As ruled in 1(c), it stays one
  value, so a reading of +11.9 there would stop.

### Amendment (the orchestrator's 22:38Z rulings on points A to D): re-frozen before anything is built

The ruling, verbatim:

> c0be464cf124 is read (docs only, +508). CM-0, CM-P, the MIX outcome, the "neither O1 nor O2" stop and the generator-recomputed figures
> are all good additions. Rulings on A-D; push them as one amendment before anything is built:
> A. THE SEA: agreed.
> - The sea's memory gets its own rows in QW4-2's addendum, measured reading a stored sea the way the code PR will.
> - The property "a store with no sea layer builds no sea" is registered, with its control: the same store with a sea layer added does
>   build one.
> B. CI vs REAL POINTS: agreed. The split is T-PROV-a/b and T-D2 in CI, R-KMSP and R-D2 in the run.
> - One addition: bm_build's new steps field needs a NAMED READER, or it's an instrument firing into a void.
> - Name it in the registration: the post-deploy staging rows assert steps ≤ 2 at New York's 4, plus any routine check that already
>   reads bm_build.
> C. REFUSALS: split, so the code PR doesn't wait on another session's contract.
> - The code PR fixes collapsed.water's VALUE (the drawn set only), with no format change; MG0b holds.
> - Refusals are counted on bm_build. The package then makes no false claim, only an omission.
> - The refused.water line is a SEPARATE contract change: weatherscope's builder and MG0b change in the same PR, with weatherscope's
>   agreement. I'll put it to weatherscope once its CI queue clears.
> - Re-freeze before running: T-PROV-a's "refused.water: 1" becomes the bm_build refusal count, and QW4-3's "counted once in
>   PROVENANCE" becomes "counted once on bm_build". QW4-3 hasn't run, so that's a re-freeze, not a re-grade.
> D. KBOS: give it both values, O1 +12.75 and O2 +12.07, each ±0.785. State that they overlap, so KBOS can't tell the worlds apart.
> - KBOS must read inside the band of the world that KJFK, KORD and KIND read. A reading only in the other world's band stops.
> - If the three read a MIX, KBOS passes within the union of the two bands.
> Send the amendment's sha. The addendum for QW4-2 to QW4-4 can carry A and C's QW4-3 change, or come separately, your choice.

**What it changes, row by row.** Where this and the section above differ, this holds. QW4-3's change is made here; the addendum for QW4-2
to QW4-4 brings only their tooling and the sea's rows.

**A. The sea.**

| row | where | predicted |
|---|---|---|
| **T-SEA0** | CI, on a synthetic store with no sea layer, and the same store with one added | **with no sea layer: no sea ring is drawn, and PROVENANCE reads no sea layer.** The control: **with the layer added** (every other object the same), the package draws the sea's rings and PROVENANCE reads the layer. The fixture puts its sea where no other water is, so a ring there can only be the sea's |

- **On real data, the no-sea half is already graded:** at all 13 points, on the store with no sea layer, every section but PROVENANCE must
  equal QW4-1's package, which has no sea.
- **The sea's memory, and the sea on real points, are rows in QW4-2's addendum.** They are measured reading a stored sea the way the code
  PR will, not the prototype's sea computed from land.

**B. The readers of `bm_build`'s new fields.**
- **`bm_build` gains three integer fields:**
  - `steps`, D2's step count;
  - `refused`, the rings the backstop refused in the set the package draws (what the package omits);
  - `refusedBuilt`, its refusals across every water set built.
- **The named reader is the code PR's post-deploy staging rows, registered with its deploy:**
  - **`steps ≤ 2` at New York's 4**;
  - at KMSP, `refused` and `refusedBuilt` equal to R-KMSP's figures from the local run.
- **No routine check reads `bm_build` today.** On main, its readers are one-off: the registered staging rows, and
  `docs/basemap-a1/instruments/bb-lines.mts`, run by hand for #541.
  - After the staging rows, these fields are read only when someone tails the builder. That is the void shape the 22:38Z ruling names.
  - It is stated here, not solved. A routine reader would be its own item.

**C. Refusals: no format change.**
- **The code PR fixes `collapsed.water`'s value** to count the drawn set only. A refused ring is never inside it. That is the defect at
  `frame.ts:242` in the prototype, where a refused ring is stored as null and counted as collapsed.
- **No `refused.water` line. PROVENANCE's line count is unchanged, so MG0b holds.** The line is a separate contract change: Weatherscope's
  builder and MG0b change together, in one PR, with Weatherscope's agreement. The orchestrator is putting it to Weatherscope.
- **The package rows:** PROVENANCE differs from QW4-1's **only in `collapsed.water`'s value**, and only at rungs 2 to 7.

| row | where | re-frozen as |
|---|---|---|
| **T-PROV-a** | CI, on QW4-3's bowtie store | with the backstop on: **`refused: 1` on the build's result, which `bm_build` carries** (the test also asserts that `buildLine` passes it through), **and `collapsed.water` equal to the same store's with the bowtie removed.** The control: with the backstop off, the bowtie is drawn with 1 proper crossing |
| **T-PROV-b** | CI, KMSP's shape | **unchanged:** `collapsed.water` equals the drawn set's count. Its control still shows that the ε × 2 set's count, and the drawn set's count with the refusal inside, both differ from it |
| **R-KMSP** | the code PR's run, on real data | **KMSP's `collapsed.water` and `refused` equal those of KMSP built at ε × 8 with no cap** (rung 0, where no rung swaps a set). The control: **`refusedBuilt` at least 1** |
| **QW4-3** | the prototype's bowtie store (its fixture is pinned in the addendum) | **the control is unchanged:** with the backstop off, the package carries the bowtie with 1 proper crossing. **With it on:** the ring is dropped and **counted once on `bm_build`**: `refused: 1`. **`collapsed.water` doesn't count it,** and the package carries 0 crossings. `refusedBuilt` counts each water set the bowtie survives into. The fixture is sized to survive at ε × 8, so that is all four: ε, × 2, × 4 and × 8 |

QW4-3 has not run, so this is a re-freeze, not a re-grade. The prototype keeps its `frame.ts:242` count until the addendum's tooling
changes it. The addendum pins that change by sha256 before QW4-3 runs.

**D. KBOS: two values that overlap.**

| point | graded | O1 (absent) | O2 (present) |
|---|---|---|---|
| **KBOS** | the peak, the code PR minus main | **+12.75 ± 0.785** (11.965 to 13.535) | **+12.07 ± 0.785** (11.285 to 12.855) |

- **They overlap,** being 0.68 apart, less than twice 0.785, **so KBOS cannot tell the worlds apart.** It is graded by the world that
  KJFK, KORD and KIND read:
  - **if the three read O1,** KBOS must read inside O1's band. **A reading only in O2's band (11.285 to under 11.965) stops**;
  - **if the three read O2,** KBOS must read inside O2's band. **A reading only in O1's band (above 12.855 to 13.535) stops**;
  - **if the three read a mix,** KBOS passes anywhere in the union, 11.285 to 13.535;
  - **outside both bands, it stops,** as any one-value point would.
- KBOS's cell phase stays recorded, not graded.

## The addendum, part 1: QW4-3 and the prototype's counting controls (frozen before they run)

**The addendum comes in two parts.**
- **Part 1, here:** QW4-3, the backstop's plant, as the 22:38Z amendment re-froze it, and two controls on the prototype it runs on.
- **Part 2, later, with its own sha before it runs:**
  - the sea: QW4-2, QW4-4, T-SEA0 on the prototype, the KB rows and the plants, S-KB-SEAM included;
  - the extent's counts, and the R2 figures.

  It carries the orchestrator's 22:46Z and 22:47Z rulings: the sea cut to the 1° level-0 grid at ingest, within 250 km of a covered
  point; the lakes; islandsDropped on `bm_build`; the account's total R2 storage against the 10 GB free tier.

### The record

- **`ne_10m_ocean.zip`,** fetched from its pinned URL (`proxy/scripts/basemap/sources.ts:30`) into its own empty directory:
  **sha256 MATCH `db626fcd5d50`**, the store manifest's pin. **`ne_10m_land.zip`** (local): **MATCH `e547d749445e`**.
- **`ne_10m_lakes.zip`** (local): `0803a06f9c3c`. **Not pinned anywhere yet.** The code PR adds it to `sources.ts`, and it is re-verified at
  its URL before use. The threshold and the US cut are with the owner (22:46Z ruling 2).

### The prototype: pq4c

**`mk-pq4c.mjs`** (sha256 `351a3fa2c4f5`) is `mk-pq4b.mjs` (`4161a87b369d`) plus anchored edits, each refused unless its anchor occurs
exactly once.
- **Its arguments were proved first:** pq4b, regenerated from `mk-pq4b.mjs` with the same arguments, came back with its whole `src`
  identical, `build.ts` `88f2316b2aaf` as registered.
- **`Q4_PROV=1`,** for ruling 2 and the 22:38Z ruling C:
  - a ring the backstop refuses is counted apart from `collapsed`;
  - rungs 2 and 5 take `collapsed.water`, and the refusals, from the ε × 8 set they draw.
- **It fixes the prototype's own two defects, named where they are:**
  - `budget.ts`'s rung 2, `c2 = { ...content, water: water8 }`, and rung 5's `c4` both keep the ε × 2 set's `collapsed.water`;
  - `frame.ts:242` stores a refused ring as null, so `collapsed.water` counts it.
- **The build reports the counts `bm_build` would carry:**
  - `refused`, the drawn set's refusals;
  - `refusedBuilt`, across every water set built (the prototype's existing counter);
  - `steps`, D2's step count.
- **With `Q4_PROV` unset, every added path is inert.** QW4-C0 grades that.
- **The files changed against pq4b:**
  - `build.ts` (`f6fe699c2760`, 5 lines), `budget.ts` (`7d98b3971671`, 3), `frame.ts` (`be6c086e95c5`, 3), `layers.ts` (`0608b62c465c`,
    3). Every other `src` file is identical.
  - It also copies `test/basemap-synth.ts` (`b023a4ed5424`) and its four `scripts/basemap` modules from the pinned commit. All five
    were checked blob-identical, so the fixture stores use the pinned encoders.
- **The children and the driver:**
  - `dr-q4c3.mts` (`b02b5a66d240`): one build per process, of pq4b or pq4c, at a point, with no memory instrument. It reports the
    package's sha, a digest per section, the non-terrain bytes, PROVENANCE's byte length and its lines;
  - `dr-q4-bowtie.mts` (`4f408a4a2cf2`): QW4-3's store, made in memory;
  - `dr-q4-c.mts` (`fb841b7a8604`): the driver, with the predictions below copied in.
- **Import checks:**
  - each child, with both prototypes, refused a bad point or argument with exit 2 after its imports and exports had resolved, and
    no build ran;
  - esbuild parses all three.

### The rows

| row | what | predicted |
|---|---|---|
| **QW4-C0** (the anchor, and the edits inert when off) | at all 13, QW4-1's knobs (`Q4_LADDER=b`, `Q4_D2=1`, `Q4_TP=1`) | **pq4b's package is QW4-1's recorded sha** at every point (the references have not moved). **pq4c with `Q4_PROV` unset is byte-identical to it** |
| **QW4-C1** (the counting fix, on real points) | the same, plus `Q4_PROV=1` | **every section but PROVENANCE equal to pq4b's,** with the same rung, radius and tolerance. **PROVENANCE has the same lines but for `collapsed.water`'s value,** and at KJAC (rung 1, no refusal) not even that. **Or, at a point, the cap edge** (named below), recorded and not a stop. **`refusedBuilt` as QW4-1 counted it:** KMSP 2, every other point 0. **`steps`: 2 at New York's 4, 1 at the 9.** **KMSP's `refused`: 0** (below). Each point's old and new `collapsed.water` is recorded |
| **QW4-3** (the backstop's plant) | `dr-q4-bowtie.mts`'s store, B's knobs with `Q4_PROV=1`, in the order below | as the four rows below |

**QW4-3's store.**
- Origin (0, 0), one water file, the client's query (r 162 km, ε 201,168 mm), water only.
- **"bowtie":** (E, S) km A(10,10), D(10,30), C(50,10), B(50,50).
  - Edges D–C and B–A cross once, near (23.3, 23.3). It has 4 vertices, so tp has nothing to add.
  - Its lobes are unequal, so it is an outer part (signed lon/lat area −800, in (E, −S) km).
  - Its vertices are at least 20 km apart, so it survives at ε × 8 (1.609 km), and is in all four water sets.
- **"square":** a 10 km outer square at (−40..−30, −40..−30), which nothing touches.

| QW4-3 | knobs, store | predicted |
|---|---|---|
| **the control** (first) | the backstop off, both rings | **the package carries the bowtie: 2 rings, 1 proper crossing;** `refused` 0, `refusedBuilt` 0 |
| **on** | both rings | `kOk`, rung 0, `steps` 1. **1 ring, 0 crossings; `refused: 1`; `refusedBuilt: 4`** (ε, × 2, × 4, × 8); **`collapsed.water: 0`** |
| **the reference** | the square alone | 1 ring, `collapsed.water: 0`, `refused` 0, `refusedBuilt` 0. **The package byte-identical to "on"'s:** the bowtie leaves no trace in the package. The store id is the same, since the manifest is |
| **the old rule** (`Q4_PROV` unset) | both rings | **`collapsed.water: 1`, which differs from the right rule's 0,** so the check can fail; `refusedBuilt` 4, 1 ring |

**THE CAP EDGE, named before the run, and a correction to the code PR's package row.**
- **The mechanism.** The ladder counts PROVENANCE's bytes against the 32,768 cap. QW4-1's packages sit close to it: KJFK at 32,767,
  and KLGA, KSFO and KSEA at 32,766. When `collapsed.water`'s new value has a different number of digits, PROVENANCE changes length.
  A package within those bytes of the cap can then fit at a different drop count, and its roads or water section legitimately changes.
- **So QW4-C1 at a point is one of two named outcomes:**
  - **equal:** every section but PROVENANCE equal, and PROVENANCE differs only in `collapsed.water` (and not at KJAC);
  - **the cap edge,** recorded and not a stop. It requires, together:
    - PROVENANCE's byte length changed;
    - the same rung, `kOk`, and at most 32,768;
    - the sections that differ are water or roads only;
    - PROVENANCE differs only in `collapsed.water`, `dropped.water` and `dropped.roads`;
    - the point is not KJAC.

  It is printed with the byte arithmetic, PROVENANCE's length against the slack under the cap. **A rung change** is the cap edge
  reaching a rung boundary, and **stops and is reported.**
- **The same applies to the code PR's package row,** frozen at `c0be464cf124`: "every section but PROVENANCE is byte-identical to
  QW4-1's package". The code PR fixes the same count, so the same edge can move a drop count there. **That row takes the same two
  outcomes, on the same conditions.** I wrote it without the mechanism, and it would have stopped a correct build.

**Why KMSP's `refused` is predicted 0, and not just recorded.**
- KMSP's package draws the ε × 8 set at rung 3. It held 176 water rings and 0 crossings both in the cost read (tp, no backstop) and in
  QW4-1 (tp and the backstop), so tp alone left that set with nothing to refuse.
- Its 2 refusals are in sets it doesn't draw: at least one in ε × 2, read from QW4-1's package change.
- **A reading other than 0 means that reasoning is wrong.** It stops, like any off reading.

| outcome | meaning | next |
|---|---|---|
| **QW4-C0's anchor off** | QW4-1's references have moved (the store, the prototype or the tree) | **stop** |
| **QW4-C0 not inert** | pq4c's edits change packages with the knob off | **stop** |
| QW4-C1 at a point: **the cap edge,** on all of its conditions above | PROVENANCE's new length moved a drop count | **recorded, with the byte arithmetic; not a stop** |
| **QW4-C1: any other difference** (a section other than water or roads, another PROVENANCE line, PROVENANCE's length unchanged, or a rung change) | the fix reaches more than the count, or the edge crossed a rung | **stop** |
| QW4-C1: `refusedBuilt`, `steps`, or KMSP's `refused` off | the counters or the reasoning above are wrong | **stop** |
| **QW4-3's control shows no crossing** | the plant didn't land, so its pass would mean nothing | **stop** |
| QW4-3 on, the reference or the old rule off | the backstop or the counting doesn't hold | **stop** |
| **anything else** | | **stop and report** |

**Machine rules:**
- No vitest runs.
- Free disk is read before the run, and it is not a large build.
- Nothing runs on staging, and nothing deploys.
- The run reads the pinned `525cf7d1` through the prototypes, not main's head.

## Part 1, read (2026-11-11, 22:57:18–22:58:34Z): stopped at QW4-3 "on", on my prediction

**The run.**
- Part 1 was accepted at `fe02f365e524`, and the go was the orchestrator's at 22:57Z.
- Before it ran: every artifact's sha256 read as registered, and C: had 76 GB free.
- The driver (`fb841b7a8604`) stopped with exit 1 at the first off row, as registered. The error file is empty. The log is scratch
  (`98cf5223931a`).
- **The optional narrowing (22:57Z) was not added.** It needs each point's new `collapsed.water` before the run, and only the old values
  were recorded. The registered cap-edge condition (PROVENANCE's byte length changed) is the same test, applied when it is read.

**QW4-C0: as predicted at all 13.** pq4b reproduced QW4-1's 13 shas, and pq4c with `Q4_PROV` unset was byte-identical to it.

**QW4-C1: as predicted at all 13. Every point read "equal", and none reached the cap edge.**
- Every section but PROVENANCE was equal, with the same rung, radius and tolerance.
- PROVENANCE differed only in `collapsed.water`, and at KJAC not at all.
- `refusedBuilt` read as QW4-1 counted it (KMSP 2, else 0), `steps` 2 at New York's 4 and 1 at the 9, and KMSP's `refused` 0.

| point | `collapsed.water`, the ε × 2 set's (old) → the drawn set's | PROVENANCE, B | non-terrain bytes, of 32,768 | slack |
|---|---|---|---|---|
| KBOS | 4 → **140** | 1,971 → 1,973 | 32,717 → 32,719 | 49 |
| KJFK | 0 → **96** | 1,970 → 1,971 | 32,767 → **32,768** | **0** |
| KLGA | 3 → **98** | 1,970 → 1,971 | 32,766 → 32,767 | 1 |
| KEWR | 1 → **89** | 1,971 → 1,972 | 32,746 → 32,747 | 21 |
| Midtown | 0 → **91** | 1,970 → 1,971 | 32,756 → 32,757 | 11 |
| KMIA | 1 → **80** | 1,965 → 1,966 | 29,182 → 29,183 | 3,585 |
| KSFO | 1 → **35** | 1,967 → 1,968 | 32,766 → 32,767 | 1 |
| KSEA | 1 → **87** | 2,117 → 2,118 | 32,766 → 32,767 | 1 |
| KORD | 3 → **80** | 1,832 → 1,833 | 32,759 → 32,760 | 8 |
| KDTW | 3 → **66** | 1,981 → 1,982 | 32,759 → 32,760 | 8 |
| KMSP | 3 → **273** | 1,832 → 1,834 | 32,743 → 32,745 | 23 |
| KIND | 0 → **24** | 1,834 → 1,835 | 32,760 → 32,761 | 7 |
| KJAC | 0 → 0 | 1,828 → 1,828 | 30,124 → 30,124 | 2,644 |

- **Ruling 2's "truncated row", now measured.** Under rungs 2 to 7, `collapsed.water` reported the ε × 2 set's count, 0 to 4. The set
  the package draws had 24 to 273 collapsed rings. The package understated what it lost by up to 270.
- **KJFK now sits exactly at the cap,** with no slack, and KLGA, KSFO and KSEA have 1 byte each. In the code PR, any byte PROVENANCE
  gains at those points moves their ladder. With the cap-edge outcome that is recorded, and a rung change stops.

**QW4-3: the control read as predicted** (2 rings, 1 proper crossing, `refused` 0, `refusedBuilt` 0).

**QW4-3 "on": OFF-PREDICTION, on one value.**
- **`refusedBuilt: 3`, predicted 4.**
- Everything else read as predicted: `kOk`, rung 0, `steps` 1, 1 ring, 0 crossings, `refused: 1`, `collapsed.water: 0`.
- The reference and the old rule did not run.

**The cause is my prediction, not the code.**
- The prototype builds **three water sets: ε, ε × 2 and ε × 8.** Its ε × 4 set takes rivers and roads only (`build.ts:114`:
  `if (L4 && (ft.kind === "rivers" || ft.kind === "roads"))`).
- That is the design's own rung 5, in this file's ladder table: "rivers and roads at ε × 4 (water stays at ε × 8)".
- So the bowtie was refused once in each water set: 3.
- **The alternative, and why the code excludes it:** four water sets, with the bowtie surviving simplification in only three. The
  ε × 4 set is never given a water feature, so it has nothing to refuse.
- **The error's reach:**
  - I wrote "all four: ε, × 2, × 4 and × 8" in the 22:38Z amendment (`b0d52f1b8c75`) and again in part 1, without reading that line;
  - the code PR's `bm_build` definition of `refusedBuilt` ("across every water set built") is unchanged. It counts three sets.

**Proposed re-freeze (committed here; pushed only after a ruling):**
- **QW4-3 "on": `refusedBuilt: 3`** (ε, × 2, × 8). **The old rule: `refusedBuilt: 3`.** Every other QW4-3 value is unchanged.
- **The 22:38Z amendment's QW4-3 row** takes the same correction: "each water set the bowtie survives into: ε, × 2 and × 8, so 3".
- **The re-run is QW4-3 alone,** all four of its rows, the control first. QW4-C0 and QW4-C1 stand as read.
  - The driver is `dr-q4-c.mts` `645a611550cf`, with `--qw43-only`. Its diff against the copy that ran (`fb841b7a8604`) is the two
    numbers 4 → 3, the flag, and a comment.
- **Anything else stops.**

**The ruling (the orchestrator, 23:01Z):**
- Yes, as proposed: QW4-3 alone, all four rows with the control first. QW4-C0 and QW4-C1 stand as read.
- **The miss is on record as both of ours:** the orchestrator accepted "refusedBuilt = 4" at 22:42Z without reading `build.ts:114`
  either. The off reading stays on record.
- **Refusals per water set: offered, and skipped as the ruling allows,** because printing them would need an edit. The prototype keeps
  each set's count inside `build`'s closure (`at[tol]`), and the child sees only the drawn set's count and the total. The `build.ts:114`
  citation stands as the elimination.
- This is QW4-3's first re-run. A second off reading goes to the orchestrator and on to the owner.

## QW4-3, re-run (2026-11-11, 23:01:39Z): every row as predicted

**The run.**
- `beef9a09d14e` was pushed first.
- Every artifact's sha256 read as registered, and C: had 76 GB free.
- The driver (`645a611550cf`, `--qw43-only`) exited 0 with an empty error file. The log is scratch (`3f33656ef447`).

| QW4-3 | read |
|---|---|
| **the control** (the backstop off) | **2 rings, 1 proper crossing;** `refused` 0, `refusedBuilt` 0. The plant landed |
| **on** | `kOk`, rung 0, `steps` 1; **1 ring, 0 crossings; `refused: 1`; `refusedBuilt: 3`; `collapsed.water: 0`** |
| **the reference** (the square alone) | 1 ring, `collapsed.water: 0`. **The package is byte-identical to "on"'s** (`5127d056ca90`): the bowtie leaves no trace in the package |
| **the old rule** (`Q4_PROV` unset) | **`collapsed.water: 1`, against the right rule's 0,** so the check can fail; `refusedBuilt` 3 |

**Part 1 is complete:** QW4-C0, QW4-C1 and QW4-3 all read as predicted, QW4-3 on its re-run. **Next is part 2, the sea,** with its own
sha before anything runs.

## The code PR's package row, narrowed point by point (the cloud reviewer, 23:03Z, relayed by the orchestrator at 23:21Z)

The ruling, verbatim:

> 2. THE NARROWING IS NOW REQUIRED (the reviewer): whether collapsed.water's digit count changes is known from QW4-1's data, so predict
>    the cap edge point by point, and a cap edge anywhere else stops. Part 1 has already run (all "equal"), so it stands as registered.
>    Put the per-point narrowing into the CODE PR's rows.

**The data is better than QW4-1's.**
- QW4-1 recorded only the old counts. QW4-C1 has since measured the new ones on pq4c, the code PR's rule on the same prototype, and
  read "equal" at all 13: no cap edge anywhere.
- So the code PR's package row, on the store with no sea layer, is narrowed to one outcome at every point.

| point | `collapsed.water` (QW4-1's → the code PR's, from QW4-C1) | digits | PROVENANCE, bytes | non-terrain bytes after, of 32,768 | the outcome |
|---|---|---|---|---|---|
| KBOS | 4 → **140** | 1 → 3 | +2 | 32,719 | **equal** |
| KJFK | 0 → **96** | 1 → 2 | +1 | **32,768** (0 to spare) | **equal** |
| KLGA | 3 → **98** | 1 → 2 | +1 | 32,767 | **equal** |
| KEWR | 1 → **89** | 1 → 2 | +1 | 32,747 | **equal** |
| Midtown | 0 → **91** | 1 → 2 | +1 | 32,757 | **equal** |
| KMIA | 1 → **80** | 1 → 2 | +1 | 29,183 | **equal** |
| KSFO | 1 → **35** | 1 → 2 | +1 | 32,767 | **equal** |
| KSEA | 1 → **87** | 1 → 2 | +1 | 32,767 | **equal** |
| KORD | 3 → **80** | 1 → 2 | +1 | 32,760 | **equal** |
| KDTW | 3 → **66** | 1 → 2 | +1 | 32,760 | **equal** |
| KMSP | 3 → **273** | 1 → 3 | +2 | 32,745 | **equal** |
| KIND | 0 → **24** | 1 → 2 | +1 | 32,761 | **equal** |
| KJAC | 0 → 0 | 1 → 1 | 0 | 30,124 | **equal, PROVENANCE unchanged** |

- **"Equal" means:**
  - every section but PROVENANCE is byte-identical to QW4-1's package;
  - PROVENANCE differs only in `collapsed.water`, which equals the value above;
  - the rung, radius and tolerance are QW4-1's.
- **The cap edge is predicted at NO point.** **A cap edge anywhere stops,** as does a `collapsed.water` other than the value above.
  This supersedes the two outcomes given the code PR's package row in part 1's correction.
- **Why this can hold at KJFK, with 0 bytes to spare:** QW4-C1 built exactly this content and it fit at 32,768. The code PR adds nothing
  to the package beyond it, since ruling C makes no format change.
  - So a single extra byte there, from any source, is off-prediction. That is the point of narrowing: it would otherwise pass as "the
    cap edge".

## The lake-join design read (frozen before it runs)

**The owner's condition** (relayed by the orchestrator, 23:21Z, stamped 23:03Z), verbatim:

> where a lake crosses the border (Lake Erie off Buffalo, say), the two sources must join into one clean lake at the closest zoom. No
> land-coloured line along the border, and no water spilling past the US shore.

**The ruling (the orchestrator, 23:28Z), verbatim:**

> YES to the DESIGN READ, read-only, local and scratch, frozen and sent to me before it runs:
> 1. Measure (a), (b) AND (c), stating (c)'s margin. A scratch polygon difference is fine for the read; it isn't store code.
> 2. Two lakes, Buffalo (Erie) and Champlain, at the closest zoom with FillRing's rule, against TIGER's water. For each, print SEAM pixels
>    (land between the two waters) and SPILL pixels (NE water on US land beyond TIGER's shore). Print where they sit too: at the border,
>    or along the shore.
> 3. Each count needs its control: a planted seam and a planted spill, each read non-zero first.
> 4. Freeze the CHOICE RULE before the run, in the owner's terms. His condition is "no land-coloured line along the border, and no water
>    spilling past the US shore", so the pass test is SEAM = 0 and SPILL = 0 at both lakes. Of the designs that pass, the simplest
>    wins, in the order (a), (c), (b).
> 5. The escape branch: if NONE passes at both lakes, nothing is chosen. It comes to me, and on to the owner with the numbers and one
>    rendered picture per design at Buffalo. Whether a few pixels of tolerance are acceptable is his call, not ours.
> KB7 is then frozen on the chosen design, and its SEAM and SPILL plants stay as registered.

### The designs

The NE lake L (Natural Earth 10 m lakes), and U, the US polygon (Natural Earth admin_0, ADM0_A3 = USA):

| design | the NE water drawn | how it can fail |
|---|---|---|
| **(a)** | L whole, over TIGER's US part | spill wherever NE's shoreline runs past TIGER's |
| **(b)** | L minus U | a seam wherever NE's border and TIGER's lake boundary don't meet |
| **(c)** | L minus U eroded by **M = 1.0 km**, so NE runs 1 km into the US side of the lake | spill near where the border meets the shore |

- **Every design is cut into hole-free pieces** (vertical cuts through each hole), each wound as an outer part. They go through the
  store's water path as the code's lake rings would: clipped, tp, the backstop, quantised, and on the same ladder as TIGER's water.
- **The two plants:**
  - `plant_seam`: (b) minus a 300 m band along the US border;
  - `plant_spill`: (b) plus a 600 m square on US land: Buffalo (42.900, −78.840), Rouses Point (44.990, −73.400).

### The two homes, the view, and what is counted

- **The homes:** Buffalo (42.886, −78.879), on Lake Erie where the border enters it; Rouses Point (44.994, −73.365), at Lake
  Champlain's north end, where the border crosses it.
- **The package:** the client's query (r 162 km, ε 201,168 mm, water, rivers, roads, places and terrain, its three levels), built by
  pq4c with the code PR's knobs (`Q4_LADDER=b`, `Q4_D2=1`, `Q4_TP=1`, `Q4_PROV=1`), terrain included, so the rung and the water set's
  tolerance are the real package's.
- **The view: the device's closest zoom.**
  - The map client requests one package for the configured radius, and a zoom changes nothing (`include/MapQuery.h`, CA10). So the
    closest zoom draws this same package.
  - radarzoom's smallest step is 5 units. For the 100 mi setting the query is built from, that is **8.04672 km**, centre to edge.
  - North-up, 240 × 240, centred on the home, mapped as `WxBasemap.h`'s Mapper maps package units, about **67 m per pixel**.
- **Water by FillRing's rule** (A10.2): a pixel is filled when its centre is inside a ring by even-odd. Each ring is filled on its own,
  and the water mask is their union.

**The masks:**
- **W_d** is the design build's water. **W_n** is the build with no NE piece: TIGER's water as the package draws it, the truth for
  "TIGER's shore".
- **L** and **U** are filled from Natural Earth by even-odd, holes included.
- **B, the lake's border band:** pixels in L whose centre is within **1.5 km** of the US border.
- **C, the closing of W_d** by a disc of **15 px** (1 km): what W_d would be with every gap narrower than 2 km filled.

| count | definition | where it sits |
|---|---|---|
| **SEAM** | L and B and not W_d and C: a land-coloured line between two waters | at the border, by definition |
| **SPILL** | W_d and not W_n and U and not (L and B): NE water on US territory where TIGER draws none, outside the lake's border band | **at the border** (centre within 1.5 km of it) or **along the shore** (beyond) |

The border band inside the lake is left out of SPILL. There, the strip between NE's border line and TIGER's lake boundary is lake to both
sources, and drawing it is what closes a seam.

### The tooling

- **The geometry:** `lake-designs.py` (sha256 `eef5e88c585e`), run in a scratch venv with shapely 2.1.2 (GEOS 3.13.1; binary wheel
  only). Its outputs are `lake-BUFFALO.json` (`aafcf98c629f`) and `lake-ROUSES.json` (`aee8e37d6704`).
  - Buffalo: (a) 749.38 km² in a 30 km box, (b) 296.06, (c) 354.51. The border runs 63.69 km through the lake.
  - Rouses Point: (a) 508.57, (b) 77.97, (c) 87.30. The border runs 10.08 km through it.
- **The inputs:**
  - **`ne_10m_admin_0_countries.zip`**, fetched from its pinned URL: **sha256 MATCH `ce1ac7036499`** (the store manifest's pin).
  - **`ne_10m_lakes.zip`**, the local copy `0803a06f9c3c`, not pinned anywhere yet. The code PR adds it to `sources.ts`.
- **`lake-fetch.mts`** (`e56c9e06aca0`) gathers the objects each home's package reads, using the builder's own `squareBox`, `boxMeets`
  and `levelTiles`.
  - It copies them from the local store, or fetches them **read-only** from the store's bucket (`blipscope-basemap`, store
    `4c0875738f38`, `wrangler r2 object get --remote`), into a scratch `lake-obj`.
  - Each cell is verified by its byte count against the index, and each tile by its sha256 against the manifest.
  - About 30 cells and the levels' tiles, so R2 Class B reads only, and nothing written. **The fetch is the read's first step,** and
    nothing has been fetched yet.
- **`lake-build.mts`** (`35b57970ab0b`) makes one build per process.
  - It injects a design's pieces in memory as water features of one scratch file, `https://www2.census.gov/zz-scratch/ne_10m_lakes.zip`,
    which sorts last, so no existing feature's file number shifts.
  - The pieces carry AWATER 1e12, so the size filter keeps every piece of the lake.
  - The touched cells are re-encoded, and each is checked to re-encode to its own bytes first. A new manifest and `current` go with
    them.
- **`lake-render.py`** (`f5d023f78518`) renders and counts. **`lake-read.py`** (`fe1dbdf5f00b`) is the driver.
- **Import checks:**
  - `lake-fetch.mts --imports-only` resolved;
  - `lake-build.mts` refused an unknown home with exit 2 after its imports;
  - both Python scripts compile;
  - nothing was fetched or built.

### Order, the controls, the choice rule, the escape

1. **Fetch,** verified. Any failure stops.
2. **Builds** at each home: none, (b), `plant_seam`, `plant_spill`, (a), (c). Every build must be ok, with 0 objects missing, and the
   knobs as read by the modules; else stop.
3. **Renders.**
4. **CONTROLS FIRST,** at each home: SEAM(`plant_seam`) > SEAM(b), and SPILL(`plant_spill`) > SPILL(b). **Either not so: the
   instrument is blind; stop, and no design is read.**
5. **THE CHOICE RULE, in the owner's terms:** a design passes when **SEAM = 0 and SPILL = 0 at both lakes.** Of those that pass, the
   simplest wins, in the order **(a), (c), (b).**
6. **THE ESCAPE:** if none passes at both lakes, **nothing is chosen.** The numbers and the pictures at Buffalo
   (`lake-out/BUFFALO-{a,b,c}.png`) go to the orchestrator, and on to the owner. Whether a few pixels are acceptable is his call.

**My expectation, stated before the run, and it grades nothing.** I expect none to pass.
- At 67 m per pixel, the water set at the rungs these packages reach (ε × 8 at rungs 2 to 7, 1.6 km) is simplified independently for
  NE and TIGER, by about 24 pixels.
- So their shores, and their meeting at the border, are unlikely to coincide to the pixel.

If that holds, the escape branch is the likely result, and the owner sees the pictures.

| outcome | meaning | next |
|---|---|---|
| a fetch, build or render failure | the inputs or the tooling | **stop** |
| **a control reads blind** | SEAM or SPILL can't see its own plant | **stop; no design is read** |
| one or more designs pass at both lakes | the frozen rule chooses | **the simplest of them,** in the order (a), (c), (b). KB7 is then frozen on it |
| **none passes** | the owner's condition isn't met by any design at the closest zoom | **the escape:** the numbers and the pictures to the orchestrator and the owner |
| **anything else** | | **stop and report** |

**Machine rules:**
- Nothing is written to R2 and nothing deploys; the fetch reads only.
- No vitest runs. Free disk was 75 GB at 23:36Z.
- The run reads the pinned `525cf7d1`'s builder through pq4c.

### Amendment (the orchestrator's 23:40Z ruling): SPILL restated, so the band excuses only open water

**The ruling, verbatim:**

> ONE DEFINITION MUST CHANGE BEFORE IT RUNS, because it biases the choice: SPILL excludes the whole 1.5 km border band. But that band is
> exactly where (c) puts NE water onto the US side by design (the 1 km erosion), and Buffalo is a shore junction: the border meets the
> US shore at the Niagara. So as written, (c)'s spill onto Buffalo's waterfront would be invisible, and (c) passes SPILL nearly by
> construction. A count that can't see the failure of the design it grades is the blind-instrument shape. Restate it so the band
> excuses ONLY open-water strip pixels:
> - excused: pixels between NE's border line and TIGER water's border-side edge, and at least a stated distance from any TIGER
>   shoreline (land/water edge that isn't the border);
> - counted as SPILL, wherever they sit: NE-only pixels within that distance of TIGER's shoreline on the US side, band included. Print
>   them as SPILL_JUNCTION beside SPILL_SHORE;
> - add a third control, plant_junction: a small square of NE water on US land at the Niagara junction, inside the old band. It must
>   read SPILL_JUNCTION > 0, or the count is blind and nothing is read.
> Also, whatever the outcome, render BUFFALO-{a,b,c}.png for the record. They're not graded.

**SPILL, restated** (it supersedes the SPILL row above):
- **The strip:** pixels in L within 1.5 km of the border.
- **TIGER's shoreline:** the edge pixels of TIGER's water (W_n) whose non-water 4-neighbour is NOT in the strip. An edge facing the strip
  is TIGER's border-side edge, not a shoreline. **Near the shoreline** means within **0.5 km** of a shoreline pixel (the stated
  distance).
- **N1** = W_d and not W_n and U: NE-only water on US territory.

| count | definition |
|---|---|
| **SPILL_SHORE** | N1 beyond 1.5 km of the border |
| **SPILL_JUNCTION** | N1 within 1.5 km of the border, AND near TIGER's shoreline or outside L |
| **EXCUSED** (printed, not counted) | N1 within 1.5 km of the border, inside L, and not near TIGER's shoreline: **the open-water strip, the only pixels excused** |
| **SPILL** | SPILL_SHORE + SPILL_JUNCTION. The choice rule's SPILL = 0 is on this total |

**The third control, plant_junction.**
- It is (b) plus a **300 m square of NE water** at a site chosen at run time, from the "none" build, by a rule frozen here. The site is
  the US-land pixel (U, not W_n) within 1.5 km of the border that is **nearest the border**:
  - taken first from those **inside L** (the old band);
  - only if there are none, from those outside L;
  - ties by row, then column.
- At Buffalo that is the Niagara junction the ruling names. The site, and which set it came from, are printed. No site at a home stops.
- **It must read SPILL_JUNCTION greater than (b)'s, at each home.** That is "> 0" as ruled, made strict where (b) has some of its own.
  Else the count is blind, and no design is read.
- **The controls are now three, all first:** the seam, the spill, the junction.
- **The pictures** (`BUFFALO-{a,b,c}.png`, and every other design's) are written before the controls are read, so they exist whatever
  the outcome. They are not graded.

**The tooling now:**
- `lake-render.py` `8333cbb92406` (with `--site`);
- `lake-junction.py` `bc1789ecaf54` (new, in the shapely venv);
- `lake-build.mts` `42a7c11f5637` (it accepts `plant_junction`);
- `lake-read.py` `50c5748ac3b2`;
- `lake-fetch.mts` `e56c9e06aca0`, unchanged.

Every script compiles, and the build again refused an unknown home with exit 2 after its imports. Nothing has been fetched or built.

### Amendment (the orchestrator's 23:43:55Z ruling): TIGER's shoreline made geometric

**Why the 23:40Z restatement re-opened the hole.**
- It defined the shoreline as TIGER water edges whose non-water neighbour is not in the strip, and defined the strip with the NE lake.
- Where NE's generalized lake overlaps US land, the very spill being hunted, that land is in the strip. So TIGER's shore edges facing
  it were classed border-side, and the spill pixels were EXCUSED.
- The frozen plant_junction site (US land inside the NE lake, nearest the border) sits in exactly that class. It would likely have read
  blind.

**The ruling's fix, as applied:**
- **EDGE:** the pixels of TIGER's water (W_n) with a non-water 4-neighbour.
- **Border-side:** an EDGE pixel within **D_b = 0.3 km** of NE's border line.
- **Shoreline:** every other EDGE pixel, independent of NE's lake. "Near the shoreline" stays within 0.5 km of it.
- **The residual ambiguity, stated:** the disc of radius D_b about each point where NE's border line meets TIGER's shore, i.e. where
  W_n changes value along the line, sampled every 10 m.
  - **JUNCTION_DISC** = N1 inside a disc: its own count, **counted in SPILL, never excused.** I read "reported and not excused" as
    counted, and say so in case it was meant otherwise.

| count | definition |
|---|---|
| SPILL_SHORE | N1 beyond 1.5 km of the border, outside every disc |
| SPILL_JUNCTION | N1 within 1.5 km of the border, (near the shoreline, or outside L), outside every disc |
| **JUNCTION_DISC** | N1 inside a disc |
| EXCUSED (printed, not counted) | N1 within 1.5 km, inside L, not near the shoreline, outside every disc: the open-water strip only |
| **SPILL** | SPILL_SHORE + SPILL_JUNCTION + JUNCTION_DISC |

**plant_junction keeps its frozen site rule,** with an assertion read first.
- **The assertion:** the planted pixels (centres inside the 300 m square) include NE-only US pixels, and every one of them is classed
  JUNCTION or JUNCTION_DISC, with none EXCUSED. If it fails, the plant doesn't test the count it is meant to; stop, and no design is
  read.
- **The control** is then: SPILL_JUNCTION + JUNCTION_DISC must be greater with the plant than in (b), at each home.

**The tooling now:**
- `lake-render.py` `dc4bd32eef9f`;
- `lake-read.py` `eb2663a4807c`;
- `lake-build.mts` `42a7c11f5637`, `lake-junction.py` `bc1789ecaf54` and `lake-fetch.mts` `e56c9e06aca0`, unchanged.

Both edited scripts compile. Nothing has been fetched or built.

### The design read, run (2026-11-11, 23:47:19–23:50:57Z): stopped at its first render, an instrument failure

**The run.**
- The go was the orchestrator's at 23:46:51Z.
- Before it ran: every artifact's sha256 read as registered, C: had 75 GB free, and no `CLOUDFLARE_API_TOKEN` was set, user-level or
  in the shell (checked as a yes/no).
- The driver exited 1, and its error file is empty. The log is scratch (`24f7508fd174`).

**What completed, before the stop:**
- **The fetch:** 15 cells and 81 tiles per home, 192 objects in all. 69 were copied from the local store and 123 fetched read-only from
  `blipscope-basemap`, **every one verified** (cells by byte count against the index, tiles by sha256 against the manifest). Nothing
  was written to R2.
- **All 12 builds:** ok, with 0 objects missing and the knobs as read. **Every build is rung 3 at 162 km,** so the water drawn is the
  ε × 8 set.

  | Buffalo | none | (b) | plant_seam | plant_spill | (a) | (c) |
  |---|---|---|---|---|---|---|
  | non-terrain bytes | 32,763 | 32,755 | 32,759 | 32,755 | 32,766 | 32,759 |
  | water rings | 48 | 49 | 49 | 49 | 51 | 49 |

**The stop: `lake-render.py BUFFALO b` exited 1, on its own assertion,** before any count was computed. No SEAM or SPILL was read.

**The cause: my renderer.**
- The route rounds a query's lat and lon to two decimals (`query.ts:62-63`). So the package is centred on **(42.89, −78.88)**, not on
  the home **(42.886, −78.879)**.
- The device draws a package about its OWN home: WxBasemap's `Mapper::ToScreen` places package units through the package's centre and
  cosine, then into a view centred on `homeLat`/`homeLon`. The client's r includes the 0.8 km margin for exactly this offset.
- My renderer mapped package units as if the package's centre were the view's. Its assertion that the two coincide caught it.
- **Alternatives, and why the values exclude them:**
  - the two builds disagree on the centre: excluded, both read 42,890,000 / −78,880,000;
  - the design geometry was built about another origin: excluded, it is the home.
- **The builds are unaffected:** they don't use the view. Only the renderer is wrong.

**Proposed fix (committed here; it runs only after a ruling).** `lake-render.py` → `6035c590519f`; the copy that ran is
`dc4bd32eef9f`. The diff is one block and one function:
- **the view is centred on the home;**
- **package rings go through the package's centre and its own cosine, then the home's view,** exactly as `Mapper::ToScreen` does;
- **the assertion becomes the true relation:** the two builds share the package centre, AND it is the home's 2-dp rounding.

Every other use was already in the home's frame and stays there: the truth masks, `--site` and the planted square.

**Proposed re-run:** the whole driver, in the frozen order, with no other change.
- The fetch's objects already exist, so step 0 re-verifies each one and fetches nothing.
- The builds are deterministic and repeat.
- This is the read's first re-run. A second instrument failure goes to the orchestrator and on to the owner.

**The ruling (the orchestrator, 23:53:19Z):**
- Yes: push and re-run the whole driver in the frozen order.
- Name the file and line the mapping was transcribed from.
- Stop the driver swallowing a child's traceback.

**Where the mapping comes from**, at the pinned `525cf7d1725d`, so it can be re-checked:
- **`src/weatherscope/WxBasemap.h:48`:** `c0_ = fmax(cos(lat0_ * (kPi / 180)), 0.01)`, the PACKAGE's cosine.
- **`:53-56`, in `Mapper::ToScreen`:**
  - `e = qx / 32767.0 * rmm_ / 1e6`, `s = qy / 32767.0 * rmm_ / 1e6`;
  - `lat = lat0_ - s / kKmPerDeg`, `lon = lon0_ + e / (kKmPerDeg * c0_)`;
  - `x = kC + (lon - homeLon) / rLon * kC`, `y = kC - (lat - homeLat) / rLat * kC`.
- **`src/weatherscope/WxRender.h:6`:** `rLat = R / 111.0`, `rLon = R / (111.0 * max(cos(home lat), 0.01))`.

**The driver no longer swallows a child's traceback.** `lake-read.py` → `2917187758b5`; the copy that ran is `eb2663a4807c`.
- On any child's failure, `stop` prints that child's last 15 stderr lines (a traceback, or wrangler's error) before it stops.
- Every failing branch passes it: the fetch, the builds, the renders, `--site` (a new check of its exit), `lake-junction` and
  plant_junction's build and render. Grading is untouched.
- **The control:** a planted child raising `ValueError("planted traceback")` printed its traceback, then `STOP`, with exit 1.

### The design read, re-run (2026-11-11, 23:55:14–23:55:24Z): stopped at plant_junction's assertion, the SECOND instrument failure

**The run.**
- It ran on the 23:53:19Z ruling, after `ec078b3ae264` was pushed and every artifact read as recorded.
- The driver exited 1, and its error file is empty. The log is scratch (`87183f506982`).
- The fetch re-verified all 192 objects and fetched nothing. The 12 builds repeated byte-for-byte in their reported figures, all rung 3.
- The renders ran, and the pictures exist (`lake-out/BUFFALO-{a,b,c}.png` and the plants'), as ruled.

**The stop.** At Buffalo, plant_junction's assertion read **FAILS**: of its 25 planted pixels, **0 were NE-only water on US land**.
- The site rule chose a US-land pixel inside the NE lake, on the border (0.00 km).
- **Per the frozen rule, no design is read.** The design lines the renders printed before the controls are in the log and are not
  read.

**The other two plants show the same, from figures the run printed:**
- **plant_spill** drew 49 rings and 14,969 water pixels, **exactly (b)'s**. Its 600 m square never became a ring.
- **plant_seam** read SEAM 3,040 against (b)'s 3,045, **no more**. Its 300 m band added no seam.

**The cause: the plants are smaller than the water the package draws.**
- Every build is rung 3, which draws water from the **ε × 8 set, simplified at about 1.6 km**.
- At that tolerance, Douglas–Peucker reduces a 600 m or 300 m square to under 3 vertices. The water path then counts it collapsed and
  never draws it.
- A 300 m band cut along the border is smoothed away in the same simplification.
- **I sized the plants against the 67 m pixel, not against the simplification the package actually draws.** The assertion (the
  orchestrator's addition) is what caught it, before any design was read.
- **The alternative,** that the plants never reached the builder, is not excluded by direct observation. The injection counts show
  each plant was written into a cell the build reads (plant_spill: 2 cells, against (b)'s 1). But the build output does not print
  `collapsed.water`, so "collapsed" is consistent with the data, not observed.

**What a fix would need (proposed, not applied; this goes to the orchestrator and on to the owner, as ruled for a second failure):**
- **Plants above the drawn tolerance:**
  - the spill square at least about 3.2 km (twice 1.6 km), so it survives as a ring;
  - the seam band at least about 3.2 km;
  - the junction square the same.
- **The plant_junction assertion restricted to its planted NE-only US pixels within 1.5 km of the border.** A square that large
  reaches past the band, so it would also hold SPILL_SHORE pixels.
- **A landing check on every plant build,** read before its control:
  - plant_spill draws more water than (b);
  - plant_seam draws less;
  - and `collapsed.water` printed beside each build.
- **A question this raises for the owner, beyond the instrument.** At rung 3 the package's water is simplified at about 1.6 km, roughly
  24 pixels at the closest zoom. Every water edge is then this coarse, TIGER's included, which bears on what "a clean lake at the
  closest zoom" can mean for this package.

## The design read's THIRD RUN, re-registered (frozen before it runs)

**The owner, with the cloud reviewer's read** (03:04Z, relayed by the orchestrator at 03:37:24Z), verbatim:

> - "Yes to the proposed fix as the third run."
> - "'One clean lake' is judged at the map's own resolution: no land drawn across open water along the border, and no seam or spill
>   bigger than the package's normal shoreline error. No extra water detail at the closest zoom just for borders."
> - The reviewer's read of the current Buffalo pictures: "(a) has no gap in the lake, with water over parts of the US shore within that
>   error; (b) has a land stripe across Lake Erie along the border, which fails; (c) has smaller amounts of both."
> - "Final pick after the third run's pictures."

**The reviewer** (03:39Z, relayed at 03:40:26Z), verbatim:

> the stripe check needs a control in both directions. It must flag the current (b) picture. It must also pass the same picture with
> the stripe painted over as water, so it can't pass by flagging everything.

The orchestrator's terms: both run before any design is graded; either one off means the instrument is blind, and nothing is graded.
This is the THIRD run, so a failure here goes straight to the orchestrator and the owner.

### The plants, above the drawn tolerance

Every build so far is rung 3, which draws water from the ε × 8 set, simplified at about 1.6 km.
- **plant_seam:** (b) minus a **3.5 km** band along the border.
- **plant_spill:** (b) plus a **3.5 km** square on US land. It adds exactly 12.25 km² at each home, so it lies wholly on new ground.
- **plant_junction:** (b) plus a **3.5 km** square at its frozen site (the 23:40Z rule, unchanged).

**Designs (a), (b) and (c), and the truth layers, are byte-for-byte as in the second run,** checked by comparing the regenerated
geometry with the second run's. Only the plants changed: at Buffalo plant_seam is 213.31 km² and plant_spill 308.31; at Rouses Point,
59.72 and 90.22.

**A landing check per plant build, read before its control:**
- plant_spill and plant_junction draw MORE water than (b), and plant_seam LESS;
- `collapsed.water` and `dropped.water` are printed beside every build.

**The junction assertion is restricted to the band:** the planted NE-only US pixels within 1.5 km of the border must number at least 1,
and every one must be JUNCTION or JUNCTION_DISC, with none EXCUSED.

### The owner's pass rule, made measurable

**E, the package's normal shoreline error,** from the SAME package, away from any border, computed per lake from the "none" build
before any design is graded:
- **W_src** is TIGER's SOURCE water, decoded from the store's own cells, with the water path's population: AWATER ≥ 1,500,000 m², outer
  parts only, each part filled on its own, and **unsimplified**.
- **The offsets,** pooled both ways: each edge pixel of TIGER's drawn water (W_n) to the nearest edge pixel of W_src, and each edge pixel
  of W_src to the nearest of W_n. Only edge pixels more than **3.0 km** from NE's border line count.
- **The statistic is the 95th percentile (P95).** P50, the max and the sample count are printed beside it. **Under 50 samples, stop.**

**The three conditions:**

| condition | measured as |
|---|---|
| **(a) no land across open water along the border, whatever its size** | **CROSS = 0.** The border line is sampled every 0.1 km. At each sample, a transect runs along the line's normal from **−4.0 to +4.0 km** in one-pixel steps, kept only if both its ends lie in the NE lake (open water). A transect CROSSES when, after a drawn-water pixel, a run of land pixels all inside the NE lake ends at drawn water again |
| **(b) spill no bigger than the normal shoreline error** | **SPILL_EXTENT ≤ E:** the largest distance from a SPILL pixel to TIGER's drawn water |
| **(b) seam no bigger than the normal shoreline error** | **SEAM_WIDTH ≤ E:** twice the largest distance from a SEAM pixel to the drawn water |

- **The transect is 4.0 km each way, not the 1.5 km band,** so that a stripe wider than the band (the 3.5 km seam plant's 1.75 km on the
  Canadian side) still shows water on both sides.
- **A design passes when all three hold at BOTH lakes.** Passing designs are **ranked simplest first: (a), (c), (b).**
- **The rule ranks; it does not replace the owner's pick.** The pictures go to him whatever the numbers say.

**The stripe check, controlled in both directions** (the reviewer):
- **STRIPE** is the connected components (8-connected) of land inside the NE lake within 4.0 km of the border that contain a land pixel
  of a crossing transect. **CROSS_REPAINTED** is CROSS recomputed with STRIPE repainted as water.
- **At Buffalo:** CROSS(b) > 0, since it must flag (b) as it stands, AND CROSS_REPAINTED(b) = 0, since it must pass (b) repainted.
- **At Rouses Point:** if CROSS(b) > 0 there, CROSS_REPAINTED(b) = 0.
- The seam plant gets the same pair at each home: CROSS > 0 and CROSS_REPAINTED = 0.

### The order, and every control first

1. **The fetch** re-verifies the 192 objects. All exist, so nothing is fetched.
2. **The builds,** with `collapsed.water` printed: none, (b), plant_seam, plant_spill, (a), (c), then plant_junction.
3. **E per lake,** printed before anything is graded.
4. **The renders.** The pictures are written here.
5. **THE CONTROLS, all before any design is graded:**
   - the landing checks;
   - the seam plant: SEAM up, SEAM_WIDTH > E, CROSS > 0, CROSS_REPAINTED = 0;
   - the spill plant: SPILL up, SPILL_EXTENT > E;
   - the junction assertion, then JUNCTION + DISC up;
   - the stripe in both directions.

   **Any one off: blind, and nothing is graded.**
6. **The pass rule and the ranked table:** per lake and design, SEAM, SPILL_SHORE, SPILL_JUNCTION, JUNCTION_DISC, EXCUSED, CROSS,
   SPILL_EXTENT, SEAM_WIDTH and E.
7. **The pictures to the owner, who picks.**

| outcome | next |
|---|---|
| a fetch, build or render failure, or E under 50 samples | **instrument failure: to the orchestrator and the owner** (the third run) |
| **a control blind** | **nothing is graded: to the orchestrator and the owner** |
| the rule ranks one or more designs | the ranking and the pictures go to the owner, who picks |
| none passes at both lakes | the numbers and the pictures go to the owner, who picks |
| **anything else** | **stop and report** |

**My expectation, stated before the run; it grades nothing.**
- From the reviewer's read, (b) fails CROSS at Buffalo, which the stripe control also requires.
- For (a) and (c), the outcome rests on E, which I don't predict. At rung 3 the shoreline itself is simplified at about 1.6 km, so E
  should be of that order.

**The tooling:**
- `lake-designs.py` `f5d8a23e5867` → `lake-BUFFALO.json` `bd95d4d989a4` and `lake-ROUSES.json` `a3b10c04114e`;
- `lake-junction.py` `4df647cac294`;
- `lake-build.mts` `7e887ec58946`, which prints `collapsed.water` and `dropped.water`;
- `lake-render.py` `2609269edafc`, with E, CROSS, the stripe repaint and the extents;
- `lake-read.py` `05dc55cadd47`;
- `lake-fetch.mts` `e56c9e06aca0`, unchanged.

The second run's copies are kept as `*.ran2.*`. Every script compiles, and the build refused an unknown home after its imports. Nothing
has run.

### Amendment (the orchestrator's 03:46:31Z ruling): like with like

**The ruling, verbatim:**

> ONE CHANGE before it runs: compare LIKE WITH LIKE. As written, SPILL_EXTENT and SEAM_WIDTH are MAXIMA, and E is a P95. The package's
> own normal shoreline has a max larger than its P95, so a join no worse than an ordinary shore would fail. That's stricter than the
> owner's "no bigger than the package's normal shoreline error". Make it two conditions, both printed:
> - SPILL_EXTENT_P95 ≤ E_P95 and SEAM_WIDTH_P95 ≤ E_P95 (the typical join no worse than the typical shore);
> - SPILL_EXTENT_MAX ≤ E_MAX and SEAM_WIDTH_MAX ≤ E_MAX (the worst of the join no worse than the worst ordinary shore).
> CROSS = 0 stays absolute.

**As applied (it supersedes the extents in the pass rule above):**
- **SPILL_EXTENT** is the distribution over SPILL pixels of the distance to TIGER's drawn water. **SEAM_WIDTH** is the distribution over
  SEAM pixels of twice the distance to the drawn water. Each is printed as its **P95 and max** (0 when empty).
- **E** keeps its pooled shoreline offsets, now used as **E_P95 and E_MAX** (both already printed by `--e`).
- **The pass rule, per lake:**
  - CROSS = 0 (absolute);
  - SPILL_EXTENT_P95 ≤ E_P95 and SEAM_WIDTH_P95 ≤ E_P95;
  - SPILL_EXTENT_MAX ≤ E_MAX and SEAM_WIDTH_MAX ≤ E_MAX.

  Both lakes, as before.
- **The plant controls follow the rule.** At each home, plant_seam must FAIL the seam condition (SEAM_WIDTH_P95 > E_P95, or
  SEAM_WIDTH_MAX > E_MAX), and plant_spill must FAIL the spill condition the same way. That shows each condition can fail. Every other
  control is unchanged.
- **The tooling:** `lake-render.py` `5d4de32615ac` and `lake-read.py` `3c4313a9ee5d`. The copies before this amendment are kept as
  `*.pre0347.*` (`2609269edafc` and `05dc55cadd47`). Both compile, and no reference to the old single extent remains. Nothing has run.

### The third run, read (2026-11-12, 03:48:49–03:49:15Z): controls blind at Buffalo; NOTHING IS GRADED

**The run.**
- It ran on the orchestrator's 03:48:30Z go. Every artifact read as registered, C: had 72 GB free, and no Cloudflare token was set.
- The driver exited 1, and its error file is empty. The log is scratch (`d92b70d4ebe1`).
- The fetch re-verified all 192 objects and fetched nothing.

**Read as specified:**
- **All 14 builds,** every one rung 3, with `collapsed.water` printed: Buffalo 18 in every build, Rouses Point 91.
- **E, before any grading:**
  - Buffalo: P95 **1.060 km**, max 1.336, P50 0.335, from 297 samples;
  - Rouses Point: P95 **1.081 km**, max 1.317, P50 0.201, from 582 samples.
- **The landing checks see** at Buffalo: each plant now changes the drawn water as it should. **The spill controls see.**

**BLIND at Buffalo, so per the frozen rule nothing is graded:**

| control | read | why it is blind |
|---|---|---|
| **the stripe: CROSS(b) > 0** | 0 | **No transect was kept at Buffalo, in any build (0).** There the lake is its narrow eastern tip, and a ±4 km transect across the border always ends on land, so "both ends in the NE lake" removes every one. The check cannot see Buffalo at all. **The alternative,** that (b) has no stripe there, doesn't hold: (b) leaves 3,045 SEAM pixels against the border |
| **seam: CROSS(plant_seam) > 0** | 0 | the same: 0 transects |
| **seam: SEAM(plant_seam) > SEAM(b)** | 65 against 3,045 | SEAM counts land inside the closing of the drawn water, and the closing (1 km radius) bridges only gaps under 2 km. The 3.5 km band opens a wider gap, which by that definition is not a seam, so SEAM falls |
| **seam: plant_seam fails the seam condition** | P95 0.569 < 1.060, max 0.671 < 1.336 | the same: the gap the plant made is not counted as seam |
| **junction: the assertion** | 1,168 planted pixels in the band: 225 JUNCTION, 0 DISC, **943 EXCUSED** | the site rule puts the square inside the NE lake at the border, and only pixels within 0.5 km of TIGER's shoreline count as junction. Most of a 3.5 km square therefore falls in the open-water strip, which the definition excuses |

**What the three share:** the measure's scales were set for pixel-sized features (a 1 km closing, a 0.5 km near-shore distance, ±4 km
transects with both ends in the lake). The plants now have to be 3.5 km to survive the package's ~1.6 km simplification, and Buffalo's
lake is narrow. **The plant sizes and the measure's scales conflict.** No parameter was changed after the run.

**Checked read-only, to name the cause rather than guess it.**
- The store does hold Lake Erie near Buffalo: one TIGER water part of **447.6 km²** (1,167 vertices, outer, kept by the water path),
  among 8 water parts within 20 km. **So TIGER's lake exists to join, and the instrument is what fails, not the data.**
- My first guess, that the store has no lake there, came from E's count of 2 source features in the 8 km view. It was wrong and is
  dropped.

**The pictures** were written before the controls, as ruled. Copied unopened to `C:\Github\_handoffs\map-server\lake-out-run3\`: 24
files, the 12 PNGs and their JSON sidecars at both lakes, every one a sha256 MATCH. Buffalo's (a), (b) and (c) are byte-identical to the
second run's (the designs and the mapping are unchanged). The plants differ.

**This is the third run, so it goes straight to the orchestrator and the owner.** No fix is proposed here. The conflict is between what
the owner's rule has to see (km-scale joins at rung 3) and the scales the measure was built with, and choosing those scales is his call.

## The lake join, PICKED: design (a) (the owner, 03:56Z). The design read is CLOSED

**The owner, in his words** (relayed by the orchestrator at 03:59:49Z):

> "Lake join: (a), chosen from the third run's pictures at Buffalo and Rouses Point. I accept (a)'s cost from the pictures: lake water
> drawn over the US shore near the border, about the size of the package's normal shoreline error (P95 about 1.1 km at the closest
> zoom). The code PR asserts (a)'s property directly: no land drawn across a border lake. No fourth instrument. If a later picture shows
> spill clearly beyond the normal error, it comes back to me."

**The context he judged against,** measured in the third run before any grading, from the package's own "none" build, away from the
border:
- Buffalo: P95 **1.060 km**, max 1.336;
- Rouses Point: P95 **1.081 km**, max 1.317.

The pictures are in `C:\Github\_handoffs\map-server\lake-out-run3\`.

**The lake-join design read is CLOSED.** There is no fourth run. Its three runs and their stops stay on record above.

### Part 2's lakes, re-registered on the pick (before any code)

**1. Design (a).**
- **Selected:** Natural Earth 10 m lakes over **1.5 km²** that are **not wholly inside the US**, decided by the containment test against the
  pinned admin_0 USA record.
- **Drawn WHOLE:** no polygon difference, and no cut at the border. They are cut only as every sea feature is, by the 1° level-0 grid at
  ingest (the 22:46Z ruling), and by the build square in the builder.
- **A lake wholly inside the US is not stored.** TIGER draws it.
- The threshold and the US cut are the owner's, ruled at 23:21Z.

**2. KB7, the structural assertion: no land drawn across a border lake.**

*Where it lives.* In the package, a lake's rings are anonymous: they merge with TIGER's water, and the sea layer is cut into 1° pieces.
So nothing in the package says which rings belong to which lake, and a package-level check would need thresholds tuned on data, which
would be a fourth instrument. **The ingest is the last stage that still knows each lake's pieces, so KB7 is asserted there,** and it is
exact.

| KB7 | where | asserted |
|---|---|---|
| **KB7-a** | CI, on a synthetic sea-layer store: one lake crossing a synthetic border line, inside one 1° cell, with a synthetic US polygon; and one lake wholly inside that polygon | the border lake's stored pieces, together, equal the lake (area equal within 1e-9 relative, and the same vertex set). The lake wholly inside the US has no stored piece |
| **KB7-b** | the code PR's ingest dry run, on real data | **Lake Erie and Lake Champlain** are both selected. Each one's stored pieces, together, equal the NE lake within the extent's cells, area within 1e-9 relative |
| **the control,** run first, for both | the same check on a (b) ingest: the lake minus the US polygon | **must FAIL,** short by the US part. It is run before KB7 is believed. If it passes, KB7 is blind, and nothing is believed |

- ~~Why that is the property. KB7-b holds at the ingest, and the builder draws every stored sea piece like other water (QW4-2's rows).~~
  **Withdrawn (the orchestrator, 04:02:26Z):** that sentence is a justification about code KB7 does not touch, the builder. A stored lake
  piece the builder drops leaves land across the lake, and the ingest can't see it: refused by the backstop, collapsed by
  Douglas–Peucker, or dropped by the ladder. QW4-1 showed the backstop refusing real rings. **KB7-c, below, replaces it with a check that
  runs.**

**KB7-c, at the builder: the last stage that knows which INPUT piece each ring came from.**

| KB7-c | where | asserted |
|---|---|---|
| **the count** | every build | **the builder reports the sea and lake input pieces it did NOT draw**, by reason: refused by the backstop, collapsed under 3 vertices, or dropped by the ladder (rung 4's smallest-first drops). It reports them as `seaRefused`, `seaCollapsed`, `seaDropped`, and their sum `seaLost`, on `bm_build` beside `refused` and `refusedBuilt`. The measurement child also reads each piece's fate, by its source key |
| **KB7-c-real** | the code PR's run, at Buffalo and Rouses Point | **`seaLost` = 0 for every piece of a border lake in view:** within the device's closest zoom, 8.04672 km of the home. Any lost piece stops, and its reason and key are reported |
| **KB7-c-ci** | CI, on a synthetic sea-layer store | a normal lake piece: `seaLost` 0. **The control, run first:** a planted lake piece the backstop must refuse, a 4-vertex bowtie as in QW4-3, reads **`seaLost` ≥ 1, with `seaRefused` ≥ 1.** If it reads 0, KB7-c is blind, and nothing is believed |

- **End to end:** KB7-a and KB7-b prove each border lake is stored whole. KB7-c proves the builder draws every piece of it in view. So
  "no land drawn across a border lake" holds by checks that run.
- **The owner's eye covers only the cost he accepted:** where the drawn shoreline sits, within about the package's normal shoreline
  error.
- **`seaLost` is a new `bm_build` field.** Its routine reader is #655's: the staging rows read it, and #655 proposes the response header
  `smoke-prod.sh` would read. It is added to #655's list.

**3. The owner's condition, as a named outcome.**

| outcome | meaning | next |
|---|---|---|
| **a later picture shows spill clearly beyond the normal error** (about 1.1 km P95 at the closest zoom) | (a)'s cost is larger than he accepted | **it goes to the owner,** in his words: "it comes back to me" |

**4. The rest of part 2** is as planned and follows separately, with its own sha before anything runs: the sea's storage (the 1°
grid at ingest, the extent of 2,584 cells), its rows, and the ingest's dry run with the R2 figures.

**Part 2's first figure, for its record: the extent is 2,584 level-0 cells** within 250 km of a covered point (`sea-extent.py`,
`3334b52116ec`).
- coverage.bin has 4,907 rows and 10,270 covered segments; the extent spans latitude 16 to 73.
- The control: all 1,397 of the store's cells fall inside it.
- Up to 1,187 more cells could be sea-only. How many hold sea comes from part 2's dry run.

## Part 2: the sea's storage, PROPOSED (for acceptance before the prototype is built)

The code PR inherits the cell format, the feature kind and how islands are stored. So they are proposed here first, and the prototype is
built only on acceptance.

**1. The sea layer's source.**
- Natural Earth 10 m **ocean**, pinned and verified (sha256 `db626fcd5d50`, the manifest's pin).
- The **lakes of design (a)** (ruled 03:56Z): Natural Earth 10 m lakes over 1.5 km² not wholly inside the US, decided by the containment
  test, each WHOLE.

**2. Cut at ingest to the 1° level-0 grid** (the 22:46Z ruling), over the **extent's 2,584 cells**.
- Each source polygon's outer ring and each of its holes is clipped to each cell's box with Sutherland–Hodgman, as the water layer clips.
- A hole crossing the box's edge becomes a ring along the edge, which even-odd still reads as land.
- A cell's piece is empty when nothing of its outer remains. A cell wholly inside a hole (land) stores nothing.

**3. Stored as a NEW feature kind, `sea`, kind code 6, in the existing cell format.**
- **One record per piece. Part 0 is the piece's outer ring; parts 1..n are its holes (islands),** following the store's existing
  (record, part) convention.
- **Only islands of at least 1.5 km² are stored.** Clipping can only shrink an island, so a smaller one could never pass the builder's
  threshold.
- **Attributes:** the source (ocean, or the lake's NE name) and the piece's area. No AWATER filter applies: the lake's threshold was
  applied at selection.
- **Its file is the pinned zip** (ocean or lakes), under the manifest's new **sea** layer, which gives the new store id.

**4. The builder's sea path** (#616's order, unchanged):
1. collect each record's parts;
2. clip the outer and the holes to the square;
3. drop the holes now under 1.5 km²;
4. **tp, then quantise, then bridge**: each hole through its nearest visible vertex, largest first, crossing-tested, adding 2 vertices
   each.

The result is one ring per piece, joining the water section and following the ladder like other water. An island that can't be bridged
is dropped and counted on `bm_build` as `islandsDropped` (the 22:46Z ruling). KB7-c's `seaLost` counts the pieces not drawn.

**5. What the dry run reports, stated in the addendum before anything uploads:**
- the cells holding sea, the sea-only cells (new to the index), the pieces, the islands kept, and the encoded bytes;
- the store's put count (every object under the new store id) and its bytes;
- **the R2 cost:** Class A puts on top of this month's OPERA writes, past the free million (the 23:21Z ruling), at $4.50 per million,
  against the owner's overage allowance;
- **the account's total R2 storage after the upload,** with the old store kept for rollback, against the 10 GB free tier, and the
  monthly cost if over. It is read from the account's analytics with the read-only token file, `analytics-read`, as a count only.

**Three choices in this proposal are mine, for the orchestrator's reading:**
- (i) **the new kind code 6 in the cell format.** `decodeFeature` throws on an unknown code (`proxy/src/basemap/store.ts:58-59` at
  `525cf7d1`), so a builder older than the code PR fails on the new store rather than misreading it. That is safe, but it fixes the
  order: **the builder deploys before `current` names the new store;**
- (ii) **islands of 1.5 km² and up stored at ingest,** and checked again after the builder's square clip;
- (iii) **one bridged ring per piece**, rather than one ring per source polygon across cells. A union is rejected (22:46Z), so pieces stay
  per cell, and S-KB-SEAM checks their shared edges.

### Accepted (the orchestrator, 04:12:32Z), with two conditions: the reader sweep, and the order both ways

(i), (ii) and (iii) are accepted. **Condition 1, every reader of the store's feature format,** read on main at `e3c2508eb8db`. It covers
everything that decodes cells, or reads cells, the manifest, the store id or packages, outside the docs:

| reader | what it reads | sees code 6? | what it does today | in the code PR |
|---|---|---|---|---|
| **the builder** (`build.ts:103` → `decodeCell` → `decodeFeature`, `store.ts:54`, `:143`) | every cell it builds from | **yes** | **throws** (`store.ts:58-59`) | **updated in the same change:** the kind, and the sea path |
| **the ingest's cell splitter** (`ingest-basemap.ts:298`, `decodeFeature`) | a cell over 24 MB, split into quarters | **yes,** if a sea cell is split | **throws** | **updated in the same change** (it shares `store.ts`'s codec) |
| `ingest-lib.ts` (re-exports the codec; `filterRecord`, `featuresFrom`) | features as it writes them | the sea is written by its own path | n/a | `FeatureKind` gains `sea` |
| `verifyStore` (`ingest-lib.ts:245`) and `d1.ts` | objects as bytes and sha256 | no | compares bytes; never decodes | unaffected |
| `basemap-check.ts` | a PACKAGE (BMPK), `current`, the manifest | no (packages carry sea as water rings) | checks PROVENANCE's files against the manifest | **re-checked** once a sea layer exists: it reads the manifest per layer |
| `basemap-md3.ts` | writes cells, as a fixture | writer only | n/a | unaffected |
| the proxy (`handler.ts:81`) | **the store id, from KV `bm:store`**, never cells | no | keys packages by store id | unaffected; it is in the ORDER below |
| tests: `basemap-ingest.test.ts` (MI-roundtrip), `basemap-synth.ts` | the codec | MI-roundtrip lists its kinds explicitly | passes, with no `sea` case | **add a `sea` case;** `SynthFeature.kind` gains `sea` |
| **Weatherscope** (`tools/weatherscope/**`), MG0b | its own sources, and the stored Bend PACKAGE | **no**: no cell reader on main (no `BMSC`, `decodeFeature` or `cells/` in `tools/`) | MG0b compares PROVENANCE lines | unaffected. A sea layer adds PROVENANCE lines only where it is read, and Bend's manifest has none |
| the firmware (C++) | packages (BMPK), never cells | no | the package format is unchanged | unaffected |
| the package cache (`bm/v1/pkg/<storeId>/…`) and its lifecycle rule | packages, keyed by store id | no | a new store id rebuilds every package | stated already |
| the dashboard | nothing of the store | no | n/a | unaffected |

**No reader throws on the new store unless it is updated in the same change:** the builder and the cell splitter both are. The sweep
also turned up a second "current", which the order has to cover.

**Condition 2, the order, both ways.** The builder reads the store id from R2's **`bm/v1/current`**. The proxy reads it from KV's
**`bm:store`**, which `publishStore` writes after `current` (MI8).

| direction | the steps, in order |
|---|---|
| **FORWARD** | 1. the builder that knows code 6 deploys (staging, then production) → 2. the new store uploads (write, verify, repair, verify) → 3. R2 `bm/v1/current` names it → 4. KV `bm:store` names it (`publishStore`, MI8) |
| **ROLLBACK, REVERSED** | 1. KV `bm:store` back to the old store id → 2. R2 `bm/v1/current` back to the old id → **3. only then may the builder roll back.** An old builder can't read code 6, so rolling it back while `current` names the new store makes it throw on every build |

- The old store's objects stay in R2 for exactly this. The dry run's report states its size.
- **Both orders are printed in the dry run's report** as ruled, beside the put count and the R2 figures.

**On the orchestrator's go (04:12:32Z):** pq4d, the prototype with the sea path, and the ingest dry run are built next. The dry run is
read-only on R2, and nothing uploads. Part 2's rows then come as one frozen registration before anything runs.

### Part 2's third home, and the smoke flag (the reviewer, via the owner, 04:06Z; relayed 04:15:42Z)

**The read-only check, first as ruled** (`check-reservoirs.py` `9ae9982e0ff6`, over the pinned `ne_10m_lakes` `0803a06f9c3c`: all
1,355 records, every text field):

| reservoir | in NE? | area | crosses the US border | centroid (2 dp) |
|---|---|---|---|---|
| Amistad | **yes**, "Amistad Reservoir" | 250.1 km² | yes (not wholly inside) | (29.53, −101.13) |
| Falcon | **yes**, "Falcon Lake" | 465.9 km² | yes | (26.78, −99.26) |

**Both are in NE and both are selected by design (a),** so the ruled stop ("if it isn't in NE") does not apply.

**The third home is FALCON LAKE AT ZAPATA, (26.91, −99.27).**
- Zapata sits on the reservoir's shore, and the border runs through the lake within the closest zoom's 8 km, so its picture shows the
  join. Del Rio is about 20 km from Amistad, so its closest zoom wouldn't show the reservoir.
- Zapata is covered in `coverage.bin` (checked read-only).
- It joins KB7-b, KB7-c-real and the code PR's pictures.

**The smoke flags `seaLost` everywhere** (added to #655, comment 6814357889).
- `seaLost` and its reasons go on the response header on every build.
- `smoke-prod.sh` prints them and FLAGS any non-zero value.
- **The flag's control:** a planted `seaLost` ≥ 1 must make the smoke flag it. It is registered with the PR that builds the smoke.
- A cache hit with no header reads "unknown", never zero.

### Part 2's ingest DRY RUN, read (2026-11-12, 04:17:39–04:17:42Z): read-only, nothing uploaded

`sea-dryrun.py` (`107997098348`), log `2155216d5e4e`, pieces `sea-pieces.json` `8ad5d92fa7c7`. Exit 0, and its error file is empty.

| | read |
|---|---|
| **the extent, recomputed** (the control) | **2,584 cells, as recorded.** All 1,397 index cells fall inside it |
| the sources | the ocean: 2 outer polygons, 6,820 holes. NE lakes: 1,355 in all; 149 wholly inside the US (not stored); 38 under 1.5 km². **56 lakes reach the extent, in 273 pieces:** Erie 16, Champlain 5, Falcon 2, Amistad 2, Ontario 14, Superior 32, Huron 36 |
| **the cut** | **1,435 cells hold sea**, of which **981 are sea-only** and new to the index. **1,914 pieces** (1,641 ocean, 273 lake). Islands: 497 kept, 135 dropped (under 1.5 km²). **Sea bytes 1,190,173,** the largest cell 9,252. **No cell is newly pushed over the 24 MB split** |
| **the new store** | **2,378 cell objects** (1,397 + 981) + 9,638 terrain tiles + 5 small objects = **12,021 puts.** About **1.762 GB:** cells 1,050,294,267 B, tiles 706,629,891 B (the manifest's sizes), small objects about 4.9 MB |
| **the order, both ways** | printed as recorded above: FORWARD builder, store, `current`, `bm:store`; ROLLBACK `bm:store`, `current`, then the builder |

**One change from the accepted proposal, flagged for ruling.** The dry run cuts each source with a BOOLEAN rectangle clip (GEOS
`clip_by_rect`, pre-tiled at 10°), not Sutherland–Hodgman per ring.
- For a 446,789-vertex ocean over 2,584 cells, that is robust and fast (3 s).
- The only difference: a hole crossing a cell's edge becomes a notch in the piece's outer ring, not an edge-coincident ring. The pieces
  are then valid polygons, and bridging never meets a hole lying along the box edge.
- **The code PR uses whichever is ruled,** and the pieces' counts are re-read on it.

**The R2 figures,** read at 04:18:46Z with main's own read-only `proxy/scripts/r2-class-a.ts` (the token file's `analytics-read`; counts
only, never the token):
- **A CORRECTION TO THE 23:21Z PREMISE, measured, not assumed.** This month's Class A operations to date are **331,840 of the free
  1,000,000**, projected to **890,472** by month's end at this rate. **The free million has NOT been used.** So the sea load's 12,021 puts
  fit inside it at the current rate (about 902,000). If the month's writes accelerate past the million, they cost at most **$0.054** (at
  $4.50 per million), inside the owner's overage allowance either way.
- **Storage:** the account holds **3.452 GB** today. `blipscope-basemap` is 1.761 GB in **11,040 objects, the same 11,040 this file
  counted from the index and the manifest.** With the old store kept for rollback and the new one added (about 1.762 GB), the total is
  **about 5.21 GB, under the 10 GB free tier: $0 a month.**
