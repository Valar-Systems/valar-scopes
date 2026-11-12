# The frame-time bench: v19's baseline on E2:08, and the per-candidate measurement for v20 (registration)

**Status: registered, nothing built or run.** Every number below is a prediction, frozen before any image exists. This
procedure becomes v20's per-candidate measurement in O11's pre-flight, rerun unchanged with the map and the weather on.

## Why

The owner (2026-11-12, after 19:31Z, relayed by the orchestrator at 19:33Z):

> "Drawing speed is a top priority for me. Make it a measured requirement." For v20: "Measure v19's frame times on
> E2:08 first, as the baseline. With map and weather both on, v20's radar is held to a frozen bar, measured on every
> candidate. My starting point: p95 frame time 50 ms or less (20 fps or better). Bring me the baseline and the
> proposed bar BEFORE THE MAP CODE IS GRADED."

The bar is frozen before any map number is read against it. This bench produces the baseline it is set from.

## 1. The image, and the proof of its identity

**The base is v19's own tree**, the tag `v19` (`7b943ae0`), not main: main carries changes after v19.
- **I-1, the tree reproduces the release.** `blipscope-s3-128` built clean from the tag, compared with the v19 release
  asset (`f6d73e39…`, 1,931,840 B) by `imgcmp.py` (`dc828e47…`). Predicted: the same length, and **0 bytes outside the
  five regions**. Anything outside means the tree is not the release, and nothing below is a baseline. Stop.
- **I-2, the bench image is that tree plus the instrument only.** A new slug-less env, `blipscope-s3-128-framebench`,
  extends `blipscope-s3-128` with `-DFRAME_BENCH`. Its code is one header, `include/FrameBench.h`, plus hook lines in
  `// THE FRAME BENCH` regions, each under `#ifdef FRAME_BENCH`. A source row (like CB-B1) checks that every
  `FRAME_BENCH` line lies inside a region, and that the regions hold no NVS write and print no coordinate or name. The
  same header and hooks are applied unchanged to v20 candidates.
- **I-3, the shipping image is untouched.** The tag tree's `blipscope-s3-128`, built with the patch applied, equals I-1's
  image by `imgcmp.py`: 0 bytes outside the regions.
- **I-4, `[frame-bench]` joins `check-no-bench-hooks.sh`'s MARKERS**, so no slug-ful image can carry it.
- **On the board:** the boot banner `[build] env=blipscope-s3-128-framebench fw=v19` and the `[frame-bench]` boot line.

## 2. What one frame is (the definition the gate reads)

**From the source (v19, `src/main.cpp`):**
- `loop()` takes `frameStartUs = micros()` at its first line (:752).
- It runs the update and the draw, `backbuffer.pushSprite` (:982), `board::DisplayFlush` (:990) and
  `framebuf::FrameDone()`.
- Then `RecordFrameUs(micros() - frameStartUs)` (:998). One cheap OTA-request check follows, and `loop()` returns.
- Every pass draws and pushes, so **one pass is one presented frame.**
- `RecordFrameUs` therefore spans update + draw + push + flush. It **excludes** the loop's tail and anything that runs
  between passes (the Arduino loop wrapper, and preemption by other tasks).

**The owner's bar is a user-visible rate**, so the instrument records two numbers for every frame and prints both:
- **`fr`**, the frame span: `RecordFrameUs`'s own value, taken where v19 takes it;
- **`p2p`**, present to present: `micros()` just after `FrameDone()` minus the previous frame's. This is the interval
  the panel shows.
