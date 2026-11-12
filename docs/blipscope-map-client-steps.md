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
- **The gate is also named at the switch itself,** `platformio.ini`'s `[weather]` block. CB10's `#error` already refuses
  that flip until step C lands.

## CI for this PR (docs-only), predicted

**32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.** The baseline is #608's
finished docs-only PR run (head `1c88ff6d776a`: 32 = 29/3/0). It started at 18:21Z, after `2e36055a` (#597, merged
17:19Z), the last change to `.github/workflows/`.

**CI for step A's predictions PR (docs-only), predicted: 32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`,
`version`), 0 failed.** The baseline is #613's finished docs-only PR run (32 = 29/3/0). The workflow changes since,
#609 and #618, added steps, not jobs.
