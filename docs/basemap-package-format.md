# The basemap package, format v1 (`BMPK`)

**Status: v1, frozen (2026-10-26).**
- **Accepted** by the reviewer, with three changes made before the freeze ([weatherscope-stage3.md](weatherscope-stage3.md) A10.10):
  1. "Bus" is matched as a whole word;
  2. Weatherscope's terrain levels are 3 px wider on every side;
  3. `--check` rebuilds with the recorded builder commit.
- **The questions marked ★ in the draft are now ruled.** Each is marked "ruled" with its question number.
- **From here, a change** to any field's meaning or any layer definition is **version 2** (§12).
- **Amended within v1 (2026-10-26; the reviewer's ruling "BMPK v1 amendments and the fixtures", recorded in [weatherscope-stage3.md](weatherscope-stage3.md) A10.12).** Clients never parse PROVENANCE, so none of these is a version change:
  1. **PROVENANCE by reference** (§7): per layer, a file count and one digest over the layer's files; the per-file list lives in a manifest;
  2. **the terrain credits corrected** (§7): credit what a mosaic contains; no 3DEP line;
  3. **the builder readings RD1-RD17 ruled for both builders** (§13), from the map server's plan (`docs/basemap-route.md` on `feat/basemap-route` at `e0c43ce`, its §10).
  4. **more readings, ruled 2026-10-28** (§13): RD18-RD21, RD-P1-RD-P6, F1 and F2, from the map server's frozen truth tables (`644a364`, its Addendum 1), with the credit and processing texts.
  5. **two refusals named, 2026-10-29** (§8, §11; [weatherscope-stage3.md](weatherscope-stage3.md) A12.15 and A12.44): a TERRAIN `count` other than 1 (`kTerrainCount`) and a PROVENANCE `count` other than its line count (`kProvenanceCount`), each with its own negative package. Both are §3's "exactly `count` items" applied to §4's `count` column, so neither is a version change.
- **Nothing described here is built yet.**
- **Asked for by:** the reviewer's ruling on Weatherscope step 3.5b ([weatherscope-stage3.md](weatherscope-stage3.md) A9.1 D, A9.9 item 2, A9.21's second paste).
- **Who builds what:**
  - **the map server** (a session of its own since 2026-10-26, taking over from blipscope-5c) builds the ingest and the Worker route from this document;
  - Weatherscope builds the device side, and a host tool that writes the same format for a stored Bend package. That package is §11's golden package.
- **What ships first:** Weatherscope reads it first. Blipscope adopts it after v19, as a setting.

**What it is.** A package holds the geography under a round map, for one location and one radius. The parts:
- **water areas:** lakes and reservoirs, filled;
- **rivers;**
- **major roads;**
- **peaks;**
- **places**, with their populations;
- **optionally, terrain shading,** at one or more view sizes.

**What it is not.** It carries **nothing product-specific**:
- no colours;
- no label rules, caps or fonts;
- no screen size, except the terrain levels a client asked for;
- no product names.

A client decides how each layer looks.

**The radius is a parameter.** Weatherscope asks for 97 km (its 60 mi view, rounded up to whole km, as `/wx` takes `r`). A Blipscope at another range asks for that.

---

## 1. Conventions

| | |
|---|---|
| byte order | little-endian, as WXC1 (`include/WxCrop.h`) |
| `varint` | unsigned LEB128, as WXC1's run lengths. **At most 5 bytes;** an encoding with a redundant trailing `0x80…00` group is **overlong and refused** |
| `svarint` | a signed value, zigzag-encoded (`(v << 1) ^ (v >> 31)`), then `varint` |
| text | UTF-8, a `u8` length then the bytes, no terminator; the source's own spelling and case |
| rounding | **every** rounding to an integer is `floor(v + 0.5)` in IEEE double. **Not** `Math.round`, **not** Python's `round()`: they disagree with this and with each other (CLAUDE.md, *a cross-language port must pin its rounding*). Write the expression out |
| determinism | the same inputs give the **same bytes**, run after run, within one implementation. There is no wall-clock time anywhere in a package |

## 2. The frame

Every coordinate in a package is in the package's own frame.

**The origin** is the location the package was built for, stored in micro-degrees. **The route quantises it to 2 decimal places**, the same quantisation as `/wx` (`crop.ts`'s `quantise`), so packages cache per tile.

**The planar metric** is the radar's own (`ProjectAt`, `WxRender.h`, `crop.ts`'s `VIEW`):

