# Missileer flight-animation bench — results

Per-beat frame timings from `[env:animtest-s3-128]` (`src/animtest_main.cpp`
driving `src/anim/FlightAnimation.*`).

```
pio run -e animtest-s3-128 -t upload --upload-port COM119 -t monitor            # COMPRESSED
pio run -e animtest-s3-128-truetime -t upload --upload-port COM119 -t monitor   # TRUE-TIME
```

**Pin the port.** A second board is usually attached; auto-detection has already
put an image on the wrong one once.

Board: _____________ (MAC / COM) · Date: _____________ · Firmware: `animtest-s3-128`

---

## Why this table exists

Art complexity gets tuned to what the bus sustains, **measured**. A 240×240
16 bpp frame is 115 KB; on the GC9A01's SPI bus the push alone has a floor
around **23 ms (~43 fps)** before a single pixel is composed. Every beat spends
its budget on top of that floor, and the beats do not spend equally — the
detonation deliberately fills the screen, the separation flash draws a
full-width streak, and midcourse draws almost nothing.

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

---

## Per-beat table

From the `FPS,<mode>,<beat>,…` lines. One table per mode; run both.

**Mode: ☐ COMPRESSED ☐ TRUE-TIME**

| # | Beat | Frames | Avg ms | **Worst ms** | Worst fps | Compose worst | Push worst | Notes |
|---|---|---|---|---|---|---|---|---|
| 1 | IGNITION | | | | | | | |
| 2 | STAGE 1 SEP | | | | | | | staging beat |
| 3 | STAGE 2 | | | | | | | |
| 4 | SHROUD | | | | | | | clamshell |
| 5 | STAGE 2 SEP | | | | | | | staging beat |
| 6 | STAGE 3 | | | | | | | |
| 7 | STAGE 3 SEP | | | | | | | staging beat |
| 8 | POST-BOOST | | | | | | | porcupine RCS |
| 9 | PSRE PITCH | | | | | | | nose-down |
| 10 | RV RELEASE | | | | | | | silent |
| 11 | BUS BACKAWAY | | | | | | | |
| 12 | PENAIDS | | | | | | | |
| 13 | MIDCOURSE | | | | | | | should be the cheapest |
| 14 | REENTRY | | | | | | | decoys burning out |
| 15 | DETONATION | | | | | | | **expected worst — full-screen** |
| 16 | MATCH CUT | | | | | | | map opens on the dot |

---

## The three art questions the numbers answer

**1. Does the separation flash cost a hitch?** It draws a full-width anamorphic
streak for 700 ms, over everything else. If STAGE 1/2/3 SEP show a worst frame
well above their neighbours, the streak is the cause and the fix is to draw it
at half vertical resolution rather than to shorten it — §11 locks the flash, not
its pixel count.

Worst sep frame: ______ ms · neighbour beats: ______ ms · **hitch? ☐ yes ☐ no**

**2. Can the detonation stay full-screen?** §11 is explicit that it is
full-screen ("at this point the frame is the event, and a fireball that politely
stays inside a viewport is a firework"). It draws ten nested filled circles at
up to 0.95 × screen. If it cannot hold the bar, the lever is **fewer rings**,
not a smaller fireball.

Detonation worst: ______ ms · rings needed to pass: ______

**3. Does the Earth limb scale?** It draws column-wise — 240 vertical spans plus
an atmosphere band per column — and it is on screen for every ascent beat. It
should be a flat cost across beats 1–12; if it is not, something else is the
variable.

Limb-bearing beats spread (max − min worst ms): ______

---

## True-time findings

The question COMPRESSED cannot answer: **does it feel right at the published
marks?** The whole §7 flight-director design rests on this and it is not
provable from a fast preview.

- Ignition → stage 1 sep is **60 s** of one continuous burn. Too long? ☐ yes ☐ no
- The ~1 s staging coast at real speed — reads as suspense, or as a bug? ______
- Midcourse is **26 minutes**. §7 hands the screen back to monitoring here; does
  the hand-back land, or does the flight feel abandoned? ______
- Terminal re-escalation at real **T−90 s** — earned, or startling? ______
- Detonation at the wall-clock impact second: ______

> Note the coast is **1 s in both modes** by design (see the TIME MODES block in
> `FlightAnimation.cpp`). If it reads differently between modes, that is a
> finding about the surrounding pacing, not about the coast.

---

## Match-cut check

§11's rule: *"Ascent ends by shrinking the vehicle to a single dot; the map opens
with that same dot… it stops working the instant either side redraws the dot
differently."*

The rig draws the dot from one function on both sides of the cut, so this should
pass by construction — the check is that it still does.

- Dot position identical across the cut? ☐ yes ☐ no
- Dot size identical? ☐ yes ☐ no
- Does the cut read as one flight? ☐ yes ☐ no

---

## Palette check

§11 reserves amber (`#ffb000`) for EXERCISE traffic and nothing else.

- Amber anywhere in the sequence or the rig chrome? ☐ no ☐ **yes — file it**
- Detonation ramp reads as Hood/Badger fire, not as a UI accent? ☐ yes ☐ no

---

## Raw capture

`bench-logs/animtest-<date>.log` — the CSV stream (`FRAME,` / `BEAT,` / `FPS,` /
`TOUCH,`).

**Attach a reader before the run.** With nothing draining USB-CDC the serial
buffer fills and the loop stalls on the print — which corrupts the frame times,
the one thing this rig exists to produce. `gametest` lost a bench day to exactly
that (20 042 ms worst poll gap unattended vs 43 ms with a reader attached); the
guard is in `setup()` here too, and a reader is still the right habit.
