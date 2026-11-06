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
