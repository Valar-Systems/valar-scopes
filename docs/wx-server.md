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

**Outside the set:** any reading not listed above means stop. Code, tests and bars are not edited to
make a reading come true. The raw reading goes to the reviewer, and the next step is decided then.
