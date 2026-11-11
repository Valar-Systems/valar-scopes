# Q-WATER: the dense-area packages' water rings (a read before anything is fixed)

**At the 25 mi view, New York's harbour draws as jagged wedges.** Map-client traced it to package A's water rings, not the device's fill.
This record separates the cause. Nothing is fixed here, and the builder doesn't change.

## The ruling (from the orchestrator, 2026-11-11 ~18:43Z), verbatim (its read)

> The separating read: local, read-only, predictions frozen first, no change to the builder.
> - At Midtown, build the water two ways from the same source: (i) as package A carries it (the ε×8 rung), and (ii) at the base ε with
>   no water rung.
> - For each, print the ring count, the vertex count per ring, and the self-intersection count, with map-client's dump as the anchor for
>   (i).
> - Also read one point where the ladder does NOT reach the water rung (KJAC, say).
> - What each result means, decided now:
>   1. (ii) has many more vertices and no crossings, while (i) matches map-client's counts: the ε×8 water rung causes both the coarseness
>      and the crossings. It goes to the owner as a property of the dense-area design, with your options (a topology-preserving
>      simplifier, a different water rung, or accept).
>   2. (ii) also crosses: the defect is upstream of the ladder (the source rings, or the clip). Find which, read-only.
>   3. (i) doesn't match map-client's counts: the instrument or the package differs. Stop and report.
>   4. Anything else: stop and report.

## What map-client read (its outcome 2)

Package A is the D5 render package: Midtown, ab then D2, 103.68 km, sha256 `fe69e900c622…`.
- **The device's fill is cleared:** `FillRing` matched an independent even-odd fill pixel for pixel at both views.
- **The package carries 88 water rings, 481 vertices in all,** at most 18 per ring. 17 rings cross themselves, with 28 proper crossings.
- **At the 25 mi view, 44 rings meet the frame.** 10 of them cross themselves; the other 34 are valid but coarse, mostly 3 to 9 vertices.

## What the builder does with water (from the code, the scratch copy Q-DR3 read)

- **Per ring** (`layers.ts`, `LayerSet.add`, "water"): project to the frame; clip to the square (Sutherland–Hodgman); simplify with
  Douglas–Peucker over the closed path (`frame.ts`, `simplifyRing`); quantise; drop consecutive duplicates. **Nothing checks that the
  result is still a simple ring.**
- **A+B** (`budget.ts`): from rung 2 on, the water is replaced by a set simplified at **ε×8, 1,609,344 mm**, about 4.8 px at the 25 mi view.
  A fitted at rung 4, so some of its smallest rings were then dropped.
- **The header's `toleranceMm`** is the rung's rivers-and-roads tolerance: 402,336 in A. The water's ε×8 appears nowhere in the header.

## The read, registered before it runs

- **Local and read-only.** The script is `qwater.mts` (scratch, sha256 `a4abd0a97573`). It reads the store copy `4c0875738f38` through the
  scratch copy's own modules, the same cell loop as `build.ts`. Nothing runs on staging.
- **The counts are map-client's `ring_stats()`, ported exactly:**
  - exact integer orientation in (qx, qy);
  - a crossing is proper when o1·o2 < 0 and o3·o4 < 0, over edge pairs (i, j) with j ≥ i+2, excluding (0, n−1);
  - a contact is any zero orientation with the point on the segment;
  - a spike is a vertex whose next edge folds straight back along its previous one.

| row | what | predicted |
|---|---|---|
| **QW0** (the anchor) | (i): package A's water as it carries it, decoded from the file | **exactly map-client's:** 88 rings, 481 vertices; per ring 3:28, 4:18, 5:9, 6:10, 7:6, 8:6, 9:5, 10:1, 13:1, 14:2, 16:1, 18:1; winding + 87, − 1; **28 proper crossings in 17 rings** (0-based: 6:2, 13:2, 22:2, 24:1, 28:1, 30:1, 36:1, 38:2, 43:1, 53:1, 55:1, 64:2, 66:4, 70:1, 80:3, 82:1, 87:2); 0 contacts, 0 spikes |
| **QW1** | (i′): the ε×8 set, before A+B's drops, from the same cells at Midtown and 103.68 km | **A's 88 rings are its survivors, in source order:** an exact subsequence. Its ring count less 88 equals the drop count in A's `PROVENANCE`. Its proper crossings are 28 or more |
| **QW2** | (ii): the base-ε set (201,168 mm), the same cells, no water rung | **more vertices than (i′), and 0 proper crossings** (outcome 1) |
| QW2′ | the ε×2 set, the same cells | reported, not predicted |
| **QW3** | KJAC as it carries its water: ab at r=162, where the ladder stops at rung 1 (ε×2) | its package is Nov 8's ab KJAC, `b3f428e5860e`. The counts are reported, not predicted |
| **every row** | | no object missing from the local copy |

