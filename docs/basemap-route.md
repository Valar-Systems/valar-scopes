# The basemap route: the server side of BMPK v1 (plan, for review before any code)

**Status: a plan. Nothing here is built, created in Cloudflare, or deployed.**
- **What it plans:** the ingest of free US map data, and a Worker route that serves one BMPK v1 package per location and radius ([basemap-package-format.md](basemap-package-format.md), frozen; on `weatherscope/stage3-5b-plan` at `cba3f9f`, not on main yet).
- **Asked for by:** the reviewer chat's brief of 2026-10-26, pasted by Daniel ("Brief: the map server").
- **Where it lives:** branch `feat/basemap-route`, cut from main `a6c2906`, pushed **without a PR**.
- **Who reads the packages:** Weatherscope first (r = 97 km, ε = 100.6 m, levels at N = 246). The Blipscope adopts the route after its v19, as a setting.
- **Not in here:** any firmware, and the format doc itself. Where this plan meets something the format doc does not say, it is listed in §10 as a reading or in §11 as a question. Nothing here edits the format.

**Section 1 is the finding that has to be ruled on first.** The format's PROVENANCE rule and its 32 KB cap cannot both hold in county-dense states, even at Weatherscope's own radius. Everything else in this plan works whichever way that is ruled.

---

## 1. The finding: PROVENANCE alone can exceed the 32 KB cap

**The two rules, as frozen:**
- §7: for each source used, `source.<layer>`, `vintage.<layer>` and `sha256.<layer>`, **"one set per file"**.
- §6: **a hard cap of 32 KB for the non-terrain sections**. PROVENANCE is section 7, a non-terrain section, so it counts.

**Why they collide:** TIGER's `AREAWATER` and `LINEARWATER` are published **per county**, and Terrain Tiles are one file **per z9 tile**. So PROVENANCE grows with the number of counties under the square and with the number of tiles in the mosaic. Nothing in §6's drop policy can shrink it:
- doubling ε does not shorten it;
- dropping water rings does not shorten it;
- dropping roads does not shorten it.

**The line lengths, measured** (each line includes its `\n`; real TIGER URL shapes):

| one file's set | bytes |
|---|---|
| a county `AREAWATER` file (`source.water`, `vintage.water`, `sha256.water`) | 195 |
| a county `LINEARWATER` file | 202 |
| a state `PRISECROADS` file | 196 |
| one Terrarium z9 tile | 188 |
| a state GNIS file, the Gazetteer, SUB-EST2024 | about 180-200 each |
| the fixed lines (`format`, `builder`, credits, `processing.terrain`, `epsilon.m`, `dropped.*`) | about 500 |

**Estimated PROVENANCE size, reading "used" as every file read (§10, RD1 (a)).**
- **County counts** are state-average county areas over the square (`(s + √A)² / A`). The real counts come from the store manifest (MS0 below).
- **Tile counts** are for the level mosaic with its one-tile margin, or for the peaks' tiles alone without terrain.
- Centres are public airports from the cleared list (`proxy/test/fixtures/wx-airports.txt`), named by code only.

| centre | Weatherscope, r 97 + terrain | r 97, no terrain | Blipscope default, r 161 (100 mi) | Blipscope max, r 222 |
|---|---|---|---|---|
| Bend (the golden) | **14.5 KB** (11 counties, 49 tiles) | 10.5 KB | 19.5 KB | 31.6 KB |
| KGJT | 16.8 KB | 12.4 KB | 25.0 KB | 40.5 KB |
| KDEN | 16.4 KB | 12.4 KB | 25.0 KB | 40.5 KB |
| KMSP | 19.5 KB | 15.1 KB | 31.5 KB | 56.8 KB |
| KMSY | 18.3 KB | 15.0 KB | 33.0 KB | 57.6 KB |
| KBUF | 20.7 KB | 16.3 KB | 33.9 KB | 57.6 KB |
| KRDU | 25.0 KB | 20.9 KB | 47.1 KB | 81.2 KB |
| KATL | **30.4 KB** (52 counties, 49 tiles) | 24.7 KB | **58.6 KB** | 104.1 KB |

**What this means:**
- **Weatherscope at 97 km with terrain:** Atlanta's PROVENANCE leaves about 1.6 KB of the cap for every water ring, river and road. That package **cannot be served**. Raleigh leaves about 7 KB, which is less than the geometry is likely to need (§5.3).
- **The Blipscope at its default 100 mi:** four of the eight centres are over 32 KB **from PROVENANCE alone**, before any geometry, and a fifth (KMSP) is within 1.3 KB of it.
- **The terrain tiles add about 9 KB (49 × 188 B) to the capped part.** That surprised me: asking for terrain makes the *non-terrain* sections bigger.
- **The ~20 KB target** (A9.1 D) was set from the mockup's geometry, which carried no PROVENANCE.

**The alternative explanation, named:** that "used" means only files that contribute a feature (RD1 (b′)), which would make all of this small. **It does not make it small.**
- **What it removes:** counties that hold no kept water ring and no part of a top-5 river.
- **What it keeps:** every tile of a terrain mosaic, every tile holding a ranked summit, and every county a kept river passes through.
- **My estimate:** 40-70 % of each county term, with the tile terms unchanged. That still leaves Atlanta at about 19-25 KB at 97 km with terrain, and KATL and KRDU over 32 KB at 222 km from PROVENANCE alone.
- **That is an estimate, not a measurement.** MS0 measures both readings exactly.

**The options, for BQ1 (§11):**
- **(A) Keep v1 as frozen.** Rule RD1, accept `422 too_large` wherever it falls, and let MS0 map where that is. This is the cheapest option, and it fails much of the eastern US.
- **(B) Take PROVENANCE out of the 32 KB cap** and give it its own bound, for example 64 KB. No field changes meaning and no byte changes: this is a §6/§10 size rule.
  - Its cost is device memory: Weatherscope would accept up to 32 KB + 64 KB + terrain.
  - A client never has to *parse* PROVENANCE to draw. It only has to hold the bytes long enough to check the CRC.
