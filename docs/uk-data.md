# UK weather and UK map layers: the plan

**Status: a plan for review. No code, and no Cloudflare resource is created by anything in here.**
- **Asked for by:** the reviewer chat's brief "UK weather and UK landmarks" (2026-10-28), pasted by Daniel.
- **Branch:** `docs/uk-data`, pushed without a PR.
- **The rest of this document is written after the measurements below.** This first section is frozen before any
  of them is taken, so the readings cannot choose their own meaning.

## Predictions, frozen before any measurement (2026-10-28, ~04:05Z)

**What has been seen before freezing, so it is not counted as a prediction:**
- the Met Office bucket's layout and one day's file names
  (`radar/<YYYY>/<MM>/<DD>/<YYYYMMDDHHMM>_ODIM_ng_radar_rainrate_composite_1km_UK.h5`, at :00/:15/:30/:45);
- the AWS registry's text ("four images per hour", "within 20 minutes", CC BY-SA);
- OPERA's documentation (CIRRUS reflectivity 1 km / 5 min, NIMBUS rain rate, CC BY 4.0, the CloudFerro
  `openradar-24h` cache with keys `OPERA@<YYYYMMDD>T<HHMM>@0@<DBZH|RATE|ACRR>.h5`).
No `LastModified` time, no file's contents and no OPERA listing have been read.

**The instruments.**
- **Times.** A frame's validity time V comes from its key, and is checked against the file's own HDF5 `what/date` +
  `what/time`. Its publish time P is the object's `LastModified`. Latency L = P − V; cadence = the step between
  consecutive V.
  - **Met Office:** every object under `radar/2026/10/<DD>/` for the 7 complete UTC days 2026-10-21 to 10-27.
  - **OPERA:** every object in the 24 h cache, per quantity.
- **Coverage.** A pixel is covered when its value is not the file's `what/nodata`. Test centres are public
  airports, named by code; their positions are loaded inside the script and never printed:
  LHR, MAN, BHX, NCL, EDI, GLA, ABZ, INV, SYY, KOI, LSI, CWL, NQY, ISC, BFS, BHD, LDY, and DUB as a neighbour.
  Each is graded on the pixel containing it and on the covered share of pixels within 160 km, over at least
  4 frames of each source.
- **The device.** A v19 Blipscope hides a crop whose frame is more than 600 s old (`wxpolicy::kMaxAgeS`,
  `include/WxPolicy.h` on `feat/wx-device`). So the **showable share** S is the share of the measured span in which
  the newest published frame is ≤ 600 s old. It is computed with no ingest lag, so it is an upper bound.

**The predictions.**

| | measured | predicted |
|---|---|---|
| **M1** | Met Office cadence, 7 days | **≥ 99 %** of steps are exactly **900 s** |
| **M2** | Met Office latency L | median in **[600, 1,200] s**; p95 **≤ 1,200 s** |
| **M3** | Met Office file contents (3 files) | quantity `RATE`; one 1 km grid; `where/projdef` a transverse Mercator (`+proj=tmerc`) |
| **M4** | Met Office coverage | every UK centre covered on its pixel in every frame, **NI's three included**; DUB covered too |
| **S-M** | Met Office showable share | **0.0 %** |
| **O1** | OPERA cadence, per quantity | `DBZH` **300 s** and `RATE` **900 s**, each ≥ 99 % of steps |
| **O2** | OPERA latency, per quantity | median in **[300, 900] s**; p95 **≤ 1,200 s** |
| **O3** | OPERA's UK coverage | UK radars named in the composite's `how/nodes`, and every centre, **NI's three included**, covered on its pixel in every frame |
| **S-O** | OPERA showable share, `DBZH` | **in [0 %, 50 %]**: no source keeps a v19 device showing UK weather even half the time |

**What the results will mean, decided now.**

| outcome | meaning for the plan |
|---|---|
| S-M and S-O both as predicted | **a v19 device cannot show UK weather without a firmware change** to its 600 s age limit, whatever the server does. The plan names the change; it does not try to hide age |
| S-O above 50 % | OPERA alone could serve v19 devices part-time; the plan weighs it against the Met Office's licence and resolution |
| M4 or O3 fails for NI | that source cannot serve Northern Ireland alone; the plan names the other |
| O3 fails for Great Britain (UK radars absent or not covered) | OPERA is not a UK source; the Met Office is the only free one |
| **anything else, or an instrument control failing** | **stop, report the raw reading, and decide. No row is re-graded** |

**Controls, run before a reading is believed.** A control that fails makes its rows BLIND, and BLIND counts as FAIL.
- **C1, the listing:** the newest listed key is fetched by direct GET and starts with the HDF5 signature
  `89 48 44 46 0D 0A 1A 0A`; a made-up key in the same folder is refused. Every listing pages to the end.
- **C2, the time parser:** on the sampled files, V from the key equals the file's own `what/date`/`what/time`.
- **C3, the coverage reader:** each grid's corner pixel (open sea, far from any radar) reads nodata in every frame,
  and FRA (inside OPERA) reads covered. A reader that calls everything covered, or nothing, is blind.

