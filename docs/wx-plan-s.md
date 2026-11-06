# Plan S: a staging copy of the weather ingest

**For:** the reviewer and whoever runs Plan S. **Status:** this PR adds a staging copy of the ingest and the checks
that keep its test code out of production. **Nothing deploys from this PR.** S0 needs the reviewer's review and merge,
the bucket (`blipscope-wx-dev`, created 2026-11-05 00:47:51Z on Daniel's word) and a slot from the reviewer. S1-S5
redeploy only the dev Worker.

The record of why this exists is `docs/wx-opuk.md` on `docs/wx-opuk-r3`:
- R3 rolled the opuk ingest back on 2026-11-05 at 00:18:33Z.
- Three invocations ran 5.4-6.1 s of CPU under `cpu_ms = 5000` and ended `ok`, which is Cloudflare's documented
  per-isolate flexibility.
- Every one of them built a `cref` frame and an `opuk` frame in the same invocation.
- Plan S (approved ~00:55Z) answers two questions:
  - can R3-6's "0 CPU-limit errors" gate see a CPU-limit outcome at all? That is the positive control.
  - what does an `opuk` build cost on Cloudflare's runtime, alone? That is the shape either fix would run.

## 1. What the PR adds

- **`proxy/wx-ingest-dev.wrangler.toml`** for the Worker `blipscope-wx-ingest-dev`.
  - The name is set in every env block, so it's one Worker and never the production name.
  - `workers_dev = false`, no routes.
  - Its only binding is `WX` -> **`blipscope-wx-dev`**, repeated in every env block because bindings don't inherit.
  - Observability on, `head_sampling_rate = 1`.
  - One env per phase: `off`, `plantcal`, `plant`, `cref`, `opuk`. The top level is `off` too, so a deploy without
    `-e` is inert.

  | env | `MODE` | `cpu_ms` | cron |
  |---|---|---|---|
  | top level, `off` | `off` | 30000 | none |
  | `plantcal` | `plant` | 30000 | every minute |
  | `plant` | `plant` | **100** | every minute |
  | `cref` | `cref` | 30000 | every minute |
  | `opuk` | `opuk` | 30000 | every minute |

- **`proxy/src/wx/ingest-dev-worker.ts`,** the dev entry. Production's entry never imports it.
  - It keeps production's shape: a `:00` check, then a `:30` check in the same invocation.
  - Each check runs by `MODE`:
    - **`plant`:** a synthetic integer busy loop of a fixed count, `PLANT_ITER`. It touches no bucket and no upstream,
      and logs one line whose `evt` is `PLANT_MARKER`. The loop is bounded by count, because `Date.now()` doesn't
      advance during CPU in workerd.
    - **`cref` / `opuk`:** production's `checkAll` over that one product.
    - **`off`:** one `wx_dev_off` line.
    - **anything else:** one `wx_dev_mode_error` line, and nothing else.
- **`checkAll`** (production's entry) gains an optional product list, defaulting to `PRODUCTS`. Production's own
  call is unchanged.
- **`proxy/scripts/check-wx-plant.mjs`,** a bundle check run in CI, with its own `--selftest`.
- **`proxy/test/wx-ingest-dev.test.ts`:** 6 tests.

## 2. The PR's checks, predicted before the code

| id | the check | predicted | sabotage, and what it must do |
|---|---|---|---|
| **PS1** | production's `checkAll`, **called with no product list**, against a fetch that answers 404 to everything | exactly **2** log lines, products exactly `{cref, opuk}` (whole set compared) | **PS1s:** the default narrowed to `PRODUCTS.slice(0, 1)` (the ruling's addition 2). Run on the new test file: **exactly 1 failed (PS1), 5 passed** |
| **PS2** | `checkAll` with `[opuk]`, then with `[cref]` | 1 line each, only that product | **PS2s:** `checkAll` iterates `PRODUCTS` and ignores the list. **Exactly 2 failed (PS2, PS6), 4 passed** |
| **PS3** | the dev entry, `MODE = plant`, with a bucket that throws on any access and a fetch that throws | resolves; exactly **1** line, `evt` = `PLANT_MARKER`, `result` = `plant(iter)` computed separately; two runs give the same result | **PS3s:** the plant branch also calls `env.WX.head(...)`. **Exactly 1 failed (PS3), 5 passed** |
| **PS4** | `MODE = off`, and `MODE` absent | exactly 1 line, `wx_dev_off`; bucket and fetch untouched | **PS45s:** the fall-through branch runs production's `checkAll` for every product instead of logging. **Exactly 2 failed (PS4, PS5), 4 passed** |
| **PS5** | `MODE = bogus` | exactly 1 line, `wx_dev_mode_error`, `mode` = `bogus`; untouched | PS45s |
| **PS6** | `MODE = cref`, then `MODE = opuk` (404 fetch) | 1 line each, only that product | PS2s |
| **PB1** | **the plant never reaches production** (the ruling's addition 1): `wrangler deploy --dry-run --outdir` from `wx-ingest.wrangler.toml`, every emitted `.js` file searched for `PLANT_MARKER`'s value | **0** in production's bundle. **Anchor:** production's bundle holds `wx_ingest_error` (at least 1), or the read is BLIND. **Control:** the dev bundle (`-e plant`) holds the marker **exactly 1** time, or the check is BLIND | **PB1s** (in `--selftest`, in CI): a temporary copy of production's entry that also re-exports from the dev entry, bundled with production's config, must read **FAIL (1)**. The copy must differ from the original, or the selftest fails; it is deleted afterwards |
| **PB2** | the dev copy's bindings, from each env's dry-run output | each of the 6 configs (top level and 5 envs): exactly **one** R2 binding, `WX` -> **`blipscope-wx-dev`**, and `MODE` as in the table. **Control:** production's dry-run reads `WX` -> `blipscope-wx`, or the parse is BLIND | **PB2s** (in `--selftest`): a temporary copy of the dev toml with the `cref` env bound to `blipscope-wx` must read **FAIL** on `cref` and pass the other 5 |

**Exit codes for `check-wx-plant.mjs`:** 0 PASS, 1 FAIL, 2 BLIND. CI fails on anything but 0. The selftest exits 0 only
when the real check passes and both sabotages fail exactly as above.

**The suite and CI**, read only from finished runs:
- **Proxy suite:** main's at `20a0323` reads 891 passed and 1 skipped (892), over 67 passed and 1 skipped files. This
  PR predicts **897 passed and 1 skipped (898)**, over **68 passed and 1 skipped files (69)**. If main moves before
  the PR's run, the prediction is the base's counts **+6 tests, +1 file**, read from the base's own run.
- **Dashboard:** unchanged.
- **The PR head's check-runs:** **41**: 38 success, 3 skipped. Same workflows as #521 and #522: device-ids,
  firmware, no-traffic-captures, workers.
- **Typecheck** (`src/` and `test/`) and **typecheck:scripts:** green.

**Every sabotage is planted, run on the new test file alone, and restored by bytes with a sha check** (the plant must
change the file, and the failure count must move, or the sabotage did not apply).

**The escape:** anything else means stop and report. An off reading waits for a ruling before any fix is pushed.

## 3. Plan S's phases (as approved ~00:55Z; frozen with the plan)

| phase | env | runs | as predicted | escape |
|---|---|---|---|---|
| **S0** | `off` | deployed inert, no cron | the version record shows exactly one binding, `WX` -> `blipscope-wx-dev`, and `cpu_ms` 30000; the deploy output shows no schedule | anything else: stop before arming |
| **S1** | `plantcal` | 3 invocations | 3 records, all `ok`, each within ±25% of their median `C` | `C` < 500 ms: stop (S2's overrun would be under 5x) |
| **S2** | `plant` | 10 invocations, one a minute | see S2's table | |
| **S3** | `cref` | 60 invocations | building invocations median in **[900, 1,700] ms**, max ≤ 4,000; the rest median ≤ 100 ms | outside: stop and decide |
| **S4** | `opuk` | until **N = 12** `stored` checks, counted after the backfill (the first `stored` after the last `backfilled`); window capped at 120 min | building invocations median in **[1,500, 3,000] ms**, max ≤ 4,000; the rest median ≤ 100 ms | outside: stop and decide. Fewer than 12 by 120 min: report the count with CloudFerro's errors; fewer than 6 is BLIND |
| **S5** | `off` | no cron | the deploy output shows no schedule; R3-6's instrument shows **0** invocations of the dev Worker over 10 min, read twice 5 min apart | anything else: stop. Deleting the Worker and the bucket is Daniel's |

**S2's verdict table:**

| result | means |
|---|---|
| at least 1 of the 10 is `exceededCpu` in the Logs invocation record **and** R3-6's instrument (`workersInvocationsAdaptive` by status) shows a non-success status in the window | **the CPU-limit gate is LIVE**. The count is printed |
| at least 9 records, all `ok` / `success` | **the gate is BLIND**: R3-6's "0 CPU-limit errors" proved nothing |
| anything else (fewer than 9 records, the two instruments disagree, an outcome outside `ok` / `exceededCpu` / `exceededResources`) | the instrument is unreliable at this scale: **stop and decide** |

**Instruments:** R3-6's own (`workersInvocationsAdaptive`, per status and per minute, for the dev script's name), and the
Workers Logs invocation records (`$workers.outcome`, `cpuTimeMs`, `wallTimeMs`), joined per invocation by
`tools/wx/r3_minute_join.py` (on `docs/wx-opuk-r3`).

## 4. Local readings, before the push (CI is the authority)

Predictions frozen at `22707c5` (00:51:39Z), before any code.

- **Typecheck** and **typecheck:scripts:** green.
- **`test/wx-ingest-dev.test.ts`:** 6 passed.
- **The test sabotages** (`proxy/scripts/sabotage-wx-plan-s.py`): **all as predicted.** Each file was restored by
  bytes, with its sha256 matching the original.

  | sabotage | failed |
  |---|---|
  | PS1s | PS1 |
  | PS2s | PS2, PS6 |
  | PS3s | PS3 |
  | PS45s | PS4, PS5 |

- **`check-wx-plant.mjs`:**
  - **PB1 PASS:** production's bundle holds the marker 0 times, with anchor 1 and 1 in the dev bundle.
  - **PB2 PASS:** all 6 dev targets bind `WX` -> `blipscope-wx-dev`, each with its `MODE`.
  - **`--selftest` PASSED:** PB1s reads FAIL with the marker found once; PB2s reads FAIL on `cref` alone.
  - **With no Cloudflare login at all** (an empty home, no token): PASS. CI has no credentials for this step and
    needs none.
- **Found while building, by the selftest, before any push:**
  - The first draft named `PLANT_MARKER` only in `devCheck`'s log line.
  - PB1s then read **PASS**. Its bundle held the loop's constants twice and the marker 0 times: esbuild kept
    `plant()` and dropped everything that named the marker.
  - So a production import of the loop alone would have passed PB1.
  - **The fix:** `plant()`'s own guard throws `PLANT_MARKER`, so the marker travels with the loop. PB1s then read
    FAIL as predicted.
  - The prediction didn't change; the check was made able to see what it was predicted to see.
- **The full proxy suite, locally: 1 failed, 896 passed, 1 skipped (898).** The total is the frozen +6.
  - The one failure is `photolicense.test.ts` Q-C1: the committed `credits.html` against its render.
  - **The alternative, that this PR broke it, is excluded:**
    - the PR touches neither of Q-C1's inputs (`proxy/photos/`, `photolicense.ts`: an empty diff);
    - this worktree checks `credits.html` out with CRLF endings, all 291 of them (`git ls-files --eol`:
      `i/lf w/crlf`), and the render emits LF;
    - main's CI, on an LF checkout, reads 891 passed.
  - The reading that counts is the PR's CI: **897 passed, 1 skipped (898).**
- **The CI step** is the one `--selftest`. It runs the real check, which must PASS, then both sabotages.

## 5. The `idle` park env (ruled ~05:05Z 2026-11-05; predictions first, before the code)

**Why.** Parking on `off` removes the cron and every phase adds it back. Cloudflare's docs give a cron trigger change
"up to 15 minutes" to propagate, and the dev copy measured it:
- adding the cron: 268.7 s and about 592 s to the first firing;
- removing it: firings for 323 s afterwards.

A version change is immediate. So **a park now keeps the cron and changes only the version.**

**What this PR adds:**
- **`[env.idle]`** in `proxy/wx-ingest-dev.wrangler.toml`: the same Worker name, `crons = ["* * * * *"]`,
  `MODE = "off"`, `cpu_ms` 30000, `WX` -> `blipscope-wx-dev` only, observability on.
- **`check-wx-plant.mjs`'s PB2** reads **7** deploy targets: the top level, `off`, `idle`, `plantcal`, `plant`,
  `cref`, `opuk`. The new target expects `MODE` `off`.
- Nothing in `src/` or `test/` changes.

**Predicted, read only from finished runs:**

| check | predicted |
|---|---|
| proxy suite | unchanged from `131c0f776d9a`: **897 passed, 1 skipped (898)**, files 68 passed, 1 skipped (69) |
| the plant selftest (CI step) | **SELFTEST PASSED**. Real PB1 PASS (production 0, anchor 1, dev 1). Real PB2 PASS on **7/7**, `idle`'s MODE `"off"`. PB1s FAIL (marker 1). PB2s FAIL on **`cref` alone, the other 6 passing** |
| check-runs on the head | **41**: 38 success, 3 skipped |

**Predicted at each `idle` park, when it runs:**
- the deploy lists `schedule: * * * * *`;
- the version record shows exactly one R2 binding, `WX` -> `blipscope-wx-dev`, `MODE` = `off`, no other binding,
  and `cpu_ms` 30000;
- invocations on a park version log only `wx_dev_off`.

**The first-invocation windows, as ruled ~05:05Z:**
- the one deploy that adds the cron (S1's fourth attempt) has **15 min**;
- every later phase deploy has **5 min**, judged on `scheduledTime` at the deploy + 360 s.
- **S5 (`off`, removing the cron):** the last invocation within **15 min** of S5's deploy, then **0 over the next
  10 min**, read twice 5 min apart.

**Escape:** anything else means stop and report.
