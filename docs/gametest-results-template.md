# Missileer game bench — results

Three numbers, produced by `[env:gametest-s3-128]` (`src/gametest_main.cpp`).
They gate build task 1 in [missileer-game-design.md](missileer-game-design.md) §13.

```
pio run -e gametest-s3-128 -t upload --upload-port COM119 -t monitor
```

**Pin the port.** A second board is usually attached; auto-detection has already
put an image on the wrong one once.

Board: _____________ (MAC / COM)  ·  Date: _____________  ·  Firmware: `gametest-s3-128`

---

## The three numbers

| # | Question | Result | Feeds |
|---|---|---|---|
| 1 | Dropout-free % of a 10 s static hold | **______ %** | whether the deputy hold gesture exists at all |
| 2 | Max simultaneous touch points | **______** | design assumes 1; >1 would allow a two-hands-one-device variant |
| 3 | NTP sync uncertainty | **______ ms** | one half of the scoring floor |
| 4 | Key-window poll interval | **______ ms** | the other half — measured inside a focused arm-C poll window, NOT the idle loop |

---

## 1. HOLD TEST — deputy switch-hold

Run **≥10 holds per arm** — the harness's own on-glass denominator is
`RUNS_PER_ARM = 10` ([gametest_main.cpp](../src/gametest_main.cpp)). This asked for 20,
so arm B "stopping early" at 10 was the harness working as designed and the
template disagreeing with it. A run is *clean* when it reaches 10 s with zero dropouts.

> **FORCE THE ARM. Do not wait to tap it.** Arms B and C have never been measured
> on the corrected build: the 26.8 h session of 2026-08-05 0855 logged `B runs 0`,
> `C runs 0` because cycling needs a tap in the top 50 px and not one of its 14
> touch samples landed there. Build with the arm pinned, and switch over serial:
>
> ```sh
> PLATFORMIO_BUILD_FLAGS="-DGAMETEST_FORCE_ARM -DGAMETEST_ARM=1" \
>   pio run -e gametest-s3-128 -t upload --upload-port COM119 -t monitor
> # then: send 'a', 'b' or 'c' over serial to switch arms live
> ```
>
> Runs from that build log `forced,1` on the `REG,arm,…` line. **Report them as
> forced** — a forced arm is fine for measuring the arm, and is not fine for
> measuring how often a user reaches it.

> **The A/B is inverted on this board — read this before recording anything.**
> The brief assumed "product config" = auto-sleep ON. This board is a CST816**D**,
> which [INCOMING-INSPECTION.md](../INCOMING-INSPECTION.md) records as shipping
> **`0xFE`=1, i.e. auto-sleep already DISABLED**. So the factory arm *is* the
> no-sleep arm, and the control arm has to deliberately **arm** sleep (`0xFE`=0 —
> the value the inspection doc calls REJECT), held only for the run.
> `AutoSleepTime` (`0xF9`) sits at 2 s underneath, so a 10 s hold against a 2 s
> timer is the collision worth measuring.
>
> ~~Long-press on the HOLD screen flips the arm.~~ **Wrong, and it was wrong when
> written** — this template last changed at 13:33 on 2026-08-04 and the fix that
> settled the mechanism landed at 14:09 the same day. A long-press cannot be the
> arm control: on the HOLD screen every press starts a hold run by definition, so
> "press and hold to change arm" and "press and hold to test" are the same
> gesture. It is a **tap on the arm chip** in the top band — or, preferably, the
> forced-arm build above.
>
> Every arm change logs a `REG,arm,…` line with `honoured,0|1` — **if `honoured`
> is 0 the chip refused the write and that arm's numbers describe an unknown
> state.** Record it as void, not as a result.

| Arm | `0xFE` | Source | Runs | Clean | **Clean %** | Dropouts | Longest (ms) |
|---|---|---|---|---|---|---|---|
| **A** factory | 1 | driver `getTouch` | | | | | |
| **B** sleep armed | 0 | driver `getTouch` | | | | | |
| **C** chip poll | 0 | chip `TouchNum` | | | | | |

- **A − B = auto-sleep tax** — what the sleep engine costs a static hold: ______ pts
- **C − B = chip-poll rescue** — what bypassing INT recovers under the *same* hostile
  config: ______ pts
- **Bus failures (`BUS_FAIL`)**: ______ runs voided

