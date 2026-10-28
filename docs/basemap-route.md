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

> **Ruled 2026-10-26 (Addendum 1): neither (B) nor (C). PROVENANCE is by reference, inside v1, and bounded at a few KB.** This section stands as the record of why.

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
bm/v1/<storeId>/terrain-sources.bin      four bits per mirrored tile: SRTM, GMTED2010, ETOPO1, NRCAN (Addendum 1, A1.4; was terrain-gate.bin)
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

> **Superseded by Addendum 1 (A1.3): there is no terrain gate in v1.** Every source a US-origin z9 mosaic can contain has terms that can be met, and the credits follow the tiles (A1.4). The paragraph below is the plan as reviewed.

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
| ~~terrain asked, the gate refuses~~ | ~~`422 terrain_unavailable`~~ **removed from v1** (A1.3) | |
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

> **Superseded (BQ1, Addendum 1).** With PROVENANCE by reference, this table no longer stands. It is re-predicted from MS0 and the Bend anchor before the builder runs on real data (A1.8), and kept below as the record.

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
| **MC** coverage and the gate (superseded by A1.7's MC: no gate; KBUF is served with the OGL-Canada statement) | public airports in the US → covered; PHNL → covered; EGLL, and a cleared non-US centre → `no_coverage`; KBUF with terrain → `terrain_unavailable`, without terrain → 200 | **S-BBOX:** coverage built from the state polygons' bounding boxes. A centre inside New York's box but in Canada (cleared first) must then read as covered, and fail |
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

> **Ruled 2026-10-26 (Addendum 1):** RD1 is **(a)**, every file read. RD2-RD17 are ruled as written, for both builders (format doc §13 at `1efb660`).

## 11. Questions, each with a recommendation

> **All ruled 2026-10-26: see Addendum 1, A1.1.**

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

---

## Addendum 1, 2026-10-26: the rulings, and the truth tables frozen before any code

### A1.1 The rulings, verbatim (source: reviewer chat, pasted by Daniel, 2026-10-26)

**The first paste:**

> Rulings on the plan, docs/basemap-route.md at e0c43ce (reviewer chat, 2026-10-26)
>
> The plan is accepted with the rulings below. Freeze the truth tables in your PR before any code, then follow §12.
>
> BQ1: neither (B) nor (C). Provenance by reference, inside v1. Clients never parse PROVENANCE (they check that it exists and the CRC), so changing its lines changes no field a reader uses, and is no version change. §7 is amended by this ruling (Weatherscope Code records it in the format doc):
> - PROVENANCE keeps format, builder, the credits, the processing line, epsilon and the dropped/collapsed counts;
> - it adds store: <storeId>;
> - per layer, it carries source.<layer> (the base URL), vintage.<layer>, files.<layer> (a count), and sha256.<layer>: a sha256 over that layer's sorted "url sha256" lines;
> - the per-file list itself lives in the store's manifest.json (for the stored Bend package: a manifest committed beside the fixture).
> So PROVENANCE is bounded at a few KB whatever the counties or tiles, and stays inside the 32 KB cap. MD3 reads the files from the manifest that the digests pin. Re-predict §5.4 from MS0 and the Bend anchor before the builder runs.
>
> RD1: (a), every file read; size no longer argues for (b′), and (a) reproduces exactly. RD2-RD17: your readings, as written, are ruled for both builders. Weatherscope's host tool adopts them and reports any it already does differently, before either builder freezes.
>
> BQ2: credit what the z9 tiles contain, per joerd's z9 row: SRTM's line for land; GMTED2010's line and its modification statement above 60° N; ETOPO1's NOAA credit on any mosaic with an ocean pixel. No 3DEP line; it is not in z9. The processing line is carried on every terrain package.
>
> BQ3: no blanket border gate. Mexico at z9 is SRTM, so serve it. Canada at z9 is NRCAN: read the Open Government Licence – Canada; if it allows redistribution of derived data with its attribution statement, carry that statement when the mosaic touches Canada and serve. Gate only where a source's terms can't be met, and say which.
>
> BQ4-BQ8 and BQ10-BQ13: as recommended. BQ9: settled above.
>
> Resources: announce each (the bucket, the Worker, the binding) before it exists, with its expected monthly cost. Anything that could add a bill waits for Daniel's yes.
>
> The PK fixtures and bend.bmpk: Weatherscope Code is asked to land them on main as their own small PR, so MV and MG can run; until then they read BLIND, as planned.

**The second paste:**

> PROVENANCE details, for both builders (reviewer chat, 2026-10-26)
>
> Weatherscope Code recorded the BMPK amendments as A10.12 (weatherscope/stage3-5b-plan @ 95fc61a): §7 by reference, the corrected credits, and a new §13 with your RD1-RD17 (RD1 as (a)). Four details are now ruled for both builders:
> 1. The layer keys, in this order: water, rivers, roads, peaks, places, population, terrain. terrain counts every z9 tile read, including tiles read only for peak elevations.
> 2. source.<layer>: the longest common directory prefix of the layer's file URLs, ending in /.
> 3. sha256.<layer>: lowercase-hex SHA-256 over the "<url> <lowercase sha256>\n" lines, sorted in byte order, every line ending in \n.
> 4. store: the first 12 hex of the manifest's sha256 (yours: the sealed manifest.json; Bend's: bend.manifest.json, with sorted keys, one-space indent and a final newline).
> Also: the terrain credits follow the tiles read, whether or not terrain was requested, since peak elevations come from them.
>
> Adopt them. If one doesn't fit the store, raise it before your truth tables freeze.

**The format doc these bind to:** `docs/basemap-package-format.md` on `weatherscope/stage3-5b-plan` at **`1efb660`** (A10.13). The branch moved from `95fc61a` to `1efb660` while I read it, so I read §7 and §13 again at `1efb660`. Every ruled detail above is in its §7, and RD1-RD17 are its §13.

### A1.2 What the rulings change in the plan

| plan | was | now |
|---|---|---|
| §1, BQ1 | options (A), (B), (C); (B) recommended | **PROVENANCE by reference**, inside v1 (A1.5). Bounded: MS-P asserts **≤ 3,072 B** on every package |
| §10, RD1 | (b′) recommended | **(a), every file read.** RD2-RD17 ruled for both builders |
| §2.3, the store | `terrain-gate.bin` | **`terrain-sources.bin`**: four bits per mirrored tile (A1.4) |
| §2.7, the gate | terrain refused where the mosaic meets non-US land | **no gate in v1** (A1.3) |
| §3, the answers | `422 terrain_unavailable` | **removed** |
| §5.4 | outcome classes under per-file PROVENANCE | **superseded**; re-predicted from MS0 and the Bend anchor before the builder runs (A1.8) |
| §8 | the check families | **frozen in A1.7**, with one more family, **MI** (the ingest) |

### A1.3 BQ3: the Open Government Licence – Canada, read

**What I read** (2026-10-26):
- **The licence:** `https://open.canada.ca/en/open-government-licence-canada`, **version 2.0**. curl was refused by the site's firewall ("Request Rejected"), so I read it through the fetch tool. **There is no byte copy and no sha256 of it.**
  - **The grant, quoted:** "The Information Provider grants you a worldwide, royalty-free, perpetual, non-exclusive licence to use the Information", to "Copy, modify, publish, translate, adapt, distribute or otherwise use the Information in any medium, mode or format".
  - **The one condition:** "Acknowledge the source of the Information by including any attribution statement specified by the Information Provider(s)" and, where possible, link to the licence. If the provider specifies no statement, the standard one applies: "Contains information licensed under the Open Government Licence – Canada."
  - **The exemptions:** personal information, third-party rights, official names and symbols, and other intellectual property such as patents and trade-marks.
  - **Non-endorsement:** no use that suggests official status or endorsement.
- **NRCAN's statement for CDEM,** from joerd's `docs/attribution.md` at `0b86765`. That file as fetched has sha256 `2ce4d3414b4592d17ad56a5af57feb480686ddcfb0a3e4ea0b566d28cde13567`. The statement: "Contains information licensed under the Open Government Licence – Canada."

