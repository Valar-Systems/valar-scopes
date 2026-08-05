# Missileer bench results — 2026-08-05

Answers to the three hardware questions in [missileer-game-design.md](missileer-game-design.md)
§13 task 1, plus the fourth number (key-window input floor) added when §6.5 established that
scoring granularity is `max(clock, input)`.

**Board:** jxl/EC-Buying 1.28" GC9A01 + CST816D, `gametest-s3-128`, COM119.
**Session:** 17.1 h uptime, three-arm hold matrix + NTP soak.
**Ledger:** `bench-logs/gametest-2026-08-05-0734.log` (from 07:34; earlier runs are RAM totals only).

---

## Verdicts

| # | Question | Verdict |
|---|---|---|
| 1 | Hold-gesture dropout | **VIABLE** — with a ≥100 ms rejoin window, driver source. §8's two-person rule survives. |
| 2 | Multitouch | **CLOSED BY DESIGN** — single contact. Not measured, not required. |
| 3 | NTP uncertainty | **75.7 ms** — forces ~0.1 s scoring buckets, not milliseconds. |
| 4 | Key-window input floor | **avg 3 ms, max 43 ms** over 31 728 samples. Not the binding constraint. |

---

## 1. Hold gesture — the three arms

```
A_factory_nosleep  runs 18  clean  9  50%  dropouts 10  longest 4044ms
B_sleep_armed      runs 10  clean  3  30%  dropouts 15  longest   45ms
C_chip_poll        runs 10  clean  0   0%  dropouts 27  longest   42ms
autosleep_tax_pts 20   chippoll_rescue_pts -30   busfail runs_voided 0
```

Every arm change was honoured (`readback` matched `intended`), so all three sets are the arm
they claim to be.

### Arm C is the worst arm — finding (b) was backwards

C exists on the premise that the chip keeps the contact the driver loses, so polling
`TouchNum` directly at 2 ms would rescue the gesture. **It does the opposite**: −30 points
against B, and 2.7 dropouts per run versus B's 1.5 and A's 0.33. The driver carries state the
raw register does not, and reading the register directly discards it.

`busfail runs_voided 0` — the bus was never the explanation, which is what that counter was
added to rule out.

This kills the "game-mode poll window that ignores INT entirely" idea in the arm-C rationale.
The implementation must use `getTouch()`.

### But the hold works, and `clean %` is the wrong statistic

All 48 dropouts, across all three arms:

```
39ms x10   40ms x5   41ms x8   42ms x10   43ms x6   44ms x3   45ms x3
```

Not one outside 39–45 ms. **A human cannot lift and replace a finger in 40 ms** — these are
reporting gaps, not releases. `clean` counts them as failures, which is why C reads 0 %, but
the runs themselves succeeded: `target_met,1` on 13 of the 14 recorded runs, with totals of
10.5–16.0 s.

With the 100 ms rejoin window the harness already implements — better than 2× margin over the
worst observed gap — **the deputy's sustained hold is reliable on all three arms.**

So §7's escalation ("if the hold is unreliable on all three arms, redesign §8 around discrete
taps") does **not** fire. Requirements it does impose:

1. Contact is debounced with a **≥100 ms rejoin**; never sampled raw.
2. Source is the **driver**, not the chip register.
3. Auto-sleep should be left **off** (arm A), worth ~20 points of clean rate.

### Caveat: the dropout *durations* are an instrument artifact

The 39–45 ms band is suspiciously tight, and it coincides with the max poll gap (43 ms). Cause:
entering a dropout forced an immediate repaint, and a 240×240 sprite push landed *inside* the
interval being measured — between "contact lost" and "contact seen again". So the measured
duration is dominated by frame time, not by the outage.

**The dropout counts are valid** (a lost contact was genuinely observed). **The durations are
upper bounds**, and the true outages are probably a single poll. Fixed after this session: the
entry repaint is gone and `poll_gap_ms` is now logged alongside `gap_ms`. Re-measure before
quoting any duration as a controller property.

This only strengthens verdict 1 — if real outages are shorter than measured, the 100 ms rejoin
has more margin, not less.

---

## 2. Multitouch — closed by design, not by measurement

`chip_touchnum` never exceeded 1, but no two-finger input was ever applied, so that proves
nothing. It does not need to: §6.8 already ruled that **each seat renders and enters only its
own 3 characters**, so the crew scope is two devices, never two fingers on one panel. Confirmed
2026-08-05 on ergonomics as well — the device sits on a desk and is operated with one thumb.

