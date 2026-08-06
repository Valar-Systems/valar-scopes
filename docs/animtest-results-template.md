# Missileer flight-animation bench — results

Per-beat frame timings from `[env:animtest-s3-128]` (`src/animtest_main.cpp`
driving `src/anim/FlightAnimation.*`).

```
pio run -e animtest-s3-128 -t upload --upload-port COM119 -t monitor            # COMPRESSED
pio run -e animtest-s3-128-truetime -t upload --upload-port COM119 -t monitor   # TRUE-TIME
```

**Pin the port.** A second board is usually attached; auto-detection has already
put an image on the wrong one once.

**Open the look target beside the board.** `docs/reference/missileer-launch-animation-preview.html`
is what the firmware is being compared against, and half the questions below are
"does the glass match the browser". Every hex and every T+ mark in the firmware
came out of that file.

Board: _____________ (MAC / COM) · Date: _____________ · Firmware: `animtest-s3-128`

---

## Why this table exists

Art complexity gets tuned to what the bus sustains, **measured**. A 240×240
16 bpp frame is 115 KB; on the GC9A01's SPI bus the push alone has a floor
around **23 ms (~43 fps)** before a single pixel is composed. Every beat spends
its budget on top of that floor, and the beats do not spend equally — the
detonation deliberately fills the screen with a 46-billow cloud, the separation
flash draws a full-width streak, and midcourse draws almost nothing.

So the deliverable is **worst case per beat**, not an average. An average of
40 fps that stalls 180 ms on every separation is a sequence with a visible
hitch three times during ascent, and the average hides exactly the frame that
drops.

`compose` and `push` are reported separately because they have different fixes.
A slow **compose** is an art problem — fewer primitives, cheaper fills. A slow
**push** is a bus problem and no amount of art tuning touches it; the only
levers are a smaller dirty rect or a lower colour depth.

---

## Verdict

Copy from the `FPS,VERDICT,…` line at the end of a full run.

| Mode | Worst beat | Worst frame | Worst fps | Verdict |
|---|---|---|---|---|
| COMPRESSED | ____________ | ______ ms | ______ | ☐ ships ☐ needs tuning |
| TRUE-TIME | ____________ | ______ ms | ______ | ☐ ships ☐ needs tuning |

**Bar:** no beat worse than **50 ms** (20 fps) sustained. Motion below that
reads as a stutter rather than as slow motion, and the ascent is judged on
motion.

> **Measured floor:** `push_worst_ms` is **25.8 ms on every beat** (2026-08-06,
> s3-128) — the SPI push of a 240×240×16bpp frame, invariant and untunable. So
> compose has a **~24 ms budget**, and a beat that misses the bar is always an
> art problem here, never a bus one. Ascent beats spend 4.4–5.4 ms of it.

Past runs: [animtest-results-2026-08-06.md](animtest-results-2026-08-06.md).

---

## Per-beat table

From the `FPS,<mode>,<beat>,…` lines. One table per mode; run both.

T+ marks are the published Northrop Grumman flight sequence via the look target,
reconciled with §12 (`stage 1 ~60 s, stage 3 ~120 s, post-boost ~180 s`).

**Mode: ☐ COMPRESSED ☐ TRUE-TIME**

| # | Beat | T+ | Frames | Avg ms | **Worst ms** | Worst fps | Compose worst | Push worst | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 1 | IGNITION | 0 | | | | | | | 4 captions ride inside it |
| 2 | STAGE 1 SEP | 62 | | | | | | | staging beat |
| 3 | STAGE 2 | 65 | | | | | | | |
| 4 | SHROUD | 121 | | | | | | | clamshell, 2 s before sep |
| 5 | STAGE 2 SEP | 123 | | | | | | | staging beat + shroud still in frame |
| 6 | STAGE 3 | 126 | | | | | | | |
| 7 | STAGE 3 SEP | 177 | | | | | | | staging beat, **no ignition** — RCS answers |
| 8 | POST-BOOST | 180 | | | | | | | porcupine RCS |
| 9 | PSRE PITCH | 205 | | | | | | | nose-down through horizontal |
| 10 | RV RELEASE | 225 | | | | | | | silent |
| 11 | BUS BACKAWAY | 233 | | | | | | | |
| 12 | PENAIDS | 245 | | | | | | | |
| 13 | MIDCOURSE | 260 | | | | | | | **not** the cheapest — the match cut opens the map inside it |
| 14 | REENTRY | 1806 | | | | | | | own sky gradient + cloud deck |
| 15 | DETONATION | 1896 | | | | | | | **expected worst — 46 puffs, full-screen** |
| 16 | MATCH CUT | — | | | | | | | map opens on the dot |

---

## The four art questions the numbers answer

**1. Does the separation flash cost a hitch?** It draws a full-width anamorphic
streak at the joint for 700 ms, over everything else. If STAGE 1/2/3 SEP show a
worst frame well above their neighbours, the streak is the cause and the fix is
to draw it at half vertical resolution rather than to shorten it — §11 locks the
flash, not its pixel count.

Worst sep frame: ______ ms · neighbour beats: ______ ms · **hitch? ☐ yes ☐ no**

