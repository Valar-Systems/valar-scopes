# Weather under the planes: the server side

Ruled 2026-10-20, on Daniel's direction to build as much of the weather feature as possible:
- the Blipscope layer is built on WXC1 v1 and the composite product (MRMS `MergedReflectivityQCComposite_00.50`, CONUS);
- **the product is a parameter from the start**: in the storage keys, the URL, the ETag and the cache keys;
- the server side covers the ingest, the crops, R2 and the proxy endpoint;
- **Daniel is told before anything needs a deploy or a new Cloudflare resource.**

This document freezes the design and the predictions **before any code exists.** It builds on the
weather spike's Part A ([#422](https://github.com/Valar-Systems/valar-scopes/pull/422),
`docs/wx-spike-2026-10.md`, accepted at `87ecb8a`) and its draft Stage 2 list. The ruled ingest shape
is (a′): a cron every minute, with a second check 30 s in.

The device side gets its own document. The joint work with Weatherscope (WXC1 v2 with rain/snow and the
product byte, and NWS alerts) is not in here.

## What this changes, and what it does not

**In the repo, in this work:**
- `proxy/src/wx/`:
  - the product registry;
  - the MRMS decoder (moved from the spike unchanged; A1 verified it against eccodes);
  - the tiled frame format;
  - the crop builder;
  - the ingest;
  - the `/wx` handler.
- `proxy/wx-ingest.wrangler.toml`: a second Worker built from the same package. Same `package.json`,
  same tests, same CI job (`workers.yml` already runs `proxy/**`).
- A top-level `[[r2_buckets]]` binding `WX` in `proxy/wrangler.toml`. That is the dev and test config
  only: tests get an in-memory bucket, and staging and production declare their bindings per env, so
  they are untouched.
- `test/fixtures/wx/server/` and a host test, so the device's decoder (`include/WxCrop.h`) is graded
  on crops **the server path produced**.

**Nothing is created in Cloudflare, and nothing is deployed.** Until Daniel creates the bucket and
binds it, the proxy's `/wx` route answers **503 `not_configured`**. That is the same "not wired yet"
answer `/missileer/*` gives without its origin. So merging this, and later deploying the proxy, cannot
break anything that exists. Each deploy is still Daniel's.

**What Daniel will be asked to create, later, in one message before it is needed:**
- **One R2 bucket** (suggested `blipscope-wx`), with a lifecycle backstop `--expire-days 1`.
- **The ingest Worker** `blipscope-wx-ingest`, with its cron `* * * * *`, `[limits] cpu_ms = 5000`,
  observability on, and **no route**. The bucket is bound read-write.
- **The bucket bound read-only-by-use** to the proxy's staging and production envs. One bucket and one
  ingest serve both: staging reads production's frames. That means fewer resources, and staging sees
  real weather.

The proxy's `cpu_ms = 200` is not touched.

## The product parameter

`proxy/src/wx/products.ts` is the registry and the only place a product is named. A product id matches
`^[a-z0-9]{2,8}$`.

| | `cref` (the composite) |
|---|---|
| NOAA source | `noaa-mrms-pds`, `CONUS/MergedReflectivityQCComposite_00.50/<YYYYMMDD>/`, files `MRMS_MergedReflectivityQCComposite_00.50_<YYYYMMDD>-<HHMMSS>.grib2.gz` |
| codes | A0's ruling: −999 → 14 (no coverage); below 15 dBZ, including −99, → 0; 15-75 dBZ in 5 dBZ steps → 1-13; 15 unused |
| cadence / max age served | 120 s / 600 s |

**Where the product appears**, and where a test asserts each:

| | form |
|---|---|
| R2 keys | `wx/<product>/frames/<frame epoch>.wxg` and `wx/<product>/latest` |
| URL | `GET /api/v1/blipscope/wx/<product>?lat=&lon=&r=` |
| ETag | `"wx1-<product>-<frame epoch>-<lat>_<lon>_<r>"`, from the **quantised** query: lat/lon rounded to 2 dp, r to whole km |
| cache key (tile cache, off until Stage 2 decides) | `https://wx-cache.internal/<product>/<frame epoch>/t/<ty>-<tx>` |

An unknown product is **404 `not_found`**, answered before any R2 read. A second product (Weatherscope's,
for example) is a registry row plus its code mapping. No other file names a product.

## The stored frame: WXG1, tiled

One object per frame, `wx/<product>/frames/<epoch>.wxg`:
- a 64-byte header: magic `WXG1`, the product id, the frame epoch, the grid geometry (ni, nj, la1, lo1,
  di, dj as in `MrmsGrid`), the tile size, and the tile counts;
- then the tiles, row-major. Each tile is **512 × 512 cells**, 4 bits each, two per byte, so **131,072
  bytes**. Edge tiles are padded with 14 to full size, so every tile's offset is
  `64 + (ty × tilesX + tx) × 131072` and no index is needed.
- CONUS is 7000 × 3500, so 14 × 7 = **98 tiles**, and the object is **12,845,120 bytes**.

`wx/<product>/latest` is a small JSON record: product, frame epoch, object key, ingested-at, and the
geometry. That way a crop reads `latest` once and then only the tiles it needs.

**Why 512:**
- the radar view is ±`radLat` × ±`radLon` degrees, each clamped at **2.0°** (`AircraftManager::ProjectAt`);
- so the widest padded crop is 2 × (2.0° + 2 cells) / 0.01° + 1 = **405 cells** per axis;
- 405 ≤ 512, so a crop spans at most 2 tiles per axis, which is **at most 4 range reads**.

## The crop: the radar's own box, not a km radius

The spike's `cropBox` used 111.32 km per degree and an unclamped km radius; Part B's open item. That is
replaced. The server computes **the view `ProjectAt` draws**, from the request's `r` and `lat`:
- `radLat = clamp(r / 111.0, 0.001, 2.0)`;
- `radLon = clamp(r / (111.0 × max(cos lat, 0.01)), 0.001, 2.0)`;
- plus **2 cells of padding** on every side.

**The server quantises first:** lat/lon to 2 dp and r to whole km. The crop is then a function of the
quantised query alone, and the ETag carries that query, so one ETag always means one crop.

The device sends lat/lon rounded to 2 dp (`wireloc::Center`, up to 0.005° off) and `r` as
`lround(rangeKm)` (up to 0.5 km off, which is at most 0.0045°). Together that is under 0.01°, one cell.
Two cells cover it, and WS4 proves it by sweep rather than by this sum.

**The constants are read from the firmware source, not copied.** `KM_PER_DEGREE`, `MAX_DEGREES`,
`MIN_DEGREES` and the cos floor are parsed out of `src/AircraftManager.cpp` by the test, so if the
radar's projection changes, the crop's test notices.

Encoding is the spike's encoder unchanged: WXC1 v1, long runs as LEB128, the 32 KB cap, and coarsening
2/4/8 with the flag. Cells outside the source grid are 14. If the box misses the grid entirely, the crop
is all 14 and no tile is read.

## The ingest: (a′), idempotent

Each cron run, and again 30 s in, does this per product:
1. **List** the newest key in NOAA's folder. The listing starts after now − 20 min, and covers the
   previous day's folder too in the first 20 min after midnight UTC.
