# What a scope in a dense area gets (a proposal, predictions first)

**The proposal this record arrives at is its last section, [The proposal: what a New York scope gets](#the-proposal-what-a-new-york-scope-gets).**
Everything between is the readings it stands on, kept as written.

## The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-06 ~04:32Z)

> 3. KJFK is also a product finding, and D4 waits on it: at these parameters, a New York Blipscope would get no map at
>    all. Before D4's production plan, propose with predictions first what a scope in a dense area gets. Examples: the
>    builder fits the budget by dropping detail, or the client asks again for a coarser package. KJFK is its registered
>    row.

## The finding, and the rule that produced it

**KJFK, at the slot's parameters** (`docs/basemap-a1.md` §9):
- r 97 km; ε 100,584 mm; three terrain levels of N = 246; all six layers.
- **The build answered 422 `too_large`:** 57 R2 reads, 38.4 MB read, wall 7,525 ms.

**The rule is the format's own** (`docs/basemap-package-format.md` §6, ruled Q26):
- A 32 KB hard cap on everything but terrain.
- Past it, in order: **double ε once**; **drop water rings**, smallest area first; **drop secondary roads**, shortest
  first. Every drop is counted in `PROVENANCE`.
- **"If it still does not fit, the route answers an error, and the client falls back to no package."**

**So KJFK is the rule working as written.**
- **Primary roads, rivers, peaks, places and `PROVENANCE`** are never dropped.
- At ε × 2 those alone are over 32 KB.
- **A New York scope gets no map,** and so would any area whose undroppable layers outgrow the cap.

**What is not known: by how much.**
- A 422 reports nothing of its size. The builder's `bm_build` line carries the bytes it **read** (38.4 MB), not the
  package it could not fit.
- **The options below differ mainly in how far over KJFK is, so measuring that comes first.**

## Step 0, the instrument (its own small code PR, predictions first)

The builder's `bm_build` line gains integer fields:
- **On a 422, the ladder:**
  - `ntStart`: the non-terrain bytes at the requested ε;
  - `ntEps2`: after ε × 2;
  - `ntNoWater`: after every water ring is dropped;
  - `ntNoSecondary`: after every secondary road is dropped;
  - `cap`: 32,768.
- **On a 422, by layer:** the non-terrain bytes of each section at the last rung (`rivers`, `roads` (primaries only by
  then), `peaks`, `places`, `provenance`).
- **On a 200:** the rung that fitted (0 to 3), and the same per-section bytes.

**Counts only:** no coordinate, no name, no query, and the same as #541's other fields.

**Its checks:**
- a 422 row reports a non-increasing ladder ending over the cap;
- a 200 row reports its rung;
- **a sabotage that reports the ladder's first value in place of its last** turns the row red.

## The options: what a New York scope would get

| | what the server or client does | a dense-area scope gets | format and firmware |
|---|---|---|---|
| **D1: more rungs** | after secondary roads: **double ε once more** (to about 400 m), then drop **rivers** shortest first, then **places** by rank beyond the first N | the full 97 km, with fewer and coarser lines | a change to §6 (Q26), so **both builders** change (the host tool and the Worker). The firmware does not change |
| **D2: a smaller disc** | after the ladder, rebuild at **r × 0.8, × 0.64, …** down to a floor (say 40 km) until it fits; the header's `radiusMm` carries the disc actually built | full detail, out to a smaller radius. Beyond it, the scope draws no map, honestly | §6 gains one rung; §8 already allows any radius. The firmware must draw the header's `radiusMm`, which §9 already expects |
| **D3: the client asks again** | on 422, the device asks for a coarser package (larger ε, then smaller r) | as D1 or D2, chosen by the device | no format change; **a firmware release**; every 422 costs the device a second round trip and the server a second build |
| **D0: as now** | the error | **no map** | nothing |

**On visibility,** from the format's own numbers (§6: 201.2 m a pixel at the finest zoom):
- **ε at 100 m** is half a pixel there;
- **× 2 (200 m)** is one pixel;
- **× 4 (400 m)** is two pixels at the finest zoom, but under one pixel at the 97 km view.

So D1's first rung costs a little sharpness up close and nothing far out. Its later rungs (rivers, places) remove things
a user may notice.

**My recommendation, tentative until step 0's numbers are read:**
- **D1's first rung (ε × 4), then D2,** both on the server. Keep the 422 only for an area that does not fit even at the
  floor radius.
- **Why:** it keeps the firmware and the client contract as they are. It loses sharpness before it loses features, and
  features before coverage. **Every step stays visible** in `PROVENANCE` and in the header's radius.
- **What would change it:**
  - if step 0 shows KJFK over the cap by a factor that ε × 4 cannot plausibly close, D2 does all the work, and its floor
    becomes the decision;
  - if it shows KJFK only just over, ε × 4 alone may close it.

## Rows, predicted (step 0's reading, staging, in a slot)

**Every query is read at ε 100,585 mm**, 1 mm more than the slot's.
- **Why:** A1 has already stored the slot's answers (KJFK's 422 as a `.err`, and the controls' packages). The same query
  would be answered from the store **without a build**, so no ladder would be reported.
- One millimetre gives each query a fresh digest and the same map, without deleting anything.

**The registered set:**
- **KJFK** (the ruled row);
- three more in the same metro: **KLGA** (40.78, −73.87), **KEWR** (40.69, −74.17), and Midtown (40.75, −73.99);
- **controls that fit:** KBOS (57 reads, wall 8,384 ms, the slot's heaviest 200) and KORD.

| row | predicted |
|---|---|
| **KJFK** | **422**, with `ntStart` ≥ `ntEps2` ≥ `ntNoWater` ≥ `ntNoSecondary` > 32,768 |
| KLGA, KEWR, Midtown | 422 or 200 (**not predicted**: this is what the reading is for). Each reports its ladder or its rung, and a 422's ladder ends over the cap |
| KBOS, KORD (the controls) | **200**, with their rung reported and every reported section's bytes summing to at most 32,768 |

**Outside the set:** a ladder that increases at any rung, or a 200 whose reported bytes exceed the cap. Either means the
instrument is wrong: stop and report.

## Order

1. **This proposal,** for a ruling on the instrument and on the direction.
2. **Step 0's code PR,** predictions first, then its staging reading in a slot.
3. **The chosen option's PR,** with the format change if D1 or D2: `docs/basemap-package-format.md` §6, and both
   builders, each with its rows and plants.
4. **Then D4's production plan.**

## Questions

- **Q-DN1:** step 0 (the instrument) first, as its own PR?
- **Q-DN2:** the direction: D1's ε rung, then D2, both server-side (recommended); or D1 alone, D2 alone, or D3?
- **Q-DN3:** the registered set: KJFK, KLGA, KEWR, Midtown, with KBOS and KORD as controls?

**CI for this docs-only PR, predicted: 31 check runs: 28 success, 3 skipped (`promote`, `ship-alert`, `version`), 0
failed.** The baseline is #564's finished docs-only run, which postdates `84409651`, the last change to the set of jobs.

## Step 0: the ladder instrument (predictions first)

### The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-06 ~15:07Z)

> 5. #567, approved to merge at 2af8f0a6c8d9:
>    - Q-DN1: yes, step 0 first, as its own PR.
>    - Q-DN2: D1's ε rung, then D2, both server-side; the exact rung waits for step 0's numbers. "No map" isn't an
>      acceptable end state for a major city.
>    - Q-DN3: the set, yes.
> 6. Q-Q1, the order: D4's prerequisites first, each with predictions first:
>    1. the SYNC_WAIT_MS code, then its ten on staging;
>    2. step 0;

### The change

**`budget.ts`:** `fitBudget` fills an optional `Ladder` as it measures. **It measures nothing new:** every value is the
`nonTerrainBytes` of an assembly the ladder already does.
- **`rung`:** the step that fitted. 0 as built; 1 after ε × 2; 2 while dropping water; 3 while dropping secondary
  roads; **−1 when nothing fits** (the 422).
- **`nt`, on a 422 only:** four values, the non-terrain bytes at each rung's full step.
  - `nt[0]`: at the requested ε;
  - `nt[1]`: at ε × 2;
  - `nt[2]`: with every water ring dropped;
  - `nt[3]`: with every secondary road dropped as well.
  - **A rung with nothing to drop repeats the value before it,** since dropping nothing changes nothing.
- **`sections`:** the bytes each non-terrain section adds, by layer (`water`, `rivers`, `roads`, `peaks`, `places`,
  `provenance`).
  - On a 200 they come from the package as built; on a 422, from the last rung tried.
  - A layer not in the package adds 0, which is its true contribution to the cap.

**`build.ts`:** a 200 and a 422 both carry the `Ladder`.

**`worker.ts`:** the `bm_build` line gains integer fields, and nothing else.
- **A 200 line:**
  - `rung` and `cap`;
  - `secWater`, `secRivers`, `secRoads`, `secPeaks`, `secPlaces`, `secProv`.
- **A 422 line:** the same, plus `nt0` to `nt3`.
- **A 404 line is unchanged,** at seven fields: it has no build.
- **The line is assembled by an exported `buildLine()`,** so a 422's line can be tested without a store too large to
  fit.

### Rows, predicted

**Fixture A**, from `basemap-synth.ts` (the MB rows), has no terrain. So its non-terrain bytes are its package's bytes,
read from rows already on main:
- A fits as built at 812 (MB1). So **`nt[0]` = 812**.
- At 811 it doubles ε and drops ring 1 (MB2). So ε × 2 alone does not fit under 811.
  - Its 2-vertex roads and small rings do not simplify further, and the header's ε field has a fixed width. So **`nt[1]`
    = 812**.
- At 667 it drops every ring and one secondary road (MB3). So **`nt[2]` is over 667**.
- With every ring and every secondary dropped it is 624 (MB5). So **`nt[3]` = 624**, and at a cap of 623 nothing fits
  (MB4).

| row | input | predicted |
|---|---|---|
| **ML1** | A at a cap of 623 | 422. **`rung` −1. `nt` = [812, 812, x, 624], with 667 < x < 812**: non-increasing, ending over the cap |
| **ML2** | A at 812 | 200, `rung` 0 |
| **ML3** | A at 811 | 200, `rung` 2 (ε, then ring 1) |
| **ML4** | A at 624 | 200, `rung` 3 |
| **ML5** | B at 841 | 200, `rung` 1 (MB6: it fits at 2ε) |
| **ML6** | each of ML1-ML5 | 32 + 16 × the non-terrain section count + the sum of `sections` **equals** the final non-terrain bytes: the package's for a 200; `nt[3]` for the 422 |
| **BB3** (re-registered) | every build line in the endpoint suite | **a 200 line has exactly 15 keys:** the 7 of today, then `rung`, `cap` and the six `sec*`. A 404 line keeps its 7. Integers only, and nothing of the query, as now |
| **BB4** | the endpoint suite's 200 (BB1's fixture) | its line's `rung` and `sec*` equal what `build()` returns for the same fixture |
| **BL1** | `buildLine()` given a planted 422 with `nt` = [9, 8, 7, 6] | exactly the 422 key list, with `nt0` to `nt3` = 9, 8, 7, 6 in that order |

**New plants,** each with its exact red set:

| plant | the defect | red |
|---|---|---|
| **S-LADDER** | `nt[3]` records the ladder's **first** value in place of its last (the check #567 named) | **{ML1, ML6}**: ML1 reads 812, not 624; and the 422's sections still sum to 624, so ML6's identity against `nt[3]` breaks too |
| **S-RUNG** | rung 2 recorded as 1 | {ML3} |
| **S-SECSUM** | `provenance`'s bytes recorded as 0 (the key kept, so BB3's key list still holds) | {ML6} |

**Before any run, the existing plants are re-checked against the new rows** (the A2.16 rule).
- Six plants already target `budget.ts`: S-BTWICE, S-DROPORDER, S-NOCOUNT, S-ROADORDER, S-PRIMARYDROP and S-TERRCAP.
- Five target `worker.ts`: S-PWRITE, S-WSTAMP, S-BB1, S-BB2 and S-BB3.
- **Some of their sets will grow.** For example, S-PRIMARYDROP makes A fit at 623, which reddens ML1.
- **Each grown set is registered in this doc, with its reason, before the driver runs.** That registration comes with
  the code, since an exact set needs the code's exact lines.

**Counts:**
- **The suite** grows by 8 (ML1-ML6, BB4, BL1), from main's 976 to **984** (983 passed, 1 skipped).
- **The driver's `baseline[all]`** grows from 159 to **167**.

### The staging reading (#567's registered set, in a slot after merge and deploy)

KJFK, KLGA, KEWR and Midtown, with KBOS and KORD as controls, all at ε 100,585 mm (fresh digests), as registered.
Predicted:
- **KJFK:** 422, with a non-increasing ladder ending over 32,768.
- **The controls:** 200, with their rung reported.
- **The others:** read as they come.

**Outside the set:** stop and report.

**The order stands** (~15:07Z): this lands after the `SYNC_WAIT_MS` code and its ten.

### Corrections found while writing the code, before any run (2026-11-06)

**These are registered before any run, each with its cause. The original sets above are kept as written.**

**1. Two ids renamed, because the driver grades by id:**
- **ML1 to ML6 → MN1 to MN6.** `ML` ids already exist in the layers suite (ML4, ML6, ML7, ML11, ML13), and the driver's
  duplicate-id guard would refuse two rows with one id.
- **BL1 → BB5.** The driver's id pattern (`M[A-Z]…`, `BB\d+`, `MV/PK-…`) would read `BL1` as UNNAMED.
- The rows themselves are unchanged.

**2. ML6's identity, stated exactly:** RD20 counts every directory entry, terrain's included, so the identity is
32 + 16 × **all** sections + the layers' bytes.
- Fixtures A and B have no terrain (only A8 adds it), so for MN6's cases the two wordings give the same number.

**3. Four existing plants re-anchored, each with the same defect.**
- **Why:** step 0 rewrote the lines they anchored. The driver's own `plant()` found 0 anchors for each, and 67 of the 71
  planted cleanly.
- **S-NOCOUNT:** one of its three edits now anchors the water attempt's new `const got`. **Its set is unchanged:**
  {MB2, MB3, MB5, MB7, MB8b}. A count rendered as 0 has the same digit count as 9 or 1, so every size, and every MN
  value, is the same.
- **S-PRIMARYDROP:** the same primary-dropping step, placed after the new "no secondary" line. **Its set grows
  {MB4} → {MB4, MN1, MN6}.** A now fits at 623, so MN1's 422 is gone, and MN6 reads that 422 too.
- **S-BB2:** "only a 200 logs", now on `buildLine()`'s call. **Its set grows {BB2} → {BB2, BB3}:** BB3's new control
  requires a 404 line to have been seen.
- **S-BB3:** the same leaked coordinate, added to `buildLine()`'s line. **Its set is unchanged:** {BB3}.

**4. One new plant's set corrected:** **S-SECSUM {MN6} → {MN6, BB4}.**
- BB4's control (`secProv` over 0) also catches provenance recorded as 0.
- BB4's comparison itself stays green, since the line and `build()` both go through `layerBytes`.

**5. Every other existing plant was evaluated against MN1-MN6, BB3, BB4 and BB5, and none moves:**
- **The plants whose sets hold no MB row** leave A and B byte-identical. The MB rows pin their exact totals.
- **S-BTWICE** (ε × 4): A's lines do not simplify, and the header's ε field and PROVENANCE's requested ε keep their
  widths. B still fits after the ε step.
- **S-DROPORDER, S-ROADORDER:** a full drop's bytes do not depend on order, and at 811 any one ring suffices.
- **S-TERRCAP, S-TNOMARGIN:** A, B and BB4's query carry no terrain.
- **S-BB1:** BB4 compares the ladder, not the reads.
- **The `query.ts` plants:** BB4's expected ladder goes through the same `parseQuery` as the proxy's request.
- **The ingest-set plants** run no MN or BB row.
- **C-LOADFAIL** still reads BLIND, since `basemap-build.test.ts` imports the layers too.

**The anchor check after these edits: 74 of 74 sabotages plant cleanly.** It uses the driver's own table and its own
`plant()`, and it is the same check that found the four.

**Counts, restated with the renames:**
- **The suite:** 976 + 8 (MN1-MN6, BB4, BB5) = **984** (983 passed, 1 skipped).
- **`baseline[all]`:** **167** cases in 17 files.
- **The driver:** **77 runs** (74 sabotages and 3 baselines), **every one AS PREDICTED**, with C-LOADFAIL BLIND among
  them.
- **The smoke driver:** CONTROL **983 / 1 / 984**, with S-SQ and S-SQP as registered and the TIME_WAIT margin reported.

**CI for the code PR, predicted: 34 check runs: 31 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.**
The baseline is #572's finished run, a `proxy/` PR that postdates `84409651`.

### Read (2026-11-06, `1610be06`): one plant off, my miss

| check | read | verdict |
|---|---|---|
| `tsc --noEmit`, both configs | clean | as predicted |
| **`sabotage-basemap.mjs`, whole** (wait clear 18:38:23Z: TIME_WAIT 75, no other `vitest` or `sabotage-` process; exit 1 at 19:30:05Z) | baselines **11 / 31 / 167** (17 files); **77 runs, 0 runtime failures**; **73 of 74 sabotages AS PREDICTED**, among them S-PRIMARYDROP {MB4, MN1, MN6}, S-BB2 {BB2, BB3}, S-BB3 {BB3}, S-NOCOUNT unchanged, S-LADDER {MN1, MN6}, S-RUNG {MN3}, and C-LOADFAIL BLIND. Highest TIME_WAIT at a run's start: 7,914 | as predicted, **but one** |
| **S-SECSUM** | **BLIND** (72 baseline cases not run, from MB1 on) | **off, my miss: a plant that did not compile** (ruled ~19:43Z) |
| the smoke driver | **not run:** the wrapper stops at the first off | |

**Why S-SECSUM read BLIND:**
- **The cause:** its anchor sits inside a one-line loop in `layerBytes`:
  `for (const s of sections) { const k = LAYER_OF[s.type]; if (k) b[k] += sectionSize(s); }`.
  - The replacement ended with a line comment, `// SABOTAGE S-SECSUM`, which commented out the loop's closing `}`.
  - So the planted `budget.ts` did not compile, and every test file importing it failed to load.
- **The anchor check could not see it:** it proves each anchor is found exactly once, not that the planted file
  compiles.

**The fix (Q-ST0, ruled ~19:43Z):**
- **The change:** a block comment, `/* SABOTAGE S-SECSUM */`.
- **Checked before any run, in memory, with TypeScript's own parser:**
  - the unplanted `budget.ts` parses;
  - **the BLIND run's plant reports `'}' expected`**;
  - the fixed plant parses;
  - the anchor check still reads 74 of 74.

**Then, as ruled:**
1. S-SECSUM alone, predicted **{MN6, BB4}**.
2. The smoke driver whole, predicted CONTROL **983 / 1 / 984**, with S-SQ and S-SQP as registered and its TIME_WAIT margin
   reported.
3. Step 0's PR.

**Neither run starts before 20:30Z,** the UK session's slot on this machine.

### Read (2026-11-06, `dc5c0cdf`): S-SECSUM as predicted; the smoke driver off, my miss

| check | read | verdict |
|---|---|---|
| **S-SECSUM, alone** (wait clear 20:45:03Z: TIME_WAIT 13, no other `vitest` or `sabotage-` process; exit 0 at 20:45:45Z) | baseline[all] **167** cases in 17 files, 0 failing. **Failing {BB4, MN6}.** 2 runs, 0 runtime failures. Highest TIME_WAIT at a run's start 3,139 (gate 8,000) | **as predicted** |
| **the smoke driver, whole** (wait clear 20:47:39Z: TIME_WAIT 48; stopped by pid at about 20:50Z) | CONTROL **991 passed, 1 skipped, of 992**, 0 failed (TIME_WAIT start 35, peak 13,469) | **off, my miss** (predicted 983 / 1 / 984). S-SQ and S-SQP were not read: I stopped the driver at the first off |

**First, the wrapper's own miss** (it changes no reading):
- `st0b-drivers.sh` looked for S-SECSUM's line as the text `{MN6, BB4}`, but the driver prints the set sorted,
  `{BB4, MN6}`.
- So the wrapper stopped after a reading that was as predicted.
- The smoke driver then ran from a second wrapper (`st0c-smoke.sh`) with the same gate.

