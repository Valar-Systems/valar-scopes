# The Pro 2.1 for v21: speed and swipes first, then the ring and the chirp (registration; nothing built or run)

**The owner's plan** (2026-11-12, relayed by the reviewer at 19:26Z), in his order:
1. "Speed and swipes first: they're what stands between the 2.1 and sale." "Test reading touch in its own fast task
   first. It may fix swipes on its own." "Then measure where the frame time goes, and apply the Weatherscope's method:
   static layers drawn once and reused."
2. "Then the 2x layout from the big-screens work, the Connect QR included." That is p4's plan, step 7. p4 takes step 1's
   results as its 2.1 baseline.
3. "Then the ring's ~2 mm shift (the panel's vertical timing) and the new-contact chirp default (my proposal: off on
   speaker models)." The chirp default stays his proposal until he rules.

Steps 1 and 3 are this page. **Nothing merges before v20's cut, and v20 wins any conflict for time.** Bench flashes of
the 2.1 (label CF:20, hub port 1) need a window. Every fact below is read at main `1e6f2c13` and LovyanGFX 1.2.21,
the version this env builds.

## Where things stand (from #676's record)

- **9.7 frames/s, 103 ms per frame**, measured (114 frames in 11.70 s).
- **Touch is read once per frame on the loop task.** `HandleTouch()` (`src/AircraftManager.cpp:9501-9516`) calls
  `tft.getTouch()` "every loop". So at 9.7 fps a quick swipe gets one or two samples.
- The owner: "almost impossible to swipe".

## Step 1a: touch in its own fast task

**The design:**
- **A variant flag**, `variant::TOUCH_TASK`, true on `s3_21` only. The 1.28" and every other build are unchanged until
  measured; their images must read PLACED against today's (the five-region comparator).
- **A FreeRTOS task on the loop's core** (core 1), one priority above the loop. It calls `tft.getTouch()` every 10 ms
  and pushes `(touched, x, y, ms)` onto a queue.
- **`HandleTouch()` drains the queue each frame** and feeds every sample, in order, to the unchanged
  `ProcessTouchSample()`. The gesture classifier is not touched.
- **After `setup()`, the task is the only `getTouch()` caller.** The setup-phase callers
  (`include/WiFiManagerHelpers.h:285`, `:335`, the boot and portal screens) all run before the task starts.

**Hazard (a), read: I2C port 0 is shared.** On the 2.1 the touch controller, the TCA9554 expander and the QMI8658 IMU
share port 0 (`include/variants/s3_21.h:67-68`; `src/board/board_s3_touch21.cpp:32`). Today they are all called from
the loop. A touch task makes two callers, so:
- **Transactions are serialised per port by LovyanGFX itself.** `lgfx::i2c::beginTransaction` takes the port's FreeRTOS
  mutex (`lgfx/v1/platforms/esp32/common.cpp:1612-1621`; the mutex at `:1017-1037`, one per port at `:1113`).
  `endTransaction` releases it through `i2c_wait` (`:1686-1690`, `:1311`). `transactionWriteRead` is a begin/end pair
  (`:1830-1841`).
- **Every 2.1 bus user goes through that pair:** the CST touch driver (`lgfx/v1/touch/Touch_CSTxxx.cpp:40-50`, `:102-122`)
  and the board code (`board_s3_touch21.cpp:62-64`, `:117`, `:126`).
- **NOT covered: `lgfx::i2c::init` takes no lock.** It releases and rebuilds the port (`common.cpp:1406-1460`), so a
  re-init during the other task's transaction would break it.
  - Today every init runs in `setup()`: `BoardPreInit` (`board_s3_touch21.cpp:90`) and `tft.init()`.
  - The one runtime re-init path, the touch watchdog's driver re-init, is off on the 2.1 (`TOUCH_WATCHDOG = false`,
    `s3_21.h:84`).
  - **The rule, with a check that runs:** the task starts only after `setup()`'s inits, and a host test asserts that
    `TOUCH_TASK` and `TOUCH_WATCHDOG` are never both true. If the watchdog is ever wanted on the 2.1, its re-init has
    to move into the touch task.

