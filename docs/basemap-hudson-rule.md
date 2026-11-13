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
- **Predicted for the section 5c head** (docs/basemap-hudson-rule.md and docs/basemap-route.md; no workflow's path filter names
  either: `workers.yml` filters on proxy/, dashboard/ and the wx fixtures, `account-budget.yml` on its own paths): **33 = 31 / 2 / 0**,
  on main as it then stands (aa40aa43d8c9 at this writing).

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
