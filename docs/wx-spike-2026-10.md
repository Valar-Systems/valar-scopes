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

**Ruled 2026-10-20: both accepted.**
- **Longitude:** the decoder stays as is (it steps by Di). **The error bound is set by the GRIB header's micro-degree rounding** of the first and last grid points, not by the decoder: any reader that interpolates first to last instead of stepping by Di will differ by up to 2 µdeg at the east edge.
- **File size:** the measured sizes are **the new upstream baseline for A7's bandwidth math**: quiet ~0.70-0.78 MB, widespread rain **1.64 MB**, storm **2.12 MB** per CONUS frame, gzipped.

### A2: crop size (2026-10-20). Bars met; the size distribution is OFF-PREDICTION

300 frames, 2024-05-21 15:00:38Z to 2024-05-22 00:58:41Z (the outbreak afternoon and evening: 0.69-1.24 M echo cells per frame, 1.52-2.18 MB gz), × 50 public airports × 3 radii = 45,000 crops (`tools/wx/a2-run.ts`; frames streamed and never stored). **Airports (ICAO)**, each checked ≥ 150 km from every repo coordinate by computation; 11 of 66 candidates were dropped and 5 more for an even spread: KSFO KLAS KPHX KSLC KDEN KABQ KELP KBIL KRAP KFAR KMSP KDSM KOMA KMCI KOKC KDFW KSAT KMAF KAMA KLBB KMSY KMEM KSTL KORD KDTW KCLE KIND KBNA KATL KBHM KJAX KMCO KTPA KCLT KRDU KDCA KPHL KJFK KBOS KBTV KBUF KPIT KCHS KLIT KGRB KBIS KCYS KRNO KFAT KGJT.

| radius | p50 (pred.) | p95 (pred.) | max (pred.) | over 32 KB after cap | coarsened |
|---|---|---|---|---|---|
| 40 km | **0.44 KB** (~0.15) | 1.52 KB (~1.5) | 2.79 KB (~4) | **0** | 0 % |
| 80 km | **1.68 KB** (~0.4) | **5.03 KB** (~4) | 8.93 KB (~12) | **0** | 0 % |
| 160 km | **6.71 KB** (~1) | **16.25 KB** (~10) | 30.45 KB (~28) | **0** | **0 %** (pred. ≤ 1 %) |

- **Bars: met.** Nothing over 32 KB left the encoder, and no crop needed coarsening at any radius.
- **Sabotage (synthetic speckle frame, cap removed):** the 160 km crop reached **118.4 KB**, inside the predicted 80-120 KB. **All 50** exceeded 32 KB and were caught. With the cap on, all were coarsened 2×2 and flagged, at 1.9 KB.
- **OFF-PREDICTION: the distribution (p50 ×3 to ×7, p95 +25-60 %).** The p50s are almost exactly the size of a perfectly uniform crop. **My RLE gives each run a 4-bit length (max 16 cells)**, so even an all-dry crop costs one byte per 16 cells: ~110 k cells at 160 km is ≥ 6.9 KB, and ~7 k at 40 km is ≥ 0.43 KB. The predictions assumed long runs.
  - That is **an encoding choice of mine, not a property of MRMS**. The ruled 4-bit codes are unaffected.
  - A long-run form (an escape plus a variable-length count) would take quiet crops to tens of bytes. **That is a wire-format change and waits for a ruling.**

### A3: crop placement (2026-10-20). As predicted

Storm frame 2024-05-21 22:00:42Z; strongest echo **65.5 dBZ** (code 11). Crops 80 km in radius, centred 40 km N, E, S and W of it; the echo and all four centres were checked ≥ 150 km from repo coordinates. The check (`tools/wx/a3_check.py`) decodes each crop **independently in Python** and places every cell on the grid from the crop's header alone.

| crop | strongest-echo cell, crop / reference | whole crop vs reference | sabotage (origin + 1 cell): echo cells mismatching |
|---|---|---|---|
| N | 11 / 11 ✓ | **100.0000 %** (145 × 165) | **39.00 %** (strongest cell → 9) |
| E | 11 / 11 ✓ | **100.0000 %** | **35.59 %** (→ 9) |
| S | 11 / 11 ✓ | **100.0000 %** | **39.88 %** (→ 9) |
| W | 11 / 11 ✓ | **100.0000 %** | **47.72 %** (→ 9) |

4/4 and 100 % as predicted. The sabotage is ≥ 5 % in all four, as predicted. Here even the single strongest cell failed under the shift; the prediction only said it *might* survive.

### A5: coverage (2026-10-20). As predicted

Quiet frame 2026-10-20 02:12:36Z, through the real crop encoder (40 km; the Iowa box 100 km). All points were checked ≥ 150 km from repo coordinates.

