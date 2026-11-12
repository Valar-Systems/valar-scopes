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
