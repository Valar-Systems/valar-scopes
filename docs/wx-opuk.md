# `opuk`: UK weather radar on the server (R2 of the UK data plan)

**Status:** R2's predictions, frozen before any R2 code. The plan is `docs/uk-data.md` on main. Its K1-K13 were frozen at
`456988b` (A1.3), and K14 at `37e6864` (A2.2, on `docs/uk-data-r1`).

**The ruling that starts R2 (reviewer chat, pasted by Daniel, 2026-10-29), verbatim:**
> Start R2 now (reviewer chat, 2026-10-29)
>
> The week's results (B, C, T) decide #445's age limit and the clutter mask. They don't decide the rest of R2's server
> code, so R2 doesn't wait for 2026-11-04. This amends my earlier "before R2's code".
> - Start R2 now: the COG reader, the resampling, the codes, WXG1, the cref dispatch, the 1,800 s limit, the covered
>   share, prune-on-store and the User-Agent. K1-K14 as frozen, predictions first.
> - The clutter mask goes behind one switch, set at 0.5 as planned. C's reading on 11-04 decides its final setting, and
>   Q17's answer with it, before R3's deploy.
> - Read L (the bucket's terms) now.
> - R3 and R4 may deploy once their checks pass and the mask is settled, under the standing rule and Daniel's $2
>   allowance. No shipped firmware reads /wx yet, so UK frames on the server affect no customer.
> - The collection keeps running to 11-04 as frozen.

**L was read the same day:** outcome (b), recorded in `docs/uk-data.md` A3.9 on `docs/uk-data-r1`.

## What R2 builds

**Everything is in `proxy/src/wx/`, with no Cloudflare resource:** the same bucket, the same ingest Worker and the
same cron.
- **`opera.ts`:** the COG reader. It reads an 8 KB range, parses IFD 0 (classic little-endian TIFF), then range-reads
  each window row of tiles and inflates them with `DecompressionStream("deflate")`.
  - It accepts only `float32` with 2 samples, chunky, deflate, no predictor, and LAEA (`ProjCoordTrans` 10) on WGS84
    (7030).
  - **The projection is read from the frame's own GeoKeys** (false easting and northing, centre latitude and
    longitude) and its tiepoint and pixel scale, never assumed.
- **`laea.ts`:** ellipsoidal Lambert azimuthal equal-area, forward and inverse.
- **`resample.ts`:** OPERA's 1 km pixels to the `opuk` 0.01° WXG1 grid.
  - **Inside each target cell:** among the source pixels whose centres fall in it, `blockCode`'s priority. The
    strongest echo wins; else 14 if any pixel has no coverage; else 0.
  - **A cell with no source centre inside** takes the pixel containing its centre.
  - **The source-to-cell map is static,** built once per isolate.
- **The codes:** NaN (undetect) → 0; −9,999,000 (nodata) → 14; any other value through `codeOfComposite`.
  - **The clutter mask is one switch** (`OPUK_QI_MASK`, default on, threshold 0.5): an echo whose quality index is
    below 0.5 becomes 0.
- **`products.ts`:** the `opuk` row.
  - its window: the UK land box plus 2.02°;
  - a WXG1 grid of 1,515 × 1,455 cells at 0.01°;
  - cadence 300 s; `maxAgeS` 1,800;
  - its source: the `openradar-24h` cache's `DBZH` GeoTIFFs.
- **The ingest:**
  - **the products' checks run concurrently** in each invocation, as §1.3 planned;
  - `opuk` prunes only on a check that writes a frame;
  - **its covered share** goes on the frame object as R2 custom metadata, so any later check that moves `latest` carries
    it into the record;
  - every CloudFerro request carries K14's User-Agent.
- **The handler:**
  - `/wx/cref` answers from `opuk` when the quantised point is inside the land box, with the ETag naming `opuk`;
  - `/wx/opuk` answers directly;
  - `/healthz`'s `wx` gains `opuk` with `frameAgeS` and `coveredShare`.
- **`smoke-prod.sh`** FAILs on a missing or zero `coveredShare` for `opuk` (K9). The UK `/wx` check is R4's.

## The checks: K1-K14 as frozen, with the details they left open pinned now; and K15

Each sabotage must turn its own row red. BLIND counts as FAIL.

| | check | predicted | sabotage |
|---|---|---|---|
| **K1** | the COG reader on **2 real frames** from the week's collection, the UK window: **the wettest and the driest** held at 2026-10-29 03:00Z, by echo cells in the window. The fixture holds exactly the byte ranges the reader requests (the first 8,192 B and each window row of tiles). The expected value is the sha256 of the window's value band (`float32` LE, NaN written as `0x7FC00000`) **computed from that frame's HDF5** by `tools/wx/opera_fixture.py`, cross-checked there against `tifffile`'s decode of the full COG, plus the projection numbers from the HDF5's `projdef` | **the reader's sha equals the HDF5's** on both frames, as do the projection numbers (to 1e-9) | band 1 read for band 0: the shas differ |
| **K2** | resampling, on a synthetic source grid with a real frame's geometry: undetect everywhere, except a one-pixel-wide diagonal line of 40 dBZ from the window's north-west to its south-east | **0** 0.01° rows between the line's first and last rows without an echo cell | the centre rule alone (nearest neighbour): gaps > 0 |
| **K3** | the codes | NaN → 0; −9,999,000 → 14; 14.99 → 0; 15.0 → 1; 75.0 → 13; 78.5 → 13; code 15 never | 15 dBZ mapped to 0 |
| **K4** | WXG1 | 1,515 × 1,455 cells, 9 tiles, **1,179,712 B**; a round trip identical cell for cell | `tilesY` for `tilesX` |
| **K5** | the dispatch | LHR → `opuk`; **CDG (inside the window, outside the land box) → today's answer** (`cref`'s all-14 crop); KMCI → `cref`; PHNL → `cref`'s all-14 crop; the land box and the CONUS grid do not overlap | dispatch by the window: CDG served `opuk` |
| **K6** | every view served from `opuk` inside its grid: the land box on a 0.05° lattice × r ∈ {1, 25, 80, 160, 222, 300, 500} km, with WS4′'s device corners | **0** views outside the grid. Printed: crops whose padding reaches outside (served as 14) | a margin of 1.0° |
| **K7** | the ETag names the served product | the same query and epoch give different ETags for `cref` and `opuk`; a device moved from LHR to KMCI never gets a 304 | an ETag without the product |
| **K8** | `opuk`'s 1,800 s limit | a 900 s-old frame → 200 (`cref` at 900 s → 503); 1,801 s → 503 `wx_stale` | the limit read from `cref` |
| **K9** | the covered share | each frame's record carries the window's covered share (cells not 14, over all cells); an all-nodata frame reads 0; `/healthz` prints it; `smoke-prod.sh` FAILs on 0 or a missing share for `opuk` | the share fixed at 1: the all-nodata frame reads 1 |
| **K10** | prune only on the storing check | a storing check prunes; a check that stores nothing makes **0** R2 lists (a wrapped binding) | prune on every check |
| **K11** | CPU and live memory in Node, on 3 real frames from the collection | CPU **≤ 0.6 s** a frame, from the tile bytes in memory to the WXG1 bytes (the one-time static map timed and printed separately); live peak **≤ 96 MB** (A4's meter). R3's production bar is twice Node (I7's factor) | 64 MB held: the peak +63-65 MB |
| **K12** | the device decodes `opuk` crops: **8** server-made crops over a real frame, at LHR, MAN, EDI, GLA, BFS, ABZ, CWL and NQY (r = 161 km) | all 8 decode with `include/WxCrop.h` to the encoder's cells (a manifest of sha256s) | one nibble flipped fails exactly that crop |
| **K13** | the host render (Q7) | a real `opuk` crop at LHR through `wxlayer::Build` and `WxS2` to a PNG, committed. Looked at, not graded | — |
| **K14** | every CloudFerro request (listing, header, tile ranges), through a wrapped `fetch` | each carries `User-Agent: BlipscopeWxIngest/1 (+https://github.com/Valar-Systems/valar-scopes)` | the header dropped from one request kind |
| **K15** (new) | the mask switch, on synthetic cells | **on:** (40 dBZ, QI 0.4) → **0**; (40, 0.5) → **6**; (40, 0.9) → 6. **off:** (40, 0.4) → 6. The default is on, at 0.5 | `<=` for `<`: (40, 0.5) → 0, red |

**What R2 does not do:**
- **no deploy:** R3 and R4 deploy once their checks pass and the mask is settled by C's reading on 11-04;
- **no change to `cref`'s data path,** except that its checks now run concurrently with `opuk`'s;
- **no firmware.**

**Outside the set:** any reading not listed above means stop and report.

### K1's frames, restated before any selection was computed (2026-10-29, ~03:40Z)

- **The problem:** K1 pins "the wettest and the driest held at 2026-10-29 03:00Z". A fixture needs the frame's own
  HDF5 (the expected value) and its first 8 KB (the collection kept only the tiles).
- **The cache has expired every 2026-10-27 frame:** 0 listed, against 288 held.
- **No selection has been computed,** so this is a restatement, not a choice made after seeing the frames.
- **K1's frames, restated:** the wettest and the driest **among the frames held at 2026-10-29 03:00Z whose HDF5 and
  GeoTIFF are still in the cache** (2026-10-28 00:00Z onward), by echo cells in the window.
- **Everything else in K1 is unchanged.**

### Two cases the rows left open, pinned before their code (2026-10-29)

- **K9's smoke check when no `opuk` frame exists yet** (`frameAgeS` null). That is the state between R2's merge and R3's
  ingest deploy. K9 says "FAILs on 0 or a missing share", and that is about a frame whose record lacks a share.
  - **Pinned:** with no frame, the check passes as "not ingesting yet", as the `/wx` check's inert answer did before
    the bucket was bound.
  - With a frame, the share must be present and > 0, or FAIL.
  - **Pinned 2026-10-29, before the smoke check's code:**
    - **`wx` is `"not_configured"`** (no bucket bound) → PASS, as the `/wx` check's inert answer does.
    - **`wx` has no `opuk` entry at all** (a Worker deployed before R2's merge) → **FAIL**, as the `/wx` check's 404
      does: production is behind main.
    - **A share above 1** → FAIL: it cannot be a share.
    - The verdict lives in `scripts/wx-opuk-verdict.sh`, sourced by `smoke-prod.sh`.
      `scripts/wx-opuk-verdict.selftest.sh` runs it on nine canned bodies and compares the whole verdict list. Its
      sabotage drops the zero test, and case 5 (share 0) must then read PASS and fail the selftest.
- **What `/wx/cref` answers at a UK point before R3 deploys,** if the proxy is redeployed with R2's code in between:
  `opuk` has no `latest`, so **503 `wx_stale`**, where today it is a 200 all-14 crop.
  - No shipped firmware reads `/wx` (the ruling), so no customer sees it.
  - Smoke's `/wx` check asks at Bend, which `cref` still serves.
  - **Recorded, not changed.**

### K4's sabotage is BLIND by construction on `opuk`'s grid (found 2026-10-29, before any K4 code ran)

- **The finding:** a WXG1 tile's offset is `ty * tilesX + tx` (`grid.ts` `tileOffset`). On `opuk`'s grid
  `tilesX = ceil(1455 / 512) = 3` and `tilesY = ceil(1515 / 512) = 3`. With the two swapped, every offset, count and
  byte is identical, so the frozen sabotage ("`tilesY` for `tilesX`") cannot turn K4 red.
- **How it was found:** by reading `tileOffset` while writing K4's test. Nothing had run.
- **What it means under the rules:** BLIND counts as FAIL, so **K4 reads FAIL** until the reviewer rules. K4's predicted
  values (1,515 × 1,455 cells, 9 tiles, 1,179,712 B, a cell-for-cell round trip) still run as frozen and are reported.
- **A proposed remedy, NOT applied, for the reviewer to rule on:** the same swap on a grid whose tile counts differ:
  `opuk`'s 1,515 rows with 1,000 columns (3 × 2 tiles). **Predicted:** the round trip then fails cell for cell.

## R2's first readings (2026-10-29, `test/wx-opera.test.ts`)

**Two rows are outside the predicted set (K1, K2), and K4's sabotage is BLIND.** Those three stop until the reviewer
rules. Every other row read as predicted, with its sabotage red.

| | predicted | got | verdict |
|---|---|---|---|
| **K1** | the reader's sha equals the HDF5's on both frames; projection to 1e-9 | projection **equal**; the window's column indices are **one higher**: `[1059, 2883, 418, 1636]` against `[1059, 2883, 417, 1635]`, so the sha differs on both frames | **OUTSIDE THE SET: stopped** |
| **K2** | 0 gap rows; the centre rule alone: gaps > 0 | **0** gap rows (the line crosses grid rows 469-1334); the centre rule alone: **0** | **sabotage BLIND = FAIL: stopped** |
| K3 | the six values, never 15 | as predicted; the sabotage reports `15 -> 0, not 1` only | pass |
| **K4** | 1,515 × 1,455, 9 tiles, 1,179,712 B, round trip identical | as predicted | values pass; **sabotage BLIND (above) = FAIL** |
| K5 | LHR `opuk`; CDG, KMCI, PHNL `cref` (CDG and PHNL all-14); no overlap | as predicted; MAN, EDI, GLA, BFS, ABZ, CWL and NQY also `opuk`; the sabotage serves CDG `opuk` | pass |
| K6 | 0 views outside | 329,371 cases, 2,634,968 views, **0 outside**, 0 crops padded past the grid, 0 not served `opuk`; margin 1.0: 495,506 outside | pass |
| K7 | different ETags; no 304 after a move | as predicted (CONTROL: the same query does get its 304); the sabotage collides | pass |
| K8 | 900 s 200, `cref` 900 s 503, 1,801 s 503 | as predicted; the sabotage refuses `opuk` at 900 s | pass |
| K9 | the share in the record; all-nodata 0; `/healthz` prints it | the dry frame **0.7513**, the same in the record and in `/healthz`; `cref`'s entry unchanged; all-nodata 0; the sabotage reads 1 | pass (the smoke half is still to write) |
| K10 | a storing check prunes; a non-storing one makes 0 lists | 1 list, then 0; the sabotage lists | pass |
| K11 | CPU ≤ 0.6 s a frame; live peak ≤ 96 MB; 64 MB held: +63-65 MB | 0.297 / 0.312 / 0.266 s; 54.46 / 54.22 / 54.22 MB; +64.03 MB (below) | pass |
| K12 | 8 `opuk` crops decode with `WxCrop.h` to the encoder's cells; one nibble flipped fails exactly that crop | **8 / 8** (LHR, MAN, EDI, GLA, BFS, ABZ, CWL, NQY at 161 km, over the wet frame, through `/wx/cref`'s dispatch; 5-998 echo cells each); the sabotage (`man-161km.wxc`, pinned before it ran) fails its `file` and `cells` lines and nothing else; g++ 13.3 in WSL | pass |
| K14 | the UA on every request | list 1, head 1, tiles 4, none missing; each of the three sabotages names exactly its own kind | pass |
| K15 | 0, 6, 6; off 6; default on at 0.5 | as predicted; the sabotage masks (40, 0.5) | pass |

**K11 (`scripts/wx-opuk-measure.ts`, Node, one measurement per process): pass.**
- **The frames:** the two K1 fixtures and a third, restated before measuring. The rule I first stated, "the newest
  frame in the collection", named the dry fixture itself (the collection last ran at 01:38Z), so it became **the
  newest collected frame that is not a fixture: `20261029T0125`**. Its head and window rows were range-read from the
  cache with K14's User-Agent and held in memory.
- **CPU, tile bytes in memory to WXG1 bytes:** **0.297, 0.312, 0.266 s** (bar 0.6). **The static map, once per
  isolate:** 0.516, 0.485, 0.531 s, printed separately.
- **Live peak (A4's meter):** **54.46, 54.22, 54.22 MB** (bar 96), at the resampling stage. **The sabotage, 64 MB
  held:** 118.25 MB, **+64.03 MB** (predicted +63-65).
- **The covered share reads 0.7513 on all three frames,** while the WXG1 shas differ. It is not a constant: the HDF5's
  own nodata counts are 818,321 and 818,320 (the radar network's footprint, one pixel apart in about 2.2 M), and K9's
  all-nodata case reads 0.

**K12's run:** the host suite's own `run.sh` stopped BLIND at an earlier block in this worktree (the Wi-Fi QR test
needs LovyanGFX under `.pio/libdeps`), so its later blocks did not run here. The K12 block's command was run on its
own and passed, and WS7's server set and its sabotage still read as before. CI's host job is the authority.

**LAEA against pyproj (PROJ 9.8.1; tolerances stated in the test before its first run):** forward 5.0e-5 m worst over
544 points, inverse 5.0e-11° worst over 1,080. A near-sphere misses by 3,192 m.

### K1: what the two files say, read-only (`tools/wx/opera_corner.py`, differences only, no coordinates)

- **The GeoTIFF** declares `PixelIsArea` (GeoKey 1025 = 1), with its tiepoint at raster (0, 0).
- **The HDF5's `UL` corner** lies exactly **+500 m east and −500 m south** of the GeoTIFF's corner: **half a pixel on
  both axes.** The HDF5 is self-consistent: UL to UR is exactly 3,800 px and UL to LL exactly 4,400 px, so its four
  corners are all pixel corners.
- **So the two files of one frame place the raster half a pixel apart.** The half pixel moved the columns' floor and,
  by coincidence, not the rows'.
- **Which is right is not settled by the data:**
  - **(a) the GeoTIFF is half a pixel off** (the pixel-is-point/area slip in a conversion). For it: the HDF5's corner
    projects to the origin, (0, 0), to within a millimetre, which looks designed.
  - **(b) the HDF5's corner attributes are off.** Nothing in the data excludes it; OPERA's documentation of the
    CIRRUS grid is the third source that could.
  - **One look for that source, not conclusive:** a web search found nothing CIRRUS-specific. Its summary says
    EUMETNET's ODIM template defines `UL_lon`/`UL_lat` as the image's corner, which would favour (a), but I could not
    read the template's text here (no PDF text tool), so that claim is **unverified**.
- **What it costs either way:** 500 m in x and y, against 1 km pixels and 0.64 × 1.11 km cells.
- **No fix is applied.** The reader still takes the GeoTIFF's corner.

### K2: why the sabotage cannot fire (reasoned, not measured), and my control

- **A diagonal does not separate the two rules.** A 0.01° cell is 0.64 km wide at these latitudes, so along any grid
  row the centre rule samples every source column, and the diagonal's pixel in that row is hit either way. The "one
  source row in ten" that the centre rule drops matters for a feature one source row tall, and the grid's parallels
  cross the source rows at about 12° here (an estimate: (−5° − 10°) × sin 55°), so even a horizontal line is likely
  caught.
- **What would separate them** (a proposed remedy, NOT applied): **isolated single-pixel echoes** (one 40 dBZ pixel in
  every 7 × 7 block, undetect elsewhere). **Predicted:** the block rule loses **0** pixels whose centre lies in the grid;
  the centre rule alone loses **> 0** (a pixel holding no cell centre is never sampled).
- **The control that also failed is mine, not frozen:** `last - first > 1000` was written without computing it. The
  line crosses 865 grid rows (the window's corners lie outside the rotated grid). It is reported, not re-thresholded.

### Changes made after a first reading, without a ruling (stated so they can be reverted)

The first run of `test/wx-opera.test.ts` was **outside the set on every fixture row**: the reader refused both frames
("the window leaves the raster").
- **The cause, in the code under test:** `opera.ts` read GeoKeys 3088 and 3089 the wrong way round (3088 is
  `ProjCenterLongGeoKey`, so lat0 came out 10 and lon0 55). **I fixed it before the second run,** without a ruling, in
  its own commit so it can be reverted. K1's projection check is the check that caught it.
- **Three defects in the test scaffolding,** none changing what a row asserts, also fixed before the second run:
  - the LAEA control compared Python's `55.0` with JavaScript's `55` as strings;
  - K8 compared the error body without its `v: 1` (the statuses were 503, as predicted);
  - the predictor-refusal plant did not apply, because OPERA's IFD carries Predictor = 1 explicitly after tag 284.
    It now plants over 317 itself, with a control that the value was 1 first.

## The ruling on R2's three stops (reviewer chat, pasted by Daniel, 2026-10-29), verbatim

> UK data session: from the reviewer chat, 2026-10-29
>
> R2's three stops, and your changes after the first reading:
>
> 1. K1: (a). The HDF5 is right, and the GeoTIFF is half a pixel off.
> - The third source: BALTRAD's RAVE navigation notes (https://baltrad.github.io/rave_navigation.html), from ODIM's
>   reference implementation, say that ODIM H5's UL/UR/LL/LR lon/lat pairs "defines the outer boundaries of the area
>   covered". Read it yourself and record the quote with its URL.
> - That agrees with the HDF5's own geometry: corner to corner is exactly 3,800 and 4,400 px, so these are outer
>   corners, not centres, and its UL corner sits on the false origin to within a millimetre. The GeoTIFF's PixelIsArea
>   tiepoint sits exactly half a pixel outside that corner. That is the classic slip of a converter that read ODIM's
>   corners as pixel centres.
> - The reader's rule, narrow on purpose: PixelIsArea only (GeoKey 1025 = 1). A tiepoint exactly half a pixel outside
>   the false origin (-xscale/2, +yscale/2, within 1 mm) is the known CIRRUS slip, and the raster's corner goes on the
>   false origin. A tiepoint exactly on the false origin is used as is (a corrected product). Anything else refuses the
>   frame, so a future change at OPERA can't move the map silently.
> - K1 stands as frozen, with its expected values from the HDF5. Add K1b, frozen before its code: the slip tiepoint
>   gives the HDF5's window on both frames; a tiepoint on the origin is used as is; a tiepoint 1 m off either is
>   refused; PixelIsPoint is refused. Sabotage: with the acceptance check removed, the 1 m case is no longer refused.
> - The first reading stays on record as the miss: one column off on both frames.
>
> 2. K2: your remedy, as K2′. The diagonal's result stays on record: 0 gap rows and a BLIND sabotage, so K2 as frozen
>    reads FAIL. K2′: isolated single-pixel 40 dBZ echoes, one in every 7x7 block, undetect elsewhere. Predicted: the
>    block rule loses 0 echo pixels whose centre lies in the grid; the centre rule alone loses > 0, with the count
>    printed. Your unfrozen "> 1000" control is dropped, with 865 recorded.
>
> 3. K4: your remedy. K4's values on opuk's 3x3 grid stand. Its sabotage runs on a 1,515 x 1,000 grid (3 x 2 tiles):
>    without the swap the round trip is identical (the control), and with tilesY for tilesX it fails cell for cell. K4
>    as frozen stays on record as BLIND.
>
> 4. Accepted: 14d5fb9 (3088 is ProjCenterLongGeoKey and 3089 ProjCenterLatGeoKey; I checked the diff) and the three
>    scaffolding fixes. All stay on record as made after the first reading. A check catching a bug in the code under
>    test, followed by a fix in its own commit, is the process working.
>
> Then re-run every R2 row with K1's correction in. It moves every resampled cell by half a pixel, so K12's crops and
> manifest are rebuilt by the same rule. Then K13's render. #499 stays a draft until CI is green on every row.

**The third source, read 2026-10-29:** <https://baltrad.github.io/rave_navigation.html>, section "ODIM H5 Area
Definition":
> These coordinates defines the outer boundaries of the area covered.

and, further on:
> Hence, the UL/UR/.. defines the actual outer boundaries of the area.

The page says the (X, Y) points of the area are "placed in the center of each sub area": the centres are the pixels,
and the four corner pairs are the outer boundary. Read through a page fetch, which returns the page's text rather than
its raw HTML; the two sentences are quoted as it returned them.

### Frozen before the code that follows them (2026-10-29)

**K1b: the reader's corner rule.** It runs on the dry fixture's real head, with the tiepoint and GeoKey 1025 planted in
copies. Every plant is asserted to have applied (the planted value is read back) before its result is believed.

| case | predicted |
|---|---|
| the real head (the CIRRUS slip: tiepoint at (−xscale/2, +yscale/2)) on **both** frames | accepted; the raster corner is the false origin; the window is the HDF5's, `[1059, 2883, 417, 1635]` |
| tiepoint planted on the false origin, (0, 0) | accepted as is; the same window |
| tiepoint 1 m off the slip, (−501, 500) | refused |
| tiepoint 1 m off the origin, (1, 0) | refused |
| GeoKey 1025 planted to 2 (PixelIsPoint) | refused |
| **sabotage:** the acceptance check removed (a test-only switch); the tiepoint used as is | the (−501, 500) case is **not** refused, so the row goes red |

**K2′: isolated echoes.** One 40 dBZ pixel (QI 0.9) at the centre of every 7 × 7 block of the dry frame's window,
undetect elsewhere.
- **"Kept"** means at least one echo cell within ±2 grid rows and ±3 grid columns of the cell holding the pixel's centre.
  At 7 px (about 7 km) between echoes, that neighbourhood (about 2.2 × 1.9 km) holds no other echo's cells.
- **The denominator** is the echo pixels whose centre lies in the grid. Printed.
- **Predicted:** the block rule loses **0**. The centre rule alone loses **> 0**, with the count printed.
- **The diagonal (K2 as frozen)** stays in the suite as the record: 0 gap rows. Its sabotage is pinned as BLIND (it
  must still read 0, as K4's frozen sabotage is pinned), and the `> 1000` control is dropped.

**K4′: K4's sabotage on a grid where it can fire.** The same pattern on a 1,515 × 1,000 grid (3 × 2 tiles).
- **Control:** without the swap, the round trip is identical (0 cells differ).
- **Sabotage:** with `tilesY` for `tilesX`, it fails cell for cell (> 0 cells differ, printed).
  - **A cell the swapped layout cannot read** (its tile was never produced; `tiledCellAt` throws) **counts as
    differing.**
- K4's frozen sabotage on `opuk`'s 3 × 3 grid stays pinned as BLIND.

**The re-run, with K1's correction in: every row's prediction as frozen.**
- **K1:** the window is the HDF5's, and the sha equals on both frames.
- **K9:** the share is present, above 0 and at most 1, and the same in the record and in `/healthz`. It will move from
  0.7513 by the half pixel, and no value was predicted, so it is printed.
- **K11:** the frozen bars.
- **K12:** rebuilt from the corrected reader: 8 / 8 decode, and the sabotage fails exactly `man-161km.wxc`.
- **K13:** rendered and committed, looked at.
- **The whole proxy suite:** 0 failing tests, so #499's CI can go green.
- **Outside the set:** any other reading means stop and report.

## R2's re-run with K1's correction (2026-10-29: `ca7cd26` reader, `f02abac` tests, `20e572b` K13)

**Every row read as frozen at `31b8c85`.** The first readings above stay as the record: K1's one-column miss, and K2's
and K4's BLIND sabotages.

| | got | verdict |
|---|---|---|
| **K1** | the window is the HDF5's, `[1059, 2883, 417, 1635]`, and the **sha equals** on both frames; projection equal; the band-1 sabotage differs | pass |
| **K1b** | the real heads are the slip on both frames (corner on the origin, the HDF5's window); a tiepoint on the origin is used as is, with the same window; (−501, 500), (1, 0) and PixelIsPoint are refused; with the check removed, (−501, 500) is not refused | pass |
| K2 as frozen | 0 gap rows (the line now crosses grid rows 470-1336); its sabotage is still 0, pinned as BLIND | FAIL as frozen (the record) |
| **K2′** | 45,414 echoes, 31,673 with their centre in the grid; the block rule loses **0**; the centre rule alone loses **2,039** (6.4 %) | pass |
| K3 | as before | pass |
| K4 / **K4′** | K4's values as before; its frozen sabotage pinned BLIND; on 3 × 2 tiles the control is identical and the swap differs on **1,001,498 of 1,515,000** cells | K4′ pass |
| K5-K8, K10, K14, K15 | as before; K6's counts unchanged (they do not depend on the reader) | pass |
| K9 | the share is now **0.7508** (it was 0.7513: the half pixel), the same in the record and in `/healthz` | pass |
| K11 | CPU **0.407, 0.298, 0.249 s**; the map 0.594, 0.547, 0.548 s; peak **54.48, 54.24, 54.24 MB**; 64 MB held: 118.26 MB, **+64.02 MB** | pass |
| K12 | rebuilt: `--check` reported all 8 `opuk` crops and the manifest stale before the rebuild and clean after, with the server set unchanged. **8 / 8** decode; the sabotage fails exactly `man-161km.wxc`, `file` and `cells`; g++ 13.3 in WSL | pass |
| **K13** | rendered, looked at (below) | done |
| LAEA | unchanged | pass |

**K11's wet frame took 0.407 s, against 0.297 s on the first run,** for the same work. That is timing noise on this
machine. Both readings are under the 0.6 s bar, and R3's production bar (twice Node) is measured on Cloudflare.

### K13: the host render, looked at

- **Built against the device branch's headers at `3f7a026`** (`include/WxLayer.h`, `include/WxS2.h`, `include/WxCrop.h`,
  `src/weatherscope/WxPalette.h`), exported read-only with `git archive`. The branch's head read `3f7a026` twice, before
  and after.
- **The tool:** `tools/wx/render_opuk_crop.cpp` and `ppm_to_png.py`. It uses ProjectAt's view for 161 km, no rotation,
  the default 240 px screen, and look A.
- **`docs/images/wx-opuk-k13-lhr.png` (K13 as frozen):** echo 0, no coverage 0, 45,244 dry pixels, 12,356 off the disc.
  - It is an **all-black disc**: no echo near London at 02:55 on 10-28 (the crop holds 5 echo cells in about 119,000,
    and no pixel falls on one), and dry draws as the background.
  - It is a true picture, but it shows the layer only by absence.
- **`docs/images/wx-opuk-extra-abz.png` (an extra, not a K13 reading):** the ABZ crop from the same frame. 478 echo
  pixels, 0 no coverage.
  - Scattered teal, green and blue cells, dimmed as look A draws them, in a band across the northern half of the disc.
    The southern half is dry.
  - It reads as a weather layer. Without a basemap under it, I cannot judge more than that.

## The clutter mask off, and OPERA on /credits (frozen 2026-11-04, before this PR's code)

**The ruling (reviewer chat, pasted by Daniel, 2026-11-04 ~17:50Z), recorded verbatim in `docs/uk-data.md` A3.17 on
`docs/uk-data-r1`:**
- the mask-off PR is approved to build from main, with M1-M4 and CI green as frozen in A3.16 (`db9eb7b`);
- `OPUK_QI_MASK` stays as a switch, default off;
- before `opuk` serves in production, /credits carries OPERA's CC BY 4.0 attribution.

**Why the mask goes off:** R1's C read OPERA's quality index (`qi_total`) as almost always exactly 0.0 on real echoes.
98.1 % of echoes sit under 0.5, read straight from the HDF5. And C1 was 0: no persistent clutter in the week. So the
mask at 0.5 removed about 98 % of the rain `opuk` would serve, including in K12's crops and K13's render.

**M1-M4, as frozen in A3.16:**

| | check | predicted |
|---|---|---|
| **M1** | K15, restated for a default of off | with the default: (40 dBZ, QI 0.4) → **6**, (40, 0.5) → 6, (40, 0.9) → 6. With the switch on: K15's frozen rows unchanged (0, 6, 6; off 6), and its sabotage (`<=` for `<`) still red |
| **M2** | K12 rebuilt with the mask off | each crop's echo cells **≥** its masked count. **The total ≥ 10 × 1,665 = 16,650.** 8 / 8 decode, and the sabotage fails exactly `man-161km.wxc` |
| **M3** | K13 re-rendered at LHR, and the ABZ extra | rendered, looked at; no echo count predicted |
| **M4** | every other R2 row (K1-K11, K14) | unchanged |
| — | the whole proxy suite, and host-tests | green |

**M5 (new, frozen here): OPERA's line on /credits.**
- **The wording** is the one A2.1 accepted in principle ("Final at R4's review"). It is held on one line of the
  template with no markup inside, as the NOAA line is. Its exact bytes:
  > Weather radar outside the US: EUMETNET OPERA CIRRUS maximum-reflectivity composite, created by Meteo France,
  > licensed under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). Reprojected to a 0.01&amp;deg; grid and
  > binned to 5 dBZ steps by Valar Systems. Not endorsed by EUMETNET.
  - The degree sign is written `&deg;` (as the page writes `&mdash;`), so the bytes are ASCII. On one line in the
    template; wrapped here only.
- **The unit test:** the rendered page holds that line exactly once, beside the NOAA line's test.
  - **Its sabotage:** the line removed from the template. The test must turn red; asserted that the plant applied.
- **The smoke check:** `smoke-prod.sh` gains `contains "/credits carries the OPERA line"` with the same bytes.
  - **Against today's production** (which predates it), it must read **FAIL**. That is its proof that it can fail.
  - It reads PASS once a Worker with this PR is deployed.

**Outside the set:** any other reading means stop and report.

### The mask off and OPERA on /credits: the readings (2026-11-04, `e0a198f`, `80bba46`)

**M1-M5 read as predicted. One existing test, outside the set, fails, and is stopped for a ruling.**

| | got | verdict |
|---|---|---|
| **M1** | the default is off at 0.5: (40, 0.4), (40, 0.5), (40, 0.9) → 6, 6, 6. Switched on, K15's rows: 0, 6, 6; off 6. The sabotage gives 0, 0, 6, 6 (red) | pass |
| **M2** | every crop ≥ its masked count: LHR 6 → **68**, MAN 186 → 31,413, EDI 191 → 34,471, GLA 102 → 39,526, BFS 90 → 30,600, ABZ 995 → 49,516, CWL 63 → 5,719, NQY 32 → 871. **Total 192,184** (bar 16,650). None coarsened; the largest is 11,663 B. `--check` is clean, and the server set is unchanged. **8 / 8 decode** (WSL, g++ 13.3); the sabotage fails exactly `man-161km.wxc`, `file` and `cells` | pass |
| **M3** | re-rendered from the same headers (`3f7a026`), looked at (below) | done |
| **M4** | `test/wx-opera.test.ts` passes, with the same printed readings (K2′ 0 lost of 31,673, K9 0.7508). **K11 re-measured:** CPU 0.343, 0.328, 0.266 s; peak 54.59, 54.35, 54.35 MB; 64 MB held, +64.02 MB | pass |
| **M5** | the rendered /credits holds OPERA's line exactly once. **The sabotage** (the line removed from the template, asserted applied) turns the unit test red; the template was restored from a byte copy, sha matched. **Smoke's own `contains()`** reads **FAIL** against today's production (HTTP 200, marker missing). Its control, the same function on the same page for the NOAA line, reads PASS | pass |

**M3, looked at:**
- **`docs/images/wx-opuk-extra-abz.png`:** a broad rain shield across the north and west of the disc, greens with
  yellow and orange cores in the west, the south-east dry. 20,618 echo pixels, against 478 with the mask on.
- **`docs/images/wx-opuk-k13-lhr.png`:** 5 echo pixels. London at 02:55 on 10-28 was mostly dry: the crop holds 68 echo
  cells, against 6 with the mask on.
  - The earlier "dry London" stays withdrawn, because it was made with the mask on. This render is the weather.

**Outside the set: an existing test fails. Stopped for a ruling.**
- **The test:** `courtesy-credits PD without a license link` (`test/photolicense.test.ts`, since `f25d14d`,
  2026-07-16).
- **What it does:** it renders a public-domain photo entry and asserts the WHOLE page holds no `creativecommons.org`.
  Its intent is that a public-domain photo gets no CC licence link. OPERA's line carries
  `https://creativecommons.org/licenses/by/4.0/` in the data-sources block, so it now fails, though the photo entry
  still renders no CC link.
- **The kind of failure:** a check that answers a broader question than its name.
- **The proposed fix, NOT applied:** scope the assertion to the photo-credits section (the page after
  `<h2>Photo credits</h2>`). **Its predictions, frozen now:**
  - with OPERA's line present, the scoped assertion **passes** for the public-domain entry;
  - **its control:** the same scoped assertion **fails** for an entry licensed `CC-BY-SA-4.0`, which shows it can
    still catch what it exists for.
- **Until the ruling,** the proxy suite on this branch is red on that one test.

## The proxy deploy that carries #521 and #522, then R3: predictions, frozen before either (2026-11-04)

**The ruling that asks for them (reviewer chat, pasted by Daniel, 2026-11-04 ~19:15Z, recorded verbatim in
`docs/uk-data.md` A3.19 on `docs/uk-data-r1`):** "that deploy (a 'carried by the deploy' row, as #513 did; the smoke as
the test identity, now 38 checks with the OPERA line passing; cref's /wx unchanged with no X-Wx-Max-Age; what /wx does
for opuk before R3), then R3's. Nothing deploys before my ruling on them."

**Read before freezing (state, not predictions):**
- **Production's `/healthz`:** commit `31cb44c` (2026-10-30). It already runs R2 (#499 is an ancestor), so `wx` already
  holds `opuk` with `frameAgeS: null, coveredShare: null`, and a UK `/wx/cref` already answers 503 `wx_stale`.
- **Staging:** `c06dc17`.
- **Main:** `e8dfa45`. C: holds 15.5 GB free, the reviewer's reading at 19:13Z.

### The proxy deploy (staging, then production, by `deploy.sh`)

| | check | predicted |
|---|---|---|
| **PD0** | **carried by the deploy:** every commit from production's `31cb44c` to the commit deployed that changes `proxy/src` or `proxy/wrangler.toml`, listed at deploy time | **exactly three:** **#512 (`a27e00a`, Basemap D3):** the basemap credits on /credits, and a STAGING-ONLY `BASEMAP` binding; **#521 (`1beba4e`):** OPERA's line on /credits; **#522 (`e8dfa45`):** `X-Wx-Max-Age`. Any other means stop and ask, because its own predictions then apply |
| **PD1** | `smoke-prod.sh` on production **before** the deploy, as the test identity | **38 checks: 37 PASS, 1 FAIL.** The FAIL is exactly `/credits carries the OPERA line` (marker MISSING): this run is the check's control, proving it can see absence. The opuk covered-share verdict reads PASS, "not ingesting yet" |
| **PD2** | `smoke-prod.sh` on production **after** the deploy, as the test identity | **38 checks: 38 PASS, 0 FAIL,** the OPERA line among them |
| **PD3** | `cref`'s `/wx` on production, before and after: `/wx/cref` at Bend (smoke's query) and at KMCI, as the test identity | **unchanged:** 200, `WXC1`, an ETag naming `cref`, **no `X-Wx-Max-Age`**, both times |
| **PD4** | `/wx` for `opuk` before R3, after the deploy: `/wx/cref` at LHR, `/wx/opuk` at LHR, and `/healthz` | **503 `wx_stale` on both, with no `X-Wx-Max-Age`** (an error carries none); `/healthz` `wx.opuk` is `{frameAgeS: null, coveredShare: null}`. The same as today: no `opuk` frame exists until R3 |
| **PD5** | the deploy itself | **`/healthz` reports the deployed commit on staging and on production.** Production's bindings equal those of the version it replaces (#512's binding is staging's only). The rollback target is read and recorded before the deploy |
| **PD6** | the fleet in the 30 min after production's deploy, E2:08 and the test identity excluded, read twice (`n6_fleet.py`, as N6) | **fleet 4xx 0;** fleet 5xx other than `/blips` 503s with no upstream **0**; those 503s **≤ 1 %** of fleet `/blips`. The two reads agree |

- **#512 is another session's stage.** Its commit says "D3's deploy and verification wait for the staging test
  identity", and its predictions are `docs/basemap-deploy.md`'s.
  - Deploying main carries it, so **this deploy waits for the reviewer's ruling on the order with the basemap
    session's D3.**
  - I do not direct that session.
- **The escape:** anything else means stop. In production, roll back first.

### R3: the `opuk` ingest's production deploy (after the proxy deploy, and after its own ruling)

**No staging ingest Worker exists** (`wx-ingest.wrangler.toml` has no environments). So "staged" means this order, each
gate read before the next step:
1. the cost statement;
2. the deploy;
3. **`cref`'s gate at +5 min.** If it misses, roll back at once;
4. `opuk`'s first frame;
5. `/wx`;
6. pruning at +70 min;
7. CPU at +2 h.

**The deploy runs only at a time the reviewer has confirmed is outside every announced board window:** the ingest is
shared with `cref`, which E2:08 reads.

| | check | predicted |
|---|---|---|
| **R3-0** | **the cost statement, re-read before the deploy** (a new read-only instrument on the GraphQL analytics API: Workers `cpuTimeUs` by `scriptName`, and R2 operations by `actionType`, over the last 30 days), against §5's increments: CPU +4-8M ms a month (K11 × I7's factor of 2, plus empty checks), Class A +26k, Class B +430k | **projected overage under $2 a month**, Daniel's allowance, with the figure stated. Over $2 means stop and ask Daniel |
| **R3-1** | the deploy: `npx wrangler deploy -c wx-ingest.wrangler.toml` from clean main | a new version; the rollback target (the current version) read and recorded before |
| **R3-2** | **`cref`'s gate:** `/healthz` `wx.cref.frameAgeS` at +5, +10, +20, +30 and +60 min; `cref`'s `wx_ingest` events in Workers Logs over the first 30 min | **`frameAgeS` ≤ 600 at every read;** `cref`'s `:00` and `:30` checks both log in every minute (minutes where NOAA's own errors log `wx_ingest_error` are counted and printed). **A miss means roll back at once** |
| **R3-3** | `opuk`'s first frame: `/healthz` `wx.opuk` at +15 min | `frameAgeS` non-null and ≤ 1,800; **`coveredShare` in [0.70, 0.80]** (K9 read 0.7508; the week's footprint moved by one pixel in about 2.2 M) |
| **R3-4** | `/wx/cref` at LHR and at KMCI, as the test identity, after R3-3 | **LHR:** 200, `WXC1`, an ETag naming `opuk`, **`X-Wx-Max-Age: 720`**, `X-Wx-Frame` = `opuk`'s latest epoch. **KMCI:** as PD3 |
| **R3-5** | pruning (Q5), at +70 min: a listing of `wx/opuk/frames/`, read twice 10 min apart; and the logs' `pruned` field | **≤ 13 frames, none older than 3,600 s** except the one `latest` names. `pruned` appears only on checks that stored or backfilled a frame |
| **R3-6** | CPU, `workersInvocationsAdaptive` for `blipscope-wx-ingest`, the first 2 h | **0 CPU-limit errors** (`cpu_ms` 5,000); **invocation p99 ≤ 4,000 ms** (I7 read 2,172 ms for `cref` alone; `opuk` adds one frame build in five minutes, about 0.5-0.8 s in workerd) |
| **R3-7** | CloudFerro's answers in the first 2 h (Workers Logs: `opuk`'s `wx_ingest_error` lines) | printed, not graded: the count, the statuses, and the attempts |
| **R3-8** | the fleet in the 30 min after R3's deploy, as PD6 | as PD6. No shipped firmware reads `/wx`, so `opuk` cannot reach the fleet |

