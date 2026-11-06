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
