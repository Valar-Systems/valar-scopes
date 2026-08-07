# docs/reference/ — look targets

Artefacts that specify **what something should look like**, kept verbatim so the
firmware can be diffed against them. Reference files are not built, not linted,
and deliberately not tidied — editing one to match the code defeats the point.

## Authority

```
docs/missileer-game-design.md   →  the spec. Wins on conflict, always.
docs/reference/*                →  the look. Wins where the spec is silent.
firmware                        →  neither. If it disagrees with both, it is a bug.
```

### "Wins where the spec is silent" — and the case that tested it

The spec is not always silent in the way it looks. §11 opens by **naming its
source**: the Northrop Grumman 2007 flight-sequence video. Where the preview and
that video disagree, the spec is *not* silent — it has already pointed at the
video, and a look target derived from a source does not overrule the source.

Settled 2026-08-06 on the liftoff smoke. The preview rolls its puffs outward at
±21 px/s and lifts them at 0–6, which builds a low bank hugging the pad. The
video's T-0 frame is a **tall vertical column** with a fireball at its foot and
no vehicle visible at all, and at T+3 the vehicle sits on top of that column.
The firmware follows the video.

The rule this is *not*: "go find better sources than the reference." The preview
remains authoritative for every value the video cannot supply — hexes, beat
lengths, projection, the Lambert solution. It is specifically about the case
where the preview is a **derived reading** of something the spec already cites.

The spec locks *direction* ("separations are axial", "the RV release is silent",
"amber is EXERCISE and nothing else"). A look target locks *values* — the exact
hexes, the published T+ marks, the shape of a mushroom cloud. Neither can
substitute for the other, and where a look target implies something the spec
forbids, the spec wins and the deviation gets written down rather than quietly
resolved.

### A look target is authoritative for WHAT and WHEN, never for WHETHER IT CAN BE SEEN

**Standing rule, 2026-08-06.** Choreography, beat timing and palette *intent*
port faithfully. **Legibility does not.** Legibility at 240 px across ~32 mm of
glass at desk distance is a device-side judgment and it overrides the reference.

The reason is in the file: these previews are authored on a 240×240 canvas
*displayed at 480 CSS px* (`canvas{width:480px}`, `image-rendering:pixelated`)
on a bright laptop. Every size and contrast decision in one was made at 2×
magnification in a viewing condition the product never has. On the panel, 1 px
is **0.135 mm**.

Two things this has already caught, both faithfully ported and both wrong on
glass:

- the reentry vehicle is ~14 px of geometry — **1.9 mm**, below the size at
  which a viewer can tell what the object is;
- it is drawn `#2B2C28` on black — about **4% luminance**, invisible on the
  panel and, in truth, invisible in the browser too. Nobody noticed because 2×
  on a backlit laptop forgives both.

**"The reference does it this way" is never a defence for something invisible on
the panel.** Fix it device-side, and record the deviation in the consuming
file's DEVIATIONS block.

## Contents

### `missileer-launch-animation-preview.html`

Canvas preview of the whole Missileer launch sequence at true 240×240, pixel
doubled — what fits there fits the 1.28" GC9A01. Open it in a browser; the
buttons jump between phases.

It covers **thirteen** phases, of which the firmware currently implements the
flight portion:

| Phase | Where it lives now |
|---|---|
| IDLE · ARRIVAL · DECODE · COMMIT · COUNTDOWN · KEY · TCD | game UI — not yet built |
| LIFTOFF (silo, ground camera) | `src/anim/FlightAnimation.*` — `Beat::Liftoff` |
| ASCENT · FLIGHT · REENTRY · DETONATION | `src/anim/FlightAnimation.*` |
| CREDITS | game UI — not yet built |

What was extracted from it into `src/anim/FlightAnimation.cpp`:

- **The published T+ marks.** The ascent beat table carries the Northrop Grumman
  2007 flight-sequence numbers — T+19 Mach 1, T+39 Mach 3, T+45 second roll,
  T+62 stage 1 separation, T+78 skirt separation, T+121 shroud, T+123 stage 2
  separation — with the design doc's §12 row (`stage 1 ~60 s, stage 3 ~120 s,
  post-boost ~180 s`) supplying the marks the preview leaves relative. The two
  agree; the preview is simply more precise.
- **Every hex.** Chrome accents, the Earth-limb radial, the *olive* vehicle with
  tan interstage bands, blue RCS, and the four named detonation ramps
  (`CAP_/CRN_/STM_/SKT_`, hot and cool) that make the mushroom cloud read as
  Hood/Badger rather than as a generic fireball.
- **The lower-third caption track**, including the two overrides that make §11's
  staging beat legible in words: `SEPARATION · COAST`, then `IGNITION`.

Deliberate deviations are listed at the top of `FlightAnimation.cpp`. The two
that matter: the preview's `IGNITION` caption is `#ffd23e` and the firmware
draws it in paper white (§11 reserves that region of the spectrum for EXERCISE
amber, and a yellow word on a lower third is chrome); and the preview's world
map — six coastline arrays, an equirectangular projection and a Lambert
time-of-flight solution, all still here and all usable — is **not** ported into
the animation module, because §7 puts full map rendering after v1 and the flight
director is the wrong home for map data.
