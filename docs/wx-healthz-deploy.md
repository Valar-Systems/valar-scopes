# /healthz every product: the deploy, staging first, then production (predictions first, 2026-11-12)

**For:** the reviewer (the orchestrator), and weatherscope, whose W5 Worker code ships in the same deploy (its rows: `022ede928170`, §S4.19).
**Status:** predictions, frozen before the slot. Nothing has deployed. The slot is the orchestrator's, after v19's step
4' closes.

## The rulings (the orchestrator), verbatim

2026-11-11 ~23:24Z, who deploys:

> WHO: you, under the claim proxy:production, from a clean checkout of main, P0 first, then deploy.sh, then P1-P4 as
> registered.

2026-11-12 00:43:32Z, the shape of the slot:

> when step 4' closes and I grant #643's slot, that deploy also carries #649's W5 Worker code (now on main). Ruled with
> weatherscope:
> - STAGING FIRST in the same slot, every row read there, then production. A staging off-reading stops before
>   production.
> - Your record cites weatherscope's W5 rows (its sha is coming): breakers, 401/404/405 on the W5 routes, and
>   staging-only leaderboard-submit rows for the changed shared request path.
> - Your P0-P4 as registered.
> - Carried-commit list re-read at slot time: #643, #649, #639 (tree-shaken), and anything else beyond 68dc169a.

## 1. What each deploy carries (drafted 00:50Z on main `af871ecb`, re-read at slot time)

Both environments deploy the same commit, main at slot time. They start from different places.
- **Production** runs `68dc169a` (`/healthz`, read 00:50Z).
- **Staging** runs `fdf62ada`, an ancestor of main from 2026-11-07. So staging's deploy carries more.