**Sabotages, each of which must turn its row red.**
- M1 / O1: drop every third key, and the ≥ 99 % row fails.
- C2: read `HHMM` as `MMHH`, and the control fails.
- C3: compare against `what/undetect` instead of `what/nodata`, and the corner control fails.
- S-M: raise the device limit to 3,600 s, and S-M rises above 0 %.

## Results: the times (read 2026-10-28 04:02:54Z, recorded before any further reading)

**The freeze's own time.** The section above says "~04:05Z", an estimate typed before committing. The commit,
`489656c`, is stamped **04:00:52Z** and was pushed in the same command, so it precedes this first reading by two
minutes. The frozen text is left as it was.

**One statement in it is not exact.** "No OPERA listing" had been read, it says. One had: the cache bucket's **top
level**, which held a single prefix, `2026/`. It showed no frame key and no time, so no prediction could have used
it. The Met Office day listing read before the freeze printed keys only, with no `LastModified`.

**The instrument:** `times.py` (sha256 `ec95328d7b9b…`), its raw output `times1.json` (sha256 `0e927cbe40bb…`). Both
are committed under `docs/uk-data/instruments/`.

| | read | verdict |
|---|---|---|
| **C1** | both sources: the newest key answers 206 with the HDF5 signature; a made-up key answers 404 | **passed** |
| **M1** | 672 frames, 96 on each of the 7 days; **671 / 671 steps are 900 s**. Sabotage (every third key dropped): 50.1 % | **as predicted**; sabotage red |
| **M2** | latency min **321**, p50 **334**, p95 **393**, max **490 s**; none over 1 h | **MISS**: predicted a median in [600, 1,200] s. The registry's "within 20 minutes" is an upper bound, not the typical delay |
| **S-M** | **29.0 %** (22.3 % with 60 s of lag, printed). Sabotage (limit 3,600 s): 100 % | **MISS**: predicted 0.0 %; sabotage red |
| **O1** | `DBZH` 336 frames, **335 / 335 steps 300 s**; `RATE` 112 frames, **111 / 111 steps 900 s**. Sabotages: 50.2 % and 50.0 % | **as predicted**; sabotages red |
| **O2** | `DBZH`: min 243, p50 **250**, p95 **286**, max 473 s. `RATE`: p50 **603**, p95 605 s | **`DBZH` MISS** (median under 300 s); **`RATE` as predicted** |
| **S-O** | `DBZH` **99.3 %** (94.2 % with 60 s of lag, printed). `RATE` 0.0 %. Sabotage (limit 3,600 s): 100 % | **MISS**: predicted [0 %, 50 %]; sabotage red |
| printed, not predicted | `ACRR` (one-hour accumulation): 112 frames every 900 s, latency p50 606 s. The cache also holds a cloud-optimised GeoTIFF beside every `.h5` | — |

**What the misses are, and what they are not.**
- **Both sources are faster than predicted.** Every miss is in the same direction: frames arrive sooner, so a
  v19 device could show them more of the time.
- **The outcome table's first row, "a v19 device cannot show UK weather without a firmware change", is
  therefore not the result.** That reading is withdrawn, not adjusted.
- **S-O above 50 % has a frozen meaning:** OPERA alone could serve v19 devices. **S-M at 29 % has none**, because the
  table assumed 0 %. Per the brief, it is reported here and its meaning is for the reviewer.
