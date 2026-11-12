# Where the 1.28" frame goes: a per-phase profile (registration; nothing built or run)

**Written 2026-11-12 20:09Z by the P4 session.** This is A0/B0 of `docs/speed-plan.md` section 2a: the one step common
to the owner's options (a) and (b). It is read-only. The orchestrator allowed this registration now (20:08Z); the run
waits on the two conditions below. **Every prediction here is frozen before any code is written or any number read.**

## 1. What it answers, and what it waits for

The owner's bar is read at 40 aircraft with map and weather on. v20 there is estimated at **86.7 + M ms p95**
(speed-plan.md 2a), with **M**, the map's steady cost, unmeasured, and weather at ~13.2 ms at 40 aircraft (measured and split
in docs/wx-device.md; corrected 20:45Z, section 4). The owner chose (a), 2026-11-12 20:10Z. Every lever in it is sized from estimates of where that time
goes. This profile replaces the
guesses with one measurement per phase.

**It runs in two parts** (amended 2026-11-12 ~20:20Z, before any code; accepted as registered at 20:17Z with the
PC-1 row and the sibling env added):
- **THE WEATHER ARMS NEED THE CLOUD SOURCE (found 2026-11-12 ~21:07Z; ruled (a) by the orchestrator).** In local
  mode neither the weather layer nor the map is drawn: `WxEnabled()` (`AircraftManager.cpp:3303-3305`) and
  `MapEnabled()` (`:3431-3434`) both need `useCloudSource && !cloudUrl.isEmpty()`. map-client's v19 FIXED scenes feed
  their traffic through the local path, so a "weather on" arm there would be weather OFF. **Ruling (a):** the bench
  answers the CLOUD `/blips` fetch from its deterministic table, through a bench-only hook confined to FRAME_BENCH
  regions. So `useCloudSource` stays true and weather and map fetch and draw as shipped. map-client owns it and
  registers it before its code.
  - **A0a's weather arms (P-CTRL and P1 ×3) run only on that (a) bench, and wait for it.**
  - **A0a's P0 arms** (weather and map off) can run on either path. The local path matches v19's FIXED scenes.
  - **A guard row, PG-1,** in section 4.
- **A0a, on main, as soon as map-client's frame-time bench is frozen** (`docs/frame-time-bench.md`, registered at
  `b0c58758dd8e`) and its CTRL (I-6, a planted +10.0 ms seen as +10.0 ± 1.5) has passed. It runs arms P-CTRL, P0 and
  P1: everything but the map. That gives M3's checkpoint the push, the labels and weather's split without waiting on
  the map.
- **A0b, once the map draw is in the tree,** on main or as the v20 candidate. On main at `e4b41fff9042` the radar's
  frame has no map draw yet (#659, step B, is an open draft; step C has no PR). It runs P-CTRL again (a new image needs
  its own control) and P2, which measures M.
- The profile rides on map-client's instrument and adds phases to it. It never replaces it.

## 2. The instrument

