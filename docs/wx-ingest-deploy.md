# The ingest deploy path: every ingest deploy checks the deploy claim

The weather ingest Workers are cref's `blipscope-wx-ingest`, the ten OPERA products' `blipscope-wx-ingest-<id>` and the
dev Worker `blipscope-wx-ingest-dev`. Until now they were deployed by hand with `wrangler deploy`, which nothing
refuses without the claim hook (`docs/session-claims.md`, "What it can't stop"). This adds `tools/wx/ingest_deploy.sh`,
which checks the deploy claim itself before it runs anything, as `proxy/scripts/deploy.sh` does for the proxy.

## The ruling (reviewer chat, pasted by Daniel, 2026-11-11 ~04:35Z), verbatim

> Ingest deploy path, ruled now so stage 3 doesn't wait on the hook fix: before any of the ten ingests deploys, every
> ingest deploy goes through a script that checks the deploy claim itself, as deploy.sh does for the proxy and as
> md_rollback.sh already logs its rollbacks. Reuse the existing claim check unchanged; if it needs a change, that goes to
> blipscope-5c. Predictions first, with a plant: the deploy is refused without the claim. I'll review it from git. It isn't
> urgent, because no ingest deploys before Daniel's cost decision.

## What the script does

```sh
BLIPSCOPE_SESSION=<name> python scripts/session-claim.py take deploy --covers wx-ingest-opuk:production --until +45m
BLIPSCOPE_SESSION=<name> bash tools/wx/ingest_deploy.sh wx-ingest-opera.wrangler.toml opuk
# ... the read-back ...
BLIPSCOPE_SESSION=<name> python scripts/session-claim.py release deploy
```

1. **Usage, before the claim is read** (exit 2, nothing runs):
   - The toml is one of the three ingest tomls in `proxy/`, and nothing else. The proxy's `wrangler.toml` goes through
     `deploy.sh`, and `basemap.wrangler.toml` isn't an ingest.
   - `wx-ingest-opera.wrangler.toml` and `wx-ingest-dev.wrangler.toml` need an env. The opera toml's top level is a
     Worker, `blipscope-wx-ingest-opera`, that must not exist, so a deploy without an env would create it.
     `wx-ingest.wrangler.toml` (cref) has no envs, so it takes none.
   - The env is a table under `[env]` in that toml, read with `tomllib`.
