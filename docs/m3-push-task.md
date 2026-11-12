# M3 on the 1.28": the push on its own task (registration; nothing built or run)

**Written 2026-11-12 20:24Z by the P4 session.** This is P2 of docs/speed-plan.md section 2b, the step checkpoint C1
reads. **No code before the owner accepts the plan and chooses staffing** (the orchestrator, 20:22Z). This is the
registration only, so that the code can start the moment he does. Every prediction is frozen here, before any code.

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
| the loop's text metrics and `getTouch` on the shared `tft` object do not race the push task's transaction state | **ASSUMED**, read in LGFXBase before the code |
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

**Who judges:** the owner, live on the glass, plus **a slow-motion phone video of each 60 s run** (a 240 fps mode if his
phone has one). The counters can then be read frame by frame afterwards: p4 grades the video, and the owner's eye
decides anything the video cannot show. **This is a window with his time,** about 15 minutes for the base and (i),
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
| **M3-0** | every non-1.28" image, including `blipscope-s3-128-framebench` | PLACED (five regions) |
| **M3-1** | the picture: the 240 golden (web build, which never takes the macro) and /diag/fb on the bench | **pixel-identical** to the base, frame for frame at the bench's fixed scenes |
| **M3-2** | the design hour on glass (section 4), against the base | **no stale band; tearing no worse than the base; no new shear**, for (i), or (ii) if (i) shows a stale band |
| **M3-3** | `p2p` p95 at 40, weather on, map off: M3's cut | **the C1 row** (speed-plan 2b): ≥ 20 ms on track; 10-20 partial; < 10 failed |
| **M3-4** | the push task's CPU per frame | (i) < 1 ms; (ii) 2-5 ms |
| **M3-5** | networking and memory beside it, over the scene: fetch `requestMs` p95, the poll gap, internal free heap and its largest block at boot | requests within **±20%** of the base; **`CanHandshake()` never false**; internal free heap lower by **4.5 ± 1 KB** for (i), or **12.6 ± 1.5 KB** for (ii). Outside: the push task contends with Wi-Fi, or the memory is not what section 3 says, so stop |
| **M3-6** | the push itself, timed in the task | **25.8 ± 1.0 ms**, unchanged (the wire time) |
| **M3-7** | the standing rule: `p2p` p95 at 20 and 60 aircraft too | **no slower** than the base at either, beyond the bench's margin |

**OUTSIDE THE SET:** a slower frame anywhere, a changed picture, a torn or stale stripe that (ii) does not cure, or
Wi-Fi contention. Each stops M3 and goes to the orchestrator, and C1 reads as "failed" until the reason is known.

## 7. Size

| item | size |
|---|---|
| reading LGFXBase's shared state (the last ASSUMED row that code depends on) | 0.25 d |
| the code: the macro, the second sprite, the task, the hand-off, FrameDone and SetPhase moved, the `fr` marking, the host check | 1 d |
| the design hour (section 4), on E2:08, with the owner's eyes and a slow-motion video | 0.25 d (one window with his time, ~15 min; +10 min for (ii)) |
| design (ii), only if (i) shows a stale stripe | +0.5 d |
| the bench read (M3-0 to M3-7) | 0.5 d (one window) |
| review, CI, merge by rule (blipscope-5c reads the shared `main.cpp` change) | 0.5 d |

**2.5 days**, or **3 days** if design (ii) is needed, which is speed-plan 2b's P2 size plus the half day.
