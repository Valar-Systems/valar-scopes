# The basemap package, format v1 (`BMPK`)

**Status: v1, frozen (2026-10-26).**
- **Accepted** by the reviewer, with three changes made before the freeze ([weatherscope-stage3.md](weatherscope-stage3.md) A10.10):
  1. "Bus" is matched as a whole word;
  2. Weatherscope's terrain levels are 3 px wider on every side;
  3. `--check` rebuilds with the recorded builder commit.
- **The questions marked ★ in the draft are now ruled.** Each is marked "ruled" with its question number.
- **From here, a change** to any field's meaning or any layer definition is **version 2** (§12).
- **Nothing described here is built yet.**
- **Asked for by:** the reviewer's ruling on Weatherscope step 3.5b ([weatherscope-stage3.md](weatherscope-stage3.md) A9.1 D, A9.9 item 2, A9.21's second paste).
- **Who builds what:**
  - blipscope-5c builds the ingest and the Worker route from this document;
  - Weatherscope builds the device side, and a host tool that writes the same format for a stored Bend package.
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
  - **Kept:** features with `AWATER ≥ 1,500,000` m², each part as a ring.
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
  - **Every dropped feature, and every one dropped in §2's collapse, is counted** in `PROVENANCE` (`dropped.water: 14`), so a gap is visible as a gap.
  - **If it still does not fit,** the route answers an error, and the client falls back to no package.

## 7. Provenance (section 7)

**Lines, in this order:**
- `format: BMPK 1`;
- `builder: <name> <commit>`, the commit of the builder's own code when it built the package;
- then for each source used: `source.<layer>: <url>`, `vintage.<layer>: <year>`, `sha256.<layer>: <64 hex of the file as fetched>`, one set per file;
- with terrain:
  - `credit.terrain: <text>`, one line per credit the terrain sources require;
  - `processing.terrain: <one line>`, describing what was done to the elevations: the hillshade, the 4 × 4 mean, the sampling and the 8-bit quantisation;
- then `epsilon.m`;
- then the `dropped.*` counts (0 when nothing was dropped; **always present**).

**A determinism check** (`--check`) rebuilds with **the `builder` commit recorded in the package**, not the checkout's HEAD. Otherwise every later commit would fail the check while the bytes it guards stay unchanged (ruled, A10.10).

**The sources' terms, to be carried** (ruled, Q34):
- **Public domain:** TIGER/Line, the Gazetteer, SUB-EST2024 and GNIS are US-government works.
- **AWS Terrain Tiles**, as the reviewer read tilezen/joerd's `docs/attribution.md` on 2026-10-26:
  - **The US sources** (3DEP, SRTM, GMTED2010) are public domain.
  - **USGS asks for a credit line for each** ("… data courtesy of the U.S. Geological Survey").
  - **GMTED2010 requires** a description of modifications.
  - **So a package with terrain carries the three credits and the processing line above,** and so does a licence file beside any stored fixture.
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

## 10. Delivery (recommended, for blipscope-5c to decide)

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
- **A sabotage of the refusal itself:** a client built to skip the CRC check must accept the bad-CRC package. That proves the check is what refuses it.

## 12. Versioning

- **The version byte** is 1.
- **A change to any field's meaning,** or to a layer definition in §5, is version 2.
- **A new section type** is not a version change: v1 clients skip it.
