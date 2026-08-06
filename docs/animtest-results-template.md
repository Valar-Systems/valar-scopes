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

## Three standing facts about this panel

Measured on the S3 1.28" Kit, and true for every screen this product draws — not
just the animation. Budget against these, not against intuition.

**1. The SPI push floor is 25.8 ms.** A 240×240 16 bpp frame is 115 KB on a
40 MHz bus. Nothing composed can beat it, so ~24 ms is the entire compose budget
against a 50 ms bar. Every budget question is an art question.

**2. Cost is per line-PIXEL, not per line — 1.06 µs/px.** Measured:
`drawLine` of 3 px is 2.45 µs and of 8 px is 7.76 µs, so setup is negligible and
length is everything. **This inverts eyeball budgeting.** A dozen long graticule
lines outweigh hundreds of short coastline segments: 12 meridians + 5 parallels
cost 3.75 ms, more than every coastline on the planet. When something is too
slow, count the pixels the strokes cover, not the strokes.

For contrast, span fills are ~37× cheaper per pixel: `fillScreen` 240×240 is
3.30 ms, i.e. **28.6 ns/px**. A full-disc `fillCircle` costs less than a thousand
pixels of stroke.

**3. Sprite caching does not pay at full-screen size.** `pushSprite` of a
240×240 PSRAM sprite is **6.24 ms** — *more than drawing the content live* — and
it spends 115 KB to do it. Nothing full-screen should be pre-rendered. This
killed a planned static-map cache before it was built; the globe that replaced
it draws live every frame and is cheaper.

> All three come from the `BENCH,` lines `animtest_main.cpp` prints at boot under
> `-DANIM_PROFILE`. Re-run them before trusting any of the above on a new panel
> or a new LovyanGFX version.

Past runs: [animtest-results-2026-08-06.md](animtest-results-2026-08-06.md).

---

## Per-beat table

From the `FPS,<mode>,<beat>,…` lines. One table per mode; run both.

T+ marks are the published Northrop Grumman flight sequence via the look target,
reconciled with §12 (`stage 1 ~60 s, stage 3 ~120 s, post-boost ~180 s`).

**Mode: ☐ COMPRESSED ☐ TRUE-TIME**

`smoke worst` is the LIFTOFF ground-smoke pass timed on its own, inside the
module, behind `-DANIM_PROFILE`. It is carried on **every** row so the column is
a constant width — and so the zeros on the other sixteen rows are themselves the
assertion that no other beat pays for it.

> **LIFTOFF is the one beat with a `preRollMs`.** It runs **T-4 → T+10**: the
> locking pin and the closure door move before first-stage ignition, and T+0
> *is* ignition. `BeatTrueStartMs` subtracts the pre-roll and `TPlusMs` holds at
> T+0 through it, so the door can take as long as it needs without moving a mark
> downstream. If STAGE 1 SEP ever logs anything but ~62,000, that subtraction is
> the first place to look.

| # | Beat | T+ | Frames | Avg ms | **Worst ms** | Worst fps | Compose worst | Push worst | Smoke worst | Notes |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | LIFTOFF | −4 → 10 | | | | | | | | ground camera; **only beat with smoke, and the only one with a pre-roll** |
| 2 | STAGE 1 | 10 | | | | | | | 0 | chase cam; 3 captions ride inside it |
| 3 | STAGE 1 SEP | 62 | | | | | | | 0 | staging beat |
| 4 | STAGE 2 | 65 | | | | | | | 0 | |
| 5 | SHROUD | 121 | | | | | | | 0 | clamshell, 2 s before sep |
| 6 | STAGE 2 SEP | 123 | | | | | | | 0 | staging beat + shroud still in frame |
| 7 | STAGE 3 | 126 | | | | | | | 0 | |
| 8 | STAGE 3 SEP | 177 | | | | | | | 0 | staging beat, **no ignition** — RCS answers |
| 9 | POST-BOOST | 180 | | | | | | | 0 | porcupine RCS |
| 10 | PSRE PITCH | 205 | | | | | | | 0 | nose-down through horizontal |
| 11 | RV RELEASE | 225 | | | | | | | 0 | silent |
| 12 | BUS BACKAWAY | 233 | | | | | | | 0 | |
| 13 | PENAIDS | 245 | | | | | | | 0 | |
| 14 | MIDCOURSE | 260 | | | | | | | 0 | **not** the cheapest — the match cut opens the **globe** inside it |
| 15 | REENTRY | 1806 | | | | | | | 0 | own sky gradient + cloud deck |
| 16 | DETONATION | 1896 | | | | | | | 0 | **expected worst — 46 puffs, full-screen** |
| 17 | MATCH CUT | — | | | | | | | 0 | map opens on the dot |

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

