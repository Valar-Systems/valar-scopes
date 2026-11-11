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
| **QW4-3** (the backstop's plant) | a fixture store whose one water ring is a 4-vertex bowtie, a ring that crosses itself and that tp cannot fix (it has no vertex to add) | **the control:** with the backstop off, the package carries the bowtie with 1 proper crossing, so the plant is shown to land. **With it on:** the ring is dropped and counted once in `PROVENANCE`, and the package carries 0 crossings |
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
