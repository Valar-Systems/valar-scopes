# Why the map has no water, and what to do about it (predictions first)

**In progress.** This freezes the reading that comes before any option is ranked. The options follow it.

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
