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
