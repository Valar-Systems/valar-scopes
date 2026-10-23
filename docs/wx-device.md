# Weather under the planes: the device side

The radar edition's half of the weather layer, under the 2026-10-20 ruling:
- build it and test it on the host now;
- bench it as soon as a board is free;
- **device-side weather does not merge until the starved-seconds counter has landed and #245 has been
  ruled on from the soak.**

The server half is [docs/wx-server.md](wx-server.md) (#443). The device talks to it only through
`GET /api/v1/blipscope/wx/<product>?lat=&lon=&r=` and the WXC1 v1 bytes it returns, which
`include/WxCrop.h` decodes.

The colour scheme, the switch's name and its default are **provisional until Daniel picks from bench
screens**. Until then the scheme is **S2**: standard colours at 40 %, planes keep their colours with a
black outline. The switch is **off by default**.

This document freezes the design and the predictions **before any device code exists**.

## The pieces

| | what | where | graded by |
|---|---|---|---|
| **WD1** | the radar's projection, extracted once, and its inverse | `include/RadarProjection.h`; `AircraftManager::ProjectAt` becomes a call to it | host |
| **WD2** | which crop cell each screen pixel shows | `include/WxLayer.h` (pure) | host |
| **WD3** | when to fetch, what to keep, when to hide | `include/WxPolicy.h` (pure) | host |
| **WD4** | S2: code to colour | `include/WxS2.h` (pure), from the pinned NWS table once #439 has merged | host + glass |
| **WD5** | the wiring: `FetchKind::Weather`, the PSRAM crop and layer buffers, the draw order, the config switch, the black plane outline | `AircraftManager`, `ConfigurationWebServer` | bench (Part B) |

**WD1 first, and as an extraction rather than a copy.** The layer has to invert exactly the projection
the planes are drawn with. A second transcription of `ProjectAt` would agree with it until one of them
changed: the `ChordWidthPx` row in CLAUDE.md. So the rule moves into one pure header, the radar calls
it, and the layer inverts it. That is a change to a shipping draw path, so WD1's first test pins the
extracted function to the current one bit for bit.

## Predictions (frozen before any device code)

Every check has a sabotage that must turn it red, and a check whose sabotage stays green is reported as
untrustworthy, not passed. The host checks run in `test/host/run.sh`, so in `host-tests`.

**WD1. The extraction is bit-identical.**
- **Reference:** the current `ProjectAt` body is copied verbatim into the test as the reference, and
  the copy is checked against the source text it came from, so a later edit to the original cannot
  leave a stale reference.
- **Sweep:**
  - homes at lat 20-55 by 2.5° × lon −125 to −65 by 5°;
  - view radii from 0.009° to 2.0° (5 steps);
  - rotations 0, 37, 90, 180 and 253°;
  - screen sizes 240, 412, 466 and 480;
  - targets on a 21 × 21 grid across ±1.2 view radii.
- **Predicted:** `radarproj::Project` returns the identical `(x, y)` for **every** case: 0 differences.
- *Sabotage:* `rotSin` sign flipped in the extraction, which must give differences > 0.

**WD2. The layer registers with the planes.** The layer computes in single precision: the ESP32-S3 has
no double-precision FPU, and a 480 px rebuild in software doubles would take about half a second.

**Setup:**
- the 8 server-made crops of WS7 (`test/fixtures/wx/server/`), with real server geometry;
- homes at each crop's centre ± 0.005°;
- view radii from each crop's own `r` (ProjectAt's clamp), at zoom × 1, × 0.6 and × 0.3;
- rotations 0, 37, 90, 180 and 253°;
- sizes 240, 412, 466 and 480.

**Predicted:**
- **(a) Every pixel against an independent reference.** The test inverts the projection itself, in
  double precision, from the forward formula, and shares no code with `WxLayer`. That gives each pixel
  centre's cell. **0 mismatches** except where the reference's position is within **1e-4 cells** of a
  cell boundary, where single and double precision may legitimately round differently. The count inside
  that band is printed.
- **(b) Round trip with the planes' projection.** For every crop cell whose centre projects onto the
  disc, `radarproj::Project` (truncating, as the planes do) gives a pixel. The cell `WxLayer` shows there
  is within **|Δrow|, |Δcol| ≤ 1** of the original cell. The bound: the pixel centre is at most 0.707 px
  from the projected point along either lat/lon axis (rotation turns the pixel), which is at most 1.18
  cells at the 2.0° clamp on 240 px, under 1.5. The share of exact hits is printed, not graded.
