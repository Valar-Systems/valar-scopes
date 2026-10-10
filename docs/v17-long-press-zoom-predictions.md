# Long-press zoom: predictions, frozen before any code (2026-10-09)

Spec: [v17-long-press-zoom.md](v17-long-press-zoom.md). This file is frozen before the first line of
code; its sha256 is recorded in the PR. Anything learned later goes in a dated addendum below the
line, never above it. **COM18 only** (COM6 and COM15 untouched). It replaces the double-tap
predictions (`2ca34591bdb2`), dropped with the double-tap design before any run.

Instruments (ship): `[card] open t=<ms>` on the first frame that draws a card; `[hold] ring`,
`[hold] rejoin after <ms>`, `[hold] cancel held=<ms>`, `[zoom] hold -> <step>`.

## On COM18, with Daniel, on live traffic (after the #385 acceptance run)

- **(a) Quick-tap latency is unchanged.** 30 taps on aircraft with the instrumented image BEFORE the
  long press (the `[card] open` line only), and 30 AFTER. **Median and p95 of release to `[card]
  open` each within one frame (50 ms) of before.**
- **(b) A long press over a crowded centre toggles zoom at least 19 of 20 times and opens no card.**
  20 holds at the centre with several blips within the tap radius. Counted: `[zoom] hold` lines >=
  19, `[card] open` lines during the holds = 0. Rejoins are counted and reported.
- **(c) Accidental toggles are at most 1 in 50 during normal tapping.** 50 ordinary taps, on aircraft
  and on empty space, as Daniel taps normally. `[zoom] hold` lines <= 1. (The recorded tap p99 is
  183 ms against a 700 ms threshold, so the expectation is 0.)
- **(d) An early release does nothing.** 10 holds released once the ring is visible and before it is
  full. 10/10 log `[hold] cancel`, with no `[card] open` and no `[zoom] hold`.
- **Threshold:** it starts at 700 ms. If Daniel changes it, (b)-(d) are run at the final value, and
  the value and his reason go in an addendum.

## On the host (`test/host/test_long_press.cpp`, `include/LongPressPolicy.h`)

- release at < 250 ms -> TAP (unchanged path); at 250 ms to threshold -> CANCEL; at the threshold
  while held -> TOGGLE once, then the release is CONSUMED (no tap, no swipe);
- movement of 40 px or more before the threshold -> no toggle; the release is classified as today;
- release then re-press within 150 ms and 20 px during the ring -> one hold, progress kept; at
  151 ms or 21 px -> cancel, then a new stroke;
- toggle: intermediate -> closest; closest -> top; single-step ladder -> consumed, no change.

## Sabotage, shown red and then undone

- **S1, drop "no card past the threshold":** (b) fails. Cards open on the releases of holds over
  aircraft (bench), and the host CONSUMED case fails.
- **S2, drop the rejoin grace:** the host rejoin case fails.

## Outside the set: stop and report

- (a) moves by more than 50 ms with no sabotage applied;
- (b) below 19/20 with no rejoin and no fragment logged: holds are being missed for a reason the
  spec did not see;
- **a split before ring start** during an intended hold (a card or a tap fires mid-hold). The
  TouchNum fallback in the spec is then built and graded before anything ships;
- a toggle from any stroke shorter than 250 ms.

---

*Addenda below this line, dated, never edits above it.*

### Addendum, 2026-10-10 (review): the drift rules, frozen BEFORE the long-press image is flashed

Daniel's holds on the old firmware: a 7,816 ms motionless hold stayed one stroke; a 3,973 ms hold
drifted 58 px and was classified as a swipe. Spec section "Drift" has the numbers.

- **Rule 1:** past the threshold the stroke is consumed. Its release is never a tap or a swipe,
  however far the finger drifted.
- **Rule 2:** before the threshold, 40 px or more within the first 250 ms is a swipe (unchanged);
  after that, slow drift is tolerated up to **80 px** (the largest drift in a logged 1-4 s hold is
  67 px). A rejoin compares the new press with the finger's last position.
- **New instruments:** `[hold] fire drift_max=<px> at=<ms>` (largest drift before the fire, and when),
  the same on a cancel, and `[screen] -> <name>` on every screen change, so "never changes screen" is
  read from the log.

**Added predictions:**
- **(e)** 10 holds with deliberate slow drift (~50-70 px) **each toggle zoom exactly once and never
  also swipe or change screen**: 10 `[zoom] hold` lines, one per stroke; 10 release lines reading
  `HOLD (consumed)`; 0 `-> SWIPE` and 0 `[screen]` lines for those strokes.
- **(f)** 10 normal fast swipes **still swipe**: 10 release lines `-> SWIPE` with `held` under 250 ms,
  each doing what it does today (left/right: a `[screen]` line; up/down: `[zoom] swipe ...`), and
  no `[hold]` line among them.

**Added sabotage:** **S3, remove rule 1**: a fired stroke whose finger drifted 40 px or more releases
as a swipe. (e) fails, with a zoom plus a swipe from one stroke (bench), and the host rule-1 case
fails.

**Host cases added:** 40 px inside 250 ms -> swipe; 79 px during the ring -> tolerated, 80 px ->
slow drag; 150 px of drift after the fire -> consumed; a rejoin is measured from the last position.

**Session order (Daniel's choice).** The long-press image first:
1. Daniel tries it freely;
2. then the frozen checks: (a-after) 30 taps, (b) 20 holds over a crowded centre, (e) 10 slow-drift
   holds, (f) 10 fast swipes, (c) 50 normal taps, (d) 10 early releases, and his verdict on the hold
   time;
3. then the #385 acceptance run;
4. then (a-before), 30 taps on the instrument-only image.

The before/after comparison is unchanged by the order.

**Outside the set, stop and report:**
- one stroke both zooms and swipes, or zooms twice;
- a fast swipe fails to swipe, or toggles zoom;
- a fire logged with `drift_max` over 80 px.
