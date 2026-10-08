# Double-tap to toggle overhead zoom (v17)

**Status: spec, not built.** Builds after v16 is promoted. Line refs at `74866aa`.
"AM" = `src/AircraftManager.cpp`.

## Customer view

On the Radar, **double-tap empty space** to jump to the closest zoom, 5 mi. Double-tap again to go
back to the default range. Single taps, aircraft taps and swipes behave exactly as before.

## What exists today

- **Taps** are classified on release in `ProcessTouchSample` (AM:8850-8899): a TAP if
  |dx| and |dy| are both < 40 px, using the **press** coordinates. There is no time threshold.
  `HandleTap` then runs synchronously, so a card is drawn in the same loop pass.
- **The radar hit-test** is inline in `HandleTap` (AM:9069-9122):
  - `TAP_RADIUS = 28` px around the blip, or a tap inside the aircraft's label box;
  - it skips ground, `ZoomCulled` and off-face contacts.
- **An empty tap** clears `pinnedIcao` and resets the tap-cycle position (AM:9103-9105).
  A repeat tap within 18 px of the last one cycles candidates, with no time window (AM:9108-9110).
- **A tap on an open card** flips its page or closes it (AM:9000-9027). A 400 ms refractory
  follows a **close** (AM:8979, 9033); there is none after an **open**.
- **Zoom** (`include/RadarZoom.h`):
  - the ladder is `{5, 10, 25, 50}` below the configured radius, plus the configured radius as the
    top step;
  - `zoomIdx == 0` is the most zoomed;
  - `ApplyZoom` shows the `"<N> <unit>"` pill for 1.5 s, plus the persistent ZOOM tag (AM:4862-4922);
  - the 10-min idle return is at AM:1658-1664.
- **5 is on the ladder whenever the configured radius is above 5.5.** At 5 or less the ladder is a
  single step (`test/host/test_radar_zoom.cpp:47-48`).
- **No double-tap exists.** `docs/gestures.md` forbids long-press on this panel (the CST816D may
  report no change interrupt under a static contact: AM.h:186-216). Its Radar table has no zoom
  rows yet; this spec adds them.

## Rules

1. **Only after a tap that hit nothing.**
   - When a Radar tap's hit-test finds no candidate, the tap does exactly what it does today
     (clears the pin), **immediately**, and arms a double-tap window.
   - A tap that hits an aircraft never arms it.
   - So **no tap anywhere gets any added delay**: the gesture only adds a reaction to a *second*
     empty tap. It never defers the first.
   - One visible side effect: a double-tap also clears a pin, because the first tap is an empty tap.
2. **The second tap** must:
   - press within the window after the first **release** (~300 ms);
   - land within ~20 px of the first press;
   - also hit nothing.

   Then it toggles zoom and is consumed: no pin clear, no cycle.
3. **Toggle:**
   - from any step except the closest, go to the **closest step** (5 mi, or the ladder's smallest);
   - from the closest step, go to the **top** (the configured radius);
   - on a single-step ladder, nothing happens (the tap is still consumed).
   - It goes through `ApplyZoom`, so the pill, the ZOOM tag and the 10-min idle return are
     unchanged.
4. **A double-tap whose first tap hits an aircraft** opens the card on the first tap, as today.
   Today the second tap would then land **on the open card** and flip or close it. So this spec
   adds a **300 ms card-open refractory**, the mirror of the existing 400 ms card-close one:
   - a tap within 300 ms of a card opening is absorbed;
   - the card stays on its first page;
   - this changes no latency: the card still opens on the first tap.
5. **Swipes** are unchanged. A swipe between the two taps cancels the window.

## Native gesture or our own timing? Decide with evidence

**The chip:**
- The CST816 has a gesture register (0x01, with 0x0B for a double click). Its double-click must be
  enabled in `MotionMask` (0xEC, `EnDClick`).
- **Nothing in the tree writes 0xEC.** LovyanGFX's `Touch_CST816S` reads from 0x02 (finger count
  and coordinates) and never 0x01. `_check_init` writes only 0x00, 0xFA=0x20 and 0xED=20.
- So native double-tap needs:
  - a register write;
  - a custom gesture read on the loop task's `lgfx::i2c` (CLAUDE.md: never Arduino `Wire`);
  - the chip's own fixed, untunable timing and area.

**Evidence already against native:**
1. **Config does not survive a silent reset.** This batch's chip resets itself silently
   (`INCOMING-INSPECTION.md`), and any register config reverts when it does.
   - **Found while writing this spec:** the 0xFE "no auto-sleep" re-arm that CLAUDE.md says runs in
     production (`MaintainNoSleep`) is compiled only under `CST816_DISABLE_AUTOSLEEP`, which
     **nothing defines**. It is used only in `#ifdef`s at `TouchWatchdog.cpp:56`, `:88` and
     `:115`, and the v16 binary contains no `no-autosleep applied` string.
   - A native double-click would need the same re-arm, on a path that is demonstrably absent today.
2. **This project grades gestures against recorded fingers, not the chip's verdict**
   (`src/probe/GestureProbe.cpp`: *"using them would grade D3 against the chip's opinion instead of
   against fingers"*).

**The bench comparison, run before choosing:** COM18, a bench env (the bench-hook pattern,
refused in shipping images by `check-no-bench-hooks.sh`).
- **N = 50 deliberate double-taps** by Daniel on empty radar, plus **N = 50 single taps** spaced
  > 1 s apart.
- Logged both ways at once:
  - our timing, from the release/press timestamps already printed (`[touch] ... press` and
    `release ... held=`);
  - the chip's 0x01 verdict, with 0xEC enabled on the bench only.
- Report:
  - detection rate (double-taps recognised / 50);
  - false-positive rate (single taps read as double / 50);
  - for our timing, the distribution of release-to-press gaps and inter-press distances.
- **Pick the higher detection rate at <= 1 false positive in 50. On a tie, our timing**, for the
  reset-durability reason above.

**Window and distance:** start at 300 ms and 20 px. Report the measured 95th percentile of
Daniel's gaps and distances, and set the window just above it, capped at 400 ms so a slow second
tap is not stolen from the next single tap.

## Latency: proving rule 1 holds

- **Measure from release to the first frame that draws the card:**
  - a log line at card-open, `[card] open t=<ms>`;
  - the release line already carries `t`.
- Over **N = 30 aircraft taps before and after** the change.
- Bench taps by Daniel on live traffic, plus a deterministic `t`-key bench tap at a blip's projected
  position, so the comparison does not depend on aim.
- **Prediction:** the median and the 95th percentile are each within one frame (~50 ms) of before.

## Telemetry (proposal)

**`doubleTapZooms`**: double-taps that toggled zoom. It ships in the same single v17 usage-format
change as `overheadCards` (8 -> 10 integers, with the disclosures in the same commit; see
`docs/v17-overhead-card.md`). It counts THAT the gesture was used, never where.

## Predictions to freeze before code (drafts)

- A double-tap on empty space toggles default -> 5 -> default, logged `[zoom] doubletap -> ...`.
- A single tap on an aircraft opens its card with the same latency as before (above).
- A double-tap whose first tap hits an aircraft opens the card, does **not** zoom, and the card
  stays open on its first page.
- Swipe up/down zoom steps are unchanged, as is the 10-min idle return.

**Sabotage, shown red then undone:**
- **Apply double-tap detection to all taps** (defer every first tap until the window closes): the
  latency measurement must show +~300 ms on aircraft taps.
- **Drop the card-open refractory:** the aircraft double-tap case shows the card flipped or
  closed.

## Docs and card

- **`docs/gestures.md`:** add Radar rows for swipe up/down (zoom, v16) and double-tap (this),
  with the build PR.
- **Pending line for `docs/CARDS-README.md`** (v17):

  > *"Double-tap an empty spot on the radar to zoom in close; double-tap again to zoom back out."*
