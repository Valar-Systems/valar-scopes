# UK weather and UK map layers: the plan

**Status: a plan for review. No code, and no Cloudflare resource is created by anything in here.**
- **Asked for by:** the reviewer chat's brief "UK weather and UK landmarks" (2026-10-28), pasted by Daniel.
- **Branch:** `docs/uk-data`, pushed without a PR.
- **The rest of this document is written after the measurements below.** This first section is frozen before any
  of them is taken, so the readings cannot choose their own meaning.

## The answers, in brief (added after the readings; the frozen section below is unchanged)

- **UK radar: OPERA's CIRRUS composite, not the Met Office's.**
  - It is reflectivity, the same quantity as MRMS's `cref`, every 5 minutes at 1 km.
  - It arrived about 250 s after its time in the 24 h measured, under CC BY 4.0.
  - A v19 device could show it **99.3 %** of the time at best. The Met Office composite manages **29.0 %**: it is rain
    rate every 15 minutes, about 334 s late, under CC BY-SA 4.0.
  - Both cover Northern Ireland. Neither covers Shetland at Sumburgh.
- **A v19 Blipscope can show UK weather with no firmware change,** provided v19 carries #445:
  - the server serves the device's existing `/wx/cref` request from the UK product when the query is in the UK;
  - and `opuk`'s server age limit sits above the device's 600 s, so a late frame never costs the device a backoff.
  - Credit lines live on `/credits`, which the device's config page links to, so they are a server change too.
  - **The map layers cannot reach a v19 Blipscope:** it has no BMPK client.
- **UK map layers:** for Great Britain, Ordnance Survey's OGL v3 sources map onto BMPK v1's sections (§2). ONS and
  NRS supply the populations.
  - **Northern Ireland is blocked on a licence question.** OpenDataNI labels OSNI's datasets OGL, but LPS's and
    NISRA's own pages restrict derived data. NI's open layers are also thin: roads surveyed 2003-2006 and no named
    rivers. A written question to LPS is Q13.
  - **Terrain:** Terrain Tiles z9 over the UK is EU-DEM, SRTM, GMTED2010 and ETOPO1, all with terms that can be met.
  - **My view:** this is a v1 coverage extension if §4's four US-defined fields are restated by role. Bilingual names
    are the one point that could force v2.
- **Cost:** no new Cloudflare resource. Workers CPU likely crosses the 30M included by cents a month, so it is asked
  first. R2 Class A goes to about 91 %.
- **Order:** the radar work (R1-R4) is server-only and can finish before v19 ships. The map layers wait for the map
  server's US v1, and reach a Blipscope only with its BMPK client, after v19.

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

**Read by a research agent this session** (2026-10-27/28), from the publishers' pages, their technical
specifications and the downloads themselves: field names, value lists, licence files and CRS. Counts are from the
October 2026 releases. No account or key was used.

### 2.1 The sources

**Great Britain: Ordnance Survey OpenData, all under OGL v3.**
- **OS's statement:** "available for unrestricted, free commercial and personal reuse under the Open Government
  Licence (OGL)".
- **OGL v3's grant** includes "exploit the Information commercially and non-commercially … by including it in your own
  product or application". Its condition: "acknowledge the source … by including or linking to any attribution
  statement specified by the Information Provider(s)".
- **No registration is needed** for the downloads below.

| layer | source | what it holds | CRS | size |
|---|---|---|---|---|
| water | **OS OpenMap – Local**, `SurfaceWater_Area` (2026-04; April and October) | "An inland waterway body sufficiently wide enough to capture as an area feature". No names. `TidalWater` is a separate layer | BNG | GeoPackage 3.5 GB (the whole map) |
| rivers | **OS Open Rivers**, `WatercourseLink` (2026-10; April and October) | 193,722 links; `form` = inlandRiver 156,209, lake 24,265, tidalRiver 11,728, canal 1,520; `watercourseName` (the Welsh or Gaelic name where there are two) and `watercourseNameAlternative` (the English) | BNG | GeoPackage 52 MB |
| roads | **OS Open Roads**, `RoadLink` (2026-10; April and October) | 3,987,488 links. `roadClassification` ∈ Motorway, A Road, B Road, Classified Unnumbered, Unclassified, Not Classified, Unknown; `primaryRoute` and `trunkRoad` flags; `formOfWay` (Slip Road, Roundabout, …); `roadClassificationNumber` | BNG | Shapefile 586 MB |
| peaks | **OS Open Names**, type `landform`, local type **"Hill Or Mountain"** (2026-10; quarterly) | 37,025 records: "A named area of land that is higher than the surrounding land". **No elevation**, and the same in all of Open Names | BNG | CSV 103 MB |
| places | **OS Open Names**, type `populatedPlace` | City 71, **Town 1,355**, Village 15,155, Hamlet 12,900, Suburban Area 10,876, Other Settlement 2,917. **No population** | BNG | (the same file) |
| population, England and Wales | **ONS, "Towns and cities, characteristics of built-up areas, England and Wales: Census 2021"**, joined on `BUA22CD` to **"Built Up Areas (December 2022) Boundaries GB BGG"** | 6,975 coded rows, every one matching the boundaries. Counts are rounded to 5. **London's 33 BUAs are not in the published table** ("we have removed London's 33 BUAs") | BNG | xlsx 28 MB |
| population, Scotland | **NRS, "2022 Census Settlement boundaries"** | 503 settlements; `Popcount` is inside the boundary file | BNG | — |
| terrain | Terrain Tiles z9 (§2.4) | — | Web Mercator | about 35 MB for the UK |

**The attribution lines the GB sources specify, verbatim:**
- **OS:** "Contains OS data © Crown copyright [and database right] [year]." OS adds: "The same acknowledgement should
  be included in any sub-licences of the data that you grant, including any further re-use."
- **OS Open Names' own licence file** carries three lines:
  - "Contains OS data © Crown Copyright and database rights 2026."
  - "Contains Royal Mail data © Royal Mail copyright and database right 2026."
  - "Contains National Statistics data © Crown copyright and database right 2026"
- **OS Open Roads, for data relating to Scotland** (OS Co-Branding Guidelines v2.0): "This product contains data
  created and maintained by Scottish Local Government."
- **ONS:** "Source: Office for National Statistics licensed under the Open Government Licence v.3.0", plus the OS
  line for its boundaries.
- **NRS:** "Copyright National Records of Scotland, contains Ordnance Survey data © Crown copyright and database right
  (insert year)."

**OS's end-of-life plans, a risk to the refresh, not to serving:**
- OS's indicative migration timeline (August 2026, "subject to change regularly") puts Open Roads, Open Rivers,
  Open Names and OpenMap – Local in an end-of-life graphic spanning FY26/27 to FY30/31.
- OS Open Zoomstack has a stated end of life of Spring 2028.
- **None is withdrawn today,** and OS promises "at least 12 months' notice".
- **OGL v3's licence is perpetual,** so a sealed store keeps its rights. Only the next refresh would need new sources.

**Northern Ireland: thin, old, and with its licensing unsettled.**

| layer | what exists, open | the gap |
|---|---|---|
| roads | **OSNI 50K Transport lines:** 25,860 features with `TEMA` (MOTORWAY 189, A_CLASS 3,908, B_CLASS 4,074, DUAL_CARR 286, …) and `SURVEY` | surveyed **2003-2006**; no names or numbers on the lines |
| rivers | **none from OSNI.** DAERA/NIEA's "Rivers Strahler Ranking" has 17,370 lines with **no name field** | no named rivers, so v1's rule (group by name) finds nothing |
| water | DAERA/NIEA **"Lakes of Northern Ireland":** 345 polygons, with names and areas (2013) | — |
| places | **OSNI Gazetteer:** 336 "towns and villages", with no type and no population; positions are "the label position on the mapping" | positions are not real-world points |
| population | **NISRA CT0046:** 639 settlements (93 suppressed), codes N11…; boundaries "Settlement Development Limits 2015" (527) | the join is by code to polygons whose licence is in doubt (below) |
| terrain | Terrain Tiles z9 (EU-DEM) | — |

**The NI licences, as the official pages state them:**
- **OpenDataNI** labels every dataset OGL v3.
- **LPS:** "It is only permissible to use and publish LPS derived data under the appropriate (charged for) LPS
  Licence." Its Q&A, asked "Can I publish a dataset I have derived from LPS data under OGL?", answers: "It may be
  possible but you will need to make an application for permission".
- **NISRA:** OSNI-branded mapping "(or any mapping based on, or derived from OSNI branded mapping) is exempt from the
  terms of the OGL".
- **So the NI map layers are blocked on a written answer from LPS** (copyright@finance-ni.gov.uk). This is not a
  reading I can settle, and a package is derived data by definition.
- **An unverified lead:** nidirect lists EuroGeographics' EuroRegionalMap as OSNI open data. Its licence page did not
  render.

### 2.2 The UK layer rules, proposed beside §5's US rules

Each row keeps §5's mechanism (clip, simplify, quantise, the budget, the drops) and changes only the source and the
selection.

| §5 | US v1 (unchanged) | UK, Great Britain |
|---|---|---|
| 5.1 water | `AREAWATER` ≥ 1,500,000 m², outer rings | **`SurfaceWater_Area` ≥ 1,500,000 m²,** outer rings, holes dropped (RD3). `TidalWater` excluded: the format's water layer is "lakes and reservoirs" |
| 5.2 rivers | `LINEARWATER` named "… Riv", grouped by `FULLNAME`, the 5 longest groups | **`WatercourseLink` with `form` inlandRiver or tidalRiver, grouped by name, the 5 longest groups.** No suffix test: UK names have none ("River …", "Afon …", "Water of …"). Canals and lake centre-lines are excluded |
| 5.3 roads | `S1100` → 1, `S1200` → 2, business routes dropped | **`roadClassification` Motorway → 1; A Road → 2;** `formOfWay` Slip Road dropped (the US keeps no ramps: `S1630` is outside `S1100`/`S1200`). Whether class 2 is **every** A road or only `primaryRoute` = true is Q12, settled by M1's sizes |
| 5.4 peaks | GNIS Summit; elevation from z9; 3.0 km de-dup; top 16 | **Open Names "Hill Or Mountain"; elevation from z9;** the same de-dup and top 16 |
| 5.5 places | Gazetteer `LSAD` 25, population from SUB-EST2024 | **Open Names `populatedPlace`, City and Town (1,426 in GB).** Population: the BUA (England and Wales) or NRS settlement (Scotland) **polygon containing the point**, else 0. **No figure is ever guessed** |
| 5.6 terrain | Terrarium z9 | unchanged (§2.4) |

**Three risks in these rules, each with a check for M1:**
- **A hill's point may not be its summit.** Open Names gives the hill area a representative point. GNIS gives a
  summit position. A z9 elevation sampled off the summit understates the hill and can mis-rank it.
  - **The check:** compare each kept hill's z9 value with the z9 maximum within 1 km.
  - The rule's fallback, for a ruling: take that maximum, and say so in the processing line.
- **London's places would get population 0** under the published ONS table, which would rank London below every
  town. That is honest by the format ("0 = no figure"), and bad for a map.
  - **The check:** whether Census 2021 counts at BUA 2022 geography, London's included, are published elsewhere by
    ONS (Nomis is the likely place).
  - Until then, 0. A guessed figure would be a lie with a plausible shape.
- **Bilingual names** (Q10): Open Names, Open Rivers and Open Roads all carry a second name in Wales and Gaelic
  Scotland. v1 has one text field.

**Northern Ireland's rules wait for LPS.** If its answer allows derived use, the NI rows would be:
- MOTORWAY → 1, A_CLASS → 2;
- the DAERA lakes;
- **no rivers**, an empty section (RD14);
- OSNI gazetteer places with NISRA populations;
- **all of it flagged as 2003-2015 data.**

Until then, NI origins are outside the map's coverage, which the route already answers with `404 no_coverage`.

### 2.3 What the coverage means at the edges

- **Coverage is GB + NI land** (NI only once LPS allows it). Weather has no such limit (§1.6).
- **A disc that crosses a border** carries UK features only. Ireland, France and the North Sea are empty, not marked:
  the US v1 rule for Canada and Mexico (`docs/basemap-route.md` §2.6), applied unchanged.
- **The Isle of Man and the Channel Islands** are covered by no source read here, so they are `no_coverage`.

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
    by `pop`) is the same for every coverage. **No v1 client would read a UK package wrongly.**
  - **This is a weaker test than the PROVENANCE amendment's,** which stayed in v1 because "Clients never parse
    PROVENANCE". Clients do read these four fields. So it needs a ruling, not an analogy.
  - **Evidence from the one client that exists:** Weatherscope's place-label rule drops a trailing " city" once
    (`src/weatherscope/WxLogic.h:457`). A UK name has no such suffix and passes through unchanged.
- **Where I think otherwise: bilingual names.** In Wales and Gaelic Scotland, OS's sources carry two names: `name1`
  is the Welsh or Gaelic name and `name2` the English (Open Roads and Open Names; Open Rivers uses
  `watercourseName` and `watercourseNameAlternative` the same way).
  - v1 has one text field, so choosing one is a data rule with no US precedent.
  - If the answer is "both", that is a new field, and so v2 (§6, Q10).
- **A new coverage also needs what v1 has once:** the coverage bits (`coverage.bin`), the credit and attribution lines,
  and the terrain bits. These are the route's (the map server's plan), not the format's.

## 3. The device: what a v19 Blipscope in the UK can show with no firmware change

