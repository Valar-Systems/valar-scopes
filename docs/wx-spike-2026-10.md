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

**Ruled 2026-10-20: A2 accepted.** The smaller medians are recorded as off-prediction, cause above: crops that are nearly all one value collapse to a few long runs. No re-run. A3 and A5 stand.

### A4 live-set meter (ruled 2026-10-20): predictions, frozen before it runs

**Why the V8 meter failed:** `heapUsed` and `external` include garbage not yet collected, and holding an extra 64 MB makes V8 collect more often, so the sabotage showed only partly in the peak. **The live set is what has to fit**, because V8 collects before an isolate reaches its limit.

**Meter:** `node --expose-gc`. A forced `gc()`, then `heapUsed + external`, **at every pipeline stage boundary and right after each large allocation** (the download buffer, the unzipped file, the packed grid, and the held block in the sabotage); not on a timer. The live peak is the largest of those samples. **Wall and CPU come from the separate un-forced runs** (above). The old un-forced peak is reported alongside, for information only.

| case | predicted live peak | |
|---|---|---|
| **storm** (the bar applies) | **~24 MB** (range 18-30): Node's live baseline (a few MB) + download 2.12 + unzipped 0.74 + the PNG data's copy 0.74 + packed grid 12.25 + rows | bar ≤ 96 MB: **met** |
| quiet | **~22 MB** | |
| **sabotage:** storm + 64 MB held | **every sample rises by 63-65 MB** (ruled band) | |

**Local workerd:** the pipeline runs once in `wrangler dev` (local, no deploy, no remote bindings) to prove it runs there. Pass means it returns the storm frame's packed grid with the **same sha256 as Node's**. **No memory claim is made from that run**; production memory is confirmed in Stage 2.

### A4 live-set meter, results (2026-10-20). Bar met; OFF-PREDICTION (peak and two sabotage samples)

`tools/wx/a4-live.ts` (`node --expose-gc`): forced `gc()`, then `heapUsed + external`, at every stage boundary and right after each large allocation (the download, the unzipped file, the PNG data blob, the packed grid, the held block). 21 samples per run (22 with the hold); 3 runs per case; every run identical to within 0.01 MB at the peak.

| case | live peak (pred.) | un-forced peak, for information | wall / CPU (un-forced runs) |
|---|---|---|---|
| **storm** | **47.05 MB** ×3 (~24, range 18-30) | 64.0-65.9 MB | 1.04-1.15 s / 0.47-0.53 s |
| quiet | **42.89 MB** ×3 (~22) | 51.0-52.8 MB | 0.92-0.97 s / 0.42-0.58 s |
| sabotage: storm + 64 MB held | 111.05 MB ×3 | 114.2-114.5 MB | |

**Storm run 1, every sample (MB):** start 19.09 · download 28.96 · gunzip 26.09 · parse 26.09 · alloc-blob 26.23 · alloc-packed 37.94 · rows 39.12, **47.05**, 46.69, 46.30, 43.73, 43.40, 43.31, 43.19, 43.03, 43.06, 43.04, 43.02, 42.91 · packed 40.65 · stored 35.95.

- **Bar (storm): live peak 47.05 MB ≤ 96 MB, MET.** CPU 0.53 s ≤ 10 s, met.
- **OFF-PREDICTION, the peak (47 against ~24 MB):**
  - Node's own live baseline is **19.1 MB** at `start`, where I predicted "a few MB";
  - **~9 MB of in-flight stream buffers** are live, not garbage, during the PNG inflate (the early `rows` samples);
  - the ingest's own increment is **28 MB**, against a predicted ~16.
- **Sabotage, every sample +63-65 MB:** 60 samples were compared (matched by stage and occurrence against the mean of the 3 storm runs). **58 are inside the band and 2 are outside**: `download` **+62.85 MB** in one run and the first `rows` block **+70.89 MB** in one run. **OFF-PREDICTION.**
  - Mechanism, inferred from the samples: a mid-stream sample includes whatever the decompression stream holds in flight at that instant, which varies run to run by up to a few MB, wider than the ruled ±1 MB band.
  - The 3 `start` samples are excluded: they are taken before the hold is allocated, so it cannot raise them (+0.00 by construction).
