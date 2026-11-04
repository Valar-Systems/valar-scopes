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
