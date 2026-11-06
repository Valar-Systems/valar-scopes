# Stage 1c: the resample map from R2 (predictions first, 2026-11-06)

**For:** the reviewer chat. **Status:** predictions, frozen before any code.
- This is stage 1c of `docs/wx-opera-europe.md`, ruled ~17:08Z (Q-COST (b)) and ordered ~18:22Z: "1c's predictions
  on its own branch, then measure with M6".
- **Nothing here deploys to production.** The dev-Worker measurement (M1-M6, §4 of the plan) comes after it.

## 1. What changes

**Today:**
- `buildOperaFrame` takes its resample map from `mapFor(geo, window, grid)`, a per-isolate cache.
- A cold isolate builds the map: `buildMap` runs an LAEA inverse for every window pixel and a forward for every
  cell no pixel centre falls in.
- K11 measured, in Node, **the map at 0.547-0.594 s against 0.249-0.407 s for the frame itself.** So in a cold
  isolate the map is the larger part of the build.
- How often production isolates are cold is not known. The plan's M5 counts it.

**New: a second level, in R2, behind the isolate cache.**
- **The key:** `wx/<product>/map/<h>.wxm`, where `<h>` is the first 32 hex digits of the sha256 of the key JSON the
  isolate cache already uses: `[proj, ulx, uly, xs, ys, window, grid]`.
- **On an isolate miss:**
  1. GET the object.
  2. If it validates, use it: **`map: "r2"`**.
  3. Otherwise build the map as today and PUT it create-only (`If-None-Match: *`): **`map: "built"`**.
- **On an isolate hit:** **`map: "cache"`**.
- **The ingest line** (`wx_ingest`, OPERA products only) carries `map`, and `mapRefused: true` when an object was
  found and refused.
- **The object (`WXM1`), little-endian:**
  - a 16-byte header: magic `WXM1`, then u32 `rows`, u32 `cols`, u32 `cells`;
  - then `cellOf` (`rows x cols` Int32) and `fill` (`cells` Int32);
  - `customMetadata.key` holds the key JSON.
- **It validates only if** the magic, all three counts (against the window and grid now in hand), the byte length
  (`16 + 4 x (rows x cols + cells)`) and the key JSON all match. Anything else is refused and the map is rebuilt.
- **Its life:** the bucket's `expire-1d` removes it after a day, and the next cold build writes it again. That is
  at most one PutObject (Class A) a product a day, plus one GET (Class B) for each cold isolate.
- **Memory:** the arrays are views over the GET's buffer, with no copy. The peak is no higher than building the map.
- **Nothing else moves.** Frame bytes, `latest`, the prune and `cref` are unchanged.

## 2. The checks (vitest, `proxy/test/wx-opera.test.ts`, on the real fixture frames)

| check | what | predicted | sabotage | predicted under it |
|---|---|---|---|---|
| **MP1** the three levels | three builds of the DRY frame on an empty bucket: isolate cache cleared before the first and the second, kept for the third | **`built`, then `r2`, then `cache`**; after the first, the object exists under its key | **MP1s:** the R2 level switched off (`IngestX.mapR2: "off"`, test-only) | **red:** the second build reads `built` |
| **MP2** same frame | the WXG1 bytes built through `r2` and through `built`, compared whole | **identical**; and the existing K-tests, which now build through the new path, still pass unchanged | **MP2s:** one Int32 of the stored `cellOf` altered; counts, length and key unchanged | **red:** the frame through `r2` differs |
| **MP3** refusal | an object under the right key, but written for a different window, so its counts differ | **refused and rebuilt:** `map: "built"`, `mapRefused: true`; the frame equals MP2's | **MP3s:** validation skipped (`IngestX.mapNoCheck`, test-only) | **red:** the wrong map is used, and the frame differs or the build throws |
| **MP4** the size | the stored object's length | **exactly `16 + 4 x (window pixels + cells)`**, with the window from `windowFor` printed; for `opuk` about 17.7 MB | | |
| **MP5** the line | an OPERA storing check's `wx_ingest` result | carries `map`; `cref`'s carries none | | |

## 3. What it is expected to do to CPU (measured at M, not graded here)

- On a warm isolate: nothing. On a cold one, the map's LAEA work is replaced by a 17.7 MB GET, which is mostly I/O.
- **M5 (frozen in §4 of the plan) counts builds by `map`,** with their medians apart. That is what turns K11's split
  into the Worker's.
- If most production builds are cold, CPU per build could fall by about half. If most are warm, by little. §4's
  "about $1.8 total" depends on which, and is unproven until M6.

## 4. CI and the merge

- **Predicted: 34 = 31 success, 3 skipped, 0 failed.** It touches `proxy/`.
- **Baseline:** #573's PR run on `3594321d3bcc`, which touches `proxy/`, finished at 34 = 31/3/0. It postdates the
  last job-set change `84409651` (#552) and the last workflow change `259e8688` (a step). Re-read at the push.
- **Merge by the rule.**

## 5. After it

**The M stage, its own PR:**
- dev product rows for `opwe`, `opsn`, the grown isles grid and `opmt` (§3 of the plan);
- their envs on `blipscope-wx-ingest-dev`, writing to `blipscope-wx-dev`;
- the measurement M1-M6 as frozen, run on the dev Worker.

**Stage 1b** is built only if M6 still reads over $2 a month, as ruled.
