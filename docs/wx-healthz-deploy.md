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

## 4. RE-REGISTERED before the claim (2026-11-12 ~04:10Z): OPERA is silent

**What was read before the deploy** (~04:06-04:08Z, while S-slot was being prepared), reported, and ruled on by the
orchestrator (~04:08Z: option (a), plus the recovery branch):
- **OPERA has published nothing since 2026-11-11T20:10:00Z** (epoch 1794427800). Every OPERA product's R2 `latest`
  reads exactly that epoch. Production's `/healthz` read `opuk` at 28,609 s against its 1,800 s `maxAgeS`, with `cref`
  at 136 s.
- **The ingests are healthy.** `blipscope-wx-ingest-opuk` and `-opmt` each ran 30 checks in 03:52-04:07Z, each
  `no-frame`, with 0 errors, at ABR 1. It is an upstream silence, not this deploy.
- **No customer is affected,** per the orchestrator from the artifacts: v18's `platformio.ini` has no
  `WX_LAYER_SHIPS`, so no released firmware draws `/wx`.

**So, frozen now, before the claim:**
- **S2/P2:** `cref` fresh (`frameAgeS` < 600, `stale: false`). The ten OPERA products read `stale: true` with
  `frameAgeS` > 1,800. `routed` as before.
- **S4/P4:** the 35 `cref` points as originally frozen. The 14 `opuk` points (5 isles, 9 R2) answer **503 wx_stale**.
- **The recovery branch:** at each S2/P2/S4/P4 read, every OPERA product's R2 `latest` epoch is read at the same time.
  A product whose `latest` is newer than 1794427800 is graded against its ORIGINAL frozen prediction, which is fresh,
  and for `opuk`'s points 200 `opuk` 720. Both outcomes are tied to a read made at row time.
- **Everything else is unchanged.** This is a re-freeze on an environment fact read before the deploy, not on any
  deploy outcome.

**The graders** (scratch, moved into the record with the readings):
- `hz_rows.py` runs S1/P1, S2/P2 (with the branch) and W5's C1. It was controlled before the deploy: on staging's
  pre-deploy shape, S1, S2 and C1 read OFF.
- `w5_rows.sh` runs C2-C4 and W5-S1 to W5-S3. Controlled the same way: C2 x3 and C3 read OFF on staging before W5;
  W5-S1 and W5-S2 read 400.
  - **For weatherscope:** C4 and W5-S3 already read 405 before W5, so neither can tell pre-W5 from post-W5.
- `opera_latest.py` supplies the branch's epochs.
- `s4_grade.py` grades `r12_prod.sh`'s lines under the re-registration. Controlled both ways: the silent-state set
  passed; one planted 200 under silence read OFF.

**S3/P3, registered after staging's deploy (04:12:56Z) and BEFORE smoke was read on staging.** My re-registration above
missed this row. S3/P3's frozen text says "no `WARN`, since every product is fresh (P2)", and that premise is the one
the re-registration changed. By H6's committed rule (a stale product's line starts with `WARN`), S3/P3 now read:
smoke passes with `FAIL: 0`; the per-product section prints 11 lines; **`WARN` on exactly the products S2/P2 grades
`stale: true`** at that read (the ten OPERA products, unless the recovery branch moved any of them); the `wx-opuk`
verdict PASS (it does not grade age). This is derived mechanically from S2/P2's branch, not from any reading.

## 5. Staging's S3 read off, rolled back; the /basemap diagnostic (frozen before its graded reads, 2026-11-12 ~04:21Z)

**What happened.**
- **The deploy:** `deploy.sh staging` (04:12:37-56Z) put up version `a9898a7d-7dc2-4f22-aa19-d07f7df25448`, confirmed
  at `/healthz` `e3c2508e`.
- **The rows:** S1, S2 (silent branch), C1, C2-C4, W5-S1 to W5-S3 and S4 all read as predicted. **S3 read OFF:** smoke
  exited 1 with `PASS: 37 FAIL: 1`, and the one FAIL was `/basemap`: `HTTP 202 : neither the inert answer nor a
  package`. The per-product section read exactly as registered.
- **The gate:** staging rolled back to S0's `eb36fe73-ed1e-433a-aafa-1e3f3188f210` (`rollbacks.log` line first; exit 0
  at 04:15:40Z; `/healthz` `fdf62ada` x3). Production was never touched.
- **One note on the record:** S3/P3's WARN count (§4) was registered after staging's deploy and before smoke was read.

**The owner's rule for the diagnostic (04:18Z, relayed by the orchestrator), and the orchestrator's frozen form
(04:18:47Z, 04:19:22Z):** on a 202, re-ask until the map is served, within a fixed limit frozen in advance. A 202 that
never resolves still fails, and a 202 is never a pass.

**THE LIMIT: on any 202, re-ask every 15 s for at most 10 min.**

**The graded reads:** staging AS IT IS (`fdf62ada`, the old version), read-only. Three reads of smoke's exact
`/basemap` request shape, about 2 min apart, at ONE FRESH point: **KBNO (Burns, OR, a public airport), 43.59,-118.96**.
Each re-asks on a 202 within the limit. Each records the HTTP code, the body class (inert / package / 202 / other) and
`basemap_verdict`.