- **(c)** Every pixel off the round screen is `kOffDisc`: **0 exceptions**. The disc test is WxRender's
  integer test, generalised to size S.
- **(d)** Every on-disc pixel whose cell falls outside the crop is **14**, never dry: **0 exceptions**.

**Sabotages:**
- the layer ignores rotation: (a) must report mismatches > 0;
- the layer swaps rLat and rLon: (a) must report mismatches > 0;
- the layer flips y: (b) must report violations > 0.

**WD3. The fetch policy, as a table and as properties.** The table covers the transitions on: enabled /
disabled; 200 valid / 200 oversize / 200 invalid / 304 / 503 / 4xx / network error; clock set / not
set; location or range changed. Its asserted properties:
- **(a)** Disabled means **0 requests**, ever.
- **(b)** A 200 replaces the held crop **only** if it is ≤ 32,768 bytes, `wxcrop::Validate` is Ok, its
  frame epoch is ≥ the held one, and it answers the current query. Otherwise the held crop is kept.
- **(c)** **Never shown older than 600 s** by the NTP clock. **Never shown without a set clock.**
- **(d)** `If-None-Match` is sent only with an ETag earned **by the current query**. A location or range
  change drops the held crop and its ETag.
- **(e)** Polling every 60 s while enabled. On errors (503, 4xx, network) the interval backs off
  60 → 120 → 240 → 480 → 600 s cap, and resets on any 200 or 304.
- **(f)** A crop made entirely of 14 is held, but marked "outside coverage", so the note is shown instead
  of an empty overlay. The decision comes from the data, not a map.
- *Sabotages, one per property:*
  - (a) no enabled check;
  - (b) no size cap; and accepting an older epoch;
  - (c) no age check; and treating an unset clock as epoch 0;
  - (d) the ETag kept across a query change;
  - (e) no backoff;
  - (f) all-14 treated as dry.

  Each must turn exactly its own property red.

**WD4. S2 (provisional) as properties, not a picture.** The picture is Daniel's, from bench screens.
- Code 0 is **exactly** the background: nothing is drawn.
- Codes 1-13 are the pinned NWS table's colours **× 0.40** over black, rounded the same way on host and
  device.
- 14 and 15 are a dim dither, **never all-background in any 2 × 2 block**, and different from each other.
- No S2 colour equals a plane colour: the existing type palette is read from source, not transcribed.
- *Sabotages:*
  - 14 drawn as 0, which must fail "never dry";
  - × 0.4 dropped, which must fail the value check.
- **Not before #439 merges:** the pinned table lives there, and a second copy is how two palettes drift.

**Part B: the bench, weather on vs off** (ruled: these three are required). These need a free board,
and the starved-seconds counter for B3. Each runs as one capture per arm of equal length, the arms in
the same hour bands on consecutive days, on the same board and firmware image, with only the switch
changed.
- **B1. p95 frame time:** on − off **≤ 3 ms**. The layer is cached and replaces the `fillScreen` black,
  so steady-state frames copy instead of fill. A rebuild (a new crop, a zoom or rotation step, a home
  change) is a one-frame spike, reported separately with a bar of **≤ 40 ms** on the s3-128.
- **B2. Heap floor during weather fetches:**
  - the minimum free **internal** heap across all `/wx` fetches is within **2 KB** of the minimum across
    `/blips` fetches in the same arm;
  - `heaphealth::CanHandshake()` is never false during one.

  The crop and layer buffers are PSRAM, so the internal heap should not see weather at all.
- **B3. Starvation no worse:** starved seconds per hour, from the counter, with weather on ≤ off + the
  off arm's own hour-to-hour spread (its max − min). Weather adds a request per minute, so a real
  increase would show as a step, not noise.
- **Outside the set, in any arm:** stop and report. The arms are never re-run to get a better number.

**Outside the set (all of the above):** stop. The code, tests and bars are not edited to make a reading
come true. The raw reading goes to the reviewer, and the next step is decided then.

## Results (2026-10-20, host, Windows MinGW)

**WD1: AS PREDICTED.**
- **8,599,500** projections over the frozen sweep, **0** different from `ProjectAt`'s body at `788ad44`.
  The reference `test/host/ref_project_at.h` was cut by script; its body sha256 is `018192a40cf5…`, and
  it was checked byte-identical to the cut.
- *Sabotage* (rotSin sign flipped): 2,653,432 of 4,299,750 differ.