- **(C) Version 2:** PROVENANCE carries one digest per layer over its files' (url, sha256) list, and the full list is published at a stable URL keyed by that digest. This is small and bounded, but every v1 reader refuses a version-2 package, Weatherscope's included.

**Recommended: (B).** It is the one that changes no byte Weatherscope's reader parses, and it keeps the package self-describing. Its bound should come from MS0's worst measured case, not from this estimate. **No route code depends on the choice:** the builder computes PROVENANCE from the manifest either way, so everything else in this plan can be built while it is ruled.

---

## 2. Architecture

**The shape:**

```
census.gov, USGS, AWS ──(offline ingest, a workstation script)──▶ R2 bucket blipscope-basemap: the store (read-only once sealed)
                                                                      │
device ──GET /api/v1/blipscope/basemap──▶ proxy (auth, rate limit, params, Cache API) ──service binding RPC──▶ builder Worker
                                                      ◀──────────── one validated package ◀───────────────────┘
```

### 2.1 Why three parts

- **The national sources are gigabytes; a request needs a square.** County files have to be fetched, filtered and indexed once, not per request. The ingest does that.
- **A cold build is too heavy for the proxy.**
  - It decodes up to about 49 terrain tiles, hillshades a mosaic of up to about 3.2 M pixels in double, and simplifies every raw vertex in the square. That is hundreds of ms of CPU.
  - The proxy runs at `cpu_ms = 200` as a deliberate runaway kill switch (wrangler.toml), and the A7 ruling kept heavy work out of it.
  - So the build runs in **its own Worker**, `blipscope-basemap`, with its own `cpu_ms`, reached only through a service binding. This follows the `blipscope-wx-ingest` precedent: a second Worker built from the same package, with the same tests and the same CI job.
  - **What it costs:** a service-binding call adds no request charge. CPU is billed across both Workers (Cloudflare pricing docs, read 2026-10-26).
- **Most requests are not builds.** A revalidation is answered from the ETag alone (§3), and a repeat of a recent request from the Cache API. Only a miss calls the builder.

**Alternatives named and set aside:**
- **Raising the proxy's `cpu_ms`:** this removes the kill switch from every route to serve one.
- **Precomputing packages:** about 15 M two-decimal US points, times every radius, ε and level set a client might send.
- **Precomputing the hillshade nationally:** `mpp` uses cos(lat0) of the *package* origin (format §5.6), so the shade is a function of the request.
- **A Durable Object:** nothing on the request path needs coordination or state.

### 2.2 The ingest (offline): `proxy/scripts/ingest-basemap.ts`

It runs on the workstation, as `ingest-routes.ts` and `ingest-photos.ts` do, and writes the store through the OAuth session. It does no request-time work and runs again only by a person's decision.

1. **Fetch** every source file into a directory outside every repo. Each file's sha256 is recorded as fetched, and files are named by their URL. **What it fetches:**
   - TIGER/Line 2024 `AREAWATER` and `LINEARWATER` for every county-equivalent of the 50 states + DC (3,144 each);
   - `PRISECROADS` for each of the 51;
   - GNIS Domestic Names per state;
   - the 2025 Gazetteer places, national;
   - SUB-EST2024;
   - `tl_2024_us_state` (coverage, §2.6);
   - Natural Earth admin-0 (public domain; the border gate, §2.7);
   - every Terrarium z9 tile the coverage area can need, with a two-tile margin.
2. **Filter only by attribute,** and only by **a superset** of §5's rules. The builder re-applies every rule, so the store can never be the only place a rule lives.
   - `AWATER ≥ 1,500,000`;
   - `FULLNAME` ending `" Riv"`;
   - `MTFCC ∈ {S1100, S1200}`, with the Bus rule **left to the builder**;
   - `feature_class = Summit`;
   - `LSAD = 25`, with the SUB-EST2024 population joined.
   - **No geometry is changed:** vertices are kept as the source's IEEE doubles, so the builder's arithmetic equals running on the original files.
3. **Index:** each feature goes into the 1° cell holding its bounding box's south-west corner. Each cell records its features' **reach** (their union box). **A rule frozen now:** a cell over 24 MB is split into quarters until none is. That bounds a request's peak memory by construction, not by hope.
4. **Coverage and the gate** (§2.6, §2.7), precomputed exactly.
5. **Write, then verify, then seal.** This is CLAUDE.md's KV bulk-put rule, applied to R2:
   - every object is put;
   - then a separate verifier enumerates the bucket and checks each object's size and sha256 against the manifest;
   - a `--force` repair re-puts any object that fails;
   - the verifier runs again;
   - only then is `bm/v1/current` written. **The writer's own "success" is not evidence.**

### 2.3 The store (R2 bucket `blipscope-basemap`, no lifecycle expiry)

```
bm/v1/<storeId>/manifest.json            every source file: layer, url, vintage, sha256, bytes; the ingest commit; the predicates applied
bm/v1/<storeId>/index.json               per cell: key, bytes, reach box
bm/v1/<storeId>/cells/<cell>.bin         the vector features of one cell (format below)
bm/v1/<storeId>/coverage.bin             one bit per two-decimal lattice point: inside the 50 states + DC
bm/v1/<storeId>/terrain/9/<x>/<y>.png    the Terrarium tile, byte for byte as fetched (so its sha256 is the file's)
bm/v1/<storeId>/terrain-gate.bin         one bit per mirrored tile: it meets land outside the 50 states + DC
bm/v1/current                            { "storeId": ... }, written last, only after verification
```