**Before the freeze, recorded and NOT graded:** one read of smoke's own point, at 04:18:51Z, gave HTTP 200, a BMPK
package, validate kOk. It is not discriminating: the deploy's 202 at 04:14Z most likely warmed that point. An earlier
attempt at 04:18:04Z is VOID, an instrument fault: it used the production key against staging and got a 401.

| outcome | the KBNO reads (read 1 is the cold one) | meaning, and what follows |
|---|---|---|
| **O1** | read 1 gives 202, then a validated package within the limit | staging's cold builder: environmental, and #643/#649 are not involved. S3's staging `/basemap` row becomes "on 202, re-ask every 15 s until served within 10 min; a 202 past 10 min fails". The deploy re-runs from S-slot, staging first, **after the orchestrator announces that v19's step 5' has closed** |
| **O2** | read 1 never gives 202: inert, or a package at once | the new code changed `/basemap`'s behaviour, a real finding. Stop, and find which carried staging commit does it (#623, #626, #627, `e0983b3b`, #618 are staging's extras). Nothing re-deploys |
| **O3** (the owner's) | read 1 gives 202 and never serves within the limit | staging's builder is broken: a separate stop. Nothing re-deploys |
| **O4** | anything else | stop and report |

**No deploy of any kind, staging or production, until v19's step 5' ends and E2:08 is restored.** `proxy:staging` is
released when the diagnostic is done; no deploy follows under it.

**The diagnostic, read (04:20:37-04:24:59Z, staging AS IT IS, `/healthz` `fdf62ada` before and after): O1.**

| read | time | HTTP | body class | verdict |
|---|---|---|---|---|
| KBNO #1, first | 04:20:37Z | 202 | 202 (`{"status":"building"}`, 21 B) | FAIL, a 202 |
| KBNO #1, re-ask 1 | 04:20:54Z (17 s later) | 200 | package (195,796 B) | PASS: BMPK, ETag `bm1-1-4c0875738f38-2f8cd599f20c7c75df6b4978`, validate kOk |
| KBNO #2 | 04:22:57Z | 200 | package, the same ETag | PASS |
| KBNO #3 | 04:24:59Z | 200 | package, the same ETag | PASS |

- **The old version gives 202 on a cold point, then serves a validated package 17 s later,** well inside the 10-min
  limit. So the 202 at 04:14Z was staging's cold builder: environmental, and #643 and #649 are not involved.
- **What follows, per O1:** S3's staging `/basemap` row reads "on 202, re-ask every 15 s until served within 10 min; a
  202 past 10 min fails". The re-ask uses smoke's exact request, and smoke's other rows are unchanged.
- **Production is unaffected:** its `wrangler.toml` env binds no `BASEMAP_PKG`, so production's `/basemap` stays the
  inert 503 that P3 already expects.
- **The deploy re-runs from S-slot, staging first,** after the orchestrator announces that v19's step 5' has closed.
- **The claim:** `proxy:staging` was released at 04:25:22Z. No deploy followed under it.

**HOW S3's re-ask runs on the re-run (the orchestrator, 04:25:58Z), so a 202 is never read as a pass by accident.**
`smoke-prod.sh` is UNCHANGED.
- **The re-ask is a separate logged loop:** scratch `bm_reask.sh`, moved into the record with the readings. It sends
  smoke's exact `/basemap` request (smoke's own point, `r=97`, `eps=100584`, the six layers and three levels, the test
  identity). On a 202 it re-asks every 15 s for at most 10 min, and **exits 0 only when its last read serves a validated
  package** (`basemap_verdict`: `PASS -- wired`). A 202 still standing at 10 min logs "STILL 202 … unresolved" and exits
  1.
- **Controlled both ways,** 04:26:46-48Z on staging as it is: smoke's (warm) point read a package and exited 0; a copy
  planted with lat=999 read 400 `bad_lat` and exited 1.
- **S3 on staging is graded:** smoke's summary reads either `FAIL: 0`, or `FAIL: 1` where that ONE FAIL is the
  `/basemap` row with `HTTP 202`. In the second case, the loop, run right after smoke, must exit 0. Any other FAIL, any
  second FAIL, or the loop exiting 1 makes S3 OFF, and the gate applies. The per-product section and the `wx-opuk`
  verdict stay as registered in §4.
- **P3 on production is unchanged:** production binds no `BASEMAP_PKG`, so its `/basemap` is the inert 503, and
  `FAIL: 0`.

## 6. The re-run (2026-11-12, after the orchestrator announced at 05:52:17Z that 5' had closed and E2:08 was restored)

**Staging: every row as predicted, and the gate passed.**