2. If `frames/<epoch>.wxg` exists, stop: **a stored frame is never re-written.**
3. Otherwise fetch it, decode it straight into the tiled buffer row by row (never a flat copy), and
   **create it with `If-None-Match: *`.** A second writer, from an overlapping run or a retry, gets a
   precondition failure and stops.
4. **Move `latest` forward only.** Read it, and if the new epoch is later, write it with
   `etagMatches = <the etag just read>` (or create-only when absent). A failed condition re-reads once
   and gives up if the stored epoch is now ≥ ours. An older frame can therefore never replace a newer one.
5. Delete its own frames older than 1 h, from a listing. The lifecycle rule is only a backstop.

## The endpoint

`GET /api/v1/blipscope/wx/<product>?lat=&lon=&r=` sits behind the proxy's existing device auth and
per-key rate limit, exactly like `blips`.

| case | answer | R2 reads |
|---|---|---|
| no `WX` binding | 503 `not_configured` | 0 |
| unknown product | 404 `not_found` | 0 |
| bad lat / lon / r (same rules as `blips`) | 400 `bad_lat` / `bad_lon` / `bad_r` | 0 |
| no `latest`, or its frame is older than 600 s by the Worker's clock | 503 `wx_stale` | 1 |
| `If-None-Match` equals the current ETag | **304**, no body | 1 |
| the box misses the grid | 200, an all-14 crop | 1 |
| otherwise | 200 `application/octet-stream`, one WXC1 crop ≤ 32,768 bytes, `ETag`, `X-Wx-Frame: <epoch>` | 1 + at most 4 |

- `/healthz` gains `wx`: `"not_configured"` without the binding, or `{ "<product>": { "frameAgeS": N } }`.
  A missing `latest` reports `null`, never a made-up age.
- `/credits` gains the attribution line: **"Weather radar derived from NOAA MRMS data. Not endorsed by
  NOAA."**
- `smoke-prod.sh` gains a `/wx` check. It is **written now and armed only once the binding exists**: it
  accepts `503 not_configured` until then, and treats anything else as a failure.

## Predictions (frozen before any code)

Every check below has a sabotage that must turn it red. A check whose sabotage stays green is reported
as untrustworthy, not passed.

**WS1. The tiled frame round-trips.**
- A deterministic synthetic 7000 × 3500 code grid is packed into WXG1 and read back cell by cell through
  the tile reader: **24,500,000 / 24,500,000** cells identical.
- The object is **12,845,120 bytes**.
- *Sabotage:* tile offsets computed with `tilesY` in place of `tilesX`, which must give mismatches > 0.

**WS2. Crops from tiles equal crops from the flat grid.**
- Setup: the 50 public airports of A2's corpus (`tools/wx/airports-a2.txt` on the spike branch at
  `a5db678`, cleared by `forbidden.py`; copied unchanged to `proxy/test/fixtures/wx-airports.txt`)
  × r ∈ {25, 80, 160, 300} km = 200 crops.
- Each crop built through R2 range reads of WXG1 is **byte-identical** to the same encoder run over the
  flat grid with the same box: **200 / 200**.
- *Sabotage:* swap `ty`/`tx` in the tile reader, which must leave at least one crop differing.

**WS3. At most 4 tile reads per crop.**
- Setup: lat 20.0-55.0 in 0.5° steps × lon −129.0 to −61.0 in 1° steps × r ∈ {1, 25, 80, 160, 222, 300,
  500} km.
- The maximum tile reads for one crop is **4**. The count is printed.
- *Sabotage:* 256-cell tiles, which must give a maximum of **9**.

**WS4. The crop covers the radar's view.**
- Setup: the same sweep. For each case, the view is computed **from constants parsed out of
  `src/AircraftManager.cpp`**, at the device's precise position. That position is anywhere within
  ±0.005° of the wire's lat/lon, and its range anywhere within ±0.5 km of `r`, worst corners taken.
- The crop's outer cell edges contain the whole view box in **every** case: **0 uncovered**.
- The overshoot per edge is ≤ 4 cells: padding (2) + outward rounding (< 1) + wire offset (< 0.95).
- *Sabotage:* padding 0, which must give uncovered > 0.
- *Control:* the parser must find exactly the four constants, or the test is BLIND (exit 2), never a pass.

**WS5. The ingest is idempotent, against the emulator.**
- **Controls first:**
  - an unconditional put on an existing key overwrites;
  - a put with `If-None-Match: *` on an existing key returns `null`.
  If the emulator does not honour the condition, WS5 is BLIND.
- **(a)** Ingesting the same frame twice writes it once. The second run reports "already stored" and
  makes no put.
- **(b)** Frames delivered newest first, then older: `latest` stays at the newest.
- **(c)** Two ingests of the same frame, started together: exactly **one** frame write succeeds.
- **(d)** Frames older than 1 h are deleted, and newer ones kept.
- *Sabotages:*
  - drop the create-only condition, which must give (c) 2 writes;
  - drop the "only if newer" test, which must make (b) move backwards.

**WS6. The endpoint matches the table above, case by case.** Every reply's status, body shape and R2
read count (counted through a wrapped binding) is asserted, plus these properties:
- the ETag carries the product, and two products with the same frame and query get **different** ETags;
- a stale ETag (an older frame) gets a 200, never a 304;
- a request outside the grid (51.47, −0.45, London) makes 1 R2 read and returns all 14;
- an unauthenticated request is 401 before any of this.
- *Sabotage:* an ETag without the product, which must make the two-product case collide.

**WS7. The device decodes what the server makes.**
- The server path (the WS6 handler over a synthetic WXG1 in R2) writes **8 fixture crops** to
  `test/fixtures/wx/server/`, with a manifest of each crop's expected cell sha256. The 8 are a plain
  crop, a coarsened one, one partly outside the grid, one all-14, and the rest from the airport set.
- `test_wx_server_crops` decodes them with `include/WxCrop.h` and matches **8 / 8** shas. It runs in
  `test/host/run.sh`, so in `host-tests`.
- `workers.yml` regenerates the fixture and fails if it is stale, like `locate-format`.
- *Sabotage:* one run nibble flipped in one fixture, which must fail exactly that crop.

