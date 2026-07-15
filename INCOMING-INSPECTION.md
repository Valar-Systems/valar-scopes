# Incoming inspection — Blipscope Kit S3 (`s3-128`)

Acceptance procedure for each incoming batch of the EC-Buying **ESP32S3-NxxRxx-128SPIx**
(N16R8: 16 MB flash / 8 MB octal PSRAM, native USB `303A:1001`, 1.28" GC9A01 + CST816D).

**Why this exists.** The board is a low-cost part from a supplier who can silently change the
touch IC revision or its factory register config between batches. Two specific substitutions
would ship a broken product without any visual difference:

1. **A touch IC whose `DisAutoSleep` (0xFE) is 0 or unwritable.** The retired C3 Kit's CST816**T**
   had 0xFE unreachable and wedged off the bus ~2×/hour. This board's CST816**D** ships with
   0xFE=1 from the factory — **but `AutoSleepTime` (0xF9) is still armed at 2 s underneath it**.
   A batch with 0xFE=0 reverts to the C3's failure class. See [[touch-wedge-v4-regression]] and
   the C3 constraints section of [CLAUDE.md](CLAUDE.md).
2. **A chip-antenna (`SOLDER_TERMINATION`) RF build.** Measured on our sample: the chip antenna
   loses association entirely (see [RF](#4-rf-configuration--100)).

Both are caught below. **Run this before the boards go anywhere near production firmware.**

---

## 0. Before you start — two gotchas that look like dead boards

- **The left-side POWER LATCH button must be LONG-PRESSED to power the SoC.** USB alone does
  *not* boot it. A flashing red LED is the charger reporting "no battery" — **that is normal**,
  not a fault. Expect to think the first board is DOA. It isn't.
- **`esptool` STUB bulk-reads die on this board's USB-JTAG** ("Packet content transfer stopped")
  at any baud. Use `--no-stub` for `read_flash`. Stub **writes** are fine and fast — normal
  flashing needs no workaround.

Serial is 115200. Each board takes ~3 min.

---

## 1. Flash the probe — 100% of boards

The probe compiles **only** `src/probe/TouchProbe.cpp` and talks to the CTP over the same
`lgfx::i2c` path the shipping `TouchWatchdog` uses, so ACK/NACK results compare 1:1 with the
C3 finding rather than through a different driver.

```sh
pio run -e probe-s3-128-phase2 -t upload -t monitor
```

Flash and monitor from **PowerShell**, not Git Bash — `pio` under MSYS can gut its own esptool
package (see [[pio-msys-esptool-trap]]).

The boot census can race the host attaching its serial console, so the probe **re-runs the census
periodically**. If you miss the boot banner, just wait for the next pass — don't reflash.

---

## 2. Identity quad — the batch fingerprint (100%)

This is the load-bearing check: it is how we detect a silent IC substitution.

```
[probe]   chip-id   (0xA7) = 0xB6
[probe]   proj-id   (0xA8) = 0x02
[probe]   fw-ver    (0xA9) = 0x02
[probe]   factory-id (0xAA) = 0x04
```

**PASS: the quad reads `B6 / 02 / 02 / 04`.**

**Any other quad ⇒ QUARANTINE the whole batch.** Do not accept, do not flash production. A
different quad means a different touch-IC revision or factory config; it must go through the
wedge evaluation (bisection + 24 h soak) again before it can ship. Record the quad you actually
got — that value is the evidence for the supplier conversation.

---

## 3. Sleep + IRQ config (100%)

From the read-only config dump (`-- config space (read-only, pre-write) --`), and the 0xFE
write/readback experiment:

| Register | Name | Expected | If it differs |
|---|---|---|---|
| `0xFE` | `DisAutoSleep` | **1** | **REJECT/QUARANTINE** — batch reverts to C3-style sleep |
| `0xF9` | `AutoSleepTime` | 2 (s) | Informational — sleep *is* armed under 0xFE; this is why 0xFE matters |
| `0xFA` | `IrqCtl` | **0x60** | Flag — `EnTouch\|EnChange`; registers are only valid at INT pulses. This config **proves the INT-gating requirement**; a different value invalidates our touch read strategy |
| `0xFB` | `AutoReset` | 50 (s) | Informational — chip self-reset on gestureless touch |
| `0xFC` | `LongPressTime` | 60 (s) | Informational — chip self-reset on long press |

`0xFB`/`0xFC` are **custom values, not datasheet defaults**. They matter for forensics: a
chip-initiated self-reset presents as a ~450 ms register blank, which is easy to misread as a
wedge. Note them so field reports aren't misdiagnosed.

The write experiment must show the register is **writable**:

```
[probe] 0xFE @<window>: pre=1 write=ok readback=1
```

**PASS: `write=ok` and `readback=1`.** `write=NACK` or `readback=0` ⇒ **REJECT** — that is the
C3's DOA class, where the no-sleep insurance cannot be applied at all.

> Production insurance: shipping firmware runs `MaintainNoSleep` (write + verify) regardless, so
> a board that merely *drifts* is re-armed at runtime. That insurance is **not** a substitute for
> this gate — it cannot save a batch where 0xFE is unwritable.

---

## 4. RF configuration — 100%

The board carries **both** a 0603-size ceramic chip antenna **and** a u.FL/IPEX socket, selected
by a 0-ohm link the schematic names `SOLDER_TERMINATION` (chip) vs `FEED_TERMINATION` (u.FL) at
`ANT1`/`RF1`.

**Required: `FEED_TERMINATION` (u.FL leg) + a YF0026-class FPC antenna fitted.**

Verify visually that the link sits on the u.FL leg and the antenna is connected. Then confirm on
serial at bench distance from the AP:

```
[WiFi] CONNECTED  IP=... RSSI=-62 dBm
```

| Config | RSSI | Association | Verdict |
|---|---|---|---|
| Chip antenna (`SOLDER_TERMINATION`) | −64 dBm | ≥1 reason-204 retry **every** association; 17 timeouts in 35 min → **total loss** | **REJECT** |
| Quectel YF0026 (`FEED_TERMINATION`) | −62 dBm | 2.4 s, **zero** reason-204 | **ACCEPT** |
| C3 reference (same desk) | −61 dBm | — | baseline |

The near-identical RSSI across all three is the point: **RX was never the problem.** The chip
antenna fails on **TX** — the handshake frames the device sends *to* the AP. So a healthy-looking
RSSI number alone does **not** clear a board; **zero reason-204 retries** is the real criterion.

Any board showing repeated reason-204 disconnects ⇒ reject and check the link/antenna before
blaming the AP.

---

## 5. Touch wiring sanity (sample — first 5 boards of a batch, then spot-check)

The probe's phase-2 tests confirm the two pins the shipping firmware depends on. **Drag a finger
on the panel continuously** while it runs.

- **INT (GPIO11):** `[probe] INT hunt: fingers=N falls:...` — GPIO11 must rack up falling edges
  under drag (our sample: 1700+); every other candidate stays flat.
- **RST (GPIO0):** fires 3× — **hands off the RESET button** while it does.
  ```
  [probe] RST test: before=ACK duringLow=NACK/... after=ACK -> ...
  ```
  Expected: chip **NACKs while held low**, then cleanly **re-ACKs** after its ~450 ms boot.

A failure here means the pin map differs from [include/variants/s3_128.h](include/variants/s3_128.h)
— quarantine the batch; the variant header (and the watchdog's hard rung) would be wrong.

**Verified pin map** for reference — hardware *and* vendor-doc cross-confirmed:

| Function | Pins |
|---|---|
| Display GC9A01 (SPI3) | SCLK=3, MOSI=10, DC=18, CS=2, RST=21, BL=42 (LEDC PWM) |
| Touch CST816D | SDA=8, SCL=9, addr `0x15`, TP_RST=0, TP_INT=11 |
| TF card | SCK=41, MOSI=47, MISO=48, CS=40 |
| Buttons / misc | SW_UP=14, SW_PW=15, SW_Down=16, VBUS detect=17, battery ADC=1 (÷2), RTC INT=45 |

> GPIO0 is a strap pin. The probe drives it deliberately; nothing else should.

---

## 6. Flash production firmware + final function check (100%)

```sh
pio run -e blipscope-s3-128 -t upload
```

Confirm on boot: radar renders, backlight responds, touch registers a tap, WiFi associates with
**zero reason-204**, and the OTA line reports the expected channel/version:

```
[ota] channel=s3-128 current=<N> latest=<N>
```

---

## Batch acceptance summary

A batch is **accepted** only when, across the sampled boards:

1. Identity quad = **B6/02/02/04** (100% of boards)
2. `DisAutoSleep` 0xFE = **1**, and `write=ok` / `readback=1` (100%)
3. `IrqCtl` 0xFA = **0x60** (100%)
4. RF = `FEED_TERMINATION` + YF0026-class antenna, **zero reason-204** (100%)
5. Touch INT/RST behave per §5 (first 5, then spot-check)

Any quad mismatch, any unwritable/zero 0xFE, or any reason-204 storm ⇒ **quarantine the batch and
open a supplier conversation**, quoting the measured values. Do not ship a partial batch on the
assumption the rest are fine — these are per-batch factory-config properties, not per-unit defects.

## Record for each batch

Keep with the batch: supplier + PO, date, quantity, the measured identity quad, 0xFE pre/write/
readback, `0xF9`/`0xFA`/`0xFB`/`0xFC` values, RSSI + reason-204 count, and the pass/fail call.
This is the evidence trail for the next supplier conversation — the antenna requirement was won
with exactly this kind of measured A/B.