- **The gate reads `p2p`.** `fr` is printed beside it, never alone, and never instead.
- **Rebuild frames:** the instrument tags a frame on which the map rebuilt (v20's `[map] draw: rebuilt`). They are
  counted separately AND included in the gate's p95. v19 has no map, so the count is 0.

**Statistics, per scene:** nearest-rank p50, p95, p99 and max of `p2p` and of `fr`, over every frame in the scene's
measured span. Computed on the host from the dumped per-frame values, never on the device.

## 3. The instrument

- **Per frame**, into a PSRAM array (bench only): `fr` and `p2p` in units of 10 µs (uint16; 0xFFFF marks an overflow
  above 655 ms, counted and reported), the tracked-aircraft count `n` (uint8), and the rebuild flag. 6 bytes per frame;
  4,096 frames per scene.
- **The dump:** at each scene's end, the arrays are printed as `[frame-bench] dump S<k> …` lines (hex, 64 frames a line),
  during the NEXT scene's settle period, which is never measured. Then they are cleared.
- **Its own cost, bounded by a measurement (I-5):** at boot, the bench times 10,000 calls of its per-frame record on the
  device and prints `[frame-bench] record cost <ns> ns/frame`. **Predicted: ≤ 5,000 ns** (two `micros()` reads and
  four stores). Over 50,000 ns (1 % of a 5 ms frame), stop: the instrument is too heavy to be a baseline.

## 4. The scenes

**THE HOME** is set in RAM inside `Initialise()`'s bench region (the radar's own config path, as the map bench did).
NVS is never written, and no coordinate or name is printed.
- **Bend,** the map bench's home, so v20's map scenes compare with it.
- **KSFO,** a dense airport, with many airports drawn.