**WS8. The real thing, measured locally in Node on live NOAA frames.** These are Stage 2's numbers, not
production's.
- **Ingest CPU** for one frame: **≤ 1.5 s** (A4's flat packing was 0.53 s).
- **Peak live memory:** **≤ 96 MB** (A4's was 47.05 MB). The meter is the forced-GC live meter of A4,
  with its sabotage band.
- **Crop build:** **p95 ≤ 10 ms** over the WS2 airport set, from tiles held in memory.
- Each result is reported against its bar. Production CPU, memory and cron delay are measured after
  Daniel deploys, against Stage 2's frozen bars: p95 lag ≤ 2 min, and no skipped frames.

**WS8, grading details fixed before its first run** (2026-10-20, after WS1-WS7, before any WS8
measurement):
- **Frame:** Stage 1's storm frame, the one A4 measured
  (`CONUS/MergedReflectivityQCComposite_00.50/20240521/MRMS_MergedReflectivityQCComposite_00.50_20240521-220042.grib2.gz`).
  Its download must hash to the manifest's `0bd824f76981…`, or WS8 is BLIND.
- **CPU and wall:** un-forced runs, `process.cpuUsage()` from just before the download to the finished
  WXG1 object, the same scope as A4's 0.53 s. **3 runs**; the bar applies to the largest.
- **Live memory:** A4's meter, `node --expose-gc`. A forced `gc()` then `heapUsed + external` at every
  stage boundary and right after each large allocation: the download, the geometry inflate, the tiled
  buffer, the decoder's stages. **3 runs**; the bar applies to the largest live peak.
- **Its sabotage:** the same run holding an extra 64 MB, touched. **Graded on the live peak:** it must
  rise by **63-65 MB** against the mean unsabotaged peak. That is A4's ruled band, applied to the peak
  only, since A4 showed mid-stream samples vary by more than the band.
- **Crop build:** all 200 cases of WS2 on the real frame. Tiles are cut from the object in memory, as R2
  would return them, and each crop is timed from `cropCellBox` to finished bytes. **p95 by nearest rank**
  (sorted[⌈0.95 n⌉ − 1]), and the maximum too.

**Outside the set:** any reading not listed above means stop. Code, tests and bars are not edited to
make a reading come true. The raw reading goes to the reviewer, and the next step is decided then.

## Results (2026-10-20, local vitest-pool-workers, Windows)

| check | result | numbers |
|---|---|---|
| **WS1** tiling round trip | **as predicted** | 98 tiles, 12,845,120 bytes; **24,500,000 / 24,500,000** cells identical; sabotage (tilesY for tilesX) 5,470,202 mismatched; control: every code 0-15 present |
| **WS2** tiles vs flat | **as predicted** | **200 / 200** crops through the worker byte-identical; sabotage (ty/tx swapped) 184 / 200 differ |
| extra (not frozen) | holds | the encoder equals the spike's `encodeCrop` on the spike's own boxes: 200 / 200 |
| **WS3** tile reads | **as predicted** | 34,293 cases, maximum **4**; sabotage (256-cell tiles) maximum **9** |
| **WS4** coverage | **0 uncovered: as predicted. Overshoot: OFF-PREDICTION** | 34,293 cases, 0 uncovered edges; sabotage (pad 0) 308,162 uncovered; control: the four constants found once each, and equal to `VIEW`. **Worst overshoot 4.180 cells against the frozen ≤ 4** |
| **WS5** ingest idempotence | **as predicted** | Controls: an unconditional put overwrites; `If-None-Match: *` on an existing key returns null; a stale `etagMatches` returns null. **(a)** stored, then already-stored: 1 frame write, 1 NOAA GET. **(b)** `latest` kept at the newest; sabotage (no "only if newer") moved it to the older frame. **(c)** two at once: stored + lost-race, **1** write; sabotage (no create-only) **2** writes. **(d)** pruned 2, kept 2. Driven through the real decoder by a synthetic GRIB2 (`proxy/test/wx-grib.ts`). |
| **WS6** endpoint | **as predicted** | 15 / 15 cases. Fresh 200: 7,655 bytes, 3 reads (2 tiles). 304, stale, off-grid: 1 read each. 401, 400 and 404: 0 reads. Different products give different ETags; sabotage (an ETag without the product) collides. `/healthz`: `not_configured`, `null` and the real age. |
| **WS7** device decodes server crops | **as predicted** | **8 / 8** server-made crops decode with `include/WxCrop.h` to the encoder's own cells. The set: uniform, coarsened (KBIS 300 km, k = 2, flagged), partly outside the grid (KMCI on the small grid), all 14 (PHNL), and 4 from the set at up to 405 × 404. Sabotage: one nibble in the KMCI crop fails exactly that crop, on both `file` and `cells`. `run.sh`: 377 ok, 0 FAIL, 0 BLIND. |
| **WS8** real frame, Node | **as predicted** | Storm frame, download sha `0bd824f76981…` matched. **CPU** 0.485 / 0.453 / **0.671** s (≤ 1.5); wall 1.14-1.30 s. **Live peak** **41.60** MB ×3 (≤ 96; A4's flat packing was 47.05). Sabotage, 64 MB held: 105.62 MB ×3, a rise of **+64.02 MB** (band 63-65). **Crops**, 200 on the real frame: p50 1.57 ms, **p95 5.89 ms** (≤ 10), max 7.41 ms; largest 27,360 bytes, none coarsened. |

**WS4's overshoot, and why the frozen bound was wrong.** The worst case is the **west edge at 52 N, r = 80
km**, with the device 0.005° east of the wire's position and its range 0.5 km shorter.
- The frozen bound used "2 + rounding (< 1) + wire offset (< 0.95)". That wire-offset term came from
  the sentence above: "together that is under 0.01°, one cell". **That sum is latitude-only.**
- In longitude, 0.5 km of range is 0.5 / (111 × cos lat) degrees: 0.0073° at 52 N and 0.0079° at 55 N.
  So the wire offset reaches 1.23-1.29 cells there, and the bound is about 4.29.
- **Alternative named:** a rounding bug in `cropCellBox` that over-pads. **Excluded:** that would push
  every edge out. Per edge, the worst overshoots are N **3.950**, S **3.878**, W **4.180** and E
  **4.180** cells. The latitude edges sit exactly inside 2 + 1 + 0.95; only the longitude edges, which
  carry the 1/cos factor, exceed 4. (A read-only diagnostic located this; the test is unchanged.)
- **Coverage is unaffected.** Two cells of padding exceed the real worst-case offset (1.29 cells at
  55 N), and the sweep finds 0 uncovered edges.
- The test still asserts the frozen ≤ 4 and fails. **Nothing is changed until there is a ruling.**

**Centres (WS6, WS7):** every centre is a public airport from the cleared list. KMCI was used for the
endpoint tests and PHNL for the off-grid ones (Stage 1's). The first draft of WS6 used two ad-hoc
points, and they were replaced before anything was committed. Re-running `forbidden.py` against today's
tree reads every cleared airport as "too close" to itself, because their coordinates are now in the
fixtures it scans. So the cleared list, not a re-run, is what admits a centre.

**WS5's first run** died on the test pool's isolated storage (EBUSY on Windows). My sabotage test left
an R2 body unread: it took the etag with `get` instead of `head`. That was fixed in the test; no measured
value was involved.

**Two endpoint-table items, done differently from the plan, and why:**
- **`/credits` does NOT gain the NOAA line yet.** "Weather radar derived from NOAA MRMS data" would be a
  false public statement while no weather is served. It goes in the PR that binds the bucket, the same
  rule as README's telemetry disclosure: the statement changes in the commit that makes it true.
- **`smoke-prod.sh` gains the `/wx` check now**, as planned. It passes on exactly two answers:
  - `503 not_configured`: the inert route;
  - `200` with WXC1 magic and an ETag: wired.

  Anything else is a FAIL: a 404 (no route) or a `503 wx_stale` (wired, ingest behind). Its branches
  have been syntax-checked, **not run**: only production can exercise them, so the first post-deploy run
  is the first real reading.

## WS4′, re-registered by ruling (2026-10-20), frozen before any run

**WS4's ≤ 4 bound stays a miss in the record:** worst case 4.180 cells. The ruling accepted the cause:
the wire-offset term was latitude-only, and the longitude edges carry 1/cos(lat). Coverage held: 0
uncovered in 34,293 cases, with the sabotage and the control working.

**The re-registered check.** The terms are the same, but evaluated per case, per edge, at that case's
own latitude, instead of as one constant.
- **Sweep:** WS4's 34,293 cases. Each case has 8 device corners (lat ± 0.005°, lon ± 0.005°, range
  ± 0.5 km, floored at 0.5 km as in WS4) and 4 edges. That is **1,097,376** corner-edge checks.
- **Overshoot:** the crop's outer cell edge minus the device's view edge, in cells, exactly as WS4
  computes it.
- **The bound:** for each case and edge, `bound = 2 (the frozen padding) + 1 (outward rounding, < 1 cell)
  + wire offset`. lat and r are the quantised query. radLat and radLon follow `ProjectAt`'s rule, with the
  constants parsed out of `src/AircraftManager.cpp`, so the 2.0° clamp, the 0.001° floor and the cos floor
  are all included exactly.
  - **N / S edges:** `wire offset = [0.005° + max over r′ ∈ {r − 0.5, r + 0.5} of |radLat(r) − radLat(r′)|] / dj`.
  - **W / E edges:** `wire offset = [0.005° + max over lat′ ∈ {lat ± 0.005}, r′ ∈ {r ± 0.5} of
    |radLon(lat, r) − radLon(lat′, r′)|] / di`. This term carries the lat rounding's effect on cos(lat).
  - Corners are enough: radLat and radLon are monotonic in r, and radLon is monotonic in lat across
    20-55 N.
  - Floating-point tolerance: **1e-9 cells**.
- **Predicted:** **0 violations** in 1,097,376 checks. Printed but not graded:
  - the worst overshoot, which is still **4.180**;
  - the largest per-case bound, **≈ 4.29** cells near 55 N;
  - the smallest margin (bound − overshoot), which is ≥ 0 by the prediction.
- **Its sabotage:** padding **3**, with the bound still computed with padding 2, must give **violations > 0**.
- **Coverage stays asserted as before:** 0 uncovered.
- **Outside the set:** stop and report.

### WS4′ results (2026-10-20): AS PREDICTED

| | result |
|---|---|
| graded: violations | **0** in **1,097,376** corner-edge checks (34,293 cases × 8 corners × 4 edges) |
| graded: sabotage, padding 3 against the padding-2 bound | **264,814** violations: red |
| graded: coverage | 0 uncovered, unchanged |
| printed: worst overshoot | 4.180 cells, the same as WS4 |
| printed: largest per-case bound | **4.301** cells, against my ≈ 4.29 |
| printed: smallest margin | **−3.7 × 10⁻¹³** cells, inside the 1e-9 tolerance |

**The largest bound is 4.301, not ≈ 4.29.** The estimate took 0.5 km / (111 × cos 55°) alone. The bound
also carries cos(lat)'s change across lat ± 0.005°, and the sweep's last row is 55.0 with corners at
55.005. This was printed, not graded, and the gap is in the bound's favour of being honest rather than
loose.

**The margin is zero to floating point, so the bound is tight, not loose.** At some corner the outward
rounding approaches its full cell from below, at the same corner where the wire offset is at its maximum.
The overshoot then equals the bound to 10⁻¹³. That is what the ruling asked for: a bound built from the
real terms that real cases reach, not a constant like 4.3 that is loose almost everywhere.


## Deploy, 2026-10-22: what it creates, what it costs, and the predictions (frozen before the bucket exists)

Daniel's instruction of 2026-10-21: deploy #443 under the standing rule. Say first what it creates and what it
is expected to cost each month, and ask him if that is more than $0.

**What it creates:**
- **The R2 bucket `blipscope-wx`,** with a lifecycle backstop that expires objects after 1 day. The ingest prunes
  anything older than `RETAIN_S` = 3600 s itself; the lifecycle rule only bounds a pruning failure.
- **The Worker `blipscope-wx-ingest`,** deployed from `proxy/wx-ingest.wrangler.toml` at main `0116e13`: cron
  `* * * * *` (two checks a minute, `:00` and `:30`), `cpu_ms = 5000`, observability on, **no route**.
- **Later, in its own reviewed change (S3):** a `WX` binding to `blipscope-wx` in the proxy's staging and
  production envs. Until then `/wx` answers 503 `not_configured`.

**Expected monthly cost: $0.** These are 30-day account figures, measured 2026-10-21 from the GraphQL Analytics API
and `wrangler r2 bucket info`, against what the Workers Paid plan and R2 include:

| | now | the weather server adds | included |
|---|---|---|---|
| R2 storage | 258 MB (`missileer-clips`) + 0.5 MB | about 0.4 GB (31 frames × 12,845,120 B). Even with pruning broken, about 9.25 GB under the lifecycle rule | 10 GB |
| R2 Class A | about 751k/month (25,049/day over the last 7 days, almost all `missileer-live` puts) | **about 130k/month:** 6 per frame (3 on the check that stores it, a `list` on each of the other 3) × 720 frames/day | 1M |
| R2 Class B | about 312k/month | about 432k/month from the ingest (5 per check), plus about 216k/month for each device with the layer on | 10M |
| Workers CPU | 2.33M ms/month (all scripts) | about 10-15M ms/month (WS8: 0.45-0.67 s per frame × 720/day) | 30M |
| Workers requests | 663k/month | 43,200 cron runs | 10M |

**The margin that is thin:** R2 Class A rises to about **88 %** of the free 1M. A second product, such as
Weatherscope's, or more Missileer writes would cross it, at $4.50 per million over. That crossing will be put to
Daniel before it happens. It isn't now.

### S1: the bucket and the ingest (nothing reads them yet)

| | check | predicted |
|---|---|---|
| **I1** | the bucket | `wrangler r2 bucket info blipscope-wx` shows it, and its lifecycle list shows a 1-day expiry |
| **I2** | the ingest deploy | exit 0; the deploy output lists the cron `* * * * *`; the newest entry in `deployments list` is the version it printed |
| **I3** | frames land | within 10 min, `wx/cref/latest` exists. Read twice at least 4 min apart: its epoch advances by a **non-zero multiple of 120 s**, and at each read the frame is **≤ 600 s** old |
| **I4** | one frame object | exactly **12,845,120 B**, starting `WXG1` |
| **I5** | pruning | at 75 min or later: **29-33** frame objects under `wx/cref/frames/`. Read twice 10 min apart, and both counts are in the band |
| **I6** | the ingest's own log (`wrangler tail`, first 20 min) | **≥ 30** `wx_ingest` lines and **0** `wx_ingest_error`. Statuses only `stored`, `already-stored` or `no-frame` |
| **I7** | CPU, first 2 h (GraphQL, `scriptName` `blipscope-wx-ingest`) | per-invocation `cpuTimeP99` **≤ 1,500 ms**; no CPU-limit errors |
| **escape** | anything else | **stop and report.** The ingest has no route and nothing reads it, so this is not an emergency. Removing it is Daniel's, because it deletes a resource |

### S2: the proxy at main `0116e13`, staging then production, with no `WX` binding (the off stage)

This deploy also carries #450, #458 and #460. The Bend site feed is off in production (`SITE_FEED_BEND` "off").
**Rollback target:** production version `7224e0dc-1832-46c3-8089-0126352da8b4` (commit `2460deb`).

| | check | predicted |
|---|---|---|
| **P1** | `/healthz` | `commit` **0116e13** on both envs; `wx` **"not_configured"** |
| **P2** | the deploy output's bindings | production: `BEND_SNAPSHOT (BendSnapshotDO)`, `SITE_FEED_BEND ("off")`, **no `WX`**; staging: the same with `("on")` |
| **P3** | `smoke-prod.sh` on production, E2:08's own credential (Daniel's consent, 2026-10-21) | **before, on `2460deb`: exactly 1 FAIL, the `/wx` check (404: that build has no route); every other check passes.** **After: 0 FAIL**, with `/wx` passing as "not wired yet" |
| **P4** | `smoke-prod.sh` on staging | reported, **not graded**: staging may hold a different device-key secret, so E2:08's credential may be refused there by design |
| **P5** | the fleet's own traffic (Analytics Engine; per-request points with a device id, **E2:08's id excluded**, since the smoke runs use it). B = the 60 min before the production deploy, A = 5-65 min after | fleet **5xx in A: 0**; fleet **4xx in A: 0**; **devices in B but not A: 0**; `/blips` count A/B **0.4-2.5**; usage rows in A from **≥ 4 devices**, every one arity **`u8`**, **0 `(none)`**; production `sitefeed` events in A: **0** |
| **P6** | fail fast, `[T, T + 15 min)` | any fleet 5xx, or any fleet 401/403, means **roll back at once** |
| **controls** (run before believing P5) | can each detector see what it looks for? | `sitefeed` events on staging (`blipscope_proxy_staging`) since its 23:06Z deploy: **> 0**. Unfiltered 4xx over all production points in 24 h: **> 0**. The device-set difference between 2026-10-21 21:00 and 22:00 (4 devices, then 6): **2** |
| **escape** | anything else, or two reads of A that disagree | **stop and decide.** Production misbehaving means roll back first |

S3's predictions (the binding, `/wx` serving crops, smoke's `/wx` passing as "wired") are frozen in S3's own PR,
before it is reviewed.

