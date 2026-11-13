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

## The addendum, part 2: the sea's rows, registered (frozen before they run; the tooling's sha256s follow in a pinning addendum)

**The prototype is pq4d**, built and self-checked as tooling; nothing graded has run.
- `mk-pq4d.mjs` (`640d557d426e`) is `mk-pq4c.mjs` plus `mk-pq4d-sea.part.mjs` (`ff7043ec19e2`). Every anchor matched exactly once.
- `sea.ts` (`026da6c00aa4`, byte for byte the part file) holds the sea path. `store.ts` (`ae746468ba65`) gains kind `sea` = 6, and
  `provenance.ts` (`21410811e7f9`) gains the `sea` layer after `water`.
- `layers.ts` (`2c88da1f3b58`) keeps and draws the sea parts. `build.ts` (`06773fa5dcd1`) routes them to the ε, × 2 and × 8 water sets,
  and `budget.ts` (`e5eb9c2c2cbe`) carries the drawn set's sea figures.
- The self-check (`pq4d-selfcheck.mts` `5fa0b7b410d7`): the modules import, and a kind-6 feature round-trips through the feature and cell
  codecs.

**The sea path, per piece** (#616's order):
1. clip the outer and the holes to the square;
2. drop holes now under 1.5 km²;
3. tp, the backstop and quantise, through the water path's own function;
4. bridge each hole, largest first, through the nearest (hole, ring) vertex pair whose segment properly crosses no edge, 2 vertices per
   bridge;
5. the backstop on the bridged ring.

Each piece's FATE (drawn, refused, collapsed, outside, or dropped by the ladder) is reported by its source key, with the drawn set's
counts and the sea's exact package bytes.

**Plants,** behind `Q4_SEAPLANT`:
- `nobridge`: the islands sent as their own rings;
- `cross`: the nearest pair taken without the crossing test, and the bridged ring's backstop skipped, so that KB2 sees the crossing
  rather than the backstop hiding it;
- `order`: bridge before simplifying;
- `seam`: odd-numbered pieces moved by **13 quanta**, about one closest-zoom pixel.
  - **A change from #616's "one quantum", flagged:** one quantum is about 5 m against the closest zoom's 67 m pixel. Sub-pixel, it would
    flip almost no pixel, which is the plant-size lesson of 23:55Z.

**Step 0, before any row: the inputs.**
- **Zapata's** cells and tiles are fetched READ-ONLY from `blipscope-basemap` (store `4c0875738f38`) into scratch `zap-obj`, exactly as
  `lake-fetch.mts` did: cells by byte count against the index, tiles by sha256 against the manifest. Nothing is written to R2.
- Then **`mk-sea-store.mts`** (`1eb7e96e1d99`) builds the scratch store `sea-obj` for the 16 homes: the 13 points, Buffalo, Rouses Point
  and Zapata. It takes:
  - the dry run's pieces as kind-6 features, under two scratch sea files that sort last;
  - each touched cell re-encoded to its own bytes first;
  - the sea-only cells, two sea regions, and a new manifest and `current`.

  Any needed cell or tile that no local source holds stops it.

**The rows.** Knobs, unless stated: `Q4_LADDER=b`, `Q4_D2=1`, `Q4_TP=1`, `Q4_PROV=1`, `Q4_SEA=1`.

| row | what | predicted |
|---|---|---|
| **P2-0** (inert) | pq4d with `Q4_SEA` and `Q4_PROV` unset, B's knobs, on `bm-obj` (no sea layer), at the 13 | **byte-identical to QW4-1's 13 shas** |
| **T-SEA0-real** (no sea layer, no sea) | pq4d with `Q4_SEA=1`, against `Q4_SEA` unset, both with `Q4_PROV=1`, on `bm-obj`, at the 13 | **byte-identical, pair by pair** |
| **T-SEA0-ci** | a synthetic store with no sea layer, and the same store with one added | without: no sea ring, no sea PROVENANCE lines. **The control: with the layer,** the package draws the sea's rings and reads the sea layer |
| **P2-1** (QW4-2) | the full knobs on `sea-obj`, at the 16 | every package `kOk`; **0 proper crossings in every ring** (bridges are contacts). The 9 fit at 162 km and New York at 129.6 km, as QW4-1 (else a VISIBLE COST, named to the owner, not a stop). Each home's sea fates, `islandsDropped` and sea bytes are recorded |
| **KB2** | P2-1's sea rings | **0 proper crossings** (the row above). **Its plant (S-KB-CROSS)** is run on the KB1 store with a hole whose nearest pair crosses another hole: KB2 must read ≥ 1 |
| **KB3** | P2-1 at KBOS, KJFK, KLGA, KSEA, KSFO and KMIA | **sea bytes ≤ 3,072** in each package (the format's own varint cost of the drawn sea rings) |
| **KB1** | a synthetic sea square with 3 islands, one touching the square's edge, through pq4d's sea path | the drawn ring rasterised by FillRing's rule against a reference raster of the same quantised outer and holes by even-odd, unbridged, at 240 × 240, at view radii r, r/2 and r/4 with 8 rotations: **0 differing pixels in all 24.** **Controls:** S-KB-NOBRIDGE differs by at least each island's pixel area; S-KB-ORDER differs (a bridge with width) |
| **KB4** (terrain, an independent truth) | P2-1's sea rings at each coast airport (KBOS, KJFK, KLGA, KSEA, KSFO, KMIA), on the 162 km level's grid (242 px) | **at least 95%** of the mask's pixels read ≤ 0 m from the store's own terrain, AND the mask covers **at least 90%** of the 0 m pixels outside TIGER's water. **The control:** the mask moved 2 km inland must fail at least one |
| **KB5** (the device's code) | Weatherscope's C++ `FillRing` (`WxBasemap.h`), built on the host (WSL Ubuntu 24.04), drawing KJFK's P2-1 package's water | **0 differing pixels** against the Python FillRing-rule renderer on the same rings. **The control:** one ring's vertex moved one pixel must differ |
| **KB8** (the seam, new: KB5 compares renderers and can't see a seam) | P2-1 at KJFK and KBOS, at the closest zoom, along every 1° cell edge crossing the source sea | **0 undrawn pixels within 1 px of a cell edge where the pixels on both sides are drawn sea.** **The control:** S-KB-SEAM (13 quanta) must read ≥ 1 |
| **KB7-a** | a synthetic lake crossing a synthetic border, inside one cell, and one wholly inside the synthetic US, through the dry run's cut | the border lake's pieces together equal it (area within 1e-9 relative); the inside lake has none. **The control:** a (b) cut, the lake minus the US, must fail |
| **KB7-b** | the dry run's pieces | **Erie, Champlain, Falcon and Amistad** are selected, and each one's pieces together equal the NE lake within the extent's cells (1e-9 relative). **The control:** (b) geometry must fail |
| **KB7-c-real** | P2-1 at **Buffalo, Rouses Point and Zapata** | **every piece of a border lake intersecting the closest-zoom view (8.04672 km) is DRAWN**: no refused, collapsed or dropped. **The control (CI-style, synthetic):** a 4-vertex bowtie lake piece reads `refused` ≥ 1 |
| **P2-M** (the sea's memory) | pq4d on `sea-obj` with and without `Q4_SEA`, at the 16, with QW4's memory instrument (one process per call, `--expose-gc`, the controls, settled samples) and its 8 MiB plant control | **RECORDED** as differences per point and phase. They become the code PR's sea-memory predictions, ± 0.785 MiB. **None over 96 MB** (a stop). The plant must read +8 ± 0.785 in the cell phase, or the instrument is blind |

**Pictures:** the closest zoom at Buffalo, Rouses Point and Zapata from P2-1, written for the owner, as ruled at 03:56Z.

**THE PINNING ADDENDUM (2026-11-12, 04:44:54Z): every artifact, and four changes from the rows above, for ruling before anything runs.**

| artifact | sha256 | what it is |
|---|---|---|
| `mk-pq4d.mjs` | `78f75e2e12f9` | pq4c's generator plus `mk-pq4d-sea.part.mjs` (`0d7bf6e74312`) |
| pq4d `sea.ts` | `4b46e0ece2fe` | the sea path, byte for byte `pq4d-sea.part.ts`. It now also keeps each drawn piece's unbridged quantised outer and islands, KB1's reference |
| pq4d `build.ts` / `budget.ts` / `layers.ts` / `store.ts` / `provenance.ts` | `f50fe805558f` / `e5eb9c2c2cbe` / `2c88da1f3b58` / `ae746468ba65` / `21410811e7f9` | `build.ts` also reports the sea rings' positions in the water section |
| pq4d `test/basemap-synth.ts` | `b023a4ed5424` | blob-identical to the pinned commit; it gains `sea` through pq4d's `store.ts` |
| `zap-fetch.mts` | `2163cdbb669b` | step 0: `lake-fetch.mts` with its home (Zapata), target (`zap-obj`) and local sources changed, and nothing else (10 diff lines) |
| `sea-dryrun.py` | `e1b68a840847` | the dry run, now also writing `sea-extent-cells.json`. **Its re-run at step 0 must leave `sea-pieces.json` at `8ad5d92fa7c7`** |
| `mk-sea-store.mts` | `66554014b0be` | `sea-obj`, `--seam-plant` → `sea-obj-seam`, and a `records.json` sidecar (record → cell, source, bbox) for KB7-c |
| `dr-p2c.mts` | `cd2d15be34e7` | the build child (store `sea`, `orig` or `seam`) |
| `dr-p2-synth.mts` | `d8aff7366286` | the synthetic child: `kb1`, `kb2cross`, `tsea0`, `bowtie` |
| `dr-p2m.mts` | `7814abed9746` | the memory child: `dr-q4c2.mts` with pq4d, the store by name and the three homes, its instrument unchanged |
| `p2-render.py` | `b9afa21eb58d` | KB1, KB8, KB4, KB5's export, and the pictures |
| `kb5-gen.py` → `kb5.cpp` | `6155b6399ef8` → `c7ff38514327` | `FillRing` extracted verbatim from `WxBasemap.h:80-120` at `525cf7d1725d` (extract `413104be01bf`, 41 lines). Built in WSL Ubuntu 24.04 with g++ 13.3, `-Wall` clean |
| `kb7.py` | `e24035aa2fab` | KB7-a and KB7-b, with their (b) controls |
| `p2-read.py` | `7e9828547270` | the driver, in the order below |

Every script compiles, and each child refused a bad argument after its imports. Nothing graded has run, and nothing has been fetched.

**The four changes, flagged:**
1. **KB8 runs at KBOS and MIDTOWN, not KJFK.** No 1° cell edge falls inside KJFK's closest-zoom view (±8 km: lat 40.57-40.71, lon
   −73.88 to −73.68). KBOS has −71° through Boston Harbor, and Midtown has −74° through the Upper Bay.
2. **S-KB-SEAM is a STORE plant, not a builder knob.** Shifting odd pieces east opens a gap only where the odd piece lies east of the
   edge, and an overlap where it lies west, so it isn't guaranteed. The store plant (`sea-obj-seam`) pulls every piece in a cell whose
   (lat + lon) is odd 0.0008° into its own cell, at its cell-edge vertices. That checkerboard gives every shared edge exactly one shrunk
   side, and 64-89 m is at least one closest-zoom pixel. The builder's `seam` knob stays in pq4d, unused.
3. **KB7-b's equality allows for what the cut drops.** "The pieces together equal the NE lake" ignored the cut's own drop of islands
   under 1.5 km² wholly inside one cell. Those are filled, adding their area. So the expected sum is the lake's area **plus those
   islands' area**, still within 1e-9 relative. The (b) control misses by the US part, many orders larger.
4. **P2-1's radius baseline for Buffalo, Rouses Point and Zapata is their own no-sea build,** since QW4-1 has no figure for them. The
   13 are held to QW4-1's radii as registered.

**The order (`p2-read.py`):**
1. **Step 0:** Zapata's fetch; the dry run's re-run (its pieces unchanged); `sea-obj` and `sea-obj-seam`.
2. **The synthetic controls, first:** the bowtie refused; T-SEA0-ci both ways; KB2's crossing plant ≥ 1, then the same store tested at
   0; KB1's nobridge and order plants, then KB1 at 0 over 24 views.
3. **P2-0 and T-SEA0-real** at the 13.
4. **P2-1** at the 16, with each home's no-sea build for the radius and the other-content costs.
5. **KB3.**
6. **KB8**, its store plant first.
7. **KB4**, its 2 km-inland control first at each airport.
8. **KB5**, its moved-vertex control first.
9. **KB7-a/b**, their controls in the same read; then **KB7-c-real.**
10. **P2-M**, its 8 MiB plant first.
11. **The pictures.**

Stop at the first off reading. VISIBLE COSTS are printed, not stops.

**Amendment (the orchestrator, 04:30:57Z): the sea's cost in OTHER content, named per point.**
- **Why:** QW4-1's packages sit at the cap (KJFK at exactly 32,768, three others 1 byte under), and the sea adds up to 3 KB. So the ladder
  may drop roads, rivers or water to fit the sea even where the radius doesn't change. "Radii as QW4-1" doesn't see that.
- **P2-1 also builds each of the 16 WITHOUT the sea,** on the ORIGINAL store: the same objects (`bm-obj`, `lake-obj`, `zap-obj`) with the
  original manifest, index and `current`. It is not `sea-obj` with the knob off, since `sea-obj`'s regions would still add the sea
  layer's PROVENANCE lines and shift the cap.
- **For each point it prints, with the sea and without:** the rung; `dropped.water` and `dropped.roads`; the secondary roads kept; the
  river lines; and the water rings that are not sea.
- **Any increase in a drop count, any decrease in kept content, or a rung change is a VISIBLE COST,** named per point to the owner beside
  the radius check. It is not a stop. That is the trade he would be making: the sea in exchange for some content near the cap.

**For the code PR,** registered now, run with it:
- **PARITY** (the 04:20:51Z ruling): its TypeScript ingest produces the SAME pieces as the dry run over all 2,584 cells: piece counts,
  ring counts, and areas within 1e-6 relative. **The control:** one piece's area altered must fail it.
- **A NEW DEPENDENCY,** if the ingest needs a polygon clipper, is named with its licence and version pin, for the orchestrator. The
  alternative is a hand-written rectangle clip that passes PARITY.
- **The reader sweep gains two entries** (04:2xZ):
  - `ingest-basemap.ts`'s `tileBitsCmd` finds the ocean zip by its layer, "naturalearth". So the ocean stays one manifest entry, and the
    tile-bits lookup learns the sea layer;
  - **a manifest cannot list one url twice,** so the prototype's two scratch sea urls are not the code PR's entries.
- **PROVENANCE's sea lines:** the prototype writes `collapsed.sea` and leaves `dropped.sea` at 0, counting the ladder's sea drops in
  `dropped.water`. The code PR separates them, as `seaDropped` on `bm_build` already does.

| outcome | meaning | next |
|---|---|---|
| a step-0 failure, a crash, or an instrument control blind | the tooling, not the sea | **stop**; nothing graded |
| **P2-0 or T-SEA0 off** | pq4d is not inert, or a no-sea store draws sea | **stop** |
| **a proper crossing in P2-1, KB1 off, or KB8 seam > 0** | the sea path draws badly | **stop and report** |
| a radius other than QW4-1's | **a visible cost of the sea,** named to the owner before the code | reported; the run goes on |
| KB3 over 3,072 bytes | the sea costs more than #616 allowed | **stop and report** |
| **KB4 under its bounds** | the mask disagrees with the terrain | **stop and report** |
| **KB7-c-real: a border-lake piece not drawn** | land across a border lake | **stop and report**, with the piece's key and fate |
| **a later picture with spill clearly beyond the normal error** | the owner's condition | **to the owner** |
| **a build over 96 MB** | | **stop** |
| **anything else** | | **stop and report** |

### Part 2, step 0's run (04:47:02-04:49:16Z, 2026-11-12): STOPPED at the sea store

Every artifact was checked against the pinning addendum's sha256 before the run.

- **Zapata's read-only fetch** (`zap-fetch.mts`): 12 cells and 64 tiles; 81 objects, 5 copied from `bm-obj` and 76 fetched read-only;
  every one verified; exit 0. Its summary line says `lake-obj:` although it wrote `zap-obj`. That is a label left over from the lake fetch
  it was copied from; it is cosmetic and was not changed.
- **The dry run, re-run:** `sea-pieces.json` is unchanged (8ad5d92fa7c7), and the extent file holds 2,584 cells.
- **The sea store and the seam store** (`mk-sea-store.mts`, with and without `--seam-plant`) both STOPPED with
  `needed cell 0/44/-77 is in no source` (exit 1, stderr empty). Under the outcome table this is a step-0 failure: the tooling, not the sea.
  **Nothing is graded and no fix is applied before a ruling.**

**The cause, read afterwards** with a read-only replica of the tool's own `needed` set (`step0-missing2.mts`, c91aff32f97a):
- **217 cells are needed by reach, and every one is in a source.**
- **102 cells receive sea and have a box that meets a home's square.** Exactly one of them is in the index, is not needed by reach, and is
  in no source: `0/44/-77`, 438,589 bytes in the index, whose box meets BUFFALO's square only.
- Its existing reach meets no square, so no earlier fetch copied it. The Lake Ontario pieces it gains do meet Buffalo's square, so the sea
  store must re-encode the cell with its existing features, and those bytes are not on disk.

**Proposed (awaiting a ruling):**
1. A read-only fetch of that one object (`blipscope-basemap`, `bm/v1/4c0875738f38/cells/0/44/-77.bin`) into a new source directory,
   `cell-obj`. It is checked by exit status and by its byte count against the index's 438,589.
2. `mk-sea-store.mts` gains that directory as a fourth source, at the end. This one-line change gives a new sha256, which is recorded
   before the run.
3. Step 0's two store builds run again.

**Predicted, from the same replica:** the fetch writes 1 object of 438,589 bytes. Both store builds exit 0, each printing
`cells 246 (144 copied, 74 with sea added, 28 sea-only new)`. Anything else stops.

**RULED (04:53:12Z): steps (1)-(3) as proposed.** `mk-sea-store.mts` now has `cell-obj` as its fourth source. It changed on two lines (the
`SRC` list and the header comment that names the sources), and its sha256 went from 66554014b0be to
`9f7a16c639b247070fa081f31d6e56fc2c9bb4d121b627918e37d1dc1b197bef` (9f7a16c639b2). Both store builds run again against the predictions
above. The partial `sea-obj` and `sea-obj-seam` left by the stopped runs are removed first, so every file the re-run produces comes from
the re-run. The cosmetic `lake-obj:` label stays as it is.

**Step 0's re-run (04:54:57-04:55:11Z): AS PREDICTED.**
- **The fetch:** exit 0, empty stderr, 1 object of 438,589 bytes (649aa404098a), matching the index.
- **Both store builds:** exit 0 with empty stderr, each printing
  `cells 246 (144 copied, 74 with sea added, 28 sea-only new); tiles 830; pieces 170 (islands 95) as records 0..169; regions +2`.
- **Not predicted, and explained before it was believed:** both builds print the SAME store id, e9ef04165ba2. `storeIdOf` is the sha256
  of the manifest text alone (`ingest-lib.ts:234`), and the seam plant changes cell geometry but not the manifest, which is identical
  in the two stores. **To show the plant took,** the two stores have the same 1,082 files. 50 of their 246 cells differ, and all 50 are
  on the odd checkerboard (0 on the even). That is exactly the number of odd sea cells holding a cell-edge vertex, 50 of 50
  (`seam-expect.mts`, a read made after the run, not a graded row). Their `index.json` files differ too, in the reach boxes.
- **Pinned as inputs for steps 2-11:**

| artifact | tree (12) | index.json |
|---|---|---|
| `sea-obj` | 28fddac31920 | d6945e124bfa |
| `sea-obj-seam` | a3389679daa7 | cbdfdfcd324c |
| `cell-obj` | afdcf87e12c7 | |

`records.json` is the same in both stores (32b072ef7994). Its bboxes are taken from the unpulled outers.

**The store id: the 04:57:54Z finding, read against main (e3c2508eb8db), NARROWED and proposed for ruling.** The finding: `storeIdOf`
hashes the manifest text only, so a later change to the ingest code with unchanged sources would write different cells under the same id.
That would mean an overwrite in place, a lost rollback, and stale ETags.
- **Main excludes that case as stated.** The production ingest stamps the manifest's `builder` with `git rev-parse HEAD`
  (`ingest-basemap.ts:317`, `:324`), so a committed change to the ingest changes the manifest text, and with it the id. The publish
  (`d1 --go`) refuses unless the dry run recorded a clean tree, `ingestCommit` equals `manifest.builder`, the dry run's id equals this one,
  and the commit exists here. That is F1, `ingest-basemap.ts:515`.
- **The prototype's two equal ids are the prototype's.** `mk-sea-store` copied `bm-obj`'s manifest, `builder` field and all, and changed
  only the files list. It never re-stamped the commit.
- **What remains, as the proposed row (QW4-ID):**
  - **(a) F1 has no test.** `proxy/test` has no case that refuses a dirty tree, a `builder` other than `ingestCommit`, or an unknown
    commit. A guard nobody has seen refuse is untested. The code PR adds that case. The control: the same inputs from a clean tree at a
    known commit pass.
  - **(b) The cell bytes must be a function of the commit and the manifest's files and predicates only.** The sea ingest reads no
    environment variable and no flag that changes a cell (the prototype's `Q4_*` knobs do not carry over). A new clipper is pinned through
    the committed lockfile.
  - **(c) The determinism control:** two dry runs at the same commit, from the same cache, give byte-identical store trees.
  - **The orchestrator's control** (the same manifest with one cell's bytes changed must give a different id) **cannot pass as main
    stands.** The id is the manifest's hash by format §7 (MI4, ruled). F1 compares commits and ids, never cell bytes, so it does not catch
    that state either. Same manifest, different cells can arise only two ways. A dirty tree, which F1 refuses and (a) tests. Or an input
    outside the manifest, or nondeterminism, which (b) and (c) exclude. **For ruling, two ways:**
    - **(i) keep §7's id and register (a) to (c).** This is my recommendation: it leaves the format, `basemap-check` and the golden test
      untouched;
    - **(ii) change §7, folding the index's hash** (every cell's key and bytes) **into the id.** The orchestrator's control then passes
      directly, but §7, `basemap-check.ts`, `basemap-golden.ts` and the store-id tests all move, and the code PR becomes a format change.

**RULED (05:00:15Z): way (i).** Format §7's id stays as it is, and **QW4-ID (a)-(c) is registered for the code PR** as written above. The
orchestrator withdrew its control ("same manifest, one cell changed"): it tested a state that (a)-(c) exclude at the source. The
orchestrator also recorded its own miss: the finding was raised from the prototype's artifact, a copied manifest that was never re-stamped,
without the production path being read. QW4-ID stays out of part 2's run.

### Part 2, steps 2-11 (the orchestrator's go at 05:06:36Z, B-AFTER graded)

Checked before the run (05:07Z): every pinned artifact matches the addendum, including the new `mk-sea-store` (9f7a16c639b2) and
`p2-read.py` (7e9828547270). No step uses pio. KB5 runs the prebuilt `kb5` in WSL. The binary itself is not pinned (9e9fc5447bc1,
recorded now). Its source is pinned (c7ff38514327), and it was built 2 s after that source was written. A wrong binary would show as
KB5 off against the Python renderer, behind its own moved-vertex control.

**One check added before the run, and it does not change the driver.** The pinned driver repeats step 0 first. Zapata's fetch skips
every object already present, so it makes no R2 reads. The dry run is re-run, and both stores are rebuilt. **After the run,**
`sea-obj` and `sea-obj-seam` must still hash to their step-0 pins (trees 28fddac31920 and a3389679daa7). A mismatch means the run read
an unpinned store and **voids every row after step 0**: stop and report.

**The run (05:09:22-05:09:27Z): STOPPED at the third synthetic control.**
- **Step 0, repeated by the driver:** Zapata's fetch made 0 copies and 0 fetches, verifying all 81 objects. The pieces are unchanged
  (8ad5d92fa7c7), and the extent holds 2,584 cells. Both stores again printed `cells 246 (144 copied, 74 with sea added, 28 sea-only new)`.
  This time they printed **`tiles 0`, not step 0's 830.** `mk-sea-store` skips a tile already at its destination (line 107), and the
  step-0 stores were still there. **The registered post-run check:** `sea-obj` and `sea-obj-seam` still hash to their pins (28fddac31920
  and a3389679daa7, 1,082 files each), so the run read the pinned stores.
- **KB7-c's control** (the bowtie refused): AS PREDICTED, refused 1. **T-SEA0-ci** (no sea layer, no sea): AS PREDICTED, rings 0 and sea
  lines 0.
- **T-SEA0-ci's control** (the layer added draws the sea): **OFF-PREDICTION.** Sea rings 1, but **sea lines 0**, where at least 1 was
  predicted. The driver stopped; nothing after it ran.

**The cause, read after the stop** (`prov-diag.mts`, 6f45fd83903b, a diagnostic build of the same synthetic store, not a graded row).
- The package's PROVENANCE keys are `format | builder | store | source.water | vintage.water | files.water | sha256.water | epsilon.m |
  dropped.water | collapsed.water`. **There is no sea line at all.**
- `provenanceLines` writes a layer's source, vintage, files, sha256, dropped and collapsed lines only for the layers in the build's `read`
  map. pq4d's RD-P6 loop fills `read` for `water, rivers, roads, peaks` only (`build.ts:123`). The generator added the sea to
  `LAYER_ORDER` and carried `collapsed.sea` through the ladder, but never added the sea's files to `read`.
- **So the sea draws without being credited,** and every pq4d package with sea undercounts PROVENANCE by six lines.

**Two consequences:**
- **The sentence above is false:** "the prototype writes `collapsed.sea` and leaves `dropped.sea` at 0". It was written from the
  generator's intent, not from a built package, and is corrected here.
- **Had P2-1 run, its costs would have read optimistic.** PROVENANCE's bytes count against the 32,768 cap, and KJFK sits exactly on it.
  The code PR must list the sea's files under RD-P6, so the prototype must too, or P2-1 measures a package the code PR cannot produce.

**Proposed (awaiting a ruling):**
1. One more anchored edit in `mk-pq4d.mjs`'s sea part. After the RD-P6 loop, under `Q4S.on && want(MASK.water)`, the sea's files met
   are added as `read.sea`, **only when there is at least one.** A store without sea therefore keeps no sea line, and `provenanceLines`
   does not throw "read with no files".
2. pq4d is regenerated into a fresh directory. **The only predicted difference** is that line in `build.ts`. The new shas of
   `mk-pq4d.mjs` and `build.ts` are recorded before any run.
3. The driver re-runs from the start, unchanged.

**Predicted for the re-run:**
- step 0 and the rows before this one read as they did;
- T-SEA0-ci's control gives sea lines **6**: `source.sea`, `vintage.sea`, `files.sea`, `sha256.sea`, `dropped.sea` and `collapsed.sea`;
- P2-0 and T-SEA0-real are unchanged, because the edit sits under `Q4S.on` and the original stores hold no sea file.

**RULED (05:11:57Z): (1)-(3).** This is the row's first re-run. A second off reading on it goes to the orchestrator and on to the owner.
- **The edit.** It was made in `mk-pq4d.mjs` and, identically, in its `mk-pq4d-sea.part.mjs`, so the generator is still pq4c's plus the
  part. It is one `before(...)` anchored on the RD-P6 places line, which matches once:
  `if (Q4S.on && want(MASK.water)) { const s = [...new Set(met)].filter((u) => byUrl.get(u)?.layer === "sea").sort().map(fileRef); if (s.length) read.sea = s; }`
- **Regenerated** into a fresh directory, with the same arguments as before (`wt-q4`, 525cf7d1725d, `dnw/src/basemap/frame.ts`):
  exit 0, empty stderr. **`diff -r` of the old tree and the new, printed in full:**
  ```
  diff -r pq4d/src/basemap/build.ts pq4d-r2/src/basemap/build.ts
  125a126
  >       if (Q4S.on && want(MASK.water)) { const s = [...new Set(met)].filter((u) => byUrl.get(u)?.layer === "sea").sort().map(fileRef); if (s.length) read.sea = s; } // SCRATCH Q-W4 part 2 (05:11:57Z ruling): the sea's files read (RD-P6), only when one is met
  ```
  The new tree is now `pq4d`; the old one is kept as `pq4d.pre0512`.
- **New sha256s, recorded before the run:**

| artifact | sha256 | was |
|---|---|---|
| `mk-pq4d.mjs` | `fd0b80a28a3f98cacf85509ada49a92ea27766e9f90fd77d2a82de49531b82b9` | 78f75e2e12f9 |
| `mk-pq4d-sea.part.mjs` | `83e49540646269b2a91eae935fc11962d88e0b4fe30084fd4e14b11d5329d718` | 0d7bf6e74312 |
| `pq4d/src/basemap/build.ts` | `4d7ee5e0a8685521299aa4cc4b5f6e2fd5e59d2acde93556a1b8465061d69797` | f50fe805558f |
| `pq4d/src/basemap/sea.ts` | unchanged, 4b46e0ece2fe | 4b46e0ece2fe |

- **The driver re-runs from the start, unchanged** (`p2-read.py`, 7e9828547270), against the predictions above: every row before
  T-SEA0-ci's control as it read; that control at **6** sea lines; P2-0 and T-SEA0-real unchanged.

**Run 2 (05:14:28-05:14:34Z): T-SEA0-ci's control AS PREDICTED; STOPPED at KB1.**
- **Step 0 as in run 1. The three rows before KB1 as before.** T-SEA0-ci's control now reads **rings 1, sea lines 6**, as predicted.
- **The KB2 and KB1 controls:** KB2's control gave crossings 4, and KB2 itself 0 crossings with 2 bridges. S-KB-NOBRIDGE differs by
  20,818; S-KB-ORDER by 26. All AS PREDICTED.
- **KB1: OFF-PREDICTION.** It found **26 differing pixels**, where 0 were predicted, in 4 of the 24 views: (162 km, 90°) 14, (162 km, 270°)
  6, (81 km, 0°) 5 and (40.5 km, 0°) 1. Nothing after KB1 ran.

**The cause, read after the stop** (diagnostics `kb1-diag.py` a32f673e608e, `kb1-diag2.py` e247ab943de8 and `kb1-diag3.py`
f88dded3e379; none is a graded row):
- **All 26 pixels are on one side,** filled by the reference and missed by the drawn ring. **Every one has its centre exactly on a bridge**
  (distance 0.0 px). The drawn ring is outer 4 + islands 3 × 4 + bridges 3 × 2 = 22 vertices, as the path intends.
- **The mechanism.** A bridge is crossed twice, once in each direction, and each crossing is computed from the edge's FIRST endpoint:
  `xa + (py - ya) * (xb - xa) / (yb - ya)`. Where the line passes exactly through a pixel centre, the two passes give the centre and the
  centre plus 1.4e-14 (2.8e-14 in the second view). Then `ceil(c - 0.5)` ends one span before the pixel and starts the next span after it,
  and **the pixel is lost.**
- **The alternative, that the bridge geometry is wrong, is EXCLUDED.** With every crossing computed from the edge's lower-y endpoint, so
  both passes give the same double, KB1 reads **0 in all 24 views.**
- **This is the DEVICE's arithmetic, not only the renderer's.** Weatherscope's `FillRing` (`WxBasemap.h:80-120` at 525cf7d1725d, extracted
  verbatim into `kb5.cpp`) computes the crossing with the same first-endpoint formula, in double. On the glass, then, a zero-width bridge
  through a pixel's centre leaves that pixel undrawn. In the synthetic case, with its 45° bridges on round coordinates, that is a 14-pixel
  line in one view. In real data it needs an exact alignment, so it should be rare, but it is not excluded.
- **S-KB-ORDER's control is BLIND on this input.** With lower-endpoint crossings, the order plant's ring against the tested reference
  differs by **0**. Its "differs, 26" came entirely from the artefact above. The plant bridges before simplifying, which should give the
  slit width. KB1's rings are 4-vertex squares that simplification leaves unchanged, so the slit stays at zero width and the plant changes
  no pixel.

**For ruling** (nothing applied; both are first off readings on their rows):
1. **KB1.** It cannot read 0 under the device's fill as it stands, so either:
   - **(a)** `FillRing` computes each crossing from the edge's lower-y endpoint. This is a one-line change to Weatherscope's device code,
     outside this session's lane: it needs that code's owner and the firmware gate, and it changes pixels customers see, though only
     pixels whose centre is exactly on an edge. `p2-render.py` and `kb5.cpp` would then mirror it.
   - **(b)** KB1 is re-registered to accept pixels whose centre lies exactly on a bridge, as a visible cost. This would be registering a
     tolerance after the result, and I do not recommend it.
   - A builder-side fix is not available: the pixel grid depends on the device's zoom, rotation and home, which the builder never knows.
2. **S-KB-ORDER.** A new synthetic input on which simplification moves vertices, for example squares carrying sub-ε wiggles, so that
   bridging first gives the slit width. Its predicted difference would be registered before any re-run, with a lower-endpoint fill.

**RULED (05:18:31Z):**
1. **KB1: (a).** (b) is rejected: it would be a tolerance chosen after the result. The fix is a device change in another session's code.
   Weatherscope owns `WxBasemap.h`, and makes it as **its own PR, registered first**. Its consumers and tests are `WxScenes.h`,
   `WxScreens.h`, map-client's `MapLayer.h`, `test_wx_basemap`, `test_wx_k35b`, `test_map_layer` and `sabotage_wx_basemap`.
   - **Part 2 HOLDS at KB1 until that PR's commit exists.** Then `kb5.cpp` extracts `FillRing` verbatim from that commit (the new
     extract is pinned), `p2-render.py` mirrors the same rule, and part 2 re-runs from the start. That is KB1's first re-run; a second
     off reading on it goes to the owner.
2. **S-KB-ORDER: accepted as blind on KB1's input. It is a control that passed for the wrong reason:** its "differs, 26" was the very
   artefact KB1 then failed on. A new synthetic input, on which simplification moves vertices, is registered with its predicted
   difference under the lower-endpoint fill before any re-run (below, when written).

**KB1's case, handed to weatherscope by bytes** (05:2xZ), in `C:\Github\_handoffs\map-server\kb1-case\`, outside every repo:
- **59 files:**
  - the 24 views' drawn rings (`view-<r>-<rot>.rings`, screen doubles) and their references (`.ref.bin`);
  - `missing.json`, the pixels each view's current fill leaves undrawn, as [x, y] in the 240 × 240 frame;
  - a hand-made 10-vertex minimal case;
  - `tools/`: the verbatim extract `kb5.cpp` (c7ff38514327), its one-line lower-endpoint copy, the source json, the mapper and the
    generators;
  - a README and `SHA256SUMS` (dbed2d338432).
- **Verified on the device's own C++ over all 25 cases:**
  - the current FillRing: 31 missing (26 + the minimal case's 5) and 0 extra;
  - the lower-endpoint crossing: 0 missing and 0 extra.
  The Python lists equal the C++ result pixel for pixel.
- **The handoff's own guard caught one defect, in my tool, not in the case.** `mk-case.py`'s helper fill lacked `p2-render.py`'s
  `if hi > lo` guard. A span wholly off-screen left gave a negative slice end, which numpy wraps, so the generator's "no drawn pixel
  outside the reference" check refused view (162 km, 225°). The guard was added. The two earlier case files and their references are
  byte-identical before and after, and the full set was regenerated into an emptied folder.

**S-KB-ORDER's new input, REGISTERED before any code (05:3xZ), for the orchestrator's acceptance.**
- **Why the old input could not see the plant.** `dpKeep` anchors a closed ring at its first vertex (`frame.ts:107-133`, RD6).
  - The tested order simplifies each ring on its own (outer, then each island), quantises, and then bridges.
  - The plant bridges first, then simplifies the whole bridged ring once.
  - KB1's islands are 4-vertex squares that no simplification touches, so both orders keep the same vertices, and the plant changed no
    pixel.
- **The new case, `kb1c`.** It has KB1's outer, (E, S) -100..200 × -100..100 km, past the square's east edge, with three CIRCULAR islands
  of 256 vertices each:
  - A: centre (-32.5, -32.5), r 7.5 km;
  - B: centre (14, 9), r 4 km;
  - C: centre (162.5, 10), r 10 km. C crosses the square's edge at E = 162, so the clip leaves it touching the edge, as in KB1.

  At ε = 201.168 m, DP keeps a vertex only where a chord's sagitta passes ε, so each circle thins to roughly 2π / 0.4 ≈ 16 vertices
  at r 10 km. **Where the anchor differs, the kept subset differs.** The island alone is anchored at its own first vertex. Inside the
  bridged ring it is anchored at the bridge's endpoint, after splits made over the whole ring. The two polygons then differ by up to about
  ε, along every circle.
- **Predicted, under the fill rule of weatherscope's commit** (a lower-endpoint crossing):

| row | input | predicted |
|---|---|---|
| KB1 (unchanged) | `kb1`, the squares | **0** differing pixels in all 24 views |
| KB1 on the new input | `kb1c`, the tested order | **0** differing pixels in all 24 views |
| S-KB-NOBRIDGE on `kb1c` | islands sent as their own rings | each view differs by at least its island pixels, as registered |
| **S-KB-ORDER on `kb1c`** | the plant's ring against `kb1c`'s tested reference | **≥ 1** differing pixel over the 24 views, **AND at least one of them with its centre more than 1 px from every bridge segment** |
| anything else | | **stop** |

  **The second clause of S-KB-ORDER's row is the lesson of its blind pass.** A difference made only on a bridge is the slit artefact,
  not the plant. The control has to show a difference the artefact cannot make, or it has not shown that KB1 sees the order.
- **The code** (after the orchestrator's acceptance, and only after weatherscope's commit exists):
  - `dr-p2-synth.mts` gains the case `kb1c`;
  - `p2-read.py` gains KB1 on `kb1c` and S-KB-NOBRIDGE / S-KB-ORDER on `kb1c` beside the existing rows, in the same place in the order;
  - `p2-render.py`'s fill mirrors the commit's rule;
  - `kb5.cpp` is re-extracted verbatim from that commit.

  Every new sha256 is recorded before the re-run.

**ACCEPTED (05:25:51Z), with S-KB-ORDER's SCOPE stated before code.** One pixel is r_view / 120 km: 1.35 km at 162, 0.675 at 81 and
0.3375 at 40.5. The two orders' polygons differ by bands up to about ε (201 m) wide, which is 0.15, 0.30 and 0.60 px in those views.
- **Where the difference can appear:**
  - Island C (E 152.5..162) is in view only at 162 km.
  - B (centre at 16.6 km, r 4) is in view in all 24.
  - A (centre at 46 km, r 7.5) is in view in all 16 views at 81 and 162 km. At 40.5 km it shows only in the rotations whose square
    reaches it.
- **How many pixels it should flip.** A band narrower than a pixel still flips pixels: about one per pixel-area of band, wherever a pixel
  centre falls inside it. A pixel's area is 1.82, 0.456 and 0.114 km² in the three views. The two orders' symmetric difference is of
  order a few km² per circle. That is a few pixels per view at 81 km, more at 40.5 km, and around one per circle at 162 km.
- **The prediction, scoped:**

| reading | means |
|---|---|
| **≥ 1 off-bridge pixel in total over the 24 views, with at least one of them in an 81 km or 40.5 km view** | **as predicted: the plant shows where the bands are widest in pixels** |
| off-bridge pixels in the 162 km views as well | allowed and not predicted (a sub-pixel band still covers some centres); recorded |
| off-bridge pixels ONLY in 162 km views, none at 81 or 40.5 km | **stop**: the model of where the plant shows is wrong |
| no off-bridge pixel anywhere | **stop**: the control is blind again |
| anything else | **stop** |

**ACCEPTED (05:27:52Z):** the scoped table above, as written. The orchestrator also withdrew its own "can only show at the small steps":
a sub-pixel band still flips about one pixel per pixel-area of band. **Part 2 now waits for weatherscope's FillRing commit, and nothing
else.**

**Weatherscope reproduced KB1's case by bytes (05:3xZ).** It verified all 59 files against `SHA256SUMS` and ran main's own FillRing with
run.sh's flags (WSL, g++ 13.3). The current code misses 31: 14, 6, 5 and 1 in the four views, plus the minimal case's 5. The lower-endpoint
version misses 0, and neither draws an extra pixel. Every list equals `missing.json` pixel for pixel. It is registered on weatherscope's
side as S4.21 (`weatherscope/stage4-plan` 46b4cf595add): FL7/FL7c on the minimal ring, FL8/FL8c on the 162 km, 90° ring, and the
S-BRIDGE plant.
- **A stale hash in my first message to weatherscope, which it reported.** That message gave `mk-case.py` as 87b9f5675a13, hashed
  before the `if x1 > x0` guard was added in place. The file since then, in the scratch folder and in the handoff's `tools/`, is
  71ac48ce5837, which `SHA256SUMS` lists. No pre-guard copy was kept. The case files it writes were byte-identical before and after the
  guard, so nothing weatherscope used is affected.

### Part 2, the pinning addendum for KB1's first re-run (the 06:24:25Z go; #662 on main)

**The source:** `f3fee349061afc346900a5f85a0eee2034791ce4`, #662's squash, read on main. It has one parent, e3c2508eb8db, and its
`WxBasemap.h` computes each crossing from the lower-y endpoint. `FillRing` is lines 80-121 there.

| artifact | sha256 | was | what changed |
|---|---|---|---|
| `kb5-gen.py` | `39e77fbb90e69791cd0e5d69904ca95eb5abf320e7bbfc022efabe9083d1ac0f` | 6155b6399ef8 | takes the commit as an argument, and extracts from the signature line (which must occur once) through the first line that is exactly `}` |
| `kb5.cpp` | `6a839559bdcfc37d5d512b4e955a324f7053650a84eb4035afc22ec7fa50bdd3` | c7ff38514327 | generated at f3fee349061a. The extract is a738ac1f9d22, 42 lines, `WxBasemap.h:80-121` |
| `kb5` (binary) | `a841b11b5f479420191e6f20bc7f492d854565794e33584e78d532dc68f85d7a` | 9e9fc5447bc1 | `g++ -O2 -Wall -o kb5 kb5.cpp`, g++ 13.3.0 in WSL Ubuntu 24.04, warning-clean |
| `p2-render.py` | `cca7d5c16db2b136f5bc89408eb8cb04675d6888c0d035c7569d937cf9271841` | b9afa21eb58d | `fill_ring` mirrors the commit's ternary, the same operations in order. `kb1` mode also counts each view's off-bridge pixels and prints `total_offbridge` and `bridges` |
| `dr-p2-synth.mts` | `73c00f842daa0ce54b7078e4e11f8c7d7febcf9e448d86c2b0ab59a85df2f71b` | d8aff7366286 | the case `kb1c`, exactly as the 80164e6096c3 table registered it |
| `p2-read.py` | `4e1fc2ad5b2dd05ea762feb411050d97c16a970c9caa6468090afed0900f081a` | 7e9828547270 | the `kb1c` rows, and the old S-KB-ORDER row on the squares changed (below) |

**Unchanged and re-verified:**

| artifact | sha256 |
|---|---|
| `mk-pq4d.mjs` | fd0b80a28a3f |
| `pq4d/.../build.ts` | 4d7ee5e0a868 |
| `sea.ts` | 4b46e0ece2fe |
| `mk-sea-store.mts` | 9f7a16c639b2 |
| `dr-p2c.mts` | cd2d15be34e7 |
| `dr-p2m.mts` | 7814abed9746 |
| `kb7.py` | e24035aa2fab |
| `sea-dryrun.py` | e1b68a840847 |
| `zap-fetch.mts` | 2163cdbb669b |
| `sea-obj` tree | 28fddac31920 |
| `sea-obj-seam` tree | a3389679daa7 |

**Controls run on the tooling before the run, none of them a graded row:**
- **The extractor.** At 525cf7d1725d the new `kb5-gen.py` reproduces the old `kb5.cpp` byte for byte (extract 413104be01bf, 41 lines,
  as pinned). At f3fee349061a the diff from the old file is the header comment and the crossing line only.
- **The binary.** Over the handoff's 25 cases it leaves 0 pixels missing and draws 0 extra.
- **The off-bridge counter.** In a copy of the new renderer with the OLD fill planted (`ctl/p2-render-oldfill.py`), run 2's
  `synth-kb1.json` reads total 26, off-bridge **0**, with 6 bridge edges (3 bridges, each walked both ways) in the same four views. So the
  counter puts the artefact on the bridge. The no-bridge plant reads off-bridge = total = 20,818.

**One driver change, flagged.** Run 2's S-KB-ORDER row on the squares ("differs, ≥ 1") was ruled blind at 05:18:31Z. Under the new fill
it is predicted to read **0**: `kb1-diag3.py` gave 0 under a lower-endpoint fill. Left as a graded "differs" row, it would stop the run
on its own blindness. It becomes **a check that reads 0**, and anything else stops, because that would contradict the diagnosis that the
plant changes no pixel on squares. The `kb1c` rows follow it in the registered order: S-KB-NOBRIDGE, then S-KB-ORDER (its off-bridge
clause and its scope, as the 80164e6096c3 table says), then KB1 on `kb1c`.

**Predicted for the re-run, from the start:**
- step 0 and every row before KB1 as in run 2;
- S-KB-ORDER on the squares 0, and KB1 0 in all 24 views;
- the `kb1c` rows as the table says;
- then the frozen order as registered: P2-0 and T-SEA0-real, P2-1, KB3, KB8, KB4, KB5 (the C++ `FillRing` equals the Python renderer), KB7-a/b,
  KB7-c-real, P2-M, and the pictures.

**This is KB1's FIRST re-run.** A second off reading on it goes to the orchestrator and on to the owner.

**ACCEPTED (06:29:10Z), with the driver change in the orchestrator's words:** "Turning the squares' ORDER row from a 'differs' control
(known blind) into a check that reads 0 is honest: it now tests the diagnosis, and anything else contradicts it and stops. The real ORDER
control is on kb1c with its off-bridge clause, as registered." **GO for KB1's first re-run, in the frozen order.**

**Run 3, KB1's first re-run (06:30:15-06:32:10Z): KB1 AS PREDICTED; STOPPED at KB8 KBOS.**
- **Step 0 and every synthetic row before KB1, as in run 2.** S-KB-ORDER on the squares reads **0**, as predicted. **KB1: 0 in all 24
  views.**
- **The `kb1c` rows, all AS PREDICTED:**
  - S-KB-NOBRIDGE: 15,388.
  - **S-KB-ORDER: total 128, off-bridge 124**, of which 4 at 162 km, 24 at 81 km and 96 at 40.5 km, over 4 bridge edges. The 162 km
    pixels are allowed and recorded.
  - KB1 on `kb1c`: 0.
- **P2-0 inert and T-SEA0-real at all 13; P2-1 at all 16** (kOk, 0 proper crossings); **KB3 at all six coast airports** (765-1,954 B).
  All as predicted. **KB8's control** (S-KB-SEAM) reads 25 seam pixels, as predicted.
- **KB8 KBOS: OFF-PREDICTION.** It found **10 seam pixels on the lon -71 edge**, where 0 were predicted. MIDTOWN's KB8 and everything after
  it did not run.
- **P2-1's visible costs were not printed:** the driver prints them at its end, and it stopped first. They are read again on the next run.

**The cause, read after the stop** (diagnostics `kb8-diag.py` and `kb8-diag2.py`, neither a graded row):
- **The 10 pixels are column x = 131, rows 123 and 127-135.** That is 0.73 px west of the edge (screen x 132.23), at the closest zoom,
  north-up, around 42.35°N. East of the edge, ring 0 (45 vertices) is drawn right up to it. West of it, ring 2 has **5 vertices and only
  one on the edge,** at lat 42.35786. From there its boundary cuts south-west to (42.30018, -71.01784), 1,464 m west of the edge. The
  undrawn wedge between that chord and the meridian is 1 px wide at row 127 and 4 px at row 142. Where the east piece's edge ends
  (42.308), it is **about 1.2 km wide.**
- **The alternative, that the stored pieces do not meet, is EXCLUDED.** In `sea-pieces.json`, cell 42/-72's piece 0 (ocean, 17
  vertices) has edge vertices at **42.30809 and 42.35786**. Cell 42/-71's piece 0 (ocean, 197 vertices) has the same two. The pieces
  meet exactly; the build parts them.
- **The mechanism.** Each piece is simplified on its own. The west piece's edge vertex at 42.30809 lies **1,231 m** from the chord that
  replaced it, so it was dropped at a tolerance of at least 1.23 km. That is the ε×8 set (1,609 m), which KBOS's rung 4 draws water from.
  The east piece kept its edge, because there its edge vertices are corners of a long straight run.
- **KB8's count understates the size.** It reads only the 1-px band beside the edge. The wedge is a triangle about 1.2 km across and
  5.5 km long, of order 700 px at the closest zoom. The instrument fired correctly; its count is not the area.

**For ruling** (nothing applied; this is KB8's first off reading):
- **(a) Edge-locked simplification, recommended.** In the sea path's simplify step, every vertex lying on the piece's own cell boundary
  is kept: the ring is split at those vertices, and Douglas-Peucker runs on each chain between them. The shared edge vertices are
  identical in the store and quantise identically, so both sides keep the same edge. The cost is a few more vertices per piece, which
  KB3 bounds. This changes #616's simplify step in pq4d and in the code PR.
- **(b) Join same-source pieces across cell edges in the builder before simplifying.** It is seamless and fewer bytes, but needs a
  polygon union at runtime (a dependency, and CPU and memory).
- **(c) Accept it as a visible cost.** I do not recommend it: at ε×8 the gap reaches about 1.6 km, roughly 24 px at the closest zoom.

**RULED (06:35:32Z): (a), edge-locked simplification in the sea path.** Every vertex on the piece's own cell boundary is kept: the ring
is split at those vertices, and DP runs on the chains between them. (b) is rejected for now (a runtime union and a dependency). (c) is
rejected (up to about 24 px of land across open sea). Before the re-run, a re-registration is pushed first:
1. a plant, **S-KB-EDGEUNLOCK** (edge-locking removed), must reproduce KBOS's wedge (KB8 ≥ 1 at KBOS), and runs first;
2. the lock must survive the rest of the path: on a synthetic two-piece edge, a host-level assertion that the two pieces' quantised edge
   vertices are equal, with a failing control;
3. the cost is PREDICTED: per point, sea bytes with the lock (KB3 ≤ 3,072 B still), and the effect on P2-1's visible costs;
4. **KB8's description, corrected:** its count reads a 1-px band beside the edge and understates a wedge's area (order 700 px at KBOS).
   The instrument fired correctly, but it measures PRESENCE, not size;
5. then the run from the start. This is KB8's first re-run; a second off reading goes to the orchestrator and on to the owner.

A slip in my report, recorded: it gave this record's commit as "28fff67fxxxx", a placeholder typed instead of a sha that had been read.

### KB8's re-registration: edge-locked simplification (the 06:35:32Z ruling), registered and pinned before the run

**The change.**
- **Where pieces are made** (`layers.ts`), a sea part's vertex whose lon or lat is a whole degree (within 1e-9) is LOCKED. That is its
  own cell's boundary, the ingest's cut.
- **The square clip moves no surviving vertex.** So `seaRings` carries each lock through the clip by the vertex's exact frame point. The
  projection is separable, x = (lon - lon0)·K·c0 and y = (lat0 - lat)·K, so every vertex on one meridian gets one double.
- **Step 3, under Q4_TP=1, is `waterQ4Locked`** (`frame.ts`, from `pq4d-frame-lock.part.ts`, inserted verbatim before `waterQ4`).
  - The anchors are index 0, Douglas-Peucker's own anchor as `simplifyRingTP` has it, plus every locked vertex. DP runs on each chain
    between consecutive anchors, and keeps the chain's ends.
  - Then comes `simplifyRingTP`'s own topology loop, byte for byte the same, followed by the same backstop.
  - **With no locked vertex, it IS `simplifyRingTP`:** one chain, 0..n, the same `dpKeep` over the same path.
- **The plant `edgeunlock`** (S-KB-EDGEUNLOCK) takes the unlocked path, which is run 3's.

| artifact | sha256 | was |
|---|---|---|
| `pq4d-frame-lock.part.ts` (new) | `90b5fcb26a99b30a32a919453775272886df697972c97bce90a1755bb37fb147` | |
| `pq4d-sea.part.ts` = `pq4d/.../sea.ts` | `1c0797cc0627236fabe13146359cfab7c1f8f0a9b3f7172c2acf523a8a6f6569` | 4b46e0ece2fe |
| `mk-pq4d.mjs` | `38def76e437c0b9727036023ade446a45c570ffda3794798ad7ae80d10c3218c` | fd0b80a28a3f |
| `mk-pq4d-sea.part.mjs` | `1510af7df0988a1087cf219f56b9754e6a864cdde9ba2b2f8cf0dbf20fdc0b7b` | 83e495406462 |
| `pq4d/.../frame.ts` | `c82e3aad5a72272dd8531d33c7e76a6de008fe60912d37a4117c306c04b4006d` | be6c086e95c5 |
| `pq4d/.../layers.ts` | `40484a780c86c0024978f9524ca97669d7b32407fba042d8d93653eb14efedd1` | 2c88da1f3b58 |
| `pq4d/.../build.ts` | unchanged, 4d7ee5e0a868 | |
| `dr-p2-synth.mts` | `25daaeea80df5eb6ce6cec4339c18916fdba6c26497e33bd4751d12de3373a79` | 73c00f842daa |
| `p2-read.py` | `ecf44d25e6d777fb935e641358019a8cb400a1c48119000106180314f97de250` | 4e1fc2ad5b2d |
| `est-lock-cost2.py` | `5460fca831bb12f122fc8fc109d73433da071b7961370882c6f050514de3595d` | |
| `lock-cost-pred.json` (its output, read by the driver) | `0e13f85a500b3d60c5c8c8d12767520d25658eb7244317ed0ce3a4f7b82ec382` | |
| run 3's KBOS package, kept (`p2-runs/run3-out/KBOS-sea-p21.json`) | `42f48f5e58144360f33d352f44ef1ec65eef16f94b22e0d3c165e179880fb208` | |
| run 3's driver output, kept (`p2-runs/p2-read.run3.out`) | `9e52a0bd988bf91f74cf740e8d4752ee025d14d42dec8324b7799a45da8e4814` | |

**Unchanged:**

| artifact | sha256 |
|---|---|
| `p2-render.py` | cca7d5c16db2 |
| `kb5` | a841b11b5f47 |
| `kb5.cpp` | 6a839559bdcf |
| `mk-sea-store.mts` | 9f7a16c639b2 |
| `dr-p2c.mts` | cd2d15be34e7 |
| `dr-p2m.mts` | 7814abed9746 |
| `kb7.py` | e24035aa2fab |

The stores are unchanged too.

**Tooling checks run before registering, none of them graded:**
- **The regenerated pq4d** differs from the old tree in exactly three files. `frame.ts` gained exactly the part file (36 lines, nothing
  else), `layers.ts` gained the lock line, and `sea.ts` is the part.
- **Identity** (`lock-identity.mts`, 51d1bb9c8534): with no locked vertex, `simplifyRingTPLocked` equals `simplifyRingTP` on
  **300 of 300** seeded random rings, at ε, ×2 and ×8.
- **The identity check's control:** on edge2's west piece, the unlocked path drops (0, 4045) and the locked path keeps it.

**1. S-KB-EDGEUNLOCK at KBOS, the plant.** It sits in KB8's block right after S-KB-SEAM and **before KB8 reads anything**: "run it first",
read as first of KB8's rows. It is flagged in case it should come earlier. **Predicted:** its KBOS water rings equal run 3's kept
package's, and **KB8 = 10**, run 3's exact count.

**2. EDGE2, the host-level assertion.** It follows the `kb1c` rows in the synthetic block, with its control first.

| row | predicted |
|---|---|
| EDGE2's control, unlocked | the two rings' quantised edge sets DIFFER: {405, 4045} (east) and {405} (west) |
| EDGE2, locked | both {405, 4045}, **equal** |

**3. The cost** (`est-lock-cost2.py`, run on run 3's P2-1 outputs and the store; none of the new code ran). For each drawn piece, the
estimate counts its locked vertices inside the square whose quantised point is missing from its own run-3 ring (its ring being the one
sharing the most of its points). Each costs about 4 B, at most 6.

| point | added ~ | sea bytes, predicted change | bound (6 B a vertex, incl. unmatched pieces) | graded tolerance (2 × bound + 24) |
|---|---|---|---|---|
| BUFFALO | 12 | +48 | 78 | 180 |
| KBOS | 12 | +48 | 96 | 216 |
| KDTW | 4 | +16 | 24 | 72 |
| KEWR | 10 | +40 | 66 | 156 |
| KJFK | 13 | +52 | 84 | 192 |
| KLGA | 13 | +52 | 84 | 192 |
| KMIA | 6 | +24 | 48 | 120 |
| KSEA | 16 | +64 | 96 | 216 |
| KSFO | 7 | +28 | 54 | 132 |
| MIDTOWN | 11 | +44 | 72 | 168 |
| ROUSES | 14 | +56 | 84 | 192 |
| ZAPATA | 0 (8 locked, all kept) | 0 | 0 | 24 |
| KIND, KJAC, KMSP, KORD | no sea | **0 exactly** | 0 | 0 |

- **Graded.** At each point, |the change in sea bytes against run 3| ≤ the tolerance. Outside it, **stop**: the cost model is wrong.
  The tolerance has room because the chain effect is not modelled: anchors split DP's chains, which can move a few interior vertices
  either way. **KB3 ≤ 3,072 B stays graded,** with the largest predicted at KSEA, 1,954 + 96 = 2,050.
- **The effect on P2-1's visible costs.** A point's non-terrain bytes grow by at most its bound, so a rung or a radius can move only where
  the point sat within that many bytes of a rung's limit. The margins are not printed anywhere, so which points those are cannot be
  predicted. NY's four sit on the cap at 129.6 km, and are the likeliest. **Each point's rung and radius are printed beside run 3's.** Any
  change is a visible cost, named to the owner, not a stop.

**4. KB8, as corrected:** it measures PRESENCE, ≥ 1 undrawn pixel in a 1-px band beside a cell edge. It does not measure a wedge's
size; KBOS's wedge was about 700 px, and KB8 counted 10.

**Predicted for the rest of the run, from the start:**
- step 0 as before;
- **every synthetic row as in run 3**: NOBRIDGE 20,818; ORDER on the squares 0; KB1 0; `kb1c`'s 15,388, 128 / 124 (4 / 24 / 96) and 0. The
  only locked vertex in any synthetic case is `kb1`'s island C corner at S = 0, a corner DP keeps anyway;
- P2-0 and T-SEA0-real unchanged at all 13, since the lock lives only in the sea path;
- P2-1 kOk with 0 proper crossings at all 16, then the cost rows above, then KB3;
- KB8's control S-KB-SEAM ≥ 1, since the plant moves its edge vertices off the line and they are no longer locked;
- **S-KB-EDGEUNLOCK = 10, then KB8 at KBOS and MIDTOWN: 0**;
- then KB4, KB5, KB7-a/b, KB7-c-real, P2-M and the pictures, as registered.

**This is KB8's first re-run.** A second off reading on it goes to the orchestrator and on to the owner. **Anything else: stop.**

**ACCEPTED (06:49:25Z):** the re-registration at 3b3608f7ac3c, as written. "Run it first" means S-KB-EDGEUNLOCK first in KB8's block,
before KB8 reads anything, as placed. The cost model and its tolerance are accepted, and KB3 stays graded. **GO: run 4, from the start,
KB8's first re-run.**

**Run 4, KB8's first re-run (06:49:47-06:51:44Z): every row AS PREDICTED up to KB8 KBOS, which read 1. That is KB8's SECOND off
reading, so it goes to the orchestrator and on to the owner.** The output is `p2-runs/p2-read.run4.out`, d131de6c6b2b.
- **Step 0 and every synthetic row as in run 3:** NOBRIDGE 20,818; ORDER on the squares 0; KB1 0; `kb1c` 15,388, 128 / 124 (4 / 24 / 96)
  and 0. **EDGE2's control:** edge sets [[405, 4045], [405]], which differ. **EDGE2:** [[405, 4045], [405, 4045]], equal.
- **P2-0 and T-SEA0-real** unchanged at all 13. **P2-1** kOk with 0 proper crossings at all 16.
- **The lock's cost, within tolerance at every point,** and exactly 0 at the four with no sea:

| point | sea bytes, run 3 → run 4 | change | predicted |
|---|---|---|---|
| KBOS | 1,398 → 1,415 | +17 | +48 |
| KJFK | 765 → 771 | +6 | +52 |
| KLGA | 745 → 732 | -13 | +52 |
| KEWR | 632 → 621 | -11 | +40 |
| MIDTOWN | 669 → 649 | -20 | +44 |
| KMIA | 1,124 → 1,146 | +22 | +24 |
| KSFO | 670 → 694 | +24 | +28 |
| KSEA | 1,954 → 2,003 | +49 | +64 |
| KDTW | 499 → 520 | +21 | +16 |
| BUFFALO | 628 → 677 | +49 | +48 |
| ROUSES | 613 → 645 | +32 | +56 |
| ZAPATA | 648 → 640 | -8 | 0 |

  **No point's rung or radius moved.** The negative changes are the chain effect the tolerance allowed for: with the lock, pieces that
  run 3 collapsed are drawn, and the ladder dropped others (KBOS: collapsed 5 → 2, dropped 0 → 3).
- **KB3:** 694-2,003 B. **KB8's control S-KB-SEAM:** 149. **S-KB-EDGEUNLOCK at KBOS:** run 3's rings exactly, and 10 seam pixels.
- **KB8 KBOS: 1 seam pixel, OFF** (predicted 0). MIDTOWN's KB8 and everything after it did not run.

**The reading, after the stop** (`kb8-diag3.py` cebfebefd636 and `kb8-diag4.py` 3cc17eca39bd, neither a graded row):
- **The lock holds at KBOS.** Run 4's west ring and east ring share their locked edge vertices exactly, at lat 42.30811 and 42.35786. In
  run 3 the west ring had only 42.35786, and the east ring only 42.30811 and 42.37973. **The 1.2 km wedge is gone.**
- **The one pixel is (131, 123), at the corner where the west piece's coast meets the edge,** lat 42.35786. The row's centre lies 3 m
  north of that vertex. On that row, the west ring's boundary is the coast running WNW from the corner, crossing at x 131.34. The east
  ring starts at the edge, x 132.24. Pixel 131's centre, 131.5, lies between them.
- **In the TRUE geometry that pixel is land.** That is the stored, unsimplified pieces of the four cells around lon -71, filled by the
  same rule and unioned on KB8's screen. **The whole west side of row 123 is land too,** so KB8's own rule finds **no** seam in the truth.
  In the package, the ε×8-simplified coast west of the corner runs slightly north of the real one. It draws sea at x 123-130 on row 123,
  where the truth has land, and it ends at the locked corner, leaving the corner pixel undrawn with sea 2 px either side.
- **So this pixel is not a gap between pieces.** It is ordinary coastline simplification at ε×8, within ε, at a point where a coast meets a
  cell edge. **KB8's rule cannot tell that from a seam.** The alternative, a gap between the two pieces, is EXCLUDED: their edge vertices are
  identical, and the truth has land at that pixel.

**For the owner (via the orchestrator).** Nothing is applied.
- **(a) KB8 counts only pixels that are sea in the TRUTH** (recommended). A seam pixel would be undrawn in the package, drawn sea in the
  stored unsimplified pieces rendered by the same rule, and drawn 2 px either side. That is what KB8 is for, since a gap between pieces
  is sea in the truth. The truth is rendered from the store, independently of the builder. The old rule's count stays printed beside it.
  Predicted: KBOS 0 and MIDTOWN 0. This changes KB8's definition after a reading, which is why it is the owner's.
- **(b) Accept KB8 KBOS = 1** as a coast corner. The pixel is land in the truth as well, so nothing extra is shown.
- **What the lock cannot reach, and needs nothing:** simplification still moves coasts by up to ε×8 everywhere. That is the ladder's
  rung, not a seam.

### KB8 (a): the owner's ruling (2026-11-12 15:07Z), registered before any control runs

**The ruling, in the owner's words:**
- "A seam pixel counts only where the unsimplified reference is also sea, and the old count stays printed."
- "Before part 2 runs, the control works both ways: a planted real gap (reference sea) is caught, and a land corner like Boston's isn't."
- "Boston's pixel is recorded as the instrument's miss."

**Boston's pixel, recorded as the instrument's miss.** In run 4, KB8 counted pixel (131, 123) at KBOS. That pixel is LAND in the
unsimplified reference, with the whole west side of its row land too. The two pieces share the edge exactly. KB8's old rule could not
tell a land pixel at a coast-meets-edge corner from a seam. **The sea path did not miss; the instrument did.**

**The rule (a), as built** (`p2-render.py` kb8, d7c9552e7158; the old count is unchanged and still printed as `SEAM_CELL`):
- The reference is the store's own pieces: `sea-pieces.json` (8ad5d92fa7c7), lon/lat, unsimplified and unplanted.
- It takes every 1-degree cell meeting the view, fills each piece by the same FillRing rule (its outer XOR its islands), unions them, and
  draws them on KB8's own north-up screen.
- `SEAM_CELL_TRUTH` counts the old rule's pixels whose reference pixel is sea.

**The two controls, run BEFORE part 2** on kept run-4 files only, with no build (`kb8a-controls.py`, ff3e8a2c25ab):

| row | input | predicted |
|---|---|---|
| **CAUGHT**: a planted real gap where the reference is sea | S-KB-SEAM, run 4's seam-store builds at KBOS and MIDTOWN (`p2-runs/run4-out/`, 6cdfd12f27d4 and 19a16af8ea54) | old rule **149** in total (run 4's own reading); **(a) ≥ 1** |
| **NOT CAUGHT**: a land corner like Boston's | Boston's own: run 4's kept KBOS package (`p2-runs/run4-KBOS-sea-p21.json`, 7c0fdfbd269e) | old rule **1**; **(a) 0** |
| UNCHANGED | the same Boston package with no reference given | `SEAM_CELL` 1, and no (a) key: the old rule is untouched |
| anything else | | **stop** |

**In part 2's run** (`p2-read.py`, 51c42cbb2a17), KB8's block becomes:
- **CAUGHT,** on the run's own S-KB-SEAM builds: (a) ≥ 1. The old count is printed; it is predicted at 149 again, the builds being
  deterministic.
- **NOT CAUGHT,** on run 4's kept KBOS package: old 1, (a) 0.
- **S-KB-EDGEUNLOCK:** run 3's rings, old rule 10, and (a) ≥ 1, because that wedge is sea in the reference.
- **The run's KBOS rings equal run 4's kept package,** so the not-caught control read the row's own package.
- **KB8 at KBOS and MIDTOWN under (a): 0,** with the old count printed and not graded.

Everything else in the run is as at 3b3608f7ac3c. This is KB8's third reading, under (a): **an off reading on it goes to the
orchestrator and on to the owner.**

**The CAUGHT side TIGHTENED (the 15:35:08Z ruling), before any control runs.** "(a) ≥ 1" against 149 would pass a rule that dropped 148
of the 149 planted pixels. The expected count is derived here from the plant's geometry, **without rendering the reference**.
- **The store's edge intervals** (`kb8a-geom.py`, 34c32a544633, from the unplanted `sea-pieces.json`). Along each in-view cell edge, an
  interval is the stretch where a side's piece runs ON the edge, two consecutive vertices on the line.
  - **KBOS, lon -71 (x 132.232):** rows 87.34-110.56 and 123.53-205.93, the same on both sides.
  - **MIDTOWN, lon -74 (x 107.46):** rows 56.44-102.91 and 194.68-214.68, the same on both sides.
- **The abutment argument.** A flagged pixel on side s lies 0.04-1 px from the edge. **Inside** s's interval, and at least a row from both
  ends, s's piece runs along the edge, so the pixel is sea in the reference (**I**). More than a row **outside** every interval, no piece of
  s touches the edge, so it is land (**O**). **Within a row of an endpoint,** where a coast meets the edge, the argument decides nothing
  (**B**).
- **The old rule's pixels, classed** (`kb8a-classify.py`, 14b5669faa5b). It re-runs the OLD rule on the kept inputs; no reference is
  rendered.
  - **CAUGHT,** run 4's seam builds: KBOS 84 (I 80, B 4), MIDTOWN 65 (I 58, O 4, B 3), a total of **149**: I 138, O 4, B 7. One B pixel is
    Boston's (131, 123), already **on record as reference land** (run 4's diagnosis), which leaves 6 undecided.
  - **EDGEUNLOCK,** run 3's KBOS package: 10 pixels, I 9 and B 1, the B being Boston's (131, 123).

| row | predicted (a) | stop if |
|---|---|---|
| **CAUGHT** (the control before part 2, and part 2's own) | **138 ≤ (a) ≤ 144**, with the old rule at 149 | under 138: a sea piece narrower than a pixel inside an interval, which the argument's one exception covers and the model does not; over 144: an O pixel or Boston's counted as sea. Either way, stop and report |
| **EDGEUNLOCK** (part 2) | **(a) = 9 exactly**, with the old rule at 10 | anything else |

**The new sha256s:**

| artifact | sha256 |
|---|---|
| `kb8a-controls.py` | `0bb3304fc3e2c21ac101995abf059da2196c06b64f271df304ae437086feebea` |
| `p2-read.py` | `426e93dd868f2899bdaee1759783a24ea1c03247e7c8ead8dba144b9f72f3407` |

They replace ff3e8a2c25ab and 51c42cbb2a17 above. NOT CAUGHT and UNCHANGED are as accepted.

**KB8 (a)'s controls, run (15:39:14-15:39:15Z, `kb8a-controls.py` 0bb3304fc3e2, on the kept run-4 inputs only): BOTH WORK, as predicted.**

| row | read | |
|---|---|---|
| **CAUGHT**, S-KB-SEAM | old **149**; **(a) 142** (KBOS 84 → 83, MIDTOWN 65 → 59), inside [138, 144] | **as predicted** |
| **NOT CAUGHT**, Boston's corner | old **1**; **(a) 0**; 3 reference pieces | **as predicted** |
| **UNCHANGED**, no reference | `SEAM_CELL` 1, no (a) key | **as predicted** |

**142 squares with the classes.** The 138 interior pixels all count. At KBOS, Boston's recorded-land (131, 123) drops and its three
endpoint pixels east of the edge count: 84 - 1 = 83. At MIDTOWN, the 4 outside pixels drop, and 1 of its 3 endpoint pixels counts:
65 - 6 = 59. **Part 2 waits for the orchestrator's word,** and the push to #652 waits for v19's promote.

**Run 5 (15:41:00-15:43:00Z, `p2-read.py` 426e93dd868f): every row AS PREDICTED through KB8, which passes in full under (a). STOPPED at
KB4's control at KBOS, which is blind.**
- **Step 0, the synthetics, P2-0, T-SEA0-real, P2-1, the lock's cost and KB3:** all as in run 4.
- **KB8 under (a):** CAUGHT (a) **142**, old 149; NOT CAUGHT (a) **0**, old 1; S-KB-EDGEUNLOCK run 3's rings, old 10, **(a) 9**;
  KBOS's rings equal run 4's; **KB8 KBOS (a) 0** (old 1, printed); **KB8 MIDTOWN (a) 0** (old 4, printed). **All as predicted.**
- **KB4's control at KBOS: OFF.** The mask moved 2 km inland reads frac 0.9649 and cover 0.9610, so **it passes both bounds**
  (0.95 and 0.90), where it was predicted to fail at least one. No KB4 row is graded; nothing after it ran.

**The cause, read in the renderer** (`p2-render.py` kb4, l.150 and l.180). KB4's grid is 242 px over the 162 km level, which is
**1.341 km a pixel**, and the control moves the whole mask **2 km, about 1.5 px**, from the sea's centroid toward the home. Only a
1.5-px strip along the coast changes. At KBOS the sea fills most of the 324 km square, so that strip is a few percent of the mask:
**a 2 km move cannot break a 5% margin on frac or a 10% margin on cover.** The control is structurally blind wherever the sea is
large against its coastline. It says nothing against the sea path. **What it does show:** at this grid, KB4 measures gross agreement
with the terrain, not 2 km placement.

**For ruling** (KB4's first off reading; nothing applied):
- **(a) a decisive control:** the mask's COMPLEMENT inside the square, the land drawn as sea. It must fail frac, because the land
  reads above 0 m. The 2 km figures stay printed as KB4's measured sensitivity, not graded. This is recommended.
- **(b) a shift per point, set from geometry before the run:** the smallest d for which d × coast length passes the bound's margin
  of the mask's area. That is more machinery, and it still assumes the newly covered strip is land above 0 m.
- **(c) record KB4's scope** (gross agreement, not 2 km) and drop the control. That is not recommended: a check with no control is
  one nobody can trust.

### KB4 re-registered (the 15:44:48Z ruling, (a) plus a shift ladder), registered before any code

**KB8 is CLOSED,** accepted in full from run 5: CAUGHT 142 / 149, NOT CAUGHT 0 / 1, S-KB-EDGEUNLOCK (a) 9 exactly, KBOS's rings equal
run 4's, and KBOS and MIDTOWN (a) 0, with the old counts printed.

**KB4's claim, as corrected:** at the 162 km level's grid (242 px, **1.341 km a pixel**), KB4 checks the sea mask's **GROSS agreement**
with the store's own terrain, **not 2 km placement**. The shoreline's error is measured elsewhere: the lake pictures' ~1.06 km P95, which
the owner accepted at 03:56Z. The old 2 km control was blind by geometry, as run 5 showed.

**1. THE DECISIVE CONTROL, graded, first at each coast airport** (KBOS, KJFK, KLGA, KSEA, KSFO and KMIA): the sea mask's COMPLEMENT inside
the build square. Every pixel the sea rings do not cover counts as sea, TIGER's own water included, since it is not the sea, and is graded
by KB4's two bounds against the same terrain.
- **Predicted: it FAILS BOTH bounds at every airport, frac_le0 < 0.95 and cover < 0.90.**
  - *frac:* the complement is mostly land, and land reads above 0 m.
  - *cover:* the ≤ 0 m pixels outside TIGER water are mostly the sea itself, which the complement leaves out.
- **Expected ranges, not graded:** frac 0.02-0.30 and cover 0.00-0.15 at KBOS, KJFK, KLGA, KSEA and KSFO. At **KMIA**, where the low
  Florida land may read ≤ 0 m, frac 0.02-0.60 and cover 0.00-0.30.
- **Anything else stops.** If the complement passes either bound at any airport, the bounds cannot tell sea from land there.

**2. KB4's SCOPE AS A NUMBER, reported and not graded:** a shift ladder per airport. The mask moves **2, 5, 10 and 20 km** from the
sea's centroid toward the home (the renderer's own direction), and frac and cover print at each step. Then **d\*** prints: the first
step that breaks a bound. The record then reads "KB4 detects a misplacement of d\* km at this airport". **If no step up to 20 km breaks a
bound, that is said plainly as KB4's scope there.** No outcome of the ladder stops the run.

**3. KB4 itself is unchanged:** at each airport, frac_le0 ≥ 0.95 and cover ≥ 0.90 for the mask as drawn. It runs after its complement
control.

**The code, after acceptance:** `p2-render.py` kb4 gains the complement's grades and the ladder in its one output line, the old 2 km
figures kept printed. `p2-read.py`'s KB4 block grades the complement first, then KB4, and prints the ladder. Every new sha256 is
recorded before the run. **A second off reading on KB4 goes to the owner.**

**KB4 (re-registered), the code, pinned before the run (15:5xZ).** `p2-render.py`
`a042ae1222d51381e65f59e8ad875c2f8ced95aa1321918f3a03d04271d3aa7e` (was d7c9552e7158): kb4 gains the complement inside the build square
(the package's own radius), graded by the same two bounds; the 2 / 5 / 10 / 20 km ladder; and d*. The old 2 km figures stay printed.
`p2-read.py` `a297d699b060250f33e102dc19552214a022c60953a2d5555db2a70d525c0cc6` (was 426e93dd868f): KB4's block grades the complement
first (BOTH bounds must fail), then KB4 unchanged, then prints the ladder and d* ("none by 20 km" when none). **The tooling smoke test**
ran at KDTW, which is not a graded airport: exit 0, empty stderr, every new key present, four ladder steps, and the 2 km step equal to
the old 2 km figures. No KB4 value was read. Everything else is as pinned.

**Run 6 (15:49:51-15:51:49Z, `p2-read.py` a297d699b060; output 4284c52c5a9c): through KB8 as in run 5; KB4 at KBOS AS PREDICTED; STOPPED
at KB4 KJFK. This is KB4's SECOND off reading, and it goes to the owner.**
- **KB4's control:**
  - KBOS: the complement reads frac **0.0136**, cover **0.0093**, failing both bounds, as predicted.
  - KJFK: the complement reads frac **0.1339**, cover **0.1476**, failing both bounds, as predicted.
- **KB4 at KBOS:** frac **0.9827**, cover **0.9907**, as predicted. **KBOS's scope:** at 2 km, frac 0.9649 and cover 0.9610; at 5 km, 0.9346
  and 0.9323; at 10 km, 0.8947 and 0.8913; at 20 km, 0.8259 and 0.7930. **d\* = 5 km**, so KB4 detects a 5 km misplacement at KBOS.
- **KB4 at KJFK: frac 0.9706, which passes, but cover 0.4312: OFF** (the bound is 0.90). Nothing after it ran.

**The cause, read after the stop** (`kb4-diag.py` e7a5c5c442fb and `rec18-diag.mts` 78811fef616a, neither a graded row). There are two
causes, and they are independent.
- **(1) An instrument flaw: KB4's cover denominator spans the whole 162 km level grid, but KJFK's package is 129.6 km** (D2's step).
  Of the 19,390 ≤ 0 m pixels outside TIGER water, **8,168 lie outside the package's own square**, where no package of that radius can
  draw. Inside the square, cover is **0.745**.
- **(2) A SEA-PATH DEFECT: the New York Bight's ocean piece is REFUSED.** Inside the square, 2,862 low pixels are uncovered.
  - 2,719 of them are sea in the store that was not drawn, and **2,672 of those are one piece: record 18** (ocean, cell 40/-74, 94
    outer vertices and one 14-vertex island). The backstop refused it. Had the store's sea been drawn, cover inside the square would
    be **0.9873**.
  - **Why it is refused** (the sea path's own steps on that piece in KJFK's frame):
    - at ε and at ×2, the outer (68 and 55 vertices) and its island bridge with **0** crossings;
    - **at ε×8** (1,609 m), which KJFK's rung draws from, the outer simplifies to 27 vertices and the island separately. Each is valid
      alone, but simplified apart they cross each other, so **the bridged ring has 2 proper crossings, and step 5 refuses the whole
      piece.**
  - **About 6,000 km² of open Atlantic is drawn as land** at every New York point that draws water from ε×8. The fates have shown
    "refused 1" at all four since run 3, and **no graded row looked at it.** P2-1's "0 proper crossings" was true, because the refused
    piece is never drawn: a check that cannot see what it refuses.

**For the owner (via the orchestrator).** Nothing is applied.
- **(A) KB4's cover, counted inside the package's own square,** as frac already is through the mask. This corrects the instrument; the
  mask cannot reach past its radius.
- **(B) The sea path:** when the bridged ring has proper crossings, **drop the island(s) involved and bridge again** before refusing the
  piece. Refuse it only when its outer alone fails. That keeps step 3's own rule (a hole refused is dropped, the piece kept). At NY it
  costs one island, and saves about 6,000 km² of sea.
- **(C) A new graded row, so this cannot recur unseen:** at each point, every sea piece whose OUTER passes step 3 is drawn, so the
  refused count from step 5 is 0, with the islands dropped printed.
- **Predicted after (A)+(B):** record 18 is drawn at all four NY points, with one island dropped at each, and KJFK's cover inside the
  square is about 0.98. These are registered exactly before any re-run.

### KB4: the owner's ruling (2026-11-12 16:24Z), registered before any code

**The ruling, in the owner's words:** "KB4: (A), (B) and (C), with one condition on (B).
- (B) drops an island only below a size frozen in advance, no bigger than what is already too small to draw at the closest zoom.
- A larger island that crosses its sea outline stops the build; it is never dropped. Long Island, Staten Island and Manhattan are the
  cases that matter here.
- Every dropped island is counted on the build record.
- (C)'s control is shown failing first: a planted crossing island, built without (B), must turn (C) red."

**(A) KB4's cover is counted inside the package's own square.** The base is the ≤ 0 m pixels outside TIGER water within ±radius, not
the whole 162 km level grid. frac is unchanged, since it is already scoped by the mask. The complement control and the ladder are
counted inside the same square. KB4's bounds stay at 0.95 and 0.90.

**(B) The island threshold, FROZEN: T = 0.0045 km² (4,496.5 m²), ONE PIXEL at the closest zoom.** The closest zoom is the 8.04672 km
view drawn across 240 px, so a pixel is 16.09344 km / 240 = 67.056 m, and its area is 0.0044965 km². An island smaller than one pixel
is too small to draw there; anything larger can cover a pixel centre.
- **An island under T, crossing its outline after bridging, is DROPPED and the ring bridged again.** It is counted on the build record
  with its id, area and point (`seaIslandsCrossDropped`, and one log line per island).
- **An island at or over T, crossing its outline, STOPS THE BUILD.** The builder answers 500 `sea_island_crossing`, logging the piece's
  key, the island's id, area and point. **It is never dropped.**
- **What that means in practice: (B)'s drop branch can never fire.** Step 2 already drops every island under 1.5 km², which is 333.6
  px at the closest zoom, so every island that reaches bridging is far over T. **As ruled, any crossing island stops the build.**

**The named islands, against T** (from the store's own pieces, `sea-pieces.json`):

| island | at the bridging step? | size | at the four NY points (KJFK, KLGA, KEWR, MIDTOWN, radius 129.6 km) |
|---|---|---|---|
| **Long Island** | **no.** The 1-degree cut makes it part of each cell piece's outline, so it is never a hole | (spans several cells) | (B) does not apply |
| **Staten Island** | **yes:** a hole in cell 40/-75's ocean piece (record 25) | **139.4 km²** (31,011 px at the closest zoom) | **drawn** in runs 3-6 at all four, with no crossing, so (B) does not fire: **drawn as before** |
| **Manhattan** | **no:** a hole in no piece of any New York cell | | (B) does not apply |
| **Jones Beach Island**, the one that crosses | **yes:** the island of record 18 (cell 40/-74, the New York Bight), at (40.617, -73.417) | **40.4 km²** (8,983 px) | **crosses its outline at ε×8** (run 6's diagnosis), so **as ruled the build STOPS at all four NY points** |

**(C) A graded row:** at every point, step-5 refusals = 0 for every piece whose outer passes step 3. Islands dropped (under T) are
printed. **Its control is shown FAILING FIRST, before (B)'s code:** a synthetic plant, `xisland`, run on the current code without (B).
- **The plant:** a sea square (0..20 × 0..20 km). Its top edge carries a flat bulge 0.18 km high, under ε, from x 5.5 to 14.5 km,
  which DP removes. A 2.4 km² island (6..14 × 19.85..20.15 km) straddles that edge inside the bulge.
- **What should happen:** simplified, the outer's top edge straightens to y = 20, the island crosses it, and today's step 5 refuses
  the piece. **Predicted: (C) reads 1 refusal, RED.**
- **With (B) as ruled,** the same plant STOPS the build (the island is 2.4 km² ≥ T).

**Predicted for the re-run, as ruled:**
- **Every row before P2-1 reads as in run 6.**
- **P2-1:** KBOS as before. **At KJFK, KLGA, KEWR and MIDTOWN the build stops** with `sea_island_crossing` (record 18, Jones Beach
  Island, 40.4 km²). That reads as predicted, but **those four points have no package.** The other 12 points are as in run 6.
- **The rows that need a New York package are not computable** (KB3 at KJFK and KLGA; KB4 at KJFK and KLGA). They are recorded as such.
- **KB4 under (A) at KBOS, KSEA, KSFO and KMIA:** the in-square cover is at or above the whole-grid figure. KBOS is a full 162 km
  square, so its figure is unchanged at 0.9907.

**THE CONSEQUENCE, for the owner before any code: as ruled, the four New York points get NO MAP.**
- One option keeps every island and drops nothing: **(B') repair before stopping.** When the outer and an island cross after
  simplification, restore the original vertices on the crossing edges of BOTH rings, DP's own next choice, the same loop
  `simplifyRingTP` already runs within one ring, until no crossing is left.
  - It **stops only if the repair cannot resolve the crossing.** It costs a few vertices; at ε and ×2 the same piece already bridges
    with 0 crossings.
  - **This is a design addition, so it is the owner's call.**
- If the owner takes (B'), its predictions are registered before code: record 18 drawn at all four, Jones Beach Island kept, and KJFK's
  in-square cover about 0.98.

### PARITY: the ingest's hand-written rectangle clip, registered before any code (the orchestrator, 16:31Z and 16:36:44Z)

**The decision:** no new dependency. The Q-W4 code PR's TypeScript ingest cuts the sea with a **hand-written rectangle clip**, held to
PARITY against the dry run's GEOS `clip_by_rect` (shapely 2.1.2, GEOS 3.13.1) over all 2,584 extent cells. If parity is not reached in
about a day, the dependency option goes to the orchestrator first, with its name, licence, pinned version and size.

**The reference** is the dry run's own output, pinned: `sea-pieces.json` 8ad5d92fa7c7, from `sea-dryrun.py` e1b68a840847. Its printed
figures (step 0's log):
- **the extent:** 2,584 cells;
- **the sources:** the ocean as 2 outer polygons with 6,820 holes; Natural Earth lakes 1,355, of which 1,168 are selected (149 wholly
  inside the US, 38 under 1.5 km²);
- **the cut:** **1,435 cells** hold sea, **1,914 pieces** (1,641 ocean, 273 lake), **497 islands kept** and **135 dropped** under
  1.5 km², 69,728 vertices, 1,190,173 sea bytes.
- **Read from it before registering:** no kept or dropped island lies within 0.001 km² of the 1.5 km² threshold, so the island rule cannot
  flip on arithmetic. The smallest piece is 2.7e-8 deg², which is why the tolerances below carry an absolute floor.

**The algorithm, stated before the code:** a rectangle-specialised boolean intersection (Weiler-Atherton on a rectangle), per source
polygon and per cell:
1. each ring is classed as wholly inside the rectangle, wholly outside, or crossing it;
2. a crossing ring is cut at its boundary crossings (edge-parametric, in double) into inside chains, and each chain's ends are placed by
   their position along the rectangle's boundary, counter-clockwise from the south-west corner;
3. the chains are stitched into closed outers by walking the boundary between them, taking the rectangle's corners as passed;
4. holes wholly inside go to the stitched outer that contains them, tested at one of their vertices;
5. **if no ring crosses** and the rectangle's centre is inside the outer and outside every hole, the piece is the whole rectangle, with
   the holes inside it;
6. **then the island rule, as the dry run:** keep a hole if its area, in deg² × 111² × cos(its own centroid's latitude), is at least
   1.5 km², using the polygon centroid formula.

There is no 10-degree pre-tiling, unlike the dry run; it changes nothing in exact arithmetic. The lake selection (design (a): at least
1.5 km², not wholly inside the US) is part of the ingest and is compared too.

**The comparison, graded, written by a verifier in the geo-venv against the reference:**

| row | what is compared | passes when |
|---|---|---|
| **PA-1**, totals | cells holding sea, pieces, islands kept and dropped, lakes selected | **exactly** 1,435 / 1,914 / 497 / 135 / 1,168 |
| **PA-2**, per cell | the number of pieces, each matched 1:1 to a reference piece of the same source by least symmetric-difference area | equal in **every** one of the 2,584 cells; every piece matched |
| **PA-3**, per pair | rings: the outer plus the islands kept | equal |
| **PA-4**, per pair | area | \|A_ts - A_geos\| ≤ max(**1e-6** × A_geos, **1e-12 deg²**) |
| **PA-5**, per pair | **shape**: the area of the symmetric difference (shapely) | ≤ max(**1e-6** × A_geos, **1e-12 deg²**). This catches two different shapes with the same area |

**Recorded, not graded: the vertex sets.** Per pair, the Hausdorff distance between the rings, in degrees, and the difference in vertex
counts. **Predicted:** Hausdorff ≤ 1e-9°. The counts may differ where GEOS keeps or drops points that are collinear along the rectangle's
edge, or starts a ring elsewhere; that is an implementation detail and not geometry. The total of sea bytes is printed beside 1,190,173.
**Why the vertex sets are not graded:** what the store needs is the same SEA, which PA-4 and PA-5 settle. Vertex-for-vertex equality with
GEOS's RectangleIntersection would grade its internal choices.

**The controls, each shown FAILING before a pass is believed** (plants in a copy, never in the ingest):

| control | the plant | predicted |
|---|---|---|
| **C-SELF**, the null | the verifier compares the reference with itself | **0 failures.** The instrument passes what is equal |
| **C-IDX**, off by one | each cell clipped by its east neighbour's rectangle | **PA-1 and PA-2 fail**, at most of the 1,435 cells |
| **C-SHIFT**, 1.1 m | the rectangle shifted 1e-5° east | **PA-4 or PA-5 fails for at least half of the 1,914 pieces.** A clipped piece's area moves by about 1e-5 of its edge length in degrees, which is over 1e-6 relative for any piece with a vertical cut. This shows the tolerance sees about a metre |
| **C-AREA**, one piece | one reference piece scaled by (1 + 1e-5) about its centroid | **PA-4 and PA-5 fail at exactly that piece**, and nowhere else |
| **C-HOLE**, the island rule | the rule off, so every hole is kept | **PA-1 fails** (497 becomes 632). **PA-3 fails** at exactly the pieces that held the 135 dropped islands |

**The verdict:**
- **PASS:** every PA row holds, and every control fails as stated.
- **Anything else stops.** A control that passes is a blind instrument, and goes back to the orchestrator before the code is trusted.
- **Parity's first run is its own check,** before the code PR. Its sha and output go in this record.

**PARITY: ACCEPTED (16:40:13Z), with a degenerate-case suite added, registered here before anything runs.**

**How much the real data exercises the touching paths:** **24** source vertices lie EXACTLY on a whole-degree cut inside the extent. All
24 are on a meridian; none is on a parallel or a corner, and all are ocean (`parity/export-sources.py` 4aef18b68282; `sources.json`
37e44482a988 holds 1,180 source polygons, the 2 ocean polygons and 1,178 lake parts from the 1,168 lakes, with 599,914 vertices). So
the real cells barely test these cases, which is why the suite is synthetic.

**The suite, PD-1 to PD-12.** The rectangle is [0, 1] × [0, 1], in degrees. **GEOS is the truth:** shapely `clip_by_rect` on the same
input, at run time, with zero-area parts dropped as the dry run drops them. **Every case is graded by PA-2 to PA-5,** with the same
tolerances. The island rule is not applied (the synthetic sizes are tiny), and holes are compared as GEOS returns them. The column
"expected" is my own reading of the geometry, recorded so a disagreement is visible. **The grade is against GEOS, not against my
reading.**

| case | the input | expected (pieces, rings) |
|---|---|---|
| **PD-1**, a vertex on an edge, inside-touching | triangle (0.2, 0.2) (1, 0.5) (0.2, 0.8) | 1 piece (itself), 1 ring |
| **PD-2**, a vertex on an edge, outside-touching | triangle (1, 0.5) (1.5, 0.2) (1.5, 0.8) | 0 pieces |
| **PD-3**, an edge along the boundary, inside | square 0.5..1 × 0.3..0.7 | 1 piece (itself), 1 ring |
| **PD-4**, an edge along the boundary, outside | square 1..1.5 × 0.3..0.7 | 0 pieces |
| **PD-5**, a partial collinear overlap, crossing | (0.5, 0.3) (1, 0.3) (1, 0.5) (1.5, 0.5) (1.5, 0.7) (0.5, 0.7) | 1 piece, 0.5..1 × 0.3..0.7, 1 ring |
| **PD-6**, touching only a corner, outside | square 1..1.5 × 1..1.5 | 0 pieces |
| **PD-7**, touching only a corner, inside | square 0.5..1 × 0.5..1 | 1 piece (itself), 1 ring |
| **PD-8**, a ring equal to the rectangle | (0, 0) (1, 0) (1, 1) (0, 1) | 1 piece, the rectangle, 1 ring |
| **PD-9**, a hole touching the boundary along an edge | outer -1..2 × -1..2; hole 0.6..1 × 0.4..0.6 | 1 piece: the rectangle notched (the hole opens onto the edge), 1 ring |
| **PD-10**, a hole touching the boundary at one vertex | outer -1..2 × -1..2; hole triangle (0.6, 0.4) (1, 0.5) (0.6, 0.6) | 1 piece; **rings 2 (a hole touching the shell at a point) or 1 (a notch): GEOS decides**, and PA-3 follows it |
| **PD-11**, a ring containing the rectangle, with a hole crossing it | outer -1..2 × -1..2; hole 0.5..1.5 × 0.3..0.7 | 1 piece: the rectangle notched by the hole's part, 1 ring |
| **PD-12**, the rectangle inside a hole | outer -2..3 × -2..3; hole -1..2 × -1..2 | 0 pieces |

**Graded:** PD-1 to PD-12 each pass PA-2 to PA-5 against GEOS. **Recorded:** my "expected" against GEOS's answer, case by case.
- **Any PD case failing stops parity before the real run,** and is fixed only once the case is recorded.
- **A disagreement between my reading and GEOS** is recorded and is not a stop; GEOS is the truth. PD-10 is the case I expect could
  go either way.

**The order:** the PD suite with C-SELF first, then the controls (each shown failing), then PA-1 to PA-5 over the 2,584 real cells.

**PARITY, the PD suite's first run (16:47:12Z; `rectclip.ts` ca60fef7b580, `rectclip-run.mts` 5cbc9f0c8713, `verify.py` 6a32d7be1fac):
STOPPED at PD-9.** 11 of 12 pass PA-2 to PA-5 against GEOS, and in all 12 my registered reading agrees with GEOS's (pieces, rings),
including PD-10: GEOS keeps a hole touching the shell at ONE vertex as a hole, so 2 rings.
- **PD-9 FAILS PA-3, PA-4 and PA-5:** a hole flush with the rectangle's edge (0.6..1 × 0.4..0.6). GEOS makes it a NOTCH in the outer,
  one ring. My clip finds every hole vertex inside the CLOSED rectangle, so it keeps the hole as a hole touching the shell along an edge,
  which is an invalid polygon.
- **The fix, before it is applied:** an edge lying ON a side of the rectangle (both ends on the same side line) counts as OUTSIDE when
  chains are built. The walk may start after such an edge, as well as at a vertex strictly outside. A ring wholly inside except for
  boundary edges therefore yields chains, and stitching turns a flush hole into a notch.
- **Traced by hand for the cases it touches:** PD-3, PD-7 and PD-8 keep their shapes (PD-8, every edge on the boundary, falls to the
  centre test and gives the rectangle). PD-4 still gives nothing. PD-10 has no boundary edge, so it stays a hole.
- **The re-run:** the whole PD suite, then the controls, then the real cells, as registered.

**PARITY, the run (16:49:48-16:54:05Z): PASS.** The hand-written clip reproduces GEOS's pieces exactly over all 2,584 cells, so no
dependency is needed.

| artifact | sha256 |
|---|---|
| `rectclip.ts` (after the PD-9 fix) | a39c8762a6ef |
| `rectclip-run.mts` | 5cbc9f0c8713 |
| `verify.py` (after the encoding fix below) | 0951ccd447d1 |
| `sources.json` | 37e44482a988 |
| `export-sources.py` | 4aef18b68282 |
| the real run's output | 6c1b785301f9 |

A full clip of the 2,584 cells takes **about 37 s**.

| step | read | |
|---|---|---|
| **PD-1 to PD-12** (after PD-9's fix, recorded at e5beb78cef9e) | **12 of 12 pass** PA-2 to PA-5 against GEOS; my reading agrees with GEOS in all 12 | as registered |
| **C-SELF** | 0 failures over 1,914 pairs | as predicted |
| **C-IDX**, off by one | PA-1 fails (1,415 / 1,904 / 504 / 136). **PA-2 fails at 492 of the 1,435 cells**; PA-5 fails at all 1,448 matched pairs | **fails, as it must.** But see below: "at most of the cells" was not met |
| **C-SHIFT**, 1e-5° | PA-4 or PA-5 fails for **1,736 of 1,914** pieces (91%); Hausdorff 0.00044° | as predicted (at least half) |
| **C-AREA** | PA-4 and PA-5 fail at **exactly one** piece (16/-155) | as predicted |
| **C-HOLE** | PA-1 fails (632 kept, 0 dropped); PA-3 fails at **44 pieces, exactly those that held the 135 dropped islands** (counted: 44 pieces, 135 islands) | as predicted |
| **THE REAL RUN** | **PA-1:** 1,435 / 1,914 / 497 / 135, exact. **PA-2 to PA-5: 0 failures** over all 1,914 pairs. **Recorded:** Hausdorff max **1.42e-14°** (first written here as 0.0, which the grading output never said; corrected at step 2's run below), and 0 pairs whose vertex counts differ; even the total, 69,728 vertices, is the reference's | **PASS** |

**Three things on record:**
- **C-IDX's magnitude was mis-stated.** I registered "at most of the 1,435 cells", and PA-2 failed at 492 (34%). A full open-ocean cell
  clipped by its neighbour's rectangle keeps its piece count and its area in degrees, so only PA-5 sees the move, and it fails at every
  matched pair. The control is not blind; my count was wrong.
- **An instrument defect, in the verifier, not the clip.** The first grading of the real run read PA-2 = 7, all in cells holding four
  lakes with accented names. `verify.py` opened the clip's UTF-8 output with Windows Python's default code page, cp1252, and garbled the
  names, so the source-matched pairing refused them. The geometry already matched: Hausdorff 1.42e-14° (first written as 0.0; corrected
  at step 2's run below). Opening every JSON file as UTF-8
  cleared it, and every grade was then re-read.
- **A finding for the code PR's ingest.** Natural Earth's `ne_10m_lakes.cpg` declares **UTF-8**, and the DBF's bytes are UTF-8. The dry
  run decodes them as latin-1. So the reference, and the prototype store's sea `src` attributes, carry garbled names for **Réservoir
  Baskatong, Réservoir Outardes Quatre, Réservoir Pipmuacan and Lake Péribonca** (7 cells).
  - **The code PR decodes by the `.cpg`.**
  - **PARITY's name match then maps the reference's names through latin-1 → UTF-8** before comparing. This is registered now, before
    that ingest exists.

### PARITY step 2: the full TypeScript ingest against the same reference, registered before any code (the orchestrator, 16:56:18Z)

**Accepted (16:56:18Z):** step 1, with PD-9's shape kept as practice. C-IDX's magnitude is my miss. The cp1252 read is now standing
practice for every session: open text as UTF-8.

**What step 2 adds to step 1's clip.** All of it is TypeScript, reusing the repo's own `proxy/scripts/basemap/shapefile.ts`, which reads
each part bit for bit and decodes DBF text as UTF-8, `fatal`:
1. **the extent:** the 1-degree cells within 250 km of a covered point, from `coverage.bin` by `sea-extent.py`'s own definition
   (haversine, R = 6371.0088), ported line for line;
2. **the sources:** the ocean record polygonized by orientation (outers clockwise, holes counter-clockwise) with holes assigned by
   containment. The lakes are polygonized the same way, a lake's parts kept together;
3. **design (a):** a lake is kept if its area is at least 1.5 km² (deg² × 111² × cos of the latitude of its area-weighted centroid,
   holes included) and it is NOT wholly inside the US (the admin_0 USA parts). "Wholly inside" means every vertex inside or on the US,
   and no edge crossing a US edge;
4. **the cut and the island rule:** step 1's clip, unchanged (`rectclip.ts` a39c8762a6ef).

**Read from the reference before registering:**
- 9 lakes have several parts, and **none of their parts overlap**, so a lake's union is its parts.
- **Every hole, ocean or lake, is in exactly one outer.**
- **No lake is within 0.001 km² of 1.5 km².**
- The US is **344 parts with no holes.**
- **15 selected lakes touch or cross the US without being inside it.** The containment test must say "not contained" for exactly those.

**Graded:**

| row | what | passes when |
|---|---|---|
| **PB-0**, the extent | the TS ingest's cells against `sea-extent-cells.json` | the **same 2,584 cells** |
| **PB-1**, the sources | ocean outers and holes; lakes read, selected, wholly inside the US, under 1.5 km² | **exactly** 2 / 6,820; 1,355 / 1,168 / 149 / 38 |
| **PB-2**, the selection | the selected lakes, by their record index in the `.shp` | **the same 1,168 records** as the dry run's |
| **PA-1 to PA-5** | the full ingest's pieces against `sea-pieces.json`, as in step 1 | as in step 1 (1,435 / 1,914 / 497 / 135, then 0 failures) |

**Names:** the TS ingest reads the DBF as UTF-8, which is correct. The dry run read it as latin-1. So **the reference's names are mapped
latin-1 → UTF-8 before PA-2's source match.** All four lakes above are fixed by that one mapping.

**The controls, each shown FAILING first** (plants in a copy, never in the ingest):

| control | the plant | predicted |
|---|---|---|
| **C-EXTENT** | 250 km becomes 240 km | **PB-0 fails** (fewer cells) |
| **C-US** | the US test off | **PB-1 and PB-2 fail**: 1,317 lakes selected (1,168 + 149) |
| **C-BORDER** | "wholly inside" by vertices only, with no edge-crossing test | **PB-2 fails only if** a selected lake has every vertex inside the US and an edge crossing out. **Predicted: it does NOT fail.** Then the edge test is untested by the data, and that is reported, not hidden |
| **C-SIZE** | 1.5 km² becomes 2.0 km² for lakes | **PB-1 fails** (fewer selected) |
| **C-NAME** | the latin-1 → UTF-8 mapping removed from the comparison | **PA-2 fails at exactly the 7 cells** of the four lakes |

**The verdict:**
- **PASS:** PB-0 to PB-2 and PA-1 to PA-5 all hold, and every control except C-BORDER fails as stated.
- **C-BORDER passing** is a registered outcome: a guard the data cannot exercise, said plainly.
- **Anything else stops.**

**ACCEPTED (16:59:17Z), with C-BORDER-S added before the freeze.** C-BORDER on the real data is predicted to pass, which would leave the
edge test a guard nobody has seen reject anything. So a SYNTHETIC case must fail without it.
- **The test "US":** the square (0, 0)..(10, 10) with a V-notch cut down from its top edge, clockwise: (0, 0) (0, 10) (4, 10) (5, 5) (6, 10)
  (10, 10) (10, 0).
- **The lake:** 3..7 × 6.5..7. **All four of its vertices are inside the test US**, but its top edge (y 7) crosses the notch between x 4.6
  and 5.4, and its bottom edge (y 6.5) between 4.7 and 5.3. **With the edge test it reads NOT inside,** so it is selected. **With
  vertices only it reads inside,** so it is dropped.
- **Predicted:** full mode **not inside**; vertices-only **inside**, which is **RED**, the control failing as it must. Both are run and
  shown. The containment code moves into one module, `parity/uscontain.ts`, used by both the ingest and this selftest, so the code under
  test is the code that ships.

**PARITY step 2, the run (17:02:12-17:09:56Z): PASS.** The full TypeScript ingest reproduces the dry run's extent, sources, selection
and pieces exactly. It starts from the pinned zips and `coverage.bin` and uses no Python.

| artifact | sha256 |
|---|---|
| `sea-ingest.mts` | 45500edf4529 |
| `uscontain.ts` | 6f7e14013016 |
| `rectclip.ts` (step 1's, unchanged) | a39c8762a6ef |
| `cborder-selftest.mts` | b38183b32106 |
| `verify2.py` (PB-0 to PB-2, then `verify.py`'s `compare`) | bd3766b3161d |
| `verify.py` (unchanged) | 0951ccd447d1 |
| `cname.py` (lists C-NAME's failing cells) | 44abf915d935 |
| `mk-plants.py` (each plant in a copy; asserts one match and a changed copy) | 7fd456d9ebfe |
| the planted copies: C-EXTENT, C-US, C-SIZE, C-BORDER | 0690184b5d73, 059fb439526c, 6e4c79189adc, 94eb103f37a2 |
| the real run's pieces | 09db20fc5a86 |

The ingest takes **about 47 s**. Its stderr was empty on every run, and every control ran from a planted copy with no option passed.

| step | read | |
|---|---|---|
| **C-BORDER-S** | full mode: **not inside**; vertices only: **inside** | as predicted: the edge test catches the notch |
| **THE REAL RUN** | **PB-0:** the same 2,584 cells. **PB-1:** 2 / 6,820; 1,355 / 1,168 / 149 / 38, exact (and 0 holes outside exactly one outer). **PB-2:** the same 1,168 records. **PA-1:** 1,435 / 1,914 / 497 / 135. **PA-2 to PA-5: 0 failures** over 1,914 pairs. **Recorded:** Hausdorff max 1.42e-14°, 0 pairs whose vertex counts differ, 69,728 vertices | **PASS** |
| **C-EXTENT** | **PB-0 fails:** 2,538 cells, 46 missing. Also PA-1, and PA-2 at 36 cells | as predicted |
| **C-US** | **PB-1 fails** (**1,317** selected, 0 inside the US). **PB-2 fails** (149 extra records). Also PA-1, and PA-2 at 197 cells | as predicted, 1,317 exact |
| **C-SIZE** | **PB-1 fails** (1,167 selected, 39 small). **PB-2 fails** (record 528 missing). **PA-1 to PA-5 pass:** the pieces are byte-identical to the real run's | PB-1 as predicted; see below |
| **C-NAME** | **PA-2 fails at exactly 7 cells**, the cells whose reference names the mapping changes: 46/-76, 46/-77, 47/-76, 49/-69, 49/-70, 49/-71, 49/-72 (four lakes) | as predicted |
| **C-BORDER** | **passes**; the pieces are byte-identical to the real run's | **the registered outcome:** the real data does not exercise the edge test. Only C-BORDER-S does |

**On record:**
- **The registration named one row per control, and three controls also failed rows it did not name.** C-EXTENT and C-US also fail
  PA-1 and PA-2: those follow from fewer cells and from extra lakes. C-SIZE also fails PB-2. No registered row went the other way, so by
  my reading this is not a stop. It is still a predicted set that was incomplete.
- **C-SIZE leaves the shipped output unchanged.** Only one lake that design (a) selects lies between 1.5 and 2.0 km², record 528, and it
  has no piece in the 2,584 cells. So PB-1 and PB-2 see the 1.5 km² threshold, but the pieces do not: with today's data and extent, any
  threshold in that range ships the same sea.
- **A correction to step 1's record: its Hausdorff max was 1.42e-14°, not 0.0.**
  - Step 1's grading output (16:54:05Z) printed 1.4210854715202004e-14. The record said 0.0 in two places, and so did my report. Both
    places are corrected above.
  - The same 7 pairs carry it in both steps. 1.42e-14 is 2⁻⁴⁶, the spacing of doubles between 64 and 128, so it is a last-bit
    difference. Which vertex carries it has not been read.
  - It is ungraded: PA-4 and PA-5 allow differences 8 orders of magnitude larger. The verdict stands; the number was wrong.
- **Step 2's pieces equal step 1's coordinate for coordinate, except in order.** One Whitefish Bay hole in 46/-85 starts one vertex
  later. Two Whitefish Bay triangles in 47/-85 are listed in the other order. Every other piece is identical in place. Nine pieces now
  carry the four lakes' UTF-8 names. Why the start and the order differ has not been read: the difference is ungraded, and the geometry
  is equal.
- **The ingest carries option seams** (`--lim`, `--us`, `--min-lake`), off by default. The registration put plants in copies, so the
  seams went unused: the real run and every control passed no option.

**Next:** KB4 (B')'s registration, before any code.

**ACCEPTED (17:14:50Z).** The extra rows the controls tripped are my miss, on record: a control's registration names its FULL red set.
Before the code PR: read why step 1 and step 2 order the Whitefish Bay rings differently, and confirm the ingest is deterministic run
to run (QW4-ID's determinism control).

### KB4 (B'): the owner's ruling (2026-11-12 16:57Z), registered before any code

**The ruling, in the owner's words** (relayed at 17:01:56Z): "(B'). Repair, not stop; my island-size stop is withdrawn. The repair has a
frozen limit, then stops. Repairs are counted on the build record. The added bytes are predicted at each New York point, which sits
near the 32 KB cap, including any rung change. (C) still fails first on a planted crossing."

**Withdrawn:** 16:24Z's (B): the threshold T, the drop under T, and `sea_island_crossing`. **(A) and (C) stand as registered.**

**(B'), a new step 4a in the sea path, between step 3 (simplify) and step 4 (bridge):**
1. **When it runs:** a piece's quantised outer and kept islands, after step 3, have a PROPER crossing between two DIFFERENT rings
   (outer and island, or island and island). That is exactly what makes today's step 5 refuse a piece. A contact (a touch, a shared
   vertex, a collinear overlap) is not a crossing, as in step 5.
2. **A round:** every ring keeps its own step-3 keep set and is quantised again. Every edge in a proper crossing, with another ring's
   edge or with its own, gets back DP's next choice: the unkept original vertex farthest from the edge's chord, the first maximum
   winning. That is `simplifyRingTPLocked`'s own rule. Locked vertices are already kept.
3. **Rounds repeat** until no ring crosses another or itself. Then step 4 bridges and step 5 runs as today.
4. **THE BUILD STOPS** (500), logged, in two cases, and nothing is drawn for that point:
   - **`sea_repair_failed`:** a round restores nothing and a crossing is left (only a source that itself crosses can do this);
   - **`sea_repair_limit`:** the piece's restored vertices exceed the frozen limit.
5. **THE LIMIT, FROZEN: 64 vertices restored per piece.** A repair is local, a few vertices on the crossing edges. 64 caps its cost
   near 64 × 6 B = 384 B, an eighth of KB3's 3,072 B sea budget. It was fixed before any prediction below ran; they all ran with it.
6. **On the build record** (`bm_build`; `q4Out.sea` in the prototype):
   - **`seaRepairs`** (pieces repaired) and **`seaRepairVertices`** (vertices restored);
   - one `sea_repair` log line per repaired piece: its key (file/record), the rings involved, the vertices restored, the rounds, and
     the island's point (its centroid, lon/lat to 3 decimals).
   - None of this is in the package's PROVENANCE, so it costs no package bytes.
7. **Placement:** before KB1's reference `q` is taken, so KB1 compares the bridged ring with the same repaired rings. The "cross" and
   "nobridge" plants act after it. The "order" plant's branch (bridge before simplify) has no step 4a.
8. **(C)'s count:** a new `refused5`, the step-5 refusals, kept apart from step 3's.

**How the predictions were made.** The tools are read-only, and nothing in pq4d changed.

| tool | sha256 | what |
|---|---|---|
| `sea-dump.mts` | 21d41711c7ff | the builder's own sea input (the store read as `build.ts` reads it) at the 16 points, at ε, ×2 and ×8, with TODAY's `seaRings` output; also KB8's seam store at KBOS and MIDTOWN, and the edge-unlocked path at KBOS |
| `est-repair.py` | 7202f48d2b2c | an INDEPENDENT Python port of steps 2-5, then (B') |
| `est-refit.py` | cb5d9ca5eaa0 | rung 7's refit, from today's measured packages |
| `kb4-pred.py` | a89fd917192b | the predicted packages; KB4 under (A), through `p2-render.py`'s own kb4 code |

**Their anchors, each passed before any prediction was read:**
- **The port reproduces today's `seaRings` exactly** (fate and ring): 753 pieces at 48 point-tolerances. On the seam store: 162 pieces
  at 6. On the unlocked path: 90 pieces at 3.
- **Today's 16 packages, rebuilt read-only, equal run 6's kept outputs** (water, sea positions, fates) at all 16.
- **`kb4-pred` re-grades today's KJFK package** to run 6's 0.9706 / 0.4312, and to the diagnostic's in-square 0.745 over 11,222 px.
- **Every tool at its final sha reproduces its outputs byte for byte.**

**WHERE (B') FIRES, predicted: ONE piece. Record 18 (ocean, cell 40/-74, the New York Bight), at ×8 only, at KJFK, KLGA, KEWR and
MIDTOWN**, and in KB8's seam store at MIDTOWN. Nowhere else: no other piece, at any point or tolerance, has two rings crossing after
step 3. That includes the unlocked path at KBOS.

| record 18 at each of the four (the rings differ frame by frame; the counts agree) | |
|---|---|
| the rings crossing | its outer and its one island, Jones Beach Island |
| the repair | **1 round, 4 vertices restored:** the outer 27 → 29, the island 3 → 5 |
| drawn | **36 vertices, 138 B** after bridging; 0 proper crossings |

**THE ADDED BYTES at the New York points.** The cap measures 32,768 B of non-terrain bytes. All four sit at rung 7: every secondary road
already dropped, and water rings dropped smallest-area first until the package fits.

| point | today, non-terrain B (headroom) | + record 18 | then over by | rung 7's extra drops (smallest first) | after, non-terrain B | `dropped.water` | water rings | sea bytes | sea rings |
|---|---|---|---|---|---|---|---|---|---|
| **KJFK** | 32,732 (36) | +138 | 102 | **5** water rings, 9.8-10.1 km² each | **32,752** | 81 → 86 | 97 → 93 | 771 → **909** | 13 → 14 |
| **KLGA** | 32,736 (32) | +138 | 106 | **5** water rings, 17.9-18.8 km² | **32,760** | 111 → 116 | 62 → 58 | 732 → **870** | 13 → 14 |
| **KEWR** | 32,742 (26) | +138 | 112 | **4:** 3 water rings, 62.3-74.0 km², and **1 sea piece, record 27** (ocean, cell 41/-74), 85.7 km² | **32,763** | 137 → 141 | 28 → 25 | 621 → **730** | 11 → 11 |
| **MIDTOWN** | 32,760 (8) | +138 | 130 | **4** water rings, 18.8-20.0 km² | **32,760** | 116 → 120 | 56 → 53 | 649 → **787** | 13 → 14 |

- **No rung change and no radius change at any of the four:** rung 7 and 129.6 km stand. Rung 7 absorbs the ring by dropping more of
  its smallest water.
- The survivors' areas have **no ties**, so the drop order is unambiguous.
- No provenance count changes digit. `collapsed.sea` counts collapses only, so record 18 moving from refused to drawn changes no
  provenance line except `dropped.water`.

**Every row's prediction for the re-run** (p2-read with (A), (B') and (C), in the registered order):

| row | predicted |
|---|---|
| STEP 0 | as run 6 (store e9ef04165ba2; 246 cells; 170 pieces) |
| **(C)'s control, FIRST, on today's code** (before (B') is written): `xisland` | **RED: 1 step-5 refusal.** Today's code counts refusals together, and xisland has no step-3 refusal |
| **xisland under (B')** | **repaired: 1 piece, 2 vertices restored over 2 rounds (the bulge's two corners, one a round); drawn; step-5 refusals 0.** Traced by hand, not by the port: the island survives only at ε (at ×2 and ×8 its 300 m width collapses it), so the repair runs at ε |
| **S-KB-REPAIRLIMIT** (`Q4_SEAPLANT=limit1`, the limit at 1) on xisland | **STOPS: 500 `sea_repair_limit`, 2 restored** (2 > 1 after round 2) |
| **xisland-tall** (the island's far edge at 20.25 km, beyond the 20.18 km bulge, so it crosses its ORIGINAL outer) | **STOPS: 500 `sea_repair_failed` in round 3, 2 restored** |
| the other synthetic controls (kb1, kb1c, edge2, kb2cross, tsea0, bowtie) | **as run 6, each with `seaRepairs` = 0.** Their islands touch or sit inside their outers; bowtie is refused at step 3 |
| P2-0 and T-SEA0-real | as run 6 (no sea layer) |
| **P2-1, the 12 others** | **byte-identical to today's** (sha256): KBOS f21d705d8b8a, KMIA 1a338238fbc8, KSFO 4d03beecdb4b, KSEA 9bd5b34d6718, KORD cb59bb06a506, KDTW 682208bf2fb2, KMSP b694c01dc502, KIND 89a4195fa4be, KJAC 42c32cd8578e, BUFFALO 52fad5e317f3, ROUSES afe7554245b8, ZAPATA e2ddaa00ecaf; `seaRepairs` 0 |
| **P2-1, KJFK** | kOk, 0 crossings, rung 7, 129.60 km; sea {drawn 14, refused 0, collapsed 3, outside 5, dropped 2, islandsDropped 0, islandsSmall 0, bridges ~~5~~ **4** (re-registered: the owner, 18:03Z), bytes 909, rings 14}; `seaRepairs` 1, `seaRepairVertices` 4 |
| **P2-1, KLGA** | the same shape: {14, 0, 3, 5, 2, 0, 0, bridges ~~5~~ **4** (re-registered), bytes 870, rings 14}; repairs 1, vertices 4 |
| **P2-1, KEWR** | {drawn 11, refused 0, collapsed 2, outside 9, dropped 4, 0, 0, bridges ~~4~~ **3** (re-registered), bytes 730, rings 11}; repairs 1, vertices 4 |
| **P2-1, MIDTOWN** | {14, 0, 3, 5, 2, 0, 0, bridges ~~4~~ **3** (re-registered), bytes 787, rings 14}; repairs 1, vertices 4 |
| **(C)**, new and graded | **step-5 refusals 0 at all 16** |
| the lock's cost | at the 12 others, as run 6. At New York the row now includes record 18: KJFK 765 → 909 (+144 ≤ 192), KLGA 745 → 870 (+125 ≤ 192), KEWR 632 → 730 (+98 ≤ 156), MIDTOWN 669 → 787 (+118 ≤ 168). There it no longer isolates the lock, and it stays graded on its tolerance |
| KB3 | KBOS 1,415; **KJFK 909; KLGA 870**; KSEA 2,003; KSFO 694; KMIA 1,146: all ≤ 3,072 |
| KB8 | CAUGHT: (a) **142**, old 149. MIDTOWN's seam package changes, but its (a) 59 and old 65 do not. NOT CAUGHT: old 1, (a) 0. S-KB-EDGEUNLOCK: run 3's rings, old 10, (a) 9. KBOS's rings are run 4's. KB8 KBOS (a) 0, old 1; **KB8 MIDTOWN (a) 0**, old 4 |
| **KB4's control, KBOS** | complement frac 0.0136, cover 0.0093: fails both, as in run 6. (A) changes nothing on a 162 km square, whose square is the whole grid |
| **KB4, KBOS** | 0.9827 / 0.9907; scope as run 6; d* 5 km |
| **KB4's control, KJFK** | complement **0.0133 / 0.0169: fails both** |
| **KB4, KJFK** | **frac 0.9713, cover 0.9831: PASS.** Scope: 2 km 0.9399 / 0.9455, 5 km 0.8969 / 0.9120, 10 km 0.8358 / 0.8619, 20 km 0.7230 / 0.7532; **d\* 2 km** |
| **KB4's control, KLGA** | complement **0.0119 / 0.0209: fails both** |
| **KB4, KLGA** | **frac 0.9672, cover 0.9791: PASS.** Scope: 2 km 0.9343 / 0.9494, 5 km 0.8852 / 0.9021, 10 km 0.8203 / 0.8485, 20 km 0.6995 / 0.7448; **d\* 2 km** |
| KB4 and its control at KSEA, KSFO, KMIA | not yet read. (A) changes nothing on their 162 km squares; predicted as registered at 15:44Z: the complement fails both bounds and KB4 passes. Not computed before the run |
| KB5 | KJFK's new package: 0 differing pixels, its moved-vertex control first, as registered |
| KB7-a/b; KB7-c-real (BUFFALO, ROUSES, ZAPATA: unchanged packages) | as registered |
| P2-M | as registered (recorded; the 8 MiB plant control; no build over 96 MB) |
| pictures | to the owner, unopened |
| **VISIBLE COSTS** (to the owner, not stops) | the New York Bight's ~6,000 km² of ocean is drawn at all four. To make room, rung 7 drops more small water: KJFK 5 rings of ~10 km², KLGA 5 of ~18 km², **KEWR 3 of 62-74 km² and one 86 km² sea piece (record 27)**, MIDTOWN 4 of ~19-20 km² |

Two notes on these numbers:
- **The KB4 values at KJFK and KLGA are computed on the PREDICTED package.** The run grades the BUILT package, so a KB4 difference there
  means the package is not as predicted.
- **The complement's frac at KJFK and KLGA (0.0133, 0.0119) sits below 15:44Z's ungraded expected floor of 0.02.** The sea now covers
  nearly all the low ground, so the complement holds almost none of it. The graded condition, both bounds failing, holds.

**The verdict:**
- **PASS:** every row as stated.
- **Anything else stops,** in particular:
  - (B') firing on any piece but record 18 at ×8 at the four New York points (and MIDTOWN's seam store);
  - any STOP at a real point;
  - a rung or radius change at New York;
  - any of the 12 packages not byte-identical;
  - New York's non-terrain bytes, drops or sea bytes off the table.

**The order of work, after acceptance:**
1. `xisland` in `dr-p2-synth.mts` (harness only). Run it on today's pq4d: (C) RED. Recorded.
2. (B') in pq4d through mk-pq4d's sea part. That includes `refused5`, the build-record counts, the `limit1` plant, and `xisland-tall`
   as a synthetic geometry (not a plant). Every sha is pinned before the run.
3. p2-read gains (A) (KB4 inside the square), (C)'s row, the xisland rows and the repair counts. Pinned.
4. The re-run, in the full order. Part 2 resumes after the orchestrator has read this.

**ACCEPTED (17:36:32Z): part 2 resumes.** The visible costs go to the owner with pictures: before and after at each New York point,
with KEWR's 86 km² sea piece marked.

**(C)'s control, on TODAY's code, before any (B') code existed (17:38:08Z): RED, as registered.**
- The run: `dr-p2-synth.mts` (101265dfcab1, with `xisland` added) on pq4d's sea.ts 1c0797cc0627.
- xisland's piece was bridged, then refused at step 5: `refused` 1, fates `["refused"]`, 0 sea rings.
- The output is kept as `xisland-today.out` (13c62893795a).

**(B')'s code, pinned before the run (17:46Z).**

| artifact | sha256 | was |
|---|---|---|
| `pq4d-frame-lock.part.ts` | b367cb15ca8e | 90b5fcb26a99 |
| `pq4d-sea.part.ts` = `pq4d/.../sea.ts` | ab03ff2600d2 | 1c0797cc0627 |
| `mk-pq4d.mjs` | 27d3a9f5b4c0 | 38def76e437c |
| `mk-pq4d-sea.part.mjs` | 6f9850543279 | 1510af7df098 |
| `edit-mkpq4d-bp.py` (the generator edit, made identically in both files) | e0d3892936a1 | |
| `pq4d/.../frame.ts` | 637eeb5b1d62 | c82e3aad5a72 |
| `pq4d/.../build.ts` | 14e399bec33d | 4d7ee5e0a868 |
| `pq4d/.../layers.ts` | unchanged, 40484a780c86 | |
| `dr-p2-synth.mts` | cf3068a6a759 | 25daaeea80df |
| `dr-p2c.mts` | 55ab458ce281 | cd2d15be34e7 |
| `p2-render.py` | 952524878562 | a042ae1222d5 |
| `p2-read.py` | a4a35809e8d7 | a297d699b060 |
| `smoke-bp.mts` (the tooling smoke) | d72baea71070 | |

**What changed:**
- **frame.ts (the lock part):**
  - `simplifyRingTPLocked` is now `simplifyRingTPLockedState(...).q`, and the topology loop's restore is one function, `restoreNext`,
    shared with step 4a.
  - `crossRingEdges` and `repairRings` implement step 4a, with `SEA_REPAIR_LIMIT` = 64.
- **sea.ts:**
  - step 4a runs only when `crossRingEdges` finds two rings crossing. It first asserts its recomputed state IS step 3's rings
    (`sea_repair_state`, a builder guard), then repairs or throws `SeaRepairStop`.
  - The counts gain `refused5`, `repairs`, `repairVertices` and `repairLog`. The plant `limit1` sets the limit to 1.
- **build.ts:** catches `SeaRepairStop` around the content builds and answers 500 with its code. D2 retries only a 422, so a stop
  is final.
- **The drivers:** they print a stopped build's code and `why`, and the new counts.
- **p2-render's kb4:** grades under (A), and prints the whole-grid cover and the base beside it.
- **p2-read:**
  - (C)'s recorded control, xisland, the two STOP plants, and `seaRepairs` 0 in every other synthetic build;
  - the 12 shas and New York's exact figures;
  - (C) at the 16;
  - KB4's registered values at KBOS, KJFK and KLGA.

**The tooling smoke** (reads no graded row): exit 0, empty stderr.
- The regenerated modules load.
- **The refactored `simplifyRingTPLocked` equals the pre-(B') one on all 1,371 clipped rings** in the sea dumps (16 points × ε/×2/×8,
  the seam store, the unlocked path), with 0 differing. `waterFixStats` moves identically.

The run follows as registered: run 7.

**Run 7 (17:50:00-17:52:09Z; `p2-read.py` a4a35809e8d7): STOPPED at "(B') at KJFK".** Every row before it read as predicted:
- STEP 0;
- (C)'s recorded control;
- **xisland under (B'):** 2 vertices restored over 2 rounds, drawn, step-5 refusals 0;
- **S-KB-REPAIRLIMIT:** 500 `sea_repair_limit`, 2 restored;
- **xisland-tall:** 500 `sea_repair_failed` in round 3, 2 restored;
- all the synthetic controls, and `seaRepairs` 0 in the 13 other synthetic builds;
- P2-0 and T-SEA0-real at the 13;
- **P2-1 kOk with 0 crossings at all 16**;
- **KBOS byte-identical** (f21d705d8b8a).

**The off reading: the sea's `bridges` count.**
- **What matched at KJFK:** non-terrain bytes 32,752, `dropped.water` 86, 93 water rings, sea bytes 909, 14 sea rings drawn, 2
  dropped, 0 refused, `refused5` 0, and `sea_repair 16036/18 rings 0,1 restored 4 rounds 1 island 40.617,-73.417`.
- **What did not:** `bridges` reads 4, and I registered 5.
- P2-1's own lines show the same at the other three New York points: KLGA 4 (registered 5), KEWR 3 (4), MIDTOWN 3 (4).
  Their other P2-1 figures (sea bytes 870 / 730 / 787, drawn, dropped, repairs 1, 4 vertices) read as registered.

**The cause, read from the code after the stop: my prediction, not the code.**
- sea.ts counts a bridge when it is MADE (`n.bridges += b.bridges`), before step 5. Today's count therefore already included record
  18's bridge: the piece was bridged, then refused.
- Under (B') it is bridged once and drawn, so the count does not change. I added 1 to a count that already held it.
- The evidence was already in hand: `xisland-today.out` reads `bridges` 1 beside `refused` 1, and run 6's ×8 counts at KJFK read
  `bridges` 4 beside `refused` 1.
- **The alternative, and why the data excludes it:** record 18 drawn WITHOUT its island bridged would also leave the count at 4. But
  KJFK's sea bytes are exactly 771 + 138, and 138 B is the bridged ring: 36 vertices, 29 + 5 + the bridge's 2. The outer alone is 29
  vertices and fewer bytes.

**Proposed, not applied (a ruling first):** register `bridges` as today's count at the four New York points (KJFK 4, KLGA 4, KEWR 3,
MIDTOWN 3). Change nothing else, and re-run from the top as run 8. This is my miss: a predicted count derived without reading
where the count is taken.

**The owner's ruling (18:03Z, relayed at 18:05:18Z), verbatim:** "yes. Correct the one expected bridge count (a bridge is counted when
made, even if the piece is later rejected), keep the miss on record, and re-run from the top."
- **Re-registered:** `bridges` at the four New York points = today's counts: KJFK 4, KLGA 4, KEWR 3, MIDTOWN 3. Marked in the table
  above, the registered values struck beside them.
- **Nothing else changed.** `p2-read.py` 1f58939262fb (was a4a35809e8d7); the change is those four numbers and a comment.
- **The miss stays on record** (above). Another off reading goes to the owner.
- Run 8, from the top, follows this commit.

**Run 8 (18:09:35-18:12:00Z; `p2-read.py` 1f58939262fb; output b3103d71e8cb): (B') reads as registered on every row. The run STOPPED at
KB4 KSEA, a point (B') does not touch.**
- **As registered:**
  - xisland and the two STOP plants; the synthetic controls; `seaRepairs` 0 in the 13 other synthetic builds; P2-0 and T-SEA0-real;
  - P2-1 kOk at all 16;
  - **(B') at the four New York points, every figure exact** (`bridges` 4/4/3/3 as re-registered);
  - **the 12 others byte-identical;**
  - **(C): step-5 refusals 0 at all 16**;
  - the lock's cost within tolerance at all 16; KB3 (KJFK 909, KLGA 870);
  - KB8's controls and rows;
  - **KB4 at KBOS, KJFK and KLGA to 4 decimals**, complement and scope included;
  - KB4's control at KSEA (complement 0.0107 / 0.0808, failing both bounds).
- **THE OFF READING: KB4 KSEA, frac 0.8861** (bound 0.95). Cover 0.9192 passes (bound 0.90). Sea px 8,190.
- **KSEA's package is byte-identical to run 6's** (9bd5b34d6718), so this reading does not come from (B'). Run 6 stopped at KJFK and
  never reached Seattle, and this registration did not compute KSEA before the run.

**Read after the stop** (`kb4-diag-ksea.py` bdf63d3d269a and `kb4-ref-source.py` f744c8ed61ad, neither a graded row):
- **The alternative that comes first, that ×8's simplification draws sea over land:** it is real, but it is the smaller part.
  - The UNSIMPLIFIED store reference on the same grid reads **frac 0.9089**. The sea path's ε rings read 0.9093.
  - Of the package's 933 px above 0 m, **636 lie where the reference is sea too.** **297 lie where it is land:** that is ×8's
    shoreline error, 3.6% of the mask, about 2.3 points of frac.
  - **So the bound is unattainable at KSEA by the source itself.** Natural Earth's 1:10m coastline and the terrain disagree along
    Puget Sound's narrow channels at 1.341 km/px.
- **The source's own KB4 figures (frac / cover), and the packages already graded:**

  | airport | source | package | the build's loss |
  |---|---|---|---|
  | KBOS | 0.9847 / 0.9909 | 0.9827 / 0.9907 | 0.0020 / 0.0002 |
  | KJFK | 0.9778 / 0.9850 | 0.9713 / 0.9831 | 0.0065 / 0.0019 |
  | KLGA | 0.9747 / 0.9813 | 0.9672 / 0.9791 | 0.0075 / 0.0022 |
  | **KSEA** | **0.9089** / 0.9377 | 0.8861 / 0.9192 | **0.0228** / 0.0185 |
  | KSFO | 0.9958 / 0.9566 | not graded yet | |
  | KMIA | 0.9760 / 0.9977 | not graded yet | |

  Only Seattle's source fails a bound; KSFO's and KMIA's sources meet both. The packages' values at KSFO and KMIA were neither printed
  nor read.

**For the owner (KB4's third off reading), via the orchestrator. Nothing is applied.**
- **(i) KSEA as a recorded exception:** the source fails the bound there. KB4 stays absolute everywhere else. Run 9 from the top
  reaches KSFO, KMIA, KB5, KB7 and P2-M.
- **(ii) KB4 graded relative to the source where the source fails:** the package within a margin of the source's own figures. Any
  margin I name now would be chosen after seeing KSEA's 0.0228, so the number is his.
- **(iii) Fix the source at Puget Sound:** a finer coastline there. That is its own scope, and nothing ships from part 2 until it lands.

### KB4 at Seattle: the owner's ruling (2026-11-12 18:55Z, relayed at 19:07:57Z), registered before run 9

**The ruling, in the owner's words:** "Seattle is the one named exception to the 95% rule, recorded with its numbers: 88.6% map, 90.9%
source. Every other airport stays strict. Seattle's reading prints every run and must not fall below 88.6%, a guard for future runs.
Every airport reports its simplification loss. Seattle goes into the before-and-after pictures, with Puget Sound's narrow channels at
the closest zoom. A finer Puget Sound coastline goes on the after-v20 list, only if the pictures show it looking wrong."

**1. KB4's rule.** At KBOS, KJFK, KLGA, KSFO and KMIA, unchanged: frac ≥ 0.95 and cover ≥ 0.90.
- **KSEA is the one named exception to the 95% (frac) rule,** recorded with its numbers: source 0.9089 / 0.9377, package 0.8861 /
  0.9192.
- **The exception covers the 95% rule only,** so **KSEA's cover stays graded at ≥ 0.90**: it reads 0.9192. If the orchestrator reads
  the ruling as excepting both, the cover row is dropped before the run.

**2. THE KSEA GUARD.** KSEA's frac and cover print on every run, and **frac ≥ 0.886**, the owner's 88.6%, read literally. Below it,
the run STOPS.
- **Its exact margin, measured on today's package:** 7,257 of 8,190 sea px at ≤ 0 m, frac 0.886081. Cover is 3,129 / 3,404 =
  0.919213.
- **So the guard sits ONE PIXEL below today's reading:** one more land pixel under the sea gives 7,257 / 8,191 = 0.885972. A
  single-pixel regression at Seattle stops a run. That is the owner's number, and its tightness is stated here, not discovered later.
- **Its red control, run first: "one step worse" is one pixel,** the instrument's smallest step.
  - **The plant:** `p2-out/KSEA-sea-guardplant.json`, today's KSEA package plus one sea ring: a square 0.4 px wide around the centre
    of a single land pixel (elevation > 0 m, inside the square, outside the mask, with no mask pixel among its 8 neighbours). It is
    pinned by sha.
  - **Its landing check:** sea px 8,191, sea px at ≤ 0 m still 7,257.
  - **Predicted:** the guard reads **RED** (frac 0.885972).

**3. THE LOSS COLUMN, printed at every airport and ungraded except KSEA's guard:** source frac / cover, package frac / cover, and the
loss.
- p2-render's kb4 takes `sea-pieces.json` and renders the source's own mask on the same grid, as `kb4-ref-source.py` did.
- **Predicted at the four already read:** KBOS 0.9847 / 0.9909 against 0.9827 / 0.9907; KJFK 0.9778 / 0.9850 against 0.9713 /
  0.9831; KLGA 0.9747 / 0.9813 against 0.9672 / 0.9791; KSEA 0.9089 / 0.9377 against 0.8861 / 0.9192.

**4. KSFO AND KMIA, predicted from their sources** (packages not read):
- **source** KSFO 0.9958 / 0.9566, KMIA 0.9760 / 0.9977.
- **Predicted: KB4 PASSES at both.** KSFO fails only on a loss above 0.0458 (frac) or 0.0566 (cover). KMIA's frac fails only on a
  loss above 0.026, which is more than every loss read so far (0.0020 to 0.0228).
- Each one's complement control fails both bounds, as registered at 15:44Z.
- **Any off reading on KB4 goes to the owner.**

**5. THE PICTURES** (to the owner, unopened by me), after run 9:
- **New York**, before and after at each of the four points: today's package against (B')'s, the dropped rings outlined in red with
  their km², record 18 in orange, and KEWR's record 27 marked.
- **Seattle**, whose package (B') does not change, is drawn as **source against package**, the simplification's loss, at the CLOSEST
  zoom (8.04672 km across 240 px). Two views:
  - the closest zoom centred on KSEA itself, the home;
  - the same scale centred on Puget Sound's narrows (the Tacoma Narrows, 47.27, -122.55).

**6. On the record, CONDITIONAL and not work:** a finer Puget Sound coastline goes on the after-v20 list ONLY if the owner judges the
pictures to show it looking wrong.

**Every other row of run 9 keeps its registered prediction,** as read in run 8, and the order is unchanged. The KB4 block now reads:
the guard's control → then per airport, the complement control, KB4 (or KSEA's guard and cover), the scope, the loss line, and the
registered exact values at KBOS, KJFK and KLGA.

**ACCEPTED (19:11:03Z):** KSEA's cover stays graded, and the guard is read literally at 0.886, one pixel below today's reading.

**Run 9's pins (19:1xZ), before the run:**

| artifact | sha256 | was |
|---|---|---|
| `p2-read.py` | e6ba08196d55 | 1f58939262fb |
| `p2-render.py` (kb4 takes `sea-pieces.json`: the source's mask on the same grid) | 0d9291a37e4b | 952524878562 |
| `mk-ksea-guardplant.py` | 61098731d958 | |
| `p2-out/KSEA-sea-guardplant.json` (the guard's control) | 6e9f1124ff32 | |

- **The plant landed as registered:** its ring covers pixel (0, 0), a 484 m land pixel in the square's north-west corner, isolated from
  the mask. That gives sea px 8,191, with 7,257 at <= 0 m.
- **The source column's anchor:** run on the four airports already read, it reproduces `kb4-ref-source.py` exactly: KBOS 0.9847 /
  0.9909 (28,297 px), KJFK 0.9778 / 0.9850 (14,205), KLGA 0.9747 / 0.9813 (11,972), KSEA 0.9089 / 0.9377 (8,164).
- **KSFO and KMIA were not run,** so their packages stay unread until run 9.
- pq4d and every other driver are unchanged since run 8.

**Run 9 (19:13:27-19:15:31Z; `p2-read.py` e6ba08196d55): KB4 PASSES at all six airports as registered. The run STOPPED at KB5's
control, the first run ever to reach KB5.**
- **Every row through KB8 reads as run 8's.**
- **The KSEA guard's control: RED as registered** (sea px 8,191, frac 0.885972 against 0.886).

| airport | KB4 (frac / cover) | loss (frac / cover) | |
|---|---|---|---|
| KBOS | 0.9827 / 0.9907 | 0.0020 / 0.0002 | exact as registered |
| KJFK | 0.9713 / 0.9831 | 0.0065 / 0.0019 | exact as registered |
| KLGA | 0.9672 / 0.9791 | 0.0075 / 0.0022 | exact as registered |
| **KSEA** (the named exception) | **0.886081** / 0.9192 | 0.0228 / 0.0185 | the guard (≥ 0.886) and cover pass |
| KSFO | 0.9943 / 0.9549 | 0.0015 / 0.0018 | **passes, as predicted from its source** |
| KMIA | 0.9741 / 0.9981 | 0.0020 / -0.0004 | **passes, as predicted from its source** |

Every complement control fails both bounds. The scopes are printed: KSEA's d* is 2 km; KSFO's and KMIA's are 10 km.

**THE OFF READING: KB5's control,** "a vertex moved one pixel differs", reads **0 px**.

**The cause, read after the stop: the control's plant site, not the C++.**
- The control moves `lines[2]` of `kb5-kjfk.rings`, which is ring 0's first vertex, by +1 px. Ring 0 lies at (1517.6, -1754.3) px,
  about 100 km outside the 240-px view, so the move cannot change a pixel in view.
- **The moved C++, the unmoved C++ and the Python renderer are byte-identical** (99a9001c790a).
- The moved file does differ from the original, in exactly that coordinate, so the plant did apply. It landed where the instrument
  cannot see.
- **The alternative, that the C++ side ignores its input:** it is not excluded by this read, and the in-view plant below separates the
  two. If the plant moves a vertex in view and the C++ still reads 0 px, that is the C++ (or its input), not the plant site.
- The registration says "one ring's vertex moved one pixel must differ". The code chose its vertex without checking that it lies in
  view. **That miss is mine:** a plant whose landing was never checked against what the instrument sees.

**Proposed, not applied (a ruling first):**
- the control moves **the first vertex inside the view (2 to 238 px)**: ring 38, vertex 1, at (25.7, 114.9) px;
- a **landing check**: that vertex is in view, and the moved file differs from the original in it alone;
- predicted: the moved C++ differs from the Python renderer by **≥ 1 px**.
- Then run 10 from the top. Nothing else changes.

### KB5's control, re-registered for run 10 (the orchestrator, 19:21Z), before the run

**The ruling** (the orchestrator's, since KB5 is not a KB4 row): re-register as proposed, with three additions.
- The cause is accepted as a plant-site miss, mine, on record.
- **The alternative, that the C++ ignores its input, stays OPEN until run 10 separates it.**
- **The retry cap:** a second off reading on KB5 goes to the owner.

**1. The plant: ring 38, vertex 1, at (25.744337, 114.868434) px,** the first vertex inside the view (2 to 238 px).
- **Moved 1 px at right angles to the edge it ends** (from vertex 0 at (-21.088577, 152.205001)), along that edge's left-hand normal
  n = (-(y1 - y0), x1 - x0) / |edge| = (0.623373, 0.781925).
- So it moves to (26.367710, 115.650358).
- `kb5-plant.py` makes the move and asserts the registered vertex is the first in view.

**2. The landing check, run BEFORE the run (19:22Z), in two parts:**
- **(a)** the moved file differs from the original in that vertex's line alone: **1 line**. OK.
- **(b)** the reference (Python) render of the moved file against the unmoved one: **21 px differ**. OK, so the instrument sees the plant.
  - It used `p2-render.py kb5r`, a new mode that renders a `.rings` file back with the same `fill_ring`. **Its anchor:** the unmoved
    rings re-render to run 9's `kb5-kjfk.py.bin` byte for byte (17,043 water px).
  - Had (b) read 0 px, the rule was STOP, with no second vertex tried.
- Both checks also run in run 10 itself, as one row ahead of the control.

**3. Predictions for run 10:**
- **KB5's control:** the moved C++ against the unmoved Python differs by **≥ 1 px**.
- **KB5's main row:** the unmoved C++ equals the Python renderer, **0 px**, as registered.
- **KB5's pair:** the moved C++ against the moved Python reads **exactly what the unmoved pair reads** (so 0 px, if the main row holds).
- **Every KB4 row: run 9's numbers exactly,** checked to 4 decimals at all six airports. That includes the complement, the scope,
  d* and the source; the KSEA guard's control is RED, and KSEA reads 0.886081.
- **Every other row as run 9's.** Then the rows run 9 never reached: KB7-a/b, KB7-c-real and P2-M as registered, and the pictures
  (the lakes'), unopened.
- **The escape hatch:** any reading outside this set STOPS, with the instrument in doubt. A second KB5 off reading goes to the owner.

**Run 10's pins:**

| artifact | sha256 | was |
|---|---|---|
| `p2-read.py` | 9eaf229657ec | e6ba08196d55 |
| `p2-render.py` (+ `kb5r`) | ce0f176ea317 | 0d9291a37e4b |
| `kb5-plant.py` | 11b99bf9fb47 | |

pq4d, the stores, the kb5 binary and every other driver are unchanged since run 9.

**Run 10, first attempt (started 19:24:11Z): ABORTED by me at about 19:25:20Z, before it printed anything.** The pinned driver was
broken, which makes this a process failure and not a reading.
- **What happened:** the orchestrator asked whether the landing row was p2-read's only change from run 9. Checking found it was not.
  - My edit replaced everything from the `K4P` literal to the KB4 loop.
  - That span also held run 9's **KSEA guard block**: `KSEA_GUARD = 0.886`, and the guard's red-control row.
  - So the pinned p2-read 9eaf229657ec had lost a registered row, and would have crashed at KSEA (`KSEA_GUARD` undefined).
- **The run was stopped by its task.** No run-10 process survived (checked by command line), and its output is 0 bytes.
- **Step 0** had rewritten both stores' indexes at 19:24:15-16Z. The next run's step 0 rebuilds them before anything reads them.
- **This is the editor-anchor row's shape, mine:** a span bounded by two unique anchors deletes whatever lies between them, and
  uniqueness says nothing about what that is.

**The fix and its proof:**
- **The guard block is restored VERBATIM**, its text taken from the session record of run 9's own edit.
- **Proof that nothing else changed:** on the repaired `p2-read.py` (7ad9bcdbb63a), reversing exactly three edits reproduces run
  9's pin **e6ba08196d55 byte for byte**:
  - (i) the KB5 block, back to its original text (taken verbatim from the session record of the Write that created it);
  - (ii) the `K4P` literal back to its three airports, and `gotv` without the source;
  - (iii) the exact-values row's label.
- So the change from run 9 is exactly: the KB5 block (the plant via `kb5-plant.py`, the landing row, the control comparing moved C++
  with unmoved Python, the pair row), `K4P` extended to six airports with the source, and that row's label. Nothing else.

**Run 10's pin, corrected: `p2-read.py` 7ad9bcdbb63a** (the aborted 9eaf229657ec is kept as `p2-read.run10-aborted.py`). The
re-registration above is unchanged. Run 10 goes from the top when the orchestrator says go.

**GO (19:30Z).** The guard's two rows are named rows, not "everything else": the control must read 8,191 px and 0.885972, and the
guard must pass at 0.886081.

**Run 10 (19:30:23-19:34:40Z; `p2-read.py` 7ad9bcdbb63a, pins re-verified before the start; output fffce461d302): PASS.
"every stop row held": 159 rows as predicted, 0 off.**
- **Every row through KB8 reads as run 9's.** That includes the (B') rows at the 16 and (C).
- **The KSEA guard's control: RED,** 8,191 px and 0.885972. **The guard passes at 0.886081.**
- **KB4 at all six airports: run 9's numbers exactly,** to 4 decimals, complement, scope, d* and source included. That also covers
  step 0's index rebuild after the aborted attempt.
- **KB5, the first time it has run end to end:**
  - its plant landed: (a) that one line, (b) 21 px in the reference render;
  - **the control: the moved C++ differs from the unmoved Python by 21 px;**
  - **the main row: the C++ FillRing equals the Python renderer, 0 px** (17,043 water px);
  - **the pair: moved C++ against moved Python reads 0 px, the same as the unmoved pair.**
  - **The open alternative is now EXCLUDED:** the C++ reads its input, because the moved C++ differs by exactly the Python's 21 px.
- **KB7-a and KB7-b, first run:** their (b) controls fail (rel 0.50 / 0.94 / 0.47 / 0.61). Lake Erie, Lake Champlain, Falcon Lake
  and Amistad Reservoir are each selected, and their pieces equal the NE lake (rel ≤ 1.9e-15).
- **KB7-c-real, first run:** every border-lake piece in view is DRAWN, at BUFFALO, ROUSES and ZAPATA.
- **P2-M, first run:**
  - the 8 MiB plant control holds: cell +7.95 MiB, later phases about -0.08;
  - **the sea's memory, recorded:** at most +0.99 MiB at any phase (KSEA's layers), +0.81 at New York;
  - every peak is 34.5 to 74.8 MiB, under the 96 MB stop.
- **The pictures** (BUFFALO, ROUSES, ZAPATA), to the owner unopened: `C:\Github\_handoffs\map-server\part2-run10-pictures\`,
  BUFFALO 7cde06eecf7c, ROUSES fe51d5ee2652, ZAPATA 67960a146809.
- **VISIBLE COSTS (to the owner, not stops): the sea layer against the no-sea build:**
  - KBOS: rung 3 → 4; dropped.water 0 → 45; dropped.roads 11,710 → 11,718; secondaries kept 8 → 0; non-sea rings 222 → 180.
  - KJFK: dropped.water 44 → 86, non-sea rings 119 → 79. KLGA: 75 → 116, rings 83 → 44. KEWR: 116 → 141, rings 35 → 14. MIDTOWN:
    82 → 120, rings 75 → 39.
  - Secondary roads kept: KSFO 719 → 640, KSEA 342 → 209, KORD 276 → 267, KDTW 386 → 349, KMSP 18 → 15, KIND 335 → 324, BUFFALO
    949 → 881, ROUSES 913 → 838.

**Part 2's rows are now all run, and every one reads as registered.** On record from the runs that stopped:
- run 7: the bridge count (my miss; re-registered by the owner);
- run 8: Seattle, the named exception, by the owner's ruling;
- run 9: KB5's plant site (my miss; re-registered);
- run 10's first attempt: the dropped guard block (my miss; restored and proven).

Still owed before the code PR:
- why the Whitefish Bay hole's start vertex and the triangles' order differ between steps 1 and 2, and the ingest's determinism run to
  run (17:14:50Z);
- the owner's verdict on the pictures, which decides the conditional Puget Sound item.

**ACCEPTED (19:37Z): part 2 is DONE, apart from the owner's picture verdicts.**

### (a) before the code PR: the ingest's determinism, and the Whitefish Bay ordering (19:3xZ)

**The determinism half, run BEFORE it was registered: my miss, on record.** I ran it at 19:37Z on my own "next, unless ordered
otherwise", before the orchestrator's go asked for pre-registration. Its outcome space was binary (byte-identical, or not), but I
wrote neither outcome down first. The result: two fresh runs of `sea-ingest.mts` (45500edf4529) and the 17:02Z run produce
**byte-identical pieces** (09db20fc5a86, all three) and equal stats (`ms` aside).

**The Whitefish Bay half, REGISTERED before it is read:**
- **The separating question:** do step 1's source rings for Whitefish Bay (`parity/sources.json`, exported from the dry run's shapely
  geometries) and step 2's (the shapefile's raw parts, read by `proxy/scripts/basemap/shapefile.ts`) hold the SAME vertex sequences
  up to a cyclic start or the order of the parts, or different vertices?
- **Why that separates the causes:** both steps clipped with the same `rectclip.ts` (a39c8762a6ef), so any difference in the output
  comes from the input. The dry run passes each lake through shapely's `unary_union`, which REBUILDS rings, choosing its own start
  vertex and part order. The TS ingest keeps the file's own.

| outcome | what it means | next |
|---|---|---|
| **O1:** the same vertex sequences, differing only by the cyclic start and/or the order of the parts | the dry run's `unary_union` reordered them; the TS ingest keeps the shapefile's own order, which is deterministic (above) | **no change to the ingest.** The id cannot vary run to run. Recorded |
| **O2:** the vertices themselves differ | step 1's and step 2's sources differ in content, which contradicts PARITY step 2's exactness | **STOP:** the instrument is in doubt |
| **outside the set** (identical sequences, or anything else) | the clip itself would be ordering by something that varies | **STOP** and report |

**The read:** `wb-dump.mts` dumps the raw parts of every NE lake record named Whitefish Bay through `shapefile.ts`, and `wb-cmp.py`
compares them with `sources.json`'s Whitefish Bay polygons ring by ring: exact equality, equality up to rotation, equality up to
reversal, and the parts' order. Counts and indices only.

**The read (19:4xZ; `wb-dump.mts` ef9e4d444a41, `wb-cmp.py` d06fe43c2b32, `wb-raw.json` 9a4b4e18607b): O1, as registered.**
- **The shapefile holds ONE Whitefish Bay record (468) with three raw parts, in file order:** a triangle (3 vertices, open), the bay's
  outer (102) and its island (14).
- **Step 1's sources hold the same three rings, every vertex the same,** as two polygons:
  - the outer with the island as its hole;
  - then the triangle.
- **Each ring is the raw part ROTATED BY ONE:** the same sequence, starting one vertex later. None is reversed.
- So the dry run's `unary_union` rebuilt each ring one vertex on, and put the triangle last. That is exactly step 1's differences
  from step 2: the hole in 46/-85 starting one vertex later, and the two triangles in 47/-85 in the other order.
- **The TS ingest keeps the shapefile's own order,** and three runs are byte-identical. So nothing in the ingest's order can vary run
  to run, and the store's id cannot either. **No change to the ingest.**

**ACCEPTED (19:43Z).** The determinism miss stays on record as mine. The code PR plan is accepted as scoped:
- (i) ONE PR, as Q-W4 registered;
- (ii) #652 merges FIRST, carrying this file and the re-basing below;
- (iii) the frame-time row runs on map-client's frame bench, on E2:08.
The PR opens only after the owner's picture verdicts.

## d0: the code PR's predictions RE-BASED on the sea store, and its frame-time row (frozen before any code)

**What stands unchanged:** every frozen code-PR row on the NO-sea store:
- "The code PR's frozen predictions" (CM-0, CM-P, CM-M, the memory rows, R-KMSP, R-D2) and their 22:38Z amendments;
- the per-point package rows ("The code PR's package row, narrowed point by point").

They predict the code PR where it builds no sea, and that is still the code PR's behaviour on a store with no sea layer.

**What part 2 adds: the code PR on the SEA store.** Each row's figures are part 2's measured ones (run 10, the pinned prototype pq4d).
The 16 packages were rebuilt read-only at 19:45Z, and each equals run 10's.

| row | input | predicted |
|---|---|---|
| **SEA-PKG** | the code PR's builder on the PROTOTYPE's sea store (`sea-obj`, store e9ef04165ba2), at the 16 points, at the client's query, as P2-1 | **byte-identical to run 10's packages.** KBOS f21d705d8b8a, KJFK 52ac9dc39de0, KLGA feb6ef3bdf04, KEWR dff28487eaf4, MIDTOWN 20a22bed5510, KMIA 1a338238fbc8, KSFO 4d03beecdb4b, KSEA 9bd5b34d6718, KORD cb59bb06a506, KDTW 682208bf2fb2, KMSP b694c01dc502, KIND 89a4195fa4be, KJAC 42c32cd8578e, BUFFALO 52fad5e317f3, ROUSES afe7554245b8, ZAPATA e2ddaa00ecaf. The prototype already holds the code PR's counting rule (Q4_PROV) and the (C) refusal split with no format change, so PROVENANCE is predicted identical too. **Any byte difference stops:** the port is not the prototype |
| **SEA-FIG** | the same builds | run 10's figures: rung, radius, D2's steps (2 at New York, 1 elsewhere), non-terrain bytes, `dropped.water`, the sea counts, `repairs` (1 at New York, 4 vertices; 0 elsewhere), `refused5` 0 everywhere |
| **SEA-STORE** | the code PR's ingest on the pinned zips, its own store | **its pieces equal `sea-pieces.json`'s by PARITY step 2's PA-1 to PA-5**, with names decoded by the `.cpg`. **The Class A count** (objects the store writes) is read from the ingest's own listing and **stated before any upload** |
| **SEA-OWN** | the code PR's builder on its OWN store, at the 16 | the water section's rings equal SEA-PKG's **as a multiset**: the store's record numbering may order them differently. Every other section is byte-identical, and PROVENANCE differs only in the store's id and file references |
| **SEA-MEM** | the code PR's sea build minus its no-sea build, per phase, QW4's instrument | **within +- 0.785 MiB of P2-M's recorded sea cost** (run 10) at each point, at the ladder phase. KBOS +0.82, KJFK +0.78, KLGA +0.75, KEWR +0.71, MIDTOWN +0.72, KMIA +0.61, KSFO +0.40, KSEA +0.99, KORD +0.01, KDTW +0.36, KMSP +0.05, KIND +0.01, KJAC +0.02, BUFFALO +0.45, ROUSES +0.46, ZAPATA +0.28. No build over 96 MB (CM-M) |
| **SEA-CI** | the code PR's host tests | green, each with its sabotage registered and shown red first: xisland repaired; S-KB-REPAIRLIMIT, 500; xisland-tall, 500; (C) `refused5` 0; EDGE2's equal edge sets (and S-KB-EDGEUNLOCK red); KB1 and KB2 on the synthetics; KB7-a; KB7-c-ci (and the bowtie refused); T-SEA0-ci; PROVENANCE `collapsed.water` == the drawn set at KMSP (with a refusal present); D2's step order (the first step the largest, at most 2 at New York) |

**FT-SEA, the frame-time row (the owner's standing rule, 2026-11-12: "every change to drawing code reports its frame time on the bench,
before and after. A slowdown beyond the margin frozen in its registration stops it").** The code PR changes what the device draws:
the sea rings, record 18, and the water and roads the ladder drops to fit them.
- **The instrument is map-client's frame bench:** `docs/frame-time-bench.md` on branch `docs/frame-time-bench` at 66d4246ebde9. It is
  cited by its sections: "2. What one frame is", "4. The scenes", "5. The control", and "The ruling ... five amendments" (A1-A5).
  - It runs on the **v20-tree framebench sibling**, which embeds the six packages below and swaps them in RAM through `Initialise`. That
    is map-client's code, registered by map-client.
  - Board E2:08, in an E2:08 window the orchestrator grants after map-client's v19 baseline.
- **The packages, pinned:** the pinned prototype's builds, as run 10 built them; the no-sea ones on the store with no sea layer, NOSEA's
  knobs. Files in `C:\Github\_handoffs\map-server\frame-row-packages\`, written by `pkg-dump.mts` (afb56874bbd7).

  | point | no-sea | sea |
  |---|---|---|
  | KJFK | ee8c834bc81a (rung 7, 129.6 km) | 52ac9dc39de0 (rung 7, 129.6 km) |
  | KBOS | ed5393b4fc0f (rung 3) | f21d705d8b8a (rung 4) |
  | KORD (the inland control) | 4238243a57a6 (rung 3) | cb59bb06a506 (rung 3) |

- **The scene:** map-client's FIXED scene (section 4, A1/A2) with the home set in RAM to the airport, its fixed traffic level, and rungs
  10, 25 and 50 mi, each with a 100 s settle and 120 s measured. At each point the no-sea package runs, then the sea one.
- **Graded:** the p95 of `p2p` (present to present), sea minus no-sea, at each point and rung, **<= M**. `fr` is printed beside it.
  Frames on which the map rebuilt are counted and included, per the bench.
- **The margin M, a formula now and a number before the run:** M = max(2 x the baseline's repeat noise, 2.0 ms).
  - The repeat noise is the |delta p95| between the baseline's CTRL plain scene and its FIXED scene, which are the same scene measured
    twice in one run.
  - M's number is recorded here after the v19 baseline reports, and before FT-SEA runs. A speed-up never stops.
- **Named in advance, the reading expected to differ most:** **KJFK at 50 mi.** It draws the most of the package: the most rings,
  record 18, and New York's dropped water. No direction is predicted, since the sea adds rings while the ladder drops others.
- **The control: KORD.** Its two packages differ only in the roads ladder (9 more secondary roads dropped, 2 non-terrain bytes). It must
  read **|delta p95| within the repeat noise** at every rung. If it does not, the instrument is in doubt: STOP.
- **Outside the set: STOP.** A slowdown over M at KJFK or KBOS stops the code PR and goes to the owner.
- **Amendment (the orchestrator, 19:51Z), every load variable pinned:** WEATHER is OFF for both packages, so the row isolates the map.
  THE AIRCRAFT LEVEL is FIXED's default, **40 drawn**, the same for sea and no-sea at every point and rung.
- **Amendment (the orchestrator's ruling (B), 2026-11-12 ~22:55Z), made BEFORE any reading, on map-client's v19 baseline**
  (docs/frame-time-bench.md at 3fb9adab3633; reader frame_bench_read_b.py b0dbd4ebc163, output baseline_b.out 29d24028cec9):
  - **The inputs.** REPEAT NOISE **0.93 ms**: |delta p95(`p2p`)| between S0, CTRL plain ("Bend, 40 fixed, 25 mi"), and S6, FIXED
    Bend/40/25 mi, the same scene measured twice. **U95 = 1.54 ms**, the bench's stated uncertainty on every single p95 (I-6's excess,
    +11.54 against its +10.0 plant). U95 comes from ONE CTRL pair, so it is an ESTIMATE.
  - **M = max(2 x repeat noise, 2 x U95, 2.0 ms) = 3.08 ms,** replacing the formula above. The graded quantity is a difference of two
    single p95s, each +- 1.54, so this bench cannot resolve less than about 3 ms; a margin inside that turns noise into STOPs, each
    wrongly going to the owner.
  - **The cost, named:** a real slowdown under 3.08 ms passes unseen. A pass is recorded as "below this bench's resolution", never as
    "no slowdown". Resolving under 3 ms would need repeats, registered then. A speed-up never stops.
  - **The control, KORD,** must read **|delta p95| <= 2 x U95 = 3.08 ms** at every rung, replacing "within the repeat noise" above (0.93
    sits below one p95's own uncertainty, so the control would fail on noise). Its two packages differ only in 9 secondary roads, which
    the device does not draw while a primary road is in view (MapLayer's S1, which the bench prints), so the pair draws the same frame:
    **it is printed beside the graded deltas every run as that run's live noise reading,** and if it ever exceeds 3.08 ms the run is the
    instrument's: STOP, and nothing in it is graded.
  - **The A/A's condition (the orchestrator, after 01:32Z):** "S1 draws no secondaries" is a count, and an A/A needs the same FRAME. In
    FT-SEA's window, each KORD package's frame is grabbed THREE times (finished frames, per the bench's frame-trust test), and the two
    packages' frame hashes must be EQUAL. Equal: the pair is a true A/A and stands as the live noise reading. Different: it is not an
    A/A, that reading is INSTRUMENT, and a literal same-bytes pair (KORD's no-sea package twice) runs instead, in the same window. The
    window is requested when the images are frozen.
  - FT-HR (docs/basemap-hudson-rule.md, #682) carries the same amendment.

## #652's CI, predicted (the docs-only shape from main 099f1946, the orchestrator's 19:06:30Z announcement)

**32 check runs: 30 success, 2 skipped (`promote`, `version`), 0 failed.** No workflow has changed on main since 099f1946 (main
d6cd3a3f24d7 at 19:46Z). The tested base is read from the run's own checkout log. A new red from `ship-alert` reads INSTRUMENT, and the
GitHub-side re-run rule applies. Anything else: stop and report.
- **Re-predicted for the amendment's head** (a new commit, so a new run): the same, **32 = 30 / 2 / 0**, provided main's workflows are
  still those of 099f1946 when the run starts. The run on 101902e5db7b is superseded, and the review reads the new head's finished run.

## The code PR's port: the rulings on the obligations checklist (the orchestrator, 2026-11-13 01:46:35Z), before build.ts

The port reads this registration as an obligations checklist: 175 obligations and 39 conflicts or supersessions, each with its line
numbers, read from this file at da3d73a07ead (sha256 df0fb3fe942c; the checklist, a scratch file, sha256 9ab06faeedd8). Eleven needed a
ruling before any code depends on them. **All eleven were ruled as proposed:**

| | the question (checklist ref) | ruled |
|---|---|---|
| **R1** | "the code PR separates dropped.sea from dropped.water" (the sea's storage proposal) against d0's SEA-PKG, byte-identical to run 10 with PROVENANCE included (6.1) | **d0 binds.** No separation in this PR: `dropped.sea` stays 0 and the ladder's sea drops stay in `dropped.water`, as run 10's packages have them. They are counted on `bm_build` as `seaDropped`. A separation, if wanted, is its own PR with its own byte prediction |
| **R2** | `refused`: build-wide, or the drawn set's (6.3) | **the drawn set's** refusals (what the package omits); `refusedBuilt` is build-wide |
| **R3** | `steps`: a list of radii (T-D2) or an integer (6.4) | **the build result carries `radii`** (D2's radii in order, which T-D2 asserts); **`bm_build`'s `steps` = radii.length** |
| **R4** | `refusedBuilt`'s scope | **every backstop refusal in every water set of EVERY D2 step of the call,** TIGER's rings and the sea's (step 3's outers and islands), as the prototype's per-process counter counted them. Step 5's refusals are `refused5`'s |
| **R5** | the new `bm_build` names (6.15, 6.16) | **`seaLost` = `seaRefused` + `seaCollapsed` + `seaDropped`**, the drawn set's; `seaRefused` includes step 5's, with **`refused5`** the step-5 subset; **`islandsDropped`**; **`seaRepairs`** and **`seaRepairVertices`**; **one `sea_repair` log line per repaired piece**, in run 10's observed format |
| **R6** | the `seaLost` response header (1.41) | **`bm_build` only.** The header is #655's; this PR does not change the response contract |
| **R7** | control knobs: T-PROV-a's backstop off, S-KB-REPAIRLIMIT's limit of 1, against "carries no plant knob" | **no knob in the shipped builder.** Every control and sabotage is an in-place edit by `proxy/scripts/sabotage-basemap.mjs`: asserted to apply once, the registered red set compared exactly, every file restored from a byte copy and its sha256 checked |
| **R8** | KB1 in CI (the prototype's FillRing raster was Python) | **a TypeScript port of FillRing's fill rule** in the test helper, its parity with the Python reference shown on the synthetics before the row runs |
| **R9** | "collapsed.water == drawn at KMSP" and "at most 2 steps at New York" listed under SEA-CI (6.14) | **local RUN rows** (R-KMSP, R-D2); CI keeps the synthetic T-PROV-a and T-D2 |
| **R10** | rows d0 does not mention: T-PROV-a/b, T-D2, QW4-ID (a)-(c), KB7-b, KB7-c-real (Buffalo, Rouses Point, Zapata), the post-deploy staging rows, the MI-roundtrip sea case, C-BORDER-S (6.34) | **all stand,** carried into the code PR |
| **R11** | KB3/4/5/8 and the KSEA guard on the code PR's builds | **identity covers only a row whose ENTIRE input is the package's bytes.** A row that also reads anything else runs |

**R11, row by row, from each instrument's inputs in run 10's reader (`p2-read.py` 7ad9bcdbb63a):**

| row | its inputs | covered by SEA-PKG's identity? |
|---|---|---|
| **KB5** | the package's water rings, rendered at the home its header names, by the C++ FillRing harness and the Python reference | **yes**: a function of the bytes alone |
| **KB3** | the sea's bytes, which needs WHICH rings are sea: the builder's own sea set (the package does not tag it) | **no: runs** |
| **KB4** (and its loss column) | the package against the sea's SOURCE pieces | **no: runs** |
| **the KSEA guard** | as KB4 | **no: runs** |
| **KB8** | the package against the unsimplified reference (the source), on the cells' edges | **no: runs** |

So the build result carries, for the run's harness and nothing else, the drawn set's sea rings (their positions in the water section), the
sea's bytes and every piece's fate, as the prototype's record did. None of it is on `bm_build` beyond R5's fields, and none is in the package.