**The assumption:** v19 carries the weather layer as it stands on `feat/wx-device` (`f9184ef`, #445).
- #445 is not merged. Its own header says device-side weather "does not merge until the starved-seconds counter has
  landed and #245 has been ruled on from the soak" (`docs/wx-device.md` at `f9184ef`).
- If v19 ships without it, no weather reaches any v19 device.

**What it asks for:** always `/wx/cref`. The call is `CloudFeed::WeatherUrl(req->cloudBase, "cref")` in
`src/AircraftManager.cpp` on `feat/wx-device`.
- **Today,** a UK query's box misses the CONUS grid. The server answers an all-14 crop, and the device shows its
  "outside coverage" note (WD3 (f)).
- **So UK weather on v19 needs one server change and no firmware.** The handler serves `cref` from `opuk` when the
  quantised point is inside **the UK land box**, the window minus its 2.02° margin (as ruled, A1.1). Otherwise it
  serves today's answer.
  - Since the margin is the largest view plus the pad, every view served from `opuk` lies inside its grid (K6).
  - A point inside the window but outside the land box gets today's answer (K5).
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
| M0 | rulings on the UK rule set (§2, §6) and on whether it is a v1 coverage extension; **the LPS question sent** (Q13, Daniel's) | R0 |
| M1 | **an offline size dry run of the GB sources:** packages at the UK centres against the 32 KB cap, plus Q12's road sizes and Q16's hill check. No Cloudflare | M0; can run before v19 |
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

**The map layers**

| | question | recommended | the alternative |
|---|---|---|---|
| Q10 | **bilingual names:** Open Names, Open Rivers and Open Roads carry a Welsh or Gaelic name and an English one, and v1 has one text field | **Daniel's product call.** For a single field, I'd recommend **the English name where the source has two, else the one name.** The rule is recorded in PROVENANCE's processing text | the name OS lists first, which is the Welsh or Gaelic one; or both names, which is a new field and so **v2** |
| Q11 | a v1 coverage extension, or v2 | **v1, with §4's four US-defined fields restated by role** (§2.5). A ruling, and an edit to the format doc by its owner | v2 |
| Q12 | roads class 2 | **decided by M1's sizes:** every A road if the packages fit 32 KB at the Blipscope's 161 km around the busiest centres (LHR, MAN, BHX); else only `primaryRoute` A roads | fix it now, unmeasured |
| Q13 | **Northern Ireland's map layers** | **a written question to LPS** (copyright@finance-ni.gov.uk): may packages derived from the OSNI open datasets be redistributed to customers' devices under OGL v3? **Sending it is Daniel's** (an outward message). Until it is answered, NI origins are `no_coverage` for maps. Weather is unaffected | build NI on the OpenDataNI label alone: LPS's and NISRA's own pages say otherwise |
| Q14 | terrain | **Terrain Tiles z9,** with an EU-DEM tile bit and credit line (§2.4) | OS Terrain 50 for GB plus OSNI's 50 m DTM for NI: a second pipeline, and NI's licence is in the same doubt as Q13 |
| Q15 | which places | **City and Town** (1,426 in GB), the nearest analogue of incorporated cities | add Village (15,155): an order of magnitude more, and the size budget would drop them first |
| Q16 | a hill's elevation | **measure first** (M1): each kept hill's z9 value at its point against the z9 maximum within 1 km; then rule | the point's value, unchecked |

## Addendum 1, 2026-10-28: the rulings, and R1's predictions

### A1.1 The rulings, verbatim (source: the reviewer chat, pasted by Daniel, 2026-10-28)

> Rulings on the UK data plan, docs/uk-data at 93f1a80 (reviewer chat, 2026-10-28)
>
> The record is accepted as written: predictions frozen at 489656c before the first reading, every miss kept, the
> disclosures accepted. One fix, to your report rather than the doc: the count is 3 hits (M1, M3, O1), 5 misses (M2,
> M4, S-M, O3, S-O), and O2 split (RATE hit, DBZH missed), not "four and four". S-M at 29 % means the Met Office feed
> can't serve a v19 device. It stays the documented fallback, at the price you stated.
>
> Radar
> - Q1, OPERA CIRRUS DBZH from its GeoTIFF: accepted.
> - Q2, serve cref by the query's point: accepted, with one change. Choose opuk when the quantised point is inside
>   the UK land box (the window minus its 2.02° margin), not anywhere in the window. Then every view served from opuk
>   lies inside its grid, since the margin is the largest view plus the pad. Add a row: a point inside the window but
>   outside the land box gets today's answer.
> - Q3, the UK window (9 tiles): accepted. Europe-wide waits for a European customer.
> - Q4, opuk's server limit at 1,800 s: accepted.
> - Q5, prune only on the check that stores: accepted.
> - Q6: yes, the NOAA line now, in its own small PR, by you, worded as docs/wx-server.md has it: "Weather radar
>   derived from NOAA MRMS data. Not endorsed by NOAA." Predictions first: the live /credits carries it after the
>   deploy, and a sabotage (the line removed) turns that check red. It merges on my approval at its head SHA, then
>   deploys under the standing rule.
>   The OPERA line goes in R4, worded: "Weather radar outside the US: EUMETNET OPERA CIRRUS maximum-reflectivity
>   composite, licensed under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). Reprojected to a 0.01° grid
>   and binned to 5 dBZ steps by Valar Systems. Not endorsed by EUMETNET." Leave out "produced by Météo-France"
>   unless the file's own metadata names the producer.
> - Q7: the host render first. The glass later, on a board Daniel names, with his yes.
> - Q8, the covered share in /healthz: accepted.
> - Q9, Shetland stated plainly: accepted.
>
> Three additions to R1, predictions first:
> 1. The end-to-end blank on a v19 device. At OPERA's median latency, a new frame has 50 s of margin before the
>    device hides the one before it (600 − 300 − 250). The ingest's :00/:30 checks and the device's 60 s poll can
>    take up to 90 s of that. So, from the week's per-frame times and the real check and poll timing, predict the
>    share of time a v19 device shows no UK weather, and the longest blank. Price extra opuk checks near each frame's
>    expected arrival (for example every 10 s from V + 240 s until it lands). This number decides whether #445 needs
>    a different age limit before it merges, so it comes before R2's code.
> 2. Clutter. DBZH reached 78.5 dBZ and RATE 687 mm/h in late October. Over the week, count echoes that sit on the
>    same cells through dry frames (wind farms, interference spokes, sea clutter). Test the GeoTIFF's quality band as
>    a mask, with a sabotage (the mask off) that must bring them back. MRMS's cref is the QC composite; the UK layer
>    should look as clean.
> 3. The bucket's terms. Record what EUMETNET says about automated use of the openradar-24h cache, and show that our
>    load (about 1,500 range reads and 3,000 listings a day) fits inside it.
>
> Map layers
> - Q10, bilingual names: Daniel's call. I've recommended the English name where there are two.
> - Q11: a v1 coverage extension, provided Q10's answer is a single name. The format doc's owner, Weatherscope Code,
>   writes the restatement of §4's four fields by role when M2 starts, not now. Nothing in a US package changes by a
>   byte.
> - Q12, decided by M1's sizes: accepted.
> - Q13, the LPS question: Daniel's. Until LPS answers, Northern Ireland is no_coverage for maps.
> - Q14, Terrain Tiles z9 with an EU-DEM tile bit and credit line: accepted.
> - Q15, City and Town: accepted.
> - Q16, measure first in M1: accepted.
>
> Order: R1 now. M1 (offline, no Cloudflare) may run beside it. M2-M4 wait as your plan says. Open the plan and R1's
> frozen predictions as one PR; it merges on my approval like any other.
>
> Cost: Daniel is being asked now for a small monthly allowance. If he gives one, it reaches you in his paste. R3
> still states the measured figure before the deploy.

### A1.1b Daniel's answers, verbatim (source: the reviewer chat, pasted by Daniel, 2026-10-28, after the rulings)

> Daniel's answers, recorded verbatim (reviewer chat, pasted by Daniel, 2026-10-28). Paste after the rulings of the
> same date.
>
> - Q10, bilingual names: "English". The English name where a source has two, else the one name, recorded in
>   PROVENANCE's processing text. So Q11 stands as ruled: a v1 coverage extension.
> - The allowance: "Ok", to "Is it OK to allow up to $2 a month in total before anyone asks you again?" So R3 needs
>   no new ask if the measured figure keeps the account's total Cloudflare overage under $2 a month. State the figure
>   in R3's report either way. Over $2, ask Daniel first.
> - Northern Ireland: "Just add it for now, i will get written permission soon. Adding the feature will take a week
>   or so".
>   - So NI is in scope now, with your §2.2 NI rows: MOTORWAY → 1, A_CLASS → 2, the DAERA lakes, no rivers (an empty
>     section), and OSNI gazetteer places with NISRA populations, all flagged as 2003-2015 data. M1 measures NI with
>     GB.
>   - The serving gate: NI is built and stored with GB, but the coverage bits that serve NI packages stay off until
>     LPS's written answer is recorded in the repo. Until then NI answers no_coverage for maps. If the answer comes
>     first, as Daniel expects, the gate costs nothing.
>   - Weather is unaffected: opuk covers NI either way.

**What these change:**
- **Q10:** the English name where a source has two, else the one name. The rule is written in PROVENANCE's processing
  text. **Q11 is settled: a v1 coverage extension.**
- **R3's cost gate:** no new ask if the measured figure keeps the account's **total** Cloudflare overage under
  **$2 a month**. Over that, Daniel is asked first. R3's report states the figure either way.
- **Northern Ireland:**
  - **Built and stored with GB** in M2, from §2.2's NI rows, all flagged as 2003-2015 data. **M1 measures NI beside
    GB.**
  - **Its coverage bits stay off until LPS's written answer is recorded in the repo.** Until then NI is `no_coverage`
    for maps.
  - Q13's letter is Daniel's.

### A1.1c Q13, sent (source: the reviewer chat, pasted by Daniel, 2026-10-28), verbatim

> Q13 update (reviewer chat, 2026-10-28): Daniel sent the LPS question today. Record Q13 as sent, awaiting a reply.
> NI stays built-but-not-served until the reply is pasted and recorded.

- **Q13's state: sent 2026-10-28, awaiting LPS's reply.**
- Northern Ireland stays built but not served for maps until the reply is pasted and recorded here.

### A1.2 What the rulings change

- **The count, corrected** (it was in my report, not in this doc):
  - **3 hits:** M1, M3, O1;
  - **5 misses:** M2, M4, S-M, O3, S-O;
  - **O2 split:** `RATE` hit, `DBZH` missed.
- **Q2, the dispatch** (§3 as amended): `opuk` serves a point only inside **the UK land box**, the window minus its
  2.02° margin.
  - A point inside the window but outside the land box gets today's answer. That is a row in K5 below.
- **The OPERA line's producer: the file's metadata does name it.**
  - Every `DBZH` file read (4 of 4) carries `how/creator_name` = `Meteo France`, with `creator_type` = institution.
  - CC BY 4.0 §3(a)(1)(A)(i) requires keeping "identification of the creator(s)" when the licensor supplies it.
  - **So R4's line, proposed for its review:** the ruled wording, with the creator named as the file spells it:
    > Weather radar outside the US: EUMETNET OPERA CIRRUS maximum-reflectivity composite, created by Meteo France,
    > licensed under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). Reprojected to a 0.01° grid and binned
    > to 5 dBZ steps by Valar Systems. Not endorsed by EUMETNET.
- **The NOAA line** is its own PR, branched from main, with its predictions in `docs/wx-server.md`.
- **This PR's head stays the approved head.** R1's readings are taken over a week, and they go to a follow-up docs
  PR, not to this branch.

### A1.3 R1's predictions, frozen before any R1 reading

**Seen before freezing, so not counted as predictions:**
- **The quality band, on one frame:** 0 to 1 in steps of 0.1 (11 distinct values), present only on echo cells.
- **The `creator_name` of four files.**
- **Production's `/healthz` commit** (`962ccb7`).
- **Free disk space.**

No frame of the week has been read.

**The week's collection (the input to T, B and C).**
- **The instrument, `collect_opera.py`,** written after this freeze. Each run:
  - lists the 24 h cache's `DBZH` keys with `LastModified`;
  - for every frame not yet held, reads the COG's first 8 KB and the UK window's level-0 tiles (both bands), and keeps
    them **raw**, one file per frame.
- **Where:** `C:\Github\Blipscope\bench-logs\uk-opera\`. It is gitignored, and outside the scratchpad, which can be
  wiped.
- **When:** at least one run every 6 h, from 2026-10-28 to 2026-11-04.
- **The gap rule:** a frame whose time passed out of the 24 h cache before any run held it is recorded as **not
  held**, never filled in.
  - **The week counts only if ≥ 6 of its 7 days are wholly held.** Otherwise T, B and C are BLIND.
- **Its controls:**
  - **The listing is static for old frames:** a frame's `LastModified` is identical in every listing that holds it.
    One that changes means stop.
  - **The tiles decode to the HDF5's cells:** once a day, one frame's `.h5` is fetched, and its UK window must equal
    the tiles' decoding cell for cell, with undetect read as NaN. A mismatch means T, B and C are BLIND.

**T. The week's timing (`DBZH`).**

| | measured | predicted |
|---|---|---|
| **T1** | cadence | **≥ 99 %** of steps exactly 300 s |
| **T2** | latency P − V | p50 in **[240, 265] s**; p95 **≤ 320 s**; **≤ 1 %** of frames over 450 s |
| **T3** | missing frames (a 300 s slot with no key) | **≤ 0.5 %** of slots |
| *sabotage* | every third key dropped | T1 red |

**B. The end-to-end blank on a v19 device (the first addition).**
- **The model:** a pure function, host-tested.
  - **Frames:** the week's `DBZH` frames, each listable at its `LastModified` (the poller's result).
  - **The ingest:** checks at each minute's :00 and :30, plus the cron's start offset.
    - **The offset** is read from `cref`'s own checks over 24 h: Workers Logs, `t0` = event time − `ms`, as I5′'s
      reconciliation did.
    - A check stores the newest listed frame, which becomes servable at the check's start plus the build time `b`.
    - **`b` = 2 s,** an assumption that R2 measures. B is also printed at `b` = 0 and 5 s.
  - **The device:**
    - polls every 60 s from its send (WD3 (g)), at a phase φ;
    - the reply arrives at once (B is also printed with 1 s added);
    - it draws its crop while the crop's frame is ≤ 600 s old (`WhatToShow`);
    - with `opuk`'s server limit at 1,800 s, it never gets a 503 for a late frame.
  - **The blank:** the exact length of each interval in which nothing is drawn, summed over the week. It is computed
    for φ = 0, 1, …, 59 s and averaged.
  - **If the offset cannot be read,** B is graded at both 0 s and 5 s, and both must meet the predictions.
- **The two schedules:** today's (:00/:30), and **the priced extra checks**: every 10 s from V + 240 s until the frame
  lands.

| | measured | predicted, today's schedule | predicted, with the extra checks |
|---|---|---|---|
| **B1** | share of the week with no UK weather drawn | **[2.0 %, 5.0 %]** | **[0.5 %, 2.5 %]** |
| **B2** | blanks per hour | **[4, 8]** | **[2, 5]** |
| **B3** | the longest blank, over every φ | **≤ 600 s** | **≤ 600 s** |
| **B4** (price, printed) | the extra checks a day, their CPU at `cref`'s measured empty check (about 8 ms), and their listings | — | — |
| **B5** (printed, not graded) | B1 at device limits of 720 s and 900 s | — | — |

- **Controls, run before the week is read:**
  - **(1)** a synthetic week at a constant 250 s latency, `b` = 0 and offset 0 must give **2.4167 % ± 0.01**. That is
    the exact value: the ingest waits a fixed 20 s, and over 60 integer phases the mean blank is 435/60 s per 300 s
    frame. It must also give **5.80 ± 0.01 blanks an hour**: 29 of 60 phases blank once per frame.
  - **(2)** the same week with a device limit of 3,600 s must give 0 %.
- **The sabotage:** the age measured from P instead of V must turn control (1) red.
- **What the results will mean** (my recommendation for the reviewer, registered now):

| outcome | meaning |
|---|---|
| **(a)** a schedule meets **B1 ≤ 1 %, B2 ≤ 1 and B3 ≤ 300 s** | #445's 600 s limit can stand for the UK, with that schedule |
| **(b)** neither schedule meets it, **and every row is as predicted** | **#445 needs a per-product age limit** (for example, one the server sends) **before it merges**. A 60 s poll against a 50 s margin blinks the layer several times an hour, whatever the ingest does. **This is the outcome I predict** |
| **(c)** anything else, a control failing, or the week BLIND | stop and decide |

**C. Clutter (the second addition).**
- **Over:** the week's `DBZH` frames, on the native 1 km grid of the UK window.
- **The definitions:**
  - **an echo:** a cell ≥ 15 dBZ, code 1 or above;
  - **an isolated echo:** an echo whose 11 × 11-cell neighbourhood holds **≤ 3** echoes in that frame, itself
    included;
  - **a persistent clutter cell:** a cell that is an isolated echo in **≥ 10 %** of the week's frames;
  - **the mask:** an echo with quality index **< 0.5** is not an echo.

| | measured | predicted |
|---|---|---|
| **C1** | persistent clutter cells, unmasked | **≥ 1** (the count printed) |
| **C2** | the same, masked | **≤ 25 %** of C1 |
| **C3** | real weather the mask removes: non-isolated echoes with quality < 0.5, as a share of all non-isolated echoes | **≤ 5 %** |
| C4 (printed) | cells ≥ 65 dBZ, unmasked and masked; and C1-C3 at thresholds 0.1 to 0.9 | — |

- **The control:** a planted persistent speck, one cell at 40 dBZ in every frame, in a cell never otherwise an echo.
  - With quality 1.0, it is counted by C1 **and survives the mask**.
  - With quality 0.2, it is counted by C1 **and is removed by the mask**.
- **The sabotage:** the mask off in C2's run makes C2 equal C1. That is red, unless C1 is 0.
- **What the results will mean:**

| outcome | meaning |
|---|---|
| **(a)** C1-C3 as predicted | R2 applies the mask at 0.5. What a masked echo becomes, 0 or 15, is Q17 |
| **(b)** C1 = 0 | the week shows no clutter; no mask |
| **(c)** C2 misses | the quality band is not a clutter mask. **Stop and decide.** The named alternative is a static clutter map built from the week |
| **(d)** C3 misses | the mask deletes real rain. **Stop and decide** |
| **(e)** anything else | stop and decide |

**L. The bucket's terms (the third addition).**
- **Read in full:**
  - every page of the ORD documentation site, and its GitHub repository's README and licence files;
  - MeteoGate's developer-portal terms;
  - any terms CloudFerro publishes for public buckets;
  - plus the response headers of one listing and one range GET on `openradar-24h`.
- **Our load, at today's schedule:**
  - **range reads:** 1 header + ≤ 5 tile rows per frame, ≤ 1,728 a day;
  - **listings:** 2,880 a day, plus about 864 with the extra checks.

| | predicted |
|---|---|
| **L1** | **no numeric limit for the S3 cache is published.** The ORD overview's "low query limits" for anonymous use is about the API, with no number |
| **L2** | **no rate-limit header** on the cache's S3 responses |

| outcome | meaning |
|---|---|
| **(a)** a stated limit, with our load under it | it fits; recorded with its source |
| **(b)** as predicted: none stated | **it cannot be shown to fit.** I recommend a short message to support.opera@eumetnet.eu stating the load, **which is Daniel's to send.** R3 waits for the answer |
| **(c)** a limit our load exceeds, or a term against automated use | stop |

**K. R2's code checks, frozen now, run in R2.** Each has its sabotage.

| | check | predicted | sabotage, which must turn it red |
|---|---|---|---|
| K1 | the COG reader, on 2 committed frames (one wet, one dry), UK window | every cell equal to the HDF5's | band 1 read for band 0 |
| K2 | resampling, on a synthetic one-pixel echo line across the LAEA window | **0** 0.01° rows it crosses without an echo | nearest-neighbour: gaps > 0 |
| K3 | the codes | NaN → 0; −9,999,000 → 14; 14.99 → 0; 15.0 → 1; 75.0 → 13; 78.5 → 13; 15 never | 15 dBZ mapped to 0 |
| K4 | WXG1 | 1,515 × 1,455 cells, 9 tiles, **1,179,712 B**; a round trip identical cell for cell | `tilesY` for `tilesX` |
| K5 | the dispatch | LHR → `opuk`; **CDG (inside the window, outside the land box) → today's answer**; KMCI → `cref`; PHNL → `cref`'s all-14 crop; the land box and the CONUS grid do not overlap | dispatch by the window: CDG served `opuk` |
| K6 | every view served from `opuk` inside its grid: the land box on a 0.05° lattice × r ∈ {1, 25, 80, 160, 222, 300, 500} km, with WS4′'s device corners | **0** views outside the grid. Printed: crops whose padding reaches outside (served as 14) | a margin of 1.0° |
| K7 | the ETag carries the served product | the same query and epoch give different ETags for `cref` and `opuk`; a device moved from LHR to KMCI never gets a 304 | an ETag without the product |
| K8 | `opuk`'s 1,800 s limit | a 900 s-old frame → 200 (`cref` at 900 s → 503); 1,801 s → 503 `wx_stale` | the limit read from `cref` |
| K9 | the covered share in `/healthz` | each frame's record carries the window's covered share; an all-nodata frame reads 0; `smoke-prod.sh` FAILs on 0 or a missing share for `opuk` | the share fixed at 1: the all-nodata frame reads 1 |
| K10 | prune only on the storing check | a storing check prunes; a check that stores nothing makes **0** R2 lists (a wrapped binding) | prune on every check |
| K11 | CPU and live memory in Node, on 3 real frames | CPU **≤ 0.6 s** a frame; live peak **≤ 96 MB** (A4's meter). R3's production bar is twice Node (I7's factor) | 64 MB held: the peak +63-65 MB |
| K12 | the device decodes `opuk` crops | 8 server-made fixture crops at UK airports decode with `include/WxCrop.h` to the encoder's cells | one nibble flipped fails exactly that crop |
| K13 | the host render (Q7) | a real `opuk` crop at LHR through `wxlayer::Build` and `WxS2` to a PNG, committed. Looked at, not graded | — |

**Outside the set:** any reading not listed above means stop and report. No code, test or bar is edited to make a
reading come true.

**What a masked echo becomes, raised now as Q17:**
- **Recommended: 0, if C3 holds.** That is what MRMS's QC does with clutter.
- **The alternative is 15 (missing):** it is honest, and it draws a hatch on every clutter spot.

## Addendum 2, 2026-10-28: the rulings on R1, K14, and M1's predictions

### A2.1 The rulings, verbatim (source: the reviewer chat, pasted by Daniel, 2026-10-28)

> Rulings (reviewer chat, 2026-10-28)
>
> 1. #492 (the NOAA line) is approved to merge at b5f37eb98313c623d238200c452806d966d49bd0, once CI on that head
>    completes with 0 failed.
> - I read all 4 files; N1 and N2 are as predicted.
> - A clean squash, no --admin. If the head moves, this approval lapses.
> - Then deploy under the standing rule and read N3-N6 as frozen, with the carried commits listed first.
>
> 2. #490 (the plan and R1's predictions) is approved to merge at 606e2ebd05996c2766479cbee22438922206a37e, once CI
>    on that head completes with 0 failed.
> - The rulings and Daniel's answers are recorded verbatim, and R1 was frozen at 456988b before any R1 reading.
> - A clean squash, no --admin. If the head moves, this approval lapses.
>
> 3. R1, as frozen: accepted, with four notes.
> - Grading T and B on the .tiff's LastModified is right, since R2 reads the COG. It was recorded at ba5b557, before
>   the first run.
> - B's outcome (a) bar (B1 ≤ 1 %, B2 ≤ 1 an hour, B3 ≤ 300 s): accepted as the bar for keeping #445's 600 s limit.
> - Q17: a masked echo becomes 0, if C3 holds. If C3 misses, stop, as your table says.
> - L: if it reads (b) (no limit stated, no rate-limit header, no term against automated use), R3 does not wait for an
>   answer.
>   - A few requests a minute on a public open-data cache is ordinary use.
>   - Daniel sends a short courtesy note to OPERA support saying who we are and what we read. I'll draft it once L is
>     read.
>   - The ingest identifies itself. A new R2 row, frozen now: K14, every request to the cache carries a User-Agent
>     naming us with a contact URL, as the map server's does. Its sabotage: the header dropped.
>
> 4. The OPERA line for R4, with "created by Meteo France" as the file spells it: accepted in principle. Final at
>    R4's review.
>
> 5. M1: start it now, predictions first, GB and NI together.
>
> 6. The collection's caveat is accepted. If this session ends, the next one re-arms it within 24 h, and the gap rule
>    covers anything lost.

**What happened under 1 and 2:**
- **#492:** merged as `77f6913`, deployed, and **production was rolled back** on N3's miss (two `/blips` 503s with no
  upstream in the post-deploy smoke run). The record is `docs/wx-server.md` on #494.
- **#490:** merged as `aa24294`, a clean squash at `606e2eb` (the tree equals `git merge-tree`'s; patch-id
  `1bed0f71957b` on both).
- **This branch** carries main by merge rather than a rebase, so `ba5b557`, which item 3 cites, stays in its history.

### A2.2 K14, frozen as ruled

| | check | predicted | sabotage, which must turn it red |
|---|---|---|---|
| **K14** | every request the `opuk` ingest makes to `openradar-24h` (listings, the 8 KB header, the tile ranges), seen through a wrapped `fetch` in the test | each carries `User-Agent: BlipscopeWxIngest/1 (+https://github.com/Valar-Systems/valar-scopes)`, the map server's form (`ingest-basemap.ts`: `BlipscopeBasemapIngest/1 (+https://github.com/Valar-Systems/valar-scopes)`) | the header dropped from one request kind |

- **The week's collector is not the ingest.** It sends `valar-systems-uk-radar-measurement/1`, which names us
  without a URL.
- It is left as committed at `ba5b557`, so the instrument does not change mid-week. It can take the same form at
  its next restart, if wanted.

### A2.3 The NI serving gate has a second path (raised as Q19)

- **The ruled gate switches off NI *origins*:** NI's coverage bits.
- **But a GB origin's square can reach NI.** At 161 km, an origin in south-west Scotland has NI land inside its
  square, so its package would carry NI-derived features (the OSNI roads, the DAERA lakes, the gazetteer places)
  before LPS has answered.
- **That is CLAUDE.md's first table:** a guard that exists, and a path that does not go through it.
- **Recommended:** the gate belongs to the NI sources, not only to the NI origins. The store marks every NI-derived
  feature, and the builder leaves them out of every package while the gate is off, whatever the origin.
  - So a GB package near NI shows NI as empty, as v1 shows Canada.
  - **A test for M2:** a GB origin whose square meets NI carries 0 NI features with the gate off, and > 0 with it on.
- M1 measures with the NI features both included and left out, so the sizes cover either ruling.

### A2.4 M1's predictions, frozen before any M1 reading

**What M1 is.** An offline size dry run of §2.2's UK rules, GB and NI together.
- No Cloudflare, no store and no route.
- It is a measurement, not the builder: the map server builds that in M2.

**The instrument, `m1_sizes.py`,** written after this freeze.
- **The geometry steps are written from the format doc:** §2 (project, clip, quantise), §6 (Douglas-Peucker and the
  budget loop) and §13 (RD1-RD21).
- **The bytes come from the format owner's encoder,** `tools/weatherscope/basemap/bmpk.py` on main.
- **Every package is checked by its reference validator,** `validate.py`.
- **The sources:** §2.1's files, downloaded once, each with its sha256 recorded in a manifest:
  - OS Open Roads, Open Rivers, Open Names and OpenMap – Local (`SurfaceWater_Area`);
  - the ONS BUA table and boundaries; NRS's 2022 settlements;
  - OSNI's 50K transport lines and its gazetteer; DAERA's lakes;
  - NISRA's CT0046 and its 2015 limits;
  - the Terrain Tiles z9 tiles the squares need.

**The centres** (public airports, by code):
- **GB:** LHR, MAN, BHX, NCL, EDI, GLA, ABZ, INV, CWL, NQY;
- **NI:** BFS, LDY.

**The radii:**
- **graded:** the Blipscope's 161 km (default) and 222 km (maximum), at ε = 671 m and 925 m (half a pixel on the 240 px
  Kit S3, as the map server's plan uses);
- **printed:** Weatherscope's 97 km at ε = 100.6 m.

**The road variants measured:**
- **(i)** class 2 = every A road, links as OS publishes them (split at every junction);
- **(ii)** class 2 = `primaryRoute` A roads, links as published;
- **(iii)** and **(iv):** (i) and (ii) with links **chained** into one polyline wherever two links meet end to end
  with the same road number and class. That is a candidate UK rule (Q18), not §2.2's.
- **All four:** slip roads dropped, Motorway as class 1.

**The outcome classes:** the map server's (§5.4 of its plan).
- **F:** within 20 KB;
- **C:** inside the 32 KB cap;
- **D:** over the cap, brought under it by §6's drops, which are counted;
- **X:** still over: `too_large`.

**Predicted:**

| | measured | predicted |
|---|---|---|
| **M1-a** | LHR, MAN, BHX at 161 km, variants (i) and (ii) | **X**. Links split at every junction give each a structural floor of about 12 B, and the motorway links in a 322 km square around London alone approach the cap |
| **M1-b** | the same, variant (iv) (primary A roads, chained) | **C or D** |
| **M1-c** | the same, variant (iii) (every A road, chained) | **D or X** |
| **M1-d** | every other centre at 161 km, variant (iv) | **F, C or D**: never X |
| **M1-e** | 222 km, every centre, variant (iv) | **D or X** |
| **M1-f** | the share of the kept hills (top 16 per centre and radius) whose z9 value at the Open Names point is more than 50 m below the z9 maximum within 1 km (Q16) | **≤ 20 %**; the median difference **≤ 30 m** |
| **M1-g** (printed) | for each centre: the bytes per layer; the drops and collapses; the class-2 road length kept against the length in the square; and BFS, LDY, GLA with NI features in and out (A2.3) | — |

**Controls, run first:**
- **every package passes `validate.py`;**
- the section lengths plus the header and directory sum to `totalBytes`;
- **a synthetic straight line** with a bump half of ε simplifies to its two ends, and **a synthetic ring** cut by the
  square stays closed;
- the encoder reproduces `syn-min.bmpk` byte for byte from `synth.py`'s data.

**Sabotages:**
- **ε = 0** (no simplification) must raise every centre's road bytes;
- **the drop policy off** must turn every D into X;
- **chaining off** in (iv) must reproduce (ii)'s bytes exactly.

**What the results will mean:**

| outcome | meaning |
|---|---|
| **(a)** variant (i) fits (F or C) at LHR, MAN and BHX at 161 km | class 2 = every A road, as published (Q12, as ruled). **Not what I predict** |
| **(b)** only (ii) fits | class 2 = primary A roads (Q12, as ruled) |
| **(c)** neither (i) nor (ii) fits, and (iii) or (iv) does | **Q12's rule cannot decide alone:** chaining becomes a question (Q18). **This is the outcome I predict** |
| **(d)** none of the four fits at 161 km | stop and decide: the Blipscope's default radius would need fewer layers or a coarser ε |
| **(e)** M1-f misses | Q16's point elevation is not a summit elevation; rule between the 1 km maximum (with the processing line saying so) and the point |
| **(f)** anything else, or a control failing | stop and decide |

**Raised now, for the rulings that M1's numbers will inform:**
- **Q18:** may the UK rules chain OS road links that meet end to end with the same road number and class?
  - It changes no field. A US package is untouched, since TIGER's lines are not split this way.
  - **Recommended: yes,** with a fixed, deterministic chaining order written into the rule.
- **Q19:** the NI gate, on the sources rather than only the origins (A2.3). **Recommended: yes.**

### A2.5 M1's results (read 2026-10-28, 17:23-17:50Z): two misses and three instrument defects. Stopped for a ruling

**The instruments, as committed before each reading:**
- fetch, extract and sizing at `e7188e0`;
- the grader and `--only161` (the sabotage reruns at the default radius) at `0223cfb`.
- **The sources:** 74 files, each with its URL, sha256 and Last-Modified in `docs/uk-data/r1/m1-manifest.json`.
- **The extract's counts** are in `m1-extract.json`.
- **Raw outputs:** `m1-sizes.json` (sha256 `cc689565d1bc…`) and the three sabotage runs, in `docs/uk-data/r1/`.

**Two faults the controls caught before any reading, both fixed before the sizing run:**
- **A text field that is not UTF-8:** OS's DBFs are code page 1252. Every one declares language driver `0x03`, and the
  extract now decodes by that byte and refuses any other.
- **The BUA boundaries short by half** (4,545 of 8,545): the service caps a page at 1,000. They were re-fetched in
  1,000-record pages, giving 8,545 features and 8,545 distinct codes.

**The controls (A2.4):**
- every one of 150 packages validates `kOk`;
- `synth.py --check` reports "every committed byte equal";
- the DP bump cases, the clipped ring and the re-entering line all behave as frozen.
- **All passed.**

| | read | verdict |
|---|---|---|
| **M1-a** | LHR, MAN, BHX at 161 km, (i) and (ii): **X** in all six (LHR (i): 1,231,423 B first) | **as predicted** |
| **M1-b** | the same, (iv): LHR **D**, MAN **D**, **BHX X** | **MISS** (BHX) |
| **M1-c** | the same, (iii): D, D, X | **as predicted** |
| **M1-d** | the other nine centres at 161 km, (iv): NCL D, EDI D, GLA D, ABZ **C**, INV **C**, CWL D, NQY **F**, BFS D, LDY D | **as predicted** |
| **M1-e** | 222 km, (iv): LHR, MAN, BHX X; **ABZ C, INV C**; the rest D | **MISS** (ABZ and INV fit without drops) |
| **M1-f** | 384 kept hills: the median is **5 m** below the 1 km maximum; **2.9 %** are more than 50 m below; the worst is 138 m | **as predicted** |

**The sabotages:**

| | read | verdict |
|---|---|---|
| no drop policy | every 161 km D became X (37 of 37) | **red, as frozen** |
| ε = 0 | **as graded, the final road section rose at 8 centres and fell at 4** (INV, NQY, BFS, LDY). The grader read the road bytes **after** §6's drops, and an unsimplified package overflows and drops more. The frozen sabotage means the bytes **before** the budget, which the instrument does not record | **not red as graded; the frozen quantity is unrecorded, so BLIND.** Beside it, as data: the first-build total rose at 12 of 12 |
| no chaining | (iv) with chaining off equals (ii) at 11 of 12 centres. **NCL differs by 1 B, in PROVENANCE**, while its road section before the budget is identical | **not red** (defect 2 below) |

**The three instrument defects, found after reading.** None is re-graded, and none is fixed until ruled.
1. **ONS's counts were never read.**
   - **The cause:** the column was found with `startswith("Count")`, and "Country code" matches first. So every
     England and Wales place got population 0. That is the editor-anchor row in CLAUDE.md's table again.
   - **The bound** (`m1_popbound.py`, the column found exactly): 6,985 counts; 1,199 places gain one. At most
     **1,451 B** is missing from any graded package, and **no outcome class changes**. The closest cases:
     - NQY 161 (iv) F: 20,237 + 134 B, still ≤ 20 KB;
     - ABZ 222 (iv) C: 32,014 + 5 B.
2. **PROVENANCE's `files.terrain` counted every tile cached in the process,** not the tiles the package read. A package
   built after more centres carries a longer count. It is 1-2 B of PROVENANCE, and it is the 1 B that kept the
   no-chaining sabotage from going red.
3. **The ε = 0 sabotage's measure is unrecorded.** The instrument keeps only the final section bytes.

**What the numbers say (M1-g, printed).** The cap is set by layers §6 cannot drop.
- **LHR at 161 km (iv), after its drops:**
  - **rivers 15,545 B**, places 7,834 B, roads 7,332 B, PROVENANCE about 1.6 KB;
  - every water ring dropped; **40 % of the A-road length kept**.
- **Open Rivers splits a river into links at every confluence:** the structural floor the map server foresaw for
  TIGER (its §5.1).
- **BHX at 161 km:** places alone take 11,579 B and rivers 13,837 B. With the motorways, which are class 1 and not
  droppable, even every A road dropped cannot fit. Hence X.
- **NI's OSNI lines are short pieces, with no numbers to chain:**
  - GLA at 161 km is 61 KB with NI's features and 33 KB without;
  - BFS with NI out is F at 11.8 KB.

**Outcomes against A2.4's table:**
- LHR and MAN are **(c)**: only the chained variants fit, and only through drops.
- **BHX is (d)**: none of the four fits at 161 km.
- **(d)'s registered meaning:** stop and decide; the default radius would need fewer layers or a coarser ε.
- **Stopped here.**

**For the ruling, the levers M1's numbers point at.** Each is a UK data rule or a format question, not decided here:
- **chain Open Rivers links** by name, as Q18 proposes for roads;
- **select places,** for example City and Town above a population, or the N most populous;
- **coarser ε at 161 km;**
- **whether §6's drop order may reach rivers and places.** That is a v1 layer-definition change, so v2, unless ruled
  otherwise.

## Addendum 3, 2026-10-28: the rulings on M1, London's populations, and M1′'s predictions

### A3.1 The rulings, items 2 and 3, verbatim (source: the reviewer chat, pasted by Daniel, 2026-10-28)

Item 1, #492's redeploy, is recorded in `docs/wx-server.md` (#494).

> 2. Q19: yes. Gate the NI sources in the builder. Until LPS's written answer is recorded, no package carries
>    NI-derived data, including a GB package whose square reaches NI.
>
> 3. M1 (A2.5): BHX's outcome (d) is accepted, with both misses kept.
> - Q18: yes. Chain OS road links that meet end to end with the same road number and class, in a fixed, deterministic
>   order written into the rule.
> - Rivers: chain Open Rivers links by name the same way.
> - Places: City and Town, keeping the N most populous (ties by name). London's missing ONS figures come first (your
>   Nomis check). If they can't be found, propose a rule that keeps London's places in, before M1′.
> - Q16: the point's z9 elevation stands; M1-f held.
> - Fix the three instrument defects before re-running: ONS's column found by exact name; files.terrain counting only
>   the tiles each package reads; and the pre-budget road bytes recorded for the ε = 0 sabotage.
> - Then M1′, predictions first: the same centres and radii, NI out (as Q19 gates it), with NI in printed only. N is
>   measured over {64, 128, 256}; the rule takes the largest N that keeps every centre out of X at 161 km.
> - If BHX is still X at 161 km, stop. Coarser ε and v2 come up only then.
> - For later, not now: NI's OSNI lines are short and unnumbered, so they will need their own chaining rule once LPS
>   answers (GLA was 61 KB with them, 33 KB without).

**The chaining rule, as it will be written** (Q18 for roads, and the same for rivers). Two links join when all of
these hold:
- they meet end to end, sharing a node by OS's own node ids (`startNode`/`endNode`);
- they carry the same key: for a road, its number and class; for a river, its name after Q10's English rule;
- exactly two links with that key meet at the node.

**The order:**
- seeds in source order (RD2);
- each chain extended first from the seed's end node, then from its start node;
- a reversed link contributes its vertices reversed;
- the shared vertex is written once.

**Not chained:** unnumbered roads and unnamed rivers.

### A3.2 London's populations: not published by BUA; a rule proposed (L), for a ruling before M1′ runs

**The Nomis check, read 2026-10-28:**
- Census 2021's usual residents (`NM_2021_1`, TS001) are published at 21 geography types, from output areas to
  regions. **None is a built-up area.**
- ONS's own article says why London's 33 BUAs have no figures: "in Greater London, the method to identify BUAs does
  not recognise individual settlements in the same way."
- **So London's BUA populations cannot be found as published figures.**

**Which places this touches:** read from M1's extract with ONS's column found exactly.
- **Only 3 of the 1,426 City and Town places:** "London", "City of Westminster" and "City of London". Each is an Open
  Names City, and each point lies in a London BUA with no count.
- "London"'s point lies in the City of Westminster BUA.
- **A fourth, New Galloway** (Scotland), lies in no NRS settlement. It keeps 0, as the rules already say.

**Proposed rule L**, by code joins only, never by name:
- **L1:** a place in a BUA with no ONS count, and inside the London region (`RGN22CD` `E12000007`, from ONS's Regions
  boundaries), takes the Census 2021 usual residents (Nomis TS001) of the local authority district containing it
  (`LAD22CD`, from ONS's district boundaries).
- **L2:** except that a City whose name equals the name of the region containing it takes the region's count.
- **What L gives** (read from Nomis): **London 8,799,728** (`E12000007`); **City of Westminster 204,236**
  (`E09000033`); **City of London 8,583** (`E09000001`).
- **Why the region for "London":** with Westminster's figure, the city a map most needs to label would rank below
  Croydon. With 0, it would be dropped from any top N.
- **The alternative:** keep London's three places in whatever N, with population 0, which the format allows ("0 = no
  figure"). That is honest, but a client that ranks by population would never label "London".
- **The sources L adds:** ONS's Regions and Local Authority Districts (December 2022) boundaries, and Nomis TS001 at
  two geography types. All are OGL v3, with ONS's attribution line, already carried for the BUA boundaries.

### A3.3 M1′'s predictions, frozen before any M1′ reading

**When it runs:** **only after rule L, or its replacement, is ruled.** If the ruling changes L, these predictions are
restated before any run.

**What changes from M1:**
- **The three instrument defects, fixed:**
  - ONS's counts column found by its exact name, "Counts";
  - `files.terrain` counting only the tiles each package's summit elevations read;
  - every package's section bytes recorded **before** §6's budget as well as after.
- **Roads:** chained (Q18), in variant (iii), every A road, and (iv), primary A roads.
- **Rivers:** chained by name (A3.1).
- **Places:** City and Town; the N most populous within R, ties by name in byte order, kept in file order;
  **N ∈ {64, 128, 256}**; London's three by rule L.
- **NI:**
  - **graded with NI out,** by the source gate Q19 rules;
  - **printed with NI in** for BFS, LDY and GLA;
  - BFS and LDY with NI out hold only the GB features their squares reach.
- **The same 12 centres, and radii 161 and 222 km** (97 km printed).

**The controls** (M1's, plus four):
- **London's three populations** read as A3.2's figures;
- **the river chaining:** a synthetic three-link river with one name, in the middle link's reversed direction, chains
  into one polyline of 2 + 2 + 2 − 2 = 4 distinct vertices, in order;
- **the place selection:** synthetic places with tied populations come out ordered by name;
- **`files.terrain`:** one package built on its own reports the same count as when it is built after every other
  centre.

**Predicted:**

| | measured | predicted |
|---|---|---|
| **M1′-a** | X at 161 km, any centre, variant (iv), for each N ∈ {64, 128, 256} | **no centre X for any N**, so **the rule takes N = 256** |
| **M1′-b** | BHX at 161 km, (iv), N = 256 | **D** |
| **M1′-c** | LHR, MAN, BHX at 161 km, (iii), N = 256 | **D**, all three: none fits without drops |
| **M1′-d** | LHR, MAN, BHX at 222 km, (iv), N = 256 | **D**: out of X once rivers and motorways chain |
| **M1′-e** | the other nine centres at 161 km, (iv), N = 256 | **F, C or D**; never X |
| **M1′-f** (printed) | per package: bytes per section before and after the budget, the drops, the class-2 road length kept (iii and iv), and NI in against NI out | — |

**Sabotages, each of which must turn its row red:**
- **ε = 0:** the pre-budget road bytes rise at every centre;
- **no drop policy:** every D becomes X;
- **river chaining off:** the pre-budget river bytes equal those of an unchained river build, centre by centre;
- **N = all places:** the places bytes equal a no-selection build, and are larger than N = 256's wherever more than 256
  places lie inside R.

**What the results will mean:**

| outcome | meaning |
|---|---|
| **(a)** M1′-a holds | N = 256 (the rule, as ruled); M2 builds with it |
| **(b)** some N leaves a centre X, but a smaller N does not | N is the largest that does not (as ruled) |
| **(c)** BHX is X at 161 km for every N | **stop** (as ruled). Coarser ε and v2 come up only then |
| **(d)** for Q12, (iii) fits (F or C) at LHR, MAN and BHX | class 2 = every A road |
| **(e)** for Q12, (iii) does not fit and (iv) does | class 2 = primary A roads |
| **(f)** for Q12, neither fits without drops | **Q12 cannot be decided by fit.** The drops decide what is kept, so I report both kept lengths and it is ruled. **This is what I predict** |
| **(g)** anything else, or a control failing | stop and decide |

### A3.4 Rule L, ruled; and M1′'s results (read 2026-10-28, 19:54-20:02Z): every row as predicted

**The ruling (reviewer chat, pasted by Daniel, 2026-10-28), verbatim:**
> 2. Rule L: accepted, both parts, joined by code.
> - L1: a place in a London BUA with no count takes its local authority district's Census 2021 usual residents (City
>   of Westminster 204,236; City of London 8,583).
> - L2, pinned to the code rather than a name rule: the Open Names City "London" takes the London region's figure
>   (E12000007), 8,799,728.
> - PROVENANCE's processing line names rule L.
> - M1′ runs now, as frozen at c7ac2db. Report both road variants' kept A-road lengths for Q12, as you planned.

**How L2 is pinned:** to the one Open Names feature, by its ID (`osgb4000000` + `074813508`, split here for the
device-id hook), and to region code `E12000007`. A control asserts that the ID is the City "London".
- PROVENANCE carries `processing.population` naming rule L, and `processing.names` with Q10's English-name rule.

**The instruments, committed before any M1′ run:** `m1p_sizes.py` and `m1p_extract_rivers.py` at `f5d7647`, the grader
at `b574a8e`.
- **Rule L's sources** (ONS's London districts and region, December 2022; Nomis TS001 at districts and regions) are
  in the manifest.
- **The rivers' re-extract** with node ids kept 98,628 polylines, M1's count, every one with both nodes.
- **Raw outputs** (`docs/uk-data/r1/`): `m1p-sizes.json` (sha256 `6201cb6aacb7…`) and the four sabotage runs.

**The controls: all passed.**
- 222 of 222 packages validate `kOk`;
- `synth.py --check` reports "every committed byte equal";
- M1's geometry cases all pass;
- the three-link river chains to exactly 4 vertices in order;
- tied places come out by name;
- rule L reads 8,799,728, 204,236 and 8,583, and the pinned ID is the City "London";
- `files.terrain` is 34 for the first package built, and 34 when the same package is rebuilt after every other centre.

| | read | verdict |
|---|---|---|
| **M1′-a** | X at 161 km, (iv): **no centre at N = 64, 128 or 256**; (iii) the same | **as predicted**: **N = 256** |
| **M1′-b** | BHX 161 km (iv) N = 256: **D** | **as predicted** |
| **M1′-c** | LHR, MAN, BHX 161 km (iii) N = 256: **D, D, D** | **as predicted** |
| **M1′-d** | LHR, MAN, BHX 222 km (iv) N = 256: **D, D, D** | **as predicted** |
| **M1′-e** | 161 km (iv) N = 256: NCL D, EDI D, **GLA C**, ABZ C, INV C, CWL D, NQY F, **BFS F, LDY F** | **as predicted** |

**The sabotages: all four red.**

| | read | verdict |
|---|---|---|
| ε = 0 | pre-budget road bytes rose at 12 of 12 centres (LHR 102,918 → 514,198 B) | red |
| no drop policy | every 161 km (iv) N = 256 D became X (6 of 6) | red |
| river chaining off | the pre-budget river bytes equal M1's unchained figures **exactly** where M1 was F/C: ABZ 8,049, INV 8,989, NQY 5,012 B. Chained they are 3,798, 4,644 and 1,896 B | red |
| N = all places | larger than N = 256 wherever more than 256 places lie inside R (LHR 470 places: 8,539 against 4,710 B; MAN 541; BHX 697; NCL 276; CWL 368), and equal elsewhere | red |

**Q12: outcome (f), as predicted.** Neither variant fits without drops at LHR, MAN and BHX. §6's drops decide what is
kept: class-2 roads, shortest first.

**The kept A-road lengths at 161 km, N = 256** (km kept / km in the square):

| centre | (iii) every A road | (iv) primary A roads |
|---|---|---|
| LHR | **11,068 / 18,824** (D) | 7,251 / 8,857 (D) |
| MAN | **13,620 / 19,532** (D) | 8,916 / 10,441 (D) |
| BHX | **13,089 / 26,599** (D) | 9,197 / 12,481 (D) |
| NCL | 8,908 / 9,828 (D) | 4,072 / 4,172 (D) |
| EDI | 9,396 / 9,854 (D) | 3,682 / 3,683 (D) |
| GLA | 8,341 / 8,488 (D) | 3,006 / 3,006 (C) |
| ABZ | 5,925 / 5,992 (D) | 1,785 / 1,785 (C) |
| INV | 5,838 / 5,841 (D) | 1,925 / 1,925 (C) |
| CWL | 11,056 / 13,525 (D) | 5,888 / 6,221 (D) |
| NQY | 4,159 / 4,207 (D) | 1,594 / 1,594 (F) |
| BFS | 2,755 / 2,755 (C) | 688 / 688 (F) |
| LDY | 1,213 / 1,213 (F) | 485 / 485 (F) |

- **In the same 32 KB, (iii) keeps more A-road length than (iv) at all 12 centres.** At LHR it is 11,068 km against
  7,251.
- **What (iii) does not promise:** shortest-first drops can remove a short primary-route chain while keeping a longer
  non-primary one. This run measured lengths, not whether every primary route survives.
- **Recommended, for the ruling:** (iii), every A road, thinned by §6's drops. Its cost is that guarantee: a primary
  route could go missing where (iv) would show it.

**NI in against NI out (printed, (iv), N = 256):**

| centre | 161 km, NI out | 161 km, NI in | 222 km, NI out | 222 km, NI in |
|---|---|---|---|---|
| GLA | C, 28,937 B | D, 56,964 B | D, 46,514 B | D, 89,641 B |
| BFS | F, 10,322 B | D, 51,826 B | C, 30,765 B | D, 68,768 B |
| LDY | F, 5,821 B | D, 51,384 B | F, 14,327 B | D, 56,487 B |

So NI's own chaining rule (ruled for later) matters: OSNI's lines alone add about 40-45 KB to an NI centre at 161 km.

### A3.5 The rulings on M1′, verbatim (source: the reviewer chat, pasted by Daniel, 2026-10-28)

> 1. M1′: accepted as read. N = 256.
>
> 2. Q12: (iii), every A road, with one change to what the drops take first.
> - The drops take non-primary chains before primary ones: non-primary chains shortest first, then primary chains
>   shortest first. So every A road stays in sparse areas, and in dense areas the primary network is the last to thin.
> - That answers the caveat you named: a short primary chain no longer goes before a longer non-primary one.
> - It's a UK source rule, like chaining. The package format doesn't change, and both builders take the drop key from
>   the source's primaryRoute flag.
> - M1″, a small re-run with predictions first: the new drop key at LHR, MAN and BHX at 161 km. Report the share of
>   primary-route length kept, and that each outcome class is unchanged from M1′.
>
> 3. #492's N6: still owed. Daniel is being asked again; your token file has the R2 and observability keys but no
>    analytics-read line.
>
> 4. NI in vs out: noted, for NI's own chaining rule after LPS answers.
>
> 5. The disk: 11 GB free is tight for a week of collection beside the other sessions' caches. If free space falls
>    below 5 GB, pause the collection and report. The gap rule covers what's lost.

### A3.6 M1″'s predictions, frozen before any M1″ reading

**A reading the ruling leaves open, fixed here before any run:**
- **The problem:** Q18 chains links by road number and class, so one chain can hold both primary and non-primary
  links. The drop key needs one flag per chain.
- **The reading: a chain is primary if any of its links has `primaryRoute` = true.** That keeps the primary network the
  last to thin, which is the ruling's stated purpose.
- **The alternative,** "primary only if all its links are", would drop a mixed chain early, primary links and all.
- **M1″ prints** how many chains are mixed, and their length, so the reviewer can see whether the reading matters.

