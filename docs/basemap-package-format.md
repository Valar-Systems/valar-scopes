# The basemap package, format v1 (`BMPK`)

**Status: a draft for review (2026-10-26). Nothing described here is built.**
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

**A level's pixel (i, j)** has its centre at E = ((i + 0.5) − N/2) × (2·Rℓ / N) and S = ((j + 0.5) − N/2) × (2·Rℓ / N), with Rℓ = `levelRadiusMm`. **It is meant to be sampled 1:1** by a client whose view radius is Rℓ and whose screen is N px across. Weatherscope asks for Rℓ = 15, 30 and 60 mi exactly (24,140,160 / 48,280,320 / 96,560,640 mm) at N = 240.

## 5. What goes in each layer (the v1 layer definitions)

These are **data rules**, the same for every client. They are the reviewer's 3.5b rulings (A9.1 D). The marked items are Weatherscope's recommendations, for review (A10's questions).

- **5.1 Water areas.**
  - **Source:** TIGER/Line 2024 `AREAWATER` (per county).
  - **Kept:** features with `AWATER ≥ 1,500,000` m², each part as a ring.
- **5.2 Rivers.**
  - **Source:** TIGER/Line 2024 `LINEARWATER`, the features whose `FULLNAME` ends in `" Riv"`.
  - **Grouped by `FULLNAME`;** each group's length is the length of its parts inside the disc of radius R.
  - **Kept:** the **K longest groups**, all their parts. ★ **K = 5 recommended** (the mockup drew 5; question Q24).
- **5.3 Roads.**
  - **Source:** TIGER/Line 2024 `PRISECROADS` (per state; every state the square touches).
  - **Kept:** `MTFCC` `S1100` or `S1200`, **except** where `FULLNAME` contains `"Bus"` (business routes).
- **5.4 Peaks.**
  - **Source:** USGS GNIS Domestic Names, `feature_class = Summit`, inside the disc of radius R.
  - **Elevation** comes from the terrain data (§5.6's DEM, unsmoothed, sampled at the summit's position), **not** a GNIS field.
  - **Ranked** by that elevation, descending; ties by GNIS `feature_id`, ascending.
  - ★ **The de-duplication rule** (Q25): a summit is dropped when a higher-ranked kept summit lies within **3.0 km**. This stops one massif's sub-summits from filling the list.
  - **Kept:** the top **16** survivors (★ Q25). The client chooses how many it shows (Weatherscope: 4).
- **5.5 Places.**
  - **Source:** the 2025 Census Gazetteer Places file, `LSAD = 25`.
  - **Kept:** places within R (inclusive) of the origin in §2's metric, in the file's order.
  - **Population:** `POPESTIMATE2024` from SUB-EST2024, joined on GEOID to the `SUMLEV 162` row whose `STATE + PLACE` equals it; 0 = no figure.
  - **This is exactly the rule `src/weatherscope/WxTowns.h` was built with**, so a package's places and the stored towns agree.
  - ★ **Not every state names its incorporated places "city"**, so the national rule is a later question, not v1's (Q27).
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
- **The budget** (A9.1 D): **about 20 KB** for everything but terrain; **N² bytes per terrain level** (57,600 at N = 240).
- ★ **Over the budget (Q26).** The builder never silently ships less. **Recommended:**
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
- `builder: <name> <commit>`;
- then for each source used: `source.<layer>: <url>`, `vintage.<layer>: <year>`, `sha256.<layer>: <64 hex of the file as fetched>`, one set per file;
- then `epsilon.m`;
- then the `dropped.*` counts (0 when nothing was dropped; **always present**).

**The sources' terms, to be carried:**
- **Public domain:** TIGER/Line, the Gazetteer, SUB-EST2024 and GNIS are US-government works.
- **Not yet read:** **AWS Terrain Tiles** is a composite, and its registry entry points to an attribution document. **Its exact obligations have not been read for this draft.** They must be read and met before terrain reaches a customer. That is a gate on the route, not a footnote.

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

**A client's view has its own centre** (lat_h, lon_h). For Weatherscope that is the radar crop's centre, an MRMS cell centre, which is **not** the 2-dp origin: it can be up to about 0.005° away, which is 2.7 px at the 15 mi zoom. So the client shifts. Per vertex, in double:

```
E_pkg = qx / 32767 * R_mm / 1e6          (km)
lon   = lon0 + E_pkg / (k * c0)          lat = lat0 - S_pkg / k
x     = C + (lon - lon_h) / rLon * C     y   = C - (lat - lat_h) / rLat * C      (the view's own Project)
```

- **No zoom is special:** the same expression serves every view radius.
- **Terrain is sampled at the client's pixel centres,** taking the level pixel that contains the point (truncation): `i = floor(E_pkg_of_pixel / (2·Rℓ / N) + N/2)`. A pixel outside [0, N) in either axis takes no terrain.

## 10. Delivery (recommended, for blipscope-5c to decide)

- **The request:** `GET` with `lat`, `lon` (any precision, quantised to 2 dp by the route), `r` (whole km), `eps` (metres), `layers` (any of `water,rivers,roads,peaks,places,terrain`), and `levels` (`radiusMm:N` pairs, only with `terrain`).
- **The response:** `application/octet-stream`, one package; `ETag` = the package's `crc32` in hex.
- **The cache key:** the quantised `lat`/`lon` and the other parameters, normalised. **The route stores nothing per device,** as `/wx`.
- **Sizes a device must accept:** the non-terrain sections up to 32 KB, plus each level's N².

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
