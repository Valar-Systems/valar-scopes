# OPERA's coverage by country: a read-only survey (2026-11-05)

**For:** Daniel, as input; this is not a build plan. **Status:** accepted as read by the reviewer
(2026-11-06 ~00:56Z). The frames were deleted afterwards. The script, `b_survey.mts`, stays out of the repo; its
sha256 is recorded below.

## The ask (the reviewer chat, pasted by Daniel, 2026-11-05 ~21:20Z), verbatim

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

## Predictions, frozen before any code (2026-11-05 21:38Z)

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

## The reading (graded run 2026-11-05 23:09:00Z)

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
  ne/ne_10m_admin_0_countries.geojson $REF 20261105T2000`, where `$REF` is the 20:00Z frame's logged `coveredShare` in full (0.750772231862 to 12 places; a 16-digit run trips the device-id hook). Output sha256
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

## The ruling on it (2026-11-06 ~00:56Z), verbatim, its B part

> 1. B is accepted as read. Delete the frames now (allowed). The record goes in with your next docs PR, not a PR of its own. b_survey.mts stays out of the repo; its sha is recorded.

- **The frames were deleted** at about 00:58Z on 2026-11-06: the 24 `.tiff` files, 45 MB, from my scratch. Their
  manifest of sha256s stays in my scratch.