| commit | what | `proxy/` source in the bundle? | production carries | staging carries |
|---|---|---|---|---|
| `af871ecb` (#649) | W5: `/api/v1/weatherscope/{alerts,conditions,forecast}` | yes | yes | yes |
| `aa9eb5e5` (#658) | the `index.ts` comment and this PR's docs | a comment only | yes | yes |
| `ed452654` (#646) | `/healthz`'s commit read, in scripts and docs | no (scripts) | yes | yes |
| `03632992` (#643) | `/healthz` reports every product | yes | yes | yes |
| `c2cf7cfc` (#639) | basemap `build.ts`, `readInOrder()` | tree-shaken out (an esbuild bundle of `src/index.ts`: 0 hits; its controls hit 1 and 3) | yes | yes |
| `d46c25c4` (#627), `37c3bcfb` (#626), `dbf8cb74` (#623), `e0983b3b`, `e89e7a95` (#618) | already in production | some | no | **yes** |

At slot time the list is re-read with `git log <running>..origin/main -- proxy/` for each environment. Anything not
in the table is reported before the deploy.

## 2. The order, in one slot

| step | what | predicted |
|---|---|---|
| **S0** | staging's rollback target: `npx wrangler deployments status --env staging` (read-only), **and** staging's `/healthz` commit through `healthz-commit.sh` | one version at 100 %, recorded **in full, by version id**, with `/healthz` reading `fdf62ada`. Staging takes ten commits at once, so its rollback target is a version, never a guess |
| **S-slot** (in the SAME step as the claim) | take `proxy:staging`; then, before `deploy.sh` runs, re-read and paste into this record: `python scripts/session-claim.py show` (the deployer lock); main's sha; staging's carried list `git log <S0's commit>..origin/main -- proxy/`; production's carried list `git log 68dc169a..origin/main -- proxy/` | the lock shows this session holding `proxy:staging`. The lists match §1's, or every difference is named before the deploy. Anything unexplained: stop |
| **S-dep** | `proxy/scripts/deploy.sh staging` from a clean checkout of main. `deploy.sh`'s own confirmation reads staging's `/healthz` commit through `healthz-commit.sh` | the confirmation reads main's sha. Release the claim after the S rows |
| **S1** (P1 on staging) | `https://scopes-staging.valarsystems.com/healthz` | `wx` lists the 11 in `PRODUCTS`' order. `cref` and `opuk` read `routed: true`, the nine `routed: false` |
| **S2** (P2 on staging) | the same body | every product has a non-null `frameAgeS` under its `maxAgeS`, and `stale: false`. Staging's `WX` is bound to `blipscope-wx`, production's bucket (`wrangler.toml`), so it reads production's frames |
| **S3** (P3 on staging) | `BASE=https://scopes-staging.valarsystems.com proxy/scripts/smoke-prod.sh` (`test-key-staging`, present) | passes. The per-product section prints 11 lines, no `WARN`, and `FAIL: 0` |
| **S4** (P4 on staging) | `BASE=… tools/wx/r12_prod.sh` at the 40 label points and the 9 R2 points | as R1-P and R2-P read on production: 5 isles points and 9 at `opuk` with 720; 35 at `cref`, byte 32 `0xE0`. This is staging's first run of the region dispatch (#623) |
| **W5 on staging** | weatherscope's rows, frozen at **`022ede928170`** (`weatherscope/stage4-plan`, `docs/weatherscope-stage4.md` §S4.19). **C1-C5** (C1: `/healthz`'s `weatherscope` breakers; C2: 401s with no key; C3: 404 on a Blipscope suffix; C4: 405 on the W5 leaderboard POST; C5: the Blipscope smoke rows unchanged), then **W5-S1 to W5-S3**, staging only (that doc calls them S1-S3): empty-body leaderboard POSTs refused before any write, 400, 400 and 405 | exactly as §S4.19 predicts them |
| **gate** | every S row and W5 row read as predicted | otherwise **stop before production**. Roll staging back to S0 if the off row is a defect, and report |
| **P0** | production's rollback target: `npx wrangler deployments status --env production` (read-only), **and** production's `/healthz` commit | one version at 100 %, recorded **in full, by version id**, with `/healthz` reading `68dc169a` |
| **P-slot** (in the SAME step as the claim) | take `proxy:production`; re-read and paste: `session-claim.py show`, main's sha, and production's carried list `git log 68dc169a..origin/main -- proxy/` | the lock shows this session holding `proxy:production`; main is the sha staging was deployed from; the list is S-slot's production list |
| **P-dep** | `deploy.sh production` | the confirmation reads main's sha. Release the claim after the P rows |
| **P1-P4** | as registered in `docs/wx-healthz-products.md` §5 | P1: the 11, routed as above. P2: all fresh, `stale: false`. P3: smoke passes, 11 lines, no `WARN`. P4: `r12_prod.sh`, as R1-P and R2-P |
| **W5 on production** | §S4.19's **C1-C5** at `022ede928170` (W5-S1 to W5-S3 are staging only) | exactly as §S4.19 predicts them |

**W5's `/healthz` field and my rows.** C1 adds a `weatherscope` object with a `breakers` array to `/healthz`. Smoke's
per-product section cuts only `"wx":{...}`, so it is unaffected (H6). `healthz-commit.sh` reads the top-level stamp
only. Smoke's greedy `"upstreams":\[.*\]` cut now runs to the breakers' `]`, but its `adsb_fi` and `airplanes_live`
greps take the first match, and the breakers' ids are `nws_*`. So no smoke row moves, consistent with W5's C5.

**If S2 or P2 reads a product stale,** the rule is `docs/wx-healthz-products.md` §5's: OPERA pauses, so re-read once
10 minutes later. If it is stale twice, read that Worker's Logs:
- if the Logs agree with the age, it is a finding about the ingest, not this deploy;
- if they disagree, the deploy is wrong, and it rolls back.

**Escape:** anything outside a row stops, is reported, and rolls back the environment it ran on, to S0 or P0. The
rollback goes through `wrangler rollback` under the same claim, with its line in `rollbacks.log`, announced. A rollback
is never refused.

## 3. CI for this PR (predicted before the push)

It changes this doc only. **32 check runs: 29 success, 3 skipped, 0 failed.** The baseline is #641's head run, docs
and `tools/wx/`: 32 = 29/3/0. #649's new `workers.yml` path filters (`tools/weatherscope/nws/**`,
`test/fixtures/wx/nws/**`) are not touched here.
