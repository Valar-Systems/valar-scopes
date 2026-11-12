# Where the 1.28" frame goes: a per-phase profile (registration; nothing built or run)

**Written 2026-11-12 20:09Z by the P4 session.** This is A0/B0 of `docs/speed-plan.md` section 2a: the one step common
to the owner's options (a) and (b). It is read-only. The orchestrator allowed this registration now (20:08Z); the run
waits on the two conditions below. **Every prediction here is frozen before any code is written or any number read.**

## 1. What it answers, and what it waits for

The owner's bar is read at 40 aircraft with map and weather on. v20 there is estimated at **86.7 + M ms p95**
(speed-plan.md 2a), with **M**, the map's steady cost, unmeasured, and weather at ~10.7 ms (measured and split in
docs/wx-device.md). The owner chose (a), 2026-11-12 20:10Z. Every lever in it is sized from estimates of where that time
goes. This profile replaces the
guesses with one measurement per phase.

**It runs in two parts** (amended 2026-11-12 ~20:20Z, before any code; accepted as registered at 20:17Z with the
PC-1 row and the sibling env added):
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
| `sweep` | `DrawSweepOverWeather`, or `DrawRadarSweep` when weather is off (`:961-968`) |
| `static` | `DrawRadar`'s circles, stale banner, weather note, airports, follow track (`AircraftManager.cpp:4142-4167`) |
| `contacts` | the per-aircraft loop's markers and trails |
| `labels` | the per-aircraft `DrawAircraftInfo` and its label box, summed over contacts |
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
| A0a | P0 | off | off | v19's shape |
| A0a | P1 | off | on | weather alone |
| A0b | P-CTRL | on | on | P2's scene with the same plant: a new image, so its own control |
| A0b | P2 | on | on | the owner's bar condition, which measures M |

About 10 minutes of trace per part, plus the bench's write and restore.

## 4. Predictions, frozen now

**PC-1, the profile's own cost, first.** At boot the image times 10,000 runs of one frame's profile points and
prints `[frame-profile] cost <ns> ns/frame`. That is 2 `micros()` reads per phase plus 2 per contact for `labels` at 40
contacts: about 100 reads and their stores. **Predicted: ≤ 100,000 ns (100 µs) per frame**, about 0.2% of a 50 ms
frame. **Over 500,000 ns (1% of a 50 ms frame), stop:** the profile would be heavy enough to move what it measures.

**The control next (P-CTRL against P1 in A0a; against P2 in A0b):** `labels` p50 moves **+5.0 ± 0.75 ms**, `fr` p50 moves **+5.0 ± 0.75 ms**,
and every other phase stays within **± 0.5 ms**. Outside that, the profile cannot place a known cost in its own
phase, and nothing after it is read.

**Accounting (every arm):** the phases' sum equals the bench's `fr` within **± 1.0 ms** at p50. A larger gap means
time the profile does not see. Stop.

| quantity (p50, 40 aircraft) | predicted | basis |
|---|---|---|
| `push` | **25.8 ± 1.0 ms** | the measured push floor |
| `flush` | **< 0.1 ms** | a no-op on SPI SKUs (`main.cpp:989`) |
| `labels` in P0 | **21 ± 7 ms** | ~0.53 ms per contact, the 2026-08-26 labels A/B |
| `contacts` in P0 | **11 ± 5 ms** | the rest of the 0.802 ms per-contact slope |
| `static` in P0 | **1-6 ms** | the airports measured at −0.09 ms; circles and text are few calls |
| `upd` | **1-6 ms** | no measurement; reported |
| `under` in P0 (the black fill) | **3.0 ± 0.3 ms** | docs/wx-device.md, step C's `clear=` with the layer off: 3.01 ms |
| `sweep` in P0 (the opaque wedge) | **2.0 ± 0.3 ms** | wx-device.md, `sweep=` with the layer off: 1.99 ms |
| **weather, P1 − P0: `under`** | **+3.8 ± 0.5 ms** | wx-device.md's one-run split (2026-11-06, E2:08, n = 16): `clear=` 3.01 → 6.82 ms (the layer's push replacing the fill) |
| **weather, P1 − P0: `sweep`** | **+4.6 ± 0.6 ms** | the same split, after the span-only blend landed: `sweep=` 6.62 against 1.99 (the blend's own cut was −1.17 ms) |
| **weather, P1 − P0: everything else** | **+2.3 ± 1.5 ms**, and **the profile places it** | the record's "about 2.3 ms sits outside both timers", unattributed until now |
| **weather's total, P1 − P0, in `fr`** | **+10.7 ± 2.0 ms** | the record's +12.2 ms p95 at n = 16, less the span-only cut. The owner's "about 13" is the record's total before that cut |
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
