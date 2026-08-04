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
| 3 | NTP sync uncertainty | **______ ms** | deviation scoring granularity (§4) — round UP |

---

## 1. HOLD TEST — deputy switch-hold

Run **≥20 holds per arm**. A run is *clean* when it reaches 10 s with zero dropouts.

> **The A/B is inverted on this board — read this before recording anything.**
> The brief assumed "product config" = auto-sleep ON. This board is a CST816**D**,
> which [INCOMING-INSPECTION.md](../INCOMING-INSPECTION.md) records as shipping
> **`0xFE`=1, i.e. auto-sleep already DISABLED**. So the factory arm *is* the
> no-sleep arm, and the control arm has to deliberately **arm** sleep (`0xFE`=0 —
> the value the inspection doc calls REJECT), held only for the run.
> `AutoSleepTime` (`0xF9`) sits at 2 s underneath, so a 10 s hold against a 2 s
> timer is the collision worth measuring.
>
> Long-press on the HOLD screen flips the arm. Every flip logs a `REG,arm,…` line
> with `honoured,0|1` — **if `honoured` is 0 the chip refused the write and that
> arm's numbers describe an unknown state.** Record it as void, not as a result.

| Arm | `0xFE` | Runs | Clean | **Clean %** | Dropouts | Longest (ms) |
|---|---|---|---|---|---|---|
| Factory (no auto-sleep) | 1 | | | | | |
| Sleep armed | 0 | | | | | |
| | | | | **Δ = auto-sleep tax** | | |

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

Leave running **≥30 min**; longer is better.

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

**Set scoring granularity to this number rounded UP**, per §13 build task 1's
"err pessimistic". If it lands at ~50 ms, score in 50 ms buckets — do not display
single milliseconds the fleet cannot actually resolve. A leaderboard quoting
precision it does not have is worse than a coarser one that is honest.

Chosen granularity: **______ ms**

---

## Raw capture

CSV tags: `HOLD,` `TOUCH,` `NTP,` `REG,` — capture with the existing bench tooling.

Attach: `bench-logs/gametest-<board>-<date>.log`
