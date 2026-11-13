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

## The ruling (the orchestrator, 19:42Z): accepted with five amendments, frozen here before any code

The defaults for Q-FB1 to Q-FB4 and Q-FB6 are accepted.

**A1, settle long enough for trails.**
- From v19's source (`src/models/TrackedAircraft.h`, the trail note): a trail is an AGE, `TRAIL_MAX_AGE_S` = 90 s,
  holding at most 45 points in a 60-entry buffer.
- `Initialise()` (AircraftManager.cpp 776-1438 at the tag) does not clear `trackedAircraft`. So trails SURVIVE a
  rung change (the same aircraft), and grow from nothing for a level or home change (new aircraft).
- **The settle becomes 100 s for every scene** (90 s plus a margin). The measured span stays 120 s.
- **New row, STATIONARITY, for every FIXED scene:** the first 30 s and last 30 s of the measured span must agree within
  **2.0 ms on p50(`p2p`)** and **4.0 ms on p95(`p2p`)**. Off, the scene is non-stationary and is the INSTRUMENT.
- **The scene order** keeps aircraft persisting where it can: each home runs level 20, then 40, then 60, each through
  rungs 10, 25 and 50.
- **The window, re-stated:** 22 scenes (CTRL 2, FIXED 18, LIVE 2) × (100 s + 120 s) = 80.7 min, plus boot, so about
  **82 min of trace**. With the image's write, the restore and the boot read, about **95 min**. Frames per measured
  span are unchanged (under 4,096).

**A2, n defined.**
- The instrument records, per frame, **TRACKED** (`trackedAircraft.size()`) and **DRAWN** (a bench counter, one
  increment per aircraft the radar actually draws in that frame, in a `FRAME_BENCH` region of the draw loop).
- The synthetic tracks lie inside **8 mi** (0.8 × the smallest rung, 10 mi), so at every rung **drawn == tracked == the
  level**.
- **The levels are graded on DRAWN.** A frame whose drawn count is not the level makes its scene the INSTRUMENT.
- **The server follows the board.** The bench sets `localUrl` in RAM at each scene change to the bench host's
  `/aircraft.json?s=<scene>`. `fixed_traffic.py` serves that scene's level around that scene's home, so the server
  needs no clock of its own. The code stage shows from the source that v19's local fetch keeps the query string. If it
  does not, the code stage registers another channel before anything is built.

**A3, the background load, named.**
- FIXED runs with no TLS cloud feed and no enrichment. A customer's board has both, on the other core and the PSRAM bus.
- LIVE has them. **Reported, not graded:** each LIVE scene beside the FIXED scene at the same home and rung (25 mi) with
  the nearest drawn n, the difference in p50, p95 and p99 of `p2p`.
- That is a KNOWN difference, so the bar's reader sees how much the fixed scenes flatter a real board.

**A4, the bench PC's address.**
- It comes from an UNTRACKED local source at build time: the environment variable `FRAME_BENCH_HOST`, read by a
  framebench-only pre-build step. It is never in `platformio.ini` or any tracked file, never printed, never in a log.
- The framebench ELF and image have no slug and are never published.
- `check_wifi.py` and the device-id guards run before every push, as they do now.

**A5, v20's gate measures a sibling.**
- For each v20 candidate, O11 measures the **framebench sibling of that candidate's tree**: the same tree, plus this
  header and its regions.
- I-3 is shown on that tree each time: the candidate's shipping image is unchanged by the patch, 0 bytes outside.
- I-5's cost bound is carried: the record cost is printed and stays ≤ 5,000 ns.
- **So the measured image is not byte for byte the shipped one.** It is the shipped tree plus a bounded instrument, shown
  each time.

## I-1 as run: OFF, and (D), registered before it runs (2026-11-12, the ruling of 19:52Z)

**I-1, as registered, read OFF (19:50Z).**
- The tag's `blipscope-s3-128` built clean here is 1,932,192 B (`944cd13a6c31…`). The release asset is 1,931,840 B.
- `imgcmp.py` refused as the INSTRUMENT: the date-time regions sit at different offsets.
- Both images' app descriptors are identical: the core build `43a8f6d`, IDF v5.5.4, the same core build date.
- **The premise miss is mine.** `imgcmp.py` was built for local-against-local comparisons, and a local build had never
  been compared byte for byte with CI's.

**What the difference is, read from the two images' strings (counts only):**
- The release holds **35** build-path strings, all under `/home/runner/.platformio` (framework header paths compiled
  into our objects). There is no project path in either image.
- The local image holds the **same 35**, under the local `.platformio` root, written with forward slashes. Their tails
  after the root are **byte-identical** to the release's (197 separators, all `/`, in both).
- The local root is 34 characters; the runner's is 24. 10 × 35 = **350 B** of the 352 B difference.
- An earlier count of "59 local strings, 24 local-only" was my pattern matching non-path bytes, which is alternative (c).

**(D), the single attempt, registered before it runs.**
- **The build:** the tag tree, `blipscope-s3-128`, clean, with
  `PLATFORMIO_BUILD_FLAGS=-fmacro-prefix-map=<the local .platformio root, forward slashes>=/home/runner/.platformio`.
  No tracked file is changed. The local root is not written here: it carries the account name.
- **Predicted:** `firmware.bin` is **1,931,840 B**, the release's length. `imgcmp.py` against `f6d73e39…` reads the
  same length, both date-time patterns at the same offsets, and **0 bytes outside the five regions**. The 2 B left over
  are predicted to vanish as alignment.
- **The separator risk is named and judged absent.** The local strings already use `/` after the root, so no
  backslash survives the mapping.
- **Outside the set** (any residue, a different length, or imgcmp refusing): STOP. The residue is reported by region and
  name, and the baseline falls back to (C) with (B) as its cross-check, as ruled.
- **If (D) passes:** alternatives (a), (b) and (c) are eliminated for the build-path question, and I-1 holds as meant.
  Every framebench build, and every v20 candidate's sibling, uses the same mapping, so A5's I-3 stays local against
  local.

## (D) as run, I-3 as run, and (3): the pinned build time (the ruling of 20:13Z), registered before the builds

**(D), as run (19:55-19:58Z): PASSED as predicted.** 1,931,840 B (`1ee6cd9f…`). imgcmp against `f6d73e39` reads 0
bytes outside the five regions (inside: 32 / 32 / 1 / 5 / 5). The whole 352 B was the framework path root.

**I-3, as run (20:06-20:09Z): OFF by 3 bytes, with the residue named.**
- The tag tree plus the patch, built as `blipscope-s3-128` with the mapping: 1,931,840 B (`27f8b243…`). imgcmp against
  (D): 3 bytes outside, at 0x185C30-0x185C32.
- **They are one literal-pool word** at 0x420F5C30, in `cnx_node_leave`, ESP-IDF's precompiled Wi-Fi library. It
  points at the string "20" in both builds: at 0x3C1664F6 in (D), and in I-3 at file offset 0x30B2. That is byte 18 of
  imgcmp's own "aboutdate" region, WiFiManager's `__DATE__ " " __TIME__`, which read "… 12:08:20" in that build
  ("… 11:57:41" in (D)).
- The linker's string tail-merging served the library's "20" from the timestamp's seconds. **Not the instrument:** the
  changed word is in a precompiled library, and both targets hold identical bytes.

**A KNOWN LIMIT OF imgcmp's FIVE-REGION RULE, for every user of it.**
- The regions cover the timestamp BYTES. They do not cover a POINTER whose target tail-merges into a timestamp
  string.
- A short literal elsewhere in the image (here "20") can be served from a timestamp's last characters. It then
  points into the region in one build and elsewhere in another.
- **The residue's signature:** a literal whose target lies inside a timestamp region and holds identical bytes.
- **The cure is to pin the build time,** not to widen the rule.

