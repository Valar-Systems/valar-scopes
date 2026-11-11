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
