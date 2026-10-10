# Long press to toggle zoom (v17)

**Status: spec, not built. Daniel's decision, 2026-10-09: LONG PRESS replaces double-tap as the zoom
shortcut.** The double-tap design, its native-vs-timed comparison and its bench runs are dropped;
none had been run. **Swipe-to-zoom (v16) stays exactly as it is.** Line refs at `7bc5f43`. "AM" =
`src/AircraftManager.cpp`.

## Customer view

On the Radar, **press and hold anywhere** (over an aircraft too) for about 0.7 s:
- from the configured range, or any zoom in between, it jumps to the **closest zoom, 5 mi**;
- from 5 mi, it goes back to the **configured range**.

A small **ring fills around your finger** while you hold. Let go before it is full and **nothing
happens**: no card, no zoom change.
- **A quick tap behaves exactly as before**, with the same latency.
- **Swipe up and down still zoom one step at a time**, unchanged.
- The ZOOM tag and the 10-minute return to the configured range are unchanged.

## Confirmed first: nothing on the Radar gives a long press a meaning today

- **The release classifier uses movement only.** A stroke is a TAP if `|dx|` and `|dy|` are both
  under `SWIPE_MIN = 40` px, else a swipe (AM:8877-8897).
  - The hold time (`now - touchPressMs`) is **printed in the release line and never used.**
  - AM:8873-8875 is an empty comment block, the remnant of the reset hold that was removed.
- **The reset menu is discrete taps, never a hold** (AM.h:199-216). `docs/gestures.md:85` forbids a
  long press because of the static-contact concern below. That doc is updated in the build PR.
- **Taps act synchronously on release.** `HandleTap` opens a card in the same loop pass (AM:9114,
  AM:9132).
- **Swipe zoom** is AM:9284-9289 (`StepZoom`: up = in, down = out).
- **The boot touch window** (`[wifi-reset] boot touch window`) runs before the Radar exists. A long
  press on the Radar cannot reach it.

## Does the CST816 drop contact during a 1 s hold?

**The concern is real and documented.** `src/gametest_main.cpp:38-45`, finding (b): with IrqCtl
0x60 (EnTouch|EnChange), *"a finger that does not MOVE may generate no change interrupt, so a static
hold can read as 'no touch' at the driver while the chip still has the finger."* The Radar reads the
driver: `ReadTouch` -> `tft.getTouch()` (`include/TouchPoll.h:26-29`). **The planned static-hold
A/B never ran** (`docs/soak-com119-2026-08-19.md:181-197`).

**What this batch has actually recorded:** every `[touch]` line in the COM18 / s3-128 logs on the
bench machine, deduplicated, 1,014 releases:
- **Five holds of 500 ms or more, all single strokes (0 splits):**
  - **today, 17:09:12-17:09:14 PDT, COM18:** a **2,293 ms stationary hold** (`d=(0,0)`) at the
    screen centre. One press (`28426229`), one release (`28428522`), nothing between. It was read
    as a TAP and opened a card;
  - the July s3-128 soak: holds of 507, 695, 699 and 736 ms.
- **Normal taps (905):** median **75 ms**, p95 **147 ms**, p99 **183 ms**. Hold times step in
  ~52 ms increments, because touch is polled once per frame.
- **54 "release, then a press within 300 ms and 20 px" sequences.** Every one follows a fragment of
  39-120 ms, in bursts of pairs (e.g. today 17:04:27-17:04:39). That is the shape of deliberate
  quick double-taps. **A log alone cannot tell that apart from a hold breaking up**, which is why the
  bench session below counts splits under known intent.
- **The chip's self-reset timers on this batch:** `0xFB` AutoReset 50 s and `0xFC` LongPressTime
  60 s (`INCOMING-INSPECTION.md:105-106`), unaffected by a silent reset (:140). Both are far above a
  0.7 s hold.

**How a split would show in the log:** during an intended hold, a release with `held` under the
threshold, then a press within about two frames (~110 ms) and 20 px.

**How a split stroke is handled (built in, not waiting to be seen):**
1. **Ring start at 250 ms**, above the tap p99 (183 ms). A release before 250 ms is a tap, exactly
   as today, with no added latency.
2. **While the ring is filling, a release opens a 150 ms rejoin grace.** A press within 20 px inside
   it resumes the same hold, keeping its progress, logged `[hold] rejoin after <ms>`. No press means
   cancel. A cancel has no visible action except the ring going away, so waiting costs nothing.
