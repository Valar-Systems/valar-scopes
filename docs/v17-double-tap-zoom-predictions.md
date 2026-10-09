# Double-tap toggle zoom: predictions, frozen before any code (2026-10-09)

The spec is [v17-double-tap-zoom.md](v17-double-tap-zoom.md). This file is frozen before the first
line of code; its sha256 is recorded in the PR. Anything learned later goes in an addendum below the
line, never into the text above it. Board: **COM18 only** (COM6 and COM15 untouched). Line refs at
`7bc5f43`.

## Stage 1: instrument, then measure the CURRENT behaviour (before the gesture exists)

A bench env, `blipscope-s3-128-tapbench` (`-DTAP_BENCH`), refused in shipping images by
`check-no-bench-hooks.sh`, which gains the marker `[tap-bench]`:
- `[card] open t=<ms>` on the first frame that draws a detail card after it opens. It is cheap and
  ships too: it is the latency instrument.
- the `t` serial key: a deterministic tap at the nearest drawn blip's projected position, through
  the same `HandleTap` path a finger takes, so the latency comparison does not depend on aim;
- the CST816's own gesture register (0x01) read after every release, with `MotionMask` 0xEC set to
  enable double-click (`EnDClick`) on the bench only, printed beside the existing press/release
  lines. **0xEC is read back after the write.** If the readback does not hold the value, the native
  column is **"not measured"**, never "0/50".

**B1, the latency baseline (no prediction of its value; it is the "before"):** release to
`[card] open`, over N = 30 aircraft taps by Daniel on live traffic plus N = 30 `t`-key taps.
Reported as median and 95th percentile, per source.

**B2, the comparison (the spec's rule, restated so it cannot move):** N = 50 deliberate double-taps
by Daniel on empty radar, plus N = 50 single taps more than 1 s apart, logged both ways at once.
- Detection = double-taps recognised / 50. False positives = single taps read as double / 50.
- **Pick the higher detection rate at <= 1 false positive in 50. On a tie, our timing.**
- Our window and distance are then set just above Daniel's measured 95th-percentile release-to-press
  gap and inter-press distance, **capped at 400 ms**.
- My expectation, written so the result can surprise me: our timing at >= 48/50 with <= 1/50 false
  positives; native lower. **The rule decides, not the expectation:** if native wins under the rule,
  native is built.
- **Outside the set, stop and decide:** neither method reaches 40/50 at <= 1 false positive (the
  spec's model of the gesture is wrong); or the chip resets silently during B2 (`[health]
  touch-wd` recovered count rises), which would make the native column a measurement of two
  configurations.

## Stage 2: the gesture (the chosen method), graded on COM18 and on the host

- **P1:** a double-tap on empty radar toggles the configured range to the closest step and back,
  through `ApplyZoom` (pill, ZOOM tag and 10-min idle return unchanged), logged
  `[zoom] doubletap -> <step>`. On a single-step ladder it does nothing, and the tap is consumed.
- **P2:** single aircraft taps keep their latency. **The median and the 95th percentile of release
  to `[card] open` are each within one frame (50 ms) of B1**, per source (Daniel's taps, `t` taps).
- **P3:** a double-tap whose first tap hits an aircraft opens the card on the first tap, does not
  zoom, and the card is still on its first page afterwards (the 300 ms card-open refractory absorbs
  the second tap).
- **P4:** an empty single tap still clears the pin on its own release, with no added delay: the
  clear lands in the same loop pass as the release line.
- **P5:** swipe zoom steps and the 10-min idle return are unchanged (`test_radar_zoom` passes
  unmodified), and a swipe between two taps cancels the window.
- **Host tests:** the detector is a pure policy (`include/DoubleTapPolicy.h`), graded in
  `test/host/test_double_tap.cpp`: in window and in distance -> toggle; late second tap -> no
  toggle; far second tap -> no toggle; first tap on an aircraft -> never arms; swipe -> disarms;
  single-step ladder -> consumed, no zoom change; card-open refractory absorbs a tap at < 300 ms
  and passes one at >= 300 ms.

**Telemetry, count only:** `doubleTapZooms`, the number of double-taps that toggled zoom: THAT the
gesture was used, never where. It is counted in `UsageStore` here and reaches the wire in the single
8 -> 10 usage-format PR (the Worker accepting both formats first), with the disclosures in that
commit.

## Sabotage, each shown red and then undone

- **S1, defer every first tap until the window closes:** P2 fails, with aircraft-tap latency up by
  roughly the window (~300 ms) in both sources.
- **S2, drop the card-open refractory:** P3 fails: the card is flipped or closed by the second tap
  (bench), and the refractory host case fails.
- **S3, drop the distance test:** the far-second-tap host case fails.

**Outside the set for Stage 2, stop and report:** P2 moves by more than 50 ms with S1 NOT applied (the
gesture costs latency by some path the spec did not see); or P1 zooms on a tap that hit an
aircraft.

## Card and docs

- `docs/CARDS-README.md`, pending for v17: *"Double-tap an empty spot on the radar to zoom in close;
  double-tap again to zoom back out."*
- `docs/gestures.md`: Radar rows for swipe up/down (zoom, v16) and double-tap (this), in the build PR.

---

*Addenda below this line, dated, never edits above it.*
