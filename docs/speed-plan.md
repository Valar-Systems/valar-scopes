# Speed: one plan for every board

**Status: a plan, no code.** Written 2026-11-12 19:37Z by the P4 session, for the owner, through the orchestrator;
section 2a (the owner's choice, sized) added ~20:10Z.

The owner, 2026-11-12 (after 19:31Z), in his words: "After v20's freeze, a speed workstream shared by all boards, with
targets measured on glass: 30 fps on the 1.28" and the P4; at least 20 (aiming for 30) on the 2.1"; 30 on the Android
screens. Methods first: static layers drawn once and reused, touch read on its own schedule, and drawing overlapped
with the panel push. Its plan comes to me with sizes before any code."

**And at 19:55Z, superseding the numbers above,** in his words:
- "I watched 10, 20, 24, 30 and 60 fps side by side. 20 fps is my minimum (p95 at or under 50 ms). 24 is the target.
  Nothing above 24 is chased. Every "30" in #680 becomes 24, including the 2.1's "ideally 30"." "The number for the
  Android screens waits until I've looked at the 23.6-inch screen itself."
- "The bar is read with 40 aircraft on screen, map and weather on. 20 and 80 are reported beside it." The firmware
  tracks at most 60 aircraft (`MAX_AIRCRAFT`), so the high point is **60** unless he rules otherwise.
- "map-client's early reading puts v19 alone at 74.5 ms p95 with 41 aircraft, and weather adds about 13 ms. So v20 as
  planned misses my bar. I'll choose now, not at pre-flight. BY TOMORROW, p4 SIZES TWO THINGS: (a) reaching 20 fps at
  40 aircraft with map and weather on; (b) v20 no slower than v19 at the same traffic. Then I choose: hold v20 for (a),
  or ship (b) on time and make 20 fps v21's bar. The speed plan starts now, since both need it."

This plan takes in:
- **big-screens steps 5 and 13.** The touch measurement and the on-glass speed baseline move here
  (docs/big-screens.md).
- **blipscope-5c's 2.1 registration**, as its 2.1 chapter: `docs/pro21-v21-speed-touch.md` on branch
  `blipscope-5c/pro21-v21-speed`, read at `3ef39747d8f7` (with amendments A1-A8).
- **the Weatherscope's static-layer method.** It is cited here from the record on main; the note the orchestrator asked
  weatherscope for replaces this when it arrives.
- **map-client's v19 frame-time bench** (`docs/frame-time-bench.md`, being built), as S0: the one instrument the
  baseline and every target are read on.

**The speed plan starts now** (the owner, 19:55Z), not after v20's content freeze: both (a) and (b) need it.

## 1. Rules every step follows

- **The owner's STANDING RULE:** "every change to drawing code reports its frame time on the bench, before and after. A
  slowdown beyond the margin frozen in its registration stops it." Each step below freezes its margin in its own
  registration, before its code.
- **The 1.28" output stays unchanged** (the owner, 17:26Z). Speed work changes the 1.28" image by design: that is the
  point of it. So its proof is that the picture is unchanged, not the bytes:
  - the 240 golden pixel-identical in the web build, which runs the same drawing code;
  - /diag/fb frames pixel-identical on the bench (/diag/fb reads the backbuffer, so it proves the drawing, not the
    scan-out);
  - big-screens section 4's stop for anything #656's comparator cannot decide. Any change to its LOOK goes to the owner
    separately.
- **Shipping images change only through a ruled step.** Measuring instruments live in bench-only envs (a slug-less CI
  row, as CLAUDE.md describes), so measuring a board never changes what ships. 5c's 2.1 chapter gates its own
  instruments behind `BLIPSCOPE_TOUCH_TASK` for the same reason.
- **Targets are measured on glass, on each board, as p95 frame time.** The owner's bar is read **at 40 aircraft with
  map and weather on**, with 20 and 60 reported beside it. **20 fps = p95 ≤ 50 ms is the minimum; 24 fps = p95 ≤ 41.7 ms
  is the target; nothing above 24 is chased.** On the 1.28" the instrument is map-client's frame-time bench (S0), and its
  statistic is p95 of `p2p` (present to present), rebuild frames included.

## 2. Where each board stands

| board | measured today | target | basis of "today" |
|---|---|---|---|
| **1.28" Kit S3** (240 px, GC9A01 SPI at 40 MHz) | **v19, map off, weather off: 74.5 ms p95 at 41 aircraft** (one early reading, map-client's map-bench image, 16:59:30Z, before its bench's control has run). Weather adds **about 13 ms** (the owner's figure, from map-client's early reading). The map's steady cost is **unmeasured**; its rebuilds at KSFO took 177-198 ms each (#680). The 2026-08-29 fit, p95 = **35.8 ms + 0.802 ms per contact** (R² 0.985), gives 68.7 ms at 41, so v19 sits 5.8 ms above it. The cloud feed carries 40 aircraft (`BLIPS_LIMIT`) and the firmware tracks up to 60 (`MAX_AIRCRAFT`) | **24 fps** (minimum 20) | the stock-defaults soak of 2026-08-29: **44.9 h, 5,392 samples**, the frame-budget note's last block in `src/AircraftManager.cpp`. That note itself corrects an earlier "48 h". It predates v19, the map and the weather layer, so it is context only: S0 (map-client's frame-time bench) is the baseline every step is judged on |
| **2.1" Waveshare** (480 px, ST7701 RGB, 8 MHz pclk) | **9.7 fps (103 ms)**, contact count not recorded | at least 20, ideally 24 | #676 (114 frames in 11.70 s) |
| **P4 4.0"** (720 px, JD9365 DSI) | **unmeasured** (no board yet) | 24 fps (minimum 20) | none |
| **Android screens** (7" 1080, 15.7" 960, 23.6" 1280; the web build in the kiosk app's WebView) | **unmeasured** on the real screens | **pending the owner's look at the 23.6"** | none. The stage-1 rig ran the WebAssembly firmware at about 0.2 ms a frame on a desktop, without drawing; that says nothing about the WebView |

**What the 1.28" numbers already say.** The panel push is **25.8 ms** of every frame (the measured push floor, in
docs/animtest-results-2026-08-06.md and docs/weatherscope-stage3.md; 115,200 bytes at 40 MHz). Push and draw run one
after the other today, so everything else in v19's 74.5 ms at 41, about 49 ms, is update, draw and flush.

## 2a. The owner's choice, sized: (a) 20 fps at 40 aircraft in v20, or (b) v20 no slower than v19

**The numbers this sizing stands on, and how firm each is.** map-client's frame-time bench (S0) is registered
(`docs/frame-time-bench.md`, branch `docs/frame-time-bench` at `b0c58758dd8e`). Its control and its baseline have NOT
run yet. When they have, every number below is re-read on it, and a sizing that moves goes back to the owner.

| number | value | status |
|---|---|---|
| v19 at 41 aircraft, map off, weather off | 74.5 ms p95 | one reading (map-client's map-bench image, 16:59:30Z), before the bench's control |
| weather on | about +13 ms | the owner's figure, from map-client's early reading. Its own registration predicted ≤ 3 ms (docs/wx-device.md, B1), so about 10 ms is unexplained |
| the map's steady cost, M | unknown | **unmeasured**; its rebuilds at KSFO took 177-198 ms each (#680), counted in the gate's p95 |
| the push | 25.8 ms | measured floor (two docs, section 2) |
| per contact | 0.802 ms, of which labels about 0.53 | the 2026-08-29 fit and the 2026-08-26 labels A/B (+8.5 ms at ~16) |

**So v20 at 40 aircraft is about 73.7 + 13 + M = 86.7 + M ms p95 (11.5 fps or worse).** The bar (20 fps) is 50 ms;
the target (24 fps) is 41.7 ms.

### (a) 20 fps at 40 aircraft, map and weather on, in v20

It needs **36.7 + M ms** off the frame (45.0 + M for 24 fps).

| lever | expected cut at 40 | read or assumed | needed for (a)? |
|---|---|---|---|
| **M3** (the push on its own task, a second backbuffer) | up to **25.8 ms**, if the overlap is complete | READ: the push holds the loop today. ASSUMED: a push task, and DMA from the PSRAM backbuffer with a cache write-back on the S3 (section 3) | **yes**: no path to 50 ms without it |
| **W** (weather back to its registered ≤ 3 ms) | about **10 ms** | the gap is measured; its cause is **not read** | yes |
| **M1 on the map** (its steady draw into the cached layer, with the weather) | **M** | unmeasured | yes, if M > 0 |
| **L1** (each contact's label layout cached) | part of ~21 ms of label cost at 40 | unmeasured | **probably**: with M3, W and the map cached, the frame is still ~0.9 ms over 50; 24 fps needs ~9 ms more |
| **L2** (SPI at 80 MHz) | ~13 ms of push, only while the push is not hidden | computed | **only if M3 cannot overlap.** Without M3 the frame is 86.7 + M; L2, W and the map leave it at ~63.7, and L1 would have to remove two-thirds of the label cost (13.7 of ~21 ms). So **(a) is probably out of reach in v20 without M3** |

**Steps, owners, sizes** (elapsed days from 2026-11-13):

| step | owner | size |
|---|---|---|
| A0: a per-phase profile of the v20 radar at 40 aircraft, map and weather on (bench-only env; map-client's bench traffic) | p4 | 1, with one E2:08 window |
| A1: M3 on the 1.28". Its first bench hour answers the DMA-from-PSRAM question | p4 (5c reads the shared main.cpp change) | 2.5 |
| A2: W, the weather layer's cost against its own prediction | the session that owns `WxLayer.h` (the orchestrator names it) | 1-2, from A0 |
| A3: the map's steady draw into the cached layer | map-client (it owns the map's draw) | 1-2, from A0 |
| A4: L1 | p4, or a second session (Q-S3) | 2 |
| A5: L2, plus an incoming-inspection row on the boards to hand | p4 | 0.5 + the inspection; only if A1 fails |

Each step also carries the standing rule's before-and-after bench run (about 0.25 d, in E2:08 windows) and its PR's
review, CI and merge by rule (about 0.5-1 d).

- **Staffing 1, p4 carrying A0, A1 and A4** (A2 and A3 with their owners, in parallel): about **8 elapsed days**, so
  (a) lands about **2026-11-21**.
- **Staffing 2, a second session carrying A4 beside A1:** about **6 days**, landing about **2026-11-19**.
- **v20's date under (a):**
  - **If M3 overlaps,** (a) lands inside F's window (November 21-25), so v20's promote holds at **November 25-30**,
    with no slack: any off reading moves it day for day.
  - **If M3 cannot overlap,** (a) is probably out of reach in v20, and that is known by about **2026-11-16** (after A0
    and A1's first bench). That is early enough to fall back to (b).
  - Either way, (a)'s code adds to v20's per-function accounting with #656 (map-client's 6a) and to O11's candidate.
- **Risk: HIGH.** It rests on M3 (two assumptions), on W (an unexplained gap), on M (unmeasured), and on two single
  readings.

### (b) v20 no slower than v19 at the same traffic

It needs about **13 + M ms** off v20's frame, less the margin, with M3 not required.

| step | owner | size |
|---|---|---|
| B0 = A0, the profile | p4 | 1 |
| B1 = A2, weather back to ≤ 3 ms (about −10) | the `WxLayer.h` owner | 1-2 |
| B2 = A3, the map's steady draw cached (−M) | map-client | 1-2 |
| B3: the residual ~3 ms, by M1 on the radar's own static parts (rings, cardinal labels, range text) in the same cached layer | p4, or the second session | 1-2 |
| B4: the measurement below: v19 twice (its repeatability) and the v20 candidate | map-client (its bench); E2:08 windows of ~75 min | 1 |

- **About 5 elapsed days with B1-B3 in parallel**, landing about **2026-11-18**: **v20 on time**. M3 (and L1 if needed)
  then goes to v21 with the 20 fps bar.
- **Risk: MODERATE.** It rests on W's cause (not read), on M (unmeasured), and on single readings. If B1-B3 fall
  short, the fallback is M3 in v20, and (b) inherits (a)'s plumbing risk.

**How "no slower than v19 at the same traffic" is measured, exactly:**
- **The instrument:** map-client's frame-time bench, its FIXED block. That is homes Bend and KSFO × levels **20, 40 and
  60** aircraft × its three rungs (18 scenes), its settle and its 120 s measured span, on E2:08.
- **The statistic:** p95 of `p2p`, rebuild frames included (the gate's own).
- **The arms:** the v19 release image, against the v20 candidate with **map and weather on**. They run on the same
  board, from the same bench build, with the same deterministic traffic tables, homes and rungs.
- **The criterion:** in every scene, p95(v20) ≤ p95(v19) + margin.
- **The margin, frozen before any v20 number is read:** the larger of **2.0 ms**, or the bench's own repeatability: the
  largest |Δp95| across the 18 scenes between **two v19 runs** in different windows. The bench's control is graded to
  ±1.5 ms, which is why 2.0 ms is the floor.
- **The escape hatch:** if the two v19 runs differ by more than **5 ms** in any scene, the bench cannot decide "no
  slower" at this scale. Then stop and report, and the owner decides.
- **Reported beside it, not gating under (b):** the owner's 20 fps bar at 40, read on the same v20 run, and the LIVE
  scenes.

### What the owner is asked, inside this choice

- **(a) or (b).**
- **Q-S2:** L1 (probably needed for (a), not for (b)), and L2 (only if M3 cannot overlap).
- **Q-S3:** staffing 1 or 2. The suggested second session is weatherscope, with the E2:08 constraint in section 5.
- **The `WxLayer.h` owner for A2/B1:** the orchestrator names it.

## 3. The methods, and what each is expected to gain

**M1. Static layers drawn once and reused** (the owner's first method; the Weatherscope's).
- **Basis, measured:** on the Weatherscope's loop, caching the layer cut the draw from **48.6-50.6 ms to 32.3-34.5 ms**,
  about 16 ms or a third, for a one-off **20 ms** build (docs/weatherscope-stage3.md, B2). On the radar, v20's weather
  layer is already cached that way: steady frames copy it instead of filling black (docs/wx-device.md, B1: on − off ≤ 3
  ms p95).
- **On the radar:** the candidates are the rings, the cardinal labels, the range text, and v20's map if it is redrawn
  every frame. The airport overlay is not a lever: it was measured at −0.09 ms (the frame-budget note).
- **Expected gain:** up to the static share of the draw: at most ~10 ms on the 1.28" with an empty sky. **That share
  is unmeasured**, so it is sized after the per-phase profile (S1), as 5c sizes it on the 2.1.

**M2. Touch read on its own schedule** (the owner's second method).
- **What it fixes:** swipes at any frame rate. It is not a frame-time method.
- **Basis:** 5c's registration predicts 1-2 samples per quick swipe today and 10 or more with a 100 Hz touch task
  (T0/T1), at a frame cost of at most +5 ms (T2). That design comes from a read of LovyanGFX's per-port I2C mutex, and
  of the one call that takes no lock, `i2c::init`.
- **The web build** reads touch once per animation frame too (`render.js`). Its version of M2 queues every pointer event
  with its own timestamp, the same idea as 5c's per-sample `TouchNow()` (A8).

**M3. Drawing overlapped with the panel push** (the owner's third method).
- **1.28" (SPI):** push frame N while the loop draws frame N+1 into a second backbuffer. The frame becomes
  max(draw, push) instead of the sum. **Expected, if the overlap is complete: ~25.8 ms (38.8 fps) with an empty sky;
  ~42 ms (23.8 fps) at 40 contacts.**

  What has to be true, each item marked READ (in the code at LovyanGFX 1.2.21, the version this env builds) or ASSUMED:

  | precondition | status |
  |---|---|
  | a second backbuffer: 115 KB more PSRAM, no internal heap | **READ**: CLAUDE.md's 2026-08-09 measurement (sprites in PSRAM leave the internal heap untouched) |
  | today's push occupies the loop for its whole length | **READ**: a full-width 16-bit `pushSprite` reaches `Panel_LCD::writeImage` → `write_bytes(src, w*h*2, use_dma = false)` (`Panel_LCD.cpp:287-305`), the CPU path of `Bus_SPI::writeBytes` |
  | the push can be started and left to run | **READ, and it is NOT what the library's DMA path does on the S3:** a frame longer than one SPI transaction is issued segment by segment, polling `SPI_USR` with `vTaskDelay(1)` between (`Bus_SPI.cpp:759-771`), so the call returns only once the last segment starts. So the overlap needs **the push on its own task**, with the loop drawing meanwhile. That task's core and its contention with Wi-Fi/TLS are **ASSUMED** workable and measured in S3 |
  | DMA can read the backbuffer from PSRAM | **ASSUMED**: the S3's GDMA reading external memory. LovyanGFX writes the cache back before a DMA only on the P4 (`Bus_SPI.cpp:711-714`), so on the S3 the frame's cache lines must be written back first by our code. Not yet read in IDF or tried |

  If the push task must use the CPU path, it occupies a core for the whole 25.8 ms, and the gain depends on that core
  not being the loop's. S3 measures which case holds before it is graded.
- **2.1 (RGB, through the bounce buffer):** draw straight into the back framebuffer and present by swapping at the
  frame wrap. That removes the 460,800-byte `pushSprite` copy (5c predicts 15-35 ms) and overlaps the flush wait (5c:
  10-35 ms). **Expected:** frame ≈ draw, which 5c predicts at 30-60 ms (17-33 fps), less the bounce refill's CPU share.
  Replaced by 5c's 1b measurement.

  | precondition | status |
  |---|---|
  | two framebuffers | **READ**: `cfg.num_fbs = 2` (`Panel_ST7701_esplcd.cpp`) |
  | a swap that takes effect at the frame wrap | **READ**: in bounce mode the driver switches the buffer it reads only at the frame wrap (the driver's header comment, `:13-14`) |
  | a scan-out that leaves the CPU free | **READ, and only partly true:** the bounce buffer is refilled by a **CPU memcpy** from the PSRAM framebuffer (`:8`), so scan-out costs CPU all the time. That share (480x480x2 bytes at ~29 Hz, ~13 MB/s of PSRAM reads) is **unmeasured**; 5c's `/diag/perf` would see it as time missing from every phase |
  | drawing straight into the back framebuffer instead of a sprite | **ASSUMED**: the radar draws through `BandCanvas` into a sprite today; pointing a sprite at the driver's buffer is not yet read in LovyanGFX |
- **P4 (DSI, a framebuffer in PSRAM):** the same shape as the 2.1. The radar draws into a sprite and copies ~1 MB into
  the panel's framebuffer every frame. **ASSUMED, nothing read yet:** that the DPI scan-out is DMA with no CPU share,
  and that LovyanGFX's `Panel_DSI` can take two framebuffers. **Expected: unmeasured.**
- **Web:** the browser already presents asynchronously. The analogue is moving the canvas replay into a worker
  (OffscreenCanvas), so it overlaps the WebAssembly frame. **Only if** the 23.6" measurement misses.

**Two more levers the measurements point to.** These are not the owner's methods, and each needs his ruling.
- **L1. The per-contact cost**, 0.802 ms per aircraft on the 1.28". Most of it is the info labels: +8.5 ms at about 16
  contacts, paired A/B on 2026-08-26, because `AircraftLabelBox` walks the same fields a second time for layout. Caching
  each contact's label layout until its text changes cuts the slope. **Expected: unmeasured**; the profile (S1) sizes
  it.
- **L2. The 1.28"'s SPI clock, 40 to 80 MHz.** Computed: the push drops from 25.8 to about 13 ms. It changes the
  shipping image (one constant) and the panel's signal margin, a per-batch risk (INCOMING-INSPECTION.md). **Only on the
  owner's ruling, and only after an incoming-inspection row** on the boards to hand.

**What the 1.28" needs** is section 2a: the owner's two options, each sized against v20's numbers.

## 4. Steps

Sizes are elapsed days for the step's owner. **F** is v20's content freeze. Each step registers its predictions and its
slowdown margin before its code.

| # | step | owner | waits on | size |
|---|---|---|---|---|
| S0 | **The baseline: map-client's v19 frame-time bench** (`docs/frame-time-bench.md`, being built), measured at **20, 40 and 60 drawn aircraft on p2p**. The bar and every target in this plan are read on that one instrument, so the 2026-08-29 soak line in section 2 is context, not the baseline. | map-client | its registration | as map-client sizes it |
| S1 | **a per-phase profile on the 1.28" and the P4** (draw, push or flush, the rest of the loop), the way 5c's `/diag/perf` does it on the 2.1; in bench-only envs, so the shipping images do not move | p4 | now (A0 in section 2a is its first run); S0's instrument | 0.5 |
| S2 | **the 2.1 chapter:** 5c's registration as written (1a touch task, T0-T3; 1b `/diag/perf`; M1 sized after 1b; F5 and the chirp) | blipscope-5c | the owner's bench windows (W1, W2) | 5c's sizes: 1 + 0.25 + 0.5 d, and M1 after 1b |
| S3 | **M3 on the 1.28":** DMA push and a second backbuffer. Frame time before and after; the 240 golden and the /diag/fb frames pixel-identical | p4 (5c reads the shared main.cpp change) | S1 | 2 + a bench window |
| S4 | **M2 beyond the 2.1:** the same touch task on the 1.28" if S1 shows it needed (graded the 5c way: distinct samples, measured finger time); the pointer-event queue in the web build | p4 | S2's T1 result (the design is proven there first) | 1 (1.28") + 1 (web) |
| S5 | **M1 in the shared drawing code:** the radar's static layers (rings, cardinal labels, range text; the map if S1 shows it redrawn) built once and reused; rebuilt on a zoom, a rotation or a home change | p4, with weatherscope's note | S1's static share; big-screens step 5 (so the layer is built with the final line weights and text sizes, once) | 3 + a bench window per board |
| S6 | **L1, the per-contact label cost**, if S1 and S5 leave the 1.28" over its target | p4 | S5; **the owner's ruling** | 2 |
| S7 | **M3 on the P4** (the framebuffer path) and its profile on glass | p4 | the P4's bench rows (docs/p4-40-bringup.md), then S1 | 2 |
| S8 | **the Android screens:** the 23.6" first (big-screens step 12), then the 7" and 15.7"; the worker replay only if it misses | p4, on the owner's hardware | big-screens step 3 (run-time size); the screens | 1 to measure; 2 if the worker is needed |
| S9 | **L2, the 1.28"'s SPI clock**, only if S3 + S5 + S6 leave it short | p4 + an incoming-inspection row | **the owner's ruling** | 0.5 + the inspection |

**Order: plumbing first, then layers.** S3 and S4 change the frame's plumbing, not how any face is drawn, so they run
first and conflict with nothing. S5 rebuilds the static layers after big-screens steps 4 and 5 have set the final text
and line sizes, so the layers are built once rather than twice. The owner's 2.1 order (S2 first, then the 2x layout)
already matches it.

## 5. Owners and the date

| owner | steps | days |
|---|---|---|
| p4 (or a second session, staffing 2) | S1, S3, S4, S5, (S6), S7, S8, (S9) | 8 before the boards and screens (S1 0.5, S3 2, S4 2, S5 3.5 with its benches); +2 (S6) and +0.5 (S9) only if needed; S7 and S8 follow the hardware |
| blipscope-5c | S2 (the 2.1) | its registration's sizes |
| map-client | S0 | its registration |
| weatherscope | the static-layer note for S5 | the note |
| the owner | bench windows; the screens; rulings Q-S1 to Q-S4 | |

**The date depends on who carries the 1.28" and web work**, because p4 also owns the big-screens plan. With step 5's
touch measurement moved here, that plan stands alone at **F + 22**.
- **Staffing 1: p4 carries both.** The order is: the plumbing (S1, S3, S4: 4.5 d), big-screens steps 2-5 (8 d), S5 (3.5 d),
  big-screens steps 6-8 (8.5 d), then the map integration and the 23.6" (3-5 d).
  - **The 1.28"'s plumbing lands at about F + 5, and S5 at about F + 16.**
  - **The big screens move from F + 22 to about F + 30**, or F + 32 if S6 is needed.
- **Staffing 2: a second session carries S1, S3, S4 and S5.** Who is **the owner's choice**; nobody has been asked. This plan
  suggests **weatherscope** (blipscope-61), on three grounds:
  - v20's plan puts nothing of it on v20's path;
  - S5 is its own method;
  - it already works on the 1.28" Kit S3.

  One constraint goes with that suggestion. Its board, 6E:64, is barred from Blipscope work (CLAUDE.md's bench table),
  so the 1.28" bench steps would run on E2:08, the Blipscope bench, in windows booked with its session.
  - **The big screens stay at F + 22.**
  - The plumbing lands at about F + 5.
  - S5 starts when big-screens step 5 lands (F + 8) and ends at about F + 12; S6, if needed, at about F + 14.

The P4 (S7) follows its bench rows and the Android screens (S8) the owner's hardware; neither is on either date. **This
plan recommends staffing 2** if a session is free: the plumbing and the layers touch nothing the big-screens
layout steps do, and staffing 2 is eight days sooner for the big screens. For v20, section 2a's steps run NOW, before F, so they meet the big-screens work only if they run past F.

## 6. What the owner is asked

- **Q-S1, ANSWERED (19:55Z):** the bar is read at **40 aircraft, map and weather on**, with 20 and 60 reported
  beside it (80 cannot be shown: `MAX_AIRCRAFT` = 60). The minimum is 20 fps and the target 24.
- **Q-S2. L1 and L2,** if the methods fall short. L1 (cache each contact's label layout) changes how the labels are
  drawn but not how they look. L2 changes a shipping constant and the panel's signal margin.
- **Q-S3. Who carries the 1.28" and web plumbing:** staffing 1 or 2 in section 5 (inside the (a)/(b) choice, section 2a).
- **Q-S4. The kiosk app.** The Android numbers depend on things the web renderer does not control. They belong to the
  kiosk sessions' app, which this plan does not touch:
  - whether the WebView runs hardware-accelerated, and at the panel's full refresh rate;
  - whether the app throttles or pauses frames when idle;
  - access for a performance trace: WebView debugging over adb;
  - which web-renderer build the app loads, and how it updates;
  - whether any app UI is drawn over the canvas every frame.

  The orchestrator takes these to the owner; the answers decide whether S8 can measure at all.

## 7. Risks, named before they happen

- **The 1.28"'s baseline is old.** The soak line predates v19, the map and the weather layer. S0's baseline (map-client's v19 bench) may
  move every figure in section 3 before a step is judged.
- **Scan-out against CPU traffic.** M3 puts more PSRAM traffic beside the panel's own scan-out. On the 2.1 that is the
  same class as F5's open question (big-screens step 13). The standing rule's before-and-after frame time catches a
  slowdown; a picture fault is caught on glass.
- **M2 on the 1.28" shares the touch IC's I2C port with the touch watchdog**, which is on there (`TOUCH_WATCHDOG`, the
  s3-128 only). 5c's host test forbids the touch task and the watchdog together. So on the 1.28" the watchdog's
  re-init has to move into the touch task first (5c's hazard (a)), which is a reason S4 waits for S1's evidence.