**WD2: (a), (c) and (d) AS PREDICTED; (b) OFF-PREDICTION.** 960 views (8 crops × 2 home offsets × 3
zooms × 5 rotations × 4 sizes).
- **(a)** 127,230,720 on-disc pixels; **0** mismatches outside the 1e-4 band, and 211,347 inside it.
  A read-only diagnostic split the band:
  - **211,232** lie within **1e-9** of a cell boundary. These are exact alignments: the test's home is a
    half-cell from each crop's centre;
  - the other **115** lie within **3.4 × 10⁻⁸** cells, the fixed point's precision.

  So every disagreement with the double reference is within 3.4e-8 cells of an edge.
- **(c)** 0 off-disc disagreements. **(d)** 0 outside-the-crop pixels that are not 14.
- **Sabotages:** rotation ignored, 16,845,039 mismatches; rLat/rLon swapped, 13,186,188; y flipped,
  8,600,352 round-trip violations. All red.
- **(b) OFF-PREDICTION: 1,740 of 25,098,437** crop cells land more than one cell away, against a frozen 0.
  - **Cause, from a read-only diagnostic:** **all 1,740** are points whose float x or y is in (−1, 0),
    and **0** are elsewhere. `Project` ends in `static_cast<int>`, which truncates toward zero, so a
    point at x = −0.4 lands on pixel 0, up to 1.5 px from that pixel's centre. The frozen bound
    assumed flooring: "the pixel centre is at most 0.707 px from the projected point". That holds only
    for non-negative coordinates.
  - **Alternative named:** a layer bug at the screen's edge pixels. **Excluded:** (a) grades every pixel,
    x = 0 and y = 0 included, against the independent reference, with 0 mismatches. And no violation
    has a non-negative float coordinate.
  - **This is a property of the planes' projection, unchanged and pre-existing:** a plane at x = −0.4 is
    drawn on column 0 too, and WD1 holds `Project` bit-identical to it. The test still asserts the
    frozen 0 and fails. **Nothing changes until there is a ruling.**

**WD3: AS PREDICTED.**
- The table: 12 / 12 rows.
- Random sequences: **200,000 steps, 0 violations** of each of (a)-(f). The sequences cover enable
  toggles, query changes (including a response landing after its query moved), clock set and unset,
  and 200 valid / oversize / invalid / older-frame / all-14, 304, 503, 404 and network failures.
- **Sabotages** (`test/host/sabotage_wx_policy.py`): **8 / 8 turn exactly their own property red, and no
  other.** Each plants into a copy of the header, and the real header's sha256 is unchanged before and
  after.
  - S-a: no enabled check.
  - S-b1: no size cap. S-b2: an older epoch accepted.
  - S-c1: no age check. S-c2: an unset clock read as epoch 0.
  - S-d: the ETag kept **and** sent across a query change. (d) has two guards, so removing one is
    correctly masked by the other.
  - S-e: no backoff.
  - S-f: all-14 shown as dry.
- **Rig note:** five plants first failed to compile under `-Werror`. Deleting a check removed a
  parameter's only use (`enabled`, `bytes`, `nowEpoch`, `clockSet`, `q`). Each was re-planted as a
  `(void)` reference. Those were compile refusals, not results; no measured value was involved.

## WD2 (b′), re-registered by ruling (2026-10-20), frozen before any run

**(b)'s frozen 0 stays a miss in the record.** 1,740 of 25,098,437 round trips were more than one cell
off. The ruling accepted the cause: `Project` truncates toward zero, so a point in (−1, 0) lands on
pixel 0, up to 1.5 px from that pixel's centre, and the frozen bound assumed flooring. **`Project` does
not change.** It is the planes' existing behaviour, WD1 pins it bit for bit, and the visible effect is at
most about 1.5 px on the left and top edge pixels.

**(b′): the same round trip, split by where the float projection fell.** It covers the same 960 views
and every crop cell whose centre `Project` puts on an on-disc pixel. The test recomputes `Project`'s
float x and y to classify each point. **Control:** for every point, `(int)x` and `(int)y` must equal
what `Project` returned, or the classification is BLIND.

