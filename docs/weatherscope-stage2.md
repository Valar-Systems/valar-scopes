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

## Addendum 2: rulings on 2.0 run 1 (2026-10-20; frozen before the motion test, before any regrade, before 2.1's code)

**Recorded as received:**
1. **Floor:** the sentinel census floor is re-registered at **−40 dBZ**. The values below −30 are a smooth tail of real reflectivity, mapped dry either way. STOP 1 stays on record, with a note that **the lower bound appears to have moved in files from 2024-12 on**. Then grade F4, F5 and change 1.
2. **P0:** run **the motion test before anything else is graded**. STOP 2 and **P1's miss on F1** stay on record, and nothing is re-graded.
3. **Palette:**
   - MetPy's `NWSReflectivity` values for **15-70 dBZ**;
   - **75 dBZ and up white**, a ruled exception, because black would read as dry;
   - the **snow ramp tops out at pale blue**, not white, so heavy snow and 75+ rain never share a colour;
   - the table pinned with its sha256, as planned.
4. **The product ruling** (SeamlessHSR or the composite) is held until F4, F5 and the motion test are in. **F3's centre** (METAR `-RA`, SeamlessHSR 12.4 dBZ) is noted as evidence for the ring-threshold ruling.

### The motion test

**What:** two consecutive SeamlessHSR files, 2 min apart, on F1 and F3. Each pair is run 1's SeamlessHSR file and the next one, so **F1 at 22:00:00Z and 22:02:00Z, and F3 at 09:52:00Z and 09:54:00Z**. The two files must be exactly 120 s apart. They go through the same pipeline as run 1: the ruled mapping, the 60 mi crop and `render_ref.py`.

**What is measured:** among disc pixels with echo in either file, the share whose level **went up** (the later file higher) and the share that **went down**. **"Went up" is what's graded, as ruled.** "Went down" is reported beside it, because it is the closer copy of run 1's miss: there, the SeamlessHSR file was the *earlier* one.

**Run 1's share to compare with,** "SeamlessHSR higher than the composite": **F1 16.31 %** (1,841 of 11,285) and **F3 12.18 %** (4,466 of 36,653).

**The readings, fixed now, per frame:**

| went-up share | reading |
|---|---|
| within **±4 points** of that frame's run-1 share | **(a)** motion explains P0 on that frame |
| **less than half** of that frame's run-1 share | **(b)** the products differ in grid or QC: **stop and report** |
| anything else: more than 4 points above, or between half and 4 points below | **outside: stop and decide.** The 2 min gap is about 3 × run 1's 42 s offset, so a share well above can't simply be read as (a) |

**(a) on both F1 and F3 means motion explains P0**, and F4, F5 and change 1 are then graded. Any mixed result: stop and decide.

**The control:** each file against itself must give 0 % went up and 0 % went down.

**My bet:** **(a)** on both frames. F1 is convective and changes a lot in 2 min; F3's stratiform rain changes less, but its run-1 share is also lower. My confidence is low, because the 2-min gap could overshoot.

### F4, F5 and change 1, graded only after (a)

- **The census floor is −40 dBZ** in `frame.ts`. That is the only change to the instrument.
- P4, P5 and P4b are graded **as frozen** in addendum 1, including P0 at 90 %, which is reported in light of the motion test.
- Run 1's numbers for F1-F3 are **not re-run and not re-graded**.

### 2.1: the palette, re-registered

| code | colour |
|---|---|
| rain 1 (15-20 dBZ) | MetPy's 15 dBZ entry `(0, 0, 246)`, dimmed to 85 % `(0, 0, 209)` or 70 % `(0, 0, 172)`. Rounding is `(v · p + 50) / 100` in integers |
| rain 2-12 (20-70 dBZ) | MetPy's 20-70 dBZ entries, exactly: `(0,255,0) (0,200,0) (0,144,0) (255,255,0) (231,192,0) (255,144,0) (255,0,0) (214,0,0) (192,0,0) (255,0,255) (153,85,201)` |
| rain 13 (≥ 75 dBZ) | **white `(255, 255, 255)`**, by ruling. MetPy's 75 dBZ entry is black |
| snow 1-13 | a ramp linear in RGB from `(30, 60, 160)` at code 1 to **pale blue `(175, 215, 255)` at code 8**, then flat for codes 9-13. Code 1 is dimmed like rain's |
| no coverage, missing, dry | as in 2.1 |

**The pin:** MetPy v1.6.3, `src/metpy/plots/colortable_files/NWSReflectivity.tbl`, sha256 `f73f0d6264d4…`. It's committed beside its test, with MetPy's BSD-3-Clause notice. Each entry is k/255 exactly, so × 255 gives integers. Stage 1's provisional values are **not** this table: codes 1-12 differ by up to 7 units, and code 13 was white.

**C3, re-registered** (it replaces C3 in 2.1):
- **(a)** no coverage and missing are achromatic.
- **(b)** dry, missing (dim and hatched) and no coverage are four different colours.
- **(c)** rain codes 2-12 equal the pinned table's 20-70 dBZ entries, **read from the committed file by the test**, and code 13 is white.
- **(d)** code 1 is the table's 15 dBZ entry times the dim factor, with the rounding above.
- **(e)** snow brightness (luma) rises with each code up to code 8 and stays flat after; the top is pale blue (blue > red) and **not white**.
- **(f)** **no snow colour equals any rain colour**, codes 1-13, both dim levels.

**C3's sabotages:**
- missing planted as violet `(110, 0, 110)` turns **exactly (a)** red;
- one rain entry off by one unit turns **exactly (c)** red;
- the snow top set to white turns **exactly (e) and (f)** red.

**Unchanged:** C1, C2, the build bands and the timings.

**Outside all of the above: stop and decide.**

## Addendum 3: rulings on the motion test, 2.1's hook change, and the look (2026-10-20; frozen before F4, F5 and change 1 are graded)

**Recorded as received:**
1. **Motion test: accepted, with no bracket test.**
   - On storm frames, SeamlessHSR's per-pixel band churn (92 % in 2 min) swamps the 16 % gap, so P0 can't discriminate there.
   - **P0 is recorded as UNINFORMATIVE on storm frames.** That is not a pass, and it is not re-graded.
   - **F3 is read as (a).**
   - Then **F4, F5 and change 1 are graded under the −40 floor**. The product ruling waits for their M5 decisions: ring and centre, at 15, 20 and 30 dBZ.