2. **The target** is the Worker the deploy reaches: its name, read from the toml with `tomllib` (the env's `name`, or the
   top level's when there is no env), without the `blipscope-` prefix, then `:production`. For example,
   `wx-ingest-opuk:production`. An env doesn't change the target: `opuk` and `opuk-inert` deploy the same Worker. Each
   ingest Worker has one deployment, so the env half is always `production`.
3. **The claim check is `deploy.sh`'s, unchanged:** the same first Python that runs, the same
   `session-claim.py check deploy --covers "$CLAIM_TARGET"` and the same refusal (exit 1, `show`, the hint). Only the
   target differs. `scripts/session-claim.py` itself isn't touched.
4. **The deploy:** `npx wrangler deploy -c <toml> [-e <env>]`, with stdin closed.
5. **The read-back:** `npx wrangler deployments status -c <toml> [-e <env>] --json`, printing each version's id and
   percentage.
6. **The exit code is the deploy's.** The caller releases the claim after their own read-back, as with `deploy.sh`.

## The checks, `tools/wx/check_ingest_deploy.sh` (predictions frozen before the code)

The driver builds a mirror tree in a temporary folder:
- byte copies of `tools/wx/ingest_deploy.sh`, `scripts/session-claim.py` and four tomls (`proxy/wrangler.toml` and the
  three ingest ones), each checked with `cmp`;
- a fake `npx` first on `PATH`, which records each call and never reaches Cloudflare;
- its own `BLIPSCOPE_CLAIMS_DIR`.

So no row touches a real claim, a real Worker or a repo file. A claim is taken with the real `session-claim.py take`.
The expired one is written as a file, because `take` refuses a past time. Each row compares the exit code and the
**whole** list of fake calls with its prediction.

| row | the claim (holder: covers) | run as | arguments | predicted |
|---|---|---|---|---|
| **ID1** (control) | alpha: `wx-ingest-opuk:production` | alpha | `wx-ingest-opera.wrangler.toml opuk` | **exit 0**; calls: `wrangler deploy -c wx-ingest-opera.wrangler.toml -e opuk`, then `wrangler deployments status -c wx-ingest-opera.wrangler.toml -e opuk --json` |
| **ID2** (no claim) | none | alpha | as ID1 | **exit 1**, `REFUSING TO DEPLOY`; no calls |
| **ID3** (someone else's) | beta: `wx-ingest-opuk:production` | alpha | as ID1 | **exit 1**; no calls |
| **ID4** (yours, another Worker) | alpha: `wx-ingest-opis:production` | alpha | as ID1 | **exit 1**; no calls |
| **ID5** (expired) | alpha: `wx-ingest-opuk:production`, ended a minute ago | alpha | as ID1 | **exit 1**; no calls |
| **ID6** (no session name) | alpha: `wx-ingest-opuk:production` | `BLIPSCOPE_SESSION` unset | as ID1 | **exit 1**; no calls |
| **ID7** (the opera top level) | alpha: `wx-ingest-opera:production` | alpha | `wx-ingest-opera.wrangler.toml` | **exit 2**; no calls |
| **ID8** (the proxy) | alpha: `proxy:production` | alpha | `wrangler.toml` | **exit 2**; no calls |
| **ID9** (cref) | alpha: `wx-ingest:production` | alpha | `wx-ingest.wrangler.toml` | **exit 0**; calls: `wrangler deploy -c wx-ingest.wrangler.toml`, then `wrangler deployments status -c wx-ingest.wrangler.toml --json` |
| **ID10** (an env the toml lacks) | alpha: `wx-ingest-opuk:production` | alpha | `wx-ingest-opera.wrangler.toml opux` | **exit 2**; no calls |
| **ID11** (the inert env, same Worker) | alpha: `wx-ingest-opuk:production` | alpha | `wx-ingest-opera.wrangler.toml opuk-inert` | **exit 0**; ID1's two calls with `-e opuk-inert` |
| **ID12** (a failed deploy) | as ID1; the fake deploy exits 7 | alpha | as ID1 | **exit 7**; ID1's two calls |
| **ID13** (the dev Worker) | alpha: `wx-ingest-dev:production` | alpha | `wx-ingest-dev.wrangler.toml off` | **exit 0**; ID1's two calls with `-c wx-ingest-dev.wrangler.toml -e off` |
| escape | anything else, in any row | | | stop and report |

**The sabotages (`--sabotage`).** Each one is planted into the mirror's copy of the script only. Its anchor must appear
exactly once and the copy must change, or the driver stops as RIG (exit 2). Then all 13 rows run. The set of rows that
differ from their prediction must equal the registered set exactly.

| plant | the edit | registered red set |
|---|---|---|
| **IDs-a** (the ruled plant: the deploy without the claim) | the claim check's `if !` line becomes `if false; then` | **{ID2, ID3, ID4, ID5, ID6}**: each deploys (exit 0, ID1's calls) |
| **IDs-b** (the target from the env, not the toml) | `CLAIM_TARGET` becomes `wx-ingest-${ENV}:production` | **{ID9, ID11, ID13}**: each is refused (exit 1, no calls) |
| **IDs-c** (the opera top level allowed) | the env-required guard is removed | **{ID7}**: it deploys `blipscope-wx-ingest-opera` |
| **IDs-d** (any toml allowed) | the list of three is removed | **{ID8}**: it deploys the proxy |
| **IDs-e** (the exit code dropped) | the last line exits 0 | **{ID12}** |
| escape | any other red set, or a crash | stop and report |

The driver exits 0 when every row (or every sabotage) is as predicted, 1 when one is off, and 2 on a rig failure.

**Where it runs:**
- here, in Git Bash, from the worktree (the Python is Windows Python 3, which has `tomllib`);
- in CI, on `ubuntu-24.04`, as two new steps in `device-ids.yml`'s `no-real-device-ids` job, beside the claims'
  selftest. It runs on every PR, because that workflow has no path filter.

## CI for the PR (predicted before the push)

The PR changes `docs/wx-ingest-deploy.md`, `tools/wx/ingest_deploy.sh`, `tools/wx/check_ingest_deploy.sh` and
`.github/workflows/device-ids.yml` (two steps, no job). None of them is in `workers.yml`'s or `web-renderer.yml`'s path
filter, so the same three workflows run as on a docs-only PR: `device-ids`, `firmware` and `no-traffic-captures`.

- **Predicted: 32 check runs: 29 success, 3 skipped, 0 failed.**
- **The baseline** is #624's finished head run (`70e1bbd397fa`, docs only): 32 = 29/3/0, with 10 build legs.
- **No job-set change since then.** Main's last workflow change is `81cfbd09` (#629), which added steps. Main's push run
  at `947e1d3825fc` has the same 10 build legs.

## Not in this change

- **`docs/session-claims.md`'s "What it can't stop" table** still says the ingest Workers are "deployed by hand with
  `wrangler`; no script". It's blipscope-5c's doc, so the row is left for them, named in the PR.
- **Nothing deploys.** No ingest deploys before Daniel's cost decision, so the script runs for real only at stage 3, and
  each stage-3 row will name it.
