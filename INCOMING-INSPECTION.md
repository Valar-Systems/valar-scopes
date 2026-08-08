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

## 0. Before you start — three gotchas that look like faults

- **The left-side POWER LATCH button must be LONG-PRESSED to power the SoC.** USB alone does
  *not* boot it. A flashing red LED is the charger reporting "no battery" — **that is normal**,
  not a fault. Expect to think the first board is DOA. It isn't.
- **`esptool` STUB bulk-reads die on this board's USB-JTAG** ("Packet content transfer stopped")
  at any baud. Use `--no-stub` for `read_flash`. Stub **writes** are fine and fast — normal
  flashing needs no workaround.
- **On a DATA USB cable, the board reboots itself ~2 s into every power-on.** You will see the
  splash, the boot prompt counting `3, 2, 1`, a white flash, and the whole boot start over. It
  looks exactly like a crash or a brownout. **It is neither**, and the boot ROM says so:

  ```
  rst:0x15 (USB_UART_CHIP_RESET)
  ```

  That code means the USB-Serial/JTAG peripheral was told to reset **by the host** — firmware
  cannot produce it. Any program that opens the port shortly after the device re-enumerates
  triggers it: a serial capture reattaching, an IDE's serial monitor, Windows' own enumerator.
  It happens on roughly half of power cycles because it depends on whether the open lands
  inside the boot window.

  > **Judge boot behaviour on a POWER-ONLY USB cable or a wall charger, with no computer
  > attached.** Verified: identical board, identical image — resets on a data cable, boots
  > clean every time on power-only. Customers are always in the power-only case, so this
  > artifact never reaches them.

  If a board reboots during boot on **power-only**, that *is* a fault — capture the serial
  output on a data cable and look for `Brownout detector was triggered` or a `Guru Meditation`
  backtrace, which are the codes that mean something real.

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
| `0xFA` | `IrqCtl` | **0x60** | Flag — factory `EnTouch\|EnChange`. A batch fingerprint, **not** the operating value (see below) |
| `0xFB` | `AutoReset` | 50 (s) | Informational — chip self-reset on gestureless touch |
| `0xFC` | `LongPressTime` | 60 (s) | Informational — chip self-reset on long press |

`0xFB`/`0xFC` are **custom values, not datasheet defaults**. They matter for forensics: a
chip-initiated self-reset presents as a ~450 ms register blank, which is easy to misread as a
wedge. Note them so field reports aren't misdiagnosed.

> **`0xFA` is a fingerprint, not the running config.** An earlier version of this table said the
> factory `0x60` "proves the INT-gating requirement" and that "registers are only valid at INT
> pulses". Both are wrong, and that wording caused a misdiagnosis in #173. Two corrections,
> measured on a conforming board (`chip id=0xB6 proj=0x02 fw=0x02  0xFA=0x60 0xED=1 0xFE=0x01`):
>
> 1. **The firmware overwrites it.** LovyanGFX's `Touch_CST816S::_check_init()` writes
>    `0xFA=0x20` (change-only) and `0xED=20` on every touch init. The device never runs on 0x60.
> 2. **The registers are readable at any time.** `Touch_CSTxxx.cpp` consults the INT level only
>    inside `if (diff_msec < 10 && _wait_cycle)` — a rate limiter, not a gate. Any poll spaced
>    ≥10 ms apart does a full I2C read regardless of INT.
>
> INT gating is still the right read strategy at frame cadence (it is what fixed the phantom-
> release double-tap), but "no INT edge ⇒ no reading" is not a property of this chip, and no
> diagnosis should rest on it. Keep recording 0x60 — a batch that differs is still a supplier
> conversation — just don't infer read behaviour from it.

> **Read `0xFA`/`0xED` from a census AFTER the first RST pulse, not from the boot census.** The
> probe prints several censuses and they legitimately disagree, because `tft.init()` has already
> run `_check_init()` by the time the first one is taken. Measured on board #1, one run:
>
> | census | `0xFA` | `0xED` | what you are looking at |
> |---|---|---|---|
> | `@phase2-boot` | `0x20` | `0x14` (20) | the **driver's** values, just written |
> | `@phase2-periodic` (after RST test 1) | `0x60` | `0x01` | the **factory** fingerprint |
>
> The RST pulse resets the chip to its factory config, and nothing re-runs `_check_init()`
> afterwards, so every later census reads the real fingerprint. Judging §3 from the boot census
> would flag a conforming board — the reference measurement quoted above (`0xFA=0x60 0xED=1`) is
> itself a post-reset read. `0xFE`, `0xF9`, `0xFB` and `0xFC` are unaffected: they read the same
> in every census, which is why this trap only bites the two registers the driver touches.

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

