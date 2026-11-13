# M3 on the 1.28": the push on its own task (registration; nothing built or run)

**Written 2026-11-12 20:24Z by the P4 session.** This is P2 of docs/speed-plan.md section 2b, the step checkpoint C1
reads. **No code before the owner accepts the plan and chooses staffing** (the orchestrator, 20:22Z). This is the
registration only, so that the code can start the moment the owner does. Every prediction is frozen here, before any code.

## 1. What changes

Today the loop draws a frame into one PSRAM sprite, then `pushSprite` sends it to the GC9A01 over SPI at 40 MHz. The
push holds the loop for all of its 25.8 ms (READ, section 2). M3 adds a **second backbuffer** and a **push task**:
1. the loop draws frame N into buffer A;
2. it hands A to the push task, which sends it, and draws frame N+1 into B meanwhile;
3. before drawing into A again, it waits for A's push to finish.

The frame becomes max(draw, push) instead of draw + push, if the overlap is complete.

**Only the 1.28"** (`s3_128.h`) takes it, behind a macro, `BLIPSCOPE_PUSH_TASK`, defined only there (`#if`, not
`if constexpr`: 5c's A4 and the gc-sections lesson). Every other SKU's image must read PLACED with the five-region
comparator. The 1.28" image changes by design. **Its picture must not:** the owner's rule that the 1.28" output stays
unchanged applies to what is drawn.

## 2. What is READ, and what is ASSUMED