| step | time | reading |
|---|---|---|
| S0 | 05:53:05-14Z | one version at 100 %, `eb36fe73-ed1e-433a-aafa-1e3f3188f210`; `/healthz` `fdf62ada` |
| S-slot | 05:53:21Z | `deploy: held -- uk-data, covering proxy:staging`; main `e3c2508eb8db`; staging's list is exactly §1's ten, production's exactly the five |
| S-dep | 05:53:36-51Z | `deploy.sh staging` exit 0; version `eadbf909-8142-41ba-8d8b-830f4d6ccf84`; confirmed `/healthz` `e3c2508e` |
| S1 | 05:53:58Z | the 11 in `PRODUCTS`' order; `routed: true` on exactly `cref` and `opuk` |
| S2 | 05:53:58Z | `cref` 84 s, `stale: false`; the ten OPERA products 35,039 s, `stale: true`. The silent branch: every OPERA `latest` read 1794427800 at 05:54:02Z |
| C1 | 05:53:58Z | the five `nws_*` breakers, `closed`, exact; no `commit` key inside |
| C2-C4 | 05:54:11-14Z | C2 x3 401 `unauthorized`; C3 404 `not_found`; C4 405 `method_not_allowed` |
| W5-S1 to W5-S3 | 05:54:11-14Z | 400 `bad_submission`; 400, byte-identical to W5-S1; 405 `method_not_allowed` |
| S3 (and W5's C5) | 05:54:33-59Z | smoke exit 0, `PASS: 38 FAIL: 0`, the first case of §5's grading. 11 per-product lines, `WARN` on exactly the ten S2 graded stale; `wx-opuk` PASS. `/basemap` served a validated BMPK package on the first ask, so the re-ask loop did not run. Every Blipscope smoke row passes |
| S4 | 05:55:33-56:04Z | `s4_grade.py`: silent branch; `cref` 35/35 as originally frozen; `opuk` 14/14 503 `wx_stale`. `r12_prod.sh`'s own "NOT AS PREDICTED" is its pre-silence prediction, which §4 re-froze. The point file was deleted |

`proxy:staging` was released at 05:56:14Z. Staging stays on `eadbf909`.

**Production: NOT deployed.**
- **P0 (05:56:26-30Z):** one version at 100 %, `c1324f7c-c833-4854-bba2-f2ec5c8b4b59`; `/healthz` `68dc169a`. As predicted.
- **P-slot (05:56:37Z):** `deploy: held -- uk-data, covering proxy:production`; main `e3c2508eb8db`, the sha staging was
  deployed from; production's list is the five. As predicted.
- **P-dep:** `deploy.sh production` was refused by this session's own permission settings before it ran. It never
  started. Production was re-read at 05:57Z at `68dc169a`, and `proxy:production` was released at 05:57:03Z.
- **What follows** (the orchestrator, 05:57:49Z): the production half waits on the owner's permission. Once that is
  given, P0 and P-slot re-run fresh, and the orchestrator is told before P-dep. Nothing else is deploying meanwhile.

**Production, after the owner's one-time OK (15:07Z): every row as predicted.**

| step | time | reading |
|---|---|---|
| P0 | 15:14:34-41Z | one version at 100 %, `c1324f7c-c833-4854-bba2-f2ec5c8b4b59`; `/healthz` `68dc169a` |
| P-slot | 15:14:41Z | `deploy: held -- uk-data, covering proxy:production`; production's list is exactly the five. **One difference, named before the deploy:** main had moved to `f3fee349`, which touches `src/weatherscope/WxBasemap.h` and `test/host/*` only. The `proxy/` diff from `e3c2508e` is empty, so the deploy ran from `e3c2508e`, the sha staging was graded on. The orchestrator verified the empty diff independently (15:14:57Z) |
| P-dep | 15:14:57-15:15:14Z | `deploy.sh production` exit 0 (`1 commit(s) behind origin/main, none touching proxy/ (not bundled, not blocking)`); version `0f4cf9bb-7420-4f33-975b-eef50c862163`; confirmed `/healthz` `e3c2508e` |
| P1 | 15:15:20Z | the 11 in `PRODUCTS`' order; `routed: true` on exactly `cref` and `opuk` |
| P2 | 15:15:20Z | `cref` 161 s, `stale: false`; the ten OPERA products 68,722 s, `stale: true`. The silent branch: every OPERA `latest` read 1794427800 at 15:15:23Z |
| C1 | 15:15:20Z | the five `nws_*` breakers, `closed`, exact; no `commit` key inside |
| C2-C4 | 15:15:26-27Z | C2 x3 401 `unauthorized`; C3 404 `not_found`; C4 405 `method_not_allowed` |
| P3 (and W5's C5) | 15:15:33-53Z | smoke exit 0, `PASS: 38 FAIL: 0`. 11 per-product lines, `WARN` on exactly the ten P2 graded stale; `wx-opuk` PASS; `/basemap` the inert 503. Every Blipscope smoke row passes |
| P4 | 15:16:01-26Z | `s4_grade.py`: silent branch; `cref` 35/35 as originally frozen; `opuk` 14/14 503 `wx_stale`. The point file was deleted |

`proxy:production` was released at 15:16:33Z; `/healthz` re-read `e3c2508e`. **The deploy is complete on both
environments.** OPERA was still silent at the last read (about 19 h); `/healthz` now shows that to anyone who reads it,
which is what #643 was for.