**2. Can the detonation stay full-screen?** §11 is explicit that it is
full-screen ("at this point the frame is the event, and a fireball that politely
stays inside a viewport is a firework"), so the lever is a **cheaper cloud, never
a smaller one**. Constants at the top of `FlightAnimation.cpp`, in **measured**
order of effect:

| Constant | Default | Effect | What you lose |
|---|---|---|---|
| `kHaloRings` | 5 | **−19 ms** | the warm wash around the head. Was 10 rings to 1.9 × capR — a 215 px radius on a 240 px screen, ~558k px of nearly full-screen overdraw |
| `kPuffRings` | 5 | ~−4 ms | internal shading on each billow |
| `kCrownPuffs` | 16 | ~−3 ms | churn in the outer head; below ~10 it reads as a dome |
| `kStemPuffs` | 14 | small | flutes in the column |
| `kSkirtPuffs` | 8 | small | the ground dust roll (also drives the brush silhouettes) |

> **This order was wrong until it was measured.** The first version of this doc
> put `kPuffRings` first on the reasoning that 46 billows must be the expensive
> thing. They are about a fifth of what the halo was spending on a wash you can
> barely see. Re-measure before re-ordering.

Detonation worst: ______ ms · levers spent: ______

**3. Does the Earth limb scale?** It draws column-wise — 240 columns × four
spans plus seven cloud ellipses — and it is on screen for every ascent beat. It
should be a flat cost across beats 1–13; if it is not, something else is the
variable.

Limb-bearing beats spread (max − min worst ms): ______

**4. Do the captions cost anything, and do they fit?** The lower third is drawn
every frame. Text is cheap, but it is 27 characters at the widest and the face
is **round** — the rows were moved to y=192/204 for exactly this reason (see the
arithmetic in `DrawCaption`). Check the longest ones on glass:

- `T+45 - SECOND ROLL MANEUVER` (line 1, 162 px into a 192 px chord) — clipped? ☐ no ☐ yes
- `MANEUVER TO WINDOW IN SPACE` (line 2, 162 px into a 171 px chord) — clipped? ☐ no ☐ yes
- Caption cost, if separable: ______ ms

---

## Look-target match

Open the preview in a browser and step the same beat on both. The firmware is
supposed to be the same picture, not merely the same idea.

| | Preview | Glass | Match? |
|---|---|---|---|
| Vehicle reads as **three olive stages with tan bands** | ✓ | | ☐ |
| Limb has a **bright atmospheric rim**, not a flat blue edge | ✓ | | ☐ |
| Separations are **axial** — spent stage recedes on the flight line | ✓ | | ☐ |
| Clamshell halves are **still in frame** at stage 2 sep | ✓ | | ☐ |
| Post-boost RCS reads **blue and cold**, never like a motor | ✓ | | ☐ |
| RV release is **silent** — no flash, no streak, no embers | ✓ | | ☐ |
| Mushroom cloud is a **churned cluster**, not a dome | ✓ | | ☐ |
| Cloud **cools to rust** by the end of the beat | ✓ | | ☐ |

Known deviations (all five are documented at the top of `FlightAnimation.cpp` —
confirm they read as acceptable rather than as bugs):

1. `IGNITION` caption is paper white, not the preview's `#ffd23e`. **Does the
   caution read as over-caution on glass?** ☐ keep ☐ revert to yellow
2. No silo / ground camera — the sequence opens already airborne.
3. No world map behind the match cut (graticule globe stands in).
4. Alpha approximated: opaque puffs, pre-blended washes, flashes collapse
   instead of fading. **Does any of it look wrong in motion?** ______
5. No pre-launch or credits phases.

---

## True-time findings

The question COMPRESSED cannot answer: **does it feel right at the published
marks?** The whole §7 flight-director design rests on this and it is not
provable from a fast preview.

- Ignition → stage 1 sep is **62 s** of one continuous burn, now carrying four
  captions (T+3 pitch, T+10 first roll, T+19 Mach 1, T+39 Mach 3, T+45 second
  roll). Do they carry it, or is it still dead air? ______
- The **1 s** staging coast at real speed — reads as suspense, or as a bug? ______
- Shroud at T+121 and stage 2 sep at T+123 are **two seconds apart**. Does that
  read as two events or as one messy one? ______
- Stage 3 sep has **no ignition** at the end of its coast — the bus answers with
  cold gas. Does the missing payoff land as intended, or as a dropped beat? ______
- Midcourse is **26 minutes**. §7 hands the screen back to monitoring here; does
  the hand-back land, or does the flight feel abandoned? ______
- Terminal re-escalation at real **T−90 s** — earned, or startling? ______
- Detonation at the wall-clock impact second (T+1,896): ______

> Note the coast is **1 s in both modes** by design (see the TIME MODES block in
> `FlightAnimation.cpp`). If it reads differently between modes, that is a
> finding about the surrounding pacing, not about the coast.

---

## Match-cut check

§11's rule: *"Ascent ends by shrinking the vehicle to a single dot; the map opens
with that same dot… it stops working the instant either side redraws the dot
differently."*

The rig draws the dot from one function on both sides of the cut, at one size and
one colour with no blink, so this should pass by construction — the check is that
it still does.

- Dot position identical across the cut? ☐ yes ☐ no
- Dot size identical? ☐ yes ☐ no
- Does the cut read as one flight? ☐ yes ☐ no

---

## Palette check

§11 reserves amber (`#ffb000`) for EXERCISE traffic and nothing else.

- Amber anywhere in the sequence or the rig chrome? ☐ no ☐ **yes — file it**
- Detonation ramps read as Hood/Badger fire, not as a UI accent? ☐ yes ☐ no

---

## Raw capture

`bench-logs/animtest-<date>.log` — the CSV stream (`FRAME,` / `BEAT,` / `FPS,` /
`TOUCH,`).

**Attach a reader before the run.** With nothing draining USB-CDC the serial
buffer fills and the loop stalls on the print — which corrupts the frame times,
the one thing this rig exists to produce. `gametest` lost a bench day to exactly
that (20 042 ms worst poll gap unattended vs 43 ms with a reader attached); the
guard is in `setup()` here too, and a reader is still the right habit.