2. **The device-id hook:** the exemption is narrowed to the **single pinned table file, by path**, not all `*.tbl`. It must be proven again that an id-shaped token in an ordinary file is still refused, and blipscope-5c reviews the change in #439.
3. **The four faults** are accepted as recorded. Captures stay outside the clone.
4. **The palette look:** Daniel rules on 85 % against 70 %, and on dim against hatched. The board keeps alternating until then.

**Fixed now, so they can't be decided after the data is seen:**
- **"Storm frames" are F1.** It is the only convective frame. F2's P0 (96.37 %) passed, and it stands.
- **On F4 and F5, P0 is graded as frozen** (≥ 90 % of echo pixels), because neither is a storm frame. Below 90 % is the frozen outcome: stop and decide.
- **The instrument is unchanged since the motion test:**
  - `frame.ts` with the −40 floor (`1ad74dd`);
  - `check.py` with the pinned renderer (`8e1de79`);
  - a new `run2` mode that runs **only** F5, F4 and change 1, writing `results2.json`. Run 1's numbers aren't touched.
- **Graded as frozen:** P4, P5 and P4b (addendum 1), P0 (above), and the controls (self-comparison and −5 dBZ sabotage) on F4 and F5.
- **P4b's stop still holds:** 20 % or more of snowing hours reading dry on either product means **stop**. My prediction is that stop.
- **Outside all of the above: stop and decide.**

## Addendum 4: Daniel's palette look (2026-10-20, from photos of the board; frozen before the palette changes)

**Recorded as received:**
1. **Lowest rain band (15-20 dBZ): 70 %.** Also add a **55 %** swatch to the card for one more look: the deep-blue fringe dominates the storm frame.
2. **Missing: B, hatched.**
3. **Change:** the snow ramp's first band must be **clearly distinct from rain's 15 dBZ blue**. Start it at a lighter, paler blue and keep the ramp to pale blue at the top. Add a host check that snow code 1 and rain code 1 differ by a clear margin, **with the metric and threshold stated before measuring**.
4. **Change:** no coverage becomes a **darker neutral grey**: still achromatic, still distinct from dry and from hatched missing. Large no-coverage areas shouldn't outshine the weather.
5. Re-render on the board, with the card and the KDFW frame alternating as now, for one more look. **C1 and C2 must still hold.**

**My reading, flagged:** snow code 1 is **no longer dimmed**. Dimming darkens it, which works against "lighter, paler". So the 15-20 dBZ dim applies to rain only.

**The palette, fixed now:**

| element | before (2.1) | now |
|---|---|---|
| rain 1 on the frame | 85 % `(0, 0, 209)` | **70 % `(0, 0, 172)`**. The card shows 85 %, 70 % and **55 % `(0, 0, 135)`** |
| snow | `(30, 60, 160)` → `(175, 215, 255)`, code 1 dimmed | **`(120, 165, 230)` → `(190, 225, 255)`** at code 8, flat above, linear in RGB; code 1 **not** dimmed |
| no coverage | `(96, 96, 96)` | **`(40, 40, 40)`** |
| missing | dim `(48, 48, 48)` or hatched | **hatched only**: `(72, 72, 72)` lines where `(x + y) % 4 == 0`, on dry. The dim option is removed. The frame's renderer draws code 15 pixel by pixel |
| rain 2-13, dry | unchanged | unchanged |

