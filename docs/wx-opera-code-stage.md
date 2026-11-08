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

## 7. Before any check ran (2026-11-08 ~00:20Z): one row off, one gap, and the rows §2 missed

Written before any D, A, W, F, FX or vitest row ran. Two items wait for a ruling; nothing is pushed until it comes.

**Already read, as predicted:** the generator's first run gave **3,603 rows, 22,822 runs, 0 points left over**, every
product's grid at least 2.02 deg beyond its served points, and G1 byte-identical; G1s (one base64 character of a run
changed) was refused at line 317. The table is 136,932 B of runs plus a 14,416 B row index, 215,634 B as the module.
The frozen "about 144 KB" counted 2 B a row, and the index is a `Uint32`, 4 B a row. Decoding it takes 0.53 ms (Node).

### 7.1 Off: D1 reads 39 of 40 on its inputs alone (for a ruling)

- **Lithuania's label point, quantised, lies exactly on 55.10 N,** the north edge of the central rule (`opce`).
  - **The generator closes every rule's edges** (first match, every bound inclusive, as the UK box), so it gives `opce`.
  - **D1 froze "`eu_country_products.py`'s products".** That script, and `eu_d1q.py`, tested points with shapely's
    `contains`, which leaves a rectangle's edges out, so they give `opba`.
- **One other fixture point sits on an edge:** D3's Brenner served side, on 47.00 N (central's south edge): `opce`
  closed, `opse` open. D3's row names no product, so its form is unaffected.
- **Why the edges are closed:** on a lattice, open rectangles leave every point on a shared edge with no rule (63.00 N
  between `opsn` and the two below it, for one), and the generator refuses a left-over point. `eu_table_size.py` closed
  them, and its 22,822 runs are the generator's exactly.
- **My miss:** `eu_d1q.py` checked "the same product as unquantised" with an area predicate, on points that can lie on
  an edge.
- **Proposed:** the edges stay closed. D1 and F2 expect the fixture's products (`tools/wx/eu_borders.py`, closed
  edges), so Lithuania expects `opce`, and the miss is recorded.

### 7.2 A gap: as frozen, the next proxy deploy from main would route Europe to products with no frame (for a ruling)

- **§1c sends `/wx/cref` to the region's product,** but nothing here deploys an ingest.
  - The proxy is deployed from main by another session. So once this merges, its next routine deploy would answer the
    35 non-isles label points (and every point like them) with **503 `wx_stale`**, where today they get cref's 200
    all-14.
  - That is the rollout order §3 forbids. The same deploy would make `/healthz` read 11 `latest` records a call
    instead of 2.
- **Built as proposed, pending the ruling:**
  - **`ROUTED`** (`products.ts`) holds the products `/wx/cref` may be answered from: `cref` and `opuk`.
  - `servedProduct` returns `cref` for a region product outside it, and `/healthz` reports `ROUTED` only.
  - **Stage 3 adds each product to `ROUTED`** after its ingest runs and its `latest` is read, each one a proxy deploy
    of its own.
  - The D rows grade the table with every product routed (`DispatchOpts.routed`).
- **What would change at that next proxy deploy under the proposal:** the isles rule's served points outside today's
  box get `opuk`, **48,721 lattice points** (`opuk`'s 1,216,382 less the box's 1,167,661).
  - That is Ireland west of 8.7 W, the Channel Islands, and France's coast between 49.1 and 49.8 N west of 1.8 E.
  - Today those points get cref's all-14. Every one is inside today's `opuk` frame, and no point loses `opuk` (D5).
- **The alternative,** a merge that changes no answer at all: `ROUTED` = `cref` alone, plus today's land box kept for
  `opuk` until stage 3. That is a second dispatch path, so it is not recommended.

### 7.3 Rows §2 missed, re-registered now (same expectations unless stated)

