# Weatherscope Stage 2: the plan (2026-10-20)

**Status: plan only.** Nothing for Stage 2 has been built, downloaded or measured. The predictions below are frozen before any of that, as in Stage 1. A result outside them means stop and decide; it is not re-graded.

**What Stage 2 is:** from Stage 1's one stored frame (`docs/weatherscope-stage1.md`) to the device Weatherscope ships as. That means the palette, the radar product, the data path, and the screens in `docs/weatherscope-screens.md` (#425): the priority order, the 15 mi ring, the past-hour loop and the warning screen.

**The rulings it rests on** (Daniel, 2026-10-20; recorded in `docs/weatherscope-stage1.md`, "Not decided here"):
1. **Palette:**
   - rain uses the standard NWS reflectivity scale;
   - snow uses its own blue-to-white intensity scale;
   - no coverage is solid grey;
   - missing is dim or hatched grey, never violet (violet reads as a hail core);
   - the lowest band (15-20 dBZ) is dimmed slightly.

   It is rendered on the board for Daniel to look at before it is final.
2. **Radar product:** SeamlessHSR, pending a side-by-side check against the composite on a few stored frames, including a dry western day with storms aloft. If the difference is negligible, the report says so, and Daniel may switch back.
3. **Data path:**
   - one server pipeline shared with the Blipscope weather layer, with the radar product as a parameter;
   - it runs on the device side, never in the website's Worker;
   - NWS alerts also come through the proxy (one request per area, a proper User-Agent), never straight from devices.

## Held: the shared parts

