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

**Predicted: 35 = 32 success, 3 skipped, 0 failed.** It touches `proxy/`, and no firmware `src/`.

**Re-frozen before the PR was opened, 2026-11-06 ~22:05Z.** This section first said 34, from #579's run. The re-read
at the push found main had moved under it:
- **The last job-set change is now `caf63cc9` (#445),** which adds the matrix leg `blipscope-s3-128-wxlook`
  (`skus.yml`) and two steps. It is also the last workflow change.
- **The baseline:** main's finished run on `1ce7a73a` (#581, docs-only), started 2026-11-06T21:48:32Z, after that
  change: **32 = 30 success, 2 skipped.** Docs-only main pushes were 31 before it, so the leg adds exactly 1.
- **The prediction, by that rule:** a `proxy/` PR was 34 = 31/3 (#579's finished run on `c63a9290677f`), so it is now
  35 = 32/3.

Merge by the rule.

## 7. The measurement's record

**The rulings on it, verbatim.**

> For: the UK data session, from the reviewer chat, 2026-11-06 ~22:54Z
>
> #579: approved at c63a9290677f, to merge under Q-M1. A failed map put now costs the next cold isolate a rebuild rather than the frame; MF1-MF3 and MF1s read as predicted. Its main run will carry #445's new leg, so predict that run from 1ce7a73a's main run before you merge.
>
> #586: approved at a1a476ec424b, to merge under Q-M1. With no overrunPx, windowFor refuses and returns exactly what it did before, so production's opuk is unchanged. The dev rows live only in the dev entry, and allowAny belongs to the sabotage alone. The dev envs bill like production against Daniel's $2, so keep each one's run bounded and state how long it ran in the M stage's record.

> For: the UK data session, from the reviewer chat, 2026-11-07 ~16:05Z
>
> #601 at e5e71f2f7f07 and #602 at bdbd49ff946a: approved once each one's CI double-reads 32 = 29/3/0. Merge under Q-M1.
> docs/wx-opuk-pkd: yes, its own PR the same way.
>
> 1. M's window: after the map server's combined staging slot closes, which itself follows blipscope-5c's "#590 read back". Daniel passes on that close. M then holds the lock for its whole run, about 3 hours, ending on `off`, with start and end announced through Daniel. The dev Worker stays on idle until then; if M is pushed past today, park it on `off` in the first free window.
> 2. M1: re-registered as you propose, before any M CPU is read. Upper bounds only, with no floor: opwe <= 616 ms, opsn <= 531 ms, opux <= 591 ms. M2-M6 stand. M1's old bands stay on record as the same miss as MD-5's.
> 3. S2: answered in the map server's #603. Only your 02:53:38Z dev deploy landed inside the slot, and nothing S2 read can depend on it through a binding, code or data.

**M1, as re-registered, before any M CPU was read.** Upper bounds only, with no floor: the Worker's `r2` median (261 ms) x
the product's ratio x 1.6.

| row | bound | expected |
|---|---|---|
| **M1 `opwe`** | median **<= 616 ms** | about 385 ms |
| **M1 `opsn`** | median **<= 531 ms** | about 332 ms |
| **M1 `opux`** | median **<= 591 ms** | about 370 ms |
| **M2 `opmt`** | median **<= 600 ms** | as frozen |
| **M3** | max, any build, **<= 6,000 ms** | as frozen |
| **M4** | **0 `exceededCpu`, 0 `exceededMemory`** | as frozen |
| **M5** | counts and medians by `map` | printed, as frozen |
| **M6** | the projection, with the stop at $2 | as frozen |

- **M1's old bands stay on record as the same miss as MD-5's:** `opwe` [1,487, 3,805], `opsn` [1,281, 3,280], `opux`
  [1,427, 3,654]. They were scaled from `opuk`'s 1,612.5 ms, which included rebuilding the map.
- **Reads are at ABR 1, by the ~18:22Z amendment.**

**How long each dev env ran, as ruled ~22:54Z:**

| env | deployed | parked | ran | ended by |
|---|---|---|---|---|
| `opwe` | 02:53:40Z | 03:55:40Z | **62 min** (cap 100) | its 12th counted `stored`, 03:54:17Z |

- **`idle`, the cron with nothing to do,** has run since about 02:42Z, and between products. It stays on until M's run, or
  until `off` in the first free window if M is pushed past today.
- **The lock:** M's first deploys ran without it confirmed (the ~15:55Z ruling records that). The rest runs in the window
  above, holds the lock for its whole run, and ends on `off`.

**`opwe`'s reading** (read 16:07Z, 900 s pieces, **ABR [1]**, after M1 was re-registered and pushed at 16:07:18Z).

| row | got | verdict |
|---|---|---|
| **M1 `opwe`** (the 12 counted builds, 03:00:46Z to 03:54:25Z, all after the last backfill at 02:56:12Z) | **median 314.5 ms**, max 622 ms, all `r2`. Between builds: n 43, median 8 ms | **as predicted** (<= 616 ms; expected about 385) |
| **M3** (the whole run, 62 records) | **max 2,391 ms**: the first invocation, `built` and a backfill together | as predicted (<= 6,000 ms) |
| **M4** (the whole run) | all 62 `ok`: **0 `exceededCpu`, 0 `exceededMemory`** | as predicted |
| **M5** (printed) | `built` 1, `r2` 13, `cache` 2 (the two backfills in the isolate that had just built or read the map). 0 `mapPutErrors`, 0 error lines | printed |

- **The memory watch at section 5 reads clean for `opwe`:** the `built` path holds `encodeMap`'s copy on top of the
  build, an estimated 106 MB against 128 MB, and that invocation ended `ok`.
- **`opwe`'s median against the expectation:** 314.5 ms is 0.82 of the 385 ms expected, the first sign of how the model
  scales. M6 refits it on all four.
