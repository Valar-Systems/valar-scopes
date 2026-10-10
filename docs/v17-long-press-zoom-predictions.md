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