```
k   = 111.0                                   km per degree
c0  = max(cos(lat0 * (pi / 180)), 0.01)       pi = 3.14159265358979323846
E   = (lon - lon0) * k * c0                   km, east positive
S   = (lat0 - lat) * k                        km, SOUTH positive (screen axes: y grows down)
```

**The radius R** is stored in millimetres: an integer, the request's whole km × 1,000,000. Every builder uses **that integer**, never the float it came from.

**Quantisation, to signed 16 bits over the square [−R, R]²:**

```
qx = floor(E_mm / R_mm * 32767 + 0.5)
qy = floor(S_mm / R_mm * 32767 + 0.5)        |qx|, |qy| <= 32767 always
```

- **A unit** is R / 32,767: about **2.96 m** at R = 97 km.
- **Geometry is clipped to the square before quantising:**
  - lines with Liang–Barsky;
  - water rings with Sutherland–Hodgman, so a ring stays closed.
- **So no vertex can fall outside the range.** A builder that would produce one has a bug, and must fail, not clamp. A clamp turns an impossible value into a plausible one.

**Order of operations, per feature:**
1. project to (E, S) in km, in double;
2. clip to the square;
3. simplify (§5);
4. quantise;
5. drop consecutive duplicate vertices.

A line left with fewer than 2 distinct vertices, or a ring with fewer than 3, is dropped and counted (§6).

## 3. Layout

```
header          32 bytes
directory       nSections x 16 bytes
section bodies  in directory order, contiguous, no gaps, no overlap
```

**Header:**

