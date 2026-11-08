# Why the map has no water, and what to do about it (predictions first)

**A proposal for review. Nothing here is ruled.** The reading below was frozen at `31b7887db9d5` before it ran.

## The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-07 ~18:19Z)

> The water finding goes first in the ocean proposal: a map of Boston or Chicago with no water isn't one Daniel would ship. Bring
> options with predictions, at least: secondary roads dropped before water; water simplified harder rather than dropped, keeping the
> largest bodies; and what a larger cap would cost the device. Before ranking them, read how much of the missing water is TIGER's
> coverage rather than the ladder.

## The finding

All seven builds in the combined slot read 0 bytes of water (`docs/basemap-dense.md`). The format's ladder, ruled as Q26, drops water
rings before secondary roads, and rung 3 always has none.

**Water can go missing in two different places:**
- **TIGER never had it.** AREAWATER covers a coastal band, not open water.
- **The ladder dropped it.** TIGER has it, and the budget removed it.

Only the second can be fixed by changing the ladder. The first needs a mask.

## The reading, registered before it runs

**What it reads, for each airport, on a 480 × 480 grid over the disc, at the slot's parameters (r 97, ε 100,584, all six layers):**

| name | what it is | how it is read |
|---|---|---|
| **Wtrue** | the disc's real water | Natural Earth 10 m: not land, or a lake. Downloaded for this read; committed nowhere |
| **Wtiger** | what the store holds | `build()` with a cap large enough that the ladder never runs, then `decode()` |
| **Wpkg** | what ships today | `build()` at today's cap, then `decode()`. A 422 ships nothing |
| **Missed** | the share of Wtrue that Wtiger doesn't cover, in cells | the two grids compared |

**The airports:**
- **coast:** KBOS, KJFK, KMIA, KSFO, KSEA;
- **Great Lakes:** KORD, KDTW;
- **inland lakes:** KMSP;
- **controls with little water:** KIND, KJAC.

**The builds read the sealed store** `4c0875738f38`, through the real `build()` over a local copy of the objects it asks for. Nothing is
written to R2.

**A check of the instrument, before any airport:** a planted square ring of known size must rasterise within 1% of its true area.

**What each result will mean:**

| reading, at the coast and Great Lakes airports | meaning | what it does to the options |
|---|---|---|
| **Missed at most 25%** | it is mostly the ladder | a ladder option alone can bring the water back |
| **Missed 25-60%** | both | a ladder change and the mask are both needed |
| **Missed at least 60%** | it is mostly TIGER | no ladder option restores open water, so the mask carries it |
| **The airports split** | it depends on the place | the proposal says which, and the mask covers the rest |

**My expectation, before reading:** Boston, New York and Miami in the middle band, San Francisco and Seattle lower, and Chicago and Detroit
highest. Lake Michigan is so large that I expect TIGER to hold only its edge.

**Outside the set, each a stop and a report, with no ranking:**
- **Wtiger exceeds Wtrue by more than 10 points at any airport.** The builder drops holes, so islands count as water, and Natural Earth is
  coarse. Either could do this, and then the two grids can't be compared.
- **Wpkg above 0 at a rung-3 airport.** That contradicts the ladder as read.
- **KIND or KJAC above 5% Wtrue.** Then the truth grid is wrong.
- **The planted ring off by more than 1%,** or **anything else not in this table.**

## The reading (2026-11-07, on store `4c0875738f38`): the airports split

