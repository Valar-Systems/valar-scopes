# Weather on the Blipscope: Stage 1 spike (October 2026)

**Report-only.** No merge, no deploy. Branch `spike/wx-overlay`, draft PR. Nothing here reaches customers.

**Data:** NOAA MRMS from the public AWS bucket `noaa-mrms-pds` (us-east-1, anonymous HTTPS) and nothing else. Downloaded MRMS files stay out of git (`tools/wx/` is code only; data goes under a scratch folder outside the repo).

**Locations:** public airports only. Every test location is checked by computation to be at least 150 km from every coordinate in our docs and tests, and those coordinates are never printed.

## Predictions, committed before any measurement

Every check gets a numeric prediction here **before** it is measured, and a sabotage that must turn it red. An unreadable result is a FAIL. A result off its prediction: stop and report, no tuning and retrying.

### Part A: the server pipeline, on the workstation

**A0: what the files are.** The reviewer's expectations, adopted as my predictions:
- **Layout:** `CONUS/MergedReflectivityQCComposite_00.50/<YYYYMMDD>/MRMS_MergedReflectivityQCComposite_00.50_<YYYYMMDD-HHMMSS>.grib2.gz`, one file every 2 min. Other domains (ALASKA, HAWAII, CARIB, GUAM) sit beside CONUS with the same naming.
- **Grid:** 7000 × 3500 at 0.01°. The north-west corner (first point) is near 54.995 N, 129.995 W (cell centres), scanning west to east and north to south.
- **Missing value** −99, **no coverage** −999.
- **Packing:** GRIB2 template 5.41 (PNG). My second guess is 5.0 (simple).
- **File size:** 0.3-2 MB gzipped, larger in stormy weather.

Any of these wrong is reported as found; the design adjusts, not the record.

**A1: TypeScript decoder vs an independent decoder** (eccodes, Python), on three frames: storms, quiet, widespread rain.

| | predicted |
|---|---|
| cells in the same class (below 15 / echo / no coverage / missing) | **100.000 %** on all three frames |
| echo values: max \|Δ\| | **≤ 0.05 dBZ** (both decode the same packed integers; 0.5 is the bar) |
| lat/lon of 1,000 sampled cells: max error | **< 1e-6 °** (0.001 is the bar) |
| **sabotage: rows shifted by one** | class agreement **< 99.9 %** on the storm frame, and echo cells beyond 0.5 dBZ **> 1 %** |

**A2: crop size**, 50 public airports × radius 40 / 80 / 160 km, every frame for 10 h including a stormy spell. Bytes per crop on the wire (header plus RLE: one byte per run, 4-bit code and 4-bit run length):

| radius | p50 | p95 | max |
|---|---|---|---|
| 40 km | ~0.15 KB | ~1.5 KB | ~4 KB |
| 80 km | ~0.4 KB | ~4 KB | ~12 KB |
| 160 km | ~1 KB | ~10 KB | ~28 KB |

- **Bar:** nothing over 32 KB leaves the server. A crop that would exceed it is coarsened (2×2, then 4×4 cells) and flagged in its header.
- **Prediction:** **0** crops exceed 32 KB after the cap. **≤ 1 %** of 160 km crops need coarsening, and **0** at 40 km.
- **Sabotage:** a synthetic speckle frame (alternating classes every cell) with the cap removed: a 160 km crop reaches **> 32 KB** (predicted **~80-120 KB**), and the check catches it.

**A3: crop placement.** Crops centred 40 km N, E, S and W of a storm frame's strongest echo:
- **Prediction:** at that lat/lon, the crop's class equals the class of the reference decoder's value, **4 of 4**. Across every cell of each crop, the crop's class equals the reference class **100 %**.
- **Sabotage:** the crop origin moved by one cell. The single strongest cell may survive a 1-cell shift when its neighbour shares a 5 dBZ class, so the **whole-crop comparison** is the one with teeth: predicted **≥ 5 %** of echo cells mismatching in each of the 4 crops.

**A4: ingest cost** of one CONUS frame (download, gunzip, decode, quantise to 4-bit, store), with a streaming design: the PNG is inflated row by row and quantised straight into a 7000 × 3500 4-bit grid (~12.3 MB).

| | Node | local workerd |
|---|---|---|
| wall time | ~3 s (incl. download) | ~3 s |
| CPU time | ~1.5 s | ~1.5 s |
| peak memory | ~150 MB RSS (Node's own baseline is ~50 MB of it) | **~40 MB** isolate heap |

- **Bars for Cloudflare:** peak ≤ 96 MB, CPU ≤ 10 s. Predicted: **both met with room** (40 MB, 1.5 s).
- **Meter sabotage:** hold an extra 64 MB during the run; the measured peak must rise **≥ 60 MB** (predicted +64 ± 4 MB).
- Local numbers stand in for production, and the report says so.

**A5: coverage.**

| point | predicted |
|---|---|
| mid-Pacific (outside the grid) | **no coverage** (14) |
| London (outside the grid) | **no coverage** (14) |
| an Atlantic patch inside the grid, far from any radar | **no coverage** (14), from −999 |
| Vancouver, Toronto, Montreal | **covered** (dry or echo, never 14). All three are within ~150 km of a US radar |

**Sabotage:** treat −999 as dry. The inside-the-grid patch then reads **dry (0)**, and the check goes red.

**A6: freshness.** A 2-hour poll; lag from frame time to crop ready:
- p50 **~2.5 min**, p95 **~3.5 min** (bar ≤ 4 min). MRMS files land about 1.5-2.5 min after frame time; the poll runs every 30 s.
- **Sabotage:** skip every other frame. p95 rises **by ~2 min** and breaks the bar.

**A7: where it runs.** A cost and upkeep comparison, not a measurement; the A4 numbers feed it. Predicted recommendation: **(a) a cron Worker writing one tiled frame to R2**, if A4's workerd peak and CPU fit. (b) is a Durable Object holding ~12 MB per frame in memory, and (c) adds a server to look after.

### Part B: device (after v18 merges)

Predictions for B1-B7 are committed in this file **before** Part B starts, as their own commit.

## Results

(Filled in as each check runs, against the predictions above. Never edited after the fact.)