- **The alternative to name before believing M2/O2:** `LastModified` might not be the moment a frame becomes
  listable (a multipart upload's start, say), which would make latency look shorter than a fetcher sees. A live
  poller (`poll_live.py`) records each new frame's first appearance in the listing for 32 minutes. Its result is
  printed beside these rows and does not grade them.

**The poller's result** (about 04:05-04:37Z; `live1.json`, committed):
- **13 frames appeared, with 0 poll errors:** 3 Met Office, and 6 `DBZH`, 2 `RATE` and 2 `ACRR` from OPERA.
- **Each was first listed 0.4-10.3 s after its `LastModified`.** The poll step is 10 s plus the listing's own time,
  so every gap is inside the instrument's resolution.
- **So the alternative is excluded at that resolution:** `LastModified` is when a fetcher can see a frame.
- **The same frames' latencies,** as data:
  - the Met Office's 04:00, 04:15 and 04:30 frames: 428, 415 and 334 s;
  - OPERA `DBZH`: 244-251 s.

## Results: contents and coverage (read 2026-10-28 ~04:10Z)

**The instrument:** `coverage.py`, over 4 frames per product: Met Office 2026-10-27 06:00, 12:00, 18:00 and 23:45;
OPERA `DBZH` 10-27 12:00 and 18:00, 10-28 00:00 and 03:55; `RATE` the same, with 03:45 last. Raw output `cov2.json`,
and the sabotage runs `cov_sab_c2.json` and `cov_sab_c3.json`, are in `docs/uk-data/instruments/`.
- **The committed copies round every float to 6 significant digits.** The repo's device-id hook refused the raw files,
  because a float written out in full (a step of about 9.3e-10 mm/h, to 16 digits) holds a 16-character hex-like run.
- **The raw files' sha256:** `cov2` `42e3c1406720…`, `cov_sab_c2` `61ada394429f…`, `cov_sab_c3` `8df063664e23…`.
- No verdict depends on a digit past the sixth.

| | read | verdict |
|---|---|---|
| **C2** | in all 12 files, the key's time equals the file's own `what/date` + `what/time` | **passed**. Sabotage (`MMHH`): fails in every product, red |
| **C3** | the four corner pixels read nodata in all 12 files; FRA reads covered in all 8 OPERA files | **passed**. Sabotage (compare with `undetect`): the corner control fails in every product, red |
| georeference (printed) | the `LR` corner against `UL` + size × scale: 2.8 m / 0.6 m (Met Office), 0.0 m (OPERA). So the corners are the image's outer edges, not pixel centres | — |
| **M3** | `RATE`, mm/h, `float32`; one dataset, one data group; 1,725 × 2,175 at 1,000 m; `+proj=tmerc` on the Airy ellipsoid; `nodata` −1, `undetect` 0; gzip-6 in 75-row chunks; `how/zr_a` 200, `zr_b` 1.6; 17 nodes, `ukcas` (Castor Bay, NI) and `iesha` (Shannon) among them | **as predicted** |
| **M4** | covered on its pixel in every frame: all 16 other UK centres, **BFS, BHD and LDY included**, and DUB. **LSI (Sumburgh, Shetland): uncovered in all 4 frames**, its 160 km disc 39.8 % covered | **MISS** (LSI) |
| **O3** | 16 UK nodes in both products' `how/nodes` (`ukcas` included), plus `iedub` and `iesha` in `DBZH`. `DBZH`: every centre covered on its pixel in every frame **except LSI** (disc 44.7 %). `RATE` (2 km): **LSI and SYY** uncovered in all 4 frames | **MISS** (LSI; SYY in `RATE` only) |

**Beside the verdicts, as data:**
- **Northern Ireland is covered by both sources**, on every frame, at all three of its centres.
- **Shetland is outside both.** KOI (Orkney) is covered everywhere, with a disc 86-88 % covered.
- **The alternative to the LSI reading:** a wrong pixel lookup. **Excluded:** the georeference closes to under 3 m;
  two projections (tmerc and laea) and three products agree on LSI; and KOI, the nearest centre, reads covered.
- **The products' values:**
  - Met Office rain rates are continuous floats: about 65,000-200,000 distinct values a frame, up to 86 mm/h.
  - OPERA `DBZH` is −32 to 78.5 dBZ, in 0.5 dB steps.
  - OPERA `RATE` is in 0.01 mm/h steps. It reaches 687 mm/h, which no surface rain does: clutter.

**One more reading, not frozen, printed:** OPERA's GeoTIFF beside each `.h5` is a cloud-optimised GeoTIFF:
- **Layout:** 2 `float32` bands (the value and the quality index), 512 × 512 deflate tiles, 4 overviews.
- **Against the HDF5:**
  - **nodata stays −9,999,000;**
  - **undetect becomes NaN;**
  - every data cell is **bit-identical** (max |Δ| 0.0).
  So the TIFF keeps "no coverage" (14) apart from "dry inside coverage" (0), which WXC1 needs.
- **The UK window** is the UK's land extent plus the radar's 2.02° maximum view:
  - **16 of the 72 full-resolution tiles,** 603,607 compressed bytes: 30.2 % of the file's level-0 tile bytes.

## 1. UK radar: the two sources, and the recommendation

### 1.1 Side by side, from the readings above

| | Met Office UK composite | OPERA CIRRUS (EUMETNET) |
|---|---|---|
| where | AWS Open Data, `met-office-radar-obs-data`, eu-west-2, anonymous S3 (and an SNS topic per new object) | CloudFerro S3 `openradar-24h` (a 24 h rolling cache), anonymous; also the MeteoGate API, which rate-limits anonymous use |
| quantity | surface rain rate, mm/h (`RATE`), from Z = 200 R^1.6, VPR-corrected | **maximum reflectivity, dBZ** (`DBZH`, product `MAX`): the same kind of quantity as MRMS's composite |
| grid | 1,725 × 2,175 at 1 km, transverse Mercator (Airy) | 3,800 × 4,400 at 1 km, Lambert azimuthal equal-area (WGS84), all of Europe |
| cadence, measured | **900 s**, 7 days, no gap | **300 s**, 24 h, no gap |
| latency, measured | p50 **334 s**, p95 393 s, max 490 s | p50 **250 s**, p95 286 s, max 473 s |
| a v19 device's showable share (an upper bound) | **29.0 %** | **99.3 %** (94.2 % with 60 s of lag) |
| NI (BFS, BHD, LDY) | covered | covered |
| Shetland (LSI) | not covered | not covered |
| format | ODIM HDF5, gzip chunks | ODIM HDF5, **and a cloud-optimised GeoTIFF** that keeps nodata and undetect apart |
| licence | **CC BY-SA 4.0**: "British Crown copyright 2024-2025, the Met Office, is licensed under CC BY-SA" (the AWS registry entry, its `License:` field) | **CC BY 4.0**, stated inside every file (`how/license`); "EUMETNET holds the property rights" of the composites (the ORD documentation) |
| service | "a non-operational service"; support Mon-Fri, "3-5 business days" | no service level stated; support at support.opera@eumetnet.eu |

Also in OPERA's cache, not recommended:
- **NIMBUS `RATE`:** 2 km, 900 s, latency 603 s, so 0 % showable on v19; SYY uncovered;
- **`ACRR`:** the one-hour accumulation.

### 1.2 Recommendation: OPERA CIRRUS `DBZH`, read from its GeoTIFF, over a UK window

- **It is the only source a v19 device can show most of the time.** That is the frozen meaning of "S-O above 50 %".
- **It is the same quantity as `cref`:** column-maximum reflectivity in dBZ.
  - So the code mapping is `cref`'s own: `codeOfComposite` in `proxy/src/wx/products.ts`, where below 15 dBZ is 0
    and 15-75 dBZ in 5 dB steps is 1-13.
  - The device's S2 palette means the same thing on both sides of the Atlantic.
- **Its licence asks for attribution only.** Under CC BY 4.0 §3(a)(4), our crops may carry our own terms, provided
  they don't stop a recipient complying with CC BY. CC BY-SA would bind the crops themselves (§1.5).
- **The GeoTIFF saves an HDF5 parser.**
  - A UK ingest range-reads 16 of 72 tiles, about 604 KB a frame.
  - TIFF tags, deflate (`DecompressionStream`) and `float32` are all a Worker needs.
- **The Met Office feed stays on record as the fallback** if OPERA ever drops the UK, with its price stated: a
  firmware change (§3) and share-alike (§1.5).

**What OPERA's frame time means.**
- The key's time (`what/time`) is the **end** of a 10-minute window: `starttime` 03:45:01 and `endtime` 03:55:00 for
  the 03:55 frame.
- The file's own comment says some countries' volumes are **advected forward** at Météo-France (Lucas-Kanade,
  pysteps) to reach that time.
- The WXC1 frame epoch would be the file's `what/time`: the file's own claim about the time it shows.

### 1.3 How it becomes WXC1 crops

**Nothing about the crop, the endpoint or WXC1 v1 changes.** Only a second product is added, and with it a second
frame store.
- **The product:** a registry row in `proxy/src/wx/products.ts`, proposed id **`opuk`**.
  - **Its window** is the UK's land extent plus the radar's 2.02° maximum view.
  - **At 0.01°** that is a WXG1 grid of **1,515 × 1,455 cells: 9 tiles, 1,179,712 B a frame.** CONUS is 98 tiles,
    12,845,120 B.
- **Why 0.01° and not finer:** the device decodes into `kWxCellsMax` = 406 × 406 cells (`src/AircraftManager.h` on
  `feat/wx-device`).
  - At 0.01° the largest crop is 405 × 405 at any latitude, because `ProjectAt` clamps the view at 2.0°. A finer grid
    would overflow it.
  - The device maps pixels from the crop header's own geometry (`include/WxLayer.h`), so the cell size is not
    otherwise a firmware assumption.
- **Resampling, LAEA 1 km to 0.01°:** for each target cell, the **strongest echo wins** among the source pixels
  whose centres fall inside it. With none inside, the pixel containing the cell's centre is used. That is the same
  rule as the crop's coarsening (`blockCode`).
  - Nearest-neighbour alone would skip about 1 source row in 10: a 0.01° cell is 1.11 km tall against 1 km pixels.
    A thin squall line is exactly what that loses.
- **The codes:**
  - NaN (undetect) → **0**;
  - −9,999,000 (nodata) → **14**;
  - any other value → `codeOfComposite`;
  - code 15 stays unused, as for `cref`.
- **The ingest:** inside the existing `blipscope-wx-ingest`, on the same cron. The second product is checked
  **concurrently** with `cref` in each check, so a NOAA hang does not delay OPERA. Per check:
  - list the cache with `start-after`, covering the last 20 min only;
  - if a new frame is listed, read the COG's header, range-read the 16 tiles, inflate, resample, and store with
    `If-None-Match: *`;
  - move `latest` forward only, as `ingest.ts` does.
- **The reads per frame:** every IFD and tile index of the COG lies in its first **5,603 bytes** (read from the
  03:55 file). So a frame is one 8 KB range read, then the 16 tiles.
  - **The tiles are stored in row-major order, each 8 bytes after the last** (GDAL's 4-byte leader and trailer;
    71 of 71 gaps measured).
  - So one window row of tiles is one range read, gaps included: about 4 reads a frame.
  - The first draft of this line said "contiguous" without checking. A check found 0 of 63 pairs contiguous,
    because of the 8-byte gaps.
- **This product's cost rule:** it prunes only on the check that stores a frame (§5's R2 Class A row).
- **The source-to-cell map is static,** since both grids are fixed, so it can be computed once rather than per frame.
  Its speed is for the implementation's own predictions.