**Hazard (b), read: never gate touch on the HTTP bus mutex** (CLAUDE.md, "Memory, networking and touch", item 2;
`HandleTouch`'s own comment at `:9505-9513`). The task polls freely and takes no network lock.

**The instrument** (the s3-21 env only, behind the same flag): `/diag/touch`, the last 32 gestures, each with its
sample count, finger time in ms, |dx|, |dy|, and how it was classified (tap / swipe / none). It is read over Wi-Fi, so
no serial port is ever opened on this board (a CH343 auto-reset hazard).

**The run: the owner's hands, about two minutes per build, in one window.** He makes 10 quick swipes (finger time
under 200 ms) and 10 slow ones (over 400 ms), alternating left and right, on the radar face. Then `/diag/touch` is read.

| row | build | predicted |
|---|---|---|
| **T0** | today's loop poll, the instrument on, the task off | quick swipes: **1-2 samples**, recognised as a swipe in **at most half**; slow swipes: 4-6 samples, recognised in **8 or more of 10** |
| **T1** | the task on (100 Hz) | quick swipes: **10 or more samples**, recognised in **9 or more of 10**; slow swipes: 40 or more samples, 9 or more of 10 |
| **T2** | the same, frame time | **at most +5 ms** against T0 (a touch read at 400 kHz is about 1 ms, 100 a second) |
| **T3** | the 1.28" images built with the flag off | PLACED against today's (no code byte moves) |

**Outside the set:** many samples but swipes still not recognised means the classifier, not the sampling. Then stop
and report; no threshold is changed under this step.

## Step 1b: where the frame time goes

The instrument is `/diag/perf` (the s3-21 env, the same flag): mean and max per phase over 100 frames. The phases are
draw (into the PSRAM sprite), `pushSprite` (the sprite into the driver's PSRAM framebuffer, `src/main.cpp:982`),
`DisplayFlush` (present, and wait for the frame-complete semaphore, `Panel_ST7701_esplcd.cpp`), and the rest of the
loop.

**The candidates, each with a number to be wrong about:**
- **The flush wait.** At an 8 MHz pixel clock the panel refreshes at about 29 Hz (`s3_21.h:28-32`), so presenting can
  wait up to one refresh, about 34 ms. Predicted mean: 10-35 ms.
  - **The pclk's stated reason is gone.** The comment says "we scan the PSRAM framebuffer without a bounce buffer", but
    the driver scans through one (`Panel_ST7701_esplcd.cpp:5`, `BOUNCE_LINES = 20` at `:31`). So the pclk is a measured
    candidate here, not something to raise on the comment's word.
- **`pushSprite`:** a 460,800-byte PSRAM-to-PSRAM copy. Predicted 15-35 ms.
- **Draw:** the sweep's gradient and the contacts, into a 480x480 PSRAM sprite. Predicted 30-60 ms.
- No single phase is predicted above 60% of the frame.

**Then the Weatherscope's method**, static layers drawn once and reused (the rings, the cardinal labels, the range
text), sized after 1b's numbers and registered then, before its code.

## Step 3: the ring's shift, and the chirp

