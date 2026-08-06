# Missileer flight-animation bench — results, 2026-08-06

First hardware run of `[env:animtest-s3-128]`. Template: [animtest-results-template.md](animtest-results-template.md).
Look target: [reference/missileer-launch-animation-preview.html](reference/missileer-launch-animation-preview.html).

Board: **COM119** (S3 1.28" Kit, `SER=90:70:69:31:D9:18`) · `tft.init=1 240x240` ·
backbuffer ok, `psram_free=8267884` · touch ready after 26 ms.

Three COMPRESSED runs, because the first one failed and the fix needed measuring
rather than guessing.

| Run | Change | Worst beat | Worst ms | Worst fps |
|---|---|---|---|---|
| 1 | as merged in #155 | DETONATION | **64.0** | 15.6 |
| 2 | `kHaloRings` 10 → 5, radius 1.9 → 1.35 × capR | DETONATION | **51.0** | 19.6 |
| 3 | `kHaloRings` 5 → 4 | DETONATION | **49.8** | 20.1 |
| 4 | real world map replaces the placeholder globe | DETONATION | **49.7** | 20.1 |
| 5 | attitude + plasma fixes, 8-offset caption halo | DETONATION | **50.2** | 19.9 |
| 6 | caption halo 8 → 4 offsets | DETONATION | **50.0** | 20.0 |
| 7 | Stage IV propulsion, one-piece shroud, RV/PBV continuity, midcourse choreography | DETONATION | **50.0** | 20.0 |
| 8 | airframe colours + halo radius 1.35 → 1.15 × capR — **shipping** | DETONATION | **48.5** | 20.6 |

TRUE-TIME was run at the run-3 config over the full 1,915 s; see below.

---

## Verdict

| Mode | Worst beat | Worst frame | Worst fps | Verdict |
|---|---|---|---|---|
| COMPRESSED (run 6, shipping) | DETONATION | **50.0 ms** | 20.0 | ⚠ **at the bar, not under it** |
| TRUE-TIME (run 3 config) | DETONATION | 49.8 ms | 20.1 | ⚠ same, and see the midcourse result |

**Fifteen of sixteen beats ship comfortably.** They sit at 30.2–34.2 ms
(29–33 fps) against a 50 ms bar, and the spread across the whole ascent is
1.1 ms — the art is nowhere near the limit.

**The detonation is a marginal pass and should not be called a pass.** 49.8 ms
against a 50.0 ms bar is 0.2 ms of margin, and runs 1–3 showed ±0.2 ms of
run-to-run variance on unchanged beats. The number straddles the bar. It is under
it on this board today; it is not under it with any confidence. See
[Open decision](#open-decision-the-last-lever).

---

## The push floor is real and it is most of the budget

`push_worst_ms` is **25.8 ms on every single beat**, all three runs, no exceptions
— a 240×240×16bpp frame over the GC9A01's SPI bus. Predicted ~23 ms from bus
arithmetic; measured 25.8.

That means **compose has a ~24 ms budget** before a beat misses the bar, and the
ascent beats are spending 4.4–5.4 ms of it. There is a lot of room for art on
every beat except the one that already fills the screen.

It also means no amount of art tuning helps a beat that is push-bound. Nothing
here is push-bound, so every finding below is an art finding.

---

## Per-beat table — COMPRESSED, run 7 (shipping config)

Everything: the real world map, the 4-offset caption halo, the subject-size
boost, the attitude/plasma fixes, Stage IV's two propulsion systems, the
one-piece shroud, the slender lit-rim RV, and the midcourse choreography.

| # | Beat | T+ | Frames | Avg ms | **Worst ms** | Worst fps | Compose worst | Push worst |
|---|---|---|---|---|---|---|---|---|
| 1 | IGNITION | 0 | 184 | 32.2 | 32.6 | 30.7 | 6.8 | 25.8 |
| 2 | STAGE 1 SEP | 62 | 93 | 31.8 | 32.6 | 30.6 | 6.8 | 25.8 |
| 3 | STAGE 2 | 65 | 150 | 32.9 | 33.0 | 30.3 | 7.2 | 25.8 |
| 4 | SHROUD | 121 | 76 | 32.5 | 32.5 | 30.8 | 6.7 | 25.8 |
| 5 | STAGE 2 SEP | 123 | 92 | 31.9 | 32.8 | 30.4 | 7.0 | 25.8 |
| 6 | STAGE 3 | 126 | 153 | 32.3 | 32.8 | 30.5 | 7.0 | 25.8 |
| 7 | STAGE 3 SEP | 177 | 95 | 31.1 | 31.4 | 31.8 | 5.6 | 25.8 |
| 8 | POST-BOOST | 180 | 182 | 32.5 | 32.5 | 30.7 | 6.7 | 25.8 |
| 9 | PSRE PITCH | 205 | 186 | 31.7 | 31.8 | 31.4 | 6.0 | 25.8 |
| 10 | RV RELEASE | 225 | 188 | 31.3 | **31.3** | 31.9 | **5.5** | 25.8 |
| 11 | BUS BACKAWAY | 233 | 158 | 31.3 | 31.4 | 31.9 | 5.5 | 25.8 |
| 12 | PENAIDS | 245 | 155 | 31.6 | 31.7 | 31.5 | 5.9 | 25.8 |
| 13 | MIDCOURSE | 260 | 347 | 34.0 | 36.8 | 27.1 | 11.0 | 25.8 |
| 14 | REENTRY | 1806 | 249 | 31.6 | 34.9 | 28.6 | 9.2 | 25.8 |
| 15 | DETONATION | 1896 | 242 | 45.1 | **50.0** | **20.0** | **24.2** | 25.8 |
| 16 | MATCH CUT | — | 181 | 32.8 | 36.1 | 27.7 | 10.3 | 25.8 |

Fifteen beats span **31.3–36.8 ms**. The cheapest is RV RELEASE at 5.5 ms
compose, which is a pleasing accident: §11's quietest moment is also the one that
draws the least — and it stayed the cheapest even after the RV got twice the size
and the bus grew an engine and eight thrusters.

**The detonation is at 50.0 ms against a 50.0 ms bar.** Not under it. Every art
addition since run 3 has been paid for out of the only beat with no headroom, and
the remaining lever (`kPuffRings`, billow shading on the marquee beat) is an open
look decision rather than a perf one.

**The detonation is at 50.0 ms against a 50.0 ms bar.** Not under it. Everything
added to the frame since run 3 — the world map does not touch this beat, but the
caption halo does — has been paid for out of the only beat with no headroom.

---

## Art question 1 — does the separation flash cost a hitch?

**No.** The three staging beats are 31.0 / 31.0 / 30.6 ms worst; their neighbours
are 30.9–31.0 ms. **Spread: 0.4 ms.** The full-width anamorphic streak is free at
this scale, and it does not need to be drawn at half vertical resolution.

☑ no hitch

---

## Art question 2 — can the detonation stay full-screen?

Yes, and it still is. §11's "full-screen" is intact; what changed is the halo.

**The template's fix ordering was wrong, and this is the run that proved it.** It
said to spend `kPuffRings` first, on the reasoning that 46 billows must be the
expensive thing. Measured:

| Element | Pixels/frame (est.) | Share |
|---|---|---|
| **Halo** — 10 filled circles to 1.9 × capR (≈215 px radius on a 240 px screen) | ~558k | **~63%** |
| All 46 cloud billows (cap + crown + stem + skirt) | ~127k | ~14% |
| Sky gradient (240 hlines) | ~58k | ~7% |
| Ground, shock ring, core, brush | ~18k | ~2% |

The halo was costing **4.5× the entire fireball** for a warm background wash. It
is the classic profile surprise: the expensive thing was the one that looked
cheap in the source, because a "subtle glow" drawn as nested near-full-screen
discs is 558k pixels of overdraw regardless of how subtle it looks.

| Lever | Applied | Compose | Worst frame |
|---|---|---|---|
| — | baseline | 38.3 ms | 64.0 ms |
| `kHaloRings` 10 → 5, radius 1.9 → 1.35 × capR | run 2 | 25.3 ms | 51.0 ms (−13.0) |
| `kHaloRings` 5 → 4 | run 3 | 24.0 ms | 49.8 ms (−1.2) |

Both changes are to the **background wash only**. The cloud itself — 46 billows,
5 shading rings each, full-screen, cooling to rust — is untouched from what
merged in #155.

---

## Art question 3 — does the Earth limb scale?

**Yes, flat.** Limb-bearing beats 1–12 span 30.2–31.2 ms worst. **Spread:
1.0 ms**, and it *decreases* monotonically as the flight proceeds (the limb
shrinks with altitude, so it draws fewer columns). The column-wise limb is not a
variable cost.

Beats 13/14/16 are higher (33.1 / 34.2 / 32.8) but they don't draw the limb — see
the correction below.

---

## Art question 4 — captions

Cost is not separable from the beats they ride on, but the ascent beats total
4.4–5.4 ms compose *including* the lower third, so the caption is well under a
millisecond. Fit is a visual check — pending, see [Still open](#still-open-visual).

---

## Corrections to the template's own assumptions

**MIDCOURSE is not the cheapest beat.** The template asserted it "should be the
cheapest". It is the third *most* expensive at 7.3 ms compose, because the match
cut opens the map **inside** midcourse (`DrawMatchCut` runs from `p > 0.45`), so
that beat draws a filled globe, five graticule ellipses, a 41-point track and a
crosshair. The assumption was mine and the code is behaving as designed; the
template has been corrected.

**REENTRY is the second most expensive beat** at 8.5 ms compose — the 240-row
gradient sky plus ten cloud-deck ellipses. Never at risk (34.2 ms worst), but
worth knowing it is 1.9× an ascent beat.

---

## TRUE-TIME — the full 1,915 s run

Every beat matches COMPRESSED to within 0.1 ms, and the verdict is identical
(DETONATION, 49.8 ms, 20.1 fps). ~59,700 frames at ~31 fps sustained for 32
minutes.

**The result that only this mode could produce:**

| | Frames | Avg ms | Worst ms | Compose worst |
|---|---|---|---|---|
| MIDCOURSE, COMPRESSED (12 s) | 373 | 31.7 | 33.1 | 7.3 |
| MIDCOURSE, TRUE-TIME (1,546 s) | **48,023** | **31.7** | **33.1** | **7.3** |

**Identical to the decimal across a 129× increase in frame count.** 48,023
consecutive frames of a 26-minute beat and the worst frame is the same as in 373.
That is the drift/leak question answered: there is no accumulating cost, no heap
creep expressed as slowing frames, and no timer drift over the longest beat in
the sequence. A 12-second compressed midcourse is a valid proxy for a 26-minute
real one, which is the assumption the whole compressed mode rests on.

Other beats, TRUE-TIME (worst ms): IGNITION 31.2 · STAGE 1 SEP 31.0 · STAGE 2
31.1 · SHROUD 31.0 · STAGE 2 SEP 31.0 · STAGE 3 30.9 · STAGE 3 SEP 30.6 ·
POST-BOOST 30.6 · PSRE PITCH 30.4 · RV RELEASE 30.2 · BUS BACKAWAY 30.3 ·
PENAIDS 30.3 · REENTRY 34.3 · DETONATION 49.8 · MATCH CUT 32.8.

The *feel* questions this mode exists for are still open — they need a human
watching, not a log. See the template's true-time section.

---

## Run 4 — the real world map

The placeholder graticule globe was replaced with the reference's actual
equirectangular map (six landmasses, 126 points, scanline-filled with stroked
coastlines, a real great-circle track and the aim-point crosshair). Cost:

| Beat | Placeholder globe | Real map | Δ compose |
|---|---|---|---|
| MIDCOURSE | 33.1 ms (7.3 compose) | **36.1 ms** (10.3) | +3.0 |
| MATCH CUT | 32.8 ms (7.0 compose) | **35.9 ms** (10.1) | +3.1 |

**Affordable.** Compose 10.3 ms against a ~24 ms budget; worst frame 36.1 ms
against a 50 ms bar. The two map beats move from 5th/6th most expensive to
2nd/3rd, behind the detonation and ahead of reentry, and the verdict is unchanged
(DETONATION, 49.7 ms). Scanline polygon fill is the reason it is this cheap —
~12.6k edge tests per frame across all six landmasses.

Flash cost: **+4 KB** (map data + projection + great-circle solution).

---

## Run 5 — three bugs the photographs found that the log could not

**1. The rotation sign was inverted, sequence-wide.** `Axis()` rotated the
opposite way from the reference's `ctx.rotate()`, so positive angle canted the
nose toward **−x** while the vehicle drifted toward **+x**. Every attitude in the
sequence was mirrored, including the PSRE pitch-over — which is the one thing §11
singles out: *"downrange velocity is conserved, so the RV releases in the
direction of travel. Getting this backwards is the tell that an animation was
drawn rather than reasoned."*

It only became visible at REENTRY, because that is the only beat whose screen
travel is large enough to contradict the attitude out loud. One sign in one
function; everything downstream followed.

**2. Reentry attitude and reentry path were written down separately** and
disagreed by 16–29°. The angle now derives from the anchor's own endpoints
(`atan2(dx, −dy)`), so editing the path moves the nose. Same single-source fix as
the match-cut dot, same reason: two records of "which way is it heading" is two
places to drift.

**3. Both ends of the plasma were on the wrong side** — the ionised trail drew
*ahead* of the vehicle and the bow shock *behind* it, an RV flying backwards down
its own wake. The reference has the trail running back along the flight line and
the sheath centred on the nose.

None of the three is detectable from a frame time, a build, or a serial log.

### And the caption cannot be fixed by moving it

The limb's bright rim (`#9FD4E8`) **sweeps down through the frame** as altitude
rises — y≈192 at liftoff to y≈235 at the top of the ascent. Any fixed caption row
is crossed by it at some point in the climb; at T+0 the row starts underneath it,
which is why liftoff specifically was unreadable.

A scrim would cover the art the caption describes, so the text is haloed instead.
Measured, and it is not free:

| Halo | IGNITION compose | DETONATION worst |
|---|---|---|
| none | 5.4 ms | 49.7 ms |
| 8 offsets | 8.2 ms (+2.8) | **50.2 ms — over the bar** |
| 4 cardinal | see run 6 | see run 6 |

Eight offsets bought legibility by spending the sequence's tightest beat, which
is the wrong trade. Four cardinal offsets are half the cost and, at a six-pixel
font, the same picture — the diagonals are already covered by the two cardinals
either side of them.

---

## The detonation, resolved — and where the pixels actually go

Recomputed at the run-7 config rather than reusing the run-1 number, which had
gone stale:

| Element | Pixels/frame | Share |
|---|---|---|
| **46 billows** | ~262k | **54%** |
| Halo (4 rings, 1.35 × capR) | ~138k | 28% |
| Sky gradient (240 hlines) | ~58k | 12% |
| Core, ground, shock ring | ~30k | 6% |

The halo **was** 63% at ten rings and 1.9×. After two cuts it is not the biggest
item any more — the billows are. "The halo is the cheap win" stopped being true
two runs before anyone would have noticed, which is a good argument for
re-deriving a cost rather than quoting one.

It is still the right lever, for a different reason: **area goes as r²**, so
1.35 → 1.15 is a 27% cut that costs only how far the glow REACHES. Ring count
stays at four, so the wash gains no banding it did not already have.

| | Compose worst | Beat worst | Margin to the bar |
|---|---|---|---|
| run 7 | 24.2 ms | 50.0 ms | **0.0** |
| run 8 | 22.7 ms | **48.5 ms** | **1.5 ms** (~7× the run-to-run variance) |

**`kPuffRings` was NOT spent.** It buys ~2.4 ms and flattens the internal shading
on all 46 billows — and that shading is what makes them read as a churned cluster
rather than flat discs, which is the difference between Hood/Badger and a generic
fireball. §11: the fix is a cheaper cloud and never a smaller one. If more is
needed later, cut `kCrownPuffs` 16 → 12 (~1.3 ms) first: losing four of sixteen
outer billows is less visible than degrading all forty-six.

### A caveat on the bar itself

The 50 ms bar was written generically, before anything was measured, and it is a
**motion**-stutter threshold. The detonation has no translation — a slow
smoothstep rise and a slow cool across 11 s — so 20 fps there does not read the
way 20 fps during a separation would. The only element that could show stepping
is the ground shock ring, which crosses at ~7.5 px/frame for 2.2 s.

That is an argument for a **per-beat bar**, not for shipping at exactly 50.0 with
no margin. Taking the margin was still right; the bar should be split when the
next beat comes close.

---

## Open decision — the last lever

49.8 ms is under the bar by less than the measurement varies. Getting real
headroom costs the fireball rather than the wash:

| Lever | Est. saving | Cost |
|---|---|---|
| `kPuffRings` 5 → 4 | ~1.7 ms → ~48.1 ms | internal shading on every billow — the cloud flattens slightly |
| `kCrownPuffs` 16 → 12 | ~0.4 ms | churn in the outer head |
| `kHaloRings` 4 → 3 | ~0.9 ms | the warm wash gets banded; probably visible |

**Not taken.** The two halo cuts were defensible unilaterally — a background wash
is not the art. Spending the billows' shading is a look decision on the marquee
beat of the sequence, and it wants the side-by-side in hand.

The alternative reading: the bar is *"no beat worse than 50 ms **sustained**"*,
and the detonation **averages 44.8 ms (22.3 fps)** with one frame at 49.8. By
that reading it already passes and no further lever is needed.

---

## Still open (visual)

Everything below needs a human beside the browser — the checklist is in the
template.

- Look-target match table (8 rows) — **not run**
- Does the reduced halo still read as Hood/Badger incandescence? **This run
  changed it; it is the first thing to check.**
- `IGNITION` caption white vs the preview's `#ffd23e` — ruling upheld, glass check
  outstanding
- Caption clipping on the round face at the two 27-character lines
- Match-cut dot continuity
- Amber audit on glass

---

## Run 13 — the map became an orthographic globe

The flat map was replaced by an **orthographic sphere**: fixed orientation,
tilted 30° off the great-circle plane, Natural Earth 1:110m coastlines at
1,306 vertices, no land fill.

### Measured

| | Worst ms | Worst fps | Compose | Push |
|---|---|---|---|---|
| MIDCOURSE | **37.5** | 26.7 | **11.7** | 25.8 |
| MATCH CUT | 36.8 | 27.2 | 11.0 | 25.8 |

**The whole globe costs +0.7 ms over the 126-point flat map it replaced.**
MIDCOURSE compose 11.0 → 11.7; MATCH CUT 10.3 → 11.0. 12.5 ms of headroom
remains and DETONATION is still the worst beat at 48.6.

### The estimate was 5 ms high, and the reason is worth keeping

Predicted ~7.9 ms for the globe against ~3–4 ms for the flat map; actual delta
+0.7 ms. The error was in the coastline term: I assumed ~2.5 px average segments
and got ~1 px, because 0.5° of spherical tolerance on a 110 px radius decimates
to almost exactly one pixel. **Since cost is per line-pixel, halving the segment
length halves the cost** — the same fact that makes the graticule expensive makes
a densely decimated coastline cheap. 1,306 vertices are nearly free; a dozen long
lines are not.

That is the useful shape of it: *vertex count is not the budget, stroke length
is.* It is why 6 meridians + 3 parallels was the right cut and why the coastline
data did not need thinning.

### What the globe is for

The flat map could not express the thing it was drawn for. The GOLF-07 track
bowed **0.83 px** off a straight line — not a bug: a near-meridional great circle
*is* straight under equirectangular. Same launch point to Beijing would have
bowed 121 px. The projection was hiding the trajectory, not misdrawing it.

On the sphere it reads as an arc, **but only if the view is tilted.** Centring on
the arc's midpoint — the natural choice — puts the view direction *in* the
great-circle plane and projects the arc to a straight line through the centre of
the disc: the same failure, faithfully reproduced. Computed bow at φ=0 is exactly
0.0 px.

| φ | bow | endpoints from centre | r/R |
|---|---|---|---|
| 0° | 0.0 px | 45.7° | 0.72 |
| 20° | 11.4 px | 49.0° | 0.75 |
| **30°** | **16.6 px** | **52.8°** | **0.80** |
| 45° | 23.5 px | 60.4° | 0.87 |

φ is **derived from the trajectory** (`v = m·cos φ + n·sin φ`, for `m` the arc
midpoint and `n` the great-circle normal), so it stays correct for whatever
target the game picks rather than being tuned for GOLF-07.

### Decisions, and what they rest on

**Fixed camera, not following.** A following camera answers nothing — the dot is
permanently centred, so there is no progress cue, and a world sliding under a
static marker reads as *the world* moving. Fixed, with both endpoints visible and
a dot crawling between them, answers "how far along am I" at a glance. §11's match
cut needs it too: the map opens on the dot the ascent shrank to, and a centred dot
has no payoff. And §7 hands this screen back to monitoring for 26 minutes — a
rotating globe is a screensaver.

Note this is *not* the caching argument. Caching was measured and rejected
(6.24 ms/frame, 115 KB); the globe draws live either way.

**No land fill.** Filling a continent on a sphere means clipping its polygon to
the limb, and the scanline fill assumes straight projected edges, which is false
here. The disc is filled once as ocean (a span fill, 28.6 ns/px) and coastlines
are stroked over it.

**6 meridians + 3 parallels.** The graticule's only job is to say *sphere*, and
the limb circle plus the coastlines' own foreshortening already carry most of
that. Nothing in a graticule answers "where" or "how far". At R=110, twelve
meridians bunch into mush near the limb — 3.75 ms for clutter at the edges.

**Stroke hierarchy**, dimmest to brightest and drawn in that order: ocean disc,
graticule, coastlines, track and vehicle.

### Two rules from the flat-map era deleted, both non-problems on a sphere

Antarctica was dropped for spanning 360° of longitude, which a single-ring
scanline fill on an equirectangular map cannot express — a globe has no seam and
no fill, so it is just more coastline. The antimeridian guard went for the same
reason. Ring count 44 → 84.

Its lesson survives in the generator anyway: the first version tested **total**
longitude span and rejected **Eurasia**, which runs Portugal to the Bering Strait
across 197° without crossing anything. Only the step between *adjacent* vertices
ever meant anything.

---

## Raw captures

`bench-logs/animtest-2026-08-06-{1144,1147,1150}.log` (runs 1–3, COMPRESSED) and
`bench-logs/animtest-true-2026-08-06-*.log` (TRUE-TIME). Not committed — CSV
volume; the tables above are the durable record.

---

# Run 9 — LIFTOFF, the ground camera (issue #156)

`bench-logs/animtest-liftoff-2026-08-06-1417.log`, COMPRESSED, one full pass.
`beats=17 sequence=99500ms`. The beat sheet gained `Beat::Liftoff` at the head
and `Ignition` became `Stage1Burn` (T+10 → T+62, 62000 → 52000).

## The number

Shipping config, after the retune below. `beats=17 sequence=101500ms`.

| | Worst ms | Worst fps | Compose worst | Push worst | **Smoke worst** |
|---|---|---|---|---|---|
| LIFTOFF | **36.9** | 27.1 | 11.1 | 25.8 | **5.9** |

Against the 50 ms bar with a 25.8 ms push floor, compose has 24.2 ms and LIFTOFF
spends **11.1 ms of it — 13.1 ms of headroom.** The smoke is 5.9 ms of that, so
it is *half the beat's compose cost and one sixth of the frame*. **Nothing is
cut.** All four levers (`kSmokeDtS`, `kSmokeRMax`, `kSmokeGrowth`, `kSmokeRise`)
stay at their defaults and the cloud keeps its 49 puffs.

The worst frame lands at **6,570 ms of a 9,000 ms beat** — 73 % through, just
*before* the vehicle leaves frame at 81 %, with the column at full height and the
vehicle still on top of it. That is the busiest the frame ever gets and it is
where the worst frame belongs. Worth recording as a sanity check on the model:
spawning stops at 4.55 pad-seconds, so peak fill is not peak *count* — it is the
moment the surviving puffs are simultaneously numerous and at maximum radius.

### First measurement, for the record

Before the retune: **35.6 ms / 9.9 compose / 5.0 smoke** at 7,000 ms and 33
puffs. The retune bought 4.9 s of visible launch for 1.2 ms of compose.

## The regression check, which is the more important result

Inserting a beat at position 0 and re-cutting the one after it is exactly the
kind of edit that silently moves published marks. It did not move any:

| Mark | Expected | Logged |
|---|---|---|
| STAGE 1 (chase cam opens) | T+10 | 10,026 |
| STAGE 1 SEP | T+62 | 62,010 |
| SHROUD | T+121 | 121,020 |
| STAGE 2 SEP | T+123 | 123,000 |
| STAGE 3 SEP | T+177 | 177,029 |
| POST-BOOST | T+180 | 180,004 |
| REENTRY | T+1806 | 1,806,067 |
| DETONATION | T+1896 | 1,896,009 |

And the sixteen pre-existing beats are **numerically unchanged** from run 7 —
DETONATION still 48.6 / 22.8, MIDCOURSE still 36.8 / 11.0, MATCH CUT still
36.1 / 10.3, REENTRY 35.0 vs 34.9. `STAGE 1` at 32.6 / 6.8 is bit-identical to
what `IGNITION` measured before the rename and the ten-second trim, which is the
cleanest possible evidence that the beat lost its first ten seconds and nothing
else. Every row's `smoke_worst_ms` is 0.0 except LIFTOFF's.

### …and one row of that was a lie the first time

`STAGE 1` originally reported `smoke_worst_ms 1.9` — on a beat that draws no
smoke. `smokeUs_` is written only by `DrawSmoke`, which only LIFTOFF calls, so on
every other beat the reading was **stale**; the harness takes a per-beat max, and
one stale sample surviving the beat boundary was enough to attribute LIFTOFF's
cost to STAGE 1.

The instructive part is that **it read 0.0 the first time and looked correct.**
At 7,000 ms and a spawn window ending at 2.35 s the last puff died well before
the beat did, so the final frames genuinely measured ~0. Lengthening the beat
exposed it. `Render()` now zeroes the counter per frame, which is what makes
"every other row is 0.0" an assertion instead of a coincidence.

---

## Run 10 — the retune: "liftoff is too fast and will easily be missed"

Correct, and the arithmetic says why. At the look target's pace the vehicle
cleared grade at 1.30 pad-seconds and was out of frame by 2.79 — **1.5 seconds of
visible transit**, followed by 4.2 seconds of held empty pad. The worst of both:
the event was over before it registered and the beat still felt long.

Five stills from the **NG 2007 video itself** (not the preview) drove three
changes. Where the two disagree the video wins — §11 names the video as its
source, and a look target derived from a source does not overrule the source.
That authority edge is now written into `docs/reference/README.md`; it had not
come up before.

**1. The launch is 3.3× slower on screen.** The 2.6 exponent is kept — it is the
hot-launch character, the creep-then-gone that a linear rise never gives. Only
the time scale moved, 1.35 → **0.534**, and it is *solved rather than dialled*:
with the exponent fixed, the ratio between "clears grade" and "leaves frame" is
fixed at 4.66 whatever the coefficient, so pinning the exit at 5.7 of the 7
pad-seconds determines everything else.

| coefficient | clears grade | leaves frame | visible | dead hold |
|---|---|---|---|---|
| 1.35 (preview) | 1.30 s | 2.79 s | 1.5 s | 4.2 s |
| **0.534** | 1.91 s | 5.68 s | 3.8 s | 1.3 s |

The beat also went **7,000 → 9,000 ms** against a true 10,000, so in wall clock
the transit is **1.5 s → 4.9 s**. That is the same decision as the staging coast
being 1 s in both columns: there is nothing in a ten-second launch to compress,
and squeezing the one moment the ground camera exists to show is how it gets
missed.

Cost, recorded because it was cited as a virtue in run 9: the 60 ms coincidence
is gone. At 1.35 the nose came through grade at T+1.86 against a LIFTOFF caption
at T+1.80. It now emerges at T+2.73 — which is arguably *more* correct, since
LIFTOFF is called at first motion (`kIgnS`, vehicle still in the hole), not when
it clears the lip. The caption runs T+1.8→T+3.0, so it now spans the emergence
instead of landing on it.

**2. The smoke is a column, not a ground bank.** The preview rolls puffs outward
at ±21 px/s and lifts them at 0–6, which hugs the pad. The video's T-0 frame is a
**tall vertical column** reaching two thirds up the frame with a fireball at its
foot and **no vehicle visible at all**; at T+3 the vehicle sits on *top* of that
column. Rise now dominates drift (`kSmokeRise` 30 px/s vs the preview's 0–6), the
outward term is halved to flare the foot, and the spawn window follows the slower
ascent — the same `base > groundY - 90` condition, re-solved: 0.45→**4.55**
pad-seconds. Spacing widened 0.06 → 0.085 to keep the count bounded: **49 puffs,
ever**, up from 33.

**3. The silo glow became a real fireball.** It was a 26×4 px strip of `#ffb450`
— which is both why the smoke buried it and why it never read as the thing the
video *opens* on. It is now three stacked flickering ellipses in the plume
palette (not the detonation ramps: this is a motor, and spending Hood/Badger on
an engine would cost the one fire palette that means *warhead*), drawn after the
smoke because light from a silo illuminates a cloud rather than hiding behind it,
and fading out as the vehicle climbs away rather than switching off at grade —
the video still shows fire at the foot of the column at T+3.

Net cost of all three: **35.6 → 36.9 ms**, +1.2 ms of compose, +0.9 ms of smoke.
13.1 ms of headroom remains.

---

## Run 11 — the silo itself

Nine stills from **real launch footage** (Vandenberg, night). Two elements in
them are in *neither* the NG animation nor the preview, and one of the two is the
first thing that happens in a launch.

**1. The Launcher Closure Door.** Not "the blast door" — that is its name. A
110-ton slab of reinforced concrete and steel, 3.5 ft thick, that **slides
sideways on steel tracks** to uncover the hole. Both existing sources open on a
hole that is simply already there.

This is not only a detail. The beat's first half-second was a dark rectangle and
a glow ramping up, with **nothing moving** — the weakest part of the shot. It now
opens on the one motion that says *silo*. The rails are drawn from frame 0,
before the door moves: a slab that slides with nothing under it reads as a
glitch.

Three things the real mechanism settled, none of which I would have guessed:

- **The locking pin goes first.** A steel bolt withdraws, a pause, *then* 110
  tons of concrete. Two-stage motion is what makes an opening read as a
  **mechanism**; one continuous move reads as a drawer. Six pixels of steel, and
  the cheapest characterful thing in the beat.
- **Fast, but not instant.** A ballistic gas generator burning solid propellant
  shoves it — violent, but it moves *"in seconds"*, not teleported. The first
  build had it at 0.30 pad-seconds, a ten-frame blur; it now gets 0.42 of the 0.9
  pad-seconds before ignition. Easing is **ease-out**, so it is at speed on the
  first frame — smoothstep, which this started with, eases *in* as well, and a
  110-ton lid that accelerates gently is a crank.
- **Ignition follows the door, inside the tube.** The first-stage motor lights
  once the closure has cleared the path, vehicle still fully below grade. `kIgnS`
  at 0.90 against an emergence at 1.91 already did that; it is now the *reason*
  rather than a coincidence.

Recorded in the source so nobody "improves" it into a hinged flap: **sideways is
the point.** A sliding lid shoves its way clear through the dirt and debris a
near-miss dumps on the surface; a hinged one lifts into that debris and jams.

Sizing is now **derived from the vehicle in the same frame** rather than guessed:
`kSegments` gives stage 1 an 11 px body for a 5.5 ft airframe, so 1 px = 0.5 ft
— 3.5 ft of slab is 7 px, and the ~21 ft closure is 42 px across. Both guesses
were close; they carry now if the vehicle's width ever changes.

---

## Run 12 — the door was still far too fast, and why that needed a beat change

**"The door needs to open much slower."** It was 0.42 pad-seconds — 0.54 s of wall
clock for 110 tons — and it read as a panel snapping aside rather than a mass
being shoved.

The obvious fix was blocked. Ignition sat at 0.9 pad-seconds, the door has to
finish before it, and the pin has to finish before *that* — so there was simply
nowhere to put the time. Slowing the door meant moving ignition, and moving
ignition inside a beat whose duration feeds `BeatTrueStartMs` moves **every
published mark downstream**. Choose one: a door that opens in half a second, or
stage 1 separating at T+66.

### The distinction that unblocks it: T-minus is not T-plus

**T+0 is first-stage ignition.** The locking pin and the closure door both move
*before* it. That time is real, it takes wall clock, and it must not consume T+
time — which is a thing the beat table had no way to express, because a beat's
`trueMs` was assumed to be entirely T+.

`BeatSpec` gains **`preRollMs`**: how much of a beat's true duration happens
before T+0. Zero for all sixteen other beats.

- `BeatTrueStartMs` subtracts it, so LIFTOFF contributes 10,000 ms to the start
  of STAGE 1 despite lasting 14,000 — every mark downstream is untouched.
- `TPlusMs` holds at T+0 through it, which is also just *correct*: the caption
  during those seconds is `T-0 - STAGE 1 IGNITION`, and it should be. Running the
  clock from the top of the beat would have put the whole caption track four
  seconds early and had the altimeter reporting climb before there was any.

LIFTOFF is now **T-4 → T+10**: `{14000, 11000, 4000}`.

### What that bought

The pad phase runs 8.75 pad-seconds with ignition at 2.50 — a 2:5 split, chosen
so the 14,000/4,000 pre-roll comes out at a clean 10,000 ms of T+ time.

| | before | after | wall clock |
|---|---|---|---|
| pin | 0.05 → 0.22 | 0.15 → 0.75 | 0.75 s |
| **slab** | 0.24 → 0.66 (0.42) | **0.85 → 2.30 (1.45)** | **1.8 s bench, 2.3 s true** |
| first light | 0.66 | 2.30 | |
| ignition | 0.90 | 2.50 | |

**3.5× slower.** The easing exponent also softened, 2.0 → 1.6: a square ease-out
puts a third of the travel in the first fifth of the time, which over 1.8 s reads
as a snap followed by a long drift — the fast part looked as quick as the old
door and the rest looked like it was running out of gas.

### Two things that had to move with it, and one that came back

The launch coefficient is **re-solved, not left alone** — 0.534 → **0.478**,
against the same fixed 4.66 ratio, so the vehicle still leaves frame at 7.84 of
8.75 and the held ground shot before the cut is unchanged. Visible transit is now
**5.3 s**.

The smoke window moved with the launch curve rather than independently of it:
0.45 → **2.05** (the reference's "half a second before ignition") and 4.55 →
**6.58** (its `base > groundY - 90`, re-solved). Hand-tuning either after a
timing change is how the smoke ends up starting before the door has opened.

And the coincidence the last retune lost is **back for free**: the nose comes
through grade at T+1.61 against the LIFTOFF caption at T+1.80. Not tuned — both
are now anchored to ignition rather than to the top of a beat with a pre-roll in
front of it, which is the whole point of the change.

### Measured, full pass

| | Worst ms | Worst fps | Compose | Push | Smoke |
|---|---|---|---|---|---|
| LIFTOFF | **36.8** | 27.2 | 11.0 | 25.8 | 5.5 |

37.2 → **36.8**, i.e. slightly *cheaper*: `kSmokeDtS` widened 0.085 → 0.095 to
hold the puff count at 48 across the longer spawn window, so there is one fewer
disc spread over more frames. 13.2 ms of headroom.

**And the marks held**, which is the result that actually mattered here — the
beat's true duration went 10,000 → 14,000 ms and not one of them moved:

| Mark | Expected | Logged |
|---|---|---|
| STAGE 1 (chase cam opens) | T+10 | **10,043** |
| STAGE 1 SEP | T+62 | **62,013** |
| SHROUD | T+121 | 121,001 |
| STAGE 2 SEP | T+123 | 123,012 |
| STAGE 3 SEP | T+177 | 177,013 |
| POST-BOOST | T+180 | 180,091 |
| REENTRY | T+1806 | 1,806,404 |
| DETONATION | T+1896 | 1,896,005 |

DETONATION unchanged at 48.6 / 22.8 and still the worst beat; STAGE 1 unchanged
at 32.6 / 6.8; every non-LIFTOFF row still `smoke 0.0`.

---

Sequence: pin 0.15 → 0.75, slab 0.85 → 2.30, first light 2.30, ignition 2.50.

**2. Fire shoots straight up out of the hole, with the missile still inside it.**
Four consecutive frames of that footage show **no vehicle at all** — just a
vertical jet of fire taller than it is wide standing on the ground. The vehicle
appears later, **coming out of the top of the fireball**.

Two changes fall out of that, and the second matters more:

- The fire was three stacked ellipses at grade. That is a *pool*. It is now a
  **tapered vertical jet**, narrowing with height — a bonfire widens as it rises,
  an exhaust escaping a hole does not.
- **It is drawn over the vehicle, not under it.** Under, which is where a glow
  belongs and where this sat until the footage arrived, the emergence is a clean
  silhouette sliding out of a slot. Over, the vehicle comes out of the fire.
  That single ordering change is the shot.

The column's height is driven by two terms pulling opposite ways, which is what
stops it swallowing the beat: **ignition** ramps it up over 0.6 s, and **the
vehicle's own height above grade** collapses it — once the motor is out of the
hole there is nothing left down there burning. Physical rather than a timer, so
it tracks any future change to the launch curve automatically. Net: hidden
through the emergence, out of the fire by ~3.2 pad-seconds, 2.5 pad-seconds of
clean climbing vehicle after.

Palette is the **plume** ramp, not the detonation ramps. This is a motor, and
spending Hood/Badger on an engine would cost the one fire palette that means
*warhead*.

### Measured

| | Worst ms | Worst fps | Compose | Push | Smoke |
|---|---|---|---|---|---|
| LIFTOFF | **37.3** | 26.8 | 11.5 | 25.8 | 6.0 |

**36.9 → 37.3 ms. The blast door and the fire column together cost 0.4 ms**, for
the two most conspicuous absences in the beat. Both are triangle and rect fills
in a frame whose cost is dominated by 49 filled circles of smoke, so they
disappear into the noise. 12.7 ms of headroom remains and no lever has been spent
on any of it.

Running total for the beat, so the trend is visible in one place:

| | Worst | Compose | Smoke | What changed |
|---|---|---|---|---|
| run 9 | 35.6 | 9.9 | 5.0 | first build — 33 puffs, 7 s beat |
| run 10 | 36.9 | 11.1 | 5.9 | slower launch, smoke column, 49 puffs, 9 s beat |
| run 11 | **37.3** | 11.5 | 6.0 | blast door, fire jet over the vehicle |

`STAGE 1` still reports `smoke 0.0` throughout, so the per-frame zeroing is
holding.

## Vehicle scale — reported, not tuned

The pad is the reference because the pad is where the vehicle is largest, and the
chase cam opens on the **same 74 px** by construction. Measured off
`kNoseAlong`→tail, 1 px = 0.135 mm:

| State | px | mm | vs pad |
|---|---|---|---|
| Pad / full stack + bus + shroud | 74 | 10.0 | 1.00 |
| After stage 1 | 52 | 7.0 | 0.70 |
| After stage 2 (shroud gone, RV cone) | 40 | 5.4 | 0.54 |
| After stage 3 (bus + cone), ×2 boost | 52 | 7.0 | 0.70 |
| RV alone, ×2 boost | 36 | 4.9 | 0.49 |

**Three of these five numbers were wrong in the earlier report** (31 / 17 / 14 px
for the last three rows). Those were the *reference's* geometry, where the shroud
and the RV are one 14 px cone. The rig gives the RV its own 18 px cone — the
jettison is a real reveal, per §11's corrected Shroud row — so every state after
the shroud goes is larger than the preview's. The instruction was to measure the
rig; measuring the rig is what caught it.

The boost's effect, stated against the pad rather than in the abstract: without
it the last two rows are 0.35 and 0.24 of the pad silhouette for the five beats
the RV is the subject of. With it, the post-stack vehicle is the same apparent
size it was after the *first* separation, and nothing ever falls below half.

## The two rulings, as built

**Own beat, not a camera split.** `Beat::Liftoff`. The instrument argument
decided it and the numbers vindicate it immediately: 9.9 ms of compose against
STAGE 1's 6.8 ms is a 46 % difference that a merged beat would have reported as
a single averaged figure, and the smoke's 5.0 ms would have been invisible inside
a 56-second beat.

**Cut on absence.** The ground camera holds from ~39 % to 100 % of the beat with
no vehicle in it. What crosses the cut instead is the vehicle's identity:
`sunLift_` lifts the *same* airframe colours toward daylight at the pad and fades
that out over the opening quarter of STAGE 1, so the frame before the cut and the
frame after it are one object at one size in one paint under changing light. The
pad is not a second palette — `DrawVehicle` is called from both cameras.

The caption block moves to rows 36/48 for this beat only, because on the ground
camera the bottom of the frame is the ground: the grade line is at y=208 and the
silo mouth spans 206–212. `T+10 - FIRST ROLL MANEUVER` is 156 px of ink into the
171 px chord at y=36 — 7 px a side, and the tightest caption fit in the sequence.

## Deviation 2 replaced

The old #2 was "no silo / ground camera". Its replacement is the pad altimeter:
the preview reads out `pow((t-IGN)*1.35,2.6)*30*38` ft, which reaches 273,000 ft
at a phase its own caption track labels T+10 — and that track says T+19 is
8,300 ft. The preview contradicts itself, and its own geometry says which half is
wrong: it draws a 59.9 ft missile 66 px tall, so a pixel is 0.9 ft, not 38.

The **motion curve is kept verbatim** (it is the look, and matching the published
climb rate would have the vehicle crawl out of the silo for four seconds, which
is wrong dramatically and physically). Only the **number** is re-derived, from
the published mark and the preview's own exponent: `8,300 × (t/19)^2.6`. The
standing rule inverted — the reference owns what and when, and a readout of a
published quantity is neither.

One thing fell out of the port for free and is worth recording because it was not
tuned: the reference's launch curve puts the nose through grade at t = 1.30 s,
which on this beat's mapping is **T+1.86**, and `kCaptions` has put the word
LIFTOFF at **T+1.80** since before the beat existed. Sixty milliseconds apart,
from two independent readings of the same published sequence.

## Still open (visual) — the second photo set

Nothing below is answerable from a log.

- Does the vehicle read as **one object** across the cut, or as two? (the
  `sunLift_` trap — the single most important thing to look at)
- Can you catch the daylight fade happening in the first 25 % of STAGE 1?
- Does the vehicle **emerge from a hole**, or slide up past a line? (the grade
  clip)
- Is the held ground shot alive with smoke to the cut, or does it go empty?
- Camera shake at ignition — present, and settled by ~2.2 s?
- Caption at rows 36/48 — clipped on the round face? This is the tightest fit
  in the sequence.
- ALT readout at y=224, below the grade line — legible, and not colliding?