**2b. What does the smoke cost, on its own?** LIFTOFF's ground smoke is the only
new particle system in the sequence and the only per-beat draw with genuinely
open-ended cost, so it is timed separately (`smoke_worst_ms`, above). Its
**worst** frame is the number, not the beat average — the cloud exists for about
half the beat and peaks for a fraction of that, so an average reports a system
that is free right up until the frame that drops.

The cloud is bounded by arithmetic rather than by a cap: the spawn window is
fixed at 0.45–4.55 pad-seconds and the spacing at 0.085 s, so it admits **49
puffs, ever**, at any frame rate or time mode. Levers, in the order to spend
them:

| Constant | Default | Effect | What you lose |
|---|---|---|---|
| `kSmokeDtS` | 0.085 | linear in puff count | density — the column stops reading as solid |
| `kSmokeRMax` | 34 | caps the late, faint, largest discs | the billowing head |
| `kSmokeGrowth` | 12 px/s | shrinks every puff at once | volume; the column becomes a rope |
| `kSmokeRise` | 30 px/s | column height | **spend last** — this is the NG video's column vs the preview's ground bank |

> `smoke_worst_ms` on a beat that draws no smoke must be **0.0**. It is zeroed
> per frame in `Render()` for exactly this reason: the counter is only written by
> `DrawSmoke`, so without that a stale sample crosses the beat boundary into the
> next beat's max. It did — 1.9 ms on STAGE 1 — and it read a convincing 0.0 the
> first time it was measured, because that beat happened to end after the last
> puff died. A non-zero in this column on any row but LIFTOFF is an instrument
> fault, not a finding.

Smoke worst: ______ ms · of a compose worst of ______ ms · **cut? ☐ no ☐ yes**

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

## Vehicle scale — the pad is the reference

LIFTOFF is where the vehicle is largest in frame, so it is where the scale
language is **established**; every later state is measured against it. The chase
cam opens on the same 74 px, deliberately — a cut that changed the subject's size
would break the same-object read as surely as a colour change would.

Measured off `kNoseAlong`→tail, which is what `DrawVehicle` actually draws.
1 px = 0.135 mm on this panel.

| State | px | mm | vs pad | Legible on glass? |
|---|---|---|---|---|
| Pad / full stack + bus + shroud | 74 | 10.0 | 1.00 | ☐ |
| After stage 1 | 52 | 7.0 | 0.70 | ☐ |
| After stage 2 (shroud gone, RV cone) | 40 | 5.4 | 0.54 | ☐ |
| After stage 3 (bus + cone), ×2 boost | 52 | 7.0 | 0.70 | ☐ |
| RV alone, ×2 boost | 36 | 4.9 | 0.49 | ☐ |

Without `kSubjectBoost` the last two are 26 px / 3.5 mm and 18 px / 2.4 mm — 0.35
and 0.24 of the pad silhouette, for the five beats the RV is the subject of. With
it, the post-stack vehicle is the same apparent size it was after the first
separation and nothing ever falls below half the pad.

> **These numbers moved once already.** An earlier version of this table read
> 31 px / 17 px / 14 px for the last three rows. Those were the *reference's*
> geometry, where the shroud and the RV are one 14 px cone; the rig gives the RV
> its own 18 px cone (the jettison is a real reveal), so every state after the
> shroud goes is larger than the reference's. Measure the rig, not the preview.

---

## The globe — what to look at

