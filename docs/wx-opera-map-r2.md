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

## 3a. The readings before the push (2026-11-06)

- **`tsc --noEmit`:** clean.
- **vitest, the two files changed** (`test/wx-opera.test.ts`, `test/wx-ingest.test.ts`): **77 of 77 passed, exit 0.**
  - It started at 19:44:01Z, in the slot the reviewer granted ~19:43Z, after the start rules held: TIME_WAIT 14 and
    no other session's vitest.
  - Two earlier 20-minute windows (to 19:04:38Z and 19:25:18Z) found the machine busy and gave up, as the suite rule
    says.

| row | got | verdict |
|---|---|---|
| **MP1** | `built`, then `r2`, then `cache`; one object under `wx/opuk/map/` after the first | as predicted |
| **MP1s** (R2 level off) | the second cold build reads `built`; nothing stored | red, as predicted |
| **MP2** | the three frames byte-identical | as predicted |
| **MP2s** (one Int32 of `cellOf`: a no-coverage pixel, 496, moved into a covered echo-free cell, 145497) | through `r2`, the frame **differs** | red, as predicted |
| **MP3** (an object written for a window one column narrower, under the right key and metadata) | `built` with `mapRefused: true`, the frame identical to MP2's; the next cold build reads `r2`, so the bad object was replaced | as predicted |
| **MP3s** (validation skipped) | the wrong map is used: the frame **differs** (through `r2`) | red, as predicted |
| **MP4** | **17,716,016 B = 16 + 4 x (2,224,675 window pixels + 2,204,325 cells)** | as predicted |
| **MP5** | `opuk`'s storing check carries `map`; `cref`'s carries neither `map` nor `mapRefused` | as predicted |

- The existing K-tests, PK1 among them, still pass through the new path.

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

## 6. The deploy to production `opuk` (registered before it runs)

**The ruling, 2026-11-06 ~20:01Z, verbatim:**

> For: UK data session, from the reviewer chat, 2026-11-06 ~20:01Z
>
> #577 (stage 1c) -> approved to merge at 3f6b81056aa7; CI 34 = 31/3/0 as predicted. Merge by the rule (Q-M1).
> - My read: mapVia takes the isolate cache, then R2 only if decodeMap validates the magic, the three counts and the byte length and the metadata holds the exact key; otherwise it builds. The put is create-only, and overwrites only after a refusal. Predictions frozen first (a2d9e8a); 77 of 77 in the granted slot; MP1-MP5 and the three sabotages as predicted.
> - Follow-up, not blocking, in its own small PR: a failed map put should not fail the storing check. Catch it, count it on the line, and keep the frame.
> - The deploy: after the PKD set (through PKD-5, ~20:48Z) has read and been reported, with its rows registered first and the rollback target recorded. State in the rows whether the bucket's expire-1d rule reaches wx/<product>/map/; if it does, each map is rebuilt once a day. M6 then measures the cost; 1b only if M6 still reads over $2/month.