| test | change |
|---|---|
| K4 (1,515 x 1,455, 9 tiles; the swap BLIND 3 = 3), K4′ | on `OPUK_FIXTURE_GRID`: both are records of that grid |
| K6 CONTROL (`OPUK_GRID` is the land box plus 2.02) | on `OPUK_FIXTURE_GRID`. K6's own sweep stays on `OPUK_GRID`, the isles grid: 0 views outside |
| K7, K8, `wx-age.test.ts` | their synthetic frames on `OPUK_FIXTURE_GRID` (any grid serves; the smaller one keeps them fast) |
| K5 | **kept, not replaced by D5:** under `ROUTED` it is R1's endpoint form (LHR `opuk`; CDG, KMCI and PHNL `cref`'s all-14) |
| PS1 (`wx-ingest-dev.test.ts`) | `checkAll` with no list checks the 11, not "cref and opuk" |
| `metrics.test.ts` (every product's route is listed) | `KNOWN_ROUTES` gains the nine `/wx/<id>` paths |
| K5's sabotage, A6 | `handleWx`'s fourth parameter is `DispatchOpts` (`{ byWindow: true }`), not a boolean |
| the healthz test (`wx-endpoint.test.ts`) | unchanged under the proposal: `cref` and `opuk` only |
| the dev toml's `opux` env | MODE `opux` is now a `wx_dev_mode_error`: it was `opuk` grown, which `opuk` now is |

**One more thing this PR changes outside the tests:** `opuk`'s recorded rollback command names
`wx-ingest-opuk.wrangler.toml`, which this PR removes. From main after the merge it is
`npx wrangler rollback <id> -c wx-ingest-opera.wrangler.toml -e opuk -m "<message>"`.

### 7.4 New rows, registered now

| check | what | predicted | sabotage | predicted under it |
|---|---|---|---|---|
| **R1** | `servedProduct` and `/wx/cref` at the 40 label points under the committed `ROUTED` | the 5 the fixture gives `opuk` (the United Kingdom, Ireland, the Isle of Man, Jersey, Guernsey) are `opuk`; the other 35 are cref's 200 all-14 with no `X-Wx-Max-Age` | **R1s:** `ROUTED` with all 11 | **red on 35:** 503 `wx_stale`, no frame stored for them |
| **R2** (printed) | the lattice points whose answer changes at the next proxy deploy | **48,721** | | |
| **FT2c** | the entry with `WX_INGEST` "on" and a `PRODUCT` that is no OPERA id (`nope`, `cref`) | one `wx_ingest_error` a check; the bucket and fetch untouched | **FT2cs:** the check falls back to `opuk` | red |
| **FXs-b** | the opera toml with one env's `PRODUCT` set to another product's id | | | **red:** FX, one product checked twice and one not at all |
| generator guards | the per-country drops (§1's table), no straddling part, no left-over point, every grid's margin at least 2.01 deg | each refuses the table | | |

### 7.5 The ruling (2026-11-08 ~02:33Z), verbatim

> For: the UK data session, from the reviewer chat, 2026-11-08 ~02:33Z
>
> Q-EDGE: yes. Edges stay closed, a point on a shared edge goes to the first rule in the table's order, and the generator says so in a comment. D1 and F2 expect the fixture's products, so Lithuania expects opce. The open-edge prediction stays on record as your miss.
> Q-ROUTED: (a), on three conditions:
> - The PR's record states, in one line the next proxy deployer can cite, that 48,721 points (western Ireland, the Channel Islands, France's coast between 49.1 and 49.8°N) switch to opuk, all of them inside today's opuk frame.
> - After that deploy, you read R1 and R2 on production.
> - Each stage-3 product joins ROUTED in its own deploy.
> The rollback: the same PR updates md_rollback.sh and every recorded rollback command to -c wx-ingest-opera.wrangler.toml -e opuk, with a dry check that it names blipscope-wx-ingest-opuk.
> The session claims, once #618 merges: your hand-run and scripted ingest deploys take the deploy claim first, as in the ~02:33Z announcement.

**For the next proxy deployer, the line to cite (Q-ROUTED's first condition):** at that deploy, **48,721 lattice points
switch from cref's all-14 to `opuk`** -- western Ireland, the Channel Islands, and France's coast between 49.1 and
49.8 N -- **all of them inside today's `opuk` frame**, and no point loses `opuk` (D5). Every other answer is unchanged:
`ROUTED` holds `cref` and `opuk` only. **After that deploy, the UK data session reads R1 and R2 on production.**

**The rollback, moved as ruled:** `tools/wx/md_rollback.sh` (in the repo now; it was scratch-only) runs
`npx wrangler rollback <target> -c wx-ingest-opera.wrangler.toml -e opuk -m ... -y`, and logs itself in `rollbacks.log`,
since the claim hook cannot see a rollback inside a script. The recorded commands in `docs/wx-opera-europe.md` (PKD-0),
`docs/wx-opuk-split.md` (opuk's stop, now `-e opuk-inert`) and `docs/wx-opera-map-r2.md` (the RD escape, and beside the
rollback that ran) name the new form, with the old one kept as "was". The records of what ran in `docs/wx-opuk.md` are
left as they ran.
- **Its dry check, read 2026-11-08 ~02:41Z** (`md_rollback.sh --dry`, read-only): `[env.opuk]` names
  `blipscope-wx-ingest-opuk` (tomllib on the file wrangler reads); the Worker `-e opuk` resolves to holds the target
  `b7626571` (`versions view`, exit 0) and runs `4da941e4` at 100%; the control, the top level, reaches no Worker (exit 1,
  10007). **DRY PASS.**
- **Its plant:** `[env.opuk]` renamed `blipscope-wx-ingest-opux` read **DRY FAIL** (`env-name`, `target-not-found`,
  `status-unreadable`); the file was restored by bytes, its sha checked.

### 7.6 How the rows run, and each plant's whole failing set (registered before the first run)

**The vitest rows,** in `test/wx-region.test.ts` unless named: G1's header, D1, D2, D3, D5, A1, F2, R1 and R2, with
the in-test sabotages D2s, D3s-b and R1s. Also W1 and W1s (`wx-dev-products.test.ts`), FT2c and FX
(`wx-ingest-split.test.ts`), and CR1 and CR1s-a/b/c (`photolicense.test.ts`). An in-test sabotage passes when it reads
exactly as predicted:
- D2s: the 8 named;
- D3s-b: 15;
- R1s: 35 at 503 `wx_stale`;
- W1s: each of the 5 refused;
- CR1s: `names UK`, `no CC BY 4.0`, `names France`.

**The plants that need a regenerated table or an edited row** run from `proxy/scripts/sabotage-wx-region.py` (local:
the generator needs the 13 MB file). Each compares the whole failed list and the rows' `RESULT` lines:

| plant | what | failed, predicted | rows, predicted |
|---|---|---|---|
| **D1s + F2s** | generated `--drop Malta` | **G1, D1, F2** | D1 `["Malta"]`, F2 `["Malta"]` |
| **D3s-a** | generated `--add Italy --allow-leftover` | **G1, D2, D3** | D2 `["Italy"]`, D3 the 5 Italian excluded sides |
| **D5s** | generated `--no-ukbox` | **G1, D5** | D5 `notOpuk` above 0, printed |
| **A1s** | `opis`'s row with `deviceMaxAgeS` undefined | **A1, F2** | A1 `["opis: row"]`, F2 `["Iceland"]` |

The unsabotaged file reads 12 of 12. Under a sabotage switch, the generator's two guards on a rule that serves nothing
and on a grid's margin report instead of refusing: without Malta, `opmt`'s rule serves nothing, and with Italy, `opse`'s
grid is 1.970 deg short in the south. G1 refuses a sabotaged header in any case.

**The split plants** (`sabotage-wx-split.py`, 5 tests unsabotaged):
- FT1s: FT1, FX.
- FT2s: FT2b, FX.
- FXs: FX.
- FXs-b: FX.
- FT2cs: FT2c.

**FB2s** (`check-wx-plant.mjs --selftest`) fails on `opuk-inert` alone. **F1s-a and F1s-b**
(`check_wx_device_cref.py --sabotage`) each read FAIL.

**Already read before this subsection, against §3's frozen rows, as predicted:**
- F1 PASS: 1 call site, `"cref"`, and no lat/lon identifier in `WxEnabled()`'s 8.
- F1s-a and F1s-b each read FAIL.
- The plant selftest passed, FB2 over all 21 targets, and FB2s failed on `opuk-inert` alone.

**Offline, `tools/wx/eu_borders.py`:** D4 against the committed table, 0 disagreements and the count printed. D4s
against the table with Italy added: its disagreements equal the points of the set inside Italy, both printed. Q-COAST
is printed.

## 8. The readings (2026-11-08, 02:57Z onward; nothing deployed)

### 8.1 As predicted

- **The ten test files this stage touches:** 205 of 205 passed (02:57:18-02:57:50Z).
  - **G1:** 3,603 rows, 22,822 runs and 6,921,706 points, with 0 bad runs.
  - **D1:** 40 of 40, with Lithuania on `opce`.
  - **D2:** 15 of 15 get cref's all-14. D2s left exactly the 8 named.
  - **D3:** all 15 crossings as predicted. D3s-b moved 15.
  - **D5:** the box's 1,167,661 points are all `opuk`.
  - **A1:** all 10 rows.
  - **F2:** 40 of 40.
  - **R1:** the 5 isles label points get `opuk` with 720, and the other 35 get cref's all-14 with no header. R1s gave
    35 at 503.
  - **R2:** 48,721, out of `opuk`'s 1,216,382.
  - **W1:** all 10 windows inside the raster, and every stated side exactly on the raster's edge.
  - **K6:** 329,371 cases, with 0 views outside the isles grid.
- **D4:** 58,789 lattice points lie within 3 cells of a served-excluded border, with **0 disagreements**.
  - **D4s** (Italy added): **5,075 disagreements, which equals the 5,075 set points inside Italy.**
  - The set's points inside each excluded neighbour: Russia 9,259; Ukraine 6,651; Italy 5,075; Belarus 3,392;
    Bulgaria 2,970; Kosovo 1,031; Albania 485; North Macedonia 260; Morocco 84.
- **The region driver:**
  - **D3s-a** failed G1, D2 and D3: D2 `["Italy"]`, and D3 the 5 Italian excluded sides.
  - **D5s** failed G1 and D5, with `notOpuk` 764,142.
  - **A1s** failed A1 and F2: A1 `["opis: row"]` and F2 `["Iceland"]`.
  - Every file was restored by bytes and its sha checked.
- **The split driver:** FT1s, FT2s, FXs, FXs-b and FT2cs, all as predicted.
- **The Plan S driver:** PS1s, PS2s, PS3s and PS45s, all as predicted with the 11 products.
- **The credits driver:** all four as registered, 37 tests.
  - It ran on an LF copy of `src/photolicense.ts`. On this CRLF checkout its two-line anchors never match, and it stops
    at RIG (exit 2).
  - That defect predates this branch. It is recorded here and not fixed in this PR. The file was restored by bytes.
- **The generator** `--check` is byte-identical, and the fixture regenerates byte-identical.
- **The inputs:** the populated places file, `ne_10m_populated_places.geojson` sha256
  `9b8e3de09048ef00dfc70357dbb9fa324493f214b5e0ae4daf1aa79a8d10116b`, came from Natural Earth's v5.1.2 tag. **The same
  tag's admin-0 file hashes to the pinned `239eec57…f8255`**, so both inputs are one release.

### 8.2 Off: D1s + F2s also turned R1s red (for a ruling)

- **What it read:** the failed set was G1, D1, F2 and **R1s**, where G1, D1 and F2 were predicted. D1 and F2 read
  `["Malta"]` as predicted.
- **Why:** R1s counts the label points that route to a product with no frame. Without Malta in the table, Malta's
  point goes to cref, which has a frame, so R1s counts 34 of 35.
- **My miss:** applying R1s to the plant, row by row, gives exactly that.
- **Nothing in the code changes.**
- **Proposed:** D1s + F2s is re-registered as G1, D1, F2 and R1s, with R1s reading 34.

### 8.3 Q-COAST, printed for the coast ruling

- **Natural Earth's 1:10m populated places in the 40:** 609.
- **20 lie outside the raster,** in the parts §1 leaves out by design: France 13, Portugal 4, Spain 3.
- **Of the 589 inside the raster, 5 (0.85 %) quantise to a point the table does not serve:** Norway 2, Sweden 1,
  Ireland 1, France 1. Those 5 are the coast question.
- **No place is named** here, and no point is printed.

### 8.4 The whole suite

- **Run once,** on Daniel's PC, 03:03:26-03:04:22Z.
- **The start rules held:** no other vitest or `sabotage-` process was running. TIME_WAIT read 564; the run waited
  for it to fall under 2,000.
- **74 files passed and 1 was skipped; 1,035 tests passed and 1 was skipped; 0 failed.** The skip is
  `basemap-md2.test.ts`'s MD2, which is not this branch's.
- stderr is the pool's temporary-folder `EBUSY` lines and the tests' own expected logs.