| point | ruled mapping | sabotage: −999 as dry | sabotage: −99 as 15 |
|---|---|---|---|
| mid-Pacific (outside grid) | 100 % code 14 ✓ | 14 | 14 |
| London LHR (outside grid) | 100 % code 14 ✓ | 14 | 14 |
| Atlantic patch (inside grid) | 100 % code 14 ✓ | **100 % code 0: red** ✓ | 14 |
| Vancouver YVR / Toronto YYZ / Montreal YUL | centre 0, 100 % code 0: covered ✓ | 0 | 15 |
| **Iowa quiet box (ruled case)** | **100 % code 0** ✓ | 0 | **100 % code 15: red** ✓ |

### A4: ingest cost, Node part (2026-10-20). OFF-PREDICTION, including the meter sabotage

Download, gunzip, decode, quantise to 4-bit and pack two cells per byte (12.25 MB), then write (`tools/wx/ingest.ts`, `tools/wx/a4-node.ts`). One run per fresh process, 3 runs per case, every run reported:

| case | wall | CPU | peak RSS (`maxRSS`) |
|---|---|---|---|
| **storm** (2.12 MB gz; the bars apply) | 1082 / 1031 / 1066 ms | 453 / 531 / 453 ms | 144.8 / 135.9 / 135.1 MB |
| quiet (0.70 MB gz) | 906 / 993 / 910 ms | 438 / 468 / 469 ms | 118.5 / 119.7 / 119.0 MB |
| **meter sabotage:** storm + 64 MB held, every page touched | 1157 / 1099 / 1179 ms | 453 / 532 / 500 ms | **182.0 / 182.7 / 182.0 MB** |

