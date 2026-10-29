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

### WD5 addendum 4, 2026-10-23: WD5e's analysis, committed before any capture, and three wordings for a ruling

**The script:** `scripts/wx-fb-check.py`. It grades `/diag/fb` frames: the raw body, RGB565 big-endian, 240 × 240.
Every colour and the ring geometry are **read from the source**: the rain table and `Dim` from `WxPalette.h`, the
looks' percentages from `WxS2.h`, the planes' colours and the three rings from `AircraftManager.cpp`. The rings are
rasterised with LovyanGFX's own `drawCircle`, ported line for line. Its verdicts are PASS, FAIL, UNTRUSTWORTHY,
BLIND and rig, with distinct exit codes.

- **UNTRUSTWORTHY** when fewer than 80 % of a ring's pixels have the ring's colour. That means the frame is torn,
  mid-draw, or the geometry is wrong, so nothing in it is graded.
- **BLIND** when no frame puts a ring or a marker over weather.

Its selftest runs in `run.sh`, with eight synthetic frames drawn in the device's order, one verdict each:
- PASS: as drawn; a 1 px trail in the marker's colour; look B with no outline.
- FAIL: weather drawn over the rings; no outline; the old r=4 disc.
- UNTRUSTWORTHY: no rings.
- BLIND: no weather.

All eight are as predicted.

**For a ruling: three frozen wordings that cannot be met as written.** The proposal is what the script grades.

| | frozen | why it cannot hold | proposed |
|---|---|---|---|
| **R3, WD5e(1)** | "every ring pixel has the ring's colour: **0 exceptions**" | planes, trails, labels and airports are drawn **after** the rings and sit on them legitimately | an exception is a ring pixel whose colour is a **weather** colour of the look, not the ring's, inside a 3 × 3 block of that colour: weather drawn over the ring. A 1 px trail or text stroke never fills 3 × 3. **0 exceptions**, and BLIND unless some ring pixel has weather 2 px either side |
| **R4, WD5e(2)** | "every plane marker has a **fully black** 1 px border" | labels and reticles are drawn after the markers and may cover a border pixel legitimately | a marker fails when **any** 8-neighbour has a **weather** colour of the look, which is what the outline is for. **0 failures** in look A, and BLIND unless some marker sits over weather. Plane colours that are also weather colours of the look (WD4f) are left out and counted as ambiguous |
| **R5, WD5f** | "**0 markers with a black border**" with the layer off | with the layer off the background is black, so every marker's border is black **with or without** an outline. Glass cannot see it | drop the marker clause from the bench. The off state's outline holds by construction: `WxOutlineNow()` requires `wxShowing == Crop`, which only `DrawWeatherUnder` sets, after it pushes the layer. **WD5f keeps its frame-p95 clause** |

Until these are ruled, no WD5e or WD5f capture is graded.

### WD5 addendum 4, ruled 2026-10-26 (reviewer chat, pasted by Daniel)

The rulings were re-issued on 2026-10-26 because this branch still showed R3-R5 unruled. **No earlier ruling on them
was held**: the #445 comments of 2026-10-23 are the questions, and nothing in this session's record answers them. The
rulings, verbatim:

> **R3, WD5e(1):** your wording, accepted. An exception is a ring pixel with a weather colour of the look inside a
> 3 x 3 block of it; 0 exceptions; BLIND unless some ring pixel has weather within 2 px. If a ring's colour is also a
> weather colour of the look, name that pair as ungradable in the predictions.
>
> **R4, WD5e(2):** your wording, accepted. A marker fails when any 8-neighbour has a weather colour of the look; 0
> failures in look A; BLIND unless some marker sits over weather. Plane colours that are also weather colours are
> counted as ambiguous and reported.
>
> **R5, WD5f:** not dropped. With the layer off, a wrongly drawn outline still shows wherever a marker touches
> something drawn before it (at least the rings), as black cut into the ring line. Restate the clause on that: with the
> layer off, no 8-neighbour of a marker that lies on a ring (by your ported geometry) is black, leaving out pixels
> covered by anything drawn after the marker; 0 failures; BLIND unless some marker touches a ring. If the draw order
> makes that ungradable, show why in the predictions, and then the construction argument stands. WD5f keeps its
> frame-p95 clause.
>
> **The instrument:** /diag/fb can copy a frame mid-redraw (about 1 in 3 fetches on Weatherscope), and its Torn header
> misses it. #480 fixes the route for every edition: it waits for a finished frame and says when a copy isn't one. So
> capture WD5e/WD5f after #480 merges and feat/wx-device has main in it. If #480 is delayed, you may capture before it,
> with your UNTRUSTWORTHY rule stated as the filter in the predictions first.

**What follows, before any capture.** Each item is frozen in its own commit, ahead of the frames it grades.
- **R3: the BLIND condition changes.** It becomes "some ring pixel has weather **within 2 px**", in place of the
  proposed "2 px either side", and `wx-fb-check.py` changes to match.
- **R3: ungradable pairs.** A ring and a weather colour of a look that are equal in RGB565 are named in the predictions
  as ungradable. **They are computed from the source**, not typed. Addendum 3 already found one: look B's code 2 is
  exactly the middle ring.
- **R4:** graded as proposed. Ambiguous markers are counted and reported.
- **R5: the clause is restated** on the ring-contact test, and the script grades it.
  - **First, a reading of the draw order:** what is drawn after a marker, and whether any of it is black where it
    would touch a ring. That decides whether the clause can be graded or the construction argument stands.
  - **That reading is written into the predictions before the clause is.**
- **Capture timing:** after #480 merges, with main merged into this branch. If #480 is delayed, the UNTRUSTWORTHY
  rule is stated first as the filter.

### WD5 addendum 5, 2026-10-26: R3-R5 as ruled, frozen before the script changes and before any capture

