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
| **F6** | **The first fetch after an update is spread: a per-device delay,** so the fleet does not ask at 03:00 in each time zone (§5). The window, and whether a device's nightly fetch is spread too, are registered with the client code | reviewer, 2026-11-06 ~22:49Z: "prefer spreading the first fetch (a per-device delay after the update) over measuring a 03:00 burst" |

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
  3. S2 (the package and its `.err` read in parallel; ruled before D4, ~18:26Z);
  4. K1 (`BASEMAP_NAME_KEY`);
  5. the dense-area code;
  6. the deploy-inputs code.
- **The map layer is on by default** (§6). DO1 to DO4 hold in the shipping build, and DOC1 to DOC3 have run as registered.
- **The first-fetch wave stays under Daniel's allowance,** or Daniel has ruled on it (below).

### The first-fetch wave, against Daniel's $2 allowance

**On by default means the whole radar fleet fetches its map right after the OTA** (reviewer, 2026-11-06 ~21:39Z).

**On today's figures, at 600 devices, the map costs about $0.08 a month, on an account at about $0.44. That is about
$0.53 against the $2, so it is under.** Anything over $2 is Daniel's call before ship. The allowance covers the whole
account (`docs/basemap-route.md`), and two things can move this answer: weather's Europe line and the real fleet size.

**When it lands.** A device takes an update at boot, and its nightly reboot is at 03:00 local
(`include/QuietHourPolicy.h`). Nothing spreads that per device. So the fleet updates within about a day, in a burst at
03:00 in each time zone. Each device's first fetch is a cold build, because packages are per location.

**One device, at the top of each measured range:**

| | per device | source |
|---|---|---|
| requests | 2: the 202, then the retry | F2 |
| builder CPU | 4.11 s | the top of the measured 2.28-4.11 s (`docs/basemap-cold-build.md`) |
| R2 Class A | 2: the lock and the package. Deletes are free | `proxy/src/basemap/handler.ts` |
| R2 Class B | 71: 67 in the build, and 4 on the request path | the most of A1's ten (KMSP); S2's two reads, on the first request and on the retry |
| storage | about 214 KB: the 32 KiB cap (`CAP_BYTES`), plus terrain, which sits outside it | three levels of N = 246, A1's parameters |

**The wave at 600 devices,** the factory batch, all sold as Blipscope at first (`docs/factory-image-plan.md`):

| line | account, a month | included | the wave adds | billed |
|---|---|---|---|---|
| Workers CPU | 52.2 M ms | 30 M | 2.47 M ms | **$0.049:** the account is already over |
| R2 Class A | 929,006 | 1 M | 1,200 | $0. If over: $0.005 |
| R2 Class B | 4.80 M | 10 M | 42,600 | $0. If over: $0.015 |
| storage | under 10 GB | 10 GB | 129 MB | $0 |

- **The account's figures** are the 30-day run rate in `docs/wx-opera-europe.md` §4, read 2026-11-06.
- **Its CPU line adds `opuk`,** which that window barely includes: about 14.5 M ms a month on the same document's medians
  (8,640 builds at 1,612.5 ms, plus the checks). That line is an estimate, and it makes the account about **$0.44**.
- **With every line billed, the wave is $0.07.**

**It is not a one-off:**
- **The 30-day rule repeats it.** Packages are deleted 30 days from upload (`docs/basemap-a1.md` 4b), so each is rebuilt
  the next time its device asks after that. The $0.07 is a monthly figure, and the wave's shape can recur on day 30.
- **The nightly reboot adds a fetch,** unless the client keeps its package across a reboot, which is not yet decided.
  That costs at most 2 Class B reads a night when the Cache API misses: 36,000 a month at 600 devices, **$0.013** if
  billed.

**So the map's total is at most $0.083 a month at 600 devices,** or $0.00014 a device.

**What can move the answer:**
- **Weather's Europe line.** `docs/wx-opera-europe.md` projected $4.02 a month on today's build code, and Q-COST (b) is
  working that down. If weather lands near $2, this wave is what crosses it, so the two figures are read together.
- **The fleet.** The allowance runs out at about **11,200 devices** on today's account. Today's fleet is not recorded
  here; it is read from `scripts/reconcile-fleet.py` at ship.
- **The builds.** The CPU range predates step 0 and the dense-area code. Three times that CPU would make the map $0.18 at
  600 devices, still under.

**Load at the burst: not measured.** Concurrency has only been read as a pair (SR1). If 600 devices sat in one time zone
and fetched over five minutes, that would be about 2 builds a second, about 8 running at once, each reading up to
50.1 MB from the store (S2's ten, KBWI). That is an arithmetic bound on a case nobody has run, not a limit anyone has hit. **As ruled
~22:49Z, the client spreads the first fetch (F6) rather than measuring a 03:00 burst.**

**At ship:** re-read the account's figures and the fleet count, and state the month's projected overage with the wave and
**v19's weather polling** (`/wx` with every cloud board on), which blipscope-5c restates before the v19 cut.
**Over $2 is Daniel's call before ship.**

**CI for #569 (this document's first PR, docs-only), predicted: 31 check runs: 28 success, 3 skipped (`promote`,
`ship-alert`, `version`), 0 failed.** The baseline is #566's finished docs-only run (31 = 28/3), which postdates
`84409651`, the last change to the set of jobs.