**My confidence in QW2's zero is not high.** Douglas–Peucker can cross a ring at any tolerance, not only at ε×8. That is what the read
decides.

**What the readings mean** (the ruling's table):

| outcome | meaning | next |
|---|---|---|
| **1:** QW0 holds; (ii) has many more vertices and no crossings | the ε×8 water rung causes both the coarseness and the crossings | to the owner, as a property of the dense-area design, with the options: a topology-preserving simplifier, a different water rung, or accept |
| **2:** (ii) also crosses | the defect is upstream of the ladder: the source rings, or the clip | find which, read-only, registered first |
| **3:** QW0 doesn't match | the instrument or the package differs | **stop and report** |
| **4:** anything else, including QW1 not being a subsequence, KJAC's hash differing, or an object missing | | **stop and report** |

## The read (2026-11-11, 18:46:30-18:46:31Z): outcome 2. The crossings are upstream of the ladder

The registration was pushed at 18:46:16Z (`f5803ee4882e`). The script's sha256 read as registered. Nothing was missing from the local
copy, and nothing ran on staging. The log is scratch (sha256 `c72e032507c8`).

| row | read | |
|---|---|---|
| **QW0** | 88 rings, 481 vertices; the same per-ring counts; winding + 87, − 1; **28 proper crossings in the same 17 rings, ring for ring**; 0 contacts, 0 spikes | **as predicted: map-client's, exactly** |
| **QW1** | the ε×8 set: 125 rings, 649 vertices, **45 proper crossings in 26 rings**. A's 88 rings are an exact subsequence of it, and the 37 dropped equal A's `PROVENANCE` (`dropped.water: 37`) | **as predicted** |
| **QW2** | the base-ε set: 199 rings, **4,946 vertices** (max 213), **185 proper crossings in 47 rings** | **off my prediction:** more vertices, as predicted, but it crosses |
| QW2′ | the ε×2 set: 199 rings, 2,673 vertices (max 86), 162 proper crossings in 49 rings | reported |
| **QW3** | KJAC: package `b3f428e5860e`, as predicted. Its water, at ε×2 (rung 1): 89 rings, 1,797 vertices, **240 proper crossings in 35 rings** | reported |

**By the ruling's table, this is outcome 2: (ii) also crosses, so the defect is upstream of the ladder.**
- **The crossings are in ordinary packages too.** KJAC never reaches the water rung, and its water crosses in 240 places.
- **The ε×8 rung doesn't cause the crossings; it removes most of them** (185 at ε, 162 at ε×2, 45 at ε×8, 28 after A's drops). What the
  rung does cause is the coarseness: 4,946 vertices at ε against 649 at ε×8.
- **My prediction for QW2 was off.** I wrote that my confidence was not high, and the read shows why.

## Outcome 2's "find which": the stage, registered before it runs

**The ruling's next step for outcome 2 is to find which stage, read-only.** For each water ring the builder makes, this read counts the
proper crossings at each stage of `LayerSet.add`'s water branch:
- **(a)** the stored ring, projected to the frame: what the ingest wrote;
- **(b)** after the clip to the square;
- **(c)** after Douglas–Peucker;
- **(d)** after quantising and dropping duplicates: the ring the package carries.

**How:**
- (a) to (c) are in km, so their orientation is the sign of a double. (d) is exact in integers.
- The counted pairs are `ring_stats()`'s. A sweep on x prunes the pairs, which changes only how fast it runs.
- The script is `qwater-f.mts` (scratch, sha256 `348b5f20e3b1`). It reads Midtown at 103.68 km and base ε, and KJAC at 162 km and ε×2.

| row | predicted |
|---|---|
| **QF0** (the anchor) | at both points, (d)'s ring list is identical to `LayerSet`'s own, and its counts reproduce QW2 (Midtown: 199 rings, 185 crossings in 47) and QW3 (KJAC: 89 rings, 240 in 35) |
| **QF1** | **the simplifier:** (a) 0 and (b) 0 at both points, with every ring that crosses at (d) crossing first at (c) |