- **The image: its own sibling env, `blipscope-s3-128-frameprofile`**, slug-less, with map-client's `-DFRAME_BENCH`
  plus `-DFRAME_PROFILE`. map-client's `blipscope-s3-128-framebench` is left alone, so its baseline image and its I-3
  identity are untouched. Gating is by `#if`, not `if constexpr`: an `if constexpr` branch still compiles its
  functions, and gc-sections keeps their `.eh_frame`, so bytes would move (5c's A4, the gc-sections lesson).
- **No other image moves:** `blipscope-s3-128`, every other slug-ful env and **`blipscope-s3-128-framebench`**, each
  built before and after the profile's code, read PLACED with the five-region comparator. A byte outside the five
  regions is a stop, reported first.
- **`[frame-profile]` joins `check-no-bench-hooks.sh`'s MARKERS**, beside `[frame-bench]`.
- **The phases**, each a `micros()` delta stored per frame (uint16, 10 µs units) beside the bench's own record:

| phase | where (main at `e4b41fff9042`) |
|---|---|
| `upd` | `appManager.Update()`, from `frameStartUs` (`src/main.cpp:752`) |
| `under` | `DrawWeatherUnder` (the layer's memcpy, or its rebuild) or `fillScreen` black (`:938-947`) |
| `map` | the map's draw, wherever step B/C puts it in the frame (read from the tree at the run) |
| `sweep` | `DrawSweepOverWeather`, or `DrawRadarSweep` when weather is off (`:961-968`). **With weather on, split three ways at the points step C already has inside `DrawSweepOverWeather` (`AircraftManager.cpp:3633-3652`, `tRaster`, `tBlend`, `tBeam`): `sw_raster`, `sw_blend`, `sw_beam`** (weatherscope's request for P5) |
| `static` | `DrawRadar`'s circles, stale banner, weather note, airports, follow track (`AircraftManager.cpp:4142-4167`) |
| `contacts` | the per-aircraft loop's markers and trails, **less the outline** |
| `outline` | the 8-copy outline loop alone, summed over contacts (`AircraftManager.cpp:4292-4298`, the `if (outline)` loop before the marker); 0 with weather off (weatherscope's request: P5's first lever) |
| `lbl_layout` | `AircraftLabelBox` (the `:4247` call, first pass) plus `DrawAircraftInfo`'s own `LabelLayout` call (`:5669`), summed over contacts: the string formatting and `textWidth` that P6 would cache (weatherscope's request) |
| `lbl_draw` | `DrawAircraftInfo`'s `drawString` loop (`:5670-5671`), summed over contacts. `labels` = `lbl_layout` + `lbl_draw` |
| `rest` | everything else inside `appManager.Draw` |
| `push` | `backbuffer.pushSprite` (`:982`) |
| `flush` | `board::DisplayFlush` (`:990`; a no-op on SPI SKUs) |

- **Its own cost** is a graded row, PC-1 in section 4, bounded the way map-client's I-5 is.

## 3. The run

On E2:08, in map-client's window terms, after its own scenes or in a window of its own. The home is Bend, 40 fixed
aircraft, 25 mi (map-client's CTRL scene). Each arm runs map-client's settle and then **120 s measured**:

| arm | map | weather | why |
|---|---|---|---|
| part | arm | map | weather | why |
|---|---|---|---|---|
| A0a | P-CTRL | off | on | P1's scene with a planted **+5.0 ms** inside `labels` |
| A0a | P0 ×3 | off | off | v19's shape, at **20, 40 and 60** aircraft (map-client's levels) |
| A0a | P1 ×3 | off | on | weather alone, at 20, 40 and 60, so weather's per-contact term is read as a slope |
| A0b | P-CTRL | on | on | P2's scene with the same plant: a new image, so its own control |
| A0b | P2 | on | on | the owner's bar condition, which measures M |

About 25 minutes of trace for A0a (seven arms), 10 for A0b, plus the bench's writes and restore.

## 4. Predictions, frozen now

**PC-1, the profile's own cost, first.** With weatherscope's splits the per-contact points grow to about 7 per
contact: about 420 at 60 aircraft, plus about 30 per-frame phase boundaries. So:
- **The per-contact points read the CPU's cycle counter** (`esp_cpu_get_cycle_count()`, one register read), not
  `micros()`. The phase boundaries keep `micros()`.
- The loop task runs on one core, so its cycle count is consistent. The boot line prints `getCpuFrequencyMhz()`, so
  cycles convert to time at a frequency the image states, not one assumed.
- At boot the image times 10,000 runs of one 60-contact frame's points and prints
  `[frame-profile] cost <ns> ns/frame cpu=<MHz>`.
- **Predicted: ≤ 100,000 ns (100 µs) per frame at 60 contacts**, about 0.2% of a 50 ms frame.
- **Over 500,000 ns (1% of a 50 ms frame), stop:** the profile would be heavy enough to move what it measures. If it
  stops, the per-contact splits go first (weatherscope's order: the sweep split, then the label split, then the
  outline), never the phase boundaries.

**The control next (P-CTRL against P1 at 40 in A0a; against P2 in A0b):** `labels` p50 moves **+5.0 ± 0.75 ms**, `fr` p50 moves **+5.0 ± 0.75 ms**,
and every other phase stays within **± 0.5 ms**. Outside that, the profile cannot place a known cost in its own
phase, and nothing after it is read.

**PG-1, the weather is really on (every weather arm), before any of its rows is read:**
- `outline` is non-zero at every contact count, and `sw_blend` is non-zero;
- the image's own weather lines say a layer was shown during the measured span.

Otherwise the arm measured weather OFF: it reads **INSTRUMENT**, and none of its rows is graded. This is map-client's
guard row (3) for its own bench, applied to the profile, so "weather off by accident" can never pass as a fast frame.

**Accounting (every arm):** the phases' sum equals the bench's `fr` within **± 1.0 ms** at p50. A larger gap means
time the profile does not see. Stop.

| quantity (p50, 40 aircraft) | predicted | basis |
|---|---|---|
| `push` | **25.8 ± 1.0 ms** | the measured push floor |
| `flush` | **< 0.1 ms** | a no-op on SPI SKUs (`main.cpp:989`) |
| `labels` (= `lbl_layout` + `lbl_draw`) in P0 | **~0.53 ms × n: 10.6 at 20, 21 at 40, 32 at 60, each ± 35%**. The split between the two is **not predicted**: reported for P6 to size its lever from | ~0.53 ms per contact, the 2026-08-26 labels A/B |
| `contacts` in P0 | **~0.27 ms × n: 5.4 at 20, 11 at 40, 16 at 60, each ± 50%** | the rest of the 0.802 ms per-contact slope |
| `static` in P0 | **1-6 ms** | the airports measured at −0.09 ms; circles and text are few calls |
| `upd` | **1-6 ms** | no measurement; reported |
| `under` in P0 (the black fill) | **3.0 ± 0.3 ms** | docs/wx-device.md, step C's `clear=` with the layer off: 3.01 ms |
| `sweep` in P0 (the opaque wedge) | **2.0 ± 0.3 ms** | wx-device.md, `sweep=` with the layer off: 1.99 ms |
| **weather, P1 − P0: `under`** | **+2.1 ± 0.3 ms**, at every level | since B1's fix, `DrawWeatherUnder` memcpys the layer (`AircraftManager.cpp:3610-3612`): the fix's board run read the clear at **5.08 ms** (wx-device.md, \"The B1 fix, the board\", F2) against the fill's 3.01 ms. The +3.8 in this row's first version was step C's sprite push, before that fix |
| **weather, P1 − P0: `sweep`** | **+4.6, −1.5 / +0.6 ms**, at every level. The band is wide below because S5's 6.62 ms was read with one echo cell on the layer ("not graded: rain on the layer"), and a dry layer may read lower (weatherscope's question, 20:45Z) | the same split, after the span-only blend landed: `sweep=` 6.62 against 1.99 (the blend's own cut was −1.17 ms) |
| `sw_raster` / `sw_blend` / `sw_beam` in P1 | **2.0 ± 0.3 / 4.4 (−1.5 / +0.3) / 0.1 ± 0.05 ms**, and their sum within **0.3 ms** of `sweep` | wx-device.md's S5 reading: 2.03 + 4.45 + 0.11 against `sweep=` 6.62 (with one echo cell on the layer; a dry layer may read the blend lower) |
| `outline` in P1 | **0.1625 ms × n, ± 15%** (3.3 / 6.5 / 9.8 at 20 / 40 / 60); **0 in P0** | the outline's own timer, 157-169 µs per contact per frame (F7's observations) |
| **weather, P1 − P0: `outline`** (its own phase now) | **+0.1625 ms × n, ± 15%**: **3.3 at 20, 6.5 at 40, 9.8 at 60** | while the layer is on, every contact gets **8 black copies of its marker** (`WxOutlineNow`, `AircraftManager.cpp:4288-4298`, in both looks since 2026-11-06). Its own timer read **157-169 µs per contact per frame, median 162.5**, rising with n (4.71 ms at n = 30, 6.24 at n = 38; wx-device.md, the observations under F7). At n = 16 that is ~2.6 ms: the record's \"2.3 outside both timers\" |
| **weather's total, P1 − P0, in `fr`** | **2.1 + 4.6 + 0.1625 n: about 9.9 at 20, 13.2 at 40, 16.4 at 60, each ± 2.5 ms** | the three rows above. The first version's +10.7 was assembled from n = 16 numbers, and the outline scales with n (weatherscope, 20:45Z; checked against wx-device.md by p4) |
| `map` in P2, steady (M) | **not predicted as a number**. The meaning of each range is fixed now instead | see below |

**The weather arms are graded only on a dry layer**, by the record's own rule (wx-device.md, S3). The keyless probe
must read 0 echo cells within 100 km at the arm's start and at its end, because the blend's per-pixel work depends on
the codes under the wedge. Otherwise the arm is "not graded: rain on the layer", and its numbers are reported.

**What M will mean, fixed before it is read:**
- **M ≤ 2 ms:** the map is not a lever. A3/B2 drop out of the sizing, and the sizing goes back with that.
- **2 < M ≤ 8 ms:** A3/B2 stand as sized (1-2 d).
- **M > 8 ms:** the map is the largest draw lever, and A3/B2 are re-sized before anything starts.

**Rebuild frames:** **0** in each measured span. Rebuilds happen at a home or zoom change, which lands in the settle,
never in the 120 s. Any rebuild frame inside a span is reported with its cost and counted in that arm's p95.

**OUTSIDE THE SET:** any reading outside the bands above stops the read at that row and goes to the orchestrator. That
includes weather's split disagreeing with the record's. The instrument is in doubt until the reason is known,
and no lever is re-sized on an off reading.

## 5. Size and owner

| item | owner | size |
|---|---|---|
| this registration | p4 | done (20:09Z) |
| the code: the sibling env, the macro, the phase points, the dump lines, PC-1; every other image PLACED | p4 | 0.5 d |
| A0a: one E2:08 window, about 25 minutes with the writes | p4, in map-client's window terms | 0.25 d |
| A0b: a second window once the map draw is in the tree, about 25 minutes | p4 | 0.25 d |
| the reads, against section 4 | p4 | 0.25 d each |

It reads only. It changes no default, no shipping image and no board's saved settings. The bench restores v19's
release app afterwards, as map-client's procedure does.