### 1.4 Rain rate to the palette's codes (the Met Office fallback only)

If the Met Office feed is ever used, rain rate converts back to reflectivity through the file's own `zr_a` and `zr_b`:
dBZ = 10·log10(200) + 16·log10(R).
- That recovers the reflectivity the rate came from, **after** the Met Office's VPR correction.
- So it is a rain-equivalent reflectivity, not raw reflectivity.

| code | dBZ | R, mm/h |
|---|---|---|
| 0 | below 15, or `undetect` (0 mm/h) | below 0.316 |
| 1 | 15-20 | 0.316-0.648 |
| 2 | 20-25 | 0.648-1.332 |
| 3 | 25-30 | 1.332-2.734 |
| 4 | 30-35 | 2.734-5.615 |
| 5 | 35-40 | 5.615-11.531 |
| 6 | 40-45 | 11.531-23.679 |
| 7 | 45-50 | 23.679-48.625 |
| 8 | 50-55 | 48.625-99.852 |
| 9-13 | 55 and up | 99.852 and up (the four frames read peaked at 86 mm/h) |
| 14 | `nodata` (−1) | — |

### 1.5 What CC BY-SA 4.0 would require of the crops (the legal code read in full: `legalcode.txt`, 20,138 B)

- **The crops are Adapted Material** (§1(a)): reprojected, resampled and re-coded.
- **§3(b)(1):** the licence we apply to them must be BY-SA 4.0 or a BY-SA-compatible licence.
- **§3(b)(3):** we "may not offer or impose any additional or different terms or conditions on, or apply any
  Effective Technological Measures to, Adapted Material that restrict exercise of the rights granted under the
  Adapter's License".
  - So a customer could pass the crops on to anyone, and no terms of ours could forbid it.
  - Whether `/wx`'s device authentication counts as such a measure is **a legal question I cannot settle.** It gates
    our service, not the bytes a recipient holds, but that is an argument, not a ruling.
