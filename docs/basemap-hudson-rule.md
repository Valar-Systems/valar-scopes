# Basemap: keeping split water whole (the Hudson rule) -- the registration, before any code

**Status.** A registration only: nothing here is built, run or deployed. The rule lands in **its own PR, after part 2's code PR
(docs/basemap-dense-code.md, #652) and before D4**. That PR's builder is part 2's builder plus this rule. Every prediction below is
frozen before a line of it exists. The predictions come from a measured scratch prototype (`pqh`, a copy of part 2's pinned prototype
`pq4d` with this rule's knobs) and from the rule's Python twin, cited by sha256 (first 12).

**The owner's pick is open.** One line of this file, section 5's OPTION, is the owner's to set: E1 at KEWR only, E1 wherever the margin
is thin (a rule, defined below), or E2. The rule itself (sections 2 and 3) is the same under all three.

## 1. What the owner asked for, and why the first rule was wrong

The owner, on run 10's pictures (2026-11-12): *"the Hudson stays whole in every New York map ... MIDTOWN drops the 20 km2 piece north
of the city, leaving a gap. KEWR drops SEA record 27 (86 km2), so the river stops at a straight line. Find the room elsewhere and show
me what that costs."*

**The first candidate ("never drop water whose removal disconnects kept water") was measured and is not the right property.**
- Run 10's packages have **0 splits** at every point: in the touch graph, no run-10 drop disconnected any kept water. The MIDTOWN gap is
  not a disconnection.
- The upper Hudson at MIDTOWN is one water BODY of three TIGER pieces (rings 67, 91 and 132 of the water8 candidates; 84.6 km2), most
  likely split by county lines. 91 and 132 are cut vertices of the whole graph only because a 5.0 km2 and a 0.4 km2 pond touch them at a
  single point; the static form kept them by that accident, and the dynamic form (which does guarantee connectivity) drops them.
- KEWR's record 27 (ring 157) shares two vertices, a 2.26 km straight line, with the 734 km2 sea piece 152: the sea data's grid seam.
  That one IS a straight cut, and the seam clause below keeps it.

**An ingest-time version (tags computed from source geometry) was measured and rejected.** The rule is about what the device DRAWS:
rings simplified at the query's eps x 8 and clipped to its square. That geometry first exists in the builder. With bodies and exact
dynamic coverage computed from the candidates' SOURCE rings, the drop sequence diverges at unit 1 or 2 at every point; 25-33 rings per
point (14-19%) land on the other side of the threshold; 15-22 per point are UNSAFE (source says covered, the drawn ring is not: drawn
coverage as low as 0.08), up to about 300 km2 of visible water per New York point; and KLGA, MIDTOWN and KEWR stop fitting at 129.6 km.

## 2. The rule (HR-RULE), exactly

Over the ladder's water candidates (the water8 set: water and sea at eps x 8, as part 2 builds it):

- **BODIES.** Rings i and j JOIN when, on the quantised integers: (1) an edge of one properly crosses an edge of the other; or (2) an edge
  of one overlaps an edge of the other collinearly over a positive length; or (3) a vertex or an edge midpoint (doubled coordinates) of
  one lies strictly inside the other (even-odd; a point on the boundary is not inside). Anything else -- a point contact -- does not
  join. Bodies are the connected components, computed once per candidate list.
- **SEAM (absolute).** A sea ring sharing >= 2 vertices with another sea ring is NEVER dropped: not alone, not with its body.
- **PARTIAL BODY.** A ring may be dropped ALONE only if another ring of its body is still kept AND it is COVERED: at least T = 0.80 of it
  lies under the other still-kept rings of its body. Otherwise it goes only with its WHOLE BODY: every still-kept ring of the body, as one
  unit, allowed when the body holds no seam ring. An isolated ring is a body of one.
- **ORDER.** At each step the smallest droppable unit goes: a single by its area, a body by the sum of its kept rings' areas; ties, the
  later index first (as `dropOrder`). The ladder drops whole units only: its bisection runs over units, never splitting one.
- **COVERAGE, by an integer raster (no clipper).** An ELIGIBLE ring (non-seam, its body has another ring) gets its own grid. Its bbox is
  [minx..maxx] x [miny..maxy] in package units, inclusive; w = maxx - minx + 1, h = maxy - miny + 1; its cell c is the smallest positive
  integer with c * c * 4096 >= w * h. Window: gx in [floor(minx / c), floor(maxx / c)], gy likewise. A cell belongs to a ring when its
  centre ((2gx+1)c/2, (2gy+1)c/2) is inside it, even-odd, with the half-open test: an edge a->b counts in the centre's row iff
  (2 a.y > Yc2) != (2 b.y > Yc2), Yc2 = (2gy+1)c, and lies left of the centre iff x_cross < Xc2 / 2, Xc2 = (2gx+1)c, decided by
  cross-multiplication in integers. coverage(i) = |cells of i under a still-kept other ring of i's body| / |cells of i|; no cell: 0.
- **T = 0.80, from the data.** At the four New York points no inland ring is covered beyond 0.602 even by every other candidate, so 0.80
  (and 0.70) protect every river piece; 0.80 lets go the TIGER pieces lying under the sea (60-70 per New York point, 94 at KBOS), each
  dropped with at most 20% of it visible. MIDTOWN's 91 is covered 0.012 and 132 is covered 0.374 (both by ring 67).

**Why the builder.** CLAUDE.md: "find the last stage that still had the information". The information is the drawn geometry, and the
builder is the first and last stage that has it. The rule adds no dependency and is integer end to end, so its Python twin and the
TypeScript agree exactly.

### Parity, measured (registered as hb-expect-ibody.txt, sha a2f26d5b1f24, before its held-out inputs existed)

- The integer body test equals shapely's bodies (intersection length > 0 or area > 0) on **16 held-out candidate lists**: the four New
  York points at 162 km, KEWR at 103.68 km, and the other 11 points at their radius. Controls, all as stated: a corner touch is not
  joined, a shared edge is, an overlap is, and a hand-merged partition reads different.
- The full twin (integer bodies + the integer raster + seam + units) gives units identical end to end to the measured prototype's at
  KJFK, KLGA, KEWR and MIDTOWN. At KBOS the first difference is unit 98 (ring 198 vs 149), past its 54-unit prefix, so all five
  packages are the same. From here the TWIN is the predictor.
- (An earlier run of the body test at the five points was made before any prediction existed, and is a post-hoc observation only.)

## 3. What the rule costs, before the owner's pick (T = 0.80, lines at eps x 4 where the ladder puts them)

| point | rung, radius | water rings dropped (run 10) | non-terrain B | visible water vs run 10, km2 | the next unit to go, and its margin |
|---|---|---|---|---|---|
| KBOS | 4, 162 km | 54 (45) | 32,767 | -76 / +22 | a 6.6 km2 single ring, **1 B**; a 2-ring 6.7 km2 body after 57 B |
| KJFK | 7, 129.6 | 91 (86) | 32,629 | -205 / +84 | a 4-ring split inland body, 50.3 km2, 80 km long, **139 B** |
| KLGA | 7, 129.6 | 106 (116) | 32,713 | -284 / +163 | a 162 km2 single ring, **55 B** |
| MIDTOWN | 7, 129.6 | 106 (120) | 32,748 | -222 / +187 | **the whole upper-Hudson body (67 + 91 + 132), 84.6 km2, at 20 B** |
| KEWR | -- | -- | -- | -- | **does not fit at 129.6 km with eps x 4 lines (+533 B)**: the owner's pick decides |

"Visible" is union minus union in km, so a dropped piece still covered by other kept water costs nothing. Margins are small by
construction (the ladder fills to within one unit of the cap); what matters is WHAT the next unit is. The other 11 points drop no water
and are byte-identical to run 10.

**Sensitivity (registered as hb-expect-ii.txt, sha fe3c860de0a8): FRAGILE at +10.** At T = 0.90 MIDTOWN drops 91 and 132 -- the whole
upper-Hudson body goes as one unit. The cause is the whole-body unit coming next, not T: any T leaves a body that size next in line once
the smaller units are spent. At T = 0.70 the controls hold. Fit does not change at either neighbour.

## 4. The owner's options for the room, in his terms

**E1: coarser lines.** Rivers and primary roads drawn within **1.6 km** of true instead of 0.8 km: at the 10-mile view up to 12 px off
instead of 6, at the 25-mile view about 5 px instead of 2.4. Measured: eps x 8 frees only 8-12% of the line bytes (2.1-3.3 KB at these
points; the registration's own expectation of 15-30% was a miss, plausibly because lines at eps x 4 are already near their floor), and
that is enough to bring back almost every lake. The ladder gains rungs 8 (lines at eps x 8), 9 (+ secondaries dropped) and 10 (+ water
units).

**E2: a smaller map.** D2's next step at KEWR: the map ends at **103.68 km (64.4 mi)** instead of 129.6 km (80.5 mi). The radar's
default view is 100 miles, so on the home screen the outer third (64 to 100 mi) is blank instead of today's 80.5 to 100. Lines become
FINER than today (eps x 2, rung 4). **E2 conflicts with a frozen row: R-D2 (#652) freezes "at most 2 steps at New York's 4; a third
step stops", and KEWR under E2 takes 3. Choosing E2 needs a ruling to amend R-D2.**

| option | KEWR | MIDTOWN | KJFK | KLGA | KBOS |
|---|---|---|---|---|---|
| **(a) E1 at KEWR only** (rung 8 is tried only where the rule made rung 7 fail; measured, it fires nowhere else) | eps x 8 lines, 129.6 km, rung 10; 36 dropped; water rings 130 vs 25; visible -0 / +696 km2; record 27 kept | the rule as is (section 3) | as is | as is | as is |
| **(b) E1 wherever the margin is thin** (the rule form, below) | as (a) | eps x 8 lines, rung 10; 10 dropped (163 of 173 kept); visible -0 / +458; the upper Hudson now 2,954 B from dropping | eps x 8, rung 9; NOTHING dropped (179 of 179); -0 / +220; its 4-ring body >= 2,214 B away | **unchanged**: no split inland body is left ahead of it (its next unit is a 162 km2 single ring at 55 B, which this definition does not protect) | **changes**: 57 B before a 2-ring 6.7 km2 body, so its lines go from 0.4 km to 1.6 km (4x). **Not yet measured**; predicted rung 9, nothing dropped |
| **(c) E2 at KEWR** | 103.68 km, rung 4, eps x 2 lines; 90 dropped; visible -7,574 / +248 km2, nearly all the band beyond 64 mi; record 27 kept; **needs R-D2 amended** | as is | as is | as is | as is |

**(b)'s definition, written out for the owner to accept or not:** at a radius where the ladder fits, if fewer than **M = 512 B** of
growth would drop a split inland body (a whole-body unit of more than one ring and no sea ring), the ladder takes eps x 8 lines (rungs
8-10) there instead. Its consequences are the column entries above. M is arbitrary: at M = 128 KJFK (139 B) would not take it; at any M
above 57 KBOS does. A fixed list of the four New York maps is the alternative to the rule form; it is a place list in code, and the
owner's to choose.

## 5. The predictions (frozen)

**OPTION: _the owner's pick: (a), (b) or (c)_** -- the only line that changes. Each option's packages are below, measured on the
prototype; the PR's builder must reproduce the chosen one byte for byte.

| row | input | predicted |
|---|---|---|
| **HR-PKG** | the Hudson PR's builder on the prototype's sea store (`sea-obj`, store e9ef04165ba2), the 16 points, the client's query, as P2-1 | byte-identical to: **(all options)** KMIA 1a338238fbc8, KSFO 4d03beecdb4b, KSEA 9bd5b34d6718, KORD cb59bb06a506, KDTW 682208bf2fb2, KMSP b694c01dc502, KIND 89a4195fa4be, KJAC 42c32cd8578e, BUFFALO 52fad5e317f3, ROUSES afe7554245b8, ZAPATA e2ddaa00ecaf (run 10's); **(a)** KBOS 9af5f9e0ce25, KJFK 4737c48e436a, KLGA e7f0af9d9fba, KEWR e5333b0c570a, MIDTOWN 8377065310f7; **(b)** KEWR e5333b0c570a, MIDTOWN af608ebcddb4, KJFK 227ecbddfdf7, KLGA e7f0af9d9fba, KBOS **to be measured and frozen before the PR, if (b) is picked**; **(c)** KBOS 9af5f9e0ce25, KJFK 4737c48e436a, KLGA e7f0af9d9fba, KEWR f77f3911ae3a, MIDTOWN 8377065310f7. Any byte difference stops |
| **HR-CTRL** | the same builds | MIDTOWN keeps rings 91 and 132; KEWR keeps record 27 (the seam ring at the river mouth; at 103.68 km, the seam ring whose box matches within 0.1 km); KBOS's next unit is the 6.6 km2 ring |
| **HR-TWIN** | the twin's units vs the PR's units, the 16 points | equal, unit for unit, at every point that drops water |
| **HR-MEM** | the PR's sea build with the rule minus without, P2-M's instrument, per phase | **at most +1.0 MiB** at the ladder phase at each of the five points (the eligible rings' count bytes, measured by the twin: KBOS 656,575 B, KJFK 558,296, KLGA 532,120, KEWR 490,901, MIDTOWN 514,929). A stop above it |
| **HR-MEM-E1** | options (a) and (b) only: the build with the eps x 8 line set minus without, at the layers phase | **at most +2.0 MiB** at each point that takes it. A bound, not an estimate: this line set has not been measured. A stop above it |
| **HR-CPU** | wall time of the build with the rule (and E1) minus without, median of 3, each of the five points | **at most +500 ms and at most 1.25x** at every point. A stop above either |
| **HR-CI** | the PR's host tests | green, each plant below shown red first by its sabotage |

**HR-MEM against P2-M's measured peaks.** **The unit: #652's MB is DECIMAL (10^6 B).** The instrument reports MiB (1024 x 1024 B) and
its driver prints the decimal figure beside it -- QW4's run read `mem 73.99 MiB at "ladder" (77.6 MB)` at KLGA, which #652 quotes as
"77.6 MB" (73.99 x 1.048576 = 77.58). So CM-M's 96 MB stop is **91.55 MiB**, and the owner's 77.2 MB report line is 73.62 MiB.

| point | P2-M sea peak, MiB (MB) | + HR-MEM's bound | + HR-MEM-E1's bound, where the option takes E1 | worst case, MiB (MB) | headroom to CM-M's 96 MB (91.55 MiB) |
|---|---|---|---|---|---|
| KBOS | 69.62 (73.00) | 70.62 | 72.62 under (b) | 72.62 (76.15) | 18.9 MiB |
| KJFK | 72.58 (76.11) | 73.58 | 75.58 under (b) | 75.58 (79.25) | 16.0 MiB |
| KLGA | 74.74 (78.37) | 75.74 | -- (no option takes E1 there) | 75.74 (79.42) | 15.8 MiB |
| KEWR | 74.77 (78.40) | 75.77 | 77.77 under (a) or (b) | 77.77 (81.55) | 13.8 MiB |
| MIDTOWN | 71.85 (75.34) | 72.85 | 74.85 under (b) | 74.85 (78.49) | 16.7 MiB |

The owner's 77.2 MB report line (#652, ruling 4): KLGA's and KEWR's sea builds ALREADY read above it before this rule (78.37 and 78.40
MB, as QW4's 77.6 did); with the bounds above KEWR could read up to 81.55 MB. A report, not a stop.

**HR-PLANTS: a plant per refusal path, each in the host tests on synthetic rings, each with its sabotage shown red first.**

| plant | the case | passes when | its sabotage, and the red it must show |
|---|---|---|---|
| PL-SEAM-ALONE | a sea ring fully covered by another ring of its body, sharing 2 vertices with another sea ring | it is never dropped alone | S-HR-SEAM: drop the seam check -> it drops |
| PL-SEAM-BODY | a two-sea-ring body joined at a seam, smaller than everything else | it is never dropped as a unit | S-HR-SEAMBODY: allow seam bodies as units -> it drops |
| PL-COVER | a ring in a two-ring body, its cells covered 79% and (a twin) 81% | 79%: refused alone; 81%: dropped alone | S-HR-T: `>` for `>=`, or T = 0.70 -> the 79% ring drops |
| PL-POINT | two ponds touching at one vertex | separate bodies: the smaller drops alone | S-HR-POINT: join on a point contact -> it cannot drop alone |
| PL-UNREL | an unrelated isolated pond, the smallest ring | it drops first, exactly as before the rule | S-HR-PROTECTALL: protect every ring -> nothing drops |
| PL-PREFIX | a three-ring body that must go as one unit to fit | the package never holds part of it | S-HR-RINGBISECT: bisect over rings -> a partial body |
| PL-RASTER | rings whose cell centres fall exactly on an edge and on a vertex | the half-open rule's counts, as the twin gives them | S-HR-CLOSED: count `>=` in the row test -> a different count |

**FT-HR, the frame-time row (the owner's standing rule, 2026-11-12: "every change to drawing code reports its frame time on the
bench, before and after. A slowdown beyond the margin frozen in its registration stops it").** The rule changes what the device draws:
KEWR goes from 25 water rings to 130 under (a), MIDTOWN, KJFK and KLGA gain rings under (b), and E1 changes the lines.
- **The instrument:** map-client's frame bench (`docs/frame-time-bench.md`, as FT-SEA cites it in docs/basemap-dense-code.md), on the
  owner's (a) cloud path, the v20-tree framebench sibling swapping the packages in RAM through `Initialise`; board E2:08, in a window
  the orchestrator grants, after FT-SEA or with it.
- **The scene:** FIXED's, the home set to the airport, **40 drawn aircraft, map AND weather ON** -- unlike FT-SEA, whose amendment
  turned weather off to isolate the map; here the reviewer's ruling puts the device's normal load on, the same for both packages.
  Rungs 10, 25 and 50 mi, each a 100 s settle and 120 s measured; run 10's package first, then the option's.
- **The packages, pinned and staged** in `C:\Github\_handoffs\map-server\frame-row-packages-hudson\` (copies of the measured builds):

  | point | run 10 | (a) | (b) | (c) |
  |---|---|---|---|---|
  | KEWR | dff28487eaf4 | e5333b0c570a | e5333b0c570a | f77f3911ae3a |
  | MIDTOWN | 20a22bed5510 | 8377065310f7 | af608ebcddb4 | 8377065310f7 |
  | KJFK | 52ac9dc39de0 | 4737c48e436a | 227ecbddfdf7 | 4737c48e436a |
  | KLGA | feb6ef3bdf04 | e7f0af9d9fba | e7f0af9d9fba | e7f0af9d9fba |
  | KBOS (it changes under every option, so it runs too) | f21d705d8b8a | 9af5f9e0ce25 | measured first if (b) | 9af5f9e0ce25 |
  | KORD (the control: unchanged under every option) | cb59bb06a506 | cb59bb06a506 | cb59bb06a506 | cb59bb06a506 |

- **Graded:** the p95 of `p2p` (present to present), the option's package minus run 10's, at each point and rung, **<= M**; `fr`
  printed beside it; frames on which the map rebuilt are counted and included, per the bench.
- **M: FT-SEA's formula,** M = max(2 x the baseline's repeat noise, 2.0 ms), the repeat noise being the |delta p95| between the
  baseline's CTRL plain scene and its FIXED scene. **Its number is recorded here from the baseline before FT-HR runs.** A speed-up
  never stops.
- **Named in advance, the reading expected to differ most: KEWR at 50 mi** under (a) or (b) -- the most rings gained (105) with the
  whole package in view. No direction is predicted: E1's lines carry fewer vertices while the water carries more rings.
- **The control: KORD,** the same bytes twice, must read |delta p95| within the repeat noise at every rung; if not, the instrument is
  in doubt: STOP.
- **Over M at any point: STOP; the PR does not merge, and the reading goes to the owner.**

**Outside the set: stop and report, no reading.** Any HR-PKG byte; a control otherwise; the twin and the PR disagreeing; a limit
crossed; a plant passing under its sabotage; any of the 11 other points changing; FT-HR over M, or its control outside the noise.

**This PR merges only after:** the owner's pick written into OPTION with that option's rows; (b)'s KBOS measured and frozen, if (b);
and FT-HR's reading inside M. Its base is re-carried by tree at that moment.

## 6. This PR's CI, predicted

A docs-only change on main 4b00c97b3dbd. The only workflow change since the docs-only shape was read (main 099f1946) is #669's one-line
install change inside an existing job, which adds no job and no trigger. **32 check runs: 30 success, 2 skipped (`promote`, `version`),
0 failed**, as #652's. The tested base is read from the run's own checkout log. A new red from `ship-alert` reads INSTRUMENT, and the
GitHub-side re-run rule applies. Anything else: stop and report.
- **Read at 1b26029cac0c:** 32 = 30 / 2 / 0 as predicted, tested on "Merge 1b26029cac0c... into 4b00c97b3dbd...".
- **Re-predicted for the FT-HR commit** (a new head, so a new run): the same, **32 = 30 / 2 / 0**, tested on the merge into main as it
  then stands (5cdc8bd8f35d at this writing: #652's docs merge, which changes no workflow).

## 7. Where the measurements are (scratch, sha256 first 12)

- The prototype: `pqh` = `pq4d` plus `hudson.ts` 726454fd61ff, `budget.ts` be866547da10, `build.ts` d996b441aa60; `pq4d` unchanged.
- The rule's measurement: expectations hb-expect.txt 8f3465bda734 (R-CUT, E1/E2), hb-expect-ii.txt fe3c860de0a8 ((ii)), hb-expect-ibody.txt
  a2f26d5b1f24 (parity); estimator hb-ii.py 5210fea2165a; runs hb-out/S08.json f39f5bb9dc5b, S07 b50c86bb2e5e, S09 5a0e9602d114,
  S08-E1c a6042baae18a, S08-F129 41a6b5653824; margins margin.json 94612890ed51, margin-e1.json b7665a4b225e.
- (B)'s measurement: cand-src.mts a62145777bce, hb-src.py 84df9bc548b4, hb-src-flips.py ef58c3dc06a5.
- The raster: hb-raster2.py fdc7bc2db6fd; the parity run hb-parity.py d50b1fcee522.
- The owner's pictures (in the hand-off folder, opened only by him): KEWR-E1-vs-E2 b68198ccb84e, MIDTOWN-rule bf673465abd0, KJFK-rule
  824d0f672584, KLGA-rule 21cba0040b84, KBOS-before-after 03a9944ce204.