3. **A dropout before 250 ms would read as a tap, as it does today.** Five of five recorded holds
   show the chip keeping contact for at least 500 ms. If the bench session sees a split before ring
   start, the fallback is the gametest finding's own fix: on a driver release, read the chip's
   TouchNum (0x02, on the loop task's `lgfx::i2c`) and trust it. That fallback is specified, and not
   built unless it is needed.

## Rules

1. **Where:** the Radar only, with nothing on top: no detail card, overhead card, What's-new summary
   or reset menu. Anywhere on the face, including over an aircraft.
2. **0-250 ms:** nothing visible. A release is a tap, unchanged. **Movement of 40 px or more inside
   these first 250 ms is a swipe** (the stroke is the swipe path's, exactly as today).
3. **250 ms to the threshold:** a ring fills around the press point (radar-chrome green, about 18 px).
   **Slow drift is tolerated up to 80 px** (drift rule 2, below). Past 80 px it is a slow drag: the ring
   cancels, and the release is classified as today.
4. **At the threshold (start 700 ms, tuned on hardware with Daniel):** zoom toggles through
   `ApplyZoom`, so the pill, ZOOM tag and idle return are unchanged. It is logged
   `[zoom] hold -> <step>`. **The stroke is consumed: its release opens no card and is not a swipe,
   however far the finger drifts** (drift rule 1, below).
5. **Toggle:** from any step except the closest, go to the closest (5 mi, or the ladder's smallest);
   from the closest, go to the top (the configured range). On a single-step ladder nothing changes,
   and the stroke is still consumed.
6. **A release between ring start and the threshold**, after the rejoin grace, **cancels**: no card,
   no zoom.

The detector is a pure policy (`include/LongPressPolicy.h`): phases, ring start, threshold, rejoin
grace, swipe cancel and consumption. It is graded on the host in `test/host/test_long_press.cpp`.

## Drift: a swipe is fast, drift is slow (review, 2026-10-10)

Daniel's holds on the old firmware: a 7,816 ms motionless hold stayed one stroke, and a **3,973 ms
hold drifted 58 px and was classified as a swipe.** Distance alone cannot tell a swipe from a finger
settling during a hold, so the rule uses speed as well. Frozen before the long-press image was
flashed.

1. **Past the threshold, the stroke is consumed.** Once the ring is complete and zoom has toggled,
   the release is **never** classified as a tap or a swipe, however far the finger has drifted.
2. **Before the threshold, speed separates them:**
   - movement of **40 px or more within the first 250 ms** is a swipe (today's swipe, unchanged);
   - after that, **slow drift is tolerated up to 80 px**, and only beyond 80 px does the hold cancel
     into a slow drag;
   - **a rejoin** after a dropout compares the new press with the finger's **last** position, not
     the press point, since the finger may have drifted.

**The numbers behind 80 px.** Every stroke of 250 ms or more in the COM18 / s3-128 logs:

| held | drift (largest axis) | read as |
|---|---|---|
| 507, 695, 699, 736 ms | 13, 13, 37, 4 px | TAP |
| 1,072 ms | 67 px | SWIPE |
| 2,293 ms (today) | 0 | TAP |
| 3,378 ms | 54 px | SWIPE |
| **3,973 ms (today)** | **58 px** | SWIPE |
| 7,816 ms (today) | 0 | TAP |
| 8,966 ms | 90 px | SWIPE |
| eight strokes of 254-328 ms | 38-206 px | 1 TAP, 7 SWIPE (fast swipes) |

- **The largest drift in a 1-4 s hold is 67 px**, so 80 px leaves 13 px of margin. The 9 s, 90 px
  stroke stays a swipe.
- **When the 58 px drift happened inside the 3,973 ms hold is not recorded:** the release line
  carries only the start and end points. The long-press image logs each hold's largest drift and when
  it was reached (`[hold] fire drift_max=<px> at=<ms>`, and on a cancel), so the free trial and (e)
  answer it.
- **Fast swipes are untouched by construction:** of 100 logged swipes under 250 ms, the smallest
  moved 41 px and the median 125 px. All of them are swipes under rule 2.
- **What changes, stated:** a **slow** drag of 40-80 px released before the threshold no longer
  swipes. During the ring a release does nothing, so it cancels. 4 of the 111 logged swipes were
  slow (250 ms or more) and moved 40-80 px:
  - 1,072, 3,378 and 3,973 ms: the drags this rule re-reads as drift;
  - one of 267 ms, which at a steady speed crossed 40 px by about 160 ms, and so stays a swipe. Its
    intermediate positions are not logged, so that is an inference, not a measurement.

## Instruments

They are cheap, and they ship:
- `[card] open t=<ms>` on the first frame that draws a card (the latency instrument);
- `[hold] ring t=<ms>`, `[hold] rejoin after <ms>`, `[hold] cancel held=<ms>`, `[zoom] hold -> <step>`.

## Predictions to freeze before code

See [v17-long-press-zoom-predictions.md](v17-long-press-zoom-predictions.md), frozen with its hash in
the PR:
- **(a)** quick-tap card latency unchanged (30 taps before vs after);
- **(b)** a long press over a crowded centre toggles zoom at least 19/20 times and opens no card;
- **(c)** accidental toggles at most 1 in 50 during normal tapping;
- **(d)** an early release does nothing.

**Sabotage:** drop the "no card past the threshold" rule, and (b) must fail.

## Bench session (COM18, with Daniel)

The #385 acceptance run first, then the long-press checks on real traffic. **Daniel says whether the
threshold feels right**, and a change is recorded with its reason.

## Telemetry

**`longPressZooms`**: holds that toggled zoom. Count only, never where. It is one of the four new
fields in the single v17 usage-format change, **8 -> 12** (decided at review), alongside
`overheadCards`, `whatsNewOpened` and `whatsNewQr`. The Worker accepts exactly 8 or 12, and the
disclosures change in the same commit.

## Docs and card

- `docs/gestures.md`: Radar rows for swipe up/down (zoom, v16) and long press (this). The
  static-contact note is updated with the evidence above: a hold is acceptable for a
  **non-destructive** shortcut whose failure is "nothing happened", and destructive controls stay
  taps.
- `docs/CARDS-README.md`, pending for v17: *"Press and hold the radar to zoom in close; again to zoom
  back out."*
- `docs/CHANGES-customer.md` (What's new), v17 summary line: *"Hold to zoom in"*.