- **Predicted (Node):** wall ~3 s, CPU ~1.5 s, peak ~150 MB on a ~50 MB baseline.
- **Measured:** wall ~1.05 s, CPU ~0.5 s, peak 135-145 MB on an **80 MB** baseline. All off-prediction, in the cheaper direction.
- **THE METER SABOTAGE FAILED: +37 to +47 MB against the required ≥ +60 MB.** `maxRSS` (Windows' peak working set) is not additive here, so **it cannot be trusted to measure the ingest's own memory**, and A4's 96 MB bar cannot be graded with it.
  - Mechanism not yet tested: possibly V8 collecting garbage earlier under memory pressure, or Windows trimming the working set.
  - **The local-workerd measurement was not run:** it would lean on the same kind of meter.

**STOP, report: A2's distribution and A4 are off-prediction.** A3 and A5 are as predicted. A6 (the 2-hour live poll) is still running and will be recorded when it ends. A4-workerd and A7 wait for a ruling.

## Ruling 2026-10-20 (after A2/A4): the long-run form, and A2's new predictions, frozen BEFORE the encoder changes

**Wire format v1, run encoding (amended; nothing has shipped, so it stays v1).** The body is a sequence of runs, row-major from the NW cell; **runs may cross row ends**.
- Each run starts with one byte: **code in the high nibble**, **length 1-15 in the low nibble**.
- **A low nibble of 0 means a long run**: the length follows as an **unsigned LEB128 varint**. The varint carries the whole run length.
- The 4-bit codes are unchanged (0 / 1-13 / 14 / 15, as in the table above).
- Part B's device decoder implements exactly this.

**A2 predictions under the long-run form.** Same 300 frames, same 50 airports, same radii:

| radius | p50 | p95 | max |
|---|---|---|---|
| 40 km | **≤ 0.15 KB** (from 0.44) | **~1.2 KB** (from 1.52) | **~2.6 KB** (from 2.79) |
| 80 km | **~0.3 KB** (from 1.68) | **~4 KB** (from 5.03) | **~8.5 KB** (from 8.93) |
| 160 km | **~1.5 KB** (from 6.71) | **~13 KB** (from 16.25) | **~29 KB** (from 30.45) |

- **Why the p50 falls far and the p95/max barely:** dry areas collapse into long runs (a uniform 160 km crop is ~36 B: the header plus one long run), but rain fields are noisy at 5 dBZ classes and are mostly runs of 1-15 that already cost one byte.
- **A uniform crop** (all one code) is **≤ 40 B** at every radius.
- **Still 0 crops over 32 KB, and 0 coarsened.**

**Round-trip check (new):** `decode(encode(crop))` equals the crop's source cells **bit for bit on all 45,000 crops**. Predicted **45,000 / 45,000**.
- **Sabotage:** an off-by-one in the long-run length (the encoder writes length − 1). The check must fail.
- Predicted: **≥ 95 %** of crops fail, i.e. every crop with at least one long run, which on this day is nearly all of them.
- If the round trip passes, **A3 and A5 stand without a re-run**: the cell values do not change.

### A4 re-meter (ruled 2026-10-20): predictions, frozen before the new meter runs

**The meter:** V8's own accounting, `process.memoryUsage().heapUsed + external`. `arrayBuffers` is already inside `external`, so it is not added twice. It is sampled **at every pipeline stage boundary** (start, after the download, after the gunzip, after the GRIB parse, at each PNG row block, after the pack, after the store) **and every 5 ms** by a timer. The peak is the largest sample.

| case | predicted peak (`heapUsed + external`) | predicted CPU |
|---|---|---|
| **storm** (2.12 MB gz; the bars apply) | **~25 MB** (range 18-35): download 2.1 + unzipped 0.75 + the PNG's data copies 0.75 + packed grid 12.25 + rows + V8's own heap | **~0.5 s** |
| quiet (0.70 MB gz) | ~23 MB | ~0.5 s |
| **meter sabotage:** storm + 64 MB held | **peak rises by 60-68 MB** (ruled band), i.e. ~89 MB | ~0.5 s |

- **Bars (unchanged, graded on the storm frame):** peak ≤ 96 MB, CPU ≤ 10 s. Predicted: **both met with large margins.**
- **Local workerd:** the pipeline is run once to prove it runs there, and **no memory claim is made from that run**. Production memory is confirmed in Stage 2.

### A4 re-meter, results (2026-10-20). OFF-PREDICTION; the meter sabotage is OUTSIDE the ruled band

`tools/wx/a4-meter.ts`: `heapUsed + external`, sampled at every stage boundary (stage hooks in `ingest.ts` / `mrms.ts`, including every 256 PNG rows) and every 5 ms. One run per fresh process, 3 per case, every run reported. Absolute values include Node's own **19.8 MB** baseline.

| case | peak (MB) | CPU (ms) | wall (ms) | per-stage maxima, run 1 (MB) |
|---|---|---|---|---|
| **storm** (bars) | **64.0 / 64.7 / 65.9** | 469 / 516 / 531 | 1145 / 1049 / 1040 | start 19.8 · download 30.8 · gunzip 32.7 · parse 32.7 · rows 60.3 · packed 62.5 · stored 62.5 |
| quiet | **52.8 / 51.1 / 51.0** | 469 / 578 / 422 | 921 / 971 / 952 | start 19.8 · download 25.9 · rows 51.8 · packed 52.0 |
| **sabotage:** storm + 64 MB held | **114.3 / 114.5 / 114.2** | 594 / 672 / 531 | 1084 / 1131 / 1037 | start 19.8 · **download 94.3 (+63.5)** · rows 113.8 (+53.5) · packed 110.4 |

- **Bars (storm):** peak **65.9 MB ≤ 96 MB** and CPU **0.53 s ≤ 10 s**. These would pass, but a bar graded by a meter that failed its own sabotage is not a grade.
- **OFF-PREDICTION, the peak:** ~65 MB against a predicted ~25 MB (18-35). My estimate counted the held buffers but not the transient ones: duplicated response bodies, decompression-stream chunks, and copies of the PNG's data blocks. The absolute figure also carries Node's 19.8 MB baseline.
- **OFF-PREDICTION, the meter sabotage: +49.5 MB** (mean of the peaks, 114.3 against 64.9), **outside the ruled 60-68 MB.**
  - The held block **does** register whole when it arrives: +63.5 MB at the "download" stage.
  - **Mechanism (inferred from the stage samples, not separately tested):** `heapUsed + external` counts garbage that has not yet been collected, and V8 collects sooner when the extra 64 MB raises memory pressure. The peak therefore depends on the garbage collector's timing and is not additive.
- **Proposed fix (needs a ruling):** force a collection before each stage-boundary sample (`node --expose-gc`). That meters live memory, which is additive by construction; then repeat the 64 MB sabotage.
- **Not yet run:** the local-workerd "it runs there" check.

### A2 re-run with the long-run form, and the round trip (2026-10-20). Round trip as predicted; p50 OFF-PREDICTION (smaller)

Same 300 frames (2024-05-21 15:00:38Z to 2024-05-22 00:58:41Z), 50 airports, 3 radii = 45,000 crops.

| radius | p50 (pred.) | p95 (pred.) | max (pred.) | over 32 KB | coarsened | smallest |
|---|---|---|---|---|---|---|
| 40 km | **0.034 KB** (≤ 0.15) ✓ | 1.44 KB (~1.2) | 2.78 KB (~2.6) | **0** | **0** | 35 B |
| 80 km | **0.035 KB** (~0.3) | 4.41 KB (~4) | 8.95 KB (~8.5) | **0** | **0** | 36 B |
| 160 km | **0.144 KB** (~1.5) | 12.78 KB (~13) ✓ | 29.94 KB (~29) ✓ | **0** | **0** | 36 B |

- **Round trip:** `decode(encode(crop))` equals the encoded cells **bit for bit on 45,000 / 45,000** crops, **as predicted**. Per the ruling, **A3 and A5 stand without a re-run**.
- **Round-trip sabotage** (the encoder writes long-run lengths one short): **45,000 / 45,000 fail (100.00 %)**, within the predicted ≥ 95 %.
- **Uniform crops:** 35-36 B, as predicted (≤ 40 B).
- **OFF-PREDICTION: the 80 km and 160 km p50s are ~10× smaller than predicted.** The prediction assumed rain inside the median airport's crop. On this storm day, more than half of the 50 airports had near-uniform crops at any moment, so their crops collapse to a few long runs.
- p95 and max come from the rainy crops and land within +5 % to +20 % of prediction. At 40 km the p95 (1.44 against ~1.2) and max (2.78 against ~2.6) are above; the 160 km values match.
- **Bars: all met.**