- **The escape:** anything else means stop. In production, roll the ingest back first.
- **R4 follows:** smoke's UK check at a public airport, asserting `opuk` and `X-Wx-Max-Age: 720`.

### The ruling on PD0-PD6 and R3, verbatim (reviewer chat, pasted by Daniel, 2026-11-04 ~20:00Z)

> 1. Main's CI at 1beba4e: two timeouts are an instrument failure, neither a pass nor a code red. Record both attempts
>    (that run and the re-run). The deploy gates on e8dfa45's own run, the last commit that changes proxy/, finishing
>    green. 46bea31 (#526) changed nothing under proxy/.
> 2. The timeouts get a fix, not only a re-run. K4' also hit vitest's 5 s default in my own run earlier today, so it sits
>    near the limit. Small PR, predictions first: each of the two tests' durations, read from CI's logs on both legs,
>    and an explicit timeout with margin for each (or a faster test). Their checks and sabotages are unchanged.
> 3. PD0-PD6: accepted as frozen. The deploy commit: 46bea31 if e8dfa45's CI finishes green; otherwise your timeout
>    fix's merge commit (it changes only tests, so PD0 stays exactly #512, #521 and #522). Use that commit, not main at
>    deploy time. The order: the map server deploys D3 (staging) from the same commit first; your deploy, staging then
>    production, waits for D3's report through Daniel.
> 4. R3-0 to R3-8: accepted as frozen. R3 runs after your PD report, at a slot you propose in it; I confirm the slot is
>    outside every announced board window before you start. Put R3-0's cost figure in that report too: over $2 a month
>    means stop and ask Daniel.

**Main's CI at `1beba4e`, both attempts (run 41897250440):**
- **Attempt 1:** `workers` / `test (proxy)` failed on two 5 s timeouts, with no wrong answer (867 passed, 2 timed
  out, 1 skipped): `test/wx-opera.test.ts:432` (K4′'s sabotage) and `test/wx-crop.test.ts:42` (WS2's 200 crops).
  `test (dashboard)` and `manifest-drift` passed.
- **Attempt 2** (the failed jobs re-run): `test (proxy)` passed. Final: 41 check-runs, 39 success, 2 skipped, 0 failed.
- **Per the ruling,** this was an instrument failure: neither a pass nor a code red.

**`e8dfa45`'s own run:** 41 check-runs, 39 success, 2 skipped (`promote`, `version`), 0 failed, on the first
attempt. **So the deploy commit is `46bea31`** (#526; `git diff e8dfa45 46bea31 -- proxy/` is empty).

### The ruling that replaces the ~20:00Z block, verbatim (reviewer chat, pasted by Daniel, 2026-11-04 ~20:15Z)

> Records accepted: main's CI at 1beba4e (the first run's two timeouts, an instrument failure, and the re-run's pass,
> both kept); e8dfa45: 41 check runs, 39 success, 2 skipped, 0 failed.
>
> 1. PD0-PD6 and R3-0 to R3-8: accepted as frozen.
> 2. The commit: 46bea31 (main now: #526 on top of e8dfa45; nothing under proxy/ changed), once its own CI reads green.
>    Not main at deploy time. PD0 stays exactly #512, #521 and #522.
> 3. The order: the map server deploys D3 (staging) from 46bea31 first. Start PD once Daniel pastes you D3's report
>    reading as predicted; if D3 is off, wait for my ruling. PD's staging step may run as soon as D3 is done; its
>    production step not before 21:30Z, when the board hold starts.
> 4. R3: inside the hold, as soon as every PD row has read as predicted, with no further go from me. Anything off in PD
>    means no R3: stop and report. Read R3-0's figure before the deploy: over $2 a month means stop and ask Daniel.
>    If R3 hasn't started by 23:30Z, don't start it: report, and I'll set a new slot.
> 5. The K4' timeout fix goes ahead, predictions first: each test's duration from CI's logs on both legs, and an
>    explicit timeout with margin (or a faster test); their checks and sabotages unchanged. It doesn't hold up the
>    deploy.

### R3-0, read before any deploy (2026-11-04 ~20:06Z, `tools/wx/cost_read.py`): UNDER $2 by either method

**As frozen, the last 30 days (10-05 to 11-04):**
- **Workers CPU 17.25 M ms:** `blipscope-wx-ingest` 13.06 M, `blipscope-proxy` 4.18 M.
- **R2:** Class A 742 k; Class B 2.77 M.
- **The overage, with `opuk`'s increment, is $0.00.**

**Why that sum understates a month.** The ingest went live on 10-23, so it ran for about 12 of the 30 days. **So the
projection is also made from the run rate:** the last 7 full days × 30/7. That is a correction to how I project, added
before the figure was reported.
- **CPU 36.0 M ms a month.** The ingest runs at 0.96 M a day, so the account is already past the 30 M included,
  without `opuk`.
- **Class A 976 k, Class B 5.42 M.**
- **With `opuk`'s increment** (CPU +5.1-7.8 M, Class A +26 k, Class B +430 k): **overage $0.23-0.28 a month.**

**Verdict: under Daniel's $2 a month by either method.** Class A sits near its 1 M allowance at the run rate, with
`opuk` adding 26 k.

**The read's controls:**
- the script totals (13.06 M + 4.18 M + small) equal the account sum;
- unclassed R2 actions are printed, not dropped: `GetBucketNotificationConfiguration` 6 and
  `GetBucketSippyConfiguration` 3.

### PD1, read before the deploy (2026-11-04 20:08:16Z): as predicted

- **Production was at `31cb44c`** (`/healthz`, read just before).
- **The whole `smoke-prod.sh`, as the test identity, from the deploy checkout at `46bea31`:** **38 checks, PASS 37,
  FAIL 1, SKIPPED 0.**
  - The one FAIL is exactly **`/credits carries the OPERA line`** (marker MISSING), the check's own control.
  - The `opuk` covered-share verdict reads **PASS, "not ingesting yet: no opuk frame"**.
- **The first attempt, 20:08:02Z, was refused before any request** ("BLIP_KEY is set in this environment"): the test
  identity's guard firing on the user-level `BLIP_KEY`. Re-run with `BLIP_KEY` and `BLIP_DEVICE` unset for that one
  command.
- **The log holds no key-shaped value** (0 runs of 64 hex). The one line naming `X-Blip-Key` is the script's banner,
  "value never printed".

**Rollback targets, read before any deploy (`wrangler deployments list`):**
- proxy production `2eb4aa5e-512a-4a8a-b586-8a77bfdfca4e` (10-30, `31cb44c`);
- proxy staging `c7fa5f84-7342-49bd-8b77-12e09fe34bc8` (11-04 04:49Z);
- `blipscope-wx-ingest` `582612da-e1b0-4a02-bf9e-de72c5d78157` (#472, 10-23: the pre-R2 ingest, `cref` only).

**The deploy checkout:** a detached worktree at `46bea3104866`, with `proxy/` clean. Wrangler is on its OAuth login;
`CLOUDFLARE_API_TOKEN` is absent at user level and in the shell.

### The ruling of ~20:25Z, verbatim (reviewer chat, pasted by Daniel, 2026-11-04)

> Accepted: 46bea31's CI green (38, 0 failed); R3-0 under $2 a month on both readings (the 7-day run rate,
> $0.23-0.28, is the right basis since the ingest went live 10-23; your doc's correction stands); PD1 as predicted
> (37 PASS, OPERA the one FAIL, opuk "not ingesting yet"); the rollback targets recorded.
>
> 1. #529 at 995f71fe9b46: approved if TO4 reads as predicted from the finished run (41 check runs: 38 success,
>    3 skipped, 0 failed). Then squash with --match-head-commit, no --admin, and record main's CI on the merge commit.
>    It changes only tests and a tool, so the deploy commit stays 46bea31.
> 2. BLIP_KEY in your environment: the guard refusing it was the guard working, and unsetting it for one command was
>    right. But nothing on main reads BLIP_KEY or BLIP_DEVICE any more, so a board key shouldn't be in any environment.
>    Find where it comes from, read-only, names only: a Windows user or system variable, a profile file, or your
>    launcher. Report the source, and whether BLIP_DEVICE is set the same way. Print no value. Daniel removes it at the
>    source. Revoking is his decision; I don't recommend it unless a value has left the PC.
> 3. D3's report is still with Daniel. Your timing stands as you list it.

### The ruling of ~20:55Z, verbatim (reviewer chat, pasted by Daniel, 2026-11-04): one combined deploy

> Recorded: #529 merged as 78e16c5, main's CI 41 (39/2/0) on the first attempt; K4' 4,505 ms.
>
> #525 (the map server's /credits fix) merged before your deploy, so it's one combined deploy:
> 1. The commit: f740f10 (main's tip), once main's CI on it reads green. Not main at deploy time. It replaces 46bea31.
> 2. PD0 is re-registered by this ruling to exactly four: #512, #521, #522 and #525. PD1 stands as read (production
>    hasn't changed); PD2-PD6 stand.
> 3. New PD7, registered before the deploy: #525 on production's live /credits, as docs/credits-correctness.md's
>    after-merge row predicts (each licence label as often as the manifest's rows hold it; adsbdb 0 times;
>    standing-data once; the ShareAlike notice on 114 rows). If PD7 is off: report it, no rollback for that alone (PD2
>    covers the page rendering), and R3 may still go if PD1-PD6 read as predicted.
> 4. The order stands: the map server's D3 (staging, from f740f10) first; start PD when Daniel pastes you D3's report
>    reading as predicted; production not before 21:30Z; R3 inside the hold, started by 23:30Z.

**Checked against `f740f10` before the deploy:**
- **PD0's list, as re-registered:** from production's `31cb44c` to `f740f10`, the commits that change `proxy/src` or
  `proxy/wrangler.toml` are exactly four: `a27e00a` (#512), `1beba4e` (#521), `e8dfa45` (#522) and `f740f10` (#525).
- **The two credit lines survive #525 byte for byte:** OPERA's line once in the template and once in smoke; the NOAA
  line once. `proxy/package-lock.json` is unchanged from `46bea31`.

**PD7's counting, pinned from the renderer before the deploy** (`renderCreditsHtml` at `f740f10`).
- **Why a raw count would mislead:**
  - every BY-SA row prints its licence link twice (once as the row's licence, once at the end of its ShareAlike
    clause);
  - OPERA's data-source line holds "CC BY 4.0";
  - "CC BY-SA 3.0" is a prefix of "CC BY-SA 3.0 AU".
- **So PD7 reads the live page's photo-credits section** (after `<h2>Photo credits</h2>`), row by row. Each `<li>`'s
  own licence is the first label after `) &middot; `.
- **Predicted, as `docs/credits-correctness.md`'s table and after-merge row give them:**
  - CC BY-SA 4.0: 48; CC BY-SA 2.0: 36; CC BY-SA 3.0: 18; CC BY-SA 3.0 AU: 12;
  - CC BY 2.0: 32; CC BY 4.0: 24; CC BY 3.0: 3; CC BY 2.5: 2;
  - Open Government Licence v1.0: 1;
  - 176 licensed rows in all.
- **Also predicted:**
  - "adsbdb" **0** times anywhere on the page;
  - "Route data from vradarserver/standing-data" exactly **once**;
  - the per-row ShareAlike clause on exactly **114** rows, each naming its own row's licence;
  - the ShareAlike paragraph exactly once.
- **If the published manifest has changed since that table,** its own counts are printed beside the table's, read
  read-only with `--remote`.

**PD7's instrument, `tools/wx/credits_pd7.py`, proved before the deploy:**
- **On #525's own render** (`proxy/photos/credits.html` at `f740f10`): **every pinned count as predicted.**
  - 9 labels, 176 licensed rows, 61 "Public domain";
  - 0 "adsbdb", the standing-data line once;
  - the clause on 114 rows, each naming its own licence;
  - the paragraph once.
- **Its control, today's live production** (which predates #525): **not as predicted.** 175 licensed rows under the
  old labels, "adsbdb" twice, no standing-data line, no ShareAlike clause. The instrument tells the two pages apart.
- **One defect, found by that dry run and fixed before any live reading:** it first searched for the plain phrase
  "Route data from vradarserver/standing-data". The template links the name, so the plain phrase never appears, and it
  read 0. It now matches the line by its own anchor (`Route data from <a href="https://github.com/vradarserver/standing-data"`).

**The deploy commit's own CI, `f740f10`, counted from the finished runs (21:06:51Z): green.**
- 45 check-runs, all completed: 43 success, 2 skipped (`promote`, `version`), 0 failed.
- All six workflows succeeded: `workers`, `firmware`, `device-ids`, `no-traffic-captures`, `photos` and
  `photo-drift`. The last two run because #525 touches `proxy/photos/`.
- The deploy checkout is a detached worktree at `f740f10b9c08`, with `proxy/` clean.

### The go, verbatim (reviewer chat, pasted by Daniel, 2026-11-04 ~21:25Z)

> D3 is held up by a smoke defect (#532) that only matters once staging has the basemap binding, so your deploy no
> longer waits for it:
> 1. Start PD now, from f740f10, once its CI reads green: staging first, production not before 21:30Z. Your staging
>    step carries #512's staging BASEMAP binding, as PD5 says; the map server reads D3's rows on that deploy afterwards.
> 2. Run no smoke against staging after your staging step: its /basemap check is the defect #532 fixes. PD's rows
>    don't need one.
> 3. Everything else as ruled at ~20:55Z: PD0 (four), PD1-PD7, R3 inside the hold after a clean PD, started by 23:30Z.
> 4. Tell Daniel the time of your staging step (UTC): the map server needs it.

### PD: the staging step (2026-11-04 21:21:06-21:21:16Z), and the controls read before production

**Staging, by `deploy.sh staging` from the clean checkout at `f740f10`:** exit 0.
- **New version `cc4dcf89-7d85-465c-8b3f-de29a0dfdc19`;** `confirm-deploy.sh`: "/healthz reports commit=f740f10".
- **#512's binding is on it:** `env.BASEMAP (blipscope-basemap-staging)`.
- **The rollback target, read just before:** `c7fa5f84` (staging was at `c06dc17`).
- **`deploy.sh`'s note:** `f740f10` is 2 commits behind main, neither touching `proxy/`. `f740f10` was deployed as
  ruled.
- **No smoke was run against staging,** as ruled (#532).

**Read before production, on production (still `31cb44c`):**
- **The `/wx` probe** (`tools/wx/pd_wx.sh`, as the test identity):
  - `/wx/cref` at Bend (smoke's query) and at KMCI: 200, `WXC1`, ETag `cref`, no `X-Wx-Max-Age`, `X-Wx-Frame` present;
  - `/wx/cref` and `/wx/opuk` at LHR: 503 `wx_stale`, no header;
  - `/healthz` `wx.opuk` `{frameAgeS: null, coveredShare: null}`.
  - That is PD3's and PD4's shape, before the deploy.
- **Production's bindings,** from `wrangler versions view 2eb4aa5e`, are saved for PD5's comparison: Durable Objects,
  `ENRICH_KV`, `METRICS`, `WX`, and the `[vars]`.
- **PD6's instrument** (`tools/wx/pd6_fleet.py`, N6's with both E2:08 and the test identity excluded), on a control
  window of 20:30-21:00Z: 1,726 fleet requests, 11 devices, 1,182 `/blips`, 4xx 0, 5xx 0. Readable.

### PD: the production step (2026-11-04 21:30:36-21:31:14Z), and the readings

**Production, by `deploy.sh production` from the clean checkout at `f740f10`:** exit 0.
- **New version `504ecc43-dfe8-4a26-bc76-24da0ba13819`;** `confirm-deploy.sh`: "/healthz reports commit=f740f10".
- **The rollback target:** `2eb4aa5e` (read just before; production was at `31cb44c`).
- **Settled before any reading:** `/healthz` reported `f740f10` three times, 30 s apart (21:31:34, 21:32:04,
  21:32:35Z).

| | got | verdict |
|---|---|---|
| **PD0** | carried: exactly `a27e00a` (#512), `1beba4e` (#521), `e8dfa45` (#522) and `f740f10` (#525) | as predicted |
| **PD1** | read before: 38 checks, 37 PASS, the one FAIL the OPERA line | as predicted (20:08Z) |
| **PD2** | `smoke-prod.sh` on production, as the test identity, 21:32:45Z: **PASS 38, FAIL 0, SKIPPED 0**. "/credits carries the OPERA line": marker present, PASS. The `opuk` verdict: PASS, "not ingesting yet" | as predicted |
| **PD3** | `/wx/cref` at Bend and at KMCI: 200, `WXC1`, ETag `cref`, **no `X-Wx-Max-Age`**, `X-Wx-Frame` present. The same as before the deploy | as predicted |
| **PD4** | `/wx/cref` and `/wx/opuk` at LHR: **503 `wx_stale`, no `X-Wx-Max-Age`**; `/healthz` `wx.opuk` `{frameAgeS: null, coveredShare: null}` | as predicted |
| **PD5** | `/healthz` reports `f740f10` on staging (21:21Z) and on production. Production's bindings, from `wrangler versions view`, are **identical, all 30 lines**, to those of the version replaced (`2eb4aa5e`). The rollback targets were recorded before each step | as predicted |
| **PD7** | the live `/credits`, 21:33:08Z: 237 rows, **176 licensed, every label at its predicted count** (CC BY-SA 4.0 48, 2.0 36, 3.0 18, 3.0 AU 12; CC BY 2.0 32, 4.0 24, 3.0 3, 2.5 2; OGL v1.0 1), 61 public domain; "adsbdb" **0**; the standing-data line **once**; the ShareAlike clause on **114** rows, all 114 naming their own licence; the paragraph **once** | as predicted |
| PD6 | the fleet, 21:31:14-22:01:14Z, read twice after the window closes | pending |

### R3's instruments, proved before R3 (2026-11-04 ~21:35Z): two findings that need a ruling

**`tools/wx/r3_reads.py` (logs, CPU) and `tools/wx/r3_list.py` (the bucket listing), each tried on today's `cref`-only
ingest:**
- **CPU (R3-6), 19:00-21:00Z:** 125 invocations, all `success`, 0 errors. **p50 734 ms, p99 2,711 ms.** The bar of
  4,000 ms leaves about 1.3 s for `opuk`.
- **Logs (R3-2, R3-5's log half, R3-7):** unsampled (ABR level 1) in 1-hour windows. 0 `opuk` events before R3.

**Finding 1 -- R3-2's log half, as frozen, can fail on today's baseline.**
- **The frozen row:** "`cref`'s `:00` and `:30` checks both log in every minute", with "a miss means roll back at
  once".
- **The baseline, 18:30-21:30Z, before any R3 change:** both checks logged in **178 of 180 minutes**, with 0 error
  lines. At 21:15Z the `:30` check left no line, while its `:00` check (2,187 ms) and both neighbours did.
- **Why it cannot be read from here:** whether a missing line is a check that did not run or a line the logs dropped.
  `/healthz`'s `frameAgeS` is the freshness gate either way.
- **A restatement, proposed, NOT applied:**
  - R3-2's gate stays `frameAgeS` ≤ 600 at every read;
  - the log half becomes: in the first 30 min, **at most 2 minutes missing one check, and no minute missing both**
    (the baseline's rate: 2 in 180).

**Finding 2 -- R3-5's listing cannot be read: the R2 read keys are refused.**
- **The finding:** `r2-wx-read-id` and `r2-wx-read-secret` get `401 Unauthorized` on `blipscope-wx`, for `cref`'s
  frames as well as `opuk`'s.
- **The alternative, a fault in my signer, is excluded:** the same signing chain reproduces AWS's published SigV4
  example for ListObjects exactly, and R2 says "Unauthorized", not "SignatureDoesNotMatch".
- **The keys' shape is as expected** (a 32-character id, a 64-hex secret). They have most likely expired or been
  revoked since the 10-23 reads.
- **Without new keys, R3-5 reads BLIND, which counts as FAIL.**

**PD6** is read once its window (21:31:14-22:01:14Z) has closed.

### The block of ~21:30Z, verbatim (reviewer chat, pasted by Daniel, 2026-11-04; it replaces the ~21:25Z block)

> Recorded: f740f10's CI green (45 checks, 43 passed, 2 skipped, 0 failed).
>
> D3 is held up by a smoke defect (#532) that only matters once staging has the basemap binding, so your deploy no
> longer waits for D3:
> 1. Start PD now, from f740f10: staging first, production not before 21:30Z. Your staging step carries #512's staging
>    BASEMAP binding, as PD5 says; the map server reads D3's rows on that deploy afterwards.
> 2. Run no smoke against staging after your staging step: its /basemap check is the defect #532 fixes. PD's rows
>    don't need one.
> 3. Everything else as ruled at ~20:55Z: PD0 (four), PD1-PD7, and R3 inside the hold after a clean PD, started by
>    23:30Z.
> 4. Tell Daniel the time of your staging step (UTC): the map server needs it.

**Pasted after the steps it covers, and the same in substance as the ~21:25Z block they ran under:**
- staging at 21:21:06Z, from `f740f10`, with the `BASEMAP` binding, and no staging smoke;
- production from 21:30:36Z, not before 21:30Z;
- the staging time given to Daniel in the report.

Nothing in it changes a step taken.

### R3 re-registered before it starts, verbatim (reviewer chat, pasted by Daniel, 2026-11-04 ~21:55Z)

> PD so far: accepted (staging 21:21:06Z, production 21:30:36Z on f740f10; PD0-PD5 and PD7 as predicted). PD6 as
> registered, after its window.
>
> R3, before it starts, re-registered by ruling (the frozen rows kept beside them):
> 1. R3-2: your restatement. The gate stays cref's frameAgeS <= 600 at every read. The log half: in the first 30 min,
>    at most 2 minutes missing one of cref's checks and no minute missing both, as today's baseline (178/180). Worse
>    than that is off, with the escape as registered.
> 2. R3-5 splits. R3-5a, the logs' pruned field at +70 min: as registered. R3-5b, the listing: read once Daniel's new
>    read keys are in the token file; at +70 min if they're there by then, otherwise as soon as they are. Until it's
>    read it is unread, not a pass, and it doesn't gate R3.
> 3. R3-6: 0 CPU-limit errors stays the gate (any error: roll back). The p99 is still graded against 4,000 ms, but with
>    cref alone at 2,711 ms, a p99 between 4,000 and 5,000 ms with no CPU-limit error is reported, not rolled back.
> 4. Start R3 when PD6 reads clean, inside the hold, started by 23:30Z.

### The new R2 read keys, shown to work (2026-11-04 21:58:09Z)

**The ruling (reviewer chat, pasted by Daniel, ~22:10Z), verbatim:**
> Daniel has put new R2 read keys (Object Read only, blipscope-wx) in the token file as r2-wx-read-id and
> r2-wx-read-secret. Before R3-5b, show they work: list cref's frames (a count and the newest one's age; no key printed).
> If that reads, R3-5b goes back to +70 min, as first registered. If it still answers 401, stop and report; R3 itself
> isn't held by it. Also, booleans only: are blip-key and blip-device still in the file?

**Read:**
- **`tools/wx/r3_list.py wx/cref/frames/`:** **30 frames, the newest 89 s old, the oldest 3,571 s.** That is `cref`'s
  hour at its 2-minute cadence, the shape I5's readings knew.
- **So R3-5b returns to +70 min,** as first registered.
- **The token file, as booleans:** `blip-key` absent; `blip-device` absent.

**PD6, read twice after its window (21:31:14-22:01:14Z), at 22:04:19Z and 22:07:01Z: as predicted.**
- Both reads are identical: 1,582 fleet requests, 11 devices, 1,053 `/blips`;
- fleet 4xx **0**; 5xx other than no-upstream `/blips` 503s **0**; those 503s **0** (0 % of `/blips`);
- E2:08 and the test identity excluded.

**Every PD row read as predicted (PD0-PD7). So R3 started inside the hold, with no further go,** as ruled.

### R3 (2026-11-04, from 22:07:22Z)

- **R3-0:** read before any deploy (20:06Z): under $2 a month on both readings.
- **R3-1:** `npx wrangler deploy -c wx-ingest.wrangler.toml` from the clean checkout at `f740f10`, 22:07:22-22:07:26Z,
  exit 0.
  - New version **`d37d9a21-7587-437b-bb1e-563462a0ffea`**: binding `WX` (`blipscope-wx`), cron `* * * * *`.
  - **The rollback target, read just before:** `582612da-e1b0-4a02-bf9e-de72c5d78157`.
- **The timed reads** (`tools/r3_schedule.sh`) run at +5, +10, +15, +20, +30 and +60 min, with the `/wx` probe at +15
  and +16 min.

**Pinned at 22:10Z, before R3-2's log half is read: "the first 30 min" are the 30 full minutes after the deploy minute,
22:08:00-22:38:00Z.**
- The deploy minute (22:07) is excluded: its `:00` check ran at 22:07:06Z, before the deploy at 22:07:22Z, and would
  count as a missing check.
- The rule is the one ruled at ~21:55Z: at most 2 minutes missing one of `cref`'s checks, and no minute missing both.

**An early look at +3 min (22:10:24Z), not a graded row:**
- `opuk` logged 5 checks: stored 2, backfilled 2, already-stored 1, **0 errors**.
- `pruned` appears only on the storing and backfilling checks.

**R3, the first 15 minutes (`tools/r3_schedule.sh`):**

| | got | verdict so far |
|---|---|---|
| **R3-2's gate** | `cref` `frameAgeS` **104 s** (+5, 22:12:25Z), **163 s** (+10), **108 s** (+15) | as predicted so far (+20, +30, +60 still to read) |
| **R3-3** | at +15 (22:22:22Z): `opuk` `frameAgeS` **444 s**, `coveredShare` **0.75077**. It was already present at +5 (446 s, 0.75077) | as predicted |
| **R3-4** | at +15, as the test identity. `/wx/cref` at LHR: **200, `WXC1`, ETag `opuk`, `X-Wx-Max-Age: 720`, `X-Wx-Frame` 1793830500** (22:15:00Z), which is `opuk`'s latest (`/healthz`, seconds later: 448 s old). `/wx/opuk` at LHR: the same. Bend and KMCI: 200, `WXC1`, ETag `cref`, no `X-Wx-Max-Age`, as PD3 | as predicted |

**R3, to +30 min:**

| | got | verdict |
|---|---|---|
| **R3-2's gate** | `cref` `frameAgeS` 104 / 163 / 108 / **166** (+20) / **168** (+30) s | as predicted so far (+60 to read) |
| **R3-2's log half** (as ruled ~21:55Z; window pinned 22:08-22:38Z) | 30 minutes: **29 with both of `cref`'s checks**; one minute (22:36) is missing its `:00` check; **no minute is missing both**; 0 error lines (unsampled, ABR 1) | as predicted |
| **R3-8** | the fleet, 22:07:26-22:37:26Z, read at 22:40:51Z and 22:43:09Z, both identical: 1,701 requests, 12 devices, 1,116 `/blips`, **4xx 0, 5xx 0** | as predicted |

**Data beside the rows:** in those 30 minutes, `opuk` logged **59 checks, 0 errors** (stored 7, backfilled 2,
already-stored 50). `pruned` appears only on the 7 storing and 2 backfilling checks.

**Finding 3, before R3-5b is read: the frozen bound cannot hold at most moments, by design.** Written at 22:47Z,
before any listing of `wx/opuk/frames/`. It comes from the code (`pruneOld`, Q5) and the logs only.

- **Measured** (Workers Logs, `opuk`'s storing checks, 22:09:39-22:44:40Z): frames come every 300 s. Each is stored
  **251-291 s after its epoch** (8 checks in a row; the first three, at the deploy, ran 491-1,150 s because of the
  backfill). `pruned` is 0 on all of them, because no frame is an hour old yet.
- **The code:** `opuk` prunes only on a check that stores a frame (Q5, as ruled). It cuts epochs older than
  `now - 3,600` *at that moment*.
- **What follows:** if a frame of epoch E is stored at about E + 280 s, the cut is E - 3,320, so the oldest frame kept
  is E - 3,300, aged **3,580 s** at the prune. That frame passes 3,600 s about 20 s later and stays until the next
  storing check, about 300 s later. It is then **3,880 s** old.
  - So for about 280 of every 300 s, the oldest frame is **3,600-3,880 s** old.
- **R3-5b's frozen row,** "none older than 3,600 s except the one `latest` names", therefore reads as off at almost
  any moment. The exception doesn't help: `latest` names the newest frame.

**Expected at 23:17:40Z** (arithmetic, not a reading): the last storing check is about 23:14:40Z, for the 23:10 frame.
- **12 frames,** epochs 22:15:00-23:10:00Z; **oldest about 3,760 s.**
- The same at 23:27:40Z.
- The pre-deploy backfill (21:50, 21:55, 22:00) is pruned one per storing check from about 22:54:40Z on. So R3-5a
  should show `pruned` 1 on the storing checks from then on, and 0 before.

**Alternative, and why the frozen row can't separate it:** a prune that fails also leaves frames over 3,600 s old.
- The bound that can tell the two apart is the one `pruneOld` itself establishes: **every epoch is at least
  (the last storing check's time - 3,600 s)**, except the one `latest` names. The time comes from the logs.
- A prune that didn't run fails that bound, by at least one frame.

**Proposed for a ruling** (the frozen row stays beside it):
- **R3-5b′:** at most 13 frames (12 expected; 11 if a store lags past 300 s), and every epoch at least the last
  storing check's time minus 3,600 s, except `latest`'s.
- **Sabotage:** evaluate the same listing with the last storing time moved forward 300 s, which pretends a prune ran
  that didn't. It must FAIL on the oldest frame.

**Until ruled, R3-5b is not read.**
- It is read-only, so a later read loses nothing.
- Read against the frozen row, it would almost surely call a healthy prune off, and the escape would then roll back
  a healthy ingest.
- Unread is not a pass. R3-5a, R3-2's +60 gate, R3-6 and R3-7 go ahead as registered.

**Ruling, pasted ~23:00Z (verbatim):**

> R3 to +30 min: accepted as read (the gate 104-168 s; the log half 29/30 with 22:36 missing its :00, none missing
> both, 0 errors; R3-8 4xx 0 and 5xx 0 on two identical reads; opuk 59 checks, 0 errors).
>
> R3-5b: (a). The frozen bound was wrong for a prune that runs only on storing checks, and I missed it when I accepted
> R3-5. The miss is kept. Re-registered before any listing, as you wrote it at b19e600: R3-5b' is at most 13 frames
> (12 expected), and every epoch is at least the last storing check's time (from the logs) minus 3,600 s, except
> latest's. Its sabotage, the same listing judged with that time moved 300 s later, must FAIL on the oldest frame.
> R3-5b' off means stop and report, with no rollback for it alone: a failed prune costs storage, not service. Everything
> else as you list it.

**R3-5b′, pinned before any listing of `wx/opuk/frames/`** (none has been made):

- **"The last storing check's time" is the check's START**, `t0`. That is the clock `pruneOld` cuts from:
  `ingest-worker.ts` passes `Date.now()`, taken before `ingestOnce`, as `nowMs`. The log line is written after the
  prune and carries `ms = now - t0`.
  - So `t0 = event timestamp - ms`, cross-checked against `epoch + lagS - ms/1000` (to ±0.5 s from rounding).
  - Taking the log line's own time instead would make the bound stricter than the code by the check's duration.
    The margin is small: the oldest kept frame sits about 9-49 s above the cut at 251-291 s of write lag.
- **"Storing" means `status` `stored` or `backfilled`.** The last such check counts only if its log line was
  written no later than the listing.
- **Order:**
  1. the listing first, then `wx/opuk/latest`'s epoch (S3 GET, Object Read);
  2. the logs at least 150 s later, so the line has landed.
- **Control:** the log read must hold an ingest event written after the listing. Otherwise a late-landing storing
  line could be missing, so the read is BLIND, which counts as FAIL.
- **Expected per read:**
  - 12 frames (at most 13);
  - the oldest epoch is the last storing check's frame minus 3,300 s, sitting 9-49 s above the cut;
  - the sabotage (that time + 300 s) fails exactly 1 frame, the oldest.
- **Off, or BLIND:** stop and report, with no rollback for R3-5b′ alone (as ruled).
- **Instrument:** `tools/wx/r3_5b.py`. It is proved first on `cref`, whose prune runs on every check with the same
  `pruneOld`, so its pruning checks are the ones logging `pruned`. That is a control on the instrument, not a reading
  of R3-5b′.

**R3-5b′'s instrument, proved on `cref` before the `opuk` read** (a control, not a reading of R3-5b′):

| run | listing | last pruning check (start; cross-check) | bound | sabotage (+300 s) | verdict |
|---|---|---|---|---|---|
| 1, 22:56:43Z | 30 frames, 21:56:41-22:54:35Z | 22:56:35.233Z; the two starts agree to -0.4 s | 0 frames below the cut, margin 5.8 s | 3 below, the oldest among them | **FAIL, on the count alone** |
| 2, 22:59:37Z | 29 frames, 22:00:38-22:56:39Z | 22:59:05.975Z (`pruned` 1); +0.4 s | 0 below, margin 92.0 s | 2 below, the oldest among them | **PASS** |

- **Run 1 found an instrument defect.** It applied R3-5b′'s 13-frame cap, which is `opuk`'s, to `cref`, which keeps
  about 30 frames an hour. The cap is now scoped to `opuk` only (`MAX_FRAMES`). Nothing else changed.
- **Run 2 proves the instrument in both directions on one listing:** the bound passes, and the sabotage fails on the
  oldest frame.
- The two start-time derivations agree within the ±0.5 s rounding both times.
- 0 lines were BLIND (ABR level 1), with 8 and 10 ingest lines written after each listing.

**R3, +60 and +70:**

| | got | verdict |
|---|---|---|
| **R3-2's gate, +60** (23:07:25Z) | `cref` `frameAgeS` **163 s**. All six reads: 104 / 163 / 108 / 166 / 168 / 163 | as predicted |
| **R3-5a** (read 23:17:45Z, logs 22:07:22-23:17:22Z, ABR 1) | `opuk`: 138 checks, **0 errors**; `pruned` on **stored 15, backfilled 2**, and on no other status | as predicted |
| **R3-5b′, read 1** (listed 23:17:43Z) | **12 frames**, 22:15:00-23:10:00Z, `latest` 23:10:00Z. Last storing check started 23:14:05.730Z (cross-check -0.4 s), for the 23:10 frame, `pruned` 1. Cut 22:14:05Z: **0 frames below it**. Sabotage (+300 s, cut 22:19:05Z): **1 frame below, the oldest**. 8 ingest lines after the listing | **PASS** on the ruled row |

**Off my own written expectation, not the ruled row: the oldest frame's margin above the cut.**
- **Predicted** (f6bf0a3, "Expected per read"): 9-49 s. **Got: 54.3 s.**
- **Cause:** I took the 9-49 s from the write lag measured at the LOG LINE (251-291 s). The bound runs on the check's
  START, which is earlier by the check's duration.
  - That duration shows directly here: the 23:14 check logged at 23:14:14Z and started at 23:14:05.73Z, so about 8.3 s.
  - The start lag is therefore about 246 s, and the margin is 300 - 245.7 = 54.3 s.
  - This is the same log-time-versus-start distinction pinned for the bound itself; I didn't carry it into my own
    arithmetic.
- **Alternative:** a prune cutting from some other clock. The data doesn't single that out:
  - any cut between 22:10:00 and 22:15:00 keeps exactly this listing;
  - and the cross-check ties the start time to the logged `epoch + lagS - ms` within 0.4 s.
  - So the miss is in the expectation, not the prune.
- **Not changed:** read 2 (23:27:40Z) is compared against the same frozen expectation. A corrected range waits for a
  ruling.

**Data beside the rows:**
- **My side expectation for R3-5a held:** `pruned` 1 on every storing check from 22:54:49Z on, 0 before. The three
  pre-deploy frames aged out one per check.
- **The log half over 70 minutes** (graded only over the first 30): 68 of 70 minutes have both of `cref`'s checks,
  with 0 error lines.

| | got | verdict |
|---|---|---|
| **R3-5b′, read 2** (listed 23:27:44Z, 10 min after read 1) | **12 frames**, 22:25:00-23:20:00Z, `latest` 23:20:00Z. Last storing check started 23:24:35.233Z (cross-check +0.4 s), for the 23:20 frame, `pruned` 1. Cut 22:24:35Z: **0 below**. Sabotage (cut 22:29:35Z): **1 below, the oldest**. Margin **24.8 s**, inside the frozen 9-49 s | **PASS** on the ruled row |

**R3-5b′ overall:** both reads pass. The listing moved forward by exactly two frames between the reads (22:15→22:25
oldest, 23:10→23:20 newest), at 12 frames each time. The one miss is my margin expectation on read 1 (54.3 s),
recorded above with its cause.

**Ruling, pasted ~23:40Z (verbatim):**

> Accepted as read: R3-2's gate at all six reads (104-168 s); R3-5a as predicted (pruned only on the 15 stored and 2
> backfilled checks); R3-5b' PASS on both reads, 12 frames each, 0 below the cut, the sabotage 1 below on the oldest.
> The instrument's first run on cref (the cap applied to the wrong product) was its proving run, recorded, not a
> reading.
>
> Your margin expectation: the miss is yours and recorded with its cause (log time against the check's start). No
> corrected range is needed: the ruled row is the bound, and the margin is printed, not graded.
>
> Carry on: R3-6 and R3-7 at 00:12Z and 00:17Z, then the full R3 report. R4's predictions (smoke's UK check) come
> after it.

**R3, +2 h: R3-6 is off, and the ingest is ROLLED BACK.**

| | got | verdict |
|---|---|---|
| **R3-6** (`workersInvocationsAdaptive`, 22:07:22-00:07:22Z; read at 00:12:04Z and 00:17:09Z, identical) | 127 invocations, all `success`, **0 errors (0 CPU-limit errors)**; CPU total 140,267 ms; **p50 738.4 ms, p99 6,020.0 ms** | **OFF.** The gate (0 CPU-limit errors) holds, but the p99 is over 4,000 ms (graded) and over the ruled 5,000 ms ceiling of the report-only band |
| **R3-7** (Workers Logs, the same window, ABR 1) | `opuk`: 237 checks, **0 `wx_ingest_error` lines** (already-stored 210, stored 25, backfilled 2) | printed, not graded |

**The escape, as registered: rolled back first.**
- Before: `wrangler deployments status` showed `d37d9a21` at 100%, unchanged since R3-1.
- `npx wrangler rollback 582612da-e1b0-4a02-bf9e-de72c5d78157 -c wx-ingest.wrangler.toml -m "..." -y` from `wt-deploy`
  ran **00:18:31-00:18:34Z, exit 0, empty stderr.** The deployment is dated 00:18:33.814Z, with `582612da` at 100%.
- **After:**
  - The last `opuk` check **started at 00:18:35.233Z**. It was the `:30` check of the 00:18 invocation, which began
    under `d37d9a21` before the rollback. It logged at 00:19:08Z.
  - No `opuk` event since, through 00:24:29Z.
  - `cref`: 5 of 5 minutes with both checks, 0 error lines (00:19-00:24Z).
  - `/healthz` `cref` `frameAgeS` read 134 s (00:18:50Z) and 173 s (00:25:32Z).
  - `opuk` `frameAgeS` keeps growing (1,233 s at 00:25:32Z), as it must with no `opuk` ingest.

**Where the p99 came from** (read-only, per-minute CPU, after the rollback):

| minute | CPU (ms) | what ran |
|---|---|---|
| 23:19Z | 6,072.8 | an `opuk` storing minute |
| 23:29Z | 6,020.0 | an `opuk` storing minute |
| 22:19Z | 5,426.5 | an `opuk` storing minute |
| next: 22:39, 23:39, 22:49Z | 3,772.7 / 3,579.8 / 3,325.8 | storing minutes |

- So the cost is `opuk`'s frame build at steady state, not the deploy's backfill transient: the backfill minutes
  (22:08-22:09Z) read 2,512.6 ms at most.
- **Baseline, `cref` alone, 19:00-21:00Z** (same instrument): the worst minute was 3,621.1 ms; none over 4,000.
- So one `opuk` build adds up to about 2-3 s of CPU in workerd, against R3-6's assumed 0.5-0.8 s.
- **Three invocations of 5.4-6.1 s ended `success` under `cpu_ms = 5000`.** I can't say which of two readings is
  right:
  - the limit has slack, or isn't applied to these invocations as configured;
  - or the analytics' `cpuTime` counts time the limit doesn't.
  - Either way, R3-6's bar was set on the measured quantity, and the measured quantity is over it.

**After R3-7's window, before the rollback (data, not graded):**
- From about 00:09Z, CloudFerro's listing timed out: **18 `wx_ingest_error` lines**, each
  `OPERA list The operation was aborted due to timeout`, after 3 attempts.
- One `:30` check (00:16:35Z) did get its list after 3 attempts (`already-stored`).
- `cref` was unaffected: 10 of 11 minutes with both checks. The one miss is 00:07, the window's own edge, where
  the `:00` check ran at 00:07:06Z, before the window opened.

**Not done, pending the ruling:**
- `wt-deploy`'s `node_modules` is kept. A ruled roll-forward or a further action would need its `wrangler`.
- R4 waits: it asserts `opuk` at LHR, and `opuk` is no longer being ingested.

**Ruling, pasted ~00:36Z 2026-11-05 (verbatim):**

> R3: received. The rollback at 00:18:33Z was the frozen escape, carried out correctly. R3-6 stays OFF in the record, with the miss kept: the prediction assumed 0.5-0.8 s per opuk build, and the reading says 2-3 s.
>
> Ruled (a): stay rolled back and bring a fix. (b) is declined:
> - it moves R3-6's bar to the reading;
> - opuk could still exhaust the invocation cref runs in;
> - it relies on a cpu_ms whose enforcement we can't yet explain.
>
> 1. Read-only, now:
>    (i) the limits the deployed d37d9a21 version actually carried, read from the version itself (wrangler versions view or the API), not from the toml;
>    (ii) for the three 5.4-6.1 s invocations: cpuTime, wallTime and outcome, as their invocation records give them;
>    (iii) what Cloudflare's docs say about how cpu_ms is enforced on scheduled events.
> 2. With that report, send a plan for one staging copy of the ingest on blipscope-wx-dev, used for two things:
>    - a positive control for the CPU-limit instrument: a planted over-limit invocation (a known busy loop under a low cpu_ms) must appear as a CPU-limit outcome in the same instrument; if it doesn't, that gate is BLIND;
>    - an opuk build's CPU on Cloudflare's runtime: real OPERA frames, N builds registered before any is sent, and a cref-only control in the same instrument. Local timing doesn't count.
>    Say how the copy stops running when the measurement ends (its cron removed). Deleting it is Daniel's call. Nothing deploys until I rule the plan and give a slot. One deployer at a time: blipscope-5c (#536) and then the map server (#534) are ahead of you.
> 3. Then the fix, with predictions frozen first. My lean is to give opuk its own Worker, which keeps its CPU, its deploys and rollbacks, and its upstream away from cref (tonight's CloudFerro listing timeouts are the example). A separate cron in the same Worker is the alternative; give the trade-offs. cref's own headroom gets a row of its own: it peaked at 3,621 ms against cpu_ms 5,000, a limit the toml says was sized at 10x A4's 0.53 s. Re-read R3-0's cost against the measured CPU.
> 4. R3 re-runs after the fix, in a slot I set outside board windows.
>
> Held: R4, until R3 passes. Keep wt-deploy/proxy/node_modules (C: had 60,141 MiB free at 00:31Z). The stored opuk frames stay in the bucket: deleting data is Daniel's call, and nothing shipped requests /wx yet.

### The ruling's item 1, read-only (2026-11-05 ~00:36-00:40Z)

**(i) The limits each version carried, from the version records** (`wrangler versions view <id> --json`, not the toml):

| version | `script_runtime.limits.cpu_ms` | `usage_model` | binding |
|---|---|---|---|
| `d37d9a21` (R3, rolled back) | **5000** | standard | `WX` -> `blipscope-wx` |
| `582612da` (live since 00:18:33Z) | **5000** | standard | `WX` -> `blipscope-wx` |

**(ii) The three invocations, from their Workers Logs invocation records** (`$workers.*`):

| scheduledTime | `cpuTimeMs` | `wallTimeMs` | `outcome` |
|---|---|---|---|
| 22:19:05.233Z | **5,426** | 38,313 | **`ok`** |
| 23:19:05.233Z | **6,072** | 38,807 | **`ok`** |
| 23:29:05.233Z | **6,020** | 39,689 | **`ok`** |

- The GraphQL per-minute figures (5,426.5 / 6,072.8 / 6,020.0) agree to the millisecond. So the two instruments
  read one quantity.
- `wallTimeMs` is about 38-40 s: the `:00` check, the wait to `:30`, then the `:30` check.

**(iii) What Cloudflare's docs say** (developers.cloudflare.com, read 2026-11-05):

- **On the limit itself.** `/workers/platform/limits/#cpu-time` and `/workers/wrangler/configuration/#limits` both say:
  *"Each isolate has some built-in flexibility to allow for cases where your Worker infrequently runs over the
  configured limit. If your Worker starts hitting the limit consistently, its execution will be terminated according
  to the limit configured."*
  - Neither page quantifies "infrequently" or "consistently".
  - The over-limit outcome is `exceededCpu` (in analytics and Logpush).
  - The Workflows page calls the same thing `exceededResources` in Workers metrics.
- **Specific to scheduled events:** the limits table gives Workers Paid **"CPU time per Cron Trigger: 30 seconds
  (< 1 hour interval)"**. The pricing page says a Cron Trigger invocation may use up to 15 minutes. The two pages
  disagree.
  - Nothing else on either page is specific to how `cpu_ms` is enforced on scheduled events.
- **So:** 3 of 127 invocations over a 5,000 ms limit, ending `ok`, is what the documented "flexibility" describes.
  - It also means **R3-6's gate, "0 CPU-limit errors", can stay at 0 while invocations run over the limit.** Whether
    it can ever fire is the positive control's question.

**A premise of item 2 doesn't hold: there is no `blipscope-wx-dev` bucket.**
- `wrangler r2 bucket list` (read-only) shows 4 buckets. The weather one is `blipscope-wx` alone.
- `blipscope-wx-dev` is the name of the proxy's **top-level dev/test binding** (`proxy/wrangler.toml`: "DEV/TEST
  ONLY: tests get an in-memory bucket"). Staging and production both bind `blipscope-wx`.
- Creating the bucket is an account change, so the plan below lists it as Daniel's.

**Beside item 1, read-only: per invocation, what each product built** (`tools/wx/r3_minute_join.py`: Workers Logs
invocation records joined to each product's check lines by `scheduledTime`). Over R3's 131 invocations, all `ok`:

| built in the invocation | n | median CPU (ms) | max (ms) |
|---|---|---|---|
| nothing | 52 | 26 | 58 |
| one `opuk` frame, no `cref` frame | 12 | **2,161** | 2,948 |
| one `cref` frame, no `opuk` frame | 53 | 1,197 | 2,504 |
| **one of each** | 12 | **3,452** | **6,072** |
| two `opuk` (the deploy's backfill), with and without `cref` | 1 + 1 | 2,274 / 2,512 | |

- Baseline, `582612da` with `cref` alone (19:00-21:00Z): building invocations have a median of 1,328 and a max of
  3,621; the rest have a median of 20.
- **What this changes in my R3 report:** I wrote "one `opuk` build adds about 2-3 s" with no alternative named.
  - The 12 invocations where `opuk` built alone bear that out: 1,515-2,948 ms.
  - The cause of the **over-5,000 readings** is more specific: all three are invocations where **both** products
    built a frame (3 of the 12 such).
  - The two deploy-minute invocations that built two `opuk` frames read only 2,274 and 2,512 ms. That's unexplained.
    A fresh isolate after the deploy, or cheaper backfill frames, would each fit, and these data separate neither.

### Plan S: one staging copy of the ingest, for the ruling (proposed 2026-11-05 00:40Z; nothing runs before the ruling and a slot)

**Prerequisite, Daniel's: the bucket.** `blipscope-wx-dev` doesn't exist (above), so Daniel creates it, or the
ruling names another target. The copy never binds `blipscope-wx`.

**The code, one PR (predictions frozen in it before the code):**
- **`proxy/wx-ingest-dev.wrangler.toml`.**
  - Worker name `blipscope-wx-ingest-dev`, in every env block, so it's one Worker and never the production name.
  - `workers_dev = false`, no routes.
  - `WX` -> `blipscope-wx-dev`, repeated in each env block, because bindings don't inherit.
  - Observability on, `head_sampling_rate = 1`.
  - One `[env.<phase>]` per phase below, each setting `MODE`, `cpu_ms` and `crons`.
- **`proxy/src/wx/ingest-dev-worker.ts`,** a dev-only entry that nothing in production imports. Its `scheduled()`
  keeps production's shape: a `:00` check, then a `:30` check in the same invocation. By `MODE`:
  - **`plant`:** a synthetic busy loop, a fixed integer iteration count `ITER` whose result is logged so it can't be
    optimised away. It touches no bucket and no upstream. (`Date.now()` doesn't advance during CPU in workerd, so the
    loop is bounded by count, not by time.)
  - **`cref` / `opuk`:** `checkAll` over that one product. `checkAll` gains an optional product list, defaulting to
    `PRODUCTS`, so production's call is unchanged. A test asserts production's entry still runs both products.
- **Host tests:** the filter; production's default; the plant's result is deterministic.

**The phases, one deploy each to the same Worker; every version record is read and printed after its deploy:**

| phase | env | `cpu_ms` | runs | for |
|---|---|---|---|---|
| **S0** | `off` | 30,000 | **no cron** | deployed inert. Its version record must show exactly one binding, `WX` -> `blipscope-wx-dev`, and no schedule, before anything is armed |
| **S1** | `plantcal` | 30,000 | 3 invocations | the plant's own CPU, `C`, read in the same instrument. `ITER` is chosen locally to aim at about 2,000 ms; local timing only picks the count and is not a reading |
| **S2** | `plant` | **100** | **10 invocations**, one a minute | **the positive control.** Same code and `ITER` as S1, at 1/20 of its CPU, every invocation, because the docs tolerate *infrequent* overruns |
| **S3** | `cref` | 30,000 | 60 invocations | the `cref`-only control |
| **S4** | `opuk` | 30,000 | until **N = 12** `stored` checks | an `opuk` build on Cloudflare's runtime |
| **S5** | `off` | 30,000 | **no cron** | the stop |

- `cpu_ms` 30,000 is the documented cap for a sub-hourly Cron Trigger, so S3 and S4 read the CPU uncut.
- **Instruments:** R3-6's own (`workersInvocationsAdaptive` by status, now with a script-name argument) and the
  Workers Logs invocation records (`$workers.outcome`, `cpuTimeMs`, `wallTimeMs`), joined per invocation by
  `r3_minute_join.py`.

**S1, frozen:**
- **As predicted:** 3 records, all `ok`, each within ±25% of their median `C`.
- **Off, then stop:** `C` under 500 ms, because S2's overrun would then be under 5x.

**S2's verdict table, frozen:**

| result | means |
|---|---|
| at least 1 of the 10 is `exceededCpu` in Logs, **and** R3-6's instrument shows a non-success status in the window | **the CPU-limit gate is LIVE.** The count is printed |
| at least 9 records, all `ok` / `success` | **the gate is BLIND:** it can't see a CPU-limit outcome, so tonight's "0 CPU-limit errors" proved nothing |
| anything else (fewer than 9 records, the two instruments disagree, an outcome outside `ok` / `exceededCpu` / `exceededResources`) | the instrument is unreliable at this scale: **stop and decide** |

**S3 and S4, frozen** (from tonight's join):

| phase | invocations that built a frame | the rest | escape |
|---|---|---|---|
| **S3** | median in **[900, 1,700] ms**, max ≤ 4,000 (tonight 1,197 / 2,504; baseline 1,328 / 3,621) | median ≤ 100 ms | outside -> stop and decide |
| **S4** | median in **[1,500, 3,000] ms**, max ≤ 4,000 (tonight, `opuk` alone: 2,161 / 2,948) | median ≤ 100 ms | outside -> stop and decide |

- **S4's count:** N = 12 counts only `stored` checks, starting after the empty bucket's backfill is done (the first
  `stored` after the last `backfilled`).
- **S4's window** closes at the 12th, or at 120 min. Fewer than 12 by then: the count is reported with CloudFerro's
  error lines. Fewer than 6 is BLIND.
- **S4's entry runs `opuk` alone,** which is exactly the invocation either fix shape would run (its own Worker, or
  its own cron). So S4 measures the fix's real invocation.

**S5, the stop:**
- The `off` env is re-deployed: no cron, and `MODE` isn't `plant`.
- **Verified:** the version record shows no schedule, and R3-6's instrument shows 0 invocations of
  `blipscope-wx-ingest-dev` over 10 min after it, read twice 5 min apart.
- **Deleting the Worker and the bucket is Daniel's.**

**Cost and footprint:**
- About 3.5 h of wall time: S0 5 min, S1 4, S2 11, S3 60, S4 up to 120, S5 15.
- At most about 250 invocations, and under about 0.4 M CPU ms (about 1.3% of the 30 M included a month).
- R2 operations on the dev bucket under 1,000.
- **Upstreams:**
  - S3 adds one NOAA reader for an hour.
  - S4 makes the dev copy CloudFerro's only reader from us, since production's `opuk` is rolled back.
- Nothing touches `blipscope-wx`, the proxy, or any board.

**Order:**
1. The PR, reviewed and merged.
2. The bucket, from Daniel.
3. A slot from the reviewer after blipscope-5c (#536) and the map server (#534). One deployer at a time.
4. S0 to S5.
5. Then the fix (item 3), with its own predictions.

**Ruling, pasted ~00:55Z 2026-11-05 (verbatim):**

> Item 1: received. Taken together, (i)-(iii) show the limit was applied as configured (both version records carry cpu_ms 5000). The three overruns that ended ok are the documented per-isolate flexibility. So R3-6's "0 CPU-limit errors" could read 0 while invocations overran, and that is why it needs Plan S's positive control before it is used again.
>
> The bucket: my ruling named blipscope-wx-dev, which doesn't exist. I took the name from the proxy's test binding without checking it. That miss is mine. Daniel is being asked for the bucket separately.
>
> The correction is accepted, and the original wording stays in the record beside it. The join is the useful result: each product alone stays under 3 s (opuk max 2,948 ms, cref max 2,504 ms), and every reading over 5,000 ms built one of each in the same invocation. That points to separation as the fix, and S4 measures the shape either fix would run.
>
> Plan S: approved, with two additions.
> 1. The plant never reaches production. Add a check on what production would ship (the dry-run bundle from wx-ingest.wrangler.toml) that finds no plant code. Its sabotage: production's entry imports the dev entry, and the check goes red.
> 2. The test that production's checkAll still runs both products gets its own sabotage: narrowing the default list to one product turns it red.
>
> Go ahead with the PR now, predictions first and then code. Nothing deploys before three things: my review and the merge, the bucket, and a slot from me for S0. S1-S5 redeploy only the dev Worker and need no further slots. Plan S shares nothing with the proxy, the Bend feed or any board, so it may overlap #536's T5.
>
> Held: R4 and node_modules, as before.

**The bucket, on Daniel's direct word in this session (~00:47Z): "yes, create blipscope-wx-dev".**
- `npx wrangler r2 bucket create blipscope-wx-dev --location wnam --storage-class Standard`: 00:47:50Z, exit 0,
  empty stderr. The location and storage class match `blipscope-wx` (WNAM, Standard), read just before.
- `wrangler r2 bucket info` reads back created 00:47:51.762Z, WNAM, Standard, 0 objects, 0 B. The weather buckets are
  now `blipscope-wx` and `blipscope-wx-dev`.
- **A side effect to know:** `blipscope-wx-dev` is also the name of the proxy's top-level dev/test binding
  (`proxy/wrangler.toml`).
  - Tests still get an in-memory bucket.
  - Only a `wrangler dev --remote` of the proxy would now reach a real, empty bucket.
  - Staging and production bind `blipscope-wx` and are unchanged.

**Daniel, via the reviewer chat, pasted ~01:00Z (verbatim):**

> Daniel's yes: create the R2 bucket blipscope-wx-dev for Plan S. Deleting it afterwards stays his call. If your token can't create buckets, say so, and Daniel will create it in the dashboard. The rest of the ~00:55Z ruling stands: the PR first, then a slot from me for S0.

- **Already done.** The bucket was created at 00:47:50Z on Daniel's direct word in this session, through the wrangler
  OAuth login, so no dashboard step is needed.

**Plan S's PR: #537** (`feat/wx-plan-s`).
- Predictions were frozen first at `22707c5` (00:51:39Z); the code is `7321f58`.
- Its own record is `docs/wx-plan-s.md`.
- Locally: all four test sabotages and both bundle sabotages read as predicted. The selftest caught a separable
  marker in the first draft before the push.
- The full suite has one local-only failure: the CRLF checkout of `credits.html`, recorded with the alternative
  excluded.
- CI is read when finished. Nothing deploys before the review, the merge and a slot.

**Item 3's cost input: R3-0 re-read against the measured CPU** (read-only, 2026-11-05 ~01:03Z; `cost_read.py`, same
method as R3-0):

| | R3-0 (projected, 20:06Z) | now (measured) |
|---|---|---|
| account Workers CPU, 7-day run rate | -- | **36.67 M ms a month**. About 29 M of it is the `cref` ingest (974,758 ms a day over the last 7 days) |
| `opuk`'s increment | 4.9-7.7 M ms a month (K11 x I7's factor of 2) | **about 19.3 M ms a month**: 8,640 builds x about 2.2 s (the join: 2,161 ms median alone; +2,255 ms over `cref`'s median when both build), plus about 6 ms x 43,200 non-building invocations |
| overage a month (run rate) | $0.24-0.30 | **about $0.52 CPU + $0.01 Class A = $0.53** |
| verdict | UNDER $2 | **UNDER $2** |

- **Why not the simpler window difference:** R3's 2 h (140,267 ms) minus the 19:00-21:00Z `cref`-only baseline
  (100,750 ms) gives 39,517 ms, or about 14.2 M a month. That **undercounts**, because `cref`'s own CPU differed
  between the windows: its building median was 1,197 ms in R3 against 1,328 in the baseline. That is the weather,
  not the code.
- The per-build figure doesn't depend on `cref`'s weather.
- Splitting `opuk` into its own Worker or cron moves this CPU to another invocation; it doesn't remove it. The cost
  stands for either fix shape.
- S4's measurement replaces the 2.2 s with a figure from `opuk` running alone.

**#537's CI at its head `7321f58b2249`, finished 01:19:05Z** (read only after every run completed):

| | predicted (`22707c5`) | got | verdict |
|---|---|---|---|
| proxy suite | 897 passed, 1 skipped (898); files 68 passed, 1 skipped (69) | **897 passed, 1 skipped (898); files 68 passed, 1 skipped (69)** | as predicted |
| "The ingest's dev copy stays out of production (selftest)" | passes | **SELFTEST PASSED:** real PB1 PASS (production 0, anchor 1, dev 1); real PB2 PASS (6/6 `WX->blipscope-wx-dev`); PB1s FAIL (marker 1); PB2s FAIL on `cref` alone | as predicted |
| check-runs on the head | 41: 38 success, 3 skipped | **41: 38 success, 3 skipped**. Workflows: device-ids, firmware, no-traffic-captures, workers, all `success` | as predicted |

- The local-only Q-C1 failure didn't occur in CI, on an LF checkout, as the exclusion said.
- **Kept off #537's head:** this record lives here, so the head stays the commit the review reads.

**Ruling, pasted ~02:45Z 2026-11-05 (verbatim):**

> #537: approved to merge at 7321f58b2249, as its CI read:
> - 41 check runs: 38 success, 3 skipped, 0 failed;
> - proxy suite: 897 passed, 1 skipped;
> - the plant selftest passed.
> Squash with --match-head-commit, no --admin. Record main's CI from a finished run (a proxy push on main has read 41: 39 success, 2 skipped).
>
> My review:
> - checkAll's product list defaults to PRODUCTS, so production's call is unchanged. PS1 and its sabotage (the default narrowed to one product) cover it.
> - The dev entry: the plant is bounded by count, logs its result, and carries PLANT_MARKER with the loop. MODE off or unknown touches nothing. cref and opuk call production's checkAll with one product.
> - The toml: every env names blipscope-wx-ingest-dev, so S1-S5 redeploy one Worker. Each env binds only blipscope-wx-dev (PB2, 6 of 6). The top level and env.off have no cron.
> - PB1's first draft couldn't see its own sabotage (esbuild kept the loop without the marker). You fixed it before the push and recorded it, and the miss stays in the record.
>
> S0: your slot opens once #537 is merged and main's CI has finished with 0 failed. No other deploy is queued. S1-S5 then need no further slots; record each phase's start and end. Plan S touches nothing shared, so it may overlap T5 and the board windows. Anything outside the table: stop and report.

**#537 merged:** squash, `--match-head-commit 7321f58b2249`, no `--admin`.
- Merge commit `131c0f776d9a`, 02:59:56Z. The remote branch is deleted.
- `git ls-tree origin/main` lists all six new files.
- Main had moved underneath it (#523, #524), but **`git diff 7321f58b2249 origin/main -- proxy/` is empty.** So the
  dev Worker that S0 deploys bundles exactly what #537's CI tested.
- Main's CI on the merge commit is read when finished. Basis: 41, with 39 success and 2 skipped. `photos` and
  `web-renderer` don't fire on this change.

**Plan S's reader, pinned before S0** (`tools/wx/s_reads.py`; the tables are §3 of `docs/wx-plan-s.md`, frozen at
`22707c5`):
- **Phase attribution:** each invocation belongs to the phase whose **version id** it carries
  (`$workers.scriptVersion.id`, confirmed present on a production record: it reads `582612da`). The window bounds
  `scheduledTime` from one phase's deploy to the next.
- **S1 and S2 counts:** the next phase is deployed after the 3rd (S1) or 10th (S2) invocation's `:30` check and
  before the next firing. If an extra invocation still lands on the old version, the phase reads it, and its row
  (3 records, or "10 invocations") is then off: stop and report.
- **S4's graded set:** the invocations whose `stored` check counts toward N, i.e. after the last `backfilled`.
  - The backfill invocations print beside it, ungraded.
  - "The rest" means S4's non-building invocations.
  - S4 ends at the 12th counted `stored`, or at 120 min.
- **S5:** R3-6's instrument and the Logs over the 10 min after S5's deploy, read twice 5 min apart. Both must show
  0 invocations.
- **Each phase's start (its deploy's time and version id) and end** are recorded, as ruled.

**One more procedural pin before S0: parking between phases.**
- A phase is read from the Logs and from GraphQL, which land minutes late. Left on its own version meanwhile, a
  phase would keep firing and change its own count.
- So after each phase's last counted invocation, the copy is **re-deployed on `off`** (inert: no cron, `MODE` off)
  while that phase is read. The next phase deploys only if the read is as predicted.
- The order:
  1. S0 (`off`);
  2. S1 (`plantcal`, 3 invocations), park, read;
  3. S2 (`plant`, 10), park, read;
  4. S3 (`cref`, 60), park, read;
  5. S4 (`opuk`, until N = 12 counted `stored`, at most 120 min);
  6. S5 (`off`), then read S4 and S5.
- A park creates a version and changes no reading. Each park's time and version id are recorded with the phases.

### Plan S runs (2026-11-05)

**Main's CI at `131c0f776d9a`, finished 03:12:20Z:**
- **41 check-runs: 39 success, 2 skipped, 0 failed**, as the ruling's basis.
- The four workflows were device-ids, firmware, no-traffic-captures and workers.
- Proxy suite 897 passed, 1 skipped (898). The plant selftest passed.
- **S0's slot opened.**

**S0, deployed 03:12:47.344-03:12:51.534Z** (`npx wrangler deploy -c wx-ingest-dev.wrangler.toml -e off` from the
clean checkout at `131c0f776d9a`, exit 0, empty stderr):
- Worker **`blipscope-wx-ingest-dev`**. The deploy says "Deployed blipscope-wx-ingest-dev triggers" and lists **no
  schedule**.
- Version **`b350ba72-d910-43d3-bd92-68e8acfe2f15`**. `deployments status`: 100%, created 03:12:50.678Z.
- **The version record** (`wrangler versions view --json`):
  - `limits.cpu_ms` **30000**, `usage_model` standard, handlers `["scheduled"]`;
  - bindings: **`WX` -> `blipscope-wx-dev` (`r2_bucket`)**, and **`MODE` = `off` (`plain_text`)**.

**S0 is stopped on its row's wording, not on the copy.**
- S0's frozen row says "the version record shows **exactly one binding**, `WX` -> `blipscope-wx-dev`". The record
  has **two**:
  - the bucket;
  - the `MODE` var, which every env carries by design. It is in §1's table, PB2 checks it, and the review read it.
- **What it would take for the record to be wrong:** a second bucket, or a binding to `blipscope-wx`. Neither is
  there.
  - PB2's own wording, "exactly one R2 binding ... and `MODE` as in the table", is what was meant, and it holds.
  - S0's row is the looser text, and I wrote it.
- **Under the rule** (anything outside the table: stop and report), S0 stops here and waits for a ruling.
- **The copy is inert:** no cron, and `MODE` off. So the stop costs nothing but time. Nothing after S0 runs until
  ruled.

**Ruling, pasted ~03:30Z (verbatim):**

> S0: as predicted, with its row's wording re-registered and the miss kept. "Exactly one binding" meant one R2 binding. The version record shows exactly one, WX → blipscope-wx-dev, beside the MODE var (off, plain text) that the plan's table and PB2 already name. Nothing else is bound. The row now reads: exactly one R2 binding, WX → blipscope-wx-dev; MODE = off; no other binding; cpu_ms 30000; no schedule.
>
> Your three pins are accepted:
> - attribution by version id;
> - parking at off between phases, with each park's time and version recorded;
> - S4's graded set.
>
> S1 may start at once, then S1-S5 as pinned. Anything outside the table: stop and report.

**How each phase is ended (procedure, not a reading).**
- `tools/wx/s_drive.py` holds `wrangler tail` open on the dev Worker, deploys the phase, and counts the phase's
  invocations as they complete. It parks on `off` at the phase's count: S1 3, S2 10, S3 60, S4 12 counted
  `stored`. If the count isn't reached in time, it parks anyway and says TIMEOUT.
- Tail is only the trigger. **The graded reading is `s_reads.py`** (Logs records + R3-6's instrument), by version id.
- **Why tail:** a cron schedule change may take time to start firing, and Logs land too late to park between the
  last counted firing and the next. Tail shows a finished invocation within seconds.

**S1, first attempt: the driver crashed after its deploy. The copy is parked; no reading was taken.**
- `s_drive.py plantcal S1 3 12`, started 03:36:09Z. The tail started, then `plantcal` was deployed:
  **version `85a35361-2a50-4e72-a814-ae13696e6e4e`**, live 03:36:35Z (`deployments status`).
- **The crash:** Python decoded wrangler's UTF-8 output as cp1252. The deploy succeeded, but its captured stdout came
  back `None` and the driver died before counting anything. `plantcal` was left live with no driver, and two
  `wrangler tail` processes were orphaned.
- **The safe state, at once:**
  - **parked** on `off` by hand from the clean checkout: 03:37:24.028-03:37:26.740Z, exit 0, empty stderr, no
    schedule, **version `e9cb8970-c9ad-41d1-9bc1-0856a25d5c45`**;
  - both orphan tails stopped by pid (0 left).
- **What `85a35361` ran,** first read at 03:38:37Z: **0 invocations** in both instruments from 03:12 to 03:40Z.
  Re-read below before anything is relied on.
- **The driver fix** (a tool, not a graded instrument):
  - every subprocess decoded as UTF-8 with `errors="replace"`;
  - **everything after a phase deploy runs under try/finally that parks on `off`;**
  - an exit hook kills the tail, so it never outlives the driver.
  - Tested without deploying: a dry-run's output decodes, and the tail connects and closes (0 left).
- **Not done without a ruling:** re-running S1 as a new `plantcal` version, with `85a35361` recorded as an aborted
  attempt. Choosing which version S1's row is read against, after an unexpected event, is the reviewer's call.

- **Re-read at 03:41:36Z, 3 min after the first:** still **0 invocations** of any dev version from 03:12 to 03:42Z
  (Logs: 0 records; R3-6's instrument: no rows). So `85a35361` never fired during its 51 s live (03:36:35-03:37:26Z),
  across one minute boundary.
  - **Explanation, not a finding:** a change of cron trigger can take more than a minute to start firing.
  - **The alternative:** the trigger never registered at all. These data don't separate the two; S1's re-run will,
    because the driver counts live and parks only on the count.

**Ruling, pasted ~03:55Z (verbatim):**

> S1: your proposal is accepted.
> - 85a35361 is recorded as an aborted attempt: live 03:36:35Z for 51 s, 0 invocations, ungraded. e9cb8970 is the park.
> - S1 re-runs as a new plantcal version, read against its own version id. S2-S5 then run as pinned.
> - The driver fix is accepted as a tool fix: UTF-8 decoding, park-on-off under try/finally, and the exit hook on the tail.
>
> One addition, for every phase deploy from here: record the time from the deploy to the first invocation. If no invocation arrives within 5 minutes of a phase deploy, park on off and report. That separates "slow to start" from "never registered", and no phase can wait open-ended.

- **In the driver:** it prints each invocation's `scheduledTime` and the seconds from the phase's deploy.
  - No invocation of the phase's version within 300 s of its deploy: it parks on `off` and ends with
    "NO FIRST INVOCATION IN 5 MIN".
  - The graded time is read from the Logs records (`scheduledTime` minus the deploy's end).

**S1, second attempt: OFF its row (1 record, not 3). The driver parked early, on its own misreading of the 5-minute
rule.**

| | |
|---|---|
| phase deploy | `plantcal`, **version `f967798b-029e-4bb6-8734-ffdac604d581`**, 04:10:33.840-04:10:38.181Z, exit 0, empty stderr; the deploy listed `schedule: * * * * *` |
| **time to first invocation** (the ~03:55Z ruling's figure) | first `scheduledTime` **04:15:06.895Z**, **268.7 s after the deploy**. Inside 5 min. The cron is **slow to start**, not unregistered, which is also why `85a35361` ran 0 in its 51 s |
| why it parked anyway | the driver judged the rule on the tail event's ARRIVAL, which comes when the invocation **finishes** (wall 35.1 s). At 300 s (04:15:38Z) the first invocation was still running; its event arrived about 4 s later. **The rule is about the invocation, so it should be judged on `scheduledTime`.** That is my defect |
| park | `off`, **version `2d9013bd-7b25-4f22-b2d5-56694f88eeab`**, 04:15:38.341-04:15:42.768Z, exit 0, empty stderr, no schedule listed |
| `f967798b`'s one invocation (Logs and R3-6, agreeing) | `ok`, **`cpuTimeMs` 10,056**, `wallTimeMs` 35,109. Two plant lines, both `result` 3554693450, with `iter` 800,000,000 |
| S1's row applied | records **1** (not 3): **OFF**. `C` (one record) = 10,056 ms, so "`C` ≥ 500" holds; "each within ±25%" can't be read from one |

**After the park: the removed cron kept firing.**
- The parked `off` version `2d9013bd` was invoked at **04:16:06Z and 04:17:06Z**, each logging `wx_dev_off` twice and
  touching nothing.
- So removing a cron lags too, as adding one did.
- **This bears on S5's frozen row** (0 invocations in the 10 min after S5's deploy). How long the removal lags is
  being watched now, read-only.

**The plant runs about 5.6x slower on Cloudflare than on my laptop:** 10,056 ms for 2 x 800 M rounds, against about
1.8 s locally. That is a fact about the plant, and why local timing was never the reading.

**The removal lag, measured (read-only, 04:18-04:27Z).** The parked `off` version `2d9013bd`, deployed with no cron at
04:15:42.768Z, was invoked **6 times, 04:16:06Z to 04:21:06Z**. Each one logged only `wx_dev_off` and used 0 ms CPU.
- **There was no firing at 04:22:06Z or later.** The count held at 6 over three reads: 04:22:36, 04:24:39 and
  04:26:41Z.
- The last firing was **323 s** after the deploy that removed the cron.
- So **both directions lag by about 5 min**: adding a cron took 268.7 s to the first firing; removing one took up to
  323 s to the last.
- With attribution by version id, neither lag moves a phase's count: every firing on a parked version is the park's.

**Proposed for a ruling** (nothing deploys until then; the copy is parked and has been quiet since 04:21:06Z):
1. **S1 runs a third time, as a new `plantcal` version, read against its own version id.**
   - `f967798b` is recorded as the second attempt: OFF on its row, 1 record, `C` 10,056 ms.
   - `2d9013bd` is its park.
2. **The driver judges the 5-minute rule on `scheduledTime`.**
   - At the deploy + 360 s, it parks if no finished invocation of the phase's version has a `scheduledTime` within
     300 s of the deploy. The extra 60 s is because an invocation is only seen when it finishes.
   - **Caps, adding the 5 min start lag:** S1 15 min, S2 20, S3 75, S4 135.
3. **S5's row, re-registered before it runs.** "0 invocations in the 10 min after S5's deploy" can't hold with a
   removal lag of about 323 s. Proposed instead:
   - after S5's deploy, invocations land **only on S5's version and log only `wx_dev_off`**;
   - **the last within 10 min of the deploy;**
   - **then 0 over the next 10 min**, read twice 5 min apart.
- **The watch's last reads** (04:28:43Z and 04:30:45Z) still hold at **6, the last at 04:21:06Z**. That is no
  firing for 9.6 min, over seven reads in all.

**Ruling, pasted ~04:30Z (verbatim):**

> S1: all three proposals are accepted, and the misses are kept.
> 1. f967798b is S1's second attempt: OFF, 1 record, C = 10,056 ms. 2d9013bd is its park. S1 runs a third time as a new plantcal version, read against its own version id.
> 2. The driver judges the 5-minute rule on scheduledTime. At deploy + 360 s it parks if no finished invocation of the phase's version started within 300 s of the deploy. The caps are S1 15 min, S2 20, S3 75 and S4 135.
> 3. S5 is re-registered before it runs. After S5's deploy, invocations land only on S5's version and log only wx_dev_off. The last one comes within 10 min of the deploy, then 0 over the next 10 min, read twice 5 min apart.
>
> Keep both measured lags in the record: about 269 s from deploy to the first firing, and firings for about 323 s after a park. A plant invocation costs about 10 s of CPU, so S2's overrun at cpu_ms 100 is about 100x, which makes the control easier to read, not harder. Anything outside the table: stop and report.

**The measured lags, as ruled to be kept:**
- **About 269 s** from a deploy that adds the cron to its first firing (`f967798b`: 268.7 s).
- **Firings for about 323 s** after a deploy that removes it (`2d9013bd`: 6 firings, the last 323 s after).

**S1, third attempt: no invocation within 5 min, so parked and reported, as ruled.**
- **Phase deploy:** `plantcal`, **version `5e4c9a8b-8e53-400d-aad8-51e937ca7ce2`**, 04:33:51.104-04:33:53.882Z,
  exit 0, empty stderr, `schedule: * * * * *` listed.
- **The rule fired as designed** at the deploy + 360 s (04:39:54.046Z): no finished invocation of `5e4c9a8b` had
  started within 300 s of the deploy.
- **Park:** `off`, **version `09f14eca-0f0c-4302-b6c8-d7bc278e3ad1`**, 04:39:54.089-04:39:57.320Z, exit 0, empty
  stderr, no schedule.
- **What fired** (Logs and R3-6, 04:30Z to 04:41:27Z): **0 invocations**. The tail saw none either. A late firing
  from this cron add, on the parked version, is being watched read-only.

**Cause, from Cloudflare's own docs** (`/workers/configuration/cron-triggers/`): *"Changes such as adding a new Cron
Trigger, updating an old Cron Trigger, or deleting a Cron Trigger may take several minutes (up to 15 minutes) to
propagate to the Cloudflare global network."*
- Every park on `off` deletes the cron, and every phase adds it back. So each phase starts a fresh propagation of up
  to 15 min.
- The three adds so far: `85a35361`, 0 in 51 s; `f967798b`, 268.7 s; `5e4c9a8b`, more than 360 s. One removal:
  323 s. All are inside the documented 15 min.
- **A version change, by contrast, is immediate:** the park `2d9013bd` was invoked on its own version 24 s after its
  deploy (04:16:06Z).
- **The alternative,** that the trigger isn't registering at all, is not excluded for `5e4c9a8b` alone. But
  `f967798b`'s firing shows the same config does register.

- **The ~04:30Z ruling was pasted a second time, unchanged** (received after the report above). It is recorded once,
  above, and was already applied: S1's third attempt ran under it.
  - It doesn't cover the report sent after that attempt, so **no fourth attempt and no change to parking runs** until
    that report is ruled.
- **The late firing, from the read-only watch:** `5e4c9a8b`'s cron add **did register**. Its first firing was at
  **04:43:45Z, about 592 s (9.9 min) after its deploy**.
  - It landed on the **parked** version `09f14eca` and logged only `wx_dev_off`, with 0 ms CPU.
  - Firings followed at 04:44:45Z and 04:45:45Z; that is three by the 04:46:27Z read.
  - So the alternative, "never registered", is now excluded for `5e4c9a8b`: the add was slow, about 592 s, inside the
    documented 15 min.
  - The park's removal, deployed 04:39:57Z, is presumably still propagating. The watch continues.
- **The adds measured so far:** 268.7 s (`f967798b`) and about 592 s (`5e4c9a8b`). `85a35361` had 0 firings in its
  51 s live, and no firing was seen afterwards either: a removal followed it within the minute.

**The PC timetable, to every session, pasted ~04:50Z (verbatim):**

> The PC timetable until tonight's E2:08 windows are done. It replaces my earlier PC rules, including "Plan S may overlap the board windows" and Weatherscope's 13:00Z start. That rule was wrong: Plan S's driver and its wrangler tail run on this PC, and W5⁵'s guard G6 counts them.
>
> - Until 10:00Z: the PC is open, except for E2:08 windows.
>   - The UK's Plan S continues as ruled. By 10:00Z it must be finished, or parked with its driver and every wrangler tail ended.
>   - Weatherscope may run its two PRs' Python now, finishing by 10:00Z.
>   - Other sessions' tests and builds may run, finishing by 10:00Z.
> - 10:00Z to 16:45Z: E2:08 windows only.
>   - blipscope-5c re-runs W5⁵ in full from 10:00Z, then K6's three arms, each announced, all ending by 16:45Z. Anything that doesn't fit is rescheduled through me.
>   - No other session runs any Python, any wrangler command (tail, dev or deploy), any build or any test on this PC in that span. Text, git, gh and CI are fine.
> - From 16:45Z: the PC is open again. The 17:00-19:00Z Bend quiet block, the website's 17:00-17:30Z reading and T5''s 19:05Z read stand as set.
> - G6 stays as frozen. A session that finds another session's process on the PC reads names and times only, and never touches it.
> - blipscope-5c: W5⁵ stopping at ~04:50Z because of the UK's tail is recorded as an instrument stop, not a reading. I've received #540 and will review it once its tests have run.

**What of mine was on the PC then** (names and times):
- The S1c driver's `wrangler tail` ran from about 04:33:39Z and closed at about 04:41:27Z, after the park.
- From 04:42:23Z, my **read-only watch** started a short `python s_reads.py` every 2 min: 04:42, 04:44, 04:46,
  04:48, 04:50...
- At 04:52Z, no node or Python process on the PC was mine.
- The watch was stopped with TaskStop. A 150 s poll to 04:55:26Z then found **0** of my `s_reads.py` or dev-tail
  processes, and no bash loop carrying it.
- So what G6 counted at about 04:50Z was most likely the watch's Python, not the tail.

**Ruling, pasted ~05:00Z (verbatim):**

> The ~04:30Z text you just received was a repeat I gave Daniel to re-paste, and it adds nothing. Your four proposals, ruled:
> 1. Accepted: an idle park env that keeps the cron, so a park changes only the version. Attribution by version id keeps every phase's count clean.
> 2. Accepted: a 15-minute first-invocation window, for the one deploy that adds the cron.
> 3. Accepted: S5 is bounded at 15 minutes for the last invocation after the cron's removal, then 0 over the next 10 minutes, read twice 5 minutes apart.
> 4. Accepted: a fourth S1 attempt, read against its own version id.
>
> Timing: your driver and its wrangler tail run on Daniel's PC, and blipscope-5c's board windows need the PC quiet. After your read-only watch ends at ~05:02Z, run nothing on the PC until Daniel pastes blipscope-5c's "the PC is free" line, or until 13:00Z, whichever comes first. Then run S1's fourth attempt and S2-S5 as pinned. Keep the measured lags (268.7 s and ~592 s) in the record.

**Ruling, pasted ~05:05Z (verbatim):**

> The ~04:30Z text you received earlier was a repeat and adds nothing. Your four proposals, ruled:
> 1. Accepted: an idle park env that keeps the cron, so a park changes only the version. Attribution by version id keeps every phase's count clean.
> 2. Accepted: a 15-minute first-invocation window, for the one deploy that adds the cron.
> 3. Accepted: S5 is bounded at 15 minutes for the last invocation after the cron's removal, then 0 over the next 10 minutes, read twice 5 minutes apart.
> 4. Accepted: a fourth S1 attempt, read against its own version id.
>
> Timing follows the ~04:50Z PC timetable. Start now. By 10:00Z, be finished, or parked with your driver and every wrangler tail ended. Anything left resumes after 16:45Z. Keep the measured lags (268.7 s and ~592 s) in the record.

- **Which governs:** the two agree on the four proposals and differ on timing. **The ~05:05Z text governs.**
  - It is the later one.
  - Its timing is the ~04:50Z timetable's: open until 10:00Z, nothing from 10:00Z to 16:45Z, resume after.
  - The ~05:00Z "until 13:00Z" contradicts that timetable.
- **The measured lags, kept:**
  - adding the cron: **268.7 s** (`f967798b`) and **about 592 s** (`5e4c9a8b`);
  - removing it: firings for **323 s** (`2d9013bd`).
- **The order from here:**
  1. The `idle` env as a small PR, predictions first, reviewed and merged.
  2. S1's fourth attempt, with the 15-min first-invocation window.
  3. S2-S4, parking on `idle`, with the 5-min rule.
  4. S5 deploys `off`, with the 15 + 10 min rule.
  - All of it by 10:00Z or parked; anything left resumes after 16:45Z.

**The `idle` env's PR: #543** (`feat/wx-plan-s-idle`).
- Predictions frozen first at `f0a0622` (04:56:50Z, `docs/wx-plan-s.md` §5); the code is `8a4b109f16f0`.
- Locally the plant selftest passed: PB2 7/7 (`idle` MODE `off`); PB2s FAIL on `cref` alone.
- CI is read when finished, then it waits for review and merge.

**Pinned before any of it runs** (tools on this branch):
- **`s_drive.py`:**
  - deploys only from `PLAN_S_WANT`, the commit #543 merges as, and refuses if it's unset;
  - parks on **`idle`** by default;
  - `--first 900` for S1's fourth attempt, the one deploy that adds the cron; 300 s for every later phase;
  - judged on `scheduledTime` at the window + 60 s.
- **S5's read** (`s_reads.py S5 <S5 deploy> <now> <S5 version>`):
  - every invocation that starts at or after S5's deploy carries S5's version and logs only `wx_dev_off`;
  - the last starts within **900 s** of the deploy;
  - **0** start in **[deploy + 900 s, deploy + 1,500 s]**;
  - read twice, at or after the deploy + 1,500 s and again 5 min later.
  - Before the deploy + 1,500 s, it reads "NOT YET", never a pass.

**#543's CI at its head `8a4b109f16f0`, finished 05:16:20Z** (read only after every run completed):

| | predicted (`f0a0622`) | got | verdict |
|---|---|---|---|
| proxy suite | 897 passed, 1 skipped (898) | **897 passed, 1 skipped (898)** | as predicted |
| the plant selftest | PASSED; real PB2 7/7 with `idle` MODE `off`; PB2s FAIL on `cref` alone | **PASSED; real PB2 7/7** (top level, `off`, `idle`, `plantcal`, `plant`, `cref`, `opuk`, each `WX->blipscope-wx-dev` with its MODE; `idle` `"off"`); **PB2s FAIL ["cref"]** | as predicted |
| check-runs | 41: 38 success, 3 skipped | **41: 38 success, 3 skipped**; device-ids, firmware, no-traffic-captures, workers, all `success` | as predicted |

**Ruling, pasted ~15:10Z (verbatim):**

> #543: approved to merge at 8a4b109f16f0, as its CI read (41: 38 success, 3 skipped, 0 failed; proxy 897 passed, 1 skipped; PB2 7/7; PB2s fails on cref alone). Squash with --match-head-commit, no --admin, and record main's CI. gh and CI are allowed during the E2:08 span.
>
> My review: env.idle keeps the cron with MODE off and binds only blipscope-wx-dev. Only S5 deploys off, the config with no cron.
>
> Plan S resumes after the map server's 16:45-17:00Z builder slot: once it reports done, or at 17:00Z, whichever comes first. Run S1's fourth attempt with the 15-minute first-invocation window, then S2-S5, parking on idle between phases, as ruled. Nothing on the PC before then.

**#543 merged** (git and gh only; inside the E2:08 span, no Python and no wrangler):
- Squash with `--match-head-commit 8a4b109f16f0`, no `--admin`, at 15:06:21Z.
- **Merge commit `788dab3cf200`, 15:06:25Z.** The remote branch is deleted. `[env.idle]` is in main's dev toml.
- **Main moved underneath it:** #541 (the basemap builder, `048ed7b`) changed 4 files under `proxy/`. Its only
  `src/` file is `src/basemap/worker.ts`.
  - **`src/wx/` is unchanged since `131c0f776d9a`.**
  - `git grep` finds no `basemap` anywhere in `src/wx/`; every import there is relative inside `src/wx/`.
  - So the dev Worker's code is what #537's and #543's selftests read.
- Main's CI on `788dab3cf200` is read when finished. Its proxy count includes #541's tests, so the comparison is
  against main's own previous finished run, not 897.
- **Plan S's deploy commit is `788dab3cf200`** (`PLAN_S_WANT`). The deploy checkout moves to it with git now.
  - Its `npm ci` is unchanged if `package-lock.json` is: see the next line.
  - Plan S resumes at 17:00Z, or when the map server reports its builder slot done, whichever is first.
- `package-lock.json` and `package.json` are unchanged between `20a0323` (the worktree's `npm ci`) and `788dab3cf200`, so the deploy checkout's `node_modules` stands.

**Ruling, pasted ~15:08Z (verbatim):**

> Plan S now resumes after the map server reports its builder slot done, or at 17:15Z, whichever comes first (it was 17:00Z). Everything else stands as ruled.

**Main's CI at `788dab3cf200` (#543's merge), finished 15:29:46Z:**
- **41 check-runs: 39 success, 2 skipped, 0 failed.** The four workflows were device-ids, firmware,
  no-traffic-captures and workers.
- **Proxy suite 900 passed, 1 skipped (901).** That equals main's previous finished run at `048ed7be9466` (#541's
  merge): 900 passed, 1 skipped. So #543 added no tests, as predicted; the +3 over 897 are #541's.
- The plant selftest passed.

**Ruling, pasted ~16:52Z (verbatim):**

> Go. #543's merge is verified: 788dab3 equals the approved head's patch, and main's CI is recorded. Start Plan S now from 788dab3: S1's fourth attempt with the 15-minute first-invocation window, then S2-S5, parking on idle between phases, as ruled. Record each phase's start and end. Anything outside the table: stop and report.

**Stated before S1's fourth attempt, not re-registered:**
- S1's cap (15 min, ruled ~04:30Z) is as long as its first-invocation window (15 min, ruled ~05:05Z).
- If the first firing starts later than about 12 min after the deploy, three invocations can't finish inside the cap.
  The driver then ends on TIMEOUT, and that is stop-and-report.
- Run as ruled, with neither limit changed.

**S1, fourth attempt (from `788dab3cf200`): the tail exited mid-phase, so the phase ended and I'm reporting. No
reading was taken.**

| | |
|---|---|
| start | `plantcal`, **version `093b18f7-45e5-4a02-acff-79ff98cc2cb1`**, 16:54:42.622-16:54:46.195Z, exit 0, empty stderr, `schedule: * * * * *` |
| what ended it | **`wrangler tail` exited on its own at 17:01:34Z** (about 7 min after it started), with exit code 0 and empty stderr. The driver ended the phase on "TAIL EXITED", as designed |
| park (end) | **`idle`, version `756c43f6-3ec8-45eb-964d-d36b9d8f4f75`**, 17:01:34.419-17:01:38.619Z, exit 0, empty stderr, **`schedule: * * * * *` kept** |
| what `093b18f7` ran | **0 invocations** (Logs and R3-6, 16:54:46Z to 17:05:39Z) |
| the cron add | **first firing 17:03:45Z, about 539 s after `093b18f7`'s deploy**, on the `idle` park. Then 17:04:45Z; both `ok`, `wx_dev_off` only, 0 ms CPU, attributed to `756c43f6`. 0 unattributed |

**Why the tail exited is not known.** Two readings, which these data don't separate:
- Cloudflare closed the tail session. Exit 0 and no stderr fit a clean close from the far side.
- wrangler's tail ends itself after a quiet period. No event had arrived in its 7 min.
- Two earlier tails of the same length were closed by the driver itself before either could show it (S1b 6.9 min,
  S1c 7.8 min). So "about 7 min" may be a session limit that has simply not been met before.

**What changes now:** the cron is live and kept by `idle`, firing at :45 past each minute. So the next phase deploy is
a version change only, and should fire on the next minute. The 15-min window was for the one deploy that adds the
cron, and that deploy has now happened.

**The adds measured:** 268.7 s, about 592 s, about 539 s. **The removal:** 323 s.

**Ruling, pasted ~17:12Z (verbatim):**

> S1's fourth attempt is recorded as a tool stop, with no reading: 093b18f7 ran 0 invocations, and the tail closed by itself at about 7 minutes. All three proposals are accepted:
> 1. Phases are driven by the clock, not the tail. Read the firing offset from the last two idle firings, deploy a phase about 20 s after a firing starts, and park about 20 s after its Nth firing starts. The graded count stays s_reads.py by version id. A count other than N reads off: stop and report.
> 2. S4's graded set is the first 12 counted stored by scheduledTime. Later ones are printed beside them, ungraded.
> 3. S1's fifth attempt runs with the 5-minute first-invocation window, since the cron is live.
> Record the tail's ~7-minute end as an instrument limit; nothing graded relies on a tail any more. Start when you receive this. The map server's builder slot may run alongside: it's a different Worker and shares nothing.

- **The tail's limit, recorded:** a `wrangler tail` on the dev Worker ended itself after about 7 min with exit 0
  (S1d, 17:01:34Z). Nothing graded relies on a tail.
- **`tools/wx/s_clock.py` replaces `s_drive.py`** for every phase from here.
  - **Offset:** from the last two firings in the Logs, which must be 60 s +/- 2 s apart.
  - **Phase deploy:** 20-40 s after a firing start, so the phase's first firing is the next one.
  - **Park on `idle`:** 20 s after the Nth firing's start.
  - **The 5-min rule:** a phase longer than 6 min is checked at the deploy + 360 s.
  - **S4:** polls the Logs each minute and parks at the first slot after the 12th counted `stored` (cap 135 min).
  - **S5:** deploys `off` in a slot and does not park.
  - Any error after a phase deploy parks on `idle`.

**S1, fifth attempt (clock-driven, from `788dab3cf200`): the count is exact, but one clause of the row is OFF.**

| | |
|---|---|
| start | offset **45.058 s** past the minute, from the last two `idle` firings (17:16:45.058Z, 17:17:45.058Z). `plantcal`, **version `13cd381e-09d2-4212-81b5-165be45a6090`**, 17:18:12.794-17:18:16.918Z, exit 0, empty stderr, `schedule: * * * * *` |
| end | park on `idle`, **version `46aa8e6c-6df1-4d84-b09b-9fcbe36bb3d6`**, 17:21:05.101-17:21:11.618Z, exit 0, empty stderr, cron kept |
| read (`s_reads.py S1`, by version; twice, 17:24:32Z and 17:26:04Z, identical) | **3 records, all `ok`** (17:18:45, 17:19:45, 17:20:45Z), 0 unattributed. CPU **7,939 / 12,693 / 9,480 ms**, wall 32.4-36.3 s. Every plant line has `result` 3554693450 |
| R3-6's instrument (no version filter, 17:18:16-17:22:00Z) | 4 `success`, 0 errors: the 3 of `13cd381e` plus the `idle` park's 17:21:45Z firing. Consistent with the Logs |

**S1's row applied:**
- 3 records, all `ok`: **yes**.
- `C` = 9,480 ms (the median), and `C` ≥ 500: **yes**.
- **Each within ±25% of `C`** (7,110-11,850): **no. 12,693 is +33.9%.** So **S1 is OFF.**

**Cause, and the alternative:**
- The three invocations ran the identical loop: the same `iter` and the same `result`. So the 1.6x spread (7.9-12.7 s)
  belongs to the runtime's CPU accounting, not the plant.
- With `f967798b`'s 10,056 ms, four readings of the same work span 7,939-12,693.
- **The ±25% was my prediction**, and it assumed identical work reads identically. That miss is mine.
- What the runtime does underneath can't be separated here: different machines, or contention on one. Either way it
  is outside the plant.

**What it means beyond S1:**
- **S2** is unaffected. Even the lowest reading puts `cpu_ms` 100 at about 79x below one invocation.
- **S3 and S4** read medians and maxima of real ingest CPU. A ±30% runtime spread on identical work is a fact about
  how wide their rows can fairly be.

**Ruling, pasted ~17:28Z (verbatim):**

> S1: (a). S1 is recorded as OFF on its ±25% clause, with C = 9,480 ms. The spread on identical work (7,939-12,693 ms, with f967798b's 10,056 beside it) is kept as a measurement. S1's job was C for S2, and C is measured: S2's cpu_ms 100 sits about 79x or more below every reading.
>
> Go on to S2-S5 as pinned, by the clock. S3's and S4's ranges stay as frozen. If either reads off, name this runtime spread as one alternative. Carry the spread into the opuk fix's CPU predictions later as a margin: identical work can read about 1.6x apart.

- **A guard added to `s_clock.py` before S2 (a tool fix; nothing graded changes).**
  - S2's invocations are killed by design. If the Logs keep no record of a killed invocation, the 5-min check would
    park S2 on a false "no invocation".
  - So when the Logs show none, the check also asks R3-6's instrument (GraphQL, any status) for invocations in
    [deploy, deploy + 300 s]. Only the phase's version runs in that span.
  - It parks only if both read 0.
- **Carried forward:** identical work read up to about 1.6x apart. That margin goes into the `opuk` fix's CPU
  predictions.

**S2 (clock-driven): LIVE, as predicted.**

| | |
|---|---|
| start | offset 45.058 s. `plant` (`cpu_ms` 100), **version `6ebea4e7-2527-4e13-b196-b1eec8eeb6ae`**, 17:41:05.102-17:41:09.300Z, exit 0, empty stderr, `schedule: * * * * *`. The 5-min check at the deploy + 360 s found a Logs record, so the GraphQL fallback wasn't needed |
| end | park on `idle`, **version `7528c59b-136d-4993-8bce-2ad205fb8d6f`**, 17:51:05.101-17:51:11.025Z, exit 0, empty stderr, cron kept |
| Logs (by version; twice, 17:55:00Z and 17:57:05Z, identical) | **10 records, 10 of 10 `exceededCpu`**, at 17:41:45 through 17:50:45Z, one a minute. 0 unattributed. No check line logged, because each was killed inside its `:00` plant |
| R3-6's own instrument (`workersInvocationsAdaptive` by status, the same window) | **`exceededResources`: 10 invocations, 10 errors**. No `success` row |
| S2's table | at least 1 `exceededCpu` in Logs **and** a non-success status in R3-6's instrument: **LIVE, 10 of 10** |

- **What this settles:** R3-6's gate ("0 CPU-limit errors") **can see a CPU-limit outcome.** A planted overrun reads
  as `exceededResources` with an error per invocation. So R3's reading of 0 errors means no invocation was killed;
  it does not mean the instrument was blind.

**Data beside the rows, not a finding: where the kill landed.**
- Every killed invocation read **`cpuTimeMs` 2,100 exactly**, all ten, under `cpu_ms` 100. That is 21x the limit,
  and wall time was 2.1-2.5 s.
- **Readings this fits, which one limit value can't separate:**
  - the kill comes at `cpu_ms` + 2,000 ms of grace. Under production's 5,000 that would be about 7,000, which fits
    R3's 5.4-6.1 s invocations ending `ok`;
  - a floor of about 2.1 s, below which no limit bites;
  - a sampling or reporting step in how `cpuTimeMs` is accounted for killed invocations.
- Telling them apart needs a second `cpu_ms` value. That is not in Plan S. It matters to the fix: the real kill line
  above `cpu_ms` is unknown, so the fix budgets against `cpu_ms` itself.

**S3 (clock-driven): OFF, on two clauses. Stopped.**

| | |
|---|---|
| start | offset 45.058 s. `cref`, **version `d06df358-bfc6-4111-9f7e-990b37980c66`**, 17:58:05.106-17:58:08.801Z, exit 0, empty stderr, `schedule: * * * * *` |
| end | park on `idle`, **version `9a2c25c6-2d2b-4d3c-b05f-1c16b60e1fde`**, 18:58:05.118-18:58:10.465Z, exit 0, empty stderr, cron kept |
| read (`s_reads.py S3`, by version; twice, 19:02:02Z and 19:04:07Z, identical) | **60 firings seen, 59 invocation records**, all `ok`; R3-6: **59 `success`**, 0 errors. Building: n 33, **median 1,336**, **max 5,733**. The rest: n 26, median 19.5, max 35 |

**S3's row applied:**

| clause | got | |
|---|---|---|
| building median in [900, 1,700] ms | 1,336 | holds |
| **building max <= 4,000 ms** | **5,733** | **OFF** |
| the rest's median <= 100 ms | 19.5 | holds |
| **N firings (the ~17:12Z rule: a count other than N reads off)** | **60 firings seen, 59 with a record** | **OFF** |

**The max: a two-build invocation during the empty bucket's backfill.**
- 18:01:45Z read 5,733 ms. It built **two** frames: `stored` at `:00`, `backfilled` at `:30`.
- All four two-build invocations came while the empty dev bucket filled: **1,647, 3,066, 3,231, 5,733 ms**.
- **Single-build invocations: n 29, median 1,186, max 2,548, min 783.** All inside S3's row.
- **Alternatives named:**
  - two builds in one invocation (the data shows exactly that, and 5,733 is about 2,867 per build);
  - the runtime's spread on identical work (up to about 1.6x), as ruled. These data don't exclude it as a
    contributor.
- **The miss is mine.** S4's graded set was pinned to exclude its backfill; S3's row wasn't, though the same empty
  bucket meant the same backfill.

**The count: one firing with no invocation record.**
- 18:22:45Z logged its `:00` line (`cref`, `already-stored`) and then **nothing: no `:30` line and no outcome record**.
- R3-6's instrument doesn't count it either: 59, not 60.
- **Alternatives:**
  - the invocation ended abnormally after its `:00` check, and neither instrument records such an end;
  - both instruments lost it in ingestion, together with the `:30` line.
  - Three separate items missing at once leans toward the first, but doesn't exclude the second.
- Production has the same signature: R3-2's log half had one minute missing a check (22:36), and the baseline has
  178 of 180.

**State:** parked on `idle` (`9a2c25c6`), firing each minute, logging `wx_dev_off` at 0 ms CPU.

**Ruling, pasted ~19:20Z (verbatim):**

> S3: (a). Recorded OFF on both clauses, causes named, data kept:
> - The max: two builds in one invocation while the empty bucket filled (1,647-5,733 ms). S3's row should have excluded backfill, as S4's does. That miss is yours, and it stays in the record.
> - The count: 59 records for 60 firings. The 18:22:45Z invocation logged :00 and then nothing in either instrument. It's recorded as a vanished invocation, cause open (an abnormal end that isn't recorded, or ingestion loss). Production's R3-2 shows the same signature.
>
> The control holds where it matters. Single-build cref invocations here read median 1,186 and max 2,548 ms, against production's cref-only median 1,197 and max 2,504. The dev copy measures what production measures. Use those as cref's figures.
>
> S4 and S5 run as pinned. Carry two things into the fix's predictions:
> - An invocation that builds twice (a backfill) costs about twice as much.
> - About 1 in 60 to 1 in 90 invocations can vanish without a record. The design must tolerate a lost check, and the count rows must allow for it.

- **Carried to the fix's predictions:**
  - identical work can read about 1.6x apart;
  - a two-build (backfill) invocation costs about twice a single build;
  - about 1 in 60 to 1 in 90 invocations can vanish without a record.
- **`cref`'s figures:** single-build median 1,186 ms, max 2,548 ms. Production's `cref`-only figures were median
  1,197 and max 2,504.

**S4 (clock-driven): AS PREDICTED.**

| | |
|---|---|
| start | offset 45.058 s. `opuk`, **version `d0b57204-62d7-417f-8c5d-26a7eae6dae6`**, 19:22:05.101-19:22:08.721Z, exit 0, empty stderr, `schedule: * * * * *` |
| end | the 12th counted `stored` at **20:23:45Z** (seen 20:24:57Z); park on `idle`, **version `672798b2-b643-482d-a5b4-694817c52375`**, 20:25:05.102-20:25:13.403Z, exit 0, empty stderr, cron kept |
| read (`s_reads.py S4`, by version; twice, 20:28:31Z and 20:30:34Z, identical) | **63 records, all `ok`**, 0 unattributed. **N counted = 12** |
| graded set (the first 12 counted `stored`) | **median 2,055 ms, max 3,123 ms**: within [1,500, 3,000] and <= 4,000 |
| the rest | n 49, **median 12 ms**, max 24: <= 100 |
| beside, ungraded | the 2 backfill invocations, 3,321 and 1,775 ms; the firing after the 12th (20:24:45Z) also ran on S4's version |

- **`opuk` alone costs a median of about 2.1 s a build on Cloudflare's runtime** (max 3,123).
- R3's join had read 2,161 median and 2,948 max for `opuk`-alone invocations in production.
- A two-build backfill invocation read 3,321, about twice.

**R3-6's instrument counted 71 where the Logs have 63, and the cause is its sampling.**
- `workersInvocationsAdaptive` by minute: every minute reads 1 invocation at `sampleInterval` 1, except **19:27Z:
  "10 requests, 77.5 ms", at `sampleInterval` 10**.
- In the Logs, that minute holds one ordinary firing (19:27:45Z, 7 ms).
- So one invocation was sampled at 1/10 and scaled into 10. The dataset's sums are estimates, even at one
  invocation a minute.
- **That is a property of R3-6's instrument:** its counts and CPU sums can be inflated by a sampled row. Its status
  and error fields still saw S2's kills 10 for 10.
- **For the fix's rows:** a count or a total read from it needs `sampleInterval`, or the Logs, beside it. The 71
  was graded nowhere, and S4's row reads from the Logs.

**S5, started:** `off`, **version `1379f573-61bb-40ed-ad4f-f4f115e1e8fc`**, 20:32:05.104-20:32:09.016Z, exit 0, empty
stderr, **no schedule listed**. Read at the deploy + 1,500 s (20:57:09Z) and 5 min later, as re-registered.

**Ruling, pasted ~20:50Z (verbatim):**

> S4: AS PREDICTED (graded median 2,055 ms, max 3,123 ms, the rest 12 ms), in line with production's opuk-alone join (2,161 / 2,948). The two-build backfill is printed beside it as about twice that.
> The sampling finding is recorded: workersInvocationsAdaptive scaled one firing into 10 at sampleInterval 10. In the fix, every count or total read from that dataset carries its sampleInterval, or is taken from Workers Logs instead.
> S5 runs as registered, with reads at 20:57:15Z and 21:02:15Z.
> After S5, the dev Worker and blipscope-wx-dev stay in place, unscheduled. Deleting them is Daniel's call, after the fix has been proven. Then item 3, the fix, with its predictions frozen first.

### Item 3: the fix, proposed for a ruling (2026-11-05 ~20:36Z; no code before the ruling, and the PR's predictions come first)

**The shape: `opuk` in its own Worker, `blipscope-wx-ingest-opuk`. Recommended over a second cron in the same Worker.**

| | **own Worker** (recommended) | second cron, same Worker |
|---|---|---|
| CPU per invocation | one product per invocation | one product per invocation (each cron is its own invocation). **Equal** |
| `cpu_ms` | its own, sized for `opuk` | one value for both, sized for the larger |
| deploy and rollback | independent: an `opuk` change never redeploys `cref` | one version: rolling back `opuk` rolls back `cref` too |
| an upstream down (tonight's CloudFerro timeouts) | its own invocations, logs and errors | its own invocations; the logs are told apart only by their lines |
| instruments | its own `scriptName` in Workers Logs and GraphQL | one script, split by log line |
| cron | the new Worker's first cron: up to 15 min to start (measured 268.7 s, ~592 s, ~539 s) | a second cron expression on the existing Worker is also a cron change, up to 15 min. **Two identical `* * * * *` can't coexist**, so it needs a second spelling and dispatch on `controller.cron` |
| cost | +43,200 invocations a month, inside the 10 M included; CPU unchanged | the same |
| new parts | a second toml, entry and deploy target | dispatch on a cron string |

**The code (one PR):**
- **The `cref` Worker** (`blipscope-wx-ingest`, same toml, same `main`): its `scheduled` checks **`cref` only**,
  through `checkAll`'s product list. `checkAll`'s default stays both products, so PS1 is unchanged.
- **The `opuk` Worker:** a new entry, `src/wx/ingest-opuk-worker.ts`, with the same `:00` / `:30` shape, checking
  **`opuk` only**.
  - It has a stop switch, the var `WX_INGEST`: `on` checks `opuk`; anything else logs one `wx_ingest_off` line and
    does nothing.
  - A new `proxy/wx-ingest-opuk.wrangler.toml` binds `WX` -> `blipscope-wx`, with `WX_INGEST` `on`, and an
    **`[env.inert]`** with the same name, the cron kept and `WX_INGEST` `off`.
  - **Its stop is a version change, which is immediate, not a cron removal** (which lags 323 s or more). This is
    tonight's `idle` lesson.
- **`cref`'s rollback** stays `582612da`.

**Sizing `cpu_ms`, with the three margins ruled ~17:28Z and ~19:20Z:**

| Worker | measured single-build max | x2, a backfill | x1.6, the runtime spread | **proposed `cpu_ms`** | today |
|---|---|---|---|---|---|
| `cref` | 3,621 (production baseline 19-21Z; S3 2,548; R3 2,504) | 7,242 | 11,587 | **12,000** | 5,000 |
| `opuk` | 3,123 (S4; production 2,948) | 6,246 | 9,994 | **12,000** | (new) |

- **`cref`'s headroom row:** today's 5,000 was sized at 10x A4's 0.53 s. A two-build `cref` backfill at the
  baseline max would read about 7,242, already over it. S3's two-build invocation read 5,733.
- 12,000 is under the documented 30,000 cap for a sub-hourly Cron Trigger.
- **The limit is budgeted as `cpu_ms` itself,** never as the grace that S2's 2,100-at-100 suggests.

**The PR's checks, predictions to be frozen in it before its code** (each with a sabotage):
- **FT1:** the `cref` Worker's entry checks only `cref`. Sabotage: its list gains `opuk`, and FT1 goes red.
- **FT2:** the `opuk` entry checks only `opuk` when `WX_INGEST` is `on`. With anything else it logs one line and
  touches no bucket and no upstream. Sabotage: the off branch falls through to a check.
- **FB2:** PB2's method on the new toml. Its top level and `inert` each bind exactly one R2 bucket, `WX` ->
  `blipscope-wx`, with `WX_INGEST` as set. The `cref` toml is unchanged in bindings. Sabotage: one bound elsewhere.
- **Suite and CI counts, from a finished run.**

**R3′, the re-run, outlined here and frozen in full before its deploy:**
- **Order:**
  1. the `opuk` Worker first. It is new, so its first firing is up to 15 min away.
  2. then the `cref` Worker to main's `cref`-only code. Its cron is unchanged, so that is a version change.
- **CPU per Worker from Workers Logs invocation records**, not from adaptive sums. Every R3-6-style figure carries
  its `sampleInterval`.
- **Count rows allow the vanished-invocation margin**, about 1 in 60 to 1 in 90:
  - in 30 min, at most 2 minutes missing one `cref` check, and **at most 1 missing both**;
  - a vanished invocation is a minute missing both, and at that rate one in 30 min is likely, not rare.
- **Backfill is excluded from graded CPU sets**, as S4's was.
- **Rollback:**
  - `cref` -> `582612da`;
  - `opuk` -> `-e inert`, a version change.
  - The stored `opuk` frames stay. Deleting data is Daniel's.

**S5: AS PREDICTED (re-registered row, two reads).**

| | |
|---|---|
| start | `off`, **version `1379f573-61bb-40ed-ad4f-f4f115e1e8fc`**, 20:32:05.104-20:32:09.016Z, exit 0, empty stderr, no schedule listed |
| reads | 20:57:19Z and 21:02:19Z, identical |
| after the deploy | **6 invocations, every one on `1379f573`, every line `wx_dev_off`**. They ran 20:32:45 to 20:37:45Z, one a minute |
| the last | **20:37:45Z, 336 s after the deploy**, within 900 s |
| [deploy + 900 s, deploy + 1,500 s] = [20:47:09, 20:57:09] | **0 invocations** |
| end | the copy has no cron and has been quiet since 20:37:45Z. **It stays in place, unscheduled**, as ruled. Deleting it and `blipscope-wx-dev` is Daniel's call, after the fix |

**Data beside the row, not graded:**
- The last firing (20:37:45Z) ended **`canceled`** after 6.8 s of wall time, under three `wx_dev_off` lines: `:00`
  twice and `:30` once.
  - Readings: a firing cancelled as the cron's removal took effect and then delivered again under the same
    `scheduledTime`; or one invocation logged twice.
- R3-6's instrument shows **6 `success` + 1 `clientDisconnected`** where the Logs show 6.
  - Readings: the parked version's 20:31:45Z invocation was still running at S5's deploy and is counted by its end
    time; the cancellation reads as `clientDisconnected`; or sampling.
  - It isn't graded; S5's row reads from the Logs.
- **The removal took 336 s here, and 323 s before.**

### Plan S, complete (2026-11-05 03:12-21:02Z)

| phase | version | verdict |
|---|---|---|
| S0 | `b350ba72` (`off`) | as predicted (row re-registered ~03:30Z) |
| S1 | `13cd381e` (fifth attempt; four aborted: `85a35361`, `f967798b`, `5e4c9a8b`, `093b18f7`) | OFF on its +/-25% clause; **`C` = 9,480 ms**; identical work read 7,939-12,693 (ruled (a)) |
| S2 | `6ebea4e7` | **LIVE: 10 of 10 `exceededCpu`; R3-6 saw `exceededResources` 10/10** |
| S3 | `d06df358` | OFF: the max was a backfill (5,733) and one invocation vanished (59 of 60). **`cref` single-build median 1,186, max 2,548** (ruled (a)) |
| S4 | `d0b57204` | **AS PREDICTED: `opuk` alone median 2,055, max 3,123** |
| S5 | `1379f573` | **AS PREDICTED: last firing 336 s after the cron's removal, then quiet** |

**What Plan S established for the fix:**
- R3-6's CPU-limit gate can see a kill, and R3's 0 errors meant no kill.
- The dev copy measures what production measures.
- `opuk` alone builds in about 2.1 s, with a max of 3.1 s.
- Four facts the fix's predictions carry:
  - identical work can read 1.6x apart;
  - a backfill costs about twice a build;
  - about 1 in 60 to 1 in 90 invocations vanish;
  - adaptive GraphQL sums can be inflated by a sampled row.
- Cron changes take up to 15 min to start or stop; version changes are immediate.

**Ruling, pasted ~21:30Z (verbatim):**

> S5: AS PREDICTED. The cancelled last firing and the clientDisconnected row are recorded beside it, ungraded. Plan S is complete, and its table stands as you give it. The dev Worker and blipscope-wx-dev stay in place, unscheduled, until R3' has passed. Deleting them is Daniel's call.
>
> Item 3: accepted as proposed. Build it in one PR, with predictions first:
> 1. The shape: opuk in its own Worker, blipscope-wx-ingest-opuk, with the cref Worker checking only cref. checkAll's default stays both, so PS1 is unchanged.
> 2. The WX_INGEST stop, plus [env.inert] keeping the cron, so stopping opuk is an immediate version change.
> 3. cpu_ms 12,000 for each Worker, from the margins as tabled. cref's headroom row is recorded: today's 5,000 sits below a measured two-build cref backfill (5,733 ms).
> 4. FT1, FT2 and FB2, each with its sabotage. Add one more check: no two deploy targets across both tomls check the same product. Its sabotage gives the opuk entry cref as well.
> 5. R3', registered in full in the PR before any deploy:
>    - every row R3 had;
>    - per-Worker CPU bars from Logs invocation records, with sampleInterval beside any GraphQL figure;
>    - backfill excluded from graded sets;
>    - the vanished-invocation margins;
>    - the order: opuk first, then the cref Worker;
>    - both rollbacks.
>    When the PR is ready, I'll set its slot and say whether it needs a board hold.
> Cost: about $0.53 a month, under $2. Accepted.

**Item 3's PR: #556** (`feat/wx-opuk-worker`).
- Predictions and R3-prime (R3′) were frozen first at `61aece0a` (21:15:52Z, `docs/wx-opuk-split.md`). The code is
  `41da3b47f553`.
- Locally: 4/4 tests passed; FT1s, FT2s and FXs read as predicted; the selftest passed.

**#556's CI at `41da3b47f553`, finished (read 21:42Z):**

| | predicted (`61aece0a`) | got | verdict |
|---|---|---|---|
| proxy suite | 915 passed, 1 skipped (916); files 70 + 1 (71) | **915 passed, 1 skipped (916); files 70 + 1 (71)** | as predicted |
| the plant selftest | PASSED; PB1 production 0 and `opuk` 0; FB2 2/2; FB2s FAIL on `inert` alone | **as predicted** (PB1 0/0, anchors 1/1, dev 1; FB2 PASS; PB1s FAIL 1; PB2s FAIL `cref`; FB2s FAIL `inert`) | as predicted |
| **check-runs on the head** | **41** (38 success, 3 skipped) | **34** (31 success, 3 skipped); all four workflows `success` | **OFF** |

**The count's cause, read from the two heads' check-run lists:**
- Compared with #543's head (41), #556's lacks exactly the eight `build (*-s3-146)` legs and has one new
  `ota-identity` job.
- Main's firmware workflow changed between the two bases:
  - #549 (`dd1246e0`) retired the 1.46" SKUs from CI;
  - `84409651` added the OTA identity job.
- 41 - 8 + 1 = 34, and every one concluded. **This PR changes no firmware workflow file.**
- **The miss is mine:** I took the basis from earlier PR heads instead of from the base's own workflow at the time.
- **The alternative,** jobs that failed to start, is excluded: the two lists differ by exactly those nine names, and
  all 34 concluded.

**Ruling, pasted 2026-11-06 ~00:56Z (verbatim):**

> 1. B is accepted as read. Delete the frames now (allowed). The record goes in with your next docs PR, not a PR of its own. b_survey.mts stays out of the repo; its sha is recorded.
> 2. #556's count: miss kept, re-registered to 34, provided it splits 31 success, 3 skipped, 0 failed (the shape of #548's finished run on the same workflows). The cause: the baseline (#521/#522) predates #549's s3-146 retirement (−8) and #552's ota-identity job (+1): 41 − 8 + 1 = 34. If the split differs, stop and report. I review #556 once its report reaches me.
>    - Rule for all sessions: a CI baseline must postdate main's last workflow change, named with the prediction.
> 3. Item A's three stopped datasets stay held, unserved, until Daniel decides, and A.3 waits as you proposed. For the record:
>    - LPS's written answer (2026-11-04) named all four datasets for this use, NISRA's boundaries included.
>    - Both discrepancies point back to LPS: the mirrors' "LPS Open Government Data Licence", and NISRA's exemption of OSNI-derived mapping from the OGL, which leaves that mapping to LPS.
>    - Daniel is choosing between relying on that answer and asking LPS to confirm the two points first.
>    - Either way, DAERA's line and NISRA's "Source: NISRA : Website: www.nisra.gov.uk" sit beside the OSNI line wherever it shows. The second acknowledgement form you couldn't trace is not used.
> 4. One whole-suite run at a time on Daniel's PC (all sessions). Before any whole-suite vitest or sabotage driver: no other session's vitest or sabotage- process running (by name and start time only), and TIME_WAIT under 2,000. If busy, poll for up to 20 minutes, then report.
> 5. R4 and node_modules stay held.

**Acted on:**
- **B's frames deleted** (2026-11-06 ~00:58Z): the 24 `.tiff` files, 45 MB, removed by literal name from my scratch.
  `frames/manifest.json`, with each frame's sha256, is kept. `b_survey.mts` stays out of the repo; its sha256 is
  `5a9590f0fac754020e1d7df563dc5dad3908293b1992bb52a56c21f197eef5c5`.
- **#556's count under the re-registration:**
  - **34 check-runs: 31 success, 3 skipped, 0 failed**, read from the finished head at `41da3b47f553`. That is the
    shape required, so **as re-registered**. The miss stays in the record.
  - **The basis this time:** main's last workflow changes before #556's base were #549 (`dd1246e0`, the s3-146
    retirement) and the OTA identity commit `84409651` (the ruling's #552). Both predate `7cca8983`, the base #556
    was cut from.

**Ruling, pasted 2026-11-06 ~02:40Z (verbatim):**

> 1. #557 is approved to merge at d155686cba5d once its CI reads 31: 28 success, 3 skipped, 0 failed. It merges clean with main.
>    - My scan of the added lines: 0 email addresses, 0 phone numbers, 0 personal names; OSNI's line verbatim with the OGL link; DAERA's and NISRA's lines present.
>    - One wording fix for your next docs change, not blocking. "LPS's answer named all four datasets" should read "LPS's answer covers the four datasets Daniel's question named", since LPS wrote "the datasets you intend to use". The imprecise wording was mine (~00:56Z and ~00:58Z).
> 2. Order: #556 first, since R3′ waits on it, then #557. Before each merge, wait until main's CI on its current head has finished, so each main reading stays attributable. Squash, --match-head-commit, no --admin. Predict main's CI for #557 as on 12569fa0 (31: 29 success, 2 skipped).
> 3. The NI gate lifts when #557 merges.
> 4. The baseline rule, refined for all sessions: a baseline must postdate main's last change to the set of jobs. A step added to an existing job doesn't count. Name the last workflow change either way.
> 5. The ~01:04Z block stands for #556 and R3′.

- **#557's condition is met:** its CI read 31: 28 success, 3 skipped, 0 failed at 01:16:26Z.
- **Neither PR is merged yet.** The ~01:04Z block that the ruling says stands for #556 and R3′ never reached this
  session. No text was pasted between ~00:58Z and ~02:40Z. #556's approval is in that block, and the order puts
  #556 first, so both merges wait for it.
- **The wording fix is queued for the next docs change:** "LPS's answer covers the four datasets Daniel's question
  named". That change touches #557's A1.1d after it merges, and this record.

**Ruling, pasted after ~02:40Z, dated 2026-11-06 ~01:04Z (verbatim). This is the block the ~02:40Z ruling's item 5 refers to:**

> 1. #556 is approved to merge at 41da3b47f553, with CI at 34: 31 success, 3 skipped, 0 failed (as re-registered, miss kept).
>    - My review:
>      - the opuk Worker ingests only when WX_INGEST is "on"; any other value stops it;
>      - the cref Worker checks only cref;
>      - each toml binds exactly WX -> blipscope-wx, and inert repeats the binding and keeps the cron;
>      - FX reads the tomls as they deploy, with a whole-list target compare and a control;
>      - both tomls state their cpu_ms reasoning.
>    - My runs on the merge with today's main (7b70745, which now includes #548 and #545):
>      - tsc clean;
>      - vitest 964 passed, 1 skipped (main's 960 plus your 4);
>      - check-wx-plant.mjs PASS: PB1 production 0, opuk 0, anchors 1 and 1, dev 1; PB2 7 of 7; FB2 on and off.
>    - Merge only after main's CI on 5c30041 (#548) and 7b70745 (#545) has finished, so each reading stays attributable. Squash with --match-head-commit 41da3b47f553, no --admin. Predict main's CI from #548's main run once it has finished; that run postdates main's last workflow change.
> 2. R3′ is registered as written, plus one row before R3′-1a:
>    - R3′-pre: the running cref Worker (582612da) logs only `cref` product lines over the last 10 minutes. If any `opuk` line shows, stop, because opuk would be built twice during the gap.
> 3. Your slot: R3′ starts once both of these are true:
>    - #556's main CI reads as predicted;
>    - Daniel pastes blipscope-5c's "#548 production verified" line (one deployer at a time).
>    There is no latest start. Production rollbacks stay yours, at once, as registered.
> 4. A board hold covers R3′: no board window in blipscope-5c or Weatherscope from R3′-1a until your R3′-8 read (30 minutes after R3′-1b). Daniel pastes the hold, and it ends when he pastes your R3′-8 line to them.
> 5. Cost: the 12 s caps put a ceiling of about $20 a month on a runaway month (both Workers at the cap every minute). R3′-6 guards the first 2 hours, and R3′-0 stays the gate (under $2). Re-run R3′-0's cost read 7 days after R3′ and report it.

**Before #556's merge (2026-11-06 ~02:50Z):**
- **Main's CI on both heads had finished:**
  - `5c300417` (#548): 34, 32 success, 2 skipped;
  - `7b70745a` (#545): 31, 29 success, 2 skipped.
  - Main's head was still `7b70745a`.
- **Predicted for main's CI on #556's merge commit**, read only from a finished run: **34 check-runs, 32 success,
  2 skipped, 0 failed; proxy suite 964 passed, 1 skipped (965).**
  - **Baseline:** #548's main run at `5c300417`, the last main push that ran the Workers workflow.
  - **Main's last workflow change** is `12569fa0`, which added steps only. The last change to the set of jobs is
    `84409651` (#552, `ota-identity`), and #548's run includes it.
  - The suite count is the reviewer's run of the merge with `7b70745a`: main's 960 plus #556's 4.

**#556 merged:** squash, `--match-head-commit 41da3b47f553`, no `--admin`, at 03:00:58Z.
- **Merge commit `9db83163a60d`, 03:01:01Z.** The remote branch is deleted.

**A miss of the ~02:40Z rule, mine.** The rule says to wait until main's CI on its current head has finished before
each merge.
- At 02:50Z main's head was `7b70745a`, with CI finished. By the merge it was **`259e8688`** (Q82's
  `check_binary_strings.py` gaps, another session's), pushed at about 03:00:32Z, 26 s before my merge.
- Its firmware and device-ids runs were still in progress. **I didn't re-read main's head right before merging.**
- **What it cost, checked:**
  - Every workflow's push concurrency group is its own `run_id`, with `cancel-in-progress` only for PRs. So my
    merge couldn't cancel `259e8688`'s runs, and each commit's runs stay attributable by its SHA.
  - `259e8688` changed `.github/workflows/device-ids.yml` by **adding a step** to the one existing job
    (`no-real-device-ids`). The set of jobs is unchanged, so the prediction's basis stands under the refined rule.
    **Main's last workflow change is now `259e8688`, steps only.**
- **For #557's merge:** main's head is re-read, and its CI checked finished, in the same command that merges.

**Main's CI on #556's merge `9db83163a60d`, finished 03:12:53Z: as predicted.**
- **34 check-runs: 32 success, 2 skipped, 0 failed.** The workflows were device-ids, firmware, no-traffic-captures
  and workers, all `success`.
- **Proxy suite 964 passed, 1 skipped (965).**
- `259e8688`'s own run finished separately, 03:10:43Z: 31, 29 success, 2 skipped.
- **R3′'s first condition is met** (#556's main CI as predicted). The second, Daniel pasting blipscope-5c's "#548
  production verified" line, is awaited.
- **The deploy checkout** is `wt-plan-s`, detached at `9db83163a60d`: clean, lockfile unchanged, `node_modules`
  valid.

**#557's merge, guarded:** main's head is re-read, and its CI checked finished with 0 failures, in the same command
as the merge.
- At 03:13:19Z main had moved again, to `9b511dc274c3` (another session's `/basemap` A1 code, no workflow change),
  with one run pending. **The guard refused, and nothing was merged.**
- A background loop retries the same guard every 30 s for up to 40 min. It merges only when the head it read is
  still main's head and that head's CI has finished clean.

**Ruling, pasted 2026-11-06 ~03:25Z (verbatim):**

> 1. #557 merged: recorded, and the NI gate is lifted. Report main's CI on ac5f8509 when it finishes.
> 2. The board hold is in place in both sessions. Weatherscope confirmed it at ~03:15Z, and it is restated in blipscope-5c's block of this time. R3′ starts when Daniel pastes blipscope-5c's "#548 production verified" line, in the registered order: R3′-pre, R3′-0, R3′-1a.
> 3. R4 and node_modules stay held.

- **The board hold is in place.** R3′ waits only for the "#548 production verified" line.

**Ruling, pasted 2026-11-06 ~04:22Z (verbatim):**

> 1. Main's CI on ac5f8509: recorded, as predicted.
> 2. R3′'s second condition is met, by ruling: #548's production deploy is deployed and checked (blipscope-5c, ~04:00Z). Its remaining steps (a one-board re-seed, the token-id sweep, C1) deploy nothing. Start R3′ now, in the registered order: R3′-pre, R3′-0, R3′-1a. The board hold is in place.
>    - The map server's A1 staging slot is running alongside. It is staging only and shares no Worker with R3′.
>    - If blipscope-5c has to roll back #548's production deploy during R3′, Daniel tells you. Mark any R3′ row read across it.
> 3. A merge rule for all sessions:
>    - wait for main's head H to finish its CI;
>    - in the same command as the merge, re-read origin/main and merge only if it is still H;
>    - afterwards, check the squash tree against merge-tree(H, PR head).
> 4. R4 and node_modules stay held.

**The rule's after-check, applied to both of my merges** (read-only):
- **#556:** merge-tree(`259e8688`, `41da3b47f553`) = `2d220734ead1`, and `9db83163`'s tree = `2d220734ead1`.
  **Equal.** The squash's parent is `259e8688`.
- **#557:** merge-tree(`9b511dc274c3`, `d155686cba5d`) = `7fae89fc0fd2`, and `ac5f8509`'s tree = `7fae89fc0fd2`.
  **Equal.** The squash's parent is `9b511dc2`.

### R3′ (2026-11-06, from ~04:25Z)

| row | got | verdict |
|---|---|---|
| **R3′-pre** (04:13-04:23Z, read 04:24:39Z, ABR 1) | the running `cref` Worker is `582612da` (100%, since the 00:18:33Z rollback on 11-05). Its Logs show **20 product lines, all `cref`**; 10 of 10 minutes have both checks; 0 error lines. The `opuk` Worker doesn't exist yet (`code: 10007`) | as predicted |
| **R3′-0** (read 04:24:54Z, `cost_read.py`) | **UNDER $2**: run-rate projection $0.25-0.30 a month with `opuk`. **Measured re-read: about $0.52 a month** (run rate 37.48 M CPU ms + `opuk` at S4's 2,055 ms x 8,640 builds + 12 ms x 43,200 = 55.76 M; Class A 0.95 M, under 1 M) | as predicted |
| **R3′-1a** (`npx wrangler deploy -c wx-ingest-opuk.wrangler.toml` from `9db83163a60d`, clean, no token) | **04:25:51.382-04:25:53.622Z, exit 0.** Version **`ea5970ba-8e83-40d6-bf27-e5f59b983514`**; the deploy listed `schedule: * * * * *`. Version record: **exactly one R2 binding, `WX` -> `blipscope-wx`; `WX_INGEST` = `on`; no other binding; `cpu_ms` 12000.** stderr held one wrangler WARNING: several environments are defined and none was given, so the top level was deployed, which is the target intended. The rollback is the stop, `-e inert` (a new Worker has no earlier version) | as predicted |
| **R3′-10** (Logs, `blipscope-wx-ingest-opuk`, polled each minute) | **first firing `scheduledTime` 04:33:14Z, 441.9 s after R3′-1a**, seen at 04:33:40Z | as predicted (within 900 s) |
| **R3′-3** (`/healthz`, 04:43:24Z, the first firing + 10 min) | `opuk` `frameAgeS` **505**, `coveredShare` **0.750769** | as predicted (<= 1,800; in [0.70, 0.80]) |
| **R3′-4** (`/wx` probe as the test identity, 04:43:25Z and 04:43:29Z, identical) | **LHR:** 200, `WXC1`, ETag `opuk`, `X-Wx-Max-Age: 720`, `X-Wx-Frame` 1793939700 (04:35:00Z), which is `opuk`'s latest by its `frameAgeS`. **KMCI and Bend:** 200, `WXC1`, ETag `cref`, no `X-Wx-Max-Age`, `X-Wx-Frame` 1793940040 | as predicted |
| **R3′-1b** (`npx wrangler deploy -c wx-ingest.wrangler.toml` from `9db83163a60d`, clean, no token) | the rollback target, read just before: **`582612da-e1b0-4a02-bf9e-de72c5d78157`** (100%). **04:44:00.284-04:44:03.622Z, exit 0, empty stderr.** Version **`257f49eb-6c01-45e5-94fa-4fd3a7e268eb`** (100%, created 04:44:02.555Z); `schedule: * * * * *` listed. Version record: **exactly one R2 binding, `WX` -> `blipscope-wx`; no vars; `cpu_ms` 12000** | as predicted |
| **R3′-2's gate** (`/healthz` `wx.cref.frameAgeS`) | **152 / 91 / 92 / 94 / 90 s** at +5, +10, +20, +30, +60 min after R3′-1b (04:49:07, 04:54:06, 05:04:08, 05:14:04, 05:44:07Z) | as predicted (<= 600 at every read) |
| **R3′-2's log half** (`blipscope-wx-ingest`, 04:45:00-05:15:00Z, read 05:15:45Z, ABR 1) | **30 of 30 minutes with both of `cref`'s checks; 0 missing one; 0 missing both; 0 error lines** | as predicted (<= 2 missing one, <= 1 missing both) |
| **R3′-9** (the same window, by service) | `blipscope-wx-ingest`: **60 product lines, all `cref`**. `blipscope-wx-ingest-opuk`: **60, all `opuk`** (stored 6, already-stored 54) | as predicted |
| **R3′-8** (the fleet, 04:44:03-05:14:03Z) | **661 requests, 11 devices, 410 `/blips`, 4xx 0, 5xx 0**; two identical reads, 05:45:01Z and 05:47:34Z | as predicted |

**R3′-8's first two reads were an instrument failure, not a reading.**
- The schedule (05:17 and 05:20Z) passed `pd6_fleet.py` ISO times with `T` and `Z`. Its SQL wraps them in
  `toDateTime('...')`, which takes `YYYY-MM-DD HH:MM:SS`, so the API answered **HTTP 422** both times.
- The window is historical, so it was read again in the script's own format, twice, at 05:45 and 05:47Z. Those
  two reads are the row's reading.
- The fault was in my call, not the fleet or the script. PD6 and R3-8 were read with the right format.
| **R3′-5b′** (`r3_5b.py opuk 150 blipscope-wx-ingest-opuk`; twice, 10 min apart) | **read 1, listed 05:36:02Z:** 12 frames, 04:35-05:30Z; last storing check started 05:34:44.872Z (cross-check -0.5 s), `pruned` 1; 0 below the cut; the sabotage puts 1 below, the oldest; margin 15.1 s; **PASS**. **Read 2, listed 05:45:43Z:** 12 frames, 04:45-05:40Z; last storing check started 05:44:15.491Z (+0.3 s); 0 below; the sabotage FAILs on the oldest; margin 44.5 s; **PASS** | as predicted |
| **R3′-5a** (`blipscope-wx-ingest-opuk`, 04:25:53-05:35:53Z, read 05:36:00Z, ABR 1) | `pruned` on **stored 14 and backfilled 2 only**. Then 0 on every storing check until 05:14:22Z, and **1** on each from 05:19:20Z. **But the first storing check (04:33:19Z) pruned 0, not 12** | **OFF**, on "the first storing check prunes exactly 12" |

**R3′-5a's escape: the `opuk` Worker stopped first, as registered.**
- R3′-5a isn't one of the rows that roll nothing back alone, so `npx wrangler deploy -c wx-ingest-opuk.wrangler.toml
  -e inert` ran at **05:48:35.534-05:48:38.067Z**: exit 0, empty stderr.
- Version **`a1c8c3b9-e905-487a-984c-032a65c7fca9`** (100%), with `WX_INGEST` `off` and the cron kept. Before it,
  `ea5970ba` was at 100%.
- The `cref` Worker (`257f49eb`) is untouched. The stored `opuk` frames stay.

**The cause, read-only:** `wrangler r2 bucket lifecycle list blipscope-wx` shows a rule **`expire-1d`: "Expire
objects after 1 days", for all prefixes, enabled**.
- R3's 12 stale frames were written from about 23:14Z on 11-04 to 00:09Z on 11-05.
- At my 21:13:50Z listing on 11-05 they were 21-22 h old. By R3′-1a they were 28-29 h old, past the rule's
  threshold, so they had been expired before `opuk`'s first storing check could prune them.
- **The miss is mine:** I took "exactly 12" from a listing 7 h old and didn't read the bucket's own lifecycle rule.
- **Alternatives, not excluded outright:**
  - a deletion by another session or by a person; nothing points to one, and deleting data is Daniel's;
  - the old `cref` Worker pruning `opuk`'s prefix; its `pruneOld` lists `framesPrefix(p)` for its own product.
  - The rule fully explains the 0.
- **The prune itself works:** later checks prune 1 frame each, and R3′-5b′ passed both listings.
- **Beside it:** the same 1-day expiry applies to `wx/*/latest`. A product whose ingest stops for over a day loses
  its `latest` pointer too.

| row | got | verdict |
|---|---|---|
| **R3′-6, `opuk` Worker** (Logs invocation records, 04:25:53-06:25:53Z, read 06:30:04Z, ABR 1). **Read across the stop at 05:48:35Z;** invocations after it build nothing and count among "the rest" | 112 records, **all `ok`, 0 `exceededCpu`**. Single-build: n 14, **median 1,612.5 ms, max 2,545 ms**. The rest: n 96, median 7 ms, max 19. Beside: two backfill invocations (backfilled + stored), 2,460 and 1,876 ms. p99 of all: 2,497 ms. GraphQL: `success` 113, 0 errors, `sampleInterval` 1 | as predicted (median in [1,280, 3,460]; max <= 5,000; rest <= 100) |
| **R3′-6, `cref` Worker** (04:44:03-06:44:03Z, read 06:49:00Z, ABR 1) | 120 records, **all `ok`, 0 `exceededCpu`**. Single-build: n 60, **median 1,051.5 ms, max 2,606 ms**. The rest: n 60, median 18 ms, max 40. No backfill. p99 2,329 ms. GraphQL: `success` 119 (one fewer than the Logs' 120 at the window's edge), 0 errors, `sampleInterval` 1 | as predicted (median in [740, 2,125]; max <= 5,800; rest <= 100) |
| **R3′-7** (`opuk`'s `wx_ingest_error` lines, 04:25:53-06:25:53Z) | **0 error lines.** Statuses: already-stored 134, stored 16, backfilled 2 | printed, not graded |

- **The state at 06:49Z:**
  - the `cref` Worker is `257f49eb`, live and `cref`-only;
  - the `opuk` Worker is on its stop, `a1c8c3b9` (`WX_INGEST` off, cron kept).
  - It waits for the ruling on R3′-5a.

**Ruling, pasted 2026-11-06 ~15:07Z (verbatim):**

> 1. R3′-6 and R3′-7: recorded, as predicted.
> 2. R3′-5a: (a). It's off on the "first store prunes exactly 12" clause, miss kept. The cause: the bucket's expire-1d rule had removed R3's 12 stale frames before R3′ ran, and the registration (from the 21:13:50Z listing) didn't account for the lifecycle's timing. Your stop at 05:48:35Z followed the escape as registered.
> 3. Redeploy `opuk` at the top level (WX_INGEST on), from the same clean checkout of 9db83163. Record the rollback target (`-e inert`) first. Re-registered before the redeploy:
>    - a listing just before it, counting the `opuk` frames older than an hour;
>    - the first storing check prunes exactly that many, then at most 1 per storing check;
>    - R3′-5b′ re-read at +70 min, read twice;
>    - R3′-3 and R3′-4 (LHR) re-read after the first firing.
>    If these read as predicted, R3′ is complete. No board hold is needed for this redeploy.
> 4. The 7-day cost re-read around 2026-11-13: yes.
> 5. R4 and node_modules stay held.

**Before the `opuk` redeploy (frozen at about 15:11Z):**
- **The rollback target:** `-e inert`. The current `opuk` version is **`a1c8c3b9-e905-487a-984c-032a65c7fca9`**
  (inert, 100%). `cref` is `257f49eb` (100%).
- **The listing, just before** (`r3_list.py wx/opuk/frames/`, 15:11:00Z): **12 frames, all older than an hour**.
  The newest is 34,261 s old (05:40:00Z), the oldest 37,561 s (04:45:00Z). So **N = 12**.
- **Re-registered, as ruled:**
  - **R3′-5a′:** the first storing check after the redeploy prunes **exactly 12**, then at most 1 per storing
    check.
  - **R3′-5b′:** re-read at the redeploy + 70 min, twice, 10 min apart, as ruled ~23:00Z on 11-04.
  - **R3′-3 and R3′-4 (LHR):** re-read at the first firing + 10 min, as registered.
- **No board hold**, as ruled.

**The `opuk` redeploy (2026-11-06):**
- `wrangler deploy -c wx-ingest-opuk.wrangler.toml` from the clean checkout of `9db83163`, top level: start
  15:11:27.109Z, end **15:11:30.916Z, exit 0**, stderr 0 bytes. Bindings printed: `env.WX` (blipscope-wx),
  `env.WX_INGEST` ("on"); schedule `* * * * *`.
- **The version record** (`versions view --json`): **`a4e6c685-295b-4f7b-a839-e1ad12c633a5`**, bindings exactly
  `WX` -> `blipscope-wx` and `WX_INGEST` = "on", `cpu_ms` 12000. As registered.
- **First firing:** scheduledTime **15:12:14Z**, 44.9 s after the deploy's end (a version change, so no cron
  propagation).
- `cref` stays `257f49eb`, untouched.

**Asked ~15:15Z: where the "exactly 12" comes from (answered before the 15:36Z read; no Logs line read yet):**
- **The source is the listing just before the redeploy,** as re-registered:
  - `r3_list.py wx/opuk/frames/` at **15:11:00Z** counted 12 frames, all older than an hour.
  - It was committed in `bc80a4b9` at 15:11:24Z and pushed at 15:11:26Z (GitHub's branch activity). The deploy
    started at 15:11:27.109Z.
  - It is not R3's figure carried over.
- **Why it matches R3's 12:**
  - Both are one full hour of 5-minute frames, the set the last running check kept.
  - Here that is R3′'s last hour before its stop at 05:48:35Z: 04:45:00Z through 05:40:00Z, 12 frames.
  - The 1-day lifecycle cannot remove them before about 04:45Z on 11-07.
- **One step checked in the code (`ingest.ts` at 9db83163, lines 253-263):**
  - `pruneOld` spares the frame `latest` points to, and in the listing that is the 05:40:00Z frame.
  - But a storing check writes its frame, moves `latest` to it, and only then prunes. So all 12 are older than
    the cut and none is `latest`'s.
  - The prediction stays **exactly 12**. Any other count is off.

**R3′-3 and R3′-4 after the redeploy (first firing 15:12:14Z + 10 min; read 15:22:19Z):**

| row | got | verdict |
|---|---|---|
| **R3′-3** (`/healthz` `wx.opuk`) | `frameAgeS` **440**, `coveredShare` **0.7508** (`cref` 102 beside it) | as predicted (age <= 1,800; share in [0.70, 0.80]) |
| **R3′-4** (`pd-wx.sh`, LHR) | `/wx/cref` LHR: HTTP 200, `WXC1`, ETag product **`opuk`**, `X-Wx-Max-Age` **720**, `X-Wx-Frame` **1793978100** (15:15:00Z). `/wx/opuk` LHR the same. `/healthz` `opuk` 444 s at the same read, so its frame is 15:15:00Z, the one served | as predicted |
| controls in the same probe | `/wx/cref` Bend and KMCI: ETag `cref`, no `X-Wx-Max-Age`, frame 1793978437 (15:20:37Z) | as before |

**R3′-5a′ (`r3p.py lines blipscope-wx-ingest-opuk`, 15:11:30-15:35:30Z, read 15:36:04Z, ABR 1):**

| row | got | verdict |
|---|---|---|
| **R3′-5a′** | **First storing check: 15:12:21Z, `stored`, `pruned` 12.** The six after it: backfilled 15:12:47Z and 15:13:28Z, then stored at 15:14:58Z, 15:19:24Z, 15:24:21Z and 15:29:21Z, **each `pruned` 0** | as predicted (exactly 12, then at most 1 each) |
| beside it | 45 product lines, all `opuk`. Statuses: already-stored 38, stored 5, backfilled 2. `pruned` appears only on storing checks (7 of 7). **0 error lines** | printed, not graded |

- The two backfills filled the holes left in the 20-minute window by the stop.
- Pruning of 1 per check starts once the first frames written after the redeploy pass an hour old. Their epochs
  are not in these lines, so no time is given here. R3′-5b′ reads that state.

**R3′-5a′ over the whole window, and an instrument note:**
- The scheduled full-window read (`r3p.py lines`, 15:11:30-16:21:30Z, read 16:21:52Z) came back **sampled at
  ABR 10**: 23 events against 69 for the 24-minute read at 15:36Z.
  - It missed the 15:12:21Z check, so it named the 15:12:47Z backfill as the "first storing check".
  - That is the sample, not the row. Every storing check it did see appears in the unsampled reads with the same
    `pruned` (15:12:47Z backfilled 0; 15:54:23Z stored 0; 16:04:25Z stored 1). The alternative, that the sampled
    read is right and the full ones wrong, is excluded because it is a strict subset of them.
- **Re-read unsampled** in 15-minute pieces at 16:22:15Z, **all at ABR 1, 0 error lines:**

| piece | lines | storing checks and `pruned` |
|---|---|---|
| 15:11:30-15:35:30Z (the 15:36Z read) | 45 | **15:12:21Z stored 12**; 15:12:47Z bf 0; 15:13:28Z bf 0; 15:14:58Z 0; 15:19:24Z 0; 15:24:21Z 0; 15:29:21Z 0 |
| 15:35:30-15:50:30Z | 30 | 15:39:28Z 0; 15:44:56Z 0; 15:49:50Z 0 |
| 15:50:30-16:05:30Z | 30 | 15:54:23Z 0; 15:59:21Z 1; 16:04:25Z 1 |
| 16:05:30-16:21:30Z | 32 | 16:09:23Z 1; 16:14:22Z 1; 16:19:52Z 1 |

- **R3′-5a′ as predicted over the whole window:** exactly 12 first, then 0 or 1 on each of the 15 storing checks
  after it.
- `r3p.py` prints `abr_levels` but does not refuse a sampled read. It should, the way a BLIND read refuses. Noted
  for the next tools change, not made here.

**R3′-5b′ (`r3_5b.py opuk 150 blipscope-wx-ingest-opuk`):**

| read | got | verdict |
|---|---|---|
| **1** (listed 16:21:54Z) | **12 frames** (15:20:00Z to 16:15:00Z), `latest` 16:15:00Z. Last pruner: 16:19:44.872Z, stored, `pruned` 1. Cut 15:19:44Z: **0 frames below**. +300 s sabotage (cut 15:24:44Z): **FAIL on the oldest**, 15:20:00Z. Margin 15.1 s (printed). Blind: none | **PASS** |
| **2** (listed 16:31:35Z) | **12 frames** (15:30:00Z to 16:25:00Z), `latest` 16:25:00Z. Last pruner: 16:29:15.897Z, stored, `pruned` 1. Cut 15:29:15Z: **0 frames below**. +300 s sabotage (cut 15:34:15Z): **FAIL on the oldest**, 15:30:00Z. Margin 44.1 s (printed). Blind: none | **PASS** |

- **The two reads were 9 min 41 s apart, not 10 min.** The driver took an expected 170 s run time off the wait,
  and read 1 finished sooner than that. Both reads are clean, and they are distinct states: different oldest
  frames, different last pruners.

**R3′: every re-registered check read as predicted. As ruled ~15:07Z, R3′ is complete.**
- `opuk` Worker: `a4e6c685`, top level, `WX_INGEST` on, cpu_ms 12000.
- `cref` Worker: `257f49eb`, `cref`-only.
- The 7-day cost re-read is due about 2026-11-13. R4 and node_modules stay held.
