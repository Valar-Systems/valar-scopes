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

### Addendum, 2026-10-12 (review): rule 3, an early release that moved 40 px or more is a swipe -- frozen BEFORE code

**The finding.** On 10 Oct, block (f): 1 of 24 swipes did nothing. It took 428 ms and moved 78 px.
It was under 40 px at 250 ms, so the ring started. It was released before the threshold, which went
to the grace and then a cancel. **Ruling: apply the fix before merge.**

- **Rule 3:** a stroke released BEFORE the threshold whose total movement is 40 px or more is a
  swipe, **exactly as v16 classifies it**.
  - "Total movement" is the classifier's own measure: the larger axis of (release point - press
    point). The caller passes its own `dx, dy` into `OnRelease`, so the rule and the classifier read
    the same number, and a release under rule 3 goes to the existing classifier unchanged.
  - **Below 40 px nothing changes.** Released before 250 ms it is the old tap. Released during the
    ring it is the grace and then a cancel, which is (d). (The review wrote "39 px early release ->
    tap path as today". What is frozen is "as today", and during the ring today is the cancel, not a
    tap.)
- **Rules 1 and 2 are unchanged.** Past the threshold the stroke is consumed whatever it moved (e).
  Drift up to 80 px still holds, so a slow-drifting hold that REACHES the threshold still toggles.
- **What this reverses:** the spec's 2026-10-10 line "a slow drag of 40-80 px released before the
  threshold no longer swipes". Those strokes swipe again, as in v16.
- **Consequence, stated: a ring-phase dropout after 40 px or more of drift now releases as a swipe**
  instead of waiting for a rejoin.
  - The rejoin exists for a finger the chip stops reporting because it does not move. A finger 40 px
    from where it started is moving. The 10 Oct session logged 0 rejoins.
  - The host case "a rejoin is measured from the last position" moves from 50 px to 30 px of drift.
    That is still more than REJOIN_PX (20), so the property it tests is intact.
- **New instrument:** `[hold] early release moved=<px> -> swipe`, logged when a ring-phase release
  passes through under rule 3. It separates rule-3 swipes from fast ones.

**Host cases** (`test/host/test_long_press.cpp`):
- G1, the 10 Oct stroke: under 40 px at 250 ms (the ring starts), 78 px at the 428 ms release ->
  **PassThrough (SWIPE)**.
- G2: a ring-phase release at 39 px -> **Grace** (unchanged).
- G3: a ring-phase release at 40 px -> **PassThrough**.
- G4: 39 px released before 250 ms -> PassThrough. The classifier makes it a tap; unchanged.
- G5, the trade: 50 px of ring drift then a release -> PassThrough (G5a). A press 2 px away is then a
  new stroke, not a rejoin (G5b).

**Sabotage S4: remove rule 3** (a ring-phase release always enters the grace). **Predicted: exactly
4 failures, G1, G3, G5a and G5b, and nothing else.** The 428/78 case is among them.

**Bench (g): 10 deliberate slow swipes**, about 400-600 ms and 50-80 px each, on the bare Radar.
- **Each must swipe exactly once:** a release line `-> SWIPE` and the swipe's own effect (up/down:
  `[zoom] swipe`; left/right: `[screen]`). There must be **no `[zoom] hold` and no `[hold] fire`** for
  those strokes.
- **Expected, and not a failure:** a `[hold] ring` line for any stroke slower than the ring start.
  Each such stroke also logs `[hold] early release moved=<px> -> swipe`.
- **Readable only if at least 5 of the 10 exercised rule 3**, i.e. logged the early-release line.
  The fast ones pass under the old rules and prove nothing about the fix. If fewer than 5 qualify,
  Daniel does more, slower ones.

**Re-run of (d), (e) and (f) in the same sitting, on the same image.** The frozen bars are
unchanged. How rule 3 is kept out of their grading, decided now:
- **(d)** is graded over early releases whose movement is under 40 px, and needs 10 of them. One that
  moved 40 px or more and swiped is reported under rule 3, not counted as a (d) failure.
- **(e)** is graded over holds that reached the threshold (`[hold] fire`). A hold released before the
  threshold is excluded and reported.
