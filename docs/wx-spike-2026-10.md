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

### Wire format v1: the cell codes (ruled 2026-10-20, after A0)

| code | meaning | MergedReflectivityQCComposite source values |
|---|---|---|
| **0** | no echo, or under 15 dBZ, **inside radar coverage** | −99 (no echo detected), and every value below 15 dBZ including real negatives (−21 to −0.5) |
| 1-13 | 15-75 dBZ in 5 dBZ steps (1 = 15-19.9 … 12 = 70-74.9, 13 = 75 and up) | the decoded dBZ |
| **14** | **no radar coverage** | −999 |
| **15** | **missing data** | never sent by this product; kept for products that have a genuine missing value |

No coverage (14) and missing (15) are never drawn as dry.

### Addendum to the predictions, after the A0 ruling, before any of A1-A6 is measured (2026-10-20)

**A1, classes compared:** no echo or below 15 (0) / echo (1-13) / no coverage (14), plus missing (15) **only where a product actually uses it**.
- **MergedReflectivityQCComposite:** 3 classes, predicted **100.000 %** agreement on all three frames. Other predictions as above.
- **SeamlessHSR**, for one scene: predicted to use the **same two sentinels**, −999 for no coverage and −99 for no echo, and **no separate missing value**. Whatever it uses is reported as found. If it has a third sentinel, that sentinel maps to 15 and is compared as its own class.

**A5, one more case:** a quiet area inside coverage, the Iowa ±1° box (checked ≥ 150 km from every repo coordinate), on a clear-night frame:
- **predicted:** **100 % code 0**, never 14 or 15;
- **sabotage:** map −99 to 15. Predicted: that box reads **100 % code 15**, and the check fails.

### Part B: device (after v18 merges)

Predictions for B1-B7 are committed in this file **before** Part B starts, as their own commit.

## Results

(Filled in as each check runs, against the predictions above. Never edited after the fact.)

### A0: what the files are (2026-10-20, frame 02:12:36Z), read with eccodes 2.50.0

| | predicted | found | |
|---|---|---|---|
| layout | `CONUS/MergedReflectivityQCComposite_00.50/<YYYYMMDD>/MRMS_..._<YYYYMMDD-HHMMSS>.grib2.gz` | exactly that. ALASKA/, CARIB/, GUAM/, HAWAII/ beside CONUS/; also `CONUS/SeamlessHSR_00.00/` | ✓ |
| cadence | 2 min | median 120 s (113-127) over 67 files | ✓ |
| file size | 0.3-2 MB gz | 0.70-0.78 MB gz (quiet night); 0.74 MB gunzipped | ✓ |
| grid | 7000 × 3500, 0.01°, first point ~54.995 N 129.995 W, W→E then N→S | 7000 × 3500, 0.01°; first 54.995 N 230.005 E (= 129.995 W); last 20.005 N 299.995 E; iScansNegatively 0, jScansPositively 0 | ✓ |
| packing | template 5.41 (PNG) | 5.41 `grid_png`, 16 bits, reference −9990, decimal scale 1 (tenths of dBZ), no bitmap | ✓ |
| **−99 is "missing"** | **missing** | **"no echo detected, inside radar coverage"**. It is 63 % of the grid on a quiet night, and 100 % of ±1° boxes over Iowa and central Texas | **✗ (meaning)** |
| −999 is "no coverage" | no coverage | no coverage: 100 % of ±1° boxes over the deep Gulf, the in-grid Atlantic and interior Mexico | ✓ |
| other negatives | not predicted | −21 to −0.5 dBZ, 22,973 cells: real weak returns, below 15 | (new) |
| landing delay | (A6 assumed 1.5-2.5 min) | file last-modified ≈ **50 s** after frame time | (A6 input) |

Value census for this frame: −999 8,411,028; −99 15,507,309; other negatives 22,973; 0-15 dBZ 346,493; ≥ 15 dBZ 212,197; max 64.0 dBZ.

**STOP, report: −99 cannot be decided-format code 15 ("missing").** Mapping it to 15 would make two thirds of CONUS "missing" (never drawn as dry) on every quiet night. The data says −99 is dry-inside-coverage, which is code **0**. A genuine "missing" (code 15) does not occur in this frame at all. Measurements A1-A6 wait for a ruling on the mapping.

### A1: TypeScript decoder vs eccodes (2026-10-20)

Decoder `tools/wx/mrms.ts` (Web APIs only; the PNG is inflated and unfiltered row by row); Node wrapper `tools/wx/decode-cli.ts`; comparison `tools/wx/compare_a1.py`, which applies the ruled mapping independently in Python and reads eccodes' own values and lat/lon arrays.

| frame | classes used | class agreement | echo cells, max \|Δ\| | lat / lon max error | bars |
|---|---|---|---|---|---|
| quiet, 2026-10-20 02:12:36Z | 0, 1-10, 14 | **100.0000 %** (0 of 24.5 M differ) | 212,197 cells, **0.0000 dBZ** | 4.6e-12 / **2.0e-6 °** | MET |
| storms, 2024-05-21 22:00:42Z | 0, 1-11, 14 | **100.0000 %** | 1,207,851 cells, **0.0000 dBZ** | 4.6e-12 / **2.0e-6 °** | MET |
| widespread rain, 2024-09-27 13:00:38Z | 0, 1-9, 14 | **100.0000 %** | 985,994 cells, **0.0000 dBZ** | 4.6e-12 / **2.0e-6 °** | MET |
| SeamlessHSR, 2024-05-21 22:02:00Z | 0, 1-11, 14 | **100.0000 %** | 991,140 cells, **0.0000 dBZ** | 4.6e-12 / **2.0e-6 °** | MET |
| **sabotage**: storm frame, rows shifted by one | | **97.959 %** (500,055 differ; prediction < 99.9 %) | **67.7 %** beyond 0.5 dBZ (prediction > 1 %) | | **NOT MET**, exit 1, as predicted |

- **SeamlessHSR's sentinels:** −99 (14,793,202 cells) and −999 (8,175,712), **exactly the composite's two**, with no third "missing" value, **as predicted**. Same grid and packing (5.41, 16-bit, reference −9990, decimal scale 1). Code 15 is not used by either product.
- **Decode time:** ~310 ms per CONUS frame in Node, an input to A4.
- **OFF-PREDICTION 1, longitude precision: 2.0e-6 °, against a prediction of < 1e-6 °.** It is inside the 0.001 ° bar by 500×.
  - **Mechanism, verified:** eccodes computes each longitude by interpolating between the stored first and last points. That matches eccodes' own array to 4.1e-11 °. The stored last point is 299.994998, 2 µdeg short of 6,999 steps of 0.01 °. The decoder steps by exactly Di = 0.01 °, so the two differ by up to 2 µdeg at the east edge. Both are valid readings of a micro-degree header.
  - **The decoder is unchanged.** My prediction was too tight.
- **OFF-PREDICTION 2, A0's file size:** the storm frame is **2.12 MB** gzipped (the rain frame 1.64 MB), **above** the predicted 0.3-2 MB range.

**STOP after A1, per the doctrine.** Both off-predictions are reported above; neither touches a bar or the design. A2-A6 wait for the reviewer.