The far side of the match cut is an **orthographic sphere**, fixed orientation,
tilted 30° off the great-circle plane. Only the last of these is checkable from a
log.

- Does the great circle read as an **arc**? (bow should be ~17 px). If it looks
  straight, the tilt has been lost — see `kGlobeTilt`, where φ=0 gives exactly
  0.0 px of bow and is the natural-looking wrong answer.
- Are **both endpoints** on the visible hemisphere, at ~80 % of the disc radius?
  Two fixed marks with a dot crawling between them is the entire reason the
  camera does not follow the vehicle.
- Does the **dot sit on the arc**, on both sides of the cut?
- Is the stroke hierarchy legible at desk distance — track brightest, then
  coastlines, then graticule, then ocean? ☐ yes ☐ mush
- Do 6 meridians + 3 parallels read as "sphere" without clutter? If the sphere
  cue is weak, the limb circle is the thing to strengthen, **not** the graticule
  count (see standing fact 2 — meridians are the most expensive pixels here).
- Are the continents recognisable? 1,306 vertices at 0.5° spherical tolerance.

---

## Look-target match

Open the preview in a browser and step the same beat on both. The firmware is
supposed to be the same picture, not merely the same idea.

| | Preview | Glass | Match? |
|---|---|---|---|
| **Locking pin retracts first**, then the slab goes — reads as a mechanism, not a drawer | — | | ☐ |
| **Launcher Closure Door slides sideways and clean off the frame** (*launch footage; in neither the NG animation nor the preview*) | — | | ☐ |
| Door takes **~1.8 s** — you can see 110 tons moving, not a panel snapping aside | — | | ☐ |
| **Ignition happens after the door clears, with the vehicle still below grade** | — | | ☐ |
| Vehicle is **fully buried** before first motion, then hot-launches | ✓ | | ☐ |
| Fire has **no straight edges anywhere** — see the note below | — | | ☐ |
| The vehicle is **hidden inside the fire** and emerges from the **top** of it | — | | ☐ |
| Smoke builds a **tall vertical column**, not a low bank (*NG video, not the preview*) | ✓ | | ☐ |
| Camera **shakes** at ignition and settles by ~2.2 s | ✓ | | ☐ |
| The launch is **slow enough to watch** — ~4.9 s of visible transit | — | | ☐ |
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
2. Pad altimeter re-derived from the published mark (`8,300 × (t/19)^2.6`)
   instead of the preview's own `×38 ft/px`, which reaches 273,000 ft at a
   caption that says T+10. The **motion curve** is the preview's, unchanged.
3. No world map behind the match cut (graticule globe stands in).
4. Alpha approximated: opaque puffs, pre-blended washes, flashes collapse
   instead of fading. **Does any of it look wrong in motion?** ______
5. No pre-launch or credits phases.

### A straight edge is the tell — read this before drawing any fire

Caught on glass 2026-08-06 and worth generalising. The silo fire was three nested
`fillTriangle` frusta, and on the panel it was **an orange rectangle with two
boxes inside it.** The taper was there in the maths (1.55 → 0.95 half-width) and
invisible on the panel.

The dominant fault was not the taper: a triangle pair gives a **flat horizontal
top**, and *one straight edge is enough to make a shape read as geometry rather
than as fire.* Supporting faults, all of which only mattered because of that one:
a 39 % narrowing over 76 px is indistinguishable from vertical sides at 240 px
with no antialiasing; three layers of similar width read as concentric
rectangles, not a gradient; and once the height collapsed, 43 × 40 px is a square
whatever the taper says.

The diagnostic was in the same photograph: **the vehicle's own plume looked
fine**, and the difference is that `DrawPlume`'s teardrops come to a *point*.

So, for anything on this panel that is meant to read as fire, smoke or plasma:

- **no flat terminating edge.** Points (`DrawPlume`) or round blobs (`DrawSmoke`,
  `DrawDetonation`, and now the silo fire). Never a frustum.
- **irregularity is not optional** at this resolution — jitter the axis, vary the
  radii. A symmetric solid of revolution reads as a drawn object.