- **Local workerd run-check: PASS.** `wrangler dev` (local; no deploy; no bindings; compatibility date 2025-12-01, the production Worker's) ran `ingest.ts` on the storm frame and returned the packed grid with **sha256 `011c2a65bd4d…`, identical to Node's**: 12,250,000 B, 7000 × 3500. No memory claim is made from it. Rig note: the first attempt used a 2026-09-01 compatibility date, newer than the local binary supports, and refused to start; it was re-run at the production date.

### A6: freshness (2026-10-20, live 2-hour poll). Bar met; OFF-PREDICTION, and THE SABOTAGE DOES NOT TURN THE BAR RED

`tools/wx/a6-poll.ts`: polled the live folder every 30 s from 02:47:43Z for 7,200 s. **60 frames** (2026-10-20 02:48:36Z to 04:46:36Z), gaps 114-127 s, **0 missed**. Each frame was downloaded, decoded and cropped (3 airports, 80 km).

| | predicted | measured |
|---|---|---|
| lag, frame time to crop ready: p50 | ~2.5 min | **1.06 min** |
| p95 (bar ≤ 4 min) | ~3.5 min | **1.28 min**, MET (max 1.33 min) |
| **sabotage:** skip every other frame (computed from the same log: a skipped frame's crop is the next processed frame's) | p95 rises ~2 min **and breaks the bar** | p95 rises **+2.00 min**, to **3.28 min**: **does not break the 4 min bar** |

- **Where the lag goes (medians):** NOAA's own landing delay **48 s** (file last-modified minus frame time; A0 had already seen ~50 s) · my poll delay **15 s** (half the 30 s interval) · download + decode + crop **0.9 s**.
- **OFF-PREDICTION (faster):** the predictions assumed files land 1.5-2.5 min after frame time; they land in under a minute.
- **OFF-PREDICTION (the check):** the sabotage moved p95 by exactly the predicted 2 min. But from a 1.3-min baseline the result stays under 4 min, so **as written, the bar cannot catch a poller that skips half the frames**.
- **Needs a ruling:** a tighter bar (for example, p95 ≤ 2 min), or a "no frame skipped" criterion. Skipping every other frame would fail the latter on 30 of 60 frames.

## Rulings 2026-10-20 (after A6 and the A4 live meter)

### A6: the lag stands; freshness is NOT a pass

- **The measurement stands:** NOAA publishes about **48 s** after frame time, and p95 to crop-ready is **1.28 min**.
- **Not a freshness pass:** the check failed its own sabotage. Skipping half the frames stayed under the 4 min bar. No local re-run.
- **My prediction miss:** A0's table had already measured the landing delay (**≈ 50 s**) and marked it "A6 input". The addendum written after A0 still left A6 predicting 1.5-2.5 min. The input was in hand and I did not carry it over.

**Stage 2 freshness checks, frozen now.** They are graded where the real poller runs:

| check | pass |
|---|---|
| lag, frame time to crop ready | **p95 ≤ 2 min** |
| no skipped frames | **every frame NOAA publishes during the run is processed, in order** |
| **sabotage:** skip every other frame | **both checks turn red** |

For information only, not a grade: on the A6 log, the real run is green on both (p95 1.28 min; 60 of 60 frames processed, in order). The sabotage is red on both (p95 3.28 min; 30 of 60 skipped).

### A4: accepted, graded PASS

- **Live peak 47.05 MB against the 96 MB bar.**
- **58 of 60** sabotage samples landed in the 63-65 MB band. The band stays as frozen, and no sample is dropped.
- **The verdict holds** because the ~49 MB margin to the bar is far larger than the meter's error.

**The two samples outside the band.** Both are in sabotage run 3.

| sample | sabotage run 3 | other sabotage runs | storm mean | rise | in flight at that instant |
|---|---|---|---|---|---|
| `download` (just after the body is read) | **91.80 MB** | 92.14 / 91.99 | 28.95 | **+62.85** | the fetch layer's own buffers. The three sabotage runs differ by 0.34 MB here; the three storm runs by 0.01 MB |
| first `rows` block (after 256 PNG rows) | **110.03 MB** | 103.01 / 103.02 | 39.14 | **+70.89** | **~7 MB of decompressed PNG data that the stream had produced ahead of the row reader.** In every other run (3 storm, 2 sabotage), the first row block sits 7.89-8.04 MB below the second. In run 3 it sits **1.02 MB** below: those chunks had already arrived by the first sample |

"In flight" is inferred from the per-stage totals. The meter records totals only, not what makes them up.

## A7: where it runs. Predictions, frozen 2026-10-20, before the freshness replay is computed

**Inputs, as ruled:** A4's numbers (**0.5 s CPU**, **47 MB live peak**, **2.12 MB** largest file), and A6's recorded landing times. A7 is a comparison, not a measurement. The one new number is a **replay of cron schedules against A6's 60 recorded frames**, defined here before it is computed.

**Options compared:**
- **(a)** a cron Worker, `* * * * *`, writing one frame to R2 (the original prediction);
- **(a′)** the same cron, but each invocation also checks again 30 s later. A cron invocation may run for up to 15 min wall time, and waiting costs no CPU;
- **(b)** a Durable Object whose alarm polls every 30 s and writes to R2;
- **(b-mem)** a Durable Object holding the frame in memory and serving crops itself (the original (b));
- **(c)** a server.

**Replay, defined:**
- For each A6 frame, the landing time `L` is the S3 `LastModified`. The local-vs-S3 clock skew is under 1 s (A6: poll delays 0.3-29.2 s on a 30 s interval).
- **(a):** the frame is picked up at the first whole UTC minute ≥ `L`.
- **(a′):** it is picked up at the first `:00` or `:30` ≥ `L`.
- Processing time is that frame's own A6 measurement (crop ready minus seen).
- Lag = pickup + processing − frame time.
- **The replay assumes zero cron jitter.** Cloudflare documents no firing-time accuracy, so Stage 2 measures it.
- **(b)** is not replayed: A6's own poller was a 30 s poll, so (b) **is** A6's measurement (p50 1.06, p95 1.28 min).

| schedule | predicted p50 | predicted p95 | frozen Stage 2 checks |
|---|---|---|---|
| (a) cron, once a minute | **1.3-1.5 min** (landing ~48 s + ~36 s wait + ~1 s) | **≤ 1.75 min** | both green |
| (a′) cron + a check 30 s in | ~1.1 min | ~1.3 min (≈ A6) | both green |
| **sabotage:** (a) skipping every other frame | | rises by ~2 min, past 2 | **both red** |

**Recommendation rule, fixed before the replay:**

| replay result for (a) | recommend |
|---|---|
| p95 ≤ 1.75 min | **(a)**, the fewest parts |
| 1.75 < p95 ≤ 2.0 min | **(a′)**. Same parts as (a), A6-level freshness. Plain cron would pass by less than 15 s, against an unmeasured jitter |
| p95 > 2.0 min | **(a′)**. (a) fails the frozen bar |
| **outside the set:** any replayed wait outside [0, 60) s for (a) or [0, 30) s for (a′), or the sabotage does not turn both checks red | **the replay is wrong: stop and report, no recommendation** |

(b-mem) and (c) are not recommended under any replay result, for the reasons in the cost and upkeep table.

### A7: results (2026-10-20). Verdict by the frozen rule: (a). One row OFF-PREDICTION (faster)

**The replay's control failed on the first run.** `tools/wx/a7-replay.ts` checks first that, with A6's own pickup times, it reproduces A6's published numbers.
- The first run did **not**: p50 1.05 against 1.06, and sabotage p95 3.23 against 3.28. It exited before printing any replay line.
- **Cause, read from A6's analysis code (not guessed):** A6 used `sorted[round(p·(n−1))]` with half-to-even rounding, and skipped the odd frames. The replay used nearest-rank percentiles and skipped the even frames.
- The replay was switched to A6's definitions. The second run reproduces all five A6 numbers: p50 1.06, p95 1.28, sabotage p50 1.33, p95 3.28, max 3.32.
- **Also found:** A6's sabotage figures cover **59** of 60 frames. Its last frame is skipped and has no later frame in the run, and the analysis left it out. The replay keeps that handling for percentiles and counts the frame under the no-skip check.

| schedule | p50 (predicted) | p95 (predicted) | max | frozen Stage 2 checks |
|---|---|---|---|---|
| **(a)** cron, once a minute | **1.42 min** (1.3-1.5) ✓ | **1.45 min** (≤ 1.75) ✓ | 1.46 | p95 green · no-skip green |
| **(a′)** cron + a check 30 s in | **0.92 min** (~1.1) | **0.95 min** (~1.3) | 0.96 | both green · **OFF-PREDICTION (faster)** |
| **sabotage:** (a) skipping every other frame | 1.45 | **3.45 min** | 3.47 | **both red** (30 skipped) ✓ |
| (b) DO alarm every 30 s = A6 as measured | 1.06 | 1.28 | 1.33 | both green |

- Every replayed wait is inside its bounds: [0, 60) s for (a) and [0, 30) s for (a′).
- **Why (a′) beats its prediction:** in all 60 frames, NOAA's file landed **17-28 s past the minute**. Frames sit 33-41 s past an even minute, and the landing delay is 39-55 s. A check at `:30` catches each file 2-13 s after it lands. My prediction assumed a random phase, which averages a 15 s wait. The same phase gives (a) its 32-43 s wait.
- **Verdict by the frozen rule: (a).** Its p95 is 1.45 min ≤ 1.75.

**For the ruling: not pre-registered, computed after the replay.** Both waits depend on that landing phase. NOAA sets it, and we observed it for only 2 hours. A bound that holds for any phase uses A6's slowest landing (55 s) and slowest processing (1.02 s):
- (a) stays **under 1.93 min**: inside the 2 min bar by ~4 s, before any cron jitter.
- (a′) stays **under 1.44 min**.

(a′) is the same Worker, the same cron and the same bill, plus one more bucket listing per invocation.

**Cost per month**, from A4's numbers: 0.5 s CPU, 47 MB live peak, 2.12 MB largest file. 720 frames a day, 21,600 a month. Workers Paid since 2026-10-20. Prices are from Cloudflare's docs, read 2026-10-20.

| | (a) / (a′) cron Worker → R2 | (b) DO alarm → R2 | (b-mem) DO serves crops from memory | (c) server |
|---|---|---|---|---|
| compute | 21,600 × 0.5 s = **10.8 M CPU-ms**. Included 30 M/month is shared with the proxy, which uses at most ~8.1 M (27 k req/day × 10 ms) → **$0** | alarm handlers ≈ 36,700 s × 128 MB ≈ **4,700 GB-s** of 400,000 included; 86,400 alarm requests of 1 M → **$0** | busy whenever devices poll: ≈ 332,000 GB-s, **83 %** of the included 400,000; ~1.19 M requests → ~$0.15 | $0 on an existing host, else a new monthly bill |
| invocations | (a) 43,200, (a′) the same plus a second listing each; 10 M included → **$0** | as left | | |
| memory | 47 MB live peak of the **128 MB** isolate limit | the same, inside the object | the same, plus the 12.25 MB grid held resident | — |
| download from NOAA | 0.5-1.5 GB/day (720 × 0.70-2.12 MB). Public bucket; Cloudflare does not bill subrequests | same | same | same |
| R2 writes | 1 write per frame (12.25 MB) = 21,600 of 1 M free → **$0** | same | none | same, plus **~265 GB/month** uploaded from the host |
| R2 storage | 1 h of frames = 30 × 12.25 MB = 368 MB of 10 GB free → **$0** | same | — | same |
| R2 reads (crops) | **estimate:** ~55 devices × 720 frames × ≤ 4 range reads ≤ 4.75 M of 10 M free → **$0**. Without caching, past ~115 such devices it costs $0.36 per million | same | none | same |

**Upkeep:**

| | what has to be built and looked after | what fails silently, and what has to read it |
|---|---|---|
| **(a) / (a′)** | one **new** Worker, one cron trigger, one R2 bucket and a lifecycle rule. Cron changes take up to 15 min to propagate. **It cannot live in the proxy:** production's `cpu_ms = 200` would stop a 0.5 s ingest on every run, and raising it loosens the runaway kill switch on device traffic. The 47 MB would also share the proxy's isolates | a stopped cron leaves an old frame being served. Needs a **frame-age field in `/healthz`** |
| **(b)** | everything in (a), plus a DO class and its migration, an alarm that reschedules itself, **and still a cron**. A handler that returns without setting the next alarm, or fails its 6 retries, ends the chain | the same frame-age field, plus the chain's state |
| (b-mem) | as (b) | the frame is lost when the object is evicted, and every crop goes through one object in one location |
| (c) | a host, its runtime and process supervisor, OS updates, and an **R2 write credential stored outside Cloudflare** | invisible to `/healthz` unless it reports in |

**Local workerd "does it run" (from A4): PASS.** `wrangler dev`, local, with compatibility date 2025-12-01 (production's), ran the storm frame. The packed grid's sha256 `011c2a65bd4d…` is identical to Node's. No memory claim is made from that run.

**Recommendation:**
- **(a)**, by the frozen rule: a separate cron Worker writing one frame per NOAA frame to R2, with a frame-age field in `/healthz`.
- **For the ruling:** the phase-free bound puts (a) within ~4 s of the 2 min bar, while (a′) keeps ~34 s at no extra cost or parts.
- Not recommended: (b), more parts for freshness (a′) already reaches; (b-mem); (c).
- Cron jitter, production CPU and production memory are all Stage 2 measurements.

