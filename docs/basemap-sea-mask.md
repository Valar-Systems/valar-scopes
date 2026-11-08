# The sea mask: islands by keyhole bridges (a design, predictions first)

**A design for review. No code exists.** It answers Q-W2 (ruled ~19:08Z): "a Natural Earth sea mask as ordinary rings, in the same store
change. For islands, try keyhole bridges: each island cut into its sea ring with a zero-width bridge, so no format change is needed. Bring
that with predictions." The water reading behind it is `docs/basemap-water.md`.

## Why a bridge is needed, and why it can be invisible

**Islands can't be drawn without one.** The format has no holes: "each ring is an independent filled area"
(`docs/basemap-package-format.md` §3). Weatherscope's `FillRing` (`src/weatherscope/WxBasemap.h:77-80`) fills each ring on its own, so an
island sent as its own ring would be filled as more water.

**A bridge can have zero width under the rule devices already use.**
- **The rule:** a pixel is filled when its centre is inside the ring by even-odd. An edge counts at a scanline when `(y1 > py) != (y2 > py)`, and
  the centre lies in `[x_a, x_b)` between paired crossings.
- **What a bridge is:** two edges on the same points, run in opposite directions. Any scanline that meets it meets both edges at the same `x`.
  They cancel, and no pixel changes.
- **It survives quantisation.** A bridge is built after quantising, from vertices the ring already has, so its two edges are the same integers.

**The validator accepts such a ring.** `bmpk::Validate` checks counts, ranges and lengths, but not whether a ring is simple
(`include/BasemapPackage.h`), so a ring that touches itself along a bridge validates.

## The design

1. **The sea:** Natural Earth 10 m ocean, plus its lakes above a size threshold. The lakes are for the far side of the border, where TIGER stops,
   such as KDTW's Lake Erie. Inside the US, TIGER's water stays as it is, and overlaps are harmless: both draw in the water colour.
2. **Clip each ring to the build square** with the same Sutherland–Hodgman the water layer uses. An island crossing the square's edge becomes a
   hole touching it, and even-odd still reads it as land.
3. **Drop islands under 1.5 km²,** the same threshold TIGER water uses, so they draw as sea.
4. **Simplify, then quantise, then bridge,** in that order. Bridging first would let simplification move one side of a bridge, giving it width.
5. **Bridging:**
   - **Each hole, largest first,** joins the ring through its nearest visible vertex. A candidate is tried only if the segment crosses no edge of
     the ring or of any other hole.
   - **The bridge is the two vertices, each repeated.** That adds 2 vertices per island.
   - **Both sides use vertices already on the ring,** so no new point is made.
6. **The rings join today's water section** and follow the ladder like other water. A sea ring is large, so it goes last in a
   smallest-first drop.
7. **The store gains a sea layer,** read per cell like the others. That is the store change ruled with A+B, so there's a new store id and every
   package rebuilds.

## Checks, registered before any code

| row | check | predicted |
|---|---|---|
| **KB1** | a planted sea square with 3 islands, one touching the square's edge. The bridged ring is rasterised with Weatherscope's `FillRing` rule, against a reference raster of the polygon with holes, at 240 × 240, for 3 view radii (r, r/2, r/4) and 8 rotations | **0 differing pixels** in all 24. **Control:** the islands sent as their own rings differ by at least each island's area |
| **KB2** | every bridge segment, in every real package below | **0 crossings** of any other edge, except at its own endpoints |
| **KB3** | KBOS, KJFK, KLGA, KSEA, KSFO and KMIA at the map client's query | `validate` **kOk**; islands kept and vertices added, reported; **sea bytes at most 3 KB** each |
| **KB4** | **an independent truth,** since Natural Earth can't grade a mask built from Natural Earth: the store's own terrain, where the sea is at or below 0 m | at each coast airport, **at least 95%** of the mask's pixels read at or below 0 m, and the mask covers **at least 90%** of the 0 m cells outside TIGER's water. **Control:** the mask moved 2 km inland must fail at least one |
| **KB5** | **the device's own code:** Weatherscope's C++ `FillRing` on the host, drawing KJFK's package | **0 differing pixels** against KB1's reference renderer over the sea |
| **KB6** | glass, by the map client | no visible seam at KJFK and KBOS, at the default view and with `radar-up` rotated |

| plant | what it does | predicted failing set |
|---|---|---|
| **S-KB-NOBRIDGE** | the islands sent as their own rings | **{KB1, KB5}** |
| **S-KB-CROSS** | the nearest vertex taken without the crossing test | **{KB2}** |
| **S-KB-ORDER** | bridges built before simplifying | **{KB1}**: simplification moves one side |

**Anything else: stop and report.**

**What could go wrong:**
- **A renderer with another fill rule.** Anti-aliasing, or a different edge rule, could show the bridge as a hairline. KB5 covers
  Weatherscope's code. The Blipscope's renderer is the map client's, so KB6 is its check, and KB1's rule is the contract.
- **An island too complex for a clean bridge.** If none of an island's vertices can see the ring without crossing an edge, the builder drops that
  island and counts it in `PROVENANCE`, rather than shipping a bad ring.

## What it changes for the map client

**More water rings, some with bridges, and no format change.** Its renderer must use the centre rule above, as KB1 states it. This goes to the
map client before its code depends on it.

**CI for this PR (docs-only), predicted: 32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.** The baseline is
#606's finished docs-only run (32 = 29/3), which postdates `caf63cc9`, the last change to the set of jobs.