Ruling 3 touches blipscope-5c's spike (#422). **These parts are not planned here** until the coordination note has gone to blipscope-5c through Daniel and it has answered:
- **The server pipeline:** ingest, the product parameter, where it runs, and the request shape (location to crop) and its caching.
- **NWS alerts through the proxy.**
- **Rain and snow on the wire.** WXC1 v1 has no precipitation-type field, and it refuses unknown flag bits and a nonzero reserved byte (#429 review). So the snow scale on real frames needs a format change in `include/WxCrop.h`, which both scopes share.

**What the device side assumes meanwhile:** WXC1 v1 crops, delivered as bytes in RAM, decoded by `include/WxCrop.h` as merged in #429. Until the pipeline exists, stored fixtures stand in for the feed.

## The order

| step | what | device code | shared |
|---|---|---|---|
| **2.0** | the product check: SeamlessHSR against the composite, on stored frames | no; host scripts | no |
| **2.1** | the palette on the board, for Daniel to look at | yes; the bench harness | no |
| 2.2 | the past-hour loop, from stored crops | yes | no |
| 2.3 | the screen logic on the host: priority, the 15 mi ring, warning containment, order and expiry | host tests | no; stored alert JSON stands in for the feed |
| 2.4 on | the data path and the alerts on the device | yes | **held** |

2.0 and 2.1 are frozen in full below. 2.2 and 2.3 are scoped here, and their numbers are frozen in an addendum before their code. 2.4 is planned with blipscope-5c.

## 2.0 The product check

**The question:** where do SeamlessHSR and MergedReflectivityQCComposite differ in what the device would **draw** and **decide**?

**Why they should differ:**
- The composite is each column's **maximum** reflectivity.
- SeamlessHSR is the reflectivity at the **lowest unblocked height** (the hybrid scan).
- Echo that never reaches the ground, such as virga or a melting-layer bright band, raises the composite and not SeamlessHSR.
- Both are drawn from the same merged 3D mosaic, so **the composite should be at least SeamlessHSR in every cell**, apart from the files' time offset.

**Known before this check** (wx spike A1, #422): on 2024-05-21 22:02Z, SeamlessHSR used exactly the composite's two sentinels, −99 for no echo and −999 for no coverage, with no third value. Neither product sends code 15. It shares the composite's grid and packing.

### The frames

Each frame comes from `noaa-mrms-pds`, using each product's file nearest the chosen time. The files are kept outside git, and only ICAO codes are printed, never a coordinate.

| frame | site and time | how it is chosen |
|---|---|---|
| **F1, storm** | KDFW, 2024-05-21, near 22:00Z | Stage 1's frame (composite file 22:00:42Z) |
| **F2, dry western day, storms aloft** | first hit of the rule | a vetted western airport, in this order: KPHX, KLAS, KABQ, KELP, KDEN, KSLC, KGJT, KRNO, KCYS, KBIL. Dates June-September, 2024 then 2025, ascending. The hit is the first hour whose METAR remarks say **VIRGA** and whose present weather reports **no precipitation**, **and** where the composite at that time has echo (≥ 15 dBZ) **within 15 mi** of the airport |
| **F3, stratiform rain** | first hit of the rule | vetted eastern airports in this order: KATL, KCLT, KRDU, KDCA, KPHL, KJFK, KBOS. November 2024 to March 2025, ascending. The hit is the first run of **3 consecutive hourly METARs with `RA` and no `SH` or `TS`** at **5 °C or more**; the frame is at the middle hour |
| **F4, snow** | first hit of the rule | KMSP, KFAR, KBIS, KDSM, KDEN, in that order. December 2024 to February 2025, ascending. The hit is the first run of **3 consecutive hourly METARs with `SN`** and no `RA`; the frame is at the middle hour |

- **The rules see only METARs and the composite, never SeamlessHSR.**
- METARs come from the Iowa Environmental Mesonet ASOS archive, which is public and keyless.
- A frame whose two files are more than 2 min apart, or whose anchor fails, is replaced by the next hit of its rule. The replacement is recorded.

### How it is measured

The same pipeline as Stage 1, so the comparison is of what the device would show:
1. The ruled mapping turns values into codes: −999 → 14, below 15 → 0, otherwise the 5 dBZ bands up to 13 (`tools/wx/mrms.ts`).
2. The crop is 60 mi around the airport (`crop.ts`).
3. `render_ref.py` turns it into the 240 × 240 screen. Each measure below is taken over the **45,244 disc pixels**.

**The measures:**

| measure | what it reports |
|---|---|
| **M1** | the share of pixels in the same **class** (dry, echo or no coverage), and in the same **code** |
| **M2** | each product's echo share |
| **M3** | among pixels with echo in either product: composite code minus SeamlessHSR code, counted as ≤ −1, 0, +1 and ≥ +2 |
| **M4** | each product's no-coverage share |
| **M5** | **the device's decisions** under each product: (a) echo within **15 mi** of the centre (ruling 1b); (b) echo **at** the centre pixel (1c). Each at two thresholds, **code ≥ 1 (15 dBZ)** and **code ≥ 4 (30 dBZ)**. What counts as precipitation for the ring has **not been ruled**, so both are reported |

**The anchor and the controls, run before any result is read:**
- **The sentinel census.** Every value in every file is −999, −99 or a reflectivity of at least −30 dBZ. Any other negative value: stop, because the mapping would call it dry.
- **Self-comparison.** The composite against itself must give 100.0000 % agreement on M1 and identical M5 decisions on every frame.
- **Sabotage.** The composite against itself with 5 dBZ subtracted must turn every pixel at code 2 to 13 into "+1" on M3, and must turn at least one 30 dBZ decision where one exists. A script that can't show a difference proves nothing when it shows none.

### Predicted

| | predicted | basis |
|---|---|---|
| **P0, every frame** | composite code ≥ SeamlessHSR code in **≥ 90 %** of pixels with echo in either | column maximum ≥ any one level; the rest comes from the files' time offset (up to 2 min), as storms move |
| **P1, F1 storm** | SeamlessHSR's echo share is **75-100 %** of the composite's; class agreement **≥ 95 %**; **M5 agrees** at both thresholds | deep convection reaches the ground; the composite's extra area is anvil and overhang aloft |
| **P2, F2 virga** | SeamlessHSR's echo share **≤ 60 %** of the composite's; **M5(a) differs at 15 dBZ**: the composite has echo within 15 mi (true by construction), SeamlessHSR **has none** | the echo aloft evaporates before the lowest unblocked height. Lowest confidence of the four: in the mountain West the hybrid scan can itself sit kilometres up |
| **P3, F3 stratiform** | echo share **85-100 %** of the composite's; composite **≥ +1 band** in **≥ 30 %** of echo pixels; **M5 agrees** | the rain reaches the ground; the bright band raises the column maximum, not the lowest height |
| **P4, F4 snow** | class agreement **≥ 95 %**; codes **equal** in **≥ 70 %** of echo pixels; **M5 agrees** | snow is shallow, and its reflectivity is often highest near the ground, where both products sample |

**"Negligible", defined now so it can't be improvised later:** on **every** frame, the M5 decisions agree at both thresholds **and** at least **98 %** of disc pixels are in the same class. Code-level differences inside echo change shades, not what the screen says; they are reported but are not part of the test. **If P2 holds, the difference is not negligible**, since the composite would announce rain that never reaches the ground. Whether that matters is Daniel's call.

**Outside the predictions:**
- An anchor or control failing: stop.
- P0 below 90 % on any frame: stop and decide. That would mean the products don't share the mosaic the way assumed.
- **Any result not named here: stop and decide.**

## 2.1 The palette on the board

**What the board shows** in the bench harness (env `weatherscope-s3-128`; Stage 1's results are not touched):
- **A palette card:**
  - the rain scale, 13 swatches;
  - the snow scale, 13 swatches;
  - no coverage;
  - missing, both dim and hatched;
  - dry (black);
  - code 1 at two dim levels, **85 %** and **70 %** of its colour.
- **The KDFW frame** in the new palette, rain only: the wire has no snow field yet.

A serial byte switches between the two. **Daniel looks at the board, and his ruling makes it final.** That look is the result of this step; nothing here grades the colours as good or bad.

**The proposal** (only what is shown, not final):

| element | proposed |
|---|---|
| rain, codes 2-13 (20-75 dBZ) | the NWS reflectivity scale, pinned in the commit to a published table with its sha256. The candidate is MetPy's `NWSReflectivity` (BSD-3-Clause). Stage 1's provisional values for codes 1-13 are **checked** against it, not assumed equal |
| rain, code 1 (15-20 dBZ) | that table's 15 dBZ blue at 85 % or 70 % |
| snow, codes 1-13 | a ramp from blue for light snow to white for heavy (the direction is read from the ruling, and the look confirms it). It runs linearly in RGB from a dark blue at code 1 to white at code 8 (50 dBZ), and stays white above. Code 1 is dimmed like rain's |
| no coverage (14) | solid neutral grey |
| missing (15) | (a) a darker neutral grey, or (b) neutral grey hatching on black |
| dry (0) | black, as in Stage 1 |

**Two things the look should settle:**
- **Light snow and light rain are both blue.** The rain scale's lowest bands are blue too. The card puts them side by side.
- **Missing never comes from MRMS.** Neither product sends code 15 (spike A1). On a real screen, missing can only come from the device side, such as a frame it couldn't fetch. So no stored frame shows it, and only the card does.

**Predicted:**

| check | predicted |
|---|---|
| **C1. The codes are unchanged** | the KDFW per-pixel code dump stays sha256 `344383b4…`: the palette changes colours, not codes |
| **C2. Board colours equal host colours** | the board's 115,200-byte sprite (RGB565) is **identical** to a host render of the same palette and frame |
| **C3. The palette's own properties** (host test) | (a) no coverage and missing are **achromatic** (R = G = B): "never violet" as a property, not a promise; (b) dry, missing and no coverage are three different colours; (c) rain codes 2-13 equal the pinned table exactly; (d) code 1 equals the table's 15 dBZ entry times the dim factor; (e) snow brightness rises with each code up to white |
| **C3's sabotage** | planting Stage 1's violet `(110, 0, 110)` as missing turns **exactly (a)** red; a rain entry off by one unit turns **exactly (c)** red |
| Build | static RAM within **±256 B** of 26,824; flash up by **less than 4 KB** |
| Timings on the KDFW frame | B3, B4 and B8 in their bands. B5-B7 still **miss** their frozen bands, each within ±10 % of its Stage 1 measurement. The hatch only costs on missing pixels, and the KDFW frame has none |

**Outside the predictions:** C1 or C2 failing, a sabotage that doesn't turn exactly its check red, or any result not named: **stop and decide.**

## 2.2 The past-hour loop, from stored crops (numbers frozen before its code)

The rulings: a tap opens it (2a), there is no +30 (2b), it runs about 15 frames (every other one) (2c) and plays once (2d).

**What gets predicted before the code:**
- PSRAM for 15 stored crops, at most 15 × 32 KB at the server's cap;
- the time per loop frame, from Stage 1's measured costs: decode, render and push came to about 50.7 ms per frame, against the ≥ 8 fps budget bar;
- the frame pacing;
- for every frame, that the code-dump sha256 equals a host reference.

The input is 15 stored crops, every other frame over one hour on the KDFW day, in whichever product Daniel rules after 2.0.

## 2.3 The screen logic on the host (numbers frozen before its code)

Pure functions with host tests, from `docs/weatherscope-screens.md`:
- the priority order (1a);
- the 15 mi ring and "now" (1b, 1c);
- the warning polygon containing the location (1d);
- which warnings count, their order and when they end (3a, 3c, 3d);
- the bottom lines (3b, 3e);
- imperial units (6a);
- town labels (5).

Stored NWS alert JSON from the 2026-10-20 sample stands in for the feed. **What counts as precipitation for the ring** (code ≥ 1 or higher) is not ruled; 2.0 reports both thresholds, and it needs a ruling before 2.3's code.

## Addendum 1: the #437 review (ruled 2026-10-20; frozen before any download)

**Recorded as received:** approved with three changes, then a go on 2.0 and 2.1. Nothing above is edited. Where this addendum changes 2.0, it governs.

### Change 3: M5 at three thresholds

M5 is reported at code ≥ 1 (15 dBZ), **code ≥ 2 (20 dBZ)** and code ≥ 4 (30 dBZ). Every "M5 agrees" above, the self-comparison control, and the definition of "negligible" now mean **agreeing at all three**.

### Fixed now, so they can't be decided after the data is seen

- **"The centre":** the source grid cell **nearest the airport's coordinates**. The even-sized crops have no single middle pixel. Its code under the ruled mapping is M5(b), and its raw value is what change 1 reports.
- **How a rule searches:**
  - The airports are taken in the listed order, and for each one, hours ascending through the date window. The first hour that meets the rule is the hit.
  - "Consecutive hourly" means routine METARs 50-70 min apart.
  - Each product's frame for a METAR is its file nearest the observation time.
- **Weather tokens:**
  - Precipitation is DZ, RA, SN, SG, IC, PL, GR, GS or UP.
  - **RA** is a token containing RA. For F3 and F5, a token with SH or TS doesn't count.
  - **SN** is a token containing SN, but BLSN or DRSN alone doesn't count: blowing snow is not falling snow.
- **The order frames are measured in:** F1, F2, F3, F5, F4, then change 1's sample. A stop fired by F4 then can't cut short the others.

### Change 2: F5, western ground rain

| frame | site and time | how it is chosen |
|---|---|---|
| **F5, western ground rain** | first hit of the rule | **F2's airports in F2's order** (KPHX, KLAS, KABQ, KELP, KDEN, KSLC, KGJT, KRNO, KCYS, KBIL), with **F3's rule and window**: 3 consecutive hourly METARs with RA and no SH or TS, at 5 °C or more, November 2024 to March 2025, ascending; **and no VIRGA remark** in any of the three. The frame is at the middle hour |

**P5, predicted:**
- **M5 agrees at every threshold.**
- **SeamlessHSR has echo at the centre** (code ≥ 1).
- P0 applies, as on every frame.

**Basis:** the rain reaches the ground, and the frame tests whether a high hybrid scan in the mountain West misses real rain.

**Outside the prediction:** SeamlessHSR dry at the centre is exactly the failure this frame exists to catch: **stop and report**.

### Change 1: F4, snow that reads as dry

**The sample:** the **first 24 snowing routine METAR hours** at the F4 airport, with snow per the token rule and no RA, starting at the F4 run's first hour. They need not be consecutive.

**Reported for each product:**
- the share of those hours whose centre is **below 15 dBZ** (code 0, which reads as dry), with its count out of 24;
- the centre's raw value in bins: no echo (−99), below 5, 5-10, 10-15, 15 or more dBZ.

**P4b, predicted:**
- composite **20-50 %**;
- SeamlessHSR **25-60 %**, and **at least the composite's share** (a column's maximum is at least its lowest height).

**Basis:** light snow (−SN, the commonest snow METAR) is often 5-15 dBZ.

**"Large", fixed now: 20 % or more on either product.** That is a **stop**: the snow threshold needs a ruling before the wire format. **So my prediction is a stop.**

### The answers, recorded

- **2.2 may predict from Stage 1's measured costs**, which were stable to 0.2 %. The frozen B5-B7 bands stay misses; the measurements are fine to predict from.
- **The ring's threshold** is ruled after 2.0 reports 15, 20 and 30 dBZ, before 2.3's code.
- **The coordination note** has gone to blipscope-5c through Daniel. The shared parts stay held until it answers.

**Downloads:**
- about two files per frame per product;
- the composite files F2's search needs;
- 24 × 2 for change 1.

All of them stay in the scratchpad, never in git.

## Bench rules (unchanged from Stage 1)

- **The board** is `907069326E64`, always found by its serial.
- **Captures:** one open of the port, never reattached; the measuring tool is never retried until it passes.
- **Files:** downloaded MRMS files stay out of git; only ICAO codes are printed, never a coordinate.
- **Merging:** blipscope-5c reviews and merges. Daniel's reviewer chat is the only source of rulings.
