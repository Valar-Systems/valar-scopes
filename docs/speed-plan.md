# Speed: one plan for every board

**Status: a plan, no code.** Written 2026-11-12 19:37Z by the P4 session, for the owner, through the orchestrator;
section 2a (the owner's choice, sized) added 20:06Z and corrected 20:16Z (weather's cost, read from the record).

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
| **1.28" Kit S3** (240 px, GC9A01 SPI at 40 MHz) | **v19, map off, weather off: 74.5 ms p95 at 41 aircraft** (one early reading, map-client's map-bench image, 16:59:30Z, before its bench's control has run). Weather adds **about 13.2 ms at 40 aircraft** (2.1 + 4.6 + 0.1625 per contact; section 2a). docs/wx-device.md measured +12.2 ms p95 at 16 contacts. The map's steady cost is **unmeasured**; its rebuilds at KSFO took 177-198 ms each (#680). The 2026-08-29 fit, p95 = **35.8 ms + 0.802 ms per contact** (R² 0.985), gives 68.7 ms at 41, so v19 sits 5.8 ms above it. The cloud feed carries 40 aircraft (`BLIPS_LIMIT`) and the firmware tracks up to 60 (`MAX_AIRCRAFT`) | **24 fps** (minimum 20) | the stock-defaults soak of 2026-08-29: **44.9 h, 5,392 samples**, the frame-budget note's last block in `src/AircraftManager.cpp`. That note itself corrects an earlier "48 h". It predates v19, the map and the weather layer, so it is context only: S0 (map-client's frame-time bench) is the baseline every step is judged on |
| **2.1" Waveshare** (480 px, ST7701 RGB, 8 MHz pclk) | **9.7 fps (103 ms)**, contact count not recorded | at least 20, ideally 24 | #676 (114 frames in 11.70 s) |
| **P4 4.0"** (720 px, JD9365 DSI) | **unmeasured** (no board yet) | 24 fps (minimum 20) | none |
| **Android screens** (7" 1080, 15.7" 960, 23.6" 1280; the web build in the kiosk app's WebView) | **unmeasured** on the real screens | **pending the owner's look at the 23.6"** | none. The stage-1 rig ran the WebAssembly firmware at about 0.2 ms a frame on a desktop, without drawing; that says nothing about the WebView |

**What the 1.28" numbers already say.** The panel push is **25.8 ms** of every frame (the measured push floor, in
docs/animtest-results-2026-08-06.md and docs/weatherscope-stage3.md; 115,200 bytes at 40 MHz). Push and draw run one
after the other today, so everything else in v19's 74.5 ms at 41, about 49 ms, is update, draw and flush.

## 2a. The owner's choice, sized: (a) 20 fps at 40 aircraft in v20, or (b) v20 no slower than v19

**Its estimates are superseded by section 2c (re-sized on the measured v19 baseline, 22:53Z).** They are kept as the record
of what the owner chose from.

**The numbers this sizing stands on, and how firm each is.** map-client's frame-time bench (S0) is registered
(`docs/frame-time-bench.md`, branch `docs/frame-time-bench` at `b0c58758dd8e`). Its control and its baseline have NOT
run yet. When they have, every number below is re-read on it, and a sizing that moves goes back to the owner.

