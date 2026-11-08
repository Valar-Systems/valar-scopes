# What a scope in a dense area gets (a proposal, predictions first)

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