- **(f)** is graded over swipes with `held` under 250 ms, as frozen.

**Image:** #388's head with rule 3 = 52ba638 + #388's commits, named by sha in the PR before the
sitting. The sitting is not tonight.

**(a) still counts.** The after taps ran on 62cf054 + 2faceff. Rule 3 touches only a release during the
ring (250 ms or more). A quick tap is under 250 ms and passes through unchanged, which G4 and the
existing tap case pin.

**Outside the set, added (stop and report):**
- a (g) stroke that both swipes and toggles, swipes twice, or does nothing;
- a (d) stroke under 40 px that swipes or opens a card;
- an `[hold] early release` line with moved < 40.

### Addendum, 2026-10-12 (review, 2): rule 3 is decided when the rejoin grace ENDS, not at the lift -- frozen BEFORE code

This supersedes the "decide at the lift" form of rule 3 (bfa36a6) and its stated trade. The 39 px
interpretation (the cancel, not a tap) is accepted as frozen above.

- **A release before the threshold opens the 150 ms rejoin grace, exactly as on 10 Oct.**
  - A re-press within the grace, within REJOIN_PX of the finger's LAST position, is a **rejoin**, one
    hold, **whatever it had drifted so far**.
  - When the grace ends with no rejoin, rule 3 decides. Total movement of 40 px or more is a **swipe**
    through the existing classifier, with the same release `dx, dy` the caller passed at the lift.
    Under 40 px it is **the cancel** that (d) tests, unchanged.
- **The grace can end two ways, and both go through the same decision:**
  - **expiry:** no press within 150 ms;
  - **a press that is not a rejoin:** too far from the last position. That press also starts a new
    stroke. (This second path is named here so it cannot quietly miss the rule.)
- **Net effect:**
  - a dropout during a drifting hold rejoins; there is no trade-off any more;
  - a real lift that moved 40 px or more swipes about 150 ms after the lift (plus up to one 52 ms
    poll);
  - the ring stays up, frozen, through those 150 ms, as it already does during the grace.
- **Instruments.** At the lift the release line reads `HOLD (released early; grace)`, as today. When
  the grace ends in a swipe, the device logs:
  - `[hold] early release moved=<px> -> swipe`;
  - `[touch] <ms> grace-end d=(<dx>,<dy>) -> SWIPE`;
  - then the swipe's own effect.

  When it ends in the cancel, it logs `[hold] cancel ...`, as today.

**Host cases** (`test/host/test_long_press.cpp`). "Ends as" means: run the release, then idle frames
every 52 ms until the stroke is resolved.
- **RJa / RJb, restored to 50 px of drift:** a press 2 px from the drifted position rejoins (RJa); a
  press back at the original point, 50 px from the finger, is a new stroke (RJb).
- **G1**, the 10 Oct stroke (428 ms, 78 px; under 40 px at the ring start): ends as a **SWIPE**. Its
  setup check (the ring started, still within the drift tolerance) is unchanged.
- **G2:** a ring release at 39 px ends as **the cancel**.
- **G3:** a ring release at 40 px ends as a **SWIPE**.
- **G4:** 39 px released before 250 ms is classified at the lift as today (a tap). Unchanged.
- **G6:** 50 px drift, lift, re-press 120 ms later at the last position.
  - G6a: it is a **rejoin**;
  - G6b: it fires once, at the ORIGINAL press + threshold;
  - G6c: its release is CONSUMED. One hold, no swipe.
- **G7:** 50 px drift, lift, no re-press.
  - G7a: nothing at the lift (the grace opens);
  - G7b: a **SWIPE at grace expiry**, not before it, carrying the stroke's own dx, dy.
- **G8:** 50 px drift, lift, a press 60 px away 80 ms later. It ends the old stroke as a **SWIPE** and
  starts a new stroke.
- The 10 Oct G5 pair (the trade) is removed; G6 now asserts the opposite of G5b.

**Sabotage, predicted exact failure sets** (anything else is outside the set: stop and report):

| sabotage | fails exactly |
|---|---|
| **A: decide at the lift** (a ring release that moved 40 px passes through at once) | RJa, G6a, G6b, G6c, G7a, G7b, G8 (7) |
| **B: remove rule 3** (the grace always ends in the cancel) | G1, G3, G7b, G8 (4) |