| outcome | meaning | next |
|---|---|---|
| **(a) > 0** | the stored rings already cross: the source or the ingest | report the rings; which of the two is a further read |
| (a) = 0, **(b) > 0** | the clip creates them | report |
| (a) = (b) = 0, **(c) > 0** (predicted) | **Douglas–Peucker creates them at any tolerance.** The fix would be a topology-preserving simplifier, and it would touch every package, not only the dense-area ladder | to the orchestrator, with the options |
| (c) = 0, **(d) > 0** | quantising creates them | report |
| **mixed** | each ring's first crossing stage is reported, and the largest share names the main cause | report |
| **QF0 off, or anything else** | the instrument is suspect | **stop and report** |

**Outside the set (the orchestrator, ~18:50Z):**
- **If (d) doesn't reproduce QW2 and QW3,** the instrument is suspect: stop.
- **If the crossings first appear at a stage other than (c),** that is reported as read, and QF1 is not re-graded.
- **Since KJAC carries them too, this is a quality question for every package.** The outcome goes to the owner with options (such as a
  topology-preserving simplifier, or a validity pass after the clip). No fix is registered before he hears it.

## The stage read (2026-11-11, 19:03:33-19:03:34Z): Douglas–Peucker makes the crossings

The registration was pushed at 18:54:38Z (`a92435627f83`). The script's sha256 read as registered. Nothing was missing from the local
copy, and nothing ran on staging. The log is scratch (sha256 `c3453576defb`).