**C3, re-registered** (it replaces addendum 2's C3):
- **(a)** no coverage and the missing hatch are achromatic.
- **(b)** dry, the missing hatch and no coverage are three different colours.
- **(c)** unchanged: rain 2-12 equal the pinned table, and 13 is white.
- **(d)** rain code 1 is the 15 dBZ entry times **55 %, 70 % and 85 %**, using `(v · p + 50) / 100`.
- **(e)** snow luma rises with each code up to code 8 and is flat after; the top is pale blue (blue > red) and not white.
- **(f)** no snow colour equals any rain colour, at any dim level.
- **(g), new: snow code 1 against rain code 1, at each of 55 %, 70 % and 85 %:**
  - **Metric:** CIE76 **ΔE\*ab**. sRGB (IEC 61966-2-1) is linearised, converted to XYZ, then to CIELAB with the D65 white point (0.95047, 1.0, 1.08883).
  - **Threshold:** **ΔE\*ab ≥ 40 and ΔL\* ≥ 25.** Lightness is the cue that survives a small LCD and colour-blind viewers.
  - **Predicted:** passes at all three levels. My estimate is ΔE around 80 and ΔL\* around 45; the thresholds grade, not the estimates.

**C3's sabotages:**
- the missing hatch planted as violet `(110, 0, 110)` turns **exactly (a)** red;
- one rain entry off by one turns **exactly (c)** red;
- the snow top set to white turns **exactly (e) and (f)** red;
- **new:** the snow start set to `(20, 30, 180)`, a blue close to rain's, turns **exactly (g)** red.

**On the board:**
- **C1:** B9 identical (the codes are unchanged).
- **C2:** the board's `WX,sprite_sha256` equals `palette_ref.py` at **70 %**. That sha256 is computed and committed **before flashing**.
- **Build:** static RAM within ±256 B of 26,824, and flash up by less than 4 KB from 463,667.
- **Timings:** B3, B4 and B8 in band; B5-B7 **MISS** on their frozen bands, within ±10 %; the budget met.
- The card and the frame keep alternating every 10 s.

**Outside all of the above: stop and decide.**

## Addendum 5: Daniel's second palette look (2026-10-20, photos of the board; frozen before any snow colour is chosen or measured)

**Recorded as received:**
1. **Rain 15-20 dBZ: 55 %.** 70 % looked the same as 85 % on the storm frame: blue dims little perceptually. **Final.**
2. **No coverage (the darker grey): approved. Missing, hatched: approved. Final.**
3. **The snow ramp:**
   - The paler start is approved in direction, but it's too washed out: codes ~6-13 look identical, and codes 1-5 barely step.
   - **Change:** start at a **medium sky blue**, still at or above the frozen C3g distance from rain code 1. Make **visible steps across codes 1-6** (15-40 dBZ, where snow actually sits). **Hold at a pale blue from code 6 up**: never white, with a clear margin from rain 75+ white.
   - **Freeze a host check before measuring:**
     - adjacent snow codes 1-6 differ by a stated minimum;
     - snow code 13 differs from rain code 13 by a stated minimum.
4. Re-render with the card and the KDFW frame alternating as now. C1 and C2 must still hold. **Daniel takes one last look at snow only; rain and the greys are final.**

**Fixed now, before any snow colour is chosen:**
- **The metric** is CIE76 ΔE\*ab, exactly as in C3g (addendum 4): sRGB is linearised, then converted to XYZ and to CIELAB with the D65 white.
- **(h), new:** each adjacent pair of snow codes 1-2, 2-3, 3-4, 4-5 and 5-6 differs by **ΔE\*ab ≥ 10**. Ten is about 4 × the just-noticeable 2.3: a step you see without comparing side by side.
- **(i), new:** snow code 13 against rain code 13 (white) differs by **ΔE\*ab ≥ 20**.
- **(e), re-registered:** snow luma rises with each code **up to code 6** and is **flat from 6 to 13**. The top is pale blue (blue > red) and not white.
- **(g), re-registered at the ruled level only:** snow code 1 against rain code 1 **at 55 %**, ΔE\*ab ≥ 40 and ΔL\* ≥ 25.
- **(d), re-registered:** rain code 1 is the 15 dBZ entry at **55 %**, using `(v · p + 50) / 100`.
- **(a), (b), (c), (f) and the pin are unchanged.**

**The snow colours are chosen AFTER this commit, to meet (e), (g), (h) and (i), and then measured by the host test.** The thresholds above don't move. Because the colours are picked against them, those checks become regression guards. Each one is proven able to fail by a sabotage:
- **S-h:** the snow start set to `(165, 205, 245)`, close to the top, so the ramp barely steps. It must turn **exactly (h)** red.
- **S-i:** the snow top set to `(240, 245, 255)`, near white but not white. It must turn **exactly (i)** red.
- **S-C3ef** (the snow top set to white) must now turn **exactly (e), (f) and (i)** red.
- **S-C3g, S-C3a, S-C3c and S-PIN** are as before.

**On the board:**
- **C1:** B9 identical.
- **C2:** the board equals `palette_ref.py` at **55 %**, with that sha256 committed before flashing.
- **Build:** RAM within ±256 B of 26,824, flash up < 4 KB from 463,667.
- **Timings:** B3, B4 and B8 in band; B5-B7 MISS within ±10 %; budget met.
- **The card:** the rain row (final, 55 %), the snow row (for the look), no coverage, hatched missing and dry. The rain dim comparison is removed.

**Outside all of the above: stop and decide.**

## Addendum 6: the final palette, and the rulings on 2.0 (2026-10-20; frozen before (e) is measured)

**Recorded as received:**
1. **Rain 15-20 dBZ: 55 %.** Final, as on the board. Close 2.1: record the final palette (the pinned table and its sha256) as a dated ruling, confirm C1 and C2 on the 55 % build, and ask blipscope-5c to review #439.
2. **The legend readings are confirmed:** the 40 dBZ gold counts as yellow (Moderate either way), and dry gets no label.
3. **The 2.0 rulings (on `981b2cb`), as written:**
   - **a. The hook: both.** Add one line to CLAUDE.md's bench/git notes: a fresh clone has `core.hooksPath` unset, so `.githooks/pre-commit` doesn't run until `git config core.hooksPath .githooks`. And set it in my clone. Daniel authorizes the CLAUDE.md edit. Its own small PR; blipscope-5c reviews and merges.
   - **b. P0: retired as a test.** It is recorded as a **falsified assumption**: at pixel scale, the composite is **not** ≥ SeamlessHSR, whatever the cause. It is not re-graded, and there are no more motion tests. **The decisions (M5) are the test.**
   - **c. Product: SeamlessHSR.** The basis: M5 agreed on F1, F4 and F5. On F2 (virga), SeamlessHSR was right. On F3 its centre missed light rain, which (d) handles differently.
   - **d. "Precipitation here now" (1c)** comes from the **NWS latest observation at the nearest station** (its present weather), not from the radar's centre pixel. Radar drives the 15 mi ring and the drawing. Recorded as a change to `docs/weatherscope-screens.md`, dated, in its own commit.
   - **e. The snow threshold:** before any wire-format change, measure the **15 mi ring** over change 1's 24 hours at **5, 10 and 15 dBZ** on **SeamlessHSR**, with predictions frozen first, and report the share of snowing hours the ring catches at each. The wire format for snow waits for that result and its ruling.
4. **Order:** close 2.1, then (e), then the legend bench build. The shared parts (the pipeline, alerts and the wire format) stay held for blipscope-5c.

### The final palette (ruled 2026-10-20 by Daniel, after three looks at the board)

**The source:** the code is `src/weatherscope/WxPalette.h` at `10e5012` (#439). Rain comes from **MetPy v1.6.3 `NWSReflectivity`**, pinned unmodified as `test/fixtures/wx/NWSReflectivity-metpy-v1.6.3.tbl`, BSD-3-Clause, **sha256 `f73f0d6264d420cb55efe79d58415ca056391a0cc607fa0efa061f3331418e2f`** (the committed blob, LF).

| code | rain | snow |
|---|---|---|
| 1 (15-20 dBZ) | `(0, 0, 135)`: the table's `(0, 0, 246)` at **55 %** | `(0, 110, 210)` |
| 2 | `(0, 255, 0)` | `(66, 129, 218)` |
| 3 | `(0, 200, 0)` | `(98, 149, 226)` |
| 4 | `(0, 144, 0)` | `(124, 169, 234)` |
| 5 | `(255, 255, 0)` | `(148, 190, 242)` |
| 6 | `(231, 192, 0)` | `(170, 212, 250)` |
| 7 | `(255, 144, 0)` | `(170, 212, 250)` (held) |
| 8 | `(255, 0, 0)` | held |
| 9 | `(214, 0, 0)` | held |
| 10 | `(192, 0, 0)` | held |
| 11 | `(255, 0, 255)` | held |
| 12 | `(153, 85, 201)` | held |
| 13 (≥ 75 dBZ) | **`(255, 255, 255)`**, by ruling (the table's entry is black) | held |

| other | colour |
|---|---|
| dry (0) | `(0, 0, 0)` |
| no coverage (14) | `(40, 40, 40)` |
| missing (15) | hatched: `(72, 72, 72)` where `(x + y) % 4 == 0`, dry elsewhere |

### (e) The snow ring at 5, 10 and 15 dBZ (frozen before measuring)

**The instrument.** `ring.ts`, committed before it runs, decodes each of change 1's 24 SeamlessHSR files. These are the same files run2 used, already downloaded. For each it:
- runs the census at the −40 floor;
- takes the **maximum raw value** over the grid cells whose centre lies **within 15 mi of KMSP**, using the device's flat projection (111.0 km per degree, longitude scaled by the cosine of KMSP's latitude). The −99 and −999 sentinels are not values;
- reports, per hour, that maximum and whether it reaches 5, 10 and 15 dBZ.

**The control,** read before any share is believed: F4's hour, 2024-12-02 06:53Z, is one of the 24. On that file, SeamlessHSR's **screen** ring showed echo at 15 and 20 dBZ and not at 30 (run2's M5). So **its raw ring maximum must be ≥ 20 and < 30 dBZ.** Anything else means the ring geometry or the decode disagrees with the screen: **stop**.

**Predicted:** the share of the 24 snowing hours whose ring maximum reaches each threshold (5 ≥ 10 ≥ 15 holds by construction):

| threshold | predicted share |
|---|---|
| ≥ 15 dBZ | **40-75 %** |
| ≥ 10 dBZ | **60-90 %** |
| ≥ 5 dBZ | **75-100 %** |

**Basis:**
- The centre cell reached 15 dBZ in only 1 of 24 hours.
- But F4's ring reached 20-25 dBZ while its centre read −1.1.
- A 15 mi ring holds about 1,800 cells, so its maximum sits well above any single cell.

**Outside the predictions:** a census or control failure stops the run. Anything else is reported as measured, and the snow wire format waits for the ruling on it.

## Addendum 7: the legend's bench build (2026-10-20; frozen before any legend code)

**Ruled:** screen 7, the legend (`docs/weatherscope-screens.md` §7), built as a bench version on the board alongside the KDFW frame, with a host check that **every swatch on it equals the palette table**. 2.1 is closed.

**What the board shows:**
- **The alternation:** every 10 s, the KDFW frame alternates with **the legend**. The palette card is retired; its look is done.
- **The legend's rows**, in plain words, each followed by its swatches:

  | row | swatches |
  |---|---|
  | Light rain | rain 1-4 |
  | Moderate | rain 5-7 |
  | Heavy | rain 8-10 |
  | Very heavy / possible hail | rain 11-13 |
  | Snow, light → heavy | snow 1-6 (7-13 hold code 6's colour) |
  | No radar coverage | 14 |
  | No data | 15, hatched |

- **Dry** gets no swatch (ruled). The optional dBZ text is left out of the bench version.

**How the check stays independent of the code that draws.** A serial byte `L` asks the board to:
- draw the legend into the sprite;
- print one line per swatch: `WXL,<i>,<kind>,<code>,<x>,<y>,<w>,<h>`;
- dump the legend **sprite's own bytes**, RGB565, as 240 tagged rows (`WXS,<row>,<crc32>,<base64>`) with blocking writes, the same discipline as B9.

A host script then:
- refuses an incomplete dump;
- checks **every pixel of every declared swatch** against the colour the **final palette** (addendum 6) gives its code. It derives those colours itself, from the pinned table and the ruled values, as `palette_ref.py` does. Solid swatches must be one colour throughout; "No data" must be the hatch pattern exactly;
- writes a PNG of the board's legend for the record.

**Predicted:**

| check | predicted |
|---|---|
| **L1**: every swatch equals the palette table | **all 21 swatches** (rain 13, snow 6, no coverage 1, no data 1): every pixel equals its expected RGB565 |
| **L2**: coverage | the board's swatch list holds rain 1-13 once each, in the groups above; snow 1-6 once each; 14 once; 15 once; nothing for dry. **Exactly the ruled mapping** |
| **L3**: the frame is unchanged | C1 (B9 identical) and C2 (the frame sprite at boot, `a88c3a99fa5c…`) |
| L4: the instrument | the legend dump arrives complete: 240 rows, every CRC good |
| Build | static RAM within ±512 B of 26,912; flash up by less than 6 KB from 465,447 |
| Timings | B3, B4 and B8 in band; B5-B7 MISS within ±10 %; budget met |

**The sabotage, S-L, on the board.** One swatch, Moderate's first (rain 5, yellow `(255, 255, 0)`), is drawn as `(255, 255, 64)`:
- planted with an assertion that it applied;
- flashed and checked: **L1 fails on exactly that swatch**, and L2 still passes;
- restored byte for byte (sha256 checked), reflashed, and checked again: L1 back to all 21.

**Outside all of the above: stop and decide.**

## Bench rules (unchanged from Stage 1)

- **The board** is `907069326E64`, always found by its serial.
- **Captures:** one open of the port, never reattached; the measuring tool is never retried until it passes.
- **Files:** downloaded MRMS files stay out of git; only ICAO codes are printed, never a coordinate.
- **Merging:** blipscope-5c reviews and merges. Daniel's reviewer chat is the only source of rulings.

## Results

### 2.0, run 1 (2026-10-20): F1-F3 measured; two stops

**How it ran:**
- **Scripts:** `tools/weatherscope/product-check/`, committed as run. The paths are the session scratchpad's.
- **Shared tools:** the spike's tools pinned at `a5db678`: `mrms.ts`'s decode and ruled mapping, and `crop.ts`.
- **Screens:** `render_ref.py` from `main`.
- **Data:** MRMS files from `noaa-mrms-pds`, kept outside git; METARs from the IEM archive.

**The frames the rules chose** (METARs and the composite only):

| frame | airport | chosen hour | composite file | SeamlessHSR file | gap |
|---|---|---|---|---|---|
| F1 | KDFW | Stage 1's frame | 22:00:42Z | 22:00:00Z | 42 s |
| F2 | KPHX | 2024-06-14 01:51Z: VIRGA, no precipitation. **The first** METAR candidate; its composite had echo within 15 mi | 01:50:39Z | 01:50:00Z | 39 s |
| F3 | KATL | 2024-11-14 09:52Z: `-RA`. The run was 08:52 / 09:52 / 10:52, at 10 °C | 09:52:43Z | 09:52:00Z | 43 s |
| F5 | KPHX | 2025-03-07 13:51Z: `RA`, 9.4 °C. The run was 12:51-14:51, with no VIRGA | 13:50:36Z | 13:50:00Z | 36 s |
| F4 | KMSP | 2024-12-02 06:53Z: `-SN`. The run was 05:53-07:53 | 06:52:36Z | 06:52:00Z | 36 s |

Change 1's sample is 24 snowing hours at KMSP, 2024-12-02 05:53Z to 12-11 15:53Z.

**STOP 1: the anchor fired on F5, F4 and all 24 change-1 hours.**
- **What it found:** values below −30 dBZ that are not sentinels. F5 has 97 in the composite and 8 in SeamlessHSR; F4 has 47 and 46. Each grid has 24.5 M cells, and the lowest value is −32. F1-F3 have none: their lowest real values are −15 to −25.
- **Not graded:** F4, F5 and change 1. Their screens were never rendered; their centre values were recorded and **not read**.
- **What the values are.** This is a probe of the anchor, `oddvals.ts`, not a grade:
  - there are many distinct values, in 0.5 dB steps in the composite and 0.1 dB steps in SeamlessHSR;
  - the counts taper smoothly from just above −30 into −30.5 to −32;
  - **none** lies inside either airport's 60 mi crop box;
  - so they look like the low tail of real reflectivity, below the −30 floor I chose. The ruled mapping makes them dry either way.
- **The alternative: a new sentinel**, meaning no coverage or missing. The data argues against it: a sentinel is one repeated value, and these are spread over at least 6 values with smoothly tapering counts. **Not argued away:** every failing file is from 2024-12 on and every passing one from before, so MRMS's lower bound may have moved. The floor needs a ruling, not my edit.
- **My plan contradicted itself here.** Its frames section says to replace a frame whose anchor fails; the census rule and the "outside" list say stop. I stopped, because replacing frames would hide a fault shared by recent files.

**The controls on F1-F3: all as predicted.**

| | self-comparison | −5 dBZ sabotage |
|---|---|---|
| F1 | 100.0000 %, identical M5 | **+1 on all 11,025** echo pixels. No decision sat on a threshold, since there is no echo within 15 mi |
| F2 | 100.0000 %, identical M5 | **+1 on all 9,817**. The 20 dBZ ring decision flipped (ring max code 2 → 1) |
| F3 | 100.0000 %, identical M5 | **+1 on all 36,305**. The 30 dBZ ring decision flipped (4 → 3), and so did the 15 dBZ centre decision (1 → 0) |

**F1-F3 against the predictions:**

| | predicted | result | |
|---|---|---|---|
| P0, F1 | composite ≥ SeamlessHSR in ≥ 90 % of echo pixels | **83.69 %**: SeamlessHSR is higher on 1,841 of 11,285 | **✗** |
| P0, F2 | as above | **96.37 %** | ✓ |
| P0, F3 | as above | **87.82 %**: SeamlessHSR is higher on 4,466 of 36,653 | **✗** |
| P1, F1 storm | SeamlessHSR's echo share 75-100 % of the composite's | **43.5 %** (10.61 % against 24.37 % of the disc) | **✗** |
| | class agreement ≥ 95 % | **85.09 %** | **✗** |
| | M5 agrees | agrees: no echo within 15 mi or at the centre in either | ✓ |
| P2, F2 virga | SeamlessHSR's echo share ≤ 60 % | **14.2 %** (3.09 % against 21.70 %) | ✓ |
| | M5(a) differs at 15 dBZ: composite echo within 15 mi, SeamlessHSR none | the composite **yes**, SeamlessHSR **no**, at both 15 and 20 dBZ; both no at 30 | ✓ |
| P3, F3 stratiform | echo share 85-100 % | **85.6 %** (68.70 % against 80.24 %) | ✓ |
| | composite ≥ +1 band in ≥ 30 % of echo pixels | **40.8 %** | ✓ |
| | M5 agrees | the ring agrees at all three thresholds; **the centre differs at 15 dBZ**: composite 19.5 dBZ (code 1), SeamlessHSR 12.4 dBZ (code 0) | **✗** |

**STOP 2: P0 is below 90 % on F1 and F3.** The pre-registered meaning: the products may not share the mosaic the way the plan assumed. Stop and decide. **Two explanations, and this run excludes neither:**
- **(a) The files' time offset** (42 s on F1, 43 s on F3). Storms move, so some pixels change in either direction. Not excluded: this run has no measure of how much one product changes in 42 s.
- **(b) Different source grids or QC.** The products are not built from the same 3D grid and QC the way the plan assumed: a column maximum cannot be lower than one level of its own column.

**The test that would tell them apart, not run:** the same comparison between two **consecutive SeamlessHSR files**, 2 min apart, on F1 and F3. That is motion alone. If its "went up" share is near the 12-16 % here, (a) explains P0; if it is much smaller, (b) holds.

**"Negligible" is not met on what was measured.** On F2, M5 differs: the composite says echo within 15 mi on a virga hour, and SeamlessHSR doesn't. F3's centre differs at 15 dBZ, and class agreement is 80.5-86.9 %, not ≥ 98 %. F4 and F5 weren't graded, so this verdict is partial.

**Two observations for the ring-threshold ruling** (reported, not graded):
- **F3:** KATL's METAR for that hour reports `-RA`. The centre reads 19.5 dBZ on the composite and **12.4 dBZ on SeamlessHSR**, so at 15 dBZ SeamlessHSR would call the station dry while it reports light rain. This is one sample.
- **F2:** the composite's ring maximum is code 2 (20-25 dBZ), so the virga echo is light. At 30 dBZ the two products agree that there is no echo.

**Waiting on rulings:**
1. the anchor's floor;
2. P0's stop: run the motion test, or accept the result as it stands;
3. then F4, F5 and change 1.

**A correction to run 1's "How it ran".** Run 1's screens did **not** come from `main`'s `render_ref.py`. They came from #434's branch version, sha256 `450e01cc4ff6…`: the clone was on that branch while run 1 ran. That version is now `main`'s, at `122e118`. #437's branch carries the older `7c1275c8…`, which has no `--out`. A branch switch swapped it in, and the motion test's first attempt then stopped before measuring anything, with no screen written. The renderer is now a pinned copy of `450e01cc4ff6…`, asserted when `check.py` loads (`8e1de79`).

### 2.0, the motion test (2026-10-20, frozen in addendum 2): F1 outside, F3 (a). STOP

**How it ran:**
- `motion.py` (`1ad74dd`), with the pinned renderer, on two consecutive SeamlessHSR files per frame, exactly 120 s apart.
- The census passed at the −40 floor (no values below it).
- Each file against itself gave 0 went up and 0 went down.
- **The early file of each pair reproduces run 1:** SeamlessHSR's echo share is 10.61 % on F1 and 68.70 % on F3, both equal to run 1's.

| frame | files | pixels with echo in either | **went up** | went down | changed level | run 1's "SeamlessHSR higher" | reading |
|---|---|---|---|---|---|---|---|
| F1 KDFW | 22:00:00Z → 22:02:00Z | 6,407 | **44.75 %** (2,867) | 47.21 % (3,025) | 92.0 % | 16.31 % | **outside**: more than 4 points above |
| F3 KATL | 09:52:00Z → 09:54:00Z | 31,695 | **8.72 %** (2,764) | 12.13 % (3,844) | 20.8 % | 12.18 % | **(a)**: within 4 points (3.46 below) |

**STOP, as pre-registered: a mixed result means stop and decide.** F4, F5 and change 1 stay **ungraded**, and their values stay unread.

**What the numbers show,** reported and not re-graded:
- **F1.** SeamlessHSR's echo **area** barely changes in 2 min, from 10.61 % to 10.17 % of the disc, with class agreement 92.46 %. But **92 % of its echo pixels change band**. On a convective frame, then, the band at a pixel churns far faster than the echo moves. Over 2 min that churn is about 3 × run 1's 16.31 %, so it could easily cover run 1's 42 s offset. This test can't show that: it measured 2 min, not 42 s. That is the reason the addendum put a share well above 16 % outside (a).
- **F3.** The rule reads (a). But the two directions differ: went up 8.72 % and went down 12.13 %, with run 1's 12.18 % in between. And run 1's offset was 43 s, shorter than the 120 s here, so motion at 43 s should be **no larger** than at 120 s. On that reading, motion covers only part of F3's 12.18 %, and the rest would point to (b). This is an observation outside the frozen rule. It isn't graded, and the rule's reading stands.

**Not established:** whether the two products differ in grid or QC. A test at the right time scale would pair files only as far apart as run 1's (40-45 s), but **SeamlessHSR publishes every 2 min** and the composite at :36-:43 s, so no same-product pair is that close. A cleaner discriminator needs a ruling: for example, the composite at :42 against SeamlessHSR at the following :00 **and** the preceding :00, which brackets the offset from both sides.

### 2.1, the palette on the board (2026-10-20): all as predicted

**Code:** `feat/weatherscope-stage2-palette` at `e80ae56`, built from `main` `122e118`.
- `src/weatherscope/WxPalette.h`: the palette.
- `test/host/test_wx_palette.cpp` and `sabotage_wx_palette.py`: its test and sabotages.
- `test/fixtures/wx/palette_ref.py`: C2's host render.
- `test/fixtures/wx/NWSReflectivity-metpy-v1.6.3.tbl`: the pinned table, with MetPy's notice. Its committed blob hashes to `f73f0d6264d4…`, the pin.

| check | predicted | result | |
|---|---|---|---|
| **C3 (a)-(f)**, host | all pass | **all pass**, plus the pin control: the file's 15 dBZ entry is `(0,0,246)` and its 75 dBZ entry `(0,0,0)`. `run.sh`: ALL HOST TESTS PASSED, 0 B on stderr | ✓ |
| C3's sabotages | violet missing → exactly (a); a rain entry off by one → exactly (c); snow top white → exactly (e) and (f) | **each as predicted**. (f) counted 24 clashes; the real header was unchanged | ✓ |
| Build | static RAM within ±256 B of 26,824; flash up by less than 4 KB | **26,912 B (+88); 465,751 B (+2,084)** | ✓ |
| **C1**: the codes unchanged | KDFW dump sha256 `344383b4…` | **240/240 rows, `344383b4…`, 0 of 57,600 differ**, equal to the board's `fb_sha256` | ✓ |
| **C2**: board colours equal host colours | the sprite identical to an independent host render | board `WX,sprite_sha256` = host `504651db359d…`: **identical** | ✓ |
| B3, B4, B8 | in their bands | 153,608 B; 0 B; 25.789 ms | ✓ |
| B5, B6, B7 | **MISS** on the frozen bands, each within ±10 % of its measurement | 2.868 / 3.582 / 22.578 ms: **MISS** on all three; +0.0 %, −0.1 %, **+2.6 %** | ✓ |
| Budget (separate) | ≤ 100 ms per frame; ≥ 8 fps | 54.8 ms; 19.5 fps | ✓ |

**C2 can fail.** Three wrong models of the sprite give three other hashes:
- little-endian storage: `edf7af1390aa…`;
- the 70 % dim: `659fba0bc9f6…`;
- a rounding `color565`: `3d28db24363e…`.

The board matched only the model C2 predicted.

**Render is +2.6 %** (22,578 µs against 22,012): the colour now comes from `wxpalette::Code()` instead of a table lookup. That's inside the ±10 % repeat band, and the budget is unaffected.

**Flash and capture:**
- **The flash:** `bench-flash.ps1` resolved COM6 from the serial; esptool read MAC `90:70:69:32:6e:64`; all four images verified. The image was `8e9c1e26f67c…`.
- **The capture:** a single open, saved **outside the clone** as `captures/wx-2.1-palette-130158.txt` (81,694 B, sha256 `4ff5b7563431…`).

**The board now shows the palette for Daniel's look.** It alternates every 10 s between the KDFW frame, in rain, with code 1 at 85 %, and the card. The card shows:
- the 13 rain swatches;
- code 1 at 85 % and 70 %;
- the 13 snow swatches;
- no coverage;
- missing, dim (A) and hatched (B);
- dry.

**The palette is final only after Daniel's look.** Open choices for that look: code 1 at 85 % or 70 %, and missing dim (A) or hatched (B).

**Two process faults, recorded:**
- **The Stage 1 and addendum-6 board captures are gone.** They were written to the clone's gitignored `bench-logs/`. When I added `tools/weatherscope` to the clone's sparse checkout (cone mode), git re-applied the sparse rules and removed that folder: in cone mode it deletes ignored files in directories outside the cone. Their sizes and sha256 values are recorded above; the raw files can't be recovered. Captures now go to the scratchpad, outside the clone.
- **The device-id hook refused the pinned table.** Its 16-digit fractions match the id shape. `.githooks/pre-commit` and `scripts/check_device_ids.py` now skip `*.tbl`, as they already skip CAD files. Control: the hook still refuses an id-shaped token in an ordinary file.

### 2.0, F4, F5 and change 1 under the −40 floor (2026-10-20, frozen in addendum 3): three stops

**How it ran:**
- `check.py run2` (`7d206fb`) on the files run 1 had already downloaded;
- `frame.ts` with the −40 floor; the pinned renderer;
- output to `results2.json`. Run 1's results are untouched.

**Anchors and controls:**
- **The census passes on both frames:** no values below −40. The low tail still reaches −32 on both products.
- **The self-comparisons are 100.0000 %, with identical M5.**
- **The sabotage gives +1 on every echo pixel:** 37,766 on F5, 327 on F4. F4's 20 dBZ ring decision, which sat on its threshold, flipped (ring max code 2 → 1). F5's ring max is code 7, so it had no decision on a threshold.
- **All 24 change-1 hours pass the census.** The composite and SeamlessHSR files are 31-43 s apart.

| | predicted | result | |
|---|---|---|---|
| **P5, F5** (KPHX, western ground rain): M5 | agrees at every threshold | **agrees**: ring and centre, at 15, 20 and 30 dBZ, all yes on both products | ✓ |
| P5: SeamlessHSR echo at the centre | yes, code ≥ 1 | **yes: code 4, 32.4 dBZ** (the composite: code 5, 36.5 dBZ) | ✓ |
| **P0, F5** | ≥ 90 % (not a storm frame) | **73.57 %**: SeamlessHSR higher on 10,436 of 39,490 echo pixels | **✗ STOP** |
| **P4, F4** (KMSP, snow): class agreement | ≥ 95 % | **98.94 %** | ✓ |
| P4: codes equal | ≥ 70 % of echo pixels | **16.2 %** (94 of 579) | **✗** |
| P4: M5 | agrees | **agrees**: the ring yes at 15 and 20 dBZ on both, no at 30; the centre no at every threshold on both | ✓ |
| **P0, F4** | ≥ 90 % | **55.79 %**, over only 579 echo pixels | **✗ STOP** |
| **P4b, change 1**: dry share at the centre | composite 20-50 %; SeamlessHSR 25-60 % and ≥ the composite's | **composite 23/24 = 95.8 %; SeamlessHSR 23/24 = 95.8 %** | **✗ in magnitude.** SeamlessHSR ≥ composite holds (equal) |
| P4b's stop | 20 % or more → stop (my prediction: a stop) | **95.8 %: the stop fires**, as I bet, at about twice the top of my range | **STOP** |

**Change 1's raw centre values** (24 snowing hours, KMSP, December 2024):

| | no echo (−99) | below 5 | 5-10 | 10-15 | 15 or more |
|---|---|---|---|---|---|
| composite | 4 | **12** | 4 | 3 | 1 |
| SeamlessHSR | 7 | **12** | 1 | 3 | 1 |

**STOP (pre-registered):**
- **P0 below 90 % on F4 and F5.** The motion test measured SeamlessHSR's 2-min band churn at 92 % (F1) and 20.8 % (F3); whether churn covers F4 and F5 is not established, since no motion test was run for them.
- **P4b's large dry share.** The snow threshold needs a ruling before the wire format.

**Two readings of change 1, and the data excludes neither:**
- **(a) Light snow at KMSP is genuinely weak:** half the hours sit below 5 dBZ at the airport's cell, far below the 5-15 dBZ "often" figure.
- **(b) A single 1 km cell under-samples it.** That cell is one sample, where the device's ring decision (within 15 mi) looks at many. **F4's ring said yes at 15 dBZ on both products while its centre said no.** Change 1 measured only the centre, not the ring, so this run can't separate (a) from (b).

**For the product ruling: the M5 decisions on every frame,** ring and centre, at 15, 20 and 30 dBZ:

| frame | do the two products agree? |
|---|---|
| F1, storm | agree at every threshold |
| **F2, virga** | **differ:** the composite says echo within 15 mi at 15 and 20 dBZ; SeamlessHSR says none. Both no at 30 |
| **F3, stratiform** | the ring agrees; **the centre differs at 15 dBZ**: composite 19.5 dBZ, SeamlessHSR 12.4, with a METAR of `-RA` |
| F4, snow | agree at every threshold |
| F5, western rain | agree at every threshold; SeamlessHSR has echo at the centre (P5 held) |

### 2.1, Daniel's look applied (addendum 4): all as predicted

**Code:** #439 at `f2a78ce`.

| check | predicted | result | |
|---|---|---|---|
| C3 (a)-(g), host | all pass | **all pass**. **C3g:** ΔE\*ab **76.8 / 85.2 / 95.5** and ΔL\* **52.9 / 47.0 / 41.4** at 55 / 70 / 85 %, against ≥ 40 and ≥ 25. My estimate was about 80 and 45. Controls: the pin, and sRGB white converting to L\* 100 | ✓ |
| The four sabotages | exactly (a); (c); (e) and (f); (g) | **each exactly as predicted**. S-C3g brings ΔE down to 9.4 at 70 % | ✓ |
| Build | ±256 B RAM; flash up < 4 KB | **26,912 B; 465,699 B** | ✓ |
| **C1** | B9 `344383b4…` | **identical**, 240/240 rows, 0 differ | ✓ |
| **C2** | board = `palette_ref.py` at 70 %, sha256 committed **before** flashing (`659fba0bc9f6…`) | **identical** | ✓ |
| B3, B4, B8; B5-B7; budget | in band; MISS within ±10 %; met | 153,608 B, 0 B, 25.791 ms; 2.870 / 3.584 / 22.695 ms (render **+3.1 %**); 54.9 ms, 19.5 fps | ✓ |

- **Flash:** the port was resolved from the serial, MAC `90:70:69:32:6e:64`, all four images verified, image `1bbad81f405c…`.
- **Capture:** saved outside the clone as `captures/wx-2.1-add4-134932.txt` (81,694 B, sha256 `535bba02306e…`).
- **The board alternates** the KDFW frame (rain code 1 at 70 %) and the card (rain code 1 at 85 / 70 / 55 %; the new snow ramp; no coverage; hatched missing; dry) **for Daniel's next look**.

**The device-id guards, as #439 has them now:**
- **Path-only exemption:** narrowed to the single pinned path by ruling (`7b07685`).
- **blipscope-5c's review then found a hole.** Its probe (d) appended an id to the pinned table, and both guards let it through: the sha256 pin existed only in prose.
- **Fixed (`a0c34e9`):** the guards skip the table **only while its blob hashes to the pin**. The hook hashes the staged blob and the checker the HEAD blob, both from one list, `scripts/device-id-pinned-files.txt`.
- **Probes (a)-(d), plus (d-real) on the real path, all as intended.** blipscope-5c's re-review is pending.

### 2.1, Daniel's second look applied (addendum 5): all as predicted

**Code:** #439 at `10e5012`. Rain code 1 is at 55 % and final; the greys are final.

**The snow colours.** These were chosen **after** addendum 5 froze the thresholds (`50fd4dd`), as design against them, and then measured by the host test:
- **The construction:** six entries, interpolated evenly in CIELAB from a medium sky blue `(0, 110, 210)` to a pale blue `(170, 212, 250)`. Code 6's colour holds through 13.
- **Why a table:** a linear RGB ramp between sky-blue endpoints left its last steps at about 7.7-10.0, at or under the bar. Even CIELAB steps give each step about the same visible size.

| check | frozen | result | |
|---|---|---|---|
| **C3h**: adjacent snow codes 1-6 | ΔE\*ab ≥ 10 each | **10.8 / 10.9 / 10.5 / 10.7 / 11.0** | ✓ |
| **C3i**: snow 13 against rain 13 (white) | ΔE\*ab ≥ 20 | **28.9** | ✓ |
| C3g at 55 % | ΔE ≥ 40, ΔL\* ≥ 25 | **50.0, 32.6** | ✓ |
| C3a-f, the pin, the `sha` and `lab` controls | pass | **all pass**; ALL HOST TESTS PASSED, 0 B on stderr | ✓ |
| The sabotages | each fails exactly its own checks | **all seven as predicted**: S-h fails exactly C3h (steps 0.4-0.9); S-i exactly C3i (6.4); S-C3ef exactly (e), (f) and (i); S-C3g, S-C3a, S-C3c and S-PIN as before | ✓ |
| Build | ±256 B RAM; flash < +4 KB | **26,912 B; 465,447 B** | ✓ |
| **C1** | B9 `344383b4…` | **identical**, 240/240 rows | ✓ |
| **C2** | board = `palette_ref.py` at 55 %, `a88c3a99fa5c…`, committed before flashing | **identical** | ✓ |
| B3, B4, B8; B5-B7; budget | in band; MISS within ±10 %; met | 153,608 B, 0 B, 25.801 ms; 2.867 / 3.585 / 22.612 ms (render **+2.7 %**); 54.9 ms, 19.5 fps | ✓ |

**How it ran:**
- **The flash:** the port was resolved from the serial, MAC `90:70:69:32:6e:64`, all four images verified, image `a3585a89d356…`.
- **The capture:** outside the clone, `captures/wx-2.1-add5-154751.txt` (81,694 B, sha256 `efa5ef56ec51…`).

**The board alternates** the KDFW frame (rain code 1 at 55 %) and the card **for Daniel's last look, at snow only.** The card shows:
- the final rain row;
- the snow row, with taller swatches;
- no coverage, hatched missing, and dry.

**Also on #439 since addendum 4:**
- **`601a236` / `162ad9f`:** the palette test enforces the table's sha256 pin itself (SHA-256 checked against the "abc" vector), with sabotage S-PIN.
- **A slip of mine:** `601a236` carried an id-shaped probe token. The device-id hook refused it, but the hook isn't installed in my clone and I had chained it with `;`, so the commit went through. **CI's `no-real-device-ids` caught it** (red on `601a236`, green on `162ad9f`), and it was fixed forward with the allowlisted synthetic id.

### 2.1 closed (2026-10-20): the final palette on the board

- **The palette was ruled final** and recorded in addendum 6, with the pinned table and its sha256. Every value in that table was **checked by program** against `WxPalette.h` at `10e5012`: 13 codes plus 3 others, all matching.
- **C1 and C2 confirmed again on the running 55 % build**, on a fresh capture at close (`captures/wx-2.1-close-*.txt`, 81,694 B, sha256 `efa5ef56ec51…`): B9 identical, 240/240 rows; the sprite `a88c3a99fa5c…`.
- **That capture is byte-identical to 15:47's.** It's the same boot of the same image, whose report re-prints the boot measurements.
- **An instrument slip, caught:** the first attempt at this capture ran from #437's branch, which predates #434 and has no `capture_stage1.py`. The port was never opened, and nothing was measured or reported from it.
- **#439** is ready for review, with blipscope-5c.

### (e) The snow ring at 5, 10 and 15 dBZ (2026-10-20, frozen in addendum 6): as predicted

**How it ran:**
- `ring.py` and `ring.ts` (`5ca695f`), on change 1's 24 SeamlessHSR files, the same files run2 used.
- The census passed on all 24 at the −40 floor.
- Each ring holds **2,104** grid cells within 15 mi of KMSP, by the device's flat projection.

**The control:** at F4's hour (the 06:52:00Z file), the raw ring maximum is **23.4 dBZ**, inside the frozen [20, 30) and agreeing with run2's screen ring (yes at 15 and 20 dBZ, no at 30). ✓

| threshold | the ring catches | predicted | |
|---|---|---|---|
| ≥ 15 dBZ | **17 / 24 = 70.8 %** | 40-75 % | ✓ |
| ≥ 10 dBZ | **18 / 24 = 75.0 %** | 60-90 % | ✓ |
| ≥ 5 dBZ | **20 / 24 = 83.3 %** | 75-100 % | ✓ |

**What it means for the snow wire format** (for the ruling; nothing is decided here):
- **The ring at 15 dBZ, which WXC1 v1 can already carry, catches 17 of the 24 snowing hours.** The airport's own cell caught 1.
- **Going below 15 dBZ adds 3 hours:** one at 10 dBZ (11.5), and two more at 5 dBZ (6.7 and 8.1).
- **4 hours (16.7 %) have no echo anywhere in the ring:** 12-03 00:52, 12-03 04:52, 12-04 23:52 and 12-11 15:52, all hours whose METAR reported snow. **No threshold catches them.** Light snow under the beam, or a timing mismatch, could explain them; this run doesn't separate the two.
- **So a lower snow threshold would raise ring detection from 70.8 % to at most 83.3 %**, and only by carrying sub-15 dBZ snow on the wire, which is the shared format change held for blipscope-5c.