**As registered, with no stop.** The planted ring read 0.00% off. No Wtiger exceeded Wtrue by 10 points (the most was KSEA's 3.0), no rung-3
airport held water, and KIND and KJAC read under 5%.

| airport | Wtrue | Wtiger | Missed | Wpkg | rung |
|---|---|---|---|---|---|
| **KBOS** | 35.3% | 15.8% | **60.0%** | 0.0% | 3 |
| **KJFK** | 41.2% | 14.4% | **68.6%** | none: a 422 | - |
| **KMIA** | 49.2% | 10.6% | **81.0%** | 10.6% | 1 |
| **KSFO** | 45.8% | 10.8% | **78.1%** | 6.4% | 2 |
| **KSEA** | 9.5% | 12.5% | **6.7%** | 9.6% | 2 |
| **KORD** | 29.5% | 30.3% | **0.4%** | 0.0% | 3 |
| **KDTW** | 19.5% | 9.5% | **56.5%** | 0.0% | 3 |
| KMSP | 0.0% | 2.3% | 0.0% | 0.0% | 3 |
| KIND | 0.0% | 0.3% | 0.0% | 0.0% | 3 |
| KJAC | 1.2% | 1.7% | 35.7% | 1.7% | 0 |

**What it means: two separate problems.**
- **The open sea was never in the data.** At KBOS, KJFK, KMIA and KSFO, TIGER misses 60-81% of the real water. No ladder change can restore
  that. It needs a mask.
- **Lakes and sounds are in the data, and the ladder drops them.** TIGER holds Lake Michigan almost whole (0.4% missed), and the package
  ships none of it: 30% of the disc gone. KSEA is the same shape. A ladder change alone fixes these.
- **Boston has both:** the ladder loses TIGER's 15.8 points, and TIGER never had about 21.
- **KDTW's 56.5%** is probably the Canadian side, since TIGER stops at the border. I haven't separated that from Lake Erie's open water.

**My expectation was wrong.** I expected Chicago and Detroit to be the worst for TIGER, and Chicago turned out the best. It stays on record.

**Limits of the reading.** Natural Earth is coarse, which is why KSEA's Wtiger reads above its Wtrue. The builder drops holes, so islands count as
water in Wtiger (at most 3.7 points of the disc). Neither changes the split.

## The options, through the real builder

**Each ran through `build()` on a scratch copy of the source, over the sealed store,** so these are real packages, with water read off the
grid. Each cell is water in the disc, then the secondary roads kept. KEWR is not here: 6 of its objects weren't in the local copy.

| airport | today | A: secondary roads first | B: water at ε×8 | **A+B** | cap 40 KB | cap 48 KB |
|---|---|---|---|---|---|---|
| **KBOS** | 0.0%, 264 | 15.4%, 0 | 0.0%, 264 | **15.5%, 116** | 0.0%, 585 | 0.0%, 1,022 |
| **KJFK** | 422 | 422 | 422 | **422** | 0.0%, 158 | 0.0%, 515 |
| **KLGA** (ε 100,585) | 422 | 422 | 422 | **422** | 0.0%, 96 | 0.0%, 396 |
| **KMIA** | 10.6%, 909 | same | same | **same** | 10.6%, 909 | 10.6%, 909 |
| **KSFO** | 6.4%, 1,182 | 10.8%, 318 | 9.6%, 1,182 | **10.5%, 965** | 10.7%, 1,182 | 10.8%, 1,182 |
| **KSEA** | 9.6%, 990 | 12.6%, 0 | 12.4%, 990 | **12.8%, 740** | 12.2%, 990 | 12.6%, 990 |
| **KORD** | 0.0%, 1,027 | 30.3%, 474 | 0.0%, 1,027 | **30.3%, 841** | 0.0%, 1,741 | 0.0%, 2,509 |
| **KDTW** | 0.0%, 1,002 | 9.5%, 402 | 0.0%, 1,002 | **9.3%, 820** | 1.1%, 1,756 | 9.5%, 1,756 |
| **KMSP** | 0.0%, 489 | 1.8%, 0 | 0.0%, 489 | **2.0%, 211** | 0.0%, 1,186 | 0.1%, 1,949 |
| **KIND** | 0.0%, 1,193 | 0.3%, 791 | 0.0%, 1,193 | **0.4%, 1,072** | 0.0%, 1,940 | 0.3%, 2,583 |
| **KJAC** | 1.7%, 189 | same | same | **same** | 1.7%, 189 | 1.7%, 189 |

- **A alone** brings the water back, but at KBOS, KSEA and KMSP it costs every secondary road.
- **B alone** does nothing where the water doesn't fit even simplified, because the ladder still drops it: KBOS, KORD, KDTW, KMSP.
- **A+B** simplifies water to ε×8 first, then drops secondary roads, then water. It keeps nearly all of TIGER's water at today's cap, and
  more secondary roads than A everywhere.
- **New York stays 422 under all three.** All its water is already gone at the last rung, and it is still 1.5-3.7 KB over. That is the dense-area
  proposal's job. At 40 KB, A+B fits there: KJFK 14.1% and 27 roads, KLGA 13.8% and none.

**What a larger cap costs, and buys.**

| cap | non-terrain | a package | vertices to draw, KORD | water at KORD with today's ladder |
|---|---|---|---|---|
| 32 KB (today) | 32 KB | about 214 KB | 5,825 | 0.0% |
| 40 KB | +8 KB | about 222 KB (+4%) | 7,405 (+27%) | 0.0% |
| 48 KB | +16 KB | about 230 KB (+7%) | 8,967 (+54%) | 0.0% |
| 64 KB | +32 KB | about 246 KB (+15%) | 12,615 (+117%) | 30.2% |

- **Storage and download barely move,** because terrain is most of the package.
- **The real cost is drawing.** Vertices are what a device draws every frame, and the map's frame cost has never been measured on the 1.28" board.
- **Device memory:** the validator enforces no size cap. Weatherscope's rasteriser needs a scratch of about 32 B per vertex, so 12,615 vertices
  is about 400 KB. The Blipscope has PSRAM, and the map client will say what it needs.
- **The cap is the builder's, so a raise changes packages for every client of the route.**
- **Today's ladder needs about 64 KB to bring Chicago's water back.** Changing the order costs nothing and does it at 32 KB.

**The sea edge is cheap.** Natural Earth's coastline, clipped to the square and simplified, costs **1.3-2.1 KB at ε×2 and 0.6-1.1 KB at ε×8.** Inland it
costs nothing.

## The proposal

**1. Change the ladder to A+B:** ε×2, then water at ε×8, then secondary roads shortest first, then water smallest first. This replaces the order
ruled as Q26, so it needs a ruling.

**2. Add a sea mask from Natural Earth, as ordinary water rings.**
- **It needs no format change and no new client code:** a ring is a ring, and the device already draws water.
- **It comes from** Natural Earth's land, plus its lakes for the far side of the border. Land is already an ingest input. Lakes would be new.
- **The hard part is islands.** The format has no holes, so Long Island or Martha's Vineyard needs a ring with a cut in it, or the sea would cover
  them. The design has to read clean on glass.
- **It needs a store change,** so a new store id, and every stored package rebuilds. Class A and the re-ingest's figures are stated before it runs.
- **It costs 0.6-2.1 KB of the cap,** taken from secondary roads.

**3. Keep the cap at 32 KB.** A larger one is the fallback for New York only if the dense-area work can't close 5-11%, since it is the one option that
adds drawing cost.

**The ranking:**

| | option | water | cost |
|---|---|---|---|
| **1** | A+B with the sea mask | lakes, sounds and sea | a ruling on the order; a store change |
| **2** | A+B alone | lakes and sounds; **no open sea** | a ruling on the order |
| **3** | A alone | lakes and sounds | every secondary road at some airports |
| **4** | a larger cap | needs about 64 KB for Chicago | more than double the vertices |
| **5** | B alone | little | it leaves Boston and Chicago at 0% |

## Before any code

**A check that can fail.** Natural Earth can't grade a mask built from Natural Earth: it would pass by construction. The mask's row reads the store's
own terrain instead, where the sea is at or below 0 m, at the coast airports, with a planted wrong ring as its control. The exact rows are
frozen with the code.

**Peak memory:** as ruled ~03:23Z, each build's peak is stated against the isolate's 128 MB. A new sea read adds to it.

**This changes what a package holds,** so once ruled it goes to the map client before its code depends on it: more water rings, fewer secondary roads,
and no format change. S1's road rule still holds. Weatherscope reads the same route, so the reviewer should say whether it takes the same ladder.

## Open, for the reviewer

- **Q-W1:** change the ladder's order to A+B, replacing Q26?
- **Q-W2:** the sea mask from Natural Earth, as ordinary water rings? It would then land with the ladder change in one store change, not two.
- **Q-W3:** do Weatherscope's route packages take the same ladder?
- **Q-W4:** the dense-area proposal also changes this ladder, so should the two be one PR? I recommend one, because they touch the same lines and the
  same driver sets.

**CI for this PR (docs-only), predicted: 32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.** The baseline is #606's
finished docs-only run (32 = 29/3), which postdates `caf63cc9`, the last change to the set of jobs.

## The re-read at the map client's query (registered 2026-11-08, before it runs)

**The ruling** (reviewer chat, pasted by Daniel, ~19:08Z): Q-W1's A+B "replaces Q26 once its table is re-read at r=161", predictions first.
**The query** (~02:33Z), and every build below uses exactly it:

`r=162 & eps=201168 & layers=water,rivers,roads,places,terrain & levels=41239440:246,81808320:244,162275520:242`

That means no peaks, an ε twice the earlier reads', and a disc 2.79 times the area of r=97.

**The airports:** the earlier ten, plus KLGA, KEWR and Midtown, so New York is read at this query too. All thirteen are read the same way as
before: the coverage split (Wtrue, Wtiger, Missed, Wpkg), then the options through the real builder at today's 32 KB cap. That is today's ladder,
A, B, and A+B, with B's water at 8 × the query's ε. Caps of 40 and 48 KB are reported, not graded, for the map client's step M. New York's
ladder figures under today's order are reported for the dense-area proposal.

**What each result will mean:**

| reading | meaning | what follows |
|---|---|---|
| **A+B fits at 32 KB everywhere except New York,** and keeps at least 80% of Wtiger's water where TIGER has it (KBOS, KORD, KSEA, KSFO, KMIA, KDTW) | the r=97 result holds at the client's query | A+B replaces Q26, as ruled |
| **A+B is a 422 at an airport outside New York** | the dense-area work reaches beyond New York at this radius | that airport, and how far over, go into the dense-area proposal with New York's |
| **A+B fits but keeps under 80% of Wtiger's water** somewhere | ε×8 isn't enough water simplification at this radius | ε×16 is compared before anything freezes |
| **The coverage split** | Missed is read again: at least 50% at the four sea airports, and under 10% at KORD | the sea mask stays the fix for the sea |

**My expectation, before reading:**
- **New York** stays a 422 under both orders, over the cap by more than at r=97 (1.5-3.7 KB then).
- **KBOS and KORD** are the most likely to join it, because 2.79 times the area brings in many more primary roads.
- **The rest fit** under A+B.
- **The coverage split** looks like r=97's.

**Outside the set, each a stop and a report:**
- an uncapped build that fails, or isn't rung 0;
- objects missing from the local copy;
- Wtiger above Wtrue by more than 10 points;
- **A+B keeping less water than today's order at any airport,** which contradicts the mechanism;
- the planted ring off by more than 1%;
- anything else not in this table.