**THE ZOOM, a mechanism the 10-min idle return CANNOT undo** (today's lesson: AircraftManager.cpp, the idle return).
- At v19, the idle return fires only when `zoomIdx < zoomLadder.Top()` (:1658).
- The bench sets each scene's zoom as the CONFIGURED RADIUS, in RAM, and re-runs `Initialise()`, which rebuilds the
  ladder from that radius (:870) and applies `Top()`.
- So every measured frame has `zoomIdx == Top()`, and the idle condition is false by construction. The bench never
  calls `ApplyZoom` below the top.
- Rungs: **10, 25 and 50 mi.** The phase line prints the rung, and the reader checks that every frame in a scene was at
  its rung (`[zoom]` lines between a scene's settle and its end: 0; any one makes the scene the INSTRUMENT).

**THE TRAFFIC.**
- **(a) FIXED, served reproducibly.**
  - **Levels: 20, 40 and 60 aircraft.** 60 is v19's own cap (`MAX_AIRCRAFT = 60`, :360), so 80 cannot be shown.
  - **The source:** v19's local `aircraft.json` path, selected in RAM in `Initialise()`'s bench region:
    `useLocalSource = true`, `localUrl` = the bench host, `localDetails = Off`. The server is a frozen script on the
    bench PC, `fixed_traffic.py`.
  - **The traffic:** n aircraft on deterministic tracks (circles at fixed radii and speeds from a seeded table), each
    position a pure function of wall time. Every fetch returns moving aircraft, and two runs on different days serve the
    same set.
  - **The placement:** all within 0.8 × the smallest rung (8 mi), so n is the same at every rung. The spread on screen
    therefore shrinks at 25 and 50 mi; that is stated, not hidden.
  - **What it CHANGES against the live source:**
    - the fetch is plain HTTP on the LAN, not HTTPS to the Worker, on the background fetch task;
    - the poll is `LOCAL_FETCH_INTERVAL` (1,000 ms, :1335), not the cloud's interval;
    - enrichment is Off (no detail-card lookups);
    - the traffic is synthetic.
  - **What it does NOT change:** the loop, the draw path (the same aircraft list into the same `Draw`), the push and
    the flush. The frame span is the same code.
  - **The bench PC's LAN address** reaches the image by a build flag that is never printed, never committed and never
    in a log line. It is in no record.
- **(b) LIVE**, beside it for realism: the cloud source at each home, at 25 mi. n is whatever is flying. Reported, not
  graded against a level.
- **(c) n** is printed with every sample, so the bar can be stated at a traffic level.

**THE RUN, driven by the bench itself on the 30 s health tick** (nothing on the PC drives the board):

| block | scenes | each | total |
|---|---|---|---|
| CTRL | Bend, 40 fixed, 25 mi: once plain, once with a planted +10.0 ms per frame | 45 s settle + 120 s measured | 5.5 min |
| FIXED | 2 homes × 3 levels × 3 rungs = 18 | 45 s settle + 120 s measured | 49.5 min |
| LIVE | 2 homes, 25 mi | 45 s settle + 120 s measured | 5.5 min |

About **61 min** of trace. The window adds the two writes (this image, then v19's restore) and the restore's boot read:
about **75 min** in all. Frames per measured scene: at 12-25 fps, 1,400-3,000, under the 4,096 array.

## 5. The control, first (I-6)

- CTRL's second scene adds `delayMicroseconds(10000)` inside the frame span (a bench line before `RecordFrameUs`).
- **Predicted:** p95(`p2p`) and p95(`fr`) each move by **+10.0 ms ± 1.5 ms** against CTRL's plain scene.
- Outside that, the instrument cannot see a known change of its own size, and nothing after CTRL is read.

## 6. The predictions, frozen before anything is built

**The prior is the firmware's own fitted line**, v19 :2182: p95 ≈ **35.80 + 0.802 × n ms**, with a ±10 ms margin.
- That line was fitted on a heap-starved control-arm board. Its own comment says it needs a re-fit, and this bench is
  that measurement.
- One reading already seen: the map bench image, map off and weather off, 16:59:30Z: frame p95 74.5 ms at n=41. The
  line gives 68.7.

| prediction | value |
|---|---|
| I-1, I-3 | 0 bytes outside the five regions |
| I-5, record cost | ≤ 5,000 ns per frame |
| I-6, CTRL | Δp95 = +10.0 ± 1.5 ms, for `p2p` and for `fr` |
| FIXED, p95(`fr`) by level | n=20: 51.8; n=40: 67.9; n=60: 83.9 ms, each **± 10 ms**, at every home and rung |
| FIXED, p95(`p2p`) − p95(`fr`) | **0 to +2.0 ms** in every scene |
| FIXED, n per measured frame | exactly the level (20, 40 or 60), after settle |
| rebuild frames | **0** in every scene (v19 has no map) |
| `[zoom]` lines in a measured span | 0 |
| overflows (0xFFFF) | 0 |
| LIVE | reported: n, p50/p95/p99/max of both, and the residual from the line. Not graded |

**What the bar question looks like, stated before measuring.** If the line holds, v19 meets "p95 ≤ 50 ms" only below
about **18 aircraft** (35.8 + 0.802 n = 50). At 40 aircraft it is near 68 ms. The baseline says whether that is so on
a healthy board. Setting the bar is the owner's call.

## 7. The verdicts, and the escape hatch

- I-1 or I-3 off: the image is not v19 plus the instrument. Nothing is read.
- I-5 off, or I-6 off: the instrument is not fit. Nothing after it is read.
- A FIXED row off its band: that row stops and is reported; the others are read.
- Any scene with a `[zoom]` line in its measured span, an overflow, or n off its level: that scene is the INSTRUMENT.
- **Anything else: stop and decide.**

## 8. The reader and the rig

- **The reader, `frame_bench_read.py`,** is frozen by sha before the window. It parses the dumps, computes nearest-rank
  statistics per scene, and checks every prediction above. Its selftest plants each failure (a +10 ms shift it must
  measure, a planted `[zoom]` line, n off its level, an overflow, a 0xFFFF value) and must call each red.
- **The capture:** `bench-trace.ps1`, single open. Nothing reads the live log (the 17:20Z lesson). `rigcheck.py`
  (`31c13c3b`) compares the log with its console copy before anything is read.
- **The window:** the owner's terms of 04:29Z, as the step B + C window ran them.
  - The image is written to app0 alone by `app-only-write.py`.
  - The restore is the v19 asset `f6d73e39…`, with the same counts-only boot read: settings kept, no portal.
  - E2:08 is reserved by serial for the whole window.

## Questions, each with the default I take unless ruled otherwise

| | question | default |
|---|---|---|
| Q-FB1 | the homes | Bend and KSFO, the map packages' homes |
| Q-FB2 | the fixed levels | 20 / 40 / 60 (60 is v19's cap) |
| Q-FB3 | the rungs | 10 / 25 / 50 mi |
| Q-FB4 | the gate's statistic | p95 of `p2p`, rebuild frames included, stated at a traffic level |
| Q-FB5 | the bench PC's address | a build flag never printed (alternative: the PC's mDNS name, if v19's local fetch resolves it; not yet read) |
| Q-FB6 | where the code lives | a branch from the tag for v19's image; the same header and hooks on main for v20, slug-less |