**F5, the ring wrapped about 20 lines** (#676). It is scan-out, not mechanical. Two alternatives stay open:
- **(i) Fixed or time-varying:** the owner's second photo later today, and one after the board's next natural restart.
  The board is never restarted for this.
- **(ii) Bounce phase or porch:** the exact wrap, read from a numbered-row test frame (the s3-21 env, the flag, a bench
  window). **A whole multiple of 20 lines** (`BOUNCE_LINES`) points at the bounce buffer's phase. **Any other count**
  points at the vertical porches (`s3_21.h:57-59`; `LGFX.h:86` `offset_y = 0`) against the ST7701's own.

No fix is chosen until both are read.

**The chirp:** the owner's proposal is new-contact chirps off by default on speaker models, keeping the alert patterns.
- It waits for his ruling.
- If ruled: one default changes (`ConfigurationWebServer.cpp:2826`, and `AircraftManager.cpp:1209`'s read of it).
- **No ConfigMigration entry** is needed: `HAS_AUDIO` is true only on s3-146 (retired), the 1.75" AMOLED and s3-21, and
  none of them ships.

## Size, and the order inside step 1

| item | size |
|---|---|
| 1a: the task, the flag, the instrument, the host test; T3 locally | 1 d |
| 1a: T0-T2 in one bench window, with his hands | 0.25 d |
| 1b: `/diag/perf` and its reading | 0.5 d |
| 1b: the static-layer method | sized after 1b's numbers |
| 3: the numbered-row test frame and its reading | 0.5 d |
| 3: the chirp default, if ruled | 0.25 d |

**CI for this registration (docs only):** firmware 30 (28 success / 2 skipped), device-ids 1, no-traffic-captures 1:
**32 = 30 / 2 / 0**.

## Amendments 1-8 (the reviewer, 19:31Z, accepted with these; frozen before any code)

**A1. Distinct samples, not reads.**
- `/diag/touch` prints both **reads** (every poll) and **distinct samples**: a change in x, y or the touched state since
  the previous read. **T1 is graded on distinct samples.** Counting reads would count one stale report many times
  (map-client's CF5, today).
- **The ceiling is the controller's own report rate.** The CST820 is CST816-compatible (`s3_21.h:67`). In the CST816
  family the normal-scan period is register `0xEE` (NorScanPer), in units of 10 ms, default 1.
- **Predicted:** `0xEE` reads **1**, so the ceiling is **100 reports/s**. The task reads `0xEE` once at start and
  `/diag/touch` prints the value.
- **T1's floor follows from the value read, by a formula frozen now:** a graded quick swipe needs at least
  `max(5, 0.5 x rate x finger time)` distinct samples, where rate = 100 / (0xEE value) per second. At 100/s, a 120 ms
  swipe needs at least 6. If `0xEE` reads other than 1, the formula takes it, and nothing is re-argued after the run.

**A2. Graded by MEASURED finger time** (`/diag/touch` records it per gesture), never by intent:
- quick means measured under 200 ms; slow means over 400 ms;
- 200-400 ms is **excluded** from grading and reported as its own count;
- an intended-quick swipe measured at 200 ms or more moves to its measured bucket, or out;
- a bucket with fewer than 6 graded gestures is **ungraded** (not enough data), and the owner repeats that bucket in
  the same window. That rule is fixed now, not improvised.

**A3. The queue:**
- depth **64** distinct samples;
- `/diag/touch` prints an **overflow counter** and the **high-water mark**;
- the worst frame seen on any board today is about 200 ms (map-client's KSFO map rebuild), about 20 samples at 100/s,
  and 64 covers three of those;
- **predicted: 0 overflows over the whole run, and a high-water mark of at most 30.**

**A4. T3's mechanism, and its prediction.**
- **Gating:** all new code (the task, the queue, `/diag/touch`, `/diag/perf`) sits under `#if BLIPSCOPE_TOUCH_TASK`, a
  macro defined only in `s3_21.h` beside its `constexpr` flags. It is a macro and not only `variant::TOUCH_TASK`
  because an `if constexpr` branch still compiles its functions, and gc-sections keeps their `.eh_frame`, so bytes
  move (the gc-sections-keeps-eh-frame lesson). This departs from CLAUDE.md's "variant flags, not `#ifdef`s" for one
  reason, image identity on every other SKU, and says so in the code.
- **The classifier's clock (A8):** three lines change in place: `millis()` becomes `TouchNow()` at
  `AircraftManager.cpp:9538`, `:9727` and `:9781`. `TouchNow()` is an inline that returns `millis()` on every build
  without the macro.
- **Line shifts are harmless here:** no `__LINE__`, `assert` or `log_*` follows the insertion points
  (`AircraftManager.cpp`, after line 9501: 0 uses; `ConfigurationWebServer.cpp`, after the first `/diag/fb` route at
  `:4322`: 0 uses). So line shifts move only debug information, which lives in the ELF and not in the image.
- **T3 predicted:** `blipscope-s3-128` and every other non-2.1 env rebuilt before and after the change read **PLACED**
  with the five-region comparator.
- **Stop if:** a byte moves outside the five regions. Then the mechanism above is wrong, and that is reported before
  anything else.

**A5. T0 is the control, read FIRST.**
- T0 must read as predicted, judged from the T0 rows alone, **before T1's build is flashed**.
- **The general escape hatch, beside the classifier row:** any reading outside these tables means **stop**. The
  instrument is in doubt, and nothing after it is read as a result.

**A6. The host test, shown failing first.** `test/host/test_touch_task_flags.cpp` includes each variant header and
asserts that the touch task and the touch watchdog are never both on. It is shown red **first**, on a planted copy of
`s3_21.h` with `TOUCH_WATCHDOG = true` (the plant asserted to have landed), then green on the real headers.

**A7. The chirp's "no migration" premise is read from the field before it is relied on.** A default reaches only keys
that were never saved, and the bench 2.1 already has saved ones (the owner turned tones off). If the default is ruled,
first a fleet read, **model counts only**, scoped before it runs: how many `fw:` rows carry a speaker model (`s3-21`,
`s3-175-amoled`, `s3-146`), with the bench label's list subtracted.
- **Predicted:** 0 outside the bench.
- **Any other count:** the change needs a ConfigMigration entry, which goes to the owner.

**A8 (mine, found while amending): the classifier's clock.** `ProcessTouchSample` takes its time from `millis()` at
the moment it runs (`AircraftManager.cpp:9538`; also `:9727` and `:9781`, the tap-suppression window). Replaying queued
samples in a burst at frame time would stamp them all with one time and collapse every gesture's duration. So each
queued sample carries its own read time. During the replay `TouchNow()` returns that time, and elsewhere it returns
`millis()`. There is still one classifier and no second path.

## The bench session plan (the reviewer schedules it with the owner)

| window | what | elapsed | the owner's hands |
|---|---|---|---|
| **W1** | flash T0 (app-only at `0x10000`, NVS kept, so his Wi-Fi, location and key survive; identity by the CH343 + `read-mac` CF:20); check the boot over Wi-Fi | ~5 min | none |
| | **T0:** his 20 swipes, plus any bucket that came out short (A2); read `/diag/touch` and `/diag/perf` | ~4 min | ~3 min |
| | T0 judged (A5). Only if it matches: flash T1 the same way, and check the boot | ~5 min | none |
| | **T1:** his 20 swipes, plus repeats; read | ~4 min | ~3 min |
| | restore the v19 release app, app-only, read back with `verify-flash` | ~4 min | none |
| **W2** | flash the numbered-row frame (F5 ii) | ~5 min | none |
| | **his one photo** of the glass; restore | ~6 min | ~1 min |

T3 and the host test are local, with no board. Port 1's power cycle is for recovering the 2.1 from download mode
only, logged.

## The frame-time rows (the owner's standing rule, 2026-11-12)

"every change to drawing code reports its frame time on the bench, before and after. A slowdown beyond the margin
frozen in its registration stops it."
- **Step 1a, frozen margin: T2.** T1's frame time is at most T0's + 5 ms. Beyond that, 1a stops and goes to the owner
  with both numbers.
- **Step 1b (the static layers), frozen margin:** registered with its code. Its aim is the owner's 2.1 target, "at least
  20 (aiming for 30)" fps on glass; today it is 9.7.
- **The 1.28" builds:** T3 (PLACED) is their row. No byte moves, so no frame time can change.

After v20's freeze these steps become the 2.1 chapter of p4's speed workstream for all boards.