**So it allows it.** Derived data (a hillshade, a summit's elevation) may be redistributed with the statement.
- **What the route does:** it carries the statement, with the licence's link, whenever any tile read meets Canada, and **serves**.
- **The alternative, named:** that a DEM falls under an exemption. **Excluded:** a DEM is not personal information, a third-party right, an official symbol, or a patent or trade-mark.

**The gate: none in v1.** Every source a US-origin z9 mosaic can contain has terms that can be met:

| source (joerd's z9 row) | where | term | met by |
|---|---|---|---|
| SRTM | land below 60° N, Mexico included | USGS asks for credit | its credit line |
| GMTED2010 | land above 60° N | credit; "describe the types of modifications" | its credit line and the processing line |
| ETOPO1 | ocean | NOAA's formal credit | its credit line |
| NRCAN | Canada | OGL – Canada: the attribution statement | its statement and the licence link |
| EUDEM | Europe | | out of reach of a US origin |

- **So `422 terrain_unavailable` leaves v1.** A later source whose terms cannot be met brings it back, under its own ruling.
- **Two more terms, recorded and not acted on:**
  - SRTM's notice asserts copyright for products distributed **outside** the US. v1 serves US origins only.
  - ETOPO1's 17 U.S.C. 403 notice binds **copyrighted** works. The packages claim no copyright.

### A1.4 BQ2: the credits and the tile bits, frozen

**The lines,** in this order, each carried when any tile read has its bit. The texts are verbatim from `attribution.md` at `0b86765`; the NRCAN statement adds the licence link the licence asks for.

| bit | `credit.terrain:` line |
|---|---|
| SRTM | `credit.terrain: SRTM data courtesy of the U.S. Geological Survey` |
| GMTED2010 | `credit.terrain: GMTED2010 data courtesy of the U.S. Geological Survey` |
| ETOPO1 | `credit.terrain: ETOPO1: DOC/NOAA/NESDIS/NCEI > National Centers for Environmental Information, NESDIS, NOAA, U.S. Department of Commerce` |
| NRCAN | `credit.terrain: Contains information licensed under the Open Government Licence – Canada. https://open.canada.ca/en/open-government-licence-canada` |

- **"ETOPO1: " is my prefix.** NOAA's formal credit names its centre, not the dataset, so without it a reader cannot tell which source the line credits (A1.5, RD-P5).

**The tile bits,** computed once at ingest from Natural Earth 10 m (public domain) into `terrain-sources.bin`:

| bit | set when the tile's footprint |
|---|---|
| SRTM | meets land south of 60° N |
| GMTED2010 | meets land north of 60° N |
| ETOPO1 | meets ocean, buffered **2 km** |
| NRCAN | meets Canada (admin-0), buffered **2 km** |

- **Why buffer at all:** a missed credit breaks a term, while an extra one overstates.
- **Why only 2 km:** crediting a source the data does not contain is itself a false statement (BQ2), and 2 km covers Natural Earth 10 m's generalisation.

**The processing line** is carried whenever any tile was read (format §7 at `1efb660`), so GMTED2010's modification statement is present wherever GMTED2010 is. Three texts, frozen:

| tiles read for | `processing.terrain:` |
|---|---|
| levels only | `Terrarium z9 elevations; hillshade from 315 deg at 45 deg with 2x vertical exaggeration; 4x4 mean of z9 pixels; each level pixel takes the z9 pixel containing its centre; shade quantised to 8 bits` |
| peaks only | `Terrarium z9 elevations; summit elevations taken from the z9 pixel containing the summit, rounded to whole metres` |
| both | the levels text, then `; summit elevations taken from the z9 pixel containing the summit, rounded to whole metres` |

### A1.5 PROVENANCE by reference, as the route writes it

**Adopted as ruled** (format §7 at `1efb660`):
- **The lines, in order:**
  - `format`;
  - `builder: blipscope-basemap <40-hex commit>`;
  - `store: <the first 12 hex of the sealed manifest.json's sha256>`;
  - for each layer read, in the order `water, rivers, roads, peaks, places, population, terrain`: `source`, `vintage`, `files` and `sha256`;
  - the credits and the processing line, whenever any tile was read;
  - `epsilon.m`, the ε requested;
  - then `dropped.<layer>` and `collapsed.<layer>` for every layer read.
- **`source.<layer>`** is the longest common directory prefix, ending in `/`.
- **The digest** is over the `<url> <sha256>\n` lines, in byte order.
- **The URLs** are the exact fetch URLs, with RD17's form for terrain.

**Store fit, raised before the freeze as the second ruling asks.** Neither changes a package's bytes, nor anything MG1 grades.
- **F1, the manifest's `builder`.** The stored fixture's manifest is `{"builder", "files", "predicates"}`.
  - In the route's store, the manifest is written once by **the ingest**, while packages are built later by the builder Worker, perhaps at another commit. So the manifest's `builder` can only be the **ingest's** commit.
  - Each package's own `builder:` line still names the builder Worker's commit.
  - **Recommended:** keep the key, for the same shape as the fixture, defined for the route as "the commit of the code that wrote this manifest".
- **F2, the inputs that never enter a package.** The store also reads the TIGER state and county files and Natural Earth, for coverage, county extents and the tile bits.
  - **Recommended:** they sit in the same `files` list under keys outside the seven (`states`, `counties`, `naturalearth`), so `store:` pins them too. No package's PROVENANCE ever lists them.
- **The manifest's serialisation is the fixture's,** adopted for the route: UTF-8 JSON, sorted keys, one-space indent, a final `\n`. Each `files` entry is `{bytes, layer, sha256, url, vintage}`. So `store:` is reproducible from the manifest bytes.

**The details still not pinned, and my readings.** These are PROVENANCE text only, which no client parses and MG1 does not grade. I offer them for the host tool to match.

| | the detail | the route's reading |
|---|---|---|
| RD-P1 | the order of the `dropped`/`collapsed` lines | per layer read, in the fixed order: `dropped.<layer>`, then `collapsed.<layer>` |
| RD-P2 | `vintage.<layer>` when a layer's files carry more than one | the distinct vintages, ascending, space-separated (normally one) |
| RD-P3 | the vintage of a file whose name carries no year (GNIS, Terrain Tiles) | the year of its `Last-Modified` as fetched, recorded in the manifest |
| RD-P4 | `epsilon.m`'s form, and line ends | the requested mm ÷ 1,000 with exactly three decimals (`100.584`); lines joined by `\n`, with no trailing `\n` |
| RD-P5 | the order and form of the credit lines | A1.4's order and texts |
| RD-P6 | a county's or state's files "read" by the route | those whose TIGER polygon's bounding box meets the square's latitude/longitude box, closed. A feature in the square lies in a county that meets it, so no feature depends on this |

**The readings that can change geometry or terrain, and so need the host tool's agreement before either builder freezes.** These are new, forced by §6 and §5.6 rather than by the rulings, and I raise them now:

| | the detail | the route's reading |
|---|---|---|
| RD18 | "smallest-area first" (§6) | the shoelace area of the ring's **quantised** vertices (exact integers), smallest first; on a tie, the later ring in source order goes first |
| RD19 | "shortest first" (§6) | the summed segment length of the road's quantised vertices, in double, shortest first; on a tie, the later road first |
| RD20 | what the 32 KB cap measures | `totalBytes` minus the **bodies** of the TERRAIN sections. Their directory entries count |
| RD21 | the z9 pixel of a point (§5.4 summits, §5.6 sampling) | `X = (lon + 180) / 360 · 2^17`, `Y = (1 − ln(tan(π/4 + φ/2)) / π) / 2 · 2^17`, each floored; tile `⌊X / 256⌋`, pixel `X mod 256` |

### A1.6 The resources, announced before they exist

| resource | made at | expected monthly cost |
|---|---|---|
| R2 bucket `blipscope-basemap`, no lifecycle expiry | D1 | **$0:** 1.6-4.8 GB (the dry run measures it) with the account's ~0.7 GB, inside the free 10 GB |
| Worker `blipscope-basemap` (staging and production), no route | D2 | **$0:** a service-binding call adds no request charge, and its CPU is inside the 30M ms included |
| the proxy's `BASEMAP` service binding (staging, then production) | D3, D4 | **$0** |

**What could add a bill.** Each waits for Daniel's yes before the step that risks it:
1. **R2 storage over 10 GB.** The dry run's measured size is reported before D1. If it plus the account's current use would pass 10 GB, I ask before creating anything.
2. **R2 Class A in an ingest month,** with the weather ingest near 88 % of the free 1M. The D1 upload's put count is stated beforehand. If it would take the month past 1M, I ask.
3. **Workers CPU from parameter churn.** This is bounded by the rate limit and the Cache API, and watched in D3's rows.

### A1.7 The truth tables, frozen

**Conventions:**
- **The synthetic origins** are (0°, 0°) for the arithmetic families and (0.37°, 0.59°) for MW and MP. The second has distinctive digits for MP's string checks. Both are the location of nothing.
- **Unless a case says otherwise:** R 97,000,000 mm; ε 100,584 mm; k 111.0; c0 = max(cos(lat0 · π/180), 0.01).
- **Synthetic stores give lat/lon** from km as `lat = lat0 − S/111`, `lon = lon0 + E/(111 · c0)`, in double.
- **The test constants:**
  - builder commit: forty `1`s;
  - file sha256s: `3`×64 (water), `4`×64 (roads), `5`×64 (tiles);
  - store ids: the real 12-hex prefix of each fixture manifest's sha256.
  - None is a 16-hex run, so the device-id hook stays quiet.
- **The expected doubles** come from a throwaway calculator written from format §2, §4, §5.6 and §9 and RD21, not from builder code (none exists). Every integer is exact.
- **Where each runs:**
  - `host`: vitest under Node;
  - `worker`: vitest-pool-workers;
  - `store`: the real sealed store, read locally, after D1.
- **BLIND or unreadable counts as FAIL.**

**The sabotage rule** (A10.11's):
- each sabotage asserts its needle appears exactly once and that the file changed, before anything is measured;
- it restores the file from a byte copy, with a sha check;
- its set is the **exact** list of cases that must fail, across every family below except:
  - **MD3a and MD4,** which compare against the *committed* code and so fire, correctly, on any change to builder output;
  - **MG,** BLIND until its fixture lands;
- every other case must be unchanged.

**The functions under test** (names frozen, so the cases bind to them), in `proxy/src/basemap/`:

| file | functions |
|---|---|
| `query.ts` | `parseQuery(url)`, `etagOf(query, rev, storeId)` |
| `format.ts` | `validate(bytes) → {status}` with A10.11's status names (`kOk`, `kHeader` … `kBody`); `encode(pkg)`; `decode(bytes)`; `crc32` |
| `frame.ts` | `toFrameKm`, `clipLine`, `clipRing`, `simplify`, `quantise` |
| `layers.ts` | `keepWater`, `isRiverName`, `selectRivers`, `isBusRoute`, `roadClass`, `rankPeaks`, `withinMm`, `keepPlaces` |
| `budget.ts` | `fitBudget` |
| `terrain.ts` | `decodeTerrarium`, `hillshade`, `boxMean4`, `shadeByte`, `levelPixelToZ9`, `elevationM` |
| `provenance.ts` | `layerDigest`, `provenanceText` |
| `build.ts` | `build(store, query, { capBytes = 32768 })`. **`capBytes` is a test seam:** the Worker entry never passes it, and MW13 reaches it only through a test builder |
| `handler.ts` | `handleBasemap` |
| `proxy/scripts/basemap/ingest-lib.ts` | `filterRecord`, `joinPopulation`, `assignCells`, `writeManifest`, `verifyStore` |

#### MQ, the request (worker, 10 cases)

**The base query, Q0:** `lat=0.37&lon=0.59&r=97&eps=100584&layers=water,rivers,roads,peaks,places`.

| case | input | expected |
|---|---|---|
| MQ1 | Q0 | `{lat2 "0.37", lon2 "0.59", rKm 97, epsMm 100584, mask 31, levels []}`. The bits: water 1, rivers 2, roads 4, peaks 8, places 16, terrain 32 |
| MQ2 | Q0 with `layers=places,peaks,roads,rivers,water` | MQ1's ETag |
| MQ3 | 100,000 seeded random (lat, lon) in [18, 72) × [−180, −64), plus every half-way value x = n + j/8 (j odd) on each axis in the same ranges (680) | the origin's `lat0Micro`/`lon0Micro` equal `Math.round(x·100) · 10,000`, through `crop.ts`'s own `quantise`, in **every** case: **0 differences**; both counts printed |
| MQ4 | r = `0.4`; `222.4`; `222.5`; `0`; `-5`; `x`; absent | rKm 1; rKm 222; `bad_r` for the other five |
| MQ5 | the bound, parsed from `src/AircraftManager.cpp` | `KM_PER_DEGREE` and `MAX_DEGREES` each found exactly once, with product **222**, equal to the route's bound. Any other count: **BLIND** |
| MQ6 | eps = `100584`; `1000`; `10000000`; `05000`; `999`; `10000001`; `100.6`; `1e5`; `+5000`; empty | 100584; 1000; 10000000; 5000 (and its ETag equals `eps=5000`'s); `bad_eps` for the other six |
| MQ7 | layers = empty; `water,water`; `lakes`; `water,`; `WATER`; `water`; `terrain` (no levels); `water,terrain` (no levels) | `bad_layers` for the first five; mask 1; `bad_levels`, twice |
| MQ8 | with `terrain` in the layers: (a) `24743664:246,49487328:246,98974656:246`; (b) `98974656:246,24743664:246,49487328:246`; (c) levels without `terrain`; (d) five pairs; (e) N 0; (f) N 1025; (g) radius 999999; (h) radius 250000001; (i) `1000000:100,1000000:200`; (j) `1000000:1024,2000000:1024`; (k) `1000000:1024,2000000:1023`; (l) `1000000`; (m) `1000000:4:5`; (n) `1000000:04` | (a) ascending, as sent; (b) (a)'s list and (a)'s ETag; (c)-(j) `bad_levels`, (j) being 2,097,168 B; (k) accepted, 2,095,121 B; (l), (m) `bad_levels`; (n) accepted, N 4 |
| MQ9 | Q0 + `foo=1` | MQ1's ETag |
| MQ10 | the ETag's form; six variants of Q0 (lat 0.38; lon 0.60; r 98; eps 100585; no `places`; + `terrain` with `1000000:4`); lat `0.3749` | matches `^"bm1-[0-9]+-[0-9a-f]{12}-[0-9a-f]{24}"$`; Q0 and the six give **seven distinct** ETags; lat `0.3749` gives MQ1's |

**Sabotages:**
- **S-QROUND:** a local `quantise` that rounds half to even. Fails exactly **{MQ3}**.
- **S-QDIGEST:** the digest leaves out `epsMm`. Fails exactly **{MQ10}**.
- **S-QSORT:** the levels are not sorted. Fails exactly **{MQ8}**.

#### MV, the validator against the negative packages (worker, 29 cases)

- **MV/PK-min … MV/PK-reserved:** A10.11's 28 PK cases, each with A10.11's expected status. They read Weatherscope's fixtures in `test/fixtures/wx/basemap/`, bound to each fixture by the PK case name its fixture list gives. **A fixture list that does not name its cases makes MV BLIND.**
- **MV-bend:** `bend.bmpk` → `kOk`.
- **BLIND until the fixtures land on main.**

**Sabotages:**
- **S-VCRC:** the CRC check skipped. Fails exactly **{MV/PK-crc}**.
- **S-VSWAP:** the overlap and gap reasons swapped. Fails exactly **{MV/PK-overlap, MV/PK-gap}**.

#### ME, the encoder (worker, 7 cases)

| case | input | expected |
|---|---|---|
| ME1 | `encode(decode(f))` for f in {`syn-min`, `syn-unknown`, `bend.bmpk`} | **byte-identical** to f. BLIND until the fixtures land |
| ME2 | 1,000 builds over seeded synthetic stores. The generator is committed in the test file before any builder code: 0-40 features per layer, lines of 2-200 vertices, rings of 3-100, R 1-222 km, ε 1,000-2,000,000 mm, random masks, 0-2 levels with N 1-64, and no road name containing `Bus` | **0** validator refusals; PROVENANCE **≤ 3,072 B** in every one (**MS-P**) |
| ME3 | a feature with a vertex at (E, S) = (R, −R) | q (32767, −32767) present; `kOk` |
| ME4 | `quantise(E_mm = 1.00002 · R_mm)` | **throws `vertex_range`**; never returns 32767 |
| ME5 | `crc32("123456789")` | `0xCBF43926` |
| ME6 | svarint of 0, 63, −64, 64, 32767, −32767 | `00`; `7e`; `7f`; `80 01`; `fe ff 03`; `fd ff 03` |
| ME7 | `build()` called **directly** (not through `parseQuery`) with every layer and the levels unsorted, `2000000:4` then `1000000:4` | the directory's types are 1, 2, 3, 4, 5, 6, 6, 7, and the two levels run 1,000,000 then 2,000,000 mm: the builder orders them itself |

**Sabotage:** **S-ECLAMP**, `quantise` clamping instead of throwing. Fails exactly **{ME4}**.

#### ML, the layer rules (host, 15 cases)

Synthetic features at origin (0, 0), through the layer functions.

| case | input | expected |
|---|---|---|
| ML1 | water with `AWATER` 1,499,999 and 1,500,000 | only the 1,500,000 kept |
| ML2 | one polygon: an outer ring and a hole | **1** ring (RD3) |
| ML3 | `FULLNAME`s: "Deschutes Riv", "Little Deschutes Riv", "Riverside Dr", "Riv", "Big Riv " (trailing space) | the first two kept |
| ML4 | 7 groups, centred E-W lines at S = 0 with in-disc lengths A 70, B 60, C 50, D 40, F 30, E 30, H 10 km, **F before E in source order** | kept, and the names table in order: A, B, C, D, **E** (the tie broken by byte order, RD8) |
| ML5 | groups P (two parts, 20 + 25 km), Q 50, R 40, S 30, T 29, U 28 | Q, P, R, S, T; U dropped |
| ML6 | "Mid Riv" 100 km at S = 0; "Edge Riv" from E −200 to 200 at S = 90 km (72.360210 km in the disc, 194 km in the square); M80, M75, M74, M73 at S = 0 | Mid, M80, M75, M74, M73; **Edge dropped** |
| ML7 | `isBusRoute` of: "US Hwy 97 Bus", "Bus US Hwy 20", "President George Bush Tpke", "Busby Rd", "BUS", "Bus", "Bus Loop", "Bus-Loop 10", "Omnibus Rd", "OmniBus Rd", a missing `FULLNAME` | drop, drop, keep, keep, keep, drop, drop, drop, keep, keep, keep |
| ML8 | `MTFCC` S1100, S1200, S1400 | class 1; class 2; dropped |
| ML9 | summits A-F, all 10 km apart, at elevations 1000, 1200, 1200, 900, 1100, 800 with `feature_id`s 10, 30, 20, 40, 50, 60 | C, B, E, A, D, F |
| ML10 | `elevationM` of the RGB (128, 0, 128), (127, 255, 128), (132, 176, 0), (128, 0, 127) | 1, 0, 1200, 0 |
| ML11 | along E: P at 10 km (2000 m); Q at 12.999 km (1990 m); R at 14.0 km (1980 m); T at 6.999 km (1970 m) | **P, R, T** kept and Q dropped. R is 1.001 km from the *dropped* Q, and only kept summits count |
| ML11b | the de-dup predicate at exactly 3,000.0 m | within, so dropped (RD9) |
| ML12 | 20 summits 10 km apart, elevations falling | the first **16** kept |
| ML13 | places in file order Z (96.9 km), A (50), M (97.1), B (10), with populations | Z, A, B, **in file order**, populations unchanged |
| ML13b | `withinMm(d = R)` | true |

**Sabotages:**
- **S-BUSSUB:** a substring test for "Bus". Fails exactly **{ML7}**.
- **S-SQUARELEN:** in-square length in place of in-disc. Fails exactly **{ML6}**.
- **S-DEDUPALL:** de-duplicates against every higher summit. Fails exactly **{ML11}**.
- **S-DEDUPLT:** strict `<` in the predicate. Fails exactly **{ML11b}**.
- **S-PLACESORT:** places sorted by distance. Fails exactly **{ML13}**.
- **S-RIVERTIE:** ties broken by source order. Fails exactly **{ML4}**.

#### MT, terrain (host, 11 cases)

| case | input | expected |
|---|---|---|
| MT1 | `decodeTerrarium` of (128, 0, 128), (127, 255, 128), (132, 176, 0), (0, 0, 0) | 0.5, −0.5, 1200, −32768, exactly |
| MT2a | an 8 × 8 DEM, flat 1000 m; lat0 0. Each MT2 case runs `hillshade`, then `boxMean4`, then `shadeByte`, at **every** pixel | **180** at all 64 |
| MT2b | 1000 + 64·x (rising east); lat0 0 | **216** at all 64 |
| MT2c | 1000 + 64·(7 − y) (rising north); lat0 0 | **117** at all 64 |
| MT2d | 1000 + 64·x + 64·y (facing north-west); lat0 0 | **247** at all 64 |
| MT2e | as MT2b, at lat0 60 | **220** at all 64 |
| MT-impulse | `boxMean4` of a 16 × 16 field, 1 at (10, 10), 0 elsewhere | 0.0625 at x, y 9..12 (16 px), 0 everywhere else |
| MT-corner | the same with the 1 at (0, 0) (edge padding 2) | (0,0) 0.5625; (1,0) and (0,1) 0.375; (2,0) 0.1875; (1,1) 0.25; (2,1) 0.125; (2,2) 0.0625; (3,0) 0 |
| MT3 | a level (98,974,656 mm, N 246) at origin (0, 0) over seeded smooth synthetic tiles: from the rule's mosaic (its square plus one tile), and from a mosaic one tile larger again | all 60,516 values **identical** |
| MT4 | `levelPixelToZ9`, origin (0, 0): pixels (0,0), (3,3), (1,2) of Rℓ 1,000,000 mm, N 4; pixel (2,2) of Rℓ 1,250,000 mm, N 5 | (65533, 65533); (65538, 65538); (65535, 65536); (65536, 65536). The last is exact, and truncation keeps it |
| MT5 | `shadeByte` of 0, 1, 0.5, 127.49/255 | 0, 255, 128, 127 |

**Sabotages:**
- **S-TALIGN:** the mean window at [i − 1, i + 2]. Fails exactly **{MT-impulse, MT-corner}**.
- **S-TPAD:** zero padding. Fails exactly **{MT-corner, MT2a, MT2b, MT2c, MT2d, MT2e}**.
- **S-TROUND:** rounding in place of truncation. Fails exactly **{MT4}**.
- **S-TNOMARGIN:** the mosaic without its margin. Fails exactly **{MT3, MB8a, MB8b}**: MB8's `files.terrain` drops from 16 to 4, so its PROVENANCE is 1 B shorter.
- **S-TCOS:** `mpp` without cos(lat0). Fails exactly **{MT2e}**.

#### MB, §6's budget (host, 9 cases, through the `capBytes` seam)

**The fixtures** (origin (0, 0); R 97 km; ε 100,584 mm; layers water and roads):
- **A, the water:** 9 square rings. Ring i (1-9) has its first vertex at (E, S) = (25 + 5(i − 1), 30) km and side 1.0 + 0.1i km. **Each encodes in 16 B.**
- **A, the roads:** 7 straight E-W roads from E = −70 km, at S = −30 − 2j km (j = 1..7). Three primaries (S1100) of 10, 11 and 12 km, then four secondaries (S1200) of 3, 4, 5 and 6 km. **Each encodes in 11 B.** No road name contains `Bus`.
  - **PROVENANCE: 511 B. Total: 812 B.**
- **B:** A plus two primaries after the seven, each 20 km with a middle vertex.
  - Z1's middle vertex is 150 m off its chord: 14 B at ε, 11 B at 2ε.
  - Z2's is 300 m off: 16 B at ε and at 2ε, 11 B at 4ε.
  - **Total: 842 B.**
- **A8:** A plus `terrain` with level `1000000:4`, over 16 flat synthetic tiles (x, y 254..257) with the SRTM bit, and vintage 2016.
  - **PROVENANCE: 1,005 B. Total: 1,346 B, of which the TERRAIN body is 24 B.**

| case | fixture, cap | status | totalBytes | toleranceMm | WATER | ROADS | dropped water / roads |
|---|---|---|---|---|---|---|---|
| MB1 | A, 812 | 200 | 812 | 100,584 | rings 1-9 | 7 | 0 / 0 |
| MB2 | A, 811 | 200 | 796 | 201,168 | rings 2-9 | 7 | 1 / 0 |
| MB3 | A, 667 | 200 | 657 | 201,168 | none (count 0, length 0) | 6 (the 3 km secondary dropped) | 9 / 1 |
| MB4 | A, 623 | **422 `too_large`** | | | | | |
| MB5 | A, 624 | 200 | 624 | 201,168 | none | 3 (the primaries) | 9 / 4 |
| MB6 | B, 841 | 200 | 839 | 201,168 | rings 1-9 | 9 (Z1 n 2, Z2 n 3) | 0 / 0 |
| MB7 | B, 838 | 200 | 823 | 201,168 | rings 2-9 | 9 (Z1 n 2, Z2 n 3) | 1 / 0 |
| MB8a | A8, 1,322 | 200 | 1,346 | 100,584 | rings 1-9 | 7 | 0 / 0 |
| MB8b | A8, 1,321 | 200 | 1,330 | 201,168 | rings 2-9 | 7 | 1 / 0 |

`epsilon.m` reads `100.584` in every case (RD13).

**Sabotages:**
- **S-BTWICE:** ε multiplied by 4, not 2. Fails exactly **{MB2, MB3, MB5, MB6, MB7, MB8b}**.
- **S-DROPORDER:** the largest ring dropped first. Fails exactly **{MB2, MB7, MB8b}**.
- **S-NOCOUNT:** `dropped.water` not incremented. Fails exactly **{MB2, MB3, MB5, MB7, MB8b}**.
- **S-ROADORDER:** the longest secondary dropped first. Fails exactly **{MB3}**.
- **S-PRIMARYDROP:** primaries droppable after the secondaries. Fails exactly **{MB4}**.
- **S-TERRCAP:** the TERRAIN body counted in the cap. Fails exactly **{MB8a, MB8b}**.

#### MD, determinism and `--check` (host and worker, 7 cases)

| case | input | expected |
|---|---|---|
| MD1 | the same request twice in one isolate, on MW's store and on ME2's first 50 stores | byte-identical |
| MD2 | the same 50 builds in workerd and in Node | identical sha256 lists. Both are V8 with the same fdlibm `Math`. **A difference stops the work and is reported** |
| MD3a | `scripts/basemap-check.ts`, on a package built at HEAD's commit over a store ingested from MI's synthetic shapefiles, with `--sources <dir>` | **exit 0**: the manifest's digests match PROVENANCE's, every file's sha256 matches, and the rebuild at that commit is byte-identical |
| MD3b | the same package with one body byte changed and its CRC recomputed | **exit 1** |
| MD3c | a `builder` commit not in the repository | **exit 2 (BLIND)**, never 0 |
| MD3d | a manifest whose water file's sha256 differs from the one the package's `sha256.water` pins | **exit 1** |
| MD4 | the `rev` guard: `test/fixtures/basemap/rev.json` maps each rev to the sha256 of the build of 3 synthetic stores | equal at the current rev. **Its sabotage:** a change to the processing text with `rev` unchanged must fail it |

#### MG, the golden row (host; BLIND until `bend.bmpk` and `bend.manifest.json` land)

| case | expected |
|---|---|
| MG0 | for every layer, the `sha256.<layer>` and `files.<layer>` recomputed from `bend.manifest.json` equal `bend.bmpk`'s PROVENANCE; and `store:` equals the first 12 hex of that manifest's sha256 |
| MG1 | my ingest and builder, fed the files `bend.manifest.json` names (each fetched or found with `--sources`, and sha256-verified), and the request read from `bend.bmpk`'s header and sections, **decode equal** within format §11 |

**The controls:**
- `bend.bmpk` compared with itself: 0 differences.
- **S-GEPS,** the builder at 1.5 ε, must FAIL on vertex counts.
- **S-GSMOOTH,** the builder without the 4 × 4 mean, must FAIL on more than 0.1 % of terrain values.

#### MP, privacy (worker, 3 cases)

| case | expected | its control (must turn it red) |
|---|---|---|
| MP1 | across every MW row, no log line contains `0.37` or `0.59`. The route's log fields are integers | a planted `console.log(lat2)` |
| MP2 | across every MW row, **0** R2, KV or Durable Object writes, counted through wrapped bindings | a planted R2 put |
| MP3 | no ETag and no cache key contains `0.37` or `0.59` | an ETag carrying `lat2` |

#### MC, coverage and the credits (store; graded after D1)

| case | input | expected |
|---|---|---|
| MC1 | the 50 cleared airports | **50** covered |
| MC2 | EGLL; (0.00, 0.00); PHNL; CYYZ, once `forbidden.py` clears it | `no_coverage`; `no_coverage`; covered; `no_coverage` |
| MC3 | KSFO's cleared point moved 0.03° east, into the bay | covered |
| MC4 | the credits with Weatherscope's three levels at KDEN; KBOS; KBUF; KELP; PANC, once cleared | [SRTM]; [SRTM, ETOPO1]; [SRTM, NRCAN]; [SRTM]; [SRTM, GMTED2010, ETOPO1]. Each carries the levels processing line, and none is refused |

**Sabotages:**
- **S-BBOX:** coverage from the state polygons' bounding boxes. Fails exactly **{MC2}**, through CYYZ.
- **S-NOCANADA:** the NRCAN bit ignored. Fails exactly **{MC4}**, through KBUF.
- **S-NOOCEAN:** the ETOPO1 bit ignored. Fails exactly **{MC4}**, through KBOS and PANC.

#### MW, the endpoint (worker, 17 cases)

**MW's store:** origin (0.37, 0.59); one water ring and one primary road in one cell; coverage over [0.30, 0.40] × [0.50, 0.60].
**Q_W:** `lat=0.37&lon=0.59&r=97&eps=100584&layers=water,roads`.
**Store reads** are counted through a wrapped binding, in a fresh builder.

| case | request | status | builder calls | store reads |
|---|---|---|---|---|
| MW1 | Q_W, no `BASEMAP` binding | `503 not_configured` | 0 | 0 |
| MW2-MW7 | Q_W with a bad lat, lon, r, eps, layers, levels | `400 bad_lat` … `bad_levels` | 0 | 0 |
| MW8 | Q_W without a device key | `401` | 0 | 0 |
| MW9 | Q_W | **200**: `application/octet-stream`, `Cache-Control: no-cache`, the ETag matching MQ10's pattern, the body `kOk` | 1 | **5** (`current`, `manifest.json`, `coverage.bin`, `index.json`, one cell) |
| MW10 | Q_W with `If-None-Match` set to MW9's ETag | **304**, no body | **0** | **0** |
| MW11 | Q_W again, without `If-None-Match` | 200, MW9's bytes (the Cache API) | **0** | **0** |
| MW12 | Q_W at lat 10.00 | `404 no_coverage` | 1 | 3 (`current`, `manifest.json`, `coverage.bin`) |
| MW13 | Q_W through a test builder calling `build()` with `capBytes` 100 | `422 too_large` | 1 | 5 |
| MW14 | Q_W against a store with no `current` | `503 basemap_unavailable` | 1 | 1 |
| MW15 | a test builder returning a valid package with its road class patched to 3 and the CRC recomputed | `500 builder_invalid`, a JSON body, never the package bytes | 1 | 0 |
| MW16 | `/v1/basemap?` + Q_W | MW9's bytes and ETag | not counted | not counted |
| MW17 | Q_W against a store whose `manifest.json` does not hash to `current`'s store id | `503 basemap_unavailable` | 1 | 2 (`current`, `manifest.json`) |

**The store-id guard (MW17), new here.** On every build the builder checks that the manifest's sha256 begins with `current`'s store id. So a half-replaced or mixed store answers 503 rather than a package whose `store:` line names a manifest it was not built from.

**Sabotages:**
- **S-WNOVALIDATE:** the handler skips `validate`. Fails exactly **{MW15}**.
- **S-W304LATE:** the ETag compared after the builder call. Fails exactly **{MW10}**.
- **S-WNOSTORECHECK:** the store-id guard skipped. Fails exactly **{MW17}**.

#### MI, the ingest (host, 7 cases, over tiny synthetic shapefiles and CSVs made by the test)

| case | input | expected |
|---|---|---|
| MI1 | `AREAWATER` at `AWATER` 1,499,999 / 1,500,000 / 2,000,000; `LINEARWATER` "A Riv" / "Riverside Dr" / empty; `PRISECROADS` S1100 "US Hwy 97 Bus", S1200 "OR 126", S1400 "Main St"; GNIS Summit / Lake; Gazetteer LSAD 25 / 57 | the store holds 2 water, "A Riv", **both** S1100 and S1200 roads (**the Bus rule is the builder's**), the Summit, and the LSAD 25 place |
| MI2 | GEOID `4105800` against SUB-EST2024 rows: SUMLEV 162 with STATE 41, PLACE 05800, `POPESTIMATE2024` 100000; a SUMLEV 157 row with the same STATE and PLACE; and a GEOID with no row | 100,000; the 157 row ignored; 0 |
| MI3 | a record whose coordinates include `0.1 + 0.2` | the store's f64 equals the source's, **bit for bit** |
| MI4 | the manifest | sorted keys, one-space indent, a final `\n`, the entry keys `{bytes, layer, sha256, url, vintage}`; `store` equals the first 12 hex of its sha256; the same inputs twice give the same bytes. **Its key set waits on F1/F2's answer, and is re-registered only by ruling if that answer differs** |
| MI5 | features crossing a cell border | each stored once, in the cell of its bounding box's south-west corner, with that cell's reach covering it |
| MI6 | a split threshold of 1,000 B through the seam | a cell over it splits into quarters until each is ≤ 1,000 B; a cell holding a single feature is never split |
| MI7 | `verifyStore` over an in-memory bucket: (a) every object intact; (b) one object missing; (c) one object's bytes changed at the same size; (d) the order of writes | (a) passes, and `current` is written; (b) fails, with no `current`, then repair, verify passes, and `current` is written; (c) fails on its sha256; (d) `current` is the last write |

**Sabotages:**
- **S-ISIZE:** the verifier compares size only. Fails exactly **{MI7}** (through c).
- **S-ISEAL:** `current` written before verifying. Fails exactly **{MI7}** (through b and d).
- **S-IBUS:** the ingest applies the Bus rule. Fails exactly **{MI1}**.

### A1.8 Re-predicted later, as ruled

**Before the builder runs on real data, frozen in this PR:**
- **MS0:** PROVENANCE's exact bytes per centre and radius, from the sealed manifest.
- **§5.4's outcome classes,** re-predicted from MS0 and Bend's measured section bytes, at Weatherscope's request and the Blipscope's 161 and 222 km.

**Unchanged:** MM1-MM6 (§6), and the deploy rows D0-D4 (§9). D2's dry run no longer lists `terrain-gate`.

### A1.9 Next

1. Step 1 of §12 is this addendum.
2. **F1/F2 and RD18-RD21 are raised in the report** and wait for their answer.
   - MI4's key set waits on F1/F2.
   - RD18-RD21 bind MB, MT4 and ML10 as I read them. If they are ruled otherwise, those rows are re-registered by that ruling.
3. **Step 2 is next:** the ingest code and its dry run, which is local, fetches public files only, and creates nothing in Cloudflare.
   - Its measured size is reported against the 6 GB stop, and against the free 10 GB, **before** D1 is announced for doing.

### A1.10 Two notes, and the rulings on Addendum 1, verbatim (source: reviewer chat, pasted by Daniel; both are dated 2026-10-28 in the paste)

**The notes:**

> Two notes (reviewer chat, 2026-10-28)
>
> 1. Weatherscope Code is landing the PK set on main as its own PR, ahead of bend.bmpk: the 28 fixtures, the tools, and the format doc (v1 with A10.12). Your MV rows, and ME1 (syn-min's contents are written out as data in tools/weatherscope/basemap/synth.py), can run against main once it merges.
> 2. SRTM's spec (§3.15, read by Weatherscope Code in joerd at 0b86765) asserts copyright for products distributed outside the US, with a notice. Carry the same notice in the route's own licence or attribution file, beside the SRTM credit. PROVENANCE keeps the credit line.

**The rulings:**

> Rulings on #486's Addendum 1 (reviewer chat, 2026-10-28)
>
> Accepted: the two rulings as recorded; the four PROVENANCE details (dropped/collapsed for every layer read); BQ3 settled by OGL – Canada, with no terrain gate; BQ2's frozen credit lines and tile bits (2 km buffers); the three processing-line texts; and the resource announcements with their bill guards.
>
> F1: as recommended. The manifest's builder is the commit of the code that wrote the manifest.
> F2: as recommended. states, counties and naturalearth go in the same files list under their own keys, so store: pins them; no package's PROVENANCE lists them.
> RD18-RD21: your readings are ruled for both builders. RD18's area is the absolute shoelace area. RD21's expression is pinned as written; if MG1 ever differs on a summit's elevation, check first for a floor at a pixel boundary (libm's log and tan can differ in the last bit between languages).
> RD-P1 to RD-P6: your readings are ruled for both builders, with A1.4's texts as the credit and processing lines.
>
> The SRTM notice: as in my earlier note, carry it in the route's licence or attribution file beside the SRTM credit, since the repo and the route's docs are public. PROVENANCE keeps the credit line only.
>
> Next as planned: the ingest code and a local dry run, with sizes reported before the bucket is created.

### A1.11 What they settle in the tables (no frozen value moves)

- **MI4 is frozen.**
  - The manifest is UTF-8 JSON with sorted keys, a one-space indent and a final `\n`. Its keys are exactly `builder`, `files` and `predicates`.
  - `builder` is **the commit of the code that wrote the manifest** (F1).
  - Each `files` entry is exactly `{bytes, layer, sha256, url, vintage}`.
  - `layer` is one of the seven package keys, or `states`, `counties` or `naturalearth` (F2).
  - **The predicted values are A1.7's,** unchanged.
- **RD18:** the area is the **absolute** shoelace area. MB's squares have one orientation, so no MB value moves.
- **RD21, for MG1's reader:** if MG1 ever differs on a summit's elevation, **first check for a floor landing at a pixel boundary.** The last bit of libm's `log` and `tan` can differ between languages. This is written into MG1's failure output, so whoever reads a failure is looking at it.
- **RD-P1 to RD-P6:** ruled, with A1.4's texts.
- **ME1b, registered now, before any code.** `encode()`, given `syn-min`'s contents as `tools/weatherscope/basemap/synth.py` writes them out as data, produces **`syn-min.bmpk` byte for byte**. That is stronger than ME1's round trip, because nothing is read back from the fixture.
  - **Its control** (on the comparator, since any encoder sabotage would also move ME2, ME3 and ME7): compared against a copy of `syn-min.bmpk` with one body byte changed and its CRC recomputed, ME1b must report a mismatch **at that offset**.
  - **It is BLIND until the fixtures are on main.**
  - The fixture's two-line provenance is data in `synth.py`, so this checks serialisation only, not my PROVENANCE layout.

### A1.12 The route's attribution file, and the SRTM notice

- **The file:** `docs/basemap-attribution.md`. It is public, and beside this plan.
- **What it carries:**
  - A1.4's credit lines;
  - the processing texts;
  - the OGL – Canada statement with its link;
  - **beside the SRTM credit, SRTM's notice,** from SRTM's specification §3.15 b as joerd's `attribution.md` quotes it at `0b86765`: "© COPYRIGHT (year of production) BY THE UNITED STATES GOVERNMENT. NO COPYRIGHT CLAIMED UNDER TITLE 17 U.S.C.";
  - the public-domain status of TIGER/Line, the Gazetteer, SUB-EST2024 and GNIS.
- **When:**
  - **The file is committed with the builder code** (step 4). It describes what the packages carry, and a statement belongs in the commit that makes it true (the `/wx` precedent for NOAA's line).
  - **The proxy's `/credits` page gains the same block in D3's change,** the one that binds the route. Not before: until then no package is served.
- **The check, MA1, registered now:**
  - the attribution file, and `/credits` once D3's change exists, contain every A1.4 credit line's text, the SRTM notice, and the OGL statement with its link;
  - **its control:** the notice deleted from the file must turn MA1 red;
  - `smoke-prod.sh` gains the `/credits` half at D3.
- **AQ1, raised: the notice's "(year of production)".**
  - The notice is a template, and §3.15b says the year is inserted. joerd dates SRTM "circa 2000 … with several updates including in 2014", and does not say which release the z9 tiles came from.
  - **Recommended:** insert **2000**, the release joerd names, and say beside it that joerd records a 2014 update.
  - **The alternative** is 2014, the latest update joerd names. A notice with no year at all would not be the notice.
  - **It wants one ruling for both builders,** since Weatherscope's licence file beside `bend.bmpk` carries the same notice.

---

## Step 2, the ingest: progress (2026-10-27)

**The source plan, from the publishers' own listings.** `ingest-basemap.ts plan` reads the TIGER directory listings and the GNIS bucket listing.

| layer | files | what |
|---|---|---|
| `water` | 3,144 | one per county |
| `rivers` | 3,144 | one per county (the territories' 89 dropped) |
| `roads` | 51 | one per state |
| `peaks` | 51 | GNIS, per state; all `Last-Modified` 2026, so vintage 2026 (RD-P3) |
| the rest | 7 | `places`, `population`, `states`, `counties` and three `naturalearth` files |
| **total** | **6,397** | |

- **Download size,** read from the listings: `AREAWATER` about 1.12 GB, `LINEARWATER` about 3.12 GB, `PRISECROADS` about 0.28 GB, GNIS 47 MB. The rest are a few MB each, apart from the county file at 84 MB.
- **The fetch is running**, into a cache outside the repository, with no failures so far.
  - The CLI **refuses a cache inside the repository**. That refusal was checked: it creates nothing.

**MI, run against the code written after its freeze.**
- **10 of 10 tests pass, over MI's 7 cases.**
- **The sabotages, after a clean baseline (0 failing cases):**

  | sabotage | frozen set | failing |
  |---|---|---|
  | S-ISIZE | {MI7} | **{MI7}**: as predicted |
  | S-ISEAL | {MI7} | **{MI7}**: as predicted |
  | S-IBUS | {MI1} | **{MI1}**: as predicted |

  Each plant asserted its anchor once and that the file changed, and each restore was sha-checked (`scripts/sabotage-basemap.mjs`).

**One explanatory note was wrong, found while writing the plant and before any run.**
- A1.7 glossed S-ISEAL as failing MI7 "through b and d".
- Planting it shows (c) goes red too: with `current` written before the verify, a corrupt store would be sealed.
- **The frozen set is the case, {MI7}, and it held.** The gloss was incomplete. It is corrected here, not edited away.

**A bug of mine, caught by a guard that runs.**
- **What went wrong:** the fetch log was written with `JSON.stringify(log, sortedKeys, 1)`. An **array** replacer is an allowlist applied at every depth, so all 6,397 records were written as `{}`.
- **Why it looked fine:** the fetch reported "4.858 GB logged", but from memory, not from the file.
- **What caught it:** the dry run's first read, which checks each file against the log's sha256. It refused at the first file.
- **The fix:**
  - the log is serialised with the manifest's deep key-sort, then **read back after every write, with every record checked**;
  - files already in the cache are adopted only when a HEAD's `Content-Length` equals their size, re-hashed, and given the HEAD's `Last-Modified` (RD-P3's vintage).
- **The result:** 6,395 adopted, 2 re-downloaded, 6,397 records read back whole.
- **The broken log is kept beside the cache as evidence.**

**Two sources changed length within the hour,** `tl_2024_34023_areawater.zip` and `tl_2024_39077_areawater.zip`. Their HEAD length no longer matched the cached copy, so they were fetched again.
- **The two explanations,** neither excluded:
  - (a) the files changed upstream;
  - (b) the first transfer was short and went unchecked, which requires a response with no `Content-Length`.
- The first copies were overwritten, so the data cannot separate them.
- **What it means for D1:** its fetch re-verifies every file, so a source that moves changes `store:`, visibly, rather than silently.
- **Corrected the same day, by reading the bytes: it was neither (a) nor (b).**
  - Both files were **247-byte HTML**: census.gov's firewall page, "Request Rejected", served with **status 200** and `text/html`.
  - The adoption's 8-way HEAD burst seems to have tripped it. It went on rejecting those two URLs while other files still downloaded.
  - **So my fetcher overwrote two good zips with a rejection page,** and logged it with a size and a sha256 as if it were a new version: a lie with a plausible shape.
  - **What caught it:** the dry run's refusal to unzip it ("invalid zip data", at the 1,500th file).
  - **The fix:** every body is now checked on its own magic bytes (`checkBody`, with this very page as its test's control). A HEAD answered with HTML is never read as "the file changed". A cached copy that fails the check is never resumed or adopted.
  - **Status and length proved nothing here; the bytes did.**

**Coverage and the terrain mirror, measured** (`ingest-basemap.ts coverage`):

| | measured | registered (§7, A1.6) |
|---|---|---|
| coverage | 11,675,779 lattice points in 4,907 rows and 10,270 intervals; `coverage.bin` **121,428 B** | |
| regions (RD-P6) | 3,144 counties, 51 states; **166,835 B** | |
| terrain tiles within 250 km plus the margin | **9,638** | **5,000-7,000: a MISS** |
| their bytes (by HEAD) | **706,629,891 B (0.707 GB)** | 0.6-0.8 GB: inside |
| their `Last-Modified` | 2017 for all 9,638, so `vintage.terrain: 2017` (RD-P3) | |

- **The tile count is a miss, recorded as registered.**
  - The estimate was made before the reach was fixed at 250 km plus a tile, and it did not account for Alaska's high latitude. There 250 km of longitude spans far more degrees, and z9 tiles are smaller.
  - **Named and not excluded:** a bug in `tilesForCoverage` that over-collects.
  - **What would separate the two:** the count with the reach set to 0. That would be the tiles the covered lattice itself touches, plus a margin.
  - I have not run it: that is a change of instrument, so it waits for the reviewer's word.
- **The bytes landed inside their band,** and both the 6 GB stop and the free 10 GB are far off. **Nothing depends on the count.** I make no fix.

**A silent zero, caught by reading the summary rather than by a guard.**
- **What happened:** the first full vector run kept **0 of 32,350 places**.
  - The 2025 Gazetteer is **pipe-delimited**, while older vintages used tabs. Read as tab-delimited, every row was one column, `LSAD` was absent, and the predicate returned false.
- **What caught it:** my reading of the run's summary. **No guard did.**
- **Now two guards would catch it:**
  - `filterRecord` refuses a **missing** column, while still accepting an empty value (MI-column, with a tab-misread control);
  - the dry run refuses any layer that keeps nothing from a non-zero record count.
- **The corrected run keeps 10,232 places, exactly as predicted.**
  - The prediction was the file's own LSAD-25 count, taken by `awk` over the raw file before the run: a different tool, which agrees.

**The sizes, measured before any bucket exists.**
- **This is a PARTIAL measurement.** The census firewall still answers two county `AREAWATER` URLs with its rejection page.
  - `--partial` writes to its own directory, names its manifest `PARTIAL`, and lists the missing files. It is never a store.
  - **The bound on the missing share:** the listing gives the two zips as 280 KB and 508 KB. Only their ≥ 1.5 km² features would be kept, so they add a few MB at most.

| | measured | registered |
|---|---|---|
| water | 68,974 parts, 24.3 M vertices, **390,386,786 B** | |
| rivers | 97,140 parts, 12.1 M vertices, **196,212,893 B** | |
| roads | 262,604 parts, 28.0 M vertices, **458,012,935 B** | |
| peaks | 68,806 summits, **3,719,902 B** | |
| places | 10,232, **649,457 B** (populations joined from 19,479 SUMLEV 162 rows) | |
| cells | 1,397, all at level 0. The largest is **10,290,428 B**, so none split (24 MB rule) | |
| manifest, index | 1,597,028 B; 138,790 B | |
| **vector store** | **1,050,734,555 B** | 1-4 GB: **inside** |
| coverage, regions | 121,428 B; 166,835 B | |
| terrain mirror | 9,638 tiles, **706,629,891 B** | the count a **MISS** (above); the bytes inside 0.6-0.8 GB |
| **the store** | **1,757,652,709 B (1.76 GB)**, plus a few MB for the two missing files and about 5 KB of tile bits | 1.6-4.8 GB: **inside**. The **6 GB stop is not reached** |

**What it means for the bill** (A1.6's guards):
- **R2 storage:** the store, plus the account's ~0.7 GB, is ~2.5 GB of the free 10 GB.
- **D1's puts:** 1,397 cells + 9,638 tiles + 6 small objects = **about 11,041**, against the weather server's ~880k of the free 1M Class A. That is about 1.1 % more.
- **Neither crosses a bill. D1 is still blocked on the two files.**

**One fix in `assignCells`, outside the frozen rows.**
- Features that all share one south-west corner can never be separated by a quarter, so splitting them recursed forever.
- Such a cell is now kept whole, like a single-feature cell. MI6's frozen expectations do not touch this case.

### D1's uploads, ruled verbatim (source: reviewer chat, pasted by Daniel; dated 2026-10-28 in the paste)

> D1's uploads (reviewer chat, 2026-10-28)
>
> Wrangler under the existing OAuth session, 8 at a time, as you recommend. No new R2 token: a 45-60 minute upload once per ingest isn't worth a new credential. The verify -> repair -> verify -> seal cycle stays as planned, and the put count is stated before D1 (about 11,000: about 1 % of the free monthly Class A writes).

- **What it settles:** the upload method.
- **What it does not settle:** how to fetch the two county files census's firewall refuses. **No retry is made until that is answered.** (Answered in the next paste.)

### Rulings on the dry run, verbatim (source: reviewer chat, pasted by Daniel; dated 2026-10-28 in the paste)

> Rulings on the dry run (reviewer chat, 2026-10-28)
>
> Accepted: A1.10 as recorded; MI 10/10 with the three sabotages; the corrected S-ISEAL note; the store at 1.76 GB, inside its band and far from the 6 GB stop. The three bugs and their guards are accepted: the log read back after every write, each file checked on its magic bytes, and a missing column or a layer that keeps nothing refused. Places at 10,232, as predicted beforehand.
>
> AQ1: the SRTM notice's year is 2000, with your note that joerd records a 2014 update. Weatherscope's licence file gets the same ruling.
>
> The tile-count miss stays recorded. First read it from the tile list as it is, by region (CONUS, Alaska, Hawaii and the margin). That's reading, not a change to the instrument. If that doesn't explain 9,638, run reach = 0, with its prediction written first. D1's gate is size, not tile count, and the size is inside.
>
> The two blocked files: retry them gently, one at a time over about 24 hours, identifying yourself in the User-Agent. If they're still refused, fetch them from the Census Bureau's own mirror, and record the mirror URLs as their URLs, with a note in the manifest. Same agency, same files, and the manifest then says truthfully where each came from.
>
> D1's uploads: Wrangler under the existing login, 8 at a time. No new R2 token: a 45-60 minute upload once per ingest isn't worth a new credential.
>
> Then the source bits and the tiles, and announce the bucket with the measured numbers. At $0, it doesn't wait for Daniel's yes.

**What follows from it:**
- **AQ1:** the attribution file's notice reads "© COPYRIGHT 2000 BY THE UNITED STATES GOVERNMENT. NO COPYRIGHT CLAIMED UNDER TITLE 17 U.S.C.", with the note beside it that joerd records a 2014 update to SRTM.
- **Raised before it can matter: the mirror's "note in the manifest" has no place in MI4's frozen shape.** MI4 fixes each entry to exactly `{bytes, layer, sha256, url, vintage}`, and `writeManifest` refuses anything else.
  - **Proposed:** an optional top-level `notes` key, a sorted list of strings such as `"<url>: fetched from the Census Bureau's mirror; www2.census.gov's firewall refused it"`, with MI4 re-registered by the ruling that admits it.
  - **It is needed only if the retries fail,** so nothing waits on it for about a day.

**The tile-count miss, read from the list as it is** (read-only, as ruled):

| region | tiles | core: its footprint meets a covered point's 250 km reach | margin: there only for the one-tile ring |
|---|---|---|---|
| CONUS | 4,213 | 3,858 | 355 |
| Alaska (the Aleutians past 180° included) | 4,862 | 4,497 | 365 |
| Hawaii | 563 | 442 | 121 |
| **total** | **9,638** | 8,797 | 841 |

- **It explains the count, so no reach = 0 run is needed.** My 5,000-7,000 estimate left out two things:
  - **Alaska's reach:** the Aleutians run about 1,900 km west, and at high latitude 250 km is more degrees and z9 tiles are smaller. The estimate assumed about 1,800 Alaskan tiles; the list holds 4,862.
  - **The Northwestern Hawaiian Islands:** Hawaii's covered lattice has 611 intervals, 160 of them west of 161° W. The estimate assumed about 30 tiles; the list holds 563.
  - **CONUS is about 11 % above its box estimate,** from the reach and the margin.
- **The named alternative, "`tilesForCoverage` over-collects", is excluded for the margin.** **0 of the 841** margin tiles lie beyond the ring: every one touches a core tile. The core test is a separate reading over `coverage.bin`, not the instrument's code.
- **What this reading cannot show:** whether the list misses a tile it should hold. That would need the instrument again, and nothing depends on it, since D1's gate is size.

**The tiles' source bits** (A1.4; `ingest-basemap.ts tile-bits`, from Natural Earth 10 m):

| | tiles |
|---|---|
| with SRTM | 3,962 |
| with GMTED2010 | 1,806 |
| with ETOPO1 | 5,078 |
| with NRCAN | 1,494 |
| with **no** bit | **0**. The run refuses any, since land and ocean cover the world, so this guard ran and held |
| `terrain-sources.bin` | 48,202 B |

- 3,914 tiles are ocean-only (ETOPO1), mostly the Aleutian and Northwestern Hawaiian reach.

**The tiles, fetched.** All **9,638**, totalling **706,629,891 B**, exactly what the HEADs had measured: 0 size mismatches, and every one a PNG by its magic bytes.
- Node's HTTP client (undici) crashed four times from inside its parser, outside any `try`.
- Each crash saved the log first, and the next run resumed, re-hashing every file. So none of the four can have logged a wrong file.

### The bucket, announced with the measured numbers (before it exists)

**`blipscope-basemap`,** R2, with no lifecycle expiry. Measured on a PARTIAL dry run (16,033 of 16,035 files; the two refused county files add a few MB at most):

| object | bytes | count |
|---|---|---|
| vector cells | 1,048,998,737 | 1,397 (the largest 10,290,428; none over 24 MB) |
| the terrain mirror | 706,629,891 | 9,638 |
| `manifest.json` (all 16,035 sources, tiles included) | 3,866,886 | 1 |
| `index.json`, `regions.json`, `coverage.bin`, `terrain-sources.bin` | 138,790; 166,835; 121,428; 48,202 | 4 |
| `current` | about 30 | 1 |
| **the store** | **1,759,970,769 (1.76 GB)** | **11,041 puts** |

- **Storage:** with the account's ~0.7 GB, about 2.5 GB of the free 10 GB. **$0.**
- **Class A, once per ingest:** 11,041 puts, plus repairs. With the weather server's ~880k a month, that is about 891k of the free 1M. **$0.**
- **Class B:** the verify reads each object back once, 11,041 reads, against the free 10M. **$0.**
- **So it does not wait for Daniel's yes** (ruled). It is created at the start of D1, immediately before the upload. An empty bucket serves no purpose before then, and D1 waits for the two refused files.
- **The upload:** Wrangler under the existing login, 8 at a time (ruled), then verify → repair → verify → seal.

**The two refused files:** the 24-hour retry is running, one file at a time, with the User-Agent `BlipscopeBasemapIngest/1 (+https://github.com/Valar-Systems/valar-scopes)`. Its first attempt, at 04:00Z on 2026-10-28, was refused again. Every attempt is logged to `retry-attempts.jsonl`.
