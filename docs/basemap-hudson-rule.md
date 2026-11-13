# Basemap: keeping split water whole (the Hudson rule) -- the registration, before any code

**Status.** A registration only: nothing here is built, run or deployed. The rule lands in **its own PR, after part 2's code PR
(docs/basemap-dense-code.md, #652) and before D4**. That PR's builder is part 2's builder plus this rule. Every prediction below is
frozen before a line of it exists. The predictions come from a measured scratch prototype (`pqh`, a copy of part 2's pinned prototype
`pq4d` with this rule's knobs) and from the rule's Python twin, cited by sha256 (first 12).

**The owner's pick is OPEN AGAIN.** He picked (b), the rule (section 5b), looked at Boston's pictures, and did not ship it: he asked
for three alternatives, each priced in bytes and pictured (section 5c), and chooses again from those. The original options were: E1 at
KEWR only, E1 wherever the margin is thin (a rule, defined below), or E2. The rule itself (sections 2 and 3) is the same under all of them.

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

**OPTION: OPEN AGAIN.** (b), the rule, was picked (15:35Z 2026-11-13) and then not shipped (16:49Z, section 5c); the owner chooses
again from section 5c's alternatives -- the only line that changes. Each option's packages are below, measured on the
prototype; the PR's builder must reproduce the chosen one byte for byte.

| row | input | predicted |
|---|---|---|
| **HR-PKG** | the Hudson PR's builder on the prototype's sea store (`sea-obj`, store e9ef04165ba2), the 16 points, the client's query, as P2-1 | byte-identical to: **(all options)** KMIA 1a338238fbc8, KSFO 4d03beecdb4b, KSEA 9bd5b34d6718, KORD cb59bb06a506, KDTW 682208bf2fb2, KMSP b694c01dc502, KIND 89a4195fa4be, KJAC 42c32cd8578e, BUFFALO 52fad5e317f3, ROUSES afe7554245b8, ZAPATA e2ddaa00ecaf (run 10's); **(a)** KBOS 9af5f9e0ce25, KJFK 4737c48e436a, KLGA e7f0af9d9fba, KEWR e5333b0c570a, MIDTOWN 8377065310f7; **(b)** KEWR e5333b0c570a, MIDTOWN af608ebcddb4, KJFK 227ecbddfdf7, KLGA e7f0af9d9fba, KBOS **7402f8cee0f5** (HR-KBOS-E1, section 5a: measured as predicted and frozen, 2026-11-13); **(c)** KBOS 9af5f9e0ce25, KJFK 4737c48e436a, KLGA e7f0af9d9fba, KEWR f77f3911ae3a, MIDTOWN 8377065310f7. Any byte difference stops |
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
  | KBOS (it changes under every option, so it runs too) | f21d705d8b8a | 9af5f9e0ce25 | 7402f8cee0f5 | 9af5f9e0ce25 |
  | KORD (the control: unchanged under every option) | cb59bb06a506 | cb59bb06a506 | cb59bb06a506 | cb59bb06a506 |

- **Graded:** the p95 of `p2p` (present to present), the option's package minus run 10's, at each point and rung, **<= M**; `fr`
  printed beside it; frames on which the map rebuilt are counted and included, per the bench.
- **M = max(2 x repeat noise, 2 x U95, 2.0 ms) = 3.08 ms** (amended BEFORE any reading, by the orchestrator's ruling (B) on the v19
  baseline; FT-SEA carries the same amendment, recorded in part 2's code PR's own doc commit). The inputs, from map-client's v19
  baseline (docs/frame-time-bench.md at 3fb9adab3633; reader frame_bench_read_b.py b0dbd4ebc163, output baseline_b.out 29d24028cec9):
  - REPEAT NOISE 0.93 ms: |delta p95(`p2p`)| between S0, CTRL plain ("Bend, 40 fixed, 25 mi"), and S6, FIXED Bend/40/25 mi -- the
    same scene measured twice in one run;
  - **U95 = 1.54 ms,** the bench's stated uncertainty on every single p95 (I-6's excess, +11.54 against its +10.0 plant). It comes
    from ONE CTRL pair, so it is an ESTIMATE.
  - **Why 3.08:** the graded quantity is a difference of two single p95s, each +- 1.54, so this bench cannot resolve less than about
    3 ms. A margin inside that would turn noise into STOPs, each wrongly going to the owner.
  - **The cost, named:** a real slowdown under 3.08 ms passes unseen. A pass is recorded as "below this bench's resolution", never as
    "no slowdown". A speed-up never stops. Resolving under 3 ms would need repeats, registered then.
- **Named in advance, the reading expected to differ most: KEWR at 50 mi** under (a) or (b) -- the most rings gained (105) with the
  whole package in view. No direction is predicted: E1's lines carry fewer vertices while the water carries more rings.
- **The control: KORD,** the same bytes twice (an A/A pair), must read **|delta p95| <= 2 x U95 = 3.08 ms** at every rung (amended
  with M). **Each run prints KORD's A/A difference beside the graded deltas, as that run's live noise reading.** If the A/A ever exceeds
  3.08 ms, the run is the instrument's: STOP, and nothing in it is graded.
- **Over M at any point: STOP; the PR does not merge, and the reading goes to the owner.**

**Outside the set: stop and report, no reading.** Any HR-PKG byte; a control otherwise; the twin and the PR disagreeing; a limit
crossed; a plant passing under its sabotage; any of the 11 other points changing; FT-HR over M (3.08 ms), or KORD's A/A over 3.08 ms.

**This PR merges only after:** the owner's NEW pick from section 5c's alternatives, each registered, built, read and pictured under
the standing road-appearance row (docs/basemap-route.md A2.49) before he is asked; that pick written into OPTION with its rows; and
FT-HR's reading inside M on the chosen option's packages. Its base is re-carried by tree at that moment. ((b)'s KBOS, 7402f8cee0f5,
stays recorded as measured; the owner looked at Boston's pictures, section 5c.)

### 5a. The owner's pick, open between two forms, and both scratch builds registered before either runs

**The owner (06:01Z 2026-11-13, relayed verbatim):** "5. The Hudson (#682): (b), coarser lines (1.6 km) in all four New York maps."

That sentence fits two forms, which differ at KLGA and KBOS:

| form | coarsens (E1) | unchanged |
|---|---|---|
| **(b) the RULE** (as written above) | KEWR, MIDTOWN, KJFK and **KBOS** | **KLGA** |
| **the FIXED LIST** (this file's named alternative: the four New York maps, a place list in code) | KEWR, MIDTOWN, KJFK and **KLGA** | **KBOS** (it keeps the rule as is, 9af5f9e0ce25, as under (a) and (c)) |

The question is with the owner, at the top of his digest. **Nothing goes into OPTION until he answers.** Each form's one unmeasured build is registered here, and **only the chosen one runs**.

| build | form | how | predicted | then |
|---|---|---|---|---|
| **HR-KBOS-E1** | the rule | the Hudson prototype on the prototype's sea store (`sea-obj`, store e9ef04165ba2), KBOS at the client's query, E1 forced (eps × 8 lines) | 162 km; **rung 9; nothing dropped** (every water ring kept); kOk; at most 32,768 non-terrain bytes; 0 proper crossings; the 2-ring 6.7 km² body whole | its sha is frozen as (b)'s KBOS row and staged for FT-HR; HR-MEM-E1 at KBOS **at most +2.0 MiB** |
| **HR-KLGA-E1** | the fixed list | the same, KLGA, E1 tried as at KEWR (rungs 8-10) | 129.6 km; **a rung from 8 to 10**; `dropped.water` **at most 106**, the rule-as-is KLGA's (section 3's table); kOk; at most 32,768; 0 proper crossings; every split inland body whole or wholly dropped, never in part | its sha is frozen as the list's KLGA row and staged for FT-HR in place of e7f0af9d9fba; HR-MEM-E1 at KLGA **at most +2.0 MiB** (the memory table's "no option takes E1 there" becomes this bound) |

**Under the fixed list,** FT-HR's packages are: KLGA the new build; KBOS 9af5f9e0ce25 (the rule as is); KEWR, MIDTOWN and KJFK as under (b); KORD the control.

**Outside the set: stop and report, no reading.**

**The bench (held after 06:04Z):** one shared no-hands window for FT-SEA and FT-HR on E2:08, 11-16 04:00-07:30Z, after map-client's v20 bench. It is granted on a frozen manifest. The packages are frozen once the chosen build reads.

### 5b. The owner's answer: the rule. HR-KBOS-E1 read as predicted; Boston's pictures

**The owner (15:35Z 2026-11-13, relayed verbatim by the orchestrator at 16:33:35Z):** "The Hudson (#682): the rule. Show me Boston's
picture under it before it ships." **The orchestrator's reading:** option (b) as registered. Newark, Midtown and JFK coarsen,
LaGuardia stays as it is, and Boston coarsens too; it is not "exactly the four NY maps". (b)'s frozen predictions stand.
**HR-KLGA-E1 (the fixed list) was not run.**

**HR-KBOS-E1, run 16:37:49Z** (`bash runlog.sh hr-kbos-e1 bash hr-kbos.sh`; KBOS only; the predictions above were pushed at 7c5e2872
first). **11 of 11 AS PREDICTED:**

