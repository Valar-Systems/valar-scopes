# OPERA radar beyond the UK: the code stage (predictions first, 2026-11-07)

**For:** the reviewer chat. **Status:** predictions, frozen before any code. **Nothing deploys.** This is stage 2 of
`docs/wx-opera-europe.md`, ruled ~23:23Z:

> The code stage: go, with nothing deployed: the 10 product rows, the region table, servedProduct by region, the
> per-product Worker envs and their checks, predictions first. Before any production deploy of the ten, the whole
> account's monthly cost goes to Daniel, with weather on every board and the Weatherscope added, since the $2 no longer
> covers it all.
> The 7-day re-read: yes, add a row for OPERA's listing timeouts: per hour, the timeouts and the frames stored.

The checks D1-D4, G1, A1, CR1, F1 and F2 were frozen in #570 (`docs/wx-opera-europe.md` §2-§6). They stand. This
document says how they are built, adds the checks the M stage taught, and names every existing test whose expected
values this changes.

## 1. What gets built

**a. The 10 product rows, in `PRODUCTS`, with `cref`.**
- Each is `opuk`'s row except its id and grid (`cadenceS` 300, `maxAgeS` 1,800, `deviceMaxAgeS` 720, source `opera`).
- Grids are §3's table: the box of its rule's served land, grown by 2.02°, at 0.01°.
- **`overrunPx` is the measured overrun** (`docs/wx-opera-europe/eu_window_edge.py`): `opis` north 242, `opib` south
  107 and west 65, `opmt` south 43, `opse` east 32, `opsn` north 54. The other five state none.
- **`opuk` keeps its id and grows to the isles grid** (1954 x 1585, la1 62.92, lo1 -15.71). `OPUK_GRID`'s old value is
  kept as the fixture grid, `OPUK_FIXTURE_GRID`: the stored OPERA fixtures hold only its window's ranges.
- **The dev rows go.** `opwe`, `opsn` and `opmt` are now production rows with the same grids. `opux` was `opuk` grown,
  which is now `opuk`. `DEV_PRODUCTS` becomes empty, and `devCheck` finds a MODE in `PRODUCTS`.

