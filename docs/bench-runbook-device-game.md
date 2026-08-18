# Bench runbook — the device game (D6)

**One sitting, four stations, ~40 minutes.** Everything the device-game work is
blocked on, bundled, because the bottleneck is a person and a board rather than
software. Do them in order; each is a `pio run` and a serial capture.

Board: **S3 1.28" Kit** (`blipscope-s3-128`), GC9A01 240×240, CST816D.
Every station is the shipping board config with the app swapped out — same
variant, same PSRAM, same partition table — so the bench board's NVS survives
the trip back to a radar build.

> **Before you start:** note the room temperature, whether the board is enclosed
> or open, and whether it is on a hub or straight into the machine. Every
> station prompts for these and **cannot** self-report them. Two minutes now, or
> the fixtures are not re-derivable later.

---

## Station 1 — gesture capture (~15 min) · unblocks **D3**

```sh
pio run -e probe-s3-128-gesture -t upload
pio device monitor -b 115200 | tee bench-logs/gesture-$(date -u +%Y%m%d-%H%M).csv
```

A grey ring appears with a white mark at 12 o'clock. **Trace it with a finger**,
saying each stroke aloud so the log has a marker, and leave a pause between
strokes (the probe writes a blank line and a `# stroke N ended` comment):

1. one **clean slow turn**, most of the way round
2. one **clean fast turn**
3. an **arc too short** — a quarter turn and lift
4. a **wrong-direction** turn (anticlockwise)
5. a drag that **wanders out of the band** and comes back
6. a **10 second hold**, dead still, no movement at all
7. a **second finger** arriving mid-turn
8. repeat 1 and 6 **three more times each** — those two carry the numbers

Then `Ctrl-]` and keep the CSV.

## Station 2 — screen state (~3 min) · unblocks **D4**

```sh
pio run -e probe-s3-128-screenstate -t upload
pio device monitor -b 115200 | tee bench-logs/screenstate-$(date -u +%Y%m%d-%H%M).log
```

Runs on its own — four scripted drills, no input needed. **Look at the panel at
least once while it runs.** The log records what the renderer *decided*, not
what the panel showed; a dead backlight or a wrong-SKU flash produces a perfect
log and a black screen.

## Station 3 — frame budget (~5 min) · unblocks **D5**

```sh
pio run -e probe-s3-128-blit -t upload
pio device monitor -b 115200 | tee bench-logs/blit-$(date -u +%Y%m%d-%H%M).log
```

Runs on its own. The numbers wanted are **p95 for a full-panel 240×240 push**
and **heap headroom with a 240² sprite live**. The launch animation is designed
against these, not against an assumed budget.

## Station 4 — forced arm runs (~15 min) · unblocks **the crew layer**

Three runs. §13 B gates the whole four-hand design on whether this panel can
hold a finger for 10 s.

```sh
pio run -e gametest-s3-128-arm -t upload                                  # arm 1
pio run -e gametest-s3-128-arm -t upload --build-flag="-DGAMETEST_ARM=2"  # arm 2
pio run -e gametest-s3-128-arm -t upload --build-flag="-DGAMETEST_ARM=3"  # arm 3
```

Capture each separately — `bench-logs/arm{1,2,3}-$(date -u +%Y%m%d-%H%M).log` —
and follow the on-screen instructions. The `--build-flag` on arms 2 and 3 is the
one incantation left in this runbook; it is here because the arm number is the
variable under test.

---

## What a bad run looks like

**A dead instrument produces confident numbers, and that has cost this project a
week once already** (the 75.7 ms clock floor came from a contaminated session,
was ruled on, and was wrong). Redo rather than ship any of these:

| station | the run is BAD if | why it matters |
|---|---|---|
| **all** | the provenance block says `elf_sha256 UNAVAILABLE` or `variant UNSET` | the capture cannot be tied to a build or a board; it is unusable as a fixture |
| **all** | you filled the `<fill in>` lines with a guess | an invented ambient is worse than a blank |
| **1** | **no** `state=1` rows at all, or `x,y` never change while you are touching | the touch IC is wedged. Re-flash and start over — do **not** record it as "the panel does not respond" |
| **1** | every stroke ends within ~5 s including the 10 s hold | the chip auto-slept. The watchdog is deliberately off here; this is a **finding**, not a bad run — keep it and say so |
| **1** | `dt_us` is consistently >20,000 on every row | the bus is running slow and the sample rate is the probe's, not the panel's. Check nothing else is flashing |
| **1** | fewer than ~8 strokes, or no `# stroke N ended` markers | the corpus cannot be segmented; D3 would infer boundaries from gaps, which is the defect it must treat separately |
| **2** | the panel stays black for the whole run | the log will still look perfect. Re-flash; suspect a wrong-SKU image |
| **2** | fewer than four `# ---- script:` blocks | it did not finish; the fixture is partial |
| **3** | `psram_total` reads 0 | the sprite is in internal RAM and every number describes a machine we do not ship |
| **3** | p95 within a few percent of the mean across every primitive | suspiciously clean — check the board was not idle-throttled or the run truncated |
| **4** | the run ends without printing a verdict line | inconclusive. It is **not** a "no" |

**The general rule, and the one worth internalising:** an instrument that has
died usually reports *nothing*, and nothing reads as *a clean negative result*.
Before believing any of these captures, confirm the instrument can see the thing
present — touch the panel and watch rows appear before you start stroke 1.

---

## After the sitting

Commit the captures under `bench-logs/` and open one PR. The two that become
**fixtures** need their provenance blocks kept intact at the head of the file:

- `gesture-*.csv` → the corpus D3 is graded against
- `blit-*.log` → the budget D5 is designed against

Then the software queue unblocks: **D3** (recognizer), **D4** (render), **D5**
(animation), and §13 B's crew-layer question gets its answer either way.
