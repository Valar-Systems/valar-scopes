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
| I5, I6, I7 | still being read | — |
| **P3 (before)** | `smoke-prod.sh` against production `2460deb`, E2:08's credential: **PASS 31, FAIL 2, SKIPPED 1**. FAILs: `/wx` (404, as predicted) and "enroll POST, unsolved token" (403 `fake_device_id` against an expected `"unverified"`; not predicted) | **MISS. S2 stopped before deploying, for a ruling** (#461) |

**I3's miss, and why it is recorded rather than re-graded.** MRMS frame times are not on an exact 120 s grid. The two frames here are 00:18:37 and 00:20:39. The figure I3 leaned on was M4a's "mode 120 s", which describes the most common step, not a fixed one. So I3 was frozen with a criterion tighter than anything measured. The frame does advance, which is what I3 was for. The clause stays a miss.

**P3's miss.** The second FAIL is pre-existing. Since `e4c768a` (2026-09-23), production refuses fake device ids before the token gate, and that check's fixture id `a1b2c3d4e5f6a7b8` is on the fake allowlist. The identical FAIL is in that day's 16:08 production baseline, taken with the old credential. Staging passes the check. The options are on #461.
