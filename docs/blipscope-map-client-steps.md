# The Blipscope map client: its steps, their predictions, and their records

**Owner: the map client session** (ruled 2026-11-07 ~19:08Z). The requirements live in
[blipscope-basemap-client.md](blipscope-basemap-client.md). This file holds each step's frozen predictions and what
was read against them. A step's rows are written here before its code, and a miss stays on record.

**Status, 2026-11-08 ~20:30Z:** step M's host side is merged (#619, `e934e93b`); its bench rows (CF1-CF6) wait for E2:08.
**Step A's predictions are below, for review.** No step A code exists.

## The ruling on the first report, verbatim (reviewer chat, pasted by Daniel, 2026-11-07 ~19:08Z)

> For: the map client, from the reviewer chat, 2026-11-07 ~19:08Z
>
> Your first report is accepted, and step M's predictions stand as frozen (CM1-CM9, CF1-CF6, the verdict rules). The
> bench image carries its own bench marker, added to check-no-bench-hooks.sh. The KSFO reading comes before the layer
> design counts as settled, as you say.
>
> Q-MC1: yes. r is the configured radius, rounded up to cover the view plus the 2-dp offset, capped at 222 km. The map
> server re-reads at r=161 before anything freezes.
> Q-MC2: yes, the wedge's green added to the map pixel. It's bit-identical over black, and Daniel judges it on glass.
> Q-MC3: yes, Daniel looks during M's window. Dither only if he asks.
> Q-MC4: yes. The map compiles under #609's WX_LAYER_SHIPS, and its strings join check-no-weather.sh, with the wxlook
> control finding them. Your first firmware push waits for #609's merge.
> Q-MC5: KJAC and KSFO come from the map server, committed with their provenance and licence files as bend's are.
> Q-MC6: yes. RAM only. A fetch delay of a device-id hash mod 2 h on the boot after an OTA and after the nightly reboot;
> at once on power-on, a first-run restart or a config save, with the mechanism registered in step A. Neither the id
> nor the hash is ever printed.
> Q-MC7: (a) yes. (b) yes, no peaks in the request. (c) place names are Daniel's call, asked in chat; nothing for them
> before step B.
> Q-MC8:
> - E2:08 for about 1 h after blipscope-5c's v19 window closes, reserved and announced through Daniel.
> - The 100 ms rebuild bar: yes. Daniel's eye on glass has the last word on a visible hitch.
> - GetBytes's Retry-After stays in your step A, since you're its only consumer; blipscope-5c has been told.
> - The client plan's first line names you as its owner: yes.
> DO1's writers: yes. The web renderer writes map-layer "false" in writing, as it does wx-layer, with check_wx_off
> extended.
> Coming from the map server's #612 once ruled: packages hold more water rings and fewer secondary roads, with no format
> change. Nothing in step M depends on it.

## Place names: Daniel's decision on Q-MC7 (c), verbatim (reviewer chat, pasted by Daniel, 2026-11-07 ~21:13Z)

> For: the map client, from the reviewer chat, 2026-11-07 ~21:13Z
>
> Q-MC7 (c), Daniel's decision: place names are on by default, with a setting to turn them off.
> - The rule, for step B's predictions: the Weatherscope's (the largest towns first, at most 8, the centre kept clear),
>   and a name never covers an airport code. Daniel judges it on glass.
> - The setting follows the map setting's rules: a new key (at most 15 characters) that no published image has ever
>   written; on when unset, and rendered ticked; hidden in v19 under the same switch; the web renderer writes it "false"
>   in writing. It gets its own rows and plants beside DO1-DO4 and DOC1-DOC3.

## What the first report found, which the ruling answers

- **The radar's default view is 100 mi (161 km).** The config page's `radius` defaults to "100" and its unit to "mi"
  (`src/ConfigurationWebServer.cpp`, `include/DisplayUnits.h`). Every server reading so far used `r=97`, `eps=100584`
  (`docs/basemap-a1/instruments/a1-request.sh`). That package is a 194 km square, **46 % of a default face**, with a
  hard edge. `r=161` is 2.75 times the area. Hence Q-MC1.
- **S2 in RGB565.** Over every shade 0-255, S2 gives 21 distinct 8-bit colours and **9 distinct RGB565 colours** (the
  sprite stores 565; `wxs2::To565` truncates). Bend's real shade lands on 7-8 of them, and the largest band holds
  51-77 % of the pixels, by level. The approved mock-up was 8-bit. Hence Q-MC3.
- **The wedge erases what is under it.** `wxsweep::Lit565` writes the wedge's green opaque on every dry pixel, so a map
  under the sweep would vanish as the beam passes. Adding the green to the pixel below is bit-identical to today over
  black. Hence Q-MC2.
- **The web renderer is one of DO1's writers.** It has no pixels, and an unset key reads as on. Hence the ruling's
  DO1 line.

These counts come from a read-only reading of the committed `test/fixtures/wx/basemap/bend.bmpk` that prints counts
only, never the origin or a coordinate.

## Step M: the map layer, and its frame cost (frozen 2026-11-07, accepted ~19:08Z)

**What it is.**
- A pure header, `include/MapLayer.h`: a validated package drawn into a 240 x 240 RGB565 layer.
  - It uses `include/BasemapPackage.h` unchanged.
  - It reuses `wxbase::FillRing`, `wxraster::Line` and `wxraster::Text` by include, as `include/WxS2.h` already
    includes a Weatherscope header. No Weatherscope file changes.
- A bench-only env that reads the frame cost from an embedded package, before any fetch exists.

**Why the frame should be cheap with weather on.** The map is drawn once into a layer, rebuilt only on zoom, config or
a new package. Weather is folded on top of it at weather's own rebuild. So a steady frame stays one `memcpy` and draws
no vectors.

### Host rows

They live in `test/host/test_map_layer.cpp`, run under WSL g++ 13.3 and MinGW. Every plant is anchored first and
restored by bytes. A plant that turns any row outside its set red, or turns nothing red, is a miss. BLIND counts as
FAIL.

| row | check | predicted | sabotage (turns exactly) |
|---|---|---|---|
| **CM1** | The gate | 27 `neg-*` each refused with its README reason, 0 pixels written; `bend` and `syn-min` built | **S-CMCRC**, the gate also accepts `kCrc`: {CM1 neg-crc} |
| **CM2** | The fast path against §9's double reference: every Bend vertex, 5 views, 4 rotations, home offset up to 0.005° | max error ≤ 0.01 px | **S-CMROT**, rotation sign flipped: the rotated cases; rotation 0 stays green |
| **CM3** | Registration with planes: a 10,000-point sweep against `radarproj::Project` | \|dx\|, \|dy\| ≤ 1 px everywhere | **S-CMCEN**, centred on the package origin: the offset cases; the zero-offset control stays green |
| **CM4** | S1 on Bend, centred on its origin (counts from an independent Python reading, done before any C++) | 5 mi 0/67 and 10 mi 0/74: secondary draws. 25 mi 4/173, 50 mi 4/308, 60 mi 4/324: secondary doesn't (primary/secondary in view) | **S-CMS1**, "in view" becomes "in the package": no road at 5 and 10 mi, those two only |
| **CM5** | S2, rounded `floor(v + 0.5)` | Shades 0-255 give 21 8-bit and 9 RGB565 colours; 0 → (0,0,0), 128 → (6,8,10), 180 → (10,12,14), 255 → (16,18,20) | **S-CMGAIN**, gain 0.75: {CM5} |
| **CM6** | Draw order on `syn-order.bmpk` | Terrain, water, rivers, roads; the last write wins | **S-CMORD**, rivers and roads swapped: {CM6} |
| **CM7** | F4: `syn-min` with R halved and its CRC recomputed in the test | Vertices land at the header's radius | **S-CMR**, uses the requested radius: {CM7} |
| **CM8** | The round screen | 45,244 pixels on the disc; the 12,356 off it stay 0 | **S-CMDISC**, the disc test skipped: {CM8} |
| **CM9** | The theme table | No colour literal outside it; the Dark row equals the plan's §4 | **S-CMLIT**, a planted literal: {CM9} |

### Registered before step M's code (2026-11-07, commit `7f30264c` at 21:25:57Z), for review

(First written here as "~21:40Z", an estimate made before the commit existed. The commits' own times are the record:
items 1-4 at 21:25:57Z, item 5 at 21:32:38Z, and the code was written after both.)

Written after the ruling and before any code. **None of these changes a frozen value.** Each one pins a case list the
frozen text left open, or adds coverage. All four are put to the reviewer with the next report.

**1. A correction to the design: `wxraster::Line` cannot be reused.** It writes only inside the Weatherscope's map
area (`InMap`, rows 24-193 inside the disc), and the radar draws its whole disc. The layer has its own line, with
the same DDA (`n = int(max(|dx|, |dy|)) + 1`, each point `floor(v + 0.5)`) clipped to the screen and the disc.
`wxbase::FillRing` and `wxraster::Text` are still reused by include. FillRing writes an 8-bit image, so the layer is
built as an index image first (land, water, river, road) and coloured in one final pass. Terrain is sampled only
where land shows.

**2. CM2's and CM3's cases, so each plant's set is exact.**
- **CM2:** views {5, 10, 25, 50, 100 mi} × rotations {0°, 30°, 90°, 211°} × home offsets {none, (+0.0049°,
  −0.0049°), (−0.0049°, +0.0031°)} from the package origin: 60 cases.
- **CM3:** the 20 zero-offset cases (every view and rotation) as the control. Then **16 offset cases**: the two offsets
  at 5 and 10 mi, every rotation.
  - **Why only 5 and 10 mi:** there the offset is over 2.5 px in at least one screen axis at every rotation. So
    S-CMCEN's error exceeds 1 px at every point, and the plant is certain to turn each case red.
  - **At 25 mi and wider** the offset is under 2.5 px, so whether a 1 px threshold sees it depends on where the points
    fall. Those views' offset cases sit in CM2, at its 0.01 px threshold.
- Each case is 10,000 points inside the disc. A point outside the package square is skipped.
- **The plant sets, read exactly:**
  - **S-CMROT:** CM2's 45 rotated cases and CM3's 27 rotated cases: 72 in all. CM3 is graded by the same vertex path,
    so a rotation fault there must show too.
  - **S-CMCEN:** CM2's 40 offset cases and CM3's 16: 56 in all.
  - Terrain sampling has its own path (CM10), so neither plant reaches it.

**3. A new row, CM10: terrain sampling and S2's level.** No frozen row graded either.
- **What it checks**, on Bend at views {5, 10, 25, 50, 60, 100 mi}, each over CM2's 4 rotations and 3 offsets:
  - **(a) The level chosen:**
    - the smallest whose half-width covers the view plus the home-to-origin distance (Q-MC7 (a));
    - when none covers, the largest. Its outside takes land.
  - **(b) Each on-disc pixel's level pixel,** against an independent double transcription of §9 with the radar's
    rotation undone.
- **Predicted:**
  - **(a):** 24,743,664 mm at 5 and 10 mi; 49,487,328 at 25 mi; 98,974,656 at 50, 60 and 100 mi. At 100 mi none
    covers, so it falls back to the largest.
  - **(b):** 0 mismatches, outside a band of 1e-6 level pixels about each boundary. The band's count and 100 mi's
    off-level count are printed, not graded.
- **Plants:**
  - **S-CMLVL,** always the largest level: {CM10 5, 10 and 25 mi}.
  - **S-CMSAMP,** sampling the pixel's corner, not its centre: all six CM10 rows.

**4. The case count is a rig check.**
- The test prints 140 cases: CM1 29, CM2 60, CM3 36, CM4 5, CM5 1, CM6 1, CM7 1, CM8 1, CM10 6.
- CM9 is the Python check's one row.
- Any other count is the rig, exit 2.

**5. CM7's view, registered before its first compile or run: the asked radius / 16 (about 6 km).**
- **Why not the asked 97 km:** there, syn-min's ring (±1,000 units) fills about 13 pixels, too few for a 2 %
  comparison.
- **At about 6 km:** it fills about 3,500 pixels at the header's radius, and about four times that under S-CMR.
- **The comparison:** the layer's water must match a reference fill from the header's radius, through the same
  centre rule, within 2 %.

### The first host run (2026-11-07, 21:33-21:35Z), as run: CM7 OFF, stopped for a ruling

- **The code as run:** `include/MapLayer.h` and `test/host/test_map_layer.cpp`, blobs `c4b4a7f939bb` and `4dcae8df50a6`
  (a local commit's hash moves on every rebase; the blobs do not). It is not pushed: the first firmware push waits for
  #609.
- **Both compilers read the same:** MinGW 5.1 and WSL `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`. 140 cases, 139
  pass, **CM7 red**.
- **Not run, per the stop:** the in-tree re-run, CM9's check and the sabotage driver.

| row | read | verdict |
|---|---|---|
| **CM1** | 27 refused, each with its reason, 0 pixels written; `bend` and `syn-min` built | as predicted |
| **CM2** | 60 cases; the worst error 0.000127 px (≤ 0.01) | as predicted |
| **CM3** | 36 cases of 10,000 points; the worst \|d\| is 1 px in every case | as predicted |
| **CM4** | 0/67, 0/74, 4/173, 4/308, 4/324; secondary drawn at 5 and 10 mi only | as predicted |
| **CM5** | 21 and 9 colours; the four samples exact | as predicted |
| **CM6** | 0 pixels off S3's order. Overlaps: road/river 1, road/water 40, river/water 40 | as predicted |
| **CM7** | header 48,500,000 mm, asked 97,000,000 mm; the layer's water 3,249 px against the reference's 3,364 (3,249 shared): −3.4 %, outside 2 % | **OFF** |
| **CM8** | 45,244 on the disc, 12,356 off it, 0 off-disc pixels lit | as predicted |
| **CM10** | every one of the 72 cases chose the predicted level; 0 mismatches. Printed: the band 0 / 35,430 / 1 / 1 / 1 / 0, off-level 279,638 at 100 mi (0 elsewhere) | as predicted |

**CM7's cause is the check, not the layer.**
- **The 115 px are one 58 px row and one 57 px column:** syn-min's river runs across the ring and its road down it.
  S3's order draws both over the water, and the check counted only water.
- **The alternative, named and excluded:** float against double moving the ring's edges. Every water pixel the layer
  drew lies inside the reference (3,249 of 3,249 shared), and the gap is two whole lines through the middle, not edge
  pixels around the ring.
- **The other alternative:** a scale fault (what S-CMR plants). That would give about four times the area, or a
  quarter of it, not 96.6 %.
- **A diagnostic, disclosed:** a copy of the test outside the tree, with CM7 drawing water only, read 3,364 px
  against 3,364, all shared, and 140 of 140. Nothing in the tree changed, and nothing was re-run in it.

**The proposed fix, for the ruling.**
- CM7 builds with terrain and water only: the river and the road are not what CM7 grades.
- **The new predictions:**
  - CM7: water 3,364 px against the reference's 3,364. This number is known from the diagnostic, so it is not a
    blind prediction.
  - Then 140 of 140 on both compilers, CM9 passing, and every plant turning exactly its registered set.

**Observations from the same run, not graded.**
- **S1 at Bend.**
  - At 25, 50 and 60 mi, Bend's 4 primaries count as in view, but each crosses only 5-11 px of the disc. They are
    two pairs of near-identical carriageways.
  - So S1 hides the secondaries, about 2,300-3,400 px of road (measured independently in Python).
  - Bend therefore shows 14, 6 and 7 road pixels at those views.
  - That is the approved rule working as written; whether a town like Bend should look like that is Daniel's call on
    glass.
- **Exact ties at 10 mi.**
  - With Bend's levels, the 10 mi view's pixel is exactly 2/3 of a level pixel (134.112 m against 201.168 m), so many
    pixel centres land exactly on level boundaries.
  - 35,430 of the 542,928 pixels checked fell in CM10's 1e-6 band and were not graded.
  - At a tie, the fixed-point floor and the double one can pick either side: a one-level-pixel shift.

### The ruling on the first run, verbatim (reviewer chat, pasted by Daniel, 2026-11-07 ~23:23Z)

> For: the map client, from the reviewer chat, 2026-11-07 ~23:23Z
>
> Q-MC9: yes. CM7 builds terrain and water only, as a check of F4 against the independent reference, with S-CMR still
> turning it red. The 3,364 is disclosed as known from the diagnostic; the blind part is everything else (140 of 140 on
> both compilers, CM9, every plant exact). Registered items 1-5: accepted. For item 1, add a row that the layer's line
> equals wxraster::Line pixel for pixel inside rows 24-193, with its plant, so the copy can't drift.
> Q-MC10: hold S1 at Bend for Daniel's look in step M's window. No minimum length now.
> #613: approved at fb4ca258 once its CI double-reads 32 = 29/3/0. Merge under Q-M1.
> The map server needs your full /basemap query before it can freeze the r=161 re-read or build the fixtures. Send it
> now, as the firmware will send it for the default view: r, ε, the layer mask (no peaks), and the terrain levels
> (radius:N per level), with how the levels cover the zoom ladder at r=161.

### Registered for the second run, before its code (2026-11-07, after the ~23:23Z ruling)

**1. CM7, as ruled:** it builds terrain and water only. Predicted: 3,364 px against the reference's 3,364. **This number
is known from the disclosed diagnostic, so it is not blind.** S-CMR still turns exactly {CM7}.

**2. A new row, CM11: the layer's line equals `wxraster::Line`, pixel for pixel.**
- **Where they are compared:** rows 24-193, on pixels both disc tests count as inside (the layer's pixel-centre test,
  `wxlayer::OffDisc`, and the Weatherscope's `InMap`).
- **The cases:** every river and road segment of Bend, through the layer's own vertex path, at CM2's 5 views × 4
  rotations with no offset: 20 cases.
- **Predicted:** 0 differing pixels in every case. The control is a lit-pixel count above 0 in the compared area, or
  the row could not fail.
- **The plant, S-CMLINE:** the layer's line rounds with `floor(v)` instead of `floor(v + 0.5)`. It turns exactly
  {CM11's 20}.
- **What the row requires of the code, so it is said here:**
  - the layer's line moves from float to the same double DDA as `wxraster::Line`;
  - the clip to the screen stays, as a range of the same steps `k`, so it skips points and never moves one.
  - No other row's prediction depends on the line's arithmetic: CM6 compares the layer with itself, and CM4 grades
    counts and decisions.
  - The cost goes to CF5's reading.

**2b. A second plant for CM9, S-CMPLAN** (registered before the driver existed, after the second run's 160 of 160).
- **Why:** S-CMLIT proves CM9's literal scan can fail. Nothing proved its other half could: the Dark row against the
  plan's §4.
- **The plant:** the table's place colour becomes (86, 86, 94); its RGB565 differs from (86, 86, 86).
- **Expected:** exactly {CM9}. The layer does not draw place names yet, so no C++ row sees it.

**3. The count becomes 160:** CM1 29, CM2 60, CM3 36, CM4 5, CM5 1, CM6 1, CM7 1, CM8 1, CM10 6, CM11 20. Any other
count is the rig. **So the blind part reads 160 of 160 on both compilers** (the ruling's "140 of 140" plus CM11's 20),
then CM9, then every plant exactly its set.

### The second host run (2026-11-07, ~23:31-23:38Z), as registered: as predicted

- **The suite:** 160 of 160 on MinGW 5.1 and on WSL g++ 13.3.0.
  - The first WSL attempt read nothing, because its output directory under WSL's `/tmp` was gone. That is the rig,
    not a reading. The directory was made, and the same command was run once more.
- **CM7:** 3,364 px against the reference's 3,364, all shared. Known from the diagnostic, as disclosed.
- **CM11:** 0 differing pixels in all 20 cases. 739-1,783 pixels lit in the compared area, so the row could fail.
- **CM9:** passes. No colour literal outside the table, and the Dark row equals the plan.
- **The sabotage driver (`test/host/sabotage_map_layer.py`):** 13 of 13 plants turned exactly their registered sets, on
  both compilers: S-CMCRC 1, S-CMROT 72, S-CMCEN 56, S-CMS1 2, S-CMGAIN 1, S-CMORD 1, S-CMR 1, S-CMDISC 1, S-CMLIT 1,
  S-CMPLAN 1, S-CMLVL 3, S-CMSAMP 6, S-CMLINE 20. The real header was unchanged.
- **The harness's own passes:** `--anchors` 13 of 13 present exactly once; `--control` 2 suites as its table;
  `lint_driver_counts.py` 0 flagged.
- **Not yet run:** the whole of `test/host/run.sh` (every block and every driver). It runs before the first firmware
  push, which waits for #609.
- **Step M is not finished:** its bench rows (CF1-CF6) wait for E2:08 after blipscope-5c's v19 window, and the KSFO
  reading waits for the map server's fixtures.

### The full WSL `run.sh` before the push (2026-11-07, 23:43-23:49Z): OFF, stopped for a ruling

- **Frozen at 23:42:41Z, before the run,** on the tree rebased onto `35a4909b`: exit 2; failed blocks exactly "the same
  TU, for the board" and "Weatherscope R0 and R0b"; `sabotage_wx_towns.py` skipped (34 drivers: 33 run, 1 skipped);
  MapLayer 160 of 160 and CM9; the new driver ok in `--control` with 13 AS PREDICTED lines in the full pass; every
  other block ok.
- **Read:** exit 2 with **seven** failed blocks: the two predicted and five more. The driver count is as predicted, and
  MapLayer reads 160 of 160 with CM9. The new driver is ok in both passes, **with 14 AS PREDICTED lines, not 13.**

**The five extra blocks: one cause, this worktree's missing libraries.**
- The worktree is new and `pio` has never run in it, so it has no `.pio/libdeps` (`ls` confirms).
- Every extra block went BLIND for that reason and says so in its own words:
  - the Wi-Fi QR test, inside the usage-report block: "fonts MISSING (no LovyanGFX under .pio/libdeps)";
  - GameConfig: "no ArduinoJson under .pio/libdeps";
  - seven-segment: "no GLCD font table … run `pio run` once";
  - both driver passes, through `sabotage_wx_fb_check.py`: "no LovyanGFX glcdfont.h under .pio/libdeps".
- **The alternative, named and excluded:** a defect in main (#609 landed in this rebase) or in this change.
  - The blocks read nothing; BLIND is a refusal to read, and it names the path it lacks.
  - None of them reads `MapLayer.h` or the new files.
  - The MapLayer block and the new driver passed in the same run.

**The 14: my miss.** The harness's `grep -c 'AS PREDICTED'` also counts the driver's summary line, "MAP LAYER SABOTAGES AS
PREDICTED". The 13 plant lines are 13 of 13.

**Proposed, for the ruling.**
- Install this worktree's libraries with `pio pkg install -e blipscope-s3-128`, from PowerShell, never Git Bash (the
  MSYS esptool trap). It installs libraries only, with no build.
- Then the same WSL `run.sh` once more. **Predicted:**
  - exit 2, with failed blocks exactly the two known;
  - 34 drivers: 33 run, 1 skipped;
  - MapLayer 160 of 160 and CM9;
  - the new driver with **14** AS PREDICTED lines (13 plants and the summary);
  - every other block ok.

**The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-08 ~02:33Z):**

> Q-MC11: yes. Install the worktree's libraries with pio pkg install -e blipscope-s3-128, from PowerShell, libraries
> only and no build. Then re-run the WSL run.sh once against your predictions (exit 2 from exactly the two known
> blocks; 34 drivers, 33 run and 1 skipped; MapLayer 160 of 160 with CM9; 14 AS PREDICTED lines, 13 plants plus the
> summary). The five BLIND blocks and the 14 stay on record as your misses. If it reads as predicted, step M's host PR
> goes up with CI predicted at 34 = 30/4/0.
> Your query has been passed to the map server.

**The re-run (2026-11-08, frozen 02:39:24Z, run 02:39-02:46Z), after `pio pkg install -e blipscope-s3-128` (libraries
only, 136 MB, no tracked file changed): as predicted on every line.**
- exit 2, with failed blocks exactly "the same TU, for the board" and "Weatherscope R0 and R0b";
- 34 drivers: 33 run, 1 skipped, in both driver passes;
- MapLayer: 160 of 160, and CM9;
- `sabotage_map_layer.py`: anchors 13 of 13, the control as its table, and 14 AS PREDICTED lines;
- every other block ok. The log's three "BLIND" lines are `ok` lines whose wanted verdict is BLIND.

The first run's five BLIND blocks and the 14 stay on record above as my misses (ruled ~02:33Z).

### The /basemap query for the default view (sent to the map server, 2026-11-07)

The radar's default is 100 mi, so its zoom ladder is {5, 10, 25, 50, 100} mi. The request:

```
GET /api/v1/blipscope/basemap?lat=<2 dp>&lon=<2 dp>&r=162&eps=201168&layers=water,rivers,roads,places,terrain&levels=41239440:246,81808320:244,162275520:242
```

| parameter | value | how it is made |
|---|---|---|
| `lat`, `lon` | 2 dp | `wireloc::Center`, as `/wx` sends them |
| `r` | **162** | `ceil(view radius + 0.8 km)`, capped at 222: 160.934 + 0.8 = 161.73. **162, not 161:** 161 would leave the home's offset from the 2-dp origin (up to 0.555 km on an axis) outside the package's square |
| `eps` | **201168** (mm) | the view radius / 800: half a pixel at 30 % of the configured radius (here 30 mi, 402.336 m a pixel). It keeps a package's density per pixel the same at any configured radius |
| `layers` | `water,rivers,roads,places,terrain` | no `peaks` (Q-MC7 (b)); places stay in, since place names are on by default |
| `levels` | `41239440:246,81808320:244,162275520:242` | one per top three ladder steps (25, 50 and 100 mi), each 1:1 with the screen (pixel = step / 120), and wider by `ceil(0.8 km / pixel)` pixels a side, so the worst offset (0.785 km) is always inside |

**How the levels cover the ladder.** The level used is the smallest whose half-width covers the view plus the
offset (Q-MC7 (a)).

| view | radius + 0.785 km | level used | level pixel | screen pixel | screen px per level px |
|---|---|---|---|---|---|
| 5 mi | 8.83 km | 41.24 km (the 25 mi level) | 335.28 m | 67.06 m | 5 |
| 10 mi | 16.88 km | 41.24 km (the 25 mi level) | 335.28 m | 134.11 m | 2.5 |
| 25 mi | 41.02 km | 41.24 km | 335.28 m | 335.28 m | 1 |
| 50 mi | 81.25 km | 81.81 km | 670.56 m | 670.56 m | 1 |
| 100 mi | 161.72 km | 162.28 km | 1,341.12 m | 1,341.12 m | 1 |

- **The bytes:** terrain 178,640 (8 + N² per level). So a package is at most 32,768 + 178,640 = **211,408 B**, and that
  is the client's `GetBytes` cap for this view.
- **Why 5 and 10 mi get no level of their own:** they are brief zooms that return to 100 mi after 10 minutes idle,
  and z9's 4 × 4 mean already smooths the terrain over about 0.9 km. A fourth level (10 mi: N = 252, 63,512 B) is
  possible within the route's limit of 4, and is not asked for.
- **Other configured radii** follow the same three rules over their own ladder. The exact rounding is registered with
  step A.

### Bench rows

Derived from #445's F run in `docs/wx-device.md`: fill 3.01 ms, `memcpy` 5.08 ms, opaque sweep 2.04 ms, blended sweep
7.68 ms.

| row | reading | predicted |
|---|---|---|
| **CF1** | Weather off, the clear | Map off 3.01 ± 0.3 ms; map on 5.08 ± 0.3 ms |
| **CF2** | Weather off, the sweep with the map on | 7.7-8.9 ms (off 2.04 ± 0.3) |
| **CF3** | Weather off, frame p95, on − off | +6.2 to +10.5 ms |
| **CF4** | **Weather on, look A (the shipping case), frame p95, on − off** | **−1.0 to +2.2 ms**, the clear unchanged |
| **CF5** | The rebuild, Bend, printed per pass | 30-70 ms. An estimate: `FillRing` runs in double, and the S3 has no double FPU |
| **CF6** | B1′ | 0 `BUDGET BROKEN` in every arm |

**How it is measured.**
- **The image:** a bench-only env extending `-wxlook`, with `bend.bmpk` embedded and no fetch. It carries its own
  bench marker, added to `scripts/check-no-bench-hooks.sh` (ruled).
- **The method:** map off and on alternate every 30 s health tick, so pairs bracket the drift. 20 pairs with weather
  off and 20 with weather on, about 1 h on E2:08, read from `[health]`, `clear=` and `sweepparts=`.
- **The window:** after blipscope-5c's v19 window closes, reserved and announced through Daniel (ruled).
- **The limit:** Bend is rural, 5,460 vertices against about 8,660 at the 32 KiB cap. The KSFO reading comes before
  the layer design counts as settled (ruled).

**The verdicts.**
- **The rebuild:** ≤ 40 ms is done. 40-100 ms stays, as one hitch per zoom step (the bar, ruled). Over 100 ms: stop
  and propose splitting across frames or a float fill.
- **A frame outside its band, or any `BUDGET BROKEN`:** stop and report.
- **Parts more than 10 % off the total, or map-off ticks drifting over 1 ms:** "instrument", stop.
- **Anything else:** the instrument is unreliable at this scale; stop and decide.

## Step A: fetching the package (predictions, for review; 2026-11-08)

**Status: ruled 2026-11-08 ~20:28Z (verbatim below); merged as #624. The code is on `feat/map-client-step-a`, after the registration below.** Step M's host side merged as #619 (`e934e93b`). The
fixtures (KJAC, KSFO) came in the map server's #621, and #620 read r = 162 under the cap everywhere but New York.
Neither changes a row here.

### What step A builds

| part | where | what |
|---|---|---|
| **the request** | `include/MapQuery.h`, pure | the query for the configured radius, built from the **stored config** (distance x the unit's factor, in double), never from the view's derived radii. `units::FromKm` is float, so the radius the radar shows can read 99.99999 mi; the request must be exact to the mm |
| **the policy** | `include/MapPolicy.h`, pure | when to ask, what each answer means, and when to ask again (F1, F2, F5-F8) |
| **the transport** | `HttpRequestManager::GetBytes` | collects `Retry-After` beside `ETag` and `X-Wx-Max-Age`, into a new `HttpResult::retryAfter`. Nothing else in `GetBytes` changes, and the weather path is untouched |
| **the wiring** | `AircraftManager`, under `WX_LAYER_SHIPS` | a `FetchKind::Basemap` on the fetch task, like the weather crop. One fetch in flight; `maplayer::Load` (the CRC and §8) runs on the fetch task; a sequence number drops any answer to an older config. The landing buffer lives in PSRAM and is allocated at the first fetch |

### The request (CA1-CA3)

- `lat`, `lon`: `wireloc::Center`, 2 dp, as `/wx` sends them.
- `r = min(222, ceil(Rv + 0.8 km))`.
- `eps = max(1000, floor(Rv_mm / 800 + 0.5))` mm.
- `layers = water,rivers,roads,places,terrain`, with no peaks.
- **One level per top-three ladder step**, each 1:1 with the screen:
  - `px = step / 120`, `m = ceil(0.8 km / px)`, `N = 240 + 2m`, `Rl = floor(N px / 2 + 0.5)`;
  - a step whose `N` would exceed 1,024, or whose `Rl` falls outside the route's 1-250 km, gets no level.
- **The cap:** `GetBytes` takes 32,768 + Σ(8 + N²).
- No `If-None-Match` is ever sent: the package is held in RAM only, so there is no ETag to revalidate.

| row | input | predicted (computed by an independent scratch reading, before any C++) |
|---|---|---|
| **CA1** | 100 mi, the default | `r=162&eps=201168&layers=water,rivers,roads,places,terrain&levels=41239440:246,81808320:244,162275520:242`, cap 211,408. The string already sent to the map server |
| **CA1b** | 40 km | `r=41&eps=50000&…&levels=10833333:260,25833333:248,41000000:246`, cap 222,412 |
| **CA1c** | 25 mi | `r=42&eps=50292&…&levels=8851392:264,16898112:252,41239440:246`, cap 226,508 |
| **CA1d** | 5 mi (a one-step ladder) | `r=9&eps=10058&…&levels=8851392:264`, cap 102,472 |
| **CA1e** | 222 km, the cap | `r=222&eps=277500&…&levels=25833333:248,50833333:244,223850000:242`, cap 212,396. Here `ceil(Rv + 0.8)` is 223, so the cap leaves up to 0.555 km of the view's edge outside the square: under 1 px at 1.85 km a pixel |
| **CA1f** | 0.1 mi, the form's minimum | `r=1&eps=1000&layers=water,rivers,roads,places`: **no levels** (every step's N exceeds 1,024), so `terrain` leaves `layers` and no `levels` is sent. The route refuses `terrain` without `levels` (`bad_levels`). Cap 32,768 |
| **CA2** | every configured radius 0.1-222 by 0.1, in km, mi and nmi | every request lies within the route's bounds, **read from `proxy/src/basemap/query.ts`** (`MAX_R_KM`, `EPS_MIN_MM`/`EPS_MAX_MM`, `LEVEL_R_MIN_MM`/`LEVEL_R_MAX_MM`, `LEVEL_N_MAX`, `LEVELS_MAX`, `TERRAIN_BYTES_MAX`); levels unique; `r` covers the view plus 0.555 km on each axis, except where 222 caps it; and for every ladder step the smallest covering level covers the step plus 0.785 km, or the step has none (counted and printed) |
| **CA3** | the cap | 32,768 is `CAP_BYTES`, read from `proxy/src/basemap/budget.ts` |

**Plants, each set computed by the same scratch reading under the plant:**
- **S-CA-MARGIN** (`r` without the 0.8 km): {CA1, CA1b, CA1c, CA2}. 5 mi, 222 km and 0.1 mi round to the same `r` either way.
- **S-CA-EPSFLOOR** (no 1,000 mm floor): {CA1f, CA2}.
- **S-CA-FLOAT** (each radius through a float before its mm): {CA1, CA1c}. 40 km, 5 mi and 222 km survive a float exactly.

### The policy (CA4-CA11)

| row | the answer | predicted behaviour | plant (turns exactly) |
|---|---|---|---|
| **CA4** | 202 with `Retry-After: 15` | the next request at exactly +15 s: not at once, not on a fixed timer (F2) | S-CA-RA, a fixed 30 s: {CA4} |
| **CA5** | 202 with `Retry-After` absent, not a whole number, or outside 1-3,600 | **refused as a value**, never clamped: the next request at +60 s, and the log says the header was refused | S-CA-RAZERO, garbage read as 0: {CA5} |
| **CA6** | 404, or 422 | the no-package screen (F5), with the reason logged. **No request again until the location or radius changes,** then one new attempt | S-CA-404RETRY, 404 read as an error: {CA6} |
| **CA7** | 500, 503, 429 without `Retry-After`, a transport error, a timeout | backoff 60, 120, 240 … s, capped at 3,600, reset by a 200. A 429 with `Retry-After` follows it as CA4 | S-CA-FLAT, no growth: {CA7} |
| **CA7b** | 401, 403 | 3,600 s: a credential, not a build, so retrying sooner proves nothing | in CA7's plant set |
| **CA8** | 200 that the gate refuses | counted as an error (CA7's backoff), **at most 3 per config, then no package**, with the gate's reason logged. A truncated transfer recovers; a format the device cannot read stops asking | S-CA-GATE, refusals without limit: {CA8} |
| **CA9** | F6, the boot | a spread boot waits **D = hash(device id) mod 7,200 s** before its first request; any other boot asks at once. Over 10,000 synthetic ids: D in [0, 7,200), the mean 3,600 ± 72, each of 12 ten-minute bins within ±15 % of 833 | S-CA-NOSPREAD {CA9 OTA, CA9 nightly}; S-CA-SPREADALL {CA9 power-on, CA9 first-run, CA9 config} |
| **CA10** | a change of location or radius | a new request **at once**, with no spread; the old package is dropped at once, since it belongs to another view. A zoom changes nothing | S-CA-ZOOMFETCH: {CA10} |
| **CA11** | an answer whose sequence number is older than the config | dropped | S-CA-NOSEQ: {CA11} |

**Which boots spread (F6), and how they are known.** The firmware already records both:
- **After the nightly reboot:** `ConsumeDeferredRebootCause()` (idempotent within a boot) reads `REBOOT_CAUSE_OTA_CHECK`.
- **After an update:** `OtaUpdater`'s attempt record reads `res == "incomplete" && to == FW_VERSION` on the new image's first
  boot. **But the check-in report finalises that record and clears it** (fire-once), so the map has to read it before then.
  - **Proposed:** one accessor in `OtaUpdater`, computed once early in `setup()`.
  - **That is blipscope-5c's file** (Q-MA1).
- **A power-on, a first-run restart and a config save carry neither,** so they ask at once.
- **The hash:** FNV-1a over a domain-separated string (`"map-spread|"` + `DeviceIdentity::LeaderboardId()`). It is never the
  MAC, and nothing prints the id, the hash or D.

### Checks that read the source (CA12-CA14)

| row | check | plant |
|---|---|---|
| **CA12** | F8: no `Preferences`, `put*`, SPIFFS or LittleFS call in the map's files | S-CA-NVS, a planted `putString`: {CA12} |
| **CA13** | privacy: the map's `printf` formats print no coordinate, no id, no hash and no delay; the URL takes lat/lon through `wireloc::Center` only | S-CA-PRINTD, printing D: {CA13} |
| **CA14** | `GetBytes` collects `Retry-After`, and the weather path's collected headers are unchanged | S-CA-NORA, the header dropped from the list: {CA14} |

### Bench, later (with step M's window or after D4)

Production has no `/basemap` until D4. The bench reads staging's route from a staging-built image:
- a cold first fetch: 202, then 200 after the `Retry-After`;
- size within the cap, and the gate passes;
- a power-on boot asks at once.

These rows are registered with the bench image, not here.

### Questions for the ruling

- **Q-MA1:** may `OtaUpdater` gain the one accessor (read once early in boot, before the check-in clears the record)?
  It is blipscope-5c's file.
- **Q-MA2:** should a boot after the network watchdog's reboot (`REBOOT_CAUSE_NET_WEDGE`) spread too?
  - **I recommend yes:** a server outage fires every board's watchdog together, and they all come back together.
  - A touch-watchdog reboot stays immediate: it is a local recovery.
- **Q-MA3:** a consequence of RAM only plus F6. **Any update, whenever it lands, leaves the map off for up to 2 h**
  afterwards.
  - Overnight that is unseen. A daytime update is visible.
  - **I recommend accepting it,** rather than narrowing the spread by the hour.
- **Q-MA4:** the numbers in CA5, CA7, CA7b and CA8 (60 s fallback, 60-3,600 s backoff, 3,600 s for credentials, 3 gate
  refusals). Proposed as stated; none is ruled yet.
- **Q-MA5:** until D4, production answers `503 not_configured`. Under CA7 a shipped device backs off to an hourly ask. That
  is only reachable if the joint release ships before D4, which the release plan does not allow.

### The ruling on step A's predictions, verbatim (reviewer chat, pasted by Daniel, 2026-11-08 ~20:28Z)

> #624: approved at 70e1bbd397fa once its CI double-reads 32 = 29/3/0. Merge through scripts/merge-by-rule.sh. Step A's code may start once it has merged.
> Q-MA1: yes, one read-only accessor in OtaUpdater.cpp. blipscope-5c reads that diff before your push, as the file's owner.
> Q-MA2: yes. A boot after the network watchdog's reboot also takes the delay, since an outage restarts the fleet together.
> Q-MA3: accepted: after an update the map can be off for up to 2 h. If Daniel sees it in daytime and dislikes it, persistence comes as its own proposal.
> Q-MA4: yes, as proposed: a 60 s fallback when Retry-After is missing, backoff from 60 to 3,600 s, 1 h after a 401 or 403, and at most 3 gate refusals per config.
> Q-MA5: accepted. The release waits for D4, as #606 orders, so a 503 not_configured only ever reaches bench and staging, and it backs off like any 503.

#624 merged on its fourth attempt (2026-11-11 01:33:12Z, squash `947e1d3825fc`). The three attempts before it each
stopped on "main moved", with nothing merged. The ruling on those, verbatim (reviewer chat, pasted by Daniel, 2026-11-11 ~01:12Z):

> Q-MC12:
> (a) Yes, make a fourth attempt once #631's merge is on main; you're second in the order. #624 is still 70e1bbd397fa, docs only, and merges cleanly onto main 81cfbd0.
> (b) Good proposal. It goes to blipscope-5c, as the owner of the claims design, after v19's cut, with predictions and a plant where the holder dies mid-wait. The declared order covers it until then.
>
> All three stops were correct: nothing merged, and the claim was released each time. Step A's code still waits for #624, and the disk rule stands.

### Registered before step A's code (2026-11-11), for review

Everything here was computed or decided before any of step A's C++ existed. The request's numbers come from a scratch
reference in Python, with float32 emulated through `struct`. It reproduces all six frozen CA1 strings and caps exactly.

**1. The ladder comes from the radar; the values come from the stored config.**
- **The disagreement.** A ladder re-derived in double disagrees with the radar's own at exactly one of CA2's 6,660
  configs. The radar's ladder is `radarzoom::BuildLadder` of the float display radius. At 25.5 nmi that float lands
  just above 25.5, so the radar shows a 25 nmi step that a double ladder omits.
- **What `mapquery::Build` does instead.** It takes the radar's `radarzoom::Ladder` and uses only its step count. The
  steps are `CANDIDATES[0 .. n-2]` and the configured radius. Each is turned into mm in double, from the stored
  distance and the unit's exact factor, never from the ladder's floats.
- **The result.** With the radar's ladder, every radar step among the top three has a level of its own at all 6,660
  configs. With the double ladder, one does not. CA1's strings are the same either way.

**2. CA2, as three rows.** Each runs over the sweep: 0.1 to 222 by 0.1, in km, mi and nmi, which is 6,660 configs.
The route's bounds are read from `proxy/src/basemap/query.ts` and the cap from `budget.ts`. Numeric separators are
allowed; anything the parser cannot read is the rig (exit 2).
- **CA2-bounds:**
  - `1 <= r <= MAX_R_KM`;
  - `EPS_MIN_MM <= eps <= EPS_MAX_MM`;
  - at most `LEVELS_MAX` levels, unique, each with `LEVEL_R_MIN_MM <= R <= LEVEL_R_MAX_MM` and `1 <= N <= LEVEL_N_MAX`;
  - `Σ(8 + N²) <= TERRAIN_BYTES_MAX`;
  - `terrain` is in `layers` exactly when there are levels.
- **CA2-rcover:** `r >= Rv + 0.555 km`, unless `r = 222`.
- **CA2-levels:** each of the radar's top three steps (its float step, in km in double) has a level of its own, whose
  pixel is within 1 % of step/120, and that level has `R >= step + 0.785 km`. The exception is a step whose own `N`
  would exceed 1,024, or whose `R` would fall outside 1-250 km. Printed, and predicted exactly:
  - 6,660 configs;
  - 19,496 steps with a level of their own;
  - 4 steps with none;
  - 11,041 steps below the top three.

**3. The policy's rules where the frozen rows left a choice** (`include/MapPolicy.h`):
- **Clocks.**
  - Every wait runs from the moment that set it. A `Retry-After` or a backoff runs from the reply's arrival (the
    `nowMs` given to `OnResponse`). The spread runs from the boot's first `AircraftManager::Initialise()`.
  - Every comparison is the wrap-safe signed difference `WxPolicy` uses (WD5 addendum 7), so a time later than the
    pass's `now` is not due.
- **`Retry-After` (CA4, CA5).** Accepted as 1-4 ASCII digits whose value is 1-3,600, so `"0015"` is 15. Everything else
  is refused: absent, empty, `0`, `3601`, a sign, a space, a decimal point, five digits, an HTTP date.
- **Statuses the table does not name.**
  - 400 is the no-package state, like 404 and 422: the same query would be refused again.
  - A 429 with a refused `Retry-After` is treated as a 429 without one (CA7's backoff).
  - Any other status is an error (CA7): a 3xx, any other 4xx, and a 304, which no request invites.
  - A 202 neither grows nor resets the backoff.
  - 401 and 403 wait 3,600 s and leave the backoff's level where it was.
- **The backoff's level.**
  - It persists across config changes, and only a 200 whose package passes the gate resets it (CA7's "reset by a 200").
  - That reset can only be observed across a config change, because F8 asks for nothing more once a package is held.
- **The gate (CA8).**
  - Every 200 that does not yield a package `maplayer::Load` accepts counts as a refusal. That covers a CRC or §8
    refusal, and also a body over the cap, a short body and a stalled one.
  - Each refusal counts once toward the 3. The third is the no-package state, with no backoff wait.
- **A reply that never comes** ends after 60 s (`kReplyTimeoutMs`) as a failed attempt (CA7), and its late reply is
  ignored. That is longer than `WxPolicy`'s 30 s, because the package can approach 1 MB at the smallest radii.
- **Config changes.**
  - The boot's first query is not a change, so the spread, if any, still holds.
  - The key is lat and lon at 2 dp, plus the whole parameter string. Every later change of the key drops the package,
    clears the stop and the gate count, and cancels any wait, so the next request goes at once (CA10).
- **The spread (CA9).** Three kinds of boot spread:
  - deferred cause `OTA_CHECK`, the nightly reboot;
  - deferred cause `NET_WEDGE` (Q-MA2);
  - the new image's first boot after an update.

  `TOUCH_WEDGE` asks at once, and so does a boot with no cause and no update record. A power-on, a first-run restart
  and a config-save boot are the same input to the device, so their three rows share one input.
  `D = FNV-1a("map-spread|" + LeaderboardId) mod 7,200` s.

**4. The cases.** `test/host/test_map_fetch.cpp` has 42 cases; `test/host/check_map_fetch.py` holds CA12-CA14.

| row | cases |
|---|---|
| request | CA1, CA1b, CA1c, CA1d, CA1e, CA1f, CA2-bounds, CA2-rcover, CA2-levels, CA3 |
| CA4 | CA4-202 and CA4-429 (`"15"`: not due at +14,999 ms, due at +15,000); CA4-edges (`"1"`, `"3600"`, `"0015"`) |
| CA5 | CA5-absent; CA5-values (the ten refused values above, each waiting 60 s and flagged as refused) |
| CA6 | CA6-404, CA6-422, CA6-400: nothing asked at +1 min, +1 h or +24 h; a radius change asks once, at once; the same answer stops it again |
| CA7 | CA7-500, CA7-503, CA7-429, CA7-transport: two in a row, waiting 60 s then 120 s |
| | CA7-timeout: two timeouts, 60 s then 120 s, with the late reply ignored |
| | CA7-cap: eight 500s, waiting 60, 120, 240, 480, 960, 1,920, 3,600 and 3,600 s |
| | CA7-reset: three 500s, an accepted 200, a radius change, then 500s that wait 60 s and then 120 s |
| CA7b | CA7b-401 and CA7b-403: exactly 3,600 s, twice |
| CA8 | CA8-first: one refusal waits 60 s and holds no package |
| | CA8-recover: two refusals, then a pass, and the package is held |
| | CA8-stop: the third refusal stops; a radius change starts the count again |
| | All three advance their clock an hour per step, so they read no backoff length past the first |
| CA9 | CA9-ota, CA9-nightly, CA9-netwedge: not due at D s − 1 ms, due at D s |
| | CA9-poweron, CA9-firstrun, CA9-config, CA9-touchwedge: due at once |
| | CA9-dist: 10,000 synthetic ids give D in [0, 7,200), a mean of 3,600 ± 72, and each of 12 ten-minute bins within ±15 % of 833.3 |
| CA10 | CA10-change: a held package is dropped and the request goes at once, also from inside a spread wait and from inside a backoff wait |
| | CA10-location: the same, for a latitude change |
| | CA10-zoom: every view step gives a byte-identical request and key, and nothing is asked |
| CA11 | CA11: a 200 that passes, but answers the config from before a change, is dropped; the new config asks at once |

The synthetic ids are made at run time with splitmix64, in the id's own shape (16 hex digits). They are never written
as literals and never printed. Neither the test nor the check prints a D, a hash or an id.

**5. The plants: 23, each set computed before the code.** The request plants' sets come from the scratch reference run
under each plant. The policy and source plants' sets come from applying the rules above, row by row.

| plant | the break | turns exactly |
|---|---|---|
| S-CA-MARGIN | `r` without the 0.8 km | CA1, CA1b, CA1c, CA2-rcover (as frozen) |
| S-CA-EPSFLOOR | no 1,000 mm floor | CA1f, CA2-bounds (as frozen) |
| S-CA-FLOAT | each radius through a float before its mm | CA1, CA1c (as frozen) |
| **S-CA-LEVELM** (new) | each level's margin 0.7 km, not 0.8 | CA1b, CA1c, CA1d, CA2-levels |
| **S-CA-CAP** (new) | the cap's base is 32,000 | CA1, CA1b, CA1c, CA1d, CA1e, CA1f, CA3 |
| S-CA-RA | a valid `Retry-After` replaced by a fixed 30 s | CA4-202, CA4-429, CA4-edges |
| S-CA-RAZERO | a refused `Retry-After` read as 0 | CA5-absent, CA5-values, **CA7-429** (a 429's absent header is the same refused value) |
| S-CA-404RETRY | the no-package branch gone, so 404, 422 and 400 read as errors | CA6-404, CA6-422, CA6-400 |
| S-CA-FLAT | every error waits 60 s: no growth, and no hour for 401 and 403 | CA7-500, CA7-503, CA7-429, CA7-transport, CA7-timeout, CA7-cap, CA7-reset, CA7b-401, CA7b-403 |
| **S-CA-NORESET** (new) | an accepted 200 does not reset the backoff | CA7-reset |
| **S-CA-NOTIMEOUT** (new) | a request never times out | CA7-timeout |
| S-CA-GATE | refusals without limit | CA8-stop |
| **S-CA-GATE1** (new) | the first refusal stops | CA8-first, CA8-recover, CA8-stop |
| S-CA-NOSPREAD | no boot spreads | CA9-ota, CA9-nightly, CA9-netwedge |
| S-CA-SPREADALL | every boot spreads | CA9-poweron, CA9-firstrun, CA9-config, CA9-touchwedge |
| **S-CA-DMOD** (new) | D mod 3,600 | CA9-dist |
| S-CA-ZOOMFETCH | levels for the three steps up to the view's step, not the ladder's top three | CA10-zoom |
| **S-CA-CHANGEWAIT** (new) | a change keeps the pending wait | CA10-change, CA10-location |
| **S-CA-KEEPPKG** (new) | a change keeps the held package | CA10-change, CA10-location, CA7-reset |
| S-CA-NOSEQ | no config sequence check | CA11 |
| S-CA-NVS | a `putString` in the map's region of `AircraftManager.cpp` | CA12 |
| S-CA-PRINTD | a log line there printing the spread | CA13 |
| S-CA-NORA | `Retry-After` dropped from `GetBytes`'s list | CA14 |

S-CA-NVS and S-CA-PRINTD go into `AircraftManager.cpp`, which the C++ test does not compile, so neither can turn a
compile failure into extra red rows.

**6. The source checks.**
- **The map's files** are:
  - `include/MapQuery.h`, `include/MapPolicy.h` and `include/MapLayer.h`;
  - the regions of `src/AircraftManager.h` and `src/AircraftManager.cpp` between `// THE MAP FETCH BEGIN` and
    `// THE MAP FETCH END`.

  The check pins the number of regions in each file. A count off in either direction, or a BEGIN without its END, is
  the rig (exit 2). Comments are stripped before reading.
- **CA12** refuses these in the map's files: `Preferences`, a `put…(` call, `nvs_set`, `nvs_commit`, `SPIFFS` and
  `LittleFS`.
- **CA13** reads every `printf`, `print` and `println` there.
  - It refuses any argument whose identifier contains one of these words: lat, lon, latitude, longitude, id,
    leaderboard, hash, fnv, spread, key, mac. Identifiers are split into words at camelCase and underscores.
  - It refuses a `"lat"` or `"lon"` parameter that is not built by `wireloc::Center(`.
  - It prints how many print calls it read. Reading none is the rig.
- **CA14** reads `GetBytes`'s `kCollectHeaders`.
  - The list must be exactly Transfer-Encoding, ETag, X-Cache, X-Upstream, X-Wx-Max-Age, Retry-After, in that order.
  - The `Retry-After` header must be read into `HttpResult::retryAfter`.

**7. Outside the host suite.**
- **The wiring.** `FetchKind::Basemap`, `RequestBasemap`, `ConsumeBasemap` and the PSRAM landing buffer sit under
  `WX_LAYER_SHIPS`, inside the marked regions.
  - The buffer is allocated at the cap of the request about to go out, and reallocated only when a later request
    needs more.
  - A failed allocation leaves the map off until the next config change.
- **Compiling, and reading the images.** The code must compile in `blipscope-s3-128` (the switch off) and in
  `blipscope-s3-128-wxlook` (the switch on). Before the push, both images are read for `/basemap`:
  - the shipping image must hold none;
  - the wxlook image must hold it. That is the control: it shows the read can see the string.
- **The OtaUpdater accessor (Q-MA1)** is `bool BootFollowsUpdate()`.
  - It is read-only: `Preferences` opened read-only on `ota-mem`.
  - Its answer is cached for the boot. Its first call is in `AircraftManager::Initialise()`, before the first check-in
    can clear the record.
  - That diff goes to blipscope-5c before the push.

### The first host run (2026-11-11, 01:55Z): g++ 13.3 as predicted, MinGW 5.1 OFF on CA2-levels. Stopped for a ruling

`test/host/test_map_fetch.cpp` (42 cases), against `include/MapQuery.h` and `include/MapPolicy.h` as first written.
Both compilers built clean with `-std=c++11 -Wall -Wextra -Werror -O1`.

- **g++ 13.3** (WSL, x86-64; CI's compiler), 01:55:17Z: **42 checks, 0 failures, as predicted on every line.**
  - The route read: `MAX_R_KM` 222, `EPS_MIN_MM`/`EPS_MAX_MM` 1,000/10,000,000,
    `LEVEL_R_MIN_MM`/`LEVEL_R_MAX_MM` 1,000,000/250,000,000, `LEVEL_N_MAX` 1,024, `LEVELS_MAX` 4,
    `TERRAIN_BYTES_MAX` 2,097,152, and `CAP_BYTES` 32,768.
  - CA2-levels printed exactly the registered counts: 6,660 configs, 19,496 steps with a level of their own, 4 with
    none, and 11,041 below the top three.
  - CA9-dist: mean 3,598.1 (the bound is 3,600 ± 72), ten-minute bins 791-883 (the bounds are 708-958).
- **MinGW 5.1** (i686), 01:55:23Z: **42 checks, 1 failure. OFF:**

      FAIL CA2-levels 6660 configs; 19494 steps with their own level; 4 with none; 11044 below the top three; 2 short, first 50.5 mi: step 4

  The other 41 cases read as predicted. Stopped here: no fix, no re-run, no other check, no sabotage.

**What I read as the cause, from the code and not from any run:**
- MinGW 5.1 here targets i686, where float arithmetic runs on the x87 unit at 80 bits. GCC 5's C++ front end keeps
  those excess bits; it has no standard excess-precision mode.
- The test copies the radar's float chain. `km / (float)u.factor` is assigned to a float, and then
  `radarzoom::BuildLadder` (inline) compares `c < configured - SAME_STEP_EPS`. On i686 both can stay at 80 bits.
- At 50.5 mi the display radius then reads a hair above 50.5, so the ladder gains a 50 mi step. True float leaves
  that step out, and true float is what SSE does (g++ 13.3 on x86-64) and what the device's single-precision FPU does.
  The extra step is the 3 more steps counted below the top three.
- 50 and 50.5 are 1.0 % apart, inside the test's 1 % own-level window. So for the 50.5 mi step the test picks the
  50 mi level, whose R (81,808,320 mm) falls short of 50.5 mi + 0.785 km (82,056,872 mm). That accounts for the 2 short.

**The alternative, and how far the data excludes it.** The alternative is a defect in `mapquery::Build` or
`BuildLadder` that shows only on this compiler.
- The data excludes it in part. g++ 13.3 reads every registered number exactly, and the failing config sits on the
  one kind of boundary (x.5) where rounding to float decides whether a step is on the ladder.
- It does not exclude a MinGW-specific miscompile.

**Proposed (Q-MC13), for the ruling.**
- **The fix.** The test's copy of the float chain forces each value the device holds as a float through a `volatile
  float`. That makes it model float on any host. The header code is untouched.
- **Prediction after the fix:** MinGW reads 42/42, with the g++ 13.3 counts exactly (6,660 / 19,496 / 4 / 11,041),
  and g++ 13.3 is unchanged.
- **If MinGW still reads off,** the cause is not excess precision, and I stop again.

This miss stays on record: a prediction made for a host whose float semantics I had not checked.

### Q-MC13's ruling (2026-11-11 ~03:37Z), verbatim, and what is registered before its runs

> Q-MC13: yes, with one confirming run. The fault is in the host's arithmetic, not the device's, so the fix belongs
> in the test's copy, and the header stays untouched.
> - The fix: force each device-float value in the test's copy through a volatile float. For +, −, ×, ÷ and sqrt,
>   rounding the x87's 80-bit result to float gives exactly the device's float result, so this is faithful, not a
>   fudge.
> - Register before running:
>   - R1: MinGW 5.1 with the fix: 42/42, with g++'s CA2-levels counts exactly (6,660 / 19,496 / 4 / 11,041).
>   - R2: g++ 13.3 with the fix: unchanged, 42/42.
>   - R3, the confirming run: MinGW 5.1 with the unchanged test, built with -msse2 -mfpmath=sse: 42/42 with g++'s
>     counts. That pins the cause on x87 excess precision rather than a MinGW miscompile.
>   - Plant: your 01:55Z reading is the unfixed state, so it stands as the plant, with nothing to re-run.
> - Anything off: stop and report, as you said.
> - Then CA12–CA14, the sabotages and the firmware build, as registered.
>
> Writing the wiring while you wait is fine, and nothing runs before this ruling. The OtaUpdater diff goes to
> blipscope-5c before any push, as you said. C is already cut, so it doesn't touch v19.

**The fix, exactly.** In `RadarLadder` in `test/host/test_map_fetch.cpp`, the three values the device holds as a
float become `volatile float`: `km`, `shown`, and the argument handed to `radarzoom::BuildLadder` (a new local,
`configured`). The comment above the function says why. Nothing else in the test changes, and no header changes:
`include/MapQuery.h`, `include/MapPolicy.h` and `include/RadarZoom.h` keep their blobs at 555068c7.

**The reference.** The 01:55Z g++ 13.3 log, `0223cabd5390` (sha-256, first 12 hex), whose last line is
`42 checks, 0 failures` and whose CA2-levels line reads 6,660 / 19,496 / 4 / 11,041. MinGW writes CRLF to a redirected
stdout, so a MinGW log is compared after its carriage returns are removed. That comparison was checked against the
01:55Z pair before this was written: stripped, the MinGW log differs from the g++ log on exactly two lines (CA2-levels
and the summary).

**The builds.** Every run uses `run.sh`'s flags (`-std=c++11 -Wall -Wextra -Werror -O1 -static-libgcc
-static-libstdc++`) with `-I include`, and runs with the worktree as the repo root, as at 01:55Z. MinGW is
PlatformIO's `toolchain-gccmingw32` (5.1.0, i686); g++ is 13.3.0 in `wsl -d Ubuntu-24.04`.

| run | compiler | test | extra flags | predicted |
|---|---|---|---|---|
| **R1** | MinGW 5.1 | with the fix | none | builds clean, exits 0, and its log, carriage returns removed, is byte-identical to `0223cabd5390` |
| **R2** | g++ 13.3 | with the fix | none | builds clean, exits 0, and its log is byte-identical to `0223cabd5390` |
| **R3** | MinGW 5.1 | unchanged: a byte copy of 555068c7's blob, its sha checked against the blob before the build | `-msse2 -mfpmath=sse` | builds clean, exits 0, and its log, carriage returns removed, is byte-identical to `0223cabd5390` |
| plant | MinGW 5.1 | unchanged | none | the 01:55Z reading, which stands: CA2-levels alone red (19,494 / 4 / 11,044, 2 short at 50.5 mi) |

They run in that order, R1, R2, R3, and the first off reading stops the rest.

**What each outcome means, decided now:**
- All three as predicted: x87 excess precision in the test's copy was the cause, and the fix models the device's
  float. Step A goes on to CA12–CA14, the sabotages and the firmware build.
- R1 off: the fix does not reach every value the x87 widens. One candidate is the test's double chain (`radLat` is
  held at 80 bits as well), but that is a guess, so I stop and report.
- R2 off: the fix changed what g++ computes, which it should not. I stop.
- R3 off with R1 and R2 as predicted: an SSE build of the unchanged test still fails, so excess precision is not the
  whole cause, and the fix may be hiding something. I stop.
- Anything outside these: the comparison itself is suspect at this scale. I stop and decide.

### Q-MC13's runs (2026-11-11, 03:41-03:42Z): R1, R2 and R3 as predicted

The fix as registered (249ce535, 03:41:11Z): `km`, `shown` and a new `configured` in the test's `RadarLadder` are
`volatile float`, and the comment above it says why. The diff is 8 lines added and 3 removed in
`test/host/test_map_fetch.cpp`, and `include/` matches 555068c7.

| run | at | build | exit | log | read |
|---|---|---|---|---|---|
| **R1** MinGW 5.1, with the fix | 03:41:40Z | clean | 0 | `0223cabd5390` once its carriage returns are removed | **as predicted**: 42/42, CA2-levels 6,660 / 19,496 / 4 / 11,041 |
| **R2** g++ 13.3, with the fix | 03:41:49Z | clean | 0 | `0223cabd5390` | **as predicted**: unchanged, 42/42 |
| **R3** MinGW 5.1, the unchanged test, `-msse2 -mfpmath=sse` | 03:42:00Z | clean | 0 | `0223cabd5390` once its carriage returns are removed | **as predicted**: 42/42 with g++'s counts |
| plant: MinGW 5.1, the unchanged test, x87 | 01:55:23Z | clean | -- | -- | the 01:55Z reading, which stands: CA2-levels alone red |

R3's copy was 555068c7's blob, `36ec7344f708`, checked before the build. The plant and R3 are the same source built
the same way except for `-msse2 -mfpmath=sse`, and that difference alone turns CA2-levels from red to green. So the
cause was x87 excess precision on the host, not a MinGW miscompile, and the fix makes the test's copy compute what the
device computes. Next, as registered: CA12-CA14, the sabotages and the firmware build.

### The wiring, written; what its checks will read, registered before they run (2026-11-11)

**Where the registration left a choice, and what I chose.** None changes a registered row's set.
- **One gate macro, `BLIPSCOPE_MAP_FETCH`,** defined once in `AircraftManager.h`'s first map region as
  `FEATURE_CLOUD_FEED && WX_LAYER_SHIPS && !BLIPSCOPE_WEB_RENDERER`. Every map site tests it.
  - Why the web renderer is left out: `tools/web-renderer/build.sh` also defines `WX_LAYER_SHIPS`. The website draws
    no raw-buffer layer, its `GetBytes` is a dead socket (`web_http.cpp`), and its OtaUpdater is a shim without
    `BootFollowsUpdate`.
  - Gated on the weather switch alone, the website would hold about 200 KB of each visitor's memory for a package that
    cannot arrive, and the build would not link.
- **`CloudFeed::BasemapUrl`,** beside `WeatherUrl`, under the same three flags. CloudFeed's own comment on
  `LeaderboardUrl` is the reason: an endpoint built outside that list is the one that stays on an old path.
  `CloudFeed.{h,cpp}` are outside the registered list of files, so blipscope-5c reads them with the OtaUpdater diff.
- **A 200 the gate never read** (over the cap, short or stalled) is logged as `why=unread` (`kMapWhyUnread`, 0xFF), not
  as a `bmpk::Status` it was not given.
- **CA13 also requires at least one `"lat"` and one `"lon"` parameter** in the map's files, so moving the request out
  of the regions cannot pass it vacuously. That is stricter than registered, never looser.
- **`check_wx_wiring.py`'s WA1 selftest plant** replaced `"X-Upstream", "X-Wx-Max-Age" };`. That text is gone, because the
  list now ends with `"Retry-After"`, so the plant would not apply and the WxWiring block would go red. It now replaces
  `"X-Upstream", "X-Wx-Max-Age",`, which occurs once and still drops `X-Wx-Max-Age` from the list. Its owner reads it.

**What each run will read** (all on the worktree, nothing pushed). They run in this order, and the first off reading stops the rest:

| # | run | predicted |
|---|---|---|
| W1 | `python -I test/host/check_map_fetch.py .` | exit 0. Exactly: `ok CA12 nothing that writes to flash`; `ok CA13 3 print calls read; 2 lat/lon parameters, all through wireloc::Center`; `ok CA14 kCollectHeaders ['Transfer-Encoding', 'ETag', 'X-Cache', 'X-Upstream', 'X-Wx-Max-Age', 'Retry-After']; Retry-After read into HttpResult::retryAfter: True`; `3 checks, 0 failures`. The 3 is the alloc line, the dropped-request line and the per-answer line. The one `Preferences` in the map's files sits in a comment, so a stripper that left comments in would turn CA12 red |
| W2 | `--anchors` for every `test/host/sabotage_*.py`, and the two board plants `run.sh` runs | every one exits 0, with every anchor present exactly once |
| W3 | `check_wx_wiring.py .`, then `--selftest` | exit 0, all 10 rows ok; then each of its 10 plants red on exactly its own row, `SELFTEST PASSED`, exit 0 |
| W4 | `sabotage_map_fetch.py --control` | both suites as its table (`42 checks, 0 failures`; `3 checks, 0 failures`), exit 0 |
| W5 | `sabotage_map_fetch.py` (MinGW 5.1, the driver's default) | the control clean, then each of the 23 plants `AS PREDICTED` with item 5's set; `real files unchanged: True`; exit 0 |
| W6 | `pio run -e blipscope-s3-128`, then `-e blipscope-s3-128-wxlook`, only while C: has 30 GB free | both build, exit 0 |
| W7 | each W6 image's `firmware.bin`, read for the bytes `/basemap` | the shipping image holds **0**; the wxlook image holds **1 or more**, the control that the read can see the string |

Not run here, and not mistaken for run: the web renderer's build (no emsdk on this machine). Its CI job builds it on the
push, and the gate above is what keeps the map out of it.

### W1-W7 as run (2026-11-11, 03:58-04:06Z): every one as predicted

| # | at | read |
|---|---|---|
| W1 | 03:58:10Z | exit 0; the four lines exactly as registered (3 print calls, 2 lat/lon parameters, the six headers in order) |
| W2 | 03:58:27Z | 36 drivers checked, every one exit 0 with its `ANCHORS` line; `plant_s_swb.py` and `plant_s_tw.py` all present exactly once |
| W3 | 03:58:39Z | `check_wx_wiring.py`: 10 rows ok, exit 0; `--selftest`: each of the 10 plants red on exactly its own row (the re-anchored WA1 plant among them), `SELFTEST PASSED`, exit 0 |
| W4 | 03:58:46Z | `test_map_fetch: 42 checks, 0 failures`; `check_map_fetch.py: 3 checks, 0 failures`; exit 0 |
| W5 | 03:58:56-03:59:12Z | the control clean; all 23 plants `AS PREDICTED`, each with item 5's set exactly; `real files unchanged: True`; exit 0 |
| W6 | 03:59:34-04:05:58Z | `blipscope-s3-128` SUCCESS (2:59), `blipscope-s3-128-wxlook` SUCCESS (3:15), C: at 96 GB free. Each log's 9 warnings that name a touched file are pre-existing (two unused variables, a `%02d` truncation, six `[ota-mem]` `%u` formats), none in the map's code |
| W7 | 04:06:07Z | shipping `firmware.bin` (1,932,448 B, written 04:02:33Z): `/basemap` x0. wxlook `firmware.bin` (1,958,752 B, written 04:05:57Z): `/basemap` x1, the control |

### W8, the whole host suite under WSL, registered before it runs

`bash test/host/run.sh` in `wsl -d Ubuntu-24.04` (g++ 13.3), on this commit. Predicted:
- **exit 2, and the failed blocks are exactly these two:** "the same TU, for the board" (no xtensa toolchain in WSL),
  and "Weatherscope R0 and R0b", the rig (no python with Pillow). Both fail on every WSL run.
- **36 drivers** in each driver pass. Anchors: `36 driver(s) checked`, plus both board plants. Controls and the full
  pass: `36 driver(s): 35 run, 1 skipped`, the skip being `sabotage_wx_towns.py` (no machine holds its Census
  archives). Lint: `36 driver(s), 0 flagged; 3 pinned line(s)`.
- **The new "MapFetch" block passes** (`42 checks, 0 failures`, then `3 checks, 0 failures`), and so does every block
  not named above, the weather wiring's selftest included.
- **Anything else stops the run's reading for a ruling.** A driver off in the full pass is first run against main's
  own tree (947e1d38), to tell what this branch broke from what was already broken.

### W8 as run (2026-11-11, 04:09:04-04:15:49Z): as predicted

The first launch at 04:08:32Z never ran `run.sh`. My wrapper called `git log` inside WSL first, and git there cannot
follow this worktree's `.git` file, which names a Windows path. It exited 128, and the `&&` stopped the run before it
began. Nothing was measured, so this is the rig, not a reading. The relaunch dropped the git call (the commit,
a1c0657c, and a clean tree were read from Windows first) and ran the suite once:
- exit 2, and `FAILED BLOCKS` lists exactly "the rig: Weatherscope R0 and R0b" and "failed: the same TU, for the
  board". The log holds no other `FAIL` line.
- Anchors: `36 driver(s) checked`, plus both board plants. Controls: `36 driver(s): 35 run, 1 skipped`. Lint:
  `36 driver(s), 0 flagged; 3 pinned line(s)`. The full pass: `36 driver(s): 35 run, 1 skipped`. The skip is
  `sabotage_wx_towns.py` in both passes.
- `sabotage_map_fetch.py` passes in all four: anchors (23), control (2 suites), lint, and the full pass (24 `AS
  PREDICTED` lines: the 23 plants and the summary).
- MapFetch: `42 checks, 0 failures`, then CA12-CA14 exactly as in W1, then `3 checks, 0 failures`.

### CI for this branch's PR, predicted (registered before the push, which waits for blipscope-5c's read of the OtaUpdater diff)

**Baseline:** #631's pull-request checks at its head `2b90f7bbf252`. #631 is the first PR to touch `src/` after main's last
job-set change (#629, `81cfbd09`).
- It ran four workflows: device-ids, firmware, no-traffic-captures and web-renderer.
- **34 checks: 30 success, 4 skipped, 0 failed.** The skips are firmware's `promote`, `ship-alert` and `version`, and
  web-renderer's `publish`.

This PR touches `docs/`, `include/`, `src/` and `test/host/`. The workflows' `pull_request` filters fire the same four:
- device-ids, firmware and no-traffic-captures have no path filter;
- web-renderer fires on `src/**` and `include/**`;
- workers.yml does not fire, because nothing under `proxy/` or `test/fixtures/wx/` changes;
- print-cards and provisioner do not fire either.

No job in those four is conditional on which files changed. **Predicted: 34 = 30/4/0, with the same names as #631's.**
- firmware: 30 = 27/3, with the ten builds, adsbdb-gate and the three host-tests legs among the 27.
- device-ids: 1/0.
- no-traffic-captures: 1/0.
- web-renderer: 2 = 1/1 (build passes, publish skipped).

**What it means if it reads otherwise:**
- A web-renderer `build` failure means the map gate did not keep the map out of the website build, and is a miss.
- A host-tests failure outside the two WSL-only blocks is a miss.
- Any other count is outside the set. I stop and report, and there is no re-run in between.

The local guards were run at a1c0657c, and each exits 0: `check_wifi.py`, `check_device_ids.py`, `check_macs.py`,
`check_binary_strings.py` and `check_redaction_vectors.py`.

**Main moved between the go and the push (2026-11-11 ~04:56Z).** The push's chain first required main to be 947e1d38. It was `68dc169a` (#630), so nothing was pushed. Read before pushing again:
- #630 changes no workflow, so the job set, and with it the baseline above, is unchanged.
- It merges cleanly with this branch (`git merge-tree` exit 0).
- It overlaps two of this PR's files, independently:
  - `src/CloudFeed.cpp`: its `static_assert` is at line 10, and `BasemapUrl` at line 100.
  - `test/host/run.sh`: separate blocks.
- Its new driver, `sabotage_ota_model.py`, plants only in `include/OtaIdentity.h`.

**The prediction stands: 34 = 30/4/0**, now on the merge with `68dc169a`.

## Step B: drawing the package under the radar (predictions, for review; 2026-11-11)

**Status: for review. Nothing here is code yet.** Step A merged as #634 (`1d7049f72ac1`): a validated package is held
in `mapPkg` (`src/AircraftManager.cpp`, `ConsumeBasemap`), and nothing draws it. Step B draws it. Step C (the wedge's
green added to the map) and step D (the settings) stay separate, as ruled.

**Read first, from the code at `78c2bec2`, and not from any run:**
- **How the radar face's frame is built today.** `src/main.cpp:943-944` clears it:
  `if (!DrawWeatherUnder(canvas)) fillScreen(black)`. `main.cpp:968-970` draws the sweep before `Draw`:
  `DrawSweepOverWeather`, else the opaque `DrawRadarSweep` (`include/DrawHelpers.h:29-58`). After that, `DrawRadar`
  draws the rings, the airports and the planes.
- **The weather layer copies over the whole frame.** `DrawWeatherUnder` copies all 115,200 B of `wxLayer565` with one
  `memcpy` (`src/AircraftManager.cpp:3615`). Dry pixels in that layer are black, so **a map drawn before it would be
  erased.** S3's order (map, then names, then weather) needs the two layers composed into one.
- **Both wedge paths overwrite whatever is under them.** The opaque wedge does, and so does the weather blend: on a dry
  pixel, `wxsweep::Lit565` returns the opaque `Green565` (`include/WxSweep.h:40-45`). Until step C, the beam erases the
  map wherever it passes.
- **Byte order.** `maplayer::Build` writes logical RGB565 (`include/MapLayer.h:276`). The canvas and `wxLayer565` hold
  it byte-swapped (`AircraftManager.cpp:3645`).
- **Airport boxes are recorded nowhere.** `DrawAirports` (`AircraftManager.cpp:5367-5413`) draws each mark as
  `drawRect(x-2, y-2, 5, 5)` and its code at `(x+5, y-3)` in the 6 × 8 GLCD font. `overlapFrame` only counts. A name
  that must not cover a code has to get the boxes from the same list and the same rule.
- **The Weatherscope's place-name code is tied to its own frame:** the y band 26-192 (`BoxFree`,
  `src/weatherscope/WxScreens.h:142-152`) and a north-up mapper with no rotation (`WxBasemap.h:43-56`). The rule
  itself carries over: `RankOrder` (largest first, a population of 0 ranking as 1,000, ties in file order; :188-201),
  `Cap` (8 at 60 mi and over, 6 at 30 and over, 5 below; :204), `Inner` (24.4 px; :126), `ClearsCentre` (:118-123),
  and the box geometry with 4 px of air (:156-184).

### What step B builds

| part | where | what |
|---|---|---|
| **the composer** | `include/MapCompose.h`, pure | composes the base layer: `Build`'s output byte-swapped, the place names drawn onto it, then every non-zero weather pixel over that. Off the disc is 0 |
| **the place names** | `include/MapPlaces.h`, pure | the Weatherscope's rule, its arithmetic repeated where its code is tied to its frame, as `MapLayer.h` repeats `wxraster::Line` under CM11. Anchors come from the map's own affine (`maplayer::AffineFor`, so rotation is included). Inside the disc (116 px), clear of the centre, **never over an airport mark or code**, 4 px of air |
| **the stale test** | `include/MapCompose.h`, pure | **the map is rebuilt** (`Build` plus the names) when the view (lat, lon, `viewRadLat`, `viewRadLon`, `rotSin`, `rotCos`), the package (step A's sequence) or the airport boxes change. **The composite is redone** (`memcpy` plus the weather overlay, no `Build`) when only the weather layer changes. Never per frame |
| **the wiring** | `AircraftManager`, under `BLIPSCOPE_MAP_FETCH`, in `// THE MAP DRAW BEGIN/END` regions | the PSRAM buffers are allocated at the first accepted package, all or none. **`DrawWeatherUnder` becomes the base draw:** with a map composite ready, one `memcpy` of the composite; without one, today's two paths byte for byte. Rebuilds run on the loop task at the next shown radar frame, as the weather layer's do |

**Memory (Q-MB1):** the map layer with its names, 115,200 B; the composite, 115,200 B; `Build`'s index image,
57,600 B; and `Scratch`, 32 B per vertex (two int32 and three double) for maxVertices + 1. That is about 288 KB of PSRAM, none of it internal (CLAUDE.md:
sprites in PSRAM cost nothing internally). **The steady frame stays one `memcpy`:** CF1 and CF4 already price it.

**The gate.** Everything compiles only under `BLIPSCOPE_MAP_FETCH`, as step A does, so the shipping image draws nothing.
CB-W, below, reads that off the image.

### The composer and the stale test (CB1-CB5), host: `test/host/test_map_draw.cpp`

| row | input | predicted | plant (turns exactly) |
|---|---|---|---|
| **CB1** | synthetic layers: map pixel M, name pixel P, weather pixel W | W where W ≠ 0; else P where a name is drawn; else M; 0 off the disc. **S3's order** | S-CB-ORDER, the weather composed under the map: {CB1} |
| **CB2** | `Build` over KJAC at 100 mi | each composite pixel with no name and no weather equals `bswap16` of `Build`'s pixel | S-CB-NOSWAP: {CB2} |
| **CB3** | no package ever accepted, weather on and off | **0** map bytes allocated, **0** `Build` calls, and the base draw's bytes equal today's: `wxLayer565` with weather on, black with it off | S-CB-EAGER, buffers allocated at `Initialise`: {CB3 alloc} |
| **CB4** | each of the six view inputs, the package sequence and the airport boxes changed, one at a time | exactly **1** map rebuild each; **1,000** frames with nothing changed: **0** | S-CB-NOROT, rotation left out of the key: {CB4 rotSin, CB4 rotCos}; S-CB-PERFRAME, rebuilding every frame: {CB4 steady} |
| **CB5** | the weather layer rebuilt, nothing else | **1** compose, **0** `Build` calls | S-CB-WXBUILD, a weather change rebuilding the map: {CB5} |

### The place names (CB6-CB8)

| row | check | plant (turns exactly) |
|---|---|---|
| **CB6** | **A sweep:** KJAC, KSFO and Bend; every ladder step; rotations 0-315° by 45°; the baked airport table near each. Every frame must hold: placed ≤ the cap; **no placed town ranks below a skipped town that would have fitted** (largest first, ties in file order); no box within `Inner` of the centre; every corner within 116 px; **0 boxes overlapping an airport mark or code box** (with 4 px of air); 0 overlapping each other. **The counts (placed, refused by reason) are computed by an independent scratch reading once Q-MB3 to Q-MB5 are ruled, and registered before code** | S-CB-AIRPORT, airports not obstacles: {CB6 airports}; S-CB-FILEORDER, file order: {CB6 rank}; S-CB-NOCAP: {CB6 cap} |
| **CB7** | a town's anchor equals the point `Build` maps from the same (qx, qy), ± 0.5 px, and is within 1 px of `radarproj::Project` (as CM3), at every rotation | S-CB-NORTHUP, the Weatherscope's north-up mapper: {CB7 at rotations ≠ 0} |
| **CB8** | **one helper gives the airport boxes to both `DrawAirports` and the names:** the same list (`cloudAirports`, else the baked `AIRPORTS`), the same cull and `ProjectAt`, the mark 5 × 5 at (x−2, y−2), the code at (x+5, y−3), 6 px a character. A source check that `DrawAirports` calls it | S-CB-SECONDPATH, `DrawAirports` drawing the code at x+6 without the helper: {CB8} |

The repeated arithmetic stays equal to the Weatherscope's by test, as CM11 does for the line: `RankOrder`, `Inner`,
`ClearsCentre` and the box geometry, over every case they share.

### Checks that read the source (CD1-CD3), and the image (CB-W)

| row | check | plant |
|---|---|---|
| **CD1** | F8 for the new files: no `Preferences`, no `put*`, SPIFFS or LittleFS | S-CD-NVS: {CD1} |
| **CD2** | **privacy:** no `printf` in the map's files prints a place name, an anchor or a coordinate. A town's name near the device says where the device is | S-CD-PRINTNAME: {CD2} |
| **CD3** | the map's View is filled from the same six fields as the weather layer's (`AircraftManager.cpp:3560-3567`) | S-CD-VIEW, `viewRadLat` swapped for `radLat`: {CD3} |
| **CB-W** | shipping `firmware.bin` holds no map-draw string, and the wxlook image holds them, read off both images as step A's W7 was | the wxlook image is its control, as it was W7's |

`check_map_fetch.py` pins its region counts (`h:3`, `cpp:9`). The new `THE MAP DRAW` markers are counted apart, by
`check_map_draw.py`, so step A's pins do not move.

### Bench, with step M's window on E2:08 (after v19's, on the owner's word)

CF1 to CF6 already price the layer, and step B is what makes them measurable on the device. Step B adds:
- **CBF1:** a zoom step's rebuild (`Build`, the names and the compose) ≤ **100 ms**, the ruled bar, printed per pass.
- **CBF2:** on glass, the names read, and none covers an airport code. The owner's eye has the last word.
- **CBF3, until step C:** the beam erases the map where it passes, through both wedge paths. **Registered here so that
  nobody reads it as a step B defect** (Q-MB2).

### Questions for the ruling

- **Q-MB1, memory.** Two PSRAM layers (the map with its names, and the composite), about 288 KB with the index and
  scratch, allocated at the first accepted package, all or none; a failure leaves the map off and logged.
  - The alternative is one layer, rebuilding the map on every weather change: 30-70 ms (CF5), about every 5 min.
  - **I recommend the two layers.**
- **Q-MB2, the wedge until step C.** Both wedge paths erase the map under the beam.
  - (a) B merges as it stands, and C follows with its own predictions. Only images built with `WX_LAYER_SHIPS` draw a
    map, which today is the wxlook image alone, so no customer sees it.
    - **The release flip changes that.** `platformio.ini:148` turns `WX_LAYER_SHIPS` on for every `${cloud.prod}` env.
      **So step C must merge before that flip.** That is a dependency for the release, not a row here.
  - (b) Fold C into B.
  - **I recommend (a).**
- **Q-MB3, the cap.** The ruling says "at most 8". The Weatherscope's rule caps at 8 from 60 mi, 6 from 30 and 5 below.
  - **I recommend the Weatherscope's `Cap`** at the radar's current view, since "the Weatherscope's rule" includes it.
  - No peaks share the cap (no peaks in the request, Q-MC7 (b)).
- **Q-MB4, what else a name must avoid.** Proposed fixed boxes, besides airport marks and codes:
  - the three ring labels (`DrawRadarCircles`, x = CENTRE + 4);
  - the cardinal letters (r = 112);
  - the screen-indicator row (y ≈ 221-227).
  - The stale banner, the weather note, the zoom pill and toasts are transient and draw on top. They are not obstacles.
- **Q-MB5, the label's look.** The Weatherscope's (a 3 × 3 dot and the text) in the theme's place grey (86, 86, 86),
  with a 1 px halo in the theme's land colour so the text reads over roads and water. The owner judges it on glass.
- **Q-MB6, the place list.** Read with `bmpk::ForEachPlace` into `MapPlaces.h`'s own list, at most 256, where more
  refuses the package's towns as the Weatherscope does.
  - This avoids including `WxScenes.h` and `WxScreens.h` into the radar image: they pull in the Weatherscope's
    scenario, theme and town tables, and an unreferenced function still costs image bytes (`.eh_frame`).
- **Q-MB7, names under the weather.** S3 puts the names below the weather, so heavy rain hides them. Confirm.

### CI for this PR (docs-only), predicted

**32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed,** in three workflows
(device-ids, firmware, no-traffic-captures).
- **The baseline is #640's finished docs-only PR run** (head `289a1e25609f`: 32 = 29/3/0, the same three skips).
- No workflow has changed since #633 (`e2c34daf82a1`), whose change to `device-ids.yml` added steps, not jobs. #640's
  run postdates it.
- **Any other count, skip set or failure means a stop and a report,** with no re-run.

### The ruling on step B's predictions, verbatim (the orchestrator, 2026-11-11 ~18:58Z)

> Ruling from the orchestrator, 2026-11-11 ~18:58Z, for map-client: step B's predictions. Go to push and open the PR.
>
> Read from git: 254834b8, parent 78c2bec21dfa (main), docs/blipscope-map-client-steps.md only, +123, no deletions, no
> coordinates past 2 dp, no name. Every row is present: CB1-CB8, CB-W, CD1-CD3, CBF1-CBF3. Your five readings of the code
> are the right things to have found before designing.
>
> Q-MB1: yes. Two PSRAM layers (~288 KB), allocated at the first package, all or none.
> - Add a bench row: psram_free falls by the predicted amount, and the internal heap is untouched (CLAUDE.md's measured
>   rule: PSRAM sprites cost nothing internally).
> - An allocation failure refuses the map (no package drawn). It never draws a half composite.
>
> Q-MB2: (a), with one condition. You found the hazard yourself: platformio.ini:148's release flip turns WX_LAYER_SHIPS on
> fleet-wide, so step C must land before it.
> - That rule must RUN, not be remembered. In this PR, add a compile-time refusal: with WX_LAYER_SHIPS defined and step
>   C's composition not present, the build stops with #error, naming step C.
> - Register a plant: define WX_LAYER_SHIPS without step C, and the build must fail.
> - Step C later removes the condition by providing what it names.
>
> Q-MB3: yes, the Weatherscope's Cap (8/6/5 by radius), with the arithmetic held equal by a test, as CM11 does.
>
> Q-MB4: yes. Ring labels, cardinal letters and the screen-indicator row are fixed obstacles; transient overlays are not.
>
> Q-MB5: register it as proposed (a 3×3 dot, grey text, a 1 px halo in the land colour). It's what customers see, so the
> owner judges it on glass at CBF2. Nothing more is built toward an alternative before then.
>
> Q-MB6: yes. Your own list from ForEachPlace, up to 256. More than 256 refuses the towns rather than cutting the list: a
> truncated list is a list that lies.
>
> Q-MB7: confirmed per S3 (the map, then the names, then non-zero weather pixels). Names under rain hide where it rains;
> that's the owner's to see on glass too. List it at CBF2 beside Q-MB5.
>
> CD2 (no printing of place names) is the right privacy row: a nearby town names the device's location.
>
> CI as you predicted: 32 = 29/3/0, any other reading stops. Then send me the PR number and head. I read twice by the full
> sha before approving.
>
> Next: push, open the PR, and add Q-MB1's and Q-MB2's rows before any code.

### Q-MB1's and Q-MB2's rows, registered before any code

| row | check | plant (turns exactly) |
|---|---|---|
| **CB9** | host: an allocation failure injected at each of the map's buffers in turn (the layer, the composite, the index, the scratch). Each time **every** map buffer is freed, the map is refused (`mapAllocFailed`, logged), `Build` is never called, and the base draw's bytes equal CB3's no-package frame. **No half composite** | S-CB-HALF, keeping the buffers that succeeded and drawing with them: {CB9} |
| **CB10** | **the build refuses step B without step C.** In a shipping build, the map's draw code with `WX_LAYER_SHIPS` defined and step C's composition absent stops with `#error`, naming step C. The condition is **proposed** below, for confirmation (Q-MB8) | P-CB-FLIP: `blipscope-s3-128` built with `-DWX_LAYER_SHIPS` added (the release flip), and **the build must fail** with that text. The control: `blipscope-s3-128-wxlook` still builds |
| **CBF4** | bench, Q-MB1: at the first accepted package, `psram_free` falls by the predicted amount and the internal heap does not move: `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` read just before and just after the allocations, in the same loop pass, differs by **0**. The PSRAM figure is the sum of the four buffers' TLSF prices, computed before the run from the package's `maxVertices`, which the map's log line prints as a count | an internal allocation planted in place of one PSRAM buffer: the internal reading moves, and CBF4 goes red |

**CBF2 also carries Q-MB5 and Q-MB7.** The label's look (a 3 × 3 dot, the theme's place grey, a 1 px halo in the land
colour) and the names hidden under rain are the owner's to judge on glass. Nothing is built toward an alternative before
then.

**Q-MB8, the refusal's condition (a question about the ruling's literal form, not a change to it).** Read literally,
"`WX_LAYER_SHIPS` defined and step C's composition not present" also refuses `blipscope-s3-128-wxlook`.
- That env sets `-DWX_LAYER_SHIPS` itself (`platformio.ini:401-408`), so the bench keeps weather on, and CI builds it
  (the firmware workflow's wxlook step, which runs `check-no-bench-hooks.sh --expect "[wx-bench]"`).
- So the literal form would fail CI's wxlook build from the moment the refusal exists, and step B could not be benched
  before step C.
- **Proposed condition:** `defined(BLIPSCOPE_MAP_FETCH) && !defined(WX_LOOK_AB) && !defined(<step C's macro>)`.
  - `BLIPSCOPE_MAP_FETCH` already excludes the web renderer, which defines `WX_LAYER_SHIPS` too.
  - `WX_LOOK_AB` is set by the wxlook env alone. Its image carries the `[wx-bench]` marker, which
    `check-no-bench-hooks.sh` refuses in every slug-ful image, so the exemption cannot reach a customer.
  - The release flip (`${weather.layer}`, `platformio.ini:148`) defines `WX_LAYER_SHIPS` without `WX_LOOK_AB`, and is
    refused.
- **Where it lives:** the ruling says "in this PR". This PR stays docs-only until Q-MB8 is answered, because the literal
  form breaks a CI build. Once it is, the refusal goes in as code, with CB10's plant run as registered, and its CI is
  predicted then.

### Q-MB8's ruling, verbatim (the orchestrator, 2026-11-11, sent between 19:04:40Z and 19:12:12Z)

Its own stamp, ~19:08Z, was estimated, not read. The orchestrator corrected it to that window (read 19:12:12Z). The
text below is as sent, stamp included.

> Ruling from the orchestrator, 2026-11-11 ~19:08Z, for map-client: Q-MB8. Your condition is accepted, and it lands in
> step B's code PR.
>
> You were right to ask rather than improvise: the literal form would have failed CI's wxlook build.
>
> The condition: refuse when defined(BLIPSCOPE_MAP_FETCH) && !defined(WX_LOOK_AB) && !defined(<step C's macro>).
> - I checked the claim the exemption rests on. WX_LOOK_AB compiles in "[wx-bench] the weather bench image"
>   (AircraftManager.cpp:943). scripts/check-no-bench-hooks.sh lists "[wx-bench]" in MARKERS (:40) and refuses it in every
>   slug-ful image. So the exemption can't reach a customer.
> - That claim becomes a registered row, not just my reading. P-CB-BENCH: a slug-ful env built with -DWX_LOOK_AB must be
>   refused by check-no-bench-hooks.sh. The control: the same env without it passes.
> - P-CB-FLIP and its control (wxlook still builds) stay as registered.
>
> Where it goes:
> - #644 stays docs only (predictions).
> - The #error lands in step B's code PR, in the FIRST commit, or in the same commit as the first line of code that draws
>   the map. main must never hold a map-drawing path without its refusal.
> - Add Q-MB8's condition and P-CB-BENCH to #644 as a docs commit now, so they're frozen before the code. That moves
>   #644's head; re-predict CI and send me the read.
>
> CB9 and CBF4 are as you added them. Good, including CBF4's internal-heap delta of 0 read in one loop pass.
>
> Next: the docs commit with Q-MB8 and P-CB-BENCH, then #644's CI read. I read twice by the full sha before approving.

### CB10's condition, P-CB-BENCH and CD4, frozen before any code

**CB10, as ruled.** In the map's draw code:

```cpp
#if defined(BLIPSCOPE_MAP_FETCH) && !defined(WX_LOOK_AB) && !defined(BLIPSCOPE_MAP_STEP_C)
#error "step B draws the map without step C: the sweep erases it. Land step C before WX_LAYER_SHIPS ships."
#endif
```

- **`BLIPSCOPE_MAP_STEP_C`** is step C's macro, named now so that it is frozen. Step C's header defines it in the same
  commit that makes the wedge's green add to the map pixel, and that is what removes the refusal. Nothing else may define
  it: CD4 below refuses a definition anywhere else.
- **Where it lands:** step B's code PR, in its first commit, or in the same commit as the first line that draws the map.
  `main` never holds a map-drawing path without its refusal.

**The plants, with what each reading means, decided now:**

| plant | how it runs | predicted | any other reading |
|---|---|---|---|
| **P-CB-FLIP** | `blipscope-s3-128` built with `-DWX_LAYER_SHIPS` added (the release flip), through `PLATFORMIO_BUILD_FLAGS`, from PowerShell | the build **fails**, and its log holds the `#error` text | a build that passes, or that fails for any other reason, stops the step and is reported |
| control | `blipscope-s3-128-wxlook` as it stands | builds | stop and report |
| **P-CB-BENCH** | `blipscope-s3-128` (slug-ful) built with `-DWX_LOOK_AB` added, then `bash scripts/check-no-bench-hooks.sh <its firmware.bin>` | **exit 1**, with `FAIL: bench hook marker '[wx-bench]'` | exit 2 is BLIND (no anchor), and a build that fails is BLIND too: neither is a pass, and either stops the step |
| control | the same script on the unchanged `blipscope-s3-128` image | **exit 0**, `ok: no bench hook` | stop and report |

**P-CB-BENCH is a premise check, so its reading has to be read off the image** ("read the artifact, not the config"):
the scan reads `firmware.bin` itself, and its anchor (`[build] env=`) has to be found before any absence counts.

**CD4, so the placement rule runs rather than being remembered.** `check_map_draw.py` holds, at every head CI sees:
- if any `THE MAP DRAW` region exists, the refusal exists, with exactly CB10's condition;
- `BLIPSCOPE_MAP_STEP_C` is defined nowhere except step C's header.

| row | plant (turns exactly) |
|---|---|
| **CD4** | S-CD-NOREFUSAL, the `#error` block deleted while a draw region stays: {CD4}. S-CD-STEPC, `BLIPSCOPE_MAP_STEP_C` defined in `AircraftManager.cpp`: {CD4} |

### CI for this PR, re-predicted at its new head

**Unchanged: 32 check runs, 29 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed,** in three
workflows (device-ids, firmware, no-traffic-captures).
- This commit is docs only, like the two before it.
- The baseline is still #640's finished docs-only PR run (`289a1e25609f`), and no workflow has changed since #633.
- The run on the previous head (`7bb2556b`) is superseded by this push. Whatever it read is not this prediction's
  subject, and it is recorded as superseded.
- **Any other reading at the new head means a stop and a report,** with no re-run.

### #644's first CI reading: OFF on `sku-readers` (2026-11-11), and the fix as ruled (sent 19:04:40-19:12:12Z)

The ruling's own stamp, ~19:25Z, was estimated, not read. The orchestrator corrected it to that window.

- **The reading,** on the first head `7bb2556b`: `sku-readers` failed at 19:04:35Z. This doc named the firmware
  workflow's file and is not on that guard's reviewed list.
  - The other non-successes were the cancellations of my second push (from 19:06:46Z), and `host-tests`' "Both legs
    must pass", which read its cancelled leg.
- **The cause, and my miss:** the Q-MB8 row cited the firmware workflow by its path and line, against standing rule C1
  (2026-10-26: write "the firmware workflow" instead). The 32 = 29/3/0 prediction was made without running the guard that
  reads docs for that name. It is in the firmware workflow, not in device-ids.
- **The fix, as ruled:** that one line reworded, not an `ALLOWED_MENTIONS` entry. From now on,
  `python3 scripts/skus.py --check-readers` runs locally before every push, chained with `&&` beside the device-ids
  guards.
- **The second head's run, recorded and not acted on:** `d7c71008` read 32 = 28 success / 3 skipped (`promote`,
  `ship-alert`, `version`) / 1 failed, the one failure `sku-readers` (finished, read twice, 19:22-19:23Z). That
  is as predicted once the cause was known, since the same line was still in the doc.
- **CI at the fix's head, re-predicted: 32 = 29/3/0,** on #640's docs-only baseline. **This was #644's first off
  reading; another goes to the owner.**

### Registered before step B's code (2026-11-11): the refusal's commit

**Status: #644 merged (`0e384618`). Step B's code goes on `feat/map-client-step-b`, and this registration is its first
commit.** The commit after it carries CB10's refusal and CD4, and no drawing code. So `main` can never hold a
map-drawing path without its refusal. That is the ruling's second clause: the refusal lands no later than the first line
that draws the map.

**What the refusal's commit holds:**
- **`src/AircraftManager.h`** gains one `// THE MAP DRAW BEGIN` / `END` region, directly after the `THE MAP FETCH`
  region (where `BLIPSCOPE_MAP_FETCH` is defined), holding CB10's block exactly as frozen. Step A's `THE MAP FETCH`
  pins (`h:3`, `cpp:9`) do not move.
- **`test/host/check_map_draw.py`** holds CD4 alone until the draw files exist, so it prints **`1 checks, 0 failures`**.
  CD1-CD3 join it in the commit that adds the files they read.
  - CD4 reads the `THE MAP DRAW` regions of `src/AircraftManager.h` and `.cpp`, pinned at `h:1`, `cpp:0` for now.
    A pin off in either direction, or an unbalanced marker, is the rig (exit 2).
  - If any region exists, the comment-stripped header must hold CB10's `#if` line and its `#error` line, compared with
    whitespace normalised.
  - A definition of `BLIPSCOPE_MAP_STEP_C` (`#define BLIPSCOPE_MAP_STEP_C`, `-DBLIPSCOPE_MAP_STEP_C`) is searched for in
    `include/`, `src/`, `platformio.ini` and `tools/`. The allowed definer list is empty until step C names its header.
- **`test/host/sabotage_map_draw.py`**, with two plants, each in a copy of the tree:
  - **S-CD-NOREFUSAL** deletes the `#error` block and keeps the region: {CD4}.
  - **S-CD-STEPC** adds `#define BLIPSCOPE_MAP_STEP_C 1` to `src/AircraftManager.cpp`: {CD4}.
  - Its count constant is `SOURCE_CHECKS = 1`, so `lint_driver_counts.py` reads a named count.
- **`test/host/run.sh`** gains a `== MapDraw ==` block after `MapFetch`'s.

**Predicted, before any of it is written:**

| run | predicted |
|---|---|
| **L1** `check_map_draw.py` | `1 checks, 0 failures`, exit 0 |
| **L2** `sabotage_map_draw.py` | control clean; S-CD-NOREFUSAL {CD4}; S-CD-STEPC {CD4}; `MAP DRAW SABOTAGES (2) AS PREDICTED`; the real files unchanged |
| **L3** `--anchors` over every driver (42 with the new one), and `--control` for the new one | every anchor present exactly once; the control matches its table |
| **L4** `check_map_fetch.py` | unchanged: `3 checks, 0 failures` |
| **L5** the four builds: pio from PowerShell, in this order, each in its own process with `PLATFORMIO_BUILD_FLAGS` set only for it | see below |

**L5, and what each reading means:**

| build | predicted | any other reading |
|---|---|---|
| **R-FLIP:** `blipscope-s3-128` with `-DWX_LAYER_SHIPS` | **fails**, and the log holds CB10's `#error` text | a pass, or a failure for any other reason, stops the step |
| **R-FLIPC:** `blipscope-s3-128-wxlook` as it stands | builds | stops the step |
| **R-BENCH:** `blipscope-s3-128` with `-DWX_LOOK_AB`, then `check-no-bench-hooks.sh` on its `firmware.bin` | builds; the scan exits **1** with `FAIL: bench hook marker '[wx-bench]'` | a failed build, or scan exit 2, is BLIND and stops the step |
| **R-BENCHC:** `blipscope-s3-128` unchanged, then the same scan | builds; the scan exits **0** | stops the step |

- **Each flag is proven by its artifact, never by the command:**
  - R-FLIP by the `#error` text in its log;
  - R-BENCH by `[wx-bench]` in its image;
  - R-BENCHC by the marker's absence behind the scan's anchor.
- R-BENCH and R-BENCHC share a build directory, so R-BENCHC must rebuild without the flag, and its scan reads the image
  it produced.
- C: is read before the builds. None runs under 30 GB.

**After L1-L5:** the whole host suite under WSL is registered before it runs, as W8 was, and so is the code PR's CI.
Nothing is pushed before the orchestrator's go.

### L1's first run: OFF, and the fix as ruled, registered before the code change (2026-11-11)

**The reading (`dce71406`, committed as it ran):**
- **L1 OFF.** Predicted `1 checks, 0 failures`, exit 0. Read: a Python traceback, exit 1.
  - CD4's definer scan ran `check_map_fetch.strip` over every file under `include/` and `src/`.
  - `include/OtaCerts.h` holds a C++ raw string literal (`R"CERTS(`), which the stripper cannot parse. It raised its own
    `Rig`, which the check did not catch.
  - **Two defects:** the scan could not read a valid C++ file, and **a crash read as a failure** (exit 1, a red)
    instead of as the rig (exit 2). The second is the 11-04 rule: a crash is an instrument failure, never a red.
- **L4 as predicted** (`3 checks, 0 failures`).
- **L2 read the rig** (exit 2), because its control could not run. The harness was right, but that is no reading of the
  plants.
- **L3 and L5:** not run.
- **My miss:** I predicted L1 without checking that the stripper I reused could read every file the new scan touches.
  Step A's check only ever fed it the map's own files.

**The fix, as ruled** (the orchestrator; its stamp is in its ledger):
1. **The definer scan is line-based, and no longer goes through the C++ stripper.**
   - In `include/` and `src/`: a directive line matching `^\s*#\s*define\s+BLIPSCOPE_MAP_STEP_C\b`.
   - In `platformio.ini` and `tools/`: `-D\s*BLIPSCOPE_MAP_STEP_C\b`, after removing `;` and `#` comments.
   - A directive must begin its line, so a `//` comment cannot match.
   - **The scan's known direction:** a matching line inside a raw string or a block comment is flagged as a definer.
     That false positive fails safe and visibly, as a red with the file named.
   - The header's refusal lines are still read through `strip()`, since `src/AircraftManager.h` parses.
2. **`main()` catches every exception and exits 2 with `RIG: <type>: <message>`.** No crash can read as a check failing.
3. **Three controls, none of them a {CD4}-set plant.** Each runs in its own copy, is asserted to have applied before it
   is read, and the real files' sha256 are compared before and after:

| control | the input, in the copy | predicted |
|---|---|---|
| **R-CD-RAWOK** | a new file `include/zz_rawok.h` holding a multi-line raw string literal, L1's input | exit 0, `1 checks, 0 failures`: the input that crashed the scan now reads clean |
| **R-CD-CRASH** | a multi-line raw string literal added to `src/AircraftManager.h`, the one file `strip()` still parses | exit 2, its output beginning `RIG: `, never exit 1 |
| **R-CD-DECODE** | a new file `include/zz_badbyte.h` holding the bytes `0xFF 0xFE`, so the line scan's read raises | exit 2, its output beginning `RIG: `, never exit 1 |

**Read before registering, without running the check:** all 411 files the scan reads (`include/`, `src/`, `tools/` by
extension, and `platformio.ini`) decode as UTF-8. So L1 cannot meet R-CD-DECODE's path by accident.

**The re-run, predicted (L1's first re-run; another stop on L1 goes to the owner):**

| run | predicted |
|---|---|
| **L1** `check_map_draw.py` | `1 checks, 0 failures`, exit 0 |
| **L2** `sabotage_map_draw.py` | control clean; S-CD-NOREFUSAL {CD4}; S-CD-STEPC {CD4}; `MAP DRAW SABOTAGES (2) AS PREDICTED`; then R-CD-RAWOK, R-CD-CRASH and R-CD-DECODE as the table above, `MAP DRAW CONTROLS (3) AS PREDICTED`; the real files unchanged |
| **L3** `--anchors` over every driver (42), and `--control` for the new one | every anchor present exactly once; the control matches its table |
| **L4** `check_map_fetch.py` | `3 checks, 0 failures` |
| **L5** | as registered above, after blipscope-p4-4a's heavy run ends: one heavy run at a time on the machine |

### The refusal's commit as run (2026-11-11): L1-L5 as registered, with a harness stop in L5

**The re-run of L1-L4** (the fix at `0372fdc2`; L1's first re-run), each as predicted:
- **L1:** `1 checks, 0 failures`, exit 0.
- **L4:** `check_map_fetch.py` `3 checks, 0 failures`.
- **L2:**
  - the control is clean;
  - S-CD-NOREFUSAL {CD4} and S-CD-STEPC {CD4}, `MAP DRAW SABOTAGES (2) AS PREDICTED`;
  - R-CD-RAWOK exit 0, `1 checks, 0 failures`;
  - R-CD-CRASH exit 2, `RIG: Rig: an unterminated literal …`;
  - R-CD-DECODE exit 2, `RIG: UnicodeDecodeError: …`;
  - `MAP DRAW CONTROLS (3) AS PREDICTED`, and the real files unchanged.
- **L3:** no anchor stranded, and the new driver's `--control` matches its table.
  - **The registration's label was wrong:** its "every driver (42)" counted **42 files: 41 drivers and
    `test/host/sabotage_common.py` (a library)**. The library prints nothing and exits 0.
  - Of the 41 drivers, 38 print "all present exactly once". The other three are the harness's own meta-controls
    (`anchors_control`, `controls_control`, `rig_control`), and each passes in its own form.
  - This is on record as a miscount in the registration's wording, not an off reading (the orchestrator's ruling).

**L5, the four builds.**
- **R-FLIP** (20:04:06-20:06:53Z): exit 1, and the log holds `src/AircraftManager.h:52:2: error: #error "step B draws the
  map without step C: …"` twice, once per translation unit that includes the header. **The refused build left no
  image:** there is no `firmware.bin` (and no `firmware.elf`) for `blipscope-s3-128`. As registered.
- **R-FLIPC:** `blipscope-s3-128-wxlook SUCCESS`, with its `firmware.bin` (1,958,752 B) written at 20:09:56Z, inside
  the run. As registered.
- **The harness stop, and my miss.**
  - The script's `Build` function wrote its start and end lines into PowerShell's pipeline, so its return value was
    [line, line, exit code].
  - R-FLIP passed its gate by luck (`-eq 0` filtered to nothing). R-FLIPC's success read as "did not build", and the
    script stopped (exit 4) before R-BENCH.
  - It is the same family as L1's crash: a gate trusted without being tested.
  - **Ruled:** R-FLIP and R-FLIPC stand on their artifacts, and R-BENCH and R-BENCHC run after the fix. This was L5's
    first stop.
- **The fix:**
  - `Build` writes its lines to the host and returns only `[int]$LASTEXITCODE`.
  - A gate self-test runs before any build: a stub that prints lines and returns 0 gated as success (True), and one
    that returns 1 gated as failure (False).
- **R-BENCH** (`blipscope-s3-128` with `-DWX_LOOK_AB`, 20:11:36-20:14:40Z): built (`Int32` 0). `check-no-bench-hooks.sh`
  on the saved image exits **1** with `FAIL: bench hook marker '[wx-bench]'`. As registered.
- **R-BENCHC** (unchanged, 20:14:40-20:17:40Z): built. The scan on the saved image exits **0**, `ok: no bench hook (8
  marker(s) checked, anchor present)`. As registered.
- **Read off the images, independently:**
  - `[wx-bench]` appears once in R-BENCH's image and not at all in R-BENCHC's;
  - the anchor `[build] env=` appears once in each;
  - the two images differ (1,934,592 B against 1,932,448 B), so R-BENCHC really rebuilt without the flag.
- Every build ran after blipscope-p4-4a confirmed the machine was clear, one heavy run at a time.

**So CB10's refusal runs, and its exemption cannot reach a customer.** A release flip without step C is refused at
compile time, the bench image still builds, and a slug-ful image carrying the bench macro is refused by the scan every
slug-ful image already goes through.

### The refusal's PR (ruled (a)): W9 and its CI, registered before the suite runs (2026-11-11)

**Ruled:** these five commits go up now as a small code PR (the refusal, CD4 and its driver, and the `run.sh` block; no
drawing code), so the guard is on `main` before any drawing path exists. Step B's drawing work then continues on a
branch from the new `main`.

**W9, the whole host suite under WSL.** `bash test/host/run.sh` runs in `wsl -d Ubuntu-24.04` (g++ 13.3) on this
branch's head, with no git call inside WSL (W8's lesson). The commit and a clean tree are read from Windows first.
Predicted:
- **exit 2, and the failed blocks are exactly W8's two:** "the same TU, for the board" (no xtensa toolchain in WSL) and
  "Weatherscope R0 and R0b", the rig (no python with Pillow).
- **38 drivers** in every driver pass.
  - The glob's 42 files, minus the four `run.sh` skips by name: the `sabotage_common.py` library and the three
    meta-controls.
  - That is W8's 36, plus #630's `sabotage_ota_model.py`, plus this branch's `sabotage_map_draw.py`.
  - **Anchors:** `38 driver(s) checked`, plus both board plants.
  - **Controls and the full pass:** `38 driver(s): 37 run, 1 skipped`, the skip being `sabotage_wx_towns.py`.
  - **Lint:** `38 driver(s), 0 flagged; 3 pinned line(s)`.
- **`sabotage_map_draw.py` passes in all four passes.** Anchors: 3 plants, 3 anchors. Control: 1 suite. Lint: not
  flagged. The full pass: exit 0, with **7** `AS PREDICTED` lines (the 2 plants, their summary, the 3 controls and
  their summary).
- **The new `MapDraw` block passes** (`1 checks, 0 failures`). So does `MapFetch` (`42 checks, 0 failures`, then
  `3 checks, 0 failures`), and so does every block not named above.
- **Anything else stops the reading for a ruling.** A block or driver off in the full pass is first run against
  `main`'s own tree (`c2cf7cfc`), to tell what this branch broke from what was already broken.

**The PR's CI, predicted: 34 check runs, 30 success, 4 skipped (`promote`, `publish`, `ship-alert`, `version`), 0
failed,** in four workflows (device-ids, firmware, no-traffic-captures, web-renderer). The PR touches `src/` and
`test/host/`, so the web renderer builds.
- **The baseline is #634's finished code PR run** (head `3de4367a3c51`: 34 = 30/4/0, the same four skips).
- **It postdates the last change to the job set.** The only workflow change since #634's base (`68dc169a`) is #633's
  device-ids change, which added steps, not jobs, and the SKU matrix has not changed.
- **The host-tests leg on ubuntu** reads `38 driver(s): 37 run, 1 skipped` in its driver passes (#634's read 37/36/1),
  and `MapDraw` `1 checks, 0 failures`.
- **The web renderer builds:** `BLIPSCOPE_MAP_FETCH` excludes it, so CB10's condition is false there. No CI env defines
  `WX_LAYER_SHIPS` without `WX_LOOK_AB`, since the release flip has not happened, so no firmware leg meets the
  `#error`.
- **Any other count, skip set or failure means a stop and a report,** with no re-run.

### W9 as run (2026-11-11): the first run OFF on the lint, then the re-run as predicted

- **W9's first run** (`1272d2c3`, 20:20:49-20:28:32Z): **OFF.**
  - There was a third failed block, "sabotage controls: every test/host/sabotage_*.py --control".
  - It runs `lint_driver_counts.py`, which read `38 driver(s), 1 flagged`. Its L2 flagged eight literal 1s in
    `sabotage_map_draw.py` against the driver's table count, `SOURCE_CHECKS = 1`.
  - None of the eight is a count: an anchor's uniqueness (twice), an exit-code tuple, two regex groups, a tuple index, a
    last-line index and an exit code.
  - Everything else read as predicted.
  - **My miss:** I predicted "0 flagged" without running the lint locally. It now runs locally after any driver change.
  - CI's host-tests leg runs the same block, so this would have been a CI failure.
- **The fix, ruled (A):** the control's table entry is `("line", f"{SOURCE_CHECKS} checks, 0 failures")`, derived
  from the one constant, so the count still lives in one place (`bf100133`). The lint locally read `38 driver(s), 0
  flagged; 3 pinned line(s)`, as predicted, and the driver's `--control` read the line, exit 0.
- **A known limit of L2, for the harness's owner** (the orchestrator raises it): L2 flags any integer literal equal to
  one of a driver's table counts. **With a count of 1 it cannot tell the count from a file's idiomatic 1s**, so any
  future driver with a count of 1 will meet this. Nothing in the lint was changed here.
- **W9's re-run** (`bf100133`, 20:30:16-20:37:45Z), **as predicted:**
  - exit 2, and `FAILED BLOCKS` lists exactly "the rig: Weatherscope R0 and R0b" and "failed: the same TU, for the
    board", with no other `FAIL` line;
  - anchors `38 driver(s) checked`; controls and the full pass `38 driver(s): 37 run, 1 skipped`; lint `38 driver(s),
    0 flagged; 3 pinned line(s)`;
  - `sabotage_map_draw.py` ok in all four passes: anchors 3 and 3, control 1 suite, lint ok, and the full pass with
    **7** `AS PREDICTED` lines;
  - `MapDraw` `1 checks, 0 failures`, and `MapFetch` `42 checks, 0 failures`, then `3 checks, 0 failures`.
- **The PR's CI prediction stands as registered above** (34 = 30/4/0 from #634's run). The fix touches only the
  driver, which CI's host-tests leg reads.

### Step B's drawing work, registered before code (2026-11-11): the place-name counts (CB6)

**Status:** #650 merged (`337732be`), so the refusal and CD4 are on `main`. The drawing work goes on
`feat/map-client-step-b-draw`, from `main` at `ed452654`. This registration is its first commit. No place-name code
exists yet.

**The rule, as ruled (Q-MB3 to Q-MB6), stated exactly so the counts below can be checked against it:**
- **The list:** the package's PLACES, in file order, at most 256. More than 256 refuses the towns rather than cutting
  the list (Q-MB6). No peaks (Q-MC7 (b)).
- **The order:** the Weatherscope's `RankOrder`: population descending, a population of 0 ranking as 1,000, ties in
  file order. The label is `LabelText`'s: the name as written, one trailing " city" dropped, at most 39 bytes.
- **The cap:** the Weatherscope's `Cap` at the view's radius in miles, as an int: 8 from 60, 6 from 30, 5 below
  (Q-MB3).
- **The anchor:** format section 9 with the view's own centre, then `radarproj::Project`'s rotation (x = C + u cos +
  w sin, y = C - u sin + w cos), in double, then `int()` truncation, as `PlaceTown`.
- **The centre:** an anchor within `Inner` (24.4 px) of (120, 120) is skipped, and every box's nearest point must be
  at least 24.4 px out.
- **The boxes, inclusive:**
  - right `{ix-1, iy-4, ix+4+w, iy+4}`, left `{ix-5-w, iy-4, ix+2, iy+4}`, with `w` = 6 × the label's bytes - 1;
  - the side away from the centre is tried first (left when x < 120);
  - every corner within 116 px of (120, 120);
  - 4 px of air from every obstacle and every placed label;
  - **no y band**: the radar has no strips.
- **The obstacles (Q-MB4):**
  - each airport in the view box: its mark `{x-2, y-2, x+2, y+2}` and its code `{x+5, y-3, x+5+6n-1, y+4}`. The
    airports are culled and projected as `DrawAirports` does (`radarproj::Project`, float);
  - the three ring labels: x 123, y 14 / 44 / 83, with the view's ring values as `DrawRadarCircles` formats them;
  - the four cardinals: r 112, `lroundf`, rotated by `radarUpDeg`, box `{px-3, py-4, px+2, py+3}`;
  - the screen-indicator row: `{99, 221, 140, 227}`, the four carousel dots at y 224.

**The independent scratch reading.** `places_ref.py`, in the map client's scratch folder, sha256
`ab0cf5a713696da8a95d7b4627e4d4de0da6c82a784ef316f5ca48e8a48a12ac`, is written from the format doc and the rule above,
never from the C++ that will implement it:
- it decodes PLACES itself (LEB128 varints, zigzag svarints, u8-length text);
- it rebuilds the device's View per ladder step at the default 100 mi, in float32 where the device's chain is float;
- it projects the airports with `Project`'s float order of operations;
- it prints counts only.

Its inputs, by sha256: `kjac.bmpk` `eda8dc42bd12…a75ce`, `ksfo.bmpk` `30aa043a6152…fd6ac`, `bend.bmpk`
`0e257a0a47a1…ef0fd`, and `include/Airports.h` `16647c22948a…c5d00` (the baked table, which stands in for the cloud
list that a device uses once it loads).

**The counts, frozen: placed labels per rotation, `radarUpDeg` 0, 45, … 315:**

| fixture (places) | 5 mi | 10 mi | 25 mi | 50 mi | 100 mi |
|---|---|---|---|---|---|
| **KJAC** (42) | 0 0 0 0 0 0 0 0 | 0 0 0 0 0 0 0 0 | 2 2 1 2 2 2 1 1 | 6 4 3 3 6 4 3 3 | 8 8 7 8 8 6 6 8 |
| **KSFO** (137) | 3 3 4 5 3 3 2 3 | 4 5 5 3 5 5 5 5 | 5 5 5 5 5 5 5 5 | 6 6 6 6 6 6 6 6 | 8 8 8 8 8 8 8 8 |
| **Bend** (9) | 0 0 0 0 0 0 0 0 | 0 0 0 0 0 0 0 0 | 1 1 1 0 1 1 1 0 | 3 4 4 3 3 4 4 3 | 3 3 4 4 3 4 4 2 |

- **In all, 380 labels** over the 120 frames.
- **Why towns were refused,** summed over the 8 rotations (in the centre / off the disc / blocked on the disc):

| step | KJAC | KSFO | Bend |
|---|---|---|---|
| 5 mi | 0 / 336 / 0 | 0 / 1047 / 14 | 8 / 64 / 0 |
| 10 mi | 0 / 336 / 0 | 6 / 859 / 30 | 8 / 64 / 0 |
| 25 mi | 0 / 320 / 3 | 5 / 293 / 56 | 8 / 48 / 10 |
| 50 mi | 0 / 264 / 39 | 10 / 89 / 100 | 8 / 8 / 28 |
| 100 mi | 16 / 24 / 216 | 82 / 16 / 227 | 24 / 0 / 21 |

  Only towns considered before the cap was reached are counted.
- **Airports in the view box** (the same at every rotation): KJAC 0 at every step; KSFO 1 / 2 / 3 / 3 / 4; Bend 0 /
  1 / 2 / 2 / 3.
- **No town anchor lies within 1e-6 px of an integer,** so no frame's count rests on how a truncation rounds.

**What CB6 will hold, registered with the counts:**
- **Every one of the 120 frames' placed count equals this table.**
- Every frame holds the properties registered at #644 (≤ the cap, rank order, the centre, the corners, 0 overlaps
  with an obstacle or another label).
- The plants are as registered (S-CB-AIRPORT, S-CB-FILEORDER, S-CB-NOCAP, S-CB-NORTHUP). Each plant's red set is
  computed by this same scratch reading under the plant, before its driver runs.
- **What a different count means:**
  - a frame off by any number stops the step for a ruling;
  - the first thing read is that frame's anchors and obstacle boxes against this reading's, to find where the two
    disagree;
  - the code is not changed to make a count match.

### `MapPlaces.h` (CB6-CB8), registered before its code (2026-11-12)

**The orchestrator's addition, frozen here as a named outcome:**
- **"THE SCRATCH READING ITSELF IS WRONG":** a CB6 count differs, and that frame's anchors and obstacles show that the
  reader, not the code, deviates from the rule.
- **That is a stop for a ruling too.** It is never a quiet fix to the reader followed by a re-grade.

**What gets built:**

| part | what |
|---|---|
| `include/AirportBoxes.h`, pure, every radar build | `radarbox::Airport(x, y, codeLen, &mark, &code)`: mark `{x-2, y-2, x+2, y+2}`, code `{x+5, y-3, x+5+6n-1, y+4}`, inclusive. **`DrawAirports` draws from it** (`drawRect(mark.x0, mark.y0, 5, 5)`, `drawString(code, codeBox.x0, codeBox.y0)`), so the names and the drawer read one rule (CB8) |
| `include/MapPlaces.h`, pure, under the map gate | `mapplaces::List` (at most 256 `Town`s: qx, qy, pop and a 40-byte label); `Load(pk, &list)`, which refuses the towns past 256 and never truncates; `Rank`; `Cap`; `Place(list, maplayer::Affine, radiusMi, fixed, nFixed, out)`, which returns the placed labels (`town`, `side`, `box`, `tx`, `ty`, `ix`, `iy`); and the three other obstacle sets: ring labels, cardinals and the indicator row, as registered |
| anchors | `maplayer::AffineFor`'s **double** coefficients: x = ax + bx·qx + cx·qy. The same projection `Build` uses, with the same rotation |

**The tests:**
- **`test/host/test_map_places.cpp`, 32 cases, against `test/fixtures/wx`.** It prints `32 checks, 0 failures`; exit 0
  all pass, 1 any fail, 2 the rig.
  - **15 count rows:** `CB6 <kjac|ksfo|bend> <5|10|25|50|100>mi`. Each row is red if any of its 8 rotations' counts
    differs from the table.
  - **7 property rows:**
    - `CB6 cap`: ≤ Cap.
    - `CB6 rank`: every town that the true rank considers before the cap, that is not placed and not in the centre,
      fails both sides against the obstacles and the labels placed for towns ranked above it.
    - `CB6 centre`.
    - `CB6 corners`.
    - `CB6 airports`: 0 boxes within 4 px of a mark or code.
    - `CB6 fixed`: 0 boxes within 4 px of a ring label, cardinal or the indicator row.
    - `CB6 labels`: 0 pairs within 4 px.
  - **`CB6 256`:** a synthetic package with 256 places is accepted, and one with 257 is refused with 0 towns.
  - **`CB6 eq`:** `Rank`, `Inner`, the centre test and the box geometry equal the Weatherscope's (`WxScreens.h`) over
    every case they share, as CM11 holds the line.
  - **8 rows `CB7 r<0..315>`:** at that rotation, over every fixture, step and town, the anchor is within 0.5 px of
    `maplayer::ToScreen`'s float mapping and within 1 px of `radarproj::Project`.
- **`check_map_draw.py` gains two source rows,** so `SOURCE_CHECKS` becomes 3: CD4, CB8 and CB8b.
  - **CB8:** `DrawAirports` calls `radarbox::Airport` and draws at its boxes, and `MapPlaces.h` builds the airport
    obstacles from the same function.
  - **CB8b (Q-MB9):** the three other drawers still hold the exact expressions the names transcribe:
    - `DrawRadarCircles`: `CENTRE + 4`, `CENTRE - ringPx[i] + inset[i]`, the insets `{RING_LABEL_OUTER_INSET, 3, 3}`
      with `RING_LABEL_OUTER_INSET = 14`, the `value < 10.0f ? 1 : 0` decimals, and `labelR = CENTRE - 7`;
    - `DrawScreenIndicator`: `y = SCREEN_SIZE - 16` and `cx - (shown - 1) * 6 + slot * 12`.
- **A new driver, `sabotage_map_places.py`,** with each red set computed by `places_plants.py`. That is the same
  reading: it execs `places_ref.py`'s registered text, sha256-checked. Its control reproduced the registered table
  exactly. Its own sha256 is `81ad9442d392571929b8f3ac46a8fa8361666a41fd4076c7a4341b773d263a76`.

| plant | red set |
|---|---|
| **S-CB-AIRPORT** (airports not obstacles) | {CB6 airports, CB6 rank, CB6 bend 25mi, CB6 bend 50mi, CB6 bend 100mi} |
| **S-CB-FILEORDER** (file order) | {CB6 rank, CB6 kjac 50mi, CB6 kjac 100mi} |
| **S-CB-NOCAP** (no cap) | {CB6 cap, CB6 kjac 50mi, CB6 kjac 100mi, CB6 ksfo 10mi, CB6 ksfo 25mi, CB6 ksfo 50mi, CB6 ksfo 100mi} |
| **S-CB-NORTHUP** (north-up anchors) | {CB6 rank, CB6 kjac 25/50/100mi, CB6 ksfo 5/10mi, CB6 bend 25/50/100mi, CB7 r45, r90, r135, r180, r225, r270, r315}: 16 |

- **And in `sabotage_map_draw.py`** (it holds the source plants):
  - **S-CB-SECONDPATH:** `DrawAirports` draws its code at `x + 6` without the helper: {CB8}.
  - **S-CB-PINS:** `RING_LABEL_OUTER_INSET` becomes 13: {CB8b}.
  - Its table becomes `("line", f"{SOURCE_CHECKS} checks, 0 failures")` with `SOURCE_CHECKS = 3`.
- **`run.sh`** gains a `== MapPlaces ==` block.
- **The driver count becomes 39** (40 runs, 1 skipped).

**Predicted for the first run:**
- `test_map_places.cpp` reads **32 checks, 0 failures** under g++ 13.3 (WSL, as CI).
- `check_map_draw.py` reads **3 checks, 0 failures**.
- Both drivers read as their tables.
- **MinGW 5.1** (i686 x87, the Windows default for the drivers) is predicted equal.
  - The anchors are double, and none lies within 1e-6 px of an integer.
  - **The named risk** is the airports' float `Project`: x87 excess precision could move a truncation.
  - A MinGW-only difference is the Q-MC13 family: a stop and a report, never a fix by guess.
- **Anything else stops the step.** The first read is the differing frame's anchors and obstacles, as registered.

**Q-MB9, for the ruling, before CB8b's code:**
- CB8 as ruled shares a helper for the airport boxes only. The names also avoid the ring labels, the cardinals and the
  indicator row, so their positions are a second path re-derived from `DrawRadarCircles` and `DrawScreenIndicator`.
- **(a)** Move those three drawers onto helpers too. That moves shipping draw code, and the ring labels' Arduino
  `String(float, decimals)` formatting is not `snprintf`'s at every value, so the helper's text could differ from what
  the drawer prints.
- **(b)** Leave the drawers as they are, and pin their expressions with CB8b, a source check that runs.
- **I recommend (b).** It changes no shipping drawing, and any change to those expressions fails the build until the
  names follow.

### Q-MB9's ruling, verbatim (the orchestrator, 2026-11-12 00:11:25Z), and CB8b as ruled

> dd8418b0e013 is READ. The parts, the 32 cases and the plants' red sets (computed by executing the registered reader,
> sha-checked, with a control reproducing the table) are good, and so is naming the x87 MinGW risk in advance as a stop.
> Q-MB9: (b). Leave DrawRadarCircles and DrawScreenIndicator as they are, and pin their exact expressions with CB8b, a
> source check that fails the build if they change before the names follow.
> - Moving shipping draw code onto helpers here risks a visible text change (String(float) vs snprintf) for no user
>   gain. A check that runs is the strong form of "the names follow the drawers".
> - S-CB-PINS {CB8b} is its failing control, and good.
> - Make CB8b pin the VALUES the names use too (x 123, y 14/44/83, r 112, the indicator box {99,221,140,227}), not only
>   the expressions. That way a changed constant fails as surely as a changed formula.
> Order: push dd8418b0 after #659's CI at f6d214a5 has read (holding to avoid cancelling it is right). Then MapPlaces.h's
> code. Predictions must be public before the code exists.

**CB8b, as ruled, holds both ends:**
1. **The drawers' expressions,** exactly as registered:
   - `CENTRE + 4`, `CENTRE - ringPx[i] + inset[i]`, insets `{RING_LABEL_OUTER_INSET, 3, 3}` with
     `RING_LABEL_OUTER_INSET = 14`, `value < 10.0f ? 1 : 0`, `labelR = CENTRE - 7`;
   - `y = SCREEN_SIZE - 16` and `cx - (shown - 1) * 6 + slot * 12`.
2. **The values `MapPlaces.h` uses,** each equal to what those expressions give, with `CENTRE` = 119, `SCREEN_SIZE` =
   240, four carousel dots, and radii 3 and 2:
   - ring-label x 123;
   - y 14, 44, 83;
   - cardinal r 112;
   - the indicator box `{99, 221, 140, 227}`.
   - `check_map_draw.py` derives each value from the drawer's own text and compares it with the constant in
     `MapPlaces.h`.
- **Its plants, both in `sabotage_map_draw.py`:**
  - **S-CB-PINS:** `RING_LABEL_OUTER_INSET` becomes 13: {CB8b}. The expression changes, so the derived y no longer
    equals 14.
  - **S-CB-PINVAL (new):** `MapPlaces.h`'s indicator box starts at x 100, not 99: {CB8b}.
- **S-CB-PINVAL turns 0 rows of `test_map_places.cpp` red.** `places_plants.py`, re-run with this one plant added
  (sha256 now `8225a0d3724cde3378fd83ce6f85ecdefa2b60e0baa4c94c3643eed1a2c12eed`), read 0 rows. Its control and the
  four earlier plants' red sets are unchanged. **So the value pin is the only thing that catches a changed constant,**
  which is the ruling's point.

### CB7's first run OFF, its diagnostic, and ruling (A): re-registered before the re-run (2026-11-12)

**The first run** (the code committed as `5041d152` before it ran; `test_map_places.cpp` under g++ 13.3):
- **32 checks, 4 failures:** `CB7 r45`, `r135`, `r225` and `r315`.
- **Everything else passed,** including all 15 count rows. So every one of the 120 frames matched the independent
  reading.

**The diagnostic** (approved 00:37:40Z). It ran a scratch copy, never the committed test, and printed counts and
magnitudes only:

| per diagonal rotation | vs `ToScreen` (tol 0.5): over / max \|Δ\| | vs `Project` (tol 1.0): over / max \|Δ\| |
|---|---|---|
| on the disc (≤ 120 px) | 0 / 0.000017-0.000018 | 0 / 0.998908-0.999518 |
| off, 120-1,000 px | 0 / 0.000073-0.000088 | 2 / 1.001297-1.001359 |
| off, 1,000-5,000 px | 0 / 0.000153-0.000191 | 1 / 1.002482-1.002590 |
| off, over 5,000 px | none there | none there |

- **By the frozen table this is outcome (iii).**
  - Every miss is off the disc, but against `Project`, not `ToScreen`, so (i) does not hold.
  - No miss is on the disc, so (ii) does not hold either.
- **What the numbers show:**
  - `Project` returns an int truncation of a FLOAT result. When the double anchor is just past an integer and
    `Project`'s float lands just short of it, the two differ by 1 plus `Project`'s float error. That error grows outward
    (1.0013, then 1.0025).
  - The on-disc maximum, 0.9995, is the same effect inside the bound by 0.0005 px.
- **My miss:** CB7 compared a DOUBLE anchor against an INT truncation of a float, so "≤ 1" was never an exact bound. It
  is a design flaw in the check, not a defect in the code. I also registered the tolerance without checking how far off
  the screen the fixtures' towns land.

**The ruling, verbatim (the orchestrator, 00:39:14Z):**

> RULED (A): CB7's Project half compares what is DRAWN, the label's integer anchor ((int)x, (int)y) against Project's
> int result, |Δ| ≤ 1 per axis. That bound is the exact property of comparing two truncations of nearly equal values,
> not a tolerance picked from data. The ToScreen half stays at ±0.5. (B) is rejected (margin, not guarantee), and so is
> (C) (chosen after the data).
> Conditions:
> 1. A re-registration as a docs commit, public before the re-run, quoting this ruling and the diagnostic's numbers.
> 2. A NEW plant that proves the new half can fail: the drawn anchor offset by 2 px (an int) must turn CB7 red at every
>    rotation. Freeze its red set from the reading, not assumed.
> 3. Re-derive S-CB-NORTHUP's CB7 red set under (A) by the reading, as you said.
> 4. Then re-run from the start: the suite, MinGW, check_map_draw.py and the drivers, as registered. This is CB7's first
>    re-run; a second off reading goes to me and on to the owner.

**CB7 under (A),** per rotation, over every fixture, step and town:
- **The `ToScreen` half, unchanged:** `|anchor - maplayer::ToScreen| ≤ 0.5` on each axis.
- **The `Project` half:** `|(int)x - Project's x| ≤ 1` and `|(int)y - Project's y| ≤ 1`, where (x, y) is the anchor, as
  the label draws it. It keeps the same 1e5 guard as before.

**Frozen from the reading** (`places_plants.py`, now sha256
`bff74aae42f10a30e9baba34b7c593e71461e9bae769047e839001f390377794`):
- It now grades CB7 under (A) per town, with the true double anchor, `Project`'s int result from its float32 replica,
  and each plant's anchor. The true anchor stands in for `ToScreen`'s float path, which the diagnostic measured within
  0.0002 px of it.
- **Its control still reproduces the registered table, and the unplanted CB7 is green at all 8 rotations.**
- **S-CB-NORTHUP, re-derived:** unchanged. 16 red: {CB6 rank; CB6 kjac 25/50/100mi; ksfo 5/10mi; bend 25/50/100mi;
  CB7 r45, r90, r135, r180, r225, r270, r315}.
- **S-CB-ANCHOR2 (new):** `Anchor`'s x gets + 2.0, so the drawn (int) anchor sits 2 px to the right. 13 red: {CB6
  rank; CB6 kjac 50mi; ksfo 5mi; ksfo 10mi; bend 50mi; CB7 r0, r45, r90, r135, r180, r225, r270, r315}.
  - **CB7 is red at every rotation,** which is the condition.
  - The counts move too, because the shifted anchors place differently.
- S-CB-AIRPORT, S-CB-FILEORDER, S-CB-NOCAP and S-CB-PINVAL: unchanged.

**The re-run, from the start, after the test's CB7 change and the driver's new plant are committed:**
- the suite under g++ 13.3: **32 checks, 0 failures**;
- MinGW 5.1: the same, with the named x87 risk. A MinGW-only difference is a stop;
- `check_map_draw.py`: **3 checks, 0 failures**;
- `sabotage_map_draw.py`: 5 plants and 3 controls, as its table;
- `sabotage_map_places.py`: 5 plants, as their sets above;
- `lint_driver_counts.py`: **39 driver(s), 0 flagged**;
- `--anchors` and `--control` for both drivers.

This is CB7's first re-run. A second off reading goes to the orchestrator and on to the owner.

### The (P1) CI, the re-run, and a driver's instrument failure (2026-11-12)

**(P1), ruled:** `5041d152` and `5a46ad05` were pushed together, so the re-registration was public before the re-run.
The CI on that head was predicted RED, exactly:
- **The tally, as frozen:** 34 = 27 success / 4 skipped / 3 failed. The failures were exactly `host-tests`,
  `host-tests (ubuntu-24.04)` and `host-tests (ubuntu-26.04)`.
- **In each leg's log, as frozen:**
  - MapPlaces `32 checks, 4 failures`, with FAIL CB7 r45, r135, r225 and r315;
  - FAILED BLOCKS exactly MapPlaces, the sabotage controls and the drivers in full;
  - MapDraw 3 / 0, lint 39 / 0, 39 drivers, and `sabotage_map_draw.py`'s 10 AS PREDICTED lines.
- **The tested merge** (from the checkout log): `f3340e7`, `5a46ad05` into `af871ecb`.
- **A correction, my miss:** `sabotage_map_places.py`'s two lines (the control off, the full pass the rig) matched
  in OUTCOME but not in CAUSE.
  - I had predicted both from the old CB7.
  - The logs say "FAIL test_map_places: did not compile". Its copy lacked `test/fixtures/wx/glcdfont.c`.
  - I called it a match without reading those lines' detail.

**The re-run from the start** (`ef99949f`: CB7 under (A), S-CB-ANCHOR2):
- **Steps 1-4, as predicted:**
  - the suite under g++ 13.3: 32 / 0;
  - MinGW 5.1: 32 / 0 (the x87 risk did not occur);
  - `check_map_draw.py`: 3 / 0;
  - `sabotage_map_draw.py`: 5 plants and 3 controls, exactly.
- **Step 5, an INSTRUMENT failure** (ruled so; not a reading, and not CB7's second): the driver's control could not
  compile, because the same `glcdfont.c` was missing from its copy.
  - The driver, a second path, missed what its sibling `sabotage_map_layer.py:121` establishes.
  - **The full comparison, ruled:**
    - The sibling's `make_tree` copies `include/*.h`, `src/weatherscope/*.h` and `test/fixtures/wx/glcdfont.c`.
    - Mine copied the two directories whole, a superset, and not the font.
    - Of the four parent-relative includes the suite can reach, three point into `src/weatherscope/` and the fourth
      is the font.
  - **The fix,** `0eed7772`, committed before the run: the font is copied exactly as the sibling does.
    `git diff --stat ef99949f 0eed7772` touches only the driver (+6), so steps 1-4 carry.
- **Steps 5-7 from step 5, as predicted:**
  - `sabotage_map_places.py`: the control clean (32 cases), and AIRPORT 5, FILEORDER 3, NOCAP 7, NORTHUP 16 and ANCHOR2
    13, each exactly its set. The sets were computed by the independent reading before any code;
  - the lint: `39 driver(s), 0 flagged; 3 pinned line(s)`;
  - `--anchors` 6 / 6 for each driver, and both `--control`s as their tables.

**The CI at this head, predicted** (code paths, so the code-PR set):
- **34 = 30 success / 4 skipped** (`promote`, `publish`, `ship-alert`, `version`) / **0 failed.** The baseline is
  #650's finished code PR run.
- **Each host-tests leg** reads:
  - MapPlaces `32 checks, 0 failures`;
  - MapDraw `3 checks, 0 failures`;
  - anchors `39 driver(s) checked`, and both driver passes `39 driver(s): 38 run, 1 skipped`;
  - lint `39 driver(s), 0 flagged; 3 pinned line(s)`;
  - `sabotage_map_places.py` ok with **6** AS PREDICTED lines (5 plants and the summary), and `sabotage_map_draw.py`
    ok with **10**;
  - `ALL HOST TESTS PASSED`.
- The tested merge is read from a checkout log.
- **Any other reading stops,** and the detail of every failing line is read before anything is called a match.

### MapCompose.h (CB1-CB5, CB9, CB11): the run, registered before its code (2026-11-12)

The GO, verbatim (the orchestrator, 01:25:29Z):

> GO for MapCompose.h's run registration, as you listed it: the steps, each plant's red set, exact row names and counts,
> and the "anything else" branch. Commit and push it before any code, and send me the sha. No merge of #659 yet: it
> merges when step B's drawing work is whole, with my own CI reads at its final head.

**What `include/MapCompose.h` holds** (pure, `namespace mapcompose`, compiled only under `BLIPSCOPE_MAP_FETCH` on the
device, as step A is):
- **The key**, everything a rebuild reads:
  - the six `wxlayer::View` fields the weather layer is built from (CD3), compared with `!=` exactly as the weather
    layer's own stale test compares them (`AircraftManager.cpp:3599-3601`);
  - `radiusMi`, the ladder step (`Cap` and `Inner` read it);
  - `pkSeq`, which the caller changes whenever a package is accepted;
  - `airportsGen`, which the caller changes whenever the airport list or its filter changes;
  - the eight other fixed boxes **by value** (three ring labels, four cardinals, the indicator row). A ring label's
    width follows the distance-unit setting, which no view field carries.
- **No theme or layer mask in the key.** Both are compile-time constants today (`maplayer::kThemes`, one row), and the
  rebuild takes neither as a parameter. A runtime theme would have to change the signature, which is a visible act.
- **The buffers, all or none,** at the first accepted package, through a caller-given allocator (the device's is the
  PSRAM `heap_caps_malloc`). There are four, all attempted before any is checked:
  - the layer (the map with its names, in the canvas's byte order), 115,200 B;
  - the composite, 115,200 B;
  - `Build`'s index image, 57,600 B;
  - the scratch: 32 B per vertex for `maxVertices + 1`, plus 128 airports' boxes (4,096 B).
  - **One failure frees every buffer** and refuses the map (`refused`, which the wiring logs as `mapAllocFailed`). The
    allocation is tried once per boot.
- **The rebuild,** in S3's order:
  1. `Build` writes the layer, through a caller-given function pointer with `maplayer::Build`'s signature, so the
     tests count the calls from outside;
  2. every pixel is byte-swapped (`bswap16`);
  3. the names: `mapplaces::Place` with the fixed boxes and the airport boxes, then each label drawn as WxScreens.h:788-789
     draws one (the 3 x 3 dot, then `wxraster::Text` with a 1 px halo), through a `wxraster::Sink` over the layer.
     Colours: the place grey and the land halo (Q-MB5).
  - The caller fills the airport boxes only when a rebuild runs. More than 128 airports **refuses the names** for that
    rebuild (`namesRefused` counts it). A cut obstacle list would let a name cover an airport nobody listed.
  - **A `Build` that returns false** records its key like any attempt, so the map stays off until the key changes. It
    is never retried per frame.
- **The compose:** the layer copied, then every non-zero weather pixel over it, and 0 off the disc
  (`wxlayer::OffDisc`). It runs when the layer was rebuilt, the weather's generation changed, or the weather became
  shown. It never runs per frame.
- **The frame:** it returns the buffer the base draw copies, or `nullptr` for today's path. The returns:
  - no package, refused, or no layer built: `nullptr`;
  - weather shown: the composite;
  - weather not shown: the layer.
  - **"Ready" is a built layer, not allocated buffers.** That is why buffers alone change nothing on screen.

**The rows** (`test/host/test_map_draw.cpp`, run against the fixtures as `test_map_places` is). **24 checks.** No row
prints a place name, an anchor or a coordinate (CD2's rule).

| row | check |
|---|---|
| `CB1 compose` | synthetic layer L (map pixels M, name pixels P) and weather W, W non-zero across the disc's edge and over some name pixels. Every on-disc pixel is W where W ≠ 0, else L. Every off-disc pixel is 0 |
| `CB1 names` | the rebuild with a synthetic `Build` (M on the disc, 0 off it) and one placed town. The layer is the label's pixels where the label draws (drawn alone on a zeroed buffer through the same sink), M elsewhere on the disc, 0 off it. **M is byte-symmetric** (`0x6B6B`), so CB1 reads order alone and CB2 reads the byte order |
| `CB2` | `Build` over KJAC at 100 mi with the weather shown and all zero, rebuilt with the names and again with an empty list. Every pixel the two composites share equals `bswap16` of `Build`'s own output, called directly |
| `CB3 alloc` / `CB3 build` / `CB3 today` | no package ever, 100 frames with the weather shown and 100 without: **0** bytes live at the allocator, **0** `Build` calls, and every frame returns `nullptr` with the canvas untouched (a canary of today's bytes: the weather layer when shown, black when not) |
| `CB4 homeLat`, `CB4 homeLon`, `CB4 rLat`, `CB4 rLon`, `CB4 rotSin`, `CB4 rotCos`, `CB4 radiusMi`, `CB4 package`, `CB4 airports`, `CB4 fixed` | from a built state, that one input changed, then one frame: exactly **1** `Build` call. `fixed`: one ring label one character wider (a unit change) |
| `CB4 steady` | 1,000 frames with nothing changed: **0** `Build` calls |
| `CB4 buildfail` | a `Build` that returns false: 1 call, then 1,000 frames: **0** more, each returning `nullptr` |
| `CB5` | the weather's generation changed, nothing else, one frame: **1** compose and **0** `Build` calls |
| `CB9 layer` / `CB9 composite` / `CB9 index` / `CB9 scratch` | the allocator fails that buffer, counting from the Map's creation. After a package and frames: **0** bytes live, `refused` set, **0** `Build` calls, and every frame returns `nullptr` with the canary untouched |
| `CB11 airports 129` | KJAC at 100 mi with 129 airports' boxes, all off the screen: `namesRefused` is 1, and the layer equals the same rebuild with an empty list, byte for byte |

**The plants** (`test/host/sabotage_map_compose.py`, in a copy of `include/MapCompose.h`, as `sabotage_map_places.py`).
Each set was read node by node against the rows above. Next to each set is why it reaches no other row:

| plant | the change | red, exactly | why no other row |
|---|---|---|---|
| **S-CB-ORDER** | the compose writes the layer over the weather | {CB1 compose} | CB2's weather is all zero; CB5 counts calls |
| **S-CB-NAMESUNDER** | the names drawn before `Build` | {CB1 names} | `Build` writes every pixel, so the two CB2 rebuilds are identical and every pixel is compared and equal; CB11's names are refused anyway |
| **S-CB-NOSWAP** | the swap removed | {CB2} | CB1's M is byte-symmetric; CB11 compares two rebuilds with each other |
| **S-CB-EAGER** | the three fixed-size buffers (layer, composite, index) allocated at the Map's start. The scratch, sized by the package's `maxVertices`, stays at the first package | {CB3 alloc} | "ready" is a built layer, so CB3's frames still return `nullptr`. CB9's failures count from the Map's creation, so each still fires and frees all, at the start or at the package |
| **S-CB-NOROT** | `rotSin` and `rotCos` left out of the key | {CB4 rotSin, CB4 rotCos} | every other row changes a different input, or none |
| **S-CB-ROTONLY** | the key compares the rotation alone | {CB4 homeLat, CB4 homeLon, CB4 rLat, CB4 rLon, CB4 radiusMi, CB4 package, CB4 airports, CB4 fixed} | the first build and CB5 need no key change; steady changes nothing |
| **S-CB-PERFRAME** | the stale test always true | {CB4 steady, CB4 buildfail, CB5} | the per-input rows run one frame, which makes one call either way; CB3 and CB9 stop before the stale test |
| **S-CB-WXBUILD** | the weather's generation added to the map's key | {CB5} | no other row changes the weather |
| **S-CB-WXSTALE** | the weather's generation left out of the compose's test | {CB5} | CB2's compose follows its rebuild; CB1 calls the compose directly |
| **S-CB-HALF** | the failure branch keeps the buffers that succeeded | {CB9 layer, CB9 composite, CB9 index, CB9 scratch} | all four are attempted before the check, so three succeed in every case |
| **S-CB-RETRY** | a failed attempt's key not recorded | {CB4 buildfail} | every other `Build` succeeds |
| **S-CB-AIRTRUNC** | over 128 airports, the list cut to 128 instead of refusing the names | {CB11 airports 129} | no other row passes more than 128 |

**What changed from #644's table, and why:**
- **S-CB-PERFRAME's set gains CB5 and the new CB4 buildfail.** CB5 counts `Build` calls over a frame, and a per-frame
  rebuild makes one. #644's {CB4 steady} was written before the design.
- **S-CB-HALF keeps the buffers that succeeded; it does not draw with them.** Drawing with a missing buffer dereferences
  null, which is a crash: an instrument failure by the 11-04 ruling, not a red.
- **CB9 moves into this step.** The all-or-none allocation lives in MapCompose.h, and its host row needs nothing from the
  wiring.
- **The key gains `radiusMi` and the fixed boxes,** which the names read. **S-CB-ROTONLY** gives a plant to those two and
  to the six rows #644 left without one.
- **New:** `CB1 names` with S-CB-NAMESUNDER; CB5's compose half with S-CB-WXSTALE; `CB4 buildfail` with S-CB-RETRY; and
  CB11 with S-CB-AIRTRUNC.
- **Q-MB1's price gains 4,096 B** (the airport boxes in the scratch). CBF4's figure is computed with it.

**The run, in order. The code is committed before step 1, as the record:**
1. The suite under g++ 13.3 (`wsl -d Ubuntu-24.04`): **`24 checks, 0 failures`**.
2. MinGW 5.1: the same. A MinGW-only difference is a stop.
3. `check_map_draw.py`: **3 checks, 0 failures**, unchanged. MapCompose.h adds no draw region; CD1-CD3 come with the
   wiring.
4. `sabotage_map_compose.py`:
   - the control: 24 cases, all pass;
   - each of the 12 plants exactly its set above;
   - `real files unchanged: True`;
   - **13 AS PREDICTED lines** (12 plants and the summary).
5. `--anchors` over all **40** drivers, then `--control` for the new one, with its table `("checks", 24)`.
6. `lint_driver_counts.py`: **`40 driver(s), 0 flagged; 3 pinned line(s)`**.
7. `run.sh` in full: a new `MapCompose` block, `24 checks, 0 failures`; `40 driver(s) checked`; both driver passes
   `40 driver(s): 39 run, 1 skipped`; `sabotage_map_compose.py: 13 AS PREDICTED line(s)`; `ALL HOST TESTS PASSED`.

**Anything else is a stop:** a different count, a red outside a frozen set, a set missing a row, a compile failure, or a
crash. I stop at the first such reading, with no fix and no re-run, and report it with the failing lines' detail read.
A crash or a plant that does not compile is reported as an instrument failure, not a red. The code's CI is predicted
when the code is pushed.

**The CI for this docs commit, predicted** (it goes to #659's branch, so the code-PR set runs):
- **34 = 30 success / 4 skipped** (`promote`, `publish`, `ship-alert`, `version`) / **0 failed**, with the same job names
  as `6302be1e`'s run, the baseline.
- Each host-tests leg reads as it did there: MapPlaces `32 checks, 0 failures`, MapDraw `3 checks, 0 failures`, 39
  drivers, lint `39 driver(s), 0 flagged; 3 pinned line(s)`, 6 and 10 AS PREDICTED lines, `ALL HOST TESTS PASSED`.
- The tested merge is read from a checkout log. Any other reading stops.

### The ruling on the changes, and CB11's count (2026-11-12)

**The ruling, verbatim (the orchestrator, 01:35:57Z):**

> d814c31efbfa is read (on #659, docs +122, before code). The changes from #644's table are re-freezes before any run,
> each with its reason stated.
> - 1 (PERFRAME's set grows to {CB4 steady, CB4 buildfail, CB5}): ACCEPTED.
> - 2 (HALF keeps the good buffers and doesn't draw, so a planted crash isn't an instrument failure): ACCEPTED.
> - 3 (CB9 moves into this step): ACCEPTED.
> - 4 (the key gains radiusMi and the eight fixed boxes, by value): ACCEPTED. A ring label's width following the unit
>   setting is exactly the second-path catch, and S-CB-ROTONLY finally gives #644's six CB4 rows a plant.
> - 6 (+4,096 B in Q-MB1's price): ACCEPTED, with CBF4 computed with it.
> - 5: ACCEPTED for CB1 names, CB5's compose half and CB4 buildfail. CB11 needs ONE COUNT before code. ">128 airports
>   refuses the names" is right as a principle: a truncated obstacle list lies. But if it can happen in a real view,
>   it's a visible behaviour (no town names over a dense region), and that's the owner's to see. So, read-only from the
>   baked Airports.h, counts only: the MAXIMUM number of airports inside any view's culled area at the largest step
>   (100 mi, plus the cull margin), swept over the US (a grid of homes, spacing stated), and the count of grid homes over
>   128.
>   - If the max is under 128 with margin: CB11 stands as a guard that should never fire, and the record states the max.
>   - If any home exceeds it: stop, and send me the count and the regions. The buffer size (+32 B per box), or names
>     refused there, then goes to the owner.
> Then code, in the registered order.

**The count.** A scratch survey, read-only, printing counts only. Its verdict table was frozen in its header before the
run:

| verdict | if | then |
|---|---|---|
| V1 | the US max over both boxes ≤ 96 (three quarters of 128) | CB11 stands as a guard that should never fire on the baked table |
| V2 | 97 to 128 | inside the buffer but without margin: stop |
| V3 | any home over 128 | stop, with the cells |
| V4 | the control fails | the instrument is unreliable: stop |
| V5 | anything else | stop |

- **The cull** is the view box itself (`AircraftManager.cpp:5380-5381`); there is no extra margin. Two boxes were swept:
  - **A**, the default top of 100 mi, with each radius capped at 2.0° (`:892-893`);
  - **B**, the largest box the code allows: `MAX_DEGREES` = 2.0° on both axes, reached by any configured radius past the
    cap.
- **The grid:** 0.05° on both axes over CONUS, Alaska and Hawaii, 925,683 homes.
- **The control:**
  - the parse equals the compiler's own view of `Airports.h`: 329 entries, and both integer checksums equal;
  - every one of the 101 airports inside the regions counts itself at its own position;
  - box B is also solved exactly (some best box has an airport on its bottom edge and one on its left). That gives the
    maximum over any home on Earth.
- **The result:** the most airports in view is **8** in box A and **10** in box B. The exact solve for B gives **10**.
  **0** homes are over 128. **V1:** CB11 stands as a guard that should never fire on the baked table.

**The cloud path, recorded as information.** On the cloud feed, which the map needs, `cloudAirports` supersedes the baked
table.
- The Worker caps its reply at `MAX_RESULTS` = 60 for the whole configured radius (`proxy/src/airports.ts:23`, sliced at
  `:87`).
- The firmware's `ParseAirports` keeps every row it receives (`CloudFeed.cpp:261-286`).
- So "at most 60" there is a premise about the Worker, and CB11 is the device's own guard on that path.

### Q-MC14: the allocation per config, growth, and two missed prices, re-frozen before code (2026-11-12)

**The question** (sent at about 01:41Z, before any of the allocation's code). Laying out MapCompose.h's allocation turned
up three gaps in the registration above:
1. **The retry rule.** The registration said "tried once per boot". Step A already set the rule for the same thing: at a
   config change, `MapConfigure` drops the held package and clears `mapAllocFailed` ("a new config earns a new
   allocation attempt", `AircraftManager.cpp:3447-3451`). That is a second path missing what the first establishes.
2. **A larger later package.** The scratch is sized from the package's own `maxVertices`, which nothing bounds but the
   package's length. Sizing for the request's byte cap would cost megabytes.
3. **Two prices missed:**
   - the place list, `mapplaces::List`: 256 towns × 52 B = 13,320 B;
   - the obstacle array is 264 boxes (8 fixed and 256 for airports), 4,224 B, not 4,096.

**The ruling, verbatim (the orchestrator, 01:43:54Z):**

> Q-MC14 is RULED 1 + 2 + 3, as you recommend. Stopping on the allocation code to ask was right; all three were caught
> before code.
> 1. ONCE PER CONFIG, through mapcompose::Forget(m), called at the same line where step A clears mapAllocFailed. Your
>    registered "once per boot" was the second path missing what the first establishes. Corrected before code, so it's
>    a re-freeze, not a miss in the code.
> 2. GROW, ALL OR NONE: if an accepted package needs more than the scratch holds, release every buffer and reallocate at
>    the new price, with CB9's rule on failure. "The guard for a missed Forget, not the main path" is the right framing.
>    The alternative (no map after a location change) is rejected.
> 3. THE PRICES: scratch = 32 × (maxVertices + 1) + 4,224 + 13,320 B, all PSRAM, and the Map struct internal under 1 KB.
>    This supersedes the 4,096 I accepted at 01:35:57Z. CBF4 is computed with the new figure.
> The new rows and plants are as listed: CB9 forget with S-CB-NOFORGET, and CB9 grow with S-CB-NOGROW. That's 26 rows,
> 14 plants and 15 AS PREDICTED lines, and the driver table ("checks", 26).
> ORDER: commit this ruling verbatim plus the re-freeze as a docs commit, and PUSH it, together with cc814479 (CB11's
> record), before any of MapCompose.h's code is committed. Send me the pushed sha. Then the code, then the 7 steps.

**The re-freeze.** This supersedes the registration above wherever the two differ.

- **The memory:**
  - "the allocation is tried once per boot" becomes **once per config**. `mapcompose::Forget(m)` frees every buffer and
    clears the refusal. The wiring calls it at the line where step A clears `mapAllocFailed`.
  - **Growth:** if an accepted package needs more than the scratch holds (`maxVertices + 1` over the scratch's capacity),
    every buffer is released and allocated again at the new price, all or none, with CB9's rule on failure.
  - **The price:**
    - the layer, 115,200 B; the composite, 115,200 B; the index, 57,600 B;
    - the scratch, **32 × (maxVertices + 1) + 4,224 + 13,320 B**;
    - all PSRAM. The allocator returns memory aligned for a `double`.
    - The `Map` struct (the key, eight placed labels, flags) stays internal, under 1 KB.
  - These figures are held by `static_assert` in the header.
- **Two rows added:**

| row | check |
|---|---|
| `CB9 forget` | a refusal (a failure injected at the first allocation), then `Forget`, then a package: allocated at that package's price, and the frame returns a buffer |
| `CB9 grow` | a built map, then a package with twice the `maxVertices` and no `Forget`: the live bytes equal the new price, and the frame returns a buffer |

- **Two plants added:**

| plant | the change | red, exactly | why no other row |
|---|---|---|---|
| **S-CB-NOFORGET** | `Forget` leaves the refusal | {CB9 forget} | no other row calls `Forget` |
| **S-CB-NOGROW** | the growth check removed: `Build` refuses the short scratch, so the frame returns `nullptr` | {CB9 grow} | no other row passes a larger package |

- **No existing set moves:**
  - S-CB-EAGER's start-time refusal is cleared only by `Forget`, so `CB9 forget` stays green under it;
  - S-CB-HALF's kept buffers are freed by `Forget`'s release, and the live bytes are read after the second package;
  - S-CB-ROTONLY: a release clears the attempt, so the next frame rebuilds whatever the key compares.
- **Each row's live bytes are read against that row's own starting figure, and every row ends by releasing its Map.**
  So a plant that leaks in one row (S-CB-HALF) cannot reach a later one.
- **The totals:** **26 rows, 14 plants, 15 AS PREDICTED lines**, and the driver's table `("checks", 26)`.
- **The run's steps change only in their counts:**
  - the suite `26 checks, 0 failures` under g++ 13.3 and under MinGW;
  - `check_map_draw.py` 3 / 0;
  - the driver: control 26 cases, each of the 14 plants exactly its set, 15 AS PREDICTED lines;
  - `--anchors` over 40 drivers, and the driver's `--control` as `("checks", 26)`;
  - lint `40 driver(s), 0 flagged; 3 pinned line(s)`;
  - `run.sh`: `MapCompose` 26 / 0, `40 driver(s): 39 run, 1 skipped`, `sabotage_map_compose.py: 15 AS PREDICTED line(s)`,
    `ALL HOST TESTS PASSED`.

**The CI for this push, predicted** (docs only, on #659's branch, so the code-PR set runs):
- **34 = 30 success / 4 skipped** (`promote`, `publish`, `ship-alert`, `version`) / **0 failed**, with `6302be1e`'s job
  names;
- each host-tests leg as there: MapPlaces 32 / 0, MapDraw 3 / 0, 39 drivers, lint 39 / 0 / 3, 6 and 10 AS PREDICTED,
  `ALL HOST TESTS PASSED`.
- The tested merge is read from a checkout log. Any other reading stops.

### S-CB-WXBUILD's set, re-frozen before the run, and the CB2 / CB11 detail (2026-11-12)

**The finding.** Before any code ran, each of the 14 plants was read against the 26 rows as the test is written. One set
does not hold: **S-CB-WXBUILD** (the weather's generation added to the map's rebuild test) also reaches `CB4 buildfail`.
- When `Build` fails, nothing composes, so the composed generation stays 0.
- `CB4 buildfail` runs with the weather shown (generation 1), the device's usual state. Under the plant, the generations
  differ on every frame, so the failed `Build` is retried 1,000 times.
- The real code rebuilds only on the key, so the row reads the plant's reach, not a defect.
- The registered reason for WXBUILD, "no other row changes the weather", missed that a map never composed leaves the
  generation unequal.

**The detail.** CB2's and CB11's "again with an empty list" was first written as a key change (`airportsGen` + 1), which
S-CB-ROTONLY would reach. Both now rebuild directly at the same key (`mapcompose::Rebuild`), which matches the registered
text. CB2 reads the byte order and CB11 the refusal; neither reads the key.

**The ruling, verbatim (the orchestrator, 01:54:47Z):**

> (i), re-freeze S-CB-WXBUILD to {CB4 buildfail, CB5}. Keeping buildfail under shown weather (gen 1, the device's usual
> state) and letting the plant's reach show is right. (ii) would shape a row to dodge a plant, so it's rejected. Catching
> this by a node-by-node read before the run is the predict-by-applying-the-rule discipline working, and it isn't a miss:
> nothing ran.
> The CB2/CB11 detail is ACCEPTED: rebuilding at the same key matches the registered text, and it keeps S-CB-ROTONLY's
> reach where it was registered.
> The counts stay at 26 rows, 14 plants, 15 AS PREDICTED. The other 13 sets are as registered.
> FREEZE EVIDENCE: this ruling, with the set, is in my ledger at 01:54:47Z. Commit it verbatim into the record locally,
> before the code commit. Under the new standing rule, don't push while 082a56d8's run is in flight: push the docs + code
> together after that run has finished and been read, with the next push's prediction stated. Then the 7 local steps as
> registered.

**The sets, as they now stand** (all 14, for the run):

| plant | red, exactly |
|---|---|
| S-CB-ORDER | {CB1 compose} |
| S-CB-NAMESUNDER | {CB1 names} |
| S-CB-NOSWAP | {CB2} |
| S-CB-EAGER | {CB3 alloc} |
| S-CB-NOROT | {CB4 rotSin, CB4 rotCos} |
| S-CB-ROTONLY | {CB4 homeLat, CB4 homeLon, CB4 rLat, CB4 rLon, CB4 radiusMi, CB4 package, CB4 airports, CB4 fixed} |
| S-CB-PERFRAME | {CB4 steady, CB4 buildfail, CB5} |
| **S-CB-WXBUILD** | **{CB4 buildfail, CB5}** |
| S-CB-WXSTALE | {CB5} |
| S-CB-HALF | {CB9 layer, CB9 composite, CB9 index, CB9 scratch} |
| S-CB-RETRY | {CB4 buildfail} |
| S-CB-AIRTRUNC | {CB11 airports 129} |
| S-CB-NOFORGET | {CB9 forget} |
| S-CB-NOGROW | {CB9 grow} |

**The standing push rule** (the orchestrator, 01:52:24Z, after `d814c31e`'s run was cancelled by the next push):
- (a) wait for the previous head's run to finish before the next push;
- (b) when a push must come first, the next prediction registers the old head's cancelled legs and the aggregate's
  failure as the expected outcome, and only the new head is read in full.
- `d814c31e` stands as read, superseded: its ubuntu-24.04 leg read every frozen line, and its ubuntu-26.04 leg was
  cancelled by the firmware workflow's concurrency group. `082a56d8`'s run is the record for those docs.

### MapCompose.h's run, as read (2026-11-12)

**The code:** `3714af52`, committed before step 1. It adds `include/MapCompose.h`, `test/host/test_map_draw.cpp`,
`test/host/sabotage_map_compose.py` and run.sh's `MapCompose` block. It sits on `c095c41c` (S-CB-WXBUILD's re-freeze).

**Steps 1-6, every one as predicted:**

| step | read |
|---|---|
| 1. the suite, g++ 13.3 (WSL) | `26 checks, 0 failures`. CB2 compared 55,345 shared pixels (2,255 named) |
| 2. the suite, MinGW 5.1.0 | `26 checks, 0 failures`, with the same CB2 counts |
| 3. `check_map_draw.py` | CD4, CB8 and CB8b ok: `3 checks, 0 failures` |
| 4. `sabotage_map_compose.py` | `control (the real files): 26 cases, all pass`. Each of the 14 plants turned red exactly its set, "extra none; missing none" on every line: ORDER 1, NAMESUNDER 1, NOSWAP 1, EAGER 1, NOROT 2, ROTONLY 8, PERFRAME 3, WXBUILD 2, WXSTALE 1, HALF 4, RETRY 1, AIRTRUNC 1, NOFORGET 1, NOGROW 1. `real files unchanged: True`; `MAP COMPOSE SABOTAGES (14) AS PREDICTED`, so 15 AS PREDICTED lines |
| 5. `--anchors` and `--control` | `40 driver(s) checked`, 0 failed. The new driver: `14 plant(s), 16 anchor(s), all present exactly once` (two plants make two edits each). Its control: `test_map_draw: 26 checks, 0 failures, exit 0 (the table: checks 26)` |
| 6. `lint_driver_counts.py` | `40 driver(s), 0 flagged; 3 pinned line(s)` |

**Step 7's wording, corrected before it ran.** The registration (`d814c31e`) and the re-freeze (`082a56d8`) said step 7
would end in `ALL HOST TESTS PASSED`. Under WSL it cannot: the standing rulings of 11-05 and 11-06 say a WSL run.sh
prediction names its environment blocks, and both texts left them out. That was my miss, caught before the run.

**The ruling, verbatim (the orchestrator, 01:58:15Z):**

> steps 1-6 are RECORDED, all as predicted. g++ and MinGW 26/0 (CB2: 55,345 shared pixels, 2,255 named); check_map_draw
> 3/0; all 14 plants exactly their sets, WXBUILD at its re-frozen 2; anchors 40 (14 plants, 16 anchors); lint 40/0/3.
> The step-7 correction is ACCEPTED as a re-freeze before the run. Your miss is recorded: the 11-05/11-06 WSL rule (name
> the environment blocks) was known and left out of d814c31e and 082a56d8. The corrected WSL prediction is FROZEN HERE as
> evidence, 01:58:15Z:
> - exit 2;
> - FAILED BLOCKS exactly "the same TU, for the board" and "Weatherscope R0 and R0b (the reference's controls, A12.18)";
> - then HOST TESTS FAILED;
> - MapCompose 26/0; 40 drivers checked; both passes "40 driver(s): 39 run, 1 skipped" (sabotage_wx_towns);
> - "sabotage_map_compose.py: 15 AS PREDICTED line(s)";
> - any other block failing, or either of the two passing, is OFF.
> "ALL HOST TESTS PASSED" belongs to CI's prediction for the push. GO for step 7. Commit this ruling verbatim with the
> record. The push waits for 082a56d8's run to be read, as ruled.

**Step 7, as predicted** (run.sh in full under WSL, 01:58:39Z to 02:06:23Z):
- exit 2;
- `FAILED BLOCKS:` lists exactly two:
  - `the rig: Weatherscope R0 and R0b (the reference's controls, A12.18) (exit 2)`, which reads "no python with Pillow";
  - `failed:  the same TU, for the board`, which reads "no xtensa toolchain found";
- then `HOST TESTS FAILED`.
- Everything else: MapCompose `26 checks, 0 failures`; `40 driver(s) checked`; both driver passes `40 driver(s): 39 run, 1
  skipped`, the skip being `sabotage_wx_towns.py` (the Census archives); lint `40 driver(s), 0 flagged; 3 pinned line(s)`;
  `sabotage_map_compose.py: 15 AS PREDICTED line(s)`, with `sabotage_map_draw.py` 10 and `sabotage_map_places.py` 6.

**The CI for the push, predicted.** The push carries `c095c41c`, `3714af52` and this record together, after
`082a56d8`'s run finished and was read as predicted (34 = 30/4/0, tested merge `d108445`), so no run is cancelled:
- **34 = 30 success / 4 skipped** (`promote`, `publish`, `ship-alert`, `version`) / **0 failed**, with `082a56d8`'s job
  names;
- each host-tests leg:
  - MapPlaces `32 checks, 0 failures`, MapDraw `3 checks, 0 failures`, **MapCompose `26 checks, 0 failures`**;
  - `40 driver(s) checked`, and both driver passes `40 driver(s): 39 run, 1 skipped`;
  - lint `40 driver(s), 0 flagged; 3 pinned line(s)`;
  - `sabotage_map_compose.py: 15`, `sabotage_map_draw.py: 10` and `sabotage_map_places.py: 6 AS PREDICTED line(s)`;
  - `ALL HOST TESTS PASSED`.
- The named risk: ubuntu-26.04's newer g++ compiling the new suite and driver under `-Werror`. Any warning is a compile
  failure, and so an off reading.
- No firmware image changes: nothing in `src/` includes `MapCompose.h` until the wiring.
- The tested merge is read from a checkout log. Any other reading stops.

### The wiring: registered before its code (2026-11-12)

The GO, verbatim (the orchestrator, 02:26:09Z):

> GO for the wiring's registration as listed (CD1-CD3, CB-W, the SPIRAM-cap source row + plant, the airportsGen-bump
> source row + plant). One requirement, because the wiring is the first step that edits a SHIPPING file
> (AircraftManager.cpp):
> - State which envs set BLIPSCOPE_MAP_FETCH, read from platformio.ini and the CI matrix, not from memory.
> - For every SHIPPING env WITHOUT the flag, predict its image is unchanged from main's, and prove it from the ARTIFACT:
>   build it on both trees and account for the difference by the five regions (or the comparator 5c used for C', with
>   its controls). Nothing outside those regions.
> - For envs WITH the flag, predict the size delta and name the functions that change.
> A wiring that leaks outside its #if is the second-path shape again, and only the built image can show it.

#### Which envs carry the map (read from the files at `cb9d6ac2`)

- **No env sets `BLIPSCOPE_MAP_FETCH`.** `src/AircraftManager.h:38` derives it:
  `defined(FEATURE_CLOUD_FEED) && defined(WX_LAYER_SHIPS) && !defined(BLIPSCOPE_WEB_RENDERER)`.
- **`WX_LAYER_SHIPS`:** the weather switch is empty (`platformio.ini:150-151`, `layer =`), so `${cloud.prod}` and
  `${cloud.staging}` carry nothing. Two places set the flag:
  - `blipscope-s3-128-wxlook`, at `:408`;
  - the web renderer's build script, which `BLIPSCOPE_WEB_RENDERER` excludes.
- **The CI matrix** (`scripts/skus.py --shipping-envs` and `--build-envs`):
  - **one shipping env, `blipscope-s3-128`** (slug `s3-128`);
  - ten build envs. **Only `blipscope-s3-128-wxlook` carries the flag.** It is slug-less, and its `[wx-bench]` marker is
    refused in every slug-ful image.

#### What the wiring adds, and where

Everything goes in `// THE MAP DRAW` regions under `#ifdef BLIPSCOPE_MAP_FETCH`.

**`src/AircraftManager.h`,** draw regions 1 → **4**:
- CB10's refusal, as it stands;
- `#include "MapCompose.h"`;
- the members:
  - `mapcompose::Map mapDraw`;
  - `uint32_t mapPkgSeq`, `mapAirportsGen` and `mapWxGen`;
- the declarations of `DrawMapBase`, `MapAirportBoxes`, `MapAlloc` and `MapFree`.

**`src/AircraftManager.cpp`,** draw regions 0 → **12**:
- **`DrawWeatherUnder`, 4 regions.**
  - At each of its three `return false;` points, a map build first returns `DrawMapBase(canvas, nullptr)`, the map
    without weather. Each `return false;` gains braces, and its text is otherwise unchanged.
  - Before the weather's `memcpy`, a map build returns `DrawMapBase(canvas, wxLayer565)` when that draws.
  - Today's lines stay as they are, so without a ready map the two paths run byte for byte.
- **The new functions, 1 region:**
  - `DrawMapBase`:
    - **scope:** `MapEnabled()`, the radar view, a location, no night clock, and a held package;
    - **the flat-canvas guard:** `DrawWeatherUnder`'s own;
    - **the key:**
      - CD3's six fields;
      - `radiusMi`: the view radius (`viewRadLat` × 111 km) converted to statute miles and rounded (`lround`), as CB6
        graded it;
      - `mapPkgSeq` and `mapAirportsGen`;
      - `RingLabels(viewRadiusDisplay, rangeUnit)`, `Cardinals(radarUpDeg)` and `IndicatorRow()`;
    - **then:** `mapcompose::Frame`, and one `memcpy`.
    - **A refusal:** sets step A's `mapAllocFailed` and prints one line, `[map] draw: buffers refused`. A rebuild prints
      `[map] draw: rebuilt in <ms> ms, <n> label(s)`. Both print counts only (CD2).
  - `MapAirportBoxes`: DrawAirports' list, filters and cull, then `ProjectCoordinateToScreen` and
    `mapplaces::AirportObstacles`. It returns 0 airports when `displayAirports` is off.
  - `MapAlloc` is `heap_caps_aligned_alloc(8, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`; `MapFree` is `heap_caps_free`.
- **`RebuildWeatherLayer`, 1 region:** `++mapWxGen` after a rebuild.
- **`MapConfigure`, 1 region, inside step A's fetch region:**
  - beside `mapAllocFailed = false;` (`:3451`): `mapcompose::Forget(mapDraw)` and `++mapPkgSeq`;
  - `mapcompose::Init(mapDraw, MapAlloc, MapFree)` once.
- **`ConsumeBasemap`, 1 region, inside step A's fetch region:** `++mapPkgSeq` on an accepted package.
- **The airport generation, 4 regions:** `++mapAirportsGen` after each of the five writes:
  - `displayAirports` (`:911`) and `airportsMin` (`:915`), with one bump after both;
  - `cloudAirports.clear()` (`:1367`);
  - `cloudAirports = std::move(...)` (`:3725`);
  - the bench harness's `displayAirports = true` (`:2452`).

`main.cpp` is not touched, and step A's fetch-region pins (`check_map_fetch.py`: h 3, cpp 9) do not move.

#### The rows (`check_map_draw.py`: 3 → 9 source checks)

| row | check | plant (turns exactly) |
|---|---|---|
| **CD1** | in `MapCompose.h`, `MapPlaces.h`, `AirportBoxes.h` and every draw region: no `Preferences`, no `.put*(`, no `SPIFFS`, no `LittleFS` | **S-CD-NVS**, a `Preferences` write planted in DrawMapBase's region: {CD1} |
| **CD2** | the three headers contain no `Serial`, `printf(` (other than `snprintf(`), `puts(`, `ESP_LOG` or `log_`. In the draw regions, no print's arguments name `label`, `places`, `placed`, `Town`, `lat`, `lon`, `homeLat`, `homeLon`, `ix`, `iy`, `tx`, `ty`, `qx`, `qy` or `anchor` (whole tokens) | **S-CD-PRINTNAME**, a town label printed in DrawMapBase's region: {CD2} |
| **CD3** | the six `view` fields `DrawMapBase` fills have the same right-hand sides as `RebuildWeatherLayer`'s `v.` fields | **S-CD-VIEW**, `viewRadLat` → `radLat` in the map's fill: {CD3} |
| **CD5** | the allocator `mapcompose::Init` is given calls `heap_caps_aligned_alloc` with `MALLOC_CAP_SPIRAM` in the same call | **S-CD-NOSPIRAM**, the cap dropped: {CD5} |
| **CD6** | every write of `cloudAirports`, `airportsMin` or `displayAirports` in `AircraftManager.cpp` is followed within 10 lines by `++mapAirportsGen;`. The control: it finds **5** write sites, and prints the count | **S-CD-NOGEN**, the bump after `:3725` removed: {CD6} |
| **CD7** | `MapAirportBoxes` holds DrawAirports' list, filter and cull lines, normalized, in the same order. The control: it extracts at least 6 lines | **S-CD-CULL**, `0.55f` → `0.5f` in `MapAirportBoxes`: {CD7} |
| **CB-W** | the shipping `firmware.bin` holds no `[map] draw` string; the wxlook image holds both, read off the two images as W7 was | the wxlook image is its control, as it was W7's |

- **CD4's pins move** to h 4 and cpp 12. Its line reads `16 draw region(s); the refusal present; ...`.
- **One control's anchor changes:** `R-CD-CRASH` anchors on CB10's `#endif` and its region end, which stays unique once
  there are four ends in the header. The control's outcome is unchanged.
- **No existing set moves.** S-CB-SECONDPATH edits DrawAirports' `drawString` line, which is not among CD7's cull lines.
  No other existing plant touches a new row's input.
- **`sabotage_map_draw.py` totals:** 11 plants and 3 controls, so **16 AS PREDICTED lines**. `SOURCE_CHECKS` = 9, so its
  table reads `9 checks, 0 failures`.
- **Lint stays at `40 driver(s), 0 flagged; 3 pinned line(s)`:** no new driver.

#### The artifact proofs, frozen

**The trees.** Main is pinned at `e3c2508e`.
- **The branch is behind main:** main is not an ancestor of `cb9d6ac2`, and main changed `OtaInstall.h` and
  `OtaUpdater.cpp`. So "the branch's tree" means **main with the branch merged into it, as CI tests it**, built from a
  local, detached test merge that is never pushed.
- **Five builds, plus one anchor control:**

| label | tree | env | what for |
|---|---|---|---|
| A1, A2 | main, built twice | `blipscope-s3-128` | the anchor control |
| B | main + `cb9d6ac2` | `blipscope-s3-128` | step B so far: the shipping image |
| Bw | main + `cb9d6ac2` | `blipscope-s3-128-wxlook` | the flagged image, before the wiring |
| C | main + the wiring head | `blipscope-s3-128` | the wiring: the shipping image |
| Cw | main + the wiring head | `blipscope-s3-128-wxlook` | the flagged image, after the wiring |

- **How they are built:**
  - every build is clean: my own `.pio/build/<env>` is deleted first, so WiFiManager recompiles every time;
  - `pio` runs from PowerShell, one build at a time, in p4's slot;
  - **under 30 GB free on C:, nothing builds.** C: had 73 GB free at registration.

**The comparator** (scratch, `imgcmp.py`). It reports every differing byte of two `firmware.bin` files outside these
five regions:
1. `app_elf_sha256`, 32 B at 0xB0;
2. the trailing image SHA-256, the last 32 B;
3. the checksum byte, just before it;
4. and 5. WiFiManager's two `__DATE__ " " __TIME__` strings, located by content (the pattern
   `Mmm dd yyyy hh:mm:ss`), not by fixed offsets.

Its controls, all run before any real comparison:
- **presence:** each image holds exactly 2 matches of that pattern, at the same offsets in both images;
- **anchor:** A1 against A2 differs in all five regions, and in **0** bytes outside them;
- **sensitivity:** a copy of A1 with one byte flipped outside the regions reports exactly **1** byte outside.
- **Any control failing stops the run before any comparison:** the instrument is unreliable.

**The predictions:**

| comparison | predicted |
|---|---|
| A1 against B | **the same length; 0 bytes outside the five regions.** DrawAirports, the one shipping function step B so far changed, inlines `radarbox::Airport` to the same calls and constants |
| B against C | **the same length; 0 bytes outside the five regions.** Every line of the wiring is under `#ifdef BLIPSCOPE_MAP_FETCH`; the three braces add no code |
| Bw against Cw | `firmware.bin` grows by **8,000 to 20,000 B**. `.bss` grows by **under 1,040 B**: the `Map` struct, under 1 KB by its `static_assert`, and three counters |
| CB-W | B and C hold no `[map] draw`; Cw holds both strings |

**Bw against Cw, by symbol** (`xtensa-esp32s3-elf-nm --size-sort -C`, by name):
- **Existing symbols whose size changes, exactly these:**
  - `AircraftManager::DrawWeatherUnder`, `::MapConfigure`, `::ConsumeBasemap`, `::RebuildWeatherLayer`, `::Initialise`
    and `::ConsumeFetchResult`;
  - the `AircraftManager` constructor and the TU's static initializer.
  - `RecordFrameUs` changes only if the bench harness's `#if` is on in wxlook. It is read from the build flags at the
    run and stated then.
- **New symbols:**
  - `AircraftManager::DrawMapBase`, `::MapAirportBoxes`, `::MapAlloc` and `::MapFree`;
  - anything in the namespaces `mapcompose`, `mapplaces`, `maplayer` (for example `Build` and its helpers), `wxraster`
    (including the font) and `radarbox`, with their lambdas.
- **Any other symbol that is new or changes size is off.**

#### The run, in order, after the code is committed

1. `check_map_draw.py`: **`9 checks, 0 failures`**, with CD4 `16 draw region(s)` and CD6 `5 write site(s)`.
2. `sabotage_map_draw.py`:
   - the control is clean;
   - the 11 plants turn exactly their sets, and the 3 controls read as their table;
   - **16 AS PREDICTED lines**.
3. `--anchors` over the 40 drivers, then `--control` for the draw driver: `("line", "9 checks, 0 failures")`.
4. Lint: `40 driver(s), 0 flagged; 3 pinned line(s)`.
5. `check_map_fetch.py`: unchanged.
6. `run.sh` under WSL:
   - exit 2, and FAILED BLOCKS exactly `the same TU, for the board` and `Weatherscope R0 and R0b (the reference's
     controls, A12.18)`; then `HOST TESTS FAILED`;
   - MapDraw `9 checks, 0 failures`, MapCompose `26 checks, 0 failures` and MapPlaces `32 checks, 0 failures`;
   - `40 driver(s): 39 run, 1 skipped`;
   - `sabotage_map_draw.py: 16 AS PREDICTED line(s)`.
7. The artifact proofs: the comparator's controls, then A1/B, B/C, Bw/Cw and CB-W, each as frozen above.

**Anything else is a stop:** a count, a set, a control, a byte outside the regions, a size outside the band, or a symbol
outside the lists. I stop at the first such reading, with no fix and no re-run, and report it with its detail read. A
crash or a build that fails is an instrument failure, reported as one.

#### Questions for the ruling

- **Q-MW1, the braces.**
  - Each of `DrawWeatherUnder`'s three `return false;` becomes `{ <map hook under #ifdef> return false; }`. The shipping
    tokens gain braces and nothing else, and B against C decides whether the code is identical.
  - The alternative keeps the tokens identical with a macro defined in a draw region, and reads worse.
  - **I recommend the braces,** with the artifact as the proof.
- **Q-MW2, the cull's second path.**
  - `MapAirportBoxes` repeats DrawAirports' list, filters and cull, held equal by CD7.
  - The alternative refactors DrawAirports into a shared iterator. That edits a shipping function, which the artifact
    proof would then have to clear.
  - **I recommend CD7.**
- **Q-MW3, one refusal flag.** A refused draw buffer sets step A's `mapAllocFailed`, so the fetch stops too, and both
  clear at the next config change, through the same line. Confirm.
- **Q-MW4, the cap's radius in miles for km and nm devices.** `Cap` and `Inner` take the view radius in statute miles,
  whatever unit the rings print (Q-MB3 ruled "by radius in miles"). Confirm.

#### The CI for this push (docs only), predicted

- **34 = 30 success / 4 skipped** (`promote`, `publish`, `ship-alert`, `version`) / **0 failed**, with `cb9d6ac2`'s job
  names.
- Each host-tests leg reads as it did there: MapPlaces 32 / 0, MapDraw 3 / 0, MapCompose 26 / 0, 40 drivers (39 run, 1
  skipped), lint 40 / 0 / 3, AS PREDICTED 15 / 10 / 6, `ALL HOST TESTS PASSED`.
- The tested merge is read from a checkout log. `cb9d6ac2`'s run has finished, so nothing is cancelled. Any other reading
  stops.

### The wiring's ruling, and four corrections found while writing its code (2026-11-12)

**The ruling, verbatim (the orchestrator, 02:35:39Z):**

> a35e19021959 is ACCEPTED as the wiring's registration (on origin, docs +200).
> - My requirement is met from the files: BLIPSCOPE_MAP_FETCH is derived (AircraftManager.h:38), and only wxlook gets it.
> - Testing main + branch as CI does, because main isn't an ancestor of the branch, is right.
> - The comparator's three controls come before any comparison.
> - The A1-vs-B row now also proves that the MapPlaces step's DrawAirports edit (via AirportBoxes.h) left the shipping
>   image unchanged. That's a check this step adds to the last one, and it matters.
> RULINGS, the first option in each:
> - Q-MW1: braces with #ifdef'd hooks, proven by B against C at 0 bytes outside.
> - Q-MW2: CD7, the cull repeated and held line for line, not a refactor of shipping DrawAirports. It's the same reasoning
>   as Q-MB9: a check that runs.
> - Q-MW3: one flag, mapAllocFailed, and one reset at a config change.
> - Q-MW4: Cap and Inner take the PHYSICAL radius in statute miles for km and nm devices too, so the label density is the
>   same on the ground whatever the unit. State the conversion constant in the record.
> GO: code, then the 7 steps in p4's build slot. Stop at the first off reading. Anything outside the five regions for
> A1-B or B-C is a stop, never an explanation after the fact.

**Q-MW4's constant.** `radiusMi` = `lround(viewRadLat × 111 / 1.609344)`:
- 111 km a degree of latitude, the radar's own `KM_PER_DEGREE`;
- **1.609344 km a statute mile**, exact by the international definition.
- It is written once, in `DrawMapBase`.

**Four corrections, found while writing the code, before anything was built or run:**

1. **Where `mapcompose::Init` runs.**
   - The registration put it in `MapConfigure`'s region. That region sits inside step A's `OnQuery` block, which does not
     run for the boot's first key, so the Map would never be initialised.
   - It runs instead at the map's first use, in `DrawMapBase`'s region: `if (mapDraw.alloc == nullptr) mapcompose::Init(...)`.
   - The region counts do not change, and CD5 finds the call in the draw regions as before.
2. **Bw against Cw, the symbol lists.**
   - **The constructor.** `AircraftManager`'s constructor is inline in the header (`AircraftManager.h:1386`), and its one
     use is the global at `main.cpp:173`.
     - So **main.cpp's static initializer** is the one expected to change, with the new members' initialisers inlined
       there.
     - **No out-of-line constructor symbol** is expected in either image. If one appears in Bw, it must change in Cw.
     - "The TU's static initializer" therefore means main.cpp's.
   - **New symbols may also appear in `bmpk`, `wxbase`, `wxlayer` and `wxs2`:** template instantiations and inline
     functions that `Build`, `Compose` and the labels newly call. **No existing symbol in those namespaces may change
     size.**
   - **New C runtime, libm and libgcc symbols the new code calls may appear,** for example `llround`, `fmin`, `fmax`,
     `floor` and soft-double helpers. None that exists may change.
   - **Nested symbols:** a listed or new function's nested symbols (its lambdas and its local statics) are counted with
     that function.
   - **`RecordFrameUs`:** its block is under `FOLLOW_BENCH`, which no env sets, so it is **unchanged** in wxlook.
3. **The rebuild's log line** carries `, Build refused the package` when `Build` returns false, so a failed build is not
   silent.
4. **CB-W's two strings, exactly:** `[map] draw: buffers refused` and `[map] draw: rebuilt in`.

All other predictions stand as registered.

### The wiring's run, as read, and the owner's ruling on step 7 (2026-11-12)

**The code** is `c4ff317a`, committed before step 1 on `eb4da9cc`, the record of the 02:35:39Z ruling and the four corrections.
- **Steps 1-6, every one as predicted:**
  - `check_map_draw.py`: `9 checks, 0 failures`; CD4 `16 draw region(s)`; CD6 `5 write site(s)`; CD7 10 lines held;
  - `sabotage_map_draw.py`:
    - all 11 plants exactly their sets;
    - the 3 controls as their table, `R-CD-CRASH` from its new anchor;
    - 16 AS PREDICTED lines;
  - `--anchors`: 40 drivers; the draw driver `12 plant(s), 12 anchor(s)`. Its control: `line 9 checks, 0 failures`;
  - lint: `40 driver(s), 0 flagged; 3 pinned line(s)`;
  - `check_map_fetch.py`: output identical to `cb9d6ac2`'s CI;
  - `run.sh` under WSL:
    - exit 2, with FAILED BLOCKS exactly the two environment blocks;
    - MapDraw 9 / 0, MapCompose 26 / 0, MapPlaces 32 / 0;
    - `40 driver(s): 39 run, 1 skipped`;
    - `sabotage_map_draw.py: 16 AS PREDICTED line(s)`.

**Step 7, the artifact proofs:**
- **The builds:** six clean builds in p4's slot. Main was pinned at `e3c2508e`. B and C are local test merges, never pushed, whose trees equal `git merge-tree`'s output:
  - A1 and A2: main (tree `fea2d987c7cc`), `blipscope-s3-128`, 1,932,544 B each;
  - B: main + `cb9d6ac2` (tree `eb84bef82d41`), 1,932,544 B. Bw, the same tree as `blipscope-s3-128-wxlook`: 1,958,816 B;
  - C: main + `c4ff317a` (tree `0b1d19f08e26`), 1,932,544 B. Cw: 1,973,344 B.
- **The comparator's first instrument failure, ruled 03:04:06Z.**
  - Its presence control found one date-time string, not two. WiFiManager's "Software Info:" string stores `__TIME__`
    and `__DATE__` apart (`hh:mm:ss\0Mmm dd yyyy`), while "aboutdate" joins them.
  - Region 5 was redefined as that pair after "Software Info:". The comparator (`dc828e47…`) was frozen before it read
    anything, and the controls were re-run on the same images.
  - **The controls then read:**
    - presence: aboutdate at 0x312C and software-info at 0x1007A, in both;
    - anchor (A1 against A2): 0 bytes outside, inside 32 / 32 / 1 / 5 / 4;
    - sensitivity: one flipped byte at 0x1000 reads exactly 1 outside.
  - The app descriptor's prebuilt stamp (0x70 / 0x80) and a library's own stamp (0x89297) are identical in both images,
    so neither is a sixth region.
- **The proofs:**

| row | read |
|---|---|
| **A1 against B** | the same length, **0 bytes outside** (inside 32 / 31 / 1 / 5 / 5). The MapPlaces step's DrawAirports edit left the shipping image unchanged |
| **B against C** | the same length, **0 bytes outside** (inside 32 / 32 / 1 / 4 / 3). **The wiring does not reach the shipping image** |
| **Bw against Cw, size** | `firmware.bin` grows by **+14,528 B**, inside the 8,000-20,000 band |
| **.bss** | `appManager`, 9,496 → 10,192 B: **+696 B**, under 1,040 |
| **the predicted changed set** | all six changed size: `ConsumeBasemap` +12, `ConsumeFetchResult` −16, `DrawWeatherUnder` +27, `Initialise` +20, `MapConfigure` +28, `RebuildWeatherLayer` −79; and main.cpp's static initializer (`_GLOBAL__sub_I_tft`, `tft` being main.cpp:85's first global) +220 |
| **CB-W** | `[map] draw` is absent from B, C and Bw; Cw holds `[map] draw: buffers refused` once and `[map] draw: rebuilt in` once. The control, as W7's: both wxlook images hold `[wx-bench]` and `[map] status=`, and both shipping images hold neither |

**The new symbols in Cw, by name, with the new code that calls each.** Read from the run's own symbol listing, not from
a new instrument. Template arguments are elided.
- **`AircraftManager`:**
  - `DrawMapBase` (910 B), called from the four `DrawWeatherUnder` hooks;
  - `MapAirportBoxes` (192 B) and its lambda (214 B), called from `mapcompose::DrawNames` through `Inputs.airports`;
  - `MapAlloc` (20 B) and `MapFree` (14 B), called from `mapcompose::Allocate` and `Release` through `Map.alloc` and
    `freeFn`. `Init` is in `DrawMapBase`.
- **`mapcompose`:**
  - `Frame` (585 B) and `Stale` (242 B), called from `DrawMapBase`;
  - `DrawNames` (338 B), called from `Rebuild`, which is inlined into `Frame`;
  - `Release` (88 B), called from `Frame`'s growth check and from `Forget`, in `MapConfigure`;
  - `LayerSink::Put` (59 B), `~LayerSink` (two, 15 and 5 B) and `vtable for mapcompose::LayerSink` (20 B), called from
    `DrawLabel`, which is inlined into `DrawNames`.
- **`maplayer`:**
  - `Build` (882 B) and its three lambdas, called from `Frame` through `Inputs.build`, the address taken in `DrawMapBase`;
  - `AffineFor`, `ChooseLevel`, `LineIdx`, `Channel`, `To565`, `ToScreen` and `kThemes`, called from `Build`. `AffineFor` is
    also called from `mapplaces::AnchorAffine`, in `Place`;
  - `ForEachLevelPixel<…>` (two), called from `Build`.
- **`mapplaces`:**
  - `Place` (1,178 B), `BoxFree` and `Overlaps`, called from `DrawNames`;
  - `RingLabels`'s `kFrac`, called from `DrawMapBase`'s key.
- **`wxraster`:** `Text` (277 B), `Put` (51 B) and `glcd::font` (1,280 B), called from `DrawLabel`.
- **`bmpk`:**
  - `ForEachRing<…>` (two), `ForEachRiver<…>` (two) and `ForEachRoad<…>` (four), called from `Build`'s passes;
  - `ForEachPlace<…>`, called from `mapplaces::Load`, in `Rebuild`;
  - `Package::Find`, called from the `ForEach*` passes and `ChooseLevel`.
- **`wxlayer::OffDisc`:** called from `mapcompose::Compose` and `Build`'s colour pass. **`wxs2::To565`:** called from
  `maplayer::To565`.
- **The runtime:**
  - `fmin` and `fmax`, called from `mapplaces::ClearsCentre`, in `BoxFree`;
  - `__fpclassifyd`, called from newlib's `fmin` and `fmax`.
- **The rest of the "new" entries** are ESP-IDF and libm statics with names defined in several files (`PIo2`, `Zero`,
  `TAG`, `chip_name`, `two_over_pi`, `npio2_hw`, `s_spinlock`, `lgfx::v1::delay`, and others). Each is paired with a
  "removed" entry of the same name. They are artifacts of the retired instrument's keying: **no verdict.**

**The ruling, verbatim** (the owner, 03:39Z, with the cloud reviewer's read; relayed by the orchestrator at 03:40:26Z):

> "Map-client's wiring: (i). The shipping image is proven unchanged by controlled instruments. The size band, .bss, the
> predicted changed set and the new symbols by name stand on their own. The symbol row is recorded as an instrument
> failure: no verdict, with the residue unexplained. symdiff is retired. Instead of (ii): before maps are switched on in
> any customer image, redo this per-function accounting with #656's layout-invariant comparator (already on the post-v19
> list), so the same tool isn't built twice."

**The symbol row: an instrument failure, no verdict.**
- The scratch `symdiff.py` (`ecc74479…`) put 260 symbols outside its lists. Three defects were seen in its detail:
  - template instantiations demangle with their return type first, so the namespace test missed them;
  - statics with duplicate names were keyed by whichever copy came second;
  - main.cpp's static initializer has no source line in `nm -l`.
- **The residue stays unexplained:** existing symbols changing by a few bytes, 118 of the 225 by at most 4 B, including
  `AircraftManager` methods the wiring never touched, inline helpers compiled in main.cpp, and precompiled ESP-IDF code.
- **The alternatives named, none excluded:** member-offset shifts; TU-wide inlining budgets, now that `AircraftManager.h`
  includes `MapCompose.h`; Xtensa link-time relaxation; more keying artifacts.
- **My miss:** the symbol criterion was registered without accounting for how this image's other symbols move when it
  grows.
- **`symdiff.py` is retired and is not to be reused.** The accounting moves to #656's comparator, under the gate below.

### The bench rows: the procedure, registered before any window (2026-11-12)

The ask, verbatim (the orchestrator, 04:03:08Z):

> YES: register the bench rows' procedure now, docs only, before any window. One requirement that decides who opens the
> window: SPLIT the rows by whether they need the owner.
> - NO-HANDS rows (serial and log readings only, e.g. CBF1's rebuild time, CBF3's wedge behaviour, CBF4's
>   PSRAM/internal-heap reading, and whichever of step M's CF1-CF6 qualify) can run in a gate-5 window that I open, with
>   you holding E2:08 (it's on the hub), after v19's step 5' and anything else on E2:08.
> - ROWS NEEDING HIS EYES (CBF2's look on glass, names under rain) go to him as his window, in his words, scheduled
>   through me with confirm-and-stamp. You don't ask him yourself, as you said.
> For each row: the image (built and identified by the five-region comparator; bench-flash rebuilds, tonight's W1
> lesson), the predictions, the plants, the exact log lines read, and which class it's in.

#### Found while writing this: CF2 and CF3 were priced for step C, not step B

Step M priced the sweep with the map on at 7.7-8.9 ms (CF2) and frame p95 at +6.2 to +10.5 ms (CF3). Both assume the
wedge **blends** over the map. In step B the wedge blends only over a shown weather layer: `DrawSweepOverWeather`
returns false unless `wxShowing == Crop`. With the weather off, the opaque wedge draws, erasing the map (CBF3). Blending
over the map is step C's work.
- **CF2 and CF3 move to step C as registered,** and step B reads its own versions, re-derived below from #445's F run:
  **CF2-B** and **CF3-B**.
- CF1, CF4, CF5 and CF6 are unchanged by this.

#### The two classes

| class | rows | who opens the window |
|---|---|---|
| **NO-HANDS** (serial, and `/diag/fb` over the LAN; nobody at the board) | CBF1, CBF3, CBF4, CF1, CF2-B, CF3-B, CF4, CF5 (Bend and KSFO), CF6 | the orchestrator, as a gate-5 window, with map-client holding E2:08 (`board-port.py --reserve blipscope-bench --by map-client`), after v19's step 5' and anything else on E2:08 |
| **THE OWNER'S EYES** | CBF2: the label's look (Q-MB5) and names under rain (Q-MB7) | the owner, in his words, scheduled through the orchestrator with confirm-and-stamp. map-client does not ask him |

**Q-MB10, before any flash.** `scripts/bench-flash.ps1` refuses `-Board blipscope-bench` (E2:08) without
`-DanielApproved`, which carries CLAUDE.md's rule for that board (ask the owner before touching it) into code.
- So even a no-hands window needs the owner's approval for that flag.
- **The question:** does the orchestrator's gate-5 window carry the owner's word for `-DanielApproved` on E2:08, or does
  each flash need his word? Nothing is flashed until this is answered.

#### The image: one bench env, as step M ruled it

`blipscope-s3-128-mapbench` is a bench-only env extending `-wxlook`, under `-DMAP_BENCH`. Its code is written after this
registration is ruled, in `// THE MAP BENCH` regions.
- **The packages:** `bend.bmpk` and `ksfo.bmpk` from `test/fixtures/wx/basemap/` are embedded. There is no fetch: each is
  handed to the wiring as an accepted package, through `mapPkg` and `++mapPkgSeq`.
- **The home:** the bench sets the device's home to the embedded package's centre, so the radar, the weather and the map
  agree. Nothing prints a coordinate or a name (CD2).
- **Its phases, run by the bench itself on the 30 s health tick,** so nothing on the PC drives the board:

| phase | package | weather | the map | what it serves |
|---|---|---|---|---|
| P0 | Bend | as configured | first package: the allocation | CBF4 |
| P1 | Bend | forced off | **alternates on/off every tick**, 20 pairs | CF1, CF2-B, CF3-B, CBF3 (`/diag/fb` during "on" ticks) |
| P2 | Bend | shown, look A | alternates on/off every tick, 20 pairs | CF4 |
| P3 | Bend | as P2 | the zoom ladder stepped one step per tick, 5 laps | CBF1, CF5 (Bend) |
| P4 | KSFO | as P2 | the zoom ladder stepped one step per tick, 5 laps | CBF1, CF5 (KSFO) |

- CF6 is read over all phases.
- **About 50 minutes** in all: P1 and P2 are 20 pairs of 30 s ticks each, so about 40 min, and P3 and P4 are 25 ticks
  each.
- **Bench-only lines,** all counts, no coordinate and no name:
  - `[map-bench] phase P<n> map=<on|off> wx=<on|off> zoom=<step>mi`, at every tick;
  - `[map-bench] alloc vertices=<n> price=<B> psram_free <before> -> <after> internal <before> -> <after>`, once, at
    P0's allocation, read in the same loop pass as `mapcompose::Frame`;
  - `[map-bench] passes s1=<us> water=<us> rivers=<us> roads=<us> colour=<us> total=<us>`, at every rebuild, from
    `BuildInfo`, with `micros` given as Build's clock in the bench only.
- **The bench marker:** `[map-bench]` is printed at boot. It joins `MARKERS` in `scripts/check-no-bench-hooks.sh`, which
  refuses it in every slug-ful image. The firmware workflow's `--expect` runs on this env, as wxlook's does (step M's
  ruling).
- **CI:** a slug-less build-only row in `skus.yml`, with its launch-gate `plant_target()` case (memory: a new env without
  one reads UNTRUSTWORTHY). Q-MB11 asks whether the row stays after the window or goes with the env.

**How the image is identified (tonight's W1 lesson: `bench-flash.ps1 -AppOnly` runs `pio run` itself):**
1. **The tree:** #659's head merged locally into the then-current main, as CI tests it. Its tree hash is printed and must
   equal `git merge-tree`'s.
2. **Build X, clean,** with `b1/build_one.ps1`. Record `firmware.bin` and its `app_elf_sha256` (0xB0).
3. `bench-flash.ps1 -PioEnv blipscope-s3-128-mapbench -Board blipscope-bench -AppOnly -By map-client`, with the
   approval flag Q-MB10 decides. Its own `pio run` produces Y in the same build directory.
4. **Before trusting the flash,** Y is compared to X with `imgcmp.py` (`dc828e47…`). It must read 0 bytes outside the
   five regions. If nothing was rebuilt, Y is X byte for byte.
5. **On the board:** the boot log's `[build] env=blipscope-s3-128-mapbench` and the `[map-bench]` marker are read. If
   the boot log prints the ELF SHA-256, its prefix must equal Y's `app_elf_sha256`.
6. **Capture:** `bench-trace.ps1`, a single open that is never reattached. Ending a trace puts an S3 board in download
   mode, so the window ends with the hub's power cycle (gate 5: only E2:08's port, logged) or `esptool read-mac`.

#### The rows

Each is read by a scratch reader, `map_bench_read.py`, frozen (sha) before the window. Its verdict table is in its
header, and every row is shown red on a **planted log** (a copy of a synthetic capture with that row's line altered)
before the real capture is read. The image plant for CBF4 is built and flashed in the same window.

| row | class | line(s) read | predicted | plant |
|---|---|---|---|---|
| **CBF4** | no-hands | `[map-bench] alloc …`, once | `internal` before and after differ by **0**. `psram_free` falls by the four buffers' price within **1,024 B** (WD5d's precedent). The price is 2 × 115,200 + 57,600 + 32 × (vertices + 1) + 4,224 + 13,320, computed from the line's own `vertices` | **P-CBF4**, registered with the row: an image whose `MapAlloc` drops `MALLOC_CAP_SPIRAM` (S-CD-NOSPIRAM's edit), flashed after the real one: `internal` falls by at least 115,200, and the reader says red |
| **CBF1** | no-hands | `[map] draw: rebuilt in <ms> ms, <n> label(s)` in P3 and P4 | every rebuild **≤ 100 ms**. The verdict as step M ruled it: ≤ 40 done; 40-100 stays, as one hitch per zoom step; over 100 stop | a planted log line `rebuilt in 150 ms`: red |
| **CF5** | no-hands | `[map-bench] passes …` in P3 (Bend) and P4 (KSFO) | Bend **30-70 ms** total, as registered. KSFO is **read and reported, not graded**: "the KSFO reading comes before the layer design counts as settled" (ruled), and no band was registered for it | a planted `total=` of 120,000 us: red for Bend |
| **CF1** | no-hands | `[health] clear=<us>/<us>us n=<n> push=<n>` over P1's ticks, by `[map-bench] map=` | map off **3.01 ± 0.3 ms**; map on **5.08 ± 0.3 ms** (one `memcpy` of the composite) | a planted map-on `clear=` of 7,000 us: red |
| **CF2-B** | no-hands | `[health] sweep=<us>/…us n= blend=` over P1 | the opaque wedge, map on and off alike: **2.04 ± 0.3 ms**, `blend=0` | a planted map-on `blend=1`: red |
| **CF3-B** | no-hands | `[health] frame … p95=<ms>` over P1's pairs | p95, on − off, equals the clear's difference: **+2.07 ms, within ± 1.0** | a planted pair at +5 ms: red |
| **CF4** | no-hands | the frame p95 and `clear=` over P2's pairs | **−1.0 to +2.2 ms** on − off, and the clear unchanged (one `memcpy` either way), as registered | a planted pair at +4 ms: red |
| **CF6** | no-hands | every `[health] BUDGET BROKEN` line, all phases | **0** | a planted `BUDGET BROKEN` line: red |
| **CBF3** | no-hands | `/diag/fb` frames in P1's "on" ticks, fetched 3 times each (the partial-frames rule) | inside the wedge's sector, the share of the theme's map colours is **under 2 %** (aircraft, rings and labels excluded by their own colours). Outside it, on the disc, **over 80 %** are map colours. It is reported as **step C's input, not a defect** (Q-MB2) | a planted frame with the map copied over the wedge: red (the inside share over 2 %) |
| **CBF2** | **the owner's eyes** | none: the owner on glass, after an optional `/diag/fb` keepsake | the owner judges the label's look (Q-MB5) and names under rain (Q-MB7). Nothing is built toward an alternative before then | none (a judgement, not a reading) |

**The verdicts, applying to every no-hands row:**
- a row outside its band: stop and report;
- a planted log the reader does not call red: the reader is the instrument, so stop;
- an image that is not identified (step 4 or 5 above): nothing is read from it;
- **anything else: the instrument is unreliable at this scale. Stop and decide.**

**After the window:**
- the readings and the capture's counts go into the record;
- the board is released with `board-port.py --release`;
- the mapbench env stays or goes as Q-MB11 rules.

#### Questions for the ruling

- **Q-MB10, the approval flag for E2:08.** Does a gate-5 window opened by the orchestrator carry the owner's
  `-DanielApproved`, or does each flash need his word? (Above.)
- **Q-MB11, the bench env's life.**
  - (a) A slug-less CI row that stays, so it keeps compiling.
  - (b) The env and its row are removed after the window.
  - **I recommend (a):** CLAUDE.md's AMOLED lesson, and step C needs the same bench.
- **Q-MB12, CF2 and CF3.** They are re-derived for step B as CF2-B and CF3-B. The registered CF2 and CF3 move to step
  C's bench, where the blended wedge exists. Confirm.

### The mapbench env: its code and CI row, registered before the code (2026-11-12)

**The ruling, verbatim (the orchestrator, 04:08:40Z):**

> 19ab390c98cc is read (on origin, docs +123). The split is clear. The procedure (the mapbench env driving its own
> phases, every row red on a planted log first, the CBF4 image plant, identification of Y against X at 0 bytes outside
> before anything is read, and the four verdicts) is good. Catching that CF2/CF3 were priced for step C's blend was a
> real find before any window.
> - Q-MB12: CONFIRMED. CF2 and CF3 move to step C as registered; CF2-B (the opaque wedge, 2.04 ± 0.3 ms, blend=0) and
>   CF3-B (+2.07 ± 1.0 ms) stand for step B. CF1 and CF4-CF6 are unchanged.
> - Q-MB11: (a). A slug-less CI row that stays, with its plant_target() and "[map-bench]" in check-no-bench-hooks.sh's
>   MARKERS. The AMOLED lesson, and step C needs it.
> - Q-MB10: NOT MINE TO ASSUME. Gate 5 lets me open no-hands windows on hub boards, but -DanielApproved is his word in
>   code, and his gate-5 text doesn't say whether it covers flashing. I'm putting it to him now: does a gate-5 window I
>   open carry his approval for flashing the held hub board (flash + restore inside the window), or does each flash need
>   his word?
> Until he answers, flash nothing. Write the mapbench env's code and its CI row meanwhile, predictions first as usual.
> The window opens anyway only after v19's step 5' on E2:08.

#### Three corrections to `19ab390c`, found while designing the code

1. **`plant_target()` needs no new case.** `scripts/check-adsbdb-out.sh`'s default site, `src/ConfigMigration.cpp`,
   serves every radar env, wxlook included. The mapbench env links that file. The case named in `19ab390c` does not
   exist and is not added.
2. **The bench marker's `--expect`.** The mapbench image needs WX_LOOK_AB's timers, because the `clear=`, `sweep=` and
   `sweepparts=` lines read by CF1-CF4 come from them. So the image carries `[wx-bench]` as well as `[map-bench]`.
   - `check-no-bench-hooks.sh --expect` requires its one marker and **every other marker absent**, so it would refuse
     this image.
   - **Q-MB13** offers two ways (below).
3. **The window is about 65 minutes, not 50:** P1 and P2 are 40 ticks each, and P3 and P4 are 25 each, all 30 s ticks.

#### The code, exactly (written after the ruling)

| file | what |
|---|---|
| `platformio.ini` | `[env:blipscope-s3-128-mapbench]`, `extends = env:blipscope-s3-128`, its flags composed as wxlook's are (never inherited, the `-U` trap): `${s3128_hw.build_flags} -DBLIPSCOPE_EXPECT_BIG_NVS ${cloud.prod} -DWX_LOOK_AB -DWX_LAYER_SHIPS -DMAP_BENCH`, and `board_build.embed_files = test/fixtures/wx/basemap/bend.bmpk, test/fixtures/wx/basemap/ksfo.bmpk` (the repo already embeds a `.wxc` this way) |
| `skus.yml` | `- { env: blipscope-s3-128-mapbench, status: build-only }`, with a comment |
| `scripts/check-no-bench-hooks.sh` | `"[map-bench]"` joins `MARKERS`; `--expect` as Q-MB13 rules; `--selftest` gains the new form's three cases |
| the firmware workflow | one step, `if: matrix.env == 'blipscope-s3-128-mapbench'`, running the bench-marker `--expect` on that image (the wxlook step's twin) |
| `src/AircraftManager.h` | **1** `// THE MAP BENCH` region: a small `MapBench` state (phase, tick, map off, weather off, lap) and two declarations |
| `src/AircraftManager.cpp` | **6** `// THE MAP BENCH` regions, each under `#ifdef MAP_BENCH`. See the list below |
| `test/host/check_map_draw.py` | **CB-B1** and **CB-B2** (below): 9 → **11** source checks |
| `test/host/sabotage_map_draw.py` | 2 plants: 11 → **13** plants, 3 controls, so **18** AS PREDICTED lines |

**The six regions in `src/AircraftManager.cpp`:**
- **File scope:**
  - the embedded packages' start and end symbols;
  - `MapBenchAccept(which)`: copies the package into PSRAM (as step A's landing buffer would hold it), passes it through
    `maplayer::Load` (the gate), and sets `mapPkg`, `mapHeldLen` and `++mapPkgSeq`;
  - `MapBenchTick()`: advances the phase on the tick and steps the zoom in P3 and P4. At P4 it switches the package to
    KSFO, which also exercises CB9's growth on the device.
- **`Initialise`, after `MapConfigure`:** sets the home to the package's centre, prints the boot marker `[map-bench]`,
  and calls `MapBenchAccept(0)`.
- **`RequestBasemap`, first line:** `return false;`. No fetch.
- **`RecordFrameUs`, after the `[health] frame` line:** `MapBenchTick();`.
- **`DrawWeatherUnder`, first line:** in P1, the weather is forced off, the map is drawn alone, and today's paths are
  skipped.
- **Nested in `DrawMapBase`'s draw region:**
  - the map-off return;
  - the `[map-bench] alloc` reading around the first `mapcompose::Frame`;
  - `micros` as Build's clock;
  - the `[map-bench] passes` line after each rebuild.
- **The lines are those registered in `19ab390c`,** all counts. Nothing prints a coordinate or a name (CD2).

#### The rows, registered

| row | check | plant (turns exactly) |
|---|---|---|
| **CB-B1** | every `MAP_BENCH` token in `src/` and `include/` sits inside a `// THE MAP BENCH BEGIN` / `END` region. The regions are pinned (h 1, cpp 6), and each holds `#ifdef MAP_BENCH` | **S-CB-BENCHLEAK**, a `#ifdef MAP_BENCH` block planted outside any region in `AircraftManager.cpp`: {CB-B1} |
| **CB-B2** | the bench regions hold no `Preferences`, `.put*(`, `SPIFFS` or `LittleFS` (CD1's list), and no print in them names a token in CD2's `PRIVATE` set | **S-CB-BENCHPRINT**, a print of `lat` planted in `RecordFrameUs`'s bench region, which is not nested in a draw region: {CB-B2} |

- **No existing set moves:**
  - the nested bench region in `DrawMapBase` prints only counts, so CD2 stays green;
  - CD4 counts draw regions, not bench regions;
  - the two plants sit where no other row reads.
- **The shipping and wxlook images must not move.** `MAP_BENCH` is defined in the mapbench env alone, so a fresh
  `blipscope-s3-128` (C2) and `-wxlook` (Cw2) at the new head are compared to C and Cw with `imgcmp.py` (`dc828e47…`),
  its three controls rerun first on A1 and A2. **0 bytes outside, both.**
- **The mapbench image (M1):**
  - **its size:** M1 against Cw2 is +416,496 B of packages plus the bench code, so **+417,496 to +421,496 B**;
  - **its strings:**
    - both bench markers present and no other;
    - CB-W's two `[map] draw` strings;
    - `[map-bench] phase`, `alloc` and `passes`;
  - read off the image with the new `--expect` (Q-MB13).

#### The run, after the code is committed

1. `check_map_draw.py`: **`11 checks, 0 failures`**.
2. `sabotage_map_draw.py`:
   - the control is clean;
   - 13 plants exactly their sets, and the 3 controls as their table;
   - **18 AS PREDICTED lines**.
3. `--anchors` over the 40 drivers, then the draw driver's `--control`: `("line", "11 checks, 0 failures")`.
4. Lint: `40 driver(s), 0 flagged; 3 pinned line(s)`.
5. `bash scripts/check-no-bench-hooks.sh --selftest`: every case passes, the new form's three included; `python
   scripts/skus.py --validate`: OK.
6. `run.sh` under WSL:
   - exit 2, FAILED BLOCKS exactly the two environment blocks;
   - MapDraw `11 checks, 0 failures`, MapCompose 26 / 0, MapPlaces 32 / 0;
   - `40 driver(s): 39 run, 1 skipped`;
   - `sabotage_map_draw.py: 18 AS PREDICTED line(s)`.
7. The builds, in p4's slot, with no board touched:
   - C2, Cw2 and M1 (main + the new head; tree = `merge-tree`'s);
   - `imgcmp.py`'s controls on A1 and A2;
   - C against C2, and Cw against Cw2: **0 bytes outside**;
   - M1's size band and strings as above.

**Anything else is a stop,** as before.

**The CI for the code push, predicted:**
- **35 = 31 success / 4 skipped / 0 failed.** The new leg is `build (blipscope-s3-128-mapbench)`, and the job set
  otherwise matches `cf58c643`'s.
- **Each host-tests leg:**
  - MapDraw `11 checks, 0 failures`, MapCompose and MapPlaces as before;
  - 40 drivers;
  - lint 40 / 0 / 3;
  - AS PREDICTED: compose 15, **draw 18**, places 6;
  - `ALL HOST TESTS PASSED`.
- **The mapbench leg's new step** reads the `--expect` line for both markers.
- **The named risk:** `board_build.embed_files` under this env's framework. The repo's precedent is a radar-family env,
  and a failure there is off.

#### Questions for the ruling

- **Q-MB13, the bench image's two markers.**
  - **(B), recommended.** `--expect` takes a comma-separated list (`--expect "[wx-bench],[map-bench]"`): every listed
    marker must be present, and every other absent. `--selftest` proves three cases: both present passes, one missing
    fails, and a third marker present fails. The fence stays as strict as it is, and gains one shape.
  - **(A)** The mapbench image's marker is `[wx-bench]`, and `[map-bench]` is not added. That contradicts Q-MB11's
    ruling, so it is listed only so that the choice is visible.

### The mapbench registration's ruling (2026-11-12)

**The ruling, verbatim (the orchestrator, 04:28:30Z):**

> 19ab390c's CI is recorded (34 = 30/4/0; tested merge a0a7e04). The mapbench registration at 14764d1405a5 is ACCEPTED,
> with its three corrections.
> - (1) No new plant_target() case. One condition: the CI prediction for the code push must name the adsbdb gate's
>   verdict on the new mapbench leg as PASS, NOT UNTRUSTWORTHY. If the default site doesn't actually serve it, CI shows it
>   and that's off.
> - (2) Two markers: noted.
> - (3) About 65 min: noted.
> Q-MB13: (B). --expect takes a comma list: every listed marker present, every other absent. check-no-bench-hooks.sh is
> SHARED (the wxlook step and others call it), so add a FOURTH --selftest case: the existing single-marker form
> ("--expect [wx-bench]") behaves exactly as before on a wxlook-shaped input. Also confirm that no existing caller's
> command line changes. (A) is rejected, as you say.
> The rows, plants and run are as listed (CB-B1/CB-B2 with their plants; check_map_draw 11/0; draw 18; the builds at 0
> outside, the comparator's controls first; M1 +417,496..+421,496 B). Then the code, predictions first, pushed under the
> standing rule. Flash nothing (Q-MB10 is still with the owner).

**The callers, read before the change.** `scripts/check-no-bench-hooks.sh` is called at six places in the firmware
workflow:
- the photo role's `--expect "[photo-mode]"` and its parent's plain scan;
- wxlook's `--expect "[wx-bench]"`;
- `--selftest`;
- the slug-ful image's scan, and the factory image's.

**None of their command lines changes.** The single-marker form keeps its exact output line and exit codes, and the
fourth selftest case proves it on a wxlook-shaped input. The new step on the mapbench leg is the only new caller.

**Added to the code push's CI prediction:** on the `build (blipscope-s3-128-mapbench)` leg, the adsbdb launch gate's
verdict is **PASS**, from its default plant site (`src/ConfigMigration.cpp`). **UNTRUSTWORTHY there is off.**

### The owner's terms for the window, and the bench code's corrections (2026-11-12)

**The owner, verbatim (04:29Z, relayed by the orchestrator at 04:30:00Z):**

> "1. Yes: a no-hands window you open may include flashing E2:08 over USB, when all of these hold: The write ranges are
> printed against the partition table before any flash. No factory image, no full erase, and nothing written to the NVS
> (settings) partition. Both images are identified by sha256 before the window: the test image, and E2:08's restore
> image. E2:08 is restored at the end, and its boot log shows it rejoining Wi-Fi with its settings, without the setup
> portal. v19 has priority on E2:08: never during step 5' or any other v19 step. Anything that would need my hands: stop
> and report.
> 2. The marker script (the reviewer): besides proving the old single-marker form unchanged, the new list form needs its
> own failing control. A build carrying a marker that isn't on its list must still be refused."

**These terms join the window's procedure, as conditions on every flash:**
- the write ranges printed against E2:08's partition table first: the app image to the app slot only; no factory image,
  no full erase, nothing written to NVS;
- **two images identified by sha256 before the window opens:**
  - the mapbench image;
  - E2:08's restore image, read at that time as whatever v19's step 5' leaves on the board;
- E2:08 restored at the end, and its boot log read for Wi-Fi rejoining with its saved settings, **not the setup portal**;
- the window only after every v19 step on E2:08;
- anything that would need the owner's hands: stop and report.

**The marker script's five new `--selftest` cases** (`scripts/check-no-bench-hooks.sh`):
1. a list, every listed marker present: passes;
2. a list, a listed marker missing: refused;
3. **a list, a marker not on the list present** (`[alert-bench]` under `"[wx-bench],[map-bench]"`): **refused**. This is
   the reviewer's failing control;
4. the one-marker form on a wxlook-shaped image: exit 0, with the exact line it always printed;
5. the one-marker form on the two-marker image: still refused.

**The ruling, verbatim (the orchestrator, 04:34:59Z):**

> the owner's terms recorded verbatim, and the five --selftest cases (the third is the reviewer's control): ACCEPTED.
> The four placement corrections, all before the code: ACCEPTED.
> - (1) the home is set in Initialise's region right after the location read, before the radius;
> - (2) a lazy accept at DrawMapBase's top;
> - (3) two nested regions in DrawMapBase, keeping CD5's single Init; the pins become h 1 / cpp 7;
> - (4) the passes line from the tick.
> The regex: ACCEPTED, and it's a real catch. A FAIL regex that can't match a row's name makes that row's plant unable
> to ever read red. That's the blind-control shape. Add to the run a check that each new plant's red line is actually
> MATCHED by the driver (e.g. assert that S-CB-BENCHLEAK's output line is parsed as a red CB-B1). Then it's seen
> working, not assumed.
> ONE CHECK for (4): P4's switch calls Initialise() from RecordFrameUs, on the loop task, mid-run. Before relying on it,
> read Initialise for anything that must not run twice: task creation, a second NVS open, a second web-server start, a
> static that assumes one call. Name what you found in the record. If anything there isn't re-entrant, use the narrower
> config-change path instead (MapConfigure plus the radius recompute), not the full Initialise.
> Then write the C++, commit it with the record, and run. Flash only in the window, after v19's E2:08 steps.

**`Initialise`, read for anything that must not run twice** (`src/AircraftManager.cpp`, about 680 lines). **It is built
to re-run.** main.cpp's loop calls it on every saved config (`ConsumeConfigChanged`), and every one-time act in it is
guarded:
- **tasks:** `StartFetchTask` and `StartEnrichTask` return at once once their handles are set ("already running;
  survive Initialise() being re-run on a config reload"); `mqtt.Begin` "spawns the publisher task once; reconfigures it
  thereafter";
- **NVS:** the follow log loads once, behind `followLogLoaded`; `logbook.Begin()` is idempotent ("only the first call
  loads from NVS"); everything else is a read through `GetStoredString`;
- **allocation:** `AllocateWeatherBuffers` returns at once when `wxBuffersOk` is set, and does not retry after
  `wxAllocTried`;
- **the web server** is not started in `Initialise` at all;
- **no function-local `static`** in it.

So the full `Initialise` is used. The bench's call runs where main.cpp's does: on the loop task, after
`framebuf::FrameDone()`, at the end of `RecordFrameUs`.
- **Its known effects, as a saved config has them:** the cloud airport list is cleared and fetched for the new home
  (`++mapAirportsGen`); the weather layer rebuilds; `MapConfigure`'s key change drops Bend and calls `Forget`.
- **For the reading:** the airport list's arrival can add one rebuild to an early P4 tick. CBF1 grades every
  `[map] draw: rebuilt in` line. CF5's passes line is the last rebuild of its tick.

**The regex check, as it runs.** `sabotage_map_draw.py` grades a plant AS PREDICTED only when the red set **parsed** from
the check's output equals its frozen set. So S-CB-BENCHLEAK reads AS PREDICTED only if its `FAIL CB-B1` line is parsed
as `CB-B1`, and a regex that could not read it would make the plant off-prediction. The run prints each plant's parsed
set.

### The mapbench run, as read (2026-11-12)

**The code** is `2887679b`, committed before step 1 on `893e1b1d` (the owner's terms and the 04:34:59Z ruling), which sits
on `1fe21b5b` (the 04:28:30Z ruling).
- **Steps 1-6, every one as predicted:**
  - `check_map_draw.py`: `11 checks, 0 failures`. CB-B1 counts 9 `MAP_BENCH` lines, all inside the 8 bench regions;
  - `sabotage_map_draw.py`:
    - 13 plants exactly their sets, and the 3 controls as their table;
    - 18 AS PREDICTED lines;
    - **the regex check, seen working:** S-CB-BENCHLEAK's raw `FAIL CB-B1: …` is parsed by the driver as {CB-B1}, and
      S-CB-BENCHPRINT's as {CB-B2};
  - `--anchors`: 40 drivers; the draw driver `14 plant(s), 14 anchor(s)`. Its control: `line 11 checks, 0 failures`;
  - lint: `40 driver(s), 0 flagged; 3 pinned line(s)`;
  - `check-no-bench-hooks.sh --selftest`: every case, the five new included, and `[map-bench]` planted and caught;
    `skus.py --validate`: OK, with 10 build-only legs;
  - `run.sh` under WSL (04:43:10Z to 04:51:18Z):
    - exit 2, with FAILED BLOCKS exactly the two environment blocks;
    - MapDraw 11 / 0, MapCompose 26 / 0, MapPlaces 32 / 0;
    - `40 driver(s): 39 run, 1 skipped`;
    - `sabotage_map_draw.py: 18 AS PREDICTED line(s)`.

**Step 7** ran after v19's step 5' closed and E2:08 was restored (the orchestrator's word, 05:52:17Z), in p4's slot,
with no board touched.
- **The comparator's controls,** rerun first on A1 and A2 (`imgcmp.py`, `dc828e47…`):
  - presence: 0x312C and 0x1007A;
  - anchor: 0 bytes outside;
  - sensitivity: exactly 1.
- **The tree:** main `e3c2508e` + `2887679b` = `f66755f7c109`, equal to `git merge-tree`'s. It is a local test merge,
  never pushed.
- **The builds,** 05:53:16Z to 06:02:21Z, every `pio` exit 0: C2 1,932,544 B; Cw2 1,973,344 B; M1 2,390,496 B.

| row | read |
|---|---|
| C against C2 | the same length, **0 bytes outside** (inside 32 / 32 / 1 / 5 / 6). **The bench does not reach the shipping image** |
| Cw against Cw2 | the same length, **0 bytes outside** (inside 32 / 32 / 1 / 6 / 5). **The bench does not reach the wxlook image** |
| **M1's size: OFF, on record** | M1 − Cw2 = **+417,152 B**, against the registered +417,496 to +421,496 B: **344 B below the band**. Ruled to stay off, with its cause (below), and not re-graded |
| M1's markers | `check-no-bench-hooks.sh --expect "[wx-bench],[map-bench]"`: `ok: … carries '[wx-bench],[map-bench]' and no other bench marker (anchor present)`. The plain scan of C2: `ok: no bench hook … (9 marker(s) checked, anchor present)` |
| M1's strings | `[map] draw: buffers refused` 1, `[map] draw: rebuilt in` 1, and `[map-bench] phase`, `alloc` and `passes` 1 each. C2 holds none of them. Cw2 holds the two `[map] draw` strings and no `[map-bench]` |

**Why the size row is off, read from the image with two direct `xtensa-esp32s3-elf-nm` lookups** (the retired
`symdiff.py` is not used):
- **The packages:** `_binary_test_fixtures_wx_basemap_bend_bmpk_end − _start` = **205,093 B**, and KSFO's = **211,403 B**.
  So **416,496 B**, exactly the files' sizes, packed contiguously.
- **The dead fetch path:** `AircraftManager::RequestBasemap` is **820 B in Cw2 and absent from M1**. The bench's
  `return false;` at its top makes the fetch body dead, and the compiler removes it.
- **So the bench code nets +656 B** (417,152 − 416,496): about **+1,476 B added, less the 820 B removed**.
- **My miss:** the band assumed the bench only adds code. It did not subtract what the bench's own hook makes dead.

**The ruling, verbatim (the orchestrator, 06:03:20Z):**

> the stop was right, and so was the read. The packages are exact (205,093 + 211,403 = 416,496 by end-start),
> RequestBasemap's 820 B is gone because the bench hook makes it dead, and both alternatives are named and handled. Your
> miss is recorded as you stated it: the band assumed the bench only adds.
> RULED (i):
> - M1's SIZE ROW stays OFF on record (+417,152 against +417,496..+421,496), with its cause. Not re-graded.
> - What the band was FOR is established by stronger direct evidence: the embedded packages present at EXACTLY their file
>   sizes, and the bench's net code +656 B, explained by +~1,476 added less the 820 B dead fetch path. Record both reads,
>   with the nm lookups that produced them.
> - (ii) is rejected: a band re-registered after the reading is a tolerance fitted to the data.
> - THEN RUN M1's string checks as registered: the --expect list, CB-W and the [map-bench] lines.
> C2 and Cw2 at 0 outside (shipping and wxlook untouched by the bench) are recorded as predicted, along with the
> comparator's controls. Nothing is pushed until step 7's remaining checks read.

**The CI for the push, predicted** (`1fe21b5b`, `893e1b1d`, `2887679b` and this record; `14764d14`'s run has finished
and was read):
- **35 = 31 success / 4 skipped** (`promote`, `publish`, `ship-alert`, `version`) / **0 failed**: `cf58c643`'s job set
  plus **`build (blipscope-s3-128-mapbench)`**.
- **The mapbench leg:**
  - the adsbdb launch gate's verdict is **PASS** from its default site (UNTRUSTWORTHY is off);
  - the new marker step reads `ok: … carries '[wx-bench],[map-bench]' and no other bench marker`.
- **Each host-tests leg:**
  - MapDraw `11 checks, 0 failures`, MapCompose 26 / 0, MapPlaces 32 / 0;
  - 40 drivers, with both passes `39 run, 1 skipped`;
  - lint 40 / 0 / 3;
  - AS PREDICTED: compose 15, **draw 18**, places 6;
  - `ALL HOST TESTS PASSED`.
- **The selftest step** runs every case, the five new included.
- **The named risk** is `board_build.embed_files` on CI's runner. Built locally, it was exact. A failure there is off.
- The tested merge is read from a checkout log. Any other reading stops.

### The CI read at `9ae188d9`, and the identity-table fix (predictions first)

**The read.** `9ae188d9`'s run finished at **35 = 30 success / 4 skipped / 1 failed**. The failure is `ota-identity`,
at `--check-table`: `fleet only: blipscope-s3-128-mapbench - s3-128`. That is off the prediction (35 = 31/4/0), so I
stopped there and did not read the other legs' logs. The next push's run is their reading.

**My miss.** A new env is listed in three files: `platformio.ini`, `skus.yml` and `test/fixtures/ota/identities.txt`.
I added it to the first two only.

**The ruling (the orchestrator, 06:27:06Z, verbatim):**

> From the orchestrator, 06:27:06Z, for map-client: the stop and the diagnosis are right. The failure names the exact
> missing row, and the flake alternative is excluded by the fixture's existing bench rows. Your miss is recorded as you
> stated it.
> The fix is APPROVED: the one row from `--emit-table`, reviewed as a one-line diff, a local --check-table, and
> --check-table added to your push guards. BUT the table you're editing has readers of its own. A row added there moves
> more than ota-identity. My read of main finds:
> - test/host/test_ota_identity.cpp:22/:110: "The counts are today's skus.yml (identities.txt …)". It PINS fleet
>   counts. This is exactly what p4 hit on #648 (CI2 OFF: host-tests failed on test_ota_identity.cpp's pinned counts
>   after adding 2 P4 rows);
> - test/host/test_ota_install.cpp:261 reads the table;
> - test/host/test_ota_model.cpp:37 (M1 over every row, so its count follows the table);
> - test/host/sabotage_ota_model.py:28 (its prediction follows the table's scoped rows);
> - run.sh :1984/:2007 (comments on those counts).
> So before the push:
> 1. List each reader, and predict what one more "- s3-128" row does to each: which pinned count moves, and to what.
>    Update any pin in the same commit.
> 2. Run those suites and drivers locally (test_ota_identity, test_ota_install, test_ota_model, sabotage_ota_model)
>    and record each reading against its prediction.
> 3. Then the push, with CI's prediction including those suites' counts.
> FYI for later: p4's #648 also adds rows to this table, so whichever lands second takes both. Name that in your record.

**One event before this record.** `--emit-table` writes `identities.txt` itself. I ran it expecting it to print to
stdout.
- It wrote one row, `blipscope-s3-128-mapbench - s3-128`, after `blipscope-s3-128-wxlook`.
- I copied that file and its one-line diff out as the reviewed diff.
- I then put the table back to HEAD (`git checkout HEAD --` on that one path); `git status` read clean.
- Nothing ran against the written table. The code commit after this record runs `--emit-table` again.

**The readers, and what one more `- s3-128` row does to each.** The table goes from 8 rows to 9. The scopes stay 3
(`-`, `eam-`, `weatherscope-`). The s3-128 rows go from 6 to 7, and those with no scope from 3 to 4.

| reader | what it counts | today | with the row |
|---|---|---|---|
| `check_ota_identity.py --check-table` | the table against the fleet | exit 1, `fleet only: …mapbench` | **exit 0** |
| `--selftest`, `--fleet-sabotage`, `--fleet` | the fleet (`skus.yml`, `platformio.ini`), not the table | PASSED, PASSED, PASS (`boards s3-128 9`) | unchanged |
| `test_ota_identity.cpp` R1 | envs x scopes | 24 (8 x 3) | **27** (9 x 3) |
| M1 | same-board pairs accepted, of envs² | 38, 26 | **51, 30** (7² + 1 + 1 = 51 of 81) |
| M2 | cross-board pairs x scopes | 78 | **90** (30 x 3) |
| M3 | own-scope matrix: accepted, refused (cross-scope) | 16, 48 (22) | **23, 58 (28)** (4² + 2² + 1 + 1 + 1 = 23; 49 - 21 = 28) |
| its check count | | 22 | 22 |
| `test_ota_install.cpp` O10 | rows, and rows x 2 pre-check heads refused as no-record | 8, 16 | **9, 18** |
| its check count | | 36 on this branch, 42 on main (HP1-HP6, #660) | 36 on this branch, 42 on the test merge |
| `test_ota_model.cpp` | `M1-<env>` per row, `M1r-<env>` per unscoped row, and M1b's 3 | `8 rows; 16 checks` | `9 rows; 18 checks` (no pin in the suite) |
| `sabotage_ota_model.py` | `MODEL_CHECKS`, its `--control` count | 16 | **18** |
| its S-M1 red set | `M1-` for every scoped row | the 3 scoped rows | unchanged: the new row has no scope |
| `sabotage_ota_identity.py`, `sabotage_ota_install.py` | the two suites; `IDENTITY_CHECKS` 22, `INSTALL_CHECKS` 36 here and 42 on main | | unchanged |
| `run.sh` :1984 and :2007 | comments, no counts | | unchanged |

**Why the last two drivers stay unchanged.** Every plant's red set is named by check id. The new row adds no
(board, scope) class the table lacks, since `- s3-128` already has three rows.

**The pins this moves, all in the code commit.**
- `test_ota_identity.cpp`: R1's `24`, M1's `38`/`26` with `(want 38, 26, 0)`, M2's `78` with `(want 78 of 78)`, and
  M3's `16`/`48`/`22` with `(want 16, 48 (22), 0)`.
- `test_ota_install.cpp`: O10's `rows == 8`/`16` with `(want 16, 0, 0, 0)`.
- `sabotage_ota_model.py`: `MODEL_CHECKS = 18`, its comment `(9 + 6 + 3 today, S4.5)`, and its docstring's `none of
  the 5 without` → `6`.
- Not edited: `docs/weatherscope-stage4.md` S4.5. It is a record of the counts at its time, and it is Weatherscope's
  record.

**#648.** p4's #648 also adds rows to this table (two P4 rows, per the ruling above), and they move the same pins.
Whichever of #648 and #659 lands second takes both. It runs `--emit-table` on the merged tree, and it derives every pin
above again from the merged table rather than by adding the two diffs.

**The local runs, predicted.** They run in WSL Ubuntu 24.04 with g++ 13.3, CI's compiler, and the drivers run with
`CXX=g++`. They run twice: on the branch head, and on the test merge of `origin/main` (`f3fee349`) and that head,
because CI tests the merge and main's install suite differs.
- **`check_ota_identity.py`:** `--check-table` exits 0, and the other three modes read as today.
- **`test_ota_identity`:** `22 checks, 0 failures`, with R1 27/27, M1 51/30/0, M2 90 of 90, and M3 23, 58 (28), 0.
- **`test_ota_install`:** O10 reads `9 identities x 2 pre-check heads: 18 no-record, 0 foreign, 0 other, 0 flash
  calls`. The summary is `36 checks, 0 failures` on the branch and `42 checks, 0 failures` on the merge.
- **`test_ota_model`:** `9 rows; 18 checks, 0 failures`.
- **`sabotage_ota_model.py`:**
  - the control is AS PREDICTED;
  - S-M1 fails exactly `M1-missileer-photo`, `M1-missileer-s3-128` and `M1-weatherscope-s3-128`;
  - S-M1b fails `M1b-short`;
  - the summary is `2 sabotages: ALL AS PREDICTED`;
  - `--control` reads `CONTROL sabotage_ota_model.py: 1 suite(s), all as the table`, with 18;
  - `--anchors` reads all present exactly once.
- **`sabotage_ota_identity.py` and `sabotage_ota_install.py`:** every plant AS PREDICTED, `--control` all as the
  table (22 and 36 on the branch, 22 and 42 on the merge), and `--anchors` all present.
- **`lint_driver_counts.py`:** exit 0.
- **The merge itself:** `origin/main` and the head merge with no conflict. Main's #660 hunks in `test_ota_install.cpp`
  sit at lines 23, 78 and 301 of the base; O10 sits between them.
- **Anything else** is off, and I stop.

**The local runs on the branch head (`935a2946`), as read** (06:37Z, WSL Ubuntu 24.04, g++ 13.3.0; the code commit
`935a2946` came before the run):
- **`test_ota_identity`:** `22 checks, 0 failures`. R1 `27/27 (9 envs x 3 scopes)`, M1 `51 accepted, 30 refused, 0
  off the rule`, M2 `90 of 90 refused`, M3 `23 accepted, 58 refused (28 cross-scope), 0 off the rule`. As predicted.
- **`test_ota_install`:** `36 checks, 0 failures`, O10 `9 identities x 2 pre-check heads: 18 no-record, 0 foreign, 0
  other, 0 flash calls`. As predicted.
- **`test_ota_model`:** `9 rows; 18 checks, 0 failures`. As predicted.
- **`sabotage_ota_model.py`:** the control AS PREDICTED; S-M1 failing `M1-missileer-photo`, `M1-missileer-s3-128`,
  `M1-weatherscope-s3-128`; S-M1b `M1b-short`; `2 sabotages: ALL AS PREDICTED`. `--control` `18 checks … (the table:
  checks 18)`, `all as the table`. `--anchors` `2 plant(s), 2 anchor(s), all present exactly once`. As predicted.
- **`sabotage_ota_identity.py`:** S-ID1b (7) and S-ID2b (9) AS PREDICTED, real headers unchanged; `--control` 22 and 36,
  all as the table; `--anchors` 2 of 2. As predicted.
- **`sabotage_ota_install.py`:** S-O8a (20), S-O8b (14) and S-ID3 (9) AS PREDICTED; `--control` 36; `--anchors` 3 of 3.
  As predicted. Its plants' O10 lines read 9 identities and 18 heads.
- **`lint_driver_counts.py`:** exit 0, `40 driver(s), 0 flagged; 3 pinned line(s)`. As predicted.
- **`check_ota_identity.py`, OFF.** `--selftest` read `selftest PASSED`, exit 0. `--fleet-sabotage`, `--fleet` and
  `--check-table` each read `BLIND: cannot run \`pio project config\`: [Errno 2] No such file or directory: 'pio'`,
  exit 2. The script defines exit 2 as BLIND: it cannot read the config, and it is never a pass.
  - **The cause:** WSL has no `pio`. My prediction put these runs in WSL without naming that block.
  - **What it is not:** it is not a reading of the table. Earlier today, from Git Bash on Windows (where `pio` is) and
    before the code commit, the same three modes read `fleet-sabotage PASSED`, `fleet PASS` (`11 built … boards s3-128
    9`) and `--check-table` exit 1 (`fleet only: …mapbench`), on the old table.
  - **What I did:** I stopped here. The test merge's runs have not started, and nothing was re-run.

**The ruling on the BLIND (the orchestrator, 06:38:29Z, verbatim):**

> From the orchestrator, 06:38:29Z, for map-client: every reader read as predicted. The pins moved as listed, the
> --emit-table write event is disclosed and handled, and the BLIND is the script's own defined "cannot read the config;
> never a pass". That's an environment block your prediction didn't name; your miss is recorded as stated. Not a
> finding about the table.
> RULED (a)-(c), with one change to (a): run the four check_ota_identity modes from POWERSHELL on Windows, not Git
> Bash. The machine's standing note is that pio under Git Bash/MSYS can damage its esptool package ("purge
> tool-esptoolpy* and rebuild from PowerShell"). `pio project config` probably doesn't touch it, but the safe shell
> costs nothing. The predictions stand as you wrote them: --selftest PASSED; --fleet-sabotage PASSED; --fleet PASS with
> "11 built … boards s3-128 9, s3-175-amoled 1, s3-21 1"; --check-table exit 0. Anything else is OFF.
> Then (b), the test-merge runs on f3fee349 + your head, as predicted in 62ddbe6a (install 42 with O10 9/18; the
> drivers' --control at 42). Then (c), the push under the standing rule, with the extended guard chain and the CI
> prediction as you listed it. Your #648 note ("re-run --emit-table on the merged tree and re-derive every pin, not add
> the diffs") is exactly right; I'll pass it to p4.

**(a) `check_ota_identity.py` from PowerShell on `af42fdbe`, as read (06:39Z).** All four modes read as predicted:
- `--selftest`: `selftest PASSED`, exit 0.
- `--fleet-sabotage`: `fleet-sabotage PASSED`, exit 0.
- `--fleet`: `fleet: 11 built skus.yml env(s) (8 retired, skipped); slug-ful 1; boards s3-128 9, s3-175-amoled 1, s3-21
  1` and `fleet PASS`, exit 0.
- `--check-table`: `ok: … identities.txt equals the fleet (9 rows)`, exit 0.

**(b) The test merge, as read.** The merge is `c782dc3e`, with parents `f3fee349` (main) and `af42fdbe`. It merged
with no conflict. The suites and drivers ran in WSL with g++ 13.3.0:
- **`test_ota_identity`:** `22 checks, 0 failures`; R1 27/27, M1 51/30/0, M2 90 of 90, M3 23, 58 (28), 0.
- **`test_ota_install`:** `42 checks, 0 failures`; O10 `9 identities x 2 pre-check heads: 18 no-record, 0 foreign, 0
  other, 0 flash calls`.
- **`test_ota_model`:** `9 rows; 18 checks, 0 failures`.
- **`sabotage_ota_model.py`:** S-M1 (the 3 scoped rows) and S-M1b AS PREDICTED; `--control` 18; `--anchors` 2 of 2.
- **`sabotage_ota_identity.py`:** S-ID1b (7) and S-ID2b (9) AS PREDICTED; `--control` 22 and **42**; `--anchors` 2 of 2.
- **`sabotage_ota_install.py`:** main's version, with 5 plants. S-O8a (22), S-O8b (14), S-ID3 (9), P-NORETRY (6) and
  P-BYPASS (6) all read AS PREDICTED, and its wiring rows read green with the detectors red. `--control` **42**;
  `--anchors` 5 of 5.
- **`lint_driver_counts.py`:** `40 driver(s), 0 flagged; 3 pinned line(s)`.
- **The wrapper itself (not a reading).** It exited 2 with `syntax error: unexpected end of file`, after every step
  above had run. I had cut the wrapper's last block (the checker modes, run in (a) instead) with a `sed` that removed
  that loop's body and `done` but left its `for … do` line. The error is my wrapper's, not any check's. Its outputs are in the scratch
  directory, one file per step.

**The push, predicted** (after the local runs read as predicted; `9ae188d9`'s run has finished):
- **The guard chain** gains `skus.py --validate`, `check-no-bench-hooks.sh --selftest` and `check_ota_identity.py
  --check-table`.
- **35 = 31 success / 4 skipped / 0 failed**, with `ota-identity` green.
- **Each host-tests leg:**
  - `test_ota_identity` `22 checks, 0 failures`, `test_ota_install` `42 checks, 0 failures`, `test_ota_model`
    `9 rows; 18 checks, 0 failures`;
  - MapDraw `11 checks, 0 failures`, MapCompose 26 / 0, MapPlaces 32 / 0. Main's FillRing fix (`f3fee349`, #662) is
    in the test merge for the first time. #662's own CI was green, and my advance read of it predicts no movement here;
  - 40 drivers, with both passes `39 run, 1 skipped`;
  - lint 40 / 0 / 3;
  - AS PREDICTED: compose 15, draw 18, places 6;
  - `ALL HOST TESTS PASSED`.
- **The mapbench leg:** the launch gate is PASS, and the marker step reads `ok: … carries '[wx-bench],[map-bench]' and
  no other bench marker`.
- **The tested merge** is read from the checkout log. Any other reading stops.

**The CI for `84bd05a2`, as read** (finished 06:58Z). Every item read as predicted:
- **The tally:** 35 = 31 success / 4 skipped (`promote`, `publish`, `ship-alert`, `version`) / 0 failed.
- **The tested merge:** every leg's checkout log reads `HEAD is now at eae4745 Merge 84bd05a2… into f3fee349…`.
- **`ota-identity`:**
  - `selftest PASSED`;
  - `fleet-sabotage PASSED`;
  - `fleet: 11 built skus.yml env(s) (8 retired, skipped); slug-ful 1; boards s3-128 9, s3-175-amoled 1, s3-21 1`
    and `fleet PASS`;
  - `--check-table`: `… identities.txt equals the fleet (9 rows)`.
- **Each host-tests leg** (ubuntu-24.04 and ubuntu-26.04, identical):
  - the OTA suites: identity `22 checks, 0 failures` (R1 27/27, M1 51/30/0, M2 90 of 90, M3 23, 58 (28), 0), model
    `9 rows; 18 checks, 0 failures`, install `42 checks, 0 failures` with O10 at 9 identities and 18 no-record;
  - MapDraw `11 checks, 0 failures`, MapPlaces 32 / 0, MapCompose 26 / 0;
  - `40 driver(s): 39 run, 1 skipped` in both passes;
  - `LINT driver counts: 40 driver(s), 0 flagged; 3 pinned line(s)`;
  - AS PREDICTED lines: compose 15, draw 18, places 6;
  - `ALL HOST TESTS PASSED`.
- **The mapbench leg:**
  - the launch gate reads `blipscope-s3-128-mapbench found 0 1 PASS`;
  - the marker step reads `ok: … carries '[wx-bench],[map-bench]' and no other bench marker (anchor present)`.

### CBF3's reader: its design, registered before its code (2026-11-12)

The window plan's CBF3 row says what is read (`/diag/fb` frames in P1's "on" ticks) and what is predicted. Inside the
wedge's sector, under 2 % of pixels are the map's colours. Outside it, on the disc, over 80 % are. The row does not say
how a pixel is called a map colour. This section fixes that, before any reader code.

**What the source draws, read before choosing.**
- **The wedge in P1** is the opaque one (`DrawRadarSweep`, `include/DrawHelpers.h`), because the weather is off and
  `DrawSweepOverWeather` returns false.
  - It is 18 triangles of `(0, lvl, 0)`, with `lvl = (1 - i/18)² x 120`, over `TAIL = 1.0` rad behind the beam.
  - The beam and its head are `DrawRadarSweepBeam`'s, shared with the weather path.
  - In RGB565 the last four slices are `0x0000`, and slice 14 is `0x0020`.
- **The map's colours** come from `include/MapLayer.h`'s Dark row: land (6, 8, 10), water, river, road, and the
  terrain ramp `land + (shade - 128) x 0.075` per channel. The names add place grey on a halo in the land colour,
  through `wxraster::Text` and `Fill`, with no blending. `To565` truncates, as `color888` does.
- **The overlap.** The only RGB565 colours that are both the map's and the wedge's are `0x0000` and `0x0020`, the
  terrain's darkest slopes. The ramp's blue sits two above its green, so no other pure green is reachable.
- **Bend's share of them, on the host.** A scratch program built Bend's layer at the package centre with
  `maplayer::Build` and printed counts only. Neither E2:08's radius nor its `radar-up` setting is known here, so it
  covered radii of 5, 10, 25, 50, 100 and 150 mi at rotations 0 and 90:
  - non-black map colours are **99.5-100 %** of the disc at every radius and both rotations;
  - `0x0000` and `0x0020` together are **0-1.4 %**;
  - no pixel had a colour outside the theme.
- **The overlays drawn after the wedge** each have their own colours. All of them are read from the source:
  - the three range rings, located by position with LovyanGFX's own `drawCircle`, as `scripts/wx-fb-check.py` ports
    it;
  - the planes' colours and the labels' colours, at fades 0.30-1.0;
  - the NEAR/HIGH/FAST highlight and the screen dots;
  - the beam and the head;
  - the airports' mark (130, 130, 130), label (100, 100, 100) and code text (140, 140, 140). `wx-fb-check.py` does not
    read these, because the weather checks never needed them, so this reader adds them.

**The method: colour classes, not a per-pixel reference.**
- **Each pixel gets one class:**
  - MAP: a map colour that is not a wedge or overlay colour;
  - WEDGE: a wedge colour that is not a map or overlay colour;
  - AMBIGUOUS: a colour in two or more of the three sets;
  - OVERLAY: an overlay colour, or a ring pixel showing its ring's colour, by position;
  - UNKNOWN: anything else, counted and listed.
- **The measure** in a region is MAP / (MAP + WEDGE + AMBIGUOUS). Overlays and unknowns are left out of both terms.
- **The regions:**
  - the centre's 12 px are never graded;
  - **inside** is the angles from θ − 1.0 + 0.06 to θ − 0.06, where θ is the beam;
  - **outside** is the disc beyond θ − 1.0 − 0.06 to θ + 0.06;
  - the two 0.06 rad margins are not graded. They are wx-fb-check's own margin.
- **The rejected alternative** is a per-pixel host reference. It would need E2:08's radius and rotation (its own
  settings, which this session does not read) and the runtime airport list (names avoid airport boxes). That means new
  bench prints and a re-identified image, for a row that is reported as step C's input.

**The frames.**
- **What is fetched:** P1's 40 ticks, 20 with the map on and 20 off, the weather off throughout. `/diag/fb` is fetched
  three times per tick (the partial-frames rule).
- **How each frame is labelled:** by the tick's own `[map-bench] phase P1 map=on|off …` line. A fetch is read only if
  no new phase line arrived between its request and its response, and only with `X-Blipscope-Frame-Finished: true`.
  Any other frame is counted and not read.
- **Trust, per frame**, by the rules `wx-fb-check.py` already enforces. The reader imports them rather than copying
  them:
  - every ring shows its colour on 80 % of its pixels;
  - there is exactly one head blob (joined across one ring pixel), 117-123 px out;
  - the beam's ray agrees on at least 70 of 101 samples;
  - the tail is `DrawHelpers.h`'s `TAIL`, read and asserted, not `WxSweep.h`'s.
  - A frame failing any of these is UNTRUSTWORTHY: counted and not graded. The reader also cross-checks every frame's
    verdict against `wedge_mask`'s; a disagreement stops the reading.
- **The unknown ceiling:** a region with more than 10 % UNKNOWN pixels makes its frame UNTRUSTWORTHY. The classes
  would not describe that frame.

**Predicted, per trustworthy frame:**
- **On ticks:** inside **under 2 %**, and outside **over 80 %**. The host's 99.5 % is for the layer alone; the
  overlays are out of the measure.
- **Off ticks, the reader's control on real frames:** outside **under 2 %**. With the map off the disc is black, which
  is AMBIGUOUS, not MAP. A reader that called an off frame mapped could not tell on from off.

**The verdicts, every one before the capture:**
- **AS PREDICTED:** every trustworthy on-frame reads inside under 2 % and outside over 80 %, and every trustworthy
  off-frame reads outside under 2 %, with at least 10 trustworthy frames of each kind. This is reported as step C's
  input, not as a defect.
- **OFF:** an on-frame inside at 2 % or more (the map shows through the opaque wedge), or outside at 80 % or less
  (an "on" tick without the map). Stop and report.
- **CONTROL FAILED:** an off-frame outside at 2 % or more. The reader is the instrument, so stop.
- **BLIND:** fewer than 10 trustworthy frames of either kind.
- **Anything else:** the instrument is unreliable at this scale. Stop and decide.

**Plants, run red before the real frames are read.** These are synthetic frames drawn from the source's own colours
and geometry, in the reader's `--selftest`:
- a control on-frame (map, wedge, rings, a head): AS PREDICTED;
- the sector holding map colours (no wedge): OFF, inside;
- an on-labelled frame black outside: OFF, outside;
- an off-labelled frame holding map colours outside: CONTROL FAILED;
- two head blobs, and then the rings missing: UNTRUSTWORTHY each;
- a region 15 % unknown colour: UNTRUSTWORTHY;
- too few frames: BLIND.

**Where it lives:** `--fb` in the scratch reader `map_bench_read.py`. Its sha is frozen again before the window, as
every row's reader is.

**Privacy.**
- The frames show a place, so they stay in the scratch directory and are never committed.
- The record carries counts and shares only.
- The fetcher never prints the board's host name or address.

**Questions for the ruling:**
- **Q-CBF3-1, the method:** colour classes, over a per-pixel reference. I recommend the colour classes.
- **Q-CBF3-2, the control:** the off ticks as the reader's control on real frames, with outside under 2 %, and at
  least 10 trustworthy frames of each kind.
- **Q-CBF3-3, the dependency:** the reader imports `wx-fb-check.py`'s ring and beam rules. That is a dependency on a
  Weatherscope script: a change there moves this reader too, or breaks it closed. The alternative is a copy that can
  drift. I recommend the import with the per-frame cross-check.
- **Q-CBF3-4, the unknown ceiling:** 10 % of a region.

**The ruling (the orchestrator, 07:09:07Z), in short:** Q-CBF3-1 colour classes, ACCEPTED. Q-CBF3-2 the off ticks as
the control, at least 10 trustworthy frames of each kind, ACCEPTED. Q-CBF3-3 IMPORT, not copy, with two additions:
`wx-fb-check.py`'s sha256 is recorded in the reading, and the orchestrator tells Weatherscope that this reader imports
its ring and beam rules. Q-CBF3-4 the 10 % unknown ceiling, ACCEPTED, with the 15 % plant reading UNTRUSTWORTHY. GO for
the code and `--selftest`, every plant red, the sha frozen and recorded; no bench-image change.

**CBF3's reader, as written and frozen (07:15Z).**
- **The reader:** `map_bench_read.py`, the scratch reader every row uses, gains `--fb <tree> <frames.jsonl>`. Its log
  grading is unchanged. `--selftest` now takes the source tree, because the frame plants are drawn from the source's
  own colours.
  - **Frozen sha256:** `65d2edf53b0600f611945019a43038f277ec4bab5a7f85dcbd43bfb93cffecd7`. The copy before it,
    `6413d64985b3…`, is kept beside it.
  - **`scripts/wx-fb-check.py`'s sha256, as the reader prints it:**
    `92d85ce4e711fd05265d85b3bf9edbb7226ca2915233f76224d769f9d3799c10`.
- **What it reads from the tree:**
  - the opaque wedge: TAIL 1.0, 18 slices, PEAK 120, and the fade, each asserted. Its levels, computed in float32 as
    the device computes them, are 107, 94, 83, 72, 62, 53, 44, 37, 30, 23, 18, 13, 9, 5, 3, 1, 0, 0;
  - the map: the Dark row, the ramp's formula, `To565` and `OffDisc`, each asserted. That is 13 RGB565 colours;
  - the overlays: 252 colours;
  - the shared colours, computed: `0000` and `0020`. If the source ever yields another set, the reader refuses
    (re-derive), because that set is registered.
- **The selftest: 23 lines, all ok, `selftest PASSED`, exit 0.**
  - The log reader's 10 are unchanged.
  - The frame cases, each with its exit and the plant's own status:
    - the control set (10 on, 10 off): AS PREDICTED;
    - the sector holding map colours: OFF, inside 100 %;
    - black outside: OFF, outside 0 %;
    - an off frame holding map colours: CONTROL FAILED;
    - two head blobs: UNTRUSTWORTHY, "2 head blobs";
    - no rings: UNTRUSTWORTHY, "a ring under 80 % of its colour";
    - 15 % unknown: UNTRUSTWORTHY, "14.4 % UNKNOWN";
    - 9 on frames: BLIND;
    - unfinished, and straddled: NOT READ.
- **Added beyond the registered list:** the cross-check against `wedge_mask` must be able to fire, so the reader's
  own locator is sabotaged in process, twice, and restored:
  - shifted by 0.2 rad: INSTRUMENT, exit 2;
  - refusing a frame `wedge_mask` locates: INSTRUMENT, exit 2;
  - a check that the locator is restored: ok.
- **The disk path, end to end:** 20 synthetic frames written as RGB565 big-endian with a manifest read AS PREDICTED,
  exit 0, and the printed sha equals `sha256sum`'s. A malformed manifest reads INSTRUMENT, exit 2. A missing tree
  reads INSTRUMENT, exit 2.
- **What the synthetic control showed about the classes:** inside the wedge, most of its greens are also faded plane
  colours, so they class as AMBIGUOUS. On the control on-frame that was 4,220 AMBIG and 1,947 WEDGE pixels, with MAP 0.
  The measure keeps both in its denominator, and a map pixel showing through is still MAP, so the inside test is not
  weakened.
- **A correction to the registration.** It listed the airports' greys as three: 130, 100 and 140. `DrawAirports`'
  body names two, the mark (130) and the label (100). The 140 grey is `DrawWeatherNote`'s text. If that note draws in
  P1, its pixels count as UNKNOWN, and the 10 % ceiling bounds them.

**Still to come before the window:** the fetcher, which writes the manifest this reader reads (`file`, `label`,
`finished`, `straddled`). It labels each fetch by the latest `[map-bench] phase P1 map=…` line, marks a fetch
straddled when a new phase line lands before its response, and prints no host name or address. It will be registered
the same way, with its own plants, before it runs.

### CBF3's fetcher: its design, registered before its code (2026-11-12)

The reader above reads a manifest of frames. This is what writes it during the window. Nothing about the bench image
changes.

**What it does.**
- **Its inputs:**
  - the serial trace's log file, written by `scripts/bench-trace.ps1` (one open, never repeated), which the fetcher
    only reads;
  - the board's address, from an environment variable set in the window's own shell. The fetcher never prints it,
    and no file it writes holds it.
- **For each P1 tick:**
  - it waits for a new `[map-bench] phase P1 map=on|off …` line in the log;
  - it fetches `/diag/fb` at +5, +10 and +15 s after that line arrived, inside the 30 s tick and clear of both ends;
  - it labels each fetch with that line's `map=` value.
- **Per fetch:**
  - the phase-line count is read before the request and again after the response. A change means `straddled: true`;
  - `X-Blipscope-Frame-Finished` gives `finished` (a missing header is `false`);
  - the body is kept only if it is 115,200 bytes, `X-Blipscope-Frame-Format` is `RGB565BE`, and the width and height
    are 240;
  - any other response (a 503, a short body, a timeout) is counted and named in the fetcher's own summary, and is not
    written to the manifest.
- **When it ends:** after the first P2 phase line, or a stated timeout. It prints counts only: ticks seen, frames
  kept, not kept by reason, straddled, and not finished.

**Predicted, for the window:** 40 P1 ticks (20 on, 20 off) and 120 fetches. P1's frame rate leaves room for a
finished frame within the server's bound, so nearly every fetch should be finished, and straddled should be 0, since
every fetch sits 5 s from either end of its tick. The reader's own minimum, 10 trustworthy frames of each kind, is
what decides BLIND, not this fetcher.

**Plants, run red before it touches the board.** It runs against a stand-in on this machine: a local HTTP server
serving synthetic frames with the device's headers, and a log file the test appends phase lines to.
- the normal case: the labels and the file count as predicted;
- a phase line appended while a response is held: `straddled: true`;
- `Finished: false`, and then the header missing: `finished: false`;
- a 503 and a short body: not written, and counted under its reason;
- the address must not appear in anything it prints or writes: grep the output and every file for the stand-in's
  address string. A hit is red.

**Questions for the ruling:**
- **Q-CF-1, the fetch times:** +5, +10 and +15 s after each phase line.
- **Q-CF-2, the address:** taken from an environment variable that is never printed. The alternative is resolving
  the board's mDNS name in the fetcher, which would put the name in the fetcher's code or its arguments.

**The ruling on the fetcher (the orchestrator, 07:17:28Z), in short:** Q-CF-1 (+5/+10/+15 s) ACCEPTED; Q-CF-2 (the
address from an unprinted environment variable) ACCEPTED. One interference was to be settled before code: a
`/diag/fb` fetch sends 115 KB over Wi-Fi while P1's frames are timed, so it can move CF1, CF2-B and CF3-B. The choices
offered were (i) excluding frames inside a fetch, (ii) fetching alternate pairs only, or (iii) a measured number.

**Chosen: (ii), alternate pairs.**
- **Why not (i):** the timing rows read `[health]` lines, which are 30 s aggregates. Excluding single frames would need
  marks the bench image does not print, and the image is not to change.
- **Why not (iii):** there is no existing measurement of `/diag/fb`'s effect on frame time on this board to cite.
  (ii) produces one.
- **The rule, one place in each tool:** P1's ticks are numbered t = 0..39 from its first phase line. The reader pairs
  them (2k, 2k + 1). The fetcher fetches only in the **even pairs** (k even, so t mod 4 is 0 or 1): 10 pairs, 10 on
  ticks and 10 off ticks, 30 frames of each kind.
  - Every `[health]` line, `BUDGET BROKEN` included, belongs to the interval of the last phase line before it (the
    reader's attribution rule). Each fetch ends within about 16 s of its tick's 30 s, so its interval is the fetched
    one.

**Predicted, the timing bands unchanged, graded over the 10 odd (un-fetched) pairs:**
- CF1: map off 3.01 ± 0.3 ms, map on 5.08 ± 0.3 ms;
- CF2-B: every un-fetched P1 interval's sweep 2.04 ± 0.3 ms, and `blend=0` over **all** of P1, since a fetch cannot
  change which wedge path runs;
- CF3-B: the mean of (on − off) p95 over the 10 un-fetched pairs, +2.07 ± 1.0 ms;
- CF6: 0 `BUDGET BROKEN` lines outside the fetched intervals, in every phase.

**Reported, not graded:** the fetched pairs' clear, sweep and p95, and their `BUDGET BROKEN` count, printed beside the
graded figures. That is the measured cost of a fetch, the number (iii) asked for.

**The control, both directions, as log plants in the reader's `--selftest`:**
- **Exclusion:** a fetched interval inflated (sweep 5.0 ms, p95 +10 ms, one `BUDGET BROKEN`) leaves every row green,
  and the inflation appears in the reported figures.
- **Sensitivity:** the same inflation in an un-fetched interval turns CF2-B, CF3-B and CF6 red.
- **The frame side:** the manifest gains `tick` (t). The `--fb` reader refuses, as the INSTRUMENT, a frame whose tick is
  not in an even pair, so the fetcher cannot drift from the rule. A plant shows it.

**Then:** the reader's changes and the fetcher, with all plants red first, and the reader frozen again.

**The reader and the fetcher, as written and frozen (07:24Z).**
- **The reader, frozen again:** `map_bench_read.py` at
  `5ad12d0adb0de643248308be272805d63bca6a6b1a7f3501577c39efa8bef9df`. That replaces `65d2edf53b06…`. The change is the
  (ii) rule:
  - `fetched_tick` is the one place it lives;
  - `BUDGET BROKEN` is attributed to its interval;
  - CF1, CF2-B's sweep, CF3-B and CF6 are graded over the un-fetched pairs, and the fetched pairs' figures are printed
    beside them;
  - the manifest's `tick` is checked.
- **Its selftest: 27 lines, all ok, `selftest PASSED`.** The 23 before, plus four:
  - **the exclusion:** a fetched interval inflated reads 0 red, and the inflation shows only in the reported figures
    (sweep 2.04-5.00, p95 +4.10, `BUDGET BROKEN` 1);
  - **the sensitivity:** the same in an un-fetched interval reads exactly CF2-B, CF3-B and CF6 red. Its p95 plant is
    +20 ms, because +10 over one pair in ten moves the mean by exactly 1.0 ms, inside the ±1.0 band, so it could not
    have failed;
  - **a manifest frame:** tick 0 is read, and tick 2 is refused as the INSTRUMENT.
  - **The old CF6 plant** sat in t = 1, a fetched interval, so it moved to t = 2 and still reads exactly CF6 red.
- **The fetcher:** `fetch_fb.py` at `7b904453a45bb014d825c599979e502adb89be8a59653c260ae2b2c167ca5c40`.
  - It imports `fetched_tick` from the reader rather than restating it.
  - It reports an error by its class name alone, since an exception's text can carry a URL.
  - It reads a UTF-16 trace log as well as a UTF-8 one.
- **Its selftest: 10 lines, all ok, `fetcher selftest PASSED`, against a local stand-in:**
  - it ran to P2;
  - exactly 12 requests, in ticks 0, 1, 4 and 5 only;
  - the labels follow the phase lines;
  - 9 kept and 3 not kept: a 503, a short body, and a format other than RGB565BE;
  - Finished false, and the header missing, both read `finished: false`;
  - the response held across a tick, and only that one, reads `straddled: true`;
  - every kept body is 115,200 bytes;
  - the address is in no output and no file, and the control finds a planted address.
  - With no address set, it is the rig, exit 2, and writes nothing.
- **The two together:** `interop_fb.py` (`8a9f63b99dd3…`) ran the fetcher against a stand-in serving the reader's own
  synthetic frames, then the reader on what the fetcher wrote. As predicted: 12 frames from ticks 0, 1, 4 and 5, all
  trustworthy (6 on, 6 off), BLIND (exit 4, under 10 of each), with no INSTRUMENT.

**The CI for `18a7a8ff` (docs only), as read** (finished 07:24Z). Every item read as predicted:
- 35 = 31 success / 4 skipped (`promote`, `publish`, `ship-alert`, `version`) / 0 failed;
- the tested merge, from every leg's checkout: `3c425a7 Merge 18a7a8ff… into f3fee349…`;
- `ota-identity`: `fleet PASS`, and the table equals the fleet (9 rows);
- both host legs: OTA identity `22 checks, 0 failures` (R1 27/27), OTA model `9 rows; 18 checks, 0 failures`, OTA
  install `42 checks, 0 failures` (O10 18 no-record); MapDraw 11 / 0, MapPlaces 32 / 0, MapCompose 26 / 0; `40
  driver(s): 39 run, 1 skipped`; lint 40 / 0 / 3; compose 15, draw 18 and places 6 AS PREDICTED lines; `ALL HOST
  TESTS PASSED`;
- the mapbench leg: the gate reads `found 0 1 PASS`, and the markers are `'[wx-bench],[map-bench]'` and no other.

**This push's guards, in their corrected form.** The HEAD check and the previous run's completion are PowerShell
statements that `throw`, run before the `&&` chain of process checks. They are not links in it.

## Step C: the wedge's green added to the map (design, registered before code; 2026-11-12)

**What is fixed already.**
- **S7** (`docs/blipscope-basemap-client.md`; Q-MC2, 2026-11-07): the wedge's green is added to the map pixel below
  it, so the map stays visible under the beam. Over black, this is bit-identical to today's opaque wedge. The owner
  judges it on glass.
- **CB10** (`src/AircraftManager.h:51`) refuses every map build without `BLIPSCOPE_MAP_STEP_C`, the bench images
  excepted. "Step C's header defines it", and CD4 holds the list of allowed definers empty until that header is named.
- **CF2 and CF3 moved here** (Q-MB12): the sweep with the map on, weather off, at 7.7-8.9 ms, and frame p95 on − off
  at +6.2 to +10.5 ms.

**Read first, from the code at `73351313` and not from any run.**
- **One function colours every wedge pixel:** `wxsweep::detail::BlendOne` (`include/WxSweep.h`), shared by `Blend`
  and `BlendRows`. A drawn weather code gets `Lit565`, look 4 raised toward 100 %. Anything else gets
  `Green565(lvl)`, which writes over whatever is there.
- **Each wedge pixel is blended once.** `Rasterise` marks each pixel with the LAST slice that covers it, and `Blend`
  visits it once and clears the mark. An additive blend depends on that, because a pixel added twice would brighten.
- **The gate:** `DrawSweepOverWeather` runs only when `wxShowing == Crop` and the weather's buffers are allocated.
  Otherwise the opaque `DrawRadarSweep` draws. The weather's slice and spans are part of its all-or-none set
  (`AllocateWeatherBuffers`), so with the weather off they may not exist.
- **The frame's background:** `DrawMapBase` returns true exactly when it copied the composite this frame. Its callers
  pass `nullptr` (the map alone) or `wxLayer565` (the weather composed over the map).
- **Which images compile the map:** `BLIPSCOPE_MAP_FETCH` needs `FEATURE_CLOUD_FEED` and `WX_LAYER_SHIPS`, so wxlook
  and mapbench compile it today and the shipping env does not.

**The design.**
1. **The add, in RGB565:** `mapsweep::Add565(px, lvl)` raises the pixel's 6-bit green by `lvl >> 2`, saturating at
   63, and leaves red and blue as they are.
   - Over black, that is `(lvl >> 2) << 5`, which equals `Green565(lvl) = (lvl & 0xFC) << 3` for every `lvl`. That
     is S7's "bit-identical over black", as an identity a test can sweep.
   - The map's brightest colour is the place grey, green 21 of 63. The largest level, 107, adds 26, so no map pixel
     saturates. The saturation is there for correctness, not because the map reaches it.
2. **Its home: a new header, `include/MapSweep.h`.** It includes `WxSweep.h` and reuses `Rasterise`, `SliceLevel` and
   `Lit565` unchanged. It adds `Add565`, and a `Blend` whose per-pixel rule is: a drawn code gives `Lit565`, as today;
   anything else gives `Add565(get(x, y), lvl)`.
   - It defines `BLIPSCOPE_MAP_STEP_C`, and is included in `AircraftManager.h`'s map draw region before CB10.
   - **`WxSweep.h`, `test_wx_sweep.cpp` and `sabotage_wx_sweep.py` are not touched.** They are the weather's,
     registered under WD6.
3. **The gate, in a map draw region of `DrawSweepOverWeather`.** When `DrawMapBase` drew the composite this frame,
   the map's blend runs.
   - The code at each pixel comes from `wxPixels` when the composite included the weather, and is 0 when the map
     showed alone.
   - Two per-frame flags, set by `DrawMapBase` and cleared at the start of each frame's clear, say which case applies.
   - Otherwise the gate is today's: the weather's blend over the crop, or the opaque wedge.
4. **The buffers:** the map gets its own slice (57,600 B) and spans (960 B) in `mapcompose`'s all-or-none set, zeroed
   when allocated.
   - Taking the weather's instead would move WD5d's registered price, a Weatherscope reading.
   - Q-MB1's price, and so CBF4's, grows by **58,560 B**.

**What the second path must establish** (CLAUDE.md, "when you add a second path"). Today's gate establishes four
things before it blends. The map's gate must re-establish each:
- **the frame's background is known:** the composite was copied this frame, by `DrawMapBase`'s own return;
- **the codes match the pixels:** `wxPixels` is read only when the composite was built from this frame's weather
  layer. With the map alone, the code is 0, never a stale crop's;
- **the slice is clean:** the map's slice is zeroed at allocation, and `Blend` clears what it visits;
- **the sprite's format:** one band, 240 x 240, 16-bit, the same check `DrawSweepOverWeather` makes today.

**Rows, host (a new suite `test_map_sweep.cpp`, with a driver `sabotage_map_sweep.py`, plants only in
`include/MapSweep.h`):**
- **CS1:** for every `lvl` 0-255, `Add565(0, lvl) == Green565(lvl)`.
- **CS2:** for every pixel value and every level the wedge uses, red and blue are unchanged, green is `min(63, g +
  (lvl >> 2))`, and the result never darkens.
- **CS3, today's wedge over black:** the map's blend over an all-black frame with every code 0 equals
  `wxsweep::Blend`'s output, byte for byte, at 360 beam angles.
- **CS4, the weather unchanged:** over a frame with drawn codes, every drawn-code pixel equals `wxsweep::Blend`'s, for
  both looks.
- **CS5, once per pixel:** over a random frame at 360 angles, every pixel equals its single-add value computed from a
  snapshot. Outside the wedge, the frame is unchanged, and the slice is all zero after the blend.
- **CS6, the map visible:** at Bend's composite on the host, every wedge pixel is "a map colour plus green", never a
  pure wedge green, except where the map itself is black.
- **Plants, each red on its own rows:** the add without saturation; the add into red; `Green565` instead of the add
  (step B's behaviour); a double blend; the slice not cleared; the codes taken from a stale crop.

**Rows, source (`check_map_draw.py`):**
- **CD4** names `include/MapSweep.h` as the one allowed definer of `BLIPSCOPE_MAP_STEP_C`. A definition anywhere else
  is still red.
- **A new row:** the map's blend is called only inside a map draw region, and only behind `DrawMapBase`'s flag.
- Their plants run as step B's did.

**Rows, the image.**
- **The shipping env:** 0 bytes outside the five regions against `main`'s, with the comparator.
- **wxlook and mapbench:** they change, as designed, and their strings are named.
- **CB10's own plant:** without step C's header, the build is refused.

**Rows, the bench** (a mapbench image built with step C, in the same procedure and window rules as step B):
- **CF2:** the map-on, weather-off sweep at 7.7-8.9 ms, `blend` equal to the interval's frame count on map-on ticks
  and 0 on map-off ticks. Map-off sweeps stay at 2.04 ± 0.3.
- **CF3:** frame p95, on − off, +6.2 to +10.5 ms.
- **CF4:** re-read with the weather on, −1.0 to +2.2 ms.
- **CBF4-C:** the price plus 58,560.
- **CBF3-C:** `--fb` gains a MAPGREEN class (a map colour plus a level's green). Inside the wedge on map-on ticks,
  MAPGREEN is over 80 % of the measure, and pure wedge greens under 2 %. Off ticks are the control, as before.
- **CBF2-C:** the owner on glass (S7, Q-MC2). It is his window.
- **The prediction for CF2's band has a known weak point.** It came from #445's F run (blended sweep 7.68 ms). The
  B1 span fix landed after that, so the real blended sweep may now be lower. The step B window's P2 reading (weather
  on, blended) measures it. **If P2's sweep falls outside 7.7-8.9, CF2's band is registered again, from that number,
  before step C's window and with a ruling.** It is not fitted after step C's reading.

**Questions for the ruling:**
- **Q-C1, the add:** in RGB565 (recommended: exact over black, one add per pixel), or in 8-bit with a requantise.
- **Q-C2, the home:** a new `include/MapSweep.h`, leaving `WxSweep.h` and its suite and driver untouched
  (recommended), or the change made in `WxSweep.h`'s `BlendOne` for every caller.
- **Q-C3, the buffers:** the map's own slice and spans, +58,560 B (recommended), or a shared pair, which would move
  WD5d.
- **Q-C4, the branch:** step C's code on a branch from #659's head, and its PR opened after #659 merges (recommended),
  or wait for #659 before writing any code.
- **Q-C5, the bench:** one window that flashes the step B image and then the step C image, each identified before the
  window and E2:08 restored at its end; or two windows. The rows of the two images contradict each other (CF2-B
  against CF2, CBF3 against CBF3-C), so neither image's rows are read from the other.

**The ruling on step C's design (the orchestrator, 07:32:23Z), in short:**
- **Q-C1:** the add in RGB565 (`g6 += lvl >> 2`, saturating). ACCEPTED.
- **Q-C2:** a new `include/MapSweep.h`, with `WxSweep.h`, its suite and its driver untouched (WD6's). ACCEPTED.
- **Q-C3:** the map's own slice and spans, +58,560 B in the all-or-none set (PSRAM). ACCEPTED, because sharing would
  move WD5d.
- **Q-C4:** the code goes on a branch from #659's head, and its PR is opened after #659 merges. ACCEPTED. When #659
  merges, the branch is rebased onto main's squash and its stacked diff read again, so the PR shows only step C.
- **Q-C5:** ONE window, with the step B image and then the step C image. ACCEPTED, under the owner's 04:29Z terms
  applied to every flash in it:
  - all THREE images (step B, step C, and E2:08's restore image) are identified by sha256 before the window;
  - the write ranges are printed against the partition table before EACH flash: app only, no NVS, no factory image,
    no full erase;
  - each image's rows are read only from that image;
  - E2:08 is restored at the end, with Wi-Fi rejoining from its saved settings and no setup portal;
  - the window opens only after v19 needs nothing of E2:08, and anything that would need the owner's hands stops it.
- **CBF2-C**, the owner's look on glass, stays his window.

### Step C's code (`b592e6a7`) and its runs, predicted before they run

**The code, as committed** on `feat/map-client-step-c-blend`, from #659's head `b9d28fa5`:
- `include/MapSweep.h`: `Add565`, and the map's `Blend` with a `get`. It defines `BLIPSCOPE_MAP_STEP_C`.
- `include/MapCompose.h`: `slice` and `spans` join the all-or-none set, allocated after the scratch and zeroed at
  allocation. `kSweepBytes` is 58,560, asserted, and `Price` grows by it.
- `src/AircraftManager.h`: `MapSweep.h` is included just above CB10, and `mapFrameDrawn`, `mapFrameWx` and
  `DrawSweepOverMap` are declared.
- `src/AircraftManager.cpp`:
  - the flags are cleared at `DrawWeatherUnder`'s start, beside `wxShowing`'s own reset;
  - they are set in `DrawMapBase` after the copy;
  - `DrawSweepOverMap` is added;
  - the gate is the first statement of `DrawSweepOverWeather`.
  - The draw regions go from 12 to 14 in the .cpp; the header stays at 4.
- **`check_map_draw.py`:** CD4's definer is `include/MapSweep.h`, `MAP_FILES` gains it, and CD8 is new. That makes 12
  checks.
- **`sabotage_map_draw.py`:** five CD8 plants. `SOURCE_CHECKS` is 12.
- **The suite and its driver:** `test_map_sweep.cpp` (CS1-CS6, 9 checks), `sabotage_map_sweep.py` (6 plants), and
  `run.sh`'s MapSweep block.
- **CB9:** `test_map_draw.cpp`'s CB9 covers six buffers (28 checks), and `sabotage_map_compose.py`'s `CASES` is 28
  with S-CB-HALF's set at six rows.
- **A correction to the registration's CS6.** It said "except where the map is black". The map has TWO pure greens,
  `0x0000` and `0x0020`, the colours it shares with the wedge, and an add over `0x0020` stays a pure green. So CS6
  excepts both, and prints their share.
- **Disclosed:** while writing, I ran `check_map_draw.py`, the three map drivers' `--anchors` and the lint, to see
  that they worked. Those were development runs; the registered runs below repeat each one.

**Predicted, local.** WSL Ubuntu 24.04, g++ 13.3, the drivers with `CXX=g++`, on `b592e6a7`'s tree:
- **`check_map_draw.py`:** `12 checks, 0 failures`; CD4 `18 draw region(s)`; CD1 `the map's 4 files and 18 draw
  region(s)`; CD8 ok.
- **`test_map_sweep`:** `9 checks, 0 failures`:
  - CS1 `0 differ`;
  - CS2 `1179648 pixel/level pairs: 0 off the reference, 0 darker or recoloured` (18 levels x 65,536);
  - CS3 0 pixels and 0 counts differ, with blended over 0;
  - CS4-A and CS4-B each 0 differ, over drawn pixels above 0;
  - CS5-once, CS5-outside and CS5-clean each 0;
  - CS6: 0 pure greens over a map colour, and the excepted share **under 3 %**. Bend's disc holds 1.4 % (0.5 + 0.9) of the two
    colours at 50 mi, from the earlier estimate.
- **`test_map_draw`:** `28 checks, 0 failures`, with CB9 slice and CB9 spans ok.
- **`sabotage_map_sweep.py`:**
  - the control AS PREDICTED;
  - S-CS-NOSAT {CS2, CS5-once};
  - S-CS-RED {CS1, CS2, CS3, CS5-once};
  - S-CS-OPAQUE {CS5-once, CS6};
  - S-CS-DOUBLE {CS3, CS5-once};
  - S-CS-NOCLEAR {CS5-clean, CS5-once}: the marks copied after `Rasterise` include the stale ones, which no blend
    reaches;
  - S-CS-NOCODE {CS4-A, CS4-B};
  - the summary `6 sabotages: ALL AS PREDICTED`, and the real header unchanged;
  - `--control` 9, and `--anchors` 6 of 6.
- **`sabotage_map_compose.py`:** 14 plants AS PREDICTED, with S-CB-HALF at the six CB9 rows. `--control` 28, and
  `--anchors` 14 plants and 16 anchors.
- **`sabotage_map_draw.py`:**
  - the control `12 checks, 0 failures`;
  - 18 plants AS PREDICTED, each new one {CD8};
  - 3 controls AS PREDICTED (R-CD-RAWOK reads `12 checks, 0 failures`);
  - 23 AS PREDICTED lines in all;
  - `--anchors` 19 of 19.
- **`lint_driver_counts.py`:** `41 driver(s), 0 flagged; 3 pinned line(s)`.
- **The full `run.sh` (WSL):**
  - exit 2, and FAILED BLOCKS exactly the environment's two: "the same TU, for the board" and "Weatherscope R0 and R0b
    (the reference's controls, A12.18)";
  - `sabotage_wx_towns` skipped;
  - MapDraw 12 / 0, MapCompose 28 / 0, MapSweep 9 / 0, MapPlaces 32 / 0, MapLayer 160 / 0;
  - `41 driver(s): 40 run, 1 skipped` in both passes, and lint 41 / 0 / 3;
  - AS PREDICTED lines: compose 15, draw 23, places 6, sweep 8;
  - then `HOST TESTS FAILED`, from the two environment blocks only.
- **Anything else** is off, and I stop.

**Predicted, the images.** These are built in p4's slot, one at a time, from PowerShell, after the local runs read as
predicted:
- **I1, the shipping env (`blipscope-s3-128`):** 0 bytes outside the five regions against #659's code at `b9d28fa5`,
  by the comparator, with its controls. Step C compiles only under `BLIPSCOPE_MAP_FETCH`.
- **I2, `-wxlook` and I3, `-mapbench`:** both compile.
  - Their sizes grow; that is reported, not graded.
  - **No printable string is added** against #659's images, because step C prints nothing.
  - I3 still carries `'[wx-bench],[map-bench]'` and no other bench marker.
- **I4, CB10's plant:** the shipping env with `-DWX_LAYER_SHIPS` (the release flip), and `MapSweep.h`'s include removed
  from a copy of the header. The build is refused with CB10's `#error`, once per translation unit that includes the
  header, as step B's run read it.
- **I5, CB10 lifted:** the same flip build with step C as committed compiles, exit 0.
- **Anything else** stops.

**The local runs on `b592e6a7`, as read (about 08:00Z), and STOPPED at one off reading.**
- **As predicted:**
  - `check_map_draw.py`: `12 checks, 0 failures`; CD4 18 regions; CD1 the map's 4 files; CD8 ok.
  - `test_map_sweep`: `9 checks, 0 failures`:
    - CS1 0 differ;
    - CS2 1,179,648 pairs, 0 off;
    - CS3 0 and 0 (2,619,490 blended);
    - CS4-A and CS4-B 0 of 88,257 drawn-code pixels each;
    - CS5-once, -outside and -clean 0;
    - CS6 0 pure greens over a map colour, **1.55 %** excepted (under 3 %).
  - `sabotage_map_sweep.py`:
    - the six plants each red on exactly its frozen set, and `6 sabotages: ALL AS PREDICTED`;
    - 8 AS PREDICTED lines, and the real header unchanged;
    - `--control` 9, and `--anchors` 6 of 6.
  - `sabotage_map_draw.py`: 18 plants and 3 controls AS PREDICTED (R-CD-RAWOK `12 checks, 0 failures`), 23 lines;
    `--control` 12; `--anchors` 19 of 19.
  - `lint_driver_counts.py`: `41 driver(s), 0 flagged; 3 pinned line(s)`.
- **OFF:** `test_map_draw` printed `28 checks, 0 failures` and exited **2**: `the RIG: 28 cases, not 26`.
  - **The cause:** the suite pins its own count, `if (g_checks != 26)` at `test/host/test_map_draw.cpp:499-500`. Its
    header comment says 26 at lines 2 and 5.
  - **My miss:** I updated the driver's `CASES` and `run.sh`'s comment but not the suite's own pin. My search for the
    old count used patterns that could not match `g_checks != 26`. A search for the bare number, `\b26\b`, in every
    reader would have found it.
  - **The same pin, read again:** `sabotage_map_compose.py` reads the rig (`CONTROL (the real files): not clean -- the
    rig`, exit 2), and its `--control` is 1 OFF (exit 2 against checks 28). Its `--anchors` read 14 plants and 16
    anchors, all present.
- **Not run:** the full `run.sh`, and the images I1-I5.

**The fix and its re-run** (the orchestrator, 07:49:28Z and 07:52:42Z).
- **The sweep:** of the bare number 26 over the whole tree, 5,155 hits, every one classified. The readers of this
  count were the four expected lines.
- **The fix:** `03a04ab2`.
- **The re-run, as predicted:** `test_map_draw` 28 / 0, exit 0, with CB9 slice and CB9 spans ok.
  `sabotage_map_compose.py` reads "28 cases, all pass", all 14 plants AS PREDICTED (S-CB-HALF 6 red), 15 lines,
  `--control` 28, and `--anchors` 14 plants and 16 anchors.

**The full `run.sh` on `03a04ab2`, as read** (WSL, 07:52-08:00Z): **OFF**, from an environment block I did not name.
- **The prediction:** exit 2, with FAILED BLOCKS exactly the two environment blocks.
- **The reading:** exit 2, with **seven** FAILED BLOCKS. Two are the predicted ones: "the same TU, for the board", and
  "Weatherscope R0 and R0b" (no Pillow). The five others all have one cause, **this worktree has no `.pio/libdeps`**:
  - the usage report block: its Wi-Fi QR check reads BLIND, with fonts missing ("no LovyanGFX under .pio/libdeps: run
    pio run once");
  - the seven-segment 1 vs I block: BLIND, no GLCD font table;
  - GameConfig: BLIND, "no ArduinoJson under .pio/libdeps";
  - the drivers' `--control` and full passes: both fail on `sabotage_wx_fb_check.py`, with "no LovyanGFX glcdfont.h
    under .pio/libdeps (run pio run once)".
- **The cause:** `wt-stepc` is a fresh worktree that has never been built. My earlier WSL runs ran in trees that held
  `.pio/libdeps`. My prediction named the two blocks this machine always has, and not this one.
- **Every step C row inside the run, as predicted:**
  - MapDraw 12 / 0, MapCompose 28 / 0, MapSweep 9 / 0, MapPlaces 32 / 0, MapLayer 160 / 0;
  - `41 driver(s): 40 run, 1 skipped` in both passes (`sabotage_wx_towns` skipped);
  - lint 41 / 0 / 3;
  - compose 15, draw 23, places 6 and sweep 8 AS PREDICTED lines.
- **What I did:** I stopped there. Nothing was fixed or re-run, and no build was started.

**The ruling on the full run (the orchestrator, 08:05:54Z), in short:**
- The first full run is graded OFF. Its five extra blocks are instrument failures (BLIND), not verdicts on step C.
  Step C's rows in it are recorded as read, and the full-run row is graded on the re-run only.
- (a) was approved, with six preconditions, as the row's FIRST re-run. A second off reading on it goes to the owner.
- **My miss, on record:** the full-run prediction did not name its environment. **The mechanical fix, from now on:**
  every full `run.sh` prediction names its worktree, and prints `ls .pio/libdeps` as a precondition.

**The re-run, as read** (WSL, 08:06:36-08:14:49Z, in `wt-main-mc`):
- **The preconditions, each as required:**
  1. HEAD `c782dc3e960628fca42e5fa69d0604c9275ace83`, porcelain empty;
  2. `platformio.ini`'s `lib_deps` identical between that HEAD and `3c8e5110` (13 lines each, and no difference in
     the whole file);
  3. `.pio/libdeps` holds `blipscope-s3-128`, `blipscope-s3-128-mapbench` and `blipscope-s3-128-wxlook`;
  4. detached at `3c8e51103de9d5de38cf561aa38cf198ea64ed50`, with HEAD's tree equal to its tree (`ef401555…`), and
     porcelain empty.
- **The reading, AS PREDICTED:**
  - exit 2, with FAILED BLOCKS exactly "Weatherscope R0 and R0b" (no Pillow) and "the same TU, for the board";
  - `sabotage_wx_towns` skipped, and `41 driver(s): 40 run, 1 skipped` in both passes;
  - lint 41 / 0 / 3;
  - MapDraw 12 / 0, MapCompose 28 / 0, MapSweep 9 / 0, MapPlaces 32 / 0, MapLayer 160 / 0;
  - compose 15, draw 23, places 6 and sweep 8 AS PREDICTED lines.
  - The blocks the first run lost now pass: the follow flag, the Wi-Fi QR sweep (36 names placed, 0 refused), the
    seven-segment 1 vs I rows, GameConfig 53 / 0, and `sabotage_wx_fb_check.py` in both passes. The log's only BLINDs
    are `wx-fb-check`'s own expected-BLIND cases, each ok.
- **Step 6:** `wt-main-mc` is back on `c782dc3e960628fca42e5fa69d0604c9275ace83`, porcelain empty.

**I1-I5, the order, with nothing new predicted.** All builds are in `wt-stepc` from PowerShell, one at a time, in p4's
slot, with C: read first. The comparator needs both images of a pair built in the same tree, so the baselines come
first:
- **The baselines:** `b9d28fa5` detached, porcelain empty first. `blipscope-s3-128`, `-wxlook` and `-mapbench` are
  built and copied out, then the branch head is checked out again.
- **I1-I3** at `3c8e5110` (or its descendant carrying only this record); I1's comparator controls run first.
- **I4:** the plant goes into `src/AircraftManager.h` (the `MapSweep.h` include removed), with a byte copy and its
  sha256 taken first. The build is `blipscope-s3-128` with `PLATFORMIO_BUILD_FLAGS=-DWX_LAYER_SHIPS`, and it is read
  by its log's `#error` text and its exit code. Then the header is restored from the copy, its sha256 compared, and
  porcelain read empty.
- **I5:** the same flip build, without the plant.

**The comparator's controls for I1, frozen before the comparator runs.** `31164e23` says only "by the comparator, with
its controls" and names no reading for them; the orchestrator asked for them at 08:19:35Z. Written while the builds ran,
fixed by sha256 in the report to the orchestrator, and committed here before `imgcmp.py` first reads any of the images.
- **Presence:** in each of Bs1, Bs2 and Cs, aboutdate's joined date-time occurs exactly once, and "Software Info:" occurs
  exactly once with its time-and-date pair after it. Within each compared pair (Bs1/Bs2, and Bs1/Cs) both sit at the
  same offsets.
- **Anchor**, Bs1 against Bs2 (the same source, `b9d28fa5`, built twice from clean): `imgcmp.py --anchor Bs1.bin Bs2.bin`
  exits **0**:
  - the same length;
  - **0 bytes outside** the five rebuild regions;
  - inside, these differ: `app_elf_sha256` (32 B at 0xB0), the trailing image SHA-256, aboutdate's date-time, and the
    "Software Info:" time-and-date pair;
  - the checksum byte may differ or not, and is printed either way.
  - **Any byte outside is the instrument** (exit 2), with every offset reported and never whitelisted on the spot.
- **Sensitivity:** `imgcmp.py --flip Bs1.bin` writes Bs1 with one byte flipped at 0x1000. Compared against Bs1, it exits
  **1** with exactly **1** byte outside, at 0x1000.
- **Then I1:** Bs1 against Cs, as `31164e23` froze it: the same length, and **0 bytes outside**.
- **Anything else** in the controls stops before I1 is read.

**The relabel (the orchestrator, 08:20:34Z), beside the frozen text above, which stays as committed:** a change of
offsets between Bs1 and Cs is NOT the instrument. It means the shipping image's layout moved, a finding about step C:
**I1 OFF, the shipping image changed**. Only a Bs1/Bs2 offset mismatch, or a missing or duplicated string, is the
instrument's. Both stop; the label says which verdict is recorded.

**The nine builds, as read** (`wt-stepc`, 08:18:04-08:44:39Z, p4's slot; C: 68.4 GB free before, 67.8 GB after):

| build | tree | env | build exit | image |
|---|---|---|---|---|
| Bs1 | `b9d28fa5` | `blipscope-s3-128` | 0 | 1,932,448 B, `54f5a0fb6237…` |
| Bs2 | `b9d28fa5` | `blipscope-s3-128` | 0 | 1,932,448 B, `517076f7d43f…` |
| Bw | `b9d28fa5` | `-wxlook` | 0 | 1,973,264 B, `1052b01c60c7…` |
| Bm | `b9d28fa5` | `-mapbench` | 0 | 2,390,416 B, `9e7635c93629…` |
| Cs (I1) | `89f7e7f5` | `blipscope-s3-128` | 0 | 1,932,448 B, `d9326b729eaa…` |
| Cw (I2) | `89f7e7f5` | `-wxlook` | 0 | 1,974,112 B, `80fc7487ac41…` |
| Cm (I3) | `89f7e7f5` | `-mapbench` | 0 | 2,391,120 B, `6faaf7110459…` |
| I4 | `89f7e7f5` + the plant | `blipscope-s3-128` + `-DWX_LAYER_SHIPS` | **1** | none (no link step) |
| I5 | `89f7e7f5` | `blipscope-s3-128` + `-DWX_LAYER_SHIPS` | **0** | 1,971,232 B, `19f4acfff8e8…` |

- **The `clean` step exited 1 for Bs1, Bw and Bm**, each env's first build in this worktree. `pio run -t clean` runs
  the pre-build patch script, which stopped with `[patch_async_buff] FATAL: ESPAsyncWebServer is a dependency of …`,
  because that env's libraries were not installed yet. The build that followed installed them and compiled from an
  empty build directory: 259 files, the same count as Cs's real clean rebuild. So each of the nine is a full build.
- **The harness, on record:**
  - every build exit was read from `$LASTEXITCODE`;
  - the I4 plant was applied (header sha256 `7b887e7e…` became `94e82fe9…`) and restored by bytes in a `finally`
    block (back to `7b887e7e…`, equal);
  - porcelain read empty at every checkpoint, and HEAD ended at `89f7e7f5`.
- **The harness's I4 label was wrong.** It logged "firmware.bin rewritten: True" by comparing the file's time before and
  after. The I4 build ran `clean` first, which removed the previous `firmware.bin`, and the failed build wrote none.
  The log has no link step, so an absent file read as "changed". The fact is: **I4 produced no image**, as step B's
  R-FLIP did.
- **I4's log:**
  - CB10's `#error "step B draws the map without step C: …"` at `src/AircraftManager.h:53:2`, **twice**, once for
    `AircraftManager.cpp.o` and once for `main.cpp.o`, as predicted;
  - **and one line NOT predicted:** `src/AircraftManager.cpp:3962:30: error: 'mapsweep' has not been declared`. The
    plant removes `MapSweep.h`, which also declares the `mapsweep::Blend` that `DrawSweepOverMap` calls. CB10's `#error`
    comes first in each translation unit (log lines 563 and 708; the extra error is line 578).
  - **By the letter of `31164e23`** ("refused with CB10's `#error`, once per translation unit … anything else stops"),
    this is an OFF reading. **STOPPED here: the comparator has not run**, and I1-I3 and I5 are not graded.

**The comparator's controls, I1, I2 and I3, as read** (08:47:53Z on; `imgcmp.py` `dc828e47…`, `strings_added.py`
`c5dee9d3…`):
- **Anchor**, Bs1 against Bs2: AS PREDICTED. Exit 0; the same length; presence in both (aboutdate at 0x312C,
  Software Info at 0x1007A); 0 bytes outside. Inside: `app_elf_sha256` 32, the image SHA-256 32, aboutdate 2,
  Software Info 2, and the checksum byte differs (printed).
- **Sensitivity:** AS PREDICTED. Bs1 against its copy flipped at 0x1000: exit 1, with exactly 1 byte outside, at 0x1000.
- **I1**, Bs1 against Cs: AS PREDICTED. The same length; presence at the same offsets; **0 bytes outside**. Step C does
  not reach the shipping image.
- **I3's markers:** `check-no-bench-hooks.sh --expect '[wx-bench],[map-bench]'` on Cm reads `ok: … carries
  '[wx-bench],[map-bench]' and no other bench marker (anchor present)`.
- **I2 and I3's strings: OFF, and the instrument is the cause.** Bw to Cw shows 72 runs "added" and 77 removed (+848
  B); Bm to Cm shows 76 added and 77 removed (+704 B).
  - Every "added" run is a burst of machine-code or address bytes that happens to be printable, such as
    `' %8b"\'3'`, `' Pt!`G'` and `'#<-R!<0'` (0x3C, `<`, is a flash-address byte), moved by the new code.
  - A printable run of six bytes cannot tell text from code. The method was written after `31164e23` and never shown,
    on a control, that it could. **My miss:** an instrument without a control.
  - **STOPPED here.** I5 is not graded.
- **The question on `clean`.** Its exit was **not checked at all.** The harness logged it (`clean exit 1`) but threw
  only on the build's exit. No rule allowed it; it was a gap. The evidence that all nine are full builds (259 compiled
  files each, as Cs's) stands. The I4 re-run checks `clean`'s exit.

**I4's re-run, (b), frozen before its build** (the orchestrator, 08:47:15Z: I4 OFF on the record; (b) is its FIRST
re-run, and a second off reading goes to the owner).
- **The readers of `BLIPSCOPE_MAP_STEP_C`** (bare-name grep over `src/`, `include/`, `test/` and `scripts/` at
  `ef11a5d1`):
  - the definer, `include/MapSweep.h:16`;
  - the one compiled reader, CB10's `#if` at `src/AircraftManager.h:55`;
  - comments at `src/AircraftManager.h:49` and `:53`;
  - host-check text, not compiled: `check_map_draw.py` (CD4's definer scan and `REFUSAL_IF`), `sabotage_map_draw.py`,
    and a `run.sh` comment.
- **The translation units that include `AircraftManager.h`, node by node,** for `blipscope-s3-128` with
  `-DWX_LAYER_SHIPS` (`BLIPSCOPE_MAP_FETCH` defined, `WX_LOOK_AB` not):
  - **`AircraftManager.cpp`:** `MapSweep.h` is included, so `mapsweep` is declared and the macro defined. The plant's
    `#undef` then removes the macro, so CB10's `#if` is true and its `#error` fires **once**. GCC compiles on, with
    `mapsweep` declared, so there is **no other error**.
  - **`main.cpp`:** the same: **once**, and no other error.
  - **`SoakHarness.cpp`:** it includes `AircraftManager.h` only under `#ifdef SOAK_TEST`, which this build does not
    set. It compiles, but never sees the header: **no `#error`**.
- **The plant:**
  - insert `#undef BLIPSCOPE_MAP_STEP_C` after the `#endif` that closes `MapSweep.h`'s include block, before CB10's
    `#if`. The anchor (the three-line include block followed by CB10's `#if` line) occurs exactly once;
  - take a byte copy and its sha256 first, and restore by bytes in a `finally` block;
  - compare the sha afterwards, and read porcelain.
- **Predicted:**
  - `clean` exits **0** (this env has built here). It is now checked, and a non-zero exit throws;
  - the build exits **1**;
  - CB10's `error: #error "step B draws the map without step C: …"` appears **exactly twice** (`AircraftManager.cpp.o`,
    `main.cpp.o`), at `src/AircraftManager.h:57` (line 56 plus the plant's one line);
  - **no other line containing `error:`**;
  - no `Linking .pio/build/blipscope-s3-128/firmware.elf` line and no `Building …/firmware.bin` line;
  - **no `firmware.bin`** after the build.
- **The harness's image check, fixed.** "No image" means: `.pio/build/blipscope-s3-128/firmware.bin` does not exist
  AND the log has no link line. It is never judged by timestamps.
  - **Its control, run on I5's output before the re-run (08:49:51Z):** the file exists (1,971,232 B, sha256
    `19f4acff…`, I5.bin's), and I5's log has 1 link line and 1 `Building …/firmware.bin` line. The check sees an
    image.
- **Anything else** stops, and goes to the owner.

**I5, I4's re-run, and I2-I3 re-registered** (the orchestrator, 08:51:39Z).
- **I5, graded as frozen** ("the same flip build with step C as committed compiles, exit 0"): **AS PREDICTED**. `clean`
  exit 0, build exit 0, `[SUCCESS]`, no line containing `error:`. The image exists by the fixed check: 1,971,232 B,
  sha256 `19f4acff…`, with 1 link line and 1 `Building …/firmware.bin` line.
- **I4's re-run, (b)** (08:53:32-08:56:11Z, p4's slot; frozen as `22b07f8d…`): **AS PREDICTED**.
  - the plant put CB10's `#if` at line 56 and its `#error` at 57 (header sha `7b887e7e…` became `34edb143…`, and was
    restored to `7b887e7e…`, equal);
  - `clean` exit 0, checked;
  - build exit 1;
  - CB10's `src/AircraftManager.h:57:2: error: #error "step B draws the map without step C: …"` **twice**, the failing
    objects `AircraftManager.cpp.o` and `main.cpp.o`;
  - **no other line containing `error:`** (2 in all);
  - no link or `Building …/firmware.bin` line, and **no `firmware.bin`**;
  - porcelain empty; HEAD `2fa53c01`.

**I2 and I3, the claim as frozen in `31164e23`** (line 3589): "**No printable string is added** against #659's images,
because step C prints nothing." It is read on the images; the source diff gives its prediction.

**(2), the prediction: the source diff `b9d28fa5..89f7e7f5` over `src/` and `include/`.** It is 4 files, +151 and −4.
Every added or removed line holding a `"` on a non-comment line:

| line | reaches an image? |
|---|---|
| `+ include/MapCompose.h: static_assert(kSweepBytes == 58560, "Q-C3: the sweep's slice and spans, 58,560 B");` | no: a `static_assert` message exists only at compile time |
| `+ include/MapSweep.h: #include "WxSweep.h"` | no: a preprocessor path |
| `+ src/AircraftManager.h: #include "MapSweep.h"   // …` | no: a preprocessor path |

There are no removed lines with a literal. **So the images' expected change is 0 added and 0 removed.** That is
`exact_stepc.txt`, which has no `+` or `-` line.

**(1), the reading: NUL-bounded C strings** (`strings_nul.py`). A run is at least 6 bytes of printable ASCII, tab, CR or
LF, with a NUL immediately before and after it. Date- and time-shaped runs are set aside and counted. The segments come
from esptool's own table (`esptool image-info`, PlatformIO's penv, v5.3.0); each segment's data starts 8 bytes after
its "File offs".
- **Frozen by sha256:**
  - `strings_nul.py` `c2d0b8cc5d22130f46edd93a7f5c07f64bc12bd3d222a0dedafc77ab503dc900`;
  - `pos_mapbench.txt` `26289b45acedc9ee9b224cceccd0442b2d8383c1265e0034e3fc8cae9b0de2e3`;
  - `exact_stepc.txt` `7ecefbfe29b45e6c4346625dfa7298533ae8e0af07c5a44790fb8970957c7ba9`.
- **The controls, before any reading, in this order:**
  - **P, positive:** Bw against Bm, the whole image, `--expect-added pos_mapbench.txt`. These are the six `[map-bench]`
    literals in `src/` at `b9d28fa5`, listed from source before any scan: the PSRAM refusal, the gate refusal, the
    passes line, the phase line, the boot marker and the alloc line. **Each must be an added run, exactly: 6 of 6**,
    exit 0. The whole added list is printed.
  - **N1, negative with no code shift:** Bs1 against Bs2, the whole image: **0 added and 0 removed** beyond the
    date-shaped runs, exit 0.
  - **N2, negative with a code shift:** Bw against Cw, `--segments IROM,IRAM` only, where no literal lives: **0 added
    and 0 removed**, exit 0.
- **The reading:** Bw against Cw, and Bm against Cm, the whole image, `--expect-exact exact_stepc.txt`: added and
  removed each **equal** to the frozen (empty) list, as whole lists, exit 0 for both.
- **Anything code-shaped in any reading is the instrument again, and stops.** Anything else stops too. This is the
  rows' first re-run, so a second off reading goes to the owner.

**I2-I3's re-run, as read: OFF at the positive control, from the instrument again** (08:59:00Z; `strings_nul.py`
`c2d0b8cc…`, `pos_mapbench.txt` `26289b45…`, both as frozen).
- **P, Bw against Bm, whole image:** exit 0, **6 of 6** listed `[map-bench]` literals among the added runs, exactly. Its
  own criterion is met.
- **But the reading it printed is not clean:** 28 added and 10 removed, with 4 date-shaped runs set aside.
  - **Real strings** among them: the six literals; `map-bench`; `blipscope-s3-128-mapbench` (the env name, and the
    `.pio/libdeps/<env>/…` source paths that the assert macros carry); `' REFUSED'`. Removed: `'[map] alloc %u B: %s\n'`
    and `'[map] request DROPPED: fetch queue full'` (the bench's dead fetch path, as M1's size row found).
  - **Address data, not text:** 16 added runs have the shape `xx xx 21 3C` plus two bytes, for example `'4E!<6A'`, which
    is bytes 34 45 21 3C 36 41: a little-endian flash-data pointer `0x3C21xxxx` in a table, bounded by zero bytes on
    both sides. Two removed runs, `'<L\rB L\rB'` and `'xK\rB\K\rB<K\rB'`, are `0x420Dxxxx` IROM addresses.
  - **So the NUL bounds do not separate strings from pointer data.** The ruling of 08:51:39Z says: "Anything code-shaped
    in a NUL-bounded reading means the instrument again: stop." **STOPPED.** N1, N2 and the reading did not run.
- **This is the rows' first re-run, and its off reading goes to the owner**, as ruled.

### Step C's bench rows: the reader's extension, registered before its code (2026-11-12)

The orchestrator's GO is 09:03:01Z. Nothing is built or run. The window is ONE window (Q-C5): the step B image, then
the step C image, each identified before it starts. The frozen reader, `map_bench_read.py` at `5ad12d0adb0d…`, grades
step B's rows. It gains **`--image B|C`** for the capture and for `--fb`.
- **B mode must give the same output as `5ad12d0a…`** on the same inputs. That is the regression control: both readers
  run on the selftest's captures and frames, and their outputs are compared.
- **The extended reader is frozen anew.** Neither `65d2edf5…` nor `5ad12d0a…` is reused for the C rows.
- **The fetcher stays as frozen** (`fetch_fb.py` `7b904453…`). The C image's P1 is fetched in the even pairs too, by
  rule (ii).

**CBF4-C:** the alloc line's price is the step B price plus `kSweepBytes`, 58,560 (`MapCompose.h`'s `static_assert`):
2 x 115,200 + 57,600 + 32 x (v + 1) + 4,224 + 13,320 + 58,560. The internal heap delta is 0, and `psram_free` falls by
the price within 1,024 B.
- **Must flag:** a C-labelled synthetic alloc line at the B price is red. **Must not flag:** a C-labelled line at the C
  price is green.
- And the reverse in B mode: the C price is red, and the B price is green.

**CF2, CF3 and CF4 on the C image,** graded over P1's un-fetched pairs (rule (ii)):
- **CF2:** on map-on intervals the sweep is in its band and `blend` equals the interval's `n`, so every frame went
  through the map's blend. On map-off intervals the sweep is 2.04 ± 0.3 ms with `blend=0`.
  - **Must flag:** `blend=0` on a map-on interval; `blend=n` on a map-off one; a map-on sweep at 2.04.
  - **Must not flag:** the in-band synthetic.
- **CF3:** the mean of p95, on − off, in its band.
- **CF4:** P2, weather on, on − off in −1.0 to +2.2 ms, unchanged. With step C, a map-on frame with the weather on
  blends through the map's path, which has the same memory traffic per pixel (one read, one write).
- **CF1 and CF6:** as step B's.

**CBF3-C, re-registered per slice** (Q-CR1). The wedge's 18 levels, shifted right by two, are the green each slice
adds: 26, 23, 20, 18, 15, 13, 11, 9, 7, 5, 4, 3, 2, 1, and then **0, 0, 0, 0**. So slices 15-18 add nothing and leave
the map as it is. They are **18.4 %** of the graded inside region by angle.
- **Why efea9576's form fails:** "MAPGREEN over 80 % inside" would read about 80 % by geometry alone, so it could not
  tell a working add from a broken one.
- **MAPGREEN is computed from source:** `Add565(m, a)` for each map colour m and each add a ≥ 1. That is 166 colours.
  - The ones that coincide with another class go to AMBIGUOUS: 4 with MAP, 14 with WEDGE (an add over 0x0000 or 0x0020
    is a pure green), and 18 with OVERLAY.
  - The reader refuses if the source yields other counts.
- **Each graded inside pixel's slice comes from its angle,** behind the located beam:
  - **the green slices**, 1-14;
  - **the zero slices**, 15-18;
  - the pixels within ±0.01 rad of the 14|15 boundary, the one place the expected class changes, are left out;
  - the 0.06 rad margins at both ends stay as CBF3's.
- **Predicted, on a C image's trustworthy map-on frame:**
  - in the green slices, MAPGREEN / (MAP + MAPGREEN + WEDGE + AMBIG) **over 95 %**;
  - in the zero slices, MAP over the same denominator **over 95 %**;
  - inside, the share of pure wedge greens (WEDGE) **under 2 %**;
  - outside, MAP **over 80 %**, as CBF3.
- **The real-frame control:** on the C image's off ticks, black plus the add is a pure green, so MAPGREEN in the green
  slices is **under 2 %**. It must not flag.
- **`--selftest`'s synthetic controls, both ways:**
  - **flags:** a step C on-frame (the map plus the add, drawn from source) reads AS PREDICTED;
  - **does not flag:** a step B on-frame (the opaque wedge over the map), graded as C, reads OFF with MAPGREEN about 0;
  - **the wrong colour:** a frame with the add put into red reads UNTRUSTWORTHY, over 10 % UNKNOWN;
  - **an off frame graded as C** passes the control.
- **The B image's frames** keep CBF3's measure, unchanged.

**Questions for the ruling:**
- **Q-CR1:** CBF3-C per slice, as above, replacing efea9576's "MAPGREEN over 80 % inside", which geometry alone puts at
  about 80 %. **I recommend yes.**
- **Q-CR2, CF2's and CF3's bands in one window:**
  - (a) keep 7.7-8.9 ms and +6.2 to +10.5 ms as frozen at step M;
  - (b) freeze a formula now: CF2(C) is the B image's P2 blended-sweep mean (the same window, the same board) ± 0.6 ms,
    and CF3(C) is 2.07 + (CF2(C) − 2.04) ± 1.0 ms. The numbers are computed from B's capture before C's capture is
    read.
  - **I recommend (b).** The 7.68 ms predates the B1 span fix, which was the named weak point. (b) takes the band from
    a reading of the same blend path on the same board, and it is fixed before C is read, so it is not fitted.
- **Q-CR3:** the C image's run ends after P2 (P0-P2: CBF4-C, CF1, CF2, CF3, CF4, CF6, CBF3-C), about 45 minutes. P3
  and P4 read the rebuild (CBF1, CF5), whose code step C does not touch, so they come from the B image only. The window
  is then about 1 h 50 instead of about 2 h 20. **I recommend yes.**

**The ruling on the reader's design (the orchestrator, 09:07:28Z), and the answers it asked for, before the code.**

**Q-CR1, YES; the ask: where each pixel's slice angle comes from.**
- **θ comes from the beam's own pixels.** These are `DrawRadarSweepBeam`'s line (80, 255, 80) and head (170, 255, 170),
  found by wx-fb-check's locator (imported): one head blob 117-123 px out, and the line on at least 70 of 101 samples.
  It is cross-checked against `wedge_mask` on every frame.
  - Both colours are OVERLAY in this reader and are **never graded**.
  - Both wedge paths call `DrawRadarSweepBeam` after the wedge, so a broken or missing add cannot move them.
  - **No wedge, MAP or MAPGREEN pixel enters θ.**
- **What it is not:** the device's own report. `/diag/fb`'s headers carry no sweep angle (Width, Height, Format, Torn,
  Seq, Phase, Tag, Buffer, Finished, Wait-Us, Attempts), and adding one would change the bench image. That is
  **Q-CR4** below.
- **A control that shows θ is independent of the graded classes**, in `--selftest`: for each synthetic frame, θ is
  located within ±0.02 rad of the angle it was drawn at, whatever the wedge drew:
  - the step C add;
  - step B's opaque wedge;
  - the add put into red;
  - no wedge at all.

**Q-CR2, (b), on its four conditions:**
1. **B's P2 inputs:** the B image's capture, its 40 P2 intervals (weather on, the map alternating; P2 is never
   fetched). Each interval's `[health] sweep=<us>/…us n=<n> blend=<b>` line, its first value. **CF2(C)'s centre is
   the mean of those 40 values**, and each interval must read `blend` equal to its `n`.
2. **The half-widths' origins:**
   - **±0.6 ms** is (8.9 − 7.7) / 2, the half-width of CF2's band as registered at step M;
   - **±1.0 ms** is CF3-B's registered half-width (+2.07 ± 1.0), the same quantity, frame p95 on − off, on this board.
   - So **CF2(C) = mean ± 0.6 ms**, and **CF3(C) = 2.07 + (mean − 2.04) ± 1.0 ms**.
3. **Before C's capture is read:** `map_bench_read.py --derive-c-bands <B capture>` prints the two bands and the B
   capture's sha256. They are sent to the orchestrator before `--image C` reads C's capture, which then takes the bands
   as arguments.
4. **If B's own P2 reads off** (any P2 interval with `blend` ≠ `n`, CF4 out of band, or the INSTRUMENT on B's
   capture), **B stops the window, and C is never graded against it.**

**Q-CR3: shown, and the answer is NO, so the C image runs the full P0-P4.**
- **What CBF1 measures:** `t0 = millis()` taken just before `mapcompose::Frame(…)` in `DrawMapBase`, printed as
  `[map] draw: rebuilt in <ms> ms` after a rebuild. Frame's order is: the growth test (`Release` if the package needs
  more vertices), then `Allocate` if unallocated, then `Rebuild`, then `Compose`.
- **What CF5 measures:** `maplayer::Build`'s own laps (the `us*` passes from its clock). That is Build only.
- **Step C's hunks against them:**
  - **the flags in `DrawWeatherUnder`:** on neither path;
  - **the flags in `DrawMapBase`:** set after `Frame` returns and after the copy, so outside CBF1's interval;
  - **`DrawSweepOverMap`:** the sweep, on neither path;
  - **`MapCompose.h`'s `Allocate`, `Price` and `Release`, with the slice and spans and the 57,600 B memset:** **ON
    CBF1's path**, on every frame that allocates. P4 switches to KSFO, whose package grows the buffers, so that frame
    runs `Release` and then `Allocate` inside CBF1's interval. Build does not change, so this is not on CF5's path.
  - **The all-or-none refusal and PSRAM headroom** (the price rises 58,560 B) can move the growth frame too.
- **So the C image runs P0-P4, and on C:**
  - CBF1's bar is unchanged: every rebuild ≤ 100 ms, the same three verdicts;
  - CF5 is unchanged: Bend 30-70 ms in total, and KSFO reported;
  - **0 `[map] draw: buffers refused` lines**, the all-or-none refusal not taken at P4's growth;
  - CBF4-C's price at P0.
  - **Reported, not graded:** P4's growth frame on C against B's, which carries the memset and the two allocations.
- **The window is therefore about 2 h 20**: two full P0-P4 runs, plus the flashes and the restore.

**Q-CR4, new:** a device-reported angle, a bench-only header such as `X-Blipscope-Sweep-Angle` under `MAP_BENCH`,
instead of the beam-located θ. It would change the bench image, which means a new code row, the CB-B regions again, and
the images identified again. **I recommend no.** The beam is not a graded class, and the control above shows that θ
does not follow the wedge.

**The reader's code, before its freeze: one check answered, and a finding that stops the freeze** (09:11:00Z on).
- **Q-CR4: NO** (the orchestrator, 09:11:00Z). θ stays the beam's, and the bench image stays as identified.
- **The check asked for:** the beam's line (80, 255, 80) is RGB565 0x57EA and its head (170, 255, 170) is 0xAFF5.
  **Neither is among the 166 MAPGREEN values.** MAPGREEN's 6-bit green tops out at 47 (the place grey's 21 plus the
  largest add, 26), and both beam colours have 63.
- **Written so far (not frozen):**
  - `--image C`, `--derive-c-bands`, `--fb-c` and their selftests: 46 lines, all ok. Every C plant reads red for its
    own reason, and θ lands within 0.005 rad of the drawn angle for all four wedge kinds.
  - **A development bug,** caught by the registered-count refusal: `fb_source_c` shifted the adds twice, giving
    MAPGREEN 67/13/7/3. The refusal fired, and the fix restored 166/4/14/18.
  - **The regression control** (`regress_b.py`): B mode equals `5ad12d0a` on all 15 comparisons. A planted one-character
    change makes it read 12 differences and exit 1.
- **THE FINDING: Q-CR1's per-slice bands fail on Bend's real map, for a correct step C.** On the host I applied step
  C's add to Bend's own layer (radii 10, 50 and 150 mi; rotations 0 and 90; 36 beam angles each), classified by the
  reader's own classes:

  | Bend layer | green slices, MAPGREEN | zero slices, MAP |
  |---|---|---|
  | 10 mi | min 94.9 %, mean 98.8 % | min **0.0 %** |
  | 50 mi | min **90.0 %**, mean 96.5 % | min **9.1 %** |
  | 150 mi | min 95.7 %, mean 98.5 % | min **3.1 %** |

  - **The cause:** the terrain ramp's colours sit one green step apart. A map colour plus green is often another map
    colour, so those pixels are AMBIG. The 4 colours in MAP ∩ MAPGREEN are Bend's dominant terrain colours.
  - **Why the synthetic missed it:** its palette weights all 13 map colours equally, so it read 96.9 % and 100 %.
- **The same layers under a simpler measure,** the claim itself ("the map stays visible under the beam"): inside the
  wedge, the share of map- or wedge-class pixels that are NOT a pure green (red 0 and blue 0). Step B's opaque wedge
  makes every one a pure green; step C keeps the map's red and blue.

  | Bend layer | step C, map on | step B, opaque | step C, off tick |
  |---|---|---|---|
  | 10 mi | 100.0 % | 0.0 % | 0.0 % |
  | 50 mi | min **93.6 %**, mean 98.6 % | 0.0 % | 0.0 % |
  | 150 mi | min 97.3 %, mean 99.2 % | 0.0 % | 0.0 % |

  At 50 mi the minimum is Bend's own dark-slope pure greens (0x0000, 0x0020) gathering in one sector.
- **Proposed, Q-CR5, for the ruling:** CBF3-C becomes the not-pure-green share inside the wedge.
  - **over 80 %** on the C image's trustworthy map-on frames (the worst real reading is 93.6 %);
  - **under 2 %** on its off ticks (the control; 0.0 % on every real layer);
  - outside, MAP over 80 %, as CBF3.
  - Selftest: a step B opaque frame graded as C must read OFF (0 %), the add-into-red frame stays UNTRUSTWORTHY, and the
    θ independence control stays.
  - MAPGREEN stays as a class, so its colours are not UNKNOWN, but its share is reported, not graded.
- **Nothing is frozen until the ruling.**

**The ruling of 09:20:17Z, and the extended reader, as written and frozen (09:25Z).**
- **Q-CR5 is accepted as the "map stays visible" half,** with a graded "the add happened" half required before the
  freeze. My miss, on record: the synthetic palette weighted the 13 map colours equally and hid the terrain AMBIG. The
  orchestrator recorded its own miss too: it accepted Q-CR1's > 95 % without asking what map content it was computed
  on.
- **CBF3-C, as frozen: two graded halves over the map/wedge-class pixels inside the wedge.**
  - **VISIBLE:** the share that is not a pure green. Over 80 % on map-on frames.
  - **ADDED:** in slice 2 (back 0.060-0.101 rad), the share greener than any map colour, 6-bit green above 21. Over
    80 % on map-on frames.
    - **Why slice 2 alone:** slice 1 lies wholly inside the 0.06 rad beam-end margin, and slice 3's add (20) over black
      is not above 21. In slice 2 (add 23), any pixel after a real add is at least 23; with no add, it is at most 21.
    - Both the map's maximum green (21) and the leading slices ([1, 2]) are derived from source and refused if they
      change.
  - **Outside:** MAP over 80 %. **Off ticks:** VISIBLE under 2 %, the control.
  - **The minimum denominators:** a frame with fewer than **3,000** map/wedge pixels inside, or **130** in the add
    region, is UNTRUSTWORTHY.
  - **Reported, not graded:** MAPGREEN's per-slice shares.
  - **CBF3-C is graded on Bend only.** The fetcher fetches P1 (Bend) and never P4 (KSFO), so no host check on KSFO is
    needed.
- **Each threshold beside its host minimum,** from Bend's own layers (10/50/150 mi, rotations 0 and 90, 36 beam angles
  each), as the reader prints them:

  | threshold | host reading |
  |---|---|
  | VISIBLE > 0.80 | min **0.936** (step B's opaque: 0.000) |
  | ADDED > 0.80 | min **1.000** (a no-op: max 0.000) |
  | off-tick VISIBLE < 0.02 | **0.000** |
  | inside pixels ≥ 3,000 | min **6,200** |
  | add-region pixels ≥ 130 | min **266** |

- **The selftest: 49 lines, all ok.** The frame plants, each red for its own reason:
  - step B's opaque wedge: VISIBLE 0.0 %, so **OFF**;
  - **the new one,** an add of zero: VISIBLE 100.0 % but ADDED 0.0 %, so **OFF on the add half**;
  - the add into red: **UNTRUSTWORTHY**, 79.6 % UNKNOWN;
  - the wedge past the add region painted over: **UNTRUSTWORTHY**, 305 pixels inside, under 3,000;
  - the add region painted over: **UNTRUSTWORTHY**, 0 pixels, under 130;
  - an off frame holding the map: **CONTROL FAILED**;
  - 9 on frames: **BLIND**.
  - θ lands within 0.005 rad on all five wedge kinds.
  - **A miss, caught on its own reading:** my first under-the-minimum plant painted the whole wedge, which met the
    earlier "nothing to measure" check instead of the minimum. It was split into the two plants above, each shown
    meeting its own rule.
- **The regression control** (`regress_b.py`, `9c72caaf…`): B mode equals `5ad12d0a` on all 15 comparisons, re-run on
  the final code. It was shown able to fail earlier: one planted space gave 12 differences and exit 1.
- **FROZEN:** `map_bench_read.py` at **`a5f7ba79adaa79ffdb607510255b239e790a33950fab8bb8dc5182fed6899b09`**, kept beside
  it as `map_bench_read.frozen-c.py`. Neither `65d2edf5…` nor `5ad12d0a…` is reused for C. The fetcher is unchanged,
  `7b904453…`.

**I2/I3, the owner's ruling (15:07Z, relayed by the orchestrator at 15:14:33Z): (A) and (C).** His words:

> "Record it as proven at the source level, not as a pass."
>
> "The image-level text check moves to the pre-ship gate, beside the per-function accounting. Its tool must pass its own
> controls there first."

**I2/I3, as recorded: PROVEN AT THE SOURCE LEVEL, not a pass.** The claim of `31164e23` line 3589 ("No printable string
is added against #659's images, because step C prints nothing") is held on step C's source, by (2):
- **The diff:** `b9d28fa5..89f7e7f5` over `src/` and `include/` is 4 files, +151 and −4: `include/MapCompose.h`,
  `include/MapSweep.h`, `src/AircraftManager.cpp`, `src/AircraftManager.h`.
- **The scope print** (`git diff --stat b9d28fa5 89f7e7f5 -- . ':!docs' ':!test'`, accepted 08:59Z): the same four
  files and nothing else. No library, `platformio.ini` or pre-build script sits outside the diff.
- **No emitted literal added or removed.** Its three added quote-bearing lines are a `static_assert` message
  (compile-time only) and two `#include` paths (preprocessor only). No removed line holds a literal.

**Both image instruments failed, and both are named:**
- **printable runs** (`strings_added.py`, at least 6 printable bytes): they counted machine-code and address bytes as
  text. Written after `31164e23`, and never shown on a control. Off, from the instrument.
- **NUL-bounded runs** (`strings_nul.py` `c2d0b8cc…`, at least 6 printable bytes with a NUL on both sides): the
  positive control found all six literals, but its reading carried pointer-table runs (`0x3C21xxxx` data pointers,
  `0x420Dxxxx` IROM addresses). That is the instrument again, the rows' second off reading, and it went to the owner.

**Carried to the pre-ship gate,** below: the image-level text check, whose tool must pass its own controls there
first.

### The step B + C window on E2:08: the request, registered before the window (2026-11-12)

The ask, verbatim (the orchestrator, 16:25:03Z):

> Send me your step B + C window request, as listed: steps and predictions committed; three images by sha256 (step B's
> test image, step C's test image, and E2:08's restore image, the v19 asset f6d73e391631, as 5c flashed it at 15:33Z);
> the write ranges against the partition table; the restore with a boot read (Wi-Fi rejoin, no portal); about 2 h 20.
> Name what the board's OTA check will do with your test images, and confirm the restore image is the v19 asset by sha.

**Nothing in this section has touched E2:08.** It runs only inside a window the orchestrator opens, under the owner's
terms of 04:29Z, and never during a v19 step.

**Four images, not three.** The registered procedure includes CBF4's image plant, P-CBF4 ("an image whose `MapAlloc`
drops `MALLOC_CAP_SPIRAM`, flashed after the real one"; the procedure was ruled good at 04:08:40Z). It is built and
identified as the fourth image. **Whether it runs is Q-W1**, at the end of this section: by its own premise, it most
likely cannot plant on this board.

#### The four images, by sha256

Main moved six commits after both heads were cut. Two of them change the OTA code (`include/OtaInstall.h` and
`src/OtaUpdater.cpp`, #660). The registered procedure builds each image from **its head merged locally into the
then-current main**, so the images are built from those merges.
- Each tree is printed, and equals `git merge-tree`'s.
- Each image is built clean by `b1/build_one.ps1`, with no `PLATFORMIO_BUILD_FLAGS` set.
- Each build's `partitions.bin` equals the release's `partitions-s3-128.bin`, byte for byte.

| image | built from | tree | `firmware.bin` | `app_elf_sha256` (0xB0) |
|---|---|---|---|---|
| **B** | `blipscope-s3-128-mapbench`: #659's head `5f13cc21` merged into main `c73aee2f` (local merge `f852543a`, never pushed), in `wt-mapclient` | `9b6b9f0187c9` | 2,390,592 B, `d489b5932566bbef028c6694a75a8d976b4c68ee63ccc74b008797830b9aa356` | `325a80fbf634…` |
| **P-CBF4** | B's tree plus S-CD-NOSPIRAM's one-line edit (`test/host/sabotage_map_draw.py`'s own anchor), in a worktree of its own, `wt-plant` | `9b6b9f0187c9` + that edit, uncommitted (`AircraftManager.cpp` `3edec203aad6` → `c2c78e056c7d`; the `MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT);` lines 4 → 3) | 2,390,592 B, `006e5071e191f76b34ff58d40ed61d95fd2f59819f03f3ce51b6f61c1c116e87` | `c100e02d1668…` |
| **C** | the same env: step C's head `b46cdac8` merged into main `c73aee2f` (local merge `27bfc9ca`, never pushed), in `wt-main-mc` | `cc99b71c9c4f` | 2,391,328 B, `58e80c8f45ce0c3b0550e5d1f0c19ed54a81d99e3a0f2205e67dad9f2ff2985e` | `458ae7836a6c…` |
| **restore** | the v19 release asset `firmware-s3-128.bin` | n/a | 1,931,840 B, `f6d73e39163177b0dc25516a104bb02021d2048a94f82a66fc42a90f0a2615be` | n/a |

- **The restore image is the v19 asset.** The local file's sha256 equals the digest GitHub publishes for the v19
  release's `firmware-s3-128.bin` (read between 16:37 and 16:43Z: release `v19`, not a prerelease, and `latest` is v19). Its table,
  `partitions-s3-128.bin`, is 3,072 B, sha256 `28f336fe57a9991a5f39db735295a978e0a6aee5710f5e7107ac44bdd812d29b`, and
  also equals the release's digest.
- **"As 5c flashed it at 15:33Z" is not checked here.** Reading E2:08's app partition before the window would touch the
  board. The window's first write checks something weaker, which is enough: `app-only-write.py` reads the board's table
  and otadata before writing, and refuses with nothing written unless the table equals the build's and the board boots
  ota_0 (below).
- **C carries all of B's firmware.** Step C's branch lacks #659's last three commits (`aff4bede`, `b06f32b9`,
  `5f13cc21`), and those touch only `docs/blipscope-map-client-steps.md` and a header comment in
  `test/host/test_ota_identity.cpp`.
- **P-CBF4 is built apart** so that B's and C's build folders keep their registered images. `wt-plant` was added at
  B's merge, with `.pio/libdeps/blipscope-s3-128-mapbench` copied from `wt-mapclient`. Rebuilding B after a plant would
  change B's sha in WiFiManager's two `__TIME__` regions.
- **The edit took in the image.** `imgcmp.py` reads P-CBF4 against B at the same length. Inside the regions it finds
  32 / 32 / 1 / 3 / 3, and **outside them 5 bytes, at 0x11F025-0x11F029**: one changed constant in one instruction,
  the size of the edit. Whether P-CBF4 runs at all is Q-W1, below.
- **Superseded, not window images:** the 16:29-16:36Z builds of the two heads *unmerged* (`dad5dd08…`, `23e1266d…`),
  built before main's movement was read.
- **A harness failure in the first clean build of the merges, not a build failure.** The wrapper ran `build_one.ps1`
  under Windows PowerShell 5.1, which turns pio's stderr warning (`#pragma mark` in QRCode) into a terminating error.
  It was re-run under pwsh, as `build_one.ps1` has always run.
- **If main gains a firmware change before the window opens,** these images stay the window's unless the reviewer rules
  a rebuild. Nothing is rebuilt silently.

#### What the board's OTA check does with the test images: nothing

- **When it runs:** once per boot, in `setup()` (`src/main.cpp`, `MaybeUpdateFirmware`), after the clock syncs.
- **What it does:** it reads the latest release's `version.txt` and prints `[ota] channel=%s%s current=%d latest=%d`
  (`src/OtaUpdater.cpp`). If `latest <= FW_VERSION` it returns, with no download.
- **The numbers:**
  - `FW_VERSION` is **19** in both merged trees (`src/OtaUpdater.h`) and in the v19 asset.
  - `version.txt` on the latest release is 3 bytes, `19` and a newline (read between 16:37 and 16:43Z).
  - So every image in the window finds `19 <= 19` and does not update.
- **The predicted line, at every boot of every image in the window:** `[ota] channel=s3-128 current=19 latest=19`.
  - The scope is empty: `FW_OTA_PREFIX` is unset in this env, and `src/OtaIdentity.cpp` defaults it to `""`.
  - The board is `variant::SLUG`, `s3-128`.
  - No other `[ota]` line follows.
- **The one way it would act:** a release after v19 published during the window. Then `latest` would be over 19, and
  the board would fetch `firmware-s3-128.bin` and replace the test image. That would pass the identity check, since
  board `s3-128` with an empty scope is exactly what the test image asks for. No release is planned, and a release needs
  the owner's go.
  - **Pre-registered:** any `[ota] newer firmware available` line, or any `current=`/`latest=` other than 19, stops
    the window. Nothing more is read from that image, and the restore follows.
- **No NVS write is added by any test image.**
  - The bench's home is held in RAM. `AircraftManager::Initialise`'s `MAP_BENCH` region sets the local `lat`/`lon`
    from the embedded package, and opens no `Preferences`.
  - The one `Preferences` use added to `src/` and `include/` since the v19 tag (`7b943ae0`) is `BootFollowsUpdate()`,
    which opens its namespace read-only.
  - So at runtime the images touch NVS only as v19 itself does: the logbook, the usage counters, the boot-reason
    stamp.

#### The write ranges, against the partition table

The table is the release's `partitions-s3-128.bin`, which is also every test build's `partitions.bin` (from
`partitions-s3-16mb-bignvs.csv`):

| partition | range | written in the window? |
|---|---|---|
| bootloader | 0x0 .. 0x8000 | **no** |
| partition table | 0x8000 .. 0x9000 | **no** (read: 0x8000 + 0xC00, by `app-only-write.py`'s check) |
| `nvs` (settings) | 0x9000 .. 0x1E000 | **no** |
| `otadata` | 0x1E000 .. 0x20000 | **no** (read: all 0x2000, by the check) |
| `app0` (ota_0) | 0x20000 .. 0x660000 | **yes, the only partition written** |
| `app1` | 0x660000 .. 0xCA0000 | no |
| `spiffs` | 0xCA0000 .. 0xFF0000 | no |
| `coredump` | 0xFF0000 .. 0x1000000 | no |

Each write is one `esptool write-flash` of one file at 0x20000, run by `scripts/app-only-write.py`. esptool erases the
4 KB sectors it writes and nothing else:

| write | file | bytes | sectors erased and written |
|---|---|---|---|
| W2, image B | B's `firmware.bin` | 2,390,592 | 0x20000 .. 0x268000 |
| W4, P-CBF4 | the plant's `firmware.bin` | 2,390,592 | 0x20000 .. 0x268000 |
| W6, image C | C's `firmware.bin` | 2,391,328 | 0x20000 .. 0x268000 |
| W9, the restore | the v19 asset | 1,931,840 | 0x20000 .. 0x1F8000 |

- All four writes lie inside `app0`.
- **No factory image, no `erase-flash`, no `--erase-all`.** `app-only-write.py` passes `--flash-mode keep
  --flash-freq keep --flash-size keep` and the one address.
- **After the restore,** `app0` from 0x1F8000 to 0x268000 keeps the tail of image C. The bootloader reads only the
  image's own length (from its header) and checks that image's own hash, so the tail is never read. It is stated here
  because it is the one byte range the restore does not return to v19's state.
- **The refusals, pre-registered.** `app-only-write.py` exits 7, having written nothing:
  - if the board's partition table differs from the build's;
  - or if otadata selects ota_1. That would mean v19 went on by OTA rather than an app-only write.
  - Either one: **stop and report.** Writing otadata is outside the owner's terms.

#### The steps

Every command runs from PowerShell.
- The board is named by its short label and resolved by its USB serial. The COM number is printed beside the label,
  never a MAC.
- The board's LAN address reaches `fetch_fb.py` through `MAPBENCH_FB_HOST` only, and is never printed.
- Every capture stays in the scratch `b1\cap\`, outside the repo. `bench-flash.ps1` writes its own flash log under the
  scratch worktree it runs from. Nothing from either is committed.

| step | what | predicted |
|---|---|---|
| **W0** (done now) | the images above; the reader `map_bench_read.py` `a5f7ba79…` (selftest 49/49), `fetch_fb.py` `7b904453…` and `imgcmp.py` `dc828e47…`, all frozen | already true |
| **W1** | `python scripts\board-port.py --reserve blipscope-bench --by map-client --for "step B+C bench window"`, then `board-port.py blipscope-bench --upload` | the claim is listed; one port |
| **W2** | from `wt-mapclient`: `scripts\bench-flash.ps1 -PioEnv blipscope-s3-128-mapbench -Board blipscope-bench -Label mapbench-B -By map-client -AppOnly -DanielApproved -NoCapture` | its `pio run` compiles nothing, and the tree's `firmware.bin` is still `d489b5932566…` (Y == X byte for byte; `imgcmp.py` reads 0 differing bytes). `checks ok: table matches (6 entries), board boots ota_0, image 2390592 B fits ota_0 @ 0x20000 (6553600 B)`, `write attempt 1: rc 0, verified`, `reset: ok`; exit 0 |
| **W3** | `scripts\bench-trace.ps1 -Port COMx -Label mapbench-B -Minutes 70 -OutDir b1\cap`, opened as soon as W2 returns. `fetch_fb.py --log <that log> --out b1\cap\fb-B` runs beside it | the boot lines arrive in one burst (USJ): `[build] env=blipscope-s3-128-mapbench fw=v19`, the `[map-bench]` boot marker, `[ota] channel=s3-128 current=19 latest=19`. Then P0 to P4, about 66 min. The fetcher ends at P2's first line, exit 0. The trace's end leaves the board in download mode, where W4 starts |
| **W4** (only under Q-W1 (b)) | P-CBF4, written from its build folder as it stands: `python scripts\app-only-write.py --port COMx --build-dir wt-plant\.pio\build\blipscope-s3-128-mapbench --before default-reset`. No `pio run`, so no rebuild; the file is re-hashed first | `checks ok …`, verified, `reset: ok`; exit 0 |
| **W5** (only under Q-W1 (b)) | `bench-trace.ps1 -Label mapbench-PCBF4 -Minutes 6`, to P0's `[map-bench] alloc` line | the line arrives once. Its four numbers are reported as Q-W1 (b) says |
| **W6** | from `wt-main-mc`: the W2 command with `-Label mapbench-C` | as W2, with `58e80c8f45ce…` and 2391328 B |
| **W7** | as W3, `-Label mapbench-C`, the fetcher into `b1\cap\fb-C` | as W3 |
| **W8** | the trace ends (download mode) | |
| **W9** | the restore: a staging folder `b1\restore\` holding `firmware.bin` (the v19 asset) and `partitions.bin` (the release's table), both re-hashed just before. Then `python scripts\app-only-write.py --port COMx --build-dir b1\restore --before default-reset` | `checks ok: table matches (6 entries), board boots ota_0, image 1931840 B fits ota_0 @ 0x20000 (6553600 B)`, verified, `reset: ok`; exit 0 |
| **W10** | the boot read: `bench-trace.ps1 -Minutes 3 -Label restore-v19`, opened as soon as W9 returns. A counts-only check reads it (below) | every positive token present, no negative token |
| **W11** | that trace's end leaves the board in download mode: `esptool read-mac` (it writes nothing) resets it. Then one HTTP request to the board's address, printing the status code only | v19 boots again; the board answers 200 |
| **W12** | `board-port.py --release blipscope-bench --by map-client`; tell the orchestrator | the claim is gone |

**The boot read (W10), from v19's own source, counted, never printed** (every line it reads can carry the LAN address,
the network's name or the device's name):
- **Positive, all of:**
  - `[build] env=blipscope-s3-128 fw=v19` (`include/BuildIdentity.h`);
  - `[WiFi] credentials saved: portal times out after` (`include/WiFiManagerHelpers.h`). The settings survived: it
    prints only when saved credentials exist;
  - `[WiFi] CONNECTED` (`src/main.cpp`);
  - one of `[WiFi] fast join OK` and `[WiFi] autoConnect() returned true (connected)`;
  - `[ota] channel=s3-128 current=19 latest=19`.
- **Negative, none of:**
  - `StartAP with SSID`, `AP IP address` and `Starting Web Portal` (WiFiManager's own lines, printed when the portal
    opens, at the NOTIFY level v19 sets);
  - `Portal saved credentials`;
  - `autoConnect() returned false`;
  - `no saved credentials`.
- **A correction to my earlier draft:** it read "no line matching /portal/i". That would fire on every boot with saved
  credentials, because the positive line above contains "portal". The tokens above replace it.

**Reading, after the board is restored** (none of it needs the board):
- B: `map_bench_read.py --image B`, `--fb wt-mapclient b1\cap\fb-B\frames.jsonl`, then `--derive-c-bands`. The bands
  and B's capture sha go to the orchestrator **before C's capture is read** (Q-CR2 (b)).
- P-CBF4: as Q-W1 rules.
- C: `--image C --cf2 LO,HI --cf3 LO,HI` and `--fb-c wt-main-mc b1\cap\fb-C\frames.jsonl`.
- Each row, its plant and its verdict are as registered above, in "The bench rows" and "Step C's bench rows".

**Duration: about 2 h 35 of board time under Q-W1 (a), about 2 h 45 under (b).**
- Each full run is P0 to P4: 40 + 40 ticks in P1 and P2, then 25 + 25 in P3 and P4, at 30 s each, so 65 min, plus
  about a minute to join and allocate. Each trace is given 70 min.
- Then three writes of about 4 min each (four under (b), plus P-CBF4's 6 min), and the boot read's 3 min.
- B is read while C runs, so reading adds nothing to the board's time.
- Step C's record above says "about 2 h 20", and so did my reply. That counted the two runs and not the flashes.

**Stops, pre-registered:**
- `app-only-write.py` exit 7: nothing was written. Stop and report.
- Exit 8, the write failed twice: the chip is left in the ROM. Try W9, the restore, once through the same path. If that
  fails, stop and report: it may need hands.
- `bench-flash`'s `pio run` compiles anything, or a flashed file is not its registered sha: nothing is read from that
  image. Go to W9.
- A boot banner is not the expected env, or a test image's `[map-bench]` marker is missing: nothing is read. Go to W9.
- Any OTA action (above): stop. Go to W9.
- A reboot inside a run (a second `[build]` banner, or the trace's read error): that run stops and is reported. Its
  first re-run needs a window of its own.
- **B's own P2 reads off** (registered at step C): C is never graded against it, and C's run is skipped. Go to W9.
- W10 shows a negative token, or misses a positive one: stop and report to the owner through the orchestrator. It may
  need his hands.
- `read-mac` does not bring the board back: stop and report.
- **Anything else: stop and decide.** Nothing that needs the owner's hands is attempted; it is stopped and reported.

#### Q-W1, found while writing this: P-CBF4 as registered most likely cannot plant (for the ruling, before the window)

**The registered row:** "an image whose `MapAlloc` drops `MALLOC_CAP_SPIRAM` …, flashed after the real one: `internal`
falls by at least 115,200, and the reader says red."

**Applying the rule, node by node:**
- `MapAlloc` serves exactly four allocations, in `mapcompose::Allocate` (`include/MapCompose.h`):
  - `layer` and `composite`, 115,200 B each;
  - `idx`, 57,600 B;
  - one scratch block of 32 × (vertices + 1) + 4,224 + 13,320 B, which is 192,296 B for Bend's 5,460 vertices.
  - **Every one is at least 57,600 B.** The 4,224 and 13,320 sit inside the scratch block. They are not
    allocations of their own.
- With `MALLOC_CAP_8BIT` alone, ESP-IDF's allocator tries the internal heap first, and takes PSRAM when no internal
  block is large enough.
- `src/main.cpp` records the internal heap's largest free block as **31,732 B at boot**, in its comment above the OTA
  check. That is an older measurement, and the internal heap at P0 has not been measured.
- **So the most likely reading:**
  - all four buffers land in PSRAM;
  - `internal` moves by **0**, and `psram_free` falls by the price;
  - **CBF4 reads green.**
  - The plant does not plant, and the reader is never shown red.
  - `internal` moves only if a free internal block of at least 57,600 B exists at P0. It would then move by 57,600
    (`idx`) or by 115,200.
- **Separately, the frozen reader cannot read a short run at all.** It refuses a capture without its phases before any
  row is graded. Shown on its own CBF4-planted synthetic capture, cut after P0: `INSTRUMENT: P1 has 0 intervals, the
  bench runs 40`, exit 2. So even a plant that did plant could not be read red from a 6-minute run.

**Options:**
- **(a), recommended: P-CBF4 leaves this window.**
  - CBF4 is still shown red by its planted log, in the reader's selftest.
  - The premise CBF4 rests on, that the buffers are asked for in PSRAM, stays checked at the source by CD5 and its
    sabotage S-CD-NOSPIRAM in CI.
  - The window is about 2 h 35.
- **(b): run P-CBF4 as an observation of the premise, ungraded** (W4-W5, about 10 min).
  - The alloc line's four numbers are reported as counts.
  - Predicted: `internal` before − after = **0**; `psram_free` falls by the price within 1,024 B.
  - The registered "≥ 115,200, and the reader says red" is withdrawn as unreachable.
  - The window is about 2 h 45.
- A plant that can plant (for example `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`, which the composer's all-or-none refusal
  would meet) needs its own registration and its own image. It is not proposed for this window.

The P-CBF4 image is built and identified above either way, so (b) costs no build in the window.

### #656's first customer pair: the expected changes, frozen before anything is read (2026-11-12)

**The ask** (weatherscope, blipscope-61, 16:44Z): for #656's layout-invariant comparator (its registration:
`docs/image-compare.md` at `a86ddb690fd5` on `weatherscope/issue656-compare`), "what you expect to change, as
demangled function patterns or objects, frozen before anything is read". The orchestrator ruled at 16:43:30Z that #656
is weatherscope's and that I tell it directly. Two pairs were offered at 16:46:17Z: **P-REDO** (the literal redo of
step B's accounting) and **P-FLIP** (the gate's own question, on the v20 candidate). P-FLIP's set is registered later,
from the candidate's source, since that commit does not exist yet. **This is P-REDO's.**

**The pair:** Bw against Cw, both `blipscope-s3-128-wxlook` (`-DWX_LOOK_AB -DWX_LAYER_SHIPS` in the env, so the map
is compiled in both). Bw is main + `cb9d6ac2`, tree `eb84bef82d41`. Cw is the wiring's commit, tree `0b1d19f08e26`.
`platformio.ini` does not differ. The source differs only in `src/AircraftManager.cpp` and `src/AircraftManager.h`.
The ELFs are kept (32,338,992 B and 32,462,300 B).

**The earlier reading, set beside it as context:** "The wiring's run, as read (2026-11-12)", above.
- `firmware.bin` +14,528 B; `.bss` (`appManager`) +696 B.
- The predicted six changed size, and main.cpp's static initializer +220 B.
- The new symbols by name, with their callers.
- The symbol row was an instrument failure, no verdict: 260 residue symbols, 118 of 225 moved by ≤ 4 B, with four
  alternatives named and none excluded.

**What the source says, read before any image** (from the two trees, by `git diff` and the preprocessor guards):
- **Three translation units include `AircraftManager.h`:** `AircraftManager.cpp`, `main.cpp`, `SoakHarness.cpp`. No
  header includes it.
- **`SoakHarness.cpp` is all `#ifdef SOAK_TEST`** (its lines 3-368), which wxlook does not set.
- **`AircraftManager.cpp` has nine hunks.** Each is placed in its enclosing function by line number in Cw's file:
  - `Initialise` (two), `MapConfigure`, `ConsumeBasemap`, `RebuildWeatherLayer`, `DrawWeatherUnder` (five, one of
    which adds the four new functions after it), and `ConsumeFetchResult`;
  - **`RecordFrameUs`'s hunk** (`++mapAirportsGen`, at Cw's 2463) is inside `#ifdef FOLLOW_BENCH` (2398-2471), which
    wxlook does not set. **So `RecordFrameUs` is not expected to change.**
- **The header** adds `#include "MapCompose.h"`, the four new functions' declarations, and **four data members
  inserted at Cw's `AircraftManager.h:725`** (`mapDraw`, `mapPkgSeq`, `mapAirportsGen`, `mapWxGen`), in the first
  private section. About 99 data members follow them in the class, and every one moves by the inserted size.

**Expected, by object, before the link:**

| object | expected | as patterns |
|---|---|---|
| every object but the two below, `SoakHarness.cpp.o` included | **identical** | none |
| `AircraftManager.cpp.o` | **changed by its source:** the six | `AircraftManager::(Initialise\|MapConfigure\|ConsumeBasemap\|RebuildWeatherLayer\|DrawWeatherUnder\|ConsumeFetchResult)\(` |
| | **new:** the four functions, and the header-only code they reach, instantiated in this object | `AircraftManager::(DrawMapBase\|MapAirportBoxes\|MapAlloc\|MapFree)` (the lambda in `MapAirportBoxes` included), `mapcompose::*` (`LayerSink`'s vtable included), `maplayer::*`, `mapplaces::*`, `bmpk::*`, `wxraster::(Text\|Put)`, `glcd::font`, `wxlayer::OffDisc`, `wxs2::To565` |
| | **changed with no source change: a member-offset shift only.** A function that reads or writes a member declared after `AircraftManager.h:725` carries a moved offset. Where an offset crosses an Xtensa load/store encoding limit, the sequence can grow by a few bytes | any other `AircraftManager::*`, and code inlined into it |
| | **not changed** | `AircraftManager::RecordFrameUs` (its hunk is compiled out) |
| `main.cpp.o` | **changed:** the static initializer, which constructs `appManager` with the new members | `_GLOBAL__sub_I_tft` |
| | **changed with no source change: a member-offset shift only**, as above | any `main.cpp` function inlining an `AircraftManager` member access past line 725 |

**Expected at the link:**
- **Library members newly retained:** `fmin`, `fmax` (called from `mapplaces::ClearsCentre`) and `__fpclassifyd`
  (called from newlib's `fmin` and `fmax`). Nothing else enters the link, and nothing leaves it.
- **Every other unit differs by relaxation and address shifts only** (#656's RELAX), the image having grown by
  +14,528 B.
- **UNDECIDED: 0.**

**How each outcome reads:**
- **As expected:** the earlier residue was offset shifts and relaxation. The wiring's accounting closes with a verdict.
- **A changed function outside the two objects, or a no-source change in them that is not an offset shift** (if #656 can
  separate offset-only changes; if it cannot, the count of such units is reported and not graded): the TU-wide
  inlining alternative, or another not yet named. Report it by name; it is not explained away.
- **A library member other than the three:** named, with its caller, and reported.
- **`RecordFrameUs` changed:** the `FOLLOW_BENCH` reading is wrong. Stop.
- **Anything else, including UNDECIDED above 0: the instrument, or this model, is unreliable at this scale. Stop and
  decide.**

**When:** P-REDO can be read as soon as #656's controls (K0, K3, K2, K4) read as predicted. It needs no v20
candidate. P-FLIP is wanted at the pre-ship gate, about 20-26 Nov.

#### Amendment 1 to P-REDO's expected set, before P-REDO runs (2026-11-12, after #656's offsets-only label of 17:16Z)

**The pair's commits.** Bw and Cw are local merges, and neither is on origin:
- Bw is `f37aa334` = main `e3c2508e` + `cb9d6ac2`.
- Cw is `338845e6` = main `e3c2508e` + `c4ff317a`.
- Both parents of each are on origin (main, and #659's branch).
- `git merge-tree --write-tree` of each pair reproduces the registered tree exactly (`eb84bef82d41` and `0b1d19f08e26`).
- So each build checks out `e3c2508e` detached, runs `git merge --no-ff --no-edit <the second parent>`, and requires
  `HEAD^{tree}` to equal the registered tree before it builds.
- **Correction:** the record above calls Cw "the wiring's commit". Cw is main merged with the wiring's commit
  `c4ff317a`, as the earlier reading says ("C: main + `c4ff317a` (tree `0b1d19f08e26`)").

**The no-source rows, amended.** #656 now labels `CHANGED (load/store offsets only: k)`, and states three cases it reads
as plain CHANGED. All three are member-offset forms:
- an `l32i.n` pushed to `l32i` (an offset of 60 or more, so the boundaries move);
- an offset past 1020, taken through `addmi` or a literal;
- an `addi`/`addmi` forming `&this->m`.

With four members inserted ahead of about 99, those forms are likely, so the record's "offset-only" would read off for a
reason that is not a defect. **Amended:** a CHANGED unit in `AircraftManager.cpp.o` or `main.cpp.o` with no source
change reads EITHER:
- `CHANGED (load/store offsets only: k)`, OR
- plain CHANGED with its **named relocations unchanged**: the same named callees and data symbols, as #656 counts "named
  relocations" in its offsets-only rule. A literal-pool slot is not a named target.

**What it means now:**
- A no-source CHANGED unit in those two objects whose named relocations CHANGE (a callee or a data symbol added or lost)
  is not an offset shift. It is reported by name as the TU-wide inlining alternative, or another not yet named. It is
  not explained away.
- The number of offsets-only units, their total k, and the number of relocation-unchanged plain CHANGED units are
  reported and not graded against a count. I cannot predict how many.
- Asked of #656: print "relocations unchanged" per unit, as weatherscope offered.
- **Unchanged:**
  - every other row of the set above;
  - the GC chain: `fmin` and `fmax` rooted in a CHANGED unit pull in `__fpclassifyd`, which #656's amendment 1a
    covers;
  - `RecordFrameUs` unchanged;
  - UNDECIDED 0;
  - the escape hatch.

#### Amendment 2 to P-REDO's expected set, before anything is read (2026-11-12, after #656's amendment 1f and its relocation print, 18:50Z)

**FILL** (#656's amendment 1f): a CHANGED function whose copies hold the same instructions apart from the assembler's
alignment fill and an instruction's own narrow or wide form now reads FILL.
- **A no-source FILL unit in `AircraftManager.cpp.o` or `main.cpp.o` counts with the no-source rows.** It is
  narrower than "relocations unchanged", and it is one of the offset forms already named: an `l32i.n` widened to
  `l32i` when a member's offset passes 60.
- **A FILL unit in any other object** is a difference in an object predicted identical. It is reported by name, and it
  is off.

**UNDECIDED.** When a FILL unit's size changes, #656 walks its final copy on the image's property table, and this link
discards most objects' tables. Such a unit reads `UNDECIDED <name> (FILL): size a -> b: the image's property table
ends at +0`. Narrow-to-wide forms change size, so these are likely in `AircraftManager.cpp.o`.
- **Amended:** UNDECIDED is allowed ONLY for a FILL unit in those two objects whose size changed, with exactly that
  named cause.
  - Each one is named and counted, and reported as UNACCOUNTED: a known blind spot of the instrument, not a pass for
    that unit.
  - P-REDO's reading then states how many units it could not decide.
- **Any other UNDECIDED** (another cause, or another object) is still "the instrument or the model is unreliable at
  this scale: stop and decide".

**"Relocations unchanged": #656's definition, confirmed as registered by weatherscope.** A function's relocations are:
- the multiset of every relocation's target name, its folded exception table included;
- with branches inside the function dropped;
- with a literal-pool slot replaced by what the literal names (`lit(sym:foo)` counts as `sym:foo`);
- with a literal holding a plain constant dropped, which covers the offsets past 1020 taken through literals;
- with every other name kept, strings included.

Strings stay in: a string gained or lost in a no-source function is the inlining signal this row exists to see. So a
no-source plain CHANGED unit with `relocations: +[...] -[...]` is not an offset shift. It is reported by name as
amendment 1 says.

**Unchanged:** every other row of the set and of amendment 1, `RecordFrameUs`, the GC chain, and the escape hatch.

### CBF3's ring rule (i): trust from ring pixels outside the wedge, registered red first (2026-11-12)

**Why.** On the step B and C bench frames, CBF3 read BLIND: C's 60 of 60 frames failed `wx-fb-check.py`'s
`trustworthy()`, "a ring under 80 % of its colour". `ringdiag.py` read the inner rings at 60-78 % with wedge greens on
ring pixels. The draw order that puts them there is not claimed here: (i-1) measures where the shortfall sits.

**Ruled (i)** (the orchestrator): trust from the ring pixels OUTSIDE the wedge's sector. It is built in my reader's
trust function, and `scripts/wx-fb-check.py` is not edited.

**THE RULE.**
1. **Locate first.** My locator and `wedge_mask` must agree, exactly as now (disagree: INSTRUMENT; not located:
   UNTRUSTWORTHY with the locator's reason).
2. **Then the rings,** over each ring's pixels with `wedge_mask`'s `in_mask` false: the 12 px core and the tail plus
   0.06 rad each side are left out.
   - A ring with under 50 % of its pixels outside the mask is UNTRUSTWORTHY ("the mask covers the ring"). The mask
     is about 18 % of a turn, so this cannot happen on a located frame, and it is a guard.
   - Otherwise a ring is short when under 80 % of its outside pixels show its colour: UNTRUSTWORTHY,
     "ring r=<r>: <k>/<n> outside the wedge". **The 80 % is unchanged**; only the pixels it counts change.
- Built by a counted-replacement builder from `map_bench_read2b.py` (`9ee8dae3…`) as `map_bench_read2c.py`, at the
  two sites `grade_frame` and `grade_frame_c`. Every other line is unchanged.

**THE CONTROLS, each predicted before the code exists:**

| # | the control | the frames | predicted |
|---|---|---|---|
| (i-1) | the inner rings outside the mask on the bench frames | B's 60 (`fb-B`, 30 on, 30 off) and C's 60 (`fb-C`), each through its own tree's source | every located frame: every ring ≥ 80 % outside the mask; ≥ 10 trustworthy of each label on B and on C |
| (i-2a) | a torn frame: every ring pixel outside the mask set to `0x0000` (the mid-draw state where the wedge is not) | the first located on-frame of B and of C | UNTRUSTWORTHY, the ring reason |
| (i-2b) | a torn frame: the same, rows y ≥ 120 only | the same frames | UNTRUSTWORTHY, the ring reason |
| (i-2c) | `wx-fb-check.py`'s own mid-draw case: every ring pixel erased, inside the mask too | the same frames | UNTRUSTWORTHY, any reason (printed) |
| (i-3a) | the innermost ring blanked outside the mask | the same frames | UNTRUSTWORTHY, naming that ring |
| (i-3b) | the threshold: the innermost ring repaired to 100 % outside, then 21 % of those pixels blanked (79 %) | the same frames | UNTRUSTWORTHY |
| (i-3c) | the threshold's other side: repaired, then 19 % blanked (81 %) | the same frames | trustworthy |
| (i-4) | the frames the old rule trusted still trusted | see below | every frame `trustworthy()` passes reads trustworthy under (i) |

**(i-4)'s frames, and a question.** These are "the WD5 frames the old rule was calibrated on". WD5e/WD5f's own captures
are not in the tree, and I have not found them on disk. The nearest are the wx replay session's RP6 frames
(docs/wx-device.md RP1-RP6; session `20261106T1958Z`: RP6-A, RP6-B and RP6-off), graded by the same `wx-fb-check.py`.
**Default (Q-R1):** RP6, unless the orchestrator names where WD5e/WD5f's frames are. RP6-off is the layer-off radar, so
its opaque wedge must be located. A frame there that the old rule trusts and the locator refuses is outside the
predicted set (see below).

**The regression control** (`regress2c.py`): `map_bench_read2c.py` equals `map_bench_read2b.py` byte for byte:
- in B mode and C mode on every log plant and on B's and C's real logs;
- in the synthetic frame selftest, whose frames have whole rings, except the lines naming the new rule.

It is shown able to fail once, with one planted difference.

**THE OUTCOMES, pre-registered:**
- **(P1)** every control as predicted: the rule is accepted. CBF3-B and CBF3-C are then graded against their frozen
  rows, unchanged, and nothing else is.
- **(P2)** a located bench frame with a ring under 80 % outside the mask: the shortfall is not the wedge. STOP; report
  counts per ring (no pixel values beyond counts); no grading.
- **(P3)** under 10 trustworthy frames of a label for a reason that is not the rings (the locator): BLIND stands for
  that capture, and the reasons are printed with counts.
- **(P4)** a plant not red for its own reason, or (i-3c) or (i-4) not trustworthy: the rule or its code is wrong.
  STOP; nothing is graded.
- **Anything else:** stop and report. A second off reading on this row goes to the owner.

**Q-R1, ruled (the orchestrator, ~21:36Z): RP6, READ-ONLY.** This is a disclosed substitution: WD5e/WD5f's own
captures were not found, so (i-4) runs on RP6 instead.
- Each file's sha256 is recorded before use, in my own scratch. Nothing is written, moved or copied into that
  session's scratchpad.
- If RP6-off's opaque wedge does not locate under (i), that is outside the set: STOP.

The ruling also ACCEPTED the registration as registered red first.

**AS RUN (21:40Z): (P2), STOP. No CBF3 grading.**
- **Frozen before the run:**
  - `map_bench_read2c.py` `aed7271f47e3b5295e76d104face247d1b53fed5bb88f11d3a9f0de35c00a194`;
  - its builder `a7d85b0028201f61afbd143c02237f5b72451895c0c836d1a3d0e8cc21a50b04`;
  - `regress2c.py` `aaa5f1e6ba1f9d5527ee2f4faadcb1359cc49f60d75c8849a2093521f8c05eb4`;
  - `ringctl.py` `89ff5ba4f58b6815bba8dad2a26c294372c54c35e751fbd3effbaec93fadffc1`.
- **What held:**
  - 2c's selftest is identical to 2b's (64 ok).
  - regress2c reads 39 identical, exit 0. Its in-process sabotage (`RING_ON_MIN` 1.01) reads 2 DIFFERS, exit 1.
  - RP6 was checked against its recorded shas (42 files) and read only.
- **(i-1), OFF:** every located bench frame has a ring under 80 % outside the mask, the map-OFF frames as much as the
  map-on ones.
  - Located: B off 27 of 30, B on 26, C off 29, C on 30.
  - Passing the trust step: 0 in each.
  - Worst outside shares:

    | ring | B | C |
    |---|---|---|
    | r=39 | 44.7 % | 41.2 % |
    | r=78 | 64.4 % | 74.0 % |
    | r=119 | 89.7 % | 90.4 % |

- **(i-2), (i-3):** not run, because no frame passed the trust step to carry a plant.
- **(i-4), OFF:**
  - RP6-A: 18 trusted by the old rule, 15 under (i). The 3 lost are all on r=39, at 140/179, 140/181 and 146/183.
  - RP6-B: 12 of 12. RP6-off: 12 of 12, its opaque wedge located.
- **What it excludes:** the wedge as the cause, since the mask leaves 81 % of r=39 outside and the map-off frames fail
  alike.
- **What it does not separate:**
  - (a) things drawn over the inner rings near the centre;
  - (b) the rings' colour or radii on this build differing from the source constants the reader takes.
  - A pixel-class diagnostic is proposed, and nothing more runs before a ruling.

**The ruling (the orchestrator, ~21:42Z):**
- P2 is ACCEPTED as read. The miss is recorded as the orchestrator's: (i) was ruled on a mechanism named as unproven.
- **This is the INSTRUMENT's second failure, not CBF3's verdict.** No row has been graded. A THIRD failure goes to the
  owner.
- **GO on the diagnostic,** registered before it runs, with RP6-A's three lost frames added.
- **Then, before any new rule, ask what the trust test is for:** proving a grab is a FINISHED frame. If the rings are
  drawn early and painted over later, they cannot prove that. The next trust test is to be proposed from the draw order
  read from the source (which elements are drawn LAST).

### The ring diagnostic, registered before it runs (2026-11-12)

- **The pixels:** on r=39 and r=78, each ring's pixels OUTSIDE `wedge_mask`'s mask that do NOT show the ring's colour
  (the shortfall).
- **The frames:** B's located frames (B tree), C's located frames (C tree), and RP6-A's three frames lost under (i)
  (C's tree, as `ringctl.py` read them).
- **The classes, each read from the source by `fb_source`/`fb_source_c`:**
  - BLACK (`0x0000`);
  - MAP (the map's colours other than black);
  - MAPGREEN (C only);
  - WEDGE;
  - LABEL (`wx["label"]`);
  - PLANE (`wx["marker"]`);
  - AIRPORT (`DrawAirports`' two colours);
  - OTHER-RING (another ring's colour);
  - CHROME (the beam, the head, the highlight, the dots);
  - WEATHER (RP6 only, look A's colours);
  - AMBIG (in two or more classes, counted apart);
  - OTHER (in none). The OTHER colours with the top three counts are printed as RGB565 values, which are not
    identifiers.
- **And per ring, the geometry:** the ring colour's share on the rasterised circle at r+k, for k = −3..+3.
- **Counts only. Pre-registered meanings:**
  - **(a)** LABEL + PLANE + AIRPORT hold more than 50 % of the shortfall, summed over a capture's located frames:
    things drawn over the inner rings.
  - **(b)** some k ≠ 0 shows the ring's colour on ≥ 80 % of its circle (the ring drawn at another radius), or a
    single OTHER colour holds more than 50 % of the shortfall (the ring drawn in a colour the source does not give).
  - **Anything else:** stop, and the class table is reported as read.
- Each capture (B, C, RP6-A) gets its own reading. B and C disagreeing is itself reported, never averaged.

**AS RUN (21:43Z): neither (a) nor (b) on any ring of any capture, so STOP.** `ringclass.py`
`1041386bf3e0e1777d8383137607eabd71b11e293462cfd1e641033db894ea67`.
- **Excluded, (b):** the ring colour shows on its circle only at k = 0 (B 63.3 % / 75.0 %, C 59.5 % / 81.7 %, RP6
  81.1 % / 94.1 % for r=39 / r=78), at 0.0-0.6 % for every k = ±1..3. Colours in no source class are negligible
  (2-5 px). The rings are at the source's radii, in the source's colour, wherever they show.
- **(a) as registered is not met:** LABEL + PLANE + AIRPORT hold 6-27 % of the shortfall.
- **AMBIG holds the most:** colours in two or more classes, 68-91 % of the shortfall. Its composition was not
  pre-registered, and it was not split after the table was seen.
- **The ruling (~21:45Z):** the STOP is accepted. It does NOT count toward the third failure. The AMBIG split is not run,
  because no decision waits on it: an element drawn early and covered later cannot prove a frame finished. Whether the
  rings were meant to stay visible is a look question, not an instrument one.

### The trust test from the draw order: the instrument's third attempt, registered before any code (2026-11-12)

**What the test is FOR:** proving a grab is a FINISHED frame. So it must be built from what is drawn LAST.

**The draw order, read from the source** (C's tree, `27bfc9ca`, read-only):
1. **The loop, per frame** (`main.cpp` 925-983):
   - `SetPhase(Clearing)`;
   - the clear: `DrawWeatherUnder` copies the whole 115,200 B layer, or `fillScreen` black, so either way every pixel
     is replaced;
   - `SetPhase(Cleared)`, then the sweep;
   - `appManager.Draw`, then `SetPhase(Drawn)`, `pushSprite`, `SetPhase(Pushed)`.
2. **`Draw`** (`AircraftManager.cpp` 4342-4422):
   - the face: on the radar, `DrawRadar`, then `DrawZoomOverlay` and `DrawHoldRing`;
   - then, on every screen, **`DrawScreenIndicator`**, `DrawClock`, `DrawVisualAlert` and the toasts (the last ones
     conditional).
3. **`DrawRadar`** draws `DrawRadarCircles` FIRST among its overlays. Then come the stale banner, the weather note,
   the airports, the follow track, and per aircraft its trail, label, marker and alerts. **That is why the rings cannot
   prove anything:** they are the earliest overlay, and the labels and markers drawn over them are in the frame by
   design.
4. **`DrawScreenIndicator`** (5398-5419):
   - one `fillCircle` per carousel screen at y = 224 and x = 120 − (shown − 1)·6 + 12·slot;
   - the active dot r = 3 in `color888(0, 255, 0)`, the others r = 2 in `color888(0, 80, 0)`;
   - the same dots `wx-fb-check.py` derives from the source, as `src["dots"]`.
   - Drawn after it: `DrawClock`, text size 1 at y = 210, so rows 210-217. It never reaches the dots' rows 221-227.
     Then the conditional alerts and toasts.

**THE TEST, two parts. Each covers the other's blind side:**
- **T1, the device's own claim:** `X-Blipscope-Frame-Finished: true`. The copy is taken inside the phase window
  (Drawn or Pushed at both ends, with the tag, `Sequence` and the clear count equal across it). This is already the
  reader's rule: a frame not Finished is not read.
- **T2, the glass:** every screen dot in `src["dots"]` has its whole `fill_circle` raster in its colour. These are the
  same raster and the same "complete dot" test as `wx-fb-check.py`'s `dot_pixels`, imported, never copied.
  - **Why it proves a finished draw:** the clear replaces every pixel, and the dots are drawn after everything on the
    face but the clock. So a grab between a clear and that frame's `DrawScreenIndicator` holds no complete dot.
  - **Otherwise** the frame is UNTRUSTWORTHY: "screen dot k: m/n px in its colour".
- **T2's named residual:** a copy that starts after a clear and reaches the dots' rows only after
  `DrawScreenIndicator` has run. It would carry dots while missing whatever was drawn in the copy's first rows' span.
  T1's phase window is what excludes that, which is why both are required.
- **The rings leave the trust test entirely.** The locator stays as it is: it defines the regions, and its refusals
  are counted apart from trust, as before.
- **Built as `map_bench_read2d.py`** from 2c by a counted builder:
  - `rings_outside` is replaced by `dots_complete` at the two sites;
  - the selftest's synthetic frames gain the dots, drawn from `src["dots"]` with `wx-fb-check.py`'s `fill_circle`;
  - one new selftest plant: the dots left out, which must read UNTRUSTWORTHY.

**THE CONTROLS, predicted:**

| # | the control | predicted |
|---|---|---|
| K1 | every B and C frame marked Finished (B 60 of 60, C 60 of 60, none straddled) | T2 passes on **120 of 120**. The locator's refusals are unchanged and counted apart (B 7, C 1, as `ringctl.py` read them). So the frames reaching CBF3's regions are B on 26, B off 27, C on 30, C off 29: at least 10 per label |
| K2a | `wx-fb-check.py`'s own mid-draw case: its `synth(..., rings=False)` frame, which it builds without dots | UNTRUSTWORTHY |
| K2b | a real Finished frame (the first on-frame of B, and of C) with every dot's raster set to `0x0000`, the cleared state | UNTRUSTWORTHY |
| K2c | the same frames torn: rows y ≥ 200 set to `0x0000` (the bottom half after the next clear) | UNTRUSTWORTHY |
| K2d | only the active dot erased | UNTRUSTWORTHY |
| K2e | one pixel of one inactive dot changed to `0x0000` (the "whole raster" edge) | UNTRUSTWORTHY |
| K3 | RP6's frames that the old rule trusted (RP6-A 18, RP6-B 12, RP6-off 12, read-only and sha-checked) | T2 passes on **42 of 42** |
| K4 | `regress2d.py`: 2d equals 2c in B and C modes, on every log plant and both real logs | identical. The frame selftest is identical except the new plant's line. Shown able to fail once |

**THE OUTCOMES:**
- **(Q1)** every control as predicted: the trust test is accepted. Then CBF3-B and CBF3-C are graded against their
  frozen rows, unchanged, and nothing else is.
- **(Q2)** a Finished bench or RP6 frame failing T2: the third attempt has failed its own control. **It goes to the
  owner**, with what covers the dots counted by source class.
- **(Q3)** a plant not UNTRUSTWORTHY: the same, to the owner.
- **Anything else:** stop, and to the owner.

**Amendment, before any code (found reading the selftest, ~21:48Z).** reader2's frame selftest holds "the rings
missing" (a synthetic on-frame without rings), expected UNTRUSTWORTHY. Under T2 the rings leave the trust test.
- So that frame, which carries the dots, reads **GRADED**. That is by design: a real frame cannot hold complete dots
  without the rings drawn before them.
- Its expectation changes to GRADED and its line is renamed "the rings missing, the dots present: trusted (the rings
  prove nothing)".
- **K4 now predicts the frame selftest identical except two lines:** this one, and the new "the screen dots missing"
  plant (UNTRUSTWORTHY).
- Nothing else changes.

**The ruling (the orchestrator, ~21:48Z): ACCEPTED. GO: build 2d and run the controls before W5.** One assumption is
to be written down as READ:

**T2'S PREMISE, READ FROM THE SOURCE: THE CLEAR AT A FRAME'S START LEAVES NO SCREEN DOT.**
- `DrawScreenIndicator` has ONE call site, `Draw` (`AircraftManager.cpp:4410`), drawing into the backbuffer every
  frame.
- The clear is `fillScreen` black, or `DrawWeatherUnder`'s copy of the weather layer. That layer is built by
  `fillScreen(0)` plus weather pixels (:3716-3720) and never holds the indicator.
- **If any cached base ever included the indicator, T2 would pass on a torn frame.** That holds for a static layer, a
  pre-composed background or a cached UI plate. M3's static layers and P5's work could introduce exactly that.
- **So it is a property they must not break:** no cached layer copied in at a frame's start may hold the screen
  dots. A change that adds one must re-establish a last-drawn witness first. p4 is told.

**AS RUN (21:52-21:54Z): Q1, every control as predicted. The trust test is accepted.**
- **Frozen:**
  - `map_bench_read2d.py` `9082be910ca087f96656636e1707dbedc88996efe7992f8a9c84b584925ae1fd`;
  - its builder `3a3c18975c22c6b631a0bebe73cee30c27f89fedd2df2e4718a2b63d0facd350`;
  - `regress2d.py` `eac0568b24f0783475a9ad8fcf2e579aabd523775735352c680b5620f43198ce`;
  - `drawctl.py` `fea261fd3c4619af0a5cdcc9284baceb7364e1752cc1fffbba2bc227a8214a49`.
- **2d's selftest:** 65 ok, 0 fail. It is 2c's with exactly the two registered lines changed.
- **K1:** T2 passes on 120 of 120 Finished bench frames. Located as predicted: B on 26, B off 27, C on 30, C off 29.
- **K2:** UNTRUSTWORTHY by T2, each:
  - `wx-fb-check.py`'s own mid-draw `synth(rings=False)`: "screen dot 0: 0/37 px";
  - on B's frame and on C's frame: every dot erased, torn at y ≥ 200, the active dot erased (each "screen dot 0:
    0/37"), and one pixel of an inactive dot ("screen dot 1: 20/21").
- **K3:** RP6-A 18 of 18, RP6-B 12 of 12, RP6-off 12 of 12.
- **K4:** `regress2d.py` exit 0, 39 ok. The frame selftest is identical except the two registered lines (removed 1,
  added 2). Its sabotage reads 2 DIFFERS, exit 1.

**CBF3, graded with 2d against its frozen rows (21:53Z):**
- **CBF3-B: AS PREDICTED,** exit 0.
  - 60 frames: 7 UNTRUSTWORTHY (the locator's), trustworthy on 26, off 27.
  - On-frames: inside map 0.00-1.96 % (< 2 %), outside map 81.33-88.02 % (> 80 %). Off-frames: outside 0.0 %.
- **CBF3-C: OFF,** exit 1.
  - 60 frames: 1 UNTRUSTWORTHY (the locator's), trustworthy on 30, off 29. The off-tick control passes (visible
    0.0-0.1 %, outside 0.0 %).
  - The 30 on-frames, against the frozen criteria:

    | criterion | read | fail |
    |---|---|---|
    | visible > 80 % | 80.8-97.8 % | 0 |
    | added > 80 % | 76.8-99.6 % | 2 (76.8 and 78.9) |
    | outside MAP > 80 % | 11.8-16.2 % | **all 30** |

  - **Not separated:**
    - (1) the add reaching the map outside the wedge;
    - (2) C's outside share, MAP / (MAP + WEDGE + AMBIG + MAPGREEN), counting plain map colours that C's sets also
      hold as AMBIG. C's `OUTSIDE_MIN` is B's 0.80 carried over, with no host minimum on C's classes.
  - A discriminator is proposed, and nothing runs before a ruling. This is CBF3-C's first off reading as a grade.

**The ruling (the orchestrator, ~21:55Z):**
- The trust test and CBF3-B are ACCEPTED (CBF3-B AS PREDICTED).
- CBF3-C is a FIRST OFF READING as a grade. GO on the discriminator, registered before it runs.
- **If (2):** that is the INSTRUMENT, not a pass. C's outside rule then needs a new registration, calibrated on C's
  classes, with a planted green-on-map defect that must read red, before CBF3-C is graded again. Nothing is loosened
  after the fact.
- **If (1):** it goes to the owner as a step C defect.
- The 2 "added" fails are a separate question, after this one. A second off reading on CBF3-C as a grade goes to the
  owner.

### CBF3-C's outside discriminator, registered before it runs (2026-11-12)

- **The pixels:** the OUTSIDE region of C's 30 on-frames, exactly as `grade_frame_c` draws it. That is the disc beyond
  the grown sector and the 12 px core, with the beam from 2d's locator.
  - **A control first:** the script's own outside counts by class must EQUAL `grade_frame_c`'s
    `counts["outside"]`, frame by frame. Unequal means the INSTRUMENT, and it stops.
- **The classes:** C's own `classify_c` (MAP, WEDGE, AMBIG, MAPGREEN, OVERLAY, UNKNOWN). Each AMBIG pixel is split
  in two ways:
  - **AMBIG-with-map:** its colour is in `src["map"]`; otherwise AMBIG-without-map.
  - **Its twin:** the same position in the nearest map-OFF frame. The nearest is by tick, the fetch index (-0/-1/-2)
    equal where one exists, and the earlier frame on a tie. The twin is reported as black (`0x0000`) or not.
- **The share's denominator is the rule's own:** MAP + WEDGE + AMBIG + MAPGREEN, summed over the 30 frames. The
  per-frame range is printed beside it.
- **Pre-registered:**
  - **(2):** MAP + AMBIG-with-map holds MORE than 80 % of the denominator. The instrument; see the ruling.
  - **(1):** MAPGREEN (only) holds MORE than 20 %. A step C defect, to the owner.
  - **Anything else:** stop, and report the table as read.
- The twins' black share is reported, not graded. Counts only.

**AS RUN (21:56Z): (2). The INSTRUMENT, not a pass and not a step C defect. CBF3-C stays ungraded.**
- `cdisc.py` `993972ef1aa970b0ec16a5dc1c73ff6392c3d71a5a3493aefc4a6ecdc0f6beda`. The control passed: its outside
  counts equal `grade_frame_c`'s on all 30 on-frames.
- **The outside, summed** (as shares of the rule's own denominator, 1,028,768):

  | class | pixels | share |
  |---|---|---|
  | MAP | 147,438 | 14.3 % |
  | AMBIG, with map | 710,538 | 69.1 % |
  | AMBIG, without map | 170,674 | 16.6 % |
  | MAPGREEN | 118 | 0.0 % |
  | WEDGE | 0 | |

  OVERLAY (74,244) and UNKNOWN (369) sit outside the denominator.
- **MAP + AMBIG-with-map: 83.4 %** (80.5-86.4 % per frame). **MAPGREEN: 0.0 %** on every frame.
- **Twins:** 78.6 % of AMBIG and 79.7 % of MAPGREEN pixels are black in the nearest map-OFF frame.
- **The reading:** C's classes make most plain map colours AMBIG, because MAPGREEN, Add565(map colour, add ≥ 1),
  overlaps the map's palette. The outside rule counted only pure MAP as map, so B's 0.80 could not be met on C's
  classes. No sign of the green reaching the map outside the wedge.
- **Not explained, and stated as such:** AMBIG-without-map at 16.6 % (its class pairs were not registered), and the
  per-frame minimum, 80.5 %, close to 80 %.
- **Next, by the ruling:** C's outside rule is re-registered on C's classes, with host minimums and a planted
  green-on-map defect outside the wedge that must read red, before CBF3-C is graded again. The two "added" fails come
  after it.

## Step D: the "Terrain shading" setting, registered before any code (2026-11-12)

**The owner's ruling, as relayed:** a new setting, "Terrain shading", with four values. Unset or unknown reads as
Strong. The owner looks at Strong and Extra strong on E2:08 in a window of his own.

| value | S2: land + (shade − mid) × gain, per channel |
|---|---|
| Off | no terrain: land is the theme's land colour |
| Subtle | gain 0.075, mid 128: **today's map** |
| **Strong, the default** | gain 0.45, mid **180** |
| Extra strong | gain 0.45, mid 128 |

**D1, the key and its values.**
- **The key:** `map-terrain` (11 characters, inside NVS's 15). The values are exactly `off`, `subtle`, `strong` and
  `xstrong`.
- **The resolver:** one pure C++11 function, `mapterrain::Resolve(const char*)` → {drawn, gain, mid}. Exact lowercase
  match only. **Anything else** (unset, empty, `STRONG`, `subtle ` with a space, `loud`) **reads as Strong.**
- **Where it is read:** `MapCompose`'s build takes the resolved values in place of the Dark row's fixed 0.075. The
  theme table keeps its row, and only the S2 inputs are overridden.
- **The host test:**
  - each known value maps to its own row, and no two collapse;
  - each planted bad value reads Strong;
  - its plant (the unknown branch returning Subtle) must read red.

**D2, the migration of "terrain subdued", read from the source.**
- "Terrain subdued" is the owner's ruling of 11-06, recorded in `docs/blipscope-basemap-client.md` (S2, gain 0.075).
  In code it is a **build constant**, the Dark row of `MapLayer.h`'s theme table, at v19's tag (`include/MapLayer.h:57`)
  and on main.
- **No saved key holds it.** `git grep` finds no terrain or `map-` key in `ConfigurationWebServer.cpp` at v19 or on
  main, and v19 ships with the map switch off (`WxLayerSwitch.h`: `kShips` false).
- So there is nothing to migrate. Every board reaches v20 with `map-terrain` unset and reads Strong, the default the
  owner chose.
- **For later, from CLAUDE.md's rule that a default only reaches keys that were never saved:** once a customer saves
  the form, their choice is frozen. Any later change of this default needs a `ConfigMigration` entry, and this
  registration names that now.

**D3, pixels: Subtle equals today's map, pixel for pixel.**
- `Channel(land, v, gain)` gains a midpoint, `floor(land + (v − mid) × gain + 0.5)`. Subtle passes (0.075, 128), the
  same integer subtraction then the same double multiply, so the arithmetic is unchanged.
- **The host test:** today's `MapLayer.h` (from main, kept as the test's reference copy) and the new one at Subtle
  build the pinned Bend package over the host sweep the CBF3-C thresholds used (10, 50 and 150 mi; rotations 0 and 90).
  **Predicted: byte-equal images**, and an equal count of images compared, printed.
- **The other values:**
  - **Off** is predicted byte-equal to today's build with `kLayerTerrain` cleared;
  - **Strong and Extra strong** are reported (distinct colours, the share of land pixels at 0), never graded. The look
    is the owner's.
- **Plants:** Subtle at gain 0.0751 must differ (red); Off with the terrain layer left on must differ (red).

**D4, frame time unchanged, and one counted rebuild per change** (the standing frame-time rule).
- **Per frame:** the composed map is copied and blended whatever its pixel values, so the per-frame cost is predicted
  unchanged across all four values.
- **A change:** the map's build key gains the resolved terrain values, so changing the setting triggers exactly ONE
  rebuild.
- **The bench row,** on the frame bench (map and weather on, Bend, 25 mi, 40 aircraft):
  - the setting is cycled in RAM by the bench's own override (`GetStoredString`, no NVS), Subtle → Off → Strong →
    Extra strong → Subtle, each held 100 s settle + 120 s measured;
  - **Predicted:** |Δ p95(`p2p`)| against Subtle ≤ the margin, for each value;
  - exactly **1** frame with the rebuild flag per change, and **0** inside any measured span;
  - each rebuild's time reported. Strong and Extra strong are predicted within ±10 % of Subtle's, the same sampling
    work; Off at or below it.
- **The margin (Q-T3):** max(1.0 ms, the repeat noise tonight's baseline measures). It is frozen once the baseline is
  graded, before any D code runs.
- **Nothing is written to NVS in the bench run.** The setting is cycled only through the bench's RAM override of
  `GetStoredString`, the same rule as the baseline's `Override()`. No `Preferences` call is added, and the
  `check_map_draw` / `check_frame_bench` NVS rows apply to the new region.

**D5, the web build does not follow.**
- The map is never compiled into the web renderer: `BLIPSCOPE_MAP_FETCH` requires `!defined(BLIPSCOPE_WEB_RENDERER)`
  (`AircraftManager.h:33-39`). So the setting has nothing to act on there.
- The config page's control sits inside the same `BLIPSCOPE_MAP_FETCH` gate.
- **A check that runs:** `check_map_draw.py` gains a row that `map-terrain` and the control's label appear only
  inside `BLIPSCOPE_MAP_FETCH` regions. Its plant, the string moved outside, must read red.

**The owner's look:** in his own window on E2:08, an image with the setting is flipped between Strong and Extra strong
on its config page. Nothing in this registration schedules or presumes that window.

**The ruling (the orchestrator, received before 22:06:14Z): ACCEPTED, Q-T1 to Q-T4 at the defaults.** Two notes, docs only, before code:
- **D4:** nothing is written to NVS in the bench run (added above).
- **D3b, what ships is what he picked: checked, not assumed.** The owner chose Strong and Extra strong from the
  reviewer's "Map Terrain Shading" page, which recolours `MapLayer.h`'s own output for `bend.bmpk` at those numbers.
  - **If that page's pixels or its recolour arithmetic can be read,** the host render at Strong and at Extra strong
    must reproduce them, by the page's own comparison (byte-equal where it holds raw values, or the arithmetic shown
    equal).
  - **If they cannot be read,** that is said here, and his look on E2:08 is the check.
  - The search for the page's source is recorded as run, before D's code.
- **D3b, the page found and READ (by 22:07Z):** the "Map Terrain Shading" artifact (`E7oXT7gtbzayL3nUo16z2A`,
  version `1794515286-f24b`), read only.
  - **Its table matches the ruling exactly:** Off g 0; Subtle g 0.075, c 128; Strong g 0.45, c 180; Extra strong
    g 0.45, c 128.
  - **Its arithmetic is in its source:** `chan(land, v, gain, mid) = floor(land + (v − mid) × gain + 0.5)`, clamped
    0-255, then `to565`, applied to `kind == 5` (terrain) pixels of its own embedded per-pixel `kind`/`shade` arrays
    for 10, 25 and 50 mi. Level ground stores shade 180, which is why Strong (mid 180) keeps flat land at the land
    colour.
  - **So D3b is checkable, in two halves:**
    - **(i) the arithmetic, from the other side:** the page's own `base()` run in node over its own DATA (its code,
      not a transcription) gives each strength's RGB565 image. The firmware's new S2 function (host-compiled) applied
      to the same `kind`/`shade` arrays must give **byte-equal** images, for all four strengths at all three zooms
      (12 images). Plant: the firmware side at mid 181 must differ.
    - **(ii) the inputs:** the page's `kind`/`shade` arrays must equal the host `MapLayer` build of `bend.bmpk` at the
      page's view. If the page's generator, and so its view, cannot be found, (ii) is stated as not established, and
      his look on E2:08 is the check for it.
  - **The ruling (the orchestrator, unstamped):** D3b is ACCEPTED as registered, with (i) the strong form. The page's
    generator is not the orchestrator's. Only the page's own source or metadata may name the view; other sessions' or
    the owner's files are not searched.
  - **(ii): NOT ESTABLISHED.** The page's own source names the centre ("Bend is at the centre") and the zooms (10, 25
    and 50 mi). It does not name the rotation, or the rule from a zoom to the map's view and asked radius. So the
    page's inputs cannot be tied to a host build of ours, and **his look on E2:08 is the check for that half.**
  - (i), the arithmetic, stands on its own: the same S2 function on the same inputs gives the same pixels.

**The outcomes:** every prediction above as stated, or stop and report. A second off reading on a row goes to the owner.
This is a customer-facing change (a new config control and a new default look), so it ships only through the owner's
gates.

| Q | the question | the default I take unless ruled |
|---|---|---|
| Q-T1 | the key and value strings | `map-terrain`: `off` / `subtle` / `strong` / `xstrong` |
| Q-T2 | Off: layer off, or gain 0 | the terrain layer not drawn (`kLayerTerrain` cleared). The pixels match gain 0, with no sampling at rebuild |
| Q-T3 | D4's margin | max(1.0 ms, the baseline's measured repeat noise) |
| Q-T4 | the control's labels on the config page | the owner's own words: "Off", "Subtle", "Strong", "Extra strong", in that order |

## Carried to later steps by the ruling

| step | what the ruling fixed |
|---|---|
| **A. Fetch** | `GetBytes` gains `Retry-After` here (the client is its only consumer). The request's `r` is the configured radius, rounded up to cover the view plus the 2-dp offset, capped at 222 km. No `peaks` in the request. RAM only. F6's delay mechanism is registered here |
| **B. Draw** | KJAC and KSFO packages from the map server, committed with their provenance and licence files. S2's level is the smallest level covering the view plus the offset. **Place names (~21:13Z):** the Weatherscope's rule (the largest towns first, at most 8, the centre kept clear), and a name never covers an airport code; Daniel judges it on glass |
| **C. Layering** | The wedge's green is added to the map pixel |
| **D. Setting** | The map compiles under `WX_LAYER_SHIPS`, and its strings join `scripts/check-no-weather.sh`, with the wxlook control finding them. The web renderer writes `map-layer` "false" in writing, with `check_wx_off.mjs` extended. **The place-name setting (~21:13Z)** follows the same rules with its own key, rows and plants beside DO1-DO4 and DOC1-DOC3 |

**A gate on the switch that turns maps on (the owner, 2026-11-12 ~03:39Z).** This is carried to whichever step flips
`WX_LAYER_SHIPS` for a customer image.
- **Before maps are switched on in any customer image, redo step B's per-function accounting** of the map's image with
  #656's layout-invariant comparator, already on the post-v19 list, so the same tool is not built twice.
- The wiring's own symbol row was an instrument failure with no verdict. Its retired `symdiff.py` is not to be reused.
- **Also at that gate (the owner, 2026-11-12 15:07Z):** the image-level text check of step C's I2/I3 ("no printable
  string is added"), beside the per-function accounting. Its tool must pass its own controls there first. Both
  instruments tried on the bench images failed (printable runs; NUL-bounded runs, which carry pointer tables), so a
  third one has to show, on controls, that it separates text from code and pointer data before it reads a customer
  image. Until then I2/I3 stands proven at the source level only.
- **The gate is also named at the switch itself,** `platformio.ini`'s `[weather]` block. CB10's `#error` already refuses
  that flip until step C lands.

## CI for this PR (docs-only), predicted

**32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.** The baseline is #608's
finished docs-only PR run (head `1c88ff6d776a`: 32 = 29/3/0). It started at 18:21Z, after `2e36055a` (#597, merged
17:19Z), the last change to `.github/workflows/`.

**CI for step A's predictions PR (docs-only), predicted: 32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`,
`version`), 0 failed.** The baseline is #613's finished docs-only PR run (32 = 29/3/0). The workflow changes since,
#609 and #618, added steps, not jobs.
