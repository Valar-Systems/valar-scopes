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