**(3), registered before the builds: the build time pinned.**
- **Every build from here** (D′, I-3′, the framebench image, and every v20 candidate's sibling) is made with the path
  mapping AND `SOURCE_DATE_EPOCH=1794441386`. That is the v19 tag's commit time, Nov 11 2026 23:56:26 UTC, set in the
  environment only.
- **(D′)** is the tag's `blipscope-s3-128`, built in a second, clean worktree at the tag. **I-3′** is the tag plus the
  patch, built as `blipscope-s3-128`.
- **The control, C-T:** (D′)'s aboutdate reads "Nov 11 2026 23:56:26", and its software-info reads "23:56:26" and
  "Nov 11 2026". If either shows the wall clock, the toolchain or the build environment ignores SOURCE_DATE_EPOCH:
  STOP.
- **I-1′, (D′) against `f6d73e39`:** 0 bytes outside the five regions. A residue must be of the named class, and is
  then named and STOPPED on, never accepted silently.
- **I-3′, I-3′ against (D′):**
  - The two timestamp regions are no longer excused: **0 differing bytes in aboutdate and in software-info**, and **0
    bytes outside the five regions**.
  - The three ELF-derived regions (`app_elf_sha256` 32 B, the image SHA-256 32 B, the checksum 1 B) are
    **PREDICTED TO DIFFER.** `app_elf_sha256` is the SHA-256 of the whole ELF, debug sections included. The patch's
    inserted lines move the line tables of everything after them, and (D′) is built in another worktree, whose path is
    in the ELF's debug info. The image hash and the checksum follow from it.
  - So the ruling's "byte-identical in full, the same sha256" is predicted false BY MECHANISM, before the run, and this
    prediction replaces it.
  - Any other differing byte: STOP.

## (3) as run, and the rulings of 20:26Z and 20:27Z

**(D′), as run (20:14-20:20Z):** 1,931,824 B, **16 B shorter than the release.**
- **C-T:** aboutdate reads "Nov 11 2026 23:56:26", the pinned time, so the epoch is honoured. But the software-info
  pair is ABSENT.
- **The mechanism:** with one pinned time, WiFiManager's separate `__TIME__` ("23:56:26") is the tail of the aboutdate
  string, so the linker tail-merges it away. The release's two timestamps were compiled 42 s apart, so they never
  merged.
- **So I-1′ is WITHDRAWN as unreachable.** A pinned-time build can never reproduce the release's layout.
- **I-1 stands on (D),** the wall-clock build, 0 bytes outside against the release.

**imgcmp's known limits, for its users:**
- its regions cover the timestamp bytes, not a pointer tail-merged into them;
- **pinning the time merges the software-info pair into aboutdate, so imgcmp cannot compare a pinned build at all.**
  Local-against-local builds with the same epoch are compared with `bindiff_elfhash.py` (`4d86b01d…`), a whole-file
  diff that excuses only `app_elf_sha256`, the trailing image SHA-256 and the checksum byte.

**Every local build from here** carries the path mapping and `SOURCE_DATE_EPOCH=1794441386`: I-3′, the framebench
image, and every candidate's sibling.

**I-3′, I-3′ against (D′), both pinned:** by `bindiff_elfhash.py`. Predicted: the same length, differing bytes only in
the three ELF-hash fields, and 0 elsewhere, the timestamps included.

**(iv), THE RELEASE CROSS-CHECK, registered before its code.** (D′) and the framebench image are not byte-tied to the
release (the merge, the 16 B shift, the instrument's code), and layout can move timing on a flash-cached chip, so this
is measured rather than assumed.
- **The release half, first in the window, with no flash:** E2:08 already runs `f6d73e39`. It is traced as it stands
  for 10 min, and its own `[health] frame avg=… p95=… n=…` lines are read. Ending that trace leaves the board in
  download mode: `esptool read-mac` on E2:08 (on the pinned list) recovers it, logged, before the framebench write.
- **The bench half, a 23rd scene, LIVE-OWN:** `Override()` answers nothing, so the board uses its own home, radius and
  source (the cloud). It runs 100 s of settle, then **300 s measured**. The same v19 `[health]` lines are read: the
  same statistic, from the same code, on both sides.
- **The rule:**
  - per 30 s window, the residual r = p95 − (35.80 + 0.802 × n), the firmware's own line, using that window's n;
  - windows with n < 5 are dropped;
  - each side needs **≥ 8 windows**, and the two sides' n interquartile ranges must overlap;
  - **|median r(bench) − median r(release)| ≤ 3.0 ms.**
- **Outside, or too few windows:** the bench image is not timing-equivalent to the release. STOP, and it goes to the
  owner.
- **The run becomes 23 scenes.** The window grows to about **112 min**: the release trace (10), the recovery, the
  framebench write, about 83 min of the bench's run plus LIVE-OWN's extra 180 s of measure, the restore and the boot
  read.

## The code, the reader and the traffic, frozen; the anchor rule for v20; the window (2026-11-12)

**THE CODE** is on branch `bench/frame-bench-v19` at `a37e99b09fb4`: the v19 tag plus the patch, never merged.
- `include/FrameBench.h` (`0ac551f888a7…`): the scene table (23 scenes, LIVE-OWN included), the RAM overrides, the
  per-frame recorder, the present timestamp, the planted delay, the self-timed cost, and the paced dump.
- The hook regions, applied by `apply_framebench.py` (`4a4237c282c6…`, counted anchors, each file's own line endings):
  7 regions in `main.cpp`, `AircraftManager.cpp` and `ConfigurationWebServer.cpp`; the env
  `blipscope-s3-128-framebench`; and `[frame-bench]` in MARKERS. The same script applies the same regions to a v20
  candidate's tree.
- `scripts/framebench_host.py`: `FRAME_BENCH_HOST` comes from the untracked env var. A refusal names the variable,
  never its value.
- **I-2,** `test/host/check_frame_bench.py`: FB1-FB5 pass on 7 regions. Its selftest plants each row and reads
  exactly that row red.
- **A correction:** a record is **8 bytes**, not the 6 written in section 3 (two uint16, four uint8, for alignment).
  4,096 records are 32 KB of PSRAM per buffer, two buffers.

**THE IMAGES,** each built with the path mapping and `SOURCE_DATE_EPOCH=1794441386`:
- **I-3′, final,** on the final patch: `blipscope-s3-128`, 1,931,824 B (`0d175f0a8d82…`). Against (D′) by
  `bindiff_elfhash.py`: "length 1931824 in both; differing bytes: app_elf_sha256 32, image SHA-256 31, checksum 1, elsewhere 0": PASSED. It has the same sha256 as the first I-3′, because the LIVE-OWN header change cannot reach the shipping image.
- **The framebench image:** `blipscope-s3-128-framebench`, 1,934,560 B, `52b8b9e9a4b6f3c284b031ef7098ddb62a2b8625616cb0ca0ac0bfc9ac90f9e3`. It is never published.

**THE HOST TOOLS, frozen by sha:**

| tool | sha256 | selftest |
|---|---|---|
| `frame_bench_read.py`, the reader | `15681a402e5c48cfab703e91101324f603a43c73384adf439e2b4b4d5f02f028` | 19 checks: the clean run; 3 graded plants (CTRL → {I-6}, BAND → {FIX-b S7}, XGAP → {XCHK}); 14 refusal plants, each confirmed by its own refusal message (BOOT, NOCOST, COST, MISSING, DUMPGAP, DUMPCOUNT, NODONE, DRAWN, ZOOM, OVERFLOW, FEWFRAMES, DRIFT, FEWREL, NOIQR); and the report lines |
| `fixed_traffic.py`, the traffic | `b530faf4df97a9e586f32f3671b79213b84540ded83b3f694a8c0083d11023a5` | deterministic, all moving, within 8 mi, 23 scenes, refusals |
| `bindiff_elfhash.py` | `4d86b01dc854809d433d9a283f72bcc2c88056905fa901976dfc2a41a863071d` | 5 cases |
| `rigcheck.py` | `31c13c3b254df62bfe46d1eec0a163087626353d26b263ee62e521a217b4112a` | 5 cases |
| `tokcount.py` | `97c4cdd2e3d79bd0c237d484fc6e753f7837e6dc747440d394bd8e6b623d7f99` | pass, fail and refuse |

- **The NOIQR plant is the 20:31Z ruling.** The overlap refusal first fired by accident inside BAND's synthetic data.
  It now has its own data and its own expected refusal, and BAND tests only the band.
- **For 5c (#674's rows):** `fixed_traffic.py --serve --port <p>` answers `/aircraft.json?home=bend|ksfo&n=<0..60>`
  for any row, beside the frame bench's `?s=<scene>`.

**THE ANCHOR RULE FOR v20's CANDIDATE SIBLINGS** (p4's M3, `docs/m3-push-task.md` `34efeafae897`, relayed 20:30Z):
- **p2p is anchored on the completion of the frame's push to the panel, wherever that runs.** A candidate that moves
  the push (M3 moves `framebuf::FrameDone()` to the push task's completion) must move the anchor with it. Otherwise
  p2p times the loop, not the glass.
- **`fr` loses the push under M3** (about 25.8 ms), whether or not the glass got faster. So `fr` is never compared
  across that change, and p2p is the only number gated.
- **A new row for candidates, P2P-FLOOR:** under M3, p2p cannot be faster than the push's own measured floor (about
  25.8 ms, by p4's measurement). A p2p below it means the anchor is misplaced: INSTRUMENT. The reader gains that row in
  its candidate version, frozen before that candidate is read.
- **Nothing changes for the v19 baseline.** Its push runs in the loop, before `FrameDone()`.

**THE WINDOW, about 112 min on E2:08:**
- **W0, before the window:** the shas above; `fixed_traffic.py --serve --port 8088` running on the bench PC; E2:08
  reserved by serial.
- **W1:** the release half of (iv). E2:08 runs `f6d73e39` as it stands. A 10 min `bench-trace` is read only for
  `[health] frame` lines. Its end leaves the board in download mode.
- **W2:** `esptool read-mac` on E2:08 (on the pinned list), logged. Then the framebench image goes to ota_0 by
  `app-only-write.py` from a staging folder holding the frozen image and the release's table, re-hashed just before.
- **W3:** one `bench-trace` for the whole run: 90 s to start, 22 scenes × 220 s, and LIVE-OWN's 400 s, about 89 min.
  Nothing reads the live log. The console copy is for nothing but the rig check.
- **W4:** `rigcheck.py` (log against console), then `frame_bench_read.py`. Grading follows, after the window.
- **W5:** the v19 restore (`f6d73e39`, app-only), the counts-only boot read (settings kept, no portal), `read-mac`,
  HTTP 200, release, and the END.
- **A named risk:** the board must reach the PC's port 8088. A firewall rule allows this `python.exe` inbound on the
  Private profile (a read-only check). If the board cannot reach it, the FIXED scenes show 0 drawn, the reader refuses
  on FIX-n, and the window ends with the restore.

## v20's FIXED-(a) scenes: the cloud /blips hook (registration, 2026-11-12, before any code)

**Why the v19 source cannot carry v20's bar.** On v20 the local source turns both layers off by construction. At main
`5cdc8bd8f35d`:
- `WxEnabled()` (AircraftManager.cpp:3303) requires `useCloudSource && !cloudUrl.isEmpty()`.
- `MapEnabled()` (:3431) requires the same, plus a location and no refused allocation.

So v19's FIXED source (section 4 (a): local, `fixed_traffic.py`) would measure v20 with no map and no weather, while the
bar is read with both on. **Ruled (a):** v20's FIXED scenes keep the cloud source and replace only the aircraft feed,
from the same table.

**H1, the hook point.**
- **Where:** one `FRAME_BENCH` region in the fetch task's `else if (req->cloud)` branch (:3019, the `/blips` GET, with
  `BLIPS_LIMIT` at :3038).
- **What it does:** while a CTRL or FIXED-(a) scene is current, the branch does not call `http.GetJson` for `/blips`.
  Instead it fills `doc` in the `/blips` wire shape, `{"v":1,"t":<now>,"a":[[hex, cs, lat×1e4, lon×1e4, altFt, gsKt,
  trackDeg, vrateFpm, category, ageS], …]}`, and marks the result a 200. It then falls through to the SAME
  `CloudFeed::ParseBlips` (:3110) the shipping path runs. `t` is the device's UTC second, and `ageS` is 0.
- **LIVE and LIVE-OWN never take the hook.** They fetch from the Worker as shipped.
- **It keeps:** the cloud source (so both layers' predicates hold); the cloud poll cadence from `/config`; `ParseBlips`
  and the dead-reckoner's stamps; and the wx, map, config and airports fetches over TLS to the Worker, unchanged.
- **IT REMOVES, NAMED:** the `/blips` request itself. That is its TLS request on the keep-alive connection, its body (n
  rows, a few KB) and its wait, on the fetch task on the other core. The connection stays open for the other fetches.
  This is A3's flattery in (a)'s form, one `/blips` request per poll, and H5 bounds it.
- **The table:** C++ in `FrameBench.h`, the formulas of `fixed_traffic.py` (`b530faf4`) in double precision, evaluated
  at the device's UTC second for the scene's home and n.
- **The category value:** whatever `ParseLocalAircraft` makes from `"A3"`, shown from the source at the code stage, so
  the icons drawn are v19 FIXED's.

**H2, the table's cross-check (TBL), from both sides.**
- **TBL-HOST:** a host test compiles the table function (pure C++11) and compares it against `fixed_traffic.py`'s
  `aircraft()`.
  - Both homes, n = 60, at t ∈ {0, 1794441386, 1794441386 + 86,400}.
  - The doubles agree within 1e-9 (degrees, feet, knots).
  - The wire-quantized rows are equal under ONE pinned rounding: C++ `lround` (half away from zero), mirrored on the
    Python side by a named helper, never Python's `round`. A test asserts the case where the two built-ins disagree, so
    the helper cannot be optimised away.
  - Plant: one phase constant changed in a copy of the table. TBL-HOST reads red at exactly that row.
- **TBL-DEV** (the device's libm, not the host's): the framebench image prints, once at boot,
  `[frame-bench] table h=<0|1> sum=<crc32>` over the quantized rows at t = 1794441386, n = 60. The reader recomputes
  each sum from `fixed_traffic.py`. **Predicted: equal for both homes.** Unequal makes the run the INSTRUMENT, and it
  stops. A last-unit flip in the device's libm is the named way this can fail on a correct port.

**H3, I-3 on the shipping image, every time** (A5 carried):
- The candidate's shipping env, built from the patched tree, against the same tree unpatched, with the path mapping and
  the pinned build time, by `bindiff_elfhash.py`: **elsewhere 0**.
- `check_frame_bench.py` gains **FB6**: the hook region exists exactly once, inside `#ifdef FRAME_BENCH`, inside the
  cloud branch. Its plant (the region moved outside the guard) reads FB6 red.

**H4, the guard rows: map and weather present, from the frame buffer and from every frame.**
- **STATE, every measured frame.** The record's `flags` gains:
  - bit 1: `wxShowing == Crop` this frame (the layer copied under the planes, the per-frame cost);
  - bit 2: the map drawn this frame;
  - bit 3: the night poll or the night clock active.

  In a CTRL or FIXED-(a) scene, a measured frame with bit 1 or bit 2 clear, or bit 3 set, makes the scene the
  INSTRUMENT.
- **PIX, the frame buffer** (the bits are the code's claim; this is the glass):
  - **Frames:** `/diag/fb` (served on every radar image) during each home's level-40, 25 mi FIXED-(a) scene and its
    FIXED-L twin, in the SETTLE only (a fetch costs frame time, and nothing measured is touched). Three finished
    frames per scene.
  - **Timing:** keyed on the scene schedule from the trace's start, never on the live log (the capture-rig row).
  - **PIX-MAP:** the map's colours EXCEPT black cover **≥ 20 %** of the disc (r ≤ 118 px) in FIXED-(a), and
    **≤ 2 %** in FIXED-L. The colours are read from the source, as reader2's `--fb` does.
    - **Why black is excluded, measured first.** The palette holds `0x0000`, the radar's own background. With it
      counted, B's map-OFF frames read 67.4-77.6 % map colour against 67.8-77.9 % for map-ON, so a ≥ 5 % rule would
      have passed a frame with no map.
    - **Without black,** measured by `mapshare.py` (`52b42eae217a623408da2d23e99a497218ffcdc6e0c034fd6e8a772d603dbc6c`)
      over the step B and C bench frames at Bend (30 per label each):
      - map-on 61.55-74.57 % (B) and 63.77-68.36 % (C);
      - map-off 0.00-0.98 % (B) and 0.39-0.90 % (C), the radar's own pixels in colours shared with the map.
      - The thresholds sit between those ranges.
    - **KSFO has no measured basis.** It is predicted ≥ 20 % from the land inside 25 mi. A KSFO share between 2 % and
      20 % is outside the set: STOP, and it is reported.
  - **PIX-WX:** markers in the avionics plane colours, none in the layer-off plane colours, and every EXERCISED outline
    pixel black.
    - An outline pixel is exercised when no other marker or label covers it.
    - `WxOutlineNow()` (:5427) is `WxEnabled()`, exactly the predicate the local source loses.
    - Markers are found by `wx-fb-check.py`'s own definition, imported, never copied.
    - BLIND (no outline pixel exercised) is outside the set: STOP.
  - **PIX-ECHO:** echo colours of the look inside the disc.
    - Graded when the held crop has echoes there, reported **DRY** when it has none. DRY is pre-registered: not a pass
      and not a fail.
    - Weather presence then rests on PIX-WX plus the printed `[wx] status=200` and `[wx] layer rebuilt in`.
    - The per-frame cost does not depend on rain: `DrawWeatherUnder` (:3589) copies the whole 115,200 B layer whenever
      a crop shows.
- **The inverse guard for the local block (H6):** STATE bits 1 and 2 clear on every frame. PIX shows no map colour
  and no outline.

**H5, FIXED-(a) against LIVE (XA), with a frozen tolerance.**
- **The line:** at each home, FIXED-(a)'s 30 s p95(`p2p`) windows at 25 mi, levels 20, 40 and 60, fitted p95 against
  drawn n.
- **The comparison:** LIVE's 30 s windows at the same home and rung, each as a residual from that line. n < 5 is
  dropped, as in (iv), and each side needs ≥ 8 windows.
- **Tolerance (default, Q-A3):** the median residual in **[−3.0, +5.0] ms.**
  - The −3.0 is (iv)'s identity gap.
  - The +2.0 above it is for the two loads FIXED-(a) lacks: the `/blips` request and the real traffic's shape (spread
    over the rung, labels, trails).
- **Outside the tolerance:** STOP. It goes to the owner with the numbers. It is a finding about the instrument's
  realism, not a grade of the candidate.

**H6, the v19-comparable local block (FIXED-L).** Six scenes, both homes × levels 20, 40, 60 at 25 mi, run exactly as
v19's FIXED: the local source, `fixed_traffic.py`, `LOCAL_FETCH_INTERVAL` 1,000 ms (:1375), and enrichment off. Both
layers are off by construction and checked (H4's inverse guard). Each is read against v19's own baseline scene of the
same home, level and rung. That is the drawing-code regression with the layers out of it.

**H7, the changes (a) makes that are not the hook, each named:**
- **The cadence.** The cloud poll comes from `/config` (CloudFeed.h:41-44 defaults: active 5 s, idle 15 s after 10 min
  without a touch, night 60 s). Trails are an age (90 s), so the poll sets their point count: about 6 points at 15 s,
  against up to 45 (the trail's cap) under the local 1 s. That assumes one point per position update, which the code
  stage shows from the source.
  - A board left untouched turns idle 10 min after boot. At v19's timing that switch would land inside scene 3.
  - **Default (Q-A4):** a bench region back-dates the last-touch stamp at the run's start, so every scene is in the idle
    cadence, the state a desk board spends nearly all its time in.
  - The scene line prints the cadence values served (counts only). A cadence change inside a scene makes it the
    INSTRUMENT.
- **The 60 level against `BLIPS_LIMIT` = 40.**
  - The Worker returns at most 40 rows. A shipping board exceeds 40 tracked only by retention: W1's release trace showed
    n 40-44.
  - **Default (Q-A1):** the hook serves n rows and ignores `limit`. The 60 row is stated as a stress row that the cloud
    path does not reach.
- **Enrichment.** The scene hexes, `fb0000`-`fb003b`, are outside the non-ICAO range (SpecialAircraft.cpp:76,
  `0x230000-0x2FFFFF`). Under the cloud source, each would be looked up at the production Worker.
  - **Default (Q-A2):** one bench line sends the scene's hexes down the shipping non-ICAO skip. There is then no
    production traffic, no invented body, and labels like-for-like with v19's FIXED.
  - The code stage shows from the source that the enrich gate is one call. If it is not, another channel is registered
    before anything is built.
- **The time of day.** The night poll and the night clock are bit 3 (H4). No sun time is printed.

**H8, the run and the reader.**

| block | scenes | each | total |
|---|---|---|---|
| CTRL | Bend, 40, 25 mi, through the hook: plain, then +10.0 ms planted | 100 s + 120 s | 7.3 min |
| FIXED-(a) | 2 homes × 3 levels × 3 rungs = 18 | 100 s + 120 s | 66.0 min |
| FIXED-L | 2 homes × 3 levels, 25 mi | 100 s + 120 s | 22.0 min |
| LIVE | 2 homes, 25 mi | 100 s + 120 s | 7.3 min |
| LIVE-OWN | the board's own settings | 100 s + 300 s | 6.7 min |

- **Length:** about 109 min of trace, plus the 90 s start. With the writes and the restore, about **125 min** (Q-A5).
- **The reader's candidate version** is frozen by sha before each candidate's read. It gains:
  - P2P-FLOOR (the anchor rule above);
  - STATE, TBL-DEV and XA, each with its own plant read red;
  - PIX, by a separate frame reader with plants:
    - a FIXED-(a) frame with the map's non-black colours set to black (PIX-MAP red);
    - a FIXED-L frame with a planted map patch (the inverse guard red);
    - a frame with the outlines removed (PIX-WX red);
    - an echo-free frame, which must read DRY, never green.
- **(iv) is carried:** LIVE-OWN against the candidate's shipping image's own `[health]` windows, from the trace taken
  when O11 flashes it.
- **The gate's number (default, Q-A6):** the worst p95(`p2p`) of the six level-40 FIXED-(a) scenes (both homes, three
  rungs), against 50 ms. The 20 and 60 levels are reported beside it.
- **Frame-time predictions are not in this registration.** They are frozen per candidate, from p4's sizing, before that
  candidate is read.

**H9, O11's procedure, rewritten.** v20-plan.md item 3 says O11's pre-flight "re-runs the baseline's procedure
unchanged". Under (a) it cannot. The rewrite:
1. Build the candidate's framebench sibling: A5's regions plus H1's hook, H4's bits and H7's two bench lines.
2. Run I-3 (H3), TBL-HOST and FB1-FB6 on that tree.
3. In O11's window, flash the candidate's shipping image and take its `[health]` trace for (iv). Then write the
   sibling, run H8's trace, and restore.
4. Read with the candidate reader. Gate on Q-A6's number. Run XA and PIX as guards.

The one-line change to v20-plan.md item 3 (pointing here) is for its owner to make (Q-A7).

**The escape hatch.** Anything outside the outcomes named above stops the read and is reported, unread. That covers a
TBL-DEV mismatch, a STATE bit unaccounted for, a PIX frame that is neither graded nor DRY, an XA side short of 8
windows, and a cadence the scene line does not predict. A second off reading on a row goes to the owner.

| Q | the question | the default I take unless ruled |
|---|---|---|
| Q-A1 | the 60 level against `BLIPS_LIMIT` 40 | serve n; the 60 row is a stated stress row |
| Q-A2 | the scene hexes' enrichment | the shipping non-ICAO skip, by one bench line |
| Q-A3 | XA's tolerance | median residual in [−3.0, +5.0] ms; outside goes to the owner |
| Q-A4 | the idle switch at 10 min | the last touch back-dated at the run's start: every scene idle |
| Q-A5 | the run's length, ~125 min | the full set for the first candidate; later candidates may drop FIXED-(a) to level 40 at three rungs plus 20 and 60 at 25 mi (10 scenes) |
| Q-A6 | the gate's number | the worst of the six level-40 FIXED-(a) scenes |
| Q-A7 | v20-plan.md item 3 | its owner edits it to point here |

**The ruling (the orchestrator, ~21:28Z): the (a) registration at `bbfb2d365394` is ACCEPTED.**
- **Q-A1:** the default. Serve n and ignore `limit`. The 60 level is a **STRESS row**: on the cloud source a real board
  tops out near 40-44 (`BLIPS_LIMIT` 40 plus retention, as W1's release trace showed, n 40-44). 60 is reachable in the
  field only on a local receiver. The orchestrator is telling the owner.
- **Q-A2:** the default. The scene hexes take the shipping non-ICAO skip, so no synthetic traffic reaches production.
  **H1's "absent" list gains enrichment's background load** beside the `/blips` request.
- **Q-A3:** the default, [−3.0, +5.0] ms. The asymmetry fits: FIXED-(a) carries less background load than LIVE.
- **Q-A4:** the default. The last touch is back-dated, so every scene is in the idle cadence.
- **Q-A5:** the default, on one condition: the six level-40 FIXED-(a) scenes, the gate's inputs, are ALWAYS in the
  set.
- **Q-A6: the owner's,** since it decides whether v20 ships. The default (the worst of the six) is put to him; the
  alternative is the median of the six. **Until he answers, the candidate reader prints both.**
- **Q-A7:** the orchestrator folds "O11 re-runs the (a) procedure" into the next v20-plan amendment.

## The baseline window as run (2026-11-12), and I-6's first off reading

**The capture is clean.**
- W1: the release trace, 10 min, exit 0.
- W2: the framebench image `52b8b9e9` written to ota_0 alone, as predicted.
- W3: one trace, 21:05:56Z to 22:37:57Z, exit 0.
- `rigcheck.py` (`31c13c3b`) PASSES on both logs: the framebench log has 2,244 stamped lines identical to its console
  copy, the release log 101, and both have 0 IOException records.
- The logs' sha256:
  - framebench `4d6cedc842822ead10a546f8d3169eb9019e0bf8289d9cd828534eadedb2d3f4`;
  - release `1893baa9b7d3652b95480d34941aeb08b64e39bb5f6cb4c31c4caf826bc10156`.

**The read** (`frame_bench_read.py` `15681a40`, exit 1; output `4dca0d455ee9…`):

| row | read |
|---|---|
| BOOT | ok |
| I-5 | ok: record cost 337 ns/frame, override cost 981 ns/call |
| RUN | ok: 23 scenes, each complete, run done |
| **I-6** | **RED:** CTRL dp95 `p2p` +11.54 ms, `fr` +11.53 ms, against +10.0 ± 1.5 |

By section 5, nothing after I-6 is graded. **The orchestrator ACCEPTED the reading** as I-6's FIRST off reading. A
second goes to the owner.

### I-6's discriminator, registered before it runs (the outcomes ruled in advance by the orchestrator)

- **The input:** the frozen framebench log above, read only through `frame_bench_read.py`'s OWN `parse()` and
  `rank()` (imported, sha asserted). S0 is CTRL plain and S1 is CTRL+10ms.
- **The quantities:**
  - for `p2p` and `fr`, each scene's p50 (nearest rank), mean and p95, and their deltas S1 − S0;
  - S0's (and S1's, reported) stationarity halves by the reader's own FIX-s method: the first and last 30 s of the span
    by cumulative `p2p`, with Δp50 and Δp95.
- **READ from the source:** the dumps hold no independent measure of the plant's cost. `SelfTime()`
  (`FrameBench.h:184-208`) times `Record` and `Override` only. The plant is `delayMicroseconds(10000)` with no timer
  of its own.
- **The order of evaluation, on `p2p`** (the gate's metric), because (3) is a special case of (2)'s "varies by
  quantile":
  1. **S0 not stationary** (|Δp50| > 2.0 or |Δp95| > 4.0, A1's thresholds): **(2), SCENE DIFFERENCE.**
  2. **(3), NOISE:** the p50 and mean deltas both in [9.5, 10.5], with only p95 above +11.5.
  3. **(1), THE PLANT IS BIGGER:** all three deltas above +10.5, and alike (max − min ≤ 1.0 ms).
  4. **(2), SCENE DIFFERENCE:** the deltas' spread (max − min) over 1.0 ms, and not (3).
  5. **Anything else:** STOP, to the owner.
- `fr`'s classification is computed the same way and printed beside it. If it differs from `p2p`'s, that is outside
  the set: STOP, to the owner.
- **What each outcome means, ruled now:**
  - **(3):** the instrument sees a known change at its own size. I-6 stays recorded OFF, and **the baseline is
    GRADED**, its p95 rows carrying that noise as a stated uncertainty.
  - **(1):** the dumps hold no plant self-timing (above), so (1) cannot be told from a biased instrument. **It goes to
    the owner as the instrument's question.** The baseline is not graded.
  - **(2):** the CTRL is confounded. A new CTRL, interleaved plant-on and plant-off in one scene, is registered for the
    next window. **The baseline is not graded until it passes.**
  - **Anything else:** STOP, to the owner.
- **It runs after W5 and END,** per the ruling: the board's restore comes first.

**W5 as run:**
- f6d73e39 was restored to ota_0 alone, verified. The boot read passes (`tokcount`): v19's banner, credentials kept,
  `[WiFi] CONNECTED`, "fast join OK", and `[ota] … current=19 latest=19` are all present; 0 portal or AP tokens.
- `read-mac` reset the board, HTTP returned 200, E2:08 was released, and the traffic server was stopped.
- END went to the orchestrator and to p4 at 22:42Z.

**THE DISCRIMINATOR AS RUN (22:42Z): (3) NOISE, on `p2p` and on `fr` alike.**
- `i6disc.py` `508bf2f938985cd9996ff7ad04f52e5e90ee7ecd40c301b159d07ad74b21f736`; output `73dc7f1b2ad928abf8898117e821961f594247145454d0ca9ab95dd965ec15e2`.
- S0 is stationary (Δp50 +0.17, Δp95 +1.12 ms, 452 and 450 frames). S1 reads Δp50 −0.04, Δp95 −2.05 (reported).

  | p2p, S0 → S1 | S0 | S1 | delta |
  |---|---|---|---|
  | p50 | 65.13 ms | 75.16 ms | **+10.03** |
  | mean | 66.42 ms | 76.55 ms | **+10.13** |
  | p95 | 79.52 ms | 91.06 ms | **+11.54** |

  The spread is 1.51. `fr` reads +10.02 / +10.13 / +11.53.
- **By the ruling:** the instrument sees a known change at its own size. I-6 stays recorded OFF, and **the baseline is
  GRADED**, its p95 rows carrying that noise as a stated uncertainty.

### Grading past I-6: the reader's change and the uncertainty, registered before the code

- **The uncertainty, U95 = 1.54 ms:** I-6's own p95 excess over the known plant (+11.54 − 10.0), the larger of `p2p`'s
  and `fr`'s.
- **The reader:** `frame_bench_read_b.py`, built from `15681a40` by a counted builder.
  - **One change only:** with the explicit flag `--i6-noise`, an I-6 off reading is still recorded RED (the exit stays
    1), but grading continues past it instead of returning.
  - Every p95 that the report and the FIX-b rows print carries "± 1.54 (I-6)". A FIX-b row within 1.54 ms of its band
    edge, on either side, is marked "within the I-6 uncertainty". Its verdict is unchanged.
  - **No band, threshold or margin moves.**
- **Without the flag, nothing differs:** the selftest output is byte-identical to `15681a40`'s, by a regression
  control shown able to fail once.
- **A new selftest case:** the CTRL plant with I-6 off, read with `--i6-noise`, must grade the remaining rows
  (FIX-b/g/r, XCHK) and still exit 1.
- **Then the read, once:** `frame_bench_read_b.py --i6-noise` over the two frozen logs.
  - Every row is reported as read: the FIX rows, the BAR READOUT at 40 drawn, A3 (LIVE minus FIXED), the REPEAT NOISE
    line, and XCHK.
  - No row is re-read and no band moved after it. A row off goes the usual way: first off reading reported, second to
    the owner.

**ACCEPTED (the orchestrator) and RUN ONCE (22:51Z).**
- **Frozen:**
  - `frame_bench_read_b.py` `b0dbd4ebc163dd3309ee05e7823ed02bb7bb0ba22d1e50ae8fe079aac9707224`;
  - its builder `328bdd4ed3d4f7d41f3c03dbf014e8a7cb31ec76e6eb32d21950f11b0242b60f`;
  - `regress_fbb.py` `3409ff944d667acf687d60ca6a9ba6c57a71b1be68569c76e43e8ab1e88e27d4`.
- **Its selftest:** 20 ok. The new case reads "exit 1, red ['I-6'], the rows past I-6 graded".
- **The regression control:** exit 0. report() is byte-identical without the flag on all 18 plants and both real logs,
  and the selftest is identical except the one new line. Its sabotage reads 5 DIFFERS, exit 1.
- **Output:** `29d24028cec97d836baa48efee8e5033f92550819c8f2b63f81d00e1eeacc779`: 59 graded rows, 10 off, exit 1.

**THE v19 BASELINE, as read** (every p95 ± 1.54 ms, from I-6):
- **Clean:**
  - BOOT, I-5, RUN: ok;
  - FIX-g: 18 of 18 ok (+0.04 to +0.06 ms);
  - FIX-r: 18 of 18 ok (0 rebuild-tagged frames);
  - XCHK: ok, median residual bench −3.08 and release −3.88, gap +0.80 ms; windows 10/23; n IQR (40, 41) and
    (40, 42).
- **I-6:** RED, recorded, ruled (3).
- **FIX-b** (p95(fr) against v19's line, 35.80 + 0.802 n, ± 10): 9 off, each a FIRST off reading.

  | n | line | Bend 10 / 25 / 50 mi | KSFO 10 / 25 / 50 mi |
  |---|---|---|---|
  | 20 | 51.8 | 56.30 / 55.58 / 54.29, all ok | 55.89 / 54.03 / 53.32, all ok |
  | 40 | 67.9 | **81.78** / **78.55**ʷ / 76.61ʷ | **81.20** / 77.05ʷ / 76.50ʷ |
  | 60 | 83.9 | **110.38** / **106.21** / **103.65** | **110.22** / **104.44** / **102.25** |

  Bold = RED. ʷ = within the I-6 uncertainty.
  - The line under-predicts this bench at 40 and 60: the traffic is packed inside 8 mi, and 10 mi is the slowest
    rung.

**THE BAR READOUT, 40 drawn, p95(`p2p`):**

| scene | 10 mi | 25 mi | 50 mi |
|---|---|---|---|
| Bend | 81.83 | 78.59 | 76.66 |
| KSFO | 81.25 | 77.11 | 76.55 |

- That is 12.2-13.1 fps at p95. The worst of six is **81.83 ms**, and the median of six **77.85 ms** (both printed,
  Q-A6).
- Against the owner's 50 ms, v19 is 27-32 ms over at 40. At 20 drawn it reads 53.4-56.4 ms.

**REPEAT NOISE** (S0 against S6, the same configuration): p50 +0.06, p95 −0.93 ms.

**A3, LIVE minus FIXED** (reported): **LIVE is FASTER.**
- Bend: drawn median 16 against FIXED n=20; p95 −7.95 ms.
- KSFO: median 38 against n=40; p95 −15.60 ms.
- LIVE's residuals from the line: −1.00 and −4.82.
- So FIXED does not flatter a real board; it is harsher. Not separated: every aircraft inside 8 mi, the local source's
  fuller trails, and Bend's lower n.
- This bears on the (a) registration's XA interval [−3.0, +5.0], whose sign assumed LIVE slower. It is flagged for a
  ruling before v20's first candidate, and not changed here.

**The ruling (the orchestrator, unstamped, received by 22:55Z): the v19 baseline is ACCEPTED AS READ.**
- FIX-b's 9 off rows are FIRST off readings: the miss is v19's line, not the instrument. Nothing is re-graded and no
  band moves.
- **A3 is the important finding, and it traces to the A2 ruling.** Tracks inside 8 mi, chosen so drawn == level at
  every rung, pack 40 aircraft into a small circle. The labels, trails and markers crowd in a way a real board rarely
  sees, so **the FIXED scenes are a STRESS case, not a typical one.**
- **XA is to be re-registered from A3's measured differences,** with its basis stated, BEFORE v20's first candidate.
  The frozen [−3.0, +5.0] is wrong in sign, and nothing has applied it yet.
- **Which traffic the owner's bar is read on is now his question:** the packed FIXED scenes (strict, a worst case), or
  a realistic spread at 40 (e.g. a recorded real traffic picture replayed deterministically), with packed kept as a
  reported stress row. The orchestrator is putting it to him with these numbers. **`fixed_traffic.py` does not change
  until he answers.**

## The bar's traffic: the recording plan, registered before any code or capture (2026-11-12)

**The owner's ruling (23:00Z, relayed 23:08:44Z), in his words:**
- "Traffic: (b). The bar is read on a recorded real traffic picture at a busy airport where the board holds its full
  ~40 aircraft, replayed identically every time. The recording is chosen and pinned before any v20 candidate is
  measured. The packed scenes and the 60 row stay on every report as labelled stress figures."
- "Reading: the worst of the six scenes (two homes x three zooms) must have p95 at or under 50 ms. The median isn't
  used."

**And (23:10Z, relayed 23:12:10Z):** "before C1, re-read v19 on the pinned real-traffic recording, all six scenes, so
C1 and the rest of 2c are judged on the real worst-of-six number." So this plan and its v19 run come before step D's
code. p4's re-size is frozen against **X = v19's worst-of-six p95(`p2p`) at 40 on the recordings**, reported with the
drawn n of every scene. Anything outside its set stops.

**RP1, the source: OUR Worker's `/blips`, the same answer a board gets.** Never the anonymous upstream (CLAUDE.md:
"never measure the sky through the anonymous endpoint").
- **Recorded on E2:08 itself (Q-RC2 default).** The board runs its own cloud source with the home set to the airport
  in RAM (the bench's `GetStoredString` override, no NVS), and fetches `/blips` exactly as shipped.
- **A `FRAME_BENCH` recorder region** in the fetch task's cloud branch prints each response body, paced as the frame
  dump is: `[frame-bench] rec <seq> <ms-since-start> <part> <hex>`, then a done line with the count.
- **So no device key or test identity is ever used from the PC,** and nothing is fetched by a path a board does not
  take.
- **The recorded radius: 50 mi** (the widest rung; Q-RC5). The Worker returns the 40 nearest within it (`BLIPS_LIMIT`).
- **Length:** 100 s settle + 120 s measured, plus a 60 s margin, so **280 s** per airport, at its busy hour.

**RP2, the airports.**
- **KSFO is one.**
- **The other is chosen by data:** of a candidate list frozen first (Q-RC3), the airport whose `/blips` at 50 mi most
  consistently returns the full 40.
- **The survey:** on E2:08, one bench pass per candidate, 10 polls each at the cadence, at each airport's busy hour.
  It prints counts only: rows per poll, and the rows inside 10 and 25 mi.
- **The choice rule, frozen now:** the highest share of polls at 40 rows; ties broken by the higher median count inside
  25 mi.
- **The survey's numbers and the choice are recorded before the capture.**

**RP3, pinning.**
- **Each recording's sha256** (the decoded body stream, in order) **is frozen in this doc before any v20 candidate is
  measured.**
- **It is never re-recorded** without a new registration.
- **Where recordings live (Q-RC1, a question):** they hold aircraft positions at the wire's 4 decimal places (`lat×1e4`)
  and the request's home at 2. CLAUDE.md's standing rule is "no coordinates beyond 2 decimal places in logs, docs or
  tool output".
  - **The default:** the recordings live only in the bench capture folder and `_handoffs`, never in the repo, never
    printed in clear.
  - **The doc holds only their shas and counts.** The trace log holding the hex bodies is kept with them under the same
    rule.
  - The rule is read as about places and people, not aircraft in public ADS-B, but that reading is the reviewer's to
    confirm.

**RP4, the replay** (the (a) hook's input, replacing its synthetic table for these scenes).
- **The recording is embedded in the bench image** (flash, about 0.1-0.4 MB; Q-RC4 default) rather than served from
  the PC, so the replay depends on nothing on the LAN.
- **Each rung replays from the recording's start:** the hook answers each `/blips` poll with the body recorded at the
  same elapsed time since the scene's settle began. `t` is shifted so that now − `t` equals the recording's own data
  age, and each row's `ageS` is kept. **So every replay is identical.**
- **Rungs 10, 25 and 50 mi** at each airport: 6 scenes, "two homes × three zooms".
- **Drawn n is recorded per frame,** as the dumps already do. A rung whose median drawn n is under **35** is named
  "thin" in the report (real traffic thins at the tight zoom). It is still in the six.
- **A check that the replay is what was recorded:** the hook prints a CRC of each served body. The reader compares the
  sequence against the recording's, and any mismatch makes the scene the INSTRUMENT.

**RP5, the v19 run on the recordings (a new window).** v19's framebench sibling plus the (a) hook and the recordings,
with I-3 on v19's shipping image as before.
- **The scenes:**
  - CTRL as an INTERLEAVED plant-on/plant-off pair in one scene: I-6 read (3) NOISE, and this is the confound-free form;
  - the 6 recorded scenes;
  - the packed stress rows at 40 and 60 (3 rungs, Bend), labelled stress;
  - LIVE at both airports;
  - LIVE-OWN.
- **X** = the worst of the six recorded scenes' p95(`p2p`), each scene's drawn n beside it, with U95 stated.
- **XA's m:** LIVE's 30 s windows as residuals from the line of the recorded scenes, per home. **XA's interval =
  m ± (3.0 + U95).** The packed m is on record, not the gate.
- The run's predictions are frozen before its window, from tonight's numbers.

**RP6, XA's METHOD,** as ruled: the number comes from RP5's run and is frozen before v20's first candidate. The (a)
registration's [−3.0, +5.0] is **superseded and never applied.** Tonight's packed m is to be recorded, labelled
"packed: on record, not the gate".

| Q | the question | the default I take unless ruled |
|---|---|---|
| Q-RC1 | the recordings' 4-dp aircraft positions against the coordinate rule | they live only in the capture folder and `_handoffs`, never in the repo, never printed; the doc holds shas and counts |
| Q-RC2 | where the recording is made | on E2:08, by a `FRAME_BENCH` recorder in the cloud branch (no key on the PC) |
| Q-RC3 | the candidate airports for RP2's survey | the 8 busiest US hubs by movements besides KSFO: KATL, KORD, KDFW, KDEN, KLAX, KJFK, KLAS, KCLT |
| Q-RC4 | how the replay reaches the board | embedded in the bench image |
| Q-RC5 | the recorded radius | 50 mi, the widest rung |
| Q-RC6 | the windows | (1) the survey and (2) the captures, at each airport's busy hour, then (3) the v19 run. Three bookings, or (1)+(2) as one if the busy hours align |

**The ruling (the orchestrator, unstamped): the recording plan is ACCEPTED.**
- **Q-RC1:** the default, and the reading is right. Aircraft positions are public ADS-B data, not a person's place;
  the 2-dp rule protects the owner's and customers' locations. The handling is kept anyway: capture folder and
  `_handoffs` only, never in git, never printed in clear, shas and counts in the doc.
- **Q-RC2, Q-RC3, Q-RC6:** the defaults. Each window is granted as requested.
- **Q-RC4:** the default, on the condition that the build reads the recording from an UNTRACKED path, like
  `FRAME_BENCH_HOST`, and that I-3 still holds with `FRAME_BENCH` off.
- **The board's REAL home must never enter a recording:** the home is set to the airport in RAM before the first
  recorded poll.
- **Q-RC5: CHANGED.** "Where the board holds its full ~40" means recording with the query the board SENDS at its
  shipping defaults, read from the source.

**Q-RC5, READ from the source (v19 and main `5cdc8bd8f35d` agree):**
- **The radius:** an unset `radius` reads as **100** (`AircraftManager.cpp:874-875` on main, :841 at v19), and an unset
  or unknown `radius-unit` normalises to **`mi`** (`DisplayUnits.h`: `DefaultUnit()` is "mi").
- So `rangeKmCfg` = 100 × 1.609344 = 160.93 km, and the `/blips` `r` is `lround` of it = **161**.
- **The limit:** **40** (`BLIPS_LIMIT`, main :3038, v19 :2944). `lat`/`lon` go at the wire's 2 dp.
- **So every recording is made with `r=161&limit=40`, the shipping query.**
- **Why that covers every rung:** the Worker returns the 40 nearest within 161 km. A smaller radius's answer is that
  set's members inside it, since the nearest are the nearest. On a real board at the default, zooming keeps this query
  and changes only what is drawn, which is exactly what the replay at a rung shows: tracked = the recording, drawn =
  those inside the rung.

**The amendments these rulings make, before any code:**
- **RP1, the real home:** the recorder prints only inside a recording scene, which starts after the scene driver's
  `Initialise()` has applied the airport home in RAM.
  - Each recorded poll also prints one boolean: "the request's lat/lon equal the scene's airport at 2 dp", never the
    coordinate itself.
  - A single false makes the recording the INSTRUMENT, and it is discarded unread.
- **RP4, the build:** the recording reaches the image through a `FRAME_BENCH`-only pre-build step, the way
  `framebench_host.py` does.
  - The step reads `FRAME_BENCH_REC_<AIRPORT>` from untracked environment variables naming files outside the tree, and
    generates a header under `.pio/build/` (untracked).
  - A refusal names the variable, never the path's contents.
  - `check_frame_bench.py` gains a row with its plant: no recording file and no generated header is tracked.
  - **I-3 is shown with `FRAME_BENCH` off,** as before.
- **A question, not changed here (Q-RC7):** the shipping default VIEW is 100 mi (`Top()` of a 100 mi ladder), the view
  most boards show, and it is not among the six scenes' rungs (10, 25, 50).
  - **Default:** the six stay the owner's "two homes × three zooms" at 10/25/50.
  - A **100 mi** scene per airport is added as a reported row, labelled "the default view", not in the worst-of-six.
  - Whether 100 mi belongs in the six is the owner's call.

**The ruling (the orchestrator, unstamped):**
- Q-RC5 and the amendments are ACCEPTED.
- Q-RC7 is with the owner, with the orchestrator's recommendation of 25/50/100.
- **The rung set is ONE frozen constant; changing it adds no other edit.** Until the owner answers, 10/25/50 plus a
  reported 100 stands.
- GO on the recorder and replay code.

### The recorder, survey and replay code: its design, registered before the code (2026-11-12)

**Where:** a new branch `bench/frame-bench-v19-rec` from `bench/frame-bench-v19` (`a37e99b09fb4`). The baseline
image's code is not changed in behaviour. Its 23-scene table stays the default when no run flag is set.

**THE AIRPORTS, read from the source** (`include/Airports.h` at v19, 2 dp, never printed):
- index 0 is SFO, then 1-8 are ATL, ORD, DFW, DEN, LAX, JFK, LAS, CLT, Q-RC3's list.
- They are copied into `FrameBench.h` as strings. `check_frame_bench.py` gains a row: every copy equals `Airports.h`'s
  value at 2 dp, with a plant (one digit changed).

**THE ONE CONSTANT:** `kRungs[]`, from which every replay scene and the reader's worst-of-six derive. A host check
plants a changed `kRungs[]` and shows the scene table and the reader follow with no other edit.
- **THE OWNER (23:31Z, relayed 23:31:39Z), in his words:**
  > "(b): the bar's six scenes are the two homes at 25, 50 and 100 miles. 100 is the board's default view and where
  > real traffic fills the screen. 10 miles may be reported beside it if it costs no extra bench window; it isn't part
  > of the bar."
  >
  > "v19's re-read on the pinned recordings (before C1) uses the same six scenes, so v19 and v20 are compared on
  > identical ground."
- **So `kRungs[]` = {25, 50, 100}.** `kReportedMi` = 10 is reported beside them, only if it fits inside the same
  windows, and is never in the worst-of-six.
- **The recordings are unchanged:** the shipping query (161 km, limit 40) covers every rung.
- **At 100 mi with real traffic, drawn n should be near the full ~40.** A scene well under it is named, as registered.
  p4's re-size reads X from these six.

**THREE RUNS from one header, selected by a build flag** (each its own slug-less env, `FRAME_BENCH` on):
1. **`FRAME_BENCH_RUN_SURVEY`,** 9 scenes, one per airport. The airport home in RAM; the shipping query (`radius`
   "100", `radius-unit` "mi", cloud). 20 s settle, then 180 s in which each poll prints
   `[frame-bench] survey A<i> poll <k> rows <n> in10 <a> in25 <b> home-ok <0|1>`. Counts only; in10/in25 are the rows
   within 10/25 statute mi of the airport, computed on the device.
2. **`FRAME_BENCH_RUN_REC`,** KSFO and the survey's choice (`FRAME_BENCH_REC_B=<index>`, a build flag). Per airport,
   20 s settle and then **300 s** recorded. For each poll:
   - **if the request's 2-dp lat/lon equal the airport's,** `serializeJson` of the parsed body is printed in paced hex
     parts: `[frame-bench] rec A<i> <seq> <ms-since-settle> <epoch> <part>/<parts> <hex>`;
   - **otherwise** `[frame-bench] rec A<i> skip home-ok 0`, with no body;
   - **so no body from any other home is ever printed.** The reader refuses a recording holding a body without the
     match, or with fewer than 280 s of polls.
3. **`FRAME_BENCH_RUN_REPLAY`,** the v19 run (RP5). Its scene table derives from `kRungs[]`.
   - **The hook:** in the cloud branch, a replay scene's poll is answered by `deserializeJson` of the recorded body at
     the same elapsed time, with `t` shifted to keep the data age. It prints `served A<i> <seq> crc <crc32>`.
   - **The recordings:** a FRAME_BENCH-only pre-build step generates a header under `.pio/build/` from
     `FRAME_BENCH_REC_<AIRPORT>` (untracked paths).
   - **The scenes:** CTRL interleaved (plant on and off in alternating 10 s blocks in one scene), the six (both
     airports × `kRungs[]` = 25/50/100), the 10 mi reported rows (if they fit the window), the packed stress rows,
     LIVE at both airports, and LIVE-OWN.

**AMENDMENT to the replay hook, before its code (by 23:50:25Z):** where the hook sits.
- **The problem:** skipping the real `/blips` GET needs an `if … else` that reaches outside a bench region. That
  breaks FB2 (a region opens with `#ifdef FRAME_BENCH` and closes with `#endif`), and risks I-3's byte-identical
  shipping image.
- **So the replay sits in the existing cloud-poll region, AFTER the parse.** The board makes its REAL `/blips` request
  at the airport, exactly as shipped, over its keep-alive TLS connection. Then, for a REPLAY scene, the region replaces
  the parsed content:
  - `doc` is re-filled from the recorded body at the same elapsed time, with `t` shifted to keep the data age;
  - `res->aircraft` and `res->dataEpoch` are rebuilt by the same `CloudFeed::ParseBlips`;
  - `served A<i> <seq> crc <crc32>` is printed.
- **Its consequence, stated:** in this run the `/blips` request's load is PRESENT, not absent. H1's "absent `/blips`
  load" does not apply to the v19 replay run. Whether v20's (a) hook should move the same way is (a)'s question, not
  this one's.
- **A failed live GET** (no parse) means no replay that poll. Such polls are counted from the trace, not hidden.

**The ruling (the orchestrator, 23:53:37Z): GO on the post-parse replay, with three conditions registered here before
its code.**
1. **IDENTICAL GROUND** (the orchestrator's ruling, not left as (a)'s open question). The owner's 23:31Z words compare
   v19 and v20 "on identical ground".
   - So **v20's replay uses this same mechanism**: the real GET, then the post-parse replacement.
   - **H1's "absent `/blips` load" is retired** for the bar's reading on both sides.
   - If v20's (a) hook cannot do it this way, that is a **stop-and-ask before v20 is measured**, not a stated
     difference.
2. **THE REPLAY'S OWN COST, MEASURED.**
   - **Where it runs, read from the source:** the cloud-poll region is in the fetch task, `osky_fetch`, pinned to
     **core 0** at priority 1 (`AircraftManager.cpp:2799` at v19). The frame loop is on core 1. So the replay's work
     sits OUTSIDE the frame span and contends only for shared resources (the PSRAM bus, the heap), as the GET itself
     does.
   - **What it adds over shipping:** a `deserializeJson` of the recorded body, a second `CloudFeed::ParseBlips`, and a
     CRC32 computed ON THE DEVICE over the body it actually deserialized (never a precomputed constant).
   - **Each poll prints its own duration,** timed with `micros()` around all three:
     `[frame-bench] served A<i> <seq> crc <crc32> us <duration>`.
   - **The replay reader reports, beside each scene's p95(`p2p`):** the replay duration's max and p95 over that
     scene's polls, the same on v19 and v20.
3. **A MISSED REPLAY POLL HAS A REGISTERED RULE.**
   - **A miss:** any of the shipping fetch path's own failure lines, between a REPLAY scene's settle line and its end
     line ("request failed", "returned HTTP", "schema mismatch").
   - That poll's interval is not the recorded picture.
   - **The rule (the orchestrator's suggestion, taken):** any miss in a scene's run, settle or measure, means **that
     scene is re-read** in a later window. A second miss on the same scene goes to the owner, under the retry cap.
     The rest of the run is graded as it stands.

### A/B/A for every before/after between two images (the orchestrator's ruling, 00:11:47Z)

**Why:** 5c's FT2 path, untouched by its fix, read +1.41 and +1.60 ms between the before session and the after
session: separate flashes, separate boots, a 112 B layout shift. Its within-session A/A held (at most 1.06). So a
between-session offset of about 1.5 ms is plausible, and nothing measured it.

**The rule, as it applies here:**
- **The bar itself is absolute** (worst-of-six p95 ≤ 50 ms). The v19 replay run (RP5) reads X against the bar, so it is
  unaffected.
- **Any v19-vs-v20 DELTA reported from this bench** carries either v19 read AGAIN after v20 in the same window (A/B/A:
  v19, v20, v19), or a stated between-session spread beside it.
  - The registered default for v20's first candidate is **A/B/A in one window**.
  - The before-to-before spread (v19's two reads) is the noise the delta is read against.
  - A delta inside that spread is reported as "not separated from the between-session noise".
  - **Refined (00:17:57Z):** the error bar is **E = max(spread, U95)**, with U95 = 1.54 ms from I-6. A single pair's
    spread can come out lucky-small, and it must not narrow the bar below the instrument's floor. "Not separated"
    means |delta| ≤ E.
- **Per-change frame-time reports** (the standing rule, step D's terrain shading among them) run A/B/A whenever two
  IMAGES are compared. That is registered with step D in docs/blipscope-map-client-steps.md.
- Nothing already registered and run is re-graded.

### p2p under M3, for v20's framebench: registered now, applied when framebench carries main (not before C1)

**The ruling (the orchestrator, 23:50Z, corrected 23:55:42Z):**
- `bench/frame-bench-v19` stays v19-based and M3-free, as the v19 baseline.
- When framebench carries main for v20, it carries M3 (draft #687), because v20 ships M3.
- p2p is stamped at the push's COMPLETION (DMA done), which is the glass, not at the loop's hand-off. The correction:
  M3 drops no frame (`HandOver` blocks on the one-slot queue), so the two stamps share their LONG-RUN period. The
  completion stamp is still right, because the glass is where the interval's jitter lives.

**The hook point** (p4, 23:55Z, tree #687 `3bc7ec26fe28`, `src/main.cpp`):
- `Present()` goes in `pushtask::Task` (the push task, core 0), right after the DMA completes, between
  `framebuf::FrameDone();` and `xSemaphoreGive(g_free[i]);`.
- The anchor is unique in that tree, with an 8-space indent:
  `"        framebuf::FrameDone();\n        xSemaphoreGive(g_free[i]);\n"`.

**M3-1, THE STATIC CHECK, on the applied tree** (it guards the anchor trap):
- `apply_framebench.py`'s v19 Present anchor, `"  appManager.RecordFrameUs(micros() - frameStartUs);\n"`, STILL matches exactly
  once under M3. It would apply silently and stamp the loop's hand-off. That is CLAUDE.md's editor-anchor row:
  unique-match establishes unambiguity, not location.
- So:
  - the applier REFUSES the loop anchor wherever the tree defines `BLIPSCOPE_PUSH_TASK`, and requires the push-task
    anchor instead;
  - `check_frame_bench.py` gains a row: `framebench::Present(` appears exactly once, inside `pushtask::Task` after
    `FrameDone()`, and nowhere on the loop task;
  - its plant (Present at the loop's hand-off) must read red.

**M3-2, THE CROSS-TASK HAND-OFF:**
- `Present()` now runs on the push task while `Record()` runs on the loop. Loop frames and presents are no longer one
  event, so a `Record` could read the previous present's p2p, or the same one twice.
- **Registered:** `Present()` publishes {the latest p2p, a present counter} under a spinlock (`portENTER_CRITICAL`).
  `Record()` reads both, and marks its frame in `flags`: bit 2 means no present since the last record, bit 3 means two
  or more.
- **Predicted:** under M3's lockstep pipeline, those two flags on under 1 % of measured frames. Over 1 % makes the scene
  the INSTRUMENT.
- **The alternative, named:** the push task records its own p2p series, independent of loop frames. The default above
  is p4's suggestion, and the alternative is held for a ruling if the flags read above 1 %.

**M3-3, THE COUPLING CONTROL: a planted push-task delay.**
- **Site:** after the DMA and before `t2`. The anchor is `"        tft.endWrite();\n        const uint32_t t2 = micros();\n"`.
  It is NOT M3-9's `Check(p)`, which runs there only every 30th push, in M3_BENCH envs.
- **The prediction's shape,** in CTRL (Bend/40/25 FIXED, draw-bound under M3 at about 52 ms by p4's estimate):
  p95(`p2p`) with the plant = max(p95 under M3, the push's p95 + D) ± U95.
  - **D = 40 ms MUST move p2p** (about 66 against about 52);
  - **D = 10 ms must NOT move p2p,** because the glass's period is genuinely unchanged while the loop is the
    bottleneck. That makes it a second, opposite-direction control.
- **The second instrument:** M3's own `[m3] push_avg_us` (every 750 pushes) must move by about D with BOTH plants,
  which proves each plant landed.
- I-6's CTRL+10ms, a LOOP delay, keeps its meaning in a draw-bound scene: +10 ± 1.5.
- The numbers are frozen with the first v20 candidate's reading; the shape is frozen now.
- **What the coupling control cannot exclude:** coupling is not location. A hand-off stamp has the same long-run period
  in a pipeline that drops no frame. So M3-1 (location) and M3-3 (coupling) are both required.

**THE ROWS** (`check_frame_bench.py`, each with a plant that must read red):

| row | what it holds |
|---|---|
| FB-R1 | the survey, recorder and hook regions each exist exactly once, inside `#ifdef FRAME_BENCH`, inside the cloud branch |
| FB-R2 | the recorder prints a body only inside the home-match branch |
| FB-R3 | no recording and no generated header is tracked by git |
| FB-R4 | no region prints a latitude or longitude value |
| FB-R5 | the airport strings equal `Airports.h` at 2 dp |
| FB-R6 | the rung set is the single `kRungs[]` constant |

The existing FB1-FB5 rows stay as they are.

**THE READERS:**
- `survey_read.py` takes each airport's first 10 home-ok polls (fewer: that airport is the INSTRUMENT). It prints the
  share at 40 rows and the median in25, and applies RP2's rule. Counts only.
- `rec_read.py` reassembles each recording, refuses on the rules above, and prints its sha256, poll count and span.
- The replay's reader is registered with RP5's predictions, before its window.

**THE IMAGES:** each with the path mapping and the pinned build time, and **I-3 shown with `FRAME_BENCH` off for each
run's tree** (0 bytes elsewhere).

**THE WINDOWS (Q-RC6):** the survey needs the US evening banks. **00:00-01:30Z** overlaps every candidate's local
evening (ET 19:00-20:30, CT 18:00-19:30, MT 17:00-18:30, PT 16:00-17:30). Tonight's is too close to make with code,
checks and images, and p4 holds the build slot until about 00:25Z. **So the survey window is requested for 00:00Z on
2026-11-14,** and KSFO's capture can share it if the code is ready.

## RP5's registration: the v19 run on the recordings, frozen before its window (2026-11-13)

Everything below is ruled and frozen before the run's image exists. The numbers that depend on the recordings (n̂, the
p95 centres, X̂) come from the reader's `--predict` over the PINNED recordings, and are added to this section before the
window is requested. They are computed by a rule frozen here, and no run data exists when they are added.

### The code, as ruled

Branch `bench/frame-bench-v19-rec`. Each commit passed `check_frame_bench.py` on its committed tree and I-3 against
D′ by `bindiff_elfhash.py` (elsewhere 0):

| commit | what it does | the ruling |
|---|---|---|
| `41b317b6e5a0` | the replay: the real `/blips` GET, then the recorded body at the same elapsed time, parsed by the same `ParseBlips`; the CRC computed on the device; `served … crc … us …` | 23:53:37Z |
| `be73655e8426` | FB-R3 guards `framebench_rec_gen.h`, not the generator's own name | 00:36:48Z |
| `f7d2acf8243a` | **F1:** the replay runs in the SETTLE too (it had been gated on `measuring`, so every settle showed live traffic, and the recording arrived at measure start with no trails); FB-R9. **F2:** LIVE-APT measures 300 s. **F3:** CTRL-IL is I-6's S0 configuration, packed Bend 40 at 25 mi, local | after 00:45:54Z |
| `d96e29db2ab0` | **(Z):** a REPLAY scene configures the shipping radius (its live GET is the recorded query, r=161) and zooms the ladder to its rung with `ApplyZoom(.., "bench")`, because a configured-radius scene sits at the ladder's Top, where `ZoomCulled` culls nothing; FB-R10 | 01:02:20Z |
| `b417d80cd4c5` | **the customer defaults:** every scene but LIVE-OWN reads what a factory-fresh board reads; the per-scene record; FB-R11 | 01:15:18Z |

`check_frame_bench.py` at `b417d80cd4c5`: 16 rows ok, selftest PASSED with 20 cases, each plant reading exactly its
own row. I-3 at `f7d2acf8243a`: `1cbaaa520e5f`; at `d96e29db2ab0`: `0fdf828e0118`; both "length 1931824 in both;
differing bytes: app_elf_sha256 32, image SHA-256 32, checksum 1, elsewhere 0". I-3 at `b417d80cd4c5`: `95e4a45ce300`,
the same.

**Q-RC5's claim corrected (F4).** Q-RC5 said a rung's replay shows "tracked = the recording, drawn = those inside the
rung". That holds only on a ZOOMED view. The bench had configured each rung as the radius at `zoomIdx == Top()`, where
v19's `ZoomCulled()` returns false ("the default view culls nothing", AircraftManager.cpp:5302-5308). So every scene
drew every airborne tracked aircraft. (Z) makes the six literally "two homes × three zooms" of a default board. The
orchestrator recorded the miss as partly its own acceptance.

### The run: 18 scenes, about 77 min of trace

| S | kind | home | level | rung | configured radius | view | settle + measure |
|---|---|---|---|---|---|---|---|
| 0 | CTRL-IL | Bend | 40 packed | 25 | 25 mi | Top | 100 s + 120 s, the plant in alternate 10 s blocks of the measure |
| 1-3 | REPLAY | KSFO | the recording | 25 / 50 / 100 | 100 mi | zoom step 3 / 4 / Top of 5 | 100 s + 120 s |
| 4-6 | REPLAY | the choice | the recording | 25 / 50 / 100 | 100 mi | as above | 100 s + 120 s |
| 7, 8 | REPLAY | KSFO, the choice | the recording | 10 | 100 mi | zoom step 2 of 5 | 100 s + 120 s; reported, never in X |
| 9-11 | FIXED | Bend | 40 packed | 25 / 50 / 100 | the rung | Top | 100 s + 120 s; stress, labelled |
| 12-14 | FIXED | Bend | 60 packed | 25 / 50 / 100 | the rung | Top | 100 s + 120 s; stress, labelled |
| 15, 16 | LIVE-APT | KSFO, the choice | live | 100 | 100 mi | Top | 100 s + 300 s |
| 17 | LIVE-OWN | the board's own | live | its own | its own | its own | 100 s + 300 s; (iv) |

90 s to the first scene, then 15 × 220 s + 3 × 400 s = 4,500 s: **76.5 min.** The ten mile rows cost 7.3 min, which
fits inside the same window, so they are in (the owner: "if it costs no extra bench window").

### The customer defaults (the cloud reviewer's item 2, ruled 01:15:18Z)

- **The fact it rests on:** a factory-fresh board answers "" for every key. So in every scene but LIVE-OWN,
  `Override()` answers "" for every key the scene does not set, and each parse in `Initialise()` applies its own
  default. Nothing is transcribed, and any key missed in a list is defaulted anyway.
- **The board's own, kept:** `cloud-url`, `cloud-key`, `cloud-key-fac`, `opensky-id`, `opensky-secret`. They draw
  nothing.
- **KEEP-CURRENT, exactly six:** `infotext`, `triangle`, `airports`, `trail`, `altcolor`, `highlight`. Their parse,
  `if (!x.isEmpty()) m = …`, keeps the member's current value on "", which after boot is E2:08's saved one. Each is
  answered with its member's initializer, snapshotted at the first `Initialise()`'s entry (main.cpp:720), before any
  parse. All six are `true` (AircraftManager.h:94-103).
- **FB-R11** holds both lists to the parses, with two plants.
- **v19 at its defaults, read from the parses:** the weather layer OFF (v19 is built without `WX_LAYER_SHIPS`); scanline,
  fade, mil-show and autodim on; heli and special off; the info fields at `defaultOn` (callsign, type, operator, speed,
  baro altitude); logbook on (`LOGBOOK_DEFAULT_ON`); brightness 255; night clock, lookup, MQTT and visual-night off;
  north up; no watchlist and no follow.
- **THE RECORD, counts and booleans only:** `[frame-bench] keep defaults: …` once at boot; `[frame-bench] drawing S<k>:
  …` after each scene's `Initialise()` (every drawing member, the info-field bitmask, `ntfy` as set 0/1 and never the
  topic, `follow` 0/1, the watchlist's size, the unit, the zoom step); `[frame-bench] enrich S<k>: submits … tracked …
  type … route …` after each scene's end.
- **The deviations, printed, never silent:**
  - `tz-offset` read at setup (main.cpp:198-199, :296): the board's own, and only the clock's text;
  - the boot brightness carried in NVS (brightcarry): the backlight only;
  - the board's own NVS-held DATA: the logbook's seen types (which contacts carry a NEW flag) and the enrichment
    caches. A fresh board starts these empty;
  - trails and tracked aircraft carried across scenes. They are state, not settings, and the 100 s settle covers them.
- **(a) Enrichment at default:** info-type and info-operator are on by default and need lookups, so the replay's real
  recorded hexes are looked up at the production Worker, as a customer's board at that airport would look them up.
  The `enrich` line records submits per scene, and the tracked contacts with a type and with a route. Route fill is
  production as it is: adsb.lol's routeset was failing on 11-12, and v19 looks routes up only for an inspected contact.
- **(b) The packed rows** ran on 11-12 at E2:08's SAVED settings, and run here at v19's defaults. So PK is reported
  beside 11-12's values, not graded. XCHK, at the board's own settings on both sides, stays the image cross-check.
- **For v20:** each image runs at its OWN defaults; v20 runs with map and weather on, as shipped. A setting whose
  defaults differ between v19 and v20 is a reported difference, not a stop. The scenes and the recordings stay
  identical, which is what "identical ground" covers.

### The reader, `fb_read_rp5.py` (frozen: `8589081a68d809859f938eabe73280585a8a4d249c95c2340c801a46f96d9382`)

**Inputs:** the replay trace; the release trace (W1); both pinned recordings with their pinned sha256 (refused on a
mismatch); the choice index; the measured image's source commit, for the transcription hashes. Reader b's `parse()`,
`rank()`, `stats()`, `health_windows()` and `lines_of()` are IMPORTED (sha `b0dbd4ebc163` asserted), never copied.
It prints statistics and verdicts only: no coordinate, no body, no name, no address.

**THE TRANSCRIPTIONS, per image (item 1):** the reader embeds its transcription of `PredictPosition` and
`GetDisplayPosition` (TrackedAircraft.h), `radarproj::Project` (RadarProjection.h), `ZoomCulled` and the view-radius
terms (AircraftManager.cpp :855-864, `ApplyZoom`'s factor). Each run reads those functions' text from git at the
measured image's commit, hashes it, and prints the reader's hash and the source's side by side. A mismatch is the
INSTRUMENT. A v19 copy never grades v20.

**BOOT and RUN** (else the INSTRUMENT):
- `[build] env=blipscope-s3-128-framebench-replay fw=v19` once; `[frame-bench] the frame bench: 18 scenes` once;
  the `keep defaults` line once, before S0's settle line;
- I-5: record cost ≤ 5,000 ns per frame (over 50,000 is a STOP); the override cost reported;
- TABLE: each settle line names the registered kind, home, level and rung;
- every scene has its settle, measure and end lines and a complete dump; `run done` once;
- KEEP: every scene but S17 has a `drawing` line whose six KEEP values equal the snapshot.

**CTRL-IL, S0** (the 00:57:15Z ruling):
- IL-n: drawn == 40 on every measured frame; IL-z: 0 `[zoom]` lines in the measure; IL-o: no overflow (else the
  INSTRUMENT).
- IL-f: the plant flag (bit 1) runs alternate, the first run OFF, 12 or 13 runs (else the INSTRUMENT). Pairs are runs
  (2j, 2j+1), j = 0..5. The first frame of every run after the first is dropped: the plant sits after the present
  (main.cpp:1006-1010), so that frame's p2p belongs to the previous run.
- IL-s: the off runs' p50(p2p) range ≤ 2.0 ms (A1's p50 threshold; else the INSTRUMENT: a non-stationary control).
- **IL-a:** the pooled Δp50 and Δmean (on − off), p2p and fr, each in [9.5, 10.5].
- **IL-I6:** each pair's Δp50 and Δmean, and the POOLED Δp95, p2p and fr, each in [8.49, 13.08] (I-6's own deltas
  [+10.03, +11.54] ± U95). Each pair's Δp95 is printed, not graded.
- **The sizing reading,** on the frozen 11-12 log with the same pairing and drop (`il_feas2.py`
  `ed537ac06150124d474d30c8cafe4ac6c60f50322cec295394b6770df97e90cd`, output
  `0b956a36c64178e0ba719d3b603734190d95f2b8a857db2e32ea278715fd1d48`), accepted:

  | | p2p | fr | band |
  |---|---|---|---|
  | IL-a pooled Δp50 | +10.05 | +10.04 | [9.5, 10.5] |
  | IL-a pooled Δmean | +10.11 | +10.11 | [9.5, 10.5] |
  | IL-I6 pooled Δp95 | +11.12 | +11.14 | [8.49, 13.08] |
  | IL-I6 per pair Δp50 | 9.81-10.13 (6/6) | 9.82-10.13 (6/6) | [8.49, 13.08] |
  | IL-I6 per pair Δmean | 9.84-10.31 (6/6) | 9.85-10.30 (6/6) | [8.49, 13.08] |
  | per pair Δp95, printed only | 9.05-13.32 (5/6) | 9.05-13.31 (5/6) | not graded: one 10 s block's p95 moves 6.55 ms on stationary traffic |

- **Outcomes:** (A) IL-a and IL-I6 in: PASS. (B) IL-a in, IL-I6 off: IL-I6's first off reading, reported; the rest is
  graded. (C) IL-a off: I-6's row off a second time, so it goes to the OWNER, and nothing after S0 is graded. (D) p2p
  and fr classified differently: an INSTRUMENT question; STOP, to the ORCHESTRATOR.
- **Predicted: (A).**

**EACH REPLAY SCENE, S1-S8:**
- ZOOM: exactly one `[zoom] bench -> <rung> mi (step k of 5)` between the settle and measure lines for a rung below
  100, and none at 100 (else the INSTRUMENT). ZOOM-T: the measure line's stamp minus that line's ≥ 3 s, the 1.5 s toast
  (:5366-5370, :5438) plus a second of truncation at each stamp (else the INSTRUMENT).
- REPLAY FIDELITY (else the INSTRUMENT): every `served` line between the settle and end lines names the scene's
  airport, a seq in its recording, and the CRC-32 of that poll's body; no `replay FAILED`; each served seq is one the
  device could pick at that line's elapsed time (the line's stamp minus the settle line's, ± 2 s).
- THE MISSED-POLL RULE (a MISS: the scene is re-read in a later window, and a second miss on the same scene goes to the
  owner): the first `served` line comes more than 6 s after the settle line; any two consecutive `served` lines, or
  the last one and the end line, more than 7 s apart (the 5 s cadence, the GET's own time, a second of stamps); any of
  `[WARN] Blipscope Cloud request failed:`, `[WARN] Blipscope Cloud returned HTTP`, `[WARN] Blipscope Cloud: blips
  schema mismatch` between the settle and end lines.
- CADENCE: every `[health]` line's `interval=` in the scene is 5000 ms (else the INSTRUMENT, H7 carried).
- R-z, R-o (else the INSTRUMENT); R-r: rebuild-tagged frames 0.
- **DRAWN (3′):** for each measured poll the reader transcribes `PredictPosition` for every row over the poll's display
  horizon, dt in [ageS, ageS + H], with H = the gap to the next served line + 1 s, and projects each position through the
  transcribed `Project` at the scene's view radius:
  - SURE: inside the disc by more than 1 px over the whole horizon;
  - EDGE: within 1 px of the edge, or across it, at some point in the horizon;
  - RETAINED: in the previous poll, not in this one, and inside (kept up to 15 s by the stale rule).

  The row: the scene's median drawn per measured frame lies in [median SURE, median (SURE + EDGE + RETAINED)]. Outside
  that range: the INSTRUMENT, not a frame-time reading. **SEPARATION**, printed per scene: the median tracked beside the
  band. Where the band's top reaches the tracked count, the row says "DRAWN cannot separate" for that scene instead of a
  pass. **Predicted:** in the band everywhere; it separates at 10, 25 and 50 mi; at 100 mi it "cannot separate", by
  construction (the default view culls nothing). **Its red plant:** a synthetic 25 mi scene with drawn = tracked on every
  frame reads the INSTRUMENT on DRAWN.
- n̂ = median SURE + median EDGE / 2, by the same transcription.
- R-dur, reported beside p95(p2p): the replay's own `us`, max and p95 over the scene's polls. **Predicted:** p95 ≤ 10,000
  µs and max ≤ 50,000 µs. Off is a first off reading, not a stop: it runs on core 0, outside the frame span.
- R-b: p95(p2p) within centre ± 10 ms, centre = 35.80 + 0.802 × n̂ − 2.91. The −2.91 is the mean of 11-12's two LIVE
  residuals from v19's line (−1.00 at Bend, −4.82 at KSFO): the recordings are live traffic.
- A median drawn under 35 is named "thin". The scene is still in the six.
- Reported: p50/p95/p99/max of p2p, p95(fr), frames, drawn median/min/max, tracked median, served polls, the `enrich`
  line, the `drawing` line.

**X, the bar's number:** the worst p95(p2p) of S1-S6, nearest rank, printed with its scene, every scene's drawn median
and thin label, and ± 1.54 (U95). Its distance from the bar, X − 50 ms, is printed. **X is FINAL** only if: CTRL-IL
reads (A) or (B); S1-S6 have no INSTRUMENT and no MISS; and XCHK reads ok. Otherwise X is printed as PARTIAL, and not
handed to p4. **X-b:** X within X̂ ± 10, X̂ being the largest of the six centres. **Predicted:** v19 is OVER the bar if
the largest n̂ among the six is 22 or more, since 35.80 + 0.802 × 22 − 2.91 = 50.53.

**THE PACKED STRESS ROWS, S9-S14:** FIX-n (drawn == level), FIX-z, FIX-o and FIX-s (stationarity) carried, each else
the INSTRUMENT; FIX-g and FIX-r carried as rows; CADENCE 1000 ms. PK is reported, not graded, beside 11-12's p95(p2p)
at 25 and 50 mi (40: 78.59 and 76.66; 60: 106.25 and 103.70), labelled "11-12: E2:08's saved settings; RP5: v19's
defaults".

**LIVE-APT, S15-S16:** reported: drawn, p2p and fr, the residual from v19's line, the `enrich` line. CADENCE 5000 ms,
else the INSTRUMENT.

**XA (RP5, RP6),** per airport home:
- the line: least squares of the 30 s windows' p95(p2p) (by cumulative p2p, full windows only) against each window's
  median drawn, over that home's three six-scenes (12 windows);
- LIVE-APT's 30 s windows (10) as residuals from it, dropping windows whose drawn is under 5 or outside the line's
  drawn range (no extrapolation); at least 8 windows a side, else STOP, to the orchestrator;
- the window count, read from the selftest: a window closes at the frame that reaches 30 s, so a 120 s scene gives
  3 full windows (9 per home on the replay side) and a 300 s LIVE-APT gives 9. A live window whose drawn exceeds the
  replay's maximum drops, so XA's STOP is a real possibility when live traffic outnumbers the recording at 100 mi;
- m = the median residual. **XA's interval for v20 = m ± (3.0 + U95) = m ± 4.54**, frozen from this run before v20's
  first candidate. **Predicted:** |m| ≤ 4.54 at each home (live and replayed traffic at the same airport). Off is a
  first off reading; the interval is still computed and frozen, and flagged.
- The packed m, on record and not the gate: LIVE-APT's windows as residuals from the packed rows' line (levels 40 and
  60, extrapolated below 40, and labelled so).

**XCHK ((iv), carried):** S17's `[health]` windows against W1's release trace: r = p95 − (35.80 + 0.802 n); n < 5
dropped; at least 8 windows a side; the n interquartile ranges overlapping; |median gap| ≤ 3.0 ms. Outside, or too few
windows: STOP, to the owner. **Predicted: ok** (11-12: +0.80).

**RECORDED PER SCENE, not graded:** night (the `[health]` ` NIGHT` suffix count) and the `drawing` line (fade among
it). The run's settings are compared with v20's run when v20 is read.

**EXIT:** 0, every graded row in and no MISS; 1, a graded row off or a MISS; 2, the INSTRUMENT or a STOP.

**THE ESCAPE HATCH.** Anything outside the outcomes named above stops the read, and it is reported unread. That
includes a line format the reader does not know, a scene the table does not name, a transcription hash mismatch, an
XA side under 8 windows, and a DRAWN band the transcription cannot compute. A second off reading on a row goes to the
owner.

**THE READER'S SELFTEST:** `--selftest <tree> b417d80cd4c5`, 26 cases, PASSED (output
`f7f60715c9f49b41be71aaa1dd9f20774bd09a0ec30c5a3bc4d57ed01d353be9`). Each case must give its exit code and red set AND
print its own reason's words, because exit codes alone let one plant pass on another row's refusal. That happened twice
while writing it: the first DRAWN plant read INSTRUMENT because no served poll fell in the measure, and the first SEQ
plant was refused by the CRC row.
- the clean run: exit 0, X FINAL, "DRAWN cannot separate" at 100 mi, THIN below, CTRL-IL outcome A;
- INSTRUMENT, each for its own reason: BOOT, KEEP, IL-D (p2p and fr disagreeing, to the orchestrator), CRC, FAILED,
  SEQ (a whole poll at the wrong time), ZOOM, ZOOM-T, DRAWN (drawn = tracked at 25 mi, the ruled red plant), CADENCE,
  HASH (one transcription hash changed), TABLE, IL-f, IL-s, FIX-n, and XA (live traffic above the replay's range: STOP,
  to the orchestrator);
- STOP: IL-C (to the owner);
- graded red, exit 1: IL-B (IL-I6 only), I-5, R-dur, R-b, XCHK;
- MISS, exit 1: the first served line late, a 20 s served gap, and a cloud failure line.

**THE TRANSCRIPTIONS** are hashed at `b417d80cd4c5`, 24 pieces: GetDisplayPosition, PredictPosition, MAX_DR_SECONDS,
Project, ZoomCulled, ProjectCoordinateToScreen, RadarBlipPosition, the view radius (:864-873), ApplyZoom's factor,
PaintAndFadeActive, SWEEP_PERIOD_MS, the ladder's candidates and BuildLadder, ToKm and KM_PER_MILE, KNOTS_TO_MS, the
six ParseBlips row fields, and the airport table. At v19's defaults, paint-and-fade is ON (`displaySweep &&
displayFade`), so DRAWN's horizon starts one sweep period (5 s) before each poll's arrival.

### The window (requested after the recordings are pinned)

- **The image:** the replay env built from `b417d80cd4c5` or its ruled successor, with the pinned recordings
  (`FRAME_BENCH_REC_{A,B}_{FILE,SHA}`), `FRAME_BENCH_REC_B` = the survey's choice, the real `FRAME_BENCH_HOST` (CTRL-IL
  and the packed rows; never printed), the path mapping and the pinned build time. Its manifest is sent and
  acknowledged before the window.
- **W0:** E2:08 reserved by serial; `fixed_traffic.py --serve` on the bench PC. **W1:** the release trace, 10 min,
  for XCHK; `read-mac` recovers. **W2:** the replay image to ota_0 alone (`app-only-write.py`). **W3:** one
  `bench-trace`, about 78 min. Nothing reads the live log. **W4:** `rigcheck.py`, then the reader, after the trace
  ends. **W5:** the v19 restore, the boot read, `read-mac`, HTTP 200, release, END.
- **When:** the recordings' UTC band (00:00-02:00Z), so that LIVE-APT sees the airports at the same hour of the day.
  About 100 min in all.