## 6. The map layer's setting: on by default

**Daniel's requirement** (reviewer chat, pasted by Daniel, 2026-11-06 ~20:25Z), verbatim:

> the map layer is on by default when it ships, for every device that hasn't set it.

**Why this needs rules rather than one constant.** CLAUDE.md's *a default only reaches keys that were never saved* is
the hazard. The config page posts the whole form. So the first save writes whatever the box showed, and a device's
"never set" ends there. #238 shipped a default nobody received for exactly this reason. Here the hazard works for us
only while the key is new and the page shows it ticked.

| | requirement |
|---|---|
| **DO1: the default is a named constant** | `MAP_DEFAULT_ON = true` sits beside `LOGBOOK_DEFAULT_ON` in `include/ConfigMigration.h`. **The device and the config page** both resolve the stored value through `configmigration::ResolveToggle()`. Neither holds a literal of its own (the logbook's page once did). **DO1 covers every writer of the key, not only those two.** When the client is written, it names each one (provisioning, the Remote edition, any settings import). Each resolves through `ResolveToggle` or leaves the key alone |
| **DO2: the key is new** | **No published firmware has ever written the key** (NVS key ≤ 15 characters). So on every device in the field it reads unset, and the default reaches it with no migration. Checked when the client is written: the name has no match in main's `src/` and `include/`, in any release tag's tree, or in **any image ever published: each OTA channel's binaries and the factory image**. The image search has its own control: a key each image is known to hold must be found, or that image was not read. **If any image ever wrote it**, or an earlier name for the same layer, with another default, DO2 fails. The client then needs a `ConfigMigration` revision that **clears** the key (as rev 3 did for the logbook) |
| **DO3: an unset key renders ticked** | So the first save after the update writes `"true"`. From then on, a stored `"false"` can only be a deliberate untick, and no later migration may clear it (the logbook's "going forward" rule). A page that leaves the box out sends no `cfg-toggles` entry for it, so the save leaves the key alone (`include/ConfigVocabulary.h`) |
| **DO4: no release shows it unticked first** | **No release may render the box unticked, even behind a flag:** not a beta, not an OTA channel, not a staged rollout. Every device that saved its form on such a build would hold an explicit `"false"`, and DO1 would reach none of them. If one does ship, DO2's clearing migration becomes a requirement before the layer ships |

**The checks, registered here before any client code:**

| row | check | predicted | its control or sabotage |
|---|---|---|---|
| **DOC1** | host test of `ResolveToggle` with `MAP_DEFAULT_ON`: stored `nullptr`, `""`, `"true"`, `"false"` | on, on, on, **off** | the `"false"` row is the control: an explicit untick wins. Sabotage `MAP_DEFAULT_ON = false`: the `nullptr` and `""` rows go red |
| **DOC2** | the config page's render of a device with **no** map key | the box is `checked` | sabotage: the page reads a literal `"false"` in place of the helper, and DOC2 goes red. **How DOC2 runs is registered with the client code**, because the page has no host render test today. If no check can be written, the client PR says so, and DOC3 becomes the only check |
| **DOC3** | bench, on a radar bench board picked under CLAUDE.md's bench-board rules: a board that **saved its form on the current release** (it has a location; the map key reads absent), flashed with the client | the map draws at first boot. The page shows the box ticked. The key is still absent until a save | the control: untick and save, then reboot. Expected: the key is `"false"`, the map stays off after the reboot, and the box stays unticked |

DOC3 runs on the board population the requirement is about. A factory-fresh board passes DOC1 and DOC2 by construction.
The devices in the field are the ones that have already saved a form.

**CI for this PR (docs-only), predicted: 31 check runs: 28 success, 3 skipped (`promote`, `ship-alert`, `version`), 0
failed.** The baseline is #578's finished docs-only run (31 = 28/3). It postdates `84409651`, the last change to the set
of jobs; the two workflow commits since (`12569fa0`, `259e8688`) only add steps to `device-ids.yml`.

**CI for the PR adding DO1's writers, DO2's images and §5's wave (docs-only), predicted: 32 check runs: 29 success, 3
skipped (`promote`, `ship-alert`, `version`), 0 failed.** #445 (`caf63cc9`) added one firmware matrix leg
(`blipscope-s3-128-wxlook`), so this is the last docs-only PR's 31 + 1. Main's docs pushes read 31 on `b5c9c6e9` and 32 on
`1ce7a73a`.

**CI for the PR adding F6 and v19's weather polling (docs-only), predicted: 32 check runs: 29 success, 3 skipped
(`promote`, `ship-alert`, `version`), 0 failed.** The baseline is #587's finished docs-only run (32 = 29/3), which postdates
`caf63cc9`, the last change to the set of jobs.