| number | value | status |
|---|---|---|
| v19 at 41 aircraft, map off, weather off | 74.5 ms p95 | one reading (map-client's map-bench image, 16:59:30Z), before the bench's control |
| weather on | **about 2.1 + 4.6 + 0.1625 n ms: 9.9 at 20, 13.2 at 40, 16.4 at 60** (corrected 20:45Z) | **read from the record** (docs/wx-device.md; weatherscope's note of 20:45Z, checked by p4): the clear is a memcpy since B1's fix, 5.08 ms against the fill's 3.01 (F2); the sweep's blend is +4.6 (6.62 against 1.99, read with one echo cell on the layer); and **every contact gets 8 black outline copies while the layer is on, 157-169 µs per contact per frame** (F7's observations). The record's "+12.2 at n = 16" fits that formula. The B1 prediction (≤ 3 ms) missed |
| the map's steady cost, M | unknown | **unmeasured**; its rebuilds at KSFO took 177-198 ms each (#680), counted in the gate's p95 |
| the push | 25.8 ms | measured floor (two docs, section 2) |
| per contact | 0.802 ms, of which labels about 0.53 | the 2026-08-29 fit and the 2026-08-26 labels A/B (+8.5 ms at ~16) |

**So v20 at 40 aircraft is about 73.7 + 13.2 + M = 86.9 + M ms p95 (11.5 fps or worse).** The bar (20 fps) is 50 ms;
the target (24 fps) is 41.7 ms.

### (a) 20 fps at 40 aircraft, map and weather on, in v20

**The owner chose (a). Its working plan, with the checkpoint and the dates, is section 2b.** The tables below are the
sizing he chose from.

It needs **36.9 + M ms** off the frame (45.2 + M for 24 fps).

| lever | expected cut at 40 | read or assumed | needed for (a)? |
|---|---|---|---|
| **M3** (the push on its own task, a second backbuffer) | up to **25.8 ms**, if the overlap is complete | READ: the push holds the loop today. ASSUMED: a push task, and DMA from the PSRAM backbuffer with a cache write-back on the S3 (section 3) | **yes**: no path to 50 ms without it. With it, the frame is about 61.1 + M |
| **W** (weather's ~13.2 ms at 40, cut further) | **unknown** | its split is READ: the clear +2.1, the blend +4.6, **the outline 0.1625 ms per contact, 6.5 at 40, its largest part**. weatherscope (P5) registers an exact, byte-identical cheaper outline first, then the blend, then the clear | one of two sources for the last ~11.1 ms |
| **M1 on the map** (its steady draw into the cached layer, with the weather) | **M** | unmeasured | yes, if M > 0 |
| **L1** (each contact's label layout cached) | part of ~21 ms of label cost at 40 | unmeasured | **yes, or W**: with M3 and the map cached the frame is ~11.1 ms over 50 (about half the label cost, if L1 alone carried it). 24 fps needs ~8.3 ms more |
| **L2** (SPI at 80 MHz) | ~13 ms of push, only while the push is not hidden | computed | **only if M3 cannot overlap.** Without M3 the frame is 86.9 + M; L2 and the map leave it at ~74.0, and W and L1 would have to find ~24.0 ms between them. So **(a) is out of reach in v20 without M3** |

**Steps, owners, sizes** (elapsed days from 2026-11-13):

| step | owner | size |
|---|---|---|
| A0: a per-phase profile of the v20 radar at 40 aircraft, map and weather on (bench-only env; map-client's bench traffic) | p4 | 1, with one E2:08 window |
| A1: M3 on the 1.28". Its first bench hour answers the DMA-from-PSRAM question | p4 (5c reads the shared main.cpp change) | 2.5 |
| A2: W, a further cut to weather's ~13.2 ms at 40 (the outline first), only where A0 shows a cheap one | the session that owns `WxLayer.h` (the orchestrator names it) | 1-2, from A0 |
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
- **Risk: HIGH.** It rests on M3 (two assumptions), on ~11.1 ms more from W or L1 (neither measured), on M
  (unmeasured), and on single readings.

### (b) v20 no slower than v19 at the same traffic: DROPPED

**The owner chose (a), 2026-11-12 20:10Z.** (b) is kept below only as the record of what was sized. Its numbers
predate the 20:45Z weather correction.

It needs about **10.7 + M ms** off v20's frame, less the margin, at **every** level, 20 included. At 20 aircraft the labels cost only ~10.6 ms, so L1 cannot carry it there, and W's further gain is unknown. **So (b) realistically needs M3 too.** With M3 complete, v20 is no slower than v19 whenever weather plus the map (10.7 + M) fits under the 25.8 ms push it hides, that is, **while M ≤ ~15 ms**.

| step | owner | size |
|---|---|---|
| B0 = A0, the profile | p4 | 1 |
| B1 = A1, M3 on the 1.28" | p4 (5c reads the shared main.cpp change) | 2.5 |
| B2 = A3, the map's steady draw cached, if the profile reads M > 2 ms (its frozen meanings) | map-client | 1-2 |
| B3 = A2, W, only where the profile shows a cheap cut | the `WxLayer.h` owner | 1-2, optional |
| B4: the measurement below: v19 twice (its repeatability) and the v20 candidate | map-client (its bench); E2:08 windows of ~75 min | 1 |

- **About 6 elapsed days** (B0 1, B1 2.5, B4 1, merges ~1.5, with B2 and B3 in parallel), landing about
  **2026-11-19**: **v20 on time**, with less slack than the first version of this sizing said. L1 and the rest of (a)
  then go to v21 with the 20 fps bar.
- **Risk: as M3's (two assumptions), plus M.** If M3 cannot overlap (known about 2026-11-16), (b) rests on W and L1
  finding ~10.7 + M ms at every level. Neither is measured, so it goes back to the owner then.
- **(a) and (b) now share their hardest step, M3.** (a) is (b) plus ~8.6 ms more from W or L1: about 2-4 more days,
  and the risk of those two levers.

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

### The owner's answer (2026-11-12 20:10Z)

"I chose (a). v20 doesn't ship until it meets the bar. p4's plan with sizes comes to me tomorrow. From it, you set
v20's new date and tell me what that does to the big-screen dates. The map and weather work carries on in
parallel." Section 2b is that plan.

## 2b. The plan for (a): steps, owners, checkpoint, dates

**The steps, owners and C1's meanings stand. The gap figures and the R1-R3 dates are re-read in section 2c**, on the
measured baseline, for the bar on LIVE-like or on FIXED traffic.

**The goal:** p95 of `p2p` **≤ 50 ms at 40 aircraft with map and weather on**, read on map-client's frame-time bench
on the v20 candidate, with 20 and 60 reported beside it. 24 fps (≤ 41.7 ms) is the target and not chased past.

**The starting point (section 2a):**
- v20 at 40 aircraft ≈ **86.9 + M ms** (weather corrected to ~13.2 ms at 40, 20:45Z);
- with M3's overlap complete, ≈ **61.1 + M**;
- the last **~11.1 + M ms** has to come from caching the map (−M), W (the outline first), and L1. That is 2.5 ms more
  than this section first said.

### What is READ and what is ASSUMED

| item | status |
|---|---|
| the push is 25.8 ms and holds the loop for all of it | **READ** (the measured floor; LovyanGFX 1.2.21's `pushSprite` path) |
| the library's DMA path on the S3 still holds its caller | **READ** (`Bus_SPI.cpp:759-771`) |
| weather's split: the clear +2.1, the blend +4.6, the outline 0.1625 ms per contact (6.5 at 40) | **READ** (docs/wx-device.md, one run, n = 16) |
| labels ~0.53 ms per contact; per contact 0.802 ms | **READ** (the 2026-08-26 A/B; the 2026-08-29 fit) |
| v19 at 41: 74.5 ms | **ONE READING**, before the bench's control |
| a push task gives the overlap; DMA from the PSRAM backbuffer with a cache write-back works on the S3; which core | **ASSUMED**, and settled by M3's first bench hour |
| M, the map's steady cost | **UNMEASURED**, and settled by A0b |
| W's further gain; L1's gain | **UNMEASURED**, and sized from A0a's phases |

### Steps

| step | what | owner | size | earliest |
|---|---|---|---|---|
| P1 | **A0a**, the profile on main (docs/frame-profile.md): P-CTRL, P0, P1 | p4 | 1 d, one E2:08 window | when map-client's bench is frozen and its CTRL has passed (about 11-14) |
| P2 | **M3**: the push on its own task and a second backbuffer. The first bench hour answers DMA from PSRAM, the cache write-back and the core | p4 (5c reads the shared `main.cpp` change) | 2.5 d to C1; **3.5 d** with M3-8 (flash writes) and M3-9 (the overnight soak), added at the owner's acceptance (docs/m3-push-task.md) | **accepted 20:34Z**; code from 11-13, C1 11-16 if the glass hour is done by the evening of 11-15 |
| **C1** | **THE CHECKPOINT, about 2026-11-16:** M3's measured cut, p95 at 40, weather on | | | after P2 |
| P3 | **A0b**, the profile with the map in the tree: P-CTRL, P2, measuring M | p4 | 0.5 d, one window | when #659 (step B) and step C are on main or in the candidate |
| P4 | **the map's steady draw into the cached layer**, if A0b reads M > 2 ms (frame-profile.md's fixed meanings) | map-client | 1-2 d | after P3 |
| P5 | **W**: only a cut A0a shows to be cheap, such as not pushing the whole 115 KB layer every frame | **weatherscope** (the owner's staffing 2, 20:34Z) | 1-2 d | after P1 |
| P6 | **L1**: each contact's label layout cached until its text changes | **weatherscope** (the owner's staffing 2, 20:34Z) | 2 d | after P1 (labels measured) |
| P7 | **the (a) reading**: map-client's bench on the combined candidate (20/40/60; the gate at 40) | map-client | 1 d, one ~75 min window | after P2, P4, P5, P6 |

Each step also carries the standing rule's before-and-after bench run (about 0.25 d) and its PR's review, CI and
merge by rule (about 0.5-1 d). All the 1.28" bench steps run on **E2:08**, the Blipscope bench, in windows booked with
its session and map-client's bench schedule. Weatherscope's 6E:64 is barred from Blipscope work, so it is not used.

**What C1 reads, and what each result means (fixed now):**
- **M3 cuts ≥ 20 ms:** on track. P4-P6 proceed as sized.
- **M3 cuts 10-20 ms:** partial (the push task contends, or the overlap is incomplete). L2 becomes the next lever,
  because it shortens the push the task has to hide. It goes to the owner at C1 with an incoming-inspection row.
- **M3 cuts < 10 ms:** M3 has failed. Without it, the frame is 86.9 + M; L2 takes ~12.9 ms off, and W and L1 would
  have to find ~24.0 + M ms between them. **The levers beyond L2 change what is drawn,** so each is his ruling:
  - **L3, a cheaper sweep blend** (up to −4.6 ms; the blend's look);
  - **L4, less label and trail work per frame**, for example labels redrawn only when their text changes, or culled
    where they overlap (the labels' look).

  The date then waits on those rulings.
- **Outside the set** (M3 makes the frame SLOWER, or the picture changes on the 240 golden): stop, and report to the
  orchestrator.

### Staffing, and when (a) lands

| | staffing 1: p4 carries P1, P2, P3, P6 | staffing 2: a second session carries P5 and P6 |
|---|---|---|
| p4's chain | P1 1 + P2 2.5 + P6 2 + merges ~1.5 + P7 1 = **~8 d** | P1 1 + P2 2.5 + merges ~1 + P7 1 = **~5.5-6 d** |
| (a) lands, if C1 is "on track" | **about 11-21 to 11-22** | **about 11-19 to 11-20** |

**The owner chose staffing 2 (2026-11-12 20:34Z):** p4 carries P1-P3, and **weatherscope carries P5 (W) and P6
(L1)**. The E2:08 constraint above applies. P1's profile measures the phases P5 and P6 need (`under`, `sweep`, `static`,
`labels`, `contacts`), so weatherscope reads them from P1 rather than measuring them a second time.

### v20's date range (docs/v20-plan.md: F, the content freeze, November 21-25; the cut 4-5 days after F)

| case | when it is known | (a) lands | F | v20 promoted |
|---|---|---|---|---|
| **R1**: C1 on track; P4-P6 find the rest at the first try | 11-16, confirmed at P7 | 11-19 to 11-22 | **unchanged, Nov 21-25** | **Nov 25-30** |
| **R2**: C1 on track; W and L1 need a second round (+3-4 d) | P7, about 11-21 | 11-23 to 11-26 | **Nov 23-26** | **Nov 27 - Dec 1** |
| **R3**: C1 partial or failed; L2 (+1-2 d with the inspection) and his rulings on L3 and L4 | **C1, about 11-16** | about 11-28 to 12-05 | about Nov 28 - Dec 5 | **about Dec 3-10, or later**: it is not sizable until he rules on L3 and L4 |

**R3's date is a range, not a promise.** C1 (about 2026-11-16) is when it becomes known, and when the owner is asked.

### The big-screen dates under each staffing

The big screens start at F, and alone take **F + 22** (docs/big-screens.md).
- **Under (a)**, the 1.28" speed work happens before F, as v20 work.
- **After F**, the speed plan's remaining items are S4 (the touch task and the web pointer queue, 2 d) and the rest of
  S5 (static layers across the boards, ~2 d). S7 (P4) and S8 (Android) follow their hardware.
- **Staffing 1** (p4 carries those too): F + 26.
- **Staffing 2:** **F + 22**, with the second session carrying them.

| case | F | big screens, staffing 2 (F + 22) | big screens, staffing 1 (F + 26) | first glass review, staffing 2 (F + 17) |
|---|---|---|---|---|
| R1 | Nov 21-25 | **Dec 13-17** | Dec 17-21 | Dec 8-12 |
| R2 | Nov 23-26 | **Dec 15-18** | Dec 19-22 | Dec 10-13 |
| R3 | about Nov 28 - Dec 5 | **about Dec 20-27** | about Dec 24-31 | about Dec 15-22 |

### The risk, stated plainly

**High, and front-loaded.** The plan rests on M3: two assumptions, settled in its first bench hour. It also rests on
~11.1 + M ms more from W, L1 and the cached map, none of them measured yet, and on single readings until map-client's
baseline runs. Everything that could sink (a) is measured by **C1, about 2026-11-16**. The plan is built so the owner
hears the bad case then, not at pre-flight.

## 2c. (a) re-sized on the MEASURED v19 baseline (2026-11-12, 22:53Z): this supersedes the estimates in 2a and 2b

map-client's v19 baseline is measured: `baseline_b.out`, sha `29d24028cec9`, read by p4 from the file itself.
- **Every p95 carries ± 1.54 ms.** That is I-6's measured noise: the bench's own control read +11.54 against +10.0 ±
  1.5, was recorded RED, and was ruled noise.
- The loop's tail is negligible: `p2p` − `fr` is +0.04 to +0.06 ms in every scene.

This replaces the single 74.5 ms reading and the 2026-08-29 fit, which under-predicts this bench at 40 and at 60.

**v19 measured, p95 of `p2p`, map and weather off:**

| traffic | 20 drawn | 40 drawn | 60 drawn |
|---|---|---|---|
| FIXED (packed: every aircraft inside 8 mi), six scenes each | 53.4-56.4 (median 55.0) | **76.55-81.83 (median 77.85, worst 81.83)**, about 12-13 fps | 102.3-110.4 (median 105.4) |
| LIVE, real traffic | Bend, median 16 drawn: 47.68 | KSFO, median 38 drawn: **61.51** (15.6 ms under FIXED KSFO/40/25) | (none) |

- **The per-contact cost is measured** at 1.14 ms (FIXED, 20 to 40) and 1.38 ms (40 to 60), against the old fit's 0.802.
- On LIVE it is 0.63 ms (16 to 38 drawn), so **LIVE at 40 ≈ 62.8 ms**, scaled from 38 on LIVE's own slope.
- **The packing is the difference.** On the FIXED scenes, labels and overlap work crowd into 8 mi.
- **Which traffic the bar is read on is the owner's decision**, and it decides (a).

**v20 at 40 = v19 + weather (2.1 + 4.6 + 0.1625 × 40 = 13.2 ms, measured; section 2a) + M:**

| bar read on | v20 at 40 | with M3's overlap complete (−25.8) | still to find for 20 fps (≤ 50 ms) | for 24 fps (≤ 41.7) | without M3, after L2 (−12.9) |
|---|---|---|---|---|---|
| **FIXED, worst scene** (S5, 81.83) | 95.0 + M (10.5 fps) | 69.2 + M | **19.2 + M** | 27.5 + M | 32.1 + M short |
| **FIXED, median** (77.85) | 91.0 + M (11.0 fps) | 65.2 + M | **15.2 + M** | 23.6 + M | 28.1 + M short |
| **LIVE-like** (62.8 at 40) | 75.9 + M (13.2 fps) | 50.1 + M | **0.1 + M** | 8.5 + M | 13.0 + M short |

All carry ± 1.54 ms (I-6) and weather's ± 2.5 ms. M, the map's steady cost, is still unmeasured: A0b reads it.

**What it means, plainly:**
- **On LIVE-like traffic, (a) is about M3 plus the map.** With M3's overlap complete, the frame lands at the bar, short
  by only the map's cost and a tenth of a millisecond. The map cached (P4) and a part of W's outline cut (P5) cover
  that. 24 fps needs ~8.5 + M more, from W and L1.
- **On FIXED traffic, (a) needs M3 and then 15-19 ms more, plus M.** W's whole ceiling is the outline (6.5) + the blend
  (4.6) + the clear (2.1) = 13.2 ms, and no realistic cut removes all of it. So the rest must come from L1, on a
  per-contact cost now measured at 1.14-1.38 ms. **(a) on FIXED is at the edge of what the methods and L1 can do.** The
  levers past them (L3, a cheaper sweep blend; L4, less label and trail work per frame) change the look, so they are
  his rulings.
- **Without M3, (a) is out of reach on either traffic** (13-32 ms short after L2).

**C1's meanings are unchanged** (the owner fixed them). What the larger gap does to the dates:

| | bar on LIVE-like traffic | bar on FIXED traffic |
|---|---|---|
| **R1** (C1 on track; the rest found at the first try) | **holds: v20 promoted Nov 25-30.** After M3, only ~0.1 + M is left, covered by P4 and part of P5 | **NOT reachable.** Even with M3 complete, 15-19 + M ms remain, more than one round of P4-P6 can be relied on to find |
| **R2** (C1 on track; a second round for W and L1, +3-4 d) | holds as the fallback: Nov 27 - Dec 1 | **the best case:** W must deliver most of its ceiling and L1 the rest; **Nov 27 - Dec 1, only if both do** |
| **R3** (C1 partial or failed; L2, then L3/L4 rulings) | about Dec 3-10, if M3 fails | **the realistic case:** L3/L4 likely needed even with M3 on track. **About Dec 3-10 or later**, not sizable until he rules on L3/L4 |

**The owner's decision that most moves v20's date is which traffic the bar is read on.** On LIVE-like traffic the plan
as sized holds (R1). On the packed FIXED scenes, planning on R3 is the honest reading. map-client's caveat stands: the
FIXED scenes pack every aircraft inside 8 mi, which is harsher than a real board.

## 3. The methods, and what each is expected to gain

**M1. Static layers drawn once and reused** (the owner's first method; the Weatherscope's).
- **Basis, measured:** weatherscope's static-layers note (2026-11-12, read-only at main `e4b41fff9042`), from
  docs/weatherscope-stage3.md:
  - a composed screen copied each frame: **41.5-42.9 ms → one 115,200 B memcpy, 5.35-6.73 ms**;
  - a loop frame: **42.4-50.3 ms → 32.3-34.5 ms**, of which about 3.1 ms was a separate band-row cut;
  - a one-off **20 ms** layer build; PSRAM only.

  On the radar, the weather layer is already cached that way: steady frames copy it instead of filling black. Its B1
  prediction (on − off ≤ 3 ms) missed; the measured cost is in section 2a.
- **The note's cautions for the radar, which this plan adopts:**
  - **Measure first.** Weatherscope's gain came from its slow software raster. The radar draws rings, text and airports
    with LovyanGFX's native primitives, which are likely far cheaper, so P1 times them before anything is designed.
  - **Keep the order.** The rings draw after the sweep, on top of it. Folding them into the weather base would put them
    under it, a visible change and a ruling. The look-keeping form is a palette-index layer replayed after the sweep.
  - **Exactness needs opaque writes:** no smooth font, no alpha, nothing that reads what is beneath.
  - **A key:** location, view radius, rotation, theme or look, `displayAirports`, and the airport list's version.
  - **No cached layer may contain the screen-indicator dots** (`DrawScreenIndicator`, y = 224). Ruled by the
    orchestrator (~21:48Z), relayed by map-client. map-client's /diag/fb frame-trust test reads those dots as the
    witness that a grab is a finished frame: they are drawn last except the clock, and the frame-start clear replaces
    every pixel. A cached layer, background or plate copied in at a frame's start that held them would let a torn frame
    pass as finished. Any change that needs the indicator in a cached layer tells map-client first, so a new
    last-drawn witness is registered before it lands (docs/blipscope-map-client-steps.md, "The trust test from the
    draw order", on feat/map-client-step-c-blend). This covers S5 here, and P5 (W) and P6 (L1).
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