- **§4(b):** our R2 frame store, which holds the whole UK composite, would itself be Adapted Material.
- **Attribution, §3(a):** the creator, the copyright notice, the licence's URI, and a note that we modified it.
  §3(a)(2) allows a link to a page that carries all of it.

**CC BY 4.0, for OPERA (read in full, 18,657 B):**
- **Attribution:** the same conditions, §3(a)(1)-(2).
- **Adapted Material:** nothing more than §3(a)(4).
- **The "no downstream restrictions" clause, §2(a)(5)(B),** applies to the *unmodified* material, which we never pass
  on.

**The OPERA credit line, proposed** for `/credits` (the wording is for the reviewer):
> Weather radar outside the US: EUMETNET OPERA CIRRUS maximum-reflectivity composite (produced by Météo-France for
> EUMETNET), licensed under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). Reprojected to a 0.01°
> grid and binned to 5 dBZ steps by Valar Systems.

### 1.6 Coverage edges

- **Northern Ireland:** covered by both sources, on every frame read, at BFS, BHD and LDY.
  - Castor Bay (`ukcas`) is in both node lists;
  - Dublin and Shannon are in OPERA's.
- **Shetland:** covered by neither.
  - Sumburgh's pixel was nodata in every frame, and its 160 km disc was 40-45 % covered.
  - Orkney (KOI) is covered.
- **Ireland, France and the North Sea:** inside OPERA's composite. A GB or NI view that reaches them shows their
  weather.
- **The Isle of Man and the Channel Islands** are not part of the UK. They fall inside the `opuk` window, so they get
  weather. They get no map layers (§2): no source read here covers them.

## 2. UK map layers

### 2.4 Terrain: Terrain Tiles z9 over the UK, and EU-DEM's terms

**What the tiles contain.** Read by a research agent this session, from joerd's docs and from the tiles themselves:
- **joerd's z9 row** (`docs/data-sources.md`, read at `b6d1081504fb`, verbatim): "`ETOPO1` | `SRTM`, `NRCAN` in
  Canada, `EUDEM` in Europe, with `GMTED` in high latitudes above 60°".
- **The tiles' own source lists** (`x-amz-meta-x-imagery-sources`, from HEAD requests on 480 z9 tiles around the UK):
  - **every one of the 207 tiles that touch UK land lists `eudem`;** 38 list it alone;
  - the only families anywhere in the box are `etopo1`, `eudem`, `gmted` and `srtm`;
  - **no Environment Agency LIDAR at z9:** joerd's row puts `data.gov.uk` from z10;
  - **NI:** 16 tiles, 12 with all four sources and 4 with EU-DEM only;
  - **north of 60 N (Shetland):** 7 tiles; 5 have no SRTM, so GMTED is used there.
- **EU-DEM's files are named `eudem_dem_5deg_*`,** which matches v1.0's tiling. Every tile's `Last-Modified` is
  2017-11-15.
- **Not established:** which source wins pixel by pixel. joerd's public code has no EU-DEM module.

**EU-DEM's terms.**
- **The credit, 2017 EEA page** (archived 2017-11-30), verbatim: "The following credit must be displayed when using
  these data: "Produced using Copernicus data and information funded by the European Union - EU-DEM layers.""
- **Regulation (EU) 1159/2013:**
  - **Art. 7:** use for "reproduction; distribution; communication to the public; adaptation, modification and
    combination with other data and information", "worldwide without limitations in time".
  - **Art. 8:** inform the public of the source; imply no Union endorsement; "Where that data or information has been
    adapted or modified, the user shall clearly state this."
  - **Neither the Regulation nor the current CLMS data policy distinguishes commercial use.** The CLMS policy adds:
    "the user has all intellectual property rights to the products he/she has created".
