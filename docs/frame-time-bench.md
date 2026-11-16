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
- **`fr` loses the push under M3** (about 24.2 ms, `push_avg`), whether or not the glass got faster. So `fr` is never
  compared across that change, and p2p is the only number gated.
- **A new row for candidates, P2P-FLOOR:** under M3, p2p cannot be faster than the push's own measured average in the
  same arm (`push_avg`; 24.18 ms on 3bc7ec26fe28's path). A p2p below it means the anchor is misplaced: INSTRUMENT. The
  reader gains that row in its candidate version, frozen before that candidate is read.
  - (p4 decided both lines on 2026-11-15, before the v20 sibling's freeze. They replace "about 25.8 ms, by p4's
    measurement", which sat 2.1 ms above every push − sync reading. A floor that high would have flagged a correct p2p
    between 24.2 and 25.8 ms as the INSTRUMENT. The floor is `push_avg`, which includes the sync, not push − sync. The
    prose now reads as the runtime row does. No reading changes.)
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
- **THE FT GUARD ROW (2026-11-14T18:04:19Z, on the orchestrator's ruling; registered before the sibling's code).**
  The frame-time rows that run on this sibling (map-server's FT-SEA and FT-HR) are FIXED-(a) scenes with a map package
  swapped in through `Initialise`. They differ in the weather layer, so FT has TWO kinds:
  - **FT-MAP (FT-SEA):** the weather layer pinned OFF by FT-SEA's amendment. H4's FIXED-(a) rule needs bit 1 SET, so
    as written it would read every FT-SEA frame as the INSTRUMENT. **STATE, every measured frame:** bit 2 SET (the map
    drawn), bit 1 CLEAR (no weather crop), bit 3 CLEAR (no night poll, no night clock).
  - **FT-WX (FT-HR):** map AND weather ON, the device's normal load (`docs/basemap-hudson-rule.md:163` at
    `map-server/hudson-rule` `fbcffaa96260`). **STATE, every measured frame:** H4's FIXED-(a) rule exactly, bits 1 and
    2 SET, bit 3 CLEAR.
  - In either kind, any measured frame otherwise makes that scene the INSTRUMENT.
  - **Corrected 2026-11-14T18:08:57Z, before any code or reader exists.** The row as first pushed (`1a286b4b5101`)
    gave both rows FT-SEA's weather-OFF bits, which would have read every FT-HR frame as the INSTRUMENT. Map-server
    caught it from FT-HR's registration.
  - **The kind comes from the record, never from a scene number.** The sibling's scene table marks each FT scene
    FT-MAP or FT-WX, its `drawing` line prints the kind, and the reader picks the guard from that kind. A scene whose
    kind is none of FT-MAP, FT-WX, FIXED-(a) and FIXED-L is outside the set: STOP. The instrument is unreliable.
  - **What sets bit 3 at FT, from the code.** Night follows the scene's home, because the bench answers the
    `latitude`/`longitude` keys with it (`FrameBench.h:213-225` at `b417d80cd4c5`) and `isNightNow` reads them, for
    the cadence (`AircraftManager.cpp:1787`, same commit) and for `nightNow` (:5187). The cadence is not pinned (H7). The night
    clock cannot fire with any airborne aircraft tracked (`NightClockActive()`, :5155-5163 at `3bc7ec26fe28`), and
    FT's 40 drawn are airborne. So bit 3 at FT is the night poll. An FT window is placed in all its homes' daylight
    with at least 10 min clear of each edge. The sun times are placement data and are never committed.
  - **PIX is not part of this row.** Neither FT registration grades a frame buffer, and PIX-MAP's thresholds have no
    measured basis at FT's homes. So FT-WX's weather-ON claim rests on bit 1 alone. PIX-WX for FT-HR is ruled out
    (Q-FT2, the orchestrator, about 18:09Z). FT-HR's reading states that limit.
  - **Red first, in the reader's selftest, before the sibling's reader is used. Eleven cases, on the orchestrator's
    ruling (2026-11-14, after 18:08Z):**
    - **C1, FT-MAP's control:** bit 2 set, bits 1 and 3 clear on every measured frame. **Predicted:** graded.
    - **C2-C4, FT-MAP's one-bit plants,** each on one measured frame: C2 bit 2 clear, C3 bit 1 set, C4 bit 3 set.
      **Predicted:** INSTRUMENT on bit 2, bit 1 and bit 3 respectively.
    - **C5, FT-WX's control:** bits 1 and 2 set, bit 3 clear on every measured frame. **Predicted:** graded.
    - **C6-C8, FT-WX's one-bit plants,** each on one measured frame: C6 bit 1 clear, C7 bit 2 clear, C8 bit 3 set.
      **Predicted:** INSTRUMENT on bit 1, bit 2 and bit 3 respectively.
    - **C9-C10, the cross-plants:** C9 is FT-MAP's control relabelled FT-WX; C10 is FT-WX's control relabelled
      FT-MAP. **Predicted:** each reads INSTRUMENT on bit 1.
    - **C11, an unknown kind.** **Predicted:** STOP.
    - **The full red set, predicted:** INSTRUMENT on exactly C2-C4 and C6-C10 (eight cases, each naming the bit above),
      STOP on exactly C11, graded on exactly C1 and C5.
    - Any other reading of the eleven is outside the set: stop and decide. The reader is not used until all eleven
      read as predicted.

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

**AMENDMENT to Q-RC6 and RP2, before the re-run (2026-11-14T02:53:14Z, on the orchestrator's ruling, unstamped).** It is
registered before any survey reading exists, and the re-run is graded against this text, never against the
paragraph above.
- **Why it moves:**
  - The 00:00Z window was missed, because no turn reached the session.
  - The rebooked window (01:45-03:15Z) died at 02:09:56Z, 23.5 min into the survey trace, when every session
    crashed. E2:08 was restored to v19, boot read PASS.
  - The partial survey is not read.
  - The owner (02:48Z): "S1 21:30Z. If the board is free earlier, start earlier so one retry still fits before S2 at
    Nov 15 00:00Z." S2 stays where it is.
- **The change:**
  - The survey and both captures run at **20:00Z on 2026-11-14**, the US **afternoon** banks: ET 15:00, CT 14:00,
    MT 13:00, PT 12:00. Not the evening banks.
  - No start inside the booking (to 23:45Z) can reach ET 19:00, which is 00:00Z.
  - **The choice rule is unchanged:** the highest share of polls at 40 rows, then the higher median in25, then the
    lower index. Fewer than 6 of the 8 candidates with 10 home-ok polls is STOP. So are the query (`r=161&limit=40`),
    the candidates (Q-RC3), the images and the readers.
- **The predictions, frozen now:**
  - **PA-1:** at least 6 of the 8 candidates have 10 home-ok polls. This is the rule's own floor. Below it is STOP,
    and the survey is the instrument.
  - **PA-2:** at least 4 of the candidates with 10 polls are at 40 rows on all 10. So the choice is made by the in25
    tie-break, not by the share. The basis: at 161 km, a top-8 hub's area in daytime holds more than the 40 nearest.
  - **PA-3, a named guess:** the winner is **A6 JFK**. The basis: the NY terminal area (JFK, LGA and EWR inside 25 mi)
    is the densest of the eight at ET 15:00. The second guess is A1 ATL.
  - **Not graded: "the same winner as the evening banks".** No evening survey exists, so that comparison has no
    reference. By the basis of PA-3 (a terminal area's density persists across banks) I expect the same winner, and it
    is a label only.
- **What each outcome means:**
  - **Any winner is a valid choice.** An airport other than the one the evening would have picked is a different scene
    set (the second recording's home), not a failure. RP5's numbers are computed from the pinned recordings by its
    frozen rule, so they follow the choice.
  - **PA-2 off** (fewer than 4 saturated): afternoon traffic is thinner than the plan assumed. The choice still stands
    by the share. The replay's "thin" label (median drawn n under 35) reports it.
  - **PA-3 off:** a wrong guess, reported. It changes nothing.
  - **A survey STOP (PA-1 off), or a crash, a killed run, or a port that will not open,** is an INSTRUMENT failure. It
    gets the booking's one retry if both runs and their restores still end by 23:45Z. A second failure stops and
    reports.
  - **None of the above** (survey_read.py exits with neither a choice nor a STOP, or the ledger cannot be read): the
    instrument is unreliable. Stop and decide. No retry is taken on it.
- **KSFO** is recorded at PT 12:00 instead of 16:00. Its drawn n is reported by the replay's existing "thin" rule, and
  nothing new is graded on it.
- **The launch:** DETACHED (WMI `Win32_Process.Create`), with its own deadline guard, and E2:08 reserved by its serial
  first. A tool or session crash then cannot kill a trace mid-run. Killing a trace is what puts an S3 into download
  mode.
- **ADDENDUM: which cause each STOP prints (2026-11-14T02:56:46Z).** On the orchestrator's ruling (02:55:49Z), a STOP
  is retried only for an instrument cause.
  - **Every STOP is the instrument's, by construction.** `survey_read.py` qualifies a candidate on its count of
    home-ok polls alone and never on rows. The firmware prints a survey poll line only after `ParseBlips` parsed a
    reply (`FrameBench.h:326`, `b417d80cd4c5`).
  - **Each disqualified candidate prints** `A<i> <code>: INSTRUMENT, <n> home-ok poll(s) (need 10); home-ok 0: <k>`.
    The `<k>` polls were not at the airport's home (home-not-ok). The rest of the shortfall, 10 − n − k when it is positive, is
    polls that never parsed: failed, timed out, or too few at the cadence.
  - **Traffic cannot cause a STOP.** A parsed poll counts at any row count, 0 included, so thin traffic only lowers
    the share at 40 rows. The branch "fine polls, too few qualified" is empty.
  - **Graded:** a STOP whose lines have that shape gets the one retry. A STOP line whose candidate lines do not have
    that shape is the escape hatch: stop and decide, no retry.
  - **An INSTRUMENT line on the scene or `run done` lines** (no STOP line) is a killed run, and retry-eligible, if the
    ledger shows the trace cut short: no `trace exit`, or a non-zero one. After a trace that ended normally (exit 0)
    it is the escape hatch.
  - `survey_read.py --selftest` PASSED at 02:56:46Z, including MANYSHORT: 5 of 8 valid gives STOP, exit 2.
- **S1, FIRST ATTEMPT (2026-11-14): S1's FIRST OFF reading.** Started 17:55:20Z on the owner's ruling ("start S1 now";
  20:00Z became its retry slot), ET 12:55 instead of the registered 15:00. Written before the retry's outcome exists.
  - **The survey graded as registered:** PA-1 ON (8 of 8 candidates with 10 home-ok polls); PA-2 ON (8 of 8 at 40 rows
    on all 10, so the in25 tie-break decided); PA-3 OFF (JFK guessed, ATL second), reported. **CHOICE A5 LAX** (in25
    40.0).
  - **The recording was REFUSED:** `rec_read.py` exit 2, `A0 SFO: INSTRUMENT, poll 31 has parts [1..9] of [10]`.
    - KSFO had 60 polls, seq 1-60. Seq 31 lacks its short final part (10/10); seq 30's and 32's last parts are 83 and
      89 characters. The line is absent from both copies of the trace, nothing stands in its place, and the trace
      holds no overflow or drop line. LAX had 60 polls, seq 1-60, all complete.
    - **Not the firmware's code:** the part loop prints every part unconditionally (`FrameBench.h:413-425` at
      `b417d80cd4c5`).
    - **Two alternatives, both named, NOT separated:** the board's USB-Serial-JTAG TX dropped the line, or the
      host's read lost it. Both copies come from one host read, so the data cannot tell them apart.
  - **The restore PASSED:** v19 `f6d73e391631` to ota_0, verified; CONNECTED, the join, 0 portal-START lines; HTTP 200.
  - **Outside the retry set** (a survey STOP, a crash, a killed run, a port that will not open), so it stopped. **The
    ruling (the orchestrator, after 18:48Z): the registered retry as frozen,** `win_rec.ps1` unchanged. Recording
    the choice alone was refused as new code before a run. Accepting KSFO without seq 31 was refused as a re-grade
    after the result. This is S1's one more attempt (the owner). A second refusal, crash or STOP goes to the owner,
    with no further attempt. The first attempt's records are kept aside.
  - **The retry started 18:49:07Z** (ET 13:49), graded against the same text with its start time stated.

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

### The reader, `fb_read_rp5.py` (frozen: `8589081a68d809859f938eabe73280585a8a4d249c95c2340c801a46f96d9382`; re-frozen at `e15a94a8a8f6` by THE G AMENDMENT below)

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
  the last one and the end line, more than 7 s apart (the 5 s cadence, the GET's own time, a second of stamps; **9 s
  since THE G AMENDMENT below**); any of
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

**THE READER'S SELFTEST** (28 cases since THE G AMENDMENT below)**:** `--selftest <tree> b417d80cd4c5`, 26 cases, PASSED (output
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

### THE G AMENDMENT, and RP5's numbers from the pinned recordings (2026-11-14T20:37:37Z, before any S2 data exists)

- **Why:** RP5's `--predict` on the pinned recordings printed its own precondition, and it FAILED: "largest
  inter-poll gap 7.2 s / 7.1 s (G = 7 s holds if both are <= 6 s)".
  - KSFO's 59 gaps: 3 over 6 s, 1 over 7 s; max 7.2 s, median 5.03 s.
  - LAX's 59 gaps: 5 over 6 s, 1 over 7 s; max 7.1 s.
  - The recordings were made on the same live path the replay's GETs use, and every `served` line follows a live
    GET. The 8 REPLAY scenes span about 1,760 s from settle to end, about 5.9 recordings' worth, so a gap over 7 s was
    near certain. As registered, S2 predicted its own MISS, which would measure the live path's natural gaps, not
    the replay.
- **THE RULE (ruled by the orchestrator, option (a)):** G = ceil(the largest recorded inter-poll gap) + 1 s for the
  stamp's truncation = ceil(7.2) + 1 = **9 s**. Neither recording has a gap over 8 s. It is derived from the pinned
  recordings and frozen before the image or any S2 data. The first-served rule (6 s) is unchanged.
- **The reader, re-frozen:** `fb_read_rp5.py` **`e15a94a8a8f6d0f2da2e083b3860f869e13f778f937871e1cab5e432e7da8c4c`**
  (was `8589081a68d8`). The diff: `GAP_S` 7 to 9, plus two selftest plants. Nothing else changed.
- **The selftest, `--selftest <tree> b417d80cd4c5`: 28 cases, PASSED** (output
  `54fd126f392f3727432bc504fa56642ff671f86a290fbf88568408498cf01e01`). The 20 s GAP plant still MISSes.
  - **The edge controls.** The stamps are whole seconds (`stamp_of` reads HH:MM:SS), so a 9.1 s or 8.9 s gap cannot
    exist in a trace. The nearest representable edge is used: **GAP10**, a 10 s served gap in S2's settle, MISSes
    ("a served gap of 10 s (> 9)"); **GAP9**, a 9 s gap, does not (exit 0, the clean run's words).
  - **Proved able to fail,** on planted copies: with G = 8, GAP9 MISSes and the selftest FAILS; with G = 10, GAP10
    reads clean and the selftest FAILS. So the two controls pin G at exactly 9.
- **RP5's numbers** (`--predict --choice 5`, the recordings KSFO `627119bca445` and LAX `91dd7606e906`; output
  `50efef9421b7`, exit 0). The only line that differs from the reader's first run is G's precondition, which now
  HOLDS ("G = 9 s holds if both are <= 8 s").

  | scene | SURE / EDGE / RETAINED / tracked | n̂ | p95 centre, band (ms) |
  |---|---|---|---|
  | S1 SFO 25 mi | 28.0 / 2.0 / 0.0 / 40.0 | 29.0, thin | 56.15, [46.15, 66.15] |
  | S2 SFO 50 mi | 40.0 / 0.0 / 0.0 / 40.0 | 40.0 | 64.97, [54.97, 74.97] |
  | S3 SFO 100 mi | 40.0 / 0.0 / 0.0 / 40.0 | 40.0 | 64.97, [54.97, 74.97] |
  | S4 LAX 25 mi | 40.0 / 0.0 / 0.0 / 40.0 | 40.0 | 64.97, [54.97, 74.97] |
  | S5 LAX 50 mi | 40.0 / 0.0 / 0.0 / 40.0 | 40.0 | 64.97, [54.97, 74.97] |
  | S6 LAX 100 mi | 40.0 / 0.0 / 0.0 / 40.0 | 40.0 | 64.97, [54.97, 74.97] |
  | S7 SFO 10 mi | 4.0 / 0.0 / 0.0 / 40.0 | 4.0, thin | 36.10, [26.10, 46.10] (never in X) |
  | S8 LAX 10 mi | 13.0 / 1.0 / 0.0 / 40.0 | 13.5, thin | 43.72, [33.72, 53.72] (never in X) |

  **X̂ = S2's centre, 64.97 ms (band [54.97, 74.97]). Against the 50 ms bar, v19 is predicted OVER.**

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

#### THE PRE FIX (2026-11-16, found before the rehearsal, not by it; the orchestrator's (a), re-acked 05:29Z)

- **The defect.** PRE's line in `win_rp5.ps1` (`227c182d6c82`, :99) read
  `$ipNow = if ($cfg.Count -eq 1) { @(...IPAddress }) } else { @() }`. A PowerShell `if` that returns a one-element
  array unrolls it to a bare string. So `$ipNow.Count` was still 1, but `$ipNow[0]` was the address's FIRST CHARACTER,
  and PRE asked whether the replay ELF holds "<one character>:8088". On the real, correct replay image it read
  **False**: the window would have stopped at PRE (exit 7) with nothing wrong, and the rehearsal, a verbatim copy, would
  have read PRE off.
- **The giveaway.** S2b's post-build reader carried the same line. It reported "the address in build.log: 457 lines":
  a single digit matches everywhere.
- **The proof, read-only, no address printed** (`unroll_check.ps1`, 2026-11-16T05:25:32Z):
  - a dummy address through the same form gives `String`, and `[0]` is one character;
  - the fixed form, `@(if ...)`, gives `Object[]`, and `[0]` is the whole value;
  - on `art/rp5-replay`'s ELF, the frozen expression reads False and the fixed one True.
  So the image was right all along: each build script takes the address with a direct `@()`.
- **The fix:** that one token on `win_rp5.ps1:99` (now `d4fafade5397`), `win_rp5_body.ps1:12` (`134f7aaf6bd6`) and
  `rehearse_rp5.ps1` (`1fb6d6549480`, with its R-SRC pin moved), nothing else.
  - The verbatim check reads 19 of 19, with the old line proven absent. The parser reads 0 errors on each.
  - The launcher, the manifest and the feed are unchanged.
  - The rehearsal's predictions are `7143c7700cd7`. The 09:00Z rehearsal is the fix's first live test, and any PRE
    reading but True is a STOP.
  - Every other b1 script was grepped for the form, FT-HR's included, and none carries it.

## C1's bench: p4's M3-3 single scene on the M3 tree, registered before its images (2026-11-13)

**What it is for.** C1 (docs/speed-plan.md 2b, the owner's meanings) reads M3's cut on ONE scene, fixed by M3-3's
registration (docs/m3-push-task.md §6 :119 and its M3-3 row :128): Bend, 40 aircraft, weather on, map off, cloud-fed
through the (a) bench's /blips hook. Not the six recorded scenes, and not CTRL. The bar's own v20 bench keeps its date
(11-15 18:00Z ready, 11-16 00:00Z); C1 runs in its own window, **11-15 02:00-03:00Z**, granted on a frozen manifest.

### The tree, the images and their proof

- **The tree:** p4's M3 tree, `3bc7ec26fe28da66846526adc68f5bd031dcc013` (#687), with THE FRAME BENCH's C1 regions
  applied by `apply_c1.py` (counted anchors) on branch `bench/frame-bench-c1`.
- **The flags (p4):** both images `${s3128_hw.build_flags}` + `-DBLIPSCOPE_EXPECT_BIG_NVS` + `${cloud.prod}` +
  `-DWX_LAYER_SHIPS` + `-DFRAME_BENCH`. **A** adds `-DBLIPSCOPE_NO_PUSH_TASK`; **B** does not. Neither has
  `-DWX_LOOK_AB` (its timers run inside the frame), `-DM3_BENCH` (Check(p), the test frame) or `-DFRAME_PROFILE`.
  `WX_LAYER_SHIPS` is load-bearing: the shipping `[weather] layer` is empty on this tree.
- **The proof, from the BUILT images, never the ini** (one static line per image in the manifest, each check shown
  able to fail on an image where the answer is no): the weather layer present in A and B (a string only that code
  has; the no-control: the tree's shipping image, built without the layer); the push task present in B and absent in
  A (its task name and its `[m3]` print; the no-control: A itself); `[frame-bench]` present in both (the no-control:
  the shipping image).
- **I-3, AS PREDICTED:** the shipping `blipscope-s3-128` built from `bench/frame-bench-c1` (`bfcb47e01c3d`, c1-I3,
  `de75e3606ba7`) against the same env from `3bc7ec26fe28` unpatched (m3-Dp, `fae94d2f70c3`), both with the path mapping
  and the pinned build time, by `bindiff_elfhash.py`: "length 1933952 in both; differing bytes: app_elf_sha256 32, image
  SHA-256 32, checksum 1, elsewhere 0".

**THE IMAGES AND THE MANIFEST** (`build_c1.ps1` `c93f8a94ccc7`; `make_c1_manifest.py`; every write ota_0 alone at
0x20000 on the release's table `28f336fe57a9…`):

| image | sha256 | bytes | write range |
|---|---|---|---|
| c1-A (A1 and A2) | `5eb7c4ad525d0afe58db58999c9186ec8dab599efbd65cfebf9bf397bf3a240a` | 1,963,024 | 0x020000..0x1FF40F |
| c1-B | `1132863bd0dfb5ce42e9da0decd873291ce212b4f9d4c777146d22031e7e7f3a` | 1,964,944 | 0x020000..0x1FFB8F |
| restore-v19 | `f6d73e39163177b0dc25516a104bb02021d2048a94f82a66fc42a90f0a2615be` | 1,931,840 | 0x020000..0x1F7A3F |

`c1_manifest.json` `864eb0e4905b10adf3403898fe8fc9df855e5793771cc6bbf45586562784631b`. `win_c1.ps1`
`e1f6ac41e7deb0f43bf2c5390d3485f5462bb1b5fccd1489ef72644855a079f0`; its sha guard's dry run: B's bytes staged as A
refused (exit 20, nothing written), A and B each matched (exit 0, no port touched).

**THE ARTIFACT LINES, ALL AS PREDICTED** (`c1_artifacts.py`):

| image | push task (`m3push`) | `[frame-bench]` | weather (`check-no-weather.sh`) | bench-hook scan | `--expect "[frame-bench]"` |
|---|---|---|---|---|---|
| m3-Dp (shipping, unpatched) | 1 | 0 | plain: none, exit 0 | exit 0 | — |
| c1-I3 (shipping, patched) | 1 | 0 | plain: none, exit 0 | exit 0 | — |
| c1-A | **0** (the no-control) | 20 | `--expect`: all present, exit 0 | **exit 1, refused** | exit 0 |
| c1-B | 1 | 20 | `--expect`: all present, exit 0 | **exit 1, refused** | exit 0 |

Each check is shown able to fail on an image where the answer is no: the push task on A, the bench marker and the
weather on the shipping two.

### The step-C refusal, and the bench exemption (the orchestrator's ruling with p4's conditions, 2026-11-13)

**The fact.** p4's tree cannot build with weather on. `src/AircraftManager.h` :51-52 refuses any map-fetching build
without step C (`#if defined(BLIPSCOPE_MAP_FETCH) && !defined(WX_LOOK_AB) && !defined(BLIPSCOPE_MAP_STEP_C)`), and CD4
forbids defining `BLIPSCOPE_MAP_STEP_C` anywhere but step C's header. The gate refused correctly: weather + map without
step C must never ship. **C1 therefore measures M3's cut on a tree WITHOUT step C**, as p4's pinned tree is; v20's real
gain is re-read on the real candidate at the bar gate. p4's "with the map off, step C's sweep over weather is
unchanged" is recorded as an ASSUMPTION, not a finding.

**The ruling: option 1, a FRAME_BENCH-only exemption, bench/frame-bench-c1 only, never merged.** Options 2
(`WX_LOOK_AB`, whose timers run inside the frame) and 3 (a tree with step C, a day and C1's slot) were refused.
- **Inside a FRAME_BENCH region:** the region in `AircraftManager.h` defines `BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT`, and
  the refusal's `#if` gains `&& !defined(BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT)`, the one non-region line changed. Any
  image carrying it carries `[frame-bench]`, which check-no-bench-hooks refuses from every slug-ful image: shown by
  `--expect` on c1-A and c1-B and a plain scan of each reading 1 (the artifact lines below).
- **Load-bearing on C1-M:** `MapEnabled()`'s bench line is the only reason the guard's premise cannot occur. The static
  half: C1-M's plant (the line removed) reads red. The runtime half: flag bit 3 (`MapEnabled()`) on NO measured frame,
  with a data-side plant reading the INSTRUMENT.
- **CD4 CARRIES the exemption on the bench branch** (`check_map_draw.py`'s `REFUSAL_IF` and `sabotage_map_draw.py`'s
  `REFUSAL` hold the exempted line, named as changed in the commit): CD4 ok, its sabotages and controls as predicted.
- **C1-X (static, with its plant):** the macro defined exactly once, inside a FRAME_BENCH region, and named exactly once
  more, on the refusal's `#if`.
- **It cannot reach main:** `check_no_bench_exemption.py` (`60b4bb282859`) refuses the macro anywhere in `src/` or
  `include/`, or FRAME_BENCH on the refusal's `#if`. `prepush_frame.ps1` (`7469bef5b609`) runs it for every branch
  that is not `bench/*`. Shown clean on wt-stepc and wt-c1 before the exemption, then REFUSED on wt-c1 after (exit 1,
  :50 and :59), and clean on wt-stepc still. Its CI step on every PR to main follows once C1's images are frozen.

### The scene, and what each image runs

| image | scenes (each 100 s settle + 120 s measure) |
|---|---|
| A (A1, then A2 after B) | C1 |
| B | C1 plain, C1 + push 10 ms, C1 + push 60 ms, C1 + loop 10 ms |

- **The scene's keys, in RAM:** the home Bend (2 dp), radius 25 mi, the cloud source, `wx-layer` "true". Every other
  key is the board's own and is RECORDED: every key `Initialise()` reads during the scene is noted with its value
  where it is short and safe, else "set" / "empty", and the location, identity and topic keys as "bench" or "set".
  The line `[frame-bench] keys S<k>: … map=off (bench override)` names the deviation.
- **Map OFF:** this tree has no map key (`MapEnabled()` :3431 is true whenever cloud-fed with a location), so a bench
  line in `MapEnabled()` returns false, unconditionally in C1's images: the map's buffers are never allocated in any arm.
- **The (a) hook:** in the cloud branch, after the REAL `/blips` GET's `ParseBlips`, the content is replaced by the
  (a) table (fixed_traffic.py's formulas, `include/FrameBenchTable.h`) at the device's UTC second, parsed by the same
  `ParseBlips`. Category 4 (`LocalCategoryToOpenSky("A3")`). Each poll prints `table served n 40 t <s> crc <c> us
  <us>`. **TBL-HOST** (`tbl_host.py`, WSL g++ 11.4 against fixed_traffic.py's own `aircraft()`, its rounding shadowed):
  360 rows, worst |Δ| 1.1e-13, the sum `b1ace60e` equal; its plant (one phase constant) reads red on 354 rows.
  **TBL-DEV:** the image prints `table h=bend n=40 t=1794441386 sum=<crc>` at boot; **predicted `b1ace60e`**; unequal
  is the INSTRUMENT (a last-unit flip in the device's libm is the named way it fails on a correct port).
- **The synthetic hexes never leave the board:** this tree's background enrichment scan has no non-ICAO skip (the
  device-side check is only on the detail card, :10287), so a bench line skips `fb0000`-`fb0027` before the cooldown
  check, and `RequestCloudEnrich` counts any that get through. **Predicted 0 per scene** (`fb submits` in the push
  line); anything else is the INSTRUMENT.
- **THE WEATHER FIELD (the orchestrator's rulings):** a whole `/wx` 200 lands; before `whole` is decided, a bench
  region keeps its 32-byte header (the REAL frame time, so `WhatToShow`'s 600-900 s age limit never hides it, and the
  real geometry) and replaces the body: code 0 everywhere, and ONE cell at code 2, the lowest drawn code, 15 mi from
  the home at bearing 135°, found on the device from the 2-dp home in the header's own geometry (crop.ts :47). Bend at
  25 mi is about 77 × 106 cells and never coarsens under the 32 KB cap, so the geometry is the same in every response
  and arm. The shipping `Validate`, `OnResponse` and rebuild run unchanged. Each response prints `wx served rows <r>
  cols <c> coarsen <k> crc <c> bytes <n>`, the CRC over everything but the 4 frame-time bytes. **The basis, stated:**
  the plan's 13.2 ms weather term came from the 10-29 replay-session arm on production's live crop, 1 echo cell within
  100 km at its start (perhaps never drawn) and 0 at its end, blend 4.45 ms against 4.32 dry: a near-dry layer. Its
  cell had no recorded code, size or position; ours is drawn by construction, about 3.3 × 2.4 px.
- **Rebuilds** follow the real radar cadence (every new real frame is Replaced, WxPolicy.h :152-159): each rebuild
  frame is tagged (flag bit 0), counted per arm beside its p95, and included in the p95 by v19's rule. If A1, B and A2's
  counts differ by more than one, the report says so; the cut is graded either way.

### M3-1, M3-2, M3-3

- **M3-1, the stamp's location (static, FB row with its plant):** `framebench::Present()` appears exactly twice:
  in `pushtask::Task` between `framebuf::FrameDone()` and `xSemaphoreGive(g_free[i])` (B), and after the loop's own
  `FrameDone()` inside its `#if !defined(BLIPSCOPE_PUSH_TASK)` block (A). The plant, a present at the loop's hand-off
  (v19's anchor), reads red. **P2P-FLOOR (runtime):** in B, the median p2p is not below the push's own average; below
  it, the anchor is misplaced: the INSTRUMENT.
- **M3-2, the hand-off:** `Present()` publishes {p2p, a present count} under a spinlock; `Record()` reads both and
  stores the presents since the last record per frame. **Predicted:** in A exactly 1 on every frame (one task); in B
  a count other than 1 on under 1 % of measured frames. Over 1 %: the INSTRUMENT, and the alternative (the push task's
  own p2p series) goes to the orchestrator. **(For push60, and for images that count presents, this is changed by
  THE PACKAGE below, 2026-11-15.)**
- **M3-3's plants:** the push delay by `vTaskDelay(pdMS_TO_TICKS(D))` after `tft.endWrite()` and before `t2` (static
  row M3-3P: a spin there reads red, because the push task outranks the loop on its core); the loop's +10 ms by
  `delayMicroseconds` before `RecordFrameUs` (I-6's). **(The loop plant's site is changed by THE PACKAGE below,
  2026-11-15: immediately before `pushtask::HandOver()`.)**
- **M3-3's second instrument, read two ways (p4's written agreement, quoted below):** (a) `[m3] push_avg_us` is a
  since-boot average (at `3bc7ec26fe28`, main.cpp :151 `g_pushes`, :152 `g_pushUsSum`/`g_syncUsSum`, :153
  `g_pushUsMax`; the push task's sums at :243-246 and its report at :247-256, whose only resets are `g_pushUsMax` and
  `g_waitUsMax` at :255-256), so the reader WINDOWS it between consecutive reports, counting only
  windows wholly inside a measured span and printing how many; (b) the bench's own per-scene push average and push-time
  histogram (1 ms bins), from a B-only region in the push task. p4's bands apply to each; (a) and (b) differing by more
  than 0.5 ms is the INSTRUMENT; a span with no window for (a) is graded on (b) alone, and says so.

> p4: "your finding is CONFIRMED, from the source. In 3bc7ec26fe28's src/main.cpp:150-151, g_pushes, g_pushUsSum and
> g_syncUsSum are never reset. Only g_pushUsMax and g_waitUsMax reset at each report (:252-253). So `[m3]
> push_avg_us` is a since-boot average, and my "+9.0 to +10.0" band applied to the printed line would read OFF on a
> correct plant. That's my miss: I wrote the band against the line's shape without reading its denominator. READ
> BOTH, (a) and (b), as you propose."

(Non-grading note: the quote's line numbers are one early at `3bc7ec26fe28`. Read from that tree, the counters are
:151-152 and the resets :255-256, as cited above; the finding is unchanged.)

### PG-1, C1's substitute (the orchestrator's acceptance; p4's proposal)

> p4: "Your per-frame STATE bit, wxShowing == Crop, on EVERY measured frame. This replaces (a): the layer was
> composited. A /diag/fb PIX-WX read in each arm's span: weather-coloured pixels present. This rules out a dry layer,
> which blends less and reads faster. (b) unchanged. Plants: a weather-off arm's frames (P0-shaped) must read
> INSTRUMENT on the STATE bit, and a dry-layer grab must read INSTRUMENT on PIX-WX."

(Non-grading note, p4's: "(b) unchanged" as quoted here is superseded by the exact (b) below (`2caa62954f87`), because
the original (b) named no line.)

- **STATE:** flag bit 2 (`wxShowing == Crop`) on every measured frame, else the INSTRUMENT. Bit 3 (the map) on no
  frame. Bit 4 (night) recorded.
- **PIX-WX:** three `/diag/fb` grabs in each image's first settle (never in a measured span), read by `pix_wx.py`: in
  the cell's neighbourhood (radius 8 px around its projected position, about (170, 170) at 25 mi), a pixel equal to a
  scaling of code 2's base colour, the colours taken from `scripts/wx-fb-check.py`'s own source derivation (imported,
  sha asserted). Predicted > 0 in each arm; 0 is the INSTRUMENT. Its plant: the same grab with those pixels repainted
  reads 0.
- **PG-1 (b), made exact** (p4's sign-off, docs/m3-push-task.md at `2caa62954f87`, "M3-3 for C1, as ported by
  map-client"):

  > **(b), made exact, per arm, ALL of:** (i) at least one "[wx] layer rebuilt in N ms (look A)" before the span ends;
  > (ii) zero "the layer stays off" and zero "[wx] buffers FAILED" since boot; (iii) every "[wx] status=" line up to
  > the span's end shows 200 or 304, with an outcome that is not error. The reader matches the strings. At
  > `3bc7ec26fe28` they are AircraftManager.cpp :3585, :3555, :3342 and :3420.

  The weather hook replaces the body after the real GET, so the real status line still prints. Each part has a
  data-side plant that reads the INSTRUMENT.

### The grading: p4's text, quoted from `95cf5b27dfb8`

docs/m3-push-task.md :184-194:

> - **The noise:** N = |p95(A2) − p95(A1)|, read before B is graded.
> - **The error bar (the orchestrator's refinement, 00:15:35Z):** E = max(N, U). A single A1/A2 pair can land close by
>   luck, and that must not shrink the bar below the instrument's own floor. U is the bench's within-session U95:
>   **1.54 ms**, I-6's measured p95 noise on map-client's bench (`baseline_b.out`, `29d24028cec9`), or the same
>   window's own CTRL repeat |Δp95| if the run carries one, whichever is larger.
> - **M3's cut:** cut = mean(p95(A1), p95(A2)) − p95(B).
> - **The grade.** C1's band (≥ 20 on track; 10-20 partial; < 10 failed) is assigned only if cut − E and cut + E fall
>   in the SAME band. If they straddle a boundary, the row reports the straddle with both ends and goes to the
>   orchestrator. It is not rounded into a band.
> - **The stop: N > 5 ms.** That is the escape hatch speed-plan 2a uses for the same bench. The bench then cannot grade a
>   cut of this size, the row is INSTRUMENT, and nothing is read from B.

docs/speed-plan.md :216-228 (the owner's meanings):

> **What C1 reads, and what each result means (fixed now):**
> - **M3 cuts ≥ 20 ms:** on track. P4-P6 proceed as sized.
> - **M3 cuts 10-20 ms:** partial (the push task contends, or the overlap is incomplete). L2 becomes the next lever,
>   because it shortens the push the task has to hide. It goes to the owner at C1 with an incoming-inspection row.
> - **M3 cuts < 10 ms:** M3 has failed. Without it, the frame is 86.9 + M; L2 takes ~12.9 ms off, and W and L1 would
>   have to find ~24.0 + M ms between them. **The levers beyond L2 change what is drawn,** so each is the owner's ruling:
>   - **L3, a cheaper sweep blend** (up to −4.6 ms; the blend's look);
>   - **L4, less label and trail work per frame**, for example labels redrawn only when their text changes, or culled
>     where they overlap (the labels' look).
>
>   The date then waits on those rulings.
> - **Outside the set** (M3 makes the frame SLOWER, or the picture changes on the 240 golden): stop, and report to the
>   orchestrator.

The boundaries are exact (p4): a cut − E of exactly 20.0 is "on track". C1's run carries no CTRL repeat, so U = 1.54.

### p4's predictions for B's arms, verbatim (each band ± E)

- **B plain:** "p2p p95 = mean(A1, A2) − cut, with the cut graded in C1's bands. Predicted on track: cut 25.8 − ε, where
  ε ≥ 0 is the PSRAM contention the CTRL argument ignores. [m3] push_avg_us − sync_avg_us = 25.8 ± 1.0 ms, with
  sync_avg reported and expected < 1.0 ms."
- **D = 10:** "push_avg rises by +9.0 to +10.0 ms. p2p p95 does NOT move, within ± E."
- **D = 60:** "push_avg rises by +59 to +60, and p2p p95 = p95(push) ± E, about 85-87 ms. It must sit at least 10 ms
  above B plain. If B plain is already within E of push + D, the control is uninformative: INSTRUMENT, stop."
- **I-6's +10 ms loop plant:** "In a draw-bound scene, Δp2p p95 = +10.0 ± 1.5, and push_avg is unchanged within ± 1.0."
- **Outside the set:** "if p2p p95 (B plain) is within 2 ms of push_avg, B is push-bound and every prediction above
  flips. Stop."
- **A (not graded):** p4's estimate, about 90-95 ms.

### The per-scene rows the reader carries (each else the INSTRUMENT unless named)

BOOT (each image's banner and the bench line naming its scene count and push task); TBL-DEV; I-5 (record cost ≤ 5,000
ns); every scene's settle, measure, end and complete dump; 0 `[zoom]` lines and no overflow in a measured span; drawn
== 40 on every measured frame; STATIONARITY (A1's thresholds, 2.0 / 4.0 ms); STATE; M3-2's present counts; every
`wx served` line equal in rows, cols, coarsen and CRC across the scenes and the three arms, and no SKIP; every `table
served` line, no FAILED, consecutive lines at most 7 s apart and no `[WARN] Blipscope Cloud` failure line in a scene
(else a MISS: that arm is re-run if the window allows, else C1 is not graded and says so); `fb submits` 0; rebuild
frames counted; the keys record present with map=off.

**The escape hatch:** anything outside the outcomes above stops the read, reported unread. A second off reading on a
row goes to the owner.

### The reader and the tools, frozen

| tool | sha256 | its proof |
|---|---|---|
| `fb_read_c1.py`, the reader | `f210ea1cf2effc6aa1b73af9e3e0d12534d176b02ff2d3b7355a7d74d02e6c41` | `--selftest`: 25 cases PASSED, each giving its exit, its red set AND its own reason's words (output `3aaedfe98289…`) |
| `pix_wx.py` | `85620cff73eaf0f7595322a081089e6b17e7b3f764c8949f002d907397238564` | 50 code-2 colours from the C1 tree's own `wx-fb-check.py`; the cell reads 6, the same colour 40 px away 0, the dry plant 0 |
| `fetch_c1.py` | `15c2ec1799801dca2da6392873ebfb279e165d9b4b9404c47c7fbc36c09e9909` | against a local stand-in: Finished kept, unfinished kept and marked, a 503 refused |
| `tbl_host.py` | `f2f838f9a3daf6a53b3ca395314bbdbb0a956adb5869d368ad2636a61a726008` | 360 rows, worst \|Δ\| 1.1e-13, sum `b1ace60e`; its plant red on 354 rows |
| `c1_artifacts.py` | `e352531a9bfcdbaa2b31b8368987dc216f80b398786d9b7541ecf75ef01eaa23` | the artifact lines below, predictions in the script |
| `check_no_bench_exemption.py` | `60b4bb282859534ea89ee1bba9a2410455bd812f7eea0f6b06f0043a529d8e1d` | clean on wt-stepc, REFUSED on wt-c1 |

The reader's 25 cases: the clean run (on track, read (a) over its window, "WITHOUT step C", the ASSUMPTION line);
the INSTRUMENT for TBL-DEV, PG-1 (b)(i), (ii) and (iii), STATE, MAP-OFF, a table hex leaving the board, the weather
field differing, push (a) and (b) disagreeing, N > 5, the keys record missing, M3-2 in A and in B, drawn ≠ 40,
STATIONARITY, P2P-FLOOR, push-bound, M3 slower, and D = 60 uninformative; graded red for I-5, D = 10's push, and the
loop's +10; a MISS for a 20 s table gap; and a STRADDLE, to the orchestrator.

### The window, 11-15 02:00-03:00Z

W0 E2:08 reserved by serial. W1 read-mac, write A (app-only, ota_0), trace ~6 min (1 scene), with three `/diag/fb`
grabs by `fetch_c1.py` at 120, 140 and 160 s after the trace opens (inside S0's settle, timed from the trace's start,
never from the live log); read-mac recovers the download mode. W2 B the same, ~18 min (4 scenes), grabs in S0's settle. W3
A again (A2), ~6 min, grabs. W4 the v19 restore, its boot read (the accepted tokens), read-mac, HTTP 200, release, END.
About 39 min; the ungraded 100 % stress pair runs only if the graded set and the restore still fit.

### C1's window, run 2026-11-14 19:40:56-20:15:00Z: C1's FIRST OFF reading (INSTRUMENT)

Moved forward under the owner's standing rule (a window starts once its dependencies are done and its board is free).
- **The instruments, as registered:** rig check PASS on A1, B and A2; PIX-WX above 0 in each arm, `[9, 15, 15]`
  from nine distinct grab files; the reader's selftest PASSED, output `3aaedfe98289`. The restore PASSED (boot read,
  HTTP 200 at 20:14:55Z).
- **The read:** `fb_read_c1.py` exit 2, `INSTRUMENT / STOP: A1 S0 C1: a frame with drawn != 40`. Nothing is read from
  B.
  - Every measured frame in every arm (A1 S0; B S0-S3; A2 S0) holds tracked = 1 and drawn = 1, by the reader's parse
    and by a hand decode of the raw dump.
  - The board's `[perf]` reads `ac=40/40` (rows received and kept per poll) and `table served n 40`, and TBL-DEV's sum
    `b1ace60e` matched.
- **The named cause, from the code:** the (a) hook adds `r.hex` and `r.flight` from a loop-local `const fbtable::Row r`
  (`FrameBench.h:312-315` at `bfcb47e01c3d`). ArduinoJson 7.4.3 adapts a `const char (&)[N]` as
  `RamString(p, N - 1, true)` (`Strings/Adapters/RamString.hpp:90-95`), which stores it BY POINTER, as a literal. So
  every row's hex and flight point at r's one stack slot, and the hex-keyed tracked set keeps one aircraft. TBL-DEV
  and the served CRC are computed from `fbtable` directly, not through the doc, so both pass.
- **The alternative, named and still open:** something after the parse collapses 40 distinct hexes into one entry.
- **The same lines** are on `bench/frame-bench-levels` `40f02c0fa812` and p4's `bench/frame-profile-p1`
  `ef79bf8282fd` and `-levels` `6979d2d7ff5b`. None of their images ran on a board (p4's audit and C1-L's table).
  The v19 benches do not carry the lines.

**THE DISCRIMINATOR, registered before its code (the orchestrator's ruling, after 20:20Z).** `c1_discrim.py` builds the
doc exactly as the hook does and counts the distinct `a[i][0]`.
- **How:** the hook's row loop is copied verbatim from `FrameBench.h` at the named commit, between two anchors that
  each occur exactly once. It is compiled on the host with that commit's `FrameBenchTable.h` and the image's
  ArduinoJson 7.4.3 at `-Os`, as the image is. The count is taken right after the loop, in the same function, where
  the hook's `ParseBlips` reads the doc.
- **Predicted on `bfcb47e01c3d` (today's hook): 1 distinct hex in 40 rows.** That is the stated cause, read red.
- **Its control:** the same copied loop with `r.hex` and `r.flight` cast to `const char*` (a pointer ArduinoJson
  copies). **Predicted: 40 distinct.** This shows the counter can read 40.
- **The meanings:** 1 and 40 confirm the cause. 40 on today's code is the alternative: the doc is built right, so the
  collapse happens after the parse. Then it stops and goes to the orchestrator. Any other count, an anchor that is not
  found exactly once, or a compile failure is outside the set: stop and decide.
- **After it:** the fix (a copied string), with the same test reading 40 on the fixed code. Then new C1 images, a new
  manifest, p4's sign-off and a new C1 window. A second OFF on C1 goes to the owner.
- **READ, as predicted:** `c1_discrim.py` v1 (`e6ded270d311`) at `bfcb47e01c3d` exits 0. It copies an 18-line block
  (sha `4619171116bf`; FrameBench.h `332128cf1dee`, FrameBenchTable.h `bade87e921d4`, ArduinoJson 7.4.3 asserted).
  TODAY reads 40 rows, 1 distinct hex, 1 distinct pointer. CONTROL reads 40 rows, 40 distinct hexes and 40 distinct
  pointers. **The stated cause is confirmed and the alternative is excluded:** the doc itself carries one hex.

**THE FIX, registered before its code.**
- **The change:** on `bench/frame-bench-c1` from `bfcb47e01c3d`, exactly two lines of `include/FrameBench.h` change:
  `e.add(r.hex);` becomes `e.add(static_cast<const char*>(r.hex));`, and the same for `r.flight`. ArduinoJson copies a
  `const char*`. No scene, Kind, Override or table line changes.
- **Predicted, each read after the commit and before any build:**
  - the commit's diff against `bfcb47e01c3d`: 1 file, 2 lines changed, nothing else;
  - `check_frame_bench.py` at the fix: the same rows as at `bfcb47e01c3d`, 0 failed;
  - `c1_discrim.py` v2 (`fb202ecd1e87`) `--fixed` at the fix: TODAY 40 distinct hexes in 40 rows, and its PLANT (the
    casts removed, the old pattern restored) 1 distinct hex. Exit 0;
  - v2 without `--fixed` at the fix: STOP, exit 2, because the old lines are absent. That is the refusal control. v2
    without `--fixed` at `bfcb47e01c3d` still reads 1 and 40, exit 0, the same as v1. Its `--fixed` mode there
    refuses with exit 2;
  - C1's scene-list lines and its Kind enum and arrays are byte-identical to `bfcb47e01c3d` (`c1_lines_diff.py`:
    "0 lines differ"; its no-control is non-zero).
- **The images,** built only when the bench PC is free (after 5c's (d)) and free space reads at least 30 GB twice:
  - c1-A and c1-B from the fix commit. Their shas differ from `5eb7c4ad525d` and `1132863bd0df`;
  - I-3: the shipping `blipscope-s3-128` env at the fix against `bfcb47e01c3d`'s reads "elsewhere 0", because the
    fix is inside `FRAME_BENCH` code;
  - `c1_artifacts.py`'s artifact lines as before.
- **Then:** a new manifest, acknowledged; p4's sign-off; a new C1 window, proposed under the standing rule.
- **Outside the set:** any other diff, a red row, a discriminator count other than those predicted, I-3 not 0
  elsewhere: stop and decide.
- **The same two lines** sit on `bench/frame-bench-levels` (`40f02c0fa812`), the base for p4's profile trees. The same
  fix there is proposed for a ruling, and its sha goes to p4.
- **READ, all as predicted:** the fix `a599fcaaffc6` (2 lines; 13 rows, 0 failed at both shas; v2 `--fixed` 40 / 1;
  lines-diff 0). Images from tree `3916897f20c9`: c1-A `d0b040058235`, c1-B `13001c132f44`. I-3 is
  byte-identical to `bfcb47e01c3d`'s c1-I3 (`de75e3606ba7`). `c1_artifacts.py` reads all as predicted. The manifest
  is `ce0ac88aba14`, acknowledged; p4 re-signed at `81b160e48613`. The levels fix `a9894185b2a8` was read as
  predicted too.

### C1's retake, 2026-11-14 21:31:51-22:06:31Z: C1's SECOND OFF reading (to the owner)

The fixed images, the acknowledged manifest, the PC kept quiet. The restore PASSED (HTTP 200 at 22:06:21Z).
- **The fix holds:** drawn == 40 on every measured frame in every arm (A1 1504; B S0-S3 1700, 1699, 1699, 1700; A2
  1504). Rig check PASS on all three; PIX-WX above 0 in each, from nine distinct grabs.
- **The read:** `fb_read_c1.py` (`f210ea1cf2ef`) exit 1, "8 rows, 7 off, 0 MISS" (output `09061761cd4d`).
  - **C1, M3's cut:** A1 p95 81.99, A2 81.95, B 73.00 ms; N 0.04, E 1.54; cut +8.97 ms, [+7.43, +10.51]. It
    STRADDLES failed and partial, which the rule sends to the orchestrator.
  - **I-5 RED:** record cost 5713 / 6827 / 5712 ns per frame, against the 5,000 limit. The first window's A1 read
    5712 too.
  - **B plain push - sync RED:** 23.69 ms, against 25.8 +/- 1.0; sync 0.46 ms.
  - **THE PLANTS NEVER RAN:** D=10 push rise +0.00 ms (want +9 to +10); D=60 push +0.01 (want +59 to +60); D=60 p2p
    73.00 (want at least 83); loop +10 p2p -0.01 (want +10 +/- 1.5). The record's delayed flag (bit 2) is 0 on every
    frame of S3, and also on every frame of every scene in the FIRST window's B trace. So it predates the hex fix.
    Without a plant that moves, the bench has not shown it can see a known change of its own size, so the cut is not
    graded on its band.
- **What the code shows, nothing concluded:** both plants are applied in `main.cpp` under `#ifdef FRAME_BENCH` (the
  push delay at :241, the loop delay at :1355). Both gate on `S().scene >= 0 && !done`, which the driver sets in
  `AircraftManager.cpp:2176-2205`. Three alternatives are named, each checkable from the c1fix-B ELF and on the host:
  (i) the call sites are not in the image; (ii) the state they read is not the driver's; (iii) an ordering effect,
  which cannot explain D=60's unmoved p2p.
- **Stopped,** to the owner. No fix and no re-run before the ruling. A third C1 window would first need the plant
  wiring found and fixed, with a check that reads red on today's images.

### THE PLANTS' CAUSE, FIX, CHECK AND PROOF (the owner's ruling (a); registered 2026-11-14T22:46:01Z, before their code)

The owner: "Investigate why the 10 ms and 60 ms deliberate slowdowns didn't fire (read-only, no board), fix it, and
prove the fix fires both at their true size before the next C1 run."
- **THE CAUSE, read as predicted (alternative (iv)):** `BLIPSCOPE_PUSH_TASK` is defined by a HEADER
  (`include/variants/s3_128.h:89`), and `FrameBench.h` chooses its `static` scene table by testing it (`:37-40`;
  `:48-52` on the levels tree).
  - main.cpp includes `FrameBench.h` at `:7`, before any header that brings in `variants/Variant.h`.
  - Under each file's own c1b compile command, `pp_read.py` (`b1f5bf5f28a8`) shows that main.cpp keeps image A's
    1-scene table and AircraftManager.cpp keeps B's 4-scene table.
  - main.cpp's two plants (`DelayOn`, `PushDelayMs`, at `:1355` and `:241`) therefore index a 1-element table with
    scenes 1 to 3, and neither can fire.
  - (ii) is excluded by the ELF (one `S()`), and (iii) by S3's unmoved p2p. The disassembly was the instrument's
    limit: IRAM targets behind `callx8` cannot be labelled.
  - The per-file items under the test are `kScenes` and `kNumScenes`, and nothing else.
- **THE FIX, on `bench/frame-bench-c1` (from `a599fcaaffc6`) and `bench/frame-bench-levels` (from `a9894185b2a8`):**
  `FrameBench.h` includes `"variants/Variant.h"` itself, before its push-task test. **Predicted diff:** 1 file, 1 line
  added, nothing removed, on each tree.
- **THE CHECK, red first:** `pp_read.py` v2 adds `--same`, which passes only if every named file keeps the same table.
  Its v1 prediction mode is unchanged.

  | env (compile database) | today (c1 `a599fcaaffc6`, levels `a9894185b2a8`) | after the fix |
  |---|---|---|
  | c1b | RED: main.cpp A, AircraftManager.cpp B | GREEN: B and B |
  | c1a | green: A and A | green: A and A |

  Predicted on both trees. The c1a row is the control showing that `--same` can pass on today's code.
- **THE GUARD IN THE TREE (the orchestrator: "so the class can't come back silently"):** `check_frame_bench.py` gains
  **FB-TU**. Every macro that `FrameBench.h` tests in an `#if`/`#ifdef`/`#elif` and that a project header `#define`s
  (`include/**`) must be defined only after `FrameBench.h` includes that header, or `variants/Variant.h` for one under
  `include/variants/`.
  - **Its plant:** the new include removed. It reads exactly {FB-TU} red.
  - **Predicted:** c1 goes from 13 rows to 14, 0 failed; levels from 14 to 15, 0 failed. The selftest PASSES.
  - It runs on every bench tree, the v20 sibling included.
- **I-3:** `FrameBench.h` compiles only under `FRAME_BENCH`. So the shipping `blipscope-s3-128` built at the fix is
  predicted byte-identical to c1fix-I3 (`de75e3606ba7`).
- **THE PROOF, a short window before any further C1 run:**
  - **The image:** c1-B built from the c1 fix commit.
  - **The window:** read-mac, write B (ota_0 only), one bench-trace of about 17 min (S0-S3), read-mac, then the v19
    restore, its boot read and HTTP 200. About 25 min on E2:08, with the PC kept quiet.
  - **The reader:** `fb_read_plants.py`. It imports `fb_read_c1.py` (`f210ea1cf2ef`, sha asserted), uses its own
    `read_trace` on the B trace, and applies C1's own row formulas.
  - **Graded at true size:**
    - DELAYED-FLAG: the record's bit 2 set on every measured frame of S3, and on no frame of S0-S2;
    - D=10 push: the push_avg rise is +9.0 to +10.0 ms;
    - D=60 push: +59 to +60 ms;
    - loop +10: Δp2p p95 is +10.0 ± 1.5, with push_avg unchanged within ± 1.0.
  - **Reported only:** D=10 p2p and D=60 p2p, read with E = U = 1.54 ms, because there are no A arms here to give N.
  - **Its selftest, red first:**
    - the clean synthetic B trace: every graded row in;
    - the synthetic D10 and LOOP plants: red on exactly D=10 push and loop +10;
    - an INERT case (the S1-S3 readings equal to S0's): red on D=10 push, D=60 push and loop +10;
    - **today's real B trace from the retake: red on all four graded rows.**
  - **Predicted:** every graded row in. An off row is C1's THIRD OFF, which goes to the owner. Anything else is
    outside the set: stop and decide.
- **Then:** the next C1 window (A1, B, A2), with images from the fix commit, placed under the standing rule. The levels
  sha goes to p4.
- **READ, as registered:**
  - the fix: `68cae820a44e` on c1 and `d813ea42be08` on levels, 1 line each;
  - the check: RED today on both c1b trees, SAME after on all four envs;
  - FB-TU: c1 14 rows and levels 15 rows, 0 failed; red on `a599fcaaffc6` and `a9894185b2a8` themselves; the selftest
    PASSED. Pushed at c1 `bcc4414ca870` and levels `a0583e88b142`;
  - the images from tree `4365655892ef`: c1-B `57f2422516ce`, c1-A `a653e50905a9`. I-3 is byte-identical to
    `de75e3606ba7`;
  - `fb_read_plants.py` `141c4feb7e69`, selftest PASSED (`27eb9d23d50f`); the REAL retake trace read red on all four
    rows;
  - the manifest `0274359ccc4c`, acknowledged.

### THE PLANTS' PROOF WINDOW, 2026-11-14 23:10:09-23:32:01Z: OUTSIDE THE SET (stopped; to the owner)

B only, the PC kept quiet. The restore PASSED (HTTP 200 at 23:31:51Z). Rig check PASS.
- **The read:** `fb_read_plants.py` exit 2, `INSTRUMENT / STOP: B S2 C1+push60: M3-2: 30 of 1428 frames with a present
  count other than 1 (> 1 %)`. That is `fb_read_c1`'s own `read_trace` refusal, so no row was graded.
- **The structure, from the reader's own parse, recorded and NOT graded:**

  | scene | frames | drawn | delayed (bit 2) | present counts | p2p p95 (ms) | push n / avg (ms) |
  |---|---|---|---|---|---|---|
  | S0 C1 | 1689 | 40 | 0 | all 1 | 73.00 | 1690 / 24.16 |
  | S1 push10 | 1688 | 40 | 0 | all 1 | 73.00 | 1688 / 34.12 |
  | S2 push60 | 1428 | 40 | 0 | 0: 15, 1: 1398, 2: 15 | 84.00 | 1428 / 83.95 |
  | S3 loop10 | 1655 | 40 | 1655 / 1655 | all 1 | 74.99 | 1655 / 24.15 |

  - **The push plants fire at their true size:** push_avg rises by +9.95 ms (band +9 to +10) and +59.79 ms (band
    +59 to +60). D=10's p2p is unmoved, as C1 predicts.
  - **The loop plant fires:** its flag is on every S3 frame and on no other frame, so the include-order fix works for
    both plants. But it moves p2p p95 by only +1.99 ms, against +10.0 ± 1.5. Graded, that row is RED.
  - **S2's M3-2:** with the push at about 84 ms, 2.1 % of frames recorded 0 or 2 presents, against the 1 % rule.
- **Named, nothing concluded:**
  - (a) In image B, the loop's +10 ms may land where the loop has slack (p2p 73 against push 24), so most of it is
    absorbed. Then "+10 ± 1.5 in a draw-bound scene" is a premise B does not meet, not a plant that is broken.
  - (b) M3-2's 1 % rule may not hold under D=60 by construction.
- **Stopped,** to the orchestrator and the owner. No fix and no re-run before the ruling.

### THE PACKAGE: the moved loop plant, M3-2's exact count, and a dropped update (registered 2026-11-15T01:13:05Z, before its code)

The owner, verbatim: "Proof: a. The proof run stays on record as off. Before the ~25-min board proof, write down the
moved loop check (expecting the full +10 ms) and the reworked 60 ms rule. Then include a deliberately dropped screen
update that the new 60 ms rule must catch. If it doesn't, the rule is too loose and comes back to me. Then C1."

**Why the loop plant moves (code reading and the proof window's own records; no new board read):**
- The buffer wait did not absorb it. The board's `[m3] loop_wait_avg_us`, windowed inside each measured span, reads
  S0 5 µs, S1 5 µs, S2 17,232 µs and S3 15 µs a frame.
- It is not short: `delayMicroseconds` in arduino-esp32 3.3.9 is a wall-clock busy loop on `esp_timer_get_time()`.
- It is not time-scaled draw work: the period moved only 1.4 %.
- It overlaps the push. The plant at `main.cpp:1355` sits after `pushtask::HandOver()` (`:1324`), inside the push
  that HandOver starts. The push task is one priority above the loop, on its core (`:307-311`). The push's CPU bursts,
  or its PSRAM contention, or both, land inside the busy-wait instead of in the next draw. That repays about 8.5 ms.

**1. THE MOVED LOOP PLANT.**
- `if (framebench::DelayOn()) delayMicroseconds(framebench::PLANTED_DELAY_US);` moves, in its FRAME BENCH region, from
  `:1355` to immediately before the `pushtask::HandOver();` call. There the previous push (started one frame earlier,
  about 24 ms long) has finished, and this frame's push has not begun.
- **C1's sentence changes** (M3-3's plants, above): "the loop's +10 ms by `delayMicroseconds` immediately before
  `pushtask::HandOver()`, outside the push's window". p4 re-signs it.
- **Predicted (B, C1+loop10):** Δp2p p95 = **+10.0 ± 1.5**, with push_avg unchanged within ± 1.0. That is the loop
  row as frozen, now on a site that meets its premise. Δfr is reported.
- **Static row M3-L** in `check_frame_bench.py`: the loop plant appears exactly once in `loop()`, in a FRAME BENCH
  region, immediately before the `pushtask::HandOver();` call, and not after it. **Its plant (the old site) reads
  exactly {M3-L} red. Today's tree (`bcc4414ca870`) reads M3-L red.**

**2. M3-2, RE-DERIVED: an exact count per scene, with the span boundary carried by a tag.**
- **Why the per-frame rule fails at push60:**
  - The push (about 84 ms) outruns the draw (about 71 ms). The loop waits in `TakeForDrawing` (`main.cpp:327`) for
    the buffer the push releases at `:258`, just after it presents at `:254` (`FrameBench.h:211-218`).
  - The loop's per-frame record (`AircraftManager.cpp:2164`; `FrameBench.h:258-270`) lands near each present, and
    jitter turns a 1,1 into a 0,2.
  - The proof window's S2 fits exactly: sum(pres) 1428 = frames = push n; 15 zeros and 15 twos, all in adjacent
    pairs; the running sum within [−1, +1].
  - The rule was derived from that trace, so its reading IN there is a consistency case, not evidence.
- **Why a sum against push n cannot catch a drop (the orchestrator):** a skipped update lowers push n and sum(pres)
  together. And a ±1 tolerance lets a single missing present through.
- **THE COUNT:**
  - In `pushtask::HandOver()`, in a FRAME BENCH region before `xQueueSend`, the queued frame is tagged with the
    current scene if that scene is measuring, and the scene's `handed` count goes up.
  - In `pushtask::Task`, right after `Present()`, a tagged frame raises its scene's `presented` count.
  - A frame belongs to the span if and only if it was handed over while measuring. Its present counts whenever it
    happens, after the span included.
  - After the scene's dump finishes (`dump S<k> done`, seconds after the end, when every in-flight frame has been
    presented), the bench prints `[frame-bench] presents S<k>: handed <H> presented <P> dropped <D>`.
- **THE RULE, every B scene: P == H exactly.** Off is the INSTRUMENT.
  - The per-frame "under 1 %" stays for the scenes that are not push60. For push60, the per-frame count is reported,
    not graded.
  - **Why one drop fails with certainty:** each tagged frame is either presented (P + 1) or dropped. One drop gives
    P = H − 1 exactly. No jitter enters, because no window boundary is involved.
- **Scope:** only images whose bench line says "presents counted" (the push-task images from this package) carry
  the presents lines. With the token, a missing presents line is the INSTRUMENT. Without it (every older trace, and
  every existing selftest case), the old rules apply unchanged. Image A is unchanged.

**3. THE DROPPED-UPDATE PLANT (the owner's addition), in its own scene.**
- A proof-only scene **S4 `C1+push60drop`**: kind `C1_PUSH60_DROP`, push delay 60 as push60. It is compiled only
  into a proof env `blipscope-s3-128-framebench-c1b-proof` (c1b plus `-DFRAME_BENCH_PROOF_DROP`, a build flag, so
  FB-TU holds). C1's image B keeps its four scenes.
- In `pushtask::Task`, right after `xQueueReceive`, a FRAME BENCH region: if `framebench::DropThis(i)` (true exactly
  once, for the 100th tagged frame of a `C1_PUSH60_DROP` scene), the frame is **not pushed and not presented**. There
  is no `pushImageDMA`, no `Present`, no `PushTimes` and no `g_pushes++`. `framebuf::SetPhase/FrameDone` and
  `xSemaphoreGive(g_free[i])` still run, so the buffer is released. The scene's `dropped` goes up, then `continue`.
- **Static row M3-D:** `DropThis` appears exactly once, in that region of `pushtask::Task`, before any push work.
  The region releases the buffer and continues. It appears nowhere else. **Its plant (the region moved after the
  push) reads exactly {M3-D} red.**
- **Predicted, S4:** `handed H presented H−1 dropped 1`. M3-2 reads it as **CAUGHT**, the expected-red control, and
  the read goes on. **S4 reading P == H (not caught) means the rule is too loose: to the owner, stop.** Any other S4
  count is the INSTRUMENT.

**4. THE READERS, each re-frozen by sha before the window; p4 re-signs M3-2's text and the plant sentence.**
- **`fb_read_c1.py` v2:** the token, the presents lines, the exact rule and the per-frame rule's scope.
  - **Every existing selftest case's output is byte-identical** (the existing selftest output is compared by sha).
  - New cases: CLEAN-P (every scene P == H) is graded. LOSTP (a clean scene at P == H − 1) is the INSTRUMENT. PAIRS
    (push60 with 0/2 pairs and P == H) passes M3-2. Each case prints its own reason's words.
- **`fb_read_plants.py` v2, for the proof image's five scenes.** Graded:
  - DELAYED-FLAG: S3 only, and none in S4;
  - D=10 push and D=60 push: true size;
  - loop +10: +10.0 ± 1.5, with push unchanged;
  - M3-2 exact: P == H in S0-S3;
  - DROP-CAUGHT: in S4, P == H − 1 and dropped 1.
  - Its selftest: v1's five cases. The REAL retake trace lacks the token and still reads red on its four rows.
  - Plus a synthetic single-drop trace, which reads CAUGHT; a synthetic drop NOT caught (S4 at P == H), which reads
    "too loose", exit 3, to the owner; and a synthetic lost present in S2, which reads the INSTRUMENT.

**5. THE IMAGES AND THE WINDOW.**
- From the package's commit on `bench/frame-bench-c1`: the proof image (c1b-proof), C1's c1-B and c1-A, and I-3,
  predicted byte-identical to `de75e3606ba7`.
- `check_frame_bench.py` gains M3-L and M3-D: 16 rows, 0 failed at the commit.
- **The proof window:** the proof image only, S0-S4 (90 s, 5 × 220 s and the dump), about 28 min on E2:08 with the
  PC quiet. It ends by 17:20Z, or starts after 20:50Z.

**5a. THE ORCHESTRATOR'S ADDITIONS (accepted with the design):**
- **The same instrument in both images.** The drop is proven on c1b-proof while C1 runs c1-B. `pp_diff.py`
  preprocesses `src/main.cpp`, `src/AircraftManager.cpp` and `src/ConfigurationWebServer.cpp` under each env's own
  compile command, with line markers and blank lines dropped, and compares the two outputs line by line.
  - **Predicted:** the only differing line is the scene table, the proof's five-scene `kScenes` line against B's
    four. The tag, `handed`, `presented`, `DropThis`, the presents line and everything else are identical.
  - **Its plant, red first:** a copy of `FrameBench.h` with ONE extra `presented` increment compiled only under
    `FRAME_BENCH_PROOF_DROP`, put first on the include path for both envs. **Predicted: RED.**
  - C1's own run re-grades the push and loop plants on c1-B anyway.
- **S4's other numbers, reported only:** S4's p2p p95 and push average against S2's (the same push60 load).
  Expected within E of S2's p2p p95, and within ± 1 ms of its push average. One dropped update in about 1,400 frames
  should not move either. Not graded.
- **The layout caveat, settled before C1, not now.** The tag and the counts go into B only, because A has no push
  task. If 5c's layout control reads LAYOUT (a ~1.7 KB shift alone moving the frame ~4 ms), then any build-only
  difference between A and B, this instrumentation included, carries a layout-noise floor of that size against C1's
  ~9 ms cut, and C1's design must answer for it before C1 runs.

**6. THE OUTCOMES, registered:**
- **IN:** the moved loop plant reads +10.0 ± 1.5; the push plants read at true size (+9 to +10, +59 to +60);
  M3-2 exact P == H in S0-S3; S4 CAUGHT.
- **S4 not caught:** the rule is too loose. To the owner: stop.
- **Any other off reading, or anything outside the set:** stop.
- **Then C1,** its slot proposed under the standing rule.

### THE SAME-INSTRUMENT CHECK, amended before its first run, and read (2026-11-15)

**The gap, found before any run.** As registered in 5a, `pp_diff.py` allowed only the `kScenes` line to differ. The
two envs' text must also differ in a second way, which has nothing to do with the counting code: the env's own name.
- `-DBUILD_ENV="$PIOENV"`, and `BuildIdentity::Env()` returns it inline in a header that `main.cpp` and
  `ConfigurationWebServer.cpp` include.
- Each env has its own `.pio/libdeps/<env>` include dirs, so any `__FILE__` in a library header carries the name.
- Both were read from the two compile databases before the amendment was written, not assumed: `BUILD_ENV` reads
  `...framebench-c1b` against `...framebench-c1b-proof`, and each env's 7 libdeps `-I` paths name its own dir.
- Run as registered, the plain check would have read RED on the env name. `FRAME_BENCH_PROOF_DROP` itself appears once
  in the tree, the `kScenes` guard.

**The amendment (the orchestrator accepted it before any run): `pp_diff_v2.py`.** `pp_diff.py` is kept unchanged
and was never run.
- In the PROOF env's text only, exactly `framebench-c1b-proof` is replaced by `framebench-c1b`. Then the `kScenes` line
  is allowed, and nothing else.
- **Guard 1:** `c1b-proof` is counted in the c1b text first; the run stops unless the count is 0.
- **Guard 2:** the replacements made in the proof text are counted; the run stops unless there is at least one.
- A normalized line that is not a path must carry the complete quoted env name (`BUILD_ENV`'s form), or the run stops.
- Normalized lines are printed per source, split into path-bearing and other.
- **A new control on the widening (`--env-plant`):** `FrameBench.h` with ONE line that differs between the envs only by
  `BUILD_ENV`. It must read PASS, while the counter plant must still read RED.
- The line alignment is `difflib.SequenceMatcher`'s opcodes, the line matcher `ndiff` uses, without the character
  hints: the same − and + lines.
- **Predicted:**
  - PLAIN: PASS. The scene table differs by 2 in every source and nothing else does. At least one normalized
    `BUILD_ENV` line in `main.cpp` and in `ConfigurationWebServer.cpp`.
  - `--plant`: RED, exactly one other line per source (3), each the planted increment.
  - `--env-plant`: PASS, one normalized probe line per source.
  - Any other shape: the instrument is unreliable here. Stop and report.

**The environment.** The compile databases (at `0d6308cb77b6`) were generated in a process created by WMI, so pio ran
with no MSYS variables. The proof env had no libdeps dir. It was given a copy of c1b's, rather than a download:
`diff -rq` identical, every `lib_deps` pinned exactly. Each database: pio exit 0, 259 entries, no "Installing" line.

**Read (02:00-02:04Z): every prediction HELD.**

| source | lines (c1b = proof) | normalized: path / other | PLAIN | `--plant` | `--env-plant` |
|---|---|---|---|---|---|
| `main.cpp` | 114,531 | 2 / 1 | table 2, else 0 | table 2, else 1: `+ if (t >= 0) s.presented[t]++;` | table 2, else 0; other 2 |
| `AircraftManager.cpp` | 118,626 | 3 / 0 | table 2, else 0 | table 2, else 1: the same line | table 2, else 0; other 1 |
| `ConfigurationWebServer.cpp` | 110,895 | 2 / 1 | table 2, else 0 | table 2, else 1: the same line | table 2, else 0; other 2 |
| **verdict** | | | **PASS, exit 0** | **RED, exit 1 (3 lines)** | **PASS, exit 0** |

- Guard 1 read 0 and guard 2 read 3 in every source (4 with the env plant).
- The env plant raised "other" by exactly 1 in each source.
- Afterwards the tree was clean and the plant include dirs were removed.
- So the proof image and C1's image B carry the same counting code. The env's own name is the only widening, and the
  control shows it hides nothing else.

### THE PACKAGE's images, and I-3's MISS on its registered words (2026-11-15)

**The four images**, from `bench/frame-bench-c1` at `0d6308cb77b6`, each built clean. pio ran in a WMI-created process
with no MSYS variables.
- The disk was read twice, 60 s apart (30.89 GiB both). Before each build it read 30.89 / 30.84 / 30.77 / 30.71 GiB,
  against a guard of 30.2 plus 0.2. Each env's build dir was removed once its image was recorded; the low point was
  30.61 GiB.

| image | env | firmware.bin sha256 | bytes |
|---|---|---|---|
| c1fix3-I3 | `blipscope-s3-128` (FRAME_BENCH off) | `805556b5f6c1` | 1,933,952 |
| c1fix3-A | `...-framebench-c1a` | `3cf72c29f08f` | 1,963,200 |
| c1fix3-B (C1's c1-B) | `...-framebench-c1b` | `669f87ec5839` | 1,965,584 |
| c1fix3-P (the proof image) | `...-framebench-c1b-proof` | `00a1b5c23475` | 1,965,616 |

**I-3 missed on its registered words.** It was registered as "byte-identical to `de75e3606ba7`", and it is not.
- `bindiff_elfhash.py`, `c1fix2-I3` against `c1fix3-I3`: the same length. The differing bytes are app_elf_sha256 32,
  the image SHA-256 32, the checksum 1, and **elsewhere 0**. The loaded image is identical to the byte, but the sha is
  not, and the registered words named the sha.
- The orchestrator recorded it as a miss, with no re-grade (the first off reading on the row).
- **The two explanations, named before the discriminator ran:**
  - (H1) this is the first fix since I-3 began to edit a `.cpp` (`main.cpp`, inside `FRAME_BENCH`). The lines after it
    shift, so the debug data change, and with them the ELF and its hash, while no loaded byte moves.
  - (H2) something else in a non-loaded, non-debug section changed.
- **The discriminator, `elf_sections.py`** (read-only; it compares two ELFs section by section; its outcomes were ruled
  before it ran):

| run | predicted | read |
|---|---|---|
| ANCHOR: `c1fix-I3` vs `c1fix2-I3` (both bins `de75e3606ba7`) | SAME | 40 = 40 sections; 0 differing; 0 bytes outside any section: **SAME** |
| PLANT: one byte flipped mid-`.debug_line` | exactly `.debug_line` named | `.debug_line`, 1 byte; nothing else: **as predicted** |
| SUBJECT: `c1fix2-I3` vs `c1fix3-I3` | H1, H2 or ESCAPE | every SHF_ALLOC section identical; differing: `.debug_info` 149 B and `.debug_line` 241 B only; 0 outside: **H1** |

- **The ruling (the orchestrator, before the run):** under H1 the row stays a MISS on its words, and the property it
  protects (the shipping image's loaded bytes, elsewhere 0) HOLDS.
- **Future I-3 rows are registered as "elsewhere 0; the 65 ELF-hash bytes may move when a `.cpp`'s line numbers
  move".** A sha never identifies a build; content does.

**The proof window's set, acknowledged by the orchestrator:**
- `plants2_manifest.json` `188bf5c91502`: c1-P `00a1b5c23475` (1,965,616 B, written 0x020000..0x1FFE2F, ota_0 alone,
  30.0 % of it) and restore-v19 `f6d73e391631`, against the release's table `28f336fe57a9`.
- The dry runs: c1-P accepted, the restore accepted. The control, the old `c1fix2-B` image under the c1-P label, reads
  STOP (exit 20).
- The launcher refuses COM99 (exit 9), since E2:08 resolves by its serial to another port.
- The window waits only on p4's re-sign of C1's changed sentence and the M3-2 rule, at these shas. It grades p4's
  +10.0 ± 1.5. The window does not depend on I-3, which guards the shipping image.

### THE PACKAGE's proof window, 2026-11-15 02:13:38-02:39:21Z: IN, every graded row as registered

p4 re-signed on the exact shas, and the orchestrator then opened the window. E2:08 was reserved by its serial and the
PC was quiet (CPU 2 %, no build or test process). The window was launched detached, with a waiter whose deadline it
never reached.

- **The image:** c1-P `00a1b5c23475` was written to ota_0 alone (1,965,616 B, verified; the checks line exact). Its
  trace was a single 21-min open, exit 0.
- **The restore:** v19's `f6d73e391631` was written and verified, and its 3-min boot trace exited 0.
  - **The restore read PASSES:** `[WiFi] CONNECTED` 1; the join, "fast join OK"; 0 portal-START lines (all six
    negatives 0); `[build] env=blipscope-s3-128 fw=v19`; OTA latest = 19.
  - read-mac ok. HTTP 200 on the second attempt; the first threw while the board was still coming up.
- **The rig check PASSES on both traces:** the log and the console carry the same line sets, with 0 IOException
  records (the proof trace 827 of 827 stamped lines, the restore 49 of 49).
- E2:08 was released at 02:40:09Z.

**The grade, `fb_read_plants.py` v2 (`bb1315423a29`), exit 0: 6 graded rows, 0 off.**

| row | registered | read |
|---|---|---|
| DELAYED-FLAG | bit 2 on every measured frame of S3, on no other scene | 1492 of 1492; the other scenes 0 |
| D=10 push | +9.0 to +10.0 ms | **+9.93** |
| D=60 push | +59 to +60 ms | **+59.76** |
| loop +10, the moved plant | p2p p95 +10.0 ± 1.5 ms; push within ± 1.0 | **+10.00**; push −0.02 |
| M3-2 exact, S0-S3 | presented == handed | 1702/1702, 1700/1700, 1428/1428, 1492/1492 |
| DROP-CAUGHT, S4 | presented == handed − 1, dropped 1 | **handed 1429, presented 1428, dropped 1** |

- **Reported only, all in band:**
  - D=10 p2p +0.01 ms, within E = U = 1.54.
  - D=60 p2p 84.00, against p95(push) 84.00, and at least B plain + 10 (82.99).
  - S4 against S2, the same push60 load: p2p p95 84.00 against 84.00; push average 83.94 against 83.94.
- **Two observations, outside every registered row, named so they are not found later as surprises:**
  - "present count != 1" is 18 in S2 and 15 in S4, and 0 elsewhere. Both are the push60 scenes, the per-frame rule's
    excluded scope, and M3-2's exact count passes in both.
  - S4 recorded no `[m3]` push windows ("(a) windows 0", against 1-2 elsewhere). S4's push read comes from the push
    average (b) alone, so the (a)-against-(b) cross-check had nothing to compare in S4.
- **So the package is proven at true size.** The moved loop plant carries its full +10 ms, where it read about +1.5
  before the move. The push plants read at true size, and the exact count holds in every clean scene. The 60 ms rule
  catches one deliberately dropped screen update among about 1,400 frames. The rule is not too loose.
- **Next: C1,** on c1-B `669f87ec5839` and c1-A `3cf72c29f08f`, with its slot proposed under the standing rule. Its
  caveats name the layout residual: 5c's control read NOT LAYOUT (a 1,740 B shift moved the frame +0.51 ms, inside
  M), but an effect of one specific alignment is not excluded. C1 goes before 5c's label window, which must not push it.

### C1's THIRD RUN, 2026-11-15 02:46:55-03:21:35Z: OFF, with the owner

THE PACKAGE's images, under the acknowledged set:
- `win_c1fix3.ps1` `1d6bf2505a42`, cut mechanically from the retake's script with every substitution counted.
- Manifest `c1fix3_manifest.json` `bf5b8ac3cace`: c1-A `3cf72c29f08f` and c1-B `669f87ec5839`, each written to ota_0
  alone.
- The dry runs: both images and the restore accepted; both controls refused.
- The reader: `fb_read_c1.py` v2 `0dde3f661ee1`, whose selftest output was byte-identical to its frozen one just before.
- E2:08 reserved by its serial. The PC had no build, test or python process; its load was a background file sync that
  had run all night, through the proof window too.
- This is C1's third run, so any off reading goes to the owner with both ends, and nothing is re-graded.

**The window:**
- All three images were written and verified, with the checks line exact each time.
- The A1 / B / A2 traces (6, 17 and 6 min) exited 0, and the `/diag/fb` grabs kept 3 of 3 in each arm.
- **The restore read PASSES:** `[WiFi] CONNECTED` 1; "fast join OK"; 0 portal-START lines; `fw=v19`, OTA latest = 19;
  read-mac ok; HTTP 200 on the second attempt.
- **The rig check PASSES on all four traces:** A1 215/215, B 670/670, A2 215/215, the restore 49/49; 0 records.
- E2:08 was released at 03:22:38Z.
- **The overlap, outcome (i), as registered before the trace was read.** Another session's read-only disk sweep
  overlapped the window's opening and ended by 02:47:54Z. A1's stamps put its settle at 02:48:49Z and its measuring at
  02:50:29Z, so the sweep touched no measured frame.

**The read: `fb_read_c1.py` v2 exit 1, "8 rows, 3 off, 0 MISS"** (output `25c2d3c38c7c`).

| row | registered | read | |
|---|---|---|---|
| **C1, M3's cut** | its band graded only if cut ± E falls in one band | A1 p95 82.39, A2 82.51, B 72.99; N 0.12, E 1.54; **cut +9.46, from +7.92 (failed) to +11.00 (partial)** | **STRADDLES** |
| **I-5, record cost** | ≤ 5,000 ns per frame | **A1 5714, B 6840, A2 5706** | **RED** |
| **B plain push − sync** | 25.8 ± 1.0 ms | **23.72** (push 24.18, sync 0.46) | **RED** |
| D=10 push | +9.0 to +10.0 | +9.93 | ok |
| D=10 p2p | within ± E | +0.01 | ok |
| D=60 push | +59 to +60 | +59.76 | ok |
| D=60 p2p | p95(push) ± E, and at least plain + 10 | 84.00 against 84.00; at least 82.99 | ok |
| loop +10 | +10.0 ± 1.5, push within ± 1.0 | +10.00; push −0.00 | ok |

- **Also read:**
  - M3-2 exact in S0-S3: 1703/1703, 1697/1697, 1428/1428, 1492/1492.
  - The push reads (a) and (b) agree in every B scene.
  - Rebuild frames 1/1/1; TBL-DEV `b1ace60e` in all three arms.
  - "present count != 1" is 26 in S2 only, the push60 scene.
- **Against the retake (11-14, the images before THE PACKAGE):**
  - The cut read +8.97, from +7.43 to +10.51, straddling the same boundary.
  - I-5 read 5713 / 6827 / 5712; the first window's A1 read 5712.
  - Push − sync read 23.69.
  - Then, no plant moved. Now the plants read at their true size inside this run's own B trace.
- **Named alternatives for the two instrument reds, not discriminated:** (a) the registered bounds (5,000 ns; 25.8 ms)
  never matched this hardware; (b) an instrument effect that is real and stable from run to run.
- **The caveats, as registered:**
  - C1 is measured on p4's pin `3bc7ec26fe28` without step C. This was checked on the tree: five bench-only commits,
    none adding it.
  - A and B differ by 2,384 B, and an effect of one specific alignment is not excluded.
  - E's floor is U = 1.54.
- **The artifacts (sha256, 12):** A1 trace `35c6f718490a`, B `360bcabb065c`, A2 `cf8f3ef40f9b`, the restore
  `c270b9c5b84c`, the window ledger `d2c414d400dc`.
- **With the owner.** The orchestrator has put the options to him. Nothing else moves on C1 until he answers.

### C1 CLOSED: the owner's ruling (2026-11-15, relayed by the orchestrator)

> "C1: a. ~9.5 ms is the answer; v20 stays on the "too small" plan (Dec 3-10 or later); no more C1 runs. The two side
> checks get corrected limits for future runs only, and this run's two misses stay on record as misses. The real v20
> number comes from the v20 bench."

- **The answer: M3's cut is about 9.5 ms** (+9.46, from +7.92 to +11.00). That is below on-track at either end.
- **v20 stays on the "too small" plan** (Dec 3-10 or later). The real v20 number comes from the v20 bench, not C1.
- **No more C1 runs.** Nothing in C1 is re-graded.
- **I-5 and B plain push − sync stay MISSES on this run.** Their corrected limits apply to future runs only. They are
  derived from the matching runs and registered in the v20 sibling's registration, not in any C1 file.

## THE v20 SIBLING: what it carries from C1, and the two corrected limits (DRAFT, registered before its code, 2026-11-15)

**What this section is.** v20's framebench sibling is already registered:
- A5, "v20's gate measures a sibling";
- H1-H9 in "v20's FIXED-(a) scenes: the cloud /blips hook", with the FT guard row;
- "p2p under M3, for v20's framebench", M3-1 to M3-3, with the anchor rule and P2P-FLOOR.

This section amends those. It carries what C1 found, and registers the two limits that the owner corrected for future
runs. Nothing here re-grades C1: its two misses stay misses.

### The tree (Q-S1)

- **Default, unless ruled:** main at the commit the sibling is cut from, plus M3, because v20 ships M3.
  - M3 is still the open draft #687, head `3bc7ec26fe28`; main's head is `b282699ac091` at this writing.
  - The cut point is stated by sha in the sibling's first commit. The shipping image of that same tree (with
    `FRAME_BENCH` off) is I-3's subject.
- **Not a candidate.** The sibling of the real v20 candidate is built from that candidate when it exists (A5). This tree
  is the one FT and S2b run on.

### What it carries from C1, each with the check that proves it

| from C1 | on the sibling | the check, each with a plant that must read red |
|---|---|---|
| the (a) hook's two casts (`a599fcaa`): ArduinoJson 7.4.3 stored a `char[N]` by pointer | carried as is | no host row exists; the proof is the reader's drawn == 40 on every measured frame, which runs in every window (and held in every C1 arm since the casts) |
| `FrameBench.h` includes `variants/Variant.h` itself (`68cae820`) | carried as is | **FB-TU** (`bcc4414c`) |
| THE PACKAGE (`0d6308cb`): the loop plant before `HandOver` | carried as is | **M3-L** |
| THE PACKAGE: M3-2's exact per-scene count (`handed` / `presented` / `dropped`, and the presents line) | carried as is | **M3-D**; the reader's exact rule (P == H in every scene without a planted drop) |
| THE PACKAGE: the proof env with one planted dropped update | its own proof env | the same-instrument check below |

**The same-instrument check, for the sibling's proof env.** `pp_diff_v2.py`'s rule (the env name normalized, two
guards, the env-name control), run on the sibling's two compile databases before its proof window.
- **Predicted:** only the scene table differs. The counter plant reads RED, and the env-name plant reads PASS.

**I-3, in the form the owner's C1 ruling registered for future rows:** "elsewhere 0; the 65 ELF-hash bytes may move
when a `.cpp`'s line numbers move".
- When the sha moves, `elf_sections.py` is the discriminator: every SHF_ALLOC section identical, and only `.debug_*`
  sections differing.
- An SHF_ALLOC section differing is the escape hatch.

### THE TWO CORRECTED LIMITS (for future runs only)

Every number below was read from the traces and the reader outputs on disk, by command, at this writing.

**I-5, the record cost.**

| population | images | readings (ns per frame) |
|---|---|---|
| the pre-C1 bench record (different code) | the baseline and survey images, 11-12 to 11-14 | 337, 339 × 5 |
| C1 bench, A (push task off, 1 scene) | c1-A, c1fix-A, c1fix3-A; A1 and A2 of three windows | 5712, 5711, 5713, 5712, 5714, 5706 |
| C1 bench, B (push task on) | c1-B, c1fix-B, c1fix2-B, c1fix3-P, c1fix3-B; five windows | 6822, 6827, 6871, 6824, 6840 |

- **Why the old limit missed:** its "≤ 5,000 ns" was predicted from the old record's content ("two `micros()` reads and
  four stores"). The pre-C1 record cost 337-339 ns. The C1 bench's record costs about 17 times that. The old limit was a
  prediction for a different record.
- **What I-5 is for:** that the record stays negligible, and that it does not change unseen.
  - Negligible is the purpose bound, which is **unchanged: over 50,000 ns (1 % of a 5 ms frame) is a STOP.**
  - Unchanged is the class band below.
- **The corrected band, per class.** From floor-100(min × 0.95) to ceil-100(max × 1.05):
  - **A (push task off): 5,400 to 6,000 ns** (from 5706 × 0.95 = 5420.7, and 5714 × 1.05 = 5999.7);
  - **B (push task on): 6,400 to 7,300 ns** (from 6822 × 0.95 = 6480.9, and 6871 × 1.05 = 7214.6).
- **Why ± 5 %:**
  - The observed spread is 8 ns (0.14 %) in A and 49 ns (0.72 %) in B, across rebuilds, three image generations and
    five windows. A 5 % band is about 7 times B's spread.
  - It still catches a change of the kind already seen, where the record grew 17 times.
- **Why two-sided:** a record that suddenly costs far less than its class is no longer that class's record. One case is
  the counting compiled out. A one-sided bound would map that impossible value onto "ok".
- **Its scope:**
  - The band holds only while the record's code is `0d6308cb`'s: `framebench::Record()` and what it calls.
  - **New row FB-REC:** that code is textually identical to `0d6308cb`'s. Its plant is one added statement, which reads
    red.
  - A sibling whose record differs re-registers its band from its proof window, with the same formula, before any FT
    read. Then the proof window's own reading is reported, not graded.

**B plain push − sync** (`[m3] push_avg_us − sync_avg_us`, B's plain scene).

| window | image | push | sync | push − sync |
|---|---|---|---|---|
| C1's retake (11-14) | c1fix-B `13001c132f44` | 24.15 | 0.46 | 23.69 |
| THE PACKAGE's proof (11-15) | c1fix3-P `00a1b5c23475` (its S0) | 24.18 | 0.46 | 23.72 |
| C1's third run (11-15) | c1fix3-B `669f87ec5839` | 24.18 | 0.46 | 23.72 |

- C1's first window and the first plants window have no reading: both readers stopped on the INSTRUMENT before this
  row. They are not computed after the fact, because that would run C1's reader on closed runs.
- **The corrected row: push − sync = 23.7 ± 1.0 ms** (22.7 to 24.7), with sync reported and expected under 1.0 ms
  (unchanged).
  - The centre is the three readings' mean, 23.71, rounded to 0.1. Their spread is 0.03 ms.
  - The width ± 1.0 is p4's own tolerance, kept: only the centre was off.
- **Named, not discriminated:** p4's 25.8 ms ("by p4's measurement") sits 2.1 ms above every reading here. It was
  measured on a different build or run, and which difference accounts for it is not known.
- **Its scope:** the push path as on `3bc7ec26fe28` (`pushtask::Task`'s DMA push and its panel settings). A sibling
  whose push path differs re-registers the centre from its proof window, before any FT read.
- **This is p4's text** (`docs/m3-push-task.md`, quoted under "p4's predictions for B's arms"). p4 re-signs it.

**The other use of 25.8 (Q-S2): DECIDED by p4.** P2P-FLOOR, in the anchor rule, carried "about 25.8 ms, by p4's
measurement" for the floor under which a p2p is the INSTRUMENT.
- **p4 decided** P2P-FLOOR's floor is "the push's own measured average in the same arm (`push_avg`; 24.18 ms on
  3bc7ec26fe28's path)". That is how the runtime row already reads, so the prose and the row cannot drift apart.
- p4 withdrew its earlier "or 23.7": the floor is `push_avg`, which includes the sync, not push − sync.
- p4 also corrected the anchor rule's descriptive `fr` sentence to "about 24.2 ms, `push_avg`". Nothing grades it.
- No reading changes: every p2p read so far is at least 70 ms.

### The sibling's proof window, before FT (registered now; its numbers frozen with its images)

- **The image:** the sibling's proof env (B class, with the planted drop).
- **The window:** one trace of S0-S4 on E2:08, with the PC quiet, then the v19 restore. As THE PACKAGE's proof window.
- **Graded, the same bands as THE PACKAGE's proof:**
  - DELAYED-FLAG;
  - D=10 push +9.0 to +10.0, and D=60 push +59 to +60;
  - loop +10 at +10.0 ± 1.5, with push within ± 1.0;
  - M3-2 exact in S0-S3;
  - DROP-CAUGHT in S4.
- **Also graded on the sibling:** I-5 in the B band (6,400 to 7,300 ns) and push − sync at 23.7 ± 1.0. Each is graded
  only if FB-REC, and the push-path scope above, hold. Otherwise it is reported and re-registered.
- **S4 not caught** goes to the owner (his C1 condition carried). **Any other off reading, or anything outside the
  set,** is a STOP.

### FT and S2b on the sibling

- **FT (map-server's FT-SEA and FT-HR)** runs on the sibling's B image, under the FT guard row as registered (FT-MAP /
  FT-WX by the record's kind, eleven selftest cases red first).
- **S2b** is RP5's v20 counterpart, registered separately and frozen before its window. It runs the sibling's image.
- Neither starts before the sibling's proof window is IN.

### Questions, each with the default I take unless ruled

| Q | the question | the default |
|---|---|---|
| Q-S1 | the sibling's tree | main at its cut commit plus #687's head, both by sha in the first commit |
| Q-S2 | P2P-FLOOR's 25.8 | unchanged (p4's rule), named |
| Q-S3 | FB-REC as a new row | added, with its plant |
| Q-S4 | the ± 5 % in I-5's band | as derived; a narrower band is the orchestrator's to set |

### THE DRAFT ACCEPTED (the orchestrator, 2026-11-15), with its rulings

- **Q-S1, ACCEPTED, and frozen.** The sibling is based on main at **`b282699ac091`** plus #687 at **`3bc7ec26fe28`**.
  If either moves before the build, the sibling is re-registered first. It is never rebased silently.
- **Q-S2, DECIDED by p4:** P2P-FLOOR's floor is `push_avg` in the same arm (24.18 ms on `3bc7ec26fe28`'s path), and the
  `fr` sentence reads about 24.2 ms (above). The stale 25.8 is gone from both.
- **Q-S3, ACCEPTED:** FB-REC, the textual identity of `framebench::Record()` and its callees against `0d6308cb`, with a
  planted extra statement reading red. It keeps the I-5 band honest.
- **Q-S4, ACCEPTED as derived:** I-5's band is A 5,400 to 6,000 ns and B 6,400 to 7,300 ns, two-sided, from all
  eleven readings. The purpose STOP over 50,000 ns is unchanged.
- **push − sync = 23.7 ± 1.0 ms: p4 RE-SIGNED it** (scoped to `3bc7ec26fe28`'s push path, for future runs only).
- The new fact is recorded: the C1 bench's record costs about 17 times the old bench record. At about 5.7 µs on an
  80 ms frame, it moves no frame reading.
- **The proof window before FT, and FT and S2b not starting before it is IN: ACCEPTED.**
- **The disk is on the sibling's critical path.** Its builds (Wednesday to Thursday) need C: over 30 GiB, and C: reads
  about 27. The orchestrator takes that to the owner if it has not recovered by Monday's readiness check.

### The sibling's tree as built, and its caveats (2026-11-15)

**The branch:** `bench/frame-bench-v20`, accepted at `0a2eeb4537e4`.
1. `3f7db5331986`: main `b282699ac091` plus M3 `3bc7ec26fe28`.
2. `48b62615e91c`: C1's five bench commits (`bfcb47e0..0d6308cb`). `include/FrameBench.h` is byte-identical to
   `0d6308cb`'s.
3. `0a2eeb4537e4`: FB-REC.
   - It covers `struct Rec`, `S()`, `Mux()`, `Ten()`, `Record()` and the measuring loop `SelfTime()`, pinned at
     `425040a3999d`.
   - `State` is left out: the FT code adds fields to it, which move no record cost.

**The checks:**
- check-no-bench-hooks: its selftest catches 10 of 10 planted markers after (1), and 11 of 11 after (2).
- check_frame_bench: 17 rows, 0 failed. Its selftest PASSED; both FB-REC plants read exactly {FB-REC}.
- The pre-push chain: 10 ok and 1 skipped (check_no_bench_exemption, as on every bench branch).
- **The merges' one conflict** was the guard script's MARKERS line, both times. It was resolved as the union, 11
  markers ending `[card-bench]` `[m3-bench]` `[frame-bench]`, so that neither side's bench hook can ride into a
  slug-ful image unrefused.

**The marker check from the other side** (`marker_xcheck.py`, a one-off, `157648e317a9`). It derives the marker set
from the hooks' own string literals in src/ and include/. Both of its plants read red: an unlisted hook, and a dropped
list entry.
- **"Equal exactly" is false on main itself,** before any merge. The same three differences read on main, on M3 and on
  the bench tip. So the check that ran is the one that answers the merge question, with its predictions frozen first:
  **each merged tree's differences equal the union of its parents' own.** It held after both merges, so the merges
  dropped nothing.
- **Caveat (i), latent, not live.** `[bench]` is printed unlisted, inside the `WX_LOOK_AB` and `FOLLOW_BENCH`
  regions. The orchestrator read it on main: no env defines `FOLLOW_BENCH`, and `WX_LOOK_AB` is defined only in
  `blipscope-s3-128-wxlook`. That env is slug-less, and its image must carry `[wx-bench]`, which refuses it in any
  slug-ful image. So no shipping image compiles either region today.
- **Caveats (ii) and (iii):** `[crash-bench]` and `[wx-key]` are listed but printed nowhere in the tree. They are stale
  entries, which refuse nothing.
- The guard fixes go to the release owner's queue as a small main PR, not to this branch.

### FT on the sibling: the embedded packages, the swap and the scenes (registration, before its code)

FT-SEA (`docs/basemap-dense-code.md`) and FT-HR (`docs/basemap-hudson-rule.md`, map-server's branch) both say that the
sibling "embeds the packages and swaps them in RAM through `Initialise`", and that this is map-client's code,
registered here. Their homes, packages, rungs, margins and controls are theirs, and are cited, not restated.

**A. Two images, one code** (Q-FT-A). Both are B class (the push task on), slug-less and never published.
- **`…-framebench-v20-ftsea`:** FT-SEA's six packages; kind FT-MAP, weather OFF; 18 scenes (3 points × 3 rungs × 2).
  Its packages are pinned and verified now: KJFK `ee8c834bc81a` / `52ac9dc39de0`, KBOS `ed5393b4fc0f` / `f21d705d8b8a`,
  KORD `4238243a57a6` / `cb59bb06a506`; 1,268,360 B in all.
- **`…-framebench-v20-fthr`:** FT-HR's run-10 column plus the chosen option's column; kind FT-WX, weather ON; 36
  scenes (6 points × 3 rungs × 2).
  - It is built only after map-server freezes the option's packages ("the packages are frozen once the chosen build
    reads").
  - Its pin list is registered then, before its image.
- **Why two:** FT-SEA's packages are frozen and FT-HR's are not. Separate images let FT-SEA proceed, and each keeps room
  in ota_0.

**B. The embedding.**
- **A framebench-only pre-build step,** in the FT envs alone. It reads a tracked pin list (point, variant, sha256,
  length; no coordinate). It reads the files from an UNTRACKED local directory named by `FRAME_BENCH_PKG_DIR`, as
  `FRAME_BENCH_HOST` is named.
- It checks each file against its pin, and writes a generated header into the build dir (never tracked). The header
  holds each package as a `const` array, and a table {point, variant, length, sha12}.
- **A missing file, or a sha or length mismatch, FAILS the build** and names the pin. A substitute is never built in.
- **Predicted image sizes:**
  - FT-SEA: c1fix3-B's 1,965,584 B, plus the six packages' 1,268,360, plus under 4 KB of table and code. That is about
    **3.24 MB, 49 % of ota_0** (6,553,600 B).
  - FT-HR: about 4.3 MB, under 70 % of ota_0. It has eleven distinct packages: KORD is shared between its columns, and
    its run-10 KJFK is FT-SEA's `52ac9dc39de0`.
  - Over ota_0 is outside the set.

**C. The swap, through production's own path.** At each FT scene's setup, in the unmeasured settle:
1. **The home is the package's own centre** (`lat0Micro`, `lon0Micro`), so no coordinate is typed anywhere, MIDTOWN's
   included (Q-FT-B).
   - **Predicted:** every pinned package's centre sits at exactly 2 dp, each a multiple of 10,000 µdeg, because each
     was built at the client's 2-dp query.
   - A host row (FT-CTR) checks this on the pins before any image, and prints booleans only. A centre off 2 dp is
     outside the set.
2. **The scene's rung** is set.
3. **`Initialise` runs,** as a config change does. `MapConfigure` keys on the new view and drops the old package (CA10).
4. **The embedded package is COPIED into the PSRAM landing buffer.** It is the same `heap_caps_malloc(MALLOC_CAP_SPIRAM)`
   that `RequestBasemap` uses, sized max(the cap, the length).
5. **The copy passes through `maplayer::Load`,** the CRC and format gate.
6. **It is installed** as `ConsumeBasemap`'s Accepted branch installs (`mapPkg`, `mapHeldLen`). The policy records it as
   held, so no map fetch goes out.

- **Why copy, and not point into flash:** production draws a package from the PSRAM landing buffer. A package read
  through the flash cache has different access costs, and would bias the very number FT measures.
- **Map ON:** C1's `MapOff()` returns true for C1's scenes only, and false for FT scenes.
- **Weather:** FT-MAP pins it OFF (FT-SEA's amendment). FT-WX has it ON, with the bench's fixed field, so both packages'
  scenes carry the same weather CRC.
- **Traffic:** the (a) hook serves `FrameBenchTable.h`'s rows around the scene's home at level 40. The table is
  home-relative (`At(lat0, lon0, i, t)`).
  - TBL-DEV's expected sum for each FT home is computed on the host by `tbl_host.py`, against `fixed_traffic.py`, and
    frozen with the image. It is never typed.

**D. The rows, each with a plant read red first.**
- **FT-PKG:** the generated table equals the pin list. On the host, the step refuses a package with one byte flipped.
- **FT-SWAP:** the install region exists exactly once, inside a FRAME BENCH region of the scene setup. It copies into
  the landing buffer and calls `maplayer::Load` on that buffer. No `mapPkg` points at an embedded array.
- **FT-NOFETCH:** `RequestBasemap`'s bench region returns before any send while an FT scene holds a package.
- **FT-CTR:** the centres, as in C.1.
- **FB-REC stays green:** the FT code adds no statement to the record's spans.
- **At run time,** in every FT scene, each else that scene is the INSTRUMENT:
  - one `[frame-bench] ft-package` line, giving the point, the variant, the on-device sha256 (12) of the PSRAM copy, the
    gate's verdict and the length. The reader requires the sha equal to the scene's pin and the gate "passed". The input
    is proven per scene, on the device;
  - 0 `[map]` request lines;
  - the FT guard row by kind, as registered.

**E. Predicted, frozen now:**
- FT-SEA's run: 18 scenes, about 66 min, plus the 90 s start.
- FT-HR's run: 36 scenes, about 132 min, plus the 90 s start.
- Each image's size and ota_0 share, as in B.
- The reader's FT version is frozen by sha before either row is read.

**F. Questions, each with the default I take unless ruled:**

| Q | the question | the default |
|---|---|---|
| Q-FT-A | one image or two | two (FT-HR's packages are not frozen) |
| Q-FT-B | where each FT home comes from | the package's own centre, checked at 2 dp by FT-CTR; MIDTOWN has no airport entry |
| Q-FT-C | the windows | FT-SEA after the sibling's proof window; FT-HR after its packages freeze; each in all its homes' daylight with 10 min clear of each edge (bit 3); their times are placement data, never committed |
| Q-FT-D | the swap's copy | into the PSRAM landing buffer, never drawn from flash |

### Q-S1 RE-REGISTERED: one tree with step C, and what that changes in FT (the orchestrator's rulings, 2026-11-15)

**Why it moved.** Enumerating what ConsumeBasemap's Accepted branch establishes, and then looking for its readers,
found none. Main `b282699ac091` draws no map:
- no file in src/ or include/ calls into the draw;
- "THE MAP DRAW" region holds only the step-C refusal's `#error`;
- `BLIPSCOPE_MAP_FETCH` compiles only under `FEATURE_CLOUD_FEED && WX_LAYER_SHIPS`, and any such build without
  `BLIPSCOPE_MAP_STEP_C` stops at that refusal.

So maps reach an image only together with step C. FT on the tree as first frozen would have installed packages that
nothing draws. Nothing had run on the sibling, so this is a re-registration, not a re-grade.

**THE TREE (ruled: one tree, so that the proof window covers the tree FT runs on):**
- main **`b282699ac091`**, plus M3 **`3bc7ec26fe28`**, plus step C **`3e9d6ec9f449`** (`feat/map-client-step-c-blend`,
  no PR yet), all pinned.
- The merge is **`76c617faa200`** on `bench/frame-bench-v20`, as one more merge into `7e7ae82f174e`. It was predicted
  by merge-tree, with every conflict listed before any was resolved.
- The proof window, FT-SEA, FT-HR and S2b all run on this tree.
- **Every reading is labelled "measured on step C at 3e9d6ec9f449, not on main".**

**Its three conflicts, one hunk each, resolved as ruled:**
- the guard script's MARKERS: the union of 12, ending `[map-bench]`;
- the release workflow: both positive-control steps, the card bench and then the map bench;
- the refusal: step C's block verbatim.

**C1's bench exemption is REMOVED** on this tree: its FRAME_BENCH define, its `#if` term, and the bench's
`REFUSAL_IF` and `REFUSAL` lines, which now equal step C's.
- Every FRAME_BENCH build defined it. On a tree with step C, it could only mask an FT image that lost step C.
- **Such an image now hits the `#error` by construction.**
- C1-X asserts step C's `MapSweep.h` include present and the exemption absent. Its three plants (the define back, the
  term back, the include removed) each read exactly {C1-X} red.

**Checked on `76c617faa200`:**
- the guard selftest catches 12 of 12 planted markers;
- `marker_xcheck`'s differences equal the union of both parents' own (each parent reads the same three as main), so
  nothing was dropped;
- check_frame_bench: 17 rows, 0 failed, and its selftest 20 of 20;
- check_map_draw: 12 checks, 0 failures; CD4 sees 18 draw regions with the refusal present;
- the pre-push chain: 10 ok and 1 skipped.

**THE RE-READ RULE (registered now).** If the step C that finally merges differs from `3e9d6ec9f449` in the draw path,
FT and S2b are re-read on the merged tree before their numbers are used for v20.
- The draw path is `include/MapCompose.h`, `include/MapPlaces.h`, `include/MapLayer.h` and `include/MapSweep.h`, plus
  the draw's call sites in `src/AircraftManager.cpp`.
- It is checked by a textual pin, as FB-REC is, with the FRAME BENCH regions removed first.
  - **Since 2026-11-15 the pin is a script:** `scripts/draw_pin.py`
    (`ba03b003b609f4c4ff39d925b64140a39519968416c8ecc3637b6218ba98ff9c`), with its value registered at `3e9d6ec9f449`. See
    "DRAW-PIN" below. "Differs in the draw path" means its exit 1 on the merged revision; exit 0 means no re-read. The
    script, not this paragraph, decides.
- **The cost, named:** step C is unreviewed. FT's deltas (package against package, under the same draw code) are robust
  to the draw's version to first order only.

**FT EXTENDS step C's reviewed MAP_BENCH swap, instead of adding a second one.**
- Step C's `MapBenchAccept` already sets the home to the package's own centre, copies the package into the PSRAM
  landing buffer, runs `maplayer::Load` and installs it.
- Under MAP_BENCH, `RequestBasemap` returns false, so nothing is fetched. The draw re-accepts whenever
  `mapHeldLen == 0`, which also covers a CA10 drop mid-scene.
- FT generalizes this to its pinned packages. The package index and `wxOff` are driven by the frame bench's scenes, and
  MAP_BENCH's phase machine (`MapBenchTick`) is off in the FT envs.
- **FT-SWAP's comparison set is the three draw-side fields that ConsumeBasemap's Accepted branch sets:** `mapPkg`,
  `mapHeldLen` and `++mapPkgSeq`. The draw reads exactly those (`mapHeldLen == 0` means no draw; `mapPkgSeq` is the
  rebuild key; `mapPkg` gives `maxVertices` and `Frame`). The policy's `held` is read only by the fetch decision.
  - A plant drops one field and must read red.

**THE FLAG SET, per FT and S2b env** (read off each built ELF against an anchor, never off the ini):
- `FEATURE_CLOUD_FEED` (`cloud.prod`), `WX_LAYER_SHIPS`, `FRAME_BENCH` and `MAP_BENCH`.
- `BLIPSCOPE_MAP_STEP_C` comes only from `MapSweep.h`, never from a `-D`; CD4 forbids that.
- FT-MAP's weather OFF comes at run time, through `mapBench.wxOff`.
- **To check before the envs are registered by sha:** that MAP_BENCH compiles WITHOUT `WX_LOOK_AB`. Step C's mapbench
  env sets it, and FT must not.
- **The plant:** an FT env with `MapSweep.h`'s include removed must fail to build at the refusal. It holds by
  construction now, and is shown at build time.

**MAP-SERVER'S FINDING: steady p95 is blind to the package.**
- On step C, `mapcompose::Frame` rebuilds only when `Stale`, that is, when the key changes: rotation, `radiusMi`,
  `pkSeq`, `airportsGen` or the fixed boxes.
- Every other frame copies the cached 240 × 240 layer, 115,200 B, at the same cost whatever the package.
- So the package costs time only on a rebuild. The FT scene splits in two:
  - **(a) STEADY:** 0 rebuilds expected, with the count printed. An airports-fetch arrival that bumps `airportsGen` is
    the named exception. The steady p95 EXCLUDES every frame `Record` flags `rebuilt`, and those frames are counted per
    part.
  - **(b) FORCED-REBUILD:** each rebuild is forced by re-accepting the same package (`++mapPkgSeq`, identical inputs).
    That is the package-accept path, and no bench-only poison of the key is used, which would be a second path into
    it. There are enough rebuilds per scene for a noise estimate, frozen with map-server's rows.
- **What `usTotal` spans, read from the code:**
  - `BuildInfo::usTotal` is measured inside `maplayer::Build` alone (`MapLayer.h:283` to `:389`): Build's passes (S1,
    water, rivers, roads, colour), with their per-pass µs. It fills only when `in.clock` is set.
  - **It does NOT include `mapplaces::Load`.** Load runs in `Rebuild` before Build (`MapCompose.h:296-300`), and only
    when `placesSeq != pkSeq`, so on every pkSeq-forced rebuild.
  - **It does not include the byte swap or `DrawNames` either** (`:306-307`).
  - The `[map] draw: rebuilt in N ms` line (`AircraftManager.cpp:3866-3872`) is `millis()` around the whole `Frame()`
    call: Load, Build, swap and names. **It stays REPORTED only**, being too coarse to grade.
- **So the subtraction "usTotal minus Load" would be wrong:** usTotal never contained Load. The registered composition
  instead:
  - **Laps:** three bench-only µs laps on `in.clock`, in FRAME BENCH regions inside `Rebuild`: Load, Build (= usTotal),
    and swap + names. The shipping image's bytes do not move, and the draw-path pin excludes the regions.
  - **The zoom/redraw hitch at this rung** = Build + swap + names. It is graded.
  - **The package-update hitch** = Load + Build + swap + names. It is reported, with Load beside it.
  - **The millis line** is reported as the outer bound, and must not be less than the µs sum.

**ADDITION 2, split in two:**
- **Per frame:** step C's own `mapFrameDrawn` must be set on every measured steady frame (the cached composite copied
  that frame). Else the scene is the INSTRUMENT.
- **Per rebuild:** BuildInfo's feature counts and `nPlaced` must equal map-server's host-computed values for that
  package and rung, registered first. Else the scene is the INSTRUMENT.
- **Plants:** a frame with the copy skipped, and a rebuild with one feature pass skipped, each reading INSTRUMENT, not a
  frame time.

**THE ORDER:** the FT code follows once map-server registers its rows and host values. Nothing builds under 30 GiB.

| Q | the question | the default |
|---|---|---|
| Q-S1′ | the tree | main `b282699ac091` + M3 `3bc7ec26fe28` + step C `3e9d6ec9f449`, merged at `76c617faa200` (ruled) |
| Q-FT-E | where Load and swap + names are timed | bench-only µs laps on `in.clock` inside `Rebuild`'s FRAME BENCH regions |
| Q-FT-F | MAP_BENCH without `WX_LOOK_AB` | to be shown compiling before the FT envs are pinned; if it cannot, back to the orchestrator |

### THE REBUILD ROW, as agreed with map-server (#682 §5k, `22728e98ddde`; the orchestrator's rulings, 2026-11-15)

**The graded figure, one sentence, word for word in both registrations:** "The graded figure is a forced rebuild's
microsecond total, measured around the one Frame() call that rebuilds, minus its mapplaces::Load lap: Build's passes,
the byte swap and the names, plus Compose when that frame shows the weather (FT-HR); Load and the total are reported,
never graded."
- **Compose, read from the code:** `Rebuild` ends with `m.layerFresh = true` (`MapCompose.h:309`). In the same
  `Frame()` call, with `wx != nullptr` and `layerFresh` set, it runs `Compose(m.layer, wx, m.composite)` (`:337`).
  With the weather off (FT-SEA), `Frame` returns `m.layer`, and Compose never runs.
  - Compose also runs on a steady frame whenever the weather layer changes (`wxGen`), with no map rebuild. That is
    steady-window noise, named here, and not part of the rebuild row.
- **Four bench-only µs laps on `in.clock`,** in FRAME BENCH regions: Load; Build (= `usTotal`, with its passes
  printed); swap + names; and Compose.
  - **The total** is a µs timer around the rebuilding `Frame()` call. The residue (the total minus the laps) is printed,
    and must not be negative.
  - The millis `rebuilt in N ms` line is printed beside them, as the outer bound.
- **N = 20 forced rebuilds per scene,** 2 s apart, after the scene's 120 s steady window. Each is forced by re-accepting
  the same package (`++mapPkgSeq`). Fewer than 10 is INSTRUMENT.
  - The statistic per scene is the median of its N, with the p95 and N printed beside.
  - The U_part order-statistic ranks at N = 20 are 5 and 16.
- **The margins:** `M_part = max(2 × |median_A − median_B|, 2 × U_part)` from KORD's A/A (the same bytes,
  `cb59bb06a506`, twice). There is one margin each for the graded figure, Load and the total. They are frozen before any
  graded reading.
- **The window's order:**
  1. KORD A/A pair 1, its margins printed and hashed;
  2. the timer plant;
  3. the graded pairs;
  4. KORD A/A pair 2, LAST, graded against pair 1's frozen margins. Over a margin is INSTRUMENT, and nothing is graded.
- **The timer plant (the timer must see a known change first):** `delayMicroseconds(20000)`, **D = 20,000 µs**, inside
  the swap + names lap. That is after Load and inside the graded figure, on KORD-A0, in its own scene.
  - **Why 20,000, not the 5,000 first agreed** (the orchestrator's suggestion, taken before the code): it must be
    separable (D > 2 × M_zoom), and no rebuild noise has been measured yet.
    - 5,000 µs would be separable only for M_zoom under 2.5 ms; 20,000 µs is separable for any M_zoom up to 10 ms.
    - The plant has its own ungraded scene, so a larger D moves nothing else, and 20 ms is far inside the 10 s task
      watchdog.
    - **STRUCK (the orchestrator's catch, 2026-11-15): "a larger D also tests the timer's scale harder".** It was false
      for the plant as first written. That plant waited on `in.clock`, the very timer the laps use, so the lap would
      read 20,000 whatever that timer's true rate. Such a wait proves only that the plant fired, and that the split
      puts it in the right lap (Load unmoved). `delayMicroseconds` would not fix it, being esp_timer on this core too.
  - **OPTION (a), taken and registered before the scene table:** the plant's wait spins on the CPU cycle counter
    (`ESP.getCycleCount()`, converted at `getCpuFrequencyMhz()`). This clock is independent of esp_timer (`micros`), and
    it reaches the pure header as `Inputs::spin`, the way `in.clock` does. So the swap + names lap reading D within its
    band now also tests the lap timer's conversion. Every rebuild record carries the CPU MHz.
    - **Its limit, named:** the cycle counter and esp_timer both derive from the one 40 MHz crystal. A crystal-frequency
      error scales both equally and is not seen. What the plant does test: esp_timer's divider and conversion path,
      the laps' arithmetic, and any µs-unit bug.
    - FT-PLANT holds the wiring: the hook counts cycles and never `micros()`. Its plant, the hook rewired to `micros()`,
      reads exactly {FT-PLANT}.
  - Every rebuild record carries a plant bit: set on every planted rebuild, clear on all others.
  - **Predicted:** the graded delta = D ± M_zoom; the total's delta = D ± M_total; the Load lap's delta within ± M_load
    (the plant sits outside Load).
  - **Separable only if D > 2 × M_zoom,** checked once pair 1 freezes the margins. Otherwise INSTRUMENT.
  - A delta near 0, Load moving, or the bit disagreeing with the plan: INSTRUMENT, and nothing graded.
- **The steady window:** each scene prints its rebuild count, expected 0. The named exception is an airports-fetch
  arrival that bumps `airportsGen`, recorded with its fetch line. A rebuild with no fetch line is outside the set.
- **The view print:** each scene prints home (at 2 dp, exact by FT-CTR), rLat, rLon and askedMm, so map-server's host
  counts can tell a view mismatch from a draw failure.

**THE HOME TABLE, pinned on the sibling at `08e68191f7b7`:**
- `test/fixtures/frame-bench/ft_sea.pins` (6) and `ft_hr.pins` (11: the run-10 A0 column and option 2-L at 19.4).
- Each full sha256 was read from the staged files' bytes, and its 12-character prefix matched against map-server's docs.
- There are 14 distinct packages (KJFK-A0, KBOS-A0 and KORD-A0 are FT-SEA's sea bytes). With the rungs 10, 25 and 50 mi,
  they make the 42 cells of map-server's host counts.
- **Read:** FT-PKG and FT-CTR, 17 pins, 0 off. Every centre is at exactly 2 dp, MIDTOWN's included.
- **The conversion,** given to map-server for its host port: `src/AircraftManager.cpp:953-954` at the pin,
  `lat = pk.lat0Micro / 1e6; lon = pk.lon0Micro / 1e6;`, in double precision, in MAP_BENCH's Initialise region. FT reuses
  it unchanged. radLat and radLon are at `:1031-1032`.

### THE COMPILE CHECK, and TBL-DEV per FT scene (the orchestrator's rulings, 2026-11-15; written 2026-11-15T08:41:47Z)

**The compile check is ACCEPTED,** at `bb28b362c287`. Both images are compile checks only, never run images.
- ft1-sea is 3,252,864 B (`66d9b7b79387`) and ft1-hr is 4,310,304 B (`17d2993a6139`). Each was built under the disk
  guard, and C: ended at 31.43 GiB.
- **Warnings:** 40 unique, the same in both builds apart from the libdeps path. None comes from FT code. The three in
  `AircraftManager.cpp` are blamed to main's own commits.
- **ft_elf.py** (`509172001d8f`) was written before any image was read, with its anchor present.
  - SEA: 18 present, 12 absent, 0 off. HR: 28, 7, 0 off.
  - Every pinned package's exact bytes are in `firmware.bin`, hashing to its pin: 6 of 6 and 11 of 11.
  - One needle was changed from a source reading before any image was read. That is disclosed and accepted. The run
    images get a fresh read, with the script at its final sha.

**Two misses, recorded as misses:**
1. **The size prediction** ("under 4 KB" over its base) took C1's tree as the base, so it is false. The FT images are
   3,252,864 B and 4,310,304 B, both under ota_0, the registered bound.
2. **TBL-DEV was not printed per FT home.** Only the boot line around Bend existed, so every FT scene's TBL row would
   have read INSTRUMENT. It was caught before any run.

**THE FIX, at `6f0cdd1381eb`** (the orchestrator's yes):
- **The line:** `[frame-bench] ft S<k> table sum <crc>`, printed once per scene as it is applied. It comes from
  `fbtable::Sum` at the home SetHome stored (the doubles OnCloudPoll serves the table around), with kLevel = 40 and
  kTblT = 1794441386. It carries a CRC only, never a coordinate.
- **Printed only in the validated branch:** the line is printed only inside the branch where the scene's package
  validated. A scene without it has no device reading. The reader grades that as INSTRUMENT (a gap), never as the
  previous scene's sum.
- **FT-TBL holds it:** `check_frame_bench`, 25 rows, 0 failed; selftest 36 of 36. Its four plants each read exactly
  {FT-TBL}:
  - the call removed;
  - the sum taken around Bend's strings;
  - the call moved outside the branch;
  - a coordinate printed beside the sum.
- **The cited lines did not move:** the conversion at `:988-989`, SetHome at `:2364`, radLat and radLon at `:1066-1067`.

**The static checks, re-run at the fix** (predictions frozen before the edit):
- **check_frame_bench:** 25 rows, 0 failed; selftest 36 of 36. The frozen prediction said 35. The fourth FT-TBL plant
  (the float-format clause) was added before any run, and the prediction amended then.
- **check_map_draw:** 12 of 12.
- **The three sabotage drivers' anchors:** 6 of 6, 16 for 14 plants, and 19 of 19.
- **marker_xcheck: A MISS IN MY PREDICTION, NOT IN THE CODE.** I predicted "equal, exit 0". That is the exact-equal
  form, already ruled a false premise on main. The registered form, the parents' union, holds:
  - the tree's difference is identical to its parent `bb28b362`'s (279 source files, MARKERS 12, 11 bench-shaped tags);
  - both of its plants read 1.
- **The push wrapper:** every guard passed.

**THE HOST'S PER-PIN SUMS, registered here before the run images are rebuilt:**
- **The tool:** `tbl_host_ft.py` (`78faa95c1992`), a one-off in my scratch like marker_xcheck. It has two sides, as
  `tbl_host.py` does:
  - **C++:** `FrameBenchTable.h`'s Sum (`e18297ebbe75`), compiled by WSL's g++, at int32 lat0Micro / 1e6, which is the
    device's conversion. The centres reach it on stdin; none is written to disk or printed.
  - **Python:** `fixed_traffic.py`'s `aircraft()` (`b530faf4`), with the pin's centre injected into its HOMES.
- **Every prediction held** (frozen in its docstring before the first run):
  - **ANCHOR:** Bend injected equals Bend by name, `b1ace60e`.
  - **SENSITIVITY:** a 0.01° move changes both sides.
  - **ROWS:** 15 (Bend and the 14 distinct pins), with C++ equal to Python on every one.
  - **PLANT:** one phase constant changed in a copy of the header. 15 of 15 rows read off, exit 1.
- **The sums** (`ft_tbl_sums.json`, sha256 `b35f21b4bcdc`), at n = 40 and t = 1794441386:

  | package | sha12 | sum |
  |---|---|---|
  | KJFK-nosea | `ee8c834bc81a` | `db63b2ec` |
  | KJFK-sea | `52ac9dc39de0` | `db63b2ec` |
  | KBOS-nosea | `ed5393b4fc0f` | `f1389592` |
  | KBOS-sea | `f21d705d8b8a` | `f1389592` |
  | KORD-nosea | `4238243a57a6` | `cba4d741` |
  | KORD-sea | `cb59bb06a506` | `cba4d741` |
  | KEWR-A0 | `dff28487eaf4` | `7229302f` |
  | MIDTOWN-A0 | `20a22bed5510` | `19f1eb3f` |
  | KLGA-A0 | `feb6ef3bdf04` | `fe512af0` |
  | KEWR-2L194 | `c619f4a89f14` | `7229302f` |
  | MIDTOWN-2L194 | `aaede1785364` | `19f1eb3f` |
  | KJFK-2L194 | `e042d61aa8f1` | `db63b2ec` |
  | KLGA-2L194 | `6ab44cd8fbea` | `fe512af0` |
  | KBOS-2L194 | `696c6d00122d` | `f1389592` |

- **Their limits, named:**
  - **Point, not variant.** The sum is built from the point's centre, so every variant of a point shares it (6 distinct
    sums over 14 pins). The variant is told by the scene line's sha12, by FT-TAB and by ft_elf.
  - **Q4 resolution only.** It sees only a move that changes a Q4 line (about 1e-4°). It checks the served table, not
    the centre's last digits.
- **The reader's rule for each FT scene's TBL row:**
  - the scene's printed sum equals its package's sum above: ok;
  - the two differ: INSTRUMENT, and the read stops. This is TBL-DEV's verdict as registered for C1 (above: "unequal is
    the INSTRUMENT"); a last-unit flip in the device's libm is the named way it fails on a correct port;
  - the line is absent: INSTRUMENT.
  - **Corrected before any run image was read (2026-11-15T08:45:50Z):** this bullet first said a difference "reads off".
    That contradicted TBL-DEV's registered verdict and the draft reader (`fb_read_ft.py`), which both say INSTRUMENT.
    The registration is the one above.
- **Where the sums come from:** `tbl_host_ft.py`, which imports `tbl_host.py`'s own mirror (`f2f838f9a3da`, sha
  asserted). The FT section's "computed on the host by `tbl_host.py`" means this tool. Never typed.

**THE RUN IMAGES, built at `6f0cdd1381eb`** (build_ft1's recipe and guard, labels ft2-*; 08:45:34Z to 08:50:20Z):
- **ft2-sea:** 3,252,976 B, sha256 `2b4e0bf1a5e143d8eb13ac8540ea0c0a796e574ff4ea15ba7e4902e8a573e93a` (112 B over ft1-sea).
- **ft2-hr:** 4,310,432 B, sha256 `054e60e67bd4bbb9176068869a3ee40daa0da05fb3464ded60f14623600722c2` (128 B over ft1-hr).
- **The guard:** 31.42 / 31.42 GB, read 60 s apart; 31.42 then 31.39 GiB before each build; 31.36 at the end. Both build
  dirs were removed, and the tree was clean after each build.
- **Every reading as frozen before either image existed:**
  - **Warnings:** each log has its ft1 twin's 40 unique warnings. The one difference is main's three in
    `AircraftManager.cpp`, each at exactly +2 lines (`:5275`, `:6831`, `:8918`). The comparison script
    (`warn_cmp.py`, `4474cbcff827`) was written after the images existed. It encodes the prediction sent before they
    did, and it reads only the build logs.
  - **ft_elf.py at its final sha `45e92539c5e7`** (frozen at 08:45:00Z, one PRESENT needle added: the table-sum format):
    SEA 19 present, 12 absent, 6 packages, 0 off; HR 29, 7, 11, 0 off.
- **map-server's cited lines** read unchanged at the commit: `:988-989`, `:2364`, `:1066-1067`. The table-sum call is
  `:2365`.
- **The orchestrator ACCEPTED both run images.**

**THE READER IS FROZEN: `fb_read_ft.py` at `8a10f116fe029ad6199b41eda38740832be6c040fd083ba2244b6d7454137835`**
(2026-11-15T08:55:39Z; a byte copy is kept under its sha). It reads one trace of one FT window; its rows are in its
docstring and THE REBUILD ROW above.
- **Its two inputs, pinned inside it.** It refuses any other bytes, as the INSTRUMENT:
  - **HOST:** map-server's `ft_host_counts.json`, sha256 `f5bb125c6c21e5a3089cdccb1d038c7bbe0901da4c185d2f5176898c07594095`.
    It has 42 keys `<sha12>:<rung>` and eight integers each, in the order the device's `counts=` prints them: rings,
    rivers, primary, secondary, primaryInView, secondaryInView, secondaryDrawn, level. All eight come from
    `maplayer::Build`'s own BuildInfo on both sides. map-server read the headers as byte-identical at `6f0cdd1381eb`.
    Its port is `fb_counts.cpp` `ba9ee2290eeb`, with `to_json.py` `48cc412f90c5` and `run.sh` `134b357c16ba` in
    PROVENANCE.txt beside it. Read here: 14 shas × rungs 10/25/50, and the four feature counts equal across the rungs of
    each package.
  - **TBL:** `ft_tbl_sums.json`, sha256 `b35f21b4bcdc...` (above).
- **Three fixes before the freeze,** each with its plant (accepted at `1f747b9d78d4`):
  - the sums file's shape, with n and t checked;
  - a package with no host sum is the INSTRUMENT (it had compared None == None and passed);
  - three TBL plants.
  - **At the freeze:** the input pins, with a plant (one byte changed under the same pin is refused).
- **Selftest: 16 of 16, PASSED.**
- **Two controls on the real files:**
  - an empty trace stops at BOOT, never at INPUT, so both pins match the files on disk;
  - the counts file passed as `--tbl` stops at INPUT.

### THE SIBLING's PROOF SET, frozen with its image (2026-11-15T09:08:12Z)

The window registered above ("The sibling's proof window, before FT"), on the orchestrator's schedule: the proof today,
then FT-SEA in daylight (12:51-21:13Z per map-server), ending by 17:20Z. **No I-3 in this set** (the orchestrator's
ruling): the proof image is a bench image on a branch that never merges, and I-3 guards a shipping image this window
does not produce.

**The image: v20-P,** env `blipscope-s3-128-framebench-c1b-proof`, at `6f0cdd1381eb`.
- 1,967,312 B, sha256 `1481f29504166b95cbe3d656370ad35e4b6f4d101cb04faef9b4534a40dd6ae9`; 63 warnings.
- The guard: 31.06 / 30.99 GB, read 60 s apart; 30.93 GiB after. The tree was clean, and the build dir was removed.

**The same-instrument check** (`pp_diff_v2.py`, `7f4e6e1f121a`, as registered). The c1b and c1b-proof compile databases
are at `6f0cdd1381eb`, 259 entries each.
- Both envs' libdeps are copies of ftsea's, verified file by file (843 files, 0 differing). Both pio logs show no
  "Installing" line.
  - The control: two independent installs (ftsea, fthr) differ only in `integrity.dat`, which lists the same seven
    libraries at the same versions in another order, with no env name.
- **Plain: PASS.** Each source differs in the scene table's 2 lines and nothing else. Both guards held: "c1b-proof" is
  counted 0 times in the c1b text, and 3 replacements were made in the proof text.
- **The counter plant: RED.** Exactly 1 other line per source, each the planted increment.
- **The env-name plant: PASS.** Every prediction HELD.

**The gate for the two added rows,** executed by the reader on every read, from git at the image's commit:
- **FB-REC:** `check_frame_bench.py`'s own `fb_rec()` at that commit. It reads `425040a3999d`.
- **PUSH-SCOPE:** `push_scope.py` (`9df639f6fed5`).
  - **SCOPE-M3:** `pushtask`'s block without its 4 bench regions equals `3bc7ec26fe28`'s, and LGFX.h, variants/s3_128.h
    and FrameBuffer.h are byte-identical.
  - **SCOPE-RD:** the block with its regions equals `0d6308cb77b6`'s (where the corrected centre was read).
  - Its selftest has three plants: a push line (both rows red); a panel header line (both red); a line inside a bench
    region (SCOPE-RD alone).

**The reader: `fb_read_v20p.py` at `183cb2ec1f052d9c01a9caa2f6ed01b3322a74759673bb1db19a0b4a56168d7c`** (a byte copy is
kept). It imports `fb_read_plants.py` (`bb1315423a29`, sha asserted, which asserts `fb_read_c1.py` `0dde3f661ee1`).
- **THE PACKAGE's six rows,** at their true size, by C1's own `read_trace()`.
- **I-5 B:** 6,400 to 7,300 ns, two-sided. Over 50,000 ns is read_trace's STOP.
- **PUSH-SYNC:** B plain (S0) push − sync = 23.7 ± 1.0 ms; sync is reported.
- Both are graded only if the gate holds; otherwise they are reported and re-registered.
- **Added for this tree: the map-drawn bit (32) on any measured frame is the INSTRUMENT.** Step C is what the sibling adds
  to C1. Its bit cannot rise in a C1 scene (DrawMapBase returns before the copy when MapEnabled() is false, and C1's
  map-off region makes it false). C1's reader never looks at the bit, so this makes the argument a check.
- **Selftest: 7 of 7, PASSED.** The gate holds at `6f0cdd1381eb`. The cases:
  - the real cap8 trace: exit 0;
  - the record cost at 5,000: I-5 B alone;
  - the record cost at 60,000: the INSTRUMENT;
  - S0's sync 1.5 ms higher: PUSH-SYNC alone;
  - the same plant with the gate off: exit 0 (reported, not graded);
  - bit 32 set on one S0 frame: the INSTRUMENT.
- **The reference reading, on cap8's real trace** (THE PACKAGE's proof, the C1 image): 8 graded rows, 0 off. Record cost
  6,824 ns; push − sync 23.72 ms (sync 0.46); bit 32 on 0 of 7,751 measured frames.

**The window's scripts,** cut by `cut_v20p.py` (`c4e040991ed4`) from cap8's accepted three. Each replacement is
asserted once, and each diff is only that: the ledger cap10, the manifest, the image label and path, and a header line.
- `win_v20p.ps1` `d9464ac4fd3d`; `launch_v20p.ps1` `0dab4b1e048e`; `make_v20p_manifest.py` `d119e5403362`.
- **The manifest** (`v20p_manifest.json`, `427e79f8578c`), against the release's table `28f336fe57a9`. Every write is
  ota_0 alone (0x20000, 6,553,600 B): no factory image, no erase, nothing to NVS.
  - v20-P `1481f2950416`, write 0x020000..0x2004CF (30.0 % of ota_0);
  - restore-v19 `f6d73e391631`, write 0x020000..0x1F7A3F (29.5 %).
- **The dry run, both halves:**
  - v20-P's file stages and matches, and no port is touched (exit 0);
  - ft2-sea's file under the v20-P label STOPs: "not the acknowledged manifest's; nothing written" (exit 20).
- **The window:** a 21-min trace of S0-S4 with the PC quiet and no /diag/fb grabs. Then the v19 restore: its boot
  tokens (a Wi-Fi rejoin, no portal), read-mac and HTTP. About 30 min in all.

**Predicted:** every graded row in (THE PACKAGE's six, I-5 B and PUSH-SYNC), exit 0.
- S4 not caught goes to the owner (exit 3).
- Any other off reading, or anything outside the set, is a STOP.

### THE SIBLING's PROOF WINDOW, as read: every graded row in (2026-11-15T09:38:35Z)

The orchestrator opened it on acknowledging the set at `920babdbba66`. E2:08 was reserved by serial for map-client
(09:11:39Z, unreserved before), and the PC was quiet of builds; the top CPU user was a background cloud sync.
- **The window's ledger (cap10):**
  - E2:08 was identified by its serial on COM18 at every step.
  - v20-P: ota_0 alone, the checks line exact, verified, reset ok (09:12:24Z). The 21-min trace exited 0 (09:33:25Z).
  - read-mac recovered the board after the trace closed.
  - restore-v19 `f6d73e391631`: ota_0 alone, verified, reset ok (09:33:48Z).
  - Its boot tokens PASS: `fw=v19`, the saved credentials, `[WiFi] CONNECTED`, `current=19 latest=19`. All six portal
    tokens are absent. The join was "fast join OK".
  - HTTP 200 on attempt 2 (09:37:13Z). E2:08 was released at 09:38:15Z.
- **The reader** (`fb_read_v20p.py` `183cb2ec1f05`, on the closed trace `b086d68f470f`; output `f8d89c088ee3`): exit 0,
  **8 graded rows, 0 off.**
  - **The gate held:** FB-REC `425040a3999d`, and PUSH-SCOPE (SCOPE-M3).
  - DELAYED-FLAG: S3 1489/1489, the other scenes 0.
  - D=10 push +9.95 ms; D=60 push +59.77 ms.
  - loop +10: +10.00 ms, push −0.01 ms.
  - M3-2 exact: S0-S3 1700/1700, 1696/1696, 1428/1428, 1489/1489.
  - DROP-CAUGHT: S4 handed 1429, presented 1428, dropped 1.
  - **I-5 B: 6,834 ns** (cap8 read 6,824). **PUSH-SYNC: 24.17 − 0.46 = 23.71 ms.**
  - Also held: TBL-DEV `b1ace60e`, and bit 32 on 0 of 7,742 measured frames.
- **Reported, not graded:** D=10 p2p +0.00; D=60 p2p 84.00 against its push's 84.00; S4 and S2 equal.
- **Named, not graded:** "present count != 1" reads 32 in S2 and 31 in S4, where cap8 read 18 and 15. The frozen reader
  exempts push60 scenes on a counted image from the per-frame 1 % rule (`fb_read_c1.py:148`). The exact per-scene count
  governs there, and it held. The difference from cap8 is recorded, not graded.
- **So FB-REC and the push scope hold on the sibling, and both corrected limits read in band.** Nothing is
  re-registered. FT follows on the orchestrator's acceptance.
- **The orchestrator recorded it IN.**

### FT's WINDOW SETS (2026-11-15T09:40:40Z): FT-SEA today, FT-HR on Tuesday

- **The scripts,** cut by `cut_ft.py` (`4777b1be57d8`) from the proof window's accepted three. Each replacement is
  asserted once. The diffs are only:
  - the label and image;
  - the ledger (cap11 for SEA, cap12 for HR);
  - the manifest;
  - the trace length;
  - the scene comment and the header.
  The restore section is untouched.
  - `win_ftsea.ps1` `1c71fbe9dfe5`, `launch_ftsea.ps1` `b4357279064f`;
  - `win_fthr.ps1` `a603e1e4c4f3`, `launch_fthr.ps1` `f61db8f312ba`;
  - `make_ft_manifest.py` `ec7ed38bc254`.
- **The trace lengths, from FrameBench.h's constants.** A scene is about 262 s: settle 100 s, measure 120 s, 20 forced
  rebuilds 2 s apart, and the dump.
  - **FT-SEA:** 31 scenes, about 135 min, traced for **145 min**;
  - **FT-HR:** 49 scenes, about 214 min, traced for **225 min**.
  - One single-open trace each, with the PC quiet and no /diag/fb grabs. Then the v19 restore, its boot tokens,
    read-mac and HTTP. FT-SEA is about 153 min end to end.
- **The manifest** (`ft_manifest.json`, `b3e498423dc9`), against the release's table `28f336fe57a9`. Every write is
  ota_0 alone: no factory image, no erase, nothing to NVS.
  - ft2-sea `2b4e0bf1a5e1`, 0x020000..0x33A2EF (49.6 % of ota_0);
  - ft2-hr `054e60e67bd4`, 0x020000..0x43C59F (65.8 %);
  - restore-v19 `f6d73e391631`, 0x020000..0x1F7A3F.
- **The dry run, both halves, for both rows:**
  - each row's own file stages and matches with no port touched (exit 0);
  - the wrong file under the row's label STOPs, "nothing written" (exit 20): v20-P's under ft2-sea, ft2-sea's under ft2-hr.
- **Daylight** (map-server; the reader requires the night bit clear on every measured frame): today 12:51-21:13Z, and
  Tuesday 12:54-21:11Z, each with 10 min clear, the same for both rows' homes.
  - **FT-SEA's start window: 12:51Z to 14:47Z**, ending by 17:20Z for the sitting.
- **The read:** `fb_read_ft.py` `8a10f116fe02` (frozen, its inputs pinned), on the closed trace:
  `--row SEA --pins test/fixtures/frame-bench/ft_sea.pins --host ft_host_counts.json --tbl ft_tbl_sums.json`.
  - **Exit 2 is the INSTRUMENT:** nothing is graded, and the read stops.
    - Named in advance: a Worker outage stops the served table (OnCloudPoll runs only after the real GET), which
      reads as STEADY drawn != 40.
  - **Exit 1** is a graded delta over its margin: a finding, to the orchestrator.
  - **Exit 0:** every instrument row in, and every graded delta within its margin.
  - **The graded deltas themselves are not predicted.** They are what the window measures, against margins frozen
    from pair 1 before any graded pair.

### FT-SEA's WINDOW (2026-11-15, written 2026-11-15T15:24Z): the reader exits 1, 9 of 9 pairs over their margin

Recorded as measured. The ruling is the orchestrator's, and nothing here interprets the numbers.

- **The start,** on the orchestrator's pre-authorized terms:
  - E2:08 was unreserved at 12:51Z, then reserved by serial (`--by map-client`) at 12:51:28Z, on COM18.
  - The PC was quiet: no build, flash or trace process outside the check itself, and CPU at 14 %.
  - `launch_ftsea.ps1 -Port COM18` ran detached at 12:52:41Z. A pid waiter had a 175-min deadline; the process
    exited by 15:22:10Z.
- **The ledger** (`cap11\window.log`):
  - ft2-sea `2b4e0bf1a5e1` (the manifest's) was written to ota_0 only: 0x020000, 3,252,976 B, verified, reset ok.
  - The trace ran 12:53:18Z-15:18:18Z, 145 min, exit 0. Its sha256 is `f5df38b36bc5`.
  - restore-v19 `f6d73e391631` was written to ota_0 only: 1,931,840 B, verified, reset ok.
  - The restore read PASSes: `[build] ... fw=v19`, `[WiFi] CONNECTED` and `[ota] ... current=19 latest=19` once each,
    and 0 of the 6 portal tokens.
  - The join: fast join OK, then HTTP 200 on attempt 2. This is the proof window's shape, line for line.
  - E2:08 was released at 15:23:08Z.
- **The read:** `fb_read_ft.py` `8a10f116fe02`, with its pinned inputs, exited 1. Its output is
  `7130f3c05164cdf5404631087bc738d98e35328f50682e3c9eba03433f933015`. Measured on step C at 3e9d6ec9f449, not on main.
  - **The instrument rows are all in.** BOOT, TBL, STEADY, FORCED (with the host counts), MARGINS, PLANT, PAIR2 and
    STEADY A/A each raise the exit-2 INSTRUMENT before the graded loop, read from the reader's code. The graded lines
    printing is therefore the evidence that every one of them held.
  - **MARGINS** (pair 1's KORD A/A, µs; sha `431231e808e5`):

    | rung | graded (g) | Load | total |
    |---|---|---|---|
    | 10 mi | 358.0 | 6.0 | 354.0 |
    | 25 mi | 355.0 | 6.0 | 355.0 |
    | 50 mi | 420.0 | 10.0 | 414.0 |

  - **PLANT** at 25 mi: graded +19,889.0 µs (D 20,000 ± 355.0), total +19,887.5 (± 355.0), Load −1.0 (within ± 6.0).
- **GRADED, nosea → sea** (the zoom hitch is the graded figure, against M_zoom; N 20/20 on every pair):

  | point | rung | zoom hitch, µs | M_zoom | Load | total | steady p95, ms (M 3.08) | verdict |
  |---|---|---|---|---|---|---|---|
  | KJFK | 10 mi | +13,244.5 | 358.0 | +0.5 | +13,244.5 | +0.00 | OVER |
  | KJFK | 25 mi | +9,685.0 | 355.0 | +0.5 | +9,686.0 | +0.00 | OVER |
  | KJFK | 50 mi | +8,221.0 | 420.0 | +3.5 | +8,222.5 | +0.00 | OVER |
  | KBOS | 10 mi | +6,678.0 | 358.0 | +0.5 | +6,676.0 | +0.00 | OVER |
  | KBOS | 25 mi | +5,169.0 | 355.0 | +0.0 | +5,166.5 | +0.00 | OVER |
  | KBOS | 50 mi | +7,542.5 | 420.0 | +0.0 | +7,538.5 | +0.00 | OVER |
  | KORD | 10 mi | −655.0 | 358.0 | +1.0 | −657.5 | +0.00 | OVER |
  | KORD | 25 mi | −429.5 | 355.0 | +0.0 | −415.5 | +0.01 | OVER |
  | KORD | 50 mi | −447.0 | 420.0 | +2.0 | −454.5 | +0.00 | OVER |

  - Every Load delta is within its M_load, and every steady p95 delta is within 3.08 ms. Only the zoom hitch moves,
    with the total beside it.
  - **Named, not explained:** KORD's three deltas are negative and the other six are positive. No explanation was
    registered, and none is offered here.
  - FORCED checked that the device's counts equal map-server's host counts for every package and rung. Reading the
    per-cell counts against these deltas is map-server's.

### S2b's map-rebuild frames: the tag, the count and the p95 rule (registered before S2b's code and window, 2026-11-15T16:05Z)

**Why (the orchestrator's ruling, from the FT-SEA redraw read).** Today a MAP rebuild frame carries no per-frame tag, so
a rebuild inside a measured window is counted in the p95 and is invisible per frame. These line refs are on the sibling
at `6f0cdd1381eb`:
- the only per-frame "rebuilt" bit, bit 0, is the weather layer's (`AircraftManager.cpp:3969`; `FrameBench.h:14`, `:379`);
- outside `FRAME_BENCH_FT`, every rebuild record prints its part as `settle`, whatever the phase (`FrameBench.h:400-401`;
  `ftPart` moves only under `FRAME_BENCH_FT`, `AircraftManager.cpp:2420-2455`).

This section is the rebuild-frame piece of S2b's registration. The rest of S2b is registered separately, before its window.

**The tag:**
- **Bit 6 (64) of `Rec.flags`: "the map layer was rebuilt on this frame".** Bits 0-5 are taken: weather rebuilt, the
  planted delay, crop, map enabled, night (`FrameBench.h:14-16`), and map-drew 32 (`AircraftManager.cpp:2347`). Bit 7
  stays free.
- **Set in `framebench::OnRebuild`** (`FrameBench.h:396`), which runs exactly once for every attempted rebuild: the
  `else if (rebuild && mapDraw.attempted)` branch, `AircraftManager.cpp:4128-4161`. A refused Build is tagged too, since it
  costs a frame.
- `Record` ORs 64 from a new `State::mapRebuilt` and clears it after the record, exactly as it does `rebuilt`
  (`FrameBench.h:379`, `:386`).
- **Outside `FRAME_BENCH_FT`, the record's part reads the phase:** `settle`, or `measure` while `measuring`. Under
  `FRAME_BENCH_FT`, the three FT parts are unchanged.

**The attribution, named.** On image B, with the push task (the v20 candidate), a frame is presented during the NEXT loop
iteration (`main.cpp:264` in the push task, against the loop's `RecordFrameUs` at `:1374`). So a rebuild's long p2p lands
on the record after the tagged one. On image A, the present comes before the record (`main.cpp:1365` then `:1374`), so it
lands on the tagged record itself. **"A rebuild's frames" are therefore the tagged record and the next one on image B, and
the tagged record alone on image A.**

**The p95 rule, decided before the window: IN.**
- The v20 bar's p95 includes rebuild frames, as v19's rule does (`:1606`) and as RP5 reads v19.
- The reasons:
  - the comparison with v19 stays like-for-like;
  - a redraw is a frame the customer sees;
  - FT-SEA's read puts the expectation at 0-1 per window.
- Printed beside every window, reported and not graded:
  - the tagged count;
  - the window's `measure` rebuild records;
  - the p95 with each rebuild's frames removed (as attributed above).

**The rows** (S2b's reader, frozen with S2b's registration):
- **TAG-AGREE:** per measured window, the tagged count equals the window's `measure` rebuild records, else the
  INSTRUMENT. They must agree: `OnRebuild` runs in the draw before `Record`, and `measuring` flips only after `Record`
  (`AircraftManager.cpp:2351`, then the scene driver).
- **TAG-SET**, static, in `check_frame_bench.py`:
  - `mapRebuilt = true` appears exactly once in `src/` and `include/`, inside `OnRebuild`;
  - `Record` sets 64 from it and clears it;
  - nothing else writes 64.
  - Its plants: P-TAG-EVERY, the set moved into `Record` unconditionally: {TAG-SET}; P-TAG-NOCLEAR, the clear removed:
    {TAG-SET}.
- **The reader's selftest:**
  - a synthetic window with one tagged record reads count 1 and prints both p95s;
  - a window whose `measure` records disagree with its tags reads the INSTRUMENT;
  - an image-B window drops two records per rebuild from the reported p95, an image-A window one.

**Predicted per S2b window (120 s):** 0 rebuilds with a populated airports list. The zoom lands before measure (RP5's
ZOOM-T, at least 3 s), and a populated list refreshes every 24 h.
- **The named exception:** the sibling is at step C's `3e9d6ec9f449`, which does NOT carry step C's airports addition
  (`c96bd0b04182`). So an EMPTY list still bumps every 5 min there, which is at most 1 rebuild per window.
- **Outside the set:** more than 1 tagged rebuild in a window, or TAG-AGREE off. That window's X is printed PARTIAL and
  goes to the orchestrator before X is used.

**What it touches:**
- the sibling only, so S2b's image;
- FT-HR runs its accepted `ft2-hr` (`054e60e67bd4`), which the manifest's sha check guards, so a moved sibling cannot
  reach it;
- the FT reader's STEADY masks name bits 4, 8, 16 and 32 only.

| Q | the question | the default I take unless ruled |
|---|---|---|
| Q-S2b-T1 | the bit | 64 (bit 6) |
| Q-S2b-T2 | in or out of the bar's p95 | IN, with the p95 without rebuild frames printed beside it, ungraded |
| Q-S2b-T3 | the record's part outside FT | `settle` / `measure` from `measuring` |
| Q-S2b-T4 | when the code lands | on the sibling, before S2b's image is built (Monday), never in FT-HR's image |

**ACCEPTED at `372bf9028345`** (the orchestrator, 2026-11-15), with T1-T4 at their defaults.

#### RE-REGISTERED as (B) before any code (2026-11-15, ~17:38Z; ACCEPTED by the orchestrator, recorded 17:40:45Z)

**The off reading, in this section's own text.** "`Record` ORs 64 ... and clears it" puts the tag inside
`framebench::Record()`. On the sibling (`6f0cdd1381eb`), FB-REC pins that text: struct `Rec`, `S()`, `Mux()`, `Ten()`,
`Record()` and `SelfTime()`, hashed to `425040a3999d`, which is `0d6308cb`'s. FB-REC's own rule is that a change
re-registers the I-5 band from a proof window. So the code as registered turned FB-REC red by construction, and it
would have cost a board window before S2b. The registration cited `Record`'s lines (`:379`, `:386`) and did not name
FB-REC. It was found before any code was written.

**(B), the accepted form.** The tag enters through the call site, as FT-DREW's bit 32 does, so `Record()` stays
byte-identical:
- `State` gains `bool mapRebuilt`. `State` is outside `S()`'s span, so FB-REC's hash does not move.
- `OnRebuild` sets it AFTER its `scene < 0 || done` return. So a tag exists exactly when a rebuild record prints.
- A new `TakeMapRebuilt()` returns 64 when the tag is set, and clears it.
- In `RecordFrameUs`'s region, the one `framebench::Record(` call passes `stateFlags | mapTag`, with `mapTag` taken on
  the line before it. The `stateFlags` line, which is FT-DREW's anchor, is untouched.
- Outside `FRAME_BENCH_FT`, the record's part reads `measuring ? "measure" : "settle"`. FT's three parts are unchanged.

**TAG-SET, restated with four clauses:**
- `mapRebuilt = true` appears exactly once in `src/` and `include/`, inside `OnRebuild`, after its early return;
- `TakeMapRebuilt`'s body reads the tag as 64 and clears it, and nothing else;
- its result enters the one `framebench::Record(` call, taken on the line before it, in a region;
- there is no other 64 flag write in the bench's files.

Its plants, each reading exactly {TAG-SET}:
- P-TAG-EVERY: the take returns 64 unconditionally;
- P-TAG-NOCLEAR: the clear removed;
- P-TAG-NOPASS (new, ACCEPTED): the call passes `stateFlags` alone.

**PREDICTED, frozen before the code ran:**
- `check_frame_bench.py` at the new sibling commit reads 26 rows (25 before), 0 failed, with FB-REC still
  `425040a3999d`.
- `--selftest` reads 39 plants (36 + 3), each red set as named, and "selftest PASSED".
- `scripts/draw_pin.py` at the new commit reads UNCHANGED, exit 0. Every edit is in `FrameBench.h` or inside a FRAME
  BENCH region, and neither is a pinned part.
- **The escape hatch:** any other row red, any plant off, or draw_pin exit 1 or 2 means stop and report.
- The device compile is not claimed until S2b's image build (Monday).
- **Timing:** the code is written during the owner's 17:45-20:50Z quiet window and committed locally. The runs (Windows
  Python counts as a run) come after the orchestrator relays the window's end, then the push, then a report before
  S2b's image is built.
- **FT-HR is unaffected:** its `ft2-hr` image (`054e60e67bd4`) is prebuilt and sha-guarded. An FT image rebuilt from
  the new commit would carry 64 on a steady-window rebuild frame, which FT's own rule expects 0 of. The frozen FT
  reader's STEADY masks name 4, 8, 16 and 32. It is never run on such an image.

**RESULT, as predicted (run 2026-11-15T19:47-19:51Z, after the orchestrator relayed the window's end at 19:47:05Z).** On
the sibling at `8fb80fef41d1`, clean before and after:
- `check_frame_bench.py` read 26 rows, 0 failed. FB-REC read `425040a3999d` against `0d6308cb`'s. TAG-SET read "set 1
  time(s), in OnRebuild after its return True; take defined 1 time(s), body exact True; Record calls 1, takes 1, the
  call passes the take True; 64 flag writes 1, only the take's True". Exit 0.
- `--selftest` read 39 plants, each red on exactly its own row, P-TAG-EVERY, P-TAG-NOCLEAR and P-TAG-NOPASS among
  them, then "selftest PASSED". Exit 0.
- `scripts/draw_pin.py --rev 8fb80fef41d1` read pin `21fc376c8a2a`, "UNCHANGED against 3e9d6ec9f449". Exit 0.

The standing rule's runs came next (every sabotage driver that plants in a changed file, plus the `--anchors` pass). Their
predictions were frozen at 19:49:56Z, and they ran in WSL, g++ 13.3:
- `--anchors` read 42 drivers, every anchor present exactly once. Its control turned red with a mangled anchor.
- `sabotage_map_draw.py`: 18 sabotages and 3 controls, AS PREDICTED.
- `sabotage_map_fetch.py`: 23 sabotages, AS PREDICTED.
- `sabotage_wx_outline.py`: 2 sabotages, AS PREDICTED.

The sibling was pushed at `8fb80fef41d1` (from `6f0cdd1381eb`), through the pre-push guard chain, every guard ok. The
device compile is still not claimed; it waits for S2b's image build.

## S2b's registration: RP5's run on the v20 sibling (DRAFT, before any of its code, 2026-11-16T04:59:55Z)

**What S2b is.** RP5's 18 scenes, on RP5's two pinned recordings, run on the v20 sibling's image (B, the push task) at
**v20's own customer defaults**. It gives v20's bar number X (the worst p95(p2p) of S1-S6), read against the absolute
50 ms bar exactly as RP5 reads v19's. This is "identical ground": the same scenes, the same recordings, each image at
its own defaults (RP5's "For v20" bullet, above). The map-rebuild tag section above is one piece of this registration;
everything else is here.

**What the sibling lacks (read from its tree at `8fb80fef41d1`, 2026-11-16 ~04:52Z).** Its `include/FrameBench.h`
descends from C1's `bfcb47e0`, not from RP5's `a37e99b0`. Neither `a37e99b0` nor the replay (`41b317b6`) is an
ancestor of the sibling. The two headers share the scene driver, `Override`, `OnCloudPoll`, `Record`, `PumpDump`,
`SelfTime`, `Allocate`, `PrepareScene`, `EndMeasure`, `Present` and `DelayOn`. On the sibling, though, `OnCloudPoll`
serves C1's and FT's synthetic table, not a recording. Missing, each with the RP5 commit it comes from:

| piece | RP5 commit | on the sibling |
|---|---|---|
| P1 the recordings: `framebench_rec_gen.h`, generated at build time by `scripts/framebench_rec.py` from the pinned files, never tracked (FB-R3) | `41b317b6`, `be73655e` | absent |
| P2 the REPLAY serve: the live GET made and parsed as shipped, then the recorded body at the same elapsed time since the settle, the CRC on the device, `served A<i> <seq> crc … us …` | `41b317b6`, `f7d2acf8` (F1: in the settle too) | absent; the hook exists with another signature |
| P3 the 18-scene table, its five kinds (CTRL-IL, REPLAY, FIXED, LIVE-APT, LIVE-OWN), `SettleMs`/`MeasureMs` per kind, `HomeName` | `41b317b6`, `f7d2acf8` (F2, F3) | absent: the tables are C1's and FT's |
| P4 (Z): a REPLAY scene configures the shipping radius and zooms the ladder to its rung with `ApplyZoom(.., "bench")` | `d96e29db` | absent |
| P5 the customer defaults: `CustomerDefault`, `Override` answering "" for every unset key, the KEEP snapshot (`SnapshotKeep`) at the first `Initialise()`'s entry | `b417d80c` | `Override` exists (C1's); the rest absent |
| P6 the record lines: `keep defaults: …` once, `drawing S<k>: …` after each scene's `Initialise()`, `enrich S<k>: submits …` at each scene's end, and the submits counter in `RequestCloudEnrich` | `b417d80c` | absent (the drawn counter exists, `AircraftManager.cpp:4900`) |
| P7 CTRL-IL's plant in alternate 10 s blocks of the measure | `f7d2acf8` (F3) | the plant exists as C1's (the moved loop plant, before `HandOver`); the alternation is absent |
| P8 the local feed for CTRL-IL and the packed rows (`FRAME_BENCH_HOST`, `fixed_traffic.py` on the bench PC) | `41b317b6` | absent |

**THE PORT (registered before its code).**
- **Where:** new commits on `bench/frame-bench-v20` itself, so S2b runs the same tree FT and the proof window ran. Every
  S2b piece sits under a new macro, `FRAME_BENCH_S2B`, set only by a new env, `blipscope-s3-128-framebench-v20-s2b`.
  The C1, FT and proof envs never define it.
- **The isolation proof (S2B-ISO), before the image:** each existing bench env's image built from the port's tip is
  byte-identical to the same env built from `8fb80fef41d1`, apart from the five registered regions (`app_elf_sha256`,
  the image SHA-256, the checksum, WiFiManager's two build-time strings), by `bindiff_elfhash.py`, "elsewhere 0". The
  envs: `framebench-v20-ftsea`, `framebench-v20-fthr`, `framebench-c1b-proof`. Each pair is two large builds, so this
  runs only with C: over 30 GiB (read twice, 60 s apart).
- **Each piece is RP5's text where v20's code allows it**, and each place it cannot be is named in the commit and here:
  - P2: the sibling's `OnCloudPoll(doc, rows, dataEpoch, parse)` gains RP5's two sent-coordinate arguments, which
    only the REPLAY branch compares (FB-R9's text);
  - P5: the KEEP list is the same six on v20. Read at `8fb80fef41d1`, `AircraftManager.cpp:1085` and `:1100-:1104`
    are the six `if (!x.isEmpty()) display… = …` parses, the same parse as v19's `:891`, `:901-:905`;
  - P7: the plant is the PACKAGE's moved loop plant, read IN at true size (+10.00 on p2p, push −0.02); RP5's
    alternate-block control is moved onto it;
  - P8: `fixed_traffic.py` `b530faf4df97` and the image's `FRAME_BENCH_HOST` (a boolean in every log), exactly as S2's.
- **`check_frame_bench.py` gains RP5's rows** for P1-P8 (FB-R3, FB-R9, FB-R10, FB-R11, each with its plant), scoped to
  `FRAME_BENCH_S2B`. Every existing row stays green, FB-REC included (`425040a3999d`: no piece touches `Record()` or
  its callees).
- **draw_pin.py at the port's tip reads UNCHANGED** (every piece sits in a FRAME BENCH region or in `FrameBench.h`).

**THE RUN:** RP5's table unchanged (S0 CTRL-IL, S1-S8 REPLAY at KSFO and LAX, S9-S14 FIXED, S15-S16 LIVE-APT, S17
LIVE-OWN), 76.5 min, `--choice 5` (LAX).

**v20's defaults, read from its parses (not transcribed), with the differences from v19 printed beside them:**
- **The weather layer:** absent means ON (`wxlayerswitch::On`, `:1115`), and every bench env builds with
  `WX_LAYER_SHIPS`. v19's replay ran with it OFF.
- **The map layer:** on wherever `MapEnabled()` holds (`:3790`: the cloud source, a cloud URL, a location, no
  allocation failure).
  - So **REPLAY, LIVE-APT and LIVE-OWN draw the map and the weather as shipped.**
  - **CTRL-IL and the packed rows draw NEITHER,** because they read the local feed, where `useCloudSource` is false.
    That is named, and it is the same in kind as RP5's packed rows, which already ran at v19's defaults and were
    reported, not graded.
- **The weather picture is live**, for each scene's home at the run's hour, and it is not recorded. Its state per scene
  is printed: the crop flag (bit 2), the weather-rebuilt frames (bit 0), and the weather showing.
- Everything else as RP5 lists it, read from the same parses.

**THE READER, `fb_read_s2b.py`:** `fb_read_rp5.py` `e15a94a8a8f6` with these changes, and no others:
- **BOOT:** `[build] env=blipscope-s3-128-framebench-v20-s2b fw=v19` (the sibling's `FW_VERSION` is still 19), and the
  18-scene line.
- **THE TRANSCRIPTIONS:** the same 24 pieces, re-located and re-hashed in the S2b commit's own source. Where a piece's
  v20 text differs from v19's, the reader transcribes v20's. A v19 copy never grades v20 (RP5's own rule).
- **I-5:** the sibling's corrected limits for image B (6,400 to 7,300 ns, two-sided; the purpose STOP over 50,000 ns),
  in place of RP5's ≤ 5,000.
- **M3-2:** bits 2 and 3 (no present, two presents) are on under 1 % of each scene's measured frames, else that scene is
  the INSTRUMENT.
- **THE REBUILD FRAMES (R-r).** RP5's "rebuild-tagged frames 0" holds on v19, where the weather was off and the map did
  not exist. On v20 both rebuild:
  - **map (bit 64):** the tag rows above (TAG-AGREE, the 0-1 per window prediction, the PARTIAL rule, IN the p95, with
    the p95 without rebuild frames printed beside it);
  - **weather (bit 0):** the same treatment, by Q-S2b-W below. Counted per scene, IN the p95, with the p95 without
    them printed beside it;
  - **the attribution on image B:** a rebuild's frames are the tagged record and the next one.
- **XA:** v20's LIVE-APT residuals from v20's own replay line, by RP5's rule, read against RP5's interval m ± 4.54
  frozen from S2. **This needs S2's run, Monday,** so S2b's XA numbers are frozen after S2 and before S2b's window.
- **XCHK:** by Q-S2b-X below.
- **X FINAL:** RP5's conditions, plus no M3-2 INSTRUMENT and TAG-AGREE in for S1-S6.
- **Its selftest:** RP5's 28 cases, re-pointed at the S2b commit, plus the tag section's three cases and an M3-2 case
  (over 1 % reads the INSTRUMENT). Each case gives its exit code and red set, and prints its reason's own words.

**PREDICTED (frozen now; v20's numbers that need S2's data are named as such):**
- check_frame_bench at the port's tip: every existing row green, plus RP5's rows with their plants; FB-REC
  `425040a3999d`.
- S2B-ISO: three pairs, "elsewhere 0".
- draw_pin: UNCHANGED, exit 0.
- **X̂ for v20: OVER the bar.** The basis: FT-SEA's steady windows on this same image read p95 73-75 ms at about 14 fps
  with 40 aircraft and the map on (`ft_abs.py`, `d989b1b1d70c`), and S2b adds the weather. The centre and band are
  frozen by rule after S2: **X̂_v20 = S2's X − 9.5** (C1's M3 cut, [7.92, 11.00]) **+ the map term + the weather term**,
  each term written out from a measurement before S2b's window. If a term has no measurement, the band widens by its
  stated bound rather than the term being guessed.
- **The escape hatch:** any reading outside these, a piece whose port changes `Record()`, or an S2B-ISO byte outside the
  five regions: stop, report, and nothing is fixed in place.

**What waits for S2 (Monday 18:15-19:55Z):** RP5's measured X and XA's m. S2b's XA interval, X̂'s centre, and the
cross-check of the transcriptions' v19-to-v20 differences against a real v19 run.

**Questions, each with the default I take unless ruled:**

| Q | the question | the default |
|---|---|---|
| Q-S2b-P | where the port lives | on `bench/frame-bench-v20`, under `FRAME_BENCH_S2B`, proven isolated by S2B-ISO |
| Q-S2b-W | weather rebuild frames (bit 0) in the p95 | IN, counted, with the p95 without them printed beside it, as for the map |
| Q-S2b-L | the live weather picture | as shipped, live, and not recorded; its per-scene state printed |
| Q-S2b-X | XCHK, which needs a release image of the same version | dropped for S2b and named: no v20 release exists. LIVE-OWN is reported against S2's LIVE-OWN, labelled "between sessions" |
| Q-S2b-A | the A/B/A rule (any v19-v20 delta needs v19 read again in the same window) | no graded cross-image delta in S2b. X against the absolute bar needs no A/B/A. v19's X from S2 is printed beside it, labelled "between sessions; E = max(spread, U95) not measured here". A graded delta would need a v19 replay in S2b's window, about 78 more minutes |
| Q-S2b-C | CTRL-IL's map and weather | off, by the local feed's construction (above). The control's +10 is the plant's, so its scene's draw content does not change what it controls for |
| Q-S2b-D | the date | S2b's window opens only after: the port's commits pass, S2B-ISO, the image and its manifest are acknowledged, and S2's numbers are frozen into this section. The target date is the orchestrator's to set |

**ACCEPTED as the plan at `092868e2e24c`** (the orchestrator, recorded 2026-11-16T05:03:50Z), with all seven defaults.
- **D, the slot:** already booked, **Tue 2026-11-17 17:50-19:55Z** (moved there earlier for daylight at its test homes),
  after FT-HR's 12:54Z start. XA and X̂ freeze after S2 (Mon about 19:50Z).
- **S2B-ISO's disk rule:**
  - read C: before each build pair, and start one only at 31.0 GiB or more;
  - after each pair's comparison, keep only its hashes and diff results, and delete that pair's build output (my
    own rebuildable) before the next;
  - never leave C: under 30.6 GiB between 09:30 and 13:00Z (5c's A8 bar at 10:00Z). A pair that would breach that
    runs after 13:00Z.

**AMENDMENT to P2, before any code (2026-11-16T05:05Z), because the accepted text contradicts its own S2B-ISO.**
- "The sibling's `OnCloudPoll` gains RP5's two sent-coordinate arguments" changes a call site that every bench env
  compiles. The FT and C1 images would then differ: the call builds two `String`s from `wireloc::Center`, whether or
  not the callee reads them. S2B-ISO exists to catch exactly that, so the plan as written fails its own proof.
- **The rule instead, for every piece:** no shared function's signature or body changes. At each FRAME BENCH call
  site an S2B piece needs, the region gains `#if defined(FRAME_BENCH_S2B)` with S2b's own call, and `#else` with the
  existing text byte for byte. A non-S2B env then compiles the same preprocessed text as at `8fb80fef41d1`, which is
  what S2B-ISO proves from the images.
- So P2 is a new `OnCloudPollS2b(...)`, carrying RP5's signature and its REPLAY branch, called only under
  `FRAME_BENCH_S2B`.
- **check_frame_bench's counting rows are kept at their counts in every env.** S2b reuses the one `Record` call
  (TAG-SET, FB-REC), the one moved loop plant (M3-L), and the one push plant site (M3-3P), and never adds a second.
  Where a row counts a call that S2b's `#if` duplicates in text, the row is scoped to the non-S2B branch, and that
  scoping is named in the row's commit.

#### THE PORT, as committed and pushed (2026-11-16, `bench/frame-bench-v20` `8fb80fef41d1` -> `7d4009fdbc92`)

- **`43663c6cda26`, P1-P8:** FrameBenchS2b.h (RP5's table and constants), the S2b branches in FrameBench.h and in
  AircraftManager.cpp's regions, the env, and `framebench_host.py` and `framebench_rec.py` copied from `b417d80cd4c5`.
  - No row needed scoping: each counting row kept its count. At the cloud-poll site the existing call stays first, in
    `#if !defined(FRAME_BENCH_S2B)`, and S2b's call goes in its `#else`, so C1-A's eight-line rule holds unchanged.
  - **The static set:** check_frame_bench 26 rows, 0 failed, FB-REC `425040a3999d`; the selftest 39 plants, PASSED;
    draw_pin UNCHANGED, exit 0.
  - **The first device compile** (env `blipscope-s3-128-framebench-v20-s2b`, the pinned recordings, REC_B 5, this PC's
    host; the clean child): pio exit 0, firmware.bin 2,106,944 B, `762564fb1650`. The ELF's discriminators read as
    predicted:
    - S2b's seven strings present (the announce, `served A`, `keep defaults`, `drawing`, `enrich`, `override cost`,
      `record cost`);
    - C1's `table served`, `wx served` and C1 announce absent;
    - the host carried, and in no log.
  - **The build's guard threw "the build dirtied the tree".** The alternative, the build writing into the tree, is
    excluded. `git status` showed only `test/host/check_frame_bench.py`, which I was editing during the build (mtime
    inside it), and the build never writes it. `src/` and `include/` were unchanged throughout.
  - The build output was 0.160 GiB; C: read 31.12 GiB before and 31.34 after its deletion.
- **`7d4009fdbc92`, the check rows:** RP5's FB-R3 to FB-R11, re-pointed at FrameBenchS2b.h and OnCloudPollS2b, plus
  **S2B-SCOPE.** S2B-SCOPE requires every line naming an S2b-only identifier to lie inside an S2b preprocessor branch:
  the text-level half of S2B-ISO.
  - 36 rows, 0 failed; the 26 earlier rows byte-identical to the 05:16Z run.
  - The selftest: 53 plants (39 + 14), each reading exactly its own row red; FB-R3's control reads none.
- **The standing rule's drivers** (WSL, g++ 13.3): `--anchors` 42 drivers, every anchor exactly once, its control red
  when mangled; `sabotage_map_draw` 18 + 3, `sabotage_map_fetch` 23, `sabotage_wx_outline` 2, all AS PREDICTED.
- **The push:** prepush_frame, every guard ok (`skus --validate` included, with the new env).
- **Next:** the reader `fb_read_s2b.py` (registered as a delta from `e15a94a8a8f6`), then S2B-ISO's three build pairs.

#### THE READER's AMENDMENTS, written before its code (R1-R3 accepted by the orchestrator, 2026-11-16 ~05:37Z)

The registration's reader is `fb_read_rp5.py` `e15a94a8a8f6` with its listed changes. Reading the sibling's own text
changed three of them, and two readings are recorded beside them.

- **(R1) I-5 reads two lines.** The S2b image prints `record cost N ns/frame` (SelfTime, FB-REC's pinned text) and
  `override cost N ns/call` (SelfTimeOverride) as two lines. Reader b's COST regex reads only RP5's one-line form, so
  its `parse()` returns no cost for this image. `fb_read_s2b` parses both lines itself:
  - each exactly once, before S0's settle line, else the INSTRUMENT;
  - the record cost graded on image B's band, 6,400 to 7,300 ns (two-sided; outside it is a graded row off, exit 1),
    with over 50,000 ns a STOP;
  - the override cost reported.
- **(R2) M3-2 reads `pres`, not flag bits.** On the sibling, `flags` bit 2 is the crop and bit 3 the map. The presents
  since the previous record are the Rec's 8th byte, `pres` (reader b's `<HHBBBB` unpack keeps it as its 6th field).
  The draft's "bits 2 and 3" is withdrawn. Instead:
  - **per frame:** `pres != 1` on under 1 % of each scene's measured frames, else that scene is the INSTRUMENT;
  - **per scene (THE PACKAGE's exact count):** its `presents S<k>: handed H presented P dropped D` line once, with
    P == H and D == 0, else the INSTRUMENT.
  - **Predicted:** 0 frames with `pres != 1` in every scene. No S2b scene has a push delay, and THE PACKAGE's proof
    window read 0 in every scene without one.
- **(R3) CTRL-IL's drop rule is unchanged in code** (the first frame of every run after the first is dropped). Its basis
  on image B is restated:
  - **The order on the loop task (B):** frame k draws, the plant runs (DelayOn, before HandOver), HandOver(k), then
    RecordFrameUs and Record(k), which reads the latest p2p that Present published.
  - **Present runs on the push task, after a frame's DMA completes.** Frame k-1 is handed over at H(k-1), and its push
    takes about 24 ms (push − sync 23.7 ms, against a loop period of about 70-80 ms). So its present, P(k-1), lands
    before H(k). Frame k's own present P(k) is still pending when Record(k) runs, since Record follows HandOver at once.
  - **So Record(k) carries P(k-1) − P(k-2), which is about H(k-1) − H(k-2): frame k-1's period,** with frame k-1's
    plant or its absence. Its `delayed` flag is frame k's (DelayOn runs in the same iteration). At a run boundary, the
    first frame of the new run carries the previous run's period. That one frame is dropped, exactly RP5's rule,
    for a different reason.
  - **The alternatives, named and excluded.**
    - Lag 0 (the push completes before the record): excluded by the order, since Record follows HandOver at once and
      the push needs about 24 ms.
    - Lag 2 (frame k-1's push not done by H(k)): excluded while the push is shorter than the period. Where it
      happened, that frame would read `pres = 0`, which (R2) counts.
  - **Its guard is (R2) itself:** `pres == 1`, exactly one present between consecutive records, is the lag-1
    condition.
- **(R4) THE TRANSCRIPTIONS ARE UNCHANGED** (read 2026-11-16, before the reader's code).
  - All 24 pieces' texts at `7d4009fdbc92` are byte-identical to `b417d80cd4c5`'s.
  - 22 are at their own paths, by the reader's own `--emit-hashes` against both commits.
  - AptLat and AptLon are at their new path, `include/FrameBenchS2b.h`, by the reader's own `extract()`, with the same
    hashes.
  - So `fb_read_s2b` keeps all 24 EXPECTED hashes and moves only those two paths. Its transcription code is RP5's
    unchanged: v20's prediction, projection, zoom and row-parse text is v19's.
- **(R5) THE [health] LINE IS UNCHANGED.** Reader b's HEALTH and RP5's HINT match the sibling image's frame-health
  lines: on FT-SEA's trace (the sibling's B image), 289 of 289 frame lines, all `interval=5000`. The control is the
  11-12 v19 baseline, which reads the same shape (183 frame lines among 488 `[health]` lines). So CADENCE and the
  windows read unchanged.

**THE FINDING BEHIND R6 AND R7: on image B, p2p sits on a 1 ms grid.** Read offline from the sibling's own proof window
(`cap10/trace-v20-P`, image B), by reader b's own `parse()`, before any S2b reader code:

| scene (proof window, image B) | p2p a whole ms | most common p2p (ms) | p95(p2p) − p95(fr) |
|---|---|---|---|
| S0 C1 plain | 48.0 % | 70.00, 69.99, 71.00, 70.99 | +0.19 |
| S1 C1+push10 | 68.0 % | 70.00, 71.00, 69.99 | −0.16 |
| S2 C1+push60 | 91.6 % | 84.00, 83.99 | −2.25 (`pres != 1` on 2.24 %: the excluded scope) |
| S3 C1+loop10 | 48.5 % | 80.00, 79.99, 81.00 | +0.16 |
| S4 C1+push60drop | 92.4 % | 84.00, 83.99 | −2.02 (`pres != 1` on 2.17 %) |

- **The control is image A,** the 11-12 v19 baseline: 0.9 to 1.6 % of p2p values are a whole ms in each of its first six
  scenes, as a `micros()` difference should be.
- **The values that are not a whole ms sit one 10 µs unit below one** (69.99, 70.99). So every p95(p2p) on B reads a
  whole millisecond: 73.00, 84.00, 83.00 and 84.00 in that window.
- **The likely mechanism, named and not relied on:** the push task presents on the 1 ms tick. No row below depends
  on it. (The orchestrator passed it to p4 as a design observation for M3, not as a ruling on this bench.)

**(R6) FIX-g ON IMAGE B: [−1.0, +2.0] ms** (accepted 2026-11-16 ~05:48Z; was [0, +2.0], derived on image A).
- On B a whole-millisecond p95(p2p) is read against a continuous p95(fr), so their difference jitters by up to one
  grid step from quantization alone. B's own scenes without a 60 ms push read −0.16 to +0.19.
- The band moves its lower edge down by exactly one grid step (1 ms). The upper edge is unchanged.
- This is a calibration from a proof-window trace, NOT from S2b's graded run, so it is not a re-fit.
- **The alternative, named:** report FIX-g on B without grading it. Not taken: FIX-g is the instrument's consistency
  check (p2p ≈ fr in lockstep), and that holds on B to within the grid.

**(R7) R-b's CENTRE AND X-b's X̂ COME FROM S2** (accepted 2026-11-16 ~05:48Z). v19's line (35.80 + 0.802 n − 2.91) is a
model of v19, not of v20.
- **R-b's per-scene centre** = S2's measured p95(p2p) of the SAME scene − 9.5 (C1's answer) + the map term + the
  weather term, band ± 10 ms. **X̂** = the largest of the six centres (the accepted X̂ rule, applied per scene).
- **The inputs** are S2's reader output: S2's per-scene p95(p2p), its X, its XA `m` per home and its LIVE-OWN windows.
  They enter as one reference file, pinned by sha256 after Monday's window and frozen into this section before S2b's.
  A term with no measurement widens the band by its stated bound, written into the same file.
- **The live residuals** print against v20's own replay line (XA's), reported only, never against v19's.
- **IL-a stays as registered.** Its pooled Δp50 band [9.5, 10.5] admits only 10.00 on a whole-ms grid. The proof
  window's plain and loop10 scenes sit at modes 70 and 80 (+10.00), and THE PACKAGE read +10.00, so IL-a is expected to
  hold. The read prints the off and on p50 modes beside IL-a, so a grid flip shows as exactly one step. Outcome C still
  goes to the owner.

#### THE READER, `fb_read_s2b.py`, written to R1-R7 and frozen by sha (2026-11-16T05:54:08Z)

- **`fb_read_s2b.py` `7b673c32b8c3618988bde031bfabe32cfac10505223f60f5ea62fd7d295e7cb0`.** It is a byte copy of
  `fb_read_rp5.py` `e15a94a8a8f6`, edited only for the registered changes. The diff is 274 changed lines, all of them in:
  - the docstring;
  - BOOT's banner;
  - R1's two cost lines and image B's band;
  - R2's `pres` share and presents count;
  - the tag rows (TAG-AGREE, at most 1 rebuild, weather counted, the p95 without rebuild frames);
  - R6's FIX-g band;
  - R7's S2 reference (R-b, X̂, XA's interval, LIVE-OWN reported in place of XCHK);
  - the two moved transcription paths;
  - the selftest's trace and its synthetic S2 reference.
- **Reader b is imported,** sha `b0dbd4ebc163` asserted, never copied. The 24 EXPECTED hashes are RP5's, unchanged (R4).
- **The S2 reference** enters by `--s2 <file> <sha256>`, refused on a mismatch.
- **Its selftest,** `--selftest <repo> 7d4009fdbc92`: **36 cases, PASSED** on its first run (output
  `4c89e147413e4f4115b2fbf16d45a1f66d5dbf30c18663976353be523f08f378`; predictions frozen 05:51:00Z).
  - RP5's 28 cases without XCHK (27), each giving its exit code, red set and its own reason's words as in RP5's table.
  - Nine of S2b's own:
    - COSTSTOP exit 2; NOCOST exit 2;
    - PRES exit 2 (3.3 % `pres != 1`); PRESCOUNT exit 2;
    - TAGDIS exit 2 (a tag without its rebuild record); TAG2 exit 2 STOP, X PARTIAL;
    - FIXG exit 1; S2MISS exit 2; XAREF exit 1.
  - COST now reads exit 1 at 8,000 ns (outside image B's band).
  - The clean case reads "map rebuilds 1 (tagged 1, TAG-AGREE)", "weather-rebuilt frames 2" and "pres != 1 under 1 %".
- **Proved able to fail** (`reader_reversal.py`, output `2b416b580c04c5189af8d84f4b6d1282fca1aa0ecb5ce07ee2dc30295be28191`).
  Each plant loosens one rule in a temporary copy, and that copy's selftest FAILS on exactly its own case:
  - FIX-g's lower edge at −2.0 fails exactly FIXG;
  - a 5 % `pres` share fails exactly PRES;
  - TAG-AGREE removed fails exactly TAGDIS.

### THE POST-v20 QUEUE: the ~200 ms full redraw (the owner's answer, 2026-11-15; queued, nothing before v20)

- **The owner, verbatim:** "Sea: a. Queue the faster water fill for after v20, and with it, look at the ~200 ms full
  redraw itself: that's a visible pause on every zoom, sea or not."
- **The entry:** after v20, look at the full map redraw as a visible pause on every zoom step, sea or not. FT-SEA's median
  forced rebuild was 185-262 ms (`ft_abs.py` output `16a911d0d279`). The faster water fill is queued with it. No analysis
  and no code before v20.
- **It starts from a measured breakdown, not a new run.** FT-SEA's trace (`f5df38b36bc5`, cap11) holds **682** rebuild
  records, each complete: 62 settle, 600 forced and 20 forced planted (a count-only read, 2026-11-15T16:03Z). Every record
  carries:
  - the four µs laps: Load, Build, swap + names, and Compose;
  - BuildInfo's six pass times (`bi=` usS1, usWater, usRivers, usRoads, usColour and usTotal);
  - the layer counts, the CPU MHz and the millis line.
- Its labels: measured on step C at 3e9d6ec9f449, not on main; weather OFF (FT-MAP); level 40.

### DRAW-PIN: the re-read rule's "differs in the draw path" as a check (registered before its code, 2026-11-15T16:10Z)

**Why (the orchestrator's ruling).** The re-read rule above was registered as "a textual pin, as FB-REC is", and no
script was ever behind it. So "differs in the draw path" was answered by reading. This makes it a check's output.

**The script:** `scripts/draw_pin.py` on this branch, frozen by sha once its selftest passes. It reads files from a
revision (`git show <rev>:<path>`) or from a directory.

**The pinned parts**, each normalised CRLF → LF with every `// THE FRAME BENCH BEGIN` … `// THE FRAME BENCH END`
region removed:
1. the four headers, whole: `include/MapCompose.h`, `include/MapPlaces.h`, `include/MapLayer.h`, `include/MapSweep.h`;
2. in `src/AircraftManager.cpp`, the bodies, by signature, from the signature line to its matching brace (string, char
   and comment text skipped while counting): `DrawWeatherUnder(`, `DrawMapBase(`, `MapAirportBoxes(`, `DrawSweepOverMap(`;
3. the gate line: the one line holding `if (mapFrameDrawn) return DrawSweepOverMap(`.

**The pin** is the sha256 over the parts' names and sha256s, in that order. The script prints every part's sha (12) and
the pin (64), and compares the pin with the registered value, which is computed at `3e9d6ec9f449`.

**Out of scope, named:**
- `include/MapAirportsKey.h`. It decides whether a rebuild happens, not its cost or its pixels, and S2b's tag row
  measures that.
- Everything else in AircraftManager.cpp: `ConsumeFetchResult`, `Initialise` and the scene driver.

**It is textual and conservative:** a comment-only edit inside a pinned part reads CHANGED. That is a false alarm, never
a miss.

**Exits:** 0 UNCHANGED; 1 CHANGED, naming the parts; 2 the rig; 3 no registered value yet. The rig is a signature found
other than once, a gate line found other than once, a FRAME BENCH region unbalanced or nested, or an unreadable file.

**Predicted:**

| what is read | predicted |
|---|---|
| `3e9d6ec9f449` | the pin is computed and stated, and becomes the registered value |
| `c96bd0b04182` (step C with the airports addition) | **UNCHANGED** |
| the sibling `6f0cdd1381eb` | **UNCHANGED**. Its FT code sits in FRAME BENCH regions, and M3 (the push task) is outside these parts. Off: STOP. It would mean FT's label "on step C at 3e9d6ec9f449" does not describe the draw path FT ran |

**The selftest's plants.** Each is graded against ITS OWN unplanted tree, so a plant's effect is isolated from the tree's:

| plant | tree | the edit | predicted |
|---|---|---|---|
| P-IN | `c96bd0b04182` | one line inside DrawMapBase's body, outside any region (`k.airportsGen = mapAirportsGen;` → `... + 0;`) | CHANGED, exactly {DrawMapBase} |
| P-BENCH | `6f0cdd1381eb` | one line inside DrawMapBase's FRAME BENCH region (`rr.cpuMhz = getCpuFrequencyMhz();` → `rr.cpuMhz = 0;`) | UNCHANGED |
| P-OUT | `c96bd0b04182` | one line in ConsumeFetchResult (the `[cloud] airports:` print's text) | UNCHANGED |
| P-HDR | `c96bd0b04182` | one line in `include/MapSweep.h` | CHANGED, exactly {MapSweep.h} |
| P-GATE | `c96bd0b04182` | the gate line's comment | CHANGED, exactly {gate} |
| P-RIG-SIG | `c96bd0b04182` | DrawMapBase's signature renamed | exit 2 |
| P-RIG-REGION | `6f0cdd1381eb` | one FRAME BENCH END in DrawMapBase removed | exit 2 |

The controls the ruling asked for are P-IN, P-BENCH and P-OUT. P-HDR, P-GATE and the two rigs cover the other parts and
the script's own refusals.

**Then:** the re-read rule points at `scripts/draw_pin.py` and its sha, and "differs in the draw path" means its exit 1.
**The escape hatch:** any other reading, on any row: STOP, report, and no fix until a ruling.

**AS RUN (2026-11-15, 16:06-16:08Z): every row as predicted.** The script, frozen:
`ba03b003b609f4c4ff39d925b64140a39519968416c8ecc3637b6218ba98ff9c`.
- **The registered value, computed at `3e9d6ec9f449`:** pin
  `21fc376c8a2a57b86cf5dbbab724b01579241590e036b2053884da919aed61e4`. The parts are:

  | part | sha (12) |
  |---|---|
  | MapCompose.h | `8f1cefd40a53` |
  | MapPlaces.h | `7c40624960a2` |
  | MapLayer.h | `68e82c2bfcdc` |
  | MapSweep.h | `fc69563a1eea` |
  | DrawWeatherUnder | `5fb58241dde6` |
  | DrawMapBase | `3854ed90252f` |
  | MapAirportBoxes | `2b6307d96887` |
  | DrawSweepOverMap | `2fd410c5b0b6` |
  | gate | `443dac1dfda5` |

  - Its first run exited 3, "no registered value yet", as designed (`5a81736bf661`).
  - The full shas were read from the script's own `parts_of` and written into it. They were not transcribed.
- **The revision reads, each exit 0, UNCHANGED, with the same pin:** `3e9d6ec9f449`, `c96bd0b04182` (step C with the
  airports addition) and the sibling `6f0cdd1381eb`. The three outputs are byte-identical (`a86c85379822`).
  - So **the sibling's draw path equals step C's at `3e9d6ec9f449`** once the FRAME BENCH regions are removed. FT's label
    "measured on step C at 3e9d6ec9f449" describes the draw path FT ran.
  - So **step C merging with the airports addition does not trigger the re-read.**
- **The selftest: 7 plants AS PREDICTED** (`4dfc10ceee7b`):
  - P-IN: CHANGED {DrawMapBase};
  - P-BENCH: UNCHANGED;
  - P-OUT: UNCHANGED;
  - P-HDR: CHANGED {MapSweep.h};
  - P-GATE: CHANGED {gate};
  - P-RIG-SIG: the rig;
  - P-RIG-REGION: the rig.
- **The identical outputs are not a blind instrument's.** Byte-identical output from three revisions is the shape a
  broken pin would also make. Here it is the prediction, and the plants separate the two worlds: an edit in a pinned body
  reads CHANGED, naming exactly its part, and an edit inside a FRAME BENCH region reads UNCHANGED.

## C1-L: a level and a weather answer per scene, for p4's A0a (registered before its code, 2026-11-13)

**What it is for.** p4's profile (docs/frame-profile.md §0, A0a) reads weather's per-aircraft term as a SLOPE, so its
arms need the (a) table at 20, 40 and 60 aircraft, and its P0 arms need weather OFF in the same image. C1 serves 40
only, with weather on. **The rulings (the orchestrator, 2026-11-13):**
- after 03:48:17Z: OK to start, on a NEW bench branch from `bfcb47e01c3d`; kLevel per scene; TBL-HOST pins Sum for 20,
  40 and 60; one check row plus its plants; full rigour;
- after 03:50:55Z: also (a), a per-scene `wx` field defaulting to true, answered through `Override("wx-layer")`, and
  (b), a FRAME_PROFILE-only include of p4's `FrameProfileScenes.h`, one owner per line;
- **the condition on (a)/(b):** C1's scene-list lines and the Kind enum and its arrays are BYTE-IDENTICAL to
  `bfcb47e01c3d`, shown by a diff printed as "0 lines differ" plus a no-control that reads non-zero;
- **the time-box:** frozen, with a sha to p4, by 11-13 ~18:00Z. If it is not, A0a runs at 40 only, with its slope
  rows named as dropped, and the branch waits. If (a)/(b) are unfinished then, the level part ships alone.

**Untouched:** `bench/frame-bench-c1` stays at `bfcb47e01c3d` (C1's frozen tree; Weatherscope's P5/P6 and p4's P1
pin it), and nothing here touches the recording window's set or C1's (images, manifests, `win_*.ps1`, readers,
`tbl_host.py`).

### The change, on `bench/frame-bench-levels` (from `bfcb47e01c3d`)

`include/FrameBench.h`:
- `kLevel` (40) moves above `Scene`, which gains `uint8_t level = kLevel;` and `bool wx = true;`. Default member
  initializers, so C1's lists `{{C1}, ...}` are unchanged and mean what they meant.
- C1's five-line block (`#if defined(BLIPSCOPE_PUSH_TASK)`, image B's list, `#else`, image A's list, `#endif`) is
  wrapped, not edited: `#if defined(FRAME_PROFILE)` / `#include "FrameProfileScenes.h"` / `#else` / the block /
  `#endif`. p4 owns that file and defines `kScenes` there. Its scenes use Kind C1 (no push delay, no loop +10), and its
  +5 ms plant lives in its own FRAME_PROFILE region by scene index, so the Kind enum, `kKindName` and `kPushDelayMs`
  are never extended. Everything stays inside `#ifdef FRAME_BENCH`, so a profile env defines both.
- `Level()` is the current scene's level (`kLevel` outside a scene). `MaxLevel()` is the largest level in the list.
  `kTableMax = 60` is the rows TBL-HOST verifies.
- The (a) hook's loop and its `table served n` line use `Level()`.
- `IsSceneHex` compares with `MaxLevel()`, never the current level. A scene that drops from 60 to 20 leaves fb0014 to
  fb003b tracked until they are evicted (`graceMs` ≤ 30 s, AircraftManager.cpp:4049, inside the 100 s settle), and they
  must still never be enriched.
- `PrintTable` prints one TBL-DEV line per distinct level, in the order of first appearance. With C1's lists that is
  the one n = 40 line it printed before.
- `Allocate()` refuses, printing once, when a scene's level is outside 1..`kTableMax`, so no scene starts.
- `Override("wx-layer")` answers the scene's `wx` ("true" or "false"). Initialise reads it at AircraftManager.cpp:940,
  and false closes the fetch gate (:3389). Buffers allocated earlier stay allocated: before S0 the board's own setting
  applies (absent means ON on this tree), so record flag bit 2 is the per-arm reading of whether weather was shown.

`src/AircraftManager.cpp` (the driver's region): the two scene lines print the scene's level and weather, `scene S<k>
<kind> home=Bend level=<n> wx=<on|off> rung=25mi settle`.

`test/host/check_frame_bench.py`: one row, **C1-L**, which needs all of the following:
- Scene's two default members;
- the FRAME_PROFILE include, once, wrapping C1's block;
- the (a) hook's loop on `Level()`;
- `IsSceneHex` on `MaxLevel()`;
- `Override`'s wx-layer line on the scene's `wx`;
- `Allocate`'s level refusal;
- the driver's two scene lines on the scene's level;
- `kLevel` named nowhere else in code.

### The predictions (frozen here, before the code)

| id | what is read | predicted |
|---|---|---|
| L-TBL | `tbl_host_lv.py` (new; `tbl_host.py`'s comparison, plus the C++ `Sum` at n = 20, 40, 60 against the Python's), the levels tree | 360 rows compared, rows off 0, worst \|Δ\| 1.1e-13 (`FrameBenchTable.h` is unchanged, so C1's figure); sums equal: n = 20 `05c015b4`, n = 40 `b1ace60e`, n = 60 `c9381f54` |
| L-TBL selftest | the same, with C1's phase plant (`0.414214` → `0.414215`) | clean exit 0; plant exit 1, rows off 354, all three sums DIFFER |
| L-ROWS | `check_frame_bench.py` on the levels tree | 14 rows, 0 failed |
| L-ROWS selftest | the 13 rows' existing plants, plus C1-L's six: the served loop on `kLevel`; `IsSceneHex` on `Level()`; the wx-layer answer fixed at "true"; `Allocate`'s refusal removed; the FRAME_PROFILE include removed; a scene line printing `kLevel` | each existing plant reads exactly its own row red; each of C1-L's reads exactly {C1-L}; PASSED |
| L-SAME | `c1_lines_diff.py` (new): the Kind enum line, C1's five-line block, `kNumScenes`, `kKindName`, `kPushDelayMs` from `FrameBench.h` at `bfcb47e01c3d` against the levels tree, byte for byte | "9 lines compared, 0 lines differ"; the no-control (`kPushDelayMs`'s 60 → 61 in a copy): "9 lines compared, 1 lines differ" |
| L-BUILD | `build_lv.ps1` (new; `build_c1.ps1`'s recipe: the path mapping and the pinned build time), each `pio` exit | the levels tree's `-framebench-c1a` and `-framebench-c1b` build, exit 0; a scratch FRAME_PROFILE compile (c1b's env + `-DFRAME_PROFILE`, in a separate worktree at the same commit, with a scratch `FrameProfileScenes.h` holding p4's seven arms as named in its 03:52Z message, sha logged, never committed) builds, exit 0 |
| L-ART | the c1b image's ELF strings | the new scene format (`level=%d wx=%s rung=%dmi settle`) and the level refusal's text present, the old `level=%d rung=%dmi settle` absent; the no-control, C1's frozen c1-B ELF: the old present, the new absent |
| L-I3 | the shipping `blipscope-s3-128` from the levels tree against m3-Dp (`fae94d2f70c3`), `bindiff_elfhash.py` | "length 1933952 in both; differing bytes: app_elf_sha256 32, image SHA-256 32, checksum 1, elsewhere 0" |

The Python's three sums came from `fixed_traffic.py` (`b530faf4`) through `tbl_host.py`'s own `py_sum`, at 03:51:30Z,
before any code. Its n = 40 reproduces C1's pinned `b1ace60e`, which is the control. The C++ side is the prediction.

**For p4's run, graded by p4's reader, not mine** (what this branch makes true on a board):
- one `table h=bend n=<N> t=1794441386 sum=<hex>` line per distinct level, in the order of first appearance:
  `05c015b4`, `b1ace60e`, `c9381f54` for 20, 40, 60;
- each scene line carries its level and its weather;
- `table served n <level>`;
- tracked == drawn == level on every measured frame;
- `fb submits` 0 in every scene;
- in a weather-off arm, flag bit 2 clear on every measured frame and no `wx served` line.

**The escape hatch:** any reading outside the table above stops the work, reported unread to the orchestrator, with no
fix before its ruling (a second off reading on a row goes to the owner). If the default member initializers or the
include do not compile, that is such a reading.

### C1-L's readings: every row as predicted (2026-11-13)

**The code:** `bench/frame-bench-levels` at `40f02c0fa812f4b83a5bb00e0730d8662bbab632`, pushed through the full pre-push
chain at 04:18:00Z. `bench/frame-bench-c1` still reads `bfcb47e01c3d`.

| id | reading | tool |
|---|---|---|
| L-TBL | "360 rows compared; worst \|d\| 1.14e-13; rows off 0"; sums n = 20 `05c015b4`, 40 `b1ace60e`, 60 `c9381f54`, each C++ equal to Python; exit 0 | `tbl_host_lv.py` `306d03a5478b`, generated by `make_tbl_host_lv.py` `9dcdccf279fe` from `tbl_host.py` `f2f838f9a3da` (unchanged) |
| L-TBL selftest | clean exit 0; the phase plant exit 1, rows off 354, all three sums DIFFER; PASSED | the same |
| L-ROWS | 14 rows, 0 failed | `check_frame_bench.py` at `40f02c0fa812` |
| L-ROWS selftest | each of the 13 existing plants reads exactly its own row red; each of C1-L's six reads exactly {C1-L}; PASSED | the same |
| L-SAME | "9 lines compared, 0 lines differ"; the no-control "9 lines compared, 1 lines differ" | `c1_lines_diff.py` `42ede18c2aa7`, on the committed blobs |
| L-BUILD | four builds, each `pio` exit 0 (below) | `build_lv.ps1` `ac73b70a6685` |
| L-ART | the six lines as predicted: lv-B's new format 2, refusal 1, old 0; c1-B's new 0, refusal 0, old 2 | `lv_artifacts.py` `7053204e328e` |
| L-I3 | "length 1933952 in both; differing bytes: app_elf_sha256 32, image SHA-256 32, checksum 1, elsewhere 0" | `bindiff_elfhash.py`, lv-I3 against m3-Dp |

| image (never flashed) | env | bytes | sha256 |
|---|---|---|---|
| lv-I3 | `blipscope-s3-128` | 1,933,952 | `c145699986b50933fcf806198f42e326d4d5c7f468936ccfc016d197aa2249dc` |
| lv-A | `-framebench-c1a` | 1,963,376 | `62df2307e9c6219147838be823c6a5b6a97e6327ef7162e8f991b3098a2753c6` |
| lv-B | `-framebench-c1b` | 1,965,568 | `eb0472aff8a8cd6d60690fd709e870cc4fa1eed806d1c5df13f0ce8da483b887` |
| lv-P (scratch) | `-framebench-c1b` + `-DFRAME_PROFILE` | 1,965,632 | `138cdd1b01e6729a42ea8c20071995c5a8e29c176daa361dac2543ddf33fa1fd` |

lv-P was built in a separate worktree at the same commit. Its scratch `FrameProfileScenes.h` (sha256 `c484e6beeac3`)
held p4's seven arms. That worktree was deleted after the build, and the file was never committed; p4 owns the real
one. **That the flag took is read from the image, not the ini**: a scene is 3 bytes (kind, level, wx). lv-P carries
the seven-arm list (3 occurrences in `firmware.bin`) and not C1's image-B list (0); lv-B carries C1's list (1) and not
the profile's (0). This was not a registered row. It proves the scratch build compiled the profile's list rather than C1's.

### THE LEVELS FIX, registered before its code (2026-11-14T20:39:53Z, on the orchestrator's ruling)

C1's first window found the (a) hook storing each row's hex and flight by pointer, so 40 rows carried one hex. That
is C1's first OFF reading, recorded in "C1's bench". `bench/frame-bench-levels` carries the same two lines
(`FrameBench.h:351-352` at `40f02c0fa812`), and p4's profile trees sit on it. The same fix goes here, and p4
re-applies its regions onto the fixed sha.
- **The change:** from `40f02c0fa812`, exactly the two lines `e.add(r.hex);` and `e.add(r.flight);` become
  `e.add(static_cast<const char*>(r.hex));` and the same for `r.flight`. Nothing else changes.
- **The instrument, v3 of `c1_discrim.py`, registered with it.** The levels loop reads `const int n = Level();` before
  the loop, and `Level()` lives in `FrameBench.h`, not in the copied block. So the harness gains one stub,
  `static int Level() { return kLevel; }`, at C1-L's default level of 40. Nothing else in the script changes.
- **Predicted, each read after the commit:**
  - the diff against `40f02c0fa812`: 1 file, 2 lines changed;
  - `check_frame_bench.py`: 14 rows, 0 failed at the fix, the same as at `40f02c0fa812` (run there too);
  - v3 at `40f02c0fa812`, default mode: TODAY 1 distinct hex, CONTROL 40. Exit 0;
  - v3 at the fix, `--fixed`: TODAY 40, PLANT 1. Exit 0. Default mode at the fix: STOP, exit 2;
  - v3 reproduces v2 on C1's two commits: `bfcb47e01c3d` reads 1 / 40, and `a599fcaaffc6` reads 40 / 1 under
    `--fixed`. The stub is unused there;
  - `c1_lines_diff.py` at the fix: "9 lines compared, 0 lines differ" against C1's frozen `bfcb47e01c3d`. Its control
    reads 1.
- **Outside the set:** any other diff, a red row, any other count, or a v3 reading on C1's commits that differs from
  v2's: stop and decide.
- **Then:** the sha goes to p4. No images are built from this branch for C1; C1's images come from
  `bench/frame-bench-c1`.
