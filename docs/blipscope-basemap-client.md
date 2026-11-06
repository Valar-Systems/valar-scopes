# The Blipscope basemap client: the plan (requirements, and Daniel's style decision)

The map server owns this client. **No firmware code exists yet.** The client code starts the day #445 merges (reviewer,
2026-11-06 ~15:19Z). Any change to the shared `Get()` goes to blipscope-5c through the reviewer.

**This document holds every requirement ruled so far, each with its source.** A requirement is added here when it is
ruled, never only remembered.

## 1. Fetching

| | requirement | source |
|---|---|---|
| **F1** | A request never waits on a build. It relies on the 202 and stays within the shared `Get()`'s 5 s | D4 ruling Q-D4d (2026-11-05 ~04:30Z); `docs/basemap-a1.md` §1 |
| **F2** | **A 202 is retried after its `Retry-After`** (15 s today), not at once and not on a fixed timer | Q-CB1 (2026-11-05 ~18:25Z): "The device side (202 and Retry-After) goes into your basemap client's plan as a requirement" |
| **F3** | **Before drawing anything,** the client runs every check in the format's §8. Any failure means the no-package screen, logged with why. There is no partial draw | `docs/basemap-package-format.md` §8 |
| **F4** | **The client draws the disc the header names** (`radiusMm`), not the one it asked for. A dense-area package may cover a smaller disc (D2) | §8 permits it; Q-DN2 (2026-11-06 ~15:07Z) |
| **F5** | **A 404 `no_coverage` or 422 `too_large` is the no-package screen.** For a major city the server must not leave it there: the dense-area fix lands before D4 | Q-DN2: "'No map' isn't an acceptable end state for a major city" |

## 2. The style: Daniel's decision (2026-11-06)

**Approved from the mock-up** (reviewer chat, pasted by Daniel, ~15:37Z), verbatim:

> Daniel approved the cockpit mock-up (2026-11-06) with these choices:
> 1. Terrain subdued, as in column A.
> 2. Roads: primary always; secondary only when no primary road is in the device's current view. Rural scopes get their
>    highways, and city views stay clean. The client plan defines "in view" exactly, with a check at KJAC and KSFO and a
>    sabotage.
> 3. Airports grey, as today. Cyan read as planes.
> 4. Required, whatever the style:
>    - The ocean: open water past TIGER's coastal band draws as land. Propose a package fix (an ocean or land mask),
>      predictions first, after the ruled D4 order. It must land before the Blipscope client ships.
>    - Airports and their codes draw above the weather, as the Blipscope already does (ruled 2026-10-26).
>    - Planes switch from cyan to white at 3,000 m, not 9 km, as ruled for #445.
> 5. Label collisions keep today's behaviour for now.
> 6. The Blipscope's default theme is Dark, the cockpit look. Light (a paper-chart style by day) and Auto come later as
>    options. Design the client's colours as a theme table, so Light can be added without rework.
> The mock-up stays uncommitted. Record the decision and keep the two sheets' sha256s in the client plan.

**The brief it answered** (~15:19Z) is unchanged: the packages stay product-neutral, and the style lives on the device.
Weatherscope's style does not change.

### The approved mock-up (uncommitted; identified by content)

| sheet | sha256 |
|---|---|
| `KSFO-sheet.png` (city) | `142bb86b4d77d3b1ad33db475d9ce7f428c0ea2f707af9a672f1961d66ecaaeb` |
| `KJAC-sheet.png` (rural) | `95573fad77778d522ff9cbe45aba0656bf20237c8d03a1a545497a441f9b9f64` |

**What the sheets show:**
- **The maps are real:** staging's packages for KSFO and KJAC (store `4c0875738f38`), and staging's airport lists.
- **The weather and the planes are synthetic,** and the sheets say so.
- Each face is 240 × 240, shown at 2×.