- **EU-DEM is discontinued** ("not maintained anymore", CLMS). No source found says that changes the terms of data
  already derived, and Art. 7's "without limitations in time" argues it does not. **That is an inference, labelled
  as one.**
- **Not researched:** EU-DEM was built from SRTM and ASTER GDEM. ASTER's own terms were not read. The EU-DEM licence
  is the one its distributor attached to the product.

**So the terms can be met,** in the same shape as A1.3's table for the US. EU-DEM gets a tile bit, a credit line and
the modification statement. The processing line already gives that statement, as it does for GMTED2010.

| source | where, in UK tiles | term | met by |
|---|---|---|---|
| EU-DEM | all UK land | credit; state the modification; no endorsement | `credit.terrain: Produced using Copernicus data and information funded by the European Union - EU-DEM layers`, plus the processing line |
| SRTM | land below 60 N | credit; **and its notice, since these packages are distributed outside the US** (A1.3 recorded it and did not act, because v1 served US origins only) | the credit line, and the §3.15 b notice A1.12 already carries |
| GMTED2010 | above 60 N (Shetland) | credit; describe modifications | its line and the processing line |
| ETOPO1 | sea | NOAA's formal credit | its line |

- **joerd's attribution block carries an Environment Agency line ending "All rights reserved".** It is **not** for
  these tiles: that data starts at z10. Copying joerd's whole block would put an "All rights reserved" line on data
  the packages do not contain.
- **The alternative source, OS Terrain 50:**
  - **What it is:** OGL v3, a 50 m grid on British National Grid, 157 MB, no registration.
  - **Its gap: Great Britain only.** NI would need OSNI's 50 m DTM, whose licence is listed two ways (OpenDataNI says
    `uk-ogl`; a data.gov.uk mirror says "LPS Open Government Data Licence"), which was not resolved.
  - **It changes the pipeline:** a second terrain format, a reprojection, and its own hillshade inputs, where §5.6
    is written in Terrarium pixels.
  - **Recommended against for v1.**

### 2.5 A coverage extension, or BMPK v2? My view

- **The bytes and the syntax are unchanged.** The version byte stays 1, and a v1 client parses a UK package exactly as
  it parses a US one.
- **The UK rules are new layer definitions beside §5's.** They change none of §5's, and no US package changes by a
  byte.
- **But §4 defines four fields by their US source,** and §12 says "a change to any field's meaning … is version 2":
  - `ROADS` class: "1 = TIGER `S1100`, primary; 2 = `S1200`, secondary";
  - the `RIVERS` names: "the TIGER `FULLNAME`";
  - the `PEAKS` name: "GNIS `feature_name` as written";
  - the `PLACES` name and `pop`: "the Gazetteer `NAME` as written" and `POPESTIMATE2024`.
- **So, read literally, a UK package's class byte carries a meaning §4 does not define.**
- **My view: it is a v1 coverage extension, provided §4's four fields are restated by role,** with a per-coverage
  source table:
  - class 1 = the national primary road network, class 2 = the secondary;
  - a name = the source's own name, as written;
  - `pop` = the source's population figure, 0 = none.
  - **Why it qualifies:** what a client does with each field (styles class 1 apart from class 2, labels a name, ranks
    by `pop`) is the same for every coverage. **No v1 client would read a UK package wrongly.** That is the test the
    PROVENANCE amendment passed.
  - **Evidence from the one client that exists:** Weatherscope's place-label rule drops a trailing " city" once
    (`src/weatherscope/WxLogic.h:457`). A UK name has no such suffix and passes through unchanged.
- **Where I think otherwise: bilingual names.** Welsh places and features can carry two names. v1 has one text field,
  so choosing one is a data rule with no US precedent. If the answer is "both", that is a new field, and so v2 (§6,
  Q10).
- **A new coverage also needs what v1 has once:** the coverage bits (`coverage.bin`), the credit and attribution lines,
  and the terrain bits. These are the route's (the map server's plan), not the format's.

## 3. The device: what a v19 Blipscope in the UK can show with no firmware change