**The bound, derived per view the same way as the frozen one.** `kRow` and `kCol` are the view's cells
per pixel along the lat and lon axes: `(2 rLat / size) / cellLat` and `(2 rLon / size) / cellLon`.
- **(b′1): x ≥ 0 and y ≥ 0.** The pixel centre is within 0.5 px of the point on each screen axis, so
  within **0.7071 px** along any rotated axis. The bound is `|Δrow| ≤ ⌊0.7071 · kRow + 0.5⌋` and
  `|Δcol| ≤ ⌊0.7071 · kCol + 0.5⌋`. That is ≤ 1 everywhere, since k ≤ 1.667, so it is the frozen ≤ 1.
  **Predicted: 0 violations.**
- **(b′2): x or y in (−1, 0).** The truncated axis is up to 1.5 px from the pixel centre and the other up
  to 0.5 px, so along any rotated axis up to **√(1.5² + 0.5²) = 1.5811 px**. The bound is
  `|Δrow| ≤ ⌊1.5811 · kRow + 0.5⌋` and `|Δcol| ≤ ⌊1.5811 · kCol + 0.5⌋`: at most 3 at the 2.0° clamp
  on 240 px. **Predicted: 0 violations.** The count of b′2 points is printed: **1,740 or more**, since it
  includes the 1,740 that failed (b).
- **Both x and y in (−1, 0)** would land on pixel (0, 0), which is off the disc for every size.
  **Predicted count: 0.**

**Sabotages:** all three existing WD2 sabotages (rotation ignored, rLat/rLon swapped, y flipped) now also
run through the round trip, and **each must give (b′1) violations > 0**.

**Outside the set:** stop and report.

### WD2 (b′) results (2026-10-20): AS PREDICTED

| | result |
|---|---|
| control: the classification's float x, y truncate to `Project`'s own pixel | **0** disagreements |
| **(b′1)** x, y ≥ 0 | **25,086,717** round trips, **0** beyond ⌊0.7071 k + 0.5⌋ |
| **(b′2)** x or y in (−1, 0) | **11,720** round trips (predicted ≥ 1,740), **0** beyond ⌊1.5811 k + 0.5⌋ |
| both x and y in (−1, 0) | **0** |
| sabotage: rotation ignored | (a) 16,845,039 mismatches; **(b′1) 6,920,035** violations |
| sabotage: rLat/rLon swapped | (a) 13,186,188; **(b′1) 2,987,897** |
| sabotage: y flipped | **(b′1) 8,606,650** |
| record: the frozen (b) | 1,740 of 25,098,437 more than one cell off; printed, not asserted |

(b′1) and (b′2) together are the full 25,098,437. The b′2 points number 11,720, more than the 1,740 that
failed (b): most points that truncate onto pixel 0 still land within one cell, because the view's cells
per pixel are often well under 1.67.


## WD4 addendum, 2026-10-22 (before any WD4 code): what #439's table and the planes' colours turned out to be

#439 has merged (main `44caba9`), so WD4 can be written. Reading the pinned table and the radar's drawing code
against WD4's frozen text turned up **two places where the frozen text cannot be met as written**, and some
details it left open. The open details are frozen here. The two contradictions are **for a ruling**, and
**no WD4 code is written until they are ruled**.

**Facts, read from source** (main `b7d7233`):
- `src/weatherscope/WxPalette.h` is pure C++11 (`stdint.h` only). Its `kRainTable[12]` holds the pinned MetPy
  table's 15-70 dBZ entries (codes 1-12, sha256 `f73f0d6264d4…`), and `Dim(v, pct) = (v·pct + 50) / 100`.
  The table's own 75 dBZ entry is **black**: Weatherscope's ruling replaced it with white (`kRain75`).
