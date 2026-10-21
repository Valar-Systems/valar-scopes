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

