# v20: the airport label yields to traffic text (registration)

**Status: REGISTERED 2026-11-14; every prediction below is frozen before any code.** Owner: the 5c session.
**Ruled 2026-11-14 (orchestrator):** Decision 1 = **Z-a**; Decision 2 = airport labels stay **out** of the counter.
Registered by Nov 16, PR by Nov 19, go/no-go Nov 21 (else v21). Content freeze is Nov 23-26.

> **RE-FREEZE, 2026-11-14, before the graded run (accepted by the orchestrator).** It was found while deriving the host
> test, before the test existed.
> - **The row replaced:** "AFTER, dropped: > 0 only in the two-box population" was wrong, and was frozen without
>   being derived. A single 42 px label whose contact sits just left of the mark intersects BOTH candidate boxes, so
>   one-contact scenes drop too. A contact right on the mark also drops the label, because its symbol (+-6 px)
>   touches both candidates.
> - **The row that replaces it:** an IDENTITY, derived in the test independently of the code, for every scene. RIGHT
>   iff the right box is clear; LEFT iff right is blocked and left is clear; DROPPED iff both are blocked.
> - **A loose sentence corrected:** at an EXACT zero offset the airport and aircraft text boxes only share an edge
>   (strict, so no collision). Picture B's overlap comes from a small vertical offset; the "> 0" row holds through
>   the other offsets.
> - **Scope addendum (ruled in):** the traffic text the counter does not hold is also an obstacle, in Z-a's separate
>   list, recorded at its own drawString: the emergency descriptor (with its marker dot), LOOK UP, and the watchlist
>   and pinned callsigns. Rings and trails stay non-obstacles.
> - **Two rows added:** AL16, "B on the left blocks the left candidate in every scene, so left is 0 there by
>   construction", and AL17, "with B on the right, LEFT is used". The LEFT branch is exercised with two contacts.

## The defect

The owner, 2026-11-13: the aircraft-type label sitting on the Bend airport label is "fine to show as 'not new in
v19'", but is logged as a v20 fix (label collision near the centre).

Evidence: v19 picture B (sha prefix f6f97101; the v18 picture f55c8cdc shows the same). The NEAR B739's label is
printed over the BDN airport label at the centre of the airfield preset.

The code, read at main 0c3341d3:

- `DrawAirports` (`src/AircraftManager.cpp`, the call at line 4179, the body at 5391) draws each visible airport's
  mark and then its code at `(x+5, y-3)`, blind, BEFORE the traffic pass.
- The traffic pass prints each aircraft's label where `LabelLayout` puts it, `(x+5, y+5)` for a contact at the mark.
- The overlap counter (`include/OverlapCount.h`; `overlapFrame`, reset at line 4164) RECORDS label boxes and badges.
  It avoids nothing, and airport labels are not in it.

So the collision is systematic wherever traffic sits on an airport mark: the airfield preset (the radar centred on the
field, so NEAR is at the centre) and every device sited at an airport. It is worst on the 240 px face. It is the same
in v18, so it is not new in v19.

## The fix (the scope accepted 2026-11-13)

- **F1, marks unchanged:** every visible airport's mark is still drawn first, under the traffic.
- **F2, labels after the traffic:** each visible airport's LABEL is placed after the traffic pass, against the
  rectangles that pass recorded. Those come from the calls that drew them (`AircraftLabelBox` and the `badge` lambda),
  never re-derived. The order is: try RIGHT `(x+5, y-3)`; else LEFT (the label's right edge 5 px left of the mark, same
  baseline); else DROP the label. **The mark always stays.**
- **F3, a pure function:** the placement is `airportlabel::Place` in a new `include/AirportLabel.h`, host-tested.
  `overlapcount::Frame` gains one read-only query, `Hits(Rect)` (any recorded rectangle intersects it). It adds no
  rectangle and changes no count.
- **F4, once per frame:** the placement is decided on the first band and reused by every band. Every SKU renders in
  one band today; the rule holds for the hook anyway.