| row | read |
|---|---|
| C1, control: run 10's KBOS rebuilt today | f21d705d8b8a, byte-identical (FT-HR's run-10 column) |
| C2, control: the rule as is at KBOS rebuilt today | 9af5f9e0ce25, byte-identical |
| C3, control: KBOS's water ring total, three ways | 242, 242, 242 (run 10's 197 + 45 dropped; the rule's 188 + 54; the sequence's n) |
| C4, control: the forced E1 fired | "forced at 162000000 (rung 7 fitted)" |
| radius | 162 km |
| rung | 9 |
| nothing dropped | 242 of 242 water rings; dropped.water 0 |
| status | kOk |
| non-terrain bytes | 32,760 (at most 32,768) |
| proper crossings | 0 over 242 rings (each water ring's own edges, as P2-1: roads are not in this row) |
| the 2-ring 6.7 km2 body whole | rings 162 and 165 present: 6.67 km2 (ring areas added, as margin.json's 6.7), box x 13.0..15.4, y -62.4..-56.8 km |

Beside the rows: every ring of run 10's KBOS package is in it (0 missing of 197); lines at 1.61 km. **(b)'s KBOS is 7402f8cee0f5**
(211,400 B), now in HR-PKG and FT-HR's table, and staged for FT-HR as
`C:\Github\_handoffs\map-server\frame-row-packages-hudson\KBOS-S08-F162.bmpk`, read back after the copy: sha256
7402f8cee0f53efc123b06607d46f4a69b62ee76618154790bff9dfebd08c302.
HR-MEM-E1 at KBOS (at most +2.0 MiB) is the code PR's row and is not measured here.

**FT-HR's packages under (b):** KEWR e5333b0c570a, MIDTOWN af608ebcddb4, KJFK 227ecbddfdf7, KLGA e7f0af9d9fba (unchanged),
KBOS 7402f8cee0f5; KORD cb59bb06a506 the control.

**Boston's two pictures, for the owner** (in `C:\Github\_handoffs\map-server\hudson-boston\`, outside every git tree; each file named
by its own sha256; not opened by this session):

| file | the package | from |
|---|---|---|
| KBOS-today-f5214e3dbaed.png (81,666 B) | f21d705d8b8a, run 10's KBOS | `pqh` with no Hudson knobs, sea store `sea-obj` e9ef04165ba2 |
| KBOS-rule-8b3b2d19d695.png (81,053 B) | 7402f8cee0f5, (b)'s KBOS | `pqh`, the rule's units + E1 forced at 162 km, the same store |

- **The renderer:** hb-pics.py's drawing, unchanged: the s3-128's Dark colours, north up, a 240 px disc x3, no terrain shading.
- **The views:** 100, 50 and 10 mi, the same in both files.
- **A GREEN outline,** the same in both: the 45 rings the rule's map draws and today's does not, 69 km2 of visible water. Water lost to
  land: 0 px at every view; the 29 water->road px at 100 mi are coarser roads drawn over water.
- **An ORANGE label on each of the 4 bodies today's map splits** (drops part of), which the rule keeps whole.
- **Line pixels that change:** 996, 1,676 and 1,103 at 100, 50 and 10 mi. Boston's lines go from 0.40 km to 1.61 km (4x).

**Q1 (the orchestrator, 16:41:26Z): the labels' areas.** **The ORANGE labels print the ring areas ADDED** (a body's overlaps counted
twice): the registration's quantity (margin.json, hb-margin.py), and HR-KBOS-E1's graded 6.67. **What is drawn is the union.** The
owner's digest uses the drawn set (the orchestrator, 16:45Z); the pictures are not re-rendered.

| body | where | label (ring areas added), km2 | drawn (union), km2 | overlap, km2 | today drops, the rule shows again, km2 |
|---|---|---|---|---|---|
| rings 162, 165 | 62 km NE (~42.90 N, 70.83 W) | 6.7 | 6.5 | 0.15 | 2.5 |
| rings 138, 152, 155 | 153 km NW | 7.9 | 7.5 | 0.35 | 4.0 |
| rings 24, 45 | 154 km NE | 11.9 | 11.3 | 0.60 | 2.7 |
| rings 95-98 | 133 km SW, by Springfield | 23.4 | 23.4 | 0.03 | 1.9 |

The first is in the 100 and 50 mi views (about 2 x 4 and 4 x 8 px at device scale); the other three in the 100 mi view only. None
is inside 10 mi.

**Q2 (the orchestrator, 16:41:26Z): the hatched band south of the centre in the rule's 10 mi view.**
- **The object.** The I-93 / US 1 / Route 3 corridor south of downtown (the Southeast Expressway). TIGER's Massachusetts roads file
  holds it as many separate primary lines: each carriageway, each route name sharing the pavement, and the HOV lanes. Primary records
  whose bounding box meets the corridor: 48 (I-93 4, Southeast Expy 4, State Rte 3 4, Sargeant William G Walsh Expy 4, US Hwy 1 2,
  US Rte 1 2, I-93 Sb/Nb HOV Lane 1 each, among others). That box test is loose. The packages carry 36 distinct lines along the
  corridor today and 37 under the rule.
- **Why hatched and not just straighter.** Each line is simplified on its own.
  - At 0.40 km (3 px at 10 mi), every copy stays within 3 px of the road. They stack in a 0.2-2.8 px band (y = 2-15 km) and cross
    401 times inside it: one line.
  - At 1.61 km (12 px), each copy keeps different vertices and strays its own way. The band fans to 4-8 px mid-corridor and 12.2 and
    16.3 px at 14-15 km south, and they cross 116 times, each crossing now wide: the hatching.
  - Each line still sits within 1.6 km of its own true path, as section 4 stated.