| offset | type | field |
|---|---|---|
| 0 | 4 bytes | magic `"BMPK"` |
| 4 | u8 | version = **1** |
| 5 | u8 | `nSections` (1-32) |
| 6 | u16 | flags = 0 (a client refuses any other value in v1) |
| 8 | i32 | `lat0Micro` (the origin, micro-degrees) |
| 12 | i32 | `lon0Micro` |
| 16 | u32 | `radiusMm`: R |
| 20 | u32 | `toleranceMm`: the simplification tolerance ε used (§5) |
| 24 | u32 | `totalBytes`: the whole package, header included |
| 28 | u32 | `crc32`: CRC-32 (IEEE 802.3, as zlib's `crc32`) of all `totalBytes`, computed with this field zeroed |

**A directory entry:**

| offset | type | field |
|---|---|---|
| 0 | u8 | `type` (§4) |
| 1 | u8 | flags = 0 |
| 2 | u16 | reserved = 0 |
| 4 | u32 | `count`: items in the section |
| 8 | u32 | `offset`: from the start of the package |
| 12 | u32 | `length`: bytes |

**Sections, as a rule:**
- **Types 1-5** appear at most once each. Type 6 may repeat, once per terrain level. Type 7 appears exactly once.
- **An unknown type** is skipped by its length, so later versions can add sections.
- **A known type whose body does not parse to exactly its `length`**, with exactly `count` items, makes the **whole package** refused.

## 4. Sections

**Vertices.** Every polyline and ring stores its vertices the same way:
- `varint n`;
- the first vertex, absolute: `svarint qx, qy`;
- then n − 1 deltas: `svarint dqx, dqy`.

Every running total must stay within ±32,767.

| type | name | `count` counts | each item |
|---|---|---|---|
| 1 | `WATER` | rings | `varint n` (≥ 3), then the vertices. **The ring is closed implicitly** (last → first). Each ring is an **independent filled area**. Holes are not represented, so an island inside a lake is filled, as the mockup draws it. Rings appear in source order. |
| 2 | `RIVERS` | polylines | **First, once, a names table:** `varint nNames`, then `nNames` × text (the TIGER `FULLNAME`, e.g. "Deschutes Riv"), ordered by in-frame length, longest first.<br>**Then each polyline:** `varint name` (an index into the table), `varint n` (≥ 2), then the vertices. |
| 3 | `ROADS` | polylines | `u8 class` (1 = TIGER `S1100`, primary; 2 = `S1200`, secondary), `varint n` (≥ 2), then the vertices |
| 4 | `PEAKS` | peaks | **In rank order:** `svarint qx, qy`, `svarint elevM` (metres, from the terrain data), text `name` (GNIS `feature_name` as written: "Mount Bachelor", not "Mt Bachelor") |
| 5 | `PLACES` | places | **In the source file's order:** `svarint qx, qy`, `varint pop` (0 = no figure), text `name` (the Gazetteer `NAME` as written, "Bend city": a client makes its own label from it) |
| 6 | `TERRAIN` | 1 (the level) | `u32 levelRadiusMm`, `u16 size N` (1-1024), `u16` reserved = 0, then N × N `u8` shade values (§5.6), row-major, row 0 north. **`length` must equal 8 + N².** |
| 7 | `PROVENANCE` | lines | UTF-8 text, `\n`-separated `key: value` lines (§7). No wall-clock time. |

**A level's pixel (i, j)** has its centre at E = ((i + 0.5) − N/2) × (2·Rℓ / N) and S = ((j + 0.5) − N/2) × (2·Rℓ / N), with Rℓ = `levelRadiusMm`.
- **Pixel size:** a level's pixel is 2·Rℓ / N. **A client asks for its own pixel size,** so a level is sampled 1:1.
- **The margin:** a client's view centre can sit off the origin (§9), so it asks for a level **wider than its view by that offset**, at the same pixel size.
- **Weatherscope's levels:**
  - **N = 246:** its 240 px view plus 3 px on every side;
  - **Rℓ = the view radius × 246/240**, exactly: **24,743,664 / 49,487,328 / 98,974,656 mm** for the 15, 30 and 60 mi views (201.168, 402.336 and 804.672 m a pixel).
- **Why 3 px is enough:** §9 shows the offset is under 2.76 px at 15 mi. The farthest on-disc pixel centre is 119.5 px from the view's centre, and 119.5 + 2.76 < 123.

## 5. What goes in each layer (the v1 layer definitions)

These are **data rules**, the same for every client. They are the reviewer's 3.5b rulings (A9.1 D). The marked items are Weatherscope's recommendations, for review (A10's questions).

- **5.1 Water areas.**
  - **Source:** TIGER/Line 2024 `AREAWATER` (per county).
  - **Kept:** features with `AWATER ≥ 1,500,000` m², each **outer** part as a ring. A shapefile writes outer rings clockwise and holes counter-clockwise; **holes are dropped** (RD3, §13). An island's ring filled as water draws the same pixels as no ring, and costs bytes.
- **5.2 Rivers.**
  - **Source:** TIGER/Line 2024 `LINEARWATER`, the features whose `FULLNAME` ends in `" Riv"`.
  - **Grouped by `FULLNAME`;** each group's length is the length of its parts inside the disc of radius R.
  - **Kept:** the **5 longest groups**, all their parts (ruled, Q24).
- **5.3 Roads.**
  - **Source:** TIGER/Line 2024 `PRISECROADS` (per state; every state the square touches).
  - **Kept:** `MTFCC` `S1100` or `S1200`, **except** business routes: a `FULLNAME` holding **"Bus" as a whole word**, case-sensitive.
    - **The test:** "Bus" with no letter or digit on either side, i.e. the regular expression `(^|[^A-Za-z0-9])Bus($|[^A-Za-z0-9])`.
    - **Examples:** "US Hwy 97 Bus" and "Bus US Hwy 20" are dropped. "President George Bush Tpke" is **kept**; a substring test would drop it, and real highways with it nationally (ruled, A10.10).
    - **A missing `FULLNAME`** is kept.
- **5.4 Peaks.**
  - **Source:** USGS GNIS Domestic Names, `feature_class = Summit`, inside the disc of radius R.
  - **Elevation** comes from the terrain data (§5.6's DEM, unsmoothed, sampled at the summit's position), **not** a GNIS field.
  - **Ranked** by that elevation, descending; ties by GNIS `feature_id`, ascending.
  - **The de-duplication rule** (ruled, Q25): a summit is dropped when a higher-ranked kept summit lies within **3.0 km**, in §2's metric. This stops one massif's sub-summits from filling the list.
  - **Kept:** the top **16** survivors (ruled, Q25). The client chooses how many it shows (Weatherscope: 4).
- **5.5 Places.**
  - **Source:** the 2025 Census Gazetteer Places file, `LSAD = 25`.
  - **Kept:** places within R (inclusive) of the origin in §2's metric, in the file's order.
  - **Population:** `POPESTIMATE2024` from SUB-EST2024, joined on GEOID to the `SUMLEV 162` row whose `STATE + PLACE` equals it; 0 = no figure.
  - **This is exactly the rule `src/weatherscope/WxTowns.h` was built with**, so a package's places and the stored towns agree.
  - **Not every state names its incorporated places "city"**, so a national rule is **version 2**, not v1 (ruled, Q27).
- **5.6 Terrain** (only when asked for).
  - **Source:** AWS Terrain Tiles, Terrarium encoding, **zoom 9**: elevation = R·256 + G + B/256 − 32768.
  - **Mosaic:** load every z9 tile meeting the level's square, **plus a one-tile margin** on every side, so no sampled value depends on where the mosaic was cut.
  - **Hillshade, computed in double** (the mockup used float32; this pins double):
    - `gx`, `gy`: central differences in tile pixels (x east, y south), one-sided at the mosaic's edge;
    - `mpp = 156543.03 * cos(lat0 * pi/180) / 2^9`;
    - `slope = atan(2.0 * hypot(gx, gy) / mpp)` (the 2× vertical exaggeration);
    - `aspect = atan2(-gx, gy)`;
    - `shade = sin(45°)·cos(slope) + cos(45°)·sin(slope)·cos(315° − aspect)`, clipped to [0, 1].
  - **Smoothing:** a 4 × 4 box mean over tile pixels (about 0.9 km), with edge padding of 2, aligned as `bendmock.py` aligns it: the mean of padded pixels [i, i + 3] × [j, j + 3].
  - **Sampling:** for each level pixel centre (§4), take the tile pixel that contains it (truncation, not interpolation).
  - **Stored:** `floor(shade * 255 + 0.5)`.

**Products choose the look.**
- **Weatherscope's ramp** is grey `10 + floor(36 · v / 255)`, with blue + 2.
- **Its label rule** turns "Mount " into "Mt ".

Both are client rules, not package rules.

## 6. Simplification, size, and what happens when a package is too big

- **Simplification:**
  - **The method:** Douglas–Peucker in the metric frame (metres, double), with tolerance ε, after clipping and before quantising. The split point is the **first** vertex at the maximum distance.
  - **The request names ε.** Weatherscope asks for half a pixel at its finest zoom: 15 mi over 120 px is 201.2 m a pixel, so **ε = 100.6 m** (`toleranceMm` 100,584).
- **The budget** (A9.1 D): **about 20 KB** for everything but terrain; **8 + N² bytes per terrain level** (60,524 at Weatherscope's N = 246).
- **Over the budget (ruled, Q26).** The builder never silently ships less:
  - **A hard cap** of 32 KB for the non-terrain sections.
  - **Past it, in order:**
    1. double ε, once;
    2. drop water rings smallest-area first;
    3. drop secondary roads shortest first.
  - **Every dropped feature is counted** in `PROVENANCE`, so a gap is visible as a gap (RD15, §13):
    - `dropped.<layer>` counts this section's drops;
    - `collapsed.<layer>` counts §2's collapses.
  - **If it still does not fit,** the route answers an error, and the client falls back to no package.

## 7. Provenance (section 7)

**PROVENANCE is by reference (amended within v1, 2026-10-26).**
- **The per-file list lives in a manifest,** not in the package. Per file it holds the layer, URL, vintage, sha256 and bytes.
  - **For a stored fixture,** the manifest is committed beside it (`bend.bmpk` → `bend.manifest.json`).
  - **For the route,** it is the store's own manifest.
- **Why:** listing one set per file made PROVENANCE alone exceed the 32 KB cap in county-dense states. That was the map server's finding, its §1.

**Lines, in this order:**
- `format: BMPK 1`;
- `builder: <name> <commit>`, the commit of the builder's own code when it built the package;
- `store: <id>`: the first 12 hex digits of the sha256 of the manifest the package was built from;
- then, for each layer read, in the fixed order `water, rivers, roads, peaks, places, population, terrain`, four lines:
  - `source.<layer>`: the base URL;
  - `vintage.<layer>`;
  - `files.<layer>`: the count of the layer's files that were read;
  - `sha256.<layer>`: the digest over that layer's sorted `url sha256` lines;
- whenever any terrain tile was read, **whether or not terrain was requested**, since the peaks' elevations come from the tiles (ruled 2026-10-26, A10.13):
  - `credit.terrain: <text>`, one line per credit the sources of the tiles read require;
  - `processing.terrain: <one line>`, describing what was done to the elevations in this package:
    - **with terrain levels:** the hillshade, the 4 × 4 mean, the sampling and the 8-bit quantisation;
    - **with peaks alone:** the summit sampling (RD10);
- then `epsilon.m`, the ε **requested** (RD13);
- then `dropped.<layer>` and `collapsed.<layer>` for every layer read (0 when none; **always present**).

**The details, pinned here as Weatherscope's host tool writes them, for the map server to match or raise.** The ruling did not spell these out.
- **The layers:**
  - `water` is `AREAWATER`, `rivers` is `LINEARWATER`, and `roads` is `PRISECROADS`;
  - `peaks` is GNIS Domestic Names, `places` is the Gazetteer, and `population` is SUB-EST2024;
  - `terrain` is every Terrarium z9 tile read: the level mosaics **and** the tiles read for peak elevations.
- **`source.<layer>`:** the longest common directory prefix of the layer's file URLs, ending in `/`.
- **`sha256.<layer>`:** lowercase hex SHA-256 of the concatenation, in byte order, of one line per file read, each `<url> <sha256, lowercase hex>\n` (one space, and every line ending in `\n`).
- **The URL forms:** each file's URL is its exact fetch URL. Terrain uses RD17's form: `https://s3.amazonaws.com/elevation-tiles-prod/terrarium/9/<x>/<y>.png`.
- **A stored fixture's manifest** is UTF-8 JSON, `{"files": [...], "builder": <commit>, "predicates": [...]}`, with keys sorted, one-space indent and a final `\n`. So its sha256, and `store:`, are reproducible.

**A determinism check** (`--check`) rebuilds with **the `builder` commit recorded in the package**, not the checkout's HEAD. Otherwise every later commit would fail the check while the bytes it guards stay unchanged (ruled, A10.10).

**The sources' terms, to be carried** (ruled, Q34):
- **Public domain:** TIGER/Line, the Gazetteer, SUB-EST2024 and GNIS are US-government works.
- **AWS Terrain Tiles: credit what a mosaic contains** (corrected 2026-10-26; the Q34 ruling had named 3DEP).
  - **What z9 contains** (tilezen/joerd's `docs/data-sources.md`, the zoom-9 row):
    - **SRTM** on US land;
    - **GMTED2010** above 60° N;
    - **ETOPO1** for ocean;
    - **NRCAN** in Canada.
  - **3DEP first appears at zoom 10, so there is no 3DEP line.**
  - **The credit lines** are USGS's ("SRTM data courtesy of the U.S. Geological Survey", and so on, per `docs/attribution.md`). GMTED2010 requires a description of modifications, which is what the processing line gives.
  - **A package carries one `credit.terrain` line per source its tiles contain, plus the processing line.** That includes a package without terrain levels whose peaks read tiles. A licence file beside any stored fixture carries the same.
  - **Bend's mosaic is inland US land below 60° N:** SRTM's line and the processing line, nothing else.
  - **Nothing there forbids redistributing derived US data.**
- **Outside the US,** other sources carry CC-BY or OGL terms. **That is a gate on the route, not on Weatherscope's stored Bend package.**
- **The exact credit wording** is copied from the attribution document by whoever builds a package, and recorded where it was read. Weatherscope does so in 3.5b's step 2.

## 8. What a client must check, before drawing anything

**A client refuses the whole package**, falls back to its no-package screen and logs why, on **any** of:
- the magic, version or header flags;
- `totalBytes` not equal to the bytes received;
- a CRC mismatch;
- `nSections` 0 or over 32;
- a section outside the package, overlapping another, or with gaps between them;
- a duplicate of types 1-5 or 7;
- a missing type 7;
- a varint over 5 bytes or overlong;
- a running vertex total outside ±32,767;
- a vertex count below the minimum for its type;
- a body that does not parse to exactly its `length` and `count`;
- a text length that runs past the section;
- a river name index ≥ `nNames`;
- a road class other than 1 or 2;
- a terrain `length` other than 8 + N², or N = 0;
- a TERRAIN `count` other than 1 (`kTerrainCount`);
- a PROVENANCE `count` other than its line count: 0 for an empty body, else the number of `\n` + 1, since the last line has no trailing `\n` (RD-P4) (`kProvenanceCount`);
- non-zero reserved fields.

**There is no partial draw.** A package that is half right is a lie with a plausible shape (CLAUDE.md, *a dropped row is a gap; a truncated row is a lie*).

## 9. Mapping a package onto a screen (informative: what Weatherscope will do)

**A client's view has its own centre** (lat_h, lon_h). For Weatherscope that is the radar crop's centre: the midpoint of `crop.ts`'s `cropCellBox`, a box rounded outward by under half a cell at each end, from the same 2-dp point. **So it lies strictly less than half a cell (0.005°) from the origin in each axis.**
- **In pixels, at a view radius of R_v km:** under 0.005 × 111 / R_v × 120 px in latitude, and no more in longitude. That is **under 2.76 px at 15 mi**, 1.38 at 30 and 0.69 at 60.
- **So the client shifts.** Per vertex, in double:

```
E_pkg = qx / 32767 * R_mm / 1e6          (km)
lon   = lon0 + E_pkg / (k * c0)          lat = lat0 - S_pkg / k
x     = C + (lon - lon_h) / rLon * C     y   = C - (lat - lat_h) / rLat * C      (the view's own Project)
```

- **No zoom is special:** the same expression serves every view radius.
- **Terrain is sampled at the client's pixel centres,** taking the level pixel that contains the point (truncation). For a client pixel (x, y), in double:

  ```
  lon_p = lon_h + ((x + 0.5) - C) / C * rLon      lat_p = lat_h + (C - (y + 0.5)) / C * rLat
  E     = (lon_p - lon0) * k * c0                 S     = (lat0 - lat_p) * k            (km)
  pix   = 2 * Rl_mm / 1e6 / N                                                         (km)
  i     = floor(E / pix + N / 2)                  j     = floor(S / pix + N / 2)
  ```

  An (i, j) outside [0, N) takes no terrain. With Weatherscope's levels (§4) that cannot happen on a disc pixel. The sum is 119.5 px plus under 2.76 px against the 123 px half-width; Weatherscope's host tests assert it over every disc pixel of every level.

## 10. Delivery (recommended; the map server decides, in its own plan)

- **The request:** `GET` with `lat`, `lon` (any precision, quantised to 2 dp by the route), `r` (whole km), `eps` (metres), `layers` (any of `water,rivers,roads,peaks,places,terrain`), and `levels` (`radiusMm:N` pairs, only with `terrain`).
- **The response:** `application/octet-stream`, one package; `ETag` = the package's `crc32` in hex.
- **The cache key:** the quantised `lat`/`lon` and the other parameters, normalised. **The route stores nothing per device,** as `/wx`.
- **Sizes a device must accept:** the non-terrain sections up to 32 KB, plus each level's 8 + N².

## 11. Checks that make the format trustworthy

- **The golden package:** the host tool's stored Bend package (A10) is committed with its provenance. The Worker's builder, fed the same source files, must produce a package that **decodes equal**:
  - **places and peaks:** identical;
  - **every vertex:** within ±1 unit;
  - **terrain:** each value within ±1, and at most 0.1 % of values different at all.

  **Byte equality is not required across languages:** `cos`, `atan` and `sin` are not bit-identical across libms. Within one implementation, it is.
- **Negative packages** (truncated, bad CRC, overlong varint, a vertex walking past ±32,767, an overlapping section, a bad road class) are committed beside the golden one. Every client test and the Worker's own encoder tests read them. Each must be refused, with its own named reason.
  - **They include A12.15's two:** `neg-terrain-count` (TERRAIN's `count` 2) is refused `kTerrainCount`, and `neg-prov-count` (PROVENANCE's `count` 3 over its 2 lines) is refused `kProvenanceCount`.
  - **The three readers** that check them all: the reference validator (`tools/weatherscope/basemap/validate.py`), the device's reader (`include/BasemapPackage.h`), and the map server's encoder tests. The set and its reasons are listed in `test/fixtures/wx/basemap/README.md`.
- **A sabotage of the refusal itself:** a client built to skip the CRC check must accept the bad-CRC package. That proves the check is what refuses it.

## 12. Versioning

- **The version byte** is 1.
- **A change to any field's meaning,** or to a layer definition in §5, is version 2.
- **A new section type** is not a version change: v1 clients skip it.

## 13. Readings ruled for both builders (RD1-RD31)

**The source:** the map server's plan, `docs/basemap-route.md` on `feat/basemap-route` at `e0c43ce`, its §10. The reviewer ruled them for both builders on 2026-10-26, with RD1 read as (a) ([weatherscope-stage3.md](weatherscope-stage3.md) A10.12).

**Why they are here:** each one changes bytes or vertex lists, so two correct builders could otherwise disagree on it.

| | the gap | the ruled reading |
|---|---|---|
| RD1 | §7's "files read" | **(a): every file read** |
| RD2 | "source order" across per-county files | files by URL ascending (so by GEOID), then record, then part |
| RD3 | water: a polygon's inner rings | **dropped** (§5.1); holes are not represented |
| RD4 | the order of `ROADS` items | source order, as RD2 |
| RD5 | a line that leaves the square and re-enters | each in-square run is its own polyline, in along-line order |
| RD6 | Douglas–Peucker on a closed ring | on the path v₀ … vₙ₋₁, v₀; the distance to a zero-length chord is the distance to its point |
| RD7 | DP's distance and its keep test | the perpendicular distance to the **segment**, clamped at its ends; a vertex is kept when the distance is **> ε** |
| RD8 | "in-frame length" (the names table) against "inside the disc" (the selection) | both are the raw clipped length inside the disc, in metres; ties broken by `FULLNAME` byte order |
| RD9 | "within 3.0 km", "within R", "inside the disc" | ≤ in every case |
| RD10 | a summit's elevation | the Terrarium value of the z9 pixel containing it (truncation), then `floor(v + 0.5)` |
| RD11 | a summit listed in two states' GNIS files | one summit per `feature_id` |
| RD12 | the coordinates read | GNIS `prim_lat_dec`/`prim_long_dec`; Gazetteer `INTPTLAT`/`INTPTLONG` |
| RD13 | the `toleranceMm` header against `epsilon.m` after "double ε" | the header holds the ε **used**, and `epsilon.m` the ε **requested** |
| RD14 | a requested layer with nothing in it | its section is present with `count` 0 |
| RD15 | `dropped.*` key names | `dropped.<layer>` for §6's drops, `collapsed.<layer>` for §2's |
| RD16 | the angles in the shade | 45° and 315° converted as `x * pi / 180`, with pi as `Math.PI` / Python's `math.pi` (the same double) |
| RD17 | the terrain URL form in PROVENANCE | `https://s3.amazonaws.com/elevation-tiles-prod/terrarium/9/<x>/<y>.png` |

**More readings, ruled for both builders on 2026-10-28** ([weatherscope-stage3.md](weatherscope-stage3.md) A10.15).
- **The source:** the map server's frozen truth tables, `docs/basemap-route.md` on `feat/basemap-route` at `644a364` (#486), its Addendum 1: A1.4 and A1.5.
- **RD18-RD21 can change geometry or terrain.** RD-P1-RD-P6, F1 and F2 change PROVENANCE text or the manifest only.

| | the gap | the ruled reading |
|---|---|---|
| RD18 | §6's "smallest-area first" | the absolute shoelace area of the ring's **quantised** vertices (exact integers), smallest first; on a tie, the later ring in source order goes first |
| RD19 | §6's "shortest first" | the summed segment length of the road's quantised vertices, in double, along the line, shortest first; on a tie, the later road first |
| RD20 | what the 32 KB cap measures | `totalBytes` minus the **bodies** of the TERRAIN sections; their directory entries count |
| RD21 | a point's z9 pixel (§5.4 summits, §5.6 sampling) | `X = (lon + 180) / 360 · 2^17` and `Y = (1 − ln(tan(π/4 + φ/2)) / π) / 2 · 2^17`, each floored; tile `⌊X / 256⌋`, pixel `X mod 256` (and the same for Y) |
| RD-P1 | the order of the `dropped`/`collapsed` lines | per layer read, in the fixed order: `dropped.<layer>`, then `collapsed.<layer>` |
| RD-P2 | `vintage.<layer>` when a layer's files carry more than one | the distinct vintages, ascending, space-separated (normally one) |
| RD-P3 | the vintage of a file whose name carries no year (GNIS, Terrain Tiles) | the year of its `Last-Modified` as fetched, recorded in the manifest |
| RD-P4 | `epsilon.m`'s form, and line ends | the requested mm ÷ 1,000 with exactly three decimals (`100.584`); **lines joined by `\n`, with no trailing `\n`** |
| RD-P5 | the order and form of the credit lines | the order and texts below |
| RD-P6 | a county's or state's files "read" | those whose TIGER polygon's bounding box meets the square's latitude/longitude box, closed |
| F1 | the manifest's `builder` | the commit of the code that wrote the manifest |
| F2 | inputs outside the seven layers (TIGER state and county files, Natural Earth) | the same `files` list, under their own keys (`states`, `counties`, `naturalearth`). No package's PROVENANCE lists them |

**More readings, ruled for both builders on 2026-10-28** ([weatherscope-stage3.md](weatherscope-stage3.md) A12.7 and A12.9).
- **The source:** the host tool's first real build (#495), which named nine details §2, §5, §6 and §7 did not pin, then a tenth from the ruling.
- **RD22-RD27, RD29 and RD31 can change geometry; RD28 is terrain; RD30 is the manifest only.**

| | the gap | the ruled reading |
|---|---|---|
| RD22 | a clip endpoint the clip did not move | Liang–Barsky returns **the source vertex itself**, never `a + u·d` recomputed |
| RD23 | ring clipping's order and crossings | Sutherland–Hodgman against the edges **west (x ≥ −R), east (x ≤ R), north (y ≥ −R), south (y ≤ R)**, closed. The output starts where the textbook pass starts. **Each crossing is computed from the previous vertex toward the current one:** `t = (lim − prev[a]) / (cur[a] − prev[a])`, the other axis `prev[o] + t · (cur[o] − prev[o])`, and the clipped axis set to `lim` exactly |
| RD24 | a ring's closing duplicate | after quantising and dropping consecutive duplicates, a last vertex equal to the first is dropped (closure is implicit) |
| RD25 | Douglas–Peucker's units | distances on the km coordinates × 1000, against ε = `toleranceMm` / 1000 m. Quantisation uses the km values (`E_km × 1e6`), so simplification selects vertices and never moves them |
| RD26 | which water parts are outer | a part, without its closing repeat, whose shoelace sum on (lon, lat) is **negative** (clockwise) |
| RD27 | the 4 × 4 mean (§5.6) | the 16 values summed row by row, left to right, from 0.0, then divided by 16 |
| RD28 | the hillshade's library maths | as each builder's library computes it (numpy for the host tool). **A golden comparison that differs is reported cell by cell, with both builders' unquantised values. A difference confined to cells within 1e-9 of a level boundary is this reading, not a defect** |
| RD29 | a river group's length | the sum over all its parts, across every county file read. A group with 0 m inside the disc is not a candidate |
| RD30 | the manifest's `files` order | **by URL alone** (MI4) |
| RD31 | how a decision computes a distance | `sqrt(x·x + y·y)`, the x term first, in double; **never `hypot`** (which stays only inside the hillshade, under RD28). Below |

**RD31's four sites,** written out so both builders compute the same doubles:

| site | the expression |
|---|---|
| Douglas–Peucker (RD6, RD7), metres | `dx = b.x − a.x`, `dy = b.y − a.y`. If both are 0: `ex = p.x − a.x`, `ey = p.y − a.y`. Else `t = ((p.x − a.x)·dx + (p.y − a.y)·dy) / (dx·dx + dy·dy)`, clamped to [0, 1]; `ex = p.x − (a.x + t·dx)`, `ey = p.y − (a.y + t·dy)`. `d = sqrt(ex·ex + ey·ey)`; kept when `d > ε` |
| inside the disc (places, summits), km | `sqrt(E·E + S·S) <= R` |
| the 3 km de-duplication, km | `ex = p.E − k.E`, `ey = p.S − k.S`; dropped when `sqrt(ex·ex + ey·ey) <= 3.0` for a higher-ranked kept `k` |
| a river's length inside the disc (RD8), km | per segment: `aa = dx·dx + dy·dy` (skipped when 0); `bb = 2·(a.x·dx + a.y·dy)`; `cc = a.x·a.x + a.y·a.y − R·R`; `disc = bb·bb − 4·aa·cc` (skipped when ≤ 0); `s = sqrt(disc)`; `t0 = max(0, (−bb − s)/(2·aa))`, `t1 = min(1, (−bb + s)/(2·aa))`; when `t1 > t0`, add `(t1 − t0)·sqrt(aa)·1000` m |

**The credit lines (RD-P5).** One line per source, in this order, each carried when any tile read contains that source. The texts are verbatim from joerd's `attribution.md` at `0b86765`; "ETOPO1: " is the map server's prefix, and the NRCAN line adds the licence link.

```
credit.terrain: SRTM data courtesy of the U.S. Geological Survey
credit.terrain: GMTED2010 data courtesy of the U.S. Geological Survey
credit.terrain: ETOPO1: DOC/NOAA/NESDIS/NCEI > National Centers for Environmental Information, NESDIS, NOAA, U.S. Department of Commerce
credit.terrain: Contains information licensed under the Open Government Licence – Canada. https://open.canada.ca/en/open-government-licence-canada
```

**The processing line,** carried whenever any tile was read:

| tiles read for | `processing.terrain:` |
|---|---|
| levels only | `Terrarium z9 elevations; hillshade from 315 deg at 45 deg with 2x vertical exaggeration; 4x4 mean of z9 pixels; each level pixel takes the z9 pixel containing its centre; shade quantised to 8 bits` |
| peaks only | `Terrarium z9 elevations; summit elevations taken from the z9 pixel containing the summit, rounded to whole metres` |
| both | the levels text, then `; summit elevations taken from the z9 pixel containing the summit, rounded to whole metres` |

**`bend.bmpk`** carries the SRTM line and the "both" text. Its licence file (A10.14) also carries SRTM's §3.15 notice, as that spec words it, with the year **2000** and a note that joerd records a 2014 update (ruled for both builders: A10.18 here, AQ1 in the route's plan): "© COPYRIGHT 2000 BY THE UNITED STATES GOVERNMENT. NO COPYRIGHT CLAIMED UNDER TITLE 17 U.S.C."
