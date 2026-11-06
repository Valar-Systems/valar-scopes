# The M stage: what the dev Worker needs before M1-M6 can run (predictions first, 2026-11-06)

**For:** the reviewer chat. **Status:** predictions, frozen before any code. **Nothing here touches production.**
This makes the measurement ruled ~17:08Z runnable: M1-M6, frozen in §4 of `docs/wx-opera-europe.md`, are unchanged.

## 1. A gap in the plan, found while reading the build path

**`windowFor` refuses a grid whose window leaves the raster** (`proxy/src/wx/opera.ts`: "the window leaves the
raster"). `opuk`'s grid has always sat well inside it, so this has never fired.

**Five of the plan's ten products leave it,** so their builds would throw on every check:

| product | window against the 3,800 x 4,400 raster |
|---|---|
| iceland (`opis`) | **north by 242 px** |
| iberia (`opib`) | **south by 107 px, west by 65 px** |
| malta (`opmt`) | **south by 43 px** |
| southeast (`opse`) | **east by 32 px** |
| scandin (`opsn`) | **north by 54 px** |
| the other five | inside |

- **Why §3 of the plan did not show it:** the sizing scripts clipped the window to the raster (`np.clip`) without
  saying so. That is the `ProgressAlong` shape, in my own instrument: a clamp mapped an impossible value onto a
  plausible one. `docs/wx-opera-europe/eu_window_edge.py`, which does not clip, found it (sha256 `ce38f5467ead06992a9753ab427907f4b68f529bf91493afff345dc33352d3b0`).
- **§3's table stands.** Its window figures are the clipped ones, which is what a build would read.
- **`opsn` and `opmt` are two of M's four products,** so M cannot run on today's code.

## 2. The fix: a stated overrun per product, not a clamp

**A plain clamp would remove a real guard.** "The window leaves the raster" is how a wrong georeference shows up,
and it has no test today.

**So:**
- A product may state **`overrunPx`**: for each edge, how many pixels its window may pass the raster.
- `windowFor` clips to the raster **only within those bounds**. Anything further is refused, as now.
- A product that states none, today's `opuk` and `cref` among them, behaves exactly as now.
- **Cells past the raster read no coverage (14):** `buildMap` already gives a cell whose containing pixel is outside
  the window `fill` -1, and `resample` reads that as 14. That is the truth: the composite has no data there.

## 3. The dev rows and envs

- **`DEV_PRODUCTS`, in `proxy/src/wx/ingest-dev-worker.ts`,** which production never imports. Four rows, each a copy
  of `opuk`'s row except for its id and grid (`cadenceS` 300, `maxAgeS` 1,800, `deviceMaxAgeS` 720, source `opera`).
  Grids are §3's, at 0.01°:

| id | is the plan's | la1 | lo1 | ni | nj | `overrunPx` |
|---|---|---|---|---|---|---|
| `opwe` | west | 55.72 | -7.15 | 1968 | 1638 | none |
| `opsn` | scandin | 73.19 | 5.16 | 2832 | 1222 | north 54 |
| `opux` | isles (`opuk` grown; a separate id, so the dev row cannot shadow production's) | 62.92 | -15.71 | 1954 | 1585 | none |
| `opmt` | malta | 38.1 | 12.16 | 444 | 433 | south 43 |

- **`devCheck`** finds a MODE in `PRODUCTS`, then in `DEV_PRODUCTS`. An unknown MODE stays a `wx_dev_mode_error`.
- **`wx-ingest-dev.wrangler.toml`** gains envs `opwe`, `opsn`, `opux` and `opmt`, each bound only to
  `blipscope-wx-dev`, naming the same Worker, with the cron kept. `check-wx-plant.mjs`'s `DEV_TARGETS` gains the same
  four, so PB2 reads **11 of 11**, and it still fails if any binds production.

## 4. The checks

**`proxy/test/wx-dev-products.test.ts`**, on the real frame head:

| check | what | predicted | sabotage | predicted under it |
|---|---|---|---|---|
| **WO1** | `windowFor` for the four dev grids, each with its `overrunPx`, on the real geometry | `opwe` and `opux` inside. **`opsn` clips exactly 54 px north, `opmt` exactly 43 px south.** Each window lies inside the raster | | |
| **WO2** | the same grids with `overrunPx` one pixel short | **refused** ("the window leaves the raster") | **WO2s:** an unbounded clamp (`allowAny`, test-only) | **red:** not refused |
| **WO3** | `opsn` and `opmt` with no `overrunPx`; and `OPUK_GRID` shifted 12° north with none (9.6° is about where its window's row 1059 reaches row 0) | **refused**, all three. This is the guard's first test | | |
| **WO4** | `buildMap` on `opmt`'s clipped window | every cell whose centre lies past the raster has **`fill` -1**, at least 1 such cell, and resample reads each as **14** | | |
| **WO5** (control) | `windowFor(geo, OPUK_GRID)` | **unchanged:** the fixture's window, 2,224,675 px | | |
| **DP1** | `DEV_PRODUCTS` against §3's table, as whole values | equal, and each row's cells, tiles and frame bytes equal `docs/wx-opera-europe.md`'s | | |
| **DP2** | production's list | `PRODUCTS` ids are exactly `["cref", "opuk"]`, and no dev id is in it | **DP2s:** a test list with `opwe` added | **red** |
| **DP3** | `devCheck` | MODE `opwe` runs `checkAll` with that product alone; an unknown MODE logs `wx_dev_mode_error` and touches no bucket | | |
| **PB2** (`check-wx-plant.mjs`) | every dev deploy target's bindings | **11 of 11** on `blipscope-wx-dev`, with PB2s still failing on `cref` alone | | |

**Also:** `tsc`, and the changed test files, under the machine rules.

**The readings before the push (2026-11-06):**
- **`tsc --noEmit`:** clean.
- **vitest, the three files concerned** (`test/wx-dev-products.test.ts`, `test/wx-opera.test.ts`,
  `test/wx-ingest.test.ts`): **87 of 87, exit 0.** It started at 21:30:16Z, on the start rules: TIME_WAIT 26 and no
  other session's vitest.
- **`check-wx-plant.mjs`:** PASS. `--selftest` passed: PB1s, PB2s on `cref` alone, and FB2s each still fail.

| row | got | verdict |
|---|---|---|
| **WO1** | `opwe` rows 1887-3835, cols 472-2169, 3,309,402 px. `opux` rows 974-2960, cols 38-1635, 3,175,226 px. **`opsn` clipped to row 0 with `n: 54`**, 2,163,980 px. **`opmt` clipped to row 4399 with `s: 43`**, 196,105 px. All inside the raster, and equal to the Python sizing to the pixel | as predicted |
| **WO2** | 53 px for `opsn` and 42 px for `opmt`: **refused**, so 54 and 43 are exact. **WO2s** (unbounded clamp): not refused | as predicted; red |
| **WO3** | `opsn`, `opmt` and `OPUK_GRID` 12° north, none stated: all **refused** | as predicted |
| **WO4** | `opmt`: **13,226 cells** with `fill` -1, **0 of them** anything but 14; the last row reads all 14 | as predicted |
| **WO5** (control) | `OPUK_GRID`: the fixture's window, 2,224,675 px | as predicted |
| **DP1** | the four rows equal §3 of the plan as whole values: cells, tiles, frame bytes, age limits | as predicted |
| **DP2** | `PRODUCTS` is exactly `cref`, `opuk`. **DP2s:** `opwe` added fails the check | as predicted; red |
| **DP3** | an unknown MODE logs `wx_dev_mode_error` and touches no bucket; MODE `opmt` gives lines for `opmt` alone | as predicted |
| **PB2** | **11 of 11** on `blipscope-wx-dev` | as predicted |

## 5. The measurement, after this PR merges

M1-M6 as frozen in §4 of the plan.
- **Order:** `opwe`, `opsn`, `opux`, `opmt`, one at a time, each to its first 12 counted single builds, clock-driven
  (`s_clock.py`), parked on `idle` between them.
- **`opux` stands for M1's `opuk` row,** with the same predicted band.
- **Each read is at ABR 1, by the ~18:22Z amendment.** M5's split comes from `r3p.py`'s `single_build_by_map`.
- **Memory, watched by M4:** a `built` build also holds `encodeMap`'s copy of the map, about 26 MB for `opwe`, on top
  of its 80 MB or so. That is about 106 MB against the 128 MB limit, tighter than the 1.5x cap suggested. An
  `exceededMemory` there is off, and it stops the run.
- **A slot is asked for separately.** Nothing runs before review, merge and that slot.

## 6. CI and the merge

**Predicted: 34 = 31 success, 3 skipped, 0 failed.** It touches `proxy/`.
- Baseline: #579's finished PR run on `c63a9290677f`, started 2026-11-06T20:11:26Z.
- Last job-set change: `84409651` (#552). Last workflow change: `259e8688` (a step). Re-read at the push.
- Merge by the rule.