- **`storeId`** is the first 12 hex digits of the manifest's sha256. So a store is named by its content, and two ingests of the same files give the same id.
- **A feature record holds:**
  - its layer;
  - its **source-order key** (file index in the manifest, record index, part index), so the builder can reproduce "source order" across cells;
  - the attributes §5 reads (`AWATER`, `FULLNAME`, `MTFCC`, GNIS `feature_id` and name, Gazetteer `GEOID`, `NAME`, population);
  - its vertices as `f64` pairs.
- **Why a new bucket and not a prefix in `blipscope-wx`:** that bucket has a 1-day lifecycle expiry, which would delete the store every day.

### 2.4 The builder (per cold request): `proxy/src/basemap/`

**A pure function** `build(store, query) → Uint8Array`, with the store behind an interface. The same code runs in the Worker over R2 and in Node over a local directory, which is what makes `--check` and the golden row possible (§8). **In format order:**

1. **Coverage:** the origin's bit. Not covered → `no_coverage`, with no other read.
2. **Cells:** read the cells whose reach meets the square. Features are **streamed** one cell at a time; nothing but simplified output is kept. Then:
   - project to (E, S) in double (§2);
   - clip, with Liang–Barsky for lines and Sutherland–Hodgman for rings;
   - Douglas–Peucker at ε **and at 2ε in the same pass**, so §6's "double ε once" needs no second read;
   - quantise, then drop consecutive duplicates, counting collapses.
   - **At the end, everything is sorted by source-order key.**
3. **Rivers:** group by `FULLNAME`, measure each group's in-disc length on the raw clipped geometry, keep the 5 longest groups, and build the names table.
4. **Peaks:** the summits in the disc. Each one's elevation is the containing pixel of its unsmoothed tile. Then rank, de-duplicate at 3.0 km, keep 16.
5. **Places:** within R inclusive, in file order, with population.
6. **Terrain,** when asked:
   - **one mosaic** for the largest level plus its one-tile margin;
   - the **gate** (§2.7);
   - PNG decode, with zlib inflate in native code;
   - the hillshade in double, the 4 × 4 mean, and sampling per level.
   - **Why one mosaic serves every level:** the margin makes every sampled value independent of where the mosaic is cut. That is §5.6's own argument, and MT3 tests it.
7. **Assemble:**
   - sections in ascending type, then levels by ascending radius;
   - PROVENANCE;
   - the CRC;
   - **§6's budget loop**, using the 2ε results already computed, then the drops in order, each counted.
8. **Validate** with the same reader rules a client uses (§8 MV). **A package the validator refuses is never served:** the route answers `500 builder_invalid`.
- **The builder refuses to build** when its `BUILD_COMMIT` is `UNSTAMPED`. PROVENANCE's `builder` line would otherwise name nothing, which wrangler.toml already treats as red.

### 2.5 The proxy route: `proxy/src/basemap/handler.ts`

It does auth, rate limiting, parameter validation and normalisation, the ETag and 304 check, the Cache API lookup, the builder call, and validation before serving.
- **The same auth and per-key rate limit as `/wx`**, and the same dispatcher. So the deprecated `/v1/basemap` alias exists automatically, and the alias tests gain a row for it.
- **The origin is quantised by importing `crop.ts`'s `quantise`, not by a second implementation.** Format §2 says the origin is quantised "the same quantisation as `/wx`", and Weatherscope's §9 shift depends on the crop's centre coming from the same 2-dp point.
  - **A sweep proves it** (MQ3): for random inputs and every half-way case, the package origin equals `/wx`'s quantised point.
  - **Named and set aside:** a conflict with format §1's `floor(v + 0.5)` rule, where v = x·100.
    - For |v| ≥ 1, `Math.round(v)` and `floor(v + 0.5)` agree in IEEE double. There `v + 0.5` is either exact, or rounds into the next binade without reaching or leaving an integer.
    - They differ only for v just under 0.5, that is |lat| or |lon| under 0.01°, which is outside the US.
    - MQ3 asserts the agreement over the US boxes rather than trusting this argument.

### 2.6 Coverage: v1 is the 50 states + DC

- **The test:** the origin's two-decimal point is inside a `tl_2024_us_state` polygon.
  - TIGER/Line's state polygons, unlike the cartographic boundary files, are not clipped to the shoreline. So a coastal home whose point rounds into a bay should still be covered.
  - **That is a claim about the data,** so MC carries a coastal cleared airport as a control row.
- **It is precomputed exactly** for every two-decimal lattice point in the US boxes, so the request-time test is one bit, and exact for every quantised origin.
- **Outside it:** `404 no_coverage` (§3). Never an empty package.
- **Puerto Rico and the island areas:** `no_coverage` in v1 (BQ4). §5.5's LSAD 25 rule finds no places there, and v1 has no national place rule (Q27).
- **A square partly outside the US** carries US features only: Canada or Mexico is empty, not marked. That is the sources' limit. BQ5 asks whether a later section type should mark it.

### 2.7 Terrain sources, read for this plan, and the border gate

**What I read (2026-10-26):** tilezen/joerd at `0b86765156d0612d837548c2cf70376c43b3405c`.
- **`docs/attribution.md`:** the US sources (3DEP, SRTM, GMTED2010) are public domain with a credit asked for. The credit lines are "3DEP data courtesy of the U.S. Geological Survey", "SRTM data courtesy of the U.S. Geological Survey" and "GMTED2010 data courtesy of the U.S. Geological Survey". GMTED2010 requires a description of modifications.
- **`docs/data-sources.md`, "Data sources per zoom", the row for zoom 9, verbatim:**

  > **9** | `ETOPO1` | `SRTM`, `NRCAN` in Canada, `EUDEM` in Europe, with `GMTED` in high latitudes above 60°