**The ring/weather colour pairs R3 asks for, computed from the source** by `wx-fb-check.py`'s own loader, not typed:
- **Look A:** **none**. All three rings are gradable.
- **Look B:** **the middle ring**, r = 78, colour (0, 64, 0) = RGB565 `0x0200`. It is a look-B weather colour (addendum
  3's code 2), so it is **ungradable in look B**. The outer ring (0, 200, 0) and the inner ring (0, 32, 0) are gradable.
- **Plane colours that are also weather colours** (R4's ambiguous set): **6** RGB565 values in look A and **3** in
  look B.

**R5's draw-order reading: the clause is gradable.** Every frame is drawn in this order:
1. the layer, or the black clear (`main.cpp:854-856`);
2. the sweep (`:860`), drawn **before** the rings;
3. `appManager.Draw` (`:863`).

Inside `DrawRadar`, the order is rings, stale and weather notes, airports, the follow track, then per aircraft:
- its trail and its label, **before** its marker;
- then the marker, preceded by its outline when `WxOutlineNow()` holds;
- then its reticles and rings.

After the loop come the zoom overlay, the hold ring, the screen indicator, the visual alert and the toasts.

**What can put black on a ring pixel next to a marker after that marker is drawn:**
- **The two black boxes on this screen:**
  - `DrawZoomOverlay`'s (`AircraftManager.cpp:5219`, `:5232`), for 1.5 s after a zoom step;
  - `DrawTouchUnavailable`'s (`:5077`), only while touch is unavailable.

  The others (`:7511`, `:8954`, and `DrawClock`'s at `:4693`) belong to the Follow and route faces.
- **Fades can't reach black.** `PaintBrightness` floors at 0.30 (`TrackedAircraft.h:283`).
  - The dimmest trail green is 40 × 0.30 = 12, which is G6 = 3 in RGB565.
  - The dimmest marker is 0.30 of its colour. The no-data grey, 90, becomes 27.
  - The visual alert's edge band is at least 0.35 of its colour. Its full-screen flash leaves no rings, so the frame
    is UNTRUSTWORTHY.
- **No text is drawn on a black background.** A search for a two-argument `setTextColor` with a black background
  finds **0**. Its control, a planted line of that shape, is found.

**So R5 is graded under two capture conditions**, which also hold the WD5e captures:
- **no touch** in the 2 s before each fetch;
- **touch reported available** in the `[health]` line.

**The script changes** (`scripts/wx-fb-check.py`, committed after this addendum):
- **(1) R3:**
  - A ring is **ungradable** in a look when its colour is a weather colour of that look. It is left out and printed
    by name.
  - A gradable ring pixel is **exercised** when some pixel within 2 px of it (its 5 × 5 block) has a weather colour of
    the look. This replaces "2 px inside and 2 px outside".
  - **The exception rule is unchanged.** BLIND when no gradable ring pixel is exercised.
- **(2) R4:** unchanged. The ambiguous count is printed per frame and in total, as it is now.
- **(3) R5, a new mode** `--off`, for frames with the layer off. A marker is defined as in (2), from every plane colour,
  since no weather is shown.
  - **A marker is exercised** when a ring pixel, by the ported `drawCircle`, is among its 8-neighbours.
  - **A marker FAILS** when any such ring pixel is black, RGB565 `0x0000`.
  - **The verdicts:** 0 failures to PASS. BLIND when no marker is exercised. UNTRUSTWORTHY by the same 80 % ring
    rule.

**The selftest, predicted case by case** (each synthetic frame is drawn in the device's order):

| case | verdict |
|---|---|
| the eight existing cases | unchanged, **except** "look B, no outline": **PASS -> BLIND**. Its weather touches only the middle ring, which is ungradable in look B |
| look B, the weather blob across the **outer** ring | **PASS** |
| look A, weather 2 px on **one** side of a ring only | **PASS**. Exercised under R3, where the old rule needed both sides |
| `--off`: a marker on the middle ring, no outline | **PASS** |
| `--off`: the same marker drawn with the outline (the defect R5 catches) | **FAIL** |
| `--off`: no marker touches a ring | **BLIND** |
| `--off`: a non-black label drawn after the marker, over one of its ring neighbours | **PASS** |
| `--off`: no rings | **UNTRUSTWORTHY** |

**The captures, frozen.** The numbers are unchanged from WD5:
- **WD5e(1):** 0 exceptions.
- **WD5e(2):** 0 failures in look A.
- **WD5f:** 0 R5 failures, and frame p95 within ±1 ms.

How they are taken:
- **When:** after #480 merges and main is merged into this branch. Each capture is 3 fetches, as WD5 says.
- **If #480 is delayed** (the ruling allows capturing before it), the filter is this rule, stated first:
  - each frame is graded alone;
  - an UNTRUSTWORTHY frame is **dropped, counted and reported**;
  - the remaining frames are graded together.
- **A known blind spot of that filter, stated before any frame.** A copy torn between the layer's push and the rings'
  draw can show weather on a few ring pixels while 80 % of the ring is still there. That reads as a WD5e(1) exception.
  - **So before #480, a WD5e(1) FAIL cannot be told from a torn copy.** It is reported as "**FAIL, not separable from
    a torn copy**", and work stops there. No defect is claimed until a capture after #480 repeats it.
  - **#480 removes that case.**
- **The conditions:** WD5e needs rain in range of the board's home; with none, it is BLIND. Both of the R5 conditions
  above hold for every fetch.

**Outside the set, for any row:**
- an ungradable pair other than the one named above;
- a selftest case not as predicted;
- a BLIND, or any verdict not in the table.

Each of these means stop and report, without re-running for a better number.

**Amended before any run, found while implementing (2026-10-26).** The outer ring's colour (0, 200, 0), RGB565
`0x0640`, **is also a plane colour**: green at about 0.78, inside the fade range. Computed from the source; the middle
and inner rings are not plane colours.
- **The danger:** a ring alone never passes the marker filter (0 blocks of 2 × 2 in any of the three rings). A marker of
  exactly `0x0640` sitting on the outer ring, though, joins the whole ring into one component.
  - In (2), any weather next to the outer ring would then count against that "marker": a false FAIL. This was already
    in addendum 4's committed script.
  - In (3), ring pixels covered anywhere on the ring would count it exercised: a false PASS where the truth is BLIND.
- **The amended definition, for both (2) and (3):** a ring pixel, by the ported `drawCircle`, that shows its ring's
  colour is **never part of a marker**. A marker on the ring then keeps its own pixels and borders the ring line, which
  is neither weather nor black.
- **One more selftest case, predicted:** a marker of the outer ring's colour on the outer ring, over weather, with its
  outline. With the amended definition it is **PASS** in look A. **Control:** addendum 4's committed `grade` (from
  `2d89084`), run on the same synthetic frame, gives **FAIL**.

**Corrected before any run, the same day: two more findings while implementing.**
- **The paragraph above is wrong about (2).** A correctly drawn outline covers every 8-neighbour of its marker (WD5o
  proves it), so in look A the black outline cuts the marker off from the ring, and they cannot merge.
  - The merge can happen only with no outline. In (2) that is already a FAIL. In (3) it is the normal case.
  - **So the definition change stands for both, and has its effect in (3).**
  - **The case predicted above is withdrawn, together with its control** (addendum 4's `grade` would also give PASS
    there, for the reason just given).
  - **It is replaced by:** "`--off`: a marker of the outer ring's colour on the outer ring, and one black pixel on the
    outer ring about 40 px away from it". **Predicted PASS**, because that pixel is not next to this marker.
    **Control:** with ring pixels allowed into markers, the marker joins the whole ring and the far pixel counts
    against it: **FAIL**.
- **Plane labels can pass as markers.** `DrawAircraftInfo` draws in `scaleColor((0, 128, 0), brightness)`, read from
  the source, with brightness 0.30-1.0. That is 24 RGB565 values, **14 of them also plane colours**.
  - **In the glcd font the labels use** (the radar sets no font), the glyphs `*`, `@`, `g`, `p` and `q` each pass the
    marker filter alone, at 19, 20, 16, 14 and 14 px with a 2 × 2 block. Parsed from LovyanGFX's `glcdfont.h` in this
    env's libdeps.
  - **The harm:** such a glyph, in one of those 14 colours, over weather in look A, has no outline: a **false (2)
    FAIL**. Touching a ring in (3), it is a **false "exercised"**. Addendum 4's committed script has the same hole.
  - **The fix:** those 14 colours are **ambiguous**, as plane colours that are also weather colours already are
    (R4). They are counted and reported, not graded. **Gradable plane colours: 220 in look A, 221 in look B.**
    Full-bright green, `0x07E0`, stays gradable.
  - **One more selftest case, predicted:** "look A, a `p` drawn in a label colour that is also a plane colour, over
    weather". With the fix it is **PASS**, with 1 ambiguous. **Control:** addendum 4's committed `grade` gives **FAIL**
    on the same frame.

### Results for WD5 addendum 5: the script (local, before CI; no capture yet)

| | read | verdict |
|---|---|---|
| **The selftest** | **18 of 18 as predicted**, `WX FB CHECK SELFTEST PASSED`. The eight original cases: as before, except "look B, no outline" is now **BLIND** (exercised 0: its weather touches only the middle ring, ungradable in B). The new cases: look B outer-ring PASS (exercised 65); one-sided weather PASS (exercised 15); the label `p` PASS with **ambiguous 1**; `--off` PASS / FAIL / BLIND / PASS / UNTRUSTWORTHY; the outer-ring-coloured marker PASS; and **its CONTROL FAIL** | **as predicted** |
| **The label control** | addendum 4's committed `grade` (`2d89084`) on the same label-`p` frame: **`WD5e(2) FAIL`**. The new grade: PASS, ambiguous 1 | **as predicted** |
| **The ungradable pairs** | the script prints `ungradable rings ... none` for look A, and names r=78 `0x0200` for look B | **as predicted** |

The WD5e and WD5f captures are **not taken yet**. They follow #480, or the pre-#480 filter, as frozen above.

### The capture rule after #480, ruled 2026-10-26 (reviewer chat, pasted by Daniel), frozen before any capture

> "The weather layer's push must land after the Clearing store and before Drawn, so /diag/fb never copies it
> half-done; show that in the diff when you push. In the captures, grade only fetches with Finished: true; a
> Finished: false fetch is the instrument's, counted and not graded. The rest as ruled: capture when there's rain on
> the bench."

- **The placement, from the merge `da5e7f7`.** Against main, the only `src/main.cpp` change is the layer push, at lines
  866-870. It sits after `SetPhase(kPhaseClearing)` (865), and before `kPhaseCleared` (872), the sweep (876),
  `appManager.Draw` (879) and `SetPhase(kPhaseDrawn)` (880). `/diag/fb` counts a copy as finished only when the phase
  reads finished at both ends with an unchanged clear count, so it cannot copy a half-pushed layer.
- **Every fetch's `X-Blipscope-Frame-Finished` header is recorded.**
  - **Graded:** only fetches with `true`.
  - **A `false` fetch** is the instrument's: counted and reported, never graded.
  - **The filter is not used:** #480 merged without stalling, so `--drop-untrustworthy` stays unused. A frame graded
    UNTRUSTWORTHY despite `Finished: true` is outside the set, and is a finding about the instrument.

### WD5e/WD5f capture protocol, frozen 2026-10-26 before E2:08 is flashed

**The trigger.** `rain_probe` (production `/wx/cref` at the Bend point, as a device asks it) reported echo within 50 km
at 22:15Z: 3 cells within 50 km, 1,519 within 100 km, and rising across three polls.

**The images**, both written app-only (`bench-flash.ps1 -AppOnly`, so NVS, Wi-Fi and location are kept):
- **WD5:** this branch's `blipscope-s3-128-wxlook`, code as at `da5e7f7`.
- **Old:** main `962ccb7`'s `blipscope-s3-128`, which has no WD5. It fetches production `/blips`, so it is also the
  build E2:08 is left on afterwards, as the Bend feed's source (the ruling's step 3).

**The serial record.** One `bench-trace.ps1` per image, a single open held for the whole image. Its `[health]` lines
give frame p95, and its `[touch-wd]` lines give the touch state. Known: closing a trace leaves this board in ROM
download mode. That is recovered afterwards, by the documented `read_mac` path.

**The settings.** They are changed only through the config page:
1. GET the form;
2. change `wx-layer` or `wx-look`, and nothing else;
3. POST every other field exactly as read (it is a whole-form save).

**The arms, in order, on the WD5 image:**
1. **Layer on, look A:** 6 moments 2 min apart, 3 `/diag/fb` fetches each.
2. **Layer on, look B:** the same.
3. **Layer off:** the same, plus 10 min of `[health]` lines.

Then the **old image:** 10 min of `[health]` lines, starting within 60 min of arm 3's (the same hour band).

**What each fetch records:** its status, its `X-Blipscope-Frame-Finished` header, and its body.
- **Only `Finished: true` fetches are graded.** `false` ones are counted.
- **Nobody touches the board** during the session.
- **Touch must be available:** no `[touch-wd]` "touch unavailable" line, and the `[health] touch-wd wedges=` count
  unchanged across each arm.

**Grading:**
- **Arm 1:** `wx-fb-check.py --look A` (WD5e(1) and (2)).
- **Arm 2:** `--look B` (WD5e(1)).
- **Arm 3:** `--off` (WD5f's marker clause, R5).
- **WD5f p95:** the median of the arm's per-30 s `p95=` values. The WD5 image with the layer off against the old image:
  `|Δ| ≤ 1 ms`.

**Outside the set, for any arm:** stop and report, without re-running for a better number:
- a BLIND;
- an UNTRUSTWORTHY frame with `Finished: true`;
- a touch-unavailable line;
- a failed flash;
- any verdict not as predicted.

**Afterwards:** E2:08 stays on the old image. The Bend feed's `written` count is read in the 10 min after it boots, to
show that a Bend device feeds production again.

### The capture session of 2026-10-26: STOPPED before any capture, on a request storm (no fix until a ruling)

**What ran:**
- **The flash:** E2:08 got this branch's `blipscope-s3-128-wxlook`, app-only. The `[build]` banner read
  `env=blipscope-s3-128-wxlook`.
- **The trace:** one `bench-trace.ps1` was opened and held.
- **The setting:** at 22:21:41Z, `wx-layer` was turned on through the config page. `cfg_post.py` checked that only
  that field changed.

**What happened:**
- **The trace:** from 22:21:42 to 22:21:47Z, the board's `[wx]` lines show two `200` replies and then a run of `304`s,
  several a second, each printing `next=60s`.
- **The server's record** (Analytics Engine, E2:08's own points, `/wx` recorded as `/other`): **23 requests in 5
  seconds**, three `200`s and the rest `304`s, then none until the layer was turned off at 22:22:14Z.
- **The prediction it breaks:** WD5c predicts **27-33 in 30 minutes**. This is outside the set, so **the session
  stopped**. No `/diag/fb` frame was captured, and nothing was graded.

**The cause, read from the code** (not yet confirmed by a test):
- **The feed path sets its timer when it sends:** `lastFetch = now; RequestFetch();`
  (`AircraftManager.cpp`, the fetch block).
- **The weather path does not.** `wxpolicy::ShouldFetch` (`include/WxPolicy.h`) stays true until `OnResponse` sets
  `attempted` and `lastAttemptMs`, which happens only when the **reply** arrives.
- **So** every loop pass between the send and the reply queues another request, and the fetch task then drains them.
- **It should recur at every interval:** the same burst should come back each time the 60 s interval lapses. The layer
  was on for 33 s, so a second burst was not observed.
- **A further consequence:** the feed's own requests share that queue.
- **Why no test caught it.** This is CLAUDE.md's "second path" shape. WD3's host tests grade the policy's answers;
  nothing grades when the wiring records an attempt.

**What was put back:**
- **The layer:** off at 22:22:14Z, verified as the only changed field.
- **The board:** E2:08 has main `962ccb7`'s `blipscope-s3-128` again (app-only, verified write), which fetches
  production `/blips`, so Bend has its feed source.

**Nothing else is changed.** The fix, the predictions for it, and when to resume the captures wait for a ruling.

**CORRECTION, before any fix (2026-10-26): the mechanism stated above is wrong; the code shows another.**
- **What was wrong:** "every loop pass between the send and the reply queues another request" cannot happen.
  - The loop schedules a fetch only behind `if (!fetchInFlight)` (`AircraftManager.cpp:1717`).
  - The fetch queue is depth 1, one outstanding at most (`:2662`).
  - So the 23 requests were **sequential**, one round trip each: about 200 ms apart, which matches the server's
    record.
- **What happens, all read from the source:**
  1. `Update()` captures `now = millis()` at the start of the pass (`:1438`).
  2. Later in that pass, `ConsumeFetchResult()` (`:1708`) hands the weather reply to `ConsumeWeather`, which calls
     `wxpolicy::OnResponse(..., millis())`. That sets `lastAttemptMs` a few ms **after** `now`.
  3. The same pass then asks `wxpolicy::ShouldFetch(wxState, enabled, now)`. `(uint32_t)(now - lastAttemptMs)` is
     negative, so it **underflows** to about 2^32, which is ≥ the 60 s interval, and the answer is "due" at once.
  4. The request goes out, the reply returns about 200 ms later, and the cycle repeats.
- **Why it ended at 22:21:47Z:** a pass where the **feed** was due took the turn (the server's record shows a `/blips`
  at 22:21:47Z). The following pass consumed a feed result, not a weather reply, so `lastAttemptMs` was older than
  `now`, the difference came out small and positive, and the answer was "not due".
- **The alternative, excluded:** concurrent queued requests. The `!fetchInFlight` gate and the depth-1 queue rule them
  out, and so does the requests' ~200 ms spacing.
- **What this means for the ruled fix:** recording the attempt at the **send**, with the pass's own `now`, means a
  reply never touches the interval's start, so this underflow cannot arise.
  - **But a host test can reproduce the old wiring's burst only if it drives the loop in the device's order:** `now`
    taken at the start of the pass, the reply consumed afterwards at a later clock.
  - **So the simulation is built that way,** and the predictions below say so.

### WD5 addendum 6, 2026-10-26: the storm's fix, as ruled. Frozen before its code

**The ruling (reviewer chat, pasted by Daniel), in short:**
- record the attempt at the send;
- `ShouldFetch` false until the interval lapses, reply or not;
- a reply never triggers a refetch;
- a reply that never comes ends after a bounded timeout, as a failed attempt with the backoff;
- all of it in `WxPolicy.h`, host-tested;
- a loop-driven 30 min host test within WD5c's 27-33, whose sabotage (today's wiring) bursts;
- on the board, 10 min with the layer on, before any capture;
- then the captures as frozen.

**The policy (`include/WxPolicy.h`), with a new property (g):**
- **`OnSend(State&, nowMs)`:** records `attempted`, `lastAttemptMs = nowMs` (**the send**, with the loop pass's own
  `now`), `inFlight = true` and `sentMs`. It returns a sequence number for the request.
- **`ShouldFetch(State&, enabled, nowMs)`:**
  - (a) first, as now.
  - **(g) While a request is in flight, the answer is false.** When `nowMs − sentMs ≥ kReplyTimeoutMs` (**30 s**),
    the request is ended as a failed attempt: the existing `Backoff`.
  - Then (e), as now, measured from the send.
- **`OnResponse(State&, seq, asked, status, ..., etag, nowMs)`:**
  - **A reply whose `seq` is not the one in flight** (late, after the timeout, or stray) **changes nothing:** `Kept`.
  - **Otherwise it ends the in-flight state and updates the crop, ETag and interval as before.** It **never moves
    `lastAttemptMs`**. `nowMs` is kept only as `lastReplyMs`, for diagnostics.

**The wiring:**
- **`RequestWeather(now)`** calls `OnSend` with the pass's `now`, and the request carries the `seq`.
- **A failed enqueue** is answered at once as a network failure (`OnResponse` with status −1), so it backs off.
- **`ConsumeWeather`** passes the reply's `seq`.

| | check | predicted |
|---|---|---|
| **G1** | host, the table: a send makes `ShouldFetch` false at once and until +60 s; a 304 at +0.4 s leaves it false until +60 s **from the send**, then true; a lost reply: false until +30 s, then the attempt fails, the interval is 120 s, and the next is due at +120 s from the send; a reply after the timeout, and a stray `seq`: `Kept`, state unchanged | all pass, as property **(g)** |
| **G2** | host, the existing table and 200,000 random steps, with `OnSend` before each `OnResponse` (the device's order) | **(a)-(f): 0 violations** |
| **G3** | host, **the loop row**: 30 min of 50 ms passes in the device's order (`now` at the start of the pass, a reply consumed afterwards at `now` + 2 ms), with one fetch slot shared with a feed (every 5 s, 300 ms each). Weather replies take 400 ms; every 4th takes 2,500 ms; every 12th is **lost** (the policy never hears; the slot frees at +10 s). The 1st and every 6th are `200`, the rest `304` | **weather requests 27-33, point prediction 28** (two losses each skip one slot); **at most 1 weather request in any 5 s window**; feed requests **≥ 355** (360 nominal) |
| **S-g** | sabotage, planted into a copy of the header: **today's wiring**. `OnSend` records nothing, `OnResponse` records the attempt at the reply's clock, and the in-flight guard is gone | **G3 red with a burst**: > 33 requests and ≥ 3 in some 5 s window. Red in **(g) only**. The eight existing sabotages stay as predicted, each red in its own property only |
| **G4** | `run.sh`: WSL `Ubuntu-24.04` g++ 13.3, and MinGW 5.1 | MinGW: **ALL HOST TESTS PASSED**. WSL: the same, apart from the board block's known toolchain gap, which main shares |
| **G5** | builds: `blipscope-s3-128` and `blipscope-s3-128-wxlook` | both build |
| **G6** | CI on #445 | 0 failures |
| **G7** | **the board, before any capture.** E2:08 gets the new `wxlook` image (app-only, banner checked) and one trace, held. **Start ≥ 12 min after boot, untouched**, so the feed is idle at 15 s by day. Then **B:** 10 min with the layer off; **A:** 10 min with it on (`cfg_post.py`). Both are read from the server's record (E2:08's points; `/wx` is `/other`) | in A: **9-11 `/wx` requests**; **at most 2 in any 5 s window**; `/blips` A/B **0.8-1.25**. Then the layer off, verified as before. **A and B must not straddle sunset** (solar elevation at the Bend point, same sign at both ends); if they would, the arm is re-timed **before** it starts |

**Outside the set:** stop and report, without re-running for a better number.

**After G7:**
- **The captures,** as frozen, when there's rain in range.
- **Then** E2:08 goes back on main's shipping build.

**Not fixed here, noted:** if the fetch task's `xQueueSend(fetchResultQueue)` fails, it deletes the result. Then
`fetchInFlight` never clears, and every fetch stops. That is a pre-existing path outside WD5, recorded for a later
look.

### Results for WD5 addendum 6 (local, before CI; the board's G7 follows)

| | read | verdict |
|---|---|---|
| **G1** | the four (g) table rows pass: not due until +60 s from the send, with the 304 at +0.4 s triggering nothing; the lost reply ends at +30 s, then 120 s and due at +120 s; the late reply and the stray `seq` are `Kept` | **as predicted** |
| **G2** | the existing table, and (a)-(f) over 200,000 random steps in the device's order: **0 violations** | **as predicted** |
| **G3** | the loop row: **28 weather requests** in 30 min (point prediction 28), **at most 1** in any 5 s, **358 feed requests** (≥ 355) | **as predicted** |
| **S-g** | today's wiring planted: **168 weather requests, 7 in one 5 s window**, feed 335. Red in (g) only. The eight existing sabotages: each red in its own property only. The header was unchanged afterwards (`eec8b5120c60`) | **as predicted** |
| **G4** | MinGW 5.1: **ALL HOST TESTS PASSED**. WSL `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`: its one FAIL is the board block's "no xtensa toolchain found", the gap main shares, and the loop row reads 28 / 1 / 358 there too | **as predicted** |
| **G5** | `blipscope-s3-128` 1,939,744 B and `blipscope-s3-128-wxlook` 1,940,272 B, both built | **as predicted** |

### WD5 addendum 7, 2026-10-26: the policy's elapsed time made safe (ruled), frozen before its code

**The ruling (reviewer chat, pasted by Daniel):** "make the policy's elapsed time safe when a recorded time is later
than now (it reads as not due), with a host row for exactly today's order. The simulation must still give 28. If that
is the only change after G7, G7 carries; show the diff."

**The change, `include/WxPolicy.h` only.** Both of `ShouldFetch`'s elapsed times become a wrap-safe **signed**
difference, `(int32_t)(nowMs − t)`:
- **the in-flight timeout:** a negative elapsed time is **still in flight**, never "timed out";
- **the interval:** a negative elapsed time is **not due**.

This holds within ±24.8 days of `millis()`. `AircraftManager` and the tests' calls are unchanged.

| | check | predicted |
|---|---|---|
| **H1** | a new (g) table row, **today's order**: the attempt recorded 2 ms **after** the `now` the pass asks with. `OnSend` at 1002 then `ShouldFetch` at 1000: **false** (in flight, not timed out). The reply consumed, then `ShouldFetch` at 1000: **false** (not due). At 61,002: **true** | pass, as (g) |
| **H2** | the loop row, unchanged | **28** weather requests, at most 1 in any 5 s, 358 feed requests |
| **H3** | **S-g, redefined** to stay "today's wiring": it also reverts the hardening to the unsigned difference, as the code was. **New S-h:** only the hardening reverted, with the attempt still recorded at the send | **S-g:** the loop bursts as before (168 requests, 7 in 5 s), red in (g) only. **S-h:** red in (g) only, on H1's row, while the loop row stays at 28, because the send-time attempt alone already prevents the burst. The eight earlier sabotages: unchanged |
| **H4** | the suites: MinGW and WSL g++ 13.3 | MinGW: ALL HOST TESTS PASSED. WSL: the board block's known gap only |
| **H5** | the diff after G7's image (`4caa662`) | **`include/WxPolicy.h` (the two comparisons and a comment), `test/host/test_wx_policy.cpp` (H1) and `test/host/sabotage_wx_policy.py` (S-g, S-h), and nothing else**. So G7 carries, by the ruling |

**Outside the set:** stop and report.

### Results for WD5 addendum 7 (local, before CI)

| | read | verdict |
|---|---|---|
| **H1** | today's order: `OnSend` at 1002, `ShouldFetch` at 1000 is false (in flight); after the reply, false at 1000 and at 61,001, true at 61,002 | **as predicted** |
| **H2** | the loop row: **28** weather requests, at most 1 in 5 s, 358 feed requests, under MinGW and under WSL g++ 13.3 | **as predicted** |
| **H3** | **S-g** (today's wiring, the unsigned compare included): 168 requests, 7 in 5 s, red in (g) only. **S-h** (only the hardening reverted): red in (g) only, on H1's row, with the loop at 28 / 1 / 358. The eight earlier sabotages are unchanged, each in its own property. The header was unchanged after the run (`ec214a6a1200`) | **as predicted** |
| **H4** | MinGW: **ALL HOST TESTS PASSED**. WSL `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`: the board block's known gap only | **as predicted** |
| **H5** | the change after G7's image (`4caa662`): `include/WxPolicy.h`, `test/host/test_wx_policy.cpp`, `test/host/sabotage_wx_policy.py`, and this doc | **as predicted**, so G7 carries |

### G7, the board (2026-10-26). The `/wx` rows pass; the `/blips` row is OFF-PREDICTION, and its instrument cannot resolve the band

**The arm, all on E2:08:**
- **The image:** `blipscope-s3-128-wxlook` from `4caa662` (app-only, banner checked, one trace held). Addendum 7's
  hardening changed only the policy and its tests, so G7 carries for it.
- **B:** `[23:12:00, 23:22:00)`, the layer off, untouched since the 23:00Z boot. *(Wrong, corrected under WD6's board
  results: the trace has presses at 23:01:44-46Z and 23:11:20-21Z.)*
- **A:** `[23:22:00, 23:32:00)`, the layer on.
- **The switches:** `cfg_post.py` verified both, each time with only `wx-layer` changed.
- **The sun at the Bend point:** +17.2°, +15.8° and +14.3° at the three ends, the same sign, so no sunset was
  straddled.
- **Touch:** available throughout. `wedges=0` in all 64 `[health] touch-wd` lines, and no "touch unavailable" line.

**The server's reads**, at 23:37:02 and 23:42:02Z, were **identical**:

| row | read | verdict |
|---|---|---|
| `/wx` in A | **10**: five `200` and five `304`, one a minute (the trace shows 23:22:01 to 23:31:01Z) | **PASS** (9-11) |
| the most `/wx` in any 5 s | **1** | **PASS** (≤ 2) |
| `/wx` in B | 0 | as expected, with the layer off |
| `/blips` A/B | **0.667** (12 / 18 points) | **OUTSIDE 0.8-1.25** |
| the layer off afterwards | verified | PASS |

**The `/blips` row, read for its cause, without re-running anything:**
- **First, my reader was wrong.** `g7_read.py` summed `_sample_interval` alone. This dataset's points are sample-weighted
  (`double4`; the proxy README says to use `SUM(_sample_interval × double4)`).
  - Weighted, `/blips` reads **B 162, A 120: 0.741, still outside.**
  - `/wx` points have weight 1, so its rows are unaffected.
- **Then, the instrument cannot see this row.** For E2:08, `/blips` is written **1 in 10, at weight 10**. About 120
  requests in a 10 min window therefore give about 12 sampled points, ±3.5 (binomial): **about ±30 %**, which is wider
  than the 0.8-1.25 band.
- **The board's own count:** its `[perf]` line reports feed polls per minute, and it reads **12 a minute in both
  windows** (one minute of 8 in B, one of 11 in A). That is about 116 in B and 119 in A, **a ratio of about 1.0**.
- **The named alternative, that the layer slowed the feed:** the board's poll counter excludes it, because the cadence
  did not move when the layer went on.

**Verdict, frozen-text first:** the row is **outside the set**, so this stops for a ruling. The pre-registered way to
read it is "the instrument is unreliable at this scale", since the server samples this route too coarsely for one
device in 10 minutes.

**Put back:** E2:08 has main `962ccb7`'s shipping build again (23:44Z, app-only, verified).

## WD6, 2026-10-26: the sweep over the weather, looks 3 and 4 (ruled). Frozen before its code

**The ruling (reviewer chat, pasted by Daniel), recorded with its source.** Daniel saw a mockup of four looks (1 today,
2 behind, 3 see-through, 4 lit) and said **"i like 3 or 4"**. Both are built as a bench setting on E2:08, as `wx-look`
is, so he picks on the glass. **Until he picks, the image draws 4.**
- **3, see-through:** in the wedge, the wedge's green level is added to each pixel's green channel, clamped at full;
  red and blue are left as they are.
- **4, lit:** in the wedge, an echo pixel takes its own rain colour at the look's percent raised toward 100 % with the
  wedge level. A dry pixel takes the wedge's green, as today.
- **Both:** the beam and its head as today; rings, airports, trails and planes on top. With the layer off, nothing
  changes.

The reviewer confirmed that this block is the whole ruling.

**The design:**
- **`include/WxSweep.h`, pure C++11 and host-tested:**
  - **The wedge:** the same 18 slices as `DrawRadarSweep` (tail 1.0 rad, level `(1 − i/18)² × 120`), rasterised
    with **LovyanGFX's `fillTriangle`, ported line for line** (with its collinear `drawLine` case).
  - **One level per pixel:** each pixel gets the level of the **last** slice that covers it, which is what today's
    opaque wedge shows. So a pixel on a shared edge is blended once, never twice.
  - **Look 3:** `SeeThrough565(c, lvl)`. Green becomes `min(255, (G6 × 4) + lvl) >> 2`; the R and B bits are
    unchanged.
  - **Look 4:** `Lit565(code, pct, lvl)`. An echo code (1-13) is `To565(Dim(its base colour, p))`, with
    `p = pct + ((100 − pct) × lvl + 60) / 120` (integer). So `p` is the look's percent at the tail (lvl 0) and 100
    at the beam (lvl 120). Any other code (0 dry, 14 no coverage, 15 no data) takes the wedge green `(0, lvl, 0)`,
    as today.
- **The device:**
  - **`DrawSweepOverWeather`** runs only when the layer was pushed this frame (`wxShowing == Crop`). It blends the
    wedge in place in the canvas buffer, with the sprite's byte order handled, then draws the beam and its head with
    today's code.
  - **Otherwise** today's `DrawRadarSweep`, unchanged.
  - **The setting:** the bench env gets `wx-sweep` ("3" or "4"; absent is 4), a select beside `wx-look`. The shipping
    image is fixed at 4.

| | check | predicted |
|---|---|---|
| **W1** | host, exhaustive: `SeeThrough565` over all 65,536 colours × lvl 0-120 | R and B bits **identical**; green exactly as above; lvl 0 changes nothing |
| **W2** | host, exhaustive: `Lit565` over codes 0-15 × looks A/B × lvl 0-120 | echo codes equal `To565(Dim(base, p))`. **lvl 0 is exactly the layer's own colour.** At lvl 120 the code's colour at 100 %. The hue is kept, because it is the same `Dim` of the same base. Codes 0, 14 and 15: the wedge green |
| **W3** | host, the buffer render: a synthetic 240 × 240 layer holding every code; the wedge at 16 beam angles; looks 3 and 4 | **every pixel outside the wedge exactly equals the layer** (0 mismatches). Inside, look 3 keeps the R and B bits (0 mismatches), and look 4 equals `Lit565` (0 mismatches). No pixel is blended twice. The wedge's pixel count is printed |
| **S-w** | sabotage: the opaque wedge restored (every wedge pixel set to the green) | **W3's inside rows red** for both looks; the outside row stays green |
| **W4** | **the frame time, predicted from G7's measurement** | **The measurement:** the layer added 12.7 ms to the frame avg (41.7 → 54.4) for 57,600 pixel copies, **about 0.22 µs per pixel**. The wedge is about 7,200 px (a 57° sector at r = 120; W3 prints the exact count). **Look 3** adds a read-modify-write per wedge pixel: **+1.5 ms**. **Look 4** adds a code read and a `Dim`: **+2.2 ms**. **Graded on `resid`** (p95 minus the firmware's own model for that contact count), so that traffic cancels. G7's layer-on `resid` median was −2.2 ms, so look 3's predicted median is **−0.7 ms** and look 4's **0.0 ms**, each **within −1/+3 ms** of its point |
| **W5** | WD5f, with the layer off, on the sweep build | frame p95 median within **±1 ms** of the old image's (main `962ccb7` shipping), on the same board in the same hour band. `resid` is reported beside it |
| **G7′** | the cadence row, **re-registered by ruling**: the layer off 10 min, then on (look 4) 10 min, on the sweep build. Polls per window from the board's own `[perf] polls=` counter | **on/off ratio 0.9-1.1.** The server's sampled ratio (weighted) is reported beside it, **ungraded** |
| **W6** | the suites, the builds and CI | MinGW: ALL PASS. WSL: only the board block's known gap. Both builds succeed. CI: 0 failures |

**The run, in order, with one trace held per image:**
1. **The old image** (E2:08 is on it now): 10 min of `[health]`, for W5's baseline.
2. **Flash the sweep build,** then **≥ 12 min untouched.**
3. **B′:** the layer off, 10 min (W5, and G7′'s off window).
4. **A′:** the layer on, look 4, 10 min (G7′'s on window, and W4 for look 4).
5. **Look 3:** 10 min (W4 for look 3).
6. **Look 4 back.** The trace closes, and the board is recovered with `esptool read_mac` if it is silent. It is left on
   the sweep build with the layer on, for Daniel's comparison on the glass.

No window may straddle sunset (01:00Z at the Bend point). Each window's sign is checked first.

**Outside the set:** stop and report, without re-running for a better number.

**Then:** Daniel picks, the captures follow in rain on his pick, and then E2:08 goes back on the shipping build.

### Results for WD6's host rows (local, before CI; the board run follows)

| | read | verdict |
|---|---|---|
| **W1** | `SeeThrough565` over all 65,536 colours × lvl 0-120: **0 mismatches** (R and B kept; green as defined; lvl 0 changes nothing) | **as predicted** |
| **W2** | `Lit565` over codes 0-15 × looks A/B × lvl 0-120: **0 mismatches** (lvl 0 is the layer's own colour, 120 the code at 100 %, non-echo codes the wedge green) | **as predicted** |
| **W3** | the render, 64 runs (16 angles × 2 looks × looks A/B): **outside 0 mismatches, inside 0 mismatches**, every marked pixel blended exactly once and its mark cleared. The wedge is **7,283 px** on average (W4 assumed about 7,200) | **as predicted** |
| **S-w** | the opaque wedge restored: red rows **[W3-in]** only. The header was unchanged afterwards (`4232849bcde9`) | **as predicted** |
| **W6 (local half)** | MinGW: **ALL HOST TESTS PASSED**, WxS2's rows unchanged after `Echo` moved onto the shared `EchoAt`. WSL `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`: the board block's known gap only. Builds: `blipscope-s3-128` 1,942,000 B and `wxlook` 1,943,024 B. The bench label `Sweep over weather (bench)` is **absent from the shipping image (0)** and present in `wxlook` (1), read from the binaries | **as predicted** |

**Stated, not graded by a host row:** that the ported rasteriser covers exactly LovyanGFX's pixels. It is a line-for-line
port of `fillTriangle` and `drawLine`; the host cannot run LovyanGFX to compare them.

### WD6, the board (2026-10-27). W4, W5 and G7′ are all OFF-PREDICTION; stopped for a ruling

**The arm, all on E2:08:**
- **The old image:** main `962ccb7` shipping, traced 00:05:30-00:16:02Z, untouched, at the idle cadence (4 polls a minute).
- **The sweep build:** `blipscope-s3-128-wxlook` from `53ba354`, app-only, banner checked, one trace held. It booted at
  00:17:39Z. **There is no `[touch]` line anywhere in its trace.**
- **The windows:**
  - B′: `[00:29:31, 00:39:31)`, the layer off.
  - A′: `[00:39:31, 00:49:31)`, the layer on, look 4.
  - Look 3: `[00:49:32, 00:59:32)`.
  - Look 4 back at 00:59:33Z, with the layer left on.
- **The switches:** made with `cfg_post.py`; each was verified with only its one field changed.
- **The sun at the Bend point:** +5.2°, +3.5° and +1.8° at the window starts, so no window straddled sunset.

| row | read | predicted | verdict |
|---|---|---|---|
| **W4, look 4** | resid median **+3.8 ms** (19 lines; p95 62.2, n 29) | 0.0, band −1..+3 | **OFF** |
| **W4, look 3** | resid median **+3.8 ms** (19 lines; p95 60.9, n 26) | −0.7, band −1.7..+2.3 | **OFF** |
| **W5** | p95 median: B′ **50.0** against the old image's **47.5**, so \|Δ\| is **2.5 ms**. Resid beside it: −9.7 and −9.8 | ≤ 1 ms | **OFF** |
| **G7′** | polls: A′ **120** / B′ **39** = **3.08** (10 `[perf]` lines each) | 0.9-1.1 | **OFF** |
| the server, ungraded | weighted `/blips`: A′ 152 / B′ 39 = 3.90 | reported only | — |

**G7′, read for its cause: the cadence followed the idle clock, not the layer.**
- **The counter:**
  - 12 polls a minute until 00:27:31Z.
  - 6 at 00:28:31Z, then 4 a minute (the 15 s idle cadence) from 00:29:31Z. That is 600 s after boot's `Initialise()`
    (its `[cloud] config` line is at 00:17:47Z), with the layer off and nothing changed. So **B′ ran wholly idle.**
  - Back to 12 a minute the minute after the 00:39:31Z POST, and through both layer-on windows.
- **The mechanism, read from the code:**
  - A saved config runs `ConsumeConfigChanged()` → `appManager.Initialise()` (`src/main.cpp`).
  - `Initialise()` seeds `lastTouchActivityMs = millis()` (`src/AircraftManager.cpp`).
  - `CurrentPollIntervalMs()` then returns the active 5 s for 600 s.
  - **So every `cfg_post.py` switch puts the board at the active cadence for 10 min.** This protocol switched the
    layer on with a POST, and B′ had none.
- **The named alternative, that the layer itself raises the cadence, is excluded by the code:**
  - `CurrentPollIntervalMs()` reads only the touch clock, the night test and the fleet config.
  - `lastTouchActivityMs` is written in three places: `Initialise()`, a touch, and the bench `k` key. None is weather
    code.
  - The data cannot exclude it on its own, because both layer-on windows began with a POST and lasted ≤ 600 s. The
    exclusion is the code's.
- **The server's 3.90 (ungraded)** points the same way, as it would under either reading.

**G7's record is wrong in one line.** It says B was "untouched since the 23:00Z boot", but G7's trace has presses:
- **23:01:44-46Z:** a tap and two zoom swipes.
- **23:11:20-21Z:** a hold and a tap.

The 23:11:21Z tap kept the board active until 23:21:21Z, which is why the counter read 12 a minute in B. The `[perf]`
line at 23:21:52Z reads 8, as the idle clock ran out, and the 23:22:01Z POST re-armed it.

**Without that tap, G7's B would have gone idle at about 23:11:46Z** (600 s after the 23:01:46Z swipe) and read about
40. So G7 had the same confound, hidden by a touch. G7's `/wx` rows do not depend on the poll cadence, and they stand.

**W4, read for its cause.** Its point was G7's absolute layer-on resid (−2.2), and resid does not hold still between
runs:

| | layer off: resid (n) | layer on: resid (n) | p95 step, off → on |
|---|---|---|---|
| G7 (`4caa662`: today's opaque wedge over the layer) | −24.7 (39) | −2.2 (31) | +13.4 |
| WD6 (`53ba354`), look 4 | −9.7 (30) | +3.8 (29) | +12.2 |
| WD6, look 3 | | +3.8 (26) | +10.9 |

- **G7's own resid step was +22.5 ms against a p95 step of +13.4,** because n fell from 39 to 31. So the firmware's
  per-contact model does not cancel traffic at this scale. Two layer-off baselines on the same board sit 15 ms apart.
- **The named alternative, that the sweep costs about 4-6 ms more than predicted, is not excluded:**
  - Against it: today's whole off → on step (layer plus sweep, +12.2) is smaller than G7's layer-only step (+13.4).
    Under it, the step would be about +17.
  - But that compares two runs, and the table shows how far apart two runs can sit. So it argues against the
    alternative and does not exclude it.
- **My prediction also had a modelling error, stated now:**
  - **The opaque wedge:** it added the blend's cost on top of G7's layer-on frame, which already included today's opaque
    wedge. The blend path replaces that wedge, so the net cost is the blend minus the opaque fill.
  - **The row scan:** the blend scans every byte of its rows in the PSRAM slice buffer, not only the wedge's pixels.
  - Neither is in the +1.5 / +2.2.

**W5, read for its cause.** Both windows were at the idle cadence and untouched, so the cadence does not separate them.
- **n differed:** the median was 27 (old) and 30 (B′), and p95 rises with n inside each window.
- **At matched n,** in the only two bins both windows share: n = 27, B′ is +0.95 ms; n = 28, +1.5 ms. So traffic
  explains part of the 2.5 ms, not all of it.
- **Two readings, both open:**
  - (a) The branch costs about 1-1.5 ms of frame time with the layer off. The sweep build carries all of #445's weather
    code, not only WD6's early return.
  - (b) The hour drifted. The old window was 00:06-00:16Z and B′ 00:30-00:39Z, and a drift of that size is what the W4
    table shows between runs.
- **The frozen reader also included five stale lines.** They arrived in one burst when the trace opened (00:05:30-31Z,
  USJ output from before the open). Without them the old median is 47.6 and \|Δ\| is 2.4, so the verdict does not move.

**Proposed re-registration.** It is frozen here and committed locally, not pushed, and it waits for the ruling.
- **G7″, the cadence:**
  - Fresh, on the same build. Each window is opened by its own verified POST: B″ with `wx-layer off` (the layer is on
    now) and A″ with `wx-layer on`.
  - Each window is 10 min long, so both sit inside the 600 s active clock.
  - A `[touch]` line in either window voids the run.
  - **Predicted:** 115-125 polls in each window, and a ratio of 0.9-1.1. Outside the set: stop and report.
- **W4′, the sweep's cost, measured directly:**
  - A `micros()` timer goes around the sweep call in `main.cpp`, whichever path runs. The bench env's `[health]` line
    prints it as `sweep=<avg>/<p95>us`.
  - So the cost is read inside each frame, not as the difference of two windows.
  - **Predicted, as averages:**
    - layer off (today's opaque wedge) ≤ 1.0 ms;
    - look 3 1.5-4.0 ms;
    - look 4 2.5-6.0 ms;
    - look 4 − look 3 ≥ 0.5 ms.
  - Outside the set: stop and report.
- **W5′, the layer off against the old image, bracketed:**
  - Old image → sweep build → old image, 10 min each in one hour band. Each window starts ≥ 12 min after its flash, and
    all are untouched (so all idle).
  - **The control:** the two old windows' p95 medians are within 1 ms of each other. If not, the hour drifted, and the
    run reads "instrument", not a grade.
  - **Predicted:** the sweep build's p95 median is within ±1 ms of the mean of the two old windows.

## Rulings, 2026-10-28 (reviewer chat, pasted by Daniel), recorded with their sources

**1. WD6.**
- **The record:**
  - "The stop was right, and the record is accepted as written": W4, W5 and G7′ off; nothing re-run and nothing
    fixed; G7's wrong line corrected in place, with its `/wx` rows standing.
  - The causes are accepted as readings: G7′ followed the idle timer (a config POST re-seeds `lastTouchActivityMs`).
    W4's anchor and W5's two explanations stay open, for the runs below to settle.
- **The re-registrations frozen in `0e5a94e` are accepted, with these changes:**
  - **G7″:** as proposed.
  - **W4′:** as proposed, but the timer sits **under the bench env's flag**, so the shipping image's code is unchanged.
    The predicted bands stand.
  - **W5′:** as proposed, but the middle image is **#445's shipping env** (`blipscope-s3-128` at the branch head, the
    layer off by default), not `wxlook`. "The row asks whether what ships costs anything with weather off, so the
    bench code doesn't belong in it."
- **When:** all three in one session on E2:08, with the predictions pushed first.

**2. Light rain becomes teal, on the Blipscope too.**
- **Daniel's choice:** teal for light rain (code 1, 15-20 dBZ) on both scopes. In the mockup, today's dark blue at
  look A's 40 % was nearly invisible on black.
- **Weatherscope Code** changes `WxPalette.h`: code 1 becomes a ruled exception, teal `(0, 150, 135)`, not taken from
  the pinned table.
- **On the Blipscope:**
  - WxS2 dims code 1 to the look's percent × 55/40 (look A 55 %, look B 34 %, rounded as `Dim` rounds). So light rain
    stays visible but below code 2 in luminance in both looks, with host rows for both.
  - `wx-fb-check.py` reads the teal from the source, like every other colour. The ambiguous and ungradable sets are
    recomputed ("teal's G != B, so no faded cyan plane equals it").
- **How it reaches #445:** by merging main once Weatherscope's palette PR is in, before any capture.

**3. Weather gets its own section.**
- **Daniel's request:** "weather should have its own section in the display section for the blipscope".
- **What changes:** the radar build's Display tab gets a "Weather" section, with a heading, holding every weather
  setting: "Weather radar (beta)" now, and on the bench image "Weather look (bench)" and "Sweep over weather (bench)".
- **What doesn't:** it is a layout change only; no field name, value, default or saved key changes.
- **When:** in #445, before it goes for approval.
- **Predictions first:**
  - every form field's name, value and default is unchanged (a check comparing the field set before and after);
  - a whole-form save round trip (GET, then POST everything back unchanged) leaves every stored setting byte-identical;
  - `cfg_post.py` still finds and changes only its one field;
  - the config-form CI check stays green.
- **Sabotage:** a renamed field in the new section turns the field-set check red.

**4. A legend in the Weather section.**
- **Daniel's question:** "should the color legend be in this section?" Ruled: yes.
- **Where and what:** under the weather switch, a compact legend of what the layer draws today:
  - light rain (the teal, once it lands), moderate, heavy, hail possible and 75+ white;
  - no radar coverage and no data, both drawn as their dithers.
- **The swatches** are drawn as the Blipscope draws them in the look in use, so the page matches the glass.
- **The colours** come from the firmware's own tables at serve time (WxS2.h over WxPalette.h), never typed into the
  HTML. Weatherscope's legend labels are reused if they fit.
- **No snow row** until the layer can draw snow (WXC1 v2 brings the type).
- **Scope:** in #445 with the Weather section, as a layout addition; every form field, value and saved setting
  unchanged, as ruling 3 says.
- **Predictions first:** each swatch's colour equals wxs2's for its code in the current look, by a check that reads the
  served page.
- **Sabotage:** a swatch typed in by hand, off by one level, turns that check red.

**5. The order for E2:08.**
- **Daniel's condition:** he picks between sweep looks 3 and 4 only once light rain is teal on the glass: "I'll pick it
  once the color changes to teal. right now it's hard to see".
- **The order:**
  1. Weatherscope Code's teal palette PR goes next.
  2. Once it's on main: merge main into #445, apply the Blipscope's teal rule, and put the sweep build on E2:08 with
     the "Sweep over weather (bench)" select still there.
  3. Daniel compares 3 and 4 in rain and picks.
  4. Then the captures, on his pick.
- **WD6's re-registered runs (G7″, W4′, W5′)** can go before or after that, as suits the board.
- **Until then,** Daniel may change "Sweep over weather (bench)" on E2:08, and nothing else.

## WD6′, 2026-10-28: G7″, W4′ and W5′ as ruled. Frozen before the timer's code and before the run

**They run now, before the teal.** The board is free and it is night at the Bend point. None of these rows needs rain.

**The images.** All are built from one tree and flashed app-only to E2:08.
- **`wxlook`:** at #445's head with the W4′ timer. It carries G7″ and W4′.
- **The shipping env `blipscope-s3-128`:** at the same head. It is W5′'s middle image.
- **The old image:** main `962ccb7`, the shipping env. It is the branch's merge base with main and W5's baseline.

**The W4′ timer, bench flag only.**
- **Where:** in `main.cpp`, under `#if defined(WX_LOOK_AB)`. The bench branch times the sweep call, whichever path runs,
  with `micros()`, and records the path it took. The shipping branch is the code that is there today, token for token.
- **What it prints:** every 30 s, right after the `[health] frame` line, its own line:
  `[health] sweep=<avg>/<p95>us n=<k> blend=<b>`.
  - The avg and p95 are over the last 128 sweep calls, the same ring and the same p95 index as the frame figure.
  - `blend` counts how many of those calls took the blend path.
- **Predicted, shipping code unchanged:** the shipping env's `firmware.bin` built before and after the timer is
  **byte-identical** (`cmp`). No stamp is injected (no extra script does), and none of the edited files uses
  `__LINE__`. Two other outcomes are named in advance:
  - differences only inside the documented rebuild regions (app ELF sha, image sha, checksum, WiFiManager's two
    timestamps) still mean "unchanged code", reported as such;
  - **anything else: stop.**
- **Predicted, the string:** `sweep=%` is in the `wxlook` ELF (1) and absent from the shipping ELF (0). As a control,
  the existing bench label "Sweep over weather (bench)" reads 1 and 0 the same way.

**Part 1: G7″ and W4′, on `wxlook`, one trace held.**
- **The windows.** Each opens with its own verified `cfg_post.py` POST, which re-arms the active cadence, and each is
  10 min, so all of it sits inside the 600 s active timer. Before the run the board is on look 4 with the layer on.
  - **B″:** POST `wx-layer off`, 10 min. G7″'s off window, and W4′'s opaque path.
  - **A″:** POST `wx-layer on`, 10 min (look 4). G7″'s on window, and W4′'s look 4.
  - **L3″:** POST `wx-sweep 3`, 10 min. W4′'s look 3.
  - **Then** POST `wx-sweep 4`, and POST `wx-layer off`, so W5′'s shipping image reads the layer off from NVS.
- **A `[touch]` line in any window voids the run.**
- **G7″:**
  - polls per window = the sum of the 10 `[perf] polls=` lines whose minute lies inside it;
  - **predicted 115-125 in B″ and in A″**, and A″/B″ within 0.9-1.1.
- **W4′:**
  - the median, per window, of the `sweep=` lines whose 30 s lies wholly inside it;
  - **predicted `blend=0` on every B″ line and `blend=n` on every A″ and L3″ line;**
  - **predicted averages:** B″ (opaque) ≤ 1.0 ms, L3″ 1.5-4.0 ms, A″ (look 4) 2.5-6.0 ms, and A″ − L3″ ≥ 0.5 ms.

**Part 2: W5′, old → new → old, all untouched.**
- **Per image:** flash it (app-only), hold one trace, check the `[build]` banner, and wait ≥ 12 min. Then a 10 min
  window **O1** (old), **N** (`blipscope-s3-128` at the head) or **O2** (old again).
- **Every window at the night cadence.** No window may straddle sunrise; each window's sign is checked first. One read
  of N's config page, made right after its boot and before its window, shows "Weather radar (beta)" unchecked.
- **The control:** \|median p95 O1 − median p95 O2\| ≤ 1 ms. If not, the run reads "instrument", not a grade.
- **Predicted:** \|median p95 N − mean(O1, O2)\| ≤ 1 ms. The resid and n are reported beside it.

**Afterwards:** E2:08 goes back on `wxlook` at the head, with the layer on and look 4, for Daniel. He changes "Sweep
over weather (bench)" and only that.

**Outside the set:** stop and report, without re-running for a better number.

### WD6′ before the run: the timer, and the shipping image (2026-10-28)

| | read | verdict |
|---|---|---|
| **the shipping image** | `blipscope-s3-128`'s `firmware.bin`, built before the timer and after it, both 1,942,000 B, **differ in 65 bytes**: 32 of `app_elf_sha256` @0xB0, 32 of the trailing image SHA-256, and 1 checksum byte. **No timestamp differs, and 0 bytes are unplaced** (`bin_regions.py`). So every loaded byte is identical | **the named second outcome:** unchanged code, reported as such. Not byte-identical, because the ELF's own hash moved. The cause is probably debug info, since the edited files' line numbers changed; I didn't keep the before-ELF, so that is not verified |
| **the string** | `sweep=%lu`: `wxlook` 1, shipping 0. The control, "Sweep over weather (bench)": 1 and 0 | **as predicted** |
| **the builds** | both envs build. `wxlook` is 1,943,328 B | — |

### WD6′, the board (2026-10-28). Part 1 is VOID (touch) and W5′ reads INSTRUMENT. Stopped for a ruling

**The session ran as frozen, 03:37-05:29Z on E2:08, all at night** (sun −28.9° to −45.0°).
- **Images:**
  - Part 1 on `wxlook` at `f9184ef`.
  - W5′ on the old image (`962ccb7`, from its own tree) → the shipping env at `f9184ef` → the old image again.
- **Every check verified:**
  - every flash exited 0;
  - every `[build]` banner read the expected env;
  - every POST verified with only its own field changed.
- **The page told the W5′ images apart:** the old image's config page has no `wx-layer` field, and the new image's
  has it unchecked. The banner cannot tell them apart.
- **Afterwards:** `wxlook` is back, with the layer on and look 4.

**Part 1 is VOID by its frozen rule.** Its trace has `[touch]` lines in two windows; a person was at the glass:
- **B″, 03:44:37Z:** one swipe, at the zoom limit, so nothing changed.
- **A″, 03:52:05-16Z:** five zoom swipes (50 → 5 → 25 mi), then a tap that opened a card for about 2 s, a flip, and a
  close.
- **L3″:** no touch.

The readings below are therefore observations, not grades.

| row | read (void) | predicted |
|---|---|---|
| G7″ B″ | 108 polls in **9** lines | 115-125 |
| G7″ A″ | 107 polls in **9** lines | 115-125 |
| G7″ A″/B″ | 0.991 | 0.9-1.1 |
| W4′ B″ (today's opaque wedge) | avg median **1.98 ms** (p95 2.55); `blend=0` on every line | ≤ 1.0 ms |
| W4′ L3″ (look 3) | **7.82 ms** (p95 8.02); `blend=n` on every line | 1.5-4.0 ms |
| W4′ A″ (look 4) | **7.89 ms** (p95 8.10); `blend=n` on every line | 2.5-6.0 ms |
| W4′ A″ − L3″ | **0.08 ms** | ≥ 0.5 ms |

**G7″, read for its cause:**
- **The cadence:** the counter reads 12 polls a minute in every whole minute of both windows (one 11). So the layer
  does not move the cadence. Both windows were already active because of their POSTs, so the touches could not have
  changed it either; the rule voids the run all the same.
- **And the frozen text was wrong in a way that could not have passed.**
  - The `[perf]` lines land at :09 past the minute, and each POST landed 1 s after one.
  - So a 600 s window holds only **9** whole counter minutes, not the "10" the text assumed.
  - At 12 a minute that gives about 108, which is outside 115-125 whatever the cadence did. My error, before the run.

**W4′, read for its cause. My bands were wrong, not the run:**
- **The named alternative, that the touches inflated the readings, is excluded:**
  - Look 4 reads 7.9-8.2 ms on every line from 03:39 to 03:41Z, before any touch.
  - L3″ had no touch and reads 7.80-7.83 ms.
  - The opaque wedge reads 1.97-2.06 ms on both sides of B″'s swipe and again after the run (04:11-04:15Z).
- **Every line is flat:** the avg and the p95 sit within about 0.3 ms of each other throughout. So this is a steady
  cost, not sporadic preemption.
- **What it settles:**
  - W4's open alternative ("the sweep costs about 4-6 ms more than predicted") is what happened. The blend path costs
    about **5.9 ms per frame more than today's opaque wedge**.
  - Looks 3 and 4 cost the same to within 0.1 ms. The cost is common to both, not in the colour arithmetic I priced
    per pixel: the rasterise, the full-row scan of the PSRAM slice buffer, and the canvas read-modify-write.
  - Today's opaque wedge itself costs 2.0 ms, twice my ≤ 1.0.

**W5′ reads INSTRUMENT, a pre-registered outcome.** There were no touches in any of its three windows.

| | p95 median | resid | n |
|---|---|---|---|
| O1 (old), 04:29-04:39Z | 44.2 | −3.6 | 15 |
| N (shipping, `f9184ef`), 04:53-05:03Z | 42.7 | −2.4 | 12 |
| O2 (old), 05:17-05:27Z | 41.4 | −0.4 | 7 |

- **The control failed:** \|O1 − O2\| = **2.80 ms > 1**. So by the frozen text there is no grade.
- **Why:** the night's traffic fell steadily (n 15 → 12 → 7), and p95 fell with it.
- N sits 0.1 ms from the old windows' mean. That is what a drift linear in time would also give, so it is not evidence
  either way.

## Rulings on WD6′, 2026-10-28 (reviewer chat, pasted by Daniel), recorded with their source

**The record (`d5ea11c`):** "Accepted as written."
- Part 1 is VOID by its rule (touches in B″ and A″), W5′ reads INSTRUMENT, and stopping was right.
- The two errors made before the run stay on record: G7″'s "10 lines" (only 9 whole minutes fit) and W4′'s bands.
- **As observations only:** the cadence held at 12 a minute with the layer on and off, and looks 3 and 4 cost the same
  (7.82 and 7.89 ms). "So Daniel's pick between them is about looks alone."

**Before any re-run:** one table from the records already held. It shows what weather costs per frame, against B1's
frozen bar (p95 frame time, on minus off, ≤ 3 ms), and says whether B1 can pass as built. If it can't, propose a fix
with predictions, or a re-registration of B1, for a ruling. **The bar stands until then.**

**G7, re-registered:** **11-13 polls in every whole minute of each window, and A″/B″ within 0.9-1.1.** The whole
minutes are counted from the `[perf]` stamps, not assumed to be 10.

**W5′:** propose an instrument that traffic drift can't swamp, for a ruling. Either more alternations in a
steady-traffic hour, or one recorded traffic load replayed to every arm; my choice, with predictions.

**Hands off:** the report before the next run gives Daniel the window in Pacific time.

**The order for E2:08 stands:**
1. Teal (#488).
2. Main merged into #445.
3. Daniel's pick, in rain.
4. The captures.

The re-runs go wherever they suit the board.

### The weather's frame cost against B1, from the records (2026-10-28)

**Only untouched windows at the default 100 mi zoom are used.** Two records are set aside, because a touch changed the
zoom inside them:
- **G7's B window:** its own trace has the zoom stepping 100 → 50 → 25 mi at 23:01:45-46Z, and the 23:22:01Z POST
  reset it to 100 mi for A. So G7's +13.4 ms p95 (+12.7 on the average) compares 25 mi with the layer off against
  100 mi with it on.
  - The "0.22 µs per pixel copy" taken from it is therefore **not a clean measurement of the layer**. W4 and this
    ruling both used it.
  - The records cannot size the zoom effect, since `ap=` counts airports loaded, not drawn. So G7 is set aside rather
    than corrected.
- **WD6′ A″:** zoomed to 25 mi by the touches at 03:52Z, and it reads 5.6 ms cheaper than L3″ at nearly the same n,
  although the timer shows the two sweeps cost the same.

| record (image) | off: p95 (n) | on: p95 (n) | on − off | adjusted for n |
|---|---|---|---|---|
| WD6, look 4 (`53ba354`) | 50.0 (30) | 62.2 (29) | **+12.2** | +13.0 |
| WD6, look 3 (`53ba354`) | 50.0 (30) | 60.9 (26) | **+10.9** | +14.1 |
| WD6′ L3″, look 3 (`f9184ef`) | 43.7 (17) | 57.8 (19) | **+14.1** | +12.5 |

The adjustment uses the firmware's own fit, 0.802 ms of p95 per contact. It is shown beside the raw figure, not
instead of it.

**Where the cost sits:**
- **The sweep:** the blend costs **+5.9 ms** over today's opaque wedge. This is W4′'s timer, which measures directly
  (look 3 7.82, look 4 7.89, opaque 1.98, every line flat), and the touches are excluded as its cause.
- **The layer:** pushing it in place of the `fillScreen` costs **about +6.5 to +8 ms**. This is by subtraction from
  the table, **not measured directly**.

**Verdict: B1 cannot pass as built.**
- With weather on, the frame's p95 rises **about 12.5-14 ms**, roughly four times the 3 ms bar.
- Each half fails on its own: the blend (+5.9) and the layer (about +7 by subtraction).
- **B1's own premise was never measured.** It says "the layer is cached and replaces the `fillScreen` black, so
  steady-state frames copy instead of fill". But a copy reads and writes where a fill only writes, and the copy here is
  LovyanGFX's sprite-to-sprite `pushSprite`, whose cost against the fill nobody timed.

The proposal is in the next record.

### G7‴, frozen as ruled (before its run)

- **The board:** `wxlook` on E2:08.
- **The windows:** B‴ opens with a verified POST of `wx-layer off`, A‴ with one of `wx-layer on`. Each is 10 min.
- **Whole minutes:** a `[perf]` line at stamp *t* is a whole minute of a window when *t* − 60 s ≥ the window's start
  and *t* ≤ its end. They are counted from the stamps, and their number per window is reported (9 or 10, depending on
  where the POST lands).
- **Predicted:**
  - **every whole minute of each window reads 11-13 polls;**
  - **A‴/B‴, the windows' totals, within 0.9-1.1.**
- **A `[touch]` line in either window voids the run.**
- **Outside the set:** stop and report.

## WD7, 2026-10-28: light rain is teal on the Blipscope too (ruled). Frozen before its code

**The ruling,** recorded above with its source.
- WxS2 dims code 1 to the look's percent × 55/40 (look A 55 %, look B 34 %, rounded as `Dim` rounds). Light rain stays
  visible but below code 2 in luminance in both looks, with host rows for both.
- `wx-fb-check.py` reads the teal from the source, and its ambiguous and ungradable sets are recomputed.
- #488 is on main (`b9b1a05`), merged into this branch at `6bd9b9a`.
  - It made code 1 a ruled exception, `wxpalette::kRain1 = (0, 150, 135)`, which `wxpalette::Rain(1)` returns.
  - Until this change, WxS2's `EchoAt` still drew code 1 from `kRainTable[0]`, the old blue.

**The design:**
- **`include/WxS2.h`:**
  - `EchoAt(code, pct)` dims `wxpalette::Rain(code)`, so the base colour is read from the palette, teal included, and
    never copied.
  - A new `CodePercent(code, pct)` gives code 1 `(pct × 55 + 20) / 40` (half up, as `Dim` rounds) and every other code
    `pct`.
  - `Echo(code, look)` is `EchoAt(code, CodePercent(code, Percent(look)))`.
- **`include/WxSweep.h`:** look 4 raises each echo code from **its own** percent toward 100 % at the beam
  (`LitPercent(CodePercent(code, pct), lvl)`). So the tail is still exactly the layer's colour, and the beam is the
  code at 100 % (the teal itself for code 1).

| | check | predicted (computed from the tree's sources before the code) |
|---|---|---|
| **TL1** | host, `test_wx_s2.cpp` WD4b re-registered: code 1 = `Dim(kRain1, CodePercent)`, plus the hand anchors | **look A (0, 83, 74)** at 55 %, **look B (0, 51, 46)** at 34 %. Codes 2-13 are unchanged (table and white × the look's percent), and none is the background |
| **TL2** | host, a new row **WD4e**: light rain below code 2, by CIELAB L* (the measure of `test_wx_palette`'s C3l) | **A: 31.1 < 36.9; B: 18.2 < 22.5.** The old blue's L* is printed beside it, not graded: 8.0 and 3.1 |
| **TL3** | host, `test_wx_sweep.cpp` W2 re-registered | look 4's expected percent starts from `CodePercent`. Level 0 is still exactly the layer's own colour, and level 120 the code at 100 %. W1 and W3 are unchanged; 0 mismatches |
| **TL4** | sabotages, in `test/host/sabotage_wx_s2.py` | **S-t1** (code 1 at the look's plain percent): **WD4b red in both looks**, WD4e green. **S-t2** (code 1 from `kRainTable[0]` again): **WD4b red in both looks**, WD4e green. Every other row stays green |
| **TL5** | `scripts/wx-fb-check.py` reads `kRain1` from `WxPalette.h` and the 55/40 rule from `WxS2.h`, and fails if either is missing | the sets are **unchanged**, because neither the old blue nor the teal is a marker, label or ring colour in either look. **Look A:** 15 weather colours; 18 ambiguous (6 marker colours that are weather colours: `0x0280 0x0320 0x2945 0x4a49 0x6320 0x632c`); **no ungradable ring**. **Look B:** 14; 17 (3: `0x2945 0x4208 0x4a49`); **r=78 `0x0200` ungradable**. The selftest's 18 cases pass |
| **TL6** | the suites, the builds and CI | MinGW: ALL PASS. WSL: only the board block's known gap. Both builds succeed. CI: 0 failures |

**Then, not graded:** the sweep build (`wxlook`) goes on E2:08, with the layer on, look 4, and "Sweep over weather
(bench)" still there, for Daniel's pick of 3 or 4 the next time it rains.

**Outside the set:** stop and report.

### Results for WD7 (local, before CI)

| | read | verdict |
|---|---|---|
| **TL1** | WD4b: **look A, code 1 = teal × 55 % = (0, 83, 74)**; **look B, teal × 34 % = (0, 51, 46)**; codes 2-13 unchanged, none the background, every anchor matches | **as predicted** |
| **TL2** | WD4e: **A: L\* 31.1 < 36.9; B: 18.2 < 22.5.** The old blue was L\* 8.0 and 3.1 | **as predicted** |
| **TL3** | W2 from each code's own percent: 0 mismatches. W1, W3-out and W3-in: 0. S-w still turns only W3-in red | **as predicted** |
| **TL4** | **S-t1 and S-t2 each turn exactly WD4b red, in both looks**, and WD4e stays green. The four existing plants were re-anchored to the new `EchoAt` and keep their red sets. **A rig fault, mine, before any result:** the re-anchored "the dim dropped" plant and S-t1 first failed to compile under `-Werror` (an unused `pct` and an unused `code`). Fixed with `(void)` in the planted text; the plants' meaning is unchanged | **as predicted** |
| **TL5** | `wx-fb-check.py` reads `kRain1` and the 55/40 rule, and asserts both shapes. **Look A:** 15 weather colours, the teal `0x0289` among them; 18 ambiguous (the same 6 marker colours); no ungradable ring. **Look B:** 14, teal `0x0185`; 17 (the same 3); r=78 `0x0200` ungradable. The selftest's 18 cases pass | **as predicted** |
| **TL6 (local half)** | MinGW: **ALL HOST TESTS PASSED**. WSL `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`: the board block's known gap only. Builds: `blipscope-s3-128` 1,941,984 B and `wxlook` 1,943,296 B | **as predicted** |

## Daniel's pick of the sweep look, 2026-10-28 (reviewer chat, pasted by Daniel), recorded with its source

- **Daniel's pick,** after comparing looks 3 and 4 on E2:08 in rain: **"i like 4: lit"**. So look 4 ships, as the
  shipping image already draws it.
- **Next, while it rains (if it holds):** the captures, on the build that is on E2:08 now (`wxlook` at `fb2e787`),
  under the frozen capture protocol. "Anything that protocol doesn't cover for look 4 or teal: stop and ask." Daniel
  gets the hands-off window in Pacific time before the start.
- **Look 3 retires after the captures, not before.** `SeeThrough565`, its rows and the "Sweep over weather (bench)"
  select are removed, with predictions first: look 4's rows unchanged, and the shipping image's draw path unchanged.
- **The cost table against B1** still comes before any re-run. With look 4 chosen, any fix to the blend path targets
  look 4's path. If a fix changes what the layer draws, the capture rows it touches are re-run on the fixed code.

### Stopped before the captures: the frozen protocol does not cover the sweep wedge (2026-10-28, a question for a ruling)

**What the protocol was frozen for.** The WD5 image (`da5e7f7`), whose sweep was today's opaque wedge.
`scripts/wx-fb-check.py` has no model of the wedge at all: no mention of the sweep, the wedge or the beam.

**What look 4 puts in the wedge** (about 7,283 px, a 57° sector, about 16 % of the disc):
- echo pixels at their colour raised toward 100 %;
- dry pixels in the wedge green, `(0, lvl, 0)`.

**None of those colours is in the checker's weather set,** with three consequences:
- **WD5e(1), inside the wedge:** a ring pixel left undrawn shows a lit colour, which the checker does not count as
  weather. So it cannot be an exception: blind, in the reassuring direction.
- **WD5e(2), inside the wedge:** a marker missing its outline is bordered by lit colours, which are not weather. So it
  cannot fail: the same.
- **Also:** a lit colour or a wedge green that equals a faded plane colour can be read as a marker.

**The obvious fix is computed, and it fails.** Adding everything the wedge can draw (each echo code at every wedge
level, and the 121 wedge greens) to the weather set:
- **all three rings become ungradable.** r=78 `0x0200` and r=39 `0x0100` equal wedge greens, and r=119 `0x0640`
  equals a lit colour;
- the ambiguous marker colours rise from **18 to 125 of 238** in look A, and from **17 to 139** in look B.

So it would blind WD5e(1) completely, and most of WD5e(2).

**The same gap existed under the opaque wedge** the protocol was frozen with. Its greens were not weather colours
either. It was not analysed then.

**The WD5f p95 clause** (the layer off against the old image, `|Δ| ≤ 1 ms`) is the same question as W5′/W5″, which
reads INSTRUMENT and whose new instrument awaits a ruling.

**Nothing has been captured.** E2:08 is unchanged: `wxlook` at `fb2e787`, the layer on, look 4.

## Rulings on the wedge, 2026-10-28 (reviewer chat, pasted by Daniel), and the mask, frozen before its code

**The rulings:**
1. **The wedge: option 1.** Mask look 4's wedge in the checker, from each frame itself.
   - The beam is located by its head colour (170, 255, 170) and its line (80, 255, 80). There must be exactly one head
     blob at the disc's edge, and the beam line must agree on the angle; anything else makes the frame UNTRUSTWORTHY
     for this purpose, never a pass.
   - The 1.0 rad tail behind the beam is masked, plus a stated margin, and the masked share is printed per frame and
     predicted before the run.
   - Rings and outlines are graded, as frozen, on the rest of the disc. **The wedge's interior is covered by W3 on the
     host, not by the captures** (recorded here as ruled).
   - Predictions and sabotages come before any capture. A wedge-coloured pixel planted as a missing ring pixel outside
     the mask must turn the row red, and a frame with its head erased must read UNTRUSTWORTHY.
   - **The earlier gap, recorded as found:** the protocol never modelled the opaque wedge either. It was frozen on 10-26
     for an image whose opaque green wedge covered about 16 % of the disc, and nothing in the checker knew it was there.
2. **WD5f's p95 clause is dropped from this session.** It is W5's question, and it waits for W5's new instrument.
   WD5f's R5 marker clause stays.
3. **After the captures,** E2:08 stays on `wxlook` until look 3's retirement build, which fetches production `/blips`.
4. **Rain:** capture only when the rain is close enough for R3 to grade. Rain 50-100 km out risks a BLIND run, so wait
   for a better day rather than spend the window. Daniel gets the hands-off window before any run.

**The mask, exactly** (in `scripts/wx-fb-check.py`, for the layer-on arms, `--look A` and `--look B`):
1. **The head:** the 8-connected components of pixels equal to `s565(170, 255, 170)`. There must be exactly one, with
   its centroid 117-123 px from the centre (119, 119), the disc's edge. θ is the centroid's angle, in screen
   coordinates as the device draws it (`xLead = cx + cos θ · r`, `yLead = cy + sin θ · r`).
2. **The beam:** the ray at θ is sampled at radii 10, 11, …, 110 (101 samples, each rounded to the nearest pixel). A
   sample hits if that pixel or one of its 8 neighbours equals `s565(80, 255, 80)`. **At least 70 % must hit.**
   Planes and labels drawn after the beam can hide some, and a beam at another angle hits almost none.
3. **Otherwise the frame is UNTRUSTWORTHY** (with `--drop-untrustworthy`, dropped and counted), never graded.
4. **The masked region:** every disc pixel within 12 px of the centre (angles are meaningless there), and every pixel
   whose angle lies in **[θ − 1.0 − 0.06, θ + 0.06]**. The margin is **0.06 rad each side**, which covers the triangles'
   truncated vertices and the head's 2 px radius at the inner ring's 39 px.
5. **Inside the mask:**
   - ring pixels are not graded (neither exercised nor exceptions);
   - a marker is not graded if any of its pixels, or any pixel within 3 px of it, is in the mask. It is counted as
     **masked**.
6. **Outside the mask: the frozen rules, plus one.** A ring pixel showing a **wedge-only colour** is an exception: a
   wedge colour outside the wedge.
   - **Wedge-only colours:** look 4's lit colours (each echo code from its own percent, at every wedge level 1-120)
     plus the wedge greens `(0, lvl, 0)`, lvl 1-120.
   - **Minus:** the look's weather colours, the marker, label and ring colours, the beam and head colours, and black.
   - They are read from the source: `WxSweep.h`'s `kPeak` and `LitPercent`, and `WxS2.h`'s rule, with their shapes
     asserted.
7. **Printed per frame:** θ, the head's distance from the centre, the beam hits, the masked share, and the masked
   markers.
8. **The `--off` arm is unchanged, with no mask.** With the layer off the wedge is today's opaque green, and R5 counts
   black ring neighbours. A wrongly drawn black outline shows against the wedge's green as it does against black, so
   the wedge hides nothing from R5 there.

| | check | predicted |
|---|---|---|
| **M1** | the masked share, for every beam angle, from the mask's own definition (computed over 360 angles before any code) | **18.58-18.69 %** of the 45,223 disc pixels. **Per capture frame: 18.4-18.9 %** |
| **M2** | selftest: a synthetic look-A frame with the look-4 wedge, its beam and its head drawn as the device draws them | the mask is found; the share is within M1's band; (1) and (2) PASS, exercised outside the mask |
| **M3** | **sabotage S-m1** (ruled): one ring pixel outside the mask set to a wedge-only colour | **(1) FAIL** |
| **M4** | **sabotage S-m2** (ruled): the same frame with its head erased | **UNTRUSTWORTHY** |
| **M5** | the same frame with two heads; with its head at 80 px; with its beam line drawn 0.3 rad from its head | **UNTRUSTWORTHY**, each |
| **M6** | the control: a wedge-coloured missing ring pixel **inside** the mask | not graded: (1) PASS, with the ring pixels there counted as masked |
| **M7** | the 18 existing selftest cases, their layer-on frames now drawn with a beam and head placed away from each case's subject | **every verdict unchanged** |
| **M8** | `wx-fb-check.py --selftest` as a whole, and CI | SELFTEST PASSED, CI 0 failures |

**Outside the set:** stop and report.

### Results for the wedge mask (local, before any capture)

| | read | verdict |
|---|---|---|
| **M1** | computed before the code over 360 beam angles: 18.58-18.69 % of 45,223 disc pixels | (the band for the captures: 18.4-18.9 %) |
| **M2** | the synthetic look-A frame with look 4's wedge, beam and head: the beam at +3.142 rad, the head 118.0 px out, beam hits 101/101, **masked share 18.62 %**; (1) and (2) PASS | **as predicted** |
| **M3** | S-m1, a wedge-only colour on a ring pixel outside the mask: **(1) FAIL**, "ring exceptions 1 (a wedge colour outside the wedge 1)" | **as predicted** |
| **M4** | S-m2, the head erased: **UNTRUSTWORTHY**, "the beam's head: 0 blobs, not exactly one" | **as predicted** |
| **M5** | two heads: UNTRUSTWORTHY ("2 blobs"). The head 80 px out: UNTRUSTWORTHY ("80.3 px from the centre"). The beam 0.3 rad off: UNTRUSTWORTHY ("0/101 samples") | **as predicted** |
| **M6** | the control, the same pixel inside the mask: not graded, (1) PASS, 231 ring pixels masked | **as predicted** |
| **M7** | the 18 existing cases, now with the wedge drawn (the beam west, the subjects east): every verdict unchanged | **as predicted** |
| **M8 (local half)** | WX FB CHECK SELFTEST PASSED: 25 cases | **as predicted** |

- **Read from the source:** the wedge-only sets are 172 colours (look A) and 218 (look B). The head and beam colours
  are not marker colours, so neither can be read as a plane.
- **The captures wait for rain close enough for R3 to grade,** and Daniel gets the hands-off window in Pacific time
  before the start.

**CI, for WD7's TL6 and the mask's M8:**
- **On `29ea144`,** which carries WD7's code and the mask: 36 check runs, **33 success, 3 skipped** (the release-only
  `promote`, `version` and `ship-alert`), **0 failed**. `host-tests` succeeded.
- **Both rows are as predicted.**
- The runs on `fb2e787` and `787627e` were cancelled when later record pushes moved the branch, so they are not
  results.

### Stopped before the captures again (2026-10-28, 19:18Z): the bench page cannot select look B

**The trigger came:** `rain_wait` saw echo within 50 km of Bend at 19:16:37Z (15 cells; 165 within 100 km).

**Before any setting was changed, the start-state read of E2:08's page found the defect.** The "Weather look (bench)"
select is served as:

    <select name="wx-look"><option value="A" selected>A: 40 </option></select>

- **The source** (`src/ConfigurationWebServer.cpp:3224-3226`, from `b56d883`, WD5 part 2) writes
  `>A: 40 % + outline</option><option value="B" … >B: 25 %</option>`.
- **What happens to it:** the page's `%PLACEHOLDER%` template processing takes everything between the two literal `%`
  signs as one unknown placeholder and serves it as nothing. So the rest of option A's label and **all of option B are
  gone**.
- **These are the only two lines in the page source with a literal `%`.** The select is bench-only (`WX_LOOK_AB`), so
  the shipping page is not affected.
- **So since `b56d883` the bench page has never offered look B on the glass.**
- **For the captures:** the protocol's arm 2 (layer on, look B, set "only through the config page") cannot be set.

**Nothing was captured, and no setting changed.** The trace and the session were never started. E2:08 is unchanged:
`wxlook` at `fb2e787`, the layer on, look A, sweep look 4.

## Rulings on the bench look select, 2026-10-28 (reviewer chat, pasted by Daniel), and its fix, frozen before the code

**The rulings:**
1. **Option 1.**
   - Fix the two labels without a literal `%` ("A: 40 percent + outline", "B: 25 percent").
   - Add the config-form CI check: it refuses a literal `%` outside a `%PLACEHOLDER%` in any page template, and its
     sabotage puts one `%` back and must turn red.
   - Predictions first: the served bench page offers A and B with their full labels; the shipping image's loaded bytes
     are unchanged; CI green.
   - Then reflash `wxlook` on E2:08 with the fix. **The captures run on that fixed build**, not on "the build on E2:08
     now", and that ruling is amended accordingly.
2. **The next good rain:**
   - First the captures, all three arms in the protocol's order, under a hands-off window given to Daniel in Pacific
     time.
   - Then, if the rain holds, **Daniel's R2 look at layer looks A and B.** He changes "Weather look (bench)" and
     nothing else. If the rain does not hold, his look waits for the next one.
   - Until he picks, the shipping image keeps drawing look A, as R2 says.
3. **Recorded as found:** the bench select has been broken since `b56d883`, and **R2's comparison has never been
   possible on the glass.**

**What the check reads.** Every `CONFIG_HTML` raw literal, and every string literal inside a template processor (the
`(const String& var)` lambdas, nine of them, which return HTML such as the look select).
- **Measured before the fix:** the nine static pages hold 0 literal `%`. The processors hold exactly two, the broken
  labels at `:3225-3226`. No processor uses a printf format.
- **The rule:** a `%` that is not part of a `%[A-Z0-9_]+%` placeholder is refused, `%%` included.

| | check | predicted |
|---|---|---|
| **L1** | the new check on the source **before** the fix (the control that it sees this bug) | **exactly 2 violations**, the two labels, and a nonzero exit |
| **L2** | the same check after the fix | 0 violations; the config-form job passes |
| **L3** | its selftest. **Sabotage:** one `%` put back into a fixed label. Plus a static page with a CSS `100%`, a processor returning `50 %`, and a `%%`; allowed: a `%NAME%` placeholder | each refused case detected, the placeholder allowed; the selftest passes |
| **L4** | the served bench page on E2:08 after the reflash | `wx-look` offers **A ("A: 40 percent + outline") and B ("B: 25 percent")**, both labels whole |
| **L5** | the shipping image, `blipscope-s3-128`'s `firmware.bin` before and after (the change is under `WX_LOOK_AB`) | **byte-identical, or different only in the documented regions** (app ELF sha, image sha, checksum, WiFiManager's timestamps). Anything else: stop |
| **L6** | the ELFs | `wxlook`: "A: 40 percent + outline" 1 and "A: 40 % + outline" 0. Shipping: both 0. The control, "Weather radar (beta)": 1 in both |
| **L7** | CI | 0 failures |

**Outside the set:** stop and report.

### Results for the look-select fix (local; CI follows)

| | read | verdict |
|---|---|---|
| **L1** | the new rule on the source before the fix: **exactly 2 violations**, the two labels in the processor at `:3184`; exit 1 | **as predicted** |
| **L2** | after the fix: "no literal '%' outside a %PLACEHOLDER%", and all 9 pages hold; exit 0 | **as predicted** |
| **L3** | selftest: S-pct (one `%` put back into a fixed label), a CSS `100%`, a processor's `50 %` and a `%%` are each detected; the fixed label and a `%NAME%` placeholder are allowed; self-test PASSED | **as predicted** |
| **L4** | E2:08 after the app-only reflash: `wx-look` offers **("A", "A: 40 percent + outline")** and **("B", "B: 25 percent")**. The layer is still on, and the sweep still look 4 | **as predicted** |
| **L5** | the shipping `firmware.bin`, before and after: **65 bytes differ**, 32 in `app_elf_sha256`, 32 in the image SHA-256 and 1 checksum byte. **0 unplaced**, so every loaded byte is identical | **as predicted** (the named second outcome) |
| **L6** | `wxlook`: "A: 40 percent + outline" 1, "A: 40 % + outline" 0, "B: 25 percent" 1. Shipping: 0, 0, 0. "Weather radar (beta)": 1 in both | **as predicted** |
| **L7** | CI on `cfc3576`: 36 check runs, **33 success, 3 skipped** (the release-only `promote`, `version` and `ship-alert`), **0 failed**. `config-form` (which runs the new rule and its selftest) and `host-tests` succeeded | **as predicted** |

## WD8, 2026-10-29: the Weather section and its legend (ruled 2026-10-28). Frozen before its code

**The rulings are recorded above** (rulings 3 and 4 of 2026-10-28). Since 2026-10-29 the work no longer waits for rain
(Daniel: "let's try to keep moving when possible").

**The design:**
- **The section.** On the radar page's Display tab, the weather switch leaves the checkbox grid. A new fieldset follows
  the Display fieldset, `<legend>Weather</legend>`, holding:
  - "Weather radar (beta)";
  - on the bench image only, "Weather look (bench)" and "Sweep over weather (bench)";
  - under the switch, the legend.

  It is served for `%WXSECTION%` by the template processor, **in cloud builds only**: a build without the feed shows no
  Weather section. **Layout only:** no field name, value, default or saved key changes.
- **The legend.** Weatherscope's labels are reused (`WxLegend.h`), without the snow row:

  | row | codes |
  |---|---|
  | Light rain | 1-4 |
  | Moderate | 5-7 |
  | Heavy | 8-10 |
  | Hail possible | 11-13 (13 is the 75+ white) |
  | No radar coverage | 14 |
  | No data | 15 |

  - **Each swatch** is a 12 × 12 inline SVG.
  - **An echo code's fill** is `rgb(r,g,b)` = `wxs2::Echo(code, look)`, computed at serve time.
  - **14 and 15** are drawn as their dithers: black, with the lit colour (`kNoCoverage`, `kMissingHatch`) on the device's
    pattern (14 where (x + y) is even; 15 where x and y are both even).
  - **The look in use:** A on the shipping image; on the bench image, the stored `wx-look`.
  - **The builder:** a pure C++11 header, `include/WxLegendHtml.h`, so the host can test exactly what is served. Its
    HTML holds no literal `%`, which the config-form check now enforces.

| | check | predicted |
|---|---|---|
| **WD8a** | host, `test_wx_legend_html.cpp`, both looks | **15 swatches** in the six rows above, with exactly those labels and no snow. Every echo swatch's fill equals `Echo(code, look)` (**0 mismatches**). The two dithers carry their lit colour on their own pattern. **No literal `%`** in the HTML |
| **WD8b** | **sabotage S-leg** (ruled): one swatch typed in by hand, one level off (code 5 at 41 % instead of 40 %) | the host row turns **red**, and `check-wx-legend.py` on a served page with that swatch altered turns **red** |
| **WD8c** | `scripts/check-wx-legend.py --url`, reading E2:08's **served** page, in look A and then look B (set through the page) | every swatch equals wxs2's value for its code in that look, computed from the source: **0 mismatches**, each look. Its selftest passes |
| **WD8d** | **the field set** (`scripts/compare-form-fields.py`): E2:08's served bench page before the WD8 build and after it. Each control is compared by name, type, value, checked state, selected option and option values | **identical**. **Sabotage S-ws** (ruled): the after page with one field in the new section renamed (`wx-sweep` → `wx-swep`) turns the comparison **red** |
| **WD8e** | **the round trip** on E2:08: read NVS (`0x9000`, 0x15000), GET the page, POST every field back unchanged, read NVS again | in the `config` namespace, **the same set of keys, and every value byte-identical**. They are compared in memory: no value or hash is printed, and both dumps are deleted at once |
| **WD8f** | `cfg_post.py --select wx-look B`, then `A` | each **verified**, only its one field changed |
| **WD8g** | the builds, the suites and CI | both builds succeed. MinGW: ALL PASS; WSL: only the known board-block gap. The config-form check (single form, vocabulary, no literal `%`) is green. CI: 0 failures |

**On the board,** each run gets a hands-off window, announced to Daniel in Pacific time. Between runs E2:08 is a working
Blipscope, back on `wxlook` with the layer on, look A and sweep look 4.

**Outside the set:** stop and report.

### G7‴, the board (2026-10-29, 03:10-03:30Z): AS PREDICTED

- **The run:** on `wxlook` at `cfc3576` (the look-select fix), one trace held. The window was announced to Daniel as
  8:05-8:40 PM PDT.
  - **B‴:** opened at 03:10:16Z by a verified POST of `wx-layer off`.
  - **A‴:** opened at 03:20:17Z by `wx-layer on`.
  - Each POST was verified with only its own field changed. The layer ended on, as it began.
- **The trace's close** was recovered with `read_mac`, and E2:08's page answered afterwards.

| | read | predicted | verdict |
|---|---|---|---|
| B‴ | **9 whole minutes, each 12 polls** (total 108) | every whole minute 11-13 | **as predicted** |
| A‴ | **9 whole minutes, each 12 polls** (total 108) | every whole minute 11-13 | **as predicted** |
| A‴/B‴ | **1.000** | 0.9-1.1 | **as predicted** |
| touch | no `[touch]` line in either window | a touch voids the run | — |

**So the cadence row is settled:** with the layer on and off, the board polls `/blips` at the same 12 a minute.

### Results for WD8 (local, before the board and CI)

| | read | verdict |
|---|---|---|
| **WD8a** | `test_wx_legend_html.cpp`, both looks: [L1] six rows, Weatherscope's labels without snow; [L2] 13 echo swatches, 0 not wxs2's colour (with the hand anchor: look A code 1 = `rgb(0,83,74)`); [L3] the two dithers, 0 cells off the device's pattern; [L4] no literal `%`, 15 swatches | **as predicted** |
| **WD8b (host)** | S-leg planted into a copy of the header (code 5 typed in as `rgb(105,105,0)`, its colour at 41 %): red rows **[L2] only**. The real header was unchanged (`1b81e94ab42c`) | **as predicted** |
| **WD8b (served-page check)** | `check-wx-legend.py --selftest`: S-leg detected, as are a wrong dither cell, a missing swatch, a snow row, and a legend drawn in A on a page selecting B; looks A and B as drawn are allowed | **as predicted** |
| **cross-check** | `check-wx-legend.py`'s grade run on the **C++ builder's actual output** (`legend_dump`). Its colours come from its own reading of the source, so this tests two independent implementations against each other: look A 15 swatches, 0 off; look B 15, 0 off | (not a frozen row; both agree) |
| **WD8d (selftest)** | `compare-form-fields.py --selftest`: the same fields moved into a new section are allowed; S-ws (a renamed field), a flipped default, a removed option, a changed value and a field moved outside the form are each detected | (the instrument; the board read follows) |
| **WD8g (local half)** | both builds succeed; `check-config-form.py`: all 9 pages hold, no literal `%`. MinGW: **ALL HOST TESTS PASSED**. WSL `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`: only the board block's known gap | **as predicted** |

## Rulings, 2026-10-29 (reviewer chat, pasted by Daniel; written earlier in the day, delivered late), with their source

**1. B1: (c).**
- **B1′, for v19:** with weather on, frame p95 stays under the firmware's own budget gate, the model plus 10 ms. That
  gate is `FRAME_P95_MARGIN_MS` in `AircraftManager.cpp`; a breach prints `[health] BUDGET BROKEN`.
- **The miss stays on record:** the frozen ≤ 3 ms and the table at `10b60c8`. **Its cause:** the premise that a
  full-screen copy costs what the fill it replaces costs was wrong. "+12.7 ms" is corrected as stated (G7's off
  window was zoomed to 25 mi by a touch).
- **Step C is approved,** on `db8f693`'s plan: the bench-only timers on the clear-or-push call and on the sweep's three
  parts, plus the one-off `memcpy`/`memset` timing at boot. It runs at the first free window after WD8, announced to
  Daniel in Pacific time.
- **Then the fix,** with its predictions frozen from step C's numbers, aimed at v19: one `memcpy` for the layer, and the
  blend over each row's span only.
- **When the fix is on the glass,** Daniel compares weather on and off by eye. His look is the last check on smoothness.

**2. W5″: accepted as proposed on `db8f693`** (its text is copied below). It runs when the board is free, after step
C, with its window announced.

**3.** G7‴ passed as predicted and is recorded. WD8 is graded when its board run and CI report. The real-rain captures
stand: whenever echo comes within 50 km, a hands-off window is announced first.

**4. The replay route:** written up with predictions. It must cover:
- **privacy:** the proxy may hold the request's location in memory only, if it needs it to re-centre; it never logs,
  prints or stores coordinates or query strings; it stores nothing per device;
- **production unchanged,** with every route except `/wx/cref` forwarded untouched;
- **the re-stamped crop** exists only on the bench PC, and is labelled as a replay wherever it is recorded;
- **the override:** how `cloud-url` is set and undone; what E2:08 does if the PC sleeps or the proxy stops mid-run
  (Daniel needs it working as a Blipscope); a check at the end that it is back on production;
- **a sabotage:** the same crop with its real (old) stamp must be refused by the age limit, which shows the replay is
  what makes the rain appear.

### Step C, frozen as ruled (`db8f693`'s plan, its meanings unchanged), before its code

**The code, bench flag only (`WX_LOOK_AB`).** Every timer uses W4′'s method: the last 128 calls, avg and p95.
- **The clear-or-push call:** `fillScreen` with the layer off, `DrawWeatherUnder`'s push with it on. Printed every 30 s
  as `[health] clear=<avg>/<p95>us n=<k> push=<p>`, where p counts the pushes.
- **The sweep's three parts** on the blend path: `Rasterise`, `Blend` and the beam. Printed as
  `[health] sweepparts=<r>/<b>/<beam>us`, each an average over the same 128 calls.
- **Once at boot:** 115,200 B copied PSRAM to PSRAM (`memcpy`) and filled (`memset`), each the median of 10, printed
  as `[bench] memcpy=<us> memset=<us>`.

**The run, on E2:08.** Two 10 min windows, each opened by a verified POST: layer off, then layer on with look 4. It
ends with the layer on. A `[touch]` line voids the run.

**What each result will mean (`db8f693`, as frozen):**
- **The instrument check:** rasterise + blend + beam lies within 0.3 ms of W4′'s whole-sweep timer (the `sweep=` line,
  still printed). If not, the split is broken: stop.
- **push − fill** (the layer-on window's clear average minus the layer-off window's): 5-9.5 ms. Outside it, the
  subtraction was wrong: stop and report.
- **`memcpy` − `memset`:**
  - ≤ 1.0 ms: a straight copy can make the layer nearly free;
  - 1-3 ms: it helps, and the sweep has to be cut too;
  - > 3 ms: no full-screen copy per frame gets near the old 3 ms. That bar is retired; B1′ is the bar now.
- **The blend's share of the sweep, and bytes scanned per wedge pixel:** these show whether span-only blending attacks
  the real cost.
- **B1′, an observation here:** the `BUDGET BROKEN` lines in the layer-on window are reported. They are graded on the
  fix's run.

**Then** the fix's predictions are frozen from these numbers, before its code.

### W5″, accepted as proposed (the text of `db8f693`, verbatim)

**W5″, the instrument: alternations, graded at matched contact count.**
- **Why n:** the firmware's own fit has frame p95 rising 0.802 ms per contact (R² 0.985, 5,392 samples), and n is
  what drifted under W5 and W5′. Matching on it removes the drift, and alternating spreads what is left across the
  hour.
- **The arms:** O1, N1, O2, N2, O3, by day, with the sun up throughout.
  - O is main `962ccb7`'s shipping env; N is #445's shipping env at its head.
  - Each is flashed app-only, waits ≥ 12 min untouched, then runs a 10 min window.
  - It takes about 2 h. Daniel gets the window in Pacific time beforehand.
- **The statistic:**
  - pool each image's `[health]` lines by n;
  - keep the n bins with ≥ 3 lines from each image;
  - take the median over those bins of (median p95 N − median p95 O).
- **The control:** the same statistic for (O1 + O3) against O2, **within ±1 ms**.
- **The floor:** ≥ 4 shared bins for both the grade and the control. Fewer, or a failed control, reads
  **"instrument"**.
- **Predicted:** the statistic within **±1 ms**. Per-arm medians, resid and n are printed beside it.
- **The fallback** if it reads "instrument": one recorded traffic load replayed to every arm in local mode. It is
  stronger, but it changes the source path and needs new tooling, so it is not first.

(The line above was cut off in this copy until `3f7a026`; it is restored from `db8f693`.)

## The replay route, for a ruling (2026-10-29): captures from a recorded rain crop, so rain stops gating v19

**What it is.** E2:08's existing runtime override, the config page's `cloud-url` field (blank means the built-in
production URL; read at `AircraftManager.cpp:1298`), is pointed at a small proxy on the bench PC for one session.
- **`/api/v1/blipscope/wx/cref`:** the proxy answers with a **recorded real rain crop**, re-centred on the request and
  re-stamped as fresh.
- **Every other route:** forwarded **untouched** to `https://scopes.valarsystems.com`, so the radar keeps working.
- **Production is not changed or deployed.**

**The crop** (changed as ruled on 2026-10-29, change 1: it is **not** recorded from production).
- **Why not production:** `/wx/cref` sits behind `authenticate()` (`proxy/src/index.ts`). A recording would need a
  device key, and no request may carry E2:08's key except E2:08's own, forwarded unchanged.
- **Made from the public archive instead, the way #496 made the Bend crops.** The tool is
  `tools/wx-replay/make_replay_crop.mts` (committed; it runs under Node's own type stripping).
  - **The encoder:** it loads main's own decode and crop path from the checkout, never copied or changed:
    `proxy/src/wx/mrms.ts` (`decodeMrms`, `codeOf`) and `proxy/src/wx/crop.ts` (`quantise`, `cropCellBox`,
    `encodeBox`).
  - **The commit check:** it refuses unless `proxy/src/wx/` equals origin/main's and the commit that last touched it is
    on origin/main. Today that commit is `d1ced73`, the encoder #496 used.
- **The input:** one of #496's 15 SeamlessHSR frames (2024-06-02, 19:10-20:06Z).
  - It is fetched from the public NOAA MRMS archive (`noaa-mrms-pds`) by its key in #496's committed
    `test/fixtures/wx/bend-loop/MANIFEST.json`.
  - It is refused unless it hashes to that manifest's `gribSha256`.
- **The point:** Bend, the site feed's public point at 2 dp, read from the header of the committed
  `test/fixtures/wx/basemap/bend.bmpk`, as #496 did.
  - It is at Bend's latitude, so the re-centring stretch stays small.
  - It is **named only.** The tool prints no coordinate, and the sidecar holds none. The point exists only inside the
    crop's header, as every WXC1 crop stores its north-west cell.
- **The range:** `r` = 161 km, E2:08's request at 100 mi.
- **Which frame, a rule fixed before any frame is decoded:** of the 15, the one with the most echo cells (codes 1-13)
  within 50 km of the point, ties going to the earliest. If even that frame has no echo within 50 km, stop: a crop
  with no rain near its centre cannot give R3 weather on the rings.
- **Re-centring and re-stamping:** WXC1 has no checksum (`include/WxCrop.h`: frame time at byte 8, the north-west cell's
  centre at 12/16). So both rewrite 12 header bytes, and the rain body is not touched.
  - **Re-centring** moves the north-west centre so the crop's own centre (from its header's rows, columns and cell
    size) sits on the request's quantised point. Its cells keep their microdegree size, so the shape stretches
    east-west by the cosine ratio of the two latitudes; that is stated.
  - **Re-stamping** sets the frame time to the current minute.
- **Labelled as a replay wherever it exists, and it exists only on the bench PC:**
  - the files are `REPLAY-<utc>-cref.wxc1`, with a sidecar `REPLAY-<utc>.json`. The sidecar holds:
    - `"replay": true`;
    - the source (the public NOAA MRMS archive, SeamlessHSR, the frame's key) and its date;
    - the encoder commit;
    - the GRIB's sha256 and the crop's sha256;
    - the echo counts;
    - the point's name;
  - the proxy's ETag reads `"replay-<epoch>"`;
  - every record of the captures says REPLAY;
  - the tool is committed; nothing it makes is committed or uploaded.

**The proxy** (`tools/wx-replay/proxy.py`, committed, Python standard library only).
- **Privacy:**
  - It holds a request's `lat`/`lon` **in memory, for that one request**, only to re-centre.
  - It **never logs, prints or stores** coordinates, query strings, headers or bodies; its log line is the route, the
    status and the byte count.
  - It **stores nothing per device.**
  - **Forwarded requests carry the device's own headers through unchanged,** including its credential. The proxy
    passes them on and never writes them anywhere.
- **Stated plainly:** the device-to-proxy leg is **plain HTTP on the bench LAN**. While the override is set, E2:08's
  credential header crosses the LAN in the clear, because the proxy cannot offer TLS that the device would trust. It is
  E2:08's bench credential, for a session of about 45 min.
- **Daniel's decision** (reviewer chat, 2026-10-29): **"Allow at home"**, with five safeguards. Each is a row below
  (RS1-RS5):
  1. the proxy runs only inside an announced hands-off window, and is stopped at its end;
  2. it answers only E2:08 on the bench LAN and refuses everything else, and E2:08's LAN IP stays redacted in docs and
     output;
  3. it never logs, prints or stores `X-Blip-Key`, any other header, query strings or coordinates, and its own log is a
     fixed set of fields;
  4. it forwards to production over HTTPS with the request unchanged apart from the base URL;
  5. after each run the `cloud-url` override is cleared, and a check shows E2:08 back on the production HTTPS base, with
     no key in any output.

  **If the key is ever printed or written anywhere: stop and report. Revoking it is then Daniel's decision.**
- **The proxy's log, its only fields:** the time (UTC, to the second); the route, one of a fixed list (`wx/cref`,
  `blips`, `enrich`, `config`, `airports`, `photo`, `other`); `replay` or `forwarded`; the status code; the response's
  byte count. Nothing else: no address, no header, no query, no body.
- **Who it answers:** only E2:08's bench-LAN address, read from the local file the bench tools already use and never
  printed. Any other client gets a refusal (403) and an `other`/`refused` log line, with no address in it. It listens on
  the bench PC's LAN address only.
- **What changes on the way through:** forwarding replaces the base URL alone, and so the `Host` that names it.
  Method, path, query, headers and body pass to `https://scopes.valarsystems.com` byte for byte, and the response
  returns unchanged.
- **The forward adds no header** (changed as ruled, change 4).
  - **None added:** no `X-Forwarded-For`, `Via` or `Forwarded`. It sends through `http.client` with `skip_host` and
    `skip_accept_encoding`, so Python adds no header of its own either.
  - **Only hop-by-hop ones dropped:** `Connection`, `Keep-Alive`, `Proxy-Authenticate`, `Proxy-Authorization`, `TE`,
    `Trailer`, `Transfer-Encoding`, `Upgrade`, and any header that `Connection` names.
  - **The response, the same way:** production's status, headers and body go back to E2:08, re-framed for its own hop
    only, with no `Server` or `Date` of the proxy's.
  - **A request with a chunked body** is not forwarded (501), because forwarding it would need a framing header the
    device did not send. None is expected: the device's HTTPClient sends `Content-Length`.
  - **RS4's server-side read covers it.** In addition, the proxy's selftest forwards a request carrying a fake key to a
    local echo server. The header names that arrive must equal those sent, less the hop-by-hop ones; it prints the
    names only.
- **The PC stays awake:** while the proxy runs it asks Windows not to sleep (`SetThreadExecutionState`, released when
  it exits). No setting is changed.
- **The firewall, read and not changed (2026-10-29):** the bench PC's network is Private, and an existing inbound
  Allow rule for its Python 3.14 (`python` on the PATH) covers Private. So the proxy runs under that interpreter and
  needs no new rule. Which client it answers is the proxy's own allow-list (RS2), not the firewall's.

**Setting and undoing the override** (through the config page, as `cfg_post.py` does, one field verified):
1. Set `cloud-url` = `http://<bench PC's LAN address>:<port>`. The save runs `Initialise()`, which reads the new base.
2. Run the session.
3. Set `cloud-url` back to blank. This is in the driver's `finally`, so an error or a stop still restores production.

**If the PC sleeps or the proxy stops mid-run,** E2:08 cannot reach any cloud route.
- It keeps its last aircraft and shows `DATA STALE` until the override is undone. **For that time it is not a working
  Blipscope.**
- The guards are the keep-awake request, the driver's `finally`, and a session of about 45 min.
- **Daniel's manual undo,** if the PC is gone: E2:08's config page, clear "Cloud URL", Save. Rebooting does not undo it,
  because the override lives in NVS.
- **Told at the start of each replay window** (as ruled, change 5). The announcement to Daniel, in Pacific time,
  carries this sentence: *"If the PC sleeps or the proxy stops, E2:08 shows DATA STALE until Cloud URL is cleared on
  its page."*
- (A bench-only firmware fallback to the default URL after repeated failures would make it automatic. That is a
  firmware change, and it is not proposed here.)

**The end check, all three:**
- E2:08's page shows `cloud-url` blank;
- the proxy's port is closed;
- production records E2:08's `/blips` again within 5 min. This is read server-side as `e208_routes.py` does, with a
  control window that is known to hold its traffic.

**The sabotage, S-old (ruled).** First, the same crop with its **real, old** frame time.
- **S-old's crop is RP2's crop with only the frame time different** (as ruled, change 2). Both are re-centred the same
  way, so the two differ in bytes 8-11 alone.
- **Its age:** its real stamp is the archive frame's own time, about two years past the 600 s limit. Its age at the run
  (the board's clock minus the frame time) is recorded in seconds and in days.
- **The order matters,** because a held crop is replaced only by a newer one (`OnQuery`/`OnResponse`: never
  backwards). With production's current crop held, an old crop would be kept out by that rule, not by the age limit.
- **So S-old runs right after a reboot**, with the override set, when nothing is held.

| | check | predicted |
|---|---|---|
| **RC1** | the crop's making (change 1) | the tool refuses unless `proxy/src/wx/` equals origin/main's and its commit is on origin/main; the GRIB hashes to #496's committed `gribSha256`; the chosen frame has echo within 50 km. A scan of the tool's output and the sidecar finds no coordinate-shaped number. The device's own parser (`wxcrop::Validate`, compiled on the host) accepts the crop, the re-stamped crop and S-old's. Each differs from the source only in bytes 8-19 (S-old's only in 12-19), and S-old's differs from the re-stamped one only in bytes 8-11 |
| **RP1** | S-old: the crop with its real (old) stamp, the first crop after the reboot | the `[wx]` line accepts it as the held crop (an outcome other than `Kept`, with `frame=` its old time), and then **the age limit refuses to show it**. **No `layer rebuilt` line, and 0 weather-colour pixels** in the `/diag/fb` frames over 2 min. **Its age is recorded** (change 2). This was read against the code before the run: with nothing held, `OnResponse` replaces (`WxPolicy.h:127` guards only a held crop), `WhatToShow` returns `Nothing` past 600 s (`:141`), and the rebuild runs only on `Show::Crop` (`AircraftManager.cpp:3348`) |
| **RP2** | then the re-stamped crop | a newer frame replaces it, **`layer rebuilt` appears**, and the `/diag/fb` frames hold weather colours. So the replay is what makes the rain appear |
| **RP3** | every other route during the run | forwarded untouched. The radar keeps updating (`[perf] polls=` 11-13 a minute in the active cadence), and production records E2:08's `/blips` throughout |
| **RP4** | privacy | the proxy's log holds routes, statuses and byte counts only. A scan of the log and of the bench files after the run finds **no** `lat=`, `lon=`, coordinate-shaped number or header value |
| **RP5** | the end check | all three as above |
| **RP6** | the captures | the frozen protocol's three arms on the replay (the mask, R3-R5, Finished-only, a hands-off window), each record labelled REPLAY |
| **RS1** | the proxy's lifetime | it starts after the hands-off window opens and is stopped before the window closes. Its port is closed at the end: a connection attempt is refused |
| **RS2** | only E2:08 | a request from any other bench-LAN client (one test request from the bench PC itself) gets **403** and an `other`/`refused` line. E2:08's address appears in **no** doc, in the proxy's log, in the driver's outputs or in any replay file. **The trace is the exception, and it was before this route existed:** the board prints its own address when it joins Wi-Fi (`src/main.cpp:479`), and the last six traces all hold it. So the trace is scanned for the key and the id (RS3) but is not graded on the address. It stays in the gitignored `bench-logs/` and is never quoted. (`3f7a026`'s wording, "no doc, log or output", was wrong about the trace; this was found reading the code before any run) |
| **RS3** | the fixed log, and the key scan (change 3) | every line of the proxy's log has exactly the listed fields. **The scan:** <br>- **where its key comes from:** only the proxy's own memory, as seen in E2:08's requests: the `X-Blip-Key` value, and E2:08's id from `X-Blip-Device` the same way. Nothing reads a key file. <br>- **when and where it runs:** inside the proxy, after the driver's outputs are closed and before the proxy exits. <br>- **what it covers:** the proxy's log, the driver's log, the harness's copy of the driver's output, the trace and every replay file (crops, sidecars, capture records and frames). <br>- **what it prints:** only booleans: key seen, key found, id found, address found (the address outside the trace, per RS2). It also finds no `lat=`, `lon=` or header name in the log. <br>- **predicted:** key seen **yes**; key found **no**, id found **no**, address found **no**. **If no key was seen, it reads "instrument", not "clean"** |
| **RS4** | the forward (change 4) | production answers the forwarded `/blips` with 200, recorded server-side for E2:08's id, as it is with no proxy. The forward adds no header and drops only hop-by-hop ones: RS4's server-side read covers it. The proxy's selftest (a fake key to a local echo server) shows the header names received equal those sent, less the hop-by-hop ones. 501s for chunked bodies: **0**. **No `smoke-prod.sh` run inside the window** (ruled 2026-10-29: its `/blips` would count as E2:08's here and in RS5) |
| **RS5** | back on production | the page's `cloud-url` is blank (the built-in `https://scopes.valarsystems.com`); the proxy logs no request after the clear; production records E2:08's `/blips` within 5 min. **Then RS3's scan runs, once, over every output of the run**, since a scan inside the proxy has to come after the clear and before the proxy exits. After it, the driver prints only fixed text: the port check and the scan's booleans |
| **RS6** | the announcement (change 5) | each replay window's announcement to Daniel, in Pacific time, carries: *"If the PC sleeps or the proxy stops, E2:08 shows DATA STALE until Cloud URL is cleared on its page."* |

**What the replay can and cannot show:**
- **It can:** the draw path on the glass (rings and outlines over a real rain pattern), on demand.
- **It cannot:** the live fetch from production. That is covered by G7, WD5's `/wx` rows and the server's own checks.
  Every real-rain day remains a stronger check of the same rows.

**Outside the set:** stop, undo the override, and report. **The key found in any output is outside the set by itself:**
stop and report; revoking it is Daniel's decision.

**The replay does not run before the reviewer has reviewed this write-up on GitHub.**

### The replay: approved with five changes (reviewer chat, 2026-10-29, pasted by Daniel), written in above

**The ruling:** the write-up at `3f7a026` is approved, with five changes, written in as rows before the run. No second
review is needed unless a reading falls outside the set. Then it runs where the queue has it, in an announced window.

| change | as ruled | written in as |
|---|---|---|
| 1 | the crop is not recorded from production (`/wx/cref` is behind `authenticate()`, and no request may carry E2:08's key but E2:08's own). It is encoded at `r` = 161 km around a public point near Bend's latitude, on a past rain day, from public MRMS archive data with the production encoder, the way #496 made the Bend crops: the encoder commit checked against origin/main, the inputs sha-checked. The sidecar records the source, the date, the encoder commit and the shas. The tool prints no coordinates; the point is named only | "The crop" above; **RC1** |
| 2 | S-old's crop is RP2's crop with only the frame time different, both re-centred the same way. Its real stamp is long past the 600 s limit: record its age | "The sabotage, S-old" above; **RP1**, **RC1** |
| 3 | RS3's scan gets the key only from the proxy's own memory, as seen in E2:08's requests. It runs inside the proxy after the driver's outputs are closed and before the proxy exits, and prints only booleans. If no key was seen, it reads "instrument", not "clean" | **RS3**, **RS5** |
| 4 | the forward adds no header (no `X-Forwarded-For`, `Via` or `Forwarded`) and drops only hop-by-hop ones. RS4's server-side read covers it | "The forward adds no header" above; **RS4** |
| 5 | at the start of each replay window, Daniel is told in Pacific time that if the PC sleeps or the proxy stops, E2:08 shows DATA STALE until Cloud URL is cleared on its page | "Told at the start of each replay window" above; **RS6** |

**Two choices made in writing these in, stated so they can be overruled:**
- **The point and the day are #496's own:** Bend's public point (`bend.bmpk`'s origin) and its 2024-06-02 storm. Its
  inputs then hash-check against a manifest already on main, so the sha check takes its answer from a record this
  work did not write.
- **RS2 is narrowed for the trace,** as stated in its row: the board's own serial log has always held its address.
- **The crop's product is #496's, not production's** (found 2026-10-29, after the crop was made). #496's frames are
  `SeamlessHSR_00.00`. Production's `/wx/cref` serves `MergedReflectivityQCComposite_00.50` (`proxy/src/wx/products.ts`).
  - **Their code mappings are identical:** `mrms.ts`'s `codeOf` with no missing value is `codeOfComposite`, so the crop
    holds the same code set the device draws.
  - **The rain pattern differs:** SeamlessHSR is a single hybrid scan, the composite the column's maximum.
  - **Why it stays:** #496's record is what makes the input sha-checkable against a manifest this work did not write.
    A composite frame of the same storm would hash-check only against a record this tool wrote at its first download.
  - Either is a real rain pattern for the draw path on the glass, which is all the replay claims (RP6). Overrule to use
    the composite if the product matters.

**Where it runs:** after the B1 fix and W5″, as the captures' route if no real rain has come by then.

**The crop tool's development runs (2026-10-29, 04:38-04:39Z, before RC1 was pushed). They do not grade RC1,** which
is graded in the replay session on the committed tool.
- **The first run's scan fired:** "a coordinate-shaped number: YES".
  - A count that printed no values found 18 tokens of 8-9 digits, **all date-shaped**: the archive files' `yyyymmdd`.
    It found 0 decimal-shaped numbers.
  - The scan was changed to count dates and set them aside. The point's own values are still checked in every form,
    microdegrees included.
- **The second run:**
  - the scan read clean, with 18 dates set aside;
  - the crop was **byte-identical** to the first run's (sha256 `58903e952374`);
  - the rule chose the 20:02Z frame, which has 157 echo cells within 50 km (the 15 frames range from 12 to 157);
  - the crop is 8,786 B, 296 × 405 cells, coarsen 1.
- **The device's parser** (`validate_crop`, g++ 13.3.0 in WSL Ubuntu 24.04) accepts it, and counts 34,359 echo cells.
  That is the encoder's own count of echo in the crop.
- **The cache:** a new, empty folder that the archive downloads went into. Every input hashed to #496's committed
  `gribSha256`.

**The proxy, local (2026-10-29): `tools/wx-replay/proxy.py --selftest` passes all 36 checks.** It runs against a
plain-HTTP stand-in for production on the bench PC itself, with a fake key and the example id. The checks:
- the header names that arrive upstream equal those sent, less the hop-by-hop ones and any field `Connection` names;
  `Host` is renamed; method, path and query are unchanged; a POST body arrives byte for byte;
- the response carries production's names, less the hop-by-hop ones, framed for the device's hop (`Content-Length`
  kept, or chunked re-chunked), with no `Server`, `Date` or `Via` of the proxy's;
- a chunked request body gets 501 and is not forwarded;
- another client gets 403 and an `other`/`refused` line, its headers unread;
- S-old differs from the crop only in bytes 12-19, the fresh crop from S-old only in 8-11, and the re-centred crop's
  centre lies on the request's quantised point within 1 µdeg; the ETag gives a 304; another range gets 503;
- every log line has the fixed fields, with no coordinate, query, header or address;
- the scan finds a planted key and a planted address, does not grade the address in the trace, prints only counts and
  booleans, and reads "instrument" when no key was seen.

**`tools/wx-replay/sabotage_proxy.py`:** each of 8 plants goes into a copy and turns the selftest red. The plants:
- S-xff, an added `X-Forwarded-For`;
- S-host, `Host` not renamed;
- S-conn, the fields `Connection` names kept;
- S-server, `Server`/`Date` added;
- S-logq, a query field logged with the guard removed;
- S-allow, every client answered;
- S-scan, the key never found;
- S-instr, "clean" with no key seen.

The unplanted copy passes, and the real file's sha256 is unchanged. **CI on `8743715`** (these tools): 36 check runs,
33 success, 3 release-only skips, 0 failed.

### How the session reads each row (fixed 2026-10-29, before any replay run)

These say **how** each row above is read. None of them changes a prediction. The session's driver and reader are
bench tools beside the others (`replay_run.py`, `replay_read.py`). The reader was written before any run, and was
smoke-tested on a fabricated session only.

**The order**, all inside one announced window of at least 75 min:
1. RC1;
2. the proxy starts (RS1);
3. the bench PC's own request (RS2);
4. `cloud-url` is set;
5. **the reboot** for RP1. It is `esptool read_mac`, the documented recovery, which resets the board into its app. It
   runs while the port is free, and the trace opens at once and is held for 52 min. After a reboot the first `[wx]`
   line has been seen about 18 s after the trace opened (step C's trace);
6. RP1, RP2, then RP6's three arms;
7. look A and the layer on are restored;
8. `cloud-url` is cleared, then 5 min pass;
9. production's server-side reads;
10. the trace closes, and the board is recovered by `read_mac`;
11. the driver's log closes; then the proxy's scan, `STOP`, and the port check.

**`cloud-url` is set and cleared** by `cfg_post.py --text-file` / `--clear`.
- The value is read from a scratch file, so it is never on a command line or in any output.
- Tried against a local stand-in page: set, cleared, only that field changed, the value never printed.
- The clear also runs in the driver's `finally`.
- **If the driver itself is killed,** the proxy sees its input end and stops, but `cloud-url` stays set. E2:08 then
  shows DATA STALE until it is cleared: by me, or by Daniel on its page (RS6's sentence).

| row | read as |
|---|---|
| **RC1** | the committed tool run afresh into the session folder: exit 0, its own scan clean. The variants are made by the proxy's own `Crop.variant` at a **fixed fake point in the open Atlantic** (never a real request), so no file holds a real position. The device's parser (`validate_crop`, WSL g++ 13.3) runs on the crop and both variants |
| **RP1 / RP2, "weather-colour pixels"** | in each `Finished: true` `/diag/fb` frame, the pixels whose colour is a weather colour of look A **and no colour anything else on the radar draws**. Planes, labels, rings, the wedge's greens, the beam, the head and black are excluded, all read from the source by `wx-fb-check.py`'s own reader. Look A has 15 weather colours, and 8 of them are unshared. On `wx-fb-check`'s synthetic frames this reads 2,781 with weather and 0 with the layer off. RP1: every finished frame 0 (none finished: BLIND). RP2: every finished frame above 0 |
| **RP1's `[wx]` line** | the first after the reboot: an outcome other than `kept`, and `frame=` equal to the crop's own. `layer rebuilt` lines from the reboot to `MODE fresh`: 0 |
| **RP1's age** | the trace line's time minus the crop's frame time, recorded with separators. Bare 8-digit numbers have a microdegree's shape, which RP4 scans for |
| **RP2's `[wx]` line** | the first `replaced` after `MODE fresh`, with `frame=` within 120 s of its line; at least one `layer rebuilt` after it |
| **RP3, "in the active cadence"** | the `[perf] polls=` lines from 60 s to 600 s after the reboot or after any verified config POST. Their median is 11-13; min and max are printed. Production's record: E2:08's `/blips` points over the override's span > 0 |
| **RP4** | RS3's scan as predicted, **and** the session's text records (`proxy.log`, `driver.log`/`.out`/`.err`, `index.json`, the crop tool's output, `end.txt`) hold no `lat=`/`lon=` and no coordinate-shaped number. Dates are set aside, as RC1's tool does |
| **RS5, "no request after the clear"** | no proxy log line later than the clear + 10 s, a margin for a request already in flight when the save lands. Production's `/blips` points in the 5 min after the clear > 0 |
| **RP6** | each arm's `Finished: true` frames through `wx-fb-check.py` (`--look A`, `--look B`, `--off`), each predicted PASS, under the frozen protocol's outside-the-set list: a BLIND, an UNTRUSTWORTHY frame with `Finished: true`, a touch-unavailable line, or a `touch-wd wedges=` count that moves within an arm |

### Results for WD8 on the board (2026-10-29, 03:56-03:59Z; window announced as 8:55-9:20 PM PDT)

The flash was `wxlook` at `c5a4293`, app-only, and it exited 0. The page then showed the Weather section and its
legend.

| | read | verdict |
|---|---|---|
| **WD8d** | `compare-form-fields`: **83 named controls, 0 differences** between E2:08's served page before (`cfc3576`) and after the WD8 build. **S-ws**, `wx-sweep` renamed in the new section of the after page: 2 differences ("only in the first page" / "only in the second page"), **red** | **as predicted** |
| **WD8c** | `check-wx-legend`'s grade on the served page: **look A, 6 rows, 15 swatches, 0 off**. After `wx-look B` was set through the page: **look B, 15 swatches, 0 off** | **as predicted** |
| **WD8f** | `cfg_post --select wx-look B`, then `A`: each **VERIFIED**, "only the asked field changed" | **as predicted** |
| **WD8e** | **the control**, NVS read twice across a plain reboot: `config` 82 keys → 82, none added, removed or changed. **The round trip**, a whole-form POST of all 56 fields exactly as read (200), then NVS read again: **82 → 82, none added, removed or changed**. Key names only were compared in memory; both dumps were deleted at once | **as predicted** |
| **end state** | the layer on, look A, sweep look 4 | — |

The control is not among the frozen rows. It was added so that a boot's own NVS writes could not be mistaken for the
round trip's, and it showed that a boot writes nothing to `config`.

**WD8g, CI:** on `c5a4293`, 36 check runs, **33 success, 3 skipped** (the release-only `promote`, `version` and
`ship-alert`), **0 failed**. `config-form` and `host-tests` (which runs `test_wx_legend_html`) succeeded. **As
predicted. WD8 is complete.**

**CI on `3f7a026`** (the replay's safeguards, docs only): 36 check runs, 33 success, 3 release-only skips, 0 failed.

### Step C, the board (2026-10-29, 04:14-04:41Z; window announced as 9:12-9:45 PM PDT). `push − fill` is OFF-PREDICTION: stopped for a ruling

**The run, as frozen.**
- **The image:** `wxlook` at `f5a8145` (the step C timers; the head was `3f7a026`, docs only since), flashed app-only
  at 04:14:20Z (exit 0).
- **The trace:** one trace held for the whole run (`trace-stepc-2026-10-28-2114.log`). It closed at 04:39:59Z;
  `read_mac` recovered the board, and its page answered at 04:40:39Z.
- **The windows:** after 2 min settling, B (a verified POST of `wx-layer` off) from 04:17:16Z, then A (`wx-layer` on,
  look A, sweep look 4) from 04:27:17Z, each 10 min. The layer ended on, as it began.
- **No `[touch]` line in the run**, so it is not void.
- **The reading:** `stepc_read.py` (sha256 `c351b035f977`), unchanged since it was written before the run, on the
  trace after two reads 5 s apart showed it static (51,483 B both times).

| frozen reading | read | its frozen meaning |
|---|---|---|
| **lines** | 19 lines in each window for each of `clear=`, `sweepparts=` and `sweep=`; push counts 0 in B and 128 of 128 in A; every A `sweep=` line blended all 128; 49 `[bench]` reprints | — |
| **the instrument** | rasterise 2.24 + blend 5.49 + beam 0.11 = **7.84 ms**, against `sweep=` **7.85 ms** | **holds** (within 0.3 ms) |
| **push − fill** | the push 6.82 ms (p95 6.91), the fill 3.01 ms (p95 3.12): **+3.82 ms** | **OUTSIDE 5-9.5: "the subtraction was wrong, stop and report"** |
| **`memcpy` − `memset`** | 5.15 − 3.30 = **1.86 ms** (115,200 B PSRAM to PSRAM, median of 49 reprints of the boot measurement) | **1-3 ms: "it helps, and the sweep has to be cut too"** |
| **the blend's share** | **70 percent** of the sweep (rasterise 29, beam 1) | span-only blending attacks the main part |
| **B1′, observed** | **0** `BUDGET BROKEN` lines in the A window | graded on the fix's run |

**So the stop is the frozen one.**
- The subtraction in "The weather's frame cost against B1" put the layer at about 6.5-8 ms. Measured directly it is
  **3.82 ms**: the subtraction overstated it by about 2.7-4.2 ms.
- **The B1 fix's predictions are not frozen,** and no fix is written, until this is ruled.

**Observations from the same run, NOT among the frozen readings** (computed after the frozen reading, from the same
lines by the same "wholly inside the window" rule, and offered for the ruling only).

| | B (off) | A (on) | A − B |
|---|---|---|---|
| frame p95, median of the `[health]` lines | 43.4 ms | 55.6 ms | **+12.2 ms** |
| frame average | 42.7 ms | 54.7 ms | **+12.0 ms** |
| contacts n, median (range) | 16 (14-18) | 16 (15-17) | 0 |
| `ap=` | 60 | 60 | — |
| `sweep=` (the opaque wedge in B, the blend path in A) | 1.99 ms | 7.85 ms | **+5.86 ms** |
| `clear=` (the fill in B, the push in A) | 3.01 ms | 6.82 ms | **+3.82 ms** |
| **the rest of the frame** (average − clear − sweep) | 37.7 ms | 40.0 ms | **+2.3 ms** |

**What these say:**
- **The cost is the records' cost.** In one run, at matched n and the same `ap=`, weather adds +12.2 ms to frame p95,
  inside the 12.5-14 the records showed (adjusted for n). So the records' total was not drift. It was the split by
  subtraction that was wrong.
- **About 2.3 ms sits outside both timers,** in the rest of the frame.

**What that 2.3 ms can be:**
- **Rebuilds:** excluded. The A window holds 6 `layer rebuilt` lines of 26-28 ms. That is about 0.015 ms per frame,
  not 2.3.
- **Drift between the windows:** excluded as the whole of it, because n and `ap=` match. A few tenths are not excluded.
- **Look A's outline:** the leading candidate, and not yet excluded or confirmed.
  - With the layer shown in look A, every contact is drawn 8 more times in black, 1 px out (`WxOutlineNow()`,
    `AircraftManager.cpp:4001`). At n = 16 that is 128 extra marker draws a frame, and no step C timer covers them.
  - If it is the cause, the remainder grows with n, and it is absent in look B.
- **Contention:** not excluded. The push and the blend stream PSRAM, and that may slow the rest of the frame's drawing.
  No part-timer can see that.

**Proposed for the ruling, frozen once ruled and before any code:**
1. **Accept step C's direct numbers as the split:**
   - the layer +3.82 ms, of which a straight `memcpy` would save about 1.7 (6.82 → about 5.15);
   - the sweep +5.86 ms, 70 percent of it the blend;
   - a remainder of +2.3 ms outside both.
2. **Separate the outline from contention** with one bench timer around the outline loop (`WX_LOOK_AB` only, printed
   every 30 s like the others), run in look A. Its frozen meanings:
   - the outline's average is within 0.5 ms of the remainder: it is the outline;
   - the outline is under 1 ms and the remainder is still about 2.3: it is contention;
   - anything else, or a remainder outside 1.3-3.3 ms on that run: "instrument", stop.
   This can ride the B1 fix's first board run, since the timer does not change the drawing.
3. **Then freeze the fix's predictions:** the push's new cost, the blend's new cost, and the frame p95 on − off at
   matched n. They are derived from these numbers, with the remainder carried as its own term.

**E2:08 now:** `wxlook` at `f5a8145`, the layer on, look A, sweep look 4.

### The rain watcher sent E2:08's key from the bench PC; stopped for a ruling (2026-10-29, 06:36Z)

**The finding.** `rain_probe.py` asks production's `/wx/cref` the way a device does. It uses E2:08's credential pair
(`BLIP_KEY`/`BLIP_DEVICE`, read from the Windows user environment and never printed), over HTTPS, from the bench PC.
- `rain_wait.py` ran it 66 times, every 10 min, from 2026-10-28 19:54Z to 06:35Z. I ran it once more at 06:36Z before
  reading its own description.
- Today's ruling on the replay says **no request may carry E2:08's key except E2:08's own.** On its face that covers
  these probes.
- **`proxy/scripts/smoke-prod.sh`** (the standing production check) uses the same pair. Whether the ruling reaches it
  is for the reviewer; nothing about smoke-prod is changed here.
- The key was never printed or written. Its only destination was production, over HTTPS.

**Stopped:** `rain_probe.py` is not run again until ruled.

**A keyless replacement, for the ruling:** `rain_mrms.mts`, a bench tool.
- It reads production's own product (`MergedReflectivityQCComposite_00.50`) straight from the public NOAA archive,
  decodes the newest frame with main's `mrms.ts`, and counts echo around Bend's public point (`bend.bmpk`'s origin,
  never printed). No credential goes anywhere.
- **Its first reading agrees with the old probe's instrument:** the same 186 × 256 grid and the same 35,364 covered
  cells within 100 km.

**The rain itself:**
- 06:35Z: 4 echo cells within 50 km (the old probe);
- 06:36Z: 0 (the old probe, a newer frame);
- shortly after: 5 within 50 km and 6 within 100 km (the keyless probe, a newer frame again).

It is light and intermittent. **No capture was started.** The queue has the captures after the B1 fix, which waits on
step C's ruling, and a few cells near the centre risks a BLIND.

## Rulings, 2026-10-29 (reviewer chat, pasted by Daniel; read from GitHub ahead of the reports), with their source

**1. Step C: the proposal, as written.**
- The frozen `push − fill` band (5-9.5) is missed, and stays on record with its cause: the subtraction overstated the
  layer by about 2.7-4.2 ms.
- The split is step C's direct numbers:
  - the layer +3.82 ms (a straight `memcpy` saves about 1.7);
  - the sweep +5.86 ms (70 percent of it the blend);
  - a remainder of +2.3 ms outside both timers.
- The outline timer (`WX_LOOK_AB` only, every 30 s) runs in look A, riding the B1 fix's first board run. It carries
  the three frozen meanings, including "instrument" for a remainder outside 1.3-3.3 ms.
- Then the fix's predictions, from these numbers, with the remainder as its own term.
- **If the outline turns out to be the 2.3 ms, say so plainly for Daniel:** it is a cost of look A only, and it bears
  on his A/B choice.

**2. The rain watcher.**
- `rain_probe.py` is retired. `rain_mrms.mts` (keyless; production's own product from the public archive) is the
  watcher from now on, printing counts only.
- The 67 keyed probes are recorded. The key went only to production over HTTPS and was never printed or written, so
  there is nothing to revoke.
- **`smoke-prod.sh`:** the replay's ruling does not reach it. It is the standing production check and stays as is.
  Two follow-ups, not urgent:
  - no smoke-prod run inside a replay window, since its `/blips` would count as E2:08's in RS4 and RS5;
  - `BLIP_KEY`/`BLIP_DEVICE` leave the Windows user environment for the token file, read per shell like the other
    tokens. Daniel moves the values himself; smoke-prod's reader is changed by me, and reviewed.

**3.** The replay's three stated choices are accepted: #496's point and day, RS2 narrowed for the trace, and
SeamlessHSR rather than the composite.

**4.** Once #499 merges, main's wx-ingest carries the UK ingest (R3). **wx-ingest is not deployed from main until R3
is ruled.**

**Written in as a consequence:** RS4 and RS5 gain a condition, **no `smoke-prod.sh` run inside a replay window.** A
smoke-prod run in the window would be E2:08's `/blips` by the server's record, and is outside the set.

## The B1 fix, 2026-10-29: frozen before its code, from step C's numbers (as ruled)

**The code.**
- **The layer, in the shipping path:** `DrawWeatherUnder` copies the layer into the canvas with **one `memcpy` of
  115,200 B**. It does so under the guard `DrawSweepOverWeather` already uses (one band of the whole frame, both
  240 × 240 at 16 bits, both buffers present); otherwise it calls `pushSprite`, as today.
- **The blend, in the shipping path:** `wxsweep::Rasterise` records each row's first and last marked column, and
  `Blend` visits only that span of each marked row.
  - Measured on the host over 3,600 angles at the sweep's own centre and radius (`sweep_geom.cpp`, g++ 13.3.0 in
    WSL): the wedge's rows are each one run. So **the span bytes equal the blended pixels: 7,279 on average** (7,182
    to 7,389), against **27,542 B scanned today** (114.8 rows of 240). The fix skips 20,263 B of zeros per frame and
    nothing else.
  - The old whole-row loop stays, as `BlendRows`: the host test's reference, and the bench's scan measurement.
- **Bench only (`WX_LOOK_AB`):**
  - the **outline timer**: the look A outline loop, summed per frame, printed every 30 s as
    `[health] outline=<avg>/<p95>us n=`;
  - `[health] sweepscan=<bytes>/<px>`: the bytes the blend visited and the pixels it blended, averaged over the same
    calls;
  - `[bench] scan0=<us>`: `BlendRows` over an all-zero 240-row slice (57,600 B), the median of 10 at boot. It gives the
    cost of scanning one byte, **a**, on this board in this run;
  - a one-off check at the first frame with a layer, that the `memcpy` leaves the canvas byte-identical to
    `pushSprite`: `[bench] layer copy: memcpy equals pushSprite: yes|no`.

**Host rows, before the board:**

| | check | predicted |
|---|---|---|
| **W5a** | span-only `Blend` against `BlendRows`, at all 3,600 angles, both looks, over a frame of varied pixels and codes | the same frame and the same cleared slice, **byte for byte**, at every angle |
| **W5b** | the spans' bytes and the blended pixels, over the same angles | equal at every angle; average 7,279 |
| **S-span** | a span that stops one pixel short | W5a **red** |
| **S-noclr** | marks left uncleared | W5a **red** |
| **W1-W4** | the existing sweep rows | unchanged, all pass |
| **F0** | the built images | the bench strings (`outline=`, `sweepscan=`, `scan0=`, `layer copy:`) are **in** the `wxlook` ELF and **absent** from the shipping `blipscope-s3-128` ELF |

**The board run.** The protocol is step C's:
- flash `wxlook` app-only, hold one trace, settle 2 min;
- B, the layer off, 10 min; then A, the layer on (look A, sweep look 4), 10 min;
- a `[touch]` line voids the run;
- graded by a reader written before the run.

| | reading | predicted, and what each outcome means |
|---|---|---|
| **F1** | `[bench] layer copy:` | **`yes`**. `no`: the copy is not the push: stop |
| **F2** | the push, A's `clear=` average | **0.85 to 1.05 × the boot `memcpy`** of the same run. Step C's in-frame fill ran at 0.91 of its boot `memset`; with step C's 5.15 ms that is 4.38-5.41 ms. Outside: stop |
| **F3** | the blend, A's `sweepparts` middle figure | **5.49 − 20,263 × a ± 0.3 ms**, with a = `scan0` / 57,600 from the same run. **My estimate of a is 10-35 ns a byte** (PSRAM through the cache), a cut of 0.2-0.7 ms. So **span-only removes the scan, and the blend's real cost is its per-pixel work, about 0.7 µs a pixel**. Lower by more than 0.3: the scan costs more inside the frame than empty, contention: stop. Higher by more than 0.3: the cut is smaller than the scan alone: stop |
| **F4** | the device's geometry, `sweepscan` | bytes visited = pixels blended = **7,279 ± 2 %**. Outside: the device's wedge is not the host's: "instrument", stop |
| **F5** | the control, the layer-off path unchanged | B's fill **3.01 ± 0.3 ms**, B's opaque sweep **1.99 ± 0.3 ms** (step C's). Outside: "instrument", stop |
| **F6** | the instrument check, carried | rasterise + blend + beam within 0.3 ms of `sweep=` |
| **F7** | the outline, frozen as ruled. The remainder = (frame average − clear − sweep) in A minus the same in B | the outline's average **within 0.5 ms of the remainder: it is the outline**. The outline **under 1 ms, with the remainder still about 2.3: contention**. Anything else, or a remainder **outside 1.3-3.3 ms: "instrument", stop** |
| **F8** | frame p95, A − B at matched n | **12.2 − (6.82 − F2's push) − (5.49 − F3's blend) ± 1.0 ms**, from the run's own parts. Outside: the parts do not explain the frame: stop |
| **F9** | **B1′, graded here** | **0** `[health] BUDGET BROKEN` lines in A |

**After the frozen readings:**
- if F7 reads "the outline", it is said plainly for Daniel: **a cost of look A only, bearing on his A/B choice**;
- Daniel compares weather on and off by eye; his look is the last check on smoothness.

**Stated before the run, so it cannot be read as a surprise:** F3 predicts that span-only blending cuts the blend by
only 0.2-0.7 ms. The scan was never most of it: 20,263 B of zeros, against 7,279 pixels each read, recoloured and
written.
- The lever that would cut the blend is the per-pixel work: for example a colour table per (code, level), built once
  per look. That is **not** built or proposed for this run.
- B1′ already held before the fix: 0 `BUDGET BROKEN` lines in step C's A window.

**Outside the set, for any row:** stop and report, without re-running for a better number.

### Results for the B1 fix, local (2026-10-29, the code at `32592f9`; CI and the board follow)

| | read | verdict |
|---|---|---|
| **W5a** | span-only `Blend` against `BlendRows` at 3,600 angles, both looks: **0 mismatches** in frame, count and cleared slice | **as predicted** |
| **W5b** | span bytes = marked at every angle; **7,278.5 on average** (rounds to 7,279) | **as predicted** |
| **S-span** | red rows `W3-in`, `W5a` | **as predicted** (see below) |
| **S-noclr** | red rows `W3-in`, `W5a` | **as predicted** (see below) |
| **S-w**, re-anchored | red row `W3-in` only | as before |
| **W1-W4** | all pass | **as predicted** |
| **F0** | `sweepscan=`, `scan0=` and `layer copy:` are in `wxlook` and absent from the shipping ELF. **`outline=` is in both**: five times in the shipping ELF | **OFF as written**: see below |

**About the sabotages.**
- W3 runs the device's path, which is now the span `Blend`. So S-span and S-noclr, planted in that loop, redden
  `W3-in` as well as `W5a`. That was stated in `sabotage_wx_sweep.py` before either ran.
- S-w is planted in the shared `BlendOne`. It reddens `W3-in` only, because `W5a`'s two sides change alike.
- The real header's sha256 is unchanged.

**About F0.** The frozen needle `outline=` is too short.
- All five shipping matches are the config page's own JavaScript, `i.style.outline=` (`ConfigurationWebServer.cpp:168-383`).
  This change does not touch that file. The bench line itself, `[health] outline=`, is in `wxlook` only.
- The other three needles read as predicted.
- So the shipping image carries no bench string. But the row as frozen reads off, and **the needle is not changed
  here: that is for the reviewer.** The proposed reading uses the full line prefixes (`[health] outline=`,
  `[health] sweepscan=`, `[bench] scan0=`, `[bench] layer copy:`).
- No board reading depends on F0, so the board run goes ahead.

**The rest of the host suite** (`run.sh`, g++ 13.3.0 in WSL Ubuntu 24.04): every block passes except **"the same TU,
for the board"**. That block needs the xtensa cross-compiler, which WSL does not have, and it fails rather than skip
by design. CI has the compiler. `wx-fb-check.py --selftest` passes against the changed header.

**Both images build:** `blipscope-s3-128` and `blipscope-s3-128-wxlook`.

### The B1 fix, the board (2026-10-29, 14:33-15:00Z; window announced as 7:32-8:05 AM PDT). F7 reads INSTRUMENT: stopped for a ruling

**The run, as frozen.**
- **The image:** `wxlook` at `229e3c6` (the code of `32592f9`), flashed app-only at 14:33Z (exit 0).
- **The trace:** one trace held (`trace-b1fix-2026-10-29-0733.log`). `read_mac` recovered the board after it closed.
- **The windows:** B (layer off) from 14:36:17Z and A (layer on, look A, sweep look 4) from 14:46:18Z, each opened by
  a verified POST. The layer ended on.
- **No `[touch]` line in the run.**
- **The reading:** `b1fix_read.py` (sha256 `30e777acd4ad`), unchanged since it was written before the run, on the trace
  after two reads showed it static.
- **19 lines in every window and kind.**

| | read | verdict |
|---|---|---|
| **F1** | `[bench] layer copy: memcpy equals pushSprite: yes` | **as predicted** |
| **F2** | the push **5.08 ms**, the same run's boot `memcpy` 5.15 ms: ratio 0.985 (0.85-1.05) | **as predicted** |
| **F3** | the blend **4.32 ms**; `scan0` 2,545 µs, so a = 44.2 ns a byte; predicted 5.49 − 20,263 × a = 4.59 ± 0.3 | **as predicted** |
| **F4** | visited 7,278 B, blended 7,278 px (7,279 ± 2 %) | **as predicted** |
| **F5** | B's fill 3.01 ms, B's opaque sweep 2.04 ms (3.01 and 1.99, ± 0.3) | **as predicted** |
| **F6** | 3.23 + 4.32 + 0.11 = 7.66 ms, against `sweep=` 7.68 | **as predicted** |
| **F7** | the remainder **4.79 ms**, the outline **5.49 ms** | **OFF: a remainder outside 1.3-3.3 ms reads "instrument", stop** |
| **F8** | frame p95 A − B +12.50 ms at n 34 / 31, adjusted for n +10.09; predicted +9.28 ± 1.0 | **as predicted** |
| **F9** | **0** `BUDGET BROKEN` lines in A | **as predicted: B1′ holds** |

**So the fix does what it was built to do:**
- the layer costs a `memcpy` (−1.74 ms on the push);
- the span-only blend cuts the blend by 1.17 ms;
- the parts explain the frame;
- B1′ holds.

**My estimate of a was wrong:** 44 ns a byte against my 10-35. So the cut is larger than the 0.2-0.7 ms I stated.
F3's own frozen form, which used the run's measured a, holds.

**Why F7 reads "instrument": the band was mine, and it was not scaled by n.**
- The 1.3-3.3 ms band was set around step C's remainder at **n = 16**. This run's traffic was **n = 29-38** in A
  (median 34).
- The frozen meaning is the stop, and it stands. The run is not repeated for a better number.

**Observations from the same lines, NOT among the frozen readings:**
- **The outline's own timer** reads **157-169 µs per contact per frame on every one of A's 19 lines** (median 162.5).
  It rises with n line by line, 4.71 ms at n = 30 and 6.24 ms at n = 38.
  - At step C's n = 16 that is about 2.6 ms, against step C's unexplained 2.3.
  - The named alternative, contention, would have to scale with the contact count at a steady 0.16 ms each. The data
    do not exclude that, but the outline loop's own direct timer is not an inference: **look A's outline costs about
    0.16 ms per contact per frame, measured.**
- **For Daniel, plainly, as an observation and not a grade:** this is a cost of look A only, and it bears on the A/B
  choice. At this morning's traffic it is about 5.5 ms a frame. Look B draws no outline.
- **Rasterise rose from 2.24 ms (step C) to 3.23 ms**, about +1.0 ms. That is the span bookkeeping: a comparison and a
  store on every marked pixel, inside a lambda whose captures live in memory. It offsets most of the blend's cut: the
  sweep as a whole went from 7.85 to 7.68 ms.
  - F6 and F8 hold with it included, but **no frozen row covered Rasterise's own cost.** That omission was mine.
  - Recording each span at the `Span` call, rather than per pixel, would remove it. That is a change, and it is not
    made here.
- The push (−1.74 ms) is now the larger saving.

**For the ruling:**
1. F7: re-read by a band scaled by n, or ruled on the direct timer as it stands.
2. Rasterise's +1.0 ms: whether to record spans per `Span` call, with its own predictions.
3. Daniel's on/off look: it can happen now. The fix is on the glass, and nothing graded depends on it.

**Not stopped by this:** W5″. It grades the layer-off path, which F5 shows unchanged. It runs next, by day, with its
window announced.

**E2:08 now:** `wxlook` at `229e3c6`, the layer on, look A, sweep look 4.

## Rulings on the fix's board run, 2026-10-29 (reviewer chat, pasted by Daniel), with their source

1. **F7 reads "instrument".** Its band was set at step C's 16 contacts, and the run had 34. The miss stays on record as
   the band's error, and the run is not repeated. W5″ is the instrument for on − off at matched n.
2. **The span tracking's +1 ms in rasterise: the change is approved.** Spans are recorded once per span call instead
   of per pixel.
   - **Predictions first:** rasterise loses the time the per-pixel recording added; the blend keeps its saving; the copy
     check's picture stays identical.
   - It rides the next board run, aimed at v19.
3. **Look A's outline**, 0.16 ms per contact per frame (about 5.5 ms at 34 contacts), is recorded as **a cost of look A
   only**. It goes to Daniel with the A/B choice.
4. **Daniel's two looks go in the replay session,** after RP6's arms, with the replay's rain on the glass:
   - first weather on against off, for smoothness (the fix's last check);
   - then look A against look B. This is R2's look: he changes "Weather look (bench)" only, and it goes back to A until
     he picks.

   Both go in the window's announcement, in Pacific time.
5. **W5″ and the replay run as queued.**

## The span change (ruled 2026-10-29): frozen before its code

**The code.** `detail::Span` already clips each span to the screen. After calling `put` for every pixel, in the same
order as now, it calls `put.Spanned(x0, x1, y)` once with the clipped range.
- `Rasterise`'s marker becomes a small struct. Its per-pixel call does what step C's did (mark, count, rows), and its
  `Spanned` records the row's first and last column.
- Every marked pixel passes through `Span`, the degenerate-line case included, so nothing is recorded anywhere else.
- `Blend`, `BlendRows` and the shipping guard are unchanged.

| | check | predicted |
|---|---|---|
| **S1** | the host rows W1-W5b | all pass. The marks are unchanged pixel for pixel, the span blend equals `BlendRows` at all 3,600 angles, and span bytes = marked |
| **S-spanrec** | a sabotage: `Spanned` records one column short | red rows exactly **`W3-in`, `W5a`, `W5b`**. S-w, S-span and S-noclr as before |
| **S2** | the board: rasterise, `sweepparts`' first figure | **2.24 ± 0.3 ms**: step C's, before any span recording (the fix run read 3.23) |
| **S3** | the board: the blend, `sweepparts`' middle figure | **4.32 ± 0.3 ms**, the fix run's. **Graded only on a dry layer**: the keyless probe must read 0 echo cells within 100 km at the arm's start and its end. Otherwise "not graded: rain on the layer", because the blend's per-pixel work depends on the codes under the wedge |
| **S4** | the board: the copy check, and the geometry | `[bench] layer copy: memcpy equals pushSprite: yes`; `sweepscan` bytes = pixels = 7,279 ± 2 % |
| **S5** | the instrument | rasterise + blend + beam within 0.3 ms of `sweep=` |

**Where it rides:** the replay session's first arm, before the replay proper.
- `wxlook` with the span change is flashed app-only, and one trace is held for 16 min.
- After 2 min of settling, 10 min of lines are read on production's own crop, with the layer on, look A and sweep
  look 4.
- The trace's close is recovered with `read_mac`, and then RC1 begins.
- **No `[touch]` line in the arm.**
- **Outside the set:** stop and report, and the replay does not start.

**The replay session, as it now runs** (the order of "How the session reads each row", with two additions):
1. the span arm above;
2. RC1 through RP6, as fixed;
3. **after RP6's arms:** the layer back on (look A, verified), then **20 min for Daniel's looks**, the replay's rain on
   the glass. First weather on against off; then look A against look B, changing "Weather look (bench)" only;
4. look A and the layer on are restored and verified;
5. RS5 and the rest, as fixed.

The touches and saves in Daniel's 20 minutes come after every graded arm.

**The timings:**
- the trace from RP1's reboot is held for **72 min**, so it outlasts Daniel's looks and RS5's 5 minutes;
- the window is **at least 105 min**;
- the proxy's hard limit is the window's length.

### Results for the span change, local (2026-10-29, `4a2fd33`; the board reads it in the replay session's first arm)

| | read | verdict |
|---|---|---|
| **S1** | W1-W5b all pass: span bytes 7,278.5 = marked 7,278.5 on average; the span blend equals `BlendRows` at all 3,600 angles, both looks | **as predicted** |
| **S-spanrec** | red rows `W3-in`, `W5a`, `W5b` | **as predicted** |
| S-w, S-span, S-noclr | red rows as before | as before |
| the real header | sha256 unchanged after the sabotages | — |

- **The rest:** `run.sh` (g++ 13.3.0, WSL) fails only "the same TU, for the board", which needs the xtensa compiler, as
  before. `wx-fb-check.py --selftest` passes.
- **Both images build.** The four bench line prefixes (`[health] outline=`, `[health] sweepscan=`, `[bench] scan0=`,
  `[bench] layer copy:`) are in `wxlook` and absent from the shipping ELF. That is F0's proposed reading, recorded
  alongside F0 as frozen, not in place of it.

### W5″, the board (2026-10-29, 15:03-17:06Z; window announced as about 8:05-10:20 AM PDT). VOID by its rule: a touch in O3

**The session ran as accepted.**
- **The images and their checks:**
  - six flashes, every one exit 0;
  - the old tree `wt-bendon` clean at `962ccb7`;
  - every `[build]` banner `blipscope-s3-128`;
  - the old image's page had no `wx-layer` field, and N's (`229e3c6`) had it unchecked;
  - the layer-off POST beforehand verified.
- **The windows, every one with the sun up** (elevation +5.9° to +20.2°): O1 15:17Z, N1 15:41Z, O2 16:05Z, N2 16:29Z,
  O3 16:54Z, each 10 min.
- **Afterwards:** `wxlook` back, with the layer on, look A, sweep look 4, verified.
- **The reading:** `w5pp_read.py` (sha256 `05f93155e5db`), written before the run, on traces read twice and static.

**VOID.** O3's trace holds 37 `[touch]` lines, all between **16:55:36 and 16:55:55Z (9:55 AM PDT)**:
- presses and releases;
- then `tap-in-detail` flips and closes: a detail card opened, flipped and closed.

A person was at the glass for about 20 s. The other four windows hold **no `[touch]` line at all**. The rule voids the
run, and it is not repeated for a better number.

| arm | lines | p95 median | resid | n median (range) |
|---|---|---|---|---|
| O1 | 18 | 53.4 | −10.5 | 35 (33-36) |
| N1 | 19 | 54.4 | −14.3 | 41 (40-43) |
| O2 | 19 | 55.0 | −13.9 | 41 (40-43) |
| N2 | 19 | 54.3 | −14.1 | 41 (40-43) |
| O3 (touched) | 19 | 51.9 | −10.5 | 33 (30-35) |

**An observation past the void, not a grade:** the same reader's own statistic, run with its void gate bypassed, reads
**"instrument" anyway**.
- **The grade** has only **3** shared n bins (40-42), below the floor of 4. N − O = −0.5 ms over them.
- **The control** has **0**. O1 and O3 ran at n 30-36 and O2 at n 40-43, so their bins never overlap.
- **The traffic rose and fell across the morning,** so the design's matched-n control needs O2's traffic to overlap
  O1's and O3's, and today it did not. That is a property of the instrument, recorded for the ruling. No redesign is
  made here.

**The rain probe and the replay's crop tool, after #499.** Main's `proxy/src/wx/` moved (#499, the UK ingest). Both
tools refuse any encoder that differs from origin/main's, and they did: the keyless watcher read "no measurement" from
16:41Z on.
- **Both now load main's encoder from a clean checkout of origin/main** (`wt-mainenc`). That is what "main's own crop
  path, loaded from a checkout" means; nothing is merged into #445 for it.
- `crop.ts` and `mrms.ts` are unchanged by #499. A dry run of the crop tool against that checkout made the crop
  **byte-identical** (`58903e952374`), now at encoder commit `1ccc7f1`.
- The probe agrees with the old instrument again (186 × 256, 35,364 covered cells).

**The span change** (`4a2fd33`, `395074b`) is now on #445 by fast-forward, after W5″'s last flash. It rides the replay
session's first arm.

### The replay session, first attempt (2026-10-29, 17:22Z): STOPPED in setup by a driver bug, before anything ran

**The window was announced** as 10:22 AM-12:12 PM PDT, with RS6's sentence and Daniel's two looks.
- **What happened:** the start state read correctly, and the span arm's flash exited 0 at 17:23:14Z. Eight seconds
  later the driver's page check raised `URLError` and the driver stopped, because the board was still booting after the
  flash.
  - The replay driver's `page()` had **no retry**, unlike every other driver's, so this was my bug in the rig.
  - **Nothing was replayed or graded.** `cloud-url` was **never set** (the proxy had not started), and no row was read.
- **Left behind:** the span arm's trace process, still holding the port. Its close would have left the board in ROM
  download mode. It was stopped, and `read_mac` recovered the board (exit 0). The page answered with the layer on.
- **The rig fixed, and nothing else changed:**
  - `page()` now retries for up to 120 s, as the other drivers' do;
  - a span-arm trace left running by any failure is now stopped and the board recovered.

  No prediction, reading or row changed.
- **Restarted under a newly announced window.** Nothing graded had been produced, so this is a re-run of the setup
  after a rig failure, not a re-run for a better number.

### The replay session (2026-10-29, 17:38-19:10Z; window announced as 10:38 AM-12:28 PM PDT, with RS6's sentence). RP6 is OUTSIDE THE SET: stopped for a ruling

**The run.**
- **The drivers:** `replay_run.py` (`a070d21ce59a`) and `replay_read.py` (`1f64ead44c1a`), the reader unchanged since it
  was written before any run.
- **The image:** `wxlook` at `aa38f2a`, which carries the span change.
- **Every record was read twice and static before grading.**

| | read | verdict |
|---|---|---|
| **S2** | the span arm (17:41-17:51Z, its own trace, no touch): rasterise **2.03 ms** | **as predicted** (2.24 ± 0.3; the fix run read 3.23) |
| **S3** | the keyless probe read **1** echo cell within 100 km at the arm's start (0 at its end) | **not graded, by its rule**. The blend read 4.45 ms, an observation only |
| **S4** | `layer copy: yes`; `sweepscan` 7,278 B / 7,278 px | **as predicted** |
| **S5** | 2.03 + 4.45 + 0.11 against `sweep=` 6.62 ms | **as predicted** |
| **RC1** | the tool afresh, on main's encoder (`wt-mainenc`): exit 0, its scan clean. The parser accepts all three. Bytes differing: S-old/source 12-19, fresh/source 8-19, S-old/fresh 8-11 | **as predicted** |
| **RP1** | after the reboot (17:57:12Z) the first `[wx]` line is `replaced`, `frame=` the crop's own; **0** `layer rebuilt`; **9/9** finished frames, **0** weather pixels each. S-old's age **75,938,129 s (878.9 days)** | **as predicted** |
| **RP2** | `replaced`, the frame within 120 s of its line; 1 `layer rebuilt`; 5 finished frames with **3,433-4,877** weather pixels | **as predicted** |
| **RP3** | 45 `[perf]` lines in the active cadence, median **12** (9-13); production recorded **79** E2:08 `/blips` points over the override's span | **as predicted** |
| **RS3 / RP4** | the scan: 82 files read, 0 missing; **key seen yes, key found no, id found no, address found no**; 1,021 log lines, all the fixed fields; no lat/lon/header/query. The text records hold no coordinate-shaped number | **as predicted** |
| **RS1** | the proxy stopped; its port refuses a connection afterwards | **as predicted** |
| **RS2** | the bench PC's own request **403**, counted | **as predicted** |
| **RS4** | 79 points server-side; 0 chunked bodies; no `smoke-prod.sh` run in the window | **as predicted** |
| **RS5** | `cloud-url` blank, the layer on, look A; **0** proxy requests after the clear + 10 s; production's `/blips` **9** points in the 5 min after | **as predicted** |
| **RP6 A / B / off** | `wx-fb-check.py` (Finished-only, as frozen): **UNTRUSTWORTHY in every arm** | **OUTSIDE THE SET** |

**The key was never found in any output.** Nothing needs revoking.

**RP6, read for its cause.** The frozen protocol puts "an UNTRUSTWORTHY frame with Finished: true" outside the set, so
the stop is the frozen one.
- **The check stops at the first untrustworthy frame, so it is not every frame.** Run with `--drop-untrustworthy`, as
  an observation only:
  - **9 of 18** frames trip in arm A, **5 of 18** in B and **4 of 18** in off;
  - every one but one trips on **"ring r=…: only …/… pixels have the ring's colour"**. For example, off frame 4: ring
    r=39, 170 of 220;
  - the other is "the beam's head: 2 blobs" in A.

  This morning's traffic was 35-43 contacts, so a ring partly covered by planes and labels near the centre is the first
  named alternative. A mid-draw frame is the second. Neither is excluded.
- **Arm A also holds 2 touches: upward swipes at 18:09:11-13Z (11:09 AM PDT),** inside the window and before Daniel's
  looks. The protocol says nobody touches the board. **`replay_read.py` did not check for touches inside the arms,**
  only for "touch unavailable" lines. That omission is mine. It does not change the verdict, which is already outside
  the set.
- **Past the verdict, an observation and not a grade:** on the trustworthy frames alone, arm A reads FAIL, B reads FAIL
  and off reads PASS.
  - **(1) ring exceptions:** 16 in A and 10 in B, all of the kind "a wedge colour outside the wedge". The named
    alternatives are a mask narrower than the drawn wedge, and wedge-only colours that something else also draws.
  - **(2) look A's outlines:** 4 of 168 markers are without one. The named alternatives are markers drawn without the
    outline loop (the special-contact reticle), and partial markers.
  - Neither is excluded, and nothing is changed for them here.

**Daniel's looks, 18:34:38-18:54:38Z (11:34-11:54 AM PDT), with the replay's rain on the glass,** in his words:
- weather on against off: **"it's a tiny bit choppier, but almot not noticable"**;
- look A against look B: **"Look B is a bit smoother, but Look A is nicer"**;
- the sweep's look 3 against look 4 (he changed "Sweep over weather (bench)"): **"Lit versues see-through doesn't seem
  to make any difference. They both look lit"**.
  - The setting took: 3 was stored, and `Initialise()` reads `wx-sweep` on every save. Over clear sky the two looks are
    identical by design; over rain only the cells under the wedge differ.
  - He had set it back to 4 by the end (RS5's page read sweep look 4).

**Daniel's decisions in this chat:**
- "we can leave the look option for the user on the config page", with **"A default"**;
- the labels: **A "Brighter rain, outlined planes", B "Fainter rain, no outlines"**.

The reviewer's ruling and its two additions follow in the next section.

**E2:08 now:** `wxlook` at `aa38f2a`, the layer on, look A, sweep look 4, `cloud-url` blank, back on production.

**For the ruling:**
1. RP6: the captures read outside the set at this traffic. Re-capture at night (low traffic), or a protocol that holds
   at 35-45 contacts. Either needs a ruling first.
2. The two observations past the verdict, each with its named alternatives, and nothing done about them.
3. `replay_read.py`'s missing touch check, inside the arms.
4. Look 3's retirement. It was queued after the captures; Daniel's look found no difference, which supports it, but its
   order was "after the captures".