- **The planes have no type palette.** `AircraftManager.cpp:3618-3623`: the base colour is live green
  `(0, 255, 0)`, or `altitudeColor()` (green, lime `(170, 255, 0)`, yellow, cyan, white by altitude band), or
  the NoData grey `(90, 90, 90)`. With the sweep on, it is then **faded**, `scaleColor(base, f)`, truncating,
  for any f from **0.30** (`PaintBrightness`'s floor) to 1.0.

### Frozen now (consistent with the frozen text)

- **One table, not two.** `include/WxS2.h` includes `src/weatherscope/WxPalette.h` and reads `kRainTable`; it
  holds no colour values of its own except the two dither patterns. The rounding is `wxpalette::Dim`, the same
  function on host and device.
- **Codes 1-12** are `Dim(kRainTable[code − 1], 40)`. That is the frozen "× 0.40", and it does **not** take
  Weatherscope's 55 % dimming of code 1, which is a Weatherscope look, not the table.
- **14 and 15 are S2's own dithers**, because Weatherscope's hatch `(x + y) % 4 == 0` fails the frozen
  property: **16 of the 64** 2 × 2 windows over an 8 × 8 patch have no lit pixel (measured).
  - **14 (no coverage):** a checkerboard, lit where `(x + y) % 2 == 0`, in `kNoCoverage` `(40, 40, 40)`.
  - **15 (no data):** a grid, lit where x and y are both even, in `kMissingHatch` `(72, 72, 72)`.
  - Both measured: **0 of 64** windows empty. The two lit sets differ, and their colours differ.
- **Code 0** is exactly the background, `(0, 0, 0)`: the layer writes the background there.

| | check (host) | predicted |
|---|---|---|
| **WD4a** | code 0 | equals the background exactly, at every pixel |
| **WD4b** | codes 1-12 | each equals `Dim(kRainTable[code − 1], 40)`, read from `WxPalette.h`, not transcribed. *Sabotage:* × 0.4 dropped: red |
| **WD4c** | 14 and 15, every 2 × 2 window over a 16 × 16 patch at every offset | no window all background, for both; the two patterns differ at some pixel. *Sabotage:* 14 drawn as 0: red |
| **WD4d** | every code 1-15 against the **unfaded** plane colours (the five altitude colours, live green, the NoData grey), compared in RGB565 as the panel draws them | **0 equal** |

### For a ruling (the frozen text cannot be met as written)

**R1. Code 13.** The frozen text says codes 1-13 are "the pinned NWS table's colours × 0.40". The table's 13th
entry (75 dBZ) is black, so code 13 would equal the background: **the strongest echoes would draw as nothing.**
- **Proposed:** code 13 = `Dim(kRain75, 40)` = `(102, 102, 102)`, Weatherscope's ruled white at 40 %, read
  from the same header. Rain 75+ then reads as a light grey.
- **The alternative:** keep the table literally, and accept that 75+ dBZ is invisible.

**R2. "No S2 colour equals a plane colour" is false whenever the sweep fades the planes.** Measured over
f = 0.30 to 1.0 in 10⁻⁵ steps, with the device's float truncation, in both RGB888 and RGB565:

| S2 at 40 % | equals |
|---|---|
| code 2 `(0, 102, 0)` | live green faded to f = 0.40 |
| code 3 `(0, 80, 0)` | live green faded to about f = 0.31-0.32 |
| code 5 `(102, 102, 0)` | the yellow altitude colour faded to 0.40 |
| code 13 `(102, 102, 102)` (if R1's proposal) | the white altitude colour faded to 0.40 |

This is structural, not bad luck: S2 is "the standard colours at 40 %", the planes' colours are standard
colours too, and the fade covers 40 %. **No 40 % scheme can meet the frozen property against faded planes.**
Unfaded, 0 of 15 collide (WD4d above). The options:
- **(a) Grade it against the unfaded colours (WD4d), and rely on WD5's black plane outline** to separate a
  faded plane from weather. WD5 then carries a check that the outline is drawn whenever the layer is on.
  *(Recommended: the factor is provisional and is Daniel's look at the bench, and the outline is already in
  the design for exactly this.)*
- **(b) Put S2 below the fade floor.** At 25 %, every rain code is darker than any faded plane of the same
  hue. One collision remains: code 13 `(64, 64, 64)` equals the NoData grey faded to about 0.71, and only in
  the NoData state.
- **(c) Change the fade floor.** That is a change to the radar's own look, outside this work.

**What happens until the ruling:** WD5's predictions are written next. WD4's code is not.

## WD5, 2026-10-22: the wiring's design and predictions (frozen before any WD5 code)

**The design**, at the places it touches (main `b7d7233` + this branch):
- **The switch:** NVS key `wx-layer` (the name is provisional), `"true"` / `"false"`, read the way `triangle` is
  (`AircraftManager.cpp:869`). **Absent means off.** The radar config form gets one checkbox, saved with
  `SaveToggle("wx-layer")`. It is a new key with an off default, so it needs no `ConfigMigration` entry. A later
  ruling to make it default **on** would need one (CLAUDE.md, "a default only reaches keys that were never saved").
- **The fetch:** `FetchKind::Weather` joins `{ Feed, CloudConfig, Airports }` (`AircraftManager.cpp:372`) on the
  existing fetch task, so there is still one request in flight across every kind. **Weather is queued behind the
  feed and never preempts it.** The loop task asks `WxPolicy` (WD3) whether a request is due, and builds
  `GET <cloud-url>/api/v1/blipscope/wx/cref?lat=<2 dp>&lon=<2 dp>&r=<lround km>` with the feed's device headers,
  and `If-None-Match` as WD3(d) allows. The result is handed back to the loop task, as the other kinds are.
- **The buffers, all PSRAM** (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`), allocated the first time the layer is
  enabled and **never freed to make room for anything** (CLAUDE.md: a PSRAM buffer costs the internal heap
  nothing):
  - the crop bytes, 32,768 B (WD3(b)'s cap);
  - the layer, one RGB565 S × S sprite (115,200 B on the s3-128), rebuilt only on a new crop, a zoom or range
    step, a rotation change or a home change (WD2's inputs).
- **The draw order.** In `main.cpp`'s band loop (`main.cpp:846`), **on the radar screen with a crop that WD3(c)
  allows to be shown**, the layer's band is pushed **instead of** `fillScreen(black)`; otherwise the fill runs as
  today. Then the sweep, then `appManager.Draw` (rings, planes, labels) as today. So the order is
  **weather → sweep → rings → planes → labels**, and every other screen is untouched.
- **The black outline:** while the layer is shown, each plane marker (triangle or dot) is drawn over a black
  copy 1 px larger. While it isn't shown, nothing changes.

| | check | predicted |
|---|---|---|
| **WD5a** (CI, the artifact) | the string `/api/v1/blipscope/wx/` in each built ELF | **present** in every radar SKU's ELF (the anchor: the grep can see it); **absent** from every non-radar edition's ELF |
| **WD5b** (host, read from source) | the switch's default in both of its readers: `AircraftManager`'s read, and the config page's `prefs.getString("wx-layer", …)` | **off in both**. *Sabotage:* a `"true"` default in either one turns it red |
| **WD5c** (bench, the server's record of what the device sent: Analytics Engine, E2:08's id) | three 30-minute arms: factory default (off), then on, then off again (the first 2 min after each change left out) | **off: 0 `/wx` requests. On: 27-33, every one 200 or 304. Off again: 0.** The on arm is the control that shows the instrument can see a `/wx` request |
| **WD5d** (bench, the `[health]` line) | across the first layer build | `psram_free` falls by **the buffers' total ± 1,024 B** (the firmware prints each allocation's size). The internal heap's `largest` stays **within 2 KB** of its value before enabling |
| **WD5e** (bench, `/diag/fb`, layer on, echo on the screen; each capture fetched 3× because mid-draw frames happen) | rings over weather, outlines | (1) every ring pixel has the ring's colour: **0 exceptions**; (2) every plane marker has a fully black 1 px border: **0 markers without one**. The analysis script is committed before the captures |
| **WD5f** (bench, the off state is today's radar) | `/diag/fb` with the layer off, against the same scene drawn by the image without WD5 | **0 markers with a black border**, and frame p95 within **±1 ms** of the old image (same board, same hour band) |

**Outside the set, for any row:** stop and report, and don't re-run for a better number. WD5c-f need the server
live (#443's deploy) and the colours (WD4, after R1 and R2 are ruled).

### WD5 addendum: the transport (frozen before any WD5 code)

**The finding.** WD5's design says the crop is fetched "on the existing fetch task". But `HttpRequestManager`
has no binary-safe GET:
- `Get()` returns the body as a `String`, and a WXC1 crop holds zero bytes;
- the response headers it collects are `X-Cache`, `X-Upstream` and `Cache-Control`, with no `ETag`.

So WD5 needs one new method on the shared client.

**The design:** `HttpRequestManager::GetBytes(url, params, headers, uint8_t* buf, size_t cap, size_t& len, String& etag)`.
- **Same client:** the same `http` object and keep-alive connection as the other kinds, on the fetch task only.
- **The cap is enforced:**
  - A `Content-Length` over `cap` is refused **before anything is read**, and the connection is closed, since
    a half-read body would poison the keep-alive socket.
  - A chunked body is read through the existing `ChunkedSocketStream` (`HttpRequestManager.cpp:83`), which
    yields, so it can't starve the Task-WDT (the EAM boot loop). Reading stops at `cap + 1` bytes, and that
    means refused.
- **Reported:** `statusCode`, `len`, and `etag` (from a collected `ETag` header). A 304 carries no body and
  `len = 0`. No `String` holds the body at any point.

| | check | predicted |
|---|---|---|
| **WD5t1** (bench) | binary safety | for one query, the device's received crop and the server's are byte-identical. The device logs `len` and the first 6 bytes of sha256 for each 200; smoke's `/wx` fetch of the same query at the same frame gives the same pair |
| **WD5t2** (bench) | 304 | a revalidation after an unchanged frame gives status 304, `len` 0, and WD3's held crop is kept (`Outcome::NotModified`) |
| **WD5t3** (bench, the `[health]` line) | the shared client survives | over the 30 min on-arm of WD5c, the feed's own polls keep their success rate: `fetchHardFailures` grows by **no more** than in the off-arm |
| **WD5t4** (CI, the artifact) | no `String` body on this path | `GetBytes` contains no `getString(` and no `String` assigned from the stream (a source check in `run.sh`). *Sabotage:* a `getString()` call added to it turns the check red |

**Not checkable on the bench, and so not claimed:** a real over-cap body. The proxy caps every crop at
32,768 B (WD3(b) and the server's `CAP_BYTES`), so the device never sees one. The refusal is exercised only
by WD3's host table (`200 oversize`), which tests the policy's reaction, not this transport's.

### WD5 addendum 2: the buffers the pipeline actually needs (frozen before any WD5 code)

**The finding.** WD5's design lists two PSRAM buffers, the crop bytes and the layer sprite. The pipeline
WD1-WD3 built needs five:
- **WD3** keeps the held crop when a new 200 is refused, so the bytes that land can't overwrite it;
- `wxcrop::DecodeInto` expands a crop into a cells grid;
- `wxlayer::Build`, the function WD2 graded, writes **one code per screen pixel** into its own buffer.

Mapping colours straight out of `ForEachPixel` would skip that buffer, but it would make a second, ungraded path
through the same logic (CLAUDE.md, "when you add a second path"). So the layer goes through `Build`, unchanged.

| buffer | owner | bytes (s3-128 / 1.46" / 2.1") |
|---|---|---|
| landing (the response body) | the fetch task, while a weather request is in flight. **One in flight at most**, so the loop never reads it during a write | 32,768 |
| held crop (WD3's) | the loop task; replaced only on `Outcome::Replaced` | 32,768 |
| cells (`DecodeInto`'s output) | the loop task, at rebuild. **406 × 406**, P1's largest box with its float-rounding cell (#451 §5.1.1); `DecodeInto` refuses a crop that wouldn't fit (`BufferTooSmall`) rather than overrun | 164,836 |
| pixel codes (`Build`'s output) | the loop task, at rebuild | S² = 57,600 / 169,744 / 230,400 |
| the layer, RGB565 | the loop task; read by the band loop | 2 S² = 115,200 / 339,488 / 460,800 |
| **total** | | **403,172 / 709,580 / 919,004** |

All of them are allocated once, at the first enable, with `MALLOC_CAP_SPIRAM`. **A failed allocation leaves the
layer off** and logs which buffer failed. It never falls back to the internal heap. WD5d's check is unchanged:
`psram_free` falls by the printed total ± 1,024 B.

## WD4 addendum 2, 2026-10-22: R1 and R2 ruled (frozen before any WD4 code)

**The rulings, as received (reviewer rulings of 2026-10-22, pasted by Daniel):**
- **R1, Daniel's decision:** draw code 13 (75+ dBZ) as white at 40 %, matching Weatherscope's "75+ white". Not black:
  the strongest cores mustn't vanish.
- **R2:** don't decide it blind. Put both versions on the bench, 40 % with the black outline and 25 %, and Daniel
  picks on the real screen during his look.

**The two looks**, both from one table (`WxPalette.h`) through one rounding (`wxpalette::Dim`):

| | look **A** | look **B** |
|---|---|---|
| codes 1-12 | `Dim(kRainTable[code − 1], 40)` | `Dim(kRainTable[code − 1], 25)` |
| code 13 | `Dim(kRain75, 40)` = `(102, 102, 102)` (R1) | `Dim(kRain75, 25)` = `(64, 64, 64)` |
| 14 and 15 | the addendum's dithers, unchanged | the same |
| the plane outline (WD5) | **on** | **off**: B separates by brightness, below the 0.30 fade floor |

R2 names the outline only for A. If Daniel wants B with the outline too, that is a one-line change after his look.

**Until he picks, the shipping image draws look A**, the frozen S2. The switch exists **only in a bench env**,
`blipscope-s3-128-wxlook`, which restates the shipping env's flags and adds `-DWX_LOOK_AB`. There:
- the radar config form gains a select, "Weather look (bench): A 40 % + outline / B 25 %", NVS key `wx-look`;
- absent means A.

| | check | predicted |
|---|---|---|
| **WD4a-c** | as frozen, for **both** looks | as frozen. WD4b reads A's codes 1-12 at 40 % and B's at 25 %, and code 13 is `Dim(kRain75, pct)` in both |
| **WD4d** | every code 1-15 against the unfaded plane colours, RGB565, **both** looks | **0 equal in both** (measured beforehand: 0 at 40 %, 0 at 25 %) |
| **WD4f** (reported, not graded) | each look against the sweep-faded planes, f = 0.30-1.0 | **A:** codes 2, 3, 5 and 13. **B:** code 13 against the NoData grey only |
| **WD4e** (CI, the artifact) | the select's label `Weather look (bench)` | **absent** from the shipping `blipscope-s3-128` ELF; **present** in `blipscope-s3-128-wxlook`'s ELF (the positive control) |
| *sabotages* | B drawn at A's factor; code 13 drawn as the table's black; 14 drawn as 0 | WD4b(B) red; WD4b red at code 13; WD4c red |

### WD5 addendum 3, 2026-10-23: the dot outline has gaps, found on the host before any bench run (frozen before the fix)

**What was found.** WD5's design says each plane marker "is drawn over a black copy 1 px larger". The triangle does
that by construction: 8 copies of itself, one per 1 px offset. The **dot** does it with one black disc of radius 4
under the radius-3 dot. LovyanGFX's `fillCircle` (`LGFXBase.cpp:302-339`, the copy in this env's `.pio/libdeps`),
ported line for line, shows that disc misses **8 of the dot's 32** 8-neighbours: (±4, ±2) and (±2, ±4). So every
dot marker over weather would have 8 gaps in its outline, with the cell showing through beside the plane.

**Why the bench would not have caught it.** WD5e grades the outline on glass, but markers are triangles by default
(`prefs.getString("triangle", "true")`). A bench at defaults never draws a dot.

**The fix.** The dot is outlined the same way as the triangle: 8 copies of the dot, one per 1 px offset, in black,
then the dot. One loop serves both marker kinds. Nothing changes while the layer is not shown.

| | check | predicted |
|---|---|---|
| **WD5o** (host, `test/host/test_wx_outline.cpp`, reading the marker block from `src/AircraftManager.cpp`) | the dot's outline, under the ported `fillCircle` | the outline is drawn as offset copies of the dot at the dot's own radius, and it covers **every** 8-neighbour of the dot: **0 gaps**. *Control, in the same test:* the old construction (one disc 1 px larger) gives **8 gaps**, so the test can see a gap. *Sabotage:* putting back `fillCircle(x, y, 4, 0u)` turns WD5o red |
| **WD5e** | unchanged | it grades the triangles on glass. The dot path is graded only by WD5o |
| **WD5f** | unchanged | the outline is drawn only while the layer is shown, so the off state does not move |

**Reported, not graded: the rings and the green rain codes.** The range rings are (0,200,0), (0,64,0) and
(0,32,0) (`AircraftManager.cpp:4901-4903`). WD4 compared the weather colours with the planes' colours, never the
rings'. In RGB565:
- **Look B:** code 2 is (0,64,0), **exactly** the middle ring, so the ring vanishes where light rain sits under it.
  Code 4 is (0,36,0), 4 levels off the inner ring.
- **Look A:** code 4 is (0,58,0), 6 levels off the middle ring. No code equals a ring.

The rings are drawn over the weather, so WD5e(1) passes either way: it cannot see this. It is an input to
Daniel's A/B choice on the bench, not a defect, and nothing is changed for it here.

### Results for WD5 addendum 3 (host, Windows MinGW, before CI)

| | read | verdict |
|---|---|---|
| **WD5o**, on the code before the fix | "one centred disc at radius 4": **8 gaps**, WD5o red; the control 8; the order check passes | the test sees the defect it was written for |
| **WD5o**, on the fix | "8 offset copies at radius 3": **0 gaps**; the control 8; the order check passes | **as predicted** |
| **sabotages** | the old disc put back: **WD5o** red, and only it. The outline drawn after the dot: **WD5o order** red, and only it | **as predicted** |
| **the host suite** | `run.sh`: **ALL HOST TESTS PASSED** | |

WD5e and WD5f are bench rows and are not read yet.