- **Against the rows: outside every registered row.** "0 proper crossings" is water rings only, and no row grades how a road looks.
  The registration stated the line cost as a distance only (section 4; (b)'s KBOS cell). The orchestrator (16:45Z): "That isn't a
  miss in your run; the registration simply never named it. The owner now sees it and decides." No fix is proposed. Whether the New
  York E1 maps show the same is not measured; their pictures are rendered only if the owner asks.
- **For later (the orchestrator, 16:45Z):** any future change to line simplification registers a road-look row: band width and
  crossing count over a named multi-line corridor at the 10 mi view, by hb-kbos-road.py's measure, with a picture, the band width
  predicted before the run.

### 5c. The owner did not ship (b): three alternatives, each priced and pictured before he chooses again

**The owner (16:49Z 2026-11-13, relayed verbatim by the orchestrator at 16:50:35Z):**

> "Hudson rule (#682): not "ship it" as it stands. I've looked at the Boston pictures.
> - 100 mi: fine. 50 mi: slightly worse. 10 mi: clearly worse. The Southeast Expressway fans into a 4–6 strand bundle, and every road becomes a straight line (I-93 north is ruler-straight). 1.6 km line detail is about 12 px at 10 mi. The device zooms to 10 and 5 mi, so people will see this.
> - The water gained in Boston (45 small rings, 4 lakes kept whole) doesn't pay for that.
> Please have map-server come back with one of these, each priced in package bytes and shown in 100/50/10 mi pictures:
>   1. Merge the stacked duplicate road lines (the ~36 expressway lines) into one before simplifying, and keep 0.4 km line detail. This may also free enough bytes that Boston doesn't need coarsening at all.
>   2. Use coarser lines only where the format lets them stay out of the 5 and 10 mi views (for example, a coarse level for 50/100 mi only).
>   3. Apply the rule only to the four New York maps, and leave Boston on today's package.
> Yes to New York's 10 mi pictures, and add 5 mi for both cities.
> Register the road-appearance check now: any change to line detail gets before/after pictures at 5/10/50/100 mi before I'm asked to decide."

**What the orchestrator asked of map-server (16:50:35Z), in short:** #682 stays open and does not merge as it stands; each option is
registered before it is built (bytes against the cap per affected map; the water: rings kept, bodies whole, dropped.water; the
road-look measure over the Southeast Expressway and one named New York multi-line corridor), then built, then read; pictures before and
after at 5, 10, 50 and 100 mi (Boston under all three; New York, every map an option changes plus today's), the same renderer, files
named by hash outside git, areas labelled as DRAWN (the union), never as rings added; the price in bytes and frame-time risk, and
whether FT-HR's 11-16 window still covers the chosen option's packages. The road-appearance check becomes a standing row (A2.49 of
docs/basemap-route.md).

**Option 2's format question, answered from the code (map-server to the orchestrator, 16:54:51Z):**
- **The format cannot carry a coarse line LEVEL for 50/100 mi only.** `levels` are terrain-only (section type 6). Rivers (type 2) and
  roads (type 3) carry one geometry per line under the header's one `toleranceMm`. A per-zoom line set would be a format change: a
  second line section or a level field; the device parser (`include/BasemapPackage.h`'s section types and count checks); `MapLayer`
  choosing a set by view radius; `format.ts`'s validator twin and the host tests; and both sets would count in the 32,768 B cap, so it
  costs bytes rather than freeing them. Not proposed.
- **It can carry detail graded by DISTANCE, unchanged.** The radar view is always centred on the home (`include/MapLayer.h` 108-111;
  the rotation keeps distances), so the 5 and 10 mi views show only the inner 16.09 km. The builder may simplify each line at today's
  detail inside `r_f` = 17 km (10 mi plus the home's offset from the hundredths-rounded package centre, up to about 0.7 km) and at
  1.6 km beyond it. Nothing reads the header tolerance as a per-line promise: the device parses it (`BasemapPackage.h` 364) and reads
  it nowhere else; on the Worker side only `basemap-build.test.ts` asserts it and `basemap-grade-store.ts` classifies by it.
- **Its cost is at 25 mi:** the zoom ladder is {5, 10, 25, 50} below the configured radius (`include/RadarZoom.h` 26), and the 25 mi
  view shows the 17-40 km ring at 1.6 km (about 4.8 px). Its pictures add 25 mi.

**A defect in section 5a, found 16:53Z: HR-KLGA-E1 was NOT blind.** 5a called it "unmeasured". The build it describes (KLGA, the rule,
E1 forced at 129.6 km) was made at 20:29:35Z on 11-12 in the S08-F129 run: **48db330265e1**, rung 10, 173 rings, dropped.water 1. It is
staged as `frame-row-packages-hudson\KLGA-S08-F129.bmpk` and drawn in the owner's KLGA-rule picture; 5a was committed at 06:07Z on
11-13. Its prediction was therefore written with the result on disk. When the fixed list (option 3) runs, the build is re-made as an
anchor (predicted byte-identical, 48db330265e1) and 5a's rows are graded against it, LABELLED NOT BLIND; the one dropped ring is checked
against "every split inland body whole or wholly dropped".

**The three options, as map-server defined them and the orchestrator accepted them (16:56:01Z), with its conditions:**

| option | definition | conditions (the orchestrator, 16:56:01Z) |
|---|---|---|
| **1** | the rule, plus merging stacked roads, with lines NEVER coarser than today's (KBOS 0.4 km, New York 0.8 km; no E1 anywhere). Before simplifying, primary lines are taken longest first and any stretch lying within d = 150 m of a line already kept is removed | (b) it changes all 16 packages, so the road-appearance row covers all 16: before/after at 5/10/25/50/100 mi for every changed map, one contact sheet per map, before over after, the set not thinned; (c) **pre-registered branch: if the merge does not free enough at KEWR to fit at 0.8 km, option 1 is reported as "does not fit KEWR", with the bytes, and is never quietly coarsened**; (d) FT-HR keeps TODAY's KORD as the A/A control in both arms, and reads option 1's KORD as a changed map |
| **2** | the rule, plus E1 BEYOND r_f = 17 km only; lines inside 17 km at today's detail (distance-graded, no format change) | (a) before freezing: what `basemap-grade-store.ts` does with a package of mixed detail, whether it could mislabel one either way, and whether `basemap-build.test.ts`'s assertion changes and to what; if the header no longer describes the package, that is said, not recorded as harmless. **The row:** at 5 and 10 mi every road and river pixel equals today's and only water differs, with river LINES counted as line pixels, which must be equal. 25 mi is added to the pictures |
| **3** | the fixed list: the rule plus E1 at KEWR, MIDTOWN, KJFK and KLGA only; Boston on today's package (f21d705d8b8a, the rule not applied) | HR-KLGA-E1 graded as a measurement LABELLED NOT BLIND, never as a confirmed prediction; the rebuild must reproduce 48db330265e1 byte for byte; the dropped ring graded against "whole or wholly dropped"; (e) the New York corridor (the NJ Turnpike by Newark Airport) named from the source file's records BEFORE its band width is predicted, and measured at 5 mi as well as 10 |

**Pictures:** 5, 10, 25, 50 and 100 mi; Boston today, option 1 and option 2 (option 3 IS today's package at Boston, said in words); New
York today plus every map each option changes; drawn areas (the union) labelled. **FT-HR:** the manifest freezes 11-15 18:00Z on the
owner's pick; without one, option 3's staged set plus whichever of 1 and 2 fit at about 11 min a package, today's KORD first.

**Next:** each option's registration (predicted bytes and water per affected map, the corridor measure predicted), pushed here before
its build: option 3 first, then 2, then 1.

### 5d. The road-look instrument, and option 3 registered (before its rebuild and before any corridor reading)

**The instrument (A2.49's corridor measure), general form:** `hb-corridor-src.py` (c8fa0470cbb5) names a corridor FROM THE SOURCE:
TIGER 2024 PRISECROADS, primary (S1100) records, projected exactly as `frame.ts`'s `toFrameKm`. `hb-road-look.py` (24dfe8d4f326)
then reads a package: the corridor's REGION is its source centreline within B = 2.0 km inside the view's disc. A package primary line
belongs to the corridor if its piece in the region is at least 1 km long with at least 80 % of it within 1 km of the centreline (a road
CROSSING the corridor fails that). **Band width:** stations every 1 km along the longest merged centreline; at each, the spread of the
corridor lines' crossings of a 4 km perpendicular, in device px at that view. **Crossings:** proper point crossings among the distinct
pieces (overlaps and shared endpoints are not counted).
- **The corridors, named from the source:** KBOS: `Southeast Expy` (4 parts, 25.3 km inside 10 mi; the same pavement also carries
  I-93, US 1, Route 3 and the HOV lanes, which the measure counts as lines along it). New York: `New Jersey Tpke` by Newark Airport
  (40 parts, 134.2 km inside KEWR's 10 mi disc, 67.9 km inside its 5 mi disc; inside MIDTOWN's 10 and 5 mi discs: 113.1 and 16.5 km).
  The same pavement also carries `I- 95` (36 parts, 135.2 km inside KEWR's 10 mi disc). Corridor files: KBOS 8a58901ee714, KEWR
  78243f2f06cb, MIDTOWN 9f0c7d1a83a8.
- **Its check on a known pair** (Boston, today f21d705d8b8a vs (b) 7402f8cee0f5, read 16:59:42Z, stdout a373600405a1):

  | view | today: lines (distinct), band median / max px, crossings | (b) |
  |---|---|---|
  | 5 mi | 14 (12), 4.4 / 5.5, 122 | 12 (11), 11.6 / 13.3, 18 |
  | 10 mi | 26 (24), 2.0 / 2.7, 233 | 25 (24), 5.4 / 8.4, 54 |

  **Crossings fall as the band widens:** stacked lines at fine detail cross each other constantly within a pixel or two. The band
  width is the reading the eye sees; the crossing count is read beside it, as a direction.
- **A correction to section 5b's Q2 answer.** "12.2 and 16.3 px at 14-15 km south" came from `hb-kbos-road.py`'s axis-aligned box,
  which took in every primary line in it. Along the named Southeast Expressway (12.7 km of axis inside 10 mi), (b)'s band reads 5.4 px
  median and 8.4 px at most. The 4-8 px mid-corridor figure stands.

**Option 3 (the fixed list), registered.** Every package below was BUILT ON 11-12; those rows are labelled NOT BLIND and the rebuild
is a determinism check. The corridor rows and the KLGA dropped-ring row are blind: none has been read.

| row | input | predicted |
|---|---|---|
| **O3-ANCHOR** (NOT BLIND) | rebuilt on `pqh` + `sea-obj` e9ef04165ba2: KEWR (S08-E1c), MIDTOWN, KJFK and KLGA (S08-F129), KBOS (A0) | byte-identical: e5333b0c570a, af608ebcddb4, 227ecbddfdf7, **48db330265e1**, f21d705d8b8a |
| **O3-BYTES** (NOT BLIND) | non-terrain bytes against the 32,768 B cap | KEWR 32,744; MIDTOWN 32,764; KJFK 32,757; KLGA 32,756; KBOS 32,759 (today's) |
| **O3-WATER** (NOT BLIND) | rings kept and dropped.water | KEWR 130 kept, 36 dropped, record 27 kept; MIDTOWN 163, 10, rings 91 and 132 kept; KJFK 179, 0; KLGA 173, 1; KBOS today's 197, 45, its 4 split bodies still split (the rule is not applied there) |
| **HR-KLGA-E1** (5a, NOT BLIND) | the KLGA rebuild | 5a's rows, graded as a MEASUREMENT: 129.6 km; a rung from 8 to 10; dropped.water at most 106; kOk; at most 32,768; 0 proper crossings |
| **O3-KLGA-DROP** (blind) | KLGA's one dropped ring | it is the FIRST unit of KLGA's rule sequence (`seq-ii-0.8-plus.json`), a unit of ONE ring. "Every split inland body whole or wholly dropped" is graded on the units: every multi-ring unit has all of its rings in the package, or none |
| **O3-RL-SEE** | the Southeast Expressway, KBOS, today vs option 3 | equal to today's reading above (option 3 IS today's package at KBOS) |
| **O3-RL-NJT** (blind) | the NJ Turnpike: KEWR today dff28487eaf4 (lines at 0.80 km) vs option 3 e5333b0c570a (1.61 km); MIDTOWN today 20a22bed5510 vs option 3 af608ebcddb4 | **10 mi:** today's band median 1.5-6 px, max at most 10; option 3's median 3-12 px and at least 1.2x today's, max at most 20. **5 mi:** today's median 3-12 px; option 3's 6-24 px and at least 1.2x today's, max at most 40. **Crossings:** option 3's FEWER than today's in every reading |

**Outside the set:** an anchor byte differs: STOP (the prototype is not what was measured). A corridor reading outside its range: the
reading is recorded as OFF and reported, never re-ranged. A corridor with no lines in a view: instrument, stop. Anything else: stop and
decide.

**Option 3's pictures:** one contact sheet per changed map (KEWR, MIDTOWN, KJFK, KLGA): today's package over option 3's, at 5, 10,
25, 50 and 100 mi; drawn areas (the union) labelled. Boston has no option 3 sheet: **option 3 IS Boston's today package**.
**Option 3 and FT-HR:** its New York packages are already staged (`KEWR-S08-E1c`, `MIDTOWN-S08-F129`, `KJFK-S08-F129`,
`KLGA-S08-F129`); Boston's bytes do not change, so no KBOS reading is needed; KORD stays the A/A control.

**Option 3's run (17:07:39Z, stdout 48cfad2769a7): the package rows 27 of 27 AS PREDICTED; O3-RL-NJT OFF at its first row.**
- O3-ANCHOR (NOT BLIND): all five rebuilt byte-identical, KLGA = 48db330265e1. A control re-made today's four New York packages
  equal to FT-HR's run-10 column. O3-BYTES as listed, every package kOk with 0 proper crossings. O3-WATER as listed (KEWR record 27
  kept; MIDTOWN rings 91 and 132 kept; KBOS's 4 bodies still split). HR-KLGA-E1, graded as a measurement (NOT BLIND): 129.6 km,
  rung 10, dropped.water 1, kOk, 0 crossings, 32,756 B. O3-KLGA-DROP (blind): the dropped ring is candidate 133, the first unit of
  KLGA's sequence, a one-ring unit; no multi-ring unit is partial at any of the four New York maps.
- O3-RL-SEE as predicted (KBOS 5 mi: 14 (12) lines, 4.4 / 5.5 px, 122 crossings; 10 mi: 26 (24), 2.0 / 2.7 px, 233).
- **O3-RL-NJT OFF, on the record:** KEWR 10 mi today's band median 0.7 px, max 1.2, against 1.5-6 predicted. The run stopped there;
  no later corridor row was graded. **Two causes, both accepted by the orchestrator (17:09:24Z):** the prediction was wrong in kind
  (by Newark the Turnpike is straight, and a straight road keeps only its endpoints at either tolerance); and **the instrument was
  unsound**: v1's membership kept a piece only if 80 % of its LENGTH lay within 1 km of the centreline, so it excluded 36 pieces
  (73.7 km) of today's package and 48 (151.1 km) of option 3's, biasing the coarser package DOWN; the 20 pieces it kept in option 3
  were all byte-identical to pieces kept today. **Reach:** the 5d Boston check and section 5b's Q2 figures may understate (b).
- **Option 3's contact sheets** (`C:\Github\_handoffs\map-server\hudson-options\`, today over option 3 at 5/10/25/50/100 mi, not opened
  by this session): KEWR-opt3-f36f049f2ef0, MIDTOWN-opt3-c90b387cd7f4, KJFK-opt3-c5a7078761a2, KLGA-opt3-16e36628ddbe. Water
  lost to land: 0 px at every view of every map. Labels: each connected piece of water one package draws and the other does not, with
  its DRAWN area, pieces of 5 km2 or more where the label fits (hb-sheet.py 206d62c78bf5, hb-sheet-geo.py 04f67f378947).

### 5e. The corridor measure v2 and its four controls, then options 2 and 3's corridor rows, registered before any run

**The ruling (the orchestrator, 17:09:24Z):** replace the membership test; four controls before any re-run; then re-register every
corridor row. O3-RL-NJT stays OFF on the record; a v2 reading is a NEW row. **Retry cap:** this was the corridor instrument's first
failure; a second OFF on a re-registered corridor row goes to the owner, and so does a third instrument failure.

**v2 (`hb-road-look2.py` 4722a9c50b85):** membership by VERTICES. The builder's simplification selects source vertices and never moves
them (frame.ts RD25), so a line's vertices sit on the road at every tolerance and only its segments stray. A MEMBER is a package
primary line with at least 2 vertices inside the view, at least 80 % of them within 0.15 km of the NAMED source lines; every segment of
a member inside the view is read. Band width at stations every 0.25 km (v1: 1 km, coarse enough to miss one straying chord) along the
longest merged named centreline, across a perpendicular of half-length 3 km, in device px. Crossings: proper point crossings among the
distinct member pieces.

**The four controls (`hb-rl2-controls.py` 01fd40db9ef6, expected values in the file).** Package: Boston today (f21d705d8b8a); corridor
`Southeast Expy`; 10 mi. Plants from the longest named part inside the view, at its middle. PLANT-STRAY: 7 vertices on the part, 0.3 km
apart, the middle one moved 1.5 km along the normal. PLANT-PARALLEL: the same positions, each moved 0.5 km the other way, not a named
record.

| control | expected |
|---|---|
| **C1, the plant can catch the defect** | the OLD test (v1, B = 2 km) EXCLUDES PLANT-STRAY: its piece count and widths unchanged by the plant |
| **C2, the fix catches the plant** | v2 counts PLANT-STRAY (members + 1); the band widens to **6.5-14.5 px** at stations within 0.55 km of the plant's peak, and no station farther away changes |
| **C3, negative control** | v2 EXCLUDES PLANT-PARALLEL: members, every width and the crossings unchanged |
| **C4, Boston (b) under v2** | 10 mi: today's median 1.0-4.5 px, max at most 10; (b)'s median 4.5-14 px and at least 2x today's, max at most 26; (b) crosses less. 5 mi: today's median 2.0-9.0, max at most 20; (b)'s median 9.0-28 and at least 2x today's, max at most 52; (b) crosses less |

**Option 3's corridor rows, v2** (new rows; informed by v1's OFF reading of a straight Turnpike, so labelled INFORMED, not blind):

| row | input | predicted |
|---|---|---|
| **O3-RL2-SEE** | Southeast Expy, KBOS, today vs option 3, 5 and 10 mi | equal in every value (option 3 IS today's package at KBOS) |
| **O3-RL2-NJT** | New Jersey Tpke; KEWR today dff28487eaf4 vs option 3 e5333b0c570a; MIDTOWN today 20a22bed5510 vs option 3 af608ebcddb4 | KEWR 10 mi: today's median 0.2-3 px, max at most 10; option 3's median 0.2-6, max at most 20. KEWR 5 mi: today 0.4-6 / at most 20; option 3 0.4-12 / at most 40. MIDTOWN 10 mi: today 0.2-4 / at most 12; option 3 0.5-8 / at most 24. MIDTOWN 5 mi: today 0.4-8 / at most 24; option 3 0.5-16 / at most 48. In every reading option 3's max at least today's, and option 3's crossings at most today's |

**Option 2's corridor rows, v2:** **O2-RL2**: at 5 and 10 mi, at KBOS (Southeast Expy), KEWR and MIDTOWN (New Jersey Tpke), every value
(members, distinct pieces, every station's width, crossings) EQUAL to today's. That holds by construction (section 5c), so a
difference is a defect in the option 2 build. **Option 1's** corridor rows are registered with option 1, before its run.

**Outside the set:** a control off: STOP, the instrument is not used (the second instrument failure under the cap goes to the owner
only at the third). A corridor row outside its range: OFF on the record, reported; a second OFF goes to the owner. Anything else: stop
and decide.

### 5f. Option 2 registered (detail graded by distance), before its build

**The prototype:** `pqo` = `pqh` plus one knob, `Q4O_GRADE` = "<fine multiple of eps>:<r_f km>", set on the E1 (eps x 8) line set only:
`frame.ts` d80ff53f48dd (`simplifyGraded`, its counters `GRADE_N`), `layers.ts` 9416caab048e, `build.ts` 5c2f72491505; every other
file byte-identical to `pqh`'s. Each run is simplified at the fine eps exactly as today's line set does it; each stretch of fine
segments that all stay farther than r_f from the centre is simplified again at 8 eps, a span accepted only if its segment also stays
outside r_f; a stretch whose result still comes inside r_f keeps its fine vertices (counted as a fallback). So every segment within
r_f is one of today's.

**The builds:** KBOS: the rule + E1 forced at 162 km + `Q4O_GRADE=2:17` (today's 0.40 km inside); KJFK and MIDTOWN: the rule + E1
forced at 129.6 km + `4:17` (today's 0.80 km inside); KEWR: the rule + E1 as at KEWR (`c`) + `4:17`. **KLGA under option 2 is (b)'s
KLGA, e7f0af9d9fba** (the rule as is, no E1), NOT BLIND: section 3 measured it at **-284 / +163 km2 of visible water against run 10**
(a visible loss the owner has not yet seen priced in this option's terms; its sheet shows it).

**Condition (a), the header and its two readers, answered before the build:**
- The header's `toleranceMm` will read **1,609,344 (8 eps)**, the coarsest detail in the package. It is TRUE AS A BOUND (no line strays
  further from its source) and FALSE AS A DESCRIPTION: inside 17 km the lines are at 0.40 km (KBOS) or 0.80 km (New York). **The field
  no longer describes the package.** A reader that takes it as "the line detail" calls the inner map 2-4x coarser than it is.
- `basemap-grade-store.ts` classifies `toleranceMm !== epsMm || dropped > 0 ? "D" : ...`. A graded package reads "D"; so do today's
  packages at these points (headers 2 eps / 4 eps, water dropped). No label changes in either direction there, but only because the
  class is coarse: it cannot tell a graded package from a uniformly coarse one.
- `basemap-build.test.ts`'s assertions (MB1-MB8b) are on synthetic stores at rungs up to 3 (`toleranceMm` 100,584 or 201,168); none
  reaches E1, so none changes. The code PR, if option 2 is picked, adds a case that asserts the header = 8 eps AND, separately, that
  every segment inside r_f is within the fine eps: the header assertion alone would pass a uniformly coarse package.

**The estimator** (`hb-o2-estimate.py` 82aff085122e, from built packages: today's minus E1's primary-road and river vertices within
17.3 km, times the E1 package's bytes per line vertex; the room from `margin-e1.json`):

| point | extra bytes, estimate | the E1 package's room | so |
|---|---|---|---|
| KBOS | 483 | 4,917 B of secondary roads still kept (rung 9) | secondaries absorb it; water untouched |
| KJFK | 125 | 595 B of secondaries (rung 9) | the same |
| MIDTOWN | 256 | rung 10, no secondaries left; 514 B before a split inland body drops | more single rings drop; no body |
| KEWR | 245 | rung 10, no secondaries left; **50 B** before a split inland body drops | **the 2-ring 8.0 km2 unit 39 (x -86..-65 km, far west, not the Hudson) drops, whole** |

**Rows (blind unless labelled):**

| row | input | predicted |
|---|---|---|
| **O2-ANCHOR** (control) | `pqo` with `Q4O_GRADE` unset, the same four E1 configs | byte-identical to `pqh`'s: KBOS 7402f8cee0f5, KJFK 227ecbddfdf7, MIDTOWN af608ebcddb4, KEWR e5333b0c570a |
| **O2-PKG** | the four graded builds | radius 162 / 129.6 km as today; header `toleranceMm` 1,609,344; kOk; 0 proper crossings; non-terrain bytes 32,600-32,768 |
| **O2-WATER** | rung, rings kept, dropped.water | KBOS rung 9, 242 kept, 0 dropped (its 4 split bodies whole); KJFK rung 9, 179, 0; MIDTOWN rung 10, dropped.water 11-40, rings 91 and 132 kept, no multi-ring unit dropped or partial; KEWR rung 10, dropped.water 38-70, record 27 kept, unit 39 dropped WHOLE, no multi-ring unit partial |
| **O2-EXTRA** (the estimator's check) | primary-road and river line bytes, option 2 minus the E1 package | KBOS 240-725 B; KEWR 120-370; MIDTOWN 130-385; KJFK 60-190 |
| **O2-PIX** (the row) | the sheets' unannotated renders, today vs option 2 | at 5 and 10 mi, the set of road and river pixels EQUAL to today's at all four (river LINES count as line pixels); secondaries drawn in neither; every water ring today draws is drawn |
| **O2-RL2** | section 5e | every corridor value equal to today's |
| **O2-GRADE** (beside, a count) | `GRADE_N` | at least one stretch coarsened at each point; fallbacks counted and reported |

**Pictures:** today over option 2 at 5/10/25/50/100 mi: KBOS, KEWR, MIDTOWN, KJFK, and KLGA (today over e7f0af9d9fba). **FT-HR:**
option 2 needs re-staging (the four graded packages; KLGA's e7f0af9d9fba is staged as `KLGA-S08`).

**Outside the set:** O2-ANCHOR off: STOP (the copy is not the prototype). O2-PIX off: STOP (a defect in the build, by construction).
Any other row off: OFF on the record, reported. Anything else: stop and decide.

**Option 2-L (the orchestrator, 17:21:30Z: register and build it beside option 2).** Option 2 with **KLGA also taking E1 beyond 17 km
only** (the rule + E1 forced at 129.6 km + `4:17`, the same configuration as KJFK and MIDTOWN). Options 2 and 2-L differ only at KLGA.
The estimator (`hb-o2-estimate.py` bf527f8a0a67, KLGA added): about **261 B** of extra line bytes against the E1 package's rung 10 with
no secondaries left and **570 B** before a split inland body drops.

**What option 2 does to KLGA, named from the source** (correcting map-server's 17:20:58Z answer to the orchestrator, which called it
all inland; `hb-whole-drops.py` b68961da9848, names by `hb-water-names.py` 855ca11c89e9 from TIGER 2024 AREAWATER's FULLNAME, the
polygon nearest each body's point; output ab8ee5c82903). (b)'s KLGA, which is option 2's, drops **25 bodies WHOLE** compared with
today, **283.6 km2 drawn**: none is a sea-mask ring, but by name about 221 km2 are rivers (**Hudson Riv** 80.6 km2 near 41.64 N 73.94 W
and 7.8 km2 near 41.03 N 73.90 W; **Delaware Riv** 46.5 and 29.4 km2; an unnamed 26.7 km2 polygon by the Delaware near 40.82 N 75.10 W;
**Housatonic Riv** 20.0 and 9.9 km2) and about 58 km2 are coastal bay pieces (Barnegat Bay 28.3, Sandy Hook Bay 7.2, Lower New York Bay
6.7, Great Peconic Bay 4.0, Shinnecock Canal 3.9, a 3.0 km2 piece named Atlantic Ocean, and smaller ones). **Why:** today's ladder drops
the 116 smallest rings (none above 18.8 km2); the rule never drops a split body's piece alone, so those pieces stay, and at 0.8 km lines
the ladder reaches unit 92 of 103 and drops whole bodies and large single rings instead. **Option 3 drops no body whole compared with
today** at any of the four New York maps (0 at each; Boston is today's package).

**The rows, extended (blind unless labelled):**

| row | input | predicted |
|---|---|---|
| **O2-ANCHOR**, KLGA | `pqo` knob unset, KLGA forced E1 at 129.6 km | 48db330265e1 (`pqh`'s) |
| **O2-PKG**, KLGA (2-L) | the graded KLGA build | 129.6 km; header 1,609,344; kOk; 0 proper crossings; 32,600-32,768 B |
| **O2-WATER**, KLGA (2-L) | rung, dropped.water, bodies | rung 10; dropped.water 2-30; no multi-ring unit dropped or partial; **the four multi-ring bodies (b) drops whole (units 81, 82, 87, 88: the Hudson by Poughkeepsie and three reaches of the Delaware) all kept** |
| **O2-EXTRA**, KLGA (2-L) | as above | 130-390 B |
| **O2-PIX**, KLGA (2-L) | as above | at 5 and 10 mi, road and river pixels EQUAL to today's; secondaries drawn in neither; every ring today draws is drawn |
| **O2-WHOLE** (the orchestrator's caption row) | bodies dropped WHOLE compared with today, per map | KBOS 0; KJFK 0; MIDTOWN 0; KLGA under 2-L 0; KEWR 0, or only unit 39 if today draws a ring of it; KLGA under option 2 (= (b)'s, NOT BLIND) 25 bodies, 283.6 km2 |

**Every option's sheet caption carries its list of bodies dropped whole compared with today, named from the source** (`hb-sheet.py`
2fec6e252125), coordinates at 2 decimal places. Sheets: KBOS, KEWR, MIDTOWN, KJFK under option 2; KLGA twice (option 2 = (b)'s KLGA;
option 2-L). Run script `hr-o2.sh` caf57498135a, grader `hb-o2-grade.py` f3afdadfbe73, driver `hb-drive.py` 7ff093b1d92b.

### 5g. Options 2 and 2-L read; the v2 controls stopped (instrument failure #2); v2.1 and its controls frozen before any run

**Options 2 and 2-L (17:33:57Z, `hr-o2.sh` caf57498135a): 46 of 46 AS PREDICTED.**
- O2-ANCHOR: `pqo` with the knob unset rebuilt all five E1 packages byte-identical to `pqh`'s.
- The graded packages, all kOk with 0 proper crossings and the header at 1,609,344:

  | map | package | rung | rings kept / dropped.water | non-terrain B | extra line bytes (estimate range) |
  |---|---|---|---|---|---|
  | KBOS | 8676f6bd0df6 | 9 | 242 / 0 | 32,755 | 418 (240-725) |
  | KJFK | 63d5ce513c7d | 9 | 179 / 0 | 32,758 | 112 (60-190) |
  | MIDTOWN | c7d812acf174 | 10 | 144 / 29 | 32,762 | 309 (130-385) |
  | KEWR | e7f15057f238 | 10 | 123 / 43 | 32,754 | 243 (120-370) |
  | KLGA (2-L) | 51caf64fd419 | 10 | 155 / 19 | 32,766 | 294 (130-390) |

- O2-WATER: no multi-ring unit partial anywhere; KEWR's unit 39 dropped whole (today draws none of it); KLGA under 2-L keeps the
  four bodies (b) drops (the Hudson by Poughkeepsie and three reaches of the Delaware); MIDTOWN keeps rings 91 and 132.
- **O2-PIX: at 5 and 10 mi the road and river pixels are IDENTICAL to today's at all five maps**; secondaries drawn in neither; every
  ring today draws is drawn. O2-GRADE: every stretch coarsened, **0 fallbacks** at every point.
- **O2-WHOLE: no body dropped whole compared with today** at KBOS, KJFK, MIDTOWN, KEWR, or KLGA under 2-L; KLGA under option 2 ((b)'s
  KLGA): 25 bodies, 283.6 km2 (section 5f).
- Sheets (`hudson-options\`, today over the option, the whole-drop list in each caption, not opened by this session): KBOS-opt2
  5850e6edc43c, KEWR-opt2 1dafadfc66fa, MIDTOWN-opt2 90345d675b9b, KJFK-opt2 a1b2ed7ba963, KLGA-opt2 3a142c0232ae ((b)'s KLGA),
  KLGA-opt2L 86d294552615. No corridor number is claimed for any option yet.

**The v2 controls (17:32:44Z, `hb-rl2-controls.py` 01fd40db9ef6, stdout c0a8acc88e01): C1 AS EXPECTED; C2's first row AS EXPECTED
(members 12 -> 13); C2's second row OFF, on the record:** the band widened at 3 stations near the plant, none farther, but at most
6.0 px against 6.5-14.5. **Two causes (the orchestrator, 17:34:40Z, counted as the corridor instrument's FAILURE #2; a third goes to
the owner):** the plant's normal was taken on a local kink, 12 degrees off square, so its tip sat about 0.32 km along the road (legs
1.60 km, not 1.53); and **fixed 0.25 km stations can step past a narrow stray's peak**, which a real straying chord is.

**v2.1 (`hb-road-look21.py` ec48f8cfb943):** v2 plus a station at the projection of every member vertex inside the view and at every
axis vertex (where the axis kinks, a segment's distance to it is not linear). **The median is read on the regular 0.25 km stations only
(v2's meaning); the max on all stations.**

**Its controls (`hb-rl21-controls.py` 95157c7c68f2, expected values in the file).** Plants in the AXIS frame: the normal from the axis
direction over s0 +- 0.3 km; **the square check printed and enforced before anything is read** (the tip projects within 0.01 km of s0
and lies 1.5 +- 0.01 km out). **Site:** of the multiples of 0.25 km (each exactly midway between two regular stations) on a straight
stretch (under 2 degrees over +-0.9 km), the one nearest the axis's middle where, unplanted, the members straddle the axis at every
regular station within 1 km (so a plant's on-axis parts add no width).

| control | expected |
|---|---|
| **C1** (stands) | the OLD test EXCLUDES PLANT-STRAY: pieces and widths unchanged |
| **C2** (re-registered) | v2.1 counts PLANT-STRAY (members + 1); **the station at the tip's projection reads 11.0-14.5 px**; regular stations more than 0.4 km from the tip unchanged |
| **C2b** (new: the sampling fix) | PLANT-NARROW (legs within +-0.1 km, the tip midway between two regular stations): **v2 UNDER-reads it, every regular station and its max unchanged**; **v2.1 reads its tip at 11.0-14.5 px**, its regular stations unchanged |
| **C3** (stands) | PLANT-PARALLEL EXCLUDED; every v2.1 value unchanged |
| **C4** (re-read under v2.1, ranges frozen here) | 10 mi: today's median 1.0-4.5 px, max at most 10; (b)'s median 4.5-14 and at least 2x today's, max at most 26; (b) crosses less. 5 mi: today's median 2.0-9.0, max at most 20; (b)'s median 9.0-28 and at least 2x today's, max at most 52; (b) crosses less |

**The corridor rows registered in 5e (O3-RL2-SEE, O3-RL2-NJT, O2-RL2) are read with v2.1:** median on the regular stations, max on all
stations; their ranges stand as registered. **Any OFF in a control stops the run, and that third instrument failure goes to the owner.**

### 5h. The v2.1 controls refused to build: the corridor instrument's FAILURE #3; the measure is parked and goes to the owner

**The run (17:39:05Z, `hb-rl21-controls.py` 95157c7c68f2, stdout e0e91ef507ad):** "no straight stretch where the members straddle the
axis (3 straight) -- the control cannot be built". Nothing was planted or read. **Why** (read-only, after the refusal): the axis is one
carriageway's SOURCE line and today's simplified package lines run beside it, not around it: of 51 regular stations the members
straddle the axis at 9, lie wholly on the + side at 29 and wholly on the - side at 13. The site rule assumed the opposite; the
precondition caught it before it could produce a false OFF.

**The orchestrator (17:40:16Z):** counted as the corridor instrument's **FAILURE #3** (a precondition that refuses to build is the
instrument unable to measure, the class of BLIND under the 11-04 rule; consistent with uk-data's (A) the same afternoon), "and it goes
to the owner". **All corridor work stopped.** Two options go to the owner, the orchestrator recommending the first: **(i) park the
corridor number** -- he decides on pictures, his own rule; options 2 and 2-L are pixel-identical to today at 5 and 10 mi anyway; or
**(ii) one more attempt** with the design below. **Until he answers, the standing road-appearance row (A2.49) is PICTURES ONLY, with no
corridor number.** No corridor reading is claimed for any option.

**The proposal for (ii), written down and NOT run** (draft `hb-rl21b-controls.py` 567eabc8a838, never run):
- **Plants as SHORT COPIES of a real member line M** (the member passing nearest the axis at s0): M's own vertices within +-0.9 km of the
  point of M nearest the axis at s0, new base vertices inserted ON M's segments at +-w along M (w = 0.3 km for PLANT-STRAY, 0.1 km for
  PLANT-NARROW), and one tip 1.5 km out along the axis normal at s0 (the normal from the axis direction over s0 +- 0.3 km). Away from
  the spike the copy lies exactly on M, so its crossings land on M's own offsets: every station away from the tip is EXACTLY unchanged,
  wherever it is sited, with no straddling assumption.
- **Short, not whole, for C1's sake:** a full-length copy would pass the OLD test (its long on-road length outweighs the spike), so C1
  could not show the defect; and C1 and C2 must use the same plant (the ruling of 17:09:24Z).
- **Built and checked before anything is read, each printed:** square (the tip projects within 0.01 km of s0 and lies 1.5 +- 0.01 km
  out); a v2.1 member by its own vertices; and, for C1, its length within 1 km of the centreline under 80 %.
- **Site:** of the multiples of 0.25 km on a straight stretch, the one nearest the axis's middle where a member passes within 0.15 km
  of the axis.
- **The tip's expected width is RELATIVE:** 1.5 km minus the unplanted members' lowest offset at the tip's station (read from the
  unplanted geometry first), +-0.05 km, with a sanity range of 10.0-14.5 px (the frozen 11.0-14.5 assumed straddling).
- C1, C2's member count and "unchanged" rows, C2b, C3 and C4 as frozen in 5g otherwise.

### 5i. The owner picks option 2-L; his 17 km check, answered from the code, and its proof registered before it runs

**The owner (18:16Z 2026-11-13, relayed verbatim by the orchestrator at 18:17:57Z):**

> "3. Hudson: option 2-L. I've looked at all ten pictures.
>    - KLGA 2-L: +386 km2, nothing dropped, 5/10 mi identical to today.
>    - Option 3 adds 21 km2 more but makes 5/10 mi roads straight and doubled.
>    - Plain option 2 at KLGA drops 25 bodies, the Hudson among them.
>    One check before it ships: is the 17 km measured from the point the screen centres on? If it's from the package centre, an off-centre home's 10 mi view can reach the coarse lines. Option 1 isn't needed for the Hudson; bring it back later as a byte saving if it earns one.
> 4. Park the road-width measure. Line changes are decided on 5/10/25/50/100 mi pictures (S8)."

**OPTION: 2-L** (pending the check below). Option 1 is shelved for the Hudson. The road-width measure is PARKED by the owner; S8 and
A2.49 are pictures at 5/10/25/50/100 mi. The code PR carries condition (a) (graded detail distinguishable in the header or the grader,
plus an MB case).

**1. The point r_f is measured from: the PACKAGE CENTRE,** the frame origin (`simplifyGraded` measures from (0, 0) of
`makeFrame(q.lat0Micro, q.lon0Micro, ...)`). **Where the guarantee comes from:**
- the device keeps its home at 4 dp and sends the basemap fetch `lat`/`lon` = `wireloc::Center(home)` (`AircraftManager.cpp`
  3524-3525 set `req->lat/lon` from the home; 2959-2960 send them), i.e. **rounded to 2 dp, half away from zero**
  (`include/WireLocation.h` `Round2`, `Center`);
- the Worker quantises again with `Math.round(x * 100) / 100` (`proxy/src/wx/crop.ts` `quantise`), a no-op on a 2-dp value, and
  keys the package by those 2-dp strings (`query.ts` `queryKey`);
- **so a package is shared only by the homes of one 0.01 deg cell, each within 0.005 deg of its centre on each axis**; no package is
  ever shared by homes further apart.

**2. The bound:** the worst home-to-centre distance is the cell's corner, 0.005 deg on each axis: **0.697 km at 40.7 N, 0.690 km at
42.36 N, and 0.785 km at the equator (the fleet-wide maximum)**, all under r_f minus 10 mi = 17 - 16.093 = **0.907 km**.

**4. Other paths that show more than 16.093 km from the home "at the 10 zoom":**
- **Corners: none.** Every shipping panel is round (`include/variants/s3_128.h` 79, `s3_146.h` 51, `s3_175_amoled.h` 61,
  `s3_21.h` 77: "round; square bounding box"), and the map's in-view test is a distance from the centre (`MapLayer.h` 147).
- **Pan: none.** The zoom is a view scale about the home (`RadarZoom.h`); nothing moves the centre.
- **NAUTICAL MILES: YES.** The radius unit can be `nmi` (`ConfigurationWebServer.cpp` 735), and the zoom steps {5, 10, 25, 50} are in
  the user's unit (`RadarZoom.h` 26). **The 10 nmi step is 18.52 km, past 17 km**: a nautical-mile user's 10 step reaches the coarse
  lines in its outer ring.
- **A CONFIGURED RADIUS between about 10.1 and 25 mi (16.2-40 km): YES.** The ladder's top step is the configured radius itself
  (`RadarZoom.h` `BuildLadder`), so such a user's DEFAULT view shows coarse lines beyond 17 km, as every 25 mi view does by design.
- **The draw call is not on main yet** (the map's draw path is map-client's): the view centre is the home only if that code projects
  about the same `lat`/`lon` the fetch rounds. Named as a coupling for map-client to confirm, not verified here.

**A remedy for the owner, priced (`hb-o2-estimate.py` 7cbe453af9d2, the same estimator):** r_f = 19.4 km (10 nmi + the fleet-wide
0.785 km + margin) costs about 45-117 B more per map than 17 km (KBOS 600 B vs 483, KEWR 290 vs 245, MIDTOWN 360 vs 256, KJFK 180 vs
125, KLGA 316 vs 261); every map but KEWR keeps it inside the room measured in 5f (KEWR, already past its 50 B, drops more single rings,
no body). Not built; the owner's call.

**3. The proof on a build, registered here before it runs (`hb-offset-check.py` 6bd76dc2f9bd).** The package for any home of a cell is
the cell centre's package (the wire carries only the 2-dp centre), so the 2-L packages already built ARE the packages for the worst
homes. For each of the five maps, the four worst homes of its cell (the 4-dp corners that still round to the centre), the 5 and 10 mi
and 5 and 10 nmi views are drawn centred on that HOME, today's package and 2-L's, and the road-and-river pixel sets compared:

| view | reach from the package centre | predicted |
|---|---|---|
| **5 mi and 10 mi** | at most 16.093 + 0.697 = 16.79 km, under 17 | **EQUAL at every corner of every map** (40 comparisons); secondaries drawn in neither |
| **5 nmi** | at most 9.26 + 0.70 km | EQUAL at every corner of every map |
| **10 nmi** | 18.52 + up to 0.70 km, past 17 | **DIFFER at every corner of every map** (lines lie in the 17-19 km ring at all five) |

**Outside the set:** a 5 or 10 mi difference: STOP, and it goes to the owner (the orchestrator, 18:17:57Z). A 10 nmi comparison that
reads EQUAL somewhere: reported, not re-ranged. Anything else: stop and decide.

**The proof's run (18:23:23Z, `hb-offset-check.py` 6bd76dc2f9bd, stdout 69820cefa8c0): AS PREDICTED in every row** (accepted by the
orchestrator, 18:24:34Z).
- **5 mi and 10 mi: road and river pixels EQUAL at all 20 worst homes** (4 corners x 5 maps; 40 comparisons); the furthest reach from
  a package centre 16.790 km; secondaries drawn in neither.
- **5 nmi: EQUAL at all 20.**
- **10 nmi: DIFFERS at all 20**, by 2 to 238 px (KLGA 2-23 px; KBOS 123-238).
- The largest home-to-centre offset over these cells: 0.697 km.

**What goes to the owner (the orchestrator, recommending YES):** r_f = 19.4 km, covering the 10 nmi step, about 45-117 B per map. If he
picks it: re-register, rebuild, re-stage and re-freeze FT-HR's manifest before 11-15 18:00Z.

**The coupling, answered by map-client (relayed after 18:27:06Z):** the draw (#659 5f13cc21, `AircraftManager.cpp` 3823-3824) centres
on the stored home at full precision, and places the package by its HEADER origin shifted by origin - home (`MapLayer.h` `AffineFor`
103-124). **So the bound holds only if the header origin is exactly the 2-dp request point. CONFIRMED from the builder, read-only:**
- shipping (main 287dc5ece2bc): `handler.ts` 145 parses the request; `query.ts` 62-63 sets `lat0Micro = Math.round(c.lat * 100) *
  10,000` from `crop.ts` `quantise` (28, `Math.round(x * 100) / 100`), the identity on a 2-dp value; `handler.ts` 92 passes it to the
  build worker and `worker.ts` 43-46 passes it through unchanged; `build.ts` 63 REFUSES an origin that is not a two-decimal point;
  82's coverage test only tests; **196 writes the header with `q.lat0Micro`/`q.lon0Micro` exactly.** No snap to a grid, tile or store
  cell. The same at #696's builder (`build.ts` 106, 266) and in the prototype the packages came from (`pqo/src/basemap/query.ts` 62-63,
  `pqo/src/wx/crop.ts` 28, `pqo/src/basemap/build.ts` 71, 237).
- **The built packages' headers** (bytes 8-15, read from the staged files): KBOS-2L 42,360,000 / -71,010,000 for the request "42.36" /
  "-71.01"; KJFK-2L 40,640,000 / -73,780,000; MIDTOWN-2L 40,750,000 / -73,990,000; KEWR-2L 40,690,000 / -74,170,000; KLGA-2L 40,780,000 /
  -73,870,000: **each EQUAL to its request centre x 10^6.** So the origin never moves, and nothing adds to the 0.785 km bound.

**FT-HR re-staged** (`C:\Github\_handoffs\map-server\frame-row-packages-hudson\`, sha256 read back after the copies): KBOS-2L
8676f6bd0df6, KJFK-2L 63d5ce513c7d, MIDTOWN-2L c7d812acf174, KEWR-2L e7f15057f238, KLGA-2L 51caf64fd419, and today's KORD-A0
cb59bb06a506 as the A/A control. The manifest names these six unless the owner picks r_f = 19.4 km.

### 5j. The owner picks r_f = 19.4 km; 2-L re-registered at 19.4 before the rebuild

**The owner (18:25Z 2026-11-13, relayed verbatim by the orchestrator at 18:33:52Z):** "1. Hudson: 19.4 km. Covers the 10 nmi step for
45-117 bytes a map; the few tiny Newark lakes are fine. For the bench, add one 25 km picture each for KLGA and KBOS. Boards set to km
show the coarser lines in that view's outer ring (about 8 px). It doesn't block the run, but I want to see it."

**OPTION: 2-L at r_f = 19.4 km.** Why 19.4: the 10 nmi step (18.52 km) plus the fleet-wide worst home-to-centre offset (0.785 km) is
19.305 km. The same prototype (`pqo`, its three files checked against 5f's hashes as a control), the knob `Q4O_GRADE` = 2:19.4 (KBOS)
and 4:19.4 (KEWR, MIDTOWN, KJFK, KLGA); driver `hb-drive.py` fd6adefe4357 (configs O2-F162-r194, O2-F129-r194, O2-E1c-r194), run
script `hr-o2-194.sh` cd79a3462dc3, grader `hb-o2-194-grade.py` 00c8c742bcfa (expected values in it), offset proof
`hb-offset-check194.py` 9144bccf2c16, sheets `hb-sheet2.py` dc15a505206a (hb-sheet.py plus km views).

**The estimate** (`hb-o2-estimate.py` 7cbe453af9d2 at 19.4 km; measured/estimated at 17 km ran 0.87-1.21): extra line bytes KBOS 600,
KEWR 290, MIDTOWN 360, KJFK 180, KLGA 316, against the room in 5f: KBOS 4,917 B and KJFK 595 B of secondaries; MIDTOWN 514 and KLGA 570
B before a split inland body drops (of which 309 and 294 B were spent at 17 km); KEWR past its 50 B already (unit 39 dropped at 17 km).

| row | predicted |
|---|---|
| **control** | `pqo` frame/layers/build = d80ff53f48dd / 9416caab048e / 5c2f72491505 |
| **O2R-PKG** | KBOS 162 km, the four New York maps 129.6 km; header 1,609,344; kOk; 0 proper crossings; non-terrain bytes 32,600-32,768 |
| **O2R-WATER** | KBOS rung 9, 242 kept, 0 dropped; KJFK rung 9, 179, 0; MIDTOWN rung 10, dropped.water 25-50, rings 91 and 132 kept, no multi-ring unit dropped or partial; KLGA rung 10, dropped.water 19-35, no multi-ring unit dropped or partial, (b)'s four bodies kept; **KEWR rung 10, dropped.water 43-60 (its few extra small lakes, accepted by the owner), record 27 kept, no multi-ring unit partial, none dropped but unit 39** |
| **O2R-EXTRA** | line bytes minus the E1 package: KBOS 300-900, KEWR 145-435, MIDTOWN 180-540, KJFK 90-270, KLGA 160-475 |
| **O2R-PIX** | at 5 and 10 mi (centred on the package centre) road and river pixels EQUAL to today's at all five; secondaries in neither; every ring today draws is drawn |
| **O2R-WHOLE** | no body dropped whole compared with today, at all five |
| **O2R-GRADE** | rfKm 19.4; at least one stretch coarsened at each |
| **O2R-OFFSET** | the 20 worst homes (as 5i), centred on the HOME: **5 mi, 10 mi, 5 nmi and 10 nmi all EQUAL** (80 comparisons; the furthest reach 18.52 + 0.697 = 19.217 km, under 19.4) |

**Then:** re-stage the five for FT-HR (sha256 read back; today's KORD-A0 control unchanged) and re-freeze the manifest before 11-15
18:00Z. **The owner's two 25 km pictures** (KLGA and KBOS, today over 2-L at 19.4, one 25 km view each; hash-named in `hudson-options\`):
informational, no gate. **Outside the set:** any O2R row off: STOP and report; the offset proof's any DIFFER: STOP, to the owner.

**The run at 19.4 (18:59Z, `hr-o2-194.sh` cd79a3462dc3, stdout 0c3fa376c71d): 42 of 42 AS PREDICTED.** The control: `pqo` unchanged.

| map | package (2-L at 19.4) | rung | rings / dropped.water | non-terrain B | extra line B |
|---|---|---|---|---|---|
| KBOS | 696c6d00122d | 9 | 242 / 0 | 32,754 | 607 |
| KJFK | e042d61aa8f1 | 9 | 179 / 0 | 32,765 | 151 |
| MIDTOWN | aaede1785364 | 10 | 142 / 31 | 32,760 | 345 |
| KEWR | c619f4a89f14 | 10 | 120 / 46 | 32,766 | 299 |
| KLGA | 6ab44cd8fbea | 10 | 151 / 23 | 32,765 | 350 |

All kOk, 0 proper crossings, header 1,609,344. MIDTOWN keeps rings 91 and 132; KEWR keeps record 27, its only multi-ring drop unit
39 (today draws none of it), the rest the small lakes the owner accepted; KLGA keeps (b)'s four bodies; **no body dropped whole
compared with today at any of the five.** At 5 and 10 mi road and river pixels IDENTICAL to today's at all five; 0 fallbacks.
**The offset proof at 19.4: 80 of 80 EQUAL** (the 20 worst homes x 5 mi, 10 mi, 5 nmi, 10 nmi; the 10 nmi step now reaches at most
19.217 km).

**FT-HR re-staged** (`C:\Github\_handoffs\map-server\frame-row-packages-hudson\`, full sha256 read back after the copies):
KBOS-2L194 696c6d00122d, KJFK-2L194 e042d61aa8f1, MIDTOWN-2L194 aaede1785364, KEWR-2L194 c619f4a89f14, KLGA-2L194 6ab44cd8fbea, and
today's KORD-A0 cb59bb06a506 unchanged. **The manifest names these six** (to be re-frozen before 11-15 18:00Z). The older staged
files are untouched.

**Pictures** (`hudson-options\`, not opened by this session): the owner's two 25 km views, informational -- KBOS-2L194-25km
f0cac58dae58 (about 170-200 road pixels and 11 river pixels move, all beyond 19.4 km; water +15 px) and KLGA-2L194-25km ab944a9282de
(about 130-150 road pixels move, beyond 19.4 km; water +1,213 px); the five sheets at 5/10/25/50/100 mi -- KBOS-2L194 bbba14e983fa,
KEWR-2L194 48efc61020cf, MIDTOWN-2L194 c5e8359531ba, KJFK-2L194 da8fb0e97024, KLGA-2L194 c754074cd3a9.

**What still gates the merge:** FT-HR's reading inside M on these six (the 11-16 window), and the code PR's condition (a).

## 6. This PR's CI, predicted

A docs-only change on main 4b00c97b3dbd. The only workflow change since the docs-only shape was read (main 099f1946) is #669's one-line
install change inside an existing job, which adds no job and no trigger. **32 check runs: 30 success, 2 skipped (`promote`, `version`),
0 failed**, as #652's. The tested base is read from the run's own checkout log. A new red from `ship-alert` reads INSTRUMENT, and the
GitHub-side re-run rule applies. Anything else: stop and report.
- **Read at 1b26029cac0c:** 32 = 30 / 2 / 0 as predicted, tested on "Merge 1b26029cac0c... into 4b00c97b3dbd...".
- **Re-predicted for the FT-HR commit** (a new head, so a new run): the same, **32 = 30 / 2 / 0**, tested on the merge into main as it
  then stands (5cdc8bd8f35d at this writing: #652's docs merge, which changes no workflow).
- **Read at 3623990b5331:** 32 = 30 / 2 / 0 as predicted, tested on "Merge 3623990b5331... into 5cdc8bd8f35d...".
- **Re-predicted for the 3.08 ms amendment's head:** the same, **32 = 30 / 2 / 0**, tested on main as it then stands (eed76e5ec332 at
  this writing). One workflow landed on main since 5cdc8bd8: #671's `account-budget.yml`, which runs on a pull request ONLY when it
  touches the budget's own paths (docs/account-budget.*, scripts/account_budget.py, tools/budget/**, the workflow itself). This PR
  touches none, so it adds no check run here. Any further workflow change before the run starts is read first.
- **Read at 1f4d05928:** 32 = 30 / 2 / 0 as predicted, tested on "Merge 1f4d05928... into eed76e5ec332...".
- **7c5e2872's push (section 5a) carried NO CI prediction: an omission, recorded here.** It read **33 = 31 / 2 / 0**, tested on
  "Merge 7c5e2872... into b7e7fdd99884...". The 33rd run is `build (blipscope-s3-128-cardbench)`, a firmware matrix leg that #674
  (c8140f8a) added on main before that base.
- **Predicted for this head (section 5b):** main is aa40aa43d8c9 at this writing. Its workflow changes since b7e7fdd99884 add STEPS
  only: three in `device-ids.yml`'s existing job (the wrangler-call check, its selftest, the helper's selftest) and steps in
  `workers.yml`, which runs no job for this PR's paths (none of the last three runs has a workers job). So **33 check runs: 31
  success, 2 skipped (`promote`, `version`), 0 failed**, tested on the merge into main as it then stands, read from the run's own
  checkout log. A new red from `ship-alert` reads INSTRUMENT. Any further workflow change before the run is read first; anything else:
  stop and report.
- **Read at 5e24c9e233e9:** 33 = 31 / 2 / 0 as predicted (stable at 17:05:53Z), tested on "Merge 5e24c9e233e9... into aa40aa43d8c9...".
- **Read at e8cbe2fe709f:** 33 = 31 / 2 / 0 as predicted (stable at 17:30:58Z), tested on "Merge e8cbe2fe709f... into 86077c7fe9f2...".
- **4af70320e13f got no CI run:** its push returned a GitHub-side 500 after updating the ref; the PR stayed at e8cbe2fe until the next ordinary push re-synced it.
- **Read at a6b1291416ab:** 33 = 31 / 2 / 0 as predicted (stable at 17:54:17Z), tested on "Merge a6b1291416ab... into b0c9412f71a8...".
- **Read at 815148dd52b4:** 33 = 31 / 2 / 0 as predicted (stable at 18:17:55Z), tested on "Merge 815148dd52b4... into eb35add21c03...".
- **Read at f476dfc49842:** 33 = 31 / 2 / 0 as predicted (stable at 18:57:51Z), tested on "Merge f476dfc49842... into 287dc5ece2bc...".
- **Predicted for the section 5i-5j head** (docs/basemap-hudson-rule.md and docs/basemap-route.md; no workflow's path filter names
  either: `workers.yml` filters on proxy/, dashboard/ and the wx fixtures, `account-budget.yml` on its own paths): **33 = 31 / 2 / 0**,
  on main as it then stands (9d0a148b44db at this writing).

## 7. Where the measurements are (scratch, sha256 first 12)

- The prototype: `pqh` = `pq4d` plus `hudson.ts` 726454fd61ff, `budget.ts` be866547da10, `build.ts` d996b441aa60; `pq4d` unchanged.
- The rule's measurement: expectations hb-expect.txt 8f3465bda734 (R-CUT, E1/E2), hb-expect-ii.txt fe3c860de0a8 ((ii)), hb-expect-ibody.txt
  a2f26d5b1f24 (parity); estimator hb-ii.py 5210fea2165a; runs hb-out/S08.json f39f5bb9dc5b, S07 b50c86bb2e5e, S09 5a0e9602d114,
  S08-E1c a6042baae18a, S08-F129 41a6b5653824; margins margin.json 94612890ed51, margin-e1.json b7665a4b225e.
- (B)'s measurement: cand-src.mts a62145777bce, hb-src.py 84df9bc548b4, hb-src-flips.py ef58c3dc06a5.
- The raster: hb-raster2.py fdc7bc2db6fd; the parity run hb-parity.py d50b1fcee522.
- The owner's pictures (in the hand-off folder, opened only by him): KEWR-E1-vs-E2 b68198ccb84e, MIDTOWN-rule bf673465abd0, KJFK-rule
  824d0f672584, KLGA-rule 21cba0040b84, KBOS-before-after 03a9944ce204.
- HR-KBOS-E1 (section 5b): the driver hb-drive.py 7f7f2af97ae0 (configs S08-F162, A0-anc and S08-anc added; the others unchanged),
  hr-kbos.sh 3fdadc6dc87a, the grader hr-kbos-grade.mts 0d824a983cd8; outputs hb-out/S08-F162.json 8cc6f560453d, A0-anc.json
  5c08fb69d59d, S08-anc.json 2695f32edc89; the run's stdout 76c91301a590.
- Boston's pictures (section 5b): hb-kbos-pics.py c63a4a580b0a (stdout 8a9ad19aedf2), the pixel breakdown hb-kbos-pxcheck.py
  19a136092cf4, the road measure hb-kbos-road.py 3655d636f598 (stdout b77c7c9a0d72); the package dumps KBOS-A0-anc.bmpk.full.json
  4f0336de03e2 (equal to KBOS-A0's) and KBOS-S08-F162.bmpk.full.json b5740246b773. The files: KBOS-today f5214e3dbaed, KBOS-rule
  8b3b2d19d695.