### S3: the `WX` binding, so `/wx` serves crops (frozen before the binding is written)

**The change:** a `[[env.staging.r2_buckets]]` and a `[[env.production.r2_buckets]]` block, each binding `WX` to
`blipscope-wx`, with `wrangler.toml`'s top-level comment updated to match. Nothing else changes. It deploys to
staging and then production **only after S2 has passed**, and only once this PR is approved and merged.
**Rollback:** `wrangler rollback` to the S2 version, which leaves `/wx` at 503 `not_configured` again.

| | check | predicted |
|---|---|---|
| **W1** | the dry-runs, before merge | `env.WX (blipscope-wx)` listed in **both** staging and production; every binding S2 listed is still there |
| **W2** | `/healthz`, after each deploy | `wx.cref.frameAgeS` **≤ 600**. Read twice at least 3 min apart, it is ≤ 600 both times, and the record moved on between the reads (the frame advanced) |
| **W3** | `smoke-prod.sh`'s `/wx` check, production | **PASS, "wired"**: HTTP 200, magic `WXC1`, an ETag |
| **W4** | revalidation | the same query sent again at once with `If-None-Match: <that ETag>` gets **304**, unless the frame advanced in between, which means **200 with a different ETag**. Anything else is a FAIL |
| **W5** | the fleet's own traffic, the same windows and rules as P5 | unchanged from S2's verdict. Also, **0 `/wx` requests from fleet devices**: no shipped firmware has the layer, and E2:08's and the smoke's own requests are excluded |
| **W6** | `/wx` answers in the smoke runs and W4 | **no 5xx** |
| **escape** | anything else | **stop.** In production, roll back to the S2 version first |