**Why 992, with every case accounted for:**
- **The 976 in the prediction** is main's count read at `62944845` (`docs/basemap-sync-wait.md`).
- **This branch then merged main** at `36d0b657` (#573, merged in by `7eb782d9`), and #573 adds 8 proxy tests.
- **Main's own CI, on the commits themselves:**
  - the `workers` run on `551fd030` (#573's parent) reads **975 passed, 1 skipped (976)**;
  - the run on `36d0b657` reads **983 passed, 1 skipped (984)**.
- **984 + step 0's 8** (MN1-MN6, BB4, BB5) = **992**, with nothing left over.
- **The alternative,** that step 0 adds 16 cases rather than 8, is excluded. Main's 984 is read from CI on the exact
  commit merged in, and 992 − 984 = 8.
- **The miss:** the count was restated after the merge, in the corrections at `1610be06`, without being read again. The
  input had moved.

**The stop:**
- **What was killed, by pid:** the driver's node process, its `timeout` and the wrapper. No driver process remained.
- **The kill landed during S-SQ,** so `proxy/scripts/smoke-prod.sh` still held its plant (the `eps` parameter dropped).
- **The restore:** from HEAD, since the tree was clean before the run (porcelain 0). The file's blob reads
  `9bd63ee14a5e` again, porcelain 0, 0 CR.

**The TIME_WAIT margin:**
- **The CONTROL's peak,** 13,469, is 1,531 under the driver's 15,000 ceiling and 2,915 under Windows' 16,384 ports.
- **Not under 1,000.**

**Proposed for a ruling (Q-ST0b):**
- **Re-register the smoke driver** as CONTROL **991 / 1 / 992**, S-SQ {SQ1, SQ2} and S-SQP {SQ-C}, each run counting 992.
- **Then run it whole.** If it reads as predicted, step 0's PR goes up.
- **The PR's suite line becomes 992** (991 passed, 1 skipped).
- **The PR's CI prediction does not change:** 34 = 31/3/0.

### Q-ST0b, ruled (reviewer chat, pasted by Daniel, ~21:39Z), verbatim

> Q-ST0b: (a), re-register as you propose.
> - The smoke driver: CONTROL 991 / 1 / 992; S-SQ {SQ1, SQ2} and S-SQP {SQ-C}, each run 992. The sets stay as frozen; only
>   the counts move, from CI's 984 on 36d0b657 plus step 0's 8. Cross-check from here: #573's diff adds 10 test cases under
>   proxy/test and removes 2, net 8, matching 976 → 984.
> - 983 / 1 / 984 stays in the record as a miss, with the wrapper's text-gate miss beside it.
> - A rule from this miss, for every count from now on: after any merge of main into a branch, re-read every count from CI
>   on the exact merged commit before restating it or predicting from it.
> - S2: "the branch base, plus 1" is read when S2's predictions are frozen, and again after any merge of main. 985 is
>   today's value, not a frozen one.
> - Chains gate on the driver's exit code. Any text a chain also matches is copied from the driver's real output (sets
>   print sorted).
> - The stop and the restore: accepted (blob 9bd63ee14a5e, porcelain 0, 0 CR). Next time, record the driver's child pids
>   before stopping it, and stop the whole tree (taskkill /T). The two node processes were most likely its own children
>   outliving it; the pids settle that.
> - TIME_WAIT: noted, 1,531 under the ceiling. The under-1,000 rule stands.
> - Then: the usual clear check, the smoke driver whole, and if it reads as predicted, step 0's PR with CI 34 = 31/3/0.

### Re-registered, frozen before the run

| check | predicted | verdict rule |
|---|---|---|
| **the smoke driver, whole** | CONTROL **991 passed, 1 skipped, of 992**, 0 failed. S-SQ **{SQ1, SQ2}**. S-SQP **{SQ-C}**. **Each run counts 992.** No raw output kept. TIME_WAIT margin reported against the 15,000 ceiling, and raised if under 1,000 | exit 0 and all of those: as predicted, step 0's PR goes up. **Anything else, including a count other than 992 or a set that differs: stop, no fix, report** |

**The count's source:** CI's 984 on `36d0b657`, the commit `7eb782d9` merged in, plus step 0's 8. No merge of main has
landed on this branch since `7eb782d9`.

**How the run is gated:** by the driver's exit code. The one text the wrapper checks is copied from the 20:49Z run's real
CONTROL line, with the new figures unchanged in shape.

**If it has to be stopped:** the driver's child pids are recorded first, and the whole tree goes with `taskkill /T`.

### Read (2026-11-06, `276a9335dcea`): as re-registered

| check | read | verdict |
|---|---|---|
| **the smoke driver, whole** (wait clear 21:47:13Z: TIME_WAIT 58, no other `vitest` or `sabotage-` process; exit 0 at 21:54:08Z) | CONTROL **991 passed, 1 skipped, of 992**, 0 failed (start 60, peak 13,587). S-SQ **{SQ1, SQ2}** (start 378, peak 13,596). S-SQP **{SQ-C}** (start 401, peak 13,608). Both restored byte-identical. No raw output kept. **ALL AS REGISTERED** | **as predicted** |

- **Each run counted 992.** The driver treats a sabotage run whose total differs from CONTROL's as an instrument failure,
  so "ALL AS REGISTERED" covers it.
- **The tree afterwards:** porcelain 0, and `smoke-prod.sh`'s blob reads `9bd63ee14a5e`.
- **The TIME_WAIT margin:** the highest peak, 13,608, is 1,392 under the driver's 15,000 ceiling and 2,776 under Windows'
  16,384 ports. Not under 1,000.

So step 0's registered checks are all read:
- `sabotage-basemap.mjs` whole: 77 runs, as registered, with S-SECSUM's plant corrected and read alone.
- S-SECSUM alone: {BB4, MN6}.
- The smoke driver, as above.

### CI for the code PR, corrected before it goes up: 35 = 32/3/0, not 34

**Predicted: 35 check runs: 32 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.** The ruled 34 = 31/3/0
predates #445.

- **The cause:** #445 (`caf63cc9`) added `blipscope-s3-128-wxlook` to `skus.yml`, which is one more leg of the firmware
  matrix, so main's last job-set change is now `caf63cc9`, not `84409651`.
- **Read from main's own pushes:** the docs push `b5c9c6e9` was 31 (29/2), with 0 `wxlook` legs. The docs push `1ce7a73a`
  is 32 (30/2), with 1.
- **So 35 is 34 + 1, derived.** No proxy PR has finished since `caf63cc9`, so it has no direct baseline yet.
- **The suite on CI reads 998** (997 passed, 1 skipped), not this tree's 992, because a PR's CI runs the merge ref: main's
  990 (`workers` on `51757039ed01`) plus step 0's 8.
- **This branch was not merged with main,** so the 992 read above stands for this tree. The merge is clean, and none of
  main's newer commits touch the basemap's files.

## Step 0's reading on staging (2026-11-07, 18:14-18:16Z): as predicted

**Read in the combined slot, graded on its own, on builder `9512e266` at ε 100,585 mm.** The slot's record is in
`docs/basemap-name-key.md`. Every query was a 202 `known-miss`, then a retry as the table shows. Exactly one build each.

| query | answer | ladder: start, ε×2, no water, no secondary | over the 32,768 cap | reported sections | reads, MB, wall ms |
|---|---|---|---|---|---|
| **KJFK** | 422 `stored-error` | 137,003 / 108,911 / 93,331 / 34,306 | **1,538** | 34,130 | 57, 38.4, 4,211 |
| **KLGA** | 422 `stored-error` | 149,881 / 119,228 / 102,940 / 35,700 | **2,932** | 35,524 | 57, 38.4, 4,038 |
| **KEWR** | 422 `stored-error` | 163,115 / 130,021 / 112,563 / 36,509 | **3,741** | 36,333 | 62, 34.6, 3,115 |
| **Midtown** | 422 `stored-error` | 155,007 / 123,096 / 106,020 / 35,810 | **3,042** | 35,634 | 57, 38.4, 3,591 |
| **KBOS** (control) | 200 `stored`, rung 3 | none: it fits | fits | 32,583 | 57, 36.9, 4,640 |
| **KORD** (control) | 200 `stored`, rung 3 | none: it fits | fits | 32,585 | 52, 23.4, 3,468 |

**Against the registered rows:**
- **KJFK:** 422, with a non-increasing ladder ending over 32,768. **As predicted.**
- **KLGA, KEWR, Midtown:** not predicted, by design. All three are 422, and each ladder ends over the cap.
- **The controls:** 200, each reported section's bytes summing to at most 32,768. **As predicted.**
- **No ladder increases, and no 200 is over the cap,** so the instrument stands.
- **Each ladder's last figure is its sections plus 176 bytes,** so the identity holds on real builds as it does in MN6.

**Two findings:**
1. **New York is close.** After every rung, the four queries end 1,538-3,741 bytes over, about 5-11%. That goes to the dense-area proposal.
2. **Water is missing.** All seven builds in the slot, KIND's included, read **0 bytes of water.** Rung 3 means the whole water layer went
   first. So Boston Harbor and Lake Michigan would not draw. This goes first in the ocean proposal (ruled ~18:19Z), after a read of how
   much of it is TIGER's coverage rather than the ladder.

## The exact rung at the map client's query: a reading registered before it runs (2026-11-08)

**Why now.** Q-DN2 ruled D1's ε rung, then D2, and left the exact rung to step 0's numbers. Since then, three things have moved:
- **The query moved to the map client's** (ruled ~02:33Z): r=162, ε 201,168 mm, no peaks, three levels of 246, 244 and 242.
- **The ladder is now A+B** (Q-W1, ~19:08Z), and the dense-area work lands with it and the sea mask in one PR (Q-W4).
- **At r=162, New York is further over** (#620, open): 7,598-8,842 B (23-27%) after every rung, against r=97's 1,538-3,741 B. Primary
  roads alone take 27,970-29,550 B of the 32,768.

So D1's rung is read at this query and on top of A+B, before it is proposed.

**How.**
- **The real builder runs on a patched copy of `src`** (never the repo's tree), over the local copy of store `4c0875738f38`, at the client's
  query. **Nothing runs on staging,** and no object is written anywhere.
- **The same 13 points as #620:** KBOS, KJFK, KLGA, KEWR, Midtown, KMIA, KSFO, KSEA, KORD, KDTW, KMSP, KIND, KJAC.
- **What it prints:** counts and bytes only, with water as a share of the disc (#620's raster), secondary roads kept, the rung, and the radius
  built.

**The variants.** None changes a package that fits under A+B, except P2, by design.

| variant | after ε×2 and A+B's rung 2 (water at ε×8) fail |
|---|---|
| **v0** | today's ladder (Q26): the anchor |
| **ab** | A+B as #620 read it: secondary roads shortest first, then water smallest first |
| **P1** | A+B; **only if A+B still fails,** rivers and roads at **ε×4** (water stays at ε×8), then A+B's two drops again |
| **P2** | rivers and roads at **ε×4 before any drop**, then A+B's two drops. Sharpness goes before features, everywhere |
| **D2** | for a point still 422 under ab or P1, the same variant rebuilt at r × 0.8, × 0.64, … down to a **40 km floor** (162, 129.6, 103.7, 82.9, 66.4, 53.1, 42.5 km) |
| **+sea** | P1, P2 and D2 again, with Natural Earth's land outline in the square added to the water section at the water's ε. **Bytes only:** it is the coastline a sea ring would carry, without the bridges' 2 vertices per island, and its area is not the sea's |

**ε×4 is 805 m.** That is 2.3 px at the finest view (41 km over 120 px) and 0.6 px at the full one (162 km). That cost is why P1 and P2
are both read.

### Rows, predicted

| row | what | predicted |
|---|---|---|
| **DR0** (anchor) | v0 on the copy against the unpatched builder | **byte-identical** for the 9 points that fit; **the same ladder**, to the byte, for the 4 New York points (#620: KJFK 238,588 / 196,364 / 176,124 / 40,366, and so on) |
| **DR1** (anchor) | ab against #620's A+B table | the same water share (to 0.1 point) and the same secondary count at the 9; **422** at all 4 New York points |
| **DR2** | P1 at the 9 that fit under ab | **byte-identical** to ab, 9 of 9: P1 only acts where A+B fails |
| **DR3** | P1 at the 4 New York points | **not predicted:** this is what the reading is for. The meanings are fixed now. **All 4 fit:** D2 is a safety net only, at this query. **1-3 fit:** D2 carries the rest. **None fits:** D2 does the work, and its floor is the decision |
| **DR4** | P2 at all 13 | every package `kOk` and at most 32,768 non-terrain bytes. **Byte-identical to ab** wherever ab fitted at rung ≤ 2. Changed wherever ab dropped anything |
| **DR5** | D2 | for each point still 422, the first radius that fits. **A 422 at the 40 km floor is a stop:** "no map" is not an acceptable end for a major city (Q-DN2) |
| **DR6** | +sea | not predicted; the same invariants as DR4. Read as the sea mask's cost against each candidate's margin |

### Peak memory, against the isolate's 128 MB (ruled ~03:23Z)

**The instrument:**
- Node with `--expose-gc`, sampling `heapUsed + arrayBuffers` after a forced collection, so it measures what is live.
- Samples are taken after each cell read, after the tiles, after the terrain levels, after the layer sets, and after the ladder.
- Each sample has the build's own baseline subtracted, taken the same way just before it.
- **No object is cached between builds,** so each build's figure is its own.

**Its control (a check that can fail):** KJFK under v0 is built twice. The second time, an extra 32 MiB buffer is held from baseline to end.
The second peak must read **32 MiB ± 1 MiB above the first,** or the instrument is not believed and nothing below is graded.

**What it cannot see:** workerd is not Node. The Worker's own bundle and globals sit outside the baseline, and garbage made between two
samples can raise a real peak above the sampled one. So the figure is a floor on the build's live memory, and is stated as that.

| band, the largest build under any variant | meaning |
|---|---|
| **at most 64 MB** (predicted) | fits with room |
| **64-96 MB** | a risk: recorded and carried into the plan, but not a stop |
| **over 96 MB** | **stop:** the design changes before any code |

### Outside the set

**Stop and report** on any of these:
- an anchor that does not reproduce;
- a package over the cap, or one that fails `validate`;
- a ladder that increases;
- an object missing from the local store copy;
- the memory control off by more than 1 MiB;
- anything else not named above.

**Order:**
1. This registration, pushed before any of it runs.
2. The reading, appended here.
3. **The exact rung, proposed for a ruling,** with each build's memory.
4. Then the one code PR (Q-W4): the rung, A+B and the sea mask, with the store change's Class A figures stated first.

### Read (2026-11-08, 03:18:36-03:19:39Z, stopped): memory is off, and its two readings disagree

**Stopped by me at 03:19:39Z,** on the first off reading. I ended the run's own process tree; nothing else was touched. The tree stayed
clean, and nothing ran on staging.

**My miss, recorded with its cause:** the script graded the memory band only at the end. The registered band is a stop, so it should have
stopped at the first build over 96 MB, the run's very first line. The rows below it ran in the 63 s before I stopped it, and are reported as
observed.

**Everything graded before the stop read as predicted:**

| row | read |
|---|---|
| memory control | the 32 MiB hold read **31.86 MiB** above (± 1 MiB) |
| **DR0** | **13 of 13.** Byte-identical at the 9 that fit. At the 4 New York points, the copy's ladder equals the repo builder's and #620's to the byte |
| **DR1** | **13 of 13.** #620's A+B table reproduced: the same water share to 0.1 point and the same secondary count at the 9; 422 at New York's 4 |
| **DR2** | **3 of 3 so far** (KBOS, KMIA, KSFO): P1 byte-identical to ab |

**DR3 was observed: P1 fits 0 of the 4 New York points.** Its registered meaning: **D2 does the work, and its floor is the decision.** At
r=162, primaries and rivers at ε×4 still don't fit, even with every secondary road and every water ring dropped.

**Memory: the registered stop band, from an instrument whose readings contradict each other.**

| build | MiB above baseline | MB |
|---|---|---|
| KJFK v0, **the process's first build** (CTRL-a) | **97.3** | **102.0: over 96, the stop band** |
| KJFK v0 again, holding 32 MiB (CTRL-b) | 129.1 | (the control: +31.86 MiB) |
| **KJFK v0, the same build, later** (DR0) | **76.9** | 80.6 |
| KLGA, KEWR, Midtown v0 (DR0) | 83.5, 77.0, 73.7 | 87.6, 80.7, 77.3 |
| KBOS, KSEA v0 (DR0) | 65.2, 56.8 | 68.4, 59.6 |
| KMIA, KSFO, KORD, KDTW, KMSP, KIND, KJAC v0 (DR0) | 24.3-39.6 | 25.5-41.5 |

**Two readings of one build differ by 20 MiB,** and the build is deterministic, so the question is what moved between them. The control
cannot answer it: **it proves a live buffer is counted, not that a dead one isn't.** Three explanations fit, and this read separates none:

1. **Real, and first-build only:** a fresh isolate pays one-time allocations, such as lazily compiled code, that a reused isolate does not.
   Then 102 MB is what a cold Worker would need.
2. **The instrument over-counts:** `arrayBuffers` can still hold buffers that are dead but not yet swept after `gc()`. Then every figure
   above is high by an unknown amount.
3. **Something cached across builds:** a grep found no module-level `Map`, `Set`, array or object in `src/basemap`. That makes it unlikely,
   but a grep doesn't prove it absent.

**What is not known: workerd.** No build on staging has run New York at r=162. The fixtures' builds (KJAC, KSFO) read 33.9 and 26.6 MiB
here.

**So nothing is graded past this point,** and the questions go to the reviewer before anything resumes.

### The ruling (reviewer chat, pasted by Daniel, 2026-11-08 ~19:07Z), verbatim

> #622's stop: right, at the registered band. Grading only at the end stays on record as your miss: from now on every band is graded per
> build.
> Q-MEM1: yes, the local diagnostic, predictions first: the dead-buffer control, peaks per phase, and each New York build as the first build
> in its own fresh process, three times each.
> Q-MEM2: yes, after Q-MEM1, and only if Q-MEM1 leaves the cold-isolate question open. One staging request isn't a deploy and needs no slot;
> say in the record when it ran.
> Q-DR3: yes, once memory is settled, stopping at the first build over 96 MB, graded per build.
> A cold build near 102 MB is the bigger risk for D4: the dense-area proposal states each build's peak against the isolate's 128 MB, as ruled.

### Q-MEM1, the memory diagnostic: registered before it runs

**What runs.**
- **Processes:** 16 separate Node processes (`--expose-gc`), one after another, on the same patched copy at v0. That copy is byte-identical
  to the repo's builder (DR0), over the local store, at the client's query. Nothing runs on staging.
- **Each process builds one query twice:** the first build is the cold one; the second, in the same process, is the warm one.
- **The 16:**
  - KJFK, KLGA, KEWR and Midtown, three processes each;
  - KJAC three times, as a light control;
  - one more KJFK process for the in-build controls, which would disturb a measured peak, so that process measures none.

**The instrument, as before:**
- `heapUsed + arrayBuffers` after `gc()`, minus that build's own baseline;
- sampled after every cell, and after the tiles, the terrain, the layer sets and the ladder, plus once at the end;
- the largest sample per phase is kept, so each build reports **its peak and the phase that holds it**.

**Also printed:** what the process keeps between builds, which is the baseline before build 2 minus the baseline before build 1. **Every
build's band is printed the moment it is measured** (at most 64 MB / 64-96 / over 96), as ruled.

**A build over 96 MB is what this diagnostic exists to measure, so here it is recorded, not a stop.** The stops are the controls and the
outside-the-set line below. DR3's resumed reading stops at the first build over 96 MB, as ruled.

| row | what | predicted |
|---|---|---|
| **MQ0** | in every process, before its builds: 32 MiB allocated and **held** | **+32 ± 1 MiB** |
| **MQ1** | in every process, before its builds: 32 MiB allocated and **dropped**, then sampled at once | **within 1 MiB** of the sample before it |
| **MQ2** | the control process, at the layer-set sample of its first build: the same live and dead pair, mid-build, against the sample just before | **+32 ± 1** and **within 1 MiB** |
| **MQ3** | each New York point's three cold builds | **agree within 2 MiB of each other**, and **KJFK's read 97.3 ± 2 MiB** (CTRL-a, reproduced) |
| **MQ4** | each warm build | **at least 15 MiB under its cold build**, and **within 2 MiB of its DR0 figure** (KJFK 76.9, KLGA 83.5, KEWR 77.0, Midtown 73.7, KJAC 33.9) |
| **MQ5** | what each process keeps between builds | **at least 15 MiB, and the same within 2 MiB at all five points**, KJAC included |
| **MQ6** | KJAC's cold-minus-warm gap | **within 3 MiB of KJFK's** |
| **MQ7** | the phase holding each cold build's peak | not predicted; reported |

**What the readings will mean, fixed now:**

| outcome | meaning | next |
|---|---|---|
| MQ0-MQ6 as predicted | **explanation 1:** a fixed cost of the first build in a fresh process, independent of the query. The figures are real, and the cold one is what a cold isolate would pay in Node | Q-MEM2, because Node can't say what workerd pays |
| **MQ1 or MQ2's dead half fails** | **explanation 2:** the instrument counts dead buffers, so every figure here and in DR0 is a ceiling, not a floor | **stop:** the instrument changes before anything is graded |
| MQ3's three runs scatter by more than 2 MiB | the instrument is unreliable at this scale | **stop** |
| KJFK's cold builds don't reproduce 97.3 | CTRL-a's figure came from something else in that process | **stop and report** |
| MQ5 scales with the query: KJAC keeps much less than KJFK | **explanation 3:** data is kept between builds | a read of what keeps it, not a fix |

**What this cannot see:**
- **Workerd is not Node.** Node's own one-time costs, such as zlib's for the PNG tiles, may not exist in workerd, and workerd may have
  others.
- **Garbage made between two samples** can raise a real peak above the sampled one.

**Outside the set:** anything not named above stops it.

### Q-MEM1, read (2026-11-08, 20:16:41-20:17:23Z): stopped. The instrument counts dead buffers

**The driver stopped itself after the 16th process,** whose dead-buffer row (MQ1) read **32.00 MiB**. KJAC's three processes never ran. The tree
stayed clean, and nothing ran on staging.

**The evidence is in all 16 processes, not only that one.** One extra field was printed for diagnosis: the 32 MiB hold, after release and
`gc()`. It read **+31.98-32.01 MiB in every process**, so a buffer with no reference, already collected, was still counted.

**My miss, with its cause:** MQ1's dead buffer was sampled right after that released hold. When one cycle's sweep of the released hold landed
in the same sample as the new dead buffer's count, the two cancelled. That read 0.00 in 15 of 16 processes for the wrong reason, so MQ1 as
designed could not see its own failure, and the extra field is what showed it.

| row | read | |
|---|---|---|
| MQ0, live hold | +31.98-32.01 MiB in all 16 | as predicted |
| **MQ1, dead buffer** | 0.00 in 15 (cancelled, above), **32.00 in 1**; the released hold still counted in **all 16** | **off: explanation 2** |
| MQ2, mid-build | live +32.01; dead 0.00, but the released hold left 32.01 | off, the same way |
| MQ3, three cold runs per point | within 0.39 MiB of each other; **KJFK 97.18-97.32** | as predicted |
| **MQ4, warm builds** | **0.03-0.73 MiB under cold**, not at least 15. **15.6-20.2 MiB above** their DR0 figures | **off** |
| **MQ5, kept between builds** | **7.42-8.02 MiB**, not at least 15 | **off** |
| MQ6, KJAC | not reached | - |
| MQ7, the peak's phase | **the cell reads**, in every build | reported |

**Bands, printed per build, as ceilings, not as peaks:**

| point | cold, 3 runs | warm, 3 runs |
|---|---|---|
| KJFK | 101.9-102.0 MB (over 96) | 101.6-101.9 MB |
| **KLGA** | **107.3-107.7 MB** (over 96) | 106.9-107.2 MB |
| KEWR | 99.4-99.5 MB (over 96) | 99.1 MB |
| Midtown | 93.9-94.2 MB (64-96) | 93.6-94.1 MB |

**What it means, by the registered table:** MQ1's dead half failed, so **explanation 2 holds.** Every figure here and in DR0 is a ceiling,
and **the instrument changes before anything is graded.**

**And what it rules out:**
- **Explanation 1, a first-build cost, is contradicted at these four points.** Cold and warm agree within 0.73 MiB in all 12 pairs, so
  CTRL-a's 97.3 against DR0's 76.9 was not that.
- **What fits instead:** the peak sits in the cell reads, where each cell is a buffer read and then dropped. How many dropped buffers are still
  counted at a sample depends on the collector's state, and DR0's process had a different history (Natural Earth loaded, the unpatched
  builder's modules, and 5 builds before KJFK's).
- **Not separated:** how far the true live peak sits below these ceilings.
- **Explanation 3, a cache:** each process keeps 7.42-8.02 MiB between builds, nearly the same at all four points. That is too small to
  explain a 20 MiB gap, and nothing in this read says what it is.

**Still open, and not answerable from Node:** whether workerd's 128 MB limit counts dropped buffers the same way.

### The ruling (reviewer chat, pasted by Daniel, 2026-11-08 ~20:28Z), verbatim

> Q-MEM1's stop: right, at the registered stop. MQ1 reading 0.00 for the wrong reason stays on record as your miss.
> Q-MEM2: yes, now, and at KLGA, the largest ceiling. One request through staging's route: not a deploy, so no slot; say in the record when it
> ran. It's the reading that decides: workerd's own answer, a 200, a 422 or exceeded memory, holds whatever the local instrument says.
> Q-MEM1b: yes, predictions first, but it doesn't block Q-MEM2. If repeated gc() can't clear the lag, retire the local instrument, as you propose.
> READ_AHEAD = 6: noted as D4's lever. The dense-area proposal states its effect on the peak.

### Q-MEM2, workerd's own answer at KLGA: registered before it runs

**The request.** One query for KLGA at the map client's query (`lat=40.78&lon=-73.87`, r=162, ε 201,168, no peaks, the three levels),
through staging's route as the test identity, with `fx-request.sh`.
- A 202 is retried after its `Retry-After`, at most three times. That is still one query and one build: the 202 is the route's protocol.
- **The builder's own log is read alongside:** `wrangler tail blipscope-basemap-staging --format pretty`, started before the request and stopped
  after it. That prints the outcome and the one `bm_build` line, which holds integers only.
- No deploy, no secret, no claim needed. The request's time goes in the record.

**How workerd would show each answer (from `handler.ts` and `worker.ts`):**
- **A build that finishes** stores a 422 `.err`, so the route answers **422 `too_large`**, directly or after a 202.
- **A builder that dies** reaches the route as **503 `basemap_unavailable`**, or as 202s while the lock holds, and nothing is stored.
- **Only the builder's tail can name why it died.**

| outcome | what is seen | meaning |
|---|---|---|
| **MG-A** (predicted) | 422 `too_large`. The tail shows one build, outcome Ok, with a `bm_build` line whose ladder is **249,684 / 205,868 / 186,014 / 41,061**, DR0's KLGA, the control that staging ran the same build | **workerd built the largest ceiling inside 128 MB.** The local ceilings over-counted, and KLGA's real peak is under the limit. Not proven: a cold isolate, since one request can't say whether this isolate was cold |
| **MG-B** | the tail says **Exceeded Memory**; the route answers 503 or 202s | **the peak is real in workerd: New York at r=162 can't be built today.** D4 waits on the dense-area work cutting the peak (`READ_AHEAD` first) |
| **MG-C** | the tail says **Exceeded CPU** (staging's `cpu_ms` is 14,530) | a different limit; memory is unanswered. **Stop and report** |
| **MG-D** | a 200 | today's ladder can't fit KLGA (DR0). **Stop** |
| **MG-E** | a 422 `stored-error` with **no build in the tail** | an earlier request already stored KLGA's answer, so this measures nothing. **Stop and report** |
| **MG-F** | MG-A, but the ladder differs from DR0's | staging didn't run the build the local reading did. **Stop** |

**Outside the set:** anything else, including a tail that can't attach or shows nothing at all, stops it. A tail that shows nothing is not a
reading.

### Q-MEM2, read (2026-11-08): MG-A, as predicted. Workerd built KLGA inside 128 MB

**When it ran:**
- the registration was frozen at 20:32:17Z (`804e35a2713d`);
- the tail attached at about 20:32:25Z;
- the request was sent at **20:33:07Z**: a 202 at once, then **422 `too_large`** at 20:33:27Z;
- the builder logged its build at **20:33:09Z**.

**Two refusals came first, and no request was sent in either.** `test-identity.sh` refused because this shell had inherited a stale
`BLIP_KEY`, then because `BLIPSCOPE_TOKEN_FILE` was not set. Both were the guard working. The request that ran used the test identity, with
`BLIP_KEY` removed from its environment only.

**The builder's tail: exactly one build, outcome Ok:**

    POST https://basemap.internal/build - Ok
    {"evt":"bm_build","status":422,"reads":98,"bytes":74385925,"readMs":37647,"maxReadMs":3771,"wallMs":8216,"rung":-1,"cap":32768,
     "nt0":249684,"nt1":205868,"nt2":186014,"nt3":41061,"secWater":0,"secRivers":8398,"secRoads":28934,"secPeaks":0,"secPlaces":1597,"secProv":1972}

**Against the table:**
- **MG-A.** The ladder is DR0's KLGA to the byte, so staging ran the same build.
- Its last rung's sections equal #620's KLGA row (roads 28,934, rivers 8,398, places 1,597, provenance 1,972).
- No Exceeded Memory, and no Exceeded CPU.

**What it establishes:**
- **Workerd built the largest local ceiling (107.7 MB) without exceeding 128 MB.** The local figures over-counted, which agrees with Q-MEM1's
  finding that dead buffers are counted.

**What it does not establish:**
- **The margin.** Workerd reports no peak, so "under 128 MB" is all this says.
- **Whether this isolate was cold.** One request can't tell.
- **Production's isolate:** it runs the same code, but nothing here measured it.

**For the dense-area proposal:** every build's peak is still to be stated against 128 MB, as ruled. The source of that figure is Q-MEM1b, if
repeated `gc()` clears the lag; otherwise no local figure stands, and the proposal says so.

**My miss, recorded with its cause:** I stopped the tail by matching its command line, not by a pid recorded at launch. Two process trees
matched:
- one under this session's own `claude.exe`;
- one whose parent had already exited.

I stopped both. The likeliest reading is that both were mine: Git Bash children are routinely left with an exited Windows parent, and
`taskkill /T` from the launcher can't reach such a tree. **But the data doesn't exclude another session's tail of the same Worker.** From
now on: record a background process's pid at launch, and stop only the tree that reaches this session.

### Q-MEM1b, the instrument changed: registered before it runs

**Why it is still needed after MG-A.** Workerd said KLGA fits under 128 MB, not by how much. The dense-area proposal must state each build's
peak against 128 MB (ruled), and `READ_AHEAD`'s effect on it. Only a local figure can give a number, so the local instrument either earns
its place here or is retired (ruled ~20:28Z).

**The change: a settled sample.**
- `gc()`, read `heapUsed + arrayBuffers`, and repeat until two readings in a row agree within 0.1 MiB, at most 4 collections.
- A sample that hasn't settled after 4 is marked **unsettled** and isn't believed.
- Q-MEM1's released hold read +32 MiB after one collection and was swept by the next, so two or three collections should settle.

**The controls, each against its own settled sample, none chained:**

| control | what | must read |
|---|---|---|
| **C1**, live hold | sample; allocate 32 MiB and keep it; sample | **+32 ± 1 MiB** |
| **C2**, released hold | release C1's hold; sample, against C1's first sample | **within 1 MiB** |
| **C3**, dead buffer | a fresh settled sample; allocate 32 MiB and drop it; sample | **within 1 MiB** |
| **C4**, mid-build | C1-C3 again, in the control process, **at a cell-read sample:** the phase that held every Q-MEM1 peak | the same three bounds |

**The same 16 processes as Q-MEM1** (New York's four, three times each; KJAC three times; one control process), each building its query cold,
then warm, with every phase sampled settled. C1-C3 run in every process before its builds. Every build's band is printed as it is measured.

| row | predicted |
|---|---|
| **MB0** | C1, C2 and C3 hold in all 16 processes, and C4 in the control process |
| **MB1** | no unsettled sample anywhere |
| **MB2** | each point's three cold builds agree within 2 MiB, and cold and warm agree within 2 MiB |
| **MB3** | **every settled peak is under its Q-MEM1 ceiling**, and **at most 64 MB** |
| **MB4** | the phase holding each peak: not predicted; reported |

**What the readings will mean, fixed now:**

| outcome | meaning |
|---|---|
| MB0-MB3 as predicted | the settled figures are the peaks the proposal states, as floors on live memory, as before |
| **MB0 or MB1 fails** | **repeated `gc()` can't clear the lag: the local instrument is retired** (ruled), and the proposal says no local figure stands |
| MB2 fails | unreliable at this scale: **stop** |
| MB3's first half fails (a settled peak at or above its ceiling) | the settled sampler measures more than the unsettled one: **stop**, since that is impossible if both are right |
| MB3's second half fails (over 64 MB) | recorded, with its band. Not a stop here, since workerd already answered KLGA (MG-A) |

**Outside the set:** anything else stops it.

**Then, if it stands: `READ_AHEAD`'s effect, as its own registration.** KLGA at `READ_AHEAD` 6, 3 and 1, each cold three times, read with the same
instrument. That is registered after this read, not now.

### Q-MEM1b, read (2026-11-08, 20:40:29-20:40:33Z): stopped at MB0. The control may be at fault, not the instrument

**The driver stopped after its first process,** the control process. The other 15 never ran. The tree stayed clean.

| control | before the builds | mid-build, at the 10th cell read (C4) |
|---|---|---|
| C1, live hold | +32.00 MiB, ok | +32.00, ok |
| **C2, released hold** | **+32.00 MiB: off** | **+32.00: off** |
| C3, dead buffer | 0.00, ok | 0.00, ok |

**Every sample settled:** none unsettled, and at most 3 collections in one. So C2's +32 was a stable reading, not one still moving.

**Two explanations fit, and this read doesn't separate them:**
1. **A lag the collector never clears.** The released buffer is dead but still counted after collections that agree with each other. Then the
   instrument is retired, as registered for MB0.
2. **C2's buffer was never dead.** C2 releases its hold by nulling a local in a function whose frame is still running. V8 can keep that
   value reachable from the frame until the function returns, so the buffer was live and counting it was correct. Then the instrument may
   be sound, and **the control was the defect.**

**What favours 2:**
- **C3's buffer** is dropped inside a function that has already returned. It read 0.00 in 16 of the 17 processes across both runs.
- **Settled sampling,** which makes each collection finish the previous sweep, didn't move C2 at all.
- **Q-MEM1's warm builds** read the same as their cold ones, within 0.73 MiB. Under 1, each warm baseline would carry the cold build's
  uncollected garbage, and so read lower.

**What 2 leaves unexplained:**
- **Q-MEM1's one C3 that read 32.00** (Midtown, third run);
- **DR0's KJFK at 76.9 MiB** against CTRL-a's 97.3, which needs some other difference between those two processes.

**This outcome wasn't in the predicted set:** MB0's registered meaning assumed C2's buffer was dead. So per the outside-the-set line, **stop and
decide.**

**The control that would separate 1 from 2,** offered for a ruling:
- **C2′:** allocate the hold in one function and return it into a module-level slot, clear the slot from a second function, then sample from
  a third. No running frame then holds the buffer.
- Under 1, C2′ reads +32; under 2, it reads within 1 MiB.

### Corrections to earlier statements in this file, found by this read (the originals are kept above)

- **Q-MEM1's "explanation 2 holds"** (its read, "What it means"): **not established.** It rested on the released hold still being counted, and
  I didn't name the alternative that the hold was still reachable. That is the finding-without-a-named-alternative this repo's practice
  forbids.
- **Q-MEM2's MG-A meaning,** as registered ("the local ceilings over-counted") and as read ("the local figures over-counted"): **an overreach,
  in both places.** Every local ceiling (93.6-107.7 MB) is under 128 MB, so a build that fits can't show that any of them over-counted. MG-A
  establishes one thing: **KLGA at r=162 built under 128 MB in workerd.**

### The ruling (reviewer chat, pasted by Daniel, 2026-11-11 ~03:37Z), verbatim

> Q-MEM1c: A, as you recommend. Run C2′ as frozen in #622, then continue on its answer.
> - C2′ is a fair control. put() and drop() each return nothing, so no running frame holds the buffer, and it separates the two
>   explanations: the lag predicts +32 after drop(), frame retention predicts within 1 MiB.
> - Frame retention: re-run Q-MEM1b's 16 processes with C2′ graded in place of C2, MB0–MB4 otherwise unchanged, registered first, stopping
>   at the first off reading. Then READ_AHEAD 6/3/1 at KLGA as its own registration, then Q-DR3. Each figure you state is the highest
>   settled reading across processes for that build, with any spread over 1 MiB between processes for the same build stated beside it.
>   KJFK's 97.3 against 76.9 stays open until the re-run reads it again.
> - The lag: the instrument is retired, as ruled. Your fallback for Q-DR3 is approved now, so you don't wait for me: rung outcomes graded
>   on sizes alone, and memory evidence only from workerd, one staging request per chosen build as in Q-MEM2, with predictions first and
>   only after W3's read-back.
> - Anything else: stop and report, as frozen.
> - Either way, the proposal says what you wrote: C2′ can't show that workerd counts memory the way Node does, and Q-MEM2's KLGA pass is
>   the only workerd reading.
>
> The one C3 reading of 32.00 (Midtown, third run) came from Q-MEM1's one-gc() sample, which the settled sampler replaced, so it isn't
> evidence against frame retention under the settled sampler. It still counts as "anything else" if C1–C3 don't reproduce.
>
> The tail: closed. It was your own; Weatherscope and the UK session say no. The second tree, two bash.exe processes naming the tail with no
> node under them, was most likely your stop command's own shells: a stop that matches command lines also matches the shell that runs it.
> Your miss stands as narrowed. From now on, stop a stream by the process tree you recorded when you launched it, never by matching command
> lines.

**The tail, for this record:** the miss recorded under Q-MEM2's read stands, narrowed as above. My one tail ran from 20:32:21.7Z to the stop
at 20:33:44.7Z, and it was the only running tail of that Worker on this machine.

### Q-MEM1c, the C2′ control: registered before it runs

**What runs:**
- **One local Node process** (`--expose-gc`), the driver `mem-diag3.mts` in its `c2p` mode, at **KLGA** at the map client's query.
- It uses the same patched copy at v0, over the same local store, as Q-MEM1b.
- **Nothing runs on staging.** W3's window holds staging in any case.

**The controls, each against its own settled sample (Q-MEM1b's sampler, unchanged):**
- **C1, C2 and C3 as in Q-MEM1b.** Their allocation and sampling lines are `mem-diag2.mts`'s, compared byte for byte, so C2 can reproduce.
- **C2′ runs after `controls()` has returned,** in its own function:
  1. a settled sample;
  2. `put()` allocates 32 MiB into a module-level slot and returns nothing; a settled sample (**C2′ live**);
  3. `drop()` sets the slot to null and returns nothing; a settled sample (**C2′ released**).
- **No running frame ever holds C2′'s buffer.**
- **Where:**
  - before the builds;
  - mid-build, at the 10th cell read of KLGA's cold build. That is Q-MEM1b's C4 point, at KLGA as proposed. Nov 8's C4 was at KJFK.
- **The warm build** then runs as before. This process measures no peak.

| row | predicted |
|---|---|
| **MC0** | C1 **+32 ± 1**, C2 **+32 ± 1** (Nov 8's off reading, reproduced), C3 **within 1 MiB**, in both places |
| **MC1** | C2′ live **+32 ± 1 MiB**, in both places |
| **MC2** | every sample settled; every object local; KLGA's cold build ends **422 `too_large`**, as DR0's KLGA (the same build) |
| **MC3** | C2′ released: **not predicted.** It is the reading that decides |

**What the readings will mean, fixed now (the table sent as Q-MEM1c, ruled ~03:37Z):**

| MC0-MC2 | C2′ released | meaning | next, as ruled |
|---|---|---|---|
| as predicted | **within 1 MiB, in both places** | **frame retention:** C2 was the defect, and the settled sampler may be sound | the re-run below, then `READ_AHEAD`, then Q-DR3 |
| as predicted | **+32 ± 1, in both places** | **the lag:** the instrument is retired, as ruled. No local figure stands | Q-DR3 on the approved fallback (below) |
| anything else | anything else | **stop and report** | - |

"Anything else" includes:
- C2′'s released reading lying between the two bounds;
- the two places disagreeing;
- any MC0-MC2 row off. C1-C3 must reproduce Nov 8's **+32 / +32 / 0**, or the controls have changed under us.

**The driver carries this table,** prints every condition whichever way it goes, and ends on one `VERDICT` line: exit 0 for frame
retention, 10 for the lag, 1 for a stop.

**What this cannot show, either way:** whether workerd counts memory the way Node does. Q-MEM2's KLGA pass stays the only workerd reading.

### Q-MEM1b, re-run with C2′ in place of C2: registered now, runs only on frame retention

**What runs:**
- **The same 16 processes, in Q-MEM1b's order:**
  - first, KJFK's control process;
  - then KJFK, KLGA, KEWR, Midtown and KJAC, three plain processes each.
- **Each builds its query cold, then warm,** with every phase sampled settled, using the same driver in its `plain` and `ctl` modes.
- **C1-C3 and C2′ run in every process before its builds,** and again mid-build in the control process (C4).
- **C2 still runs and is printed,** reported only, not graded.

| row | predicted (MB1-MB4 as Q-MEM1b's) |
|---|---|
| **MB0** | C1, **C2′** (live +32 ± 1 **and** released within 1 MiB) and C3 hold in all 16 processes, and C4 (the same, mid-build) in the control process |
| **MB1** | no unsettled sample anywhere |
| **MB2** | each point's three cold builds agree within 2 MiB, and cold and warm agree within 2 MiB |
| **MB3** | **every settled peak is under its Q-MEM1 ceiling**, and **at most 64 MB** |
| **MB4** | the phase holding each peak: not predicted; reported |

**Its ceilings, stated now** (MB3 named them without listing them). Each is the highest of Q-MEM1's three readings for that point and
build, compared at 0.1 MB:

| point | cold | warm |
|---|---|---|
| KJFK | 102.0 MB | 101.9 MB |
| KLGA | 107.7 MB | 107.2 MB |
| KEWR | 99.5 MB | 99.1 MB |
| Midtown | 94.2 MB | 94.1 MB |

**KJAC has no Q-MEM1 ceiling,** since Q-MEM1 never reached it. So KJAC's MB3 first half isn't graded; its second half is.

**Graded per build, in the driver, stopping at the first off reading** (the runner stops on any non-zero exit):
- **MB0 and MB1:** in every process, as its controls and samples finish.
- **MB2:** cold against warm in every process. The three colds at a point are graded at its second and third process, from a results
  file the driver appends to.
- **MB3's first half:** at every build, against the table above.
- **MB3's second half:** printed as each build's band; recorded, not a stop.

**What the readings will mean (Q-MEM1b's table, with C2′ in MB0):**

| outcome | meaning |
|---|---|
| MB0-MB3 as predicted | the settled figures are the peaks the proposal states, as floors on live memory |
| **MB0 or MB1 fails** | **repeated `gc()` can't clear the lag: the local instrument is retired** (ruled), and the proposal says no local figure stands |
| MB2 fails | unreliable at this scale: **stop** |
| MB3's first half fails (a settled peak at or above its ceiling) | the settled sampler measures more than the unsettled one: **stop**, since that is impossible if both are right |
| MB3's second half fails (over 64 MB) | recorded, with its band. Not a stop here, since workerd already answered KLGA (MG-A) |

**How its figures are stated (ruled):** each is the highest settled reading across the processes for that build. Any spread over 1 MiB
between processes for the same build is stated beside it.

**KJFK's 97.3 against 76.9:** the re-run's three cold KJFK figures are reported beside both. They aren't predicted, and the question stays
open until this reads them.

**Outside the set:** anything else stops it.

**Then, if it stands:**
1. `READ_AHEAD` 6, 3 and 1 at KLGA, each cold three times, as its own registration after this read;
2. then Q-DR3, stopping at the first build over 96 MB, graded per build.

### If C2′ shows the lag: Q-DR3 on the approved fallback

- **The instrument is retired.**
- **Q-DR3's rung outcomes are graded on sizes alone.**
- **Memory evidence comes only from workerd:** one staging request per chosen build, as in Q-MEM2. Its predictions come first, and nothing
  is sent until W3's read-back is posted.
- **The dense-area proposal then states:**
  - that no local figure stands;
  - each chosen build's workerd outcome;
  - that `READ_AHEAD`'s effect can't be given as a number.

### Q-MEM1c, read (2026-11-11, 03:44:45-03:44:50Z): frame retention. C2 was the defect

The registration was frozen at 03:44:37Z (`439d698ce278`). One process ran, with nothing on staging and the tree clean.

| row | before the builds | mid-build, at KLGA's 10th cell read | |
|---|---|---|---|
| MC0: C1 / C2 / C3 | +32.00 / 32.00 / 0.00 | +32.00 / 32.00 / 0.00 | as predicted: Nov 8's readings reproduced |
| MC1: C2′ live | +32.00 | +32.00 | as predicted |
| MC2 | every sample settled (at most 3 collections); every object local; KLGA's cold build 422 | | as predicted |
| **MC3: C2′ released** | **0.00** | **0.00** | **within 1 MiB in both places** |

**By the frozen table: frame retention.** A buffer that no running frame holds is not counted after a settled sample, in either place.
C2's +32 was a buffer still reachable from its own running function, so counting it was correct. The driver's verdict line read `FRAME
RETENTION`, exit 0.

### Q-MEM1b, re-run with C2′ in place of C2, read (2026-11-11, 03:45:14-03:46:10Z): as predicted, except MB3's second half

All 16 processes ran, in the registered order, and each exited 0. The error file is empty, the tree stayed clean, and nothing ran on
staging.

| row | read | |
|---|---|---|
| **MB0** | C1 +32.00, C2′ +32.00 then 0.00, C3 0.00, in all 16 processes; C4 the same in the control process. C2, reported only: 32.00 in all 17 places | as predicted |
| **MB1** | no unsettled sample. At most 3 collections in one sample at New York, 4 at KJAC | as predicted |
| **MB2** | each point's three colds agree within **0.01 MiB**. Cold against warm: 0.34-0.36 MiB at New York, 1.58-1.61 at KJAC | as predicted |
| **MB3, first half** | every settled peak is under its Q-MEM1 ceiling, by 2.3-3.3 MB | as predicted |
| **MB3, second half** | **over 64 MB at all four New York points**: KJFK, KLGA and KEWR over 96, Midtown 64-96. KJAC at most 64 | **off: recorded, not a stop, as registered** |
| MB4 | the peak is in the cell reads at all four New York points, and in the ladder at KJAC | reported |
| kept between builds | 7.43-8.02 MiB at New York, 7.02 at KJAC | reported |

**The figures, stated as ruled:** the highest settled reading across processes for each build. No spread between processes for the same
build is over 1 MiB; the largest is 0.03 MiB.

| point | cold | warm | band (cold) |
|---|---|---|---|
| KJFK | **95.03 MiB, 99.6 MB** | 94.68 MiB, 99.3 MB | over 96 |
| **KLGA** | **99.54 MiB, 104.4 MB** | 99.18 MiB, 104.0 MB | over 96 |
| KEWR | **92.36 MiB, 96.8 MB** | 92.01 MiB, 96.5 MB | over 96 |
| Midtown | 87.11 MiB, 91.3 MB | 86.75 MiB, 91.0 MB | 64-96 |
| KJAC | 37.31 MiB, 39.1 MB | 35.73 MiB, 37.5 MB | at most 64 |

**What it means, by the registered table:**
- MB0-MB2 and MB3's first half hold. MB3's second half is registered as recorded, not a stop.
- So **the settled figures are the peaks the proposal states,** as floors on live memory in Node.
- KLGA's 104.4 MB is the build workerd ran inside 128 MB (MG-A).
- **Not shown:** whether workerd counts the same way.

**KJFK's 97.3 against 76.9, read again (ruled to stay open until now):**
- The re-run's three cold KJFK builds read **95.03 MiB each**. That is 2.3 MiB under CTRL-a's 97.3, which was one collection per sample in
  a process's first build. The gap is the same as between one collection and a settled sample at every other point.
- It is **18.1 MiB above DR0's 76.9.**
- DR0's process read lower than the settled figures at every point it shares with this run:

  | point | DR0's figure is lower by |
  |---|---|
  | KJFK | 18.1 MiB |
  | KLGA | 16.0 MiB |
  | KEWR | 15.4 MiB |
  | Midtown | 13.4 MiB |
  | KJAC | 3.4 MiB |

- **So DR0's process is the outlier, not CTRL-a.**
- **What made it read low is not established.** That process had a different history:
  - CTRL-b's 32 MiB hold;
  - Natural Earth loaded;
  - the unpatched builder's modules;
  - five builds before KJFK's.

  This read separates none of those.
- Under the ruled rule, the proposal states this run's figures, and DR0's memory figures are not used. Whether that closes the question is
  for the reviewer.

### Corrections to earlier statements in this file, found by these two reads (the originals are kept above)

- **Q-MEM1b's MB3 prediction, "at most 64 MB,"** was wrong by 27-40 MB at New York.
  - **Cause:** I predicted it from Q-MEM1's "the instrument counts dead buffers" reading. C2′ shows that reading came from the control's
    defect.
  - So the one-collection figures were within 2.3-3.3 MB of the settled peaks, not far above them.
- **Q-MEM1's read, "every figure here and in DR0 is a ceiling, not a floor":** superseded.
  - Q-MEM1's figures sit 2.3-3.3 MB above the settled peaks. That is about 2-3 MiB of garbage that one collection had not yet cleared.
  - DR0's sit 3.4-18.1 MiB below them, for a reason not established (above).
- **Q-MEM1's read, "each cell is a buffer read and then dropped":** wrong, by the code.
  - `readInOrder` pushes every read's promise into `pending` and never removes it.
  - A resolved promise keeps its bytes, so every cell read stays reachable until the cell loop ends.
  - The `READ_AHEAD` registration below tests this.

### `READ_AHEAD` 6, 3 and 1 at KLGA: registered before it runs

**What runs:**
- **Nine local Node processes** at KLGA, at the map client's query, at v0, in the order 6, 6, 6, 3, 3, 3, 1, 1, 1. Nothing runs on
  staging.
- **The driver is `mem-diag4.mts`.** In each process: C1, C2′ and C3 (mem-diag3's code), then **one cold build** with every phase
  sampled settled.
- **The patched copy gains one line:** `READ_AHEAD` is read from `DENSE_READ_AHEAD`, default 6 (the repo's value). The runner sets it per
  process, before the builder is imported.
- **The driver refuses unless the `READ_AHEAD` in effect,** imported from the builder itself, equals the value asked for.

**Why I predict no effect, from the code:**
- `readInOrder` issues up to `READ_AHEAD` reads ahead.
- It also keeps every read's promise in `pending` until the loop ends, so every cell read so far stays reachable.
- At the last cell's sample, every cell has been read, whatever `READ_AHEAD` is.
- **KLGA's 21 cells total 60.91 MiB.** That was read from the local store before this registration, with no memory sampled. The largest
  window of 6 cells is 23.86 MiB, of 3 is 13.39 MiB, and of 1 is 4.86 MiB.
- So the peak sits at the end of the loop and holds all 60.91 MiB, at any `READ_AHEAD`.
- **If only the reads in flight were held instead,** the peak would fall with `READ_AHEAD`, by up to about 19 MiB from 6 to 1, and would
  sit before the loop's end.

| row | predicted |
|---|---|
| **RA0** | `READ_AHEAD` 6, three processes: the knob takes, and each cold peak is **99.54 ± 0.10 MiB at "cell"**, 422. That is the re-run's KLGA, reproduced through the knob |
| **RA1** | `READ_AHEAD` 3 and 1, three processes each: each cold peak is **within 1 MiB of `READ_AHEAD` 6's highest** |
| **RA2** | in every process, **the last cell's sample is within 0.10 MiB of the cell phase's largest:** the peak is at the end of the loop |
| **RA3** | C1 +32 ± 1; C2′ +32 ± 1, then within 1; C3 within 1; every sample settled; every object local; every build 422 |
| RA4 | the later phases' figures: reported, not predicted |

**Graded per build, in the driver, stopping at the first off reading.**

**What the readings will mean, fixed now:**

| outcome | meaning |
|---|---|
| RA0-RA3 as predicted | **`READ_AHEAD` is not the lever on the peak.** The peak holds every cell KLGA reads (60.91 MiB of its 99.54), because `readInOrder` keeps each read after it is consumed. The lever is to release a read once it is consumed. That is a code change, proposed for a ruling with its own registration, and not made here |
| RA1 off: 3 or 1 more than 1 MiB under 6 | `READ_AHEAD` does move the peak, so my reading of the code is wrong somewhere: **stop and report**, with the figures |
| RA2 off | the peak isn't at the end of the loop: the same, **stop and report** |
| RA0 or RA3 off | the knob or the instrument is not what was measured before: **stop** |

**Outside the set:** anything else stops it.

**What this cannot show:** workerd. The proposal states Node's figures, beside Q-MEM2's KLGA pass.

### `READ_AHEAD`, read (2026-11-11, 03:53:08-03:53:10Z): stopped at RA0, in the first process

The registration was frozen at 03:52:59Z (`bc0314987e2f`). One process ran, then the driver stopped itself (exit 8). The error file is
empty, the tree stayed clean, and nothing ran on staging.

| row | read | |
|---|---|---|
| the knob | `READ_AHEAD` 6 in effect, read back from the builder | took |
| RA3 | C1 +32.00; C2′ +32.00, then 0.00; C3 0.00; every sample settled (at most 3 collections); every object local; 422 | as predicted |
| **RA0** | **99.42 MiB** at "cell", against **99.54 ± 0.10** | **off by 0.02 MiB beyond the bound: stop** |
| RA2, printed, not graded (the stop came first) | the cell phase's largest is at **cell 16 of 21**; the last cell reads **97.65, 1.77 MiB below it** | would have been off |
| RA4 | tiles 54.97, terrain 55.22, layers 58.36, ladder 58.39, end 7.25 MiB | reported |

**What RA0's 0.12 MiB can and can't be:**
- **The build changed:** excluded.
  - The patched copy differs from the re-run's by one line, which the diff shows.
  - The driver read back `READ_AHEAD` = 6 from the builder.
  - So the reads were issued exactly as in the re-run.
- **The driver changed what a baseline-relative sample sees:** not excluded. `mem-diag4` differs from `mem-diag3` in three ways:
  - no C2 sample;
  - one build;
  - more module code.

  A shift of about 0.1 MiB from that is unsurprising.
- **My miss, with its cause:** I set RA0's ± 0.10 from `mem-diag3`'s spread between processes (0.01 MiB). That spread measures how
  repeatable one driver is, not whether two drivers agree. The bound was tighter than a change of driver can be expected to hold.

**What the printed RA2 reading says, and it matters more than RA0:**
- **Cell 16 is where, at `READ_AHEAD` 6, the last of KLGA's 21 reads is issued.** From then on no cell buffer arrives, so under my
  reading (every read is kept until the loop ends) the samples should stay flat or rise as the layer sets grow.
- **They fall by 1.77 MiB instead.** Two explanations fit, and this read separates neither:
  1. **The build releases something during its last cells** that my reading of the code missed.
  2. **Something outside the build shrinks during it.** The settled sampler runs about 70 full collections per build, and V8 can, for
     example, flush compiled code that hasn't run recently. Then a baseline-relative sample falls with nothing in the build released.
- **For most cells being held until the loop ends:** the fall from the last cell (97.65) to the tiles phase (54.97) is 42.7 MiB, as the
  read generator finishes. Holding only the reads in flight would release a few MiB there, not 42.7.
- **Against a clean separation:** the cell list and the store index also go out of scope at that point.

**By the registered table: RA0 off, so stop.** Nothing is graded past it. The proposal that follows goes to the reviewer before anything
resumes.

### The ruling (reviewer chat, pasted by Daniel, 2026-11-11 ~04:10Z), verbatim

> C2′ settled it: frame retention. The control was faulty, the settled sampler is sound, and the re-run's figures stand as Node's figures.
> The three corrections are accepted, with the originals kept.
>
> KJFK: closed. Three cold builds agree at 95.03 MiB on the validated sampler, and DR0 ran in a different process state with the faulty
> control. Record it as "DR0: outlier, cause not established, not used". The proposal uses this run's figures.
>
> Q-RA1: A, as you recommend. Re-register RA0 anchored to this driver, add C5 (drift), and grade RA2 with C5. Then the release variant gets
> its own registration. Your miss on RA0's ± 0.10 stays on record: a bound between two drivers comes from a measured spread between
> drivers, never from one driver's repeatability.
>
> Q-DR3a: (a). Q-DR3 waits for the release reading, then runs with whichever lever that reading supports. The release reading is local and
> quick, and Q-DR3 then grades sizes and memory together, once, on the builder that will ship.
>
> Staging is free again, though your local work needs no slot. #622 joins the merge list when you say so.

**KJFK, as ruled: DR0: outlier, cause not established, not used.**

### `READ_AHEAD` 6, 3 and 1 at KLGA, re-registered (Q-RA1, A): registered before it runs

**What runs:**
- **Nine local processes** at KLGA, in the order 6, 6, 6, 3, 3, 3, 1, 1, 1. Nothing runs on staging.
- **The same patched copy and knob** as the stopped run.
- **The driver, `mem-diag5.mts`,** is `mem-diag4` with three changes:
  1. RA0 is anchored to this driver;
  2. **C5** runs after the build;
  3. RA2 is graded together with C5, not as a stop by itself.

**C5, drift.**
- **What it does:** after the build, in the same process, it takes settled samples with nothing built between them. It stops once it has
  run **as many full collections as that process's build ran**, which is about 70.
- **It must read** within 0.50 MiB of its first sample (**holds**), or end at least 1.00 MiB below it without ever rising 0.50 above
  (**falls**). Anything else stops the run.
- **Why after the build:** run before, it could age out compiled code and move the very peak RA0 anchors on.
- **Where its bounds come from:**
  - 0.50 is well above the largest spread between processes measured for one build (0.03 MiB).
  - 1.00 is well below the fall it has to explain: 1.77 MiB over the last five cells.

**The bounds, and where each comes from** (as ruled, a bound between drivers comes from a spread measured between drivers):
- **Against the re-run's 99.54: ± 0.50 MiB.** The spread measured between driver configurations at KLGA's cold build is **0.14 MiB**:
  - 99.42, `mem-diag4`;
  - 99.53-99.54, `mem-diag3`, plain;
  - 99.56, `mem-diag3`'s C2′ process, cell phase.

  The bound is about 3× that.
- **Among this driver's three `READ_AHEAD` 6 reads: at most 0.10 MiB.** That is one driver's repeatability, and the measured figure for one
  driver is 0.01 MiB (`mem-diag3`, three processes).

| row | predicted |
|---|---|
| **RA0** | `READ_AHEAD` 6: the knob takes. Each cold peak is **within 0.50 MiB of 99.54, at "cell"**, and the three agree **within 0.10 MiB** |
| **RA1** | `READ_AHEAD` 3 and 1: each cold peak **within 1 MiB of `READ_AHEAD` 6's highest** (unchanged) |
| **RA3** | C1 +32 ± 1; C2′ +32 ± 1, then within 1; C3 within 1; every sample settled, C5's included; every object local; every build 422 (unchanged) |
| **C5** | **the same reading, holds or falls, in all nine processes.** Which one is not predicted: it is what decides |
| **RA2** | at `READ_AHEAD` 6, in all three: the last cell's sample **more than 0.10 MiB below** the cell phase's largest. That is the stopped run's printed reading, now predicted. At 3 and 1: reported, not graded |
| RA4 | the later phases' figures: reported |

**Graded per build, in the driver, stopping at the first off reading.** The stops are:
- RA0, RA1 and RA3;
- a C5 that neither holds nor falls;
- a C5 that differs from an earlier process's.

The joint reading is graded once the ninth process finishes.

**What the readings will mean, fixed now:**

| RA2 at `READ_AHEAD` 6 (all three) | C5 (all nine) | meaning | next |
|---|---|---|---|
| off | **holds** | **explanation 1:** the build itself releases something in its last cells | the release variant's predictions are re-derived from the code, naming what is released, before its registration |
| off | **falls** | **explanation 2:** something outside the build shrinks, so RA2's fall is not the build's | the release variant is read against C5's drift |
| anything else | anything else | **stop and report** | - |

**And RA1, whichever row:**
- If RA1 holds, `READ_AHEAD` is not the lever on the peak.
- If it fails, `READ_AHEAD` moves the peak: **stop and report**, as before.

**What C5 cannot see:**
- It runs after the build, so it covers whatever shrinks with nothing being built. Something that shrinks only while a build runs would read
  "holds" and be taken for explanation 1.
- If that matters, the release variant's own registration is where it gets separated.

**Outside the set:** anything else stops it.

**Then:**
1. the release variant, as its own registration (ruled);
2. then Q-DR3, on the lever that reading supports (ruled (a)).

### `READ_AHEAD`, re-registered, read (2026-11-11, 04:33:16-04:33:24Z): stopped at RA1, in the fourth process. `READ_AHEAD` 3 is 1.77 MiB under 6

The registration was frozen at 04:33:08Z (`cd00ec265629`). Four processes ran; the driver stopped itself in the fourth (exit 7). The
error file is empty, the tree stayed clean, and nothing ran on staging.

| process | peak | where | the last cell | C5 (78 collections after the build; the build ran 77) | |
|---|---|---|---|---|---|
| `READ_AHEAD` 6 | 99.49 MiB | cell 16 of 21 | 97.72 | holds: +0.00 / 0.00 | RA0 ok |
| `READ_AHEAD` 6 | 99.52 | cell 16 | 97.75 | holds | RA0 ok |
| `READ_AHEAD` 6 | 99.53 | cell 16 | 97.75 | holds | RA0 ok; the three spread 0.04 |
| **`READ_AHEAD` 3** | **97.76** | **cell 21 of 21** | 97.76 | holds | **RA1 off: 1.77 MiB under 6's 99.53** |

**RA3 held in all four:** controls, settling (at most 3 collections), objects and 422. **RA2 at 6 was off in all three,** as predicted.

**The joint table was not graded,** since the stop came before the ninth process. Its printed pattern so far is RA2 off at 6 with C5 holding,
which is the explanation-1 row.

**By the registered table: RA1 off. `READ_AHEAD` moves the peak, so stop and report.**

**What the four processes show, and how my reading of the code fares:**
- **Held:** every read is held until the loop ends, whatever `READ_AHEAD` is. The last cell reads 97.72-97.76 MiB at both 6 and 3.
- **Wrong:** that the peak sits at the loop's end.
  - At 6 it sits at cell 16, where every read is first held, and it is **1.77 MiB above the loop's end.**
  - At 3, every read is first held only at cell 19, after whatever made cell 16 high has gone. So the peak falls to the loop's end.
- **`READ_AHEAD` 1 can't go lower than 97.75.** At the loop's end every read is held at any setting. So **the most `READ_AHEAD` can save at
  KLGA is about 1.8 MiB of 99.5.** That is a bound from these readings and from the code, not a measurement at 1.

**What makes cell 16 1.78 MiB higher than the loop's end is not established.**
- **Not the layer sets:** `LayerSet.add` only appends.
- **Not something outside the build,** as far as C5 can see: 0.00 MiB of drift after every build.
- **The candidate:** `decodeCell` returns a cell's features as one array, which the loop iterates. At the sample that follows, the array may
  still be reachable from `build`'s running frame, which is C2's effect again. Each cell's sample would then include that cell's decoded
  features, and those differ from cell to cell.
- **Either way it is real memory:** the array is live while its cell is processed, so workerd pays it too.
- **The alternative:** some other per-cell state. This run can't separate them, because it prints only each phase's largest sample, not
  every cell's.

### The ruling (reviewer chat, pasted by Daniel, 2026-11-11 ~04:39Z), verbatim (its Q-RA2 part)

> Q-RA2: A. Stop measuring READ_AHEAD. The proposal states it as measured: 6 to 3 saved 1.77 MiB at KLGA in one process, and no setting
> can read below the loop's end, so the most it can save is about 1.8 MiB. It is not D4's lever. Register the release variant now: derive
> its exact prediction before anything runs, print every cell's sample so the same run tests the decoded-features candidate, and include a
> control that can fail. If the variant becomes the real builder, that's its own PR and review.

**`READ_AHEAD`, for the proposal, as ruled:** 6 to 3 saved 1.77 MiB at KLGA, in one process. No setting can read below the loop's end, so
the most it can save is about 1.8 MiB. It is not D4's lever.

### The release variant at KLGA: registered before it runs

**What changes, in the scratch copy only:**
- **`readInOrder` gains a knob,** `DENSE_RELEASE=1`. With it set, each read's slot in `pending` is cleared once the read is handed over,
  so a consumed read is let go. The change is one line:
  - set: `const v = await pending[i]!; pending[i] = undefined; yield v;`
  - unset: the original `yield await pending[i]!`, unchanged.
- **The tile loop gains a sample** after each tile, `MEM?.("tile")`. The tile loop reads through the same `readInOrder`. Once the cells
  are let go, it could hold the build's true peak, and nothing sampled inside it before.
- **Both edits are byte-checked against a kept copy** (`dn-build.ts.before-rv`).
- **What stays the same:** `decodeFeature` copies every number and string into new arrays, so no view into a read survives it. Letting a
  read go therefore frees all of it.

**What runs (local, at KLGA at the map client's query, `READ_AHEAD` 6, in the driver `mem-diag6.mts`):**
- **In every process:**
  1. C1, C2′ and C3;
  2. one cold KLGA build, with **every cell's and every tile's sample printed**, and each read's size;
  3. one unsampled KJAC build, whose package bytes are hashed.
- **v0 processes also measure each cell's decoded size directly** after the build. The cell's buffer is decoded into a module slot that no
  running frame holds, and settled samples are taken before and after.
- **Phase 1:** three v0 processes, without the knob.
- **Between the phases:** `pred6.mts` computes the variant's exact predicted trace from phase 1 by the formula below. The numbers and the
  prediction file's sha256 are **frozen in this record and pushed before phase 2 runs.**
- **Phase 2:** three release-variant processes, then three **planted** ones. A planted process has the knob set, plus a store that keeps
  every buffer it hands out, which restores v0's holding.

**The prediction, derived now:**
- **What the variant holds:** at each sample, the same as v0 minus the reads consumed before that sample. Nothing else differs: the same
  reads are issued at the same moments, and the decoding and layer sets are the same.
- **So, for sample k:**
  - **cell k:** v0's sample at cell k, minus the sizes of cells 1 to k−1;
  - **tile k:** v0's sample at tile k, minus the sizes of tile-list reads 1 to k−1;
  - **after the loops** (tiles, terrain, layers, ladder, end): v0's samples, since v0 lets its reads go when each loop ends.
- **The plant holds every read,** so each cell sample equals v0's.
- **The direction is known now:**
  - v0's cell phase reads 97.72-99.53 MiB, and holds all 60.91 MiB of KLGA's cells by cell 16.
  - The variant's last cell must therefore read at least **36.8 MiB**, which is everything else.
  - Its cell phase is predicted to read tens of MiB lower.
  - The build's largest may move into the tile loop.
- **The exact figures** are the frozen trace.

| row | predicted |
|---|---|
| **RV0** (phase 1) | v0, three processes: the cell phase's largest within **0.50 of 99.51** (`mem-diag5`'s three) at **cell 16**, the three within **0.10**; the build's largest in the cell phase |
| **RV3** (every process) | C1 +32 ± 1; C2′ +32 ± 1, then within 1; C3 within 1; every sample settled; every object local; KLGA 422 with **DR0's ladder** (249,684 / 205,868 / 186,014 / 41,061); **KJAC 200, its package hash the same in all nine** |
| **RV1** (phase 2) | the variant, three processes: **every sample within 0.50 MiB of the frozen trace**: cells, tiles, and after the loops |
| **PL1** (phase 2, **the control that can fail**) | the plant, three processes: **every cell sample within 0.50 MiB of v0's.** Its later samples are reported |
| **DF** (phase 1) | **not predicted; graded both ways.** It decides the decoded-features candidate (below) |

**DF:**
- **What it compares:** from cell 16, where the last read is issued, every read is held and the layer sets only grow. Each cell's sample
  minus that cell's own decoded size is compared from one cell to the next.
- **The candidate holds** if that value never falls by more than 0.10 MiB between cells. Then each cell's sample includes that cell's
  decoded features, which account for the fall from cell 16 to 21.
- **It fails** if any step falls further. Then "some other per-cell state" stands.

**Where the bounds come from:**
- **0.50:** comparisons between configurations (two drivers, or two code paths). It is about 3× the spread measured between driver
  configurations at this build (0.14 MiB).
- **0.10:** within one driver: the three v0 processes, and DF's steps within one process. The measured spread for one driver is 0.04 MiB
  (`mem-diag5`'s three).

**What the readings will mean, fixed now:**

| outcome | meaning | next |
|---|---|---|
| RV0 and RV3 hold; RV1 and PL1 hold | **the variant cuts KLGA's peak to the frozen figure, and the cut is the reads it lets go.** The plant restores v0 | the proposal states the figure; Q-DR3 runs on the variant (ruled (a)); the variant as the real builder is its own PR and review (ruled) |
| RV1 off, PL1 holds | the variant still holds something the formula doesn't count, such as a consumed read reachable from a running frame | **stop and report** |
| PL1 off | holding every read does not restore v0, so the variant's difference is not only the reads | **stop** |
| KJAC's hash differs, or KLGA's ladder | the variant changes what is built | **stop** |
| RV0 or RV3 off (phase 1) | the anchor or the instrument is not what was measured before | **stop**; nothing is predicted from it |

**DF, either way, is reported beside the table and is not a stop.**

**What this cannot see:**
- **Workerd.**
- **Memory between two samples,** such as inside one cell's decode or one tile's PNG decode. Samples come only at the hooks.
- **Other points.** New York's other three points and the dense rungs come with Q-DR3, on whichever builder this supports (ruled (a)).

**Outside the set:** anything else stops it.

### The release variant, phase 1, read (2026-11-11, 04:57:45-04:57:53Z): stopped at RV0, in the second v0 process

The registration was frozen at 04:57:36Z (`27054fc20d7c`). Two v0 processes ran; the driver stopped itself in the second (exit 8). The error
file is empty, the tree stayed clean, and nothing ran on staging. No prediction file was made, and phase 2 didn't run.

| row | v0, process 1 | v0, process 2 | |
|---|---|---|---|
| RV3 | controls ok; settled (at most 3); objects local; 422, DR0's ladder; KJAC 200, `b3f428e5860e` | the same; KJAC `b3f428e5860e` | as predicted |
| RV0, against 99.51 ± 0.50 | 99.33 at cell 16 (−0.18) | 99.55 at cell 16 (+0.04) | as predicted |
| **RV0, the three within 0.10** | - | **spread 0.22** | **off: stop** |
| **DF** | sample minus decoded size, cells 16-21: 92.71 / 94.01 / 94.91 / 95.12 / 95.33 / 95.49 | 92.93 / 94.23 / 95.13 / 95.34 / 95.54 / 95.71 | **the candidate holds in both:** never falls, rising at least 0.17 MiB a step |

**The spread is an offset per process, not a different build.**
- Process 2 reads 0.22-0.23 MiB above process 1 at every cell, from cell 1, and 0.35-0.36 MiB above it in the tile loop and after.
- The two traces have the same shape, read the same objects, and give the same ladder and KJAC bytes.
- **What the offset is:** not established. A difference in each process's own state at its baseline fits, and this read can't separate
  that from anything else.

**My miss, with its cause:** I set the 0.10 among the three v0 processes from `mem-diag5`'s repeatability (0.04). This driver's repeatability
had never been measured. It is the RA0 miss in a new form: the bound for a quantity has to come from a spread measured for that quantity,
in the configuration that will be graded.

**DF, read both ways, as registered:**
- **The candidate holds:** each cell's sample includes that cell's decoded features. Cell 16 decodes to **6.62 MiB** (its read is 4.72) and
  cell 21 to 2.06.
- So the fall from cell 16 to 21 (1.78 MiB) is cell 16's decoded features (6.62) giving way to cell 21's (2.06), less the layer sets'
  growth (2.78).
- It is real memory: the decoded cell is live while its features are added.

**Also printed, reported:**
- **The tile loop holds 73 tiles.** Its largest sample is 61.60 / 61.95 MiB at its end; v0's tile reads are 0.04-0.13 MiB each.
- **After the loops:** tiles 56.23 / 56.59, terrain 56.48 / 56.84, layers 59.61 / 59.97, ladder 59.67 / 60.02, end 8.53 / 8.88.
- **Decoded size per cell:** 0.27-6.62 MiB, the same in both processes to 0.03.

**A preview from these two processes, by the registered formula. Not frozen, not graded:**
- The variant's cell phase would read about 53.6 MiB at cell 16. That is v0's 99.44, less 45.88 MiB of cells 1-15.
- Its build's largest would move to the ladder phase, about 59.8 MiB (62.7 MB).
- **Why the ladder phase is a floor:** the formula keeps v0's figures after the loops. Letting reads go can't reduce what the layer sets
  and the ladder hold.

**By the registered table: RV0 off, so stop.** Nothing is graded past it.

### The ruling (reviewer chat, pasted by Daniel, 2026-11-11 ~05:06Z), verbatim (its Q-RV1 and DF parts)

> Q-RV1: A. Three fresh v0 processes, graded on RV3 and the between-driver anchor (±0.50 of 99.51 at cell 16), with their spread measured
> and reported, not bounded. pred6 then sets phase 2's per-sample bound at twice the largest spread at any one sample, never under 0.50,
> frozen with the trace before phase 2. Your miss stays on record, and it's the same lesson as RA0: measure a driver's repeatability with
> that driver before bounding anything on it.
>
> DF holding is a real finding, and the preview, the ladder phase near 62.7 MB, is promising. It stays a preview until phase 2 grades it.

### The release variant, re-registered (Q-RV1, A): registered before it runs

**What changes from the registration at `27054fc20d7c`, and nothing else does:**
- **Phase 1** is three fresh v0 processes. The stopped run's two are not reused.
  - Each is graded on **RV3** and on **RV0's anchor:** the cell phase's largest within ± 0.50 of 99.51, at cell 16, and the build's largest
    in the cell phase.
  - **Their spread is measured and reported, not bounded.**
- **Between the phases,** the formula script, now `pred7.mts`, also sets **phase 2's per-sample bound:** twice the largest spread among v0's
  three at any one sample (cells, tiles, and after the loops), and never under 0.50.
  - The bound is frozen with the trace before phase 2: its value and the prediction file's sha256, in this record, pushed.
- **RV1 and PL1** are graded at that frozen bound. The driver, now `mem-diag7.mts`, reads the bound from the frozen file and refuses one that
  isn't a number of at least 0.50.

**Unchanged:**
- the variant and the tile-loop sample in the scratch copy;
- the formula;
- RV3, and the KJAC hash and KLGA ladder checks;
- DF, graded both ways and not a stop;
- the meaning table;
- "outside the set".

**For orientation only, not a prediction:** the stopped run's two processes differed by up to 0.36 MiB at one sample, which would give a
bound of about 0.72. The bound is whatever the fresh three give.

### The release variant, re-registered, phase 1, read (2026-11-11, 05:10:16-05:10:30Z): as predicted

The registration was frozen at 05:10:08Z (`96854fe67da4`). Three v0 processes ran, each exiting 0. The error file is empty, the tree stayed
clean, and nothing ran on staging.

| row | v0, process 1 | process 2 | process 3 |
|---|---|---|---|
| RV0: the cell phase's largest, ± 0.50 of 99.51, at cell 16 | 99.39 (−0.12) | 99.55 (+0.04) | 99.54 (+0.03) |
| RV3 | controls ok; settled (at most 3); objects local; 422, DR0's ladder; KJAC `b3f428e5860e` | the same | the same |
| DF (both ways) | holds (never falls; the smallest step +0.17) | holds | holds |

**The spread, measured, not bounded (ruled):**
- **0.16 MiB** at the cell peak;
- **0.392 MiB** at the largest single sample, which is in the tile loop;
- 0.164 across the cells, and 0.360 after the loops.

### The release variant, phase 2: the predicted trace, frozen before it runs

**Made by `pred7.mts`** from phase 1's three traces, by the registered formula. The prediction file `pred7.json` has sha256
`5d96afdbb2e24a3ed6b2d41c866eeca546ac005c6d8105a2b21f7241d3f84481`. Phase 1's results file is `b0150c6ca7f7`.

**The bound for every sample in RV1 and PL1:** twice 0.392, and never under 0.50, so **0.785 MiB.**

**The variant (RV1), predicted, in MiB above its baseline:**

| cells 1-7 | 27.32, 30.03, 33.67, 33.16, 33.40, 38.92, 46.32 |
|---|---|
| cells 8-14 | 45.96, 45.49, 44.89, **52.47**, 51.67, 47.53, 48.66 |
| cells 15-21 | 46.51, **53.60**, 47.54, 44.82, 40.17, 39.86, 38.59 |
| tile loop | 73 samples, 37.48 at tile 1 to **56.60 at tile 72** (largest) |
| after the loops | tiles 56.47, terrain 56.72, layers 59.86, **ladder 59.90**, end 8.77 |

- **The variant's largest is predicted at 59.90 MiB (62.8 MB, at most 64), in the ladder phase.**
- v0's is 99.49 MiB (104.3 MB) at cell 16.
- The cell phase's largest falls from 99.49 to 53.60.

**The plant (PL1), predicted:** each cell equals v0's mean.

| cells 1-7 | 27.32, 33.44, 39.24, 42.52, 47.61, 53.33, 62.71 |
|---|---|
| cells 8-14 | 66.80, 69.70, 71.75, 84.16, 87.29, 87.78, 90.77 |
| cells 15-21 | 91.14, **99.49**, 98.16, 98.52, 96.62, 97.38, 97.72 |

The plant's later samples are reported, not graded.

**Order:** three variant processes, then three planted ones. Each is graded per build, stopping at the first off reading, against this
file. The meaning table is as registered at `27054fc20d7c`.

### The release variant, phase 2, read (2026-11-11, 05:12:02-05:12:06Z): stopped at RV1, in the first variant process, at one cell

The trace was frozen at 05:11:54Z (`8469f8d575c3`). The runner confirmed `pred7.json`'s sha256 before running. One variant process ran; the
driver stopped itself (exit 15). The error file is empty, the tree stayed clean, and nothing ran on staging. The plant didn't run.

| | read | frozen | difference |
|---|---|---|---|
| RV3 | controls ok; settled (at most 3); objects local; 422, DR0's ladder; KJAC `b3f428e5860e` | - | as predicted |
| cells 1-10 and 12-21 | 27.37, 30.07, 33.72, ... 53.64 at cell 16, ... 38.63 | 27.32, 30.03, 33.67, ... 53.60, ... 38.59 | **+0.04 to +0.05 at every one** |
| **cell 11** | **57.34** | **52.47** | **+4.87: off (bound 0.785)** |
| the tile loop's largest | 56.73 at its end | 56.60 | +0.13 |
| after the loops | tiles 56.66, terrain 56.83, layers 59.98, **ladder 60.01**, end 8.88 | 56.47, 56.72, 59.86, **59.90**, 8.77 | +0.11 to +0.19 |
| **the build's largest** | **60.01 MiB (62.9 MB, at most 64), in the ladder phase** | 59.90, in the ladder phase | +0.11 |

**By the registered table:** RV1 off, with PL1 not yet read. **The variant still holds something the formula doesn't count. Stop and report.**

**What cell 11's excess is, as far as this read goes:**
- The other cells sit about 0.05 above their frozen values, so the excess is **about 4.82 MiB, at cell 11 only.** By cell 12 the trace is
  back on the formula (51.71 against 51.67).
- **It can't be anything v0 holds too,** such as a second copy of a decoded cell. The formula subtracts v0's trace, so that would cancel.
  It has to be something v0 holds and the variant should not: **a consumed read still reachable.**
- **Which read:**
  - **cell 10's,** 4.82 MiB, the one consumed just before. A stale slot in a running frame, C2's effect, would fit.
  - **cell 4's,** 4.86 MiB. Its size is within noise of the excess, though I know of no mechanism that would hold it.

  The sizes alone can't separate the two.
- **Whether it repeats, or appears at other cells in other processes:** not known. One process ran.

**What it does and does not change:**
- **The build's largest is unchanged.** It is in the ladder phase, and cell 11's 57.34 sits under it.
- Even if every cell held its previous read as well, the largest such cell would be cell 11's 57.29 (52.47 + 4.82), which is still under
  the ladder phase's 59.90.
- **Not shown:** whether workerd keeps a consumed read reachable in the same way, or more often.

### The ruling (reviewer chat, pasted by Daniel, 2026-11-11 ~05:18Z), verbatim

> Phase 1: accepted, as predicted, with the spread measured (0.392 at most) and the bound derived from it (0.785).
>
> Phase 2's first process: the result that matters held. The variant's largest was 60.01 MiB in the ladder phase, within 0.785 of the
> frozen 59.90, with KJAC and DR0's ladder unchanged. Cell 11's +4.87 MiB is a real off reading, and it stays on record.
>
> Q-RV2: A, tightened. Re-register phase 2, with pred7.json and 0.785 unchanged, and run all six processes fresh: three variant, three
> planted.
> - RV1's one exception: a cell may read above its frozen value by the size of the read consumed just before that cell, ± 0.785. That is
>   the stale-slot mechanism's own prediction. Record the cell, the excess, and every read it matches. Any other excess, including one that
>   matches only an older read, is off: stop.
> - The build's largest stays graded against 59.90 ± 0.785, and the planted processes must reproduce v0. Those two are what the proposal
>   stands on.
> - Report how many cells, in which processes, showed the exception. A consumed read still reachable is memory workerd may pay too.

### The release variant, phase 2, re-registered (Q-RV2, A tightened): registered before it runs

**Unchanged:**
- `pred7.json`, sha256 `5d96afdbb2e2…`, which the runner checks before running;
- the bound, 0.785 MiB;
- the scratch copy, `6281e0f2b00e`;
- RV3, and PL1;
- the meaning table.

**Six fresh processes:** three variant, then three planted, against a fresh results file. They run in the driver `mem-diag8.mts`, which
differs from `mem-diag7` in three places, and in nothing else:

1. **RV1's one exception,** for the variant's cells only:
   - **Allowed:** a cell may read above its frozen value by the size of **the read consumed just before it** (cell k−1's), ± 0.785. That
     cell is recorded with its excess and **every read it matches**, and it is not a stop.
   - **Off, and a stop:** any other excess, including one that matches only an older read.
   - **No exception anywhere else:** tiles, the samples after the loops, and the plant's cells.
2. **The build's largest is graded on its own:** against **59.90 ± 0.785**, in every variant process.
3. **KJAC's package is graded against v0's frozen hash** `b3f428e5860e`, as well as against the earlier processes.

| row | predicted |
|---|---|
| RV3 | as registered |
| **RV1** | every sample within 0.785 of `pred7.json`'s variant trace, **except the named exception** |
| **the build's largest** | within **59.90 ± 0.785**, in all three variant processes |
| **PL1** | every planted cell within 0.785 of v0's mean, in all three planted processes |
| **the exception's count** | **not predicted; reported:** how many cells, in which processes, and which reads they match |

**What the proposal stands on (ruled):** the build's largest, and PL1.

### The release variant, phase 2, re-registered, read (2026-11-11, 05:21:20-05:21:35Z): the variant as predicted; stopped at PL1, in the first planted process

The registration was frozen at 05:21:13Z (`5c983f8bbcd3`). The runner confirmed `pred7.json`'s sha256. Four processes ran: three variant
processes, each exiting 0, then one planted process, which stopped itself (exit 16). The error file is empty, the tree stayed clean, and
nothing ran on staging.

**The variant, three processes:**

| | process 1 | process 2 | process 3 |
|---|---|---|---|
| RV3 | ok; KJAC `b3f428e5860e`; DR0's ladder | the same | the same |
| RV1, the exception aside | within 0.09 MiB | within 0.15 | within 0.14 |
| **the build's largest, 59.90 ± 0.785** | **59.99 at "ladder"** | **60.02 at "ladder"** | **60.02 at "ladder"** |
| **the exception** | **cell 11, +4.83** | **cell 11, +4.87** | **cell 11, +4.87** |

**The variant's largest, stated as ruled:** **60.02 MiB (62.9 MB, at most 64), in the ladder phase.** That is the highest across three
processes; they spread 0.03. **v0's is 99.55 MiB (104.4 MB) at cell 16.**

**The exception: one cell, cell 11, in all three processes,** and in the stopped run's process too.
- In every case it matches the reads of cells 4 (4.86), 7 (4.45) and 10 (4.82). Cell 10's is the read consumed just before.
- No other cell showed any excess.
- **For workerd:** the largest cell sample including it is 57.35. That is under the ladder phase, but it is real memory in Node, and
  workerd may pay it too.

**The plant: PL1 off, at cell 15** (95.84 against v0's 91.14). **By the registered table: stop.**

**What the planted process shows:**
- **Plant minus v0's mean, per cell:** **+0.05 at cells 1-11**, then **+4.70 at cells 12-21.**
- The +0.05 is the same per-process offset the variant shows.
- **The +4.65 step is the size of the three reads the builder makes before the cell loop:** the manifest (3.688 MiB), the index (0.846) and
  the coverage (0.116), which total **4.650**. `current` is empty.
- **So v0 keeps those three reads until cell 11, and lets them go at cell 12.** The plant keeps every buffer the store hands out, so it
  keeps them to the end.
- **The plant restores v0's cell reads:** cells 1-11 to 0.05, and from cell 12 the only difference is those three reads.
- The planted process's later samples, reported:
  - tile loop's largest 127.55, tiles 127.50, ladder 130.93 MiB;
  - that is 71.0 MiB above v0's after the loops, which is all 60.91 MiB of cells, plus 4.65, plus 5.38 MiB of the tile list.
- **The alternative:** some other 4.65 MiB, held from cell 12 by the plant only. **Why it is unlikely:**
  - the nearest cell reads are 4.45 and 4.86, each about 0.2 away;
  - the three pre-loop reads match to the hundredth.

  The read doesn't exclude it outright.
- **Not established:** why v0 lets the three reads go at cell 12. The variant's one exception sits at the same transition, cell 11. One
  explanation could cover both, and this read doesn't name it.

**My miss, with its cause:** I registered the plant as "keeps every buffer the store hands out", and predicted that it equals v0. I never
checked that v0 holds every such buffer. It holds the reads `readInOrder` hands out, and holds the three earlier reads only until cell 11.
The control was built wider than the claim it tests.

### The ruling (reviewer chat, pasted by Daniel, 2026-11-11 ~05:26Z), verbatim

> The variant: accepted. Three processes, the build's largest 59.99–60.02 MiB in the ladder phase, within 59.90 ± 0.785. Stated as ruled:
> 60.02 MiB (62.9 MB), spread 0.03. The cell-11 exception fell inside the ruled allowance in 4 of 4 processes, matching cell 10's read, the
> one consumed just before, with cells 4 and 7 also matching as recorded. No other cell showed it.
>
> Q-PL1: A. Re-register the plant only, so the store keeps exactly the reads readInOrder hands out, which is what the variant lets go.
> pred7.json's plant trace and the 0.785 bound stay unchanged. Freeze the prediction, every cell within 0.785 of v0's mean, then run three
> fresh planted processes. The variant's three processes stand.
>
> Your miss stays on record: the plant kept more than the claim it tests. Why v0 lets the three pre-loop reads go at cell 12, at the same
> transition as the variant's exception, stays open in the proposal. It isn't needed for any figure.
>
> After A: the proposal stands on the list you gave. Q-DR3 then runs on the variant, as ruled, and making the variant the real builder is its
> own PR.

### The plant, re-registered (Q-PL1, A): registered before it runs

**The one change,** in the driver `mem-diag9.mts`: the planted store keeps **only the reads `readInOrder` hands out.** Those are the cells
(`/cells/`) and the tile list (`/terrain/9/` and `terrain-sources.bin`): what v0's `pending` holds, and what the variant lets go. The
manifest, index, coverage and `current` reads are no longer kept. Nothing else differs from `mem-diag8`.

**What runs:**
- **Three fresh planted processes** (`DENSE_RELEASE=1`, the scratch copy `6281e0f2b00e`), against a fresh results file.
- The runner checks `pred7.json`'s sha256 first.

| row | predicted |
|---|---|
| **PL1** | **every cell within 0.785 MiB of v0's mean** (`pred7.json`'s plant trace, unchanged), in all three processes |
| RV3 | as registered; KJAC `b3f428e5860e`; DR0's ladder |
| the later samples | reported, not graded. **Expected**, not predicted: above v0's by the cells (60.91 MiB) and the tile list read so far, not by the three pre-loop reads |

**What the readings will mean:**
- **PL1 holds:** keeping exactly the reads the variant lets go restores v0. **The variant's cut is those reads,** and the proposal stands on
  the list ruled at ~05:26Z.
- **PL1 off:** stop and report.

**Outside the set:** anything else stops it.

### The plant, re-registered, read (2026-11-11, 05:30:51-05:31:02Z): as predicted

The registration was frozen at 05:30:41Z (`e98fabf180d8`). The runner confirmed `pred7.json`'s sha256. Three planted processes ran, each
exiting 0. The error file is empty, the tree stayed clean, and nothing ran on staging.

| | process 1 | process 2 | process 3 |
|---|---|---|---|
| **PL1: every cell within 0.785 of v0's mean** | **within 0.01** | **within 0.05** | **within 0.06** |
| the cell phase's largest | 99.50 at cell 16 | 99.54 at cell 16 | 99.55 at cell 16 |
| RV3 | ok; KJAC `b3f428e5860e`; DR0's ladder | the same | the same |
| later samples (reported) | tiles 122.81, ladder 126.24 | 122.85, 126.28 | 122.86, 126.28 |

- **The later samples sit 66.34-66.39 MiB above v0's:** the cells (60.91), plus the tile list (5.38), plus the 0.05 offset. The three
  pre-loop reads aren't in them, as expected.
- **By the registered table: PL1 holds.** Keeping exactly the reads the variant lets go restores v0, so **the variant's cut is those reads.**

### The release variant: what the proposal stands on (ruled ~05:26Z)

- **KLGA at r=162,** in Node, settled:
  - **v0: 99.55 MiB (104.4 MB) at cell 16.**
  - **The variant: 60.02 MiB (62.9 MB, at most 64), in the ladder phase.** Three processes, spread 0.03, within the frozen 59.90 ± 0.785.
- **The variant's cut is the consumed reads,** shown by the plant (PL1, three processes).
- **The variant can't go lower than the ladder phase,** because letting reads go doesn't reduce what the layer sets and the ladder hold.
- **The output is unchanged:** KJAC's package hash and KLGA's ladder are the same in v0, the variant and the plant.
- **`READ_AHEAD`:** 6 to 3 saved 1.77 MiB in one process, and no setting can save more than about 1.8 MiB. It is not D4's lever.
- **DF holds:** each cell's sample includes that cell's decoded features, up to 6.62 MiB at cell 16.
- **One consumed read stays reachable at cell 11,** in 4 of 4 variant processes and at no other cell, matching the read consumed just
  before. It is real memory, under the ladder phase.
- **Open, and not needed for any figure (ruled):** why v0 lets the three pre-loop reads go at cell 12, at the same transition as that
  exception.
- **Not shown:** workerd. Q-MEM2's KLGA pass is the only workerd reading.
- **Next, as ruled:**
  1. Q-DR3 on the variant, predictions first;
  2. the variant as the real builder is its own PR and review.

### Q-DR3 on the release variant: registered before it runs

**The rulings it carries out:**
- ~19:07Z Nov 8: "Q-DR3: yes, once memory is settled, stopping at the first build over 96 MB, graded per build."
- ~04:10Z Nov 11, Q-DR3a (a): it runs on the lever the release reading supports, and "grades sizes and memory together, once, on the
  builder that will ship."
- ~05:35Z: "Q-DR3 on the variant, predictions first." Workerd evidence belongs to the variant's own PR, not here.

**What runs:**
- **The rung reading registered on Nov 8:** the same 13 points, the same variants (v0, ab, P1, P2, D2, +sea), the same rows DR0-DR6 in the
  same order, at the map client's query.
- **The builder:** the scratch copy **with the release variant on** (`DENSE_RELEASE=1`, copy `6281e0f2b00e`), at `READ_AHEAD` 6.
- **One change of method:** each build runs **in its own fresh process.**
  - That is the configuration the memory figures were validated in.
  - The Nov 8 driver ran every build in one process, and that process's history made DR0's figures the outlier.
- **The drivers:** `dr-parent.mts` grades; `dr-child.mts` runs one build.
- **In each child:**
  - **A copy build** runs C1, C2′ and C3 first, then samples settled at every hook (each cell, each tile, then tiles, terrain, layers and
    ladder) and once at the end.
  - **The repo's unpatched builder** runs for DR0's bytes only, with no memory measured.
- **Nothing runs on staging.**

**Predicted:**

| row | predicted |
|---|---|
| **DR0** | at the 9 points that fit, **the variant's package is byte-identical to the repo builder's and to Nov 8's** (KBOS `ce5b1ea7589e`, KMIA `000dde33cc42`, KSFO `399cb059f84a`, KSEA `69ff5b17e2ee`, KORD `b54fa953fef7`, KDTW `8976db34bf1a`, KMSP `863f7f2955a7`, KIND `f740fbc3f99e`, KJAC `b3f428e5860e`); at the 4 New York points, **the same ladder** as the repo builder and #620 |
| **DR0, memory anchor** | KLGA under the variant: **59.90 ± 0.785 MiB, in the ladder phase** (the frozen figure) |
| **DR1** | ab at the 9: #620's water share and secondary count, **and Nov 8's package** (KBOS `cd74b62d4cd3`, KMIA `7745a0d61ec6`, KSFO `5803f1a67c63`, KSEA `19c859a5d731`, KORD `15fd35c61882`, KDTW `3ff4efbc56b1`, KMSP `f3f3557eb233`, KIND `deb9172d580e`, KJAC `b3f428e5860e`); **422** at the 4 New York points |
| **DR2** | P1 at the 9: **byte-identical to ab, 9 of 9.** Nov 8 read 3 of 3 before it stopped |
| **DR3** | **P1 fits 0 of 4 New York points,** as Nov 8 read. By its registered meaning: **D2 does the work, and its floor is the decision** |
| **DR4** | P2 at all 13: invariants. **Identical to ab at KMIA and KJAC** (ab's rung ≤ 2); **changed at the 7 where ab dropped secondary roads** (rung 3). New York: not predicted |
| **DR5** | D2, under ab and P1, at the 4 New York points: **the first radius that fits, not predicted.** A 422 at the 40 km floor is a stop (as registered) |
| **DR6** | +sea: P1 and P2 at all 13, then D2 for any still 422. Invariants only |
| **memory, every copy build** | **graded as each build is measured** (ruled). **Over 96 MB: stop.** 64-96 MB: recorded, not a stop. Not predicted per build, apart from the anchor |
| **every process** | C1 +32 ± 1; C2′ +32 ± 1, then within 1; C3 within 1; every sample settled; every object local. Every 422's ladder non-increasing and ending over 32,768; every 200 `kOk` and at most 32,768 non-terrain bytes |

**Expected, not predicted:** the 9 points under v0 well under 64 MB. New York's P1 and P2 builds above v0's, because those variants carry
the extra layer sets at ε×4 and ε×8. Which band those builds land in is what this reads.

**Graded per build, stopping at the first off reading.** A child that crashes or prints no result is an instrument failure, and stops it.

**What the readings will mean:**
- DR3's and DR5's meanings are as registered on Nov 8.
- **A build over 96 MB stops it** (ruled); the proposal then says which build and variant.
- **All rows as predicted:** the rung outcomes and every build's memory band on the variant are what the dense-area proposal states.
- **Anything else stops it.**

**What this cannot see:**
- **Workerd:** one staging request on the variant belongs to its own PR (ruled ~05:35Z).
- **Memory between two samples.**

### Q-DR3 on the release variant, read (2026-11-11, 05:42:00-05:43:04Z): stopped at DR1, KJFK under ab, by my own check

The registration was frozen at 05:41:45Z (`bfc7e30027ae`). The parent's process is recorded in `dr-q3.pid`. Twenty-eight builds ran, each in
its own process, and the parent stopped itself (exit 1). The error file is empty, the tree stayed clean, and nothing ran on staging.

**Everything graded before the stop read as predicted:**
- **DR0, 13 of 13.**
  - At the 9 points that fit, the variant's package is byte-identical to the repo builder's and to Nov 8's.
  - At the 4 New York points, the ladders equal the repo builder's and #620's.
- **The memory anchor:** KLGA under the variant reads **59.96 MiB in the ladder phase**, against 59.90 ± 0.785.
- **DR1, KBOS:** #620's water share and secondary count, and Nov 8's package.
- Every child's controls held, every sample settled, and every object was local.

**The stop:**
- **What tripped:** the parent checks that every 422's ladder is non-increasing and ends over 32,768. **ab's 422 at KJFK reports no ladder**
  (an empty list), and an empty list fails the "ends over the cap" half.
- **This is not a reading:** only v0's 422 reports a ladder here. Nov 8's log shows the same, with "DR1-ab KJFK 422 r 162.0 km" and no
  ladder.
- **My miss, with its cause:** I registered "every 422's ladder non-increasing and ending over 32,768" for every variant. The Nov 8 log,
  which I read while writing these predictions, showed that the variants' 422s report none. The check was written for a population it
  didn't describe.

**Memory, every build graded as measured (none over 96 MB):**

| build | MiB | MB | band |
|---|---|---|---|
| DR0-copy (the variant at v0), the 9 that fit | 38.85-56.65 | 40.7-59.4 | at most 64 |
| DR0-copy KLGA / KEWR / Midtown | 59.96 / 60.76 / 58.08, in the ladder phase | 62.9 / 63.7 / 60.9 | at most 64 |
| **DR0-copy KJFK** | **61.58, in the cell phase** | **64.6** | **64-96: recorded** |
| **DR1-ab KBOS** | **68.51, ladder** | **71.8** | **64-96: recorded** |
| **DR1-ab KJFK** | **71.46, ladder** | **74.9** | **64-96: recorded** |

- **ab builds read higher than v0's** at the same point: KBOS 56.65 → 68.51, KJFK 61.58 → 71.46.
- **KJFK under v0 peaks in the cell phase,** not the ladder phase, unlike the other three New York points.
- **Neither was predicted,** and both are recorded as read.

**Nothing is graded past the stop.** The proposal that follows goes to the reviewer before anything resumes.

### The ruling (reviewer chat, pasted by Daniel, 2026-11-11 ~05:47Z), verbatim

> Q-DR3b: A. Re-register the ladder check so it applies only where a ladder is reported: v0's 422s, non-increasing and ending over
> 32,768. A variant's 422 is predicted to report none, so one that does is off. Everything else stays as frozen. Rerun every row from the
> start, one run graded once, as ruled. Your miss stays on record.
>
> The memory readings stand as read, with nothing over 96 MB. ab reading 10–12 MiB above v0 at KBOS and KJFK wasn't predicted. The rerun
> will read it again. In the proposal, state which phase ab's extra sits in, and give one sentence on why the A+B water ladder costs memory
> there.

### Q-DR3, re-registered (Q-DR3b, A): registered before it runs

**What changes from the registration at `bfc7e30027ae`.** The drivers are now `dr-parent2.mts` and `dr-child2.mts`, and nothing else
differs:
1. **The ladder check applies only where a ladder is reported:**
   - **v0's 422** must report a ladder that is non-increasing and ends over 32,768;
   - **a variant's 422** (ab, P1, P2, at any radius, with or without +sea) is **predicted to report none.** One that does is off, and
     stops the run.
2. **Each copy build also prints its largest sample per phase** (cell, tile, tiles, terrain, layers, ladder, end). At the end, **ab minus v0
   per phase** is printed for each point. Both are **reported, not graded,** for the proposal's statement of where ab's extra sits.

**Unchanged:** every row, prediction, bound, stop and order of the registration at `bfc7e30027ae`, including:
- the memory anchor;
- the 96 MB stop;
- the fresh process per build.

The run is fresh and starts from DR0, one run graded once (ruled).

**What the code already says about ab's extra, for the proposal's sentence:**
- **For every variant other than v0, ab included, the scratch copy builds two extra layer sets in the cell loop, and both sets'
  contents at the layer-set step:**
  - **L8:** water at ε×8, which A+B's water rung uses;
  - **L4:** rivers and roads at ε×4, which only P1 and P2 use.
- So part of ab's extra may belong to the copy, not to A+B.
- **The per-phase figures will show where the extra sits.** Separating L8's share from L4's would need a variant this registration doesn't
  have.

### The ruling (reviewer chat, pasted by Daniel, 2026-11-11 ~05:53Z), verbatim

> Pre-ruled, so the result doesn't need a round trip. If the per-phase figures put ab's extra in the cell loop's coarse sets, the ε×4 rivers
> and roads that only P1 and P2 use, the proposal states ab's figure as an upper bound, with the copy's share named. A follow-up
> registration then measures ab with the copy building only the sets ab uses, predictions first, local. If the extra sits elsewhere, report
> it as read.

### Q-DR3, re-registered, read (2026-11-11, 05:51:36-05:56:22Z): every graded row as predicted; no build over 96 MB

The registration was frozen at 05:51:29Z (`d5bc0061e734`). The parent's process is recorded in `dr-q3b.pid`. **111 builds** ran, each in its
own process, and the parent exited 0. The error file is empty, the tree stayed clean, and nothing ran on staging.

| row | read |
|---|---|
| DR0 | **13 of 13.** The variant's packages are byte-identical to the repo builder's and to Nov 8's at the 9; #620's ladders at the 4. **Memory anchor: KLGA 59.98 MiB in the ladder phase** |
| DR1 | **13 of 13.** #620's water share and secondary count, and Nov 8's package, at the 9; 422 at the 4 |
| DR2 | **9 of 9:** P1 byte-identical to ab |
| **DR3** | **P1 fits 0 of 4 New York points, as predicted. By its registered meaning: D2 does the work, and its floor is the decision** |
| DR4 | P2 identical to ab at KMIA and KJAC, changed at the 7, 422 at the 4 |
| **DR5** | **D2's first fit, at all 4 New York points: 103.7 km under ab, 129.6 km under P1.** No 422 at the floor |
| DR6 | +sea: P1 and P2 fit at 162 km at the 9 and 422 at the 4. **D2's first fit: 129.6 km at all 4, under both.** Every package `kOk`, at most 32,768 |
| ladders | every v0 422 non-increasing and over the cap; **no variant's 422 reported a ladder** (the code sets `ladder.nt = []` for them) |
| controls | every child: C1, C2′ and C3 held, every sample settled, every object local |

**D2's fitted builds, the ones a New York scope would get:**

| | KJFK | KLGA | KEWR | Midtown |
|---|---|---|---|---|
| ab at 103.7 km | 54.0 MB, water 13.0%, 17 secondary | 55.1 MB, water 12.7%, 1 secondary | 58.8 MB, water 9.6%, 0 secondary | 55.9 MB, water 11.3%, 0 secondary |
| P1 at 129.6 km | 60.1 MB, water 10.8%, 0 secondary | 61.6 MB, water 10.4%, 0 secondary | **66.2 MB** (64-96), water 8.3%, 0 secondary | 61.8 MB, water 9.9%, 0 secondary |

**Memory: no build over 96 MB.** The largest per variant:

| variant | largest | where |
|---|---|---|
| v0 | 61.58 MiB, 64.6 MB | KJFK, in the cell phase |
| ab | 73.67 MiB, 77.3 MB | KEWR |
| P1 / P2 | 73.66 / 73.69 MiB | KEWR |
| P1+sea / P2+sea | 73.84 / 73.85 MiB, 77.4 MB | KEWR |

**30 builds fell in 64-96 MB,** recorded and not a stop:
- 20: every New York build at 162 km under ab, P1 and P2, with or without +sea;
- 5: KBOS under the same five;
- 4: KEWR at 129.6 km under ab (its 422 try), P1, P1+sea and P2+sea;
- 1: KJFK under v0.

**Where ab's extra sits (ab minus v0, per phase, MiB):**

| point | cell | tile loop, tiles, terrain | layers | ladder | end |
|---|---|---|---|---|---|
| KJFK | 9.71 | 12.31 | 12.92 | 12.95 | −0.01 |
| KLGA | 8.15 | 13.00-13.02 | 13.63 | 13.65 | −0.01 |
| KEWR | 8.23 | 12.22 | 12.88 | 12.87 | 0.00 |
| Midtown | 9.67 | 12.06 | 12.71 | 12.71 | 0.01 |
| KBOS | 8.96 | 11.28 | 11.90 | 11.85 | −0.02 |
| the other 8 | 0.98-6.42 | 1.47-6.76 | 1.56-7.18 | 1.62-7.14 | −0.03 to 0.02 |

- **It sits in the layer sets the cell loop builds.**
  - It grows during the cell loop and holds flat through the tile loop, the tiles and the terrain, where no layer set is touched.
  - It grows about 0.6 MiB at the layer-set step, where their contents are made.
  - It is gone at the end.
- **ab, P1 and P2 read the same at each point, to 0.05 MiB,** though only P1 and P2 use the ε×4 set. The copy builds both coarse sets, L4 and
  L8, for every variant but v0, so this read can't say how the extra splits between them.
- **By the pre-ruled branch (~05:53Z): ab's figure is an upper bound,** and the copy's share is named: L4, rivers and roads at ε×4, which ab
  never reads.
  - `budget.ts` calls `make(tol4)` only inside `at4()`, which runs for P1 and P2 alone.
  - The follow-up below measures ab with the copy building only the sets ab uses.

**The one sentence (ruled):** A+B's water rung needs the water simplified at ε×8, and the builder makes it in its single pass over the
cells, so a coarser water set is held beside the ε and ε×2 sets from the cell loop to the end of the build. The scratch copy also holds
an ε×4 rivers-and-roads set for every variant, which A+B never reads, so the ab figures above are upper bounds.

### ab with only the sets it uses (pre-ruled ~05:53Z): registered before it runs

**What changes, in the scratch copy only:**
- **A knob, `DENSE_SETS=used`.** With it set, the ε×4 rivers-and-roads set (L4) is built only for P1 and P2. ab then builds L8, the ε×8
  water its rung uses, and nothing it doesn't read.
- **Unset, the code behaves exactly as before:** every variant but v0 builds both coarse sets.
- **The builder exports `SETS_USED`,** so each child reports, from the builder module itself, whether the knob took.
- **Both edits are byte-checked** against a kept copy (`dn-build.ts.before-sets`). They are three lines.

**What runs:**
- **Fourteen local processes, in the drivers `dr-used.mts` and `dr-child3.mts`,** each build in its own process, with the release on and
  `READ_AHEAD` 6:
  1. **AU0,** ab at KLGA without the knob;
  2. then **ab with the knob** at all 13 points, at 162 km.
- **The references** are Q-DR3's read above, frozen in the driver.
- **Nothing runs on staging.**

| row | predicted |
|---|---|
| **AU0** (the control that can fail) | without the knob, the edited copy reproduces Q-DR3's ab at KLGA: **73.64 ± 0.785 MiB, in the ladder phase, 422.** If the edit had changed the default path, this would move |
| **AU1** | with the knob: **ab's packages unchanged.** Nov 8's ab hashes at the 9, and 422 with no ladder at the 4. `budget.ts` reads the ε×4 content only in `at4()`, which runs for P1 and P2 alone |
| **AU2** | each point's figure **between v0's and ab-with-every-set's** from Q-DR3, ± 0.785 |
| **AU3** (graded both ways, not a stop) | at the 4 New York points, **the copy's ε×4 set is the larger part of ab's extra.** The ε×4 set holds every river and road, most of New York's features, while A+B's ε×8 set holds water only |
| **every process** | the knob as asked (read from the builder); C1, C2′ and C3; settled; objects local; **no build over 96 MB**, graded per build; every 200 `kOk` and at most 32,768 |

**Where the bounds come from:** 0.785 is the bound measured for this configuration: twice the largest spread between processes at one
sample (0.392).

**What the readings will mean:**
- **As predicted:** ab's figures with the knob are A+B's own memory. They replace the upper bounds in the proposal, and **the split of ab's
  extra into the copy's ε×4 set and A+B's own ε×8 water** is stated per point.
- **Anything else:** stop and report.

**What this cannot see:**
- **Workerd.**
- **P1 and P2 are unchanged by the knob.** Their figures stand as read, since they do use the ε×4 set.

### ab with only the sets it uses, read (2026-11-11, 06:01:57-06:02:33Z): every graded row as predicted

The registration was frozen at 06:01:50Z (`3708d62428c0`). The parent's process is recorded in `dr-used.pid`. Fourteen builds ran, each in
its own process, and the parent exited 0. The error file is empty, the tree stayed clean, and nothing ran on staging.

- **AU0, the control:** without the knob, ab at KLGA read **73.56 MiB in the ladder phase, 422,** against Q-DR3's 73.64. The default path
  is the one measured.
- **AU1:** with the knob, **ab's packages are unchanged.** Nov 8's ab hashes at the 9; 422 with no ladder at the 4.
- **AU2:** every point is between v0's figure and ab-with-every-set's.
- **Every process:** the builder reported the knob as asked; the controls held; every sample settled; every object was local; no build was
  over 96 MB.

**ab's figures, A+B's own memory, replacing the upper bounds (MiB, MB):**

| point | v0 | **ab, only its sets** | ab, every set (Q-DR3) |
|---|---|---|---|
| KJFK | 61.58 (64.6) | **61.72 (64.7), in the cell phase** | 71.46 (74.9) |
| KLGA | 59.98 (62.9) | **60.12 (63.0)** | 73.64 (77.2) |
| KEWR | 60.80 (63.8) | **60.93 (63.9)** | 73.67 (77.3) |
| Midtown | 58.06 (60.9) | **58.22 (61.0)** | 70.76 (74.2) |
| KBOS | 56.63 (59.4) | **56.79 (59.5)** | 68.48 (71.8) |
| the other 8 | 38.72-49.12 | **38.74-49.28** | 40.36-52.55 |

**The split of ab's extra at the ladder phase** (AU3, graded both ways: the copy's set is the larger part at all 4, as expected):

| point | ab's extra | the copy's ε×4 set | **A+B's own (ε×8 water)** |
|---|---|---|---|
| KJFK | 12.95 | 12.84 | **0.11** |
| KLGA | 13.66 | 13.52 | **0.14** |
| KEWR | 12.87 | 12.74 | **0.13** |
| Midtown | 12.70 | 12.54 | **0.16** |
| KBOS | 11.85 | 11.69 | **0.16** |
| the other 8 | 1.62-7.14 | 1.61-7.09 | **0.01-0.10** |

**For the proposal:**
- **A+B costs 0.01-0.16 MiB over v0 at these 13 points.** The 10-14 MiB that Q-DR3 read under ab was the scratch copy's ε×4 set.
- **P1 and P2 do use that set,** so their figures stand as read: up to 73.69 MiB (77.3 MB) at 162 km, and 60.1-66.2 MB at D2's 129.6 km.
- **D2's ab builds at 103.7 km (54.0-58.8 MB) were read with every set,** so they are upper bounds too. With only its sets, ab's figures
  are within about 0.2 MiB of v0's.
- **KJFK's 64.7 MB sits in the cell phase under both v0 and ab.** It is not A+B's.

## The proposal: what a New York scope gets

### The ruling (reviewer chat, pasted by Daniel, 2026-11-11 ~16:52Z), verbatim

> Q-DP1: yes. Write the dense-area proposal into #622, stating the figures as ruled, including ab as an upper bound if the copy's coarse
> sets carry its extra. Then ask for #622 to join the merge list. I'll review the proposal from git before approving the merge.

### What it stands on

- **The query is the map client's:** r=162, ε 201,168 mm, no peaks, terrain levels at 41.2, 81.8 and 162.3 km.
- **The release variant, as ruled (~05:26Z):** KLGA at r=162 reads **60.02 MiB (62.9 MB), spread 0.03,** in the ladder phase, against
  v0's 99.55 MiB (104.4 MB). **Every figure below is on the variant.** The rest of [its list](#the-release-variant-what-the-proposal-stands-on-ruled-0526z)
  stands as ruled.
- **`READ_AHEAD`, as ruled (~04:39Z):** 6 to 3 saved 1.77 MiB at KLGA, in one process. No setting can read below the loop's end, so the
  most it can save is about 1.8 MiB. It is not D4's lever.
- **Q-DR3, re-registered, read** (05:51:36-05:56:22Z, 111 builds): the rungs, and each build's memory.
- **ab with only the sets it uses, read** (06:01:57-06:02:33Z, 14 builds): A+B's own memory.
- **All of it ran locally, in Node.** Nothing here ran on staging.

### The two options

**D2 does the work in either.** DR3 read P1 fitting none of the 4 New York points at 162 km, and its registered meaning is that D2 does
the work and its floor is the decision. What is left to choose is what runs before D2:
- **ab, then D2:** A+B's ladder (ε×2; water at ε×8; secondary roads shortest first; water smallest first), then the same ladder on a
  package of radius r × 0.8, × 0.64, … until one fits. A package's lines and rings fill the square [−R, R]² (format §2).
- **P1, then D2:** the same, with one more rung before D2: rivers and roads at ε×4 (805 m), then A+B's two drops again. **This is
  Q-DN2's direction as ruled** ("D1's ε rung, then D2").

**Outside New York the two give the same packages** (DR2: P1 byte-identical to ab, 9 of 9). There they differ only in memory.

**What a New York scope gets** (KJFK, KLGA, KEWR and Midtown, all four alike unless named):

| | **ab, then D2** | **P1, then D2** |
|---|---|---|
| water, rivers and roads: a square of half-side | **103.68 km**, corners at 146.63 km | **129.6 km**, corners at 183.28 km, past the view |
| places (kept within R of the origin, format §5.5) | to 103.68 km | to 129.6 km |
| of the widest view (100 mi, 160.93 km) | **52.8%** of its area | **80.0%** of its area |
| the 25 mi and 50 mi views (40.23 and 80.47 km) | covered whole | covered whole |
| rivers and roads | ε×2, 402 m: **1.2 px** at the closest view, 0.6 px at the middle | ε×4, 805 m: **2.4 px** at the closest view, 1.2 px at the middle |
| water, at ε×8 (1.6 km), as a share of the disc of radius R (#620's raster) | 9.6-13.0% | 8.3-10.8% |
| secondary roads kept | KJFK 17, KLGA 1, KEWR 0, Midtown 0 | 0 |
| primary roads, rivers, places, `PROVENANCE` | whole | whole |
| terrain | the three requested levels, squares of half-side 41.24, 81.81 and 162.28 km: the widest view whole | the same |
| with the sea mask | **not read** (below) | 129.6 km; water 69.4-79.3% |
| builds for the first request | 3: 162 and 129.6 km (each a 422), then 103.7 km | 2: 162 km (a 422), then 129.6 km |
| those builds' wall in Node, summed (local reads, not R2) | 7.3-8.4 s | 5.8-6.7 s |

- **The views and the pixel figures.** A scope set to 100 mi has the steps 25, 50 and 100 mi (`include/MapQuery.h`): 40.2336 km over
  120 px (0.335 km a pixel), 80.4672 km (0.671) and 160.9344 km (1.341). ε is the widest view over 800, so ε×2 is exactly 1.2 px at the
  closest view and ε×4 exactly 2.4.
- **The coverage figures** take the view centred on the package's origin. The device's own position is up to 0.785 km away (the
  2-dp origin), which the query's +0.8 km margin covers.
- **Terrain keeps its levels.** In the builds read here, D2 shrinks only the vector square; the terrain levels keep the radii the query
  asked for, and terrain is outside the cap. So beyond the square, the package still carries terrain, and no water, roads or places.
- **The device needs nothing new.** It places the package by the radius its header names (F4, `include/MapLayer.h`), and once a package is
  held it asks nothing more for that config (`include/MapPolicy.h`, F8).
- **The extra builds are paid once per query.** A1 stores the answer, so a repeat builds nothing.

### Each build's peak against the isolate's 128 MB (ruled ~19:07Z, Nov 8)

**The instrument:** Node, `heapUsed + arrayBuffers` after `gc()`, settled, each build in its own process, on the release variant. **MB is
10^6 bytes, and 128 MB is read in the same unit.** A figure marked ≤ is an upper bound ([next section](#abs-figures-as-ruled)).

| point | ab, then D2 | P1, then D2 | P1 with the sea mask, then D2 |
|---|---|---|---|
| KJFK | 162 km: 64.7 · 129.6 km: ≤ 60.1 · 103.7 km: ≤ 54.0 | 162 km: 74.9 · 129.6 km: 60.1 | 75.1 · 60.3 |
| KLGA | 63.0 · ≤ 61.6 · ≤ 55.1 | 77.2 · 61.6 | 77.4 · 61.7 |
| KEWR | 63.9 · ≤ 66.2 · ≤ 58.8 | 77.2 · 66.2 | 77.4 · 66.3 |
| Midtown | 61.0 · ≤ 61.7 · ≤ 55.9 | 74.2 · 61.8 | 74.4 · 61.9 |
| KBOS | 59.5 | 71.8 | 72.0 |
| the other 8 | 40.6-51.7 | 42.3-55.1 | 42.5-55.4 |
| **the largest** | **64.7 measured (51% of 128); ≤ 66.2 with the bound (52%)** | **77.2 (60%)** | **77.4 (60%)** |
| builds in 64-96 MB | 1 of 21 measured (KJFK at 162 km), and KEWR's 129.6 km try, whose bound is 66.2 | 6 of 17: KBOS, the 4 at 162 km, KEWR at 129.6 km | the same 6 of 17 |
| over 96 MB | none | none | none |

**What it is a figure of:** a floor on the build's live memory, not workerd's. Garbage made between two samples, and the Worker's own
bundle, are not counted. The only workerd reading is Q-MEM2's: KLGA at 162 km under v0, before the variant, built under 128 MB, with the
margin unknown. **The variant's own PR brings one KLGA request on staging** (ruled ~05:35Z).

### ab's figures, as ruled

- **Q-DR3 read ab with every coarse set the scratch copy builds,** including the ε×4 rivers-and-roads set that only P1 and P2 read:
  68.48-73.67 MiB (71.8-77.3 MB) at New York and KBOS, at 162 km. **Those figures are upper bounds on ab.**
- **The copy's share, named:** that ε×4 set carried **11.69-13.52 MiB of ab's 11.85-13.66 MiB extra** over v0 at those five points, and
  1.61-7.09 of 1.62-7.14 MiB at the other 8.
- **ab's own, measured with only its sets:** A+B costs **0.01-0.16 MiB over v0** at all 13 points, with its packages unchanged. Those are
  the 162 km figures in the table above.
- **D2's ab builds at 129.6 and 103.7 km were read with every set only,** so they stay upper bounds (≤ above).
- **Where the extra sits (ruled ~05:47Z):** in the layer sets the cell loop builds. It grows through the cell loop, holds flat through the
  tiles and the terrain, grows about 0.6 MiB at the layer-set step, and is gone at the end.
- **The one sentence:** A+B's water rung needs the water simplified at ε×8, which the builder makes in its single pass over the cells and
  holds beside the ε and ε×2 sets until the build ends; that set is water only, so it costs 0.01-0.16 MiB.

### What these readings cannot see

- **Workerd** (above).
- **ab with the sea mask, at New York.** DR6 read P1 and P2 with it, not ab.
  - **Outside New York it is read through P1.** P1 reaches its ε×4 rung only when A+B fails (`budget.ts`), and P1 with the sea fitted at
    rung 1-3 at all 9. So its packages there are A+B's with the sea.
  - **At New York I expect it to fit first at 103.7 km, as ab does.** Once every secondary road and water ring is dropped, the two hold the
    same layers, except that `PROVENANCE`'s drop counts are text and can differ by a few bytes. That margin is not read, and neither is how
    much water it keeps.
  - **The code PR reads it, predictions first.** If it doesn't fit at 103.7 km, the comparison above changes.
- **Places denser than New York.** The 13 points are the registered set. D2's floor (Q-DP3) is what covers the rest.
- **Two things stay open, and neither is needed for any figure:**
  - why v0 lets the three pre-loop reads go at cell 12 (ruled open);
  - why KJFK alone peaks in the cell phase, under v0 (64.6 MB) and ab (64.7 MB) alike. It is v0's, not A+B's.

### My recommendation: ab, then D2

1. **The two differ only on the widest view, and P1 pays everywhere.**
   - Both cover the 25 mi and 50 mi views whole.
   - P1 buys the band between the 103.68 km square and the 129.6 km square on the 100 mi view: 80.0% of that view covered, against
     52.8%.
   - It pays with rivers and roads at 805 m across the whole square, including the two views both options cover: 2.4 px at the closest
     view, against ab's 1.2.
2. **Memory is the ruled risk for D4** (~19:07Z, Nov 8).
   - The builder makes every layer set in its one pass over the cells, before it knows whether A+B will fail, so every P1 build holds the
     ε×4 set. Making it only after a failure would take a second pass over the reads.
   - That costs 1.6-13.5 MiB at all 13 points, including the 9 where P1's package is ab's.
   - P1's largest build is 77.2 MB, with 6 of its 17 in 64-96 MB. ab's largest measured is 64.7 MB, KJFK's own v0 peak, and its one bound
     is 66.2.
3. **ab's cost is one more build, for New York only, once per query:** 3 against 2, 1.2-1.7 s more in Node.

**It departs from Q-DN2 as ruled.** That ruling came before any reading at r=162. The ε rung was meant to fit New York before the package
shrinks. At the client's query it fits none of the 4 (DR3), so D2 runs either way, and the rung only moves where D2 stops.

**What would change it:**
- if the widest view's coverage is worth more than sharpness at the two closer views: then P1, then D2, with the memory above;
- if the code PR reads ab with the sea mask not fitting at 103.7 km at New York.

### D2's floor

- **The readings never came near it:** the first fit was 103.7 or 129.6 km at all 4 New York points.
- **D2's steps from 162 km** are 129.6, 103.7, 82.9, 66.4, 53.1 and 42.5 km. The next, 34.0 km, is under the registered 40 km floor.
- **A 42.47 km square still covers the closest view (25 mi, 40.23 km).** A smaller one couldn't fill even that view.
- **Past the last step, §6's 422 stays,** and the client falls back to no package. It asks nothing more until the location or radius
  changes (`include/MapPolicy.h`, CA6).
- The last step is 6.9% of the requested square's area. No point read comes near it.

### The order

1. **This proposal,** for Q-DP2 to Q-DP4.
2. **The release variant as the real builder:** its own PR and review, with its one KLGA request on staging (ruled ~05:26Z and ~05:35Z).
   Every figure here stands on it.
3. **The code PR (Q-W4):** A+B, the chosen rung, D2 and the sea mask, in one store change, with the format's §6 changed. Predictions first,
   including the chosen option with the sea mask at New York, and each build's memory.
4. **Then D4's production plan.**

### Questions

- **Q-DP2:** ab, then D2 (recommended), in place of Q-DN2's ε rung; or P1, then D2, as Q-DN2 ruled?
  - **The owner decided B, 2026-11-11: P1, then D2** (relayed by the orchestrator, ~18:55Z).
- **Q-DP3:** D2's steps from 162 km by × 0.8, the last at 42.5 km (the finest view), then §6's 422 as now?
- **Q-DP4:** the order above: the release variant's PR, then the Q-W4 code PR, then D4's plan?

**CI for this push (docs-only), predicted: 32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.** The
baseline is this PR's own run at `31bb7973883d` (32 = 29/3/0). No workflow change has landed on main since: the last is `e2c34daf` (#633),
merged before that run.

### Corrections: the packages are squares, not discs (2026-11-11, from the orchestrator ~18:35Z)

**What was wrong.**
- **The proposal described each package as a disc of radius R.** The format clips lines and rings to the square [−R, R]²
  (`docs/basemap-package-format.md` §2: "Geometry is clipped to the square before quantising"; its order of operations, step 2). Only
  points keep the disc: places (§5.5) and peaks (§5.4), and the rivers' length selection (§5.2).
- **It used views of 41, 82 and 162 km.** The views are the scope's 25, 50 and 100 mi steps (`include/MapQuery.h`): 40.2336, 80.4672 and
  160.9344 km. 41.24, 81.81 and 162.28 km are the terrain levels' radii, and 162 km is the build radius r.

**What changed above, in the proposal section only.** Each original is kept here:

| was | now |
|---|---|
| "the same ladder on a disc of r × 0.8, × 0.64, …" | "on a package of radius r × 0.8, × 0.64, …", with the square noted |
| "water, rivers, roads and places, out to **103.7 km** / **129.6 km**" | water, rivers and roads to a square of half-side 103.68 km (corners 146.63 km) / 129.6 km (corners 183.28 km, past the view); places to R |
| "of the widest view (162 km): 64% of the radius, 41% of the area / 80% of the radius, 64% of the area" | of the 100 mi view: **52.8%** / **80.0%** of its area |
| "the 41 km and 82 km views" | the 25 mi and 50 mi views (40.23 and 80.47 km), covered whole by both |
| "ε×4, 805 m: **2.3 px** at the finest view" | 2.4 px at the closest view |
| "the finest view is 41 km over 120 px (0.34 km a pixel), the middle 82 km (0.68), the widest 162 km (1.35)" | 40.2336 km (0.335), 80.4672 km (0.671), 160.9344 km (1.341) |
| "terrain: the three requested levels, out to 162 km" | squares of half-side 41.24, 81.81 and 162.28 km: the widest view whole |
| "D2 shrinks only the vector disc"; "beyond the disc" | the vector square; beyond the square |
| "It draws the disc the package's header names" | it places the package by the radius its header names |
| "P1 buys the ring from 103.7 to 129.6 km on the 162 km view" | the band between the two squares: 80.0% of the 100 mi view against 52.8% |
| "across the whole disc … 2.3 px at the finest view" | across the whole square … 2.4 px at the closest view |
| "before the disc shrinks" | before the package shrinks |
| "42.5 km still covers the finest view (41.2 km). A smaller disc couldn't fill even the closest view." | a 42.47 km square still covers the closest view (25 mi, 40.23 km); a smaller one couldn't fill even that view |
| "about 7% of the requested disc's area" | 6.9% of the requested square's area |

The water-share row now names its measure: #620's raster counts the disc of radius R inside the package's square.

**Derived from the format, not from the renders:**
- **A's square lies inside the view:** its corner, 103.68 × √2 = 146.63 km, is under 160.93. Its share is (2 × 103.68)² / (π × 160.9344²)
  = **52.84%**.
- **B's corners lie past the view** (183.28 km). Its share is the view's disc less the four circular segments beyond the square's edges:
  1 − (4/π)(acos t − t√(1 − t²)), with t = 129.6 / 160.9344, = **79.99%**.
- **ε is the widest view over 800** (`include/MapQuery.h`), so ε×2 is 1.2, 0.6 and 0.3 px at the 25, 50 and 100 mi views, and ε×4 is
  2.4, 1.2 and 0.6 px.
- The view is taken as centred on the package's origin. The device's own position is up to 0.785 km away.

**What agrees.** These are map-client's, in its scratch folder, and nothing is copied here. They are two different things:
- **The geometry, `reach.log`** (sha256 `02979c1bb4df…`): computed in Python from the two packages' header radii and the device's 100 mi
  view. It reads 52.8% and 80.0%. A disc reading at this view would give 41.5% and 64.9%.
- **The observation, `square_check.log`** (sha256 `f36c449e3c66…`): renders from the device's own `maplayer::Build` at main
  `e0941a90c4e4`, at its own views, centred on the package's origin.
  - In all four renders, no vector pixel lies outside |dx|, |dy| ≤ r_px + 1.
  - A's farthest pixel at the 100 mi view is 108.90 px out, at (+77.5, −76.5): its square's corner. The format puts that corner at 109.33 px,
    at ±77.31 px on each axis.

**Kept as written, not corrected:**
- the earlier record's figures that used 41 and 162 km, such as the Nov 8 registration's "2.3 px at the finest view (41 km over 120 px)";
- #622's description;
- **Q-DP3 as asked and ruled** ("the last at 42.5 km (the finest view)"). Its meaning is unchanged, since a 42.47 km square still covers the
  25 mi view.

**Unchanged:** every reading and memory figure, and the recommendation's other reasons. Q-DP2 is now the owner's call, made from the
renders (ruled ~18:20Z).