> **This section runs in two halves, and the second one cannot happen here.** The probe from §1
> does no networking at all, so there is no `[WiFi]` line on the console while §1–§5 are running.
> Do the **visual** check now — link on the u.FL leg, antenna connected — and take the RSSI and
> reason-204 evidence from the **§6 boot**, which is the first time production firmware brings up
> the radio. Reading §4 as a single step in document order leaves an inspector waiting for output
> that build cannot produce.

Verify visually that the link sits on the u.FL leg and the antenna is connected. Then, on the §6
boot, confirm on serial at bench distance from the AP:

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

**The env is `blipscope-s3-128`. There is no other one.** Not a `-prodburn` (deleted), not
`-cloud` or `-soak` (staging-only bench envs). Flashing one of those ships the wrong backend or
none at all, and the board looks perfectly healthy either way.

```sh
pio run -e blipscope-s3-128 -t upload --upload-port COM<n>
```

> **Always pin `--upload-port`.** A soak board is usually attached alongside the unit under
> inspection, and PlatformIO's auto-detect has no idea which is which. The failure is silent and
> expensive in exactly one direction: it reflashes the board carrying a multi-hour run.

> **Flash from a checkout of `main`, and verify the built image — not the command.** This names
> an env, not a commit: it builds whatever the working tree holds. On 2026-08-08 this checkout sat
> on an unrelated feature branch while two shipping fixes were merged, so the identical command
> would have produced a board with no cloud feed and no stale-ladder floor, and nothing about the
> flash, the boot, or the radar would have looked wrong.
>
> ```sh
> git log --oneline -1                                        # on main, and current
> grep -ac 'scopes\.valarsystems\.com'  .pio/build/blipscope-s3-128/firmware.elf   # >= 1
> grep -ac 'scopes-staging'             .pio/build/blipscope-s3-128/firmware.elf   # 0
> ```

Confirm on boot: radar renders, backlight responds, touch registers a tap, WiFi associates with
**zero reason-204**, and the OTA line reports the expected channel/version:

```
[build] env=blipscope-s3-128 fw=v<N>
[WiFi] CONNECTED  IP=...  RSSI=-<nn> dBm
[ota] channel=s3-128 current=<N> latest=<N>
```

**This boot is where §4's serial half gets recorded** — the RSSI reading and the zero-reason-204
criterion. Watch it here; the probe build could not produce it.

---

## 7. First-run acceptance — the path every board takes exactly once (100%)

