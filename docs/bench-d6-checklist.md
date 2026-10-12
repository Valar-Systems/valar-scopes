# D6 bench sitting — checklist

The **how**, with blanks. The **what and why** is [bench-runbook-device-game.md](bench-runbook-device-game.md),
and its pass/fail criteria are quoted here word for word; nothing below adds one.
Where the runbook leaves a result undefined there is a blank and a question (end of page).

Operator ________  ·  Board ☐ `missileer` (90706931E9D8) ☐ `missileer2` (90706931E794, needs #393, Q9)  ·  Start (UTC) ________

**Rulings needed before you start:**
**Q1** station 2 ☐ run the candidate image (#399, `clock_age_s=-1`) ☐ skip it  ·
**Q2** station 4 ☐ arms A, B, C (prepared) ☐ other ________

---

## Setup (2 min) — PowerShell, from this branch's checkout

```powershell
cd C:\Github\vsc-wt\d6                         # branch bench/d6-turnkey-prep: has the scripts
$B = 'missileer'                               # or 'missileer2' (Q9)
$I = 'C:\Github\missileer-overnight\d6\images' # pre-built images, each with a MANIFEST.txt
python scripts/board-port.py --list            # lists ports only; opens none
```

Every flash is `d6-flash-image.py`: it resolves the board **by USB serial** (never a COM number),
refuses the Blipscope soak board and any non-Missileer board, writes **only app0 (0x20000)**, never
NVS (Wi-Fi and identity survive; station 4 needs the stored Wi-Fi), and stops if the board's table
or otadata would make that write wrong. Every capture is `d6-capture.py`: same board resolution,
raw bytes to `bench-logs\`, a `.meta.txt` sidecar tying it to the image, and it **refuses a board
still on a TinyUSB (Missileer) build**, which means the flash did not take.
Stop a capture with **Ctrl+C**. Nothing you type reaches the board.

## Before you start (runbook)

> note the room temperature, whether the board is enclosed or open, and whether it is on a hub or
> straight into the machine. Every station prompts for these and **cannot** self-report them.

Room temp ________ · ☐ enclosed ☐ open · ☐ hub ☐ direct · (an invented ambient is worse than a blank)

---

## Station 1 — gesture capture (~15 min) · unblocks D3 and the crew layer

```powershell
python scripts/d6-flash-image.py --board $B --image "$I\station1-gesture"
python scripts/d6-capture.py     --board $B --name gesture --image "$I\station1-gesture"
```

- ☐ flash ended `WRITTEN + VERIFIED` · ☐ capture printed `boot captured` (if not: Ctrl+C, re-run with `--reset`)
- ☐ provenance in the file: `# elf_sha256` equal to the image's MANIFEST (the sidecar says `MATCH`), `# variant S3_128`, `# partition app0 @ 0x020000`
- ☐ *"confirm the instrument can see the thing present — touch the panel and watch rows appear before you start stroke 1."*

Trace the ring, leaving a pause between strokes. The probe marks each one `# --- stroke N ended ---`.
**The log cannot hear you** (Q6), so write each stroke down here as you go:

| # | runbook step | done | note | | # | runbook step | done | note |
|---|---|---|---|---|---|---|---|---|
| 1 | clean slow turn | ☐ | ____ | | 8 | slow turn (repeat) | ☐ | ____ |
| 2 | clean fast turn | ☐ | ____ | | 9 | slow turn (repeat) | ☐ | ____ |
| 3 | arc too short | ☐ | ____ | | 10 | slow turn (repeat) | ☐ | ____ |
| 4 | wrong direction | ☐ | ____ | | 11 | 10 s hold (repeat) | ☐ | ____ |
| 5 | wanders out of band | ☐ | ____ | | 12 | 10 s hold (repeat) | ☐ | ____ |
| 6 | **10 s hold** | ☐ | ____ | | 13 | 10 s hold (repeat) | ☐ | ____ |
| 7 | second finger mid-turn | ☐ | ____ | | | | | |

**Hold durations — report them even when they are fine** (the sidecar's per-stroke `first->last` lines):
hold 1 ______ s · hold 2 ______ s · hold 3 ______ s · hold 4 ______ s

> **STOP AND REPORT — this one is the answer, not a bad run.** **If strokes die at ~5 s, including
> the 10 s hold: the run is GOOD and the result is important. Stop and report before continuing.**
> Do **not** re-flash and try again. Keep the capture, note how long each hold survived, and say so.

☐ strokes died at ~5 s: STOPPED, holds survived ______ / ______ / ______ / ______ s

## Station 2 — screen state (~3 min) · unblocks D4 · *only if Q1 = run*

```powershell
python scripts/d6-flash-image.py --board $B --image "$I\station2-screenstate-CANDIDATE"
python scripts/d6-capture.py     --board $B --name screenstate --image "$I\station2-screenstate-CANDIDATE"
```

- ☐ `WRITTEN + VERIFIED` · ☐ `# elf_sha256` matches the MANIFEST (the sidecar says `MATCH`)
- ☐ **looked at the panel at least once while it ran** — what it showed: ______________________
- ☐ the capture ends `# ---- done. four scripts captured. ----` · ☐ skipped by ruling (Q1)

## Station 3 — frame budget (~5 min) · unblocks D5

```powershell
python scripts/d6-flash-image.py --board $B --image "$I\station3-blit"
python scripts/d6-capture.py     --board $B --name blit --image "$I\station3-blit"
```

Runs on its own; stop the capture after `[blit] === done.`. **This probe prints no provenance
block** (Q4): the sidecar and the image MANIFEST are its provenance.

| wanted (runbook) | line in the capture | value |
|---|---|---|
| p95 for a full-panel 240×240 push (Q5) | `[blit] 6. backbuffer -> panel (SPI)` p95 | ______ ms |
| | `[blit] 2. blit 240x240 (full-bleed)` p95 | ______ ms |
| heap headroom with a 240² sprite live | `[blit] heap with BOTH sprites` | psram_free ______ largest ______ tlsOk __ |

## Station 4 — forced arm runs (~15 min) · unblocks the crew layer

Three runs, each captured separately. Follow the on-screen instructions. The results template asks for
**≥10 holds per arm** ([gametest-results-template.md](gametest-results-template.md)).
These builds print **no provenance block** (Q4), and their arm is set at build time (Q2):
`GAMETEST_ARM` 0 = A, 1 = B, 2 = C.

```powershell
python scripts/d6-flash-image.py --board $B --image "$I\station4-armA"
python scripts/d6-capture.py     --board $B --name armA --image "$I\station4-armA"
python scripts/d6-flash-image.py --board $B --image "$I\station4-armB"
python scripts/d6-capture.py     --board $B --name armB --image "$I\station4-armB"
python scripts/d6-flash-image.py --board $B --image "$I\station4-armC"
python scripts/d6-capture.py     --board $B --name armC --image "$I\station4-armC"
```

| arm | `REG,arm` honoured=1, forced=1 | runs | clean | clean % | dropouts | longest ms | verdict line (Q3) |
|---|---|---|---|---|---|---|---|
| **A** factory, 0xFE=1 | ☐ | ___ | ___ | ___ | ___ | ___ | ☐ ______ |
| **B** sleep armed, 0xFE=0 | ☐ | ___ | ___ | ___ | ___ | ___ | ☐ ______ |
| **C** chip poll, 0xFE=0 | ☐ | ___ | ___ | ___ | ___ | ___ | ☐ ______ |

(from each capture's last `HOLD,summary` line, which the sidecar copies)

---

## What a bad run looks like (runbook, verbatim): tick any that apply, then **redo the capture**

*(Everything below means **redo the capture**. The STOP AND REPORT line above does not.)*

| ☐ | station | the run is BAD if | seen in |
|---|---|---|---|
| ☐ | **all** | the provenance block says `elf_sha256 UNAVAILABLE` or `variant UNSET` | ____ |
| ☐ | **all** | you filled the `<fill in>` lines with a guess | ____ |
| ☐ | **1** | **no** `state=1` rows at all, or `x,y` never change while you are touching | ____ |
| ☐ | **1** | `dt_us` is consistently >20,000 on every row | ____ |
| ☐ | **1** | fewer than ~8 strokes, or no `# stroke N ended` markers | ____ |
| ☐ | **2** | the panel stays black for the whole run | ____ |
| ☐ | **2** | fewer than four `# ---- script:` blocks | ____ |
| ☐ | **3** | `psram_total` reads 0 | ____ (Q4: BlitProbe prints no `psram_total`) |
| ☐ | **3** | p95 within a few percent of the mean across every primitive | ____ |
| ☐ | **4** | the run ends without printing a verdict line | ____ (Q3: which line?) |

> the touch IC is wedged (station 1, no `state=1` rows): *Re-flash and start over — do **not** record
> it as "the panel does not respond"*. Station 2 black panel: *Re-flash; suspect a wrong-SKU image*.
> Station 4 with no verdict line: *inconclusive. It is **not** a "no"*.

Redos: ________________________________________________

---

## After the sitting

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\d6-collect-logs.ps1
#   add -Station2Skipped if Q1 = skip; -Since '<start time>' if the sitting spanned >24 h
```

Copies the captures + sidecars, the flash logs, the image manifests, `git rev-parse HEAD` and this
checklist into `bench-logs\d6-<date>\`, and lists every expected item it **did not** find.
Then, per the runbook: commit the captures under `bench-logs/` and open one PR; keep the provenance
blocks at the head of `gesture-*.csv` and `blit-*.log`; **report the hold durations explicitly**.

Optional (Q10), to put the board back on Missileer (NVS preserved; the partition table is the same):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-flash.ps1 -PioEnv missileer-s3-128 -Board $B -Label missileer-restore -NoCapture
```

A TinyUSB Missileer build is monitored with **RTS low**: `pio device monitor -p (python scripts/board-port.py $B) --rts 0 --dtr 1`.

## If something goes wrong

| symptom | do |
|---|---|
| flash exit **5** | board not connected, or something still holds its port (close the capture). If a dead image left no USB: hold BOOT, tap RESET. |
| flash exit **6** | the board's partition table differs, or it boots app1. An app-only write would be wrong there. Use path (a) below (`pio upload` rewrites otadata, keeps NVS); a table that differs needs a full flash, which is Q8. |
| flash exit **1** | the write failed or was not verified. Re-run it. |
| capture: `REFUSED ... TinyUSB` | the board is still on a Missileer image: the flash did not take. |
| capture: no `boot captured` in 8 s | Ctrl+C, re-run the same line with `--reset`. |
| capture shows `waiting for download` | re-run the capture with `--reset` (DTR low at release = normal boot). |

**Path (a), the rebuild path** — `bench-flash.ps1` builds at this checkout's commit. Two builds are
not byte-identical, so the provenance `elf_sha256` will **not** match the MANIFEST (expected; the
flash log records the build). Station 2 is not buildable this way from this branch (Q1).

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-flash.ps1 -PioEnv probe-s3-128-gesture -Board $B -Label d6-station1-gesture -NoCapture
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-flash.ps1 -PioEnv probe-s3-128-blit    -Board $B -Label d6-station3-blit    -NoCapture
$env:PLATFORMIO_BUILD_FLAGS='-DGAMETEST_ARM=0'; powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-flash.ps1 -PioEnv gametest-s3-128-arm -Board $B -Label d6-station4-armA -NoCapture; Remove-Item Env:PLATFORMIO_BUILD_FLAGS
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-flash.ps1 -PioEnv gametest-s3-128-arm -Board $B -Label d6-station4-armB -NoCapture
$env:PLATFORMIO_BUILD_FLAGS='-DGAMETEST_ARM=2'; powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-flash.ps1 -PioEnv gametest-s3-128-arm -Board $B -Label d6-station4-armC -NoCapture; Remove-Item Env:PLATFORMIO_BUILD_FLAGS
```

Always `-NoCapture`: without it, bench-flash starts `bench-capture.ps1`, which opens with RTS and
DTR high and writes to a folder outside this repo. Capture with `d6-capture.py` as above.

---

## Why these commands differ from the runbook's

- `pio run -t upload` and `pio device monitor` with no port **pick one themselves**, and the
  Blipscope soak board is usually attached. Ending a monitor resets the board it was on.
- `pio run` has **no `--build-flag` option**, and `GAMETEST_ARM` is **0-based** (0=A 1=B 2=C).
  The runbook's `=3` fails the firmware's own `static_assert`, and its "arm 1" builds arm **B**.
  The A and C images were built with `PLATFORMIO_BUILD_FLAGS`. A build with `=3` failing proved that value is the one compiled.
- **Station 2's env does not compile on main** (`DrawDrill` gained `clockAgeS` in 4638a34).
  The candidate image is from draft #399.
- Each probe prints its provenance **once, at boot**, and waits at most 3 s for a host. So the
  capture opens with DTR=0 RTS=0, which resets a USB-Serial-JTAG board and puts the boot in the file.
  All four D6 envs are `ARDUINO_USB_MODE=1`. This is the **opposite** of the TinyUSB Missileer rule
  (`--rts 0 --dtr 1`), and is safe only because a TinyUSB board is refused (Q7).
- The probe's marker reads `# --- stroke N ended ---`; pio 6 exits a monitor with Ctrl+C, not Ctrl-].
  Arm captures are named `armA/B/C-*.log` (the runbook's `arm{1,2,3}`), after the arm actually running.

## Open questions (rulings; none were decided for you)

1. **Station 2**: main cannot build it. Run the #399 candidate, which passes `clock_age_s=-1` ("never synced") into the fixture, or skip it?
2. **Station 4 arms**: the runbook says arms 1, 2, 3; the code has A, B, C (0, 1, 2). Are the three runs A, B, C, in that order?
3. **Station 4 "verdict line"**: the runbook does not name it. Is it the `HOLD,summary` / `HOLD,delta` pair printed after each `HOLD,run_end`, or something else?
4. **Stations 3–4 print no provenance block**, yet the runbook's "all" rows and "keep `blit-*.log`'s provenance intact" assume one. Is the sidecar plus MANIFEST enough, or should BlitProbe gain `PrintProvenance` before the sitting? And "`psram_total` reads 0": the nearest line BlitProbe prints is `psram_free=` on its `[blit] heap` lines.
5. **"Full-panel 240×240 push"**: is that row 6 (backbuffer → panel over SPI) or row 2 (240×240 sprite blit)? Both are recorded.
6. **"Say each stroke aloud so the log has a marker"**: a serial log cannot record speech. Is the stroke table above enough, or should audio/video run alongside?
7. **Capture lines**: opening with DTR=0 RTS=0 resetting the board comes from bench-capture.ps1's 2026-08-05 measurement on a USB-Serial-JTAG s3-128, not from these images. If it does not reset, `--reset` drives esptool's own reset. Also, each capture starts with the ROM boot lines **before** the provenance block. Keep them, or trim them for the fixtures?
8. **Full flash / erase**: none planned. All four envs use `partitions-s3-16mb-bignvs.csv`, the Missileer env's table, and station 4 joins Wi-Fi from NVS. If the helper refuses a board (exit 6: a different table, or it boots app1), is a full `pio upload`, or for a different table a full flash that can move NVS, approved for that board?
9. **`missileer2`**: not in `board-port.py` on this branch (#393 is unmerged), so `$B='missileer2'` is refused until #393 merges or is merged locally for the sitting. Which board?
10. **After the sitting**: put `missileer-s3-128` back on the board, and from which commit?