> **C is the result that matters most.** It runs under the hostile sleep config on purpose:
> if direct register polling holds clean with the engine armed, it holds clean everywhere.
> A clean C means the deputy gesture is solved by a **game-mode poll window** that ignores
> INT entirely — and the shipping product never writes a touch register, keeping the exact
> factory config the incoming-inspection gate checks for.
>
> `BUS_FAIL` is counted separately and never folded into dropouts: a wedged bus is an
> equipment fault, and averaging it into a touch-quality number would corrupt the figure the
> gate is read from. A run with >100 ms of consecutive NACKs is voided, not scored — holding
> the last known state forever would report an eternal flawless hold, which is the opposite
> artifact arm C exists to rule out, in the direction that looks like success.

Dropout histogram (ms): `<10` ___ · `<25` ___ · `<50` ___ · `<100` ___ · `≥100` ___

Poll cadence — avg ___ ms, max ___ ms.
**Sampling floor: a dropout shorter than the max poll gap is invisible.** If the
max gap approaches the smallest bucket, the clean result is not trustworthy.

### Driver vs chip disagreement

`TOUCH,` lines carry both `driver_touched` and `chip_touchnum`. A static finger
that generates no *change* interrupt can read as no-touch at the driver while the
chip still holds the point — `IrqCtl` (`0xFA`) is `0x60` = `EnTouch|EnChange`.

Disagreements seen: ______  → if non-zero, **the dropout is INT-gating, not
auto-sleep**, and the fix is a driver/IRQ change rather than a register write.

**Verdict:** ☐ hold gesture is viable ☐ viable only with auto-sleep off ☐ not viable — redesign the crew layer

---

## 2. MULTITOUCH PROBE

`MAX POINTS SEEN` after deliberately pressing with 2–3 fingers: **______**

- **1** → expected. The two-hands rule needs two *devices*, as §13 assumes.
- **>1** → a single-device variant becomes possible; revisit the crew design.

---

## 3. NTP TEST

~~Leave running **≥30 min**; longer is better.~~ **A 30-minute run produces NOTHING.**
Corrections arrive on a **3-hour** cadence (measured: sync timestamps 10,800,000 ms
apart in both sessions), and the first drift figure needs a *second* sync — so 30
minutes yields zero corrections and the harness correctly reports `-1`, UNMEASURED.
This instruction could never have produced the number the table below asks for.

**Leave running ≥24 h**, which is ~8 corrections. And note what 8 is worth: the
published floor is the **max** of the observed |adjustment|, because a quantile over
8 points interpolates between the top two values and returns a number with no
sampling basis. Record the sample count next to the figure, always.

⚠️ **The worst correction is a running maximum and has not converged.** The firmware
now raises `*** CLOCK FLOOR EXCEEDED ***` when a live correction exceeds the published
floor (199 ms). Re-rule trigger: **three alarms, or any single correction above 250 ms** —
at that point extend `test/fixtures/ntp-corrections-2026-08.json` in valar-eam-feed and
re-open the A.3 ruling.

| Measure | Value |
|---|---|
| Time to first sync from boot | ______ ms |
| Sync events observed | ______ |
| Worst adjustment applied | ______ ms |
| Poll-cadence floor | ______ ms |
| **Reported uncertainty** | **______ ms** |

The firmware reports `max(worst adjustment, max poll gap)` — the correction NTP
had to apply is the error a device would carry into a deviation score, and we
cannot claim tighter than we sample.

### The scoring floor has TWO halves

`granularity = max(NTP uncertainty, key-window poll interval)`

| Half | Value | From |
|---|---|---|
| Clock | ______ ms | NTP screen (`worst adjustment`) |
| Input | ______ ms | `HOLD,keywindow,max_ms` — arm-C window only |
| **Floor** | **______ ms** | the larger |

**Use the key-window figure, not the idle poll figure.** The idle loop measured
~45 ms max; the launch key-turn runs a focused high-rate poll, so the idle number
is the wrong floor and would set the bucket an order of magnitude too coarse.

**Round UP**, per §13's "err pessimistic". The leaderboard displays the smallest
bucket this floor supports — a ranking quoting precision it does not have is
worse than a coarser one that is honest, and somebody will eventually try to beat
it by a millisecond.

Chosen granularity: **______ ms**

---

## Raw capture

CSV tags: `HOLD,` `TOUCH,` `NTP,` `REG,` — capture with the existing bench tooling.

Attach: `bench-logs/gametest-<board>-<date>.log`
