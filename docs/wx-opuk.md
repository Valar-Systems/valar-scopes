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
