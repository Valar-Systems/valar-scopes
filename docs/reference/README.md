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

The spec locks *direction* ("separations are axial", "the RV release is silent",
"amber is EXERCISE and nothing else"). A look target locks *values* — the exact
hexes, the published T+ marks, the shape of a mushroom cloud. Neither can
substitute for the other, and where a look target implies something the spec
forbids, the spec wins and the deviation gets written down rather than quietly
resolved.

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
| LIFTOFF (silo, ground camera) | **not implemented** — the largest gap; see below |
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