- **F5, out of scope:** aircraft-on-aircraft overlap (#310's declutter) and airport-on-airport overlap.

### Decision 1, z-order (for the reviewer)

Placing labels after the traffic pass means a grey airport label is drawn ABOVE blips, trails and the Follow track,
where today it sits beneath them.

- **Z-a (recommended):** each drawn aircraft SYMBOL is an obstacle too. Its box is recorded at its own draw call
  (`DrawAircraftTriangle` returns the extent it drew), into a separate list, so the overlap counter is unchanged. The
  label then yields to the aircraft as well, and never covers a blip. Trails and the track are lines that already cross
  the label today; they are not obstacles.
- **Z-b:** text obstacles only, exactly as scoped. A label may then be drawn over a blip it touches.

The rows below are frozen for **Z-a**. Where Z-b would differ, the row says so.

**RULED: Z-a.** A label landing on a blip defeats the point, which is to keep traffic readable. **The one visible change,
named for the go/no-go package:** airport labels now draw ABOVE trails and the Follow track line, where before they sat
beneath. Row (b) shows it on glass.

### Decision 2, the overlap counter (row c)

**Recommended: airport labels stay OUT of the counter's pairs and area.** The counter is #310's measure of traffic text
on traffic text, and its series must stay comparable across versions. The fix's own property, that no airport label
lands on traffic text, is asserted on the host instead. `Hits()` only reads the frame.

## Rows, frozen

**(a) The host sweep**, `test/host/test_airport_label.cpp`. A population, not one case:

- The airport mark is at the screen centre (the airfield preset's case), on the 240 px face and on the 960 web face.
- One obstacle population: an aircraft label box (the two- and three-line sizes `LabelLayout` makes at the device font,
  with w x h stated in the test) at every offset (dx, dy) in [-48, 48] x [-32, 32], step 2 px, from the mark. Under Z-a
  the aircraft symbol box is added at the contact's position.
- A second population: two aircraft boxes, one on each side of the mark.
- The badge sizes (NEW, NEAR) in the same sweep.

| count | predicted |
|---|---|
| BEFORE (the old rule: always right, blind), over-text | **> 0**. The sweep includes picture B's geometry: a contact at the mark puts its label at (x+5, y+5), under BDN's (x+5, y-3) |
| AFTER, over-text | **0, exactly** |
| AFTER, right + left + dropped | the number of scenes |
| AFTER, dropped | > 0 only in the two-box population |
| CONTROL, no obstacle | every label RIGHT: 0 left, 0 dropped |

**Red first:** the over-text == 0 assertion runs against the old rule first and must read red, printing its count. The
fix then makes it green.

**Sabotages:**
- **S-AL-BLIND** (placement ignores obstacles): the AFTER over-text row goes red.
- **S-AL-LEFT** (no left candidate): the "left > 0" row goes red; over-text stays 0.
- **S-AL-SYM** (Z-a's symbol obstacles off): the symbol-overlap row goes red. Under Z-b this row does not exist.

**(b) Pictures, before (main) and after.** The web renderer's airfield preset at 960 and a 240 px render of picture B's
scene (the NEAR B739 at the BDN mark):
- before: BDN's label overlaps the B739 label;
- after: BDN's label reads to the LEFT of its mark, or is dropped; the mark is in place; the B739 label is clear.
- Each picture is checked by eye AND by a pixel count: pixels of the airport-label colour inside the B739 label box,
  before > 0, after 0.
- **A third scene (ruled):** an airport label crossed by an aircraft TRAIL. Before, the trail draws over the label; after,
  the label draws over the trail, with its text intact. This is the z-order change, judged on glass.

**(c)** As Decision 2, **ruled**: the counter keeps airport labels out, so #310's series stays comparable.

**(d) Frame time.**
- **Bench A/B/A on the Blipscope bench board (preferred, ruled).** The board is booked through Nov 16. A window on
  Nov 17-18 is requested from the orchestrator once the code is ready, and booked with its wake.
- A host timing of `Place()` over the sweep is the fallback, only if no window fits.
- The bound per frame: visible airports x 2 candidates x up to 192 recorded rectangles, plus the symbol list.
- Predicted: no change outside the A/B/A noise band on the device; under 1 microsecond per airport on the host.

## Readings (the code PR)

**Row (a), read 2026-11-14 against the re-frozen rows above.** `test/host/test_airport_label.cpp`, AL1-AL17, all green.
Faces 240 and 960 read the same counts (AL15), so one line per population:

| population | scenes | right | left | dropped | AFTER over-text | AFTER over-symbol | BEFORE over-text |
|---|---|---|---|---|---|---|---|
| one contact | 6,468 | 4,228 | 1,072 | 1,168 | **0** | **0** | 1,890 |
| two contacts, B on the left | 1,617 | 1,155 | 0 | 462 | **0** | **0** | 348 |
| two contacts, B on the right | 1,617 | 0 | 1,275 | 342 | **0** | **0** | 1,617 |

The identity row (AL11) reads 0 misses in every population. **Red first:** against the old rule, exactly AL9, AL10,
AL11, AL13 and AL14 read BAD. **"Left 0" with B on the left** is by construction, not a gap: B's boxes cover the left
candidate in every one of those scenes (AL16, asserted per scene). The B-on-the-right population is where the LEFT branch
is used with two contacts (AL17).

**Sabotages,** `test/host/sabotage_airport_label.py`: each plant reads exactly its frozen red set, and the real files are
byte-identical before and after. It has the `--anchors` and `--control` modes `test/host/run.sh` requires of every driver.

| plant | red |
|---|---|
| S-AL-BLIND | AL9, AL10, AL11, AL13, AL14, AL17 |
| S-AL-LEFT | AL11, AL13, AL17 |
| S-AL-SYM | AL10, AL11 |
| S-AL-FRAME (`Frame::Hits` ignores a dropped rectangle) | AL5 |
| S-AL-OVF (an overflowed obstacle list is ignored) | AL6 |

**Rows (b) and (d)** are read after this PR's code is fixed, not before: (b) from the web renderer's build of this
branch, (d) in a bench window on Nov 17-18.

## Outside the set

Each of these stops the work and goes to the reviewer:

- the sweep's BEFORE over-text reads 0: the population misses the defect, so the instrument is wrong;
- AFTER over-text > 0;
- a picture in which a mark moves or disappears;
- a frame time outside the noise band.