| item | status |
|---|---|
| the push holds the loop for 25.8 ms | **READ**: the measured floor; `push_sprite` → `pushImage(..., use_dma = sprite buffer's use_dma())` (`LGFX_Sprite.hpp:421-425`) → `Panel_LCD::writeImage` → `write_bytes` |
| LovyanGFX turns DMA OFF for PSRAM sprites on purpose | **READ**: `push_sprite`'s own comment, "DMA disable with use SPIRAM" (`LGFX_Sprite.hpp:424`). The library's authors avoid this path, which is the main warning on design (i) below |
| the S3's DMA path for a long buffer polls segment by segment, yielding with `vTaskDelay(1)` | **READ**: `Bus_SPI.cpp:759-771`; so a push task blocks inside it, and the loop does not |
| the DMA descriptors point straight at the source, with no bounce and no PSRAM check | **READ**: `_setup_dma_desc_links` (`Bus_SPI.cpp:1237-1258`) |
| a cache write-back call exists on this S3 framework | **READ**: `esp_cache_msync` in `esp_mm/include/esp_cache.h` (pioarduino's esp32s3 libs) |
| at run time the radar touches the display only through `pushSprite`, `setBrightness` (the backlight PWM), text metrics and `getTouch` (the touch IC's own I2C) | **READ**: `main.cpp` (only `pushSprite` in the loop) and `AircraftManager.cpp` (:1427, :5297, :5610-5688, :9515) |
| the loop's display calls on the shared `tft` object do not race the push task's SPI transaction | **READ (2026-11-12, ~20:50Z; LovyanGFX 1.2.21):** `setTextSize` writes only `_text_style` (`LGFXBase.hpp:677`); `fontHeight`/`textWidth` read the font and text style; `setBrightness` stores a byte and drives the backlight (`Panel_Device::setBrightness` → `_light->setBrightness`, `Panel_Device.cpp:42-45`); `getTouch` → `getTouchRaw` (`Panel_Device.cpp:486-495`) touches the SPI transaction only when the touch's `bus_shared` is set, and the 1.28"'s CST816 block sets `bus_shared = false` on its own I2C port (`LGFX.h`, the `BLIPSCOPE_TOUCH_CST816` block); the touch watchdog's recovery calls only `tft.touch()->init()` (`TouchWatchdog.cpp:126-128`: the touch reset pin and I2C). **So nothing the loop calls at run time writes the SPI bus or the panel's transaction count.** `getTouchRaw` reads `getStartCount()`, but only acts on it when `bus_shared` is set |
| the S3's GDMA reads PSRAM correctly from LovyanGFX's descriptors once the cache is written back (alignment, external-memory block size) | **ASSUMED**, and design (i)'s first bench hour settles it |
| which core the push task runs best on, and its effect on Wi-Fi/TLS (core 0) | **ASSUMED**, and measured (M3-5) |

## 3. Buffer ownership, and what each design costs in memory

**The rule: the loop never draws into a buffer the push task is reading.** Both designs keep **two PSRAM
backbuffers**, A and B, so the loop can draw frame N+1 while frame N is sent.

- **The hand-off.** Each buffer has a binary semaphore, "free".
  1. The loop takes buffer X's "free" before drawing into it.
  2. When frame N is drawn, the loop puts X's index on a one-slot queue to the push task. It never touches X again
     until X is free.
  3. The push task sends X.
  4. **Only after the send completes**, it gives X's "free" back and calls `framebuf::FrameDone()` (section 5).

  So at any moment one buffer belongs to the loop and the other to the push task, and a frame is handed over exactly
  once, at its completion.
- **Memory, design (i):**
  - PSRAM: the second 240 × 240 × 2 = **115,200 B** backbuffer. CLAUDE.md's 2026-08-09 measurement found that PSRAM
    sprites leave the internal heap untouched.
  - Internal RAM: the push task's stack (**4,096 B**) and LovyanGFX's DMA descriptors for 115,200 B (29 `lldesc_t`
    of 12 B, about **350 B**, DMA-capable).
- **Memory, design (ii):** the same, plus **two 4,096-byte internal DMA bounce buffers (8,192 B)**. That is
  **about 12.6 KB of internal heap** in all, against **about 4.5 KB** for (i). It is the cost CLAUDE.md's contiguous-heap
  rule cares about.
- **Both are allocated once, at boot, before Wi-Fi starts**, never per frame, so they cannot fragment the heap later.
  M3-5 checks the TLS handshake beside them.

## 4. Two designs; the first bench hour picks one

- **(i) DMA straight from the PSRAM backbuffer.** `esp_cache_msync(buf, 115200, C2M)` writes the frame back, then
  `pushImageDMA` (or `writeBytes(..., use_dma = true)`) sends the PSRAM buffer itself.
  - Push task CPU: predicted **under 1 ms per frame**; the rest is the yielding segment loop.
  - Risk: the path the library avoids.
- **(ii) A ping-pong through internal SRAM.** Two 4 KB internal DMA buffers. The push task copies the frame from PSRAM
  into one while the other is sent by DMA.
  - Safe for DMA.
  - Push task CPU: predicted **2-5 ms per frame** for the 115,200-byte copy.
  - The waits are written to yield, never to spin.

**The first bench hour, on glass.** A bench-only test frame:
- **a frame counter printed in 8 horizontal bands**, the same number in every band of one frame;
- **it covers rows 0-215 only:** the bottom 24 rows stay the radar's own, so its screen-indicator dots (y = 224)
  remain visible. That keeps map-client's frame-trust test working on this image, and gives M3-1b's plant its control.
  A full-screen test frame would have hidden the dots and left the trust test with no control (found while coding
  M3-1b);
- **a vertical bar moving 4 px per frame**;
- **every row's colour distinct.**

It runs **60 s on today's image (the base) and 60 s on design (i).** The GC9A01 has no tear-sync line, so today's push
may already tear, and the base is the comparison.

**What fails it:**
- **A stale band (decisive):** a band showing a counter **2 or more frames older** than the bands around it, in any
  frame. Only a cache line not written back before the DMA can do that, and the base cannot show it.
- **More tearing than the base:** frames with two different counters across their bands, counted per second, above
  the base's rate by more than half again.
- **Shear the base does not have:** the moving bar broken or slanted across a band boundary.

**Who judges:** the owner, live on the glass, plus **a slow-motion phone video of each 60 s run** (a 240 fps mode if the
phone has one). The counters can then be read frame by frame afterwards: p4 grades the video, and the owner's eye
decides anything the video cannot show. **This is a window with the owner's time,** about 15 minutes for the base and (i),
plus 10 more if (ii) is needed; the orchestrator schedules it.

Clean → (i). A stale band → (ii), built and run the same way. More tearing or shear than the base → stop, and report
before either design is chosen. No other outcome is read as a design choice.

## 5. The second paths this creates

CLAUDE.md's rule: enumerate what the first path establishes.

- **`framebuf::FrameDone()`** runs after the push today (`main.cpp:995`). map-client's `p2p` (present to present) is
  timed from it, and /diag/fb's readers trust it. **Under M3 it moves to the push task's completion** (section 3's hand-off), so `p2p` stays
  present-to-present. Left in the loop, it would measure the loop and not the glass.
- **`framebuf::SetPhase(kPhasePushed)`** (`:983`) moves with it, for the same reason.
- **`RecordFrameUs` (`fr`)** spans update + draw + push + flush today (`:998`). Under M3 it no longer contains the push,
  so **`fr` drops by up to ~25.8 ms whether or not the glass got faster.** The gate reads `p2p`, and every report states
  `fr`'s new meaning beside it.
- **/diag/fb** reads "the backbuffer". With two, it reads the last COMPLETE frame: the one the push task last finished,
  not the one being drawn. Its Torn flag keeps its meaning.
- **The weather layer's flat path** (`DrawWeatherUnder`, `DrawSweepOverWeather`) works on whichever sprite the
  `BandCanvas` holds, so it needs no change. A host check confirms both buffers pass its `flat` test.
- **The frame-budget line and `[health]` frame numbers** are `fr`-based, so they are re-fitted on `p2p`, or marked.

## 6. Predictions, frozen now

Read on map-client's bench (frozen, CTRL passed) with A0a's profile env, Bend, 40 aircraft, weather on, map off. Each
row is graded before the next is read.

| row | what | predicted |
|---|---|---|
| **M3-0** | every image the macro does not reach (**corrected, the orchestrator's ruling (c), 23:50Z:** `blipscope-s3-128-framebench` extends `blipscope-s3-128`, so it is REACHED once its branch carries M3's base, and it is not in this set) | PLACED (five regions) |
| **M3-1** | the picture: the 240 golden (web build, which never takes the macro) and /diag/fb on the bench | **pixel-identical** to the base, frame for frame at the bench's fixed scenes |
| **M3-1b** | **the frame-trust property** (the orchestrator, 2026-11-12). map-client's frame-trust test reads the screen-indicator dots (`DrawScreenIndicator`, y = SCREEN_SIZE − 16 = 224) as proof that a /diag/fb grab is a finished frame, because the frame-start clear leaves none. **After M3, a planted mid-draw grab must still read UNTRUSTWORTHY.** In the m3bench envs, `/diag/fb-middraw` serves one grab of the DRAW buffer taken right after the clear and before the radar's `Draw`, labelled exactly like a finished frame (`Finished: true`, plus `X-Blipscope-Bench: m3-middraw-plant`). The glass test frame stops 24 rows short, so the dots stay visible in the same image | the plant reads **UNTRUSTWORTHY** under map-client's test, and a real /diag/fb grab from the same image (the control) reads **finished**. Either outcome otherwise: stop |
| **M3-2** | the design hour on glass (section 4), against the base | **no stale band; tearing no worse than the base; no new shear**, for (i), or (ii) if (i) shows a stale band. The test frame stops 24 rows short of the bottom, so the indicator dots stay visible for M3-1b |
| **M3-3** | `p2p` p95 at 40, weather on, map off: M3's cut. **Read on the (a) bench only** (the cloud `/blips` hook, ruled 2026-11-12; in local mode weather is never drawn), with the profile's PG-1 guard: the weather must be shown, or the reading is INSTRUMENT. **Run A/B/A across flashes**, graded as set out below the table | **the C1 row** (speed-plan 2b): ≥ 20 ms on track; 10-20 partial; < 10 failed |
| **M3-4** | the push task's CPU per frame | (i) < 1 ms; (ii) 2-5 ms |
| **M3-5** | networking and memory beside it, over the scene: fetch `requestMs` p95, the poll gap, internal free heap and its largest block at boot | requests within **±20%** of the base; **`CanHandshake()` never false**; internal free heap lower by **4.5 ± 1 KB** for (i), or **12.6 ± 1.5 KB** for (ii). Outside: the push task contends with Wi-Fi, or the memory is not what section 3 says, so stop |
| **M3-6** | the push itself, timed in the task | **25.8 ± 1.0 ms**, unchanged (the wire time) |
| **M3-7** | the standing rule: `p2p` p95 at 20 and 60 aircraft too | **no slower** than the base at either, beyond the bench's margin |
| **M3-8** | **flash writes beside the push** (the cloud reviewer's row, accepted by the owner): an OTA download and install, and a config-page save, each with the push task running, on whichever design the first hour picks | **no crash or watchdog reset** (the boot reason and boot count unchanged until the install's own restart); **no stuck frame** (no `p2p` above 2 s outside the install's own reboot); **no torn frame** on glass (the owner's eyes and the slow-motion video, as in section 4); **the install completes and boots** the new image (its `[build]` banner and version on serial or in its check-in) |
| **M3-9** | **(i)'s longer soak, before (i) is chosen for good** (the cloud reviewer's row, accepted). (i) is the path LovyanGFX turns off on purpose, and a clean 60 s look is thin evidence for a rare coherency fault | (i) runs **through C1's bench window and one overnight run on E2:08**, with periodic photos and the coherency self-check below; **zero stale stripes and zero self-check mismatches** keeps (i). **One of either sends it to (ii).** When in doubt, (ii): it costs 2-5 ms a frame |

**M3-0, READ 2026-11-12 23:42Z (local, one worktree path, `SOURCE_DATE_EPOCH=1762862445`, base `4b00c97b3dbd`, change
`3bc7ec26fe28`, draft #687): PASS on the 13 envs it built.**
- **The instrument held.** Every compile-time stamp in all 14 PLACED images reads the epoch's. Only the framework's
  prebuilt stamps differ from it, and they are identical on both sides.
- **The 7 PLACED envs: 7 of 7 PASS.** Sizes are equal, and the only differing bytes are `app_elf_sha256` and the
  trailing checksum and SHA-256 (64-65 bytes per env, other 0). The envs: blipscope-pro-s3-21, missileer-s3-128,
  missileer-photo, weatherscope-s3-128, weatherscope-bench-s3-128, blipscope-pro-s3-175-amoled, animtest-s3-128.
- **The 6 REACHED envs compile (exit 0):** blipscope-s3-128, -wxlook, -joindiag, -m3bench, -m3bench-base,
  -m3bench-plant.
- **The marker check, on the m3bench image (sha `2ca8f914a729`):** `--expect "[m3-bench]"` exits 0, a plain scan exits 1,
  and a PLACED control image (pro-s3-21) exits 0. All three as predicted.
- **The framebench clause is NOT EXERCISED, and it is not passed.** The env exists only on map-client's
  `bench/frame-bench-v19` (`a37e99b09fb4`). That branch is v19-based, so M3 cannot reach it and nothing was built for it.
  By ruling (c), it stays M3-free while it is the v19 baseline. It takes M3 only with main, and only after map-client has
  registered where `p2p` is stamped under M3, with a control that a planted push delay shows in `p2p`. M3-0 is not
  re-graded.

**M3-1b, READ 2026-11-13 01:43Z on E2:08 (a no-hands window under the owner's standing terms): AS PREDICTED.**
- **The image:** `blipscope-s3-128-m3bench` at `3bc7ec26fe28`, sha256 `2ca8f914a729`. It was written app-only to ota_0
  (0x020000..0x1FA98F) from a frozen build dir, with no rebuild, and the write was verified.
- **The test:** map-client's `map_bench_read2d.py` (`9082be910ca0`), called through `m3_1b_trust.py`. T2 is
  `dots_complete`, on the dots that this tree's `wx-fb-check.py` (`92d85ce4e711`) derives. Before the window, its
  self-test read as predicted: a mid-draw synth with no dots was UNTRUSTWORTHY, the same frame with the tree's dots
  drawn was TRUSTED, and one dot pixel short was UNTRUSTWORTHY.
- **The read:** 3 real `/diag/fb` grabs were TRUSTED. 3 `/diag/fb-middraw` grabs, carrying the plant header and labelled
  Finished, were UNTRUSTWORTHY by T2 ("screen dot 0: 0/37 px in its colour"). **Under M3, the frame-trust test still
  rejects a grab labelled finished that is not.**
- **The window's own failure, before the read:** the first run's script stopped at W3 on a PowerShell variable
  collision ($H and $h are one variable). No grab had been taken and the board had not been touched. The
  orchestrator ruled it an instrument failure, and the fixed script took the grabs on the running image.
- **The restore:** v19, `f6d73e39`, was written and verified. The board fast-joined Wi-Fi in 3.7 s from its saved
  settings, the portal never ran, and HTTP answered 200. Two of p4's boot-read predicates were off in FORM: they
  expected `autoConnect() returned true` and no line containing "portal", but v19 joins by `TryFastJoin()` first,
  and its "credentials saved: portal times out" line is a config print. The orchestrator recorded it as a prediction
  miss, not a failed restore. Restore reads now use "[WiFi] CONNECTED" AND ("fast join OK" OR "autoConnect()
  returned true"), AND zero portal-START lines.

**M3-3 runs A/B/A (the orchestrator's rule for every before/after frame-time row, 00:11:47Z). Registered before C1.**
- **The rule's reason.** blipscope-5c's FT2 path was untouched by its fix, yet it read +1.41 and +1.60 ms between two
  sessions with different flashes and boots. Its within-session A/A held at most 1.06. So an offset between sessions
  of about 1.5 ms is plausible, and it is unmeasured.
- **M3 cannot be switched inside one image.** `BLIPSCOPE_PUSH_TASK` is a compile-time macro. A is the bench image built
  with `-DBLIPSCOPE_NO_PUSH_TASK`, the same source and flags otherwise (the pattern of `m3bench-base` against `m3bench`).
  B is that image without the flag. They are two flashes and two boots, so this is the **between-session** case, and
  the rule applies in full.
- **The order:** A1 (before), then B (after), then A2 (before again). Each is flashed and booted, and each runs the same
  scene with the same settle and the same measured span, inside one E2:08 window. The images are built first and the
  sha of each is recorded before the first flash.
- **The noise:** N = |p95(A2) − p95(A1)|, read before B is graded.
- **The error bar (the orchestrator's refinement, 00:15:35Z):** E = max(N, U). A single A1/A2 pair can land close by
  luck, and that must not shrink the bar below the instrument's own floor. U is the bench's within-session U95:
  **1.54 ms**, I-6's measured p95 noise on map-client's bench (`baseline_b.out`, `29d24028cec9`), or the same
  window's own CTRL repeat |Δp95| if the run carries one, whichever is larger.
- **M3's cut:** cut = mean(p95(A1), p95(A2)) − p95(B).
- **The grade.** C1's band (≥ 20 on track; 10-20 partial; < 10 failed) is assigned only if cut − E and cut + E fall
  in the SAME band. If they straddle a boundary, the row reports the straddle with both ends and goes to the
  orchestrator. It is not rounded into a band.
- **The stop: N > 5 ms.** That is the escape hatch speed-plan 2a uses for the same bench. The bench then cannot grade a
  cut of this size, the row is INSTRUMENT, and nothing is read from B.
- **The cost:** one more flash and one more run of the scene, about 10 minutes of the window.

**M3-3 for C1, as ported by map-client (p4's sign-off, 2026-11-13; each item ruled by the orchestrator before any run).**
These replace the parts of the M3-3 row that named A0a's profile, which has never run (P1 is unwritten).
- **The window:** E2:08, 11-15 02:00-03:00Z. The tree is `3bc7ec26fe28`.
- **The scene:** Bend, 40 fixed aircraft, 25 mi (map-client's CTRL scene), weather on, map off. /blips comes from the (a)
  hook. Every scene settles for 100 s and is then measured for 120 s.
- **The images.** Both are `${s3128_hw.build_flags}` + `-DBLIPSCOPE_EXPECT_BIG_NVS` + `${cloud.prod}` +
  `-DWX_LAYER_SHIPS` + `-DFRAME_BENCH`.
  - A adds `-DBLIPSCOPE_NO_PUSH_TASK`. B is the same set without it.
  - Neither carries `WX_LOOK_AB`, whose timers sit in the frame; `M3_BENCH`, which is bench only; or `FRAME_PROFILE`,
    which is not written.
  - `WX_LAYER_SHIPS` is set per env because main's `[weather] layer` is still empty, which compiles weather out. It is
    v20's shipping weather.
- **The weather:** a fixed field, at the plan's basis. The real header and the real frameEpoch are kept, so the layer is
  not hidden as stale. The body is code 0, plus ONE cell of code 2, 15 mi from home at bearing 135°, about 3.3 × 2.4 px.
  The basis is the 2026-10-29 replay-session span arm, near-dry: blend 4.45 ms with one counted cell, against 4.32 dry.
  Rebuilds follow the real frame cadence, and they are counted, tagged and included by v19's rule. A and B share that
  cadence: fetch time and rebuild triggers count no frames.
- **PG-1, its substitute (accepted):**
  - the STATE bit `wxShowing == Crop` on every measured frame (the "shown" half);
  - a PIX-WX read per arm, weather pixels > 0;
  - **(b), made exact, per arm, ALL of:** (i) at least one "[wx] layer rebuilt in N ms (look A)" before the span ends;
    (ii) zero "the layer stays off" and zero "[wx] buffers FAILED" since boot; (iii) every "[wx] status=" line up to
    the span's end shows 200 or 304, with an outcome that is not error. The reader matches the strings. At
    `3bc7ec26fe28` they are AircraftManager.cpp :3585, :3555, :3342 and :3420.
  - Each part has a data-side plant that must read INSTRUMENT. The weather hook replaces the body after the real GET, so
    the real status line still prints.
- **The B arms:** plain, D = 10, D = 60, and the loop's +10 (I-6).
  - D is `vTaskDelay(pdMS_TO_TICKS(D))` in the push task, never a spin. The push task runs one priority above the loop,
    on the loop's core, so a spin would preempt the loop.
  - D = 60, not 40: with weather on, B's loop is about 64-69 ms, and push + 40 sits inside that.
- **The push readings** (`[m3] push_avg_us` is a since-boot average, `main.cpp:151-152`, so it is never graded as
  printed). The push bands apply to two readings, and a disagreement between them over 0.5 ms is INSTRUMENT:
  - (a) the reader's windowed average between consecutive `[m3]` lines lying wholly inside a span;
  - (b) map-client's own per-scene FRAME_BENCH push average.
- **p95(push)** comes from map-client's per-scene histogram, in 1 ms bins, timed over t2 − t0 so that it includes D.
  The reader takes the nearest-rank bin.
- **The predictions:**
  - plain: the cut is graded in C1's bands with E (above);
  - D = 10: the push average rises +9.0 to +10.0 ms, and p2p p95 does not move (± E);
  - D = 60: the push average rises +59 to +60 ms, and p2p p95 = p95(push) ± E, at least 10 ms above B plain, or
    INSTRUMENT;
  - the loop's +10: Δp2p p95 = +10.0 ± 1.5, and push unchanged within ± 1.0;
  - outside the set: p2p p95 of B plain within 2 ms of the push average means B is push-bound. Stop.

**OUTSIDE THE SET:** a slower frame anywhere, a changed picture, a torn or stale stripe that (ii) does not cure, or
Wi-Fi contention. Each stops M3 and goes to the orchestrator, and C1 reads as "failed" until the reason is known.

**How M3-8's OTA install is served without touching the fleet.** The path is the one the code already documents
(`OtaUpdater.cpp:101-104`). A bench image is built with `-DOTA_RELEASE_BASE=https://github.com/Valar-Systems/valar-scopes/releases/download/<bench tag>`,
pointing at a **prerelease**:
- Prereleases never resolve through `releases/latest`, so no fleet device can see it. The bench board still
  exercises the full production OTA path: GitHub's CDN, the pinned root CAs (`OtaCerts.h`), `Update.begin`/`write`,
  and the identity check.
- The prerelease carries a `version.txt` one above the bench image's `FW_VERSION`, and a `firmware-s3-128.bin` that is
  the same M3 bench image rebuilt at that version.
- **Publishing it is blipscope-5c's** (releases are its) **with the owner's OK.** Its tag is set with blipscope-5c,
  and it is deleted after the row.
- A local HTTPS server is not used. The updater pins GitHub's root CAs (`setCACert(OTA_ROOT_CAS)`, `:710`), so a local
  server would need a bench-only CA baked in, a second path the shipping image never takes.

**M3-8's config-page save** posts the page's own current values back unchanged, read from the page first. It never
posts a partial body: the 2026-08-02 scripted POST that silently switched features off is the lesson
(`AircraftManager.cpp`'s frame-budget note). Or the owner saves in a browser, during the same window.

**M3-9's coherency self-check** is a bench-only instrument, added to the photos because one photo every few minutes
can miss a stale stripe that lasts one frame.
- Every 30th frame, right after the push's DMA, the same PSRAM range is read again by a second DMA
  (`esp_async_memcpy`, present in this S3 framework at `esp_hw_support/include/esp_async_memcpy.h`) into internal
  SRAM. Its CRC is compared with the CPU's CRC of the buffer.
- A mismatch means the DMA saw bytes the CPU had not written back, the exact fault behind a stale band. It is
  counted and printed as `[m3] coherency mismatch`.
- **ASSUMED until read:** that `esp_async_memcpy` reads PSRAM on the S3, and its alignment rules.
- Its cost: about 2 ms on a second DMA channel every 30th frame, under 0.1 ms a frame on average. It runs **only in
  the bench env**, never in the image graded for M3-3.
- **Its control, first:** one planted run skips `esp_cache_msync` on purpose for one frame in 30. The self-check must
  count those frames as mismatches, or it is not believed.

## 7. Size

| item | size |
|---|---|
| reading LGFXBase's shared state (the last ASSUMED row that code depends on) | 0.25 d |
| the code: the macro, the second sprite, the task, the hand-off, FrameDone and SetPhase moved, the `fr` marking, the host check | 1 d |
| the design hour (section 4), on E2:08, with the owner's eyes and a slow-motion video | 0.25 d (one window with the owner's time, ~15 min; +10 min for (ii)) |
| design (ii), only if (i) shows a stale stripe | +0.5 d |
| the bench read (M3-0 to M3-7) | 0.5 d (one window) |
| M3-8: the bench prerelease (blipscope-5c publishes, with the owner's OK), the install and the save, in a window | 0.5 d, plus 5c's time |
| M3-9: the coherency self-check, its planted control, and the overnight run on E2:08 | 0.5 d, plus one night of E2:08 |
| review, CI, merge by rule (blipscope-5c reads the shared `main.cpp` change) | 0.5 d |

**3.5 days**, or **4 days** if design (ii) is needed: speed-plan 2b's 2.5 for P2, plus M3-8 and M3-9.

**What the overnight soak does to C1.** C1 (about 2026-11-16) needs M3's code (from 11-13, about 2 days), the
glass hour, the bench read and one night on E2:08.
- **C1 holds at 11-16 only if the glass hour is done by the evening of 11-15**, so (i) soaks overnight 11-15 to 11-16.
- **Each day later the glass hour lands moves C1 by a day.** If design (ii) is chosen at the glass hour, no soak is
  needed, and C1 holds at 11-16 on (ii)'s numbers.
- **M3-8 needs blipscope-5c's prerelease and its time.** It can run after C1 without moving it, since C1 reads
  M3-3, not M3-8. It must pass before M3 merges.