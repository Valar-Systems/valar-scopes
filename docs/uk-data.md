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