**What follows from that row:**
- **At z9, US land is SRTM** (GMTED above 60° N, so most of Alaska), and the ocean is **ETOPO1** (NOAA).
- **3DEP first appears at zoom 10.** So the ruled credit set (3DEP, SRTM, GMTED2010) credits a source that z9 does not use, and omits ETOPO1, which every coastal mosaic contains. That is BQ2.
- **Canada at z9 is NRCAN** (Open Government Licence – Canada).
- **Mexico at z9 is SRTM:** INEGI appears only from zoom 10.

**The gate, v1:** terrain is served only when **no tile of the mosaic** (with its margin) meets land outside the 50 states + DC.
- **The test:** each mirrored tile's footprint against Natural Earth admin-0, buffered 5 km for its generalisation, precomputed into `terrain-gate.bin`.
- **Otherwise:** `422 terrain_unavailable`, and the client asks again without terrain.
- **This is conservative near Mexico** (SRTM there is US public domain). BQ3 asks whether to gate only on Canada.

---

## 3. The request and the response (format §10, decided here)

`GET /api/v1/blipscope/basemap?lat=&lon=&r=&eps=&layers=&levels=`

| parameter | rule | refused as |
|---|---|---|
| `lat`, `lon` | as `/wx`: finite, \|lat\| ≤ 90, \|lon\| ≤ 180; quantised by `crop.ts`'s `quantise` | `400 bad_lat` / `bad_lon` |
| `r` | `quantise`'s whole km, `max(1, round(r))`; at most **222** (`MAX_DEGREES × KM_PER_DEGREE`, parsed from `src/AircraftManager.cpp` as WS4 parses them, with a BLIND control) | `400 bad_r` |
| `eps` | **integer millimetres**, 1,000 to 10,000,000. Weatherscope sends `eps=100584` (BQ6: metres cannot carry the doc's own `toleranceMm` 100,584) | `400 bad_eps` |
| `layers` | comma list from `water,rivers,roads,peaks,places,terrain`; at least one; no repeats; any order (normalised to a mask) | `400 bad_layers` |
| `levels` | with `terrain` only, and required with it: 1-4 `radiusMm:N` pairs, N 1-1,024, `radiusMm` 1,000,000-250,000,000, no repeats, total `Σ(8 + N²)` ≤ 2,097,152; normalised to ascending radius | `400 bad_levels` |

**The normalised query** is `(lat2, lon2, rKm, epsMm, mask, levels)`. Unknown parameters are ignored, as `/wx` ignores them, and never enter a key.

**The answers:**

| case | answer | builder calls |
|---|---|---|
| no `BASEMAP` service binding | `503 not_configured` | 0 |
| a bad parameter | `400 bad_*` | 0 |
| unauthenticated / fake id / rate limited | `401` / `403` / `429`, exactly as `/wx` | 0 |
| `If-None-Match` equals the ETag | **`304`**, no body | 0 |
| a Cache API hit | `200`, the cached package | 0 |
| origin outside v1 coverage | `404 no_coverage` (BQ7) | 1 (one bit read) |
| terrain asked, the gate refuses | `422 terrain_unavailable` | 1 |
| still over the cap after §6's drops | `422 too_large` | 1 |
| no sealed store, or a store read failed | `503 basemap_unavailable` | 1 |
| the built package fails validation | `500 builder_invalid`, never the bytes | 1 |
| otherwise | `200 application/octet-stream`, one package, `ETag`, `Cache-Control: no-cache` | 1 |

**The ETag is computed from the inputs, not the output** (BQ8):
- **Its form:** `"bm1-<rev>-<storeId>-<digest>"`, where `digest` is 24 hex digits of the normalised query's sha256. **24 hex, never 16:** a 16-hex string trips the repo's device-id hook.
- **Why:** a revalidation is answered with no build, no store read and no CPU to speak of. §10's "ETag = crc32" would need the package to answer a 304.
- **Its risk:** builder output changing without the ETag changing. Under it, a device would keep an old package forever.
- **The guard, which runs:** `rev` is an integer in the builder. A committed table maps each `rev` to the sha256 of the builder's output on three synthetic stores. CI fails if those outputs change while `rev` does not.

**The Cache API key** is `https://basemap-cache.internal/<the same digest>`: per colo, evictable, and never enumerated.

---

## 4. Privacy, as `/wx`

- **The location is quantised to 2 dp** by `/wx`'s own function, before anything else reads it.
- **Nothing is stored per device, or per request.** The request path makes **no R2, KV or Durable Object write**. MP2 counts writes through wrapped bindings, as WS6 counted reads, and predicts **0**.
  - The only thing the route writes is the per-colo Cache API entry. It is ephemeral and evictable, it cannot be listed, and its key is a digest, not a coordinate.
  - **A durable R2 package cache is named and set aside:** it would be an enumerable list of the 1 km cells customers live in, and the package header carries the origin, so hashing the key would not help.
- **Our own log lines** (`evt:"basemap"`) carry the status, HIT or MISS, sizes, CPU, the mask, `r`, `epsMm` and the drop counts. They carry **no lat, lon or digest**. The Analytics Engine point carries counts only.
  - **MP1** runs a request and asserts that no log line contains the quantised lat or lon string.
  - **Its control:** a planted `console.log(lat2)` must turn it red.
- **The ETag is not a privacy mechanism, and is not claimed as one.** The two-decimal lattice is small enough to brute-force a hash of it. The digest is there so that headers and cache keys never carry a coordinate as text.
- **One exposure this route does not add but should not hide:** Workers Logs' invocation record carries the request URL, and so `lat`/`lon` at 2 dp. That is true of `/blips` and `/wx` today. It is fleet-wide and not this route's to change (BQ12).
- **Tests and tools:**
  - centres are public airports from the cleared list, named by code;
  - a new centre is admitted only by `forbidden.py`;
  - the Bend golden's centre exists only inside its package header;
  - no script prints a coordinate.

---

## 5. Sizes

### 5.1 The byte model

| item | bytes |
|---|---|
| a varint / svarint | 1 below 128 / ±64; 2 below 16,384 / ±8,192; 3 above |
| a polyline's first vertex (absolute q, mostly over ±8,192) | 6 |
| a later vertex, a delta of a few hundred units | about 4 |
| a river polyline's overhead (name index, n, first vertex) | about 8 |
| a road polyline's overhead (class, n, first vertex) | about 8 |
| a peak (q, elevation, a name of about 15) | about 25 |
| a place (q, population, a name of about 12) | about 22 |

**A unit is R / 32,767:** 2.96 m at 97 km, 4.91 m at 161 km, 6.78 m at 222 km.

**The structural floor matters more than ε.**
- TIGER splits a river into edges wherever any other TIGER edge meets it (a road, a boundary, another stream). That is my understanding of TIGER's topology; the ingest counts the edges.
- Douglas–Peucker keeps both ends of every edge.
- So each edge costs at least about 12 B **whatever ε is**, and a larger ε does not remove the floor.

### 5.2 PROVENANCE

See §1's table. **MS0 replaces it with exact numbers before any builder code runs:**
- PROVENANCE's bytes are a function of the manifest and the files a square touches, with no geometry involved;
- a 30-line script over the sealed store computes them for every centre and radius in §5.4, under both RD1 readings;
- the results are frozen as MS0's values, before the builder exists.

### 5.3 Geometry: estimates now, bands frozen later

**Weatherscope's Bend request, estimated** (r 97, ε 100.6 m):

| layer | bytes |
|---|---|
| water | 3-8 KB |
| rivers | 5-15 KB (the 5 groups' edges at the 12 B floor, plus meanders) |
| roads | 3-8 KB |
| peaks and places | about 0.6 KB |
| **total geometry** | **12-32 KB** |

With PROVENANCE at 14.5 KB (terrain on) or 10.5 KB (terrain off), Bend lands **between 20 and 32 KB, or in §6's drop policy**. That is wide because no source file has been counted yet.

**Why the geometry bands are frozen later:** the first anchor is Weatherscope's own `bend.bmpk`. Its section lengths are a measurement of the same layer rules. So the geometry bands for the other centres are frozen from Bend's measured bytes per km of river, per ring and per road, **before** the builder runs on any other centre.

### 5.4 Predicted outcomes at the two products' radii

**The Blipscope's ranges, read from firmware:**
- **The configured radius** is any value from 0.1 to 222 km (`ConfigurationWebServer.cpp`: `max="222"`, `MAX_KM = 222`). The default is **100 mi**, which is 161 km on the wire (`lround`).
- **The zoom ladder** is {5, 10, 25, 50} below the configured radius, plus the radius itself (`include/RadarZoom.h`). It is a view scale only, so one package at the configured radius serves every zoom step.
- **ε** is the Blipscope's own choice at adoption. **These predictions use half a pixel at the configured radius on the 240 px Kit S3:** 671 m at 161 km, 925 m at 222 km.

**The outcome classes:**
- **F:** fits within 20 KB;
- **C:** inside the 32 KB cap;
- **D:** over the cap, brought under it by §6's drops (counted);
- **X:** `422 too_large`.

**Predicted under RD1 (a), with peaks on.** These are graded once the builder runs on the sealed store:

| centre | r 97 + terrain (Weatherscope) | r 97 | r 161 | r 222 |
|---|---|---|---|---|
| Bend (golden) | C or D | C or D | D or X | X |
| KGJT, KDEN | C or D | C or D | D or X | X |
| KMSP, KMSY | D or X | D or X | X | X |
| KBUF | **`terrain_unavailable`** (Canada in the mosaic) | D or X | X | X |
| KRDU | X | D or X | X | X |
| KATL | **X** | X | X | X |

**The outside-the-set branch, pre-registered:** an outcome outside its cell means **the size model is wrong, so stop and report.** A cell is never re-registered without a ruling, and a miss stays recorded.

**If BQ1 is ruled (B)** (PROVENANCE outside the cap), this table is re-predicted from MS0 and the Bend anchor before the builder runs, and the new table is frozen in the PR.

---

## 6. Timings

Bands, graded at the stage named. The outside-the-set rule applies to each.

| | measured where | predicted |
|---|---|---|
| **MM1** cold build, Bend golden inputs, three levels | local Node, the WS8 meters (`process.cpuUsage`, the forced-GC live peak), 3 runs, the largest | CPU **≤ 1,500 ms**; live peak **≤ 96 MB** |
| **MM2** cold build, KDEN r 97 + terrain | the staging builder, Workers Logs `cpuTime` | CPU **≤ 3,000 ms**; wall ≤ 8 s |
| **MM3** cold build, KATL r 222, no terrain (the vertex-heaviest square in the sample) | local Node | printed, not graded. **It sets the builder's `cpu_ms`** at 10 × the measured CPU, capped at 30,000 |
| **MM4** a Cache API hit | the staging proxy | proxy CPU **≤ 10 ms**; 0 builder calls |
| **MM5** a 304 | the staging proxy | proxy CPU **≤ 5 ms**; 0 builder calls, 0 store reads |
| **MM6** the service binding does not inherit the proxy's limit | staging: one build whose builder CPU is over 200 ms, through the proxy | **200**. An exceeded-CPU outcome on either Worker means the builder is capped by the proxy's 200 ms, so stop |

**The reasons behind MM1:** WS8 decoded a full CONUS frame in 0.45-0.67 s. A Bend mosaic is about 3.2 M pixels of trigonometry in double, plus about 10⁵ raw vertices.

---

## 7. Cloudflare resources and costs

**What it would create.** Each is announced to Daniel before it exists, with its expected monthly cost, as `/wx`'s deploy did:
- **the R2 bucket `blipscope-basemap`,** with **no** lifecycle expiry: the store must persist;
- **the Worker `blipscope-basemap`,** in staging and production envs, with **no route**;
- **a `BASEMAP` service binding** in the proxy's staging and production envs.

**Expected monthly cost: $0.** The "now" column is `docs/wx-server.md`'s 2026-10-21 account figures plus the weather server:

| | now | the basemap adds | included |
|---|---|---|---|
| R2 storage | about 0.7 GB (Missileer 0.26, weather 0.4) | the store: vectors **1-4 GB** (raw doubles, measured by the ingest's dry run before upload) + about 5,000-7,000 tiles at about 110 KB each, **0.6-0.8 GB** | 10 GB |
| R2 Class A | about 880k/month (about 88 % of the free 1M, weather included) | about 10k **once** per ingest; **0** per request | 1M |
| R2 Class B | about 0.75M/month | 2-140 per cold build (cells + tiles); 1,000 cold builds/month is at most 0.14M | 10M |
| Workers requests | 0.7M/month | a device's boot fetch + revalidations: negligible | 10M |
| Workers CPU | about 12-17M ms/month with the weather ingest | 0.3-3 s per cold build; 1,000 cold builds is at most 3M | 30M |

**What could add a bill, each flagged:**
1. **R2 storage over 10 GB.** This happens only if the weather pruning fails (9.25 GB worst case under its lifecycle rule) **and** the store is at the top of its band. The excess would be cents: $0.015 per GB-month.
2. **R2 Class A in an ingest month.** The weather ingest is already at about 88 %. A full ingest adds about 1 %. The crossing comes from more Missileer writes, not from this, but it is the month to watch.
3. **Workers CPU, if a client churned its parameters.** About 10,000 distinct cold builds a month at 3 s each would add 30M ms. The per-key rate limit and the Cache API bound it, and MW's endpoint test shows a 304 costs no build.
- **No plan upgrade is needed.** The account is on Workers Paid: 5 min CPU per request available, 128 MB per isolate, 10,000 subrequests (Cloudflare docs, read 2026-10-26).

---

## 8. The checks

**These are families with their sabotages.** The exact truth tables are frozen in the PR **before** the code they grade, as A10.11 did. Every check has a sabotage that must turn it red. **BLIND or unreadable counts as FAIL.**

| family | what it grades | its sabotage (must go red) |
|---|---|---|
| **MQ** the request | every `400` case; permuted `layers` and `levels` give one ETag; `r` ≤ 222 parsed from firmware (BLIND if the parser does not find exactly the two constants); **MQ3:** the origin equals `/wx`'s quantised point over 10⁵ random inputs and every half-way case | MQ3: a local `Math.round` copy that rounds half to even; MQ3 must fail |
| **MV** the validator: **every §8 refusal from its own negative package** | `validate()` returns A10.11's named reason for each of the **28 PK fixtures** (`test/fixtures/wx/basemap/*.bmpk`, made by `make_package.py --synthetic`), in A10.11's pinned order: `syn-min` and `syn-unknown` accepted, each `neg-*` refused for its one reason | **S-VCRC:** the CRC check skipped, so exactly {PK-crc} is accepted. **S-VSWAP:** the overlap and gap reasons swapped, so exactly {PK-overlap, PK-gap} report the wrong reason |
| **ME** the encoder | **ME1:** my encoder, given `syn-min`'s described contents, writes **byte-identical** `syn-min.bmpk`. No trig is involved, so cross-language byte equality is expected here, unlike §11's golden row. **ME2:** 1,000 builds over random synthetic stores → 0 validator refusals. **ME3:** a vertex at exactly ±32,767 encodes; a builder that would emit 32,768 **throws**, never clamps | ME3: the clip disabled must throw `vertex_range`, not return a package |
| **ML** the layer rules (§5) | water at `AWATER` 1,499,999 dropped and 1,500,000 kept; `" Riv"` by suffix; top-5 groups by in-disc length with a tie; **Bus**: the doc's three examples, plus "Busby Rd" kept, "BUS" kept (case), "Bus" alone dropped, a missing `FULLNAME` kept; peaks ranked by DEM, ties by `feature_id`, the 3.0 km de-dup, 16 kept; places at exactly R kept, a missing population is 0 | **S-BUSSUB:** a substring test, so "President George Bush Tpke" and "Busby Rd" drop |
| **MB** §6's budget | a synthetic over-cap store: ε doubled once; then water smallest-area first; then secondary roads shortest first; each counted in `dropped.*`; still over → `422 too_large` | **S-DROPORDER:** largest first. **S-NOCOUNT:** a drop not counted |
| **MT** terrain | a plane DEM gives the shade worked out by hand; one-sided differences at the edge; the 4 × 4 mean aligned as `bendmock.py`'s [i, i + 3]; truncation sampling; `floor(shade·255 + 0.5)`. **MT3:** the same level from a mosaic one tile larger is **byte-identical** | MT3 with **no** margin: differences > 0 |
| **MD** determinism | **MD1:** the same request twice in one isolate is byte-identical. **MD2:** workerd (vitest-pool-workers) against Node is byte-identical; both are V8, so the same fdlibm `Math` (predicted; a difference stops and is reported). **MD3:** `scripts/basemap-check.ts <pkg>` reads the `builder` commit from PROVENANCE, checks out that commit in a temporary worktree, re-fetches each listed file and verifies its sha256, re-ingests and rebuilds from the header's origin, R and ε and the sections present, and exits 1 on any byte difference | MD3: a package with one byte changed and its CRC recomputed → exit 1. A commit or file it cannot get → exit 2 (BLIND), never 0 |
| **MG** **the golden row** (registered now) | **MG1:** my builder, fed **the source files named in `bend.bmpk`'s PROVENANCE** (fetched and sha256-verified) and the request read from its header and sections, **decodes equal** within format §11: places and peaks identical; every section the same item count; every line and ring the same vertex count, with each vertex within ±1 unit; terrain within ±1 with at most 0.1 % different, per level. **Graded when `bend.bmpk` exists; BLIND until then, which is not a pass; never skipped.** A failure caused by a §10 reading is reported with the reading named, and nothing is fixed without a ruling | **S-GEPS:** my builder at 1.5 ε must FAIL on vertex counts. **S-GSMOOTH:** no 4 × 4 mean must FAIL on > 0.1 %. **The control:** `bend` against itself → 0 differences |
| **MP** privacy | **MP1:** no log line carries the quantised lat or lon. **MP2:** **0** R2, KV or DO writes on the request path, counted through wrapped bindings. **MP3:** the ETag and the cache key contain no coordinate string | a planted `console.log(lat2)` turns MP1 red; a planted R2 put turns MP2 red |
| **MC** coverage and the gate | public airports in the US → covered; PHNL → covered; EGLL, and a cleared non-US centre → `no_coverage`; KBUF with terrain → `terrain_unavailable`, without terrain → 200 | **S-BBOX:** coverage built from the state polygons' bounding boxes. A centre inside New York's box but in Canada (cleared first) must then read as covered, and fail |
| **MW** the endpoint table (§3) | every row's status, body shape, builder-call count and store-read count, as WS6 did; unauthenticated → 401 before anything; `/v1/basemap` byte-identical to `/api/v1/blipscope/basemap`; a validator refusal → `500 builder_invalid`, never the bytes | a sabotaged builder writing road class 3 must produce `500`, not `200` |
| **MS** sizes | **MS0:** exact PROVENANCE bytes per centre and radius from the sealed manifest (frozen before the builder). **MS1:** §5.4's classes. Every size is printed per section | under BQ1 (A), **S-NOPROV:** a builder that leaves PROVENANCE out of the cap total must move Atlanta's r 97 + terrain from X to a lower class |

**Fixtures this plan depends on but does not make:** the 28 PK fixtures and `bend.bmpk` are Weatherscope's (A10.11, A10.2).
- MV and MG are written against them and run in `workers.yml`.
- **Until the fixtures exist on main, those rows read BLIND, and BLIND is a FAIL,** so the PR cannot read as fully green while they are absent. Their source-file list comes from `bend.bmpk`'s own PROVENANCE.

---

## 9. The deploy: staged, verified, with rollback conditions

Each stage is reviewed and merged before it deploys, as Daniel's standing deploy rule says. Anything with a cost above $0 is asked first. Its predictions are frozen in its PR.

| stage | what | verified by |
|---|---|---|
| **D0** | the code on main; the proxy deployed with **no `BASEMAP` binding** | `/basemap` answers `503 not_configured`; `/healthz` gains `basemap: "not_configured"`; `smoke-prod.sh` gains a `/basemap` check that passes on exactly `503 not_configured` or `200` with `BMPK` magic + ETag + `validate()` → `kOk`, and treats anything else as FAIL |
| **D1** | the bucket; the ingest; verify → repair → verify; seal `current` | the verifier's full enumeration: every object present with its sha256, 0 missing, read twice at an interval (the store is static). **The dry run's size is reported before any put; over 6 GB means stop** |
| **D2** | the builder Worker, staging then production, **no route**, deployed by a script that stamps `BUILD_COMMIT` as `deploy.sh` does | dry-runs show the `BASEMAP_STORE` R2 binding in both envs; nothing can reach it yet |
| **D3** | the proxy's `BASEMAP` binding, **staging** | **V1:** `/healthz` `basemap` = `{store, rev, commit}`, with the builder's commit. **V2:** smoke's `/basemap` passes "wired" at a cleared public centre. **V3:** the same request with `If-None-Match` → **304**. **V4:** EGLL → `404 no_coverage`. **V5:** MM2, MM4, MM5, MM6 |
| **D4** | the same binding, **production** | V1-V4 on production, then **V6:** the fleet's own traffic under P5's rules (B = 60 min before, A = 5-65 min after): fleet 5xx in A **0**; fleet 4xx **0**; devices in B but not A **0**; `/blips` A/B **0.4-2.5**; `/basemap` requests from fleet devices **0** (no shipped firmware asks), excluding the smoke's own |

**Rollback conditions,** each decided now:
- **Any fleet 5xx, or any fleet 401/403, in [T, T + 15 min) after D3 or D4:** roll the proxy back at once to the version recorded before the deploy.
  - **What rollback does:** `/basemap` returns to `503 not_configured`. The builder and the bucket are inert without the binding.
  - **Deleting either is Daniel's,** since it deletes a resource.
- **Any `500 builder_invalid`:** roll back and stop. The validator refused something the builder made, which is a finding.
- **Builder `exceededCpu` or `exceededMemory` > 0 on staging:** production is not deployed until it is explained.
- **Anything outside the predictions, or two reads of a window that disagree:** stop and decide. If production misbehaves, roll back first.

---

## 10. Readings the format doc leaves open (golden-row risks)

**Each of these changes bytes or vertex lists, so two correct builders can disagree on it,** and MG1 would then fail for a reason that is not a bug in either. My reading is given for each. **BQ9 asks that the host tool's readings be stated, through Daniel, or ruled, before either builder freezes them.**

| | the gap | my reading |
|---|---|---|
| **RD1** | §7 "each source used ... one set per file" | **(a)** every file read; **(b′)** files with a feature that passed §5's rules, before §6's drops. Recommended: **(b′)**. It is what "used" says, and a rebuild from those files alone is the same, because §6's drops are taken from features those files hold |
| **RD2** | "source order" across per-county files | files by URL ascending (so by GEOID), then record, then part |
| **RD3** | water: a polygon's inner rings. §5.1 says "each part as a ring", and a shapefile's parts include its holes | **dropped**, against the literal text. Holes are not represented (§4), and an island's ring filled as water draws the same pixels while costing bytes. **The literal reading keeps them,** and either is self-consistent |
| **RD4** | the order of `ROADS` items | source order, as RD2 |
| **RD5** | a line that leaves the square and re-enters | each in-square run is its own polyline, in along-line order |
| **RD6** | Douglas–Peucker on a closed ring | on the path v₀ … vₙ₋₁, v₀; the distance to a zero-length chord is the distance to its point |
| **RD7** | DP's distance, and the keep test | the perpendicular distance to the **segment** (clamped at the ends); kept when **> ε** |
| **RD8** | "in-frame length" (names table) against "inside the disc" (selection) | both are the raw clipped length inside the disc, in metres; ties broken by `FULLNAME` byte order |
| **RD9** | "within 3.0 km" / "within R" / "inside the disc" | ≤ in every case (§5.5 says inclusive once) |
| **RD10** | a summit's elevation | the Terrarium value of the z9 pixel containing it (truncation), then `floor(v + 0.5)` |
| **RD11** | a summit listed in two states' GNIS files | one summit per `feature_id` |
| **RD12** | coordinates read | GNIS `prim_lat_dec`/`prim_long_dec`; Gazetteer `INTPTLAT`/`INTPTLONG` |
| **RD13** | the `toleranceMm` header against `epsilon.m` after "double ε" | the header holds the ε **used**; `epsilon.m` the ε **requested**. So `--check` can rebuild the request |
| **RD14** | a requested layer with nothing in it | its section is present with `count` 0, so the layer mask can be read back from the package |
| **RD15** | `dropped.*` key names | `dropped.<layer>` for §6's drops and `collapsed.<layer>` for §2's. **If one key per layer is meant, say so** |
| **RD16** | the angles in the shade | 45° and 315° converted as `x * Math.PI / 180` |
| **RD17** | the terrain URL form in PROVENANCE | `https://s3.amazonaws.com/elevation-tiles-prod/terrarium/9/<x>/<y>.png` |

---

## 11. Questions, each with a recommendation

- **BQ1, PROVENANCE and the cap (§1).** Recommended: **(B)**, PROVENANCE outside the 32 KB cap with its own bound, set from MS0's worst measured case. **Ruled before the size predictions (§5.4) are re-frozen.**
- **BQ2, the terrain credits at z9 (§2.7).** joerd's own z9 row names SRTM, GMTED (above 60° N) and ETOPO1, not 3DEP. Recommended:
  - carry SRTM's and GMTED2010's lines;
  - **add ETOPO1's** NOAA note on any mosaic with an ocean pixel;
  - drop 3DEP's, since crediting a source the data does not contain is a false statement.
  - **This touches Weatherscope's stored Bend package too.** Bend is inland, so ETOPO1 does not arise there; the 3DEP line does.
- **BQ3, the border gate.** Recommended: refuse terrain where the mosaic meets any land outside the 50 states + DC. **The alternative** gates only on Canada (NRCAN at z9) and serves near Mexico (SRTM at z9).
- **BQ4, coverage.** Recommended: the 50 states + DC in v1; Puerto Rico and the island areas `no_coverage` until a national place rule (v2).
- **BQ5, partial squares at a border.** Recommended: v1 serves US features only, and the PR says so. A later section type could carry a coverage outline; v1 clients would skip it, so that is not a version change.
- **BQ6, `eps` in millimetres.** Recommended: an integer `eps` in mm. "100.6 m" cannot carry the doc's own `toleranceMm` of 100,584, and format §2 already insists that R be "that integer, never the float it came from".
- **BQ7, the no-coverage answer.** Recommended: `404 no_coverage`, a 4xx so a device does not retry. **The alternative,** `422`, puts it beside `too_large`.
- **BQ8, the ETag.** Recommended: built from the inputs plus `rev`, with the CI guard (§3), so a 304 costs no build. **The alternative** is §10's crc32, which needs a cached package or a rebuild to answer a 304.
- **BQ9, the open readings (§10).** Recommended: the host tool's readings of RD1-RD17 are stated, through Daniel, before MG1's code is frozen. Any disagreement is ruled once, for both builders.
- **BQ10, the Blipscope's package radius.** At r 161-222 most of the sample is `too_large` under (A), and at r 222 under any reading. Recommended: the route accepts r ≤ 222 as the radar does. **Whether the Blipscope asks for its configured radius or a smaller basemap radius is blipscope-5c's adoption decision.** This plan only reports what each radius costs.
- **BQ11, where the build runs.** Recommended: its own Worker behind a service binding, **subject to MM6.** If MM6 shows the builder inherits the proxy's 200 ms, I stop and report; I do not raise the proxy's limit.
- **BQ12, request URLs in Workers Logs.** Recommended: no change in this route (§4). It is the same for `/blips` and `/wx`, and a fleet-wide question.
- **BQ13, where the ingest runs.** Recommended: the workstation, as the route and photo ingests do, with the KV-style verify → repair → verify → seal. A GitHub Action would add a CI credential with R2 write access.

---

## 12. Order of work, once ruled

1. **The rulings on BQ1-BQ13, and RD1-RD17 aligned.** Then the truth tables for MQ, MV, ME, ML, MB, MT, MD, MG, MP, MC and MW, frozen in the PR, **before any code**.
2. **The ingest's fetch and dry run:** source sizes reported against the 6 GB stop. Then **MS0**: PROVENANCE exact per centre and radius, frozen.
3. **The store:** written, verified, repaired, verified and sealed (D1).
4. **The builder and the route,** with the host and Worker tests and their sabotages. MV and MG read BLIND until Weatherscope's fixtures exist.
5. **MM1/MM3 locally.** Then the builder's `cpu_ms` is set from MM3, and §5.4 is re-predicted from the Bend anchor if BQ1 moved it.
6. **D0 → D2 → D3 (staging) → D4 (production),** each with its rows.
7. **The results,** recorded here, misses included.