| | Midtown, base ε, 103.68 km | KJAC, ε×2, 162 km |
|---|---|---|
| rings (and `LayerSet`'s own list) | 199, identical | 89, identical |
| stored-ring vertices, the largest and in all | 9,871; 222,749 | 8,186; 184,861 |
| **(a)** the stored ring | **0** | **0** |
| **(b)** after the clip | **0** | **0** |
| **(c)** after Douglas–Peucker | **186 in 47 rings** | **238 in 34 rings** |
| **(d)** after quantising: the ring carried | 185 in 47 rings | 240 in 35 rings |
| the rings crossing at (d), by the first stage that crosses | c 47 | c 34, **d 1** |

- **QF0, the anchor: as predicted.** At both points (d)'s ring list is identical to `LayerSet`'s own, and its counts reproduce QW2 and QW3.
- **QF1: as predicted at Midtown, all but one ring at KJAC.** The stored rings and the clip are clean everywhere. **81 of the 82 crossing
  rings first cross at Douglas–Peucker.** One KJAC ring crosses only after quantising. As ruled, that is reported as read, and QF1 is not
  re-graded.
- **So the crossings come from the simplifier, at any tolerance, in every package.** Quantising adds or removes a few at the margin
  (Midtown 186 → 185; KJAC 238 → 240). The dense-area ladder's ε×8 rung removes most of them and makes the rest coarser.

**For the owner, with options (no fix is registered before he hears it):**
1. **A topology-preserving simplifier** for water rings: one that refuses a simplification that would make the ring cross itself, keeping
   vertices instead. The packages stay valid at the same tolerance, at some cost in bytes. The cost would be read before any choice.
2. **A validity pass after simplifying and quantising:** detect a ring that crosses itself and rebuild only that ring at a finer
   tolerance (or drop it). This is simpler, but it leaves the simplifier as it is.
3. **Accept:** the device's even-odd fill draws a crossing ring as wedges. That is what the 25 mi view shows today, at New York and at
   KJAC.

## The ruling (from the orchestrator, sent between 19:04:40Z and 19:12:12Z on 2026-11-11), verbatim (its pricing part)

> This is a quality question for every map package, so it goes to the owner. Before it does, price the two fixes, so he chooses on
> numbers.
>
> The cost read: local, read-only, predictions committed first, no change to the shipped builder (scratch copies only).
> - At all 13 points, three variants: (1) a topology-preserving simplifier for water rings; (2) a validity pass after quantising (rebuild
>   a crossing ring at finer tolerance, or drop it); (3) today's.
> - For each, report:
>   - the crossings left (predicted 0 for (1) and (2));
>   - the package bytes against the 32,768 cap;
>   - kOk or 422;
>   - the D2 first-fit radius at the 4 New York points, under B (the owner's pick);
>   - each build's peak memory against 77.2 MB.
> - What each result means, decided now:
>   - a variant that leaves crossings is not a fix;
>   - a variant that pushes any point to a 422, or a New York point to a smaller disc than B's 129.6 km, has a visible cost, and the owner
>     sees that cost named;
>   - anything else: report.
> - After that, the options go to him with your three, and my recommendation.

## The cost read: registered before it runs

**Under B** (the owner's pick, ~18:55Z): P1 at r=162 at all 13 points, then D2's steps at the 4 New York points until one fits. No sea
mask: that is the code PR's.

**The builder is a scratch copy, `dnw`:** Q-DR3's copy with one knob, `DENSE_WATER`, in the water branch. Nothing else differs.
- **unset (today's):** `dedupe(simplifyRing(clipped, ε).map(quantise), true)`, as now. `layers.ts` changes in three places only (the
  knob, its import, and the water line), and every other module is byte-identical to the copy Q-DR3 read.
- **"tp", a topology-preserving simplifier:** Douglas–Peucker as today. Then, while the quantised ring crosses itself, each crossing edge
  gets back the original vertex farthest from its chord (Douglas–Peucker's own next choice), until no edge crosses or none can be added.
- **"vp", a validity pass after quantising:** today's ring if it doesn't cross. Otherwise it is rebuilt at ε/2, ε/4, ε/8, ε/16 and ε/32,
  then unsimplified, and the first that doesn't cross is kept. If none does, the ring is dropped.
- **The artifacts:**
  - `frame.ts` `e1ab75e3cae9` (today's file with the two functions appended) and `layers.ts` `d683830b2e25`;
  - the child `dr-child6.mts` `e6f8e30e446e`: Q-DR3's sets-follow-up child, pointed at `dnw`, reporting each package's water crossings
    and the knob as the module read it;
  - the parent `dr-wc.mts` `de34071cf8ce`.
- **Each build runs in its own process,** with Q-DR3's controls and settled samples, on the local store copy. Nothing runs on staging.
- **The child's imports were checked before this was frozen:** given an unknown point under each of the three knob settings, it loaded
  every module and refused the point (exit 2). No build ran.

| row | what | predicted |
|---|---|---|
| **T** (the anchors) | today's, at all 13 | **Q-DR3's P1 packages:** at the 9, Nov 8's ab hashes (KBOS `cd74b62d4cd3` … KJAC `b3f428e5860e`); at New York, D2's first fit at 129.6 km (KJFK `66c2da06e716`, KLGA `9ca71d65c11d`, KEWR `34dc63f7e668`, Midtown `ccc58c1c9884`). **Each build's memory within 0.785 of Q-DR3's P1 figure** at that point and radius. Its crossings are reported |
| **TP** | "tp", at all 13 | **0 crossings in every package;** it fits where B fits (the 9 at 162 km, New York at 129.6 km); every build's memory within 0.785 of today's same build. Rung, bytes, water share and secondary roads are reported |
| **VP** | "vp", at all 13 | **the same three predictions as TP.** Rings rebuilt and rings dropped are reported |
| **every build** | | its controls; every sample settled; every object local; the knob as asked, read from the module; kOk and at most 32,768 non-terrain bytes; no build over 96 MB |

**Why "fits where B fits" is predicted:** water is droppable on the ladder, and neither fix touches anything else. The fixes can cost
water kept, or move a point to a later rung, but they can't stop a point fitting at the radius B fits at. A later rung is reported, not
graded.

**What each result means** (the ruling's table, with the instrument's own stops):

| outcome | meaning |
|---|---|
| **a T row off** | the instrument or the copy is not what Q-DR3 read: **stop** |
| a control, an unsettled sample, a missing object, the knob not taking, or a build over 96 MB | **stop** |
| **TP or VP leaves crossings** | **that variant is not a fix** |
| TP or VP pushes a point to a 422, or a New York point below 129.6 km | **a visible cost, named to the owner** |
| anything else, including a memory row off | **reported** |

**Since every TP and VP outcome has a ruled meaning, their off readings don't stop the run.** The owner gets the whole price list. Today's
rows stop it, because without them the other two can't be read.

**What this cannot see:** workerd; the sea mask's own rings, which the code PR brings and which both fixes would also simplify; and the
time each fix adds in workerd. Node's wall per build is reported.
