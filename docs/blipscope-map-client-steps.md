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

**Status: predictions only. No code until they are ruled.** Step M's host side merged as #619 (`e934e93b`). The
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

## Carried to later steps by the ruling

| step | what the ruling fixed |
|---|---|
| **A. Fetch** | `GetBytes` gains `Retry-After` here (the client is its only consumer). The request's `r` is the configured radius, rounded up to cover the view plus the 2-dp offset, capped at 222 km. No `peaks` in the request. RAM only. F6's delay mechanism is registered here |
| **B. Draw** | KJAC and KSFO packages from the map server, committed with their provenance and licence files. S2's level is the smallest level covering the view plus the offset. **Place names (~21:13Z):** the Weatherscope's rule (the largest towns first, at most 8, the centre kept clear), and a name never covers an airport code; Daniel judges it on glass |
| **C. Layering** | The wedge's green is added to the map pixel |
| **D. Setting** | The map compiles under `WX_LAYER_SHIPS`, and its strings join `scripts/check-no-weather.sh`, with the wxlook control finding them. The web renderer writes `map-layer` "false" in writing, with `check_wx_off.mjs` extended. **The place-name setting (~21:13Z)** follows the same rules with its own key, rows and plants beside DO1-DO4 and DOC1-DOC3 |

## CI for this PR (docs-only), predicted

**32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.** The baseline is #608's
finished docs-only PR run (head `1c88ff6d776a`: 32 = 29/3/0). It started at 18:21Z, after `2e36055a` (#597, merged
17:19Z), the last change to `.github/workflows/`.

**CI for step A's predictions PR (docs-only), predicted: 32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`,
`version`), 0 failed.** The baseline is #613's finished docs-only PR run (32 = 29/3/0). The workflow changes since,
#609 and #618, added steps, not jobs.