Under A, G1 and G3 still pass, because they still swipe, only at the lift. G7 is what separates "when".

**Bench (g), restated for the new instruments.** Each slow swipe must show exactly ONE `-> SWIPE`
and the swipe's own effect, and no `[zoom] hold` or `[hold] fire`:
- a fast swipe shows it on its release line;
- a rule-3 swipe shows it on its `grace-end` line, about 150 ms after the lift.

Readable only if at least 5 of the 10 logged `[hold] early release`.

**(f)** is graded over release lines with `held` under 250 ms, as frozen. `grace-end` lines carry no
`held` and are not counted in (f). **(d)** and **(e)** are as in the previous addendum. Daniel's 4-step
sitting is unchanged.

### Addendum, 2026-10-13 (review): check (g)'s scope, and the "hold toggled back" usage counter -- frozen BEFORE code

**(g), ruled.** (g) is graded on strokes inside its stated range: 50-80 px, lifted before 0.7 s. On
the 13 Oct sitting (image ed8f09f03625) that gives **n = 6, and 6 of 6 swiped exactly once**, 5 of
them through rule 3.
- **Out of scope:** the 6 strokes at 26-39 px. They behaved per the frozen "39 px -> unchanged" rule,
  which is v16 parity: v16 would not have swiped them either.
- **Also outside the range:** the strokes above 80 px, the 40-49 px strokes and the held-past-0.7 s
  strokes are reported, not graded.
- **The slow-slide decision:** option 1, recorded in the spec ("Product decision 2026-10-13").

**The counter: `holdToggleBacks`, "a hold toggled back by another hold within 5 s"** (a likely
accidental hold). Its place is next to the long-press count in the usage report. It counts THAT it
happened, never what the hold was over: one integer.

- **Definition.** When a hold FIRES (the zoom toggles), it counts as a toggle-back if and only if
  BOTH hold:
  - the previous hold fire was **at most 5000 ms earlier**, measured fire to fire on the device clock,
    and **5000 ms exactly counts**;
  - **nothing else changed the zoom** in between, i.e. the zoom index just before this fire equals
    the index the previous fire set. A swipe zoom in between makes it not a toggle-back.

  Notes:
  - The first fire of a session never counts.
  - A fire on a single-step ladder toggles nothing (`ToggleTarget` returns -1), so it is not a fire
    for this counter.
  - Three quick holds count 2: each of the 2nd and 3rd toggles the previous one back.
- **The pure half:** in `include/LongPressPolicy.h`. A small state (whether there was a previous
  fire, its time, the index it set) and one decision function, called at every fire. The device only
  increments the usage counter when it returns true.
- **Host cases** (`test/host/test_long_press.cpp`):

  | case | setup | counts |
  |---|---|---|
  | T1 | two fires 3000 ms apart, no other zoom change | 1 |
  | T2 | 6000 ms apart | 0 |
  | T3 | exactly 5000 ms | 1 |
  | T4 | 5001 ms | 0 |
  | T5 | a swipe zoom change between two fires 3000 ms apart | 0 |
  | T6 | three fires 2000 ms apart | 2 |
  | T7 | the first fire ever | 0 |

- **Sabotages, predicted exact failure sets:**
  - **A: drop the 5 s window** (any consecutive fire counts): fails T2 and T4.
  - **B: drop the no-other-zoom-change condition:** fails T5.
- **Where the code lands:** with the usage-report format change, alongside its digits-and-commas test,
  the Worker's shape check and the disclosures (README Privacy & telemetry, support.html), in the
  same commit, per CLAUDE.md's telemetry entry. #388 itself is not changed for it.
- **Open, for a ruling: the field count.** The format change was decided as 8 -> 12 (longPressZooms,
  overheadCards, whatsNewOpened, whatsNewQr), with the Worker accepting exactly 8 or 12. This counter
  makes 13. Either the format becomes 8 -> 13 (the Worker accepts exactly 8 or 13, and its rejection
  test covers 9-12 and 14), or one of the four gives way. Not decided here.