Recorded as *not required* rather than *unmeasured*; the two age differently.

**Residual, separate question:** a stray second contact (resting palm, knuckle) is not a design
input but will happen, and if the controller then reports a garbage coordinate it could corrupt
the **key-turn bezel drag** mid-sortie. If that probe shows garbage, the key-turn needs a
plausibility filter — reject position jumps above N px/poll — rather than trusting the stream.

---

## 3. NTP uncertainty — 75.7 ms

```
NTP,beat,...,syncs,6,worst_us,75686,uncertainty_ms,20042,poll_avg_ms,8,poll_max_ms,20042
```

`worst_us,75686` = **75.7 ms**, the largest correction NTP applied across 6 syncs over 17 h.
That is the honest figure: it is exactly the error a device carries into a deviation score.

**Discard `uncertainty_ms,20042`.** That figure is `max(drift, poll floor)` and the poll floor
was contaminated by the harness freeze below. The real floor is `max(75.7 ms, 43 ms)` = **75.7 ms**.

Two syncs are required before any uncertainty exists at all; a lone sync reports `-1`
(UNMEASURED), because an earlier session read `syncs,1 worst_us,0 uncertainty_ms,6036` and
every part of that was misleading.

---

## 4. Key-window input floor — avg 3 ms, max 43 ms

```
HOLD,keywindow,avg_ms,3,max_ms,43,samples,31728
```

Sampled only while an arm-C hold is live — the focused high-rate window the launch key-turn
will actually run in, not the idle loop. Input is **not** the binding constraint; the clock is.

---

## 5. Scoring granularity — amended 2026-08-05

`max(75.7 ms clock, 43 ms input)` = ~76 ms, so the finest honest bucket is **0.1 s**. Design §4's
"milliseconds" was provisional pending this measurement; §13's rule (display the smallest bucket
the measured floor supports) is what executes. Ruling:

1. **Single-execution deviation: tenths of a second** (`|key − T|`, 0.1 s buckets). The board
   states this plainly and footnotes the 75.7 ms clock floor as the reason.
2. **Aggregates may display finer.** A monthly average over N launches has effective precision
   ~0.1/√N s — quantization error averages down — so the proficiency-cycle ladder may show
   hundredths on season averages while single launches show tenths. Stated on the board as
   *"single sorties in tenths; cycle averages sharpen with volume."*
3. Applies **wherever deviation renders**: the device VOTE REGISTERED screen (`+0.1 s` format),
   credits lines, `/log`, duels.

This kills the beat-a-record-by-a-fake-millisecond failure mode while keeping the ladder
competitive: 20 buckets across the 2 s window *will* tie on single nights at 50 players, and the
month is the real ladder.

---

## 6. The harness freeze — root-caused, and it was ours

The board was "almost unusable" for a full bench day, then flawless the next. Same firmware,
same board, continuous uptime. The difference was whether anything was **reading the serial
port**.

`gametest_main.cpp` has its own `setup()` and never inherited the product's guard
([main.cpp:104](../src/main.cpp#L104)):

```cpp
Serial.setTxBufferSize(4096);
Serial.setTxTimeoutMs(2);
```

With nothing draining the USB-CDC, `Serial.write` blocks once the TX ring fills. The harness
prints a TOUCH line on every transition, so an unattended session fills it in seconds and the
loop stalls on the print.

| Condition | Worst poll gap |
|---|---|
| Unattended | **20 042 ms** |
| Recorder attached, 31 728 samples | **43 ms** |

The proof it was buffering rather than scheduling: when a recorder finally attached 17 h into
the run, the first lines it delivered were the beats from **60 s and 120 s after boot** — data
that had been sitting in the TX ring the whole time.

Guard added. Two consequences worth carrying:

- **No poll-gap or timing figure from an unattended bench session is trustworthy**, including
  the 6036 ms and 20 042 ms readings that at different times were mistaken for a scheduling
  fault and a hardware limit.
- A harness that freezes *only when nobody is watching* is the worst possible failure shape,
  and it corrupted precisely the number the scoring floor is derived from.

---

## Open items

- **Stray-contact probe** (§2 residual) — 30 s, before the next reflash.
- **Re-measure dropout durations** with the repaint artifact removed, if any duration is ever
  quoted as a controller property. The counts and the ≥100 ms rejoin conclusion do not depend
  on it.
- The soak board has **not** been reported freezing; the freeze above is bench-only and now
  explained. If a shipping device ever shows it, this is the first thing to check — though the
  product has carried the guard since before this session.