Sections 1–6 all inspect a board that is **already provisioned**. The customer's first five
minutes are a different code path, and it is the only one no log can verify after the fact: by
the time a device checks in, it has already survived it. Two bugs shipped into that path in one
week ([#166](https://github.com/Valar-Systems/valar-scopes/issues/166),
[#173](https://github.com/Valar-Systems/valar-scopes/issues/173)) and **both were found by
accident**, when something wiped WiFi unintentionally. Neither announced itself: a dead `:80`
looks identical to a healthy one, and a recovery gesture that cannot fire logs the same line as
a customer who did not press hard enough.

Fifty boards are about to take this path exactly once each. Run it last, on **every** board,
after §6 passes.

**Record it.** Start a ledger before step 1 so the pass is evidence rather than scrollback:

```powershell
Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass',`
  '-File','c:\Github\Blipscope\scripts\bench-capture.ps1','-Port','COM<n>','-Label','board-<NN>'
```

A section whose whole premise is "no log can verify this afterwards" should not then be verified by
someone remembering they watched it. The recorder redacts credentials at write time, so the ledgers
are safe to keep.

1. **Wipe Wi-Fi the way a customer does.** Stats screen → double-tap `[ Reset Wi-Fi ]`. The device
   forgets the credentials and reboots into the config portal.
   *(Do not use the boot-hold for this — see step 4.)*

2. **Provision through the portal.** Join the `Blipscope-XXXXXX` AP from a phone, choose the
   bench SSID, enter the password, save. **Do not power-cycle the device by hand.** The whole
   point is the first boot *after* provisioning, and a customer does not reboot here.

3. **The config page must answer on that first boot** — no power cycle in between. **Poll; do
   not judge on one attempt:**

   ```sh
   for i in $(seq 1 20); do
     curl -s -o /dev/null -w "$i: %{http_code} %{size_download}\n" --max-time 3 http://<device-ip>/
     sleep 2
   done
   ```

   Expect **`200`** and ~50 KB **within 30 s of the device reporting an IP**.

   > **A refusal in the first ~20 s is normal and is NOT the bug.** The network stack answers
   > well before `server.begin()` runs, so connections in that gap are actively refused.
   > Measured on a healthy board: IP at boot, `ERR_CONNECTION_REFUSED` through ~13 s, first
   > `200` at **~17.5 s uptime**. An earlier version of this step said "a refused connection is
   > a fail, not a retry" — that would quarantine healthy batches, and it is also exactly what
   > a customer sees if they type the address the moment setup finishes.

   **A refusal that persists past 30 s is the failure.** That is #166: AsyncWebServer lost the
   bind to the portal's listener, and `begin()` returns `void`, so nothing said so. Re-flashing
   does not clear it; only a power cycle does — which is precisely why it must be caught here
   and not by the customer.

   > There is currently **no serial line** that reports the bind outcome. The liveness check
   > added for #166 was reverted in #172 because the probe socket itself tore down the live
   > listener. Until a safe replacement lands
   > ([#181](https://github.com/Valar-Systems/valar-scopes/issues/181)), `curl` is the only
   > evidence. Do not skip it.

4. **Recovery gesture.** Power-cycle. Within ~3 s the screen shows **TOUCH & HOLD / to reset
   WiFi**. Touch it *then* — **not before** — and hold through the countdown:

   ```
   [wifi-reset] boot touch window: driver=1 polls=56 touched=1
   [wifi-reset] boot touch detected; hold to confirm
   [wifi-reset] hold completed -- clearing credentials (misses=0 longestGap=56ms)
   ```

   > **If a serial recorder is attached, the FIRST countdown after the power cycle will be
   > interrupted and start over. Do the gesture on the second one.** That white flash is the
   > recorder reopening the port inside the boot window, not a board fault, and the ROM says so
   > in the ledger: `rst:0x15 (USB_UART_CHIP_RESET)` — a code firmware cannot produce (§0). No
   > DTR/RTS setting avoids it; the trigger is the `open()` itself. Keep the recorder attached
   > anyway: an unrecorded pass is scrollback, and this is the one section that exists *because*
   > no log can verify the path afterwards.

   > **The touch has to ARRIVE after the prompt, and this is not a UX preference.** A finger
   > already on the glass at power-on is calibrated into the CST816's no-touch baseline by the
   > TP_RST pulse inside `tft.init()`, and is then reported as absent until it lifts — measured:
   > 12 s of solid contact, one reset, then zero touches for 48 s with the thumb still down
   > ([#173](https://github.com/Valar-Systems/valar-scopes/issues/173)). **No board will ever
   > pass a "hold from power-on" test.** If one fails *this* step with the touch arriving
   > correctly, that is a genuine board fault.

   `misses` counts polls that saw no contact during the hold; expect **0**. A non-zero `misses`
   with a large `longestGap` means that batch's touch IC drops samples mid-contact — record it,
   that is the phantom-release class the C3 had (see §3). The gesture tolerates gaps under
   250 ms, so it will still pass; the number is the early warning, not the failure.

---

## 8. Board #1 only — one OTA from a virgin second slot

**Run this on the FIRST board of the batch, after §7, before the other 49 start.** It is the one
check the pilot run cannot repeat later, because after this board takes an update it will never
be in the starting state again.

**Why board #1 and not the bench.** Every OTA test to date ran on a unit that already had a
working image in the other slot — the bench board had been flashed both ways many times. A
factory-fresh board has `app1` **blank**. The download path has no reason to care, and that is
exactly the shape of assumption that has cost a week each time it turned out to be wrong: the
CST816 "obviously" reports a held finger, `end()` "obviously" releases the TLS transport,
`begin(url)` "obviously" validates a certificate. None of them did. This is the cheapest
possible place to find out, and after board #1 it becomes the most expensive.

1. **Confirm the starting state is genuinely virgin.** A USB flash writes `app0` and resets
   otadata, so this is the state all 50 ship in:

   ```
   [ota-slot] boot running=app0 @0x020000 state=UNDEFINED next=app1 fw=<N>
   ```

   `next=app1` with nothing ever written there is the case under test. If this reads `app1`, the
   board is not virgin — reflash over USB and start again.

2. **Trigger one real update.** Build against the pinned pre-release harness (see
   [RELEASING.md](RELEASING.md) — and **re-clobber the assets first**, the tag holds a mixed
   matrix from CI):

   ```sh
   pio run -e blipscope-s3-128-otatest -t upload
   ```

3. **The same three checks, from the state the fleet is actually in:**

   | check | expected |
   |---|---|
   | download completes | `[ota-mem] pct=100 ...`, no `Update failed` |
   | slot switches | `[ota-slot] boot running=app1 @0x660000 ... fw=<N+1>` |
   | **survives power loss** | power-cycle, then `running=app1 ... state=VALID` |

   > **Do not wait for `PENDING_VERIFY` — you will never see it, and its absence is not a
   > failure.** Rollback IS armed on these builds (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`),
   > so the state genuinely passes `NEW → PENDING_VERIFY → VALID`. But arduino-esp32's
   > `initArduino()` calls `esp_ota_mark_app_valid_cancel_rollback()` before `setup()` runs, and
   > `LogOtaSlot("boot")` is inside `setup()`. The transition is real, resolved, and
   > structurally unobservable from where we log it. **`VALID` on the first post-update boot is
   > the correct and expected reading.**

   > **This is a correctness gate, not a brick gate.** An earlier version of this section said
   > `PENDING_VERIFY` risks "a rollback to `app0`, which on a virgin board is a slot that was
   > never written". That is **backwards**: on a virgin board `app0` is the slot that *was*
   > written, by the USB flash, and `app1` is the blank one. OTA writes the blank slot and boots
   > it; if that image fails, rollback targets `app0` — the working factory image. **The
   > brick-by-rollback scenario does not exist**, because the rollback target is always the
   > previously-working slot by construction.
   >
   > What §8 actually proves is narrower and still worth a gate: that the download, the write,
   > and the boot-partition switch all behave when the destination has **never been written**.
   > That is the only state the fleet ships in and the only state this board can be tested from
   > once.

4. **Then reflash board #1 to production over USB** (`pio run -e blipscope-s3-128 -t upload`) so
   it rejoins the batch in the same state as the other 49 — `app0`, otadata reset, no pinned OTA
   base compiled in. Record the result on the batch sheet; the other 49 do **not** repeat §8.

### Result — 2026-08-07, board #1, PASSED

```
[ota-slot] boot running=app0 @0x020000 state=UNDEFINED next=app1 fw=5   <- virgin
[ota] channel=s3-128 current=5 latest=6
[ota-mem] pre-update free=236820 largest=135156 free8=236820 tlsOk=1
[ota-mem] pct=0   free=176080 largest=118772 free8=176080  TRIAL tlsOk=1 rej=0
[ota-mem] pct=100 free=180836 largest=118772 free8=180836  TRIAL tlsOk=1 rej=0
[ota-slot] boot running=app1 @0x660000 state=VALID next=app0 fw=6       <- first boot
[ota-slot] boot running=app1 @0x660000 state=VALID next=app0 fw=6       <- after power cycle
```

Note `largest=118772` unchanged across the whole transfer while `free8` moves — the plateau
from #163, reproduced during the operation it most mattered for. Read `free8` and the trial
result; `largest` is retained only so the plateau stays visible beside the number that tracks.

---

## Batch acceptance summary

A batch is **accepted** only when, across the sampled boards:

1. Identity quad = **B6/02/02/04** (100% of boards)
2. `DisAutoSleep` 0xFE = **1**, and `write=ok` / `readback=1` (100%)
3. `IrqCtl` 0xFA = **0x60** (100%)
4. RF = `FEED_TERMINATION` + YF0026-class antenna, **zero reason-204** (100%)
5. Touch INT/RST behave per §5 (first 5, then spot-check)
6. First-run per §7: portal provisioning completes, `curl http://<ip>/` returns **200** on the
   first boot after setup with no power cycle, and the boot touch-to-forget reaches
   `hold completed` with `misses=0` (100%)
7. **Board #1 only** — §8: one OTA from a virgin `app1` completes, the slot switches, and the
   new image is still `running=app1 state=VALID` after a power cycle. Board #1 does not rejoin
   the batch until this passes; the other 49 do not repeat it

Any quad mismatch, any unwritable/zero 0xFE, or any reason-204 storm ⇒ **quarantine the batch and
open a supplier conversation**, quoting the measured values. Do not ship a partial batch on the
assumption the rest are fine — these are per-batch factory-config properties, not per-unit defects.

## Record for each batch

Keep with the batch: supplier + PO, date, quantity, the measured identity quad, 0xFE pre/write/
readback, `0xF9`/`0xFA`/`0xFB`/`0xFC` values, RSSI + reason-204 count, and the pass/fail call.
This is the evidence trail for the next supplier conversation — the antenna requirement was won
with exactly this kind of measured A/B.