**Where the client differs from the mock-up, by the decision:**
- **Airports draw above the weather.** The mock-up drew them below it.
- **Planes turn white at 3,000 m.** The mock-up used 9 km.
- **Roads follow S1 below.** The mock-up showed columns A and E separately.

## 3. The client's rules

| | rule |
|---|---|
| **S1: roads** | **Primary (class 1) roads always draw. Secondary (class 2) roads draw only when no primary road is in the current view.** |
| **S1's "in view", exactly** | The current view is the device's projection at its current centre and zoom (§9 of the format, and the firmware's `ProjectAt`). A road is **in view** when **any of its segments** (consecutive vertices, mapped to screen pixels in double) lies within **120 px** of the screen centre (120, 120): the segment's closest point to the centre is at most 120 px away. The decision is re-made whenever the view changes (centre or zoom), never per package |
| **S2: terrain** | Subdued: land plus (shade − 128) × 0.075, per channel, on the theme's land colour, sampled per §9 from the level nearest the view's radius. The theme may switch it off |
| **S3: draw order** | land and terrain; then water; rivers; roads; place names; the weather; then **airports and their codes above the weather** (ruled 2026-10-26); then planes |
| **S4: planes, with weather on** | **cyan below 3,000 m, white from 3,000 m up** (as ruled for #445) |
| **S5: airports** | grey as today: the 5 × 5 outline square in mark grey, and the code 5 px right in label grey |
| **S6: labels** | collisions keep today's behaviour (decision 5) |

**S1's check, with values measured on the approved packages** (view half-width 49.5 km, centred on each package's
origin). These are registered here, before any client code:

| row | primary roads in the package | primary in view | secondary in view | predicted |
|---|---|---|---|---|
| **KJAC** | 8 | **0** | 56 | **secondary roads draw** (56) |
| **KSFO** | 316 | **186** | 426 | **secondary roads do not draw** |

**S1's sabotage:** "in view" replaced by "in the package". At KJAC, 8 primaries are in the package and none is in view, so
the sabotaged rule draws **no road at all**. That is the defect S1 exists to prevent, and it turns KJAC's row red.

## 4. The theme table

Every colour the client draws comes from **one table, keyed by theme.**
- **Dark is the default.**
- **Light** (a paper-chart style by day) and **Auto** come later. Each is one more row of values, never more code.
- The table's entries:

| entry | Dark (approved) | Light |
|---|---|---|
| land | (6, 8, 10) | later |
| terrain gain (S2) | 0.075 | later |
| water | (18, 34, 56) | later |
| river | (30, 56, 104) | later |
| road | (72, 72, 72) | later |
| place name | (86, 86, 86) | later |
| airport mark / label | (130, 130, 130) / (100, 100, 100) | later |
| weather ≥ 20 / 30 / 40 / 50 dBZ | green (0, 190, 0) / yellow (230, 230, 0) / red (230, 0, 0) / magenta (230, 0, 230) | later |
| planes with weather on: low / high | cyan (0, 255, 255) / white (255, 255, 255) | later |

**The weather and plane colours are signal, not style.** A Light theme may change their surround but should keep their
hues; that will be its own decision when Light is designed.

## 5. Before the client ships

- **The ocean.** Open water past TIGER's coastal band draws as land. A package fix (an ocean or land mask) is proposed,
  predictions first, **after the ruled D4 order**, and must land before the client ships.
- **D4's prerequisites,** in the order ruled 2026-11-06 ~15:07Z:
  1. the `SYNC_WAIT_MS` code and its ten on staging;
  2. step 0 (the dense-area instrument);
  3. K1 (`BASEMAP_NAME_KEY`);
  4. the dense-area code;
  5. the deploy-inputs code.

**CI for this docs-only PR, predicted: 31 check runs: 28 success, 3 skipped (`promote`, `ship-alert`, `version`), 0
failed.** The baseline is #566's finished docs-only run (31 = 28/3), which postdates `84409651`, the last change to the
set of jobs.
