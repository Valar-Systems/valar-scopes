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

## Raw captures

`bench-logs/animtest-2026-08-06-{1144,1147,1150}.log` (runs 1–3, COMPRESSED) and
`bench-logs/animtest-true-2026-08-06-*.log` (TRUE-TIME). Not committed — CSV
volume; the tables above are the durable record.