**b. The region table.**
- **The generator, `proxy/scripts/gen-wx-region.mjs`:**
  - reads the Natural Earth 1:10m admin-0 file, v5.1.2, and refuses any file whose sha256 is not `239eec57…f8255`;
  - keeps the 40 countries (`ADMIN` names, `docs/wx-opera-europe.md` §1) and drops each polygon part with no vertex in
    the OPERA raster;
  - tests every 0.01° lattice point by the pixel-centre rule (even-odd scanline), and adds today's UK land box
    (Q-UKBOX);
  - gives each served point the first rule rectangle that holds it (§3's ordered rules);
  - writes **`proxy/src/wx/region.generated.ts`**: a header (the file's sha256, the 40 names, the rules), the first row
    and column, and the runs, as a base64 `Uint16Array` of `(first col, last col, product index)` triples with a
    per-row offset.
  - **`--check`** regenerates it and refuses any difference.
- **Predicted size: 3,603 rows, 22,822 runs, about 144 KB of runs** (`eu_table_size.py`, shapely, 2026-11-07), so
  about 200 KB as base64.

**c. `servedProduct` by region.** `/wx/cref` is served by the product the table gives the quantised point, else by
`cref` as today. `landBox` and `inLandBox` go. `byWindow` stays, as D2s's and D3s-b's sabotage.

**d. The per-product Worker envs (Q-WORKERS).**
- `ingest-opuk-worker.ts` becomes **`ingest-opera-worker.ts`,** reading its product from a `PRODUCT` var. An unknown
  product logs `wx_ingest_error` and builds nothing.
- **`wx-ingest-opera.wrangler.toml` replaces `wx-ingest-opuk.wrangler.toml`.** It has an `[env.<id>]` for each of the
  10 (name `blipscope-wx-ingest-<id>`, its own cron, `cpu_ms` 12,000, `WX` -> blipscope-wx, `WX_INGEST` "on"), and an
  `[env.<id>-inert]` for each (the cron kept, `WX_INGEST` "off").
  - `opuk`'s env keeps the Worker name `blipscope-wx-ingest-opuk`.
  - **The top level is inert,** with no cron and `WX_INGEST` "off", so a deploy without `-e` builds nothing.

## 2. Existing tests this re-registers (named, with the new expectation)

| test | today | after |
|---|---|---|
| K1, K2, K4′, K9, K10/PK1, MP1-MP4, MF1-MF3, WO5 (fixture-based) | `opuk` with `OPUK_GRID` | **a test product on `OPUK_FIXTURE_GRID`,** with the same expectations; WO5's window still 2,224,675 px |
| K5 (the UK land box dispatch) | the box | **replaced by D5** (every lattice point in the old box goes to `opuk`) |
| DP1, DP2, DP3 (`wx-dev-products.test.ts`) | 4 dev rows; `PRODUCTS` is `cref` and `opuk` | **DP1** removed (the rows are production's); **DP2:** `PRODUCTS` ids are exactly the 11, `DEV_PRODUCTS` empty; **DP3:** MODE `opmt` reaches the production row |
| WO1-WO4 | the 4 dev grids | **all 10 grids** (W1 below) |
| FT1, FT2, FX (`wx-ingest-split.test.ts`) | the opuk toml's 2 targets | **the opera toml's 21 targets** (FX below) |
| FB2 (`check-wx-plant.mjs`) | the opuk Worker's 2 targets | **all 21**, each bound only to blipscope-wx with its `WX_INGEST` |
| `wx-age.test.ts` (A1's ancestor) | `opuk` 720 | **every OPERA product** 720 (A1) |

## 3. The checks, frozen

| check | what | predicted | sabotage | predicted under it |
|---|---|---|---|---|
| **G1** | `--check` (run here, with the pinned file); the module's header against `products.ts`; runs sorted, non-overlapping, product indexes valid | byte-identical; header rules = the products' order; all runs valid | **G1s:** one run edited | `--check` refuses |
| **D1** | `servedProduct("cref")` at the 40 label points, quantised | **40 of 40** on `eu_country_products.py`'s products | **D1s:** generated without Malta | **red on 1** |
| **D2** | the 15 left-out countries' label points | **15 of 15 -> `cref`** | **D2s:** `byWindow` | **red on exactly 8** (Italy, Bulgaria, Belarus, Albania, Ukraine, Kosovo, Faroes, North Macedonia) |
| **D3** | the 15 named crossings, points from `tools/wx/eu_borders.py` (shapely, independent), as a committed fixture | served side -> its product, excluded side -> `cref`, **all 15** | **D3s-a:** Italy added (left-over refusal off). **D3s-b:** `byWindow` | **a: red on the 5 Italian sides. b: red on all 15** |
| **D4** | every lattice point within 3 cells of a served-excluded border: the table against shapely | **0 disagreements**, the count printed | **D4s:** the table with Italy added | red: the Italian-side points |
| **D5** (Q-UKBOX) | every lattice point in today's UK box | **all -> `opuk`** | **D5s:** the box dropped from the generator | red: the box's sea points go to `cref` |
| **W1** | `windowFor` for all 10 grids with their `overrunPx`, on the real head; and one pixel short | each inside the raster; **one short is refused** for the 5 that state one; the other 5 have no edge to state | **W1s:** one product's `overrunPx` removed | that product refused |
| **A1** | every OPERA product | `maxAgeS` 1,800, `deviceMaxAgeS` 720; `X-Wx-Max-Age: 720` on 200 and 304 | **A1s:** one row without `deviceMaxAgeS` | red on it |
| **CR1** | `/credits`' OPERA line | exactly one; "CC BY 4.0" and the licence link; no country or region name, **except "France" inside the producer's name, "Meteo France"** (re-registered: #570's list held all 40 names, and as frozen it would fail on today's correct line) | **CR1s-a/b:** "in the UK"; CC BY removed. **CR1s-c:** "Meteo France" -> "France" alone | red on each |
| **F1** | `scripts/check_wx_device_cref.py`, on main's firmware (#445 is merged) | every `WeatherUrl(` passes `"cref"`; `WxEnabled()` reads no lat/lon | **F1s:** a planted second call, a planted latitude test | red on each |
| **F2** | `handleWx` at the 40 label points with the device's rounding, a synthetic frame seeded per product | 40 of 40 by the product D1 names, `X-Wx-Max-Age: 720` | **F2s:** without Malta | red on 1 |
| **FX** | the opera toml as deployed text: 21 targets | each env's name, cron, `cpu_ms` 12,000, `WX` -> blipscope-wx and `WX_INGEST`, as a whole list; the top level inert | **FXs:** one env bound to blipscope-wx-dev | red on it |
| **Q-COAST** (printed) | Natural Earth 1:10m populated places in the 40 countries | the share whose quantised point falls outside the region, for the coast ruling | | |

- **F1 joins CI** as two `run` steps in `device-ids.yml` (the check and `--sabotage`). That is a step change, not a job
  change.

## 4. The 7-day re-read (about 2026-11-13), its rows registered now

| row | what | form |
|---|---|---|
| cost | `cost_read.py` over 30 days | the total and each line, against §4's projection |
| MD-6 | `built` lines a day for `opuk` | about 1 a day (`expire-1d`) plus any cold-isolate races |
| **OPERA timeouts** (ruled ~23:23Z) | per hour, for 7 days: `wx_ingest_error` lines by kind, and frames stored | **printed:** every full hour's timeouts and frames stored, with any hour under 12 frames named |

## 5. Before any production deploy of the ten

**The whole account's monthly cost goes to Daniel first,** as ruled: with weather on every board and the Weatherscope
added, from the same instrument. The $2 no longer covers it all. Stage 3's rows are registered in their own PR, after
that.

## 6. CI and the merge

**Predicted: 35 = 32 success, 3 skipped, 0 failed.** It touches `proxy/` and `scripts/`.
- **Baseline:** #586's finished PR run on `a1a476ec424b` (`proxy/`), started after `caf63cc9`: 35 = 32/3.
- **Last job-set change:** `caf63cc9` (#445). **Last workflow changes:** `2e36055a` and `848330eb`, steps only. F1's
  two steps add no job. Re-read at the push.
- **The suite:** run whole, one run at a time on Daniel's PC under the start rules, because the re-registrations touch
  many files.

Merge by the rule.