### Results so far (S1 deployed 2026-10-22T00:16Z; S2 not deployed)

| | read | verdict |
|---|---|---|
| **I1** | `blipscope-wx` created 00:15:58Z, WNAM; lifecycle `expire-1d` ("Expire objects after 1 days"), beside the default 7-day multipart-abort rule | **as predicted** |
| **I2** | deploy exit 0; `schedule: * * * * *`; version `47ad5010-19b5-4ca9-b186-ead1180527fd`, and `deployments list` shows it as the newest (00:16:20Z) | **as predicted** |
| **I3** | first seen 00:19:35Z (deployed 00:16:21Z; the 00:17 and 00:18 ticks never ran, which is cron propagation). Read A, epoch 00:18:37Z; read B at 00:23:39Z, epoch 00:20:39Z. **The advance is 122 s.** The frame was about 58 s and 180 s old at the two reads | **MISS on one clause: "a non-zero multiple of 120 s".** The rest is as predicted (exists within 10 min, advances, ≤ 600 s old) |
| **I4** | `wx/cref/frames/1792628317.wxg`: **12,845,120 B**, magic `WXG1`, product `cref` | **as predicted** |
| **I6** | the tail, deploy + 20 min: **18 invocations** (00:19:26Z-00:36:05Z), every one `ok`, 0 exceptions; **36 `wx_ingest` lines**; **0 `wx_ingest_error`**; statuses `stored` 9 and `already-stored` 27; `pruned` 0, since no frame is an hour old yet; `lagS` 57-154 s | **as predicted** |
| **I5** | **read 1 (01:32Z):** R2's storage analytics' newest row (00:40Z) said `objectCount 0` while frames existed. **Read 2 (01:42Z):** 31 objects, 385,353,781 B (analytics and `bucket info` agree), but the newest bucket starts at 01:10Z, so it describes about 55-65 min after the deploy, not 75 or more. **The two reads disagree.** **Beside it, not as its grade:** a direct probe at 01:33Z found the first frame (00:18:37Z, 4,464 s old) gone and a frame 1,830 s old present at 12,845,120 B. The probe had first been shown to tell a present key from an absent one. The writer's own log (both tails, de-duplicated): 46 frames stored, 16 pruned, so 30 present. Pruning began at the 01:19:05Z check, when the first frame turned 3,600 s, and removes one frame per new frame | **BLIND**, by the reviewer's ruling (not passed, and not re-read until one passes) |
| **I5′** | frozen at `e636e79` (a direct listing, 31 objects and a byte band, read twice) | **not yet read: no instrument runs.** `wrangler` (4.35 and 4.151.3) has no object listing, and `wrangler dev --remote` failed (the account's `edge-preview` call answered 400) |
| **I7** | `workersInvocationsAdaptive`, `blipscope-wx-ingest`, 00:16-02:17Z: **114 invocations**, 0 errors, CPU total 61,880 ms, **mean 543 ms**. Quantiles: p25 8, p50 576, p75 1,006, p90 1,338, **p99 2,172 ms** | **MISS: p99 2,172 ms > 1,500.** No CPU-limit errors (`cpu_ms = 5000`). Recorded, not re-graded |
| **P3 (before)** | `smoke-prod.sh` against production `2460deb`, E2:08's credential: **PASS 31, FAIL 2, SKIPPED 1**. FAILs: `/wx` (404, as predicted) and "enroll POST, unsolved token" (403 `fake_device_id` against an expected `"unverified"`; not predicted) | **MISS. S2 stopped before deploying, for a ruling** (#461) |

**I3's miss, and why it is recorded rather than re-graded.** MRMS frame times are not on an exact 120 s grid. The two frames here are 00:18:37 and 00:20:39. The figure I3 leaned on was M4a's "mode 120 s", which describes the most common step, not a fixed one. So I3 was frozen with a criterion tighter than anything measured. The frame does advance, which is what I3 was for. The clause stays a miss.

**P3's miss.** The second FAIL is pre-existing. Since `e4c768a` (2026-09-23), production refuses fake device ids before the token gate, and that check's fixture id `a1b2c3d4e5f6a7b8` is on the fake allowlist. The identical FAIL is in that day's 16:08 production baseline, taken with the old credential. Staging passes the check. The options are on #461.

### I5′ (the reviewer's ruling of 2026-10-22, pasted by Daniel; frozen before any new read)

**The ruling, as received:** I5 is recorded **BLIND**, not passed, and it is not re-read until one passes.
I5′ is accepted:
- **Primary instrument:** a direct listing of the bucket (object count and total bytes), not the analytics.
- **Reads:** twice, at least 10 minutes apart, both at 75 minutes or more after the deploy.
- **Predicted:** 31 objects in both reads (30 frames plus `latest`), with total bytes inside a stated band, and the
  two reads agree.
- **The analytics** are reported beside it, as data, not graded.
- **The 01:42Z reads don't count,** because they were taken before the ruling.

**The instrument.** `wrangler` 4.35 has no object listing, so the listing is a throwaway Worker run with
`wrangler dev --remote`: a preview session bound to `blipscope-wx`, deploying nothing. It pages
`bucket.list()` to the end and returns, for every object:
- the count and the summed `size`;
- the frame count and the oldest and newest frame epochs;
- the keys that aren't frames.

**Its control, run before either read:** the listing must contain `wx/cref/latest`, and the epoch in that
record (fetched with `r2 object get`) must equal the newest frame epoch the listing reports. A listing that can't
see the record it is checked against is not believed.

| | measured | predicted |
|---|---|---|
| **I5′** | two direct listings of `blipscope-wx`, at least 10 min apart, both after 01:31:21Z (deploy 00:16:21Z + 75 min) | **31 objects in both**: 30 under `wx/cref/frames/` plus `wx/cref/latest`, and no other key. **Total bytes in [385,353,750, 385,353,850]** in both: 30 × 12,845,120 = 385,353,600, plus the `latest` record at 150-250 B. The two reads agree on the count |

| outcome | meaning |
|---|---|
| **(a) as predicted** | pruning holds the bucket at one hour of frames |
| **(b)** 32 objects (31 frames + `latest`), every frame younger than 3,600 + 120 s | a read landing in the seconds between a frame's put and its prune (the same check does both), or cadence jitter fitting 31 frames into the hour. **Not a pass:** reported, and the next step is decided then |
| **(c)** anything else: under 30 frames, over 32 objects, a frame older than 3,720 s, a key that is neither a frame nor `latest`, bytes outside the band for the frame count, reads that disagree, or the control failing | pruning or the instrument is wrong. **Stop and decide** |

**I7's miss, and what it changes.** Invocations that build nothing cost about 8 ms (p25). Those that build a frame cost about
1-2 s, roughly twice WS8's 0.45-0.67 s, which was measured in Node, not in workerd. The bar was frozen from the Node figure.
**The cost statement's CPU row was an estimate, and the measurement replaces it:** 543 ms × 43,200 invocations is about
**23.4M CPU-ms a month** from the ingest. With the proxy's 2.33M that makes about **25.8M of the 30M included (86 %)**, not
the 12-17M estimated. **The expected cost is still $0,** but CPU now sits beside R2 Class A (about 88 %) as a thin margin.
Anything that adds CPU, a second product above all, crosses it.

**Outside any graded row (the second tail, 00:26-01:50Z, 86 invocations, all `ok`, 0 `wx_ingest_error`):** one `lost-race`
at 00:49:40Z. An extra invocation started at 00:49:34Z, off the minute boundary. Its `:30` check followed its `:00` check with no
wait, which is consistent with a second delivery of the 00:49 trigger. It stored frame `1792630119` 0.26 s after the earlier
invocation's `:30` check began building the same frame, and that check lost the conditional put. Exactly one write landed and
`latest` advanced once: the overlap the idempotent ingest was built for. **I6's status set left `lost-race` out.** I6 was graded
on the first 20 minutes, where none occurred, so its verdict stands.

### S2, restated before it runs (2026-10-23): the commit is main `4752767`

**Why it moved.** P3's ruling (2026-10-23) had the smoke check fixed first (#463). #465 and #466 merged as well,
so main is now `4752767`. `deploy.sh` deploys main's head, and it refuses a tree that is behind main in `proxy/`.

**What did not move.** `git diff 0116e13 4752767` over `proxy/src`, `proxy/wrangler.toml`,
`proxy/package.json`, `proxy/package-lock.json` and `proxy/tsconfig.json` is **empty**. Under `proxy/`, only
`scripts/smoke-prod.sh` differs (#463: the probe id, not deployed code). **So the Worker S2 deploys is `0116e13`'s
code.**

**Carried since production's `2460deb`, all reviewed and merged:** #450 `882d49e`, #458 `6bb61df`, #460 `1990156`,
#443 `0116e13`, #463 `2484bda` (the script only).

**The changes to S2's rows:**
- **P1 reads `4752767`** wherever it said `0116e13`;
- **P3 "after"** uses #463's script: **0 FAIL**, as frozen, with `/wx` passing as "not wired yet". SKIPs are not
  predicted, because `/v1/photo` depends on the live sky (#463's E1);
- **every other row is as frozen.**

### S2's controls, read before the deploy (2026-10-23), and Daniel's decision

| control | predicted | read | verdict |
|---|---|---|---|
| (a) `sitefeed` events on staging since its 2026-10-21 23:06Z deploy | > 0 | **0** | **BLIND, not failed.** Since that deploy, staging received 14 `/v1/blips` requests, and **every one was refused 401**. Staging holds a different device-key secret, so no successful aircraft fetch ran, and the Bend writer never ran. The instrument is unexercised, not shown broken |
| (b) unfiltered production 4xx, last 24 h | > 0 | **158** | as predicted: the 4xx detector can see a 4xx |
| (c) the device-set difference, 2026-10-21 22:00 against 21:00 | 2 | **2** (6 against 4) | as predicted: the set comparison can see a difference |

**Daniel's decision (2026-10-23), as given:** "Proceed, row BLIND. Record control (a) as BLIND and P5's sitefeed row as
unreadable, not passed. P2 is a hard gate: if the deploy output doesn't list SITE_FEED_BEND "off", stop and roll back.
Prove the sitefeed instrument at #464 instead, where the writer runs in production: freeze a positive control there,
with sitefeed events appearing within a stated number of minutes of switching the feed on."

**So:**
- **P5's "production `sitefeed` events in A: 0" row is unreadable,** whatever it reads.
- **P2 is a hard gate:** production's deploy output must list `env.SITE_FEED_BEND ("off")`, or roll back at once.
- **The instrument is proved at #464,** with a positive control frozen there before its deploy.

### I5′'s instrument, restated before any read (2026-10-23)

**Why it moved.** The frozen instrument, a throwaway Worker under `wrangler dev --remote`, never ran: the account's
`edge-preview` call answered 400. Daniel chose the replacement (2026-10-22): **a read-only R2 token**, bucket-scoped to
`blipscope-wx` with the "Object Read only" permission. He created it on 2026-10-23. It is held in the token file, never
printed.

**The instrument now.** A direct listing through R2's S3-compatible API: `ListObjectsV2` over the whole bucket, no
prefix, paged by continuation token to the end, signed with SigV4. It reports:
- the object count and the summed `Size`;
- the frame count and the oldest and newest frame ages;
- every key that is not `wx/cref/frames/<epoch>.wxg`.

**The control, before either read:** `GetObject` on `wx/cref/latest`, whose `epoch` must equal the newest frame epoch
in the listing. The same rule as frozen, through the same API. **One registered race:** the fetch follows the listing
by under a second. If a frame lands in between, `latest` is newer than the listing's newest. That case re-runs the
control once, and the race is recorded. **A second mismatch is outcome (c).**

**Unchanged:** the two reads at least 10 min apart, the 31 objects, the byte band
[385,353,750, 385,353,850], outcomes (a)-(c), and the analytics reported beside them as data, not graded.

### S2's results (deployed 2026-10-23; main `4752767`)

**Staging** 14:48Z, version `8d935932`. **Production** 14:49:21-14:49:38Z, version
`e31772f4-7b27-45dd-9132-238ed2f085e1`. The rollback target was `7224e0dc` (`2460deb`).

| | read | verdict |
|---|---|---|
| **P1** | production `/healthz`: `commit` **4752767**, `wx` **"not_configured"**; staging the same | **as predicted** (as restated) |
| **P2** (the hard gate) | production's deploy output: `BEND_SNAPSHOT (BendSnapshotDO)`, **`SITE_FEED_BEND ("off")`**, no `WX`; staging the same with `("on")` | **as predicted** |
| **P3 after** | `smoke-prod.sh`, production, E2:08's credential: **PASS 34, FAIL 0, SKIPPED 0**; `/wx` HTTP 503 `not_configured`, "not wired yet" | **as predicted** |
| **P4** | staging: PASS 21, FAIL 10, SKIPPED 3. Every FAIL is a **401**: staging holds a different device-key secret | reported, not graded |
| **P6** | fleet `[14:49:38, 15:04:38)`, read at 15:08:30 and 15:10:33Z, identical: **446 requests, 5 devices, every one 200** | **as predicted** |
| **P5** | A = `[14:54:38, 15:54:38)`, read at 16:00:05 and 16:02:54Z, **identical in every field**; B = `[13:49:38, 14:49:38)`. Fleet 5xx in A **0**, 4xx in A **0**. Devices in B but not A **0** (B 5, A 6). `/blips` A/B 1,392 / 1,092 = **1.27**. Usage rows in A from **6 devices, all `u8`, 0 `(none)`**. B's 5 rows were all `(none)`, since arity was recorded only once #450 deployed | **as predicted** on every graded row |
| **P5, `sitefeed`** | `{}` in A | **unreadable, not passed** (control (a) BLIND; Daniel's decision) |
| **W1** (S3's dry-runs, read 2026-10-23 on `443d579`) | both envs list exactly S2's bindings **plus `env.WX (blipscope-wx)`**: none removed. Staging 16 → 17, production 17 → 18 | **as predicted** |

### I5′'s result (2026-10-23): a MISS, recorded as registered

**The read, verbatim:** `{"at": "15:27:36Z", "pages": 1, "objects": 30, "bytes": 372508661, "frames": 29, "other_keys":
["wx/cref/latest"], "oldest_frame_age_s": 3902, "newest_frame_age_s": 423, "newest_frame_epoch": 1792768833, "control":
{"latest_epoch": 1792768833, "equals_newest_listed": true}}`

| | read | verdict |
|---|---|---|
| **control** | `latest`'s epoch equals the newest listed frame (1792768833, 15:20:33Z) | **passed** |
| **I5′** | **29 frames** + `latest` = 30 objects; **oldest frame 3,902 s**; 372,508,661 B, which is exactly 29 × 12,845,120 + a 181 B `latest` | **MISS: outcome (c)**, on two clauses (under 30 frames; a frame older than 3,720 s). **Stopped there:** no second read was taken |

**It stays a miss.** The reviewer ruled (2026-10-23, pasted by Daniel) that it is recorded verbatim, with no re-grading.

**The cause is not yet established.** It counts only once both numbers are reconciled with the ingest's logs: each run's
outcome from 15:20 to 15:34Z, and whether the delete step ran. **What is established so far:**
- **The ingest's NOAA calls were failing.** A tail from 15:32:27Z saw the runs scheduled 15:29-15:33Z. Their checks gave
  `NOAA list 522`, `525`, `520` and "Network connection lost", each after 39-177 s. Then `stored` at the 15:34 run (frame
  15:32:38).
- **NOAA itself was publishing.** Its listing at 15:28:23Z holds 15:22:38, 15:24:37 and 15:26:40, none of which the bucket had.
- **The code: when the delete runs.** `pruneOld` runs at the end of every check that gets past the NOAA listing (`stored`,
  `already-stored`, `lost-race`). A failed listing throws before it. Its cutoff is `t0 − 3,600 s`, where `t0` is
  `Date.now()` at the start of the check (`ingest-worker.ts:15`), not when it prunes.
- **So the 3,902 s frame (epoch 14:22:34Z) survives only if** no check that started after 15:22:34Z reached the delete before
  15:27:36Z.
- **R2's operation analytics for the bucket**, per minute, as data:
  - `ListObjects`, which only `pruneOld` issues in the ingest, appears at 15:24 and 15:29.
  - `PutObject` appears at the same minutes.
  - `DeleteObjects` appears at 15:21, then not until 15:32.
  - The 15:27 row (`GetObject` 1, `ListObjects` 1) is this read.
  - A put that failed partway, and a delete cut off by a start-time cutoff, are both consistent with this. **Only the
    logs separate them.**
- **The logs exist but are not yet read.** Workers Logs is on (`head_sampling_rate = 1`). The analytics token is refused
  by the telemetry API (403), so the runs from 15:20 to 15:29Z need a read token or the dashboard.

**Did anything request `/wx` while the newest frame was over 600 s old (15:30:33 to about 15:34Z)?** **No.**
- `/wx` is not in `KNOWN_ROUTES`, so its requests are recorded under `/other`. In that window `/other` has **0 points**
  in production and in staging.
- **The control:** the smoke runs' own `/wx` requests appear as production `/other` 503 at 14:49 and 16:03Z, and as
  staging 401 at 14:48Z.
- **What such a request would have got:** 503 `not_configured`. Neither env binds `WX` yet, so the designed `wx_stale`
  answer is unreachable until S3.

### I5″ (frozen 2026-10-23, before any read)

**The ruling (reviewer chat, pasted by Daniel):** register I5″ with I5′'s predicted set, frozen before reading, and
read it only after **at least 70 minutes with no ingest errors**. I5′'s miss stays in the record.

**The instrument:** as I5′'s, restated above: the S3-API listing and its `latest` control. That includes the
registered race, re-run once.

**The error-free window, observed, not assumed:**
- **The source:** a continuous `wrangler tail` of `blipscope-wx-ingest` (pretty format), recorded to a file.
- **The window:** 70 minutes or more in which the tail holds **0 `wx_ingest_error` lines**.
- **The tail's own control:** it prints its connection banner, and it records **at least one `wx_ingest` or
  `wx_ingest_error` line in every 5-minute span** of the window. A 5-minute span with no line means the tail cannot
  see the runs. Then the window does not count: it restarts with a new tail.
- **Any error resets it:** a `wx_ingest_error` restarts the 70 minutes.

**The reads:** two, at least 10 min apart, both inside or at the end of an error-free window of 70 minutes or more.

| | predicted |
|---|---|
| **I5″** | **31 objects in both reads:** 30 under `wx/cref/frames/` plus `wx/cref/latest`, and no other key. **Total bytes in [385,353,750, 385,353,850]** in both. The two reads agree on the count |

| outcome | meaning |
|---|---|
| **(a)** as predicted | pruning holds the bucket at one hour of frames |
| **(b)** 32 objects (31 frames + `latest`), every frame younger than 3,600 + 120 s | the read landed between a frame's put and its prune, or cadence jitter fitted 31 frames into the hour. **Not a pass:** reported, and the next step is decided then |
| **(c)** anything else | **stop and decide**. That covers: under 30 frames, over 32 objects, a frame older than 3,720 s, a key that is neither a frame nor `latest`, bytes outside the band for the frame count, reads that disagree, or the control failing |
| **(d)** no error-free 70 minutes within 24 h of this freeze | I5″ is not read. **The NOAA failures become their own finding** |

### Rulings of 2026-10-23 (reviewer chat, pasted by Daniel), recorded before any I5″ read

**S3 (#461), as given:** "approved to merge at 426a02b5a219098479c37a5f7da1f52d0b365f65". Conditions: the CI total on
that exact head with no failures; a clean squash; no --admin. "Put any further results in a follow-up docs PR, not on
this branch, so the approved head stays the head. Then deploy S3 as frozen: staging, then production; W1-W6; on an
escape, roll production back first. Start each deploy when the ingest tail shows the latest runs storing. If W2 still
misses because of a NOAA gap, it's a miss: roll back, record it and report it, with no re-grading."

**Merged** as `91554c8`: 37/37 checks counted on `426a02b`, 0 failures. The squash's tree equals `git merge-tree`'s
(`966740601c47`), and its patch-id equals the PR's.

**I5″'s window instrument, verbatim:** "I5″'s window instrument moves from wrangler tail to Workers Logs, frozen now,
before any I5″ read. A wrangler tail can't be relied on to stay connected for 70 minutes, and Workers Logs keeps every
run (head_sampling_rate = 1). The window is any 70 minutes in which Workers Logs holds 0 wx_ingest_error events for
blipscope-wx-ingest and at least one wx_ingest or wx_ingest_error event in every 5-minute span. The control: the same
query over 15:20-15:40Z must return the errors the tail saw then (NOAA list 520/522/525 and 'Network connection lost').
If it doesn't, the instrument is blind and no window counts. The reads, their spacing and the predicted set are
unchanged. The tail may keep running as a cross-check, and both tails' history is recorded as it happened."

**The query.** Workers Observability telemetry, `view: events`, filtered to `$metadata.service = blipscope-wx-ingest`.
Each log line is classified by its `evt`: `wx_ingest` or `wx_ingest_error`.

**The tails' history, as it happened:**
- **Tail 1:** connected 16:09:04Z and exited by itself at 16:39:39Z, exit 0, with nothing on stderr. It recorded 28
  invocations, 51 `wx_ingest` and 5 `wx_ingest_error`. The errors came from the runs at 16:07, 16:08, 16:09 and
  16:13Z (wrangler prints run times in local time, UTC-7).
- **Tail 2:** connected 16:40:40Z, and is still running as a cross-check.

**The NOAA-resilience change** (ruled the same day) restarts I5″'s 70-minute window on the new code once it is
deployed. Its predictions stay unchanged.

### I5′'s reconciliation, from Workers Logs (read 2026-10-23, 16:45-16:50Z)

**The token works.** A read-only Workers Observability token (`observability-read`) answered the telemetry query for
`blipscope-wx-ingest`. Every check's start below is the log time minus its `ms`, so it is `t0`, the time the check's
prune cutoff comes from.

**The 3,902 s frame, reconciled.** Its epoch is 15:27:36 − 3,902 s = **14:22:34Z**.
- **The only logged success that reached the delete step before the read** was the check that started **15:22:11**. It
  finished at 15:24:56, stored frame 15:20:33 (the newest in the listing), and logged **`pruned 0`**.
- **Its cutoff was 15:22:11 − 3,600 s = 14:22:11,** 23 s before the frame's epoch, so the frame was rightly kept.
- **Every logged check that started after 15:22:34 and ended before 15:27:36 failed at the NOAA listing**, so its
  delete never ran. They started at 15:23:56 (`522`), 15:24:06 (`522`), 15:24:53 (`525`), 15:24:56 (`525`),
  15:26:46 (`525`) and 15:27:04 (`525`).
- **The next success started 15:28:14** and pruned 3, after the read.
- **Independently of the logs:** the listing at 15:27:36 still held the 14:22:34 frame, so no delete with a later cutoff
  ran before it. R2's analytics show no `DeleteObjects` from 15:22 to 15:31.

**The 29 frames: partly reconciled.**
- **What is established:** NOAA's own listing (read 15:28:23Z) holds **30 frames** from 14:22:34 to 15:20:33, with no
  step over 150 s. The bucket held **29** of them, so **exactly one NOAA frame was never stored.**
- **Not established:** which frame it was, and why. The ingest stores only the newest NOAA frame on each successful
  check, so a frame is lost when no check succeeds while it is the newest. **Workers Logs can't name it:** it shows
  `stored` for only 22 of the 30, while the bucket proves 29 were stored. See the next section.

**The new instrument's control, as frozen: passed.** Workers Logs over 15:20-15:40Z returns every error the tail saw
then, matching to the millisecond:
- `NOAA list 522`: 49,570, 39,201 and 49,058 ms;
- `NOAA list 525`: 137,571 and 56,857 ms;
- `NOAA list 520`: 72,656 ms;
- "Network connection lost": 176,749 ms.

In all, that window holds **25 `wx_ingest_error` and 19 `wx_ingest`** lines.

### Workers Logs drops ingest events: I5″'s window instrument fails a ground-truth check (outside the set)

**A diagnostic listing,** at 16:48:55Z and **not an I5″ read**, was taken to check the instrument against the bucket:
- **The bucket:** 24 frames, 15:48:40 to 16:46:39.
- **Workers Logs has a `stored` line for 20 of them.** None for 15:52:40, 15:54:36, 16:22:29 or 16:36:38.
- **Tail 1 recorded `stored` for both 16:22:29 and 16:36:38**, along with the `already-stored` checks after each.
  Over 16:09-16:39 it recorded 28 invocations, and Workers Logs holds 26.
- **The same holds earlier:** 22 `stored` lines for the 29 frames the bucket proves were stored between 14:22:34 and
  15:20:33. That is 48 invocations logged in 70 minutes of a once-a-minute cron.
- **Not the query:** re-querying two 10-minute slices on their own returned exactly the counts the 70-minute query gave
  for them (20 and 12 events).

**So Workers Logs is not complete for this Worker,** despite `head_sampling_rate = 1`. One cause consistent with this is
the account's daily Workers Logs limit. The proxy also logs every request at 100 %, and logs past the limit are
sampled. **Not established.**

**What it means for I5″.** The ruled control (the 15:20-15:40Z errors) **passed**, but the instrument drops events. A
70-minute span with 0 `wx_ingest_error` in Workers Logs can't tell "no errors" from "errors not kept". That is the
escape hatch: **the instrument is unreliable for this question. Stop and decide.**
- **No I5″ window is counted** on Workers Logs until this is ruled.
- **Tail 2** (connected 16:40:40Z) keeps running as the cross-check the ruling allowed.