**#577 merged** at 20:02:15Z as `51757039ed01`, by the rule.
- Main's head H was `38e5bb482961` (#576), with its CI finished: 34 = 32 success, 2 skipped.
- The merge command re-read `origin/main`, found it still H, and merged with `--match-head-commit 3f6b81056aa7`.
- The squash tree equals `merge-tree(H, 3f6b81056aa7)` (`59e8b89497dc`).
- **Main CI on `51757039`, predicted 34 = 32 success, 2 skipped, 0 failed.** The baseline is `36d0b657`'s main run
  (#573's merge, a `proxy/` main push, finished at 34 = 32/2).

**When:** after the PKD set (stage 1a, through PKD-5, about 20:48Z) has read and been reported, and after this
merge's main CI reads as predicted. One deployer; only the `opuk` Worker; the `cref` Worker and the proxy untouched.

**Does `expire-1d` reach `wx/<product>/map/`? Yes.**
- `wrangler r2 bucket lifecycle list blipscope-wx` (read 2026-11-06 ~20:03Z) shows two rules. `expire-1d`:
  enabled, **prefix "(all prefixes)"**, "Expire objects after 1 days". The default multipart abort rule (7 days).
- So **each map object expires about a day after it is written,** and the next cold isolate rebuilds it and writes
  it again. That is about one `built` and one PutObject a day for each product, plus any cold isolates that race
  before the first put lands.

| row | what | predicted |
|---|---|---|
| **MD-0** | before the deploy: the rollback target, and the map prefix | `wrangler deployments status`: **`b7626571-e7dd-4f62-9afa-80f4e0e73d5b` at 100%** (stage 1a). `r3_list.py wx/opuk/map/`: **0 objects** (0 at 20:04:22Z, re-read just before). The deploy runs from a clean detached checkout of `51757039`, top level |
| **MD-1** | the version record | bindings exactly `WX` -> blipscope-wx and `WX_INGEST` = "on"; `cpu_ms` 12000 |
| **MD-2** | at the first firing + 10 min | `r3_list.py wx/opuk/map/`: **exactly 1 object of 17,716,016 B**. That is MP4's size: OPERA's geometry is fixed (K1; identical across all 24 survey frames), and `OPUK_GRID` is unchanged. At the same read, /healthz and LHR as PKD-3: `frameAgeS` <= 1,800, `coveredShare` in [0.70, 0.80]; ETag `opuk`, `X-Wx-Max-Age` 720, frame = `latest` |
| **MD-3** | every storing check in the first 70 min (Logs, 900 s pieces, ABR 1) | **`pruneKeys` 900**; a **`map` field** of `built`, `r2` or `cache`; **no `mapRefused`**; **0 error lines**. **The first storing check after the deploy reads `built`**: a new version starts in new isolates, and the bucket holds no map. At least one `built` in all; the counts by `map` are printed |
| **MD-4** | R3′-5b′'s rule at +70 min, twice, 10 min apart | <= 13 frames; none below the last storing start - 3,600 s except `latest`'s; the +300 s sabotage fails on the oldest |
| **MD-5** | CPU over 2 h (900 s pieces, ABR 1) | single-build median in **[1,008, 2,580] ms**; max <= 5,000 ms; **0 `exceededCpu`**. **The split by `map` is printed, not graded:** n, median and max for `built`, `r2` and `cache`. That is M5's reading for `opuk` in production |
| **MD-6** | the daily rebuild | printed at the 7-day cost re-read (about 2026-11-13): `built` lines a day from Logs |
| escape | anything outside these rows | **roll `opuk` back to `b7626571` at once**, stop and report |

- **The ~18:22Z amendment applies:** a sampled Logs read is re-read once over a narrower window; a second sampled
  read is BLIND, which fails the row and triggers the rollback.
- **Tools** on docs/wx-opuk-r3: `r3p.py` counts storing checks by `map` (`map_on`) and splits single-build CPU by
  `map` (`de32c2ff`). `r3_list.py` prints non-frame objects' sizes (`b984a087`); its control lists `wx/opuk/` as 12
  frames plus `latest` at 212 B.

**The record.** Main CI on `51757039` read **34 = 32 success, 2 skipped, 0 failed**, as predicted (20:15:56Z and
20:16:02Z). The PKD set was reported at about 20:48Z. Started at about 21:25Z, on Daniel's "resume".
- **The deploy:** `npx wrangler deploy -c wx-ingest-opuk.wrangler.toml --env=""` from `wt-1cd`, detached at
  `51757039`, clean. Start 21:24:59.173Z, **end 21:25:06.701Z (T), exit 0**, stderr empty.
  - Main had moved on to `b5c9c6e9f976` by then. The registered commit was deployed, not main's head.
  - **Version `a55d1557-f8df-48d5-9df4-c00112276eac`.**

| row | got | verdict |
|---|---|---|
| **MD-0** (21:24:36-21:24:40Z) | `b7626571-e7dd-4f62-9afa-80f4e0e73d5b` at 100%; `wx/opuk/map/` **0 objects** | as predicted |
| **MD-1** (`versions view --json`) | `a55d1557`: bindings exactly `WX` -> blipscope-wx and `WX_INGEST` = "on"; `cpu_ms` 12000 | as predicted |
| **first firing** | scheduledTime **21:25:14Z**, 8.9 s after T | printed |
| **MD-2** (first firing + 10 min, 21:35:19Z) | `wx/opuk/map/`: **1 object, 17,716,016 B**. `wx.opuk` `frameAgeS` **320**, `coveredShare` **0.7508**. LHR, on `/wx/cref` and `/wx/opuk`: ETag **`opuk`**, `X-Wx-Max-Age` **720**, `X-Wx-Frame` 1794000600 (21:30:00Z), the frame `/healthz` names at 21:35:26Z (326 s). Controls: Bend and KMCI `cref`, no max-age | as predicted |
| early look at MD-3 (T to 21:34:14Z, read 21:35:26Z, 900 s pieces, ABR [1]) | 18 `opuk` lines. The **first storing check, 21:29:32Z: `stored`, `map` `built`, `pruneKeys` 900**, no `pruned`, no `mapRefused`; already-stored 17; **0 error lines** | as MD-3 predicts; MD-3 itself is read at T+72 min |