- **overlapping blobs give raggedness for free**, and let fire hand off to smoke
  as one continuous thing rather than two stacked shapes.

### LIFTOFF, and the one thing to actually look at

The cut out of the ground camera is a **cut on absence** — the vehicle leaves
frame at ~39 % of the beat, the shot holds on smoke, and the cut is motivated by
the subject having gone. Nothing has to match across it *except* the vehicle
itself, which is why the pad and the chase cam draw the **same object at the same
size in the same paint** (`Lit()` over `sunLift_`, not a second palette).

That is the failure mode to hunt for, and it is only visible in motion:

- Step LIFTOFF → STAGE 1 across the boundary. Does the vehicle read as **one
  object under changing light**, or as **two different objects**? ☐ one ☐ two
- Is the daylight fade (first 25 % of STAGE 1) **invisible**, or can you catch it
  happening? ☐ invisible ☐ visible
- Does the vehicle **emerge from a hole**, or slide up past a line? ☐ hole ☐ line
- Is the held ground shot (39 %→100 %) **still alive** with smoke, or does it go
  empty before the cut? ☐ alive ☐ empty

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

### Polarity — do this FIRST, before judging any other colour

**The GC9A01 boots inverted.** `src/main.cpp` undoes it per-variant and the bench
TUs did not, so for one session every colour on this rig was the exact complement
of what the code wrote — and it is invisible from the build, the serial log and
the frame timings. The only symptom is looking at it.

The standing check is the match-cut dot, because it is a known hex on a known
ground:

| | Correct | Inverted |
|---|---|---|
| Match-cut dot (`pal::Red()`) | **`#FF3B30`** red | **`#00C4CF`** cyan |
| Space background | black | white |
| Vehicle stages | olive | blue-grey |
| Earth limb | blue | orange |

**Dot cyan, or background white ⇒ stop.** Nothing else on this page means
anything until polarity is right. The boot log now prints `invert=` — confirm it
matches the variant's `BLIPSCOPE_DISP_INVERT`.

- Polarity correct? ☐ yes ☐ **no — stop, nothing below is valid**

### Amber

§11 reserves amber (`#ffb000`) for EXERCISE traffic and nothing else.

- Amber anywhere in the sequence or the rig chrome? ☐ no ☐ **yes — file it**
- Detonation ramps read as Hood/Badger fire, not as a UI accent? ☐ yes ☐ no

---

## Standing rule: the reference cannot vouch for legibility

**The look target is authoritative for WHAT and WHEN, never for WHETHER IT CAN BE
SEEN.** It is authored on a 240×240 canvas displayed at **480 CSS px** on a bright
laptop; the panel is 240 px across ~32 mm of glass at desk distance, where 1 px is
**0.135 mm**. Choreography, beat timing and palette intent port faithfully.
Legibility is a device-side judgment that overrides the reference, and *"the
reference does it this way"* is never a defence for something invisible on the
panel.

So when a match row below fails, ask which kind of failure it is:

- **Wrong shape / wrong moment / wrong hue** → the port is wrong, fix the port.
- **Right in the browser, unreadable on glass** → the reference is wrong, fix it
  device-side and record it in the `.cpp`'s DEVIATIONS block.

Sizes as ported, for reference (1 px = 0.135 mm):

| State | px | physical |
|---|---|---|
| Full stack + bus + shroud | 74 | 10.0 mm |
| After stage 1 | 52 | 7.0 mm |
| After stage 2 (shroud already gone) | 31 | 4.2 mm |
| After stage 3 — bus + cone | 17 | 2.3 mm |
| RV alone | 14 | **1.9 mm** |

---

## Raw capture

`bench-logs/animtest-<date>.log` — the CSV stream (`FRAME,` / `BEAT,` / `FPS,` /
`TOUCH,`).

**Attach a reader before the run.** With nothing draining USB-CDC the serial
buffer fills and the loop stalls on the print — which corrupts the frame times,
the one thing this rig exists to produce. `gametest` lost a bench day to exactly
that (20 042 ms worst poll gap unattended vs 43 ms with a reader attached); the
guard is in `setup()` here too, and a reader is still the right habit.