**The drop order** (§6's steps, with the ruled key on class 2):
1. double ε once;
2. water rings, smallest area first (RD18);
3. class-2 chains: **non-primary chains shortest first, then primary chains shortest first** (RD19's length, ties as RD19).

**Scope:** LHR, MAN and BHX at 161 km, variant (iii), N = 256, NI out. M1′'s instruments are used, with only the drop
key added behind a flag.

**The metrics:**
- **the primary-route length kept:** the in-square length of the class-2 chains flagged primary that survive the drops,
  over the in-square length of all chains flagged primary;
- the non-primary length kept, measured the same way.

| | measured | predicted |
|---|---|---|
| **M1″-a** | each centre's outcome class | **D, D, D**: unchanged from M1′ |
| **M1″-b** | the non-primary length kept | **0 km at all three.** M1′'s (iv), with primary roads alone, still needed drops at all three, so every non-primary chain goes before any primary one is touched |
| **M1″-c** | the share of primary-route length kept | **≥ 60 % at each.** M1′'s (iv) kept 82 %, 85 % and 74 %; mixed chains add non-primary length to the primary set, so the share can only be lower |
| M1″-d (printed) | mixed chains (count, km), primary km kept and in the square, bytes per section | — |

**Controls:**
- **The order function, on a synthetic set:** a long non-primary chain, a short non-primary chain and a short primary
  chain come out non-primary-short, non-primary-long, primary-short.
- **On every package:** if any primary chain is dropped, no non-primary chain is kept.

**Sabotage:** the drop key off (M1′'s shortest-first order). It must reproduce M1′'s (iii) results at all three centres
**exactly**: bytes after the budget, and class-2 km kept (11,068.3, 13,620.1 and 13,088.9).

**What the results will mean:**

| outcome | meaning |
|---|---|
| **(a)** M1″-a to c as predicted | the drop key stands as ruled |
| **(b)** an outcome class changes | stop and decide |
| **(c)** anything else, or a control failing | stop and decide |

### A3.7 M1″'s results (read 2026-10-28, about 22:09-22:20Z): every row as predicted, outcome (a)

**The instrument:** `m1p_sizes.py` at `a9790e2`, with `--m1pp --primarylast`; the grader at `b60ac34`. Raw outputs:
`docs/uk-data/r1/m1pp.json` and `m1pp-sab.json`.

**A slip, recorded:** the sabotage's first launch ran from the wrong directory and never started ("can't open file",
exit 2). A shell `&` had backgrounded the `cd` with the first run only. It was re-run from the right directory, exit 0.
No reading came from the failed launch.

**The controls, all passed:**
- the synthetic order comes out non-primary short, non-primary long, primary short (`[1, 0, 2]`);
- on all 3 packages, no non-primary chain is kept while a primary one is dropped;
- all `kOk`;
- `synth.py --check` matches every byte.

| | read | verdict |
|---|---|---|
| **M1″-a** | LHR **D**, MAN **D**, BHX **D**; M1′ had D, D, D | **as predicted**: no class changes |
| **M1″-b** | non-primary length kept: **0 km** at all three (of 9,675, 8,629 and 13,504 km in the square) | **as predicted** |
| **M1″-c** | primary-route length kept: **LHR 80.8 %** (7,388 of 9,149 km), **MAN 84.6 %** (9,219 of 10,896), **BHX 72.5 %** (9,490 of 13,088) | **as predicted** (≥ 60 %) |
| sabotage: the drop key off | reproduces M1′'s (iii) **exactly** at all three: bytes after the budget 32,761 / 32,760 / 32,757; class-2 km kept 11,068.3 / 13,620.1 / 13,088.9 | **red, as required** |

**Printed (M1″-d):**
- **Mixed chains:** LHR 140 (565 km), MAN 177 (800 km), BHX 259 (1,045 km): about 6-8 % of the primary set's length.
  **So the "any link primary" reading moves little.**
- **The trade, stated plainly:** the ruled key keeps **less total A-road length** than M1′'s shortest-first order. LHR
  keeps 7,388 km against 11,068. What it keeps is the primary network, and every non-primary chain in these three
  squares goes first.
- **Where the class-2 set fits without drops** (GLA, ABZ, INV, NQY, BFS and LDY at 161 km in M1′), the key never acts:
  every A road stays, as the ruling intends for sparse areas.

### A3.8 The ruling on M1″, verbatim (source: the reviewer chat, pasted by Daniel, 2026-10-28)

> M1″: accepted, outcome (a). Q12 is settled: every A road, with non-primary chains dropped before primary ones, and a
> chain counting as primary if any of its links is (A3.6). The sabotage reproducing M1′ exactly shows the drop key is
> the only change. The wrong-directory launch produced no reading and is noted. N6 waits for the analytics-read line,
> which Daniel is adding to the token file now.

**The UK road rule, settled:**
- class 1 = Motorway;
- class 2 = every A road, with slip roads dropped;
- links chained by number and class (Q18);
- under §6's budget, non-primary chains drop before primary ones, each shortest first. A chain is primary if any of
  its links is.
- **With:** rivers chained by name; places City and Town, the 256 most populous within R; London by rule L; NI gated
  out until LPS answers (Q19).
- **These are M2's inputs.**

**N6, since:** read once the line was in the file. As predicted, with no regression (`docs/wx-server.md` on #494).

### A3.9 L, the bucket's terms (read 2026-10-29, ~03:30Z): outcome (b), as predicted

**The ruling that started R2 (reviewer chat, pasted by Daniel, 2026-10-29)** said "Read L (the bucket's terms) now".
It is recorded verbatim with R2's predictions (`docs/wx-opuk.md`).

**What was read:**
- **the ORD documentation:** its overview, discovering-and-accessing and MQTT pages; the HVD summary was read
  2026-10-28;
- **its GitHub repository's README:** CC BY 4.0 for the documentation, no terms of use, and no LICENSE file;
- **MeteoGate's site:** its "Terms and conditions" link leads to the documentation's references page, which links the
  **MeteoGate Data Governance Policy** (`Data_governance_policy.docx`, 202,756 B, sha256 `4ecbdfa0f7f7…`, 163
  paragraphs, all read);
- **the API-key portal:** it renders only a title without scripts, so it carried no readable terms.

**L2, the headers.** A listing (200) and an 8 KB range GET (206) on `openradar-24h`, both sent with the ingest's
User-Agent.
- **No rate-limit, quota or retry header on either. As predicted.**
- **Present, and worth knowing:** `x-amz-expiration: expiry-date="…", rule-id="24h-cache-lifecycle"`. That is the
  cache's own lifecycle rule.

**L1, a published limit for the S3 cache:**
- **None. As predicted.**
- The ORD page names only an `x-ratelimit-remaining` header for anonymous *API* queries, with no number. The S3 cache
  sent no such header.

**The only governing text,** from the Data Governance Policy, verbatim:
> "To protect the availability of this service for everyone, accidental or deliberate heavy traffic from an IP address
> may result in disconnection. In addition, Registered users may be contacted by EUMETNET's FEMDI Coordinating Member
> to support them find and access data in a sustainable way."

**No term forbids automated use.**

**Outcome (b):** no limit stated, no rate-limit header, no term against automated use.
- **As ruled (A2.1, item 3), R3 does not wait.** The reviewer drafts the courtesy note, and Daniel sends it.
- **Our load against the one clause:**
  - about 1,728 range reads and 2,880 listings a day: **about 3 requests a minute;**
  - from Cloudflare's egress, not one fixed address;
  - with a User-Agent naming us and a contact URL (K14).
  - That is not the "heavy traffic" the clause guards against.

### A3.10 The collection paused by the disk rule, and resumed (2026-11-04)

**The disk rule (ruled 2026-10-28):** "If free space falls below 5 GB, pause the collection and report."

**What happened:**
- **The watcher fired at 23:30:03Z on 11-03,** with 1 GB free.
- **Its notice reached the session only at 04:27Z on 11-04.** The watcher of the time only exited; it did not stop the
  loop. So the collection ran twice after the threshold, at 00:38Z and 03:42Z, both without error (2,340 frames held).
- **The loop was stopped at 04:27:47Z** and the pause written to `loop.log`.
- **The collection was not what filled the disk.** It holds about 900 MB (876 MB of frames, 17 MB of HDF5, 7 MB else),
  against a fall from about 39 GB free on 10-29 to 1 GB. The largest items found:
  - WSL Ubuntu's virtual disk, 53.1 GB, being written at the time;
  - `~/.platformio`, 14.2 GB, and the repo's `.pio`, 7.7 GB.
- **About 16.5 GB was freed between 04:27Z and 04:36Z by something outside this session** (nothing was deleted by it).
  `df` and two Windows counters agreed at 17,722 MB, steady over 20 s.
- **No data was at risk.** The cache holds the previous UTC day and today. All of 11-03 was already collected, and any
  run before 11-06 00:00Z collects every frame up to the frozen end.

**The decision, Daniel, 2026-11-04, verbatim:** "1".

It answered:
> 1. **Resume**, with the disk watcher re-armed: catch-up runs on the 3-hour cycle to 16:00Z, or one final run after
>    16:00Z. Either way the frozen window is complete.
> 2. **Or close the collection at 03:44Z** and read T, B and C on what's held.

**Done, 04:48:14Z:** the loop was re-armed on the same cycle and the same end, 2026-11-04T16:00Z, and so is the watcher.
- **The loop's last run lands just after 16:00Z,** as the original loop's would have. That covers both readings of (1).
  Frames after 16:00Z are filtered out by time at the readings.
- **One change in how the ruling is carried out, not in the ruling:** below 5 GB, the watcher now pauses the
  collection itself, instead of only exiting. It writes `PAUSE` in the collection folder, and the loop skips every run
  while that file exists.
- **Rehearsed on copies before arming:**
  - with `PAUSE` present, the collector does not run;
  - without it, the collector runs;
  - given a threshold it must trip, the watcher creates `PAUSE` and exits 2;
  - after the loop's end, the watcher exits 0 and creates no `PAUSE`.
- **The scripts:** `docs/uk-data/instruments/r1_loop.sh` and `r1_watch.sh`.

### A3.11 The ruling on A3.10, verbatim (source: the reviewer chat, pasted by Daniel, 2026-11-04 ~05:05Z)

> A3.10: accepted.
> - The restart on Daniel's "1" is accepted: same cycle, same frozen end (16:00Z), and frames after 16:00Z dropped by time.
> - The watcher now writing PAUSE is accepted as how the disk rule is carried out. The rule itself is unchanged.
> - The rehearsal on copies is accepted as the check that it can fire.
> - The record of where the disk went is noted. The collection holds about 0.9 GB; the rest is WSL's virtual disk and
>   PlatformIO's caches, which aren't yours to touch.
>
> Next as you planned: the last run after 16:00Z, then T, B and C. C's reading comes to me with the clutter-mask proposal
> before R3, predictions first.

### A3.12 T, B and C: what the frozen rows leave open, pinned before any instrument runs (2026-11-04, ~05:20Z)

**Nothing below has been computed on the week.** No instrument runs on the week before the loop logs `loop ended`.
The instruments are proved first on synthetic data, each control and sabotage included.

**The week, and its controls.**
- **The week's frames:** every `DBZH` `.tiff` key whose V lies in **[2026-10-28T00:00Z, 2026-11-04T16:00Z]**. Later
  frames are dropped by time, as ruled.
- **The gap rule's 7 days** are the UTC days 10-28 to 11-03. A day is **wholly held** when every key listed with a V
  on that day has its frame held (its `.json` and `.tiles`). The part of 11-04 up to 16:00Z counts in the readings,
  not in the gap rule.
- **The listing control:** for every key in more than one listing, the `.tiff`'s `LastModified` is identical in all.
  One that differs means stop.
- **The decode control:** `decode_check.py` on each of the 8 held `.h5` files (one a UTC day, 10-28 to 11-04). One
  mismatch makes T, B and C BLIND.

**T.**
- **T1:** the steps between consecutive V's, sorted.
- **T2:** P − V per key, with P the `.tiff`'s `LastModified` (A2.1, note 1). p50 and p95 by the nearest-rank method.
- **T3:** the 300 s slots from 10-28T00:00Z to 11-04T16:00Z inclusive, **2,209**, of which slots with no key.
- **The sabotage:** every third key, by V, dropped.

**B.**
- **The offset:** read from `cref`'s `wx_ingest` events in Workers Logs (`t0` = event time − `ms`) over the newest
  24 h the logs hold.
  - **Two numbers:** the median of `t0` mod 60 for the `:00` checks, and the same for the `:30` checks.
  - **If either cannot be read,** B is graded with both checks at offsets 0 s and 5 s, and both must meet the
    predictions.
- **The model:**
  - **A check** at time t stores the newest frame with P ≤ t, which is servable from t + `b`.
  - **The served frame** at time s is the newest frame stored by a check with t + `b` ≤ s.
  - **The device** polls at φ + 60 n, and from its reply holds the frame served at the poll. It draws while
    now − V ≤ 600 s (`WhatToShow`: blank only when the age is over 600).
- **The span measured:** [10-28T00:10Z, 11-04T16:00Z]. Starting 600 s in removes the start-up blank that frames
  before the week would have covered.
- **The readings:**
  - **B2:** blank intervals in the span, over its hours, averaged over the 60 phases.
  - **B3:** the longest over every phase.
- **The extra checks:** besides `:00` and `:30`, a check at V + 240 + 10 k s (k = 0, 1, …) until one finds the frame
  listed, at the `:00` checks' offset.
  - **B4:** their count a day, times 8 ms of CPU, and one listing each.
- **`b` = 2 s graded;** B printed at 0 and 5 s, and with the reply 1 s late.

**C.**
- **The grid:** the collection's stored UK window, on the HDF5's georeference (the corrected corner).
- **An echo:** value ≥ 15 (NaN and −9,999,000 are not). **The mask:** quality < t, strict, with t = 0.5 graded.
- **Isolated:** the 11 × 11 neighbourhood centred on the cell (±5), clipped at the window's edge, holds ≤ 3 echoes,
  the cell included.
- **C2's masked run** recomputes isolation over the masked echoes.
- **Persistent:** isolated in at least 0.10 × N of the week's N held frames.
- **C3:** over the week, the unmasked run's non-isolated echoes with quality < 0.5, over all its non-isolated echoes.
- **C4:** the cell-frames ≥ 65 dBZ, unmasked and masked; and C1-C3 at t = 0.1, 0.2, …, 0.9.
- **The control's cell:** the first cell in row-major order that is covered (not −9,999,000) in every frame and whose
  11 × 11 neighbourhood holds no echo in any frame. Planted at 40 dBZ in every frame:
  - with quality 1.0, **C1 + 1 and C2 + 1**;
  - with quality 0.2, **C1 + 1 and C2 unchanged.**
  - If no such cell exists, the control is BLIND, so C is BLIND.
- **The sabotage:** C2's run with the mask off gives C2 = C1, which is red unless C1 is 0.

**Outside the set:** any reading not listed in A1.3 or here means stop and report.

### A3.13 The ruling on A3.12 and the instruments, verbatim (source: the reviewer chat, pasted by Daniel, 2026-11-04 ~05:30Z)

> A3.12 and the instruments (da7f1bb): accepted. Run them as planned once the loop logs its end: the 8 daily decode checks
> and the listing check, then T, B and C. C's reading comes to me with the clutter-mask proposal, predictions first,
> before R3.

### A3.14 R1's readings (2026-11-04, from 16:59Z): the controls and T as predicted; B outside the set

**The collection was static before it was read.** The loop logged its end at 16:58:42Z, with no errors and 2,499
frames held. The frame files (4,998) and the listings' bytes were the same in two reads 30 s apart. `loop.err` is
empty.

**The controls:**
- **Decode:** `decode_check.py` on the 8 daily `.h5` files gives **0 differing cells of 2,221,632** on each. `r1_c.py`'s
  own decoder gives 0 on the same 8 too, so C's decoder is held to the same truth as the control's.
- **Listing:** no key's `LastModified` changed between listings.
- **The gap rule:** **7 of 7 days wholly held** (10-28 to 11-03: 288, 284, 286, 288, 288, 288 and 288 keys, none
  missing a held frame). The week counts. It has 2,201 frames up to 16:00Z on 11-04.

**T: every row as predicted.**

| | got | predicted |
|---|---|---|
| T1 | **99.73 %** of steps exactly 300 s | ≥ 99 % |
| T2 | p50 **252.4 s**, p95 **283.2 s**, **0.64 %** over 450 s | p50 in [240, 265], p95 ≤ 320, ≤ 1 % over 450 |
| T3 | **8** of 2,209 slots missing (0.36 %) | ≤ 0.5 % |

The sabotage (every third key dropped) is red in the instrument's selftest (T1 0.50).

**B's offsets, read as pinned, with one step added and stated.**
- **The first read was sampled.** It covered 24 h in one query and returned 423 events spread across the day at
  sampling (ABR) level 10: 125 `:00` and 143 `:30` checks of `cref`. Medians: 6.025 s and 5.233 s.
- **Read again in 24 one-hour windows,** each unsampled (ABR level 1, none at the 2,000-event page limit): 1,438 and
  1,436 checks. Medians **6.019 s and 5.233 s.** The sample agreed. The complete read is the one used.
- **Every `:30` check reads exactly 5.233 s,** which looked like an artifact, and it is not:
  - the cron's own `scheduledTime` sits 5.233 s past the minute;
  - each `:30` check starts exactly 30.000 s after it, on a timer;
  - the `:00` checks start 0.6-1.6 s after `scheduledTime`.
  So the checks really run at about :06 and :35.

**B: OUTSIDE THE SET, outcome (c). Stopped for a ruling.** Graded at `b` = 2 s, offsets 6.019 / 5.233 s:

| | today's schedule | predicted | with the extra checks | predicted |
|---|---|---|---|---|
| **B1** | **5.51 %** | [2.0, 5.0] | **2.97 %** | [0.5, 2.5] |
| B2 | 7.31 an hour | [4, 8] | 4.37 an hour | [2, 5] |
| **B3** | **1,177 s** | ≤ 600 | **1,177 s** | ≤ 600 |
| B4 | — | — | 768 extra checks a day: 6.1 s of CPU at 8 ms, and 768 listings | printed |

- **B1 and B3 miss on both schedules.** B2 is as predicted.
- **Neither schedule meets outcome (a)'s bar.** Outcome (b) needed every row as predicted, so this is **(c): stop
  and decide.**
- **Printed:** B1 at `b` = 0 is 5.11 / 2.73 %; at `b` = 5 it is 6.15 / 3.37 %; with the reply 1 s late, 5.72 / 3.10 %.
- **B5:** at a device limit of 720 s, B1 is **0.69 / 0.66 %**; at 900 s, **0.30 / 0.29 %** (today's / extra).

**Data beside the verdict, not a re-grade:**
- **All 8 missing slots fall between 00:10 and 00:45 UTC,** on 10-29, 10-30 and 11-04. So do the latest frames (up to
  561 s) and the longest blanks: at the worst phase, 1,177 s at 10-29 00:15 and 877 s at 11-04 00:25. OPERA's
  composite stalls in that hour on some nights, and that is what breaks B3.
- **That is not what breaks B1.** At the worst phase, blanks over 300 s hold only 4.7 % of the blank time.
- **With the checks at :00/:30 exactly** (the schedule my ranges assumed), measured on the same week:
  - today's schedule: B1 **4.67 %**, inside its range;
  - with the extra checks: B1 **2.92 %**, still outside its range.
  - So the measured offsets account for today's miss.
  - The extra schedule's range was too low on its own account. I have not shown why, and do not offer a
    cause.

### A3.15 B's stop: the cause as read, and the options (2026-11-04 ~17:15Z, asked for ahead of C)

**More data beside the verdict, not a re-grade:** B at three device limits, measured on the same week at the same
offsets. "An ideal ingest" serves each frame the instant it is listed (a check at every frame's own P, `b` = 0): the
best any ingest could do.

| device limit | today's schedule | with the extra checks | an ideal ingest |
|---|---|---|---|
| **600 s** (#445 today) | 5.51 %, 7.31/h, 1,177 s | 2.97 %, 4.37/h, 1,177 s | **2.19 %, 2.96/h, 1,169 s** |
| **720 s** | 0.69 %, 0.15/h, 1,057 s | 0.66 %, 0.13/h, 1,057 s | 0.64 %, 0.13/h, 1,049 s |
| **900 s** | 0.30 %, 0.03/h, 877 s | 0.29 %, 0.03/h, 877 s | 0.29 %, 0.03/h, 869 s |

(B1, B2, B3 in each cell.)

**The cause, as read.**
- **B1 and B2 at 600 s are set by the device's limit, not by the ingest.**
  - A frame reaches OPERA's listing with T2's 252 s median latency (95th percentile 283 s).
  - That leaves under 50 s of the 300 s that the 600 s limit allows between frames, which is less than the 60 s
    between the device's polls.
  - **The alternative, our ingest's timing, is excluded:** an ideal ingest still blanks **2.19 %, 2.96 an hour.** No
    schedule can reach outcome (a)'s bar (1 %, 1 an hour) at 600 s. The ingest's whole share is the gap between 5.51 %
    and 2.19 %.
- **B3 is OPERA's own nightly gap.**
  - The longest blanks sit on the missing slots and late frames at 00:10-00:45 UTC (10-29, 10-30, 11-04).
  - **The alternative, an ingest cause, is excluded:** B3 is the same on today's schedule, the extra checks and an
    ideal ingest (1,177 / 1,177 / 1,169 s). No ingest can serve a frame the source has not published.
  - B3 shrinks with the device limit only by the limit's own increase (1,177 → 1,057 → 877 s).
- **Why my ranges missed:**
  - today's B1 range assumed checks at :00/:30, and production runs them at about :06/:35 (at :00/:30 the week gives
    4.67 %, inside the range);
  - the extra schedule's range sat below what the extra checks reach. They land 0.78 points above an ideal ingest (the
    10 s grid, the 6 s offset, `b`), the only thing that differs between those two runs.

**The options.**

| | what | the week says |
|---|---|---|
| **1** | **#445 gets a per-product age limit, with `opuk` at 720 s** (sent by the server per product, or set in #445 for `opuk`); `cref` keeps 600 s; today's schedule; no extra checks | B1 **0.69 %**, B2 **0.15 an hour**: inside outcome (a)'s B1 and B2 bars. B3 1,057 s, only on OPERA's nightly gaps |
| 2 | the same at 900 s | B1 0.30 %, B2 0.03/h, B3 877 s. Frames up to 15 min old are shown, for 0.4 more points |
| 3 | keep 600 s and buy the extra checks | B1 2.97 %, B2 4.37/h: the layer still blinks about four times an hour, for 768 more listings a day |
| 4 | the device polls faster (WD3 (g)) | not modelled. It changes #445's request rate, and at 600 s the floor is set by OPERA's latency |

**Recommended: option 1.**
- 720 s meets B1 and B2's bar with no new cost. The extra checks buy 0.03 points at 720 s, so they are not worth 768
  listings a day.
- A frame is at most 12 min old when shown. That is consistent with `opuk`'s 1,800 s server limit, and fresher than
  option 2.
- **B3's bar (≤ 300 s) cannot be met at any limit up to 900 s while OPERA stalls at night.** I recommend recording B3
  as the source's gap, not grading it against the ingest. During such a gap the device draws nothing, which is honest.
- **This is the conclusion outcome (b) named:** #445 needs a per-product age limit before it merges. The row grading
  is (c), so it is put to you rather than taken.

### A3.16 C's readings (2026-11-04, 16:59-17:33Z), and the clutter-mask proposal with its predictions frozen

**C: OUTSIDE THE SET. Stopped for a ruling.** `r1_c.py` over the week's 2,201 held frames, with no errors.

| | got | predicted |
|---|---|---|
| **C1** | **0** persistent clutter cells | ≥ 1 |
| C2 | 0 | ≤ 25 % of C1 |
| **C3** | **98.0 %** of non-isolated echoes have quality < 0.5 | ≤ 5 % |
| C4 (printed) | cell-frames ≥ 65 dBZ: **801 unmasked, 2 masked**. C2 is 0 at every threshold; C3 runs from 96.9 % (t = 0.1) to 98.1 % (t = 0.9) | — |

- **The control: as predicted.** The planted cell at quality 1.0 gives C1 1 and C2 1; at quality 0.2, C1 1 and C2 0.
- **The sabotage is BLIND, as A1.3 foresaw** ("red, unless C1 is 0"). With C1 = 0, turning the mask off cannot change
  C2. The instrument printed `red: true` here. That label is wrong when C1 is 0, and this line replaces it.
- **The outcomes that apply:** **(b)**, C1 = 0, "the week shows no clutter; no mask"; and **(d)**, C3 misses, "the mask
  deletes real rain". Two outcomes at once is "anything else", so **stop and decide.** Both point the same way.

**C3 checked against an independent instrument before being believed.** The decode controls hold the value band to
the HDF5; nothing yet held the quality band. The alternative was that `r1_c.py` misreads band 2.
- **Excluded:** each `.h5` carries its own quality field, `dataset1/data1/quality1` (`how/task` =
  `pl.imgw.quality.qi_total`). On every echo cell of the 8 daily frames, the decoder's quality band equals it:
  **0 differences.**
- **From the HDF5 alone, with no code of ours:** 98.1 % of echoes, and 98.1 % of non-isolated echoes, have quality
  < 0.5 (per frame, 89.5 % to 99.9 %).
- **The histogram says why.** On echoes, `qi_total` is almost always **exactly 0.0**, with a small group at 1.0 and
  little between. Across the 8 frames, 173,239 of the 177,937 echoes read 0.0, and 3,188 read 1.0. In this
  composite, the quality index is not a per-echo clutter score.
- **And C1 = 0:** no cell is an isolated echo in 10 % of the week's frames. The composite reaches us without
  persistent specks.

**What this means for R2, already merged.**
- `OPUK_QI_MASK` is **on, at 0.5**, on main. So it removes about 98 % of the rain `opuk` would serve.
- **K12's crops and K13's render were made with it on.** The 8 crops on main hold 1,665 echo cells in total, 6 at LHR.
- **K13's "LHR is all dry" is likely the mask, not the weather,** and is withdrawn as a statement about the weather.
- Nothing is deployed: R3 has not run, and no device reads `opuk`.

**The proposal: turn the mask off before R3.**
- `OPUK_QI_MASK = { on: false, threshold: 0.5 }`. The switch stays, so a future product with a real clutter score can
  use it.
- Q17 (what a masked echo becomes) is then moot: nothing is masked.
- **One PR, from main.** Its predictions, frozen here before any code:

| | check | predicted |
|---|---|---|
| **M1** | K15, restated for a default of off | with the default: (40 dBZ, QI 0.4) → **6**, (40, 0.5) → 6, (40, 0.9) → 6. With the switch on: K15's frozen rows unchanged (0, 6, 6; off 6), and its sabotage (`<=` for `<`) still red |
| **M2** | K12 rebuilt with the mask off | each crop's echo cells **≥** its masked count (it cannot fall: the unmasked echoes include the masked ones). **The total ≥ 10 × 1,665 = 16,650.** 8 / 8 decode, and the sabotage fails exactly `man-161km.wxc` |
| **M3** | K13 re-rendered at LHR, and the ABZ extra | rendered, looked at; no echo count is predicted |
| **M4** | every other R2 row (K1-K11, K14) | unchanged: the mask is not in their path (K2′ plants quality 0.9; K9's share counts code 14, which the mask never makes) |
| — | the whole proxy suite, and host-tests | green |

**Outside the set:** any other reading means stop and report.

**C's outputs:** `docs/uk-data/r1-readings/r1-c.json` (rounded as before) and the quality cross-check in
`r1-qi-check.jsonl`.

### A3.17 The rulings on B and C, verbatim (source: the reviewer chat, pasted by Daniel, 2026-11-04 ~17:50Z)

> C: the mask-off PR is approved to build, from main, with M1-M4 and CI green as frozen in A3.16. The evidence is
> accepted: the decoder is held to the HDF5's own qi_total with 0 differences, 98.1 % of echoes are under 0.5 read
> straight from the file, and C1 = 0.
> - OPUK_QI_MASK stays as a switch, default off.
> - K13's "dry London" is withdrawn, as you recorded. The instrument's `red: true` label for the C1 = 0 case is corrected
>   in the record.
> - A follow-up on record: re-read C on a summer week (June-August), when anomalous propagation is likeliest. If
>   persistent clutter shows then, the mask question reopens with that week's data.
>
> B: option 1, a per-product age limit, with opuk at 720 s and cref at 600 s. Today's schedule; no extra checks.
> - The server tells the device each product's limit. Your side: the /wx/cref handler states 720 for opuk, and states
>   nothing new for cref. blipscope-5c's side, in #445: read it, clamp it to [600, 900], and default to 600 when it's
>   absent or invalid.
> - The channel (a response header, or a field in the crop's header) is agreed between you and blipscope-5c, with the
>   reason. You propose it, predictions first, in your PR.
> - B3 is recorded as OPERA's nightly gap (00:10-00:45 UTC), not graded against the ingest. During it the device draws
>   nothing.
> - The misses stay on record, with their causes as you read them (the :06/:35 offsets; the extra schedule's range).
>
> R3, the opuk ingest's production deploy, comes after the mask-off PR merges, and after the server side of the age limit
> merges. Predictions first. Deploy it staged and verify it, outside any announced board window: it shares the ingest with
> cref, which E2:08 reads.
>
> Before opuk serves in production, /credits carries OPERA's CC BY 4.0 attribution. Confirm it's there, or add it to the
> mask-off PR.

**/credits, checked against main at `0a84a64`:** it does not carry OPERA. It has the NOAA line (with a unit test and a
smoke check) and nothing for OPERA. So the attribution goes into the mask-off PR, in the wording A2.1 accepted in
principle ("Final at R4's review"), with its own predictions frozen in that PR before its code.

**A follow-up on record:** C re-read on a summer week (June-August).

### A3.18 The rulings on #521 and #522, verbatim (source: the reviewer chat, pasted by Daniel, 2026-11-04 ~18:25Z)

> #521:
> - The credits-test fix is approved: the check reads the photo-credits section only. Predictions as frozen: the
>   public-domain photo passes, and a CC-BY-SA-4.0 one still fails. Its control: the PD photo moved into the
>   CC-licensed form turns it red.
> - M1-M5 accepted, as read: 192,184 echo cells against 1,665 masked; M3's renders; K11 0.27-0.34 s CPU a frame, 54.6 MB
>   peak; M5 with its control.
> - Approved to merge at the head that adds exactly that fix on top of b265b3ce6941. CI complete, 0 failed, with the
>   count predicted before the push. Squash with --match-head-commit, no --admin; record main's CI. Anything else: send
>   it to me.
>
> #522: X-Wx-Max-Age is the channel, ruled for both sides. The reasons are accepted: WXC1 v1 stays frozen, a 304 restates
> the header, and a lost header falls back to 600.
> - blipscope-5c implements the device side in #445 (my block to it).
> - #522 can leave draft now. A device that doesn't read the header is unaffected.
> - Approved to merge at 04da4bc76b70 once its CI is complete with 0 failed and the count predicted. Same merge terms.
>
> R3 after both merge: predictions first. Staged and verified, outside any announced board window. It shares the ingest
> with cref.

### A3.19 The ruling on #521's and #522's count, verbatim (source: the reviewer chat, pasted by Daniel, 2026-11-04 ~19:15Z)

> #521 and #522: (a). The count is re-registered at 41 on both: 38 passed, plus firmware.yml's `version`, `promote` and
> `ship-alert`, skipped. `ship-alert` and `version` need `build`, and `promote` needs `version`, so their checks exist
> only once the build matrix finishes; your baseline was read before that. The miss is kept on both PRs. Lesson
> accepted, with one addition: count only from a finished run, and a job behind `needs:` shows up only when what it
> needs has finished.
> - Merge #521 at edfe52fea3c1, then #522 at 04da4bc76b70: squash, --match-head-commit, no --admin. Record main's CI
>   on each merge commit.
> - I checked both against today's main (ec4f0d4, after #518, #513 and #517): each merges cleanly, and #522 merges
>   cleanly after #521. #521 and #513 both touch smoke-prod.sh: the merge is clean, and your edfe52f run came after
>   #513 merged, so it tested them together. Nothing under proxy/src changed on main since your base. If GitHub
>   reports a conflict anyway, stop: a hand-resolved head needs a fresh look.
> - After #521 merges, production's smoke has the OPERA check, and it fails until a proxy deploy carries #521. Your
>   next predictions: that deploy (a "carried by the deploy" row, as #513 did; the smoke as the test identity, now 38
>   checks with the OPERA line passing; cref's /wx unchanged with no X-Wx-Max-Age; what /wx does for opuk before R3),
>   then R3's. Nothing deploys before my ruling on them.
>
> Disk: accepted. node_modules in your own worktrees counts as regenerable (npm ci rebuilds it). C: read 15.5 GB free
> at 19:13Z, steady: above the 10 GB stop, so disk-wise R3's small Worker bundle is fine. Large builds still wait for
> 30 GB.

### A3.20 Items A (Northern Ireland: LPS's answer) and B (OPERA coverage by country), verbatim (source: the reviewer chat, pasted by Daniel, 2026-11-05 ~21:20Z)

> Two items. Neither touches production, staging or the dev bucket, and neither may delay item 3 or R3'.
>
> A. Northern Ireland: LPS's written OK has arrived
> 1. Facts, taken from the email thread itself. On 2026-10-28 (15:53Z) Daniel asked LPS whether map files derived from these may be redistributed to customer devices under OGL v3.0: OSNI Open Data 50K Transport; OSNI Open Data Gazetteer; DAERA's Lakes of Northern Ireland (if derived from OSNI mapping); NISRA settlement populations joined to NISRA settlement boundaries (to rank towns). LPS's IPR/Copyright team (OSNI) answered on 2026-11-04 at 14:03Z: yes, they are Open Data and can be used to create small map files sent to customer devices, provided the OGL acknowledgement is included. Required wording, verbatim:
>    Contains Ordnance Survey of Northern Ireland information licensed under the Open Government Licence v3.0.
>    Where a link is possible, "Open Government Licence v3.0" links to https://www.nationalarchives.gov.uk/doc/open-government-licence/version/3/
> 2. Read-only first: open each dataset's own publisher page and record its licence and any attribution statement it asks for, with URL and date read. LPS answered for all four, but DAERA and NISRA publish separately: any line their page asks for goes in beside the OSNI line. If a page isn't OGL v3.0, or conflicts with LPS's answer, stop and report that dataset; its layer stays unserved until I rule.
> 3. Record (small docs PR): where the NI gate is written, put the dates, organisation and team, the four datasets as asked, the answer in your own words, the line verbatim, and step 2's findings. No personal names, email addresses or phone numbers in the repo; Daniel keeps the email. The NI gate lifts when this PR merges. NI still ships with GB under the UK map plan, so its timing doesn't change. Predict CI from the latest finished docs-only run; I review before merge.
> 4. The line ships in the same change that first serves NI-derived data, never after, everywhere the GB OS OpenData line goes: docs/basemap-attribution.md, /credits, and the provenance list of each package holding NI-derived features. Predictions frozen first. Checks: (a) live /credits holds the line exactly, as one string, with the OGL link; (b) an NI fixture package carries the NI provenance entry. Sabotages: change one word → (a) red; drop the entry from the NI fixture → (b) red.
>
> B. OPERA coverage by country (Daniel's go): read-only survey
> Purpose: rank European countries by how much of their land the open OPERA composite covers. Input for Daniel, not a build plan. Run it after your item-3 PR is pushed, outside any R3' slot or board hold.
> 1. Read-only: no deploy, no Worker/bucket/KV writes anywhere, ingest untouched. Read only the public OPERA source the ingest already uses, one request at a time: at most 24 frames plus their listings, 1 GB or less, in your own scratch. Check free disk first (the under-10 GB rule stands). You may delete your own downloaded frames afterwards.
> 2. Frames: the DBZH composite the ingest reads, hourly on the hour, 2026-11-04 00:00–23:00Z (or the latest full UTC day the source holds; say which). Fewer than 20 readable → stop and report.
> 3. Countries: Natural Earth admin-0 at 1:10m (name the version), rasterised onto the composite's own grid with the same reader and georeferencing as opuk (K1 corner rule), pixel-centre rule. Covered = data or undetect; nodata = not covered. Covered % = covered / in-domain land pixels; in-domain % = in-domain land / the country's whole land. Rank countries with at least 50% in-domain and at least 100 pixels; list the rest apart with their numbers.
> 4. Controls, predictions frozen before code. Each has a sabotage that must turn it red; one that doesn't = BLIND = FAIL → stop and report.
>    C1 same reading: on one frame, your UK figure matches wx-opuk-verdict.sh's for that frame, exactly if both count the composite's own pixels, otherwise within a tolerance you state in the predictions. Sabotage: count undetect as uncovered, on a frame with undetect pixels in that area (give the count).
>    C2 geometry: for ranked countries wholly in-domain, pixels × pixel area within ±3% of the polygon's area computed in an equal-area projection. Sabotage: rasterise with the wrong pixel size.
>    C3 placement: Shetland (no radar coverage, per R1) under 5% covered in every frame. Sabotage: a misplaced raster (e.g. north–south flip). Pre-ruled: if your first sabotage doesn't turn it red, pick one that does, miss kept.
> 5. Report in your reply; no PR until I've read it. Ranked table: country | in-domain % | covered % (median of frames) | lowest frame | frames read | contributing radars by country prefix (only if the files carry a node list). Also give the controls and sabotages (predicted/observed), plus the script's sha256 and command line. Coverage only: clutter, quality and latency aren't graded.

### A3.21 A, step 2: each dataset's publisher page, read (2026-11-05, 21:28-21:36Z). Three of the four stop

Read-only. Every URL is the one fetched, and every quote is the page's own.

| dataset (as asked) | the page read | licence stated | attribution it asks for | verdict |
|---|---|---|---|---|
| **DAERA, Lakes of Northern Ireland** | DAERA's own hub catalogue, `https://opendata-daerani.hub.arcgis.com/api/feed/dcat-us/1.1.json` (landing page `https://opendata-daerani.hub.arcgis.com/datasets/DAERANI::lakes-of-northern-ireland`), read 21:32:18Z | **OGL v3.0**: "DAERA NIEA dataset contains Ordnance Survey of Northern Ireland information licensed under the Open Government Licence v3.0. Per the National Archive guidance on OGL found at: Open Government Licence (nationalarchives.gov.uk)", linking `http://www.nationalarchives.gov.uk/doc/open-government-licence/version/3/` | that same sentence. It is DAERA's line, and it confirms the data is OSNI-derived | **consistent with LPS's answer.** DAERA's line goes in beside the OSNI line |
| **OSNI Open Data 50K Transport** | **OSNI's own pages could not be read.** OpenDataNI (`admin.opendatani.gov.uk/dataset/...`) answers **403** to every fetch, and its public front end gives 404/500. OSNI's hub item (`osni-spatialni.opendata.arcgis.com`, ArcGIS item `476bb4e6f8e64f60a08ae84888733bd4`) answers "Item does not exist or is inaccessible". **Read instead, a mirror:** `https://www.data.gov.uk/dataset/828a8d26-b199-4620-9e02-b61557792500/osni-open-data-50k-transport-transport-lines`, harvested from OSNI's hub (record updated 8 July 2026) | licence field "UK Open Government Licence (OGL)", **but its text: "By download or use of this dataset you agree to abide by the LPS Open Government Data License."** | none stated beyond that | **STOP: the publisher's page can't be read, and the mirror names the "LPS Open Government Data Licence", not OGL v3.0** |
| **OSNI Open Data Gazetteer** (place names) | the same blocks. Mirror: `https://www.data.gov.uk/dataset/4ad70f94-c874-452c-94ee-2b2c39f57006/osni-open-data-gazetteer-place-names` (harvested from OSNI's hub; updated 8 July 2026) | field "UK Open Government Licence (OGL)"; **text: "By download or use of this dataset you agree to abide by the LPS Open Government Data Licence."** | none beyond that | **STOP, as for 50K Transport** |
| **NISRA settlement populations joined to NISRA settlement boundaries** | NISRA's pages: `https://www.nisra.gov.uk/publications/2011-census-headcount-and-household-estimates-settlements-northern-ireland` (published 26 March 2015) and `https://www.nisra.gov.uk/publications/settlement-2015-documentation` (published 2 May 2016). Neither states a licence; both defer to the site's `https://www.nisra.gov.uk/crown-copyright` (last updated 30 December 2025) | Crown copyright page: "under the terms of Open Government Licence", linking OGL v3, **and: "Ordnance Survey of Northern Ireland (OSNI) branded Land & Property Services (LPS) mapping (or any mapping based on, or derived from OSNI branded mapping) is exempt from the terms of the OGL."** The boundaries' own OpenDataNI page (Settlement Development Limits 2015) answers 403 | "acknowledge the source of the Information by including attribution statements specified by the Information Provider(s)", with the example **"Source: NISRA : Website: www.nisra.gov.uk"** | **populations: OGL**, NISRA's line beside the OSNI line. **Boundaries: STOP. NISRA's own page exempts OSNI-derived mapping from OGL, which conflicts with LPS's answer** |

**Recorded beside, not a finding:** search results quoted a second LPS acknowledgement form, "Contains LPS
Intellectual Property © Crown copyright and database right (year) This information is licensed under the terms of the
Open Government Licence". I couldn't trace it to a readable LPS page: the LPS Q&A URL found answered 404. It is not
the line LPS's IPR team gave in writing.

**As ruled, until the reviewer rules:** the layers built from the three stopped datasets stay unserved.
- **Roads:** OSNI 50K Transport.
- **Places:** OSNI Gazetteer, and NISRA boundaries for the ranking.
- **Water (DAERA lakes)** is consistent. NISRA's populations are OGL, but they are only used through the boundary join.
- **The docs PR (A.3) waits** for that ruling, so it records one settled answer, not a partial one.

### A3.22 B, the OPERA coverage survey: predictions, frozen before any code (2026-11-05 21:38Z)

**Frames.**
- The source the ingest reads is `https://s3.waw3-1.cloudferro.com/openradar-24h`, and it keeps 24 hours. So
  2026-11-04's frames are already gone.
- **The latest full UTC day the source will hold is 2026-11-05, 00:00-23:00Z hourly.** Every one of those 24 frames
  is present together only between the 23:00 frame's arrival (about 23:05Z) and the 00:00 frame's expiry (about
  00:00Z on 11-06).
- **Plan:**
  - list, then download 00:00-21:00Z now, oldest first, one request at a time;
  - 22:00 and 23:00Z after they land;
  - nothing else.
- **Predicted:** **24 readable**. Fewer than 20 means stop and report. At most 24 frames plus their listings, under
  1 GB, in my scratch only. Free disk was 43 GB at 21:36Z.
- **No deploy, no Worker, bucket or KV write anywhere, and the ingest untouched.** The frames are deleted
  afterwards.

**The reader and georeferencing:**
- `parseHead`, with K1's corner rule, from `proxy/src/wx/opera.ts`, and `laea` from `proxy/src/wx/laea.ts`, imported
  unchanged and run under `tsx`.
- The tiles are decoded with `readWindow`'s own loop over the whole raster. **No change to `opera.ts`.**
- **Predicted:** every frame parses, with corner **`cirrus-slip`** (as K1 read). Width, height, pixel size and LAEA
  parameters are identical in all 24.

**Classification, as opuk's `operaCode`:**
- `NaN` (undetect) or any finite value other than `NODATA` (-9999000) is **covered**.
- `NODATA` is **not covered**.

**Countries:**
- **Natural Earth 1:10m admin-0 countries, v5.1.2** (the GitHub release's GeoJSON, `ne_10m_admin_0_countries`). The
  version is read from the release tag and recorded.
- Each polygon is projected vertex by vertex into the frame's LAEA and rasterised onto the composite grid by the
  **pixel-centre rule** (even-odd scanline).
- **in-domain land pixels:** country pixels inside the raster.
- **whole land:** the polygon's area in LAEA, which is equal-area, divided by one pixel's area.
- **in-domain %** = in-domain land / whole land. **covered %** = covered / in-domain land, per frame, as a median.
- **Ranked:** in-domain >= 50% and >= 100 pixels. The rest are listed apart with their numbers.
- Admin-0 "countries" include overseas parts: France includes French Guiana, so its in-domain % is below 100. That
  is said as read, not corrected.

**The controls** (each with a sabotage that must turn it red; a sabotage that doesn't is BLIND, which is FAIL):

| | the check | predicted | sabotage | predicted under it |
|---|---|---|---|---|
| **C1** same reading | frame **2026-11-05 20:00Z**. The survey's classification is sampled at each `OPUK_GRID` cell centre (the containing composite pixel), as the share of cells covered. The reference is that frame's `coveredShare` as opuk's code computed it: the `wx_ingest` line logged when Plan S's S4 stored it (`blipscope-wx-ingest-dev`, code at `788dab3cf200`, the same `buildOperaFrame` as production). That is the number `wx-opuk-verdict.sh` reads from `/healthz` when the frame is `latest` | **within +/-0.005** (absolute) of the reference. **Not exact:** opuk's `resample` gives a cell holding several pixel centres echo over nodata over undetect, while the survey takes the centre pixel. They differ only on coverage edges | **C1s:** undetect counted as uncovered | **red, far outside +/-0.005**. In November most covered cells are undetect. The undetect count in the box is printed |
| **C2** geometry | ranked countries with in-domain 100%: pixels x pixel area against the polygon's LAEA area | **within +/-3%** for every one | **C2s:** rasterise with the pixel size x1.1 | **red for every one** (about -17%) |
| **C3** placement | Shetland: UK pixels whose centres lie at lat 59.8-61.0, lon -2.2 to -0.6 | **covered < 5% in each of the 24 frames** (R1: no radar coverage) | **C3s:** the raster flipped north-south | **red** (> 5%): the box's rows then read the domain's far south. If it isn't red, another sabotage that is red is picked (pre-ruled), and the miss kept |

- **Contributing radars by country prefix:** reported only if the files carry a node list. Whether they do is read
  from the GeoTIFF's own tags, not assumed.
- **Coverage only:** clutter, quality and latency are not graded.
- **Escape:** anything else means stop and report. The report goes in my reply, with no PR until it's read: the
  ranked table, the controls and sabotages (predicted and observed), the script's sha256 and its command line.

### A3.23 B's reading: OPERA's coverage by country (graded run 2026-11-05 23:09:00Z, 2026-11-05's 24 hourly frames)

**Inputs:**
- **Frames:** 24 of 24 readable, 2026-11-05 00:00-23:00Z hourly, from `openradar-24h`. Fetched one request at a time
  (00:00-21:00Z at 21:39-21:40Z, 22:00Z at 22:08Z, 23:00Z at 23:08Z), every one HTTP 200, 46,959,585 B in total.
  The sha256 of each is in `frames/manifest.json` in my scratch.
- **Raster:** 3800 x 4400 at 1,000 m, LAEA (lat0 55, lon0 10). The corner is **`cirrus-slip`** in all 24, as predicted.
- **Natural Earth 1:10m admin-0 countries v5.1.2** (GitHub release tag), 258 countries; file sha256
  `239eec57ac17f100a11e2536cffc56752c318b50ae765b0918ff7aab4ce8f255`.
- **The reader:** `parseHead` and `laea` from `proxy/src/wx/`, identical to main. The script is
  `b_survey.mts`, sha256 `5a9590f0fac754020e1d7df563dc5dad3908293b1992bb52a56c21f197eef5c5`.
- **The command:** `node ../wt-plan-s/proxy/node_modules/tsx/dist/cli.mjs b_survey.mts frames
  ne/ne_10m_admin_0_countries.geojson $REF 20261105T2000`, where `$REF` is the 20:00Z frame's logged `coveredShare` in full (0.750772231862 to 12 places; its full float sits in the S4 log line, uncopied here because a 16-digit run trips the device-id hook). Output sha256
  `0b0d43ac4363093b840655aaa763a57b78946b856f13bc1c60156b6374c1a6bd`.
- **A development pass** over the first 22 frames ran first. It is not the reading, and the graded run's controls
  match it.

**The controls, all as predicted:**

| | predicted | observed |
|---|---|---|
| **C1** (frame 20:00Z) | within +/-0.005 of the reference 0.7507722 | **0.7508004 (+0.000028)** |
| **C1s** (undetect counted as uncovered) | red, far outside | **red: 0.0562 (-0.6945)**; undetect cells in the box: **1,531,016 of 2,204,325** |
| **C2** (39 ranked countries wholly in-domain) | each within +/-3% | **all 39 within; the largest are Malta +1.94% and Jersey -1.72%** |
| **C2s** (pixel size x1.1) | red for every one | **red for all 39: -15.5% to -18.6%** |
| **C3** (Shetland, 1,365 pixels) | < 5% covered in each frame | **0% in all 24** |
| **C3s** (north-south flip) | red | **red: 100% in every frame** |

**The ranking** (in-domain >= 50% and >= 100 pixels; covered = data or undetect, which is the radar footprint).
Radars come from each frame's own node list (156-162 nodes a frame), by OPERA prefix, as median (min-max):

| country | in-domain % | covered % (median) | lowest frame | frames | its own radars |
|---|---|---|---|---|---|
| Aland, Andorra, Austria, Belgium, Bosnia and Herzegovina, Croatia, Czechia, Denmark, Estonia, Finland, Germany, Hungary, Ireland, Isle of Man, Jersey, Liechtenstein, Lithuania, Luxembourg, Malta, Moldova, Montenegro, Netherlands, Poland, Romania, Slovakia, Slovenia, Switzerland | 98.3-101.9 | **100.00** | 100.00 | 24 | be 3, hr 5, cz 2, dk 5 (4-5), ee 2, fi 12, de 17 (15-17), hu 5 (4-5), ie 2, lt 2, mt 1, nl 2, pl 8 (7-10), ro 7 (6-7), sk 4, si 2, ch 5; **none in the node list** for at, ba, ax, ad, im, je, li, lu, md, me |
| France | 86.0 (its overseas parts are outside) | **100.00** | 100.00 | 24 | fr 24 (23-24) |
| Portugal | 96.3 | **100.00** | 100.00 | 24 | pt 3 |
| Sweden | 100.0 | **100.00** | 99.39 (00:00Z) | 24 | se 11 |
| Iceland | 100.0 | **99.99** | 99.99 | 24 | is 3 |
| **United Kingdom** | 100.0 | **99.44** | 99.44 | 24 | uk 15 (14-15) |
| Norway | 83.7 | **99.29** | 99.29 | 24 | no 11 |
| Latvia | 100.0 | **97.90** | 97.90 | 24 | none listed |
| Spain | 98.5 | **92.58** | 92.58 | 24 | es 9 |
| Serbia | 100.0 | **90.99** | 90.99 | 24 | none listed |
| Italy | 100.0 | **70.57** | 70.57 | 24 | **none listed** |
| Bulgaria | 100.0 | **67.88** | 65.15 (13:00Z) | 24 | none listed |
| Belarus | 100.0 | **45.05** | 45.05 | 24 | none listed |
| Albania | 99.9 | **27.53** | 27.53 | 24 | none listed |
| Ukraine | 81.5 | **25.75** | 25.75 | 24 | none listed |
| Kosovo | 100.0 | **17.25** | 17.25 | 24 | none listed |
| Faroe Islands | 100.6 | **0.00** | 0.00 | 24 | none listed |
| Greece | 100.0 | **0.00** | 0.00 | 24 | none listed |
| North Macedonia | 100.0 | **0.00** | 0.00 | 24 | none listed |

**Listed apart** (under 50% in-domain, or under 100 pixels), as in-domain % / pixels / covered % (median):
- Morocco 24.1 / 142,833 / 21.53
- Russia 9.1 / 1,548,111 / 16.61
- Turkey 27.3 / 212,896 / 0
- Tunisia 46.7 / 73,065 / 0
- Algeria 11.6 / 268,411 / 0
- Greenland 5.5 / 119,529 / 0
- San Marino 102.8 / 62 / 100
- Monaco 100.9 / 19 / 100
- Gibraltar 108.3 / 4 / 100
- Guernsey 100 / 74 / 100

**Read beside the table, not graded:**
- **Coverage hardly moves within the day.** For every country except Sweden (00:00Z) and Bulgaria (13:00Z), the
  lowest frame equals the median. Covered includes undetect, so this is the radar network's footprint, not the
  weather.
- **Some countries are covered with none of their own radars in the node list:** Austria, Luxembourg, Moldova and
  Latvia at 98-100%, and Italy at 70.6%. That fits neighbours' radars reaching across the border.
  - The alternative, a country prefix the survey doesn't recognise, is excluded only where the node list shows no
    other unrecognised prefix. All 24 prefixes found are ISO-2 codes or `uk`, and none of them is at, it, lu, md or
    lv.
- **Above 100% in-domain** (Malta 101.9, Gibraltar 108.3) is pixel-centre quantisation on tiny polygons, the same
  effect C2 bounds at +/-3% for the ranked ones.
- **The frames stay in my scratch until the reviewer has read this**, then they are deleted as allowed.