**The assumption:** v19 carries the weather layer as it stands on `feat/wx-device` (`f9184ef`, #445).
- #445 is not merged. Its merge waits on the starved-seconds counter and the #245 ruling.
- If v19 ships without it, no weather reaches any v19 device.

**What it asks for:** always `/wx/cref`. The call is `CloudFeed::WeatherUrl(req->cloudBase, "cref")` in
`src/AircraftManager.cpp` on `feat/wx-device`.
- **Today,** a UK query's box misses the CONUS grid. The server answers an all-14 crop, and the device shows its
  "outside coverage" note (WD3 (f)).
- **So UK weather on v19 needs one server change and no firmware.** The handler serves `cref` from whichever product's
  coverage holds the quantised point: CONUS for MRMS, the UK window for `opuk`, and today's answer for neither.
  - **The boxes are static per product,** so choosing costs no R2 read.
  - **The ETag carries the product actually served.** A device that moves between regions can never get a 304 for
    the wrong one.
  - **The two boxes do not overlap,** and a test asserts it.

**The age limits, product by product:**

| where | the limit | with OPERA `DBZH` | with the Met Office |
|---|---|---|---|
| Blipscope v19, `wxpolicy::kMaxAgeS` (`include/WxPolicy.h`) | a crop over **600 s** old is not drawn | drawn 99.3 % of the time at best, 94.2 % with 60 s of lag; a short gap when a frame is late | drawn **29.0 %** at best: **not usable without a firmware change** |
| the server, `Product.maxAgeS` (`cref`: 600) | over it, **503 `wx_stale`** | **must be above 600 for `opuk`** (proposed 1,800), as explained below | — |
| Weatherscope: the orange time, and `kStaleAfterS` (`src/weatherscope/WxScreens.h`) | the time turns orange over 10 min; "RADAR STALE" over 30 min | never orange in normal running | orange for about 70 % of each 15-minute cycle: the frame's age runs from 5.6 to 20.6 min |

**Why the server's limit for `opuk` must be above the device's.**
- **The device's rule:** it treats every reply that is neither 200 nor 304 as an error, and **doubles its
  interval** (WD3 (e): 60 → 120 → 240 s).
- **With the server's limit at 600 s:** a device polling in the seconds while an OPERA frame is late gets a 503,
  waits 2 minutes, and misses the next frame.
- **With it at 1,800 s:** the device gets a 200 carrying the late frame's true time, hides it by its own rule, and
  polls again in 60 s.
- **The frame time is never altered.** The device sees the frame's real age and decides.

**Credit lines.** The Blipscope shows **no data credit on the glass.**
- **Where credits live:** in every cloud build, the config page links "Full credits" to `<cloud base>/credits`
  (`src/ConfigurationWebServer.cpp`).
- **What that page lists:** its data sources are static text in `proxy/src/photolicense.ts` (`renderCreditsHtml`).
- **So the OPERA line is a server change.** CC BY 4.0 §3(a)(2) allows exactly this: "a URI or hyperlink to a
  resource that includes the required information".
- **Found while reading, as information:** `/credits` has **no NOAA MRMS line.** `docs/wx-server.md` put it in "the PR
  that binds the bucket". That binding (#461) went live on 2026-10-23, and the live page, read today, has no weather
  line.

**What would need firmware:**
- **the Met Office feed,** at any share above 29 %. Its 15-minute cadence needs a per-product age limit on the
  device, such as one the server sends and the device honours;
- **any credit on the glass,** if one is wanted;
- **the map layers, whatever the server does.** No Blipscope code reads BMPK; only `src/weatherscope/` does.
  `docs/basemap-route.md` says "The Blipscope adopts the route after its v19, as a setting".

## 4. The work, and its order

**Each step is its own PR, with its predictions frozen and pushed before its code, a sabotage per check, and BLIND
read as FAIL.** Nothing below creates a Cloudflare resource.

**The radar: server only. All of it can run before Blipscope v19 ships.**

| | step | needs |
|---|---|---|
| R0 | rulings on this plan (§6) | — |
| R1 | **predictions for `opuk`**, frozen: | R0 |
| | - the COG reader, on committed real frames, against the HDF5's own cells; | |
| | - the resampling (the strongest-echo rule, with the nearest-neighbour sabotage that must drop a one-pixel line); | |
| | - the codes; the WXG1 size; | |
| | - the `cref` dispatch (no overlap, the ETag's product, a UK airport served `opuk`, a CONUS airport still `cref`, a point in neither unchanged); | |
| | - the 1,800 s limit; | |
| | - the window's covered share per frame, printed in `/healthz` (Q8), with a sabotage (an all-nodata frame) that must show 0 | |
| | - CPU and live memory in Node, with a bar set from I7's workerd factor | |
| | **and a week of OPERA timing.** The cache holds only 24 h, so it is read once a day for 7 days. This plan's 99.3 % is one day | |
| R2 | the code and its host and vitest checks; a local dry run against live OPERA, which also measures CloudFerro's failure rate from a Worker-like fetch | R1 |
| R3 | **the cost statement, re-read, and Daniel's yes** (§5: CPU likely crosses 30M by cents); then the ingest deploys, in the same Worker | R2 |
| R4 | the proxy: | R3 |
| | - the dispatch; | |
| | - `/credits` gains the OPERA line, and the missing NOAA line; | |
| | - `smoke-prod.sh` gains a UK check at a public airport, served from `opuk` | |
| | staging, then production, under the standing deploy rule | |
| R5 | UK weather on the glass: a bench board running #445's code with its location set to a public UK airport (§6, Q7) | R4, a board |

**The UK customer sees UK weather when both v19 (with #445) and R4 are live,** whichever comes later. R1-R4 do not
wait for v19.

**The map layers: they wait for the map server's US v1, and reach a Blipscope only after its BMPK client.**

| | step | needs |
|---|---|---|
| M0 | rulings on the UK rule set (§2, §6) and on whether it is a v1 coverage extension | R0 |
| M1 | **an offline size dry run of the UK sources:** packages at the UK centres, against the 32 KB cap. No Cloudflare | M0; can run before v19 |
| M2 | the UK truth tables, frozen; then the map server's ingest gains the UK as a second coverage: | the map server's US v1 live |
| | - its sources; its coverage bits (GB + NI); | |
| | - the terrain tile bits with EU-DEM; | |
| | - the credit and attribution lines | |
| M3 | the route serves UK origins | M2 |
| M4 | **the Blipscope's BMPK client** (firmware, after v19, "as a setting") | after v19 |

**Not mine to schedule:** M2 and M3 are the map server session's, and M4 is the Blipscope firmware's. Here they are
the order this plan proposes, for the reviewer.

## 5. Costs against the free tiers

**The "now" column is `docs/wx-server.md`'s:** account figures measured 2026-10-21, with the ingest's CPU from I7 on
2026-10-23. **None was re-read for this plan.** The implementation's cost statement re-reads them before any deploy,
as #443's did.

| | now | the UK radar adds | included | past it |
|---|---|---|---|---|
| Workers CPU | about **25.8M ms/month**: the proxy 2.33M, the ingest about 23.4M | **about 4-7M ms, estimated:** 288 frames a day × 0.4-0.7 s, plus about 2,600 empty checks a day × about 8 ms. Unmeasured, and I7 found workerd about twice Node | 30M | $0.02 per million ms: **cents a month** |
| R2 Class A | about 881k: 751k `missileer-live`, 130k `cref` | **+26k** if `opuk` prunes only when it stores; **+105k** if it prunes on every check as `cref` does (10 checks per 5-minute frame) | 1M | $4.50 per million |
| R2 Class B | about 744k: 312k before weather, plus 432k from the `cref` ingest | about +430k from the ingest, plus about 216k for each UK device with the layer on | 10M | $0.36 per million |
| R2 storage | about 0.65 GB | **+14 MB** (one hour of 1.18 MB frames); 0.34 GB if pruning failed for the lifecycle's whole day | 10 GB | $0.015 per GB-month |
| Workers requests | 663k, plus 43,200 cron runs | **0**: the same Worker and the same cron | 10M | — |
| Data in from CloudFerro | — | about 604 KB × 288 = **174 MB a day** | not billed to us | — |

**What this means:**
- **No new Cloudflare resource.** The bucket and the ingest Worker exist. `opuk` is a product inside them.
- **CPU is the row that crosses.** 25.8M plus 4-7M is 30-33M against 30M included, which is **a bill of cents.**
  - By the brief, that is asked of Daniel first, with the measured figure from the implementation's own run, before
    the ingest change deploys.
- **R2 Class A** goes to about 91 % with the prune-on-store rule, or about 99 % without it. So the rule is part of the
  proposal, not an optimisation. Missileer's growth alone could cross it either way: `docs/wx-server.md` already
  named this margin as thin.
- **The Met Office feed would cost less CPU** (96 frames a day, not 288), but more in work: an HDF5 parser and the
  firmware change.
- **The map layers** carry their costs in the map server's own US v1 resources (`docs/basemap-route.md` announces
  them in its §7 and A1.6). The UK adds storage to that store and no new resource; the builder's CPU per request is
  unchanged.
  - **Terrain:** 480 z9 tiles in a box around the UK, about 35 MB at the US store's 73 KB a tile.
  - **Vectors:** unmeasured. §4's dry run measures them.

## 6. Questions, each with a recommendation

**The radar**

| | question | recommended | the alternative |
|---|---|---|---|
| Q1 | the source | **OPERA CIRRUS `DBZH`**, from its GeoTIFF | the Met Office composite: 29 % showable on v19 and share-alike; or both, with the Met Office as a hot standby, which still needs firmware to be useful |
| Q2 | how a v19 device gets it | **the server dispatches `cref` by the query's point** (§3); no firmware | a new product id only new firmware asks for: the customer waits for a later release |
| Q3 | the product and its window | **`opuk`: the UK's land extent plus 2.02°,** 9 tiles a frame | all of OPERA's Europe: frames of about 17 MB (estimated, not built) and several times the CPU, for customers who do not exist yet |
| Q4 | `opuk`'s server age limit | **1,800 s.** The server answers a late frame with a 200 and its true time, and the device hides it by its own 600 s rule | 600 s, as `cref`: a 503 in a late frame's seconds doubles the device's interval |
| Q5 | `opuk`'s pruning | **only on the check that stores a frame:** R2 Class A at about 91 % rather than 99 % | as `cref`: on every check |
| Q6 | `/credits` | **the OPERA line in R4;** and **the NOAA MRMS line now, in its own small PR,** since it has been true since 2026-10-23 | both in R4 |
| Q7 | R5, the glass | **first a host render** of a real `opuk` crop through `wxlayer::Build` and `WxS2` to a PNG, with no board; **then** a short window on a bench board set to a UK public airport, Daniel's yes first, the location restored after. E2:08 feeds the Bend site, and moving it stops that feed for the window | the glass only |
| Q8 | knowing that OPERA still carries the UK | **R1 makes the ingest record the covered share of the UK window per frame, and `/healthz` print it** beside `frameAgeS`. `smoke-prod.sh` reads `/healthz`, so the number has a reader. A UK withdrawal would otherwise look exactly like "outside coverage" | nothing: the failure would be silent and would look like the device's normal note |
| Q9 | Shetland | **say it plainly** wherever coverage is described: at Sumburgh, neither source covers the pixel, and 40-45 % of a 160 km view | — |
