# The Blipscope map client: its steps, their predictions, and their records

**Owner: the map client session** (ruled 2026-11-07 ~19:08Z). The requirements live in
[blipscope-basemap-client.md](blipscope-basemap-client.md). This file holds each step's frozen predictions and what
was read against them. A step's rows are written here before its code, and a miss stays on record.

**Status, 2026-11-07:** step M's predictions are accepted as frozen. **No client code exists yet.** The first firmware
push waits for #609's merge (Q-MC4).

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
