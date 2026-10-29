# Basemap deploys: what each stage still needs, five questions, and the order

Written 2026-10-29, while #486 waits on the A2.46 ruling. The stages are §9 of `docs/basemap-route.md`, and §9's own rule governs them: **each stage is reviewed and merged before it deploys, and its predictions are frozen in its PR.** Under A2.47, no production deploy (D0 or D4) and no `smoke-prod.sh` run happens before 19:28Z on 2026-10-29.

Nothing here is code yet. The questions below decide the code, so they come first.

## What #486 leaves for the deploy stages

| stage | what §9 requires | what is missing |
|---|---|---|
| **D0** (production) | the proxy with the route and **no binding**; `/healthz` gains `basemap: "not_configured"`; `smoke-prod.sh` gains a `/basemap` check passing on exactly `503 not_configured`, or `200` with BMPK magic, an ETag and `validate()` → kOk | the `/healthz` field and the smoke check, both |
| **D2** | the builder Worker `blipscope-basemap`, staging then production, **no route**, stamped by a script as `deploy.sh` stamps the proxy; dry runs show the R2 binding in both envs | a wrangler config for the builder; its deploy step; see Q1 and Q2 for two things that would go wrong as written |
| **D3** (staging) | the proxy's `BASEMAP` binding; V1 `/healthz` `basemap` = `{store, rev, commit}` "with the builder's commit"; V2-V5 | the binding; **the builder does not return its commit**, so V1 cannot be met (Q4); `/credits` gains the attribution block, and the smoke its `/credits` half |
| **D4** (production) | the same binding in production; V1-V4, then V6 on the fleet's traffic | the binding |

## Five questions, each with a recommendation

**Q1. The R2 binding's name.**
- **The conflict:** §9's D2 row says `BASEMAP_STORE`. The code under review (`worker.ts`, its `BuilderEnv`, and the MW tests) uses **`STORE`**.
- **Recommended:** keep `STORE`, and correct the row. Renaming means changing reviewed code for a label.

**Q2. How the builder is stamped.**
- **The mismatch:** `deploy.sh` stamps the proxy with `--define BUILD_COMMIT:"<sha>"`, a compile-time constant. The builder reads **`env.BUILD_COMMIT`**, a runtime var, and refuses to build without 40 hex (`builder_unstamped`, §2.4).
- **The consequence:** stamped exactly "as `deploy.sh` does", the builder would see no var and **refuse every build**.
- **Recommended:** keep the runtime var, and stamp with `--var BUILD_COMMIT:<sha>`. The MW tests already pass it that way.
- **Missing too:** no test covers the unstamped refusal itself. One goes in, with its sabotage.

**Q3. The deploy script.**
- **What `deploy.sh` guarantees, the first path to match:** a clean `proxy/`, a commit that exists on the remote, a tree not behind `origin/main`, the commit stamped, and a confirmation afterwards through `/healthz`.
- **Recommended:** `deploy.sh` gains a builder mode (`--builder`) that runs the **same** guards against the builder's config. The stamp becomes `--var`, per Q2.
- **The confirmation the builder cannot give:** it has no route, so it cannot confirm itself the way the proxy does. Its confirmation is the proxy's `/healthz` `basemap.commit` once D3 binds it (Q4).
- **So D2 alone is confirmed only by the deploy's own output,** and that is stated as the weaker form.

**Q4. `/healthz` when bound, and the builder's commit.**
- **Recommended, for the builder:** it adds an `x-basemap-commit` header beside `x-basemap-store`.
- **Recommended, for `/healthz`:** `basemap` becomes `{rev, store, commit}`, taken from the isolate's last builder response. `store` and `commit` are **null before the first build in that isolate**, never a made-up value (the rule `/healthz`'s `wx` already follows).
- **What that means for V1:** it is read after one `/basemap` request (V2's).
- **The alternative, not recommended:** `/healthz` calling the builder on every read. It would put a build-path call on a health probe.

**Q5. How the smoke runs `validate()`.**
- **The constraint:** `smoke-prod.sh` is bash and curl, and runs no node today.
- **Recommended:** the verdict goes in a sourced file, as opuk's does (`basemap-verdict.sh`), with a selftest in CI over canned answers and a sabotage. `validate()` runs as `npx tsx scripts/basemap-validate.ts <file>`, the proxy's own validator, printing one status.
- **Why:** so the smoke refuses exactly what the device would.
- **The alternative, not recommended:** a magic-and-length check in bash. It would pass a package the device refuses.

## The order, once Q1-Q5 are ruled

1. **PR "D0 + D2"**, after #486 merges. Its predictions are frozen before its code. It holds:
   - the `/healthz` field (both states, per Q4);
   - the smoke check and its selftest (Q5);
   - the builder's config: `blipscope-basemap` and `blipscope-basemap-staging`, `workers_dev = false`, no route, the `STORE` binding to `blipscope-basemap`, and `cpu_ms` 14,530 (A2.37, accepted in A2.40);
   - `deploy.sh --builder` (Q3), the `x-basemap-commit` header, and the unstamped test (Q2).
   - **Then:** D2 (staging, then production), and D0 (production, **after 19:28Z**).
2. **PR "D3":** the staging binding, `/credits`' attribution block and the smoke's `/credits` half. **Then D3** on staging, with V1-V5 and the rollback rules of §9.
3. **PR "D4":** the production binding. **Then D4,** V1-V4 and V6, outside any replay window.

**Cost:** **$0 at every stage,** as §9's table states. A service-binding call adds no request charge, and the builder's CPU is inside the included 30M ms. Anything that would change that is asked first.

## Rulings on Q1-Q5, verbatim (source: reviewer chat, pasted by Daniel, 2026-10-29)

> 2. #505, the deploy plan: good catches, all three. Rulings:
> - Q1: keep STORE. It's the builder's own R2 binding (BuilderEnv in worker.ts), unambiguous there. Correct §9's BASEMAP_STORE.
> - Q2: stamp the builder the way the proxy is stamped, not with --var. The builder reads a compile-time BUILD_COMMIT (declared, with its wrangler define defaulting to "UNSTAMPED"), and deploy.sh --builder passes --define exactly as it does for the proxy. That keeps one stamping mechanism, and the stamp travels inside the bundle it describes, so a dashboard var can't make PROVENANCE's builder line drift from the code. Add the test: an unstamped bundle refuses every build as builder_unstamped.
> - Q3: deploy.sh --builder, with the same guards. D2's own output is its first confirmation; D3's binding test on staging is the real proof, and the plan should say so.
> - Q4: as recommended. The builder returns x-basemap-commit, and /healthz shows {rev, store, commit} from the isolate's last builder response: null before the first build, never made up.
> - Q5: as recommended. A sourced verdict file with a CI selftest, calling the proxy's own validate(), the pattern K9's smoke uses.
> 3. The order as you list it: the D0 + D2 PR after #486 merges, predictions first (the stamp change and its test go in it); D2 on staging then production; D0 on production after 19:28Z with its smoke run; then the D3 PR and staging; then the D4 PR and production, outside any replay window. Each stage is reviewed, merged, deployed and verified, at $0.

(Item 1 of the same message, the A2.46 ruling and #486's conditional approval, is recorded in `docs/basemap-route.md`.)

**Q3, as the plan now says:** D2's confirmation is weak and stated as such. The deploy's own output shows the full stamp and the `STORE` binding, and that is all D2 alone can show. **The real proof is D3's binding test on staging:** `/healthz` `basemap.commit` equals the commit D2 stamped, read after one `/basemap` build.

## The D0 + D2 PR: predictions, frozen before its code

**What the code will be:**
- **C1, `worker.ts`:**
  - `declare const BUILD_COMMIT: string`;
  - `makeBuilder(commit)` returns the fetch handler, and the default export is `makeBuilder(BUILD_COMMIT)`;
  - `BuilderEnv` keeps only `STORE`;
  - a 200 carries **`x-basemap-commit`** beside `x-basemap-store`.
  - The endpoint tests construct `makeBuilder(COMMIT)` in place of passing `BUILD_COMMIT` in `env`.
- **C2, `proxy/basemap.wrangler.toml`:**
  - names: `blipscope-basemap` at the top level and in `[env.production]`, `blipscope-basemap-staging` in `[env.staging]`;
  - `main = "src/basemap/worker.ts"`, the proxy's `compatibility_date`, `workers_dev = false`, **no route**;
  - in every env: `define BUILD_COMMIT = "\"UNSTAMPED\""`, since defines are not inherited, and the `STORE` R2 binding to `blipscope-basemap`;
  - `[limits] cpu_ms = 14530`.
- **C3, `deploy.sh --builder <staging|production>`:**
  - **the same guards 1-3:** a clean `proxy/`, a commit on the remote, not behind `origin/main` in `proxy/`;
  - **the stamp is the full 40-hex HEAD,** because PROVENANCE pins it and `--check` checks it out. The proxy's stamp stays short;
  - **`ALLOW_DIRTY=1` is refused with `--builder`,** before any wrangler call. A `-dirty` stamp is not 40 hex, so the builder would refuse every build;
  - **it skips the `DEVICE_KEY_SECRET` check** (proxy-only);
  - **it runs** `npx wrangler deploy --config basemap.wrangler.toml --env <env> --define BUILD_COMMIT:"\"<sha40>\""`;
  - **and has no `/healthz` confirmation.** It prints that D2's output is its only confirmation and D3's binding test is the proof.
- **C4, `handler.ts`:**
  - the isolate cache keeps the store id **and** the commit from the last builder 200;
  - a 200 whose `x-basemap-commit` is not 40 hex is **`500 builder_invalid`**, as a bad store id already is;
  - `basemapHealth(env)` is `"not_configured"` unbound, else `{rev, store, commit}`, with null before the first build.
- **C5:** `/healthz` gains `basemap: basemapHealth(env)`.
- **C6, the smoke (the opuk/K9 pattern):**
  - `scripts/basemap-verdict.sh` (sourced) and `scripts/basemap-validate.ts` (prints one status from the proxy's `validate()`);
  - `scripts/basemap-verdict.selftest.sh`, run by a `workers.yml` step;
  - a `smoke-prod.sh` section requesting `/api/v1/blipscope/basemap` with the smoke's own `LAT`, `LON` and `R` and its device auth. The body is binary, so it is never dumped.
- **C7:** §9's D2 row in `docs/basemap-route.md` reads `STORE` (Q1).

**Rows:**

| row | what | predicted |
|---|---|---|
| **MW18** | the default export, built under the test build's own define (`"UNSTAMPED"`), POSTed four requests in §5.4's four shapes (r97 with terrain, r97, r161, r222), at MW's synthetic origin | **all four `503 builder_unstamped`**, with **0 store reads** |
| **MW19** | a builder 200 whose `x-basemap-commit` is missing, or not 40 hex | **`500 builder_invalid`**, and nothing cached |
| **MH1** | `/healthz` with no `BASEMAP` binding | `basemap` is **`"not_configured"`** |
| **MH2** | bound, before any `/basemap` request in the isolate | **`{rev: 1, store: null, commit: null}`** |
| **MH3** | after one `/basemap` 200 | **`{rev: 1, store: <the builder's store id>, commit: <the builder's commit>}`** |
| **S-WSTAMP** | the builder's 40-hex check removed | **exactly {MW18}** |
| **S-WNOCOMMIT** | the handler's commit check removed | **exactly {MW19}** |
| **S-HMADEUP** | `basemapHealth` answers a constant commit before the first build | **exactly {MH2}** |
| **S-HNOCACHE** | `basemapHealth` ignores the cache | **exactly {MH3}** |
| **every existing row** | the full pass | **46 of 46 AS PREDICTED, C-LOADFAIL AS PREDICTED,** 0 runtime failures. MW1-MW17, MP1-MP3, MA1 and MD1-MD4 pass with the builder built by `makeBuilder(COMMIT)`. The package bytes are unchanged, because the builder's code path is not touched |
| **the smoke selftest** | canned answers through `basemap_verdict` | the whole verdict list, compared exactly: `503 not_configured` → PASS (inert); `200` + syn-min + ETag → PASS (wired); `200` + `neg-crc` + ETag → FAIL (refused as kCrc); `200` + syn-min, no ETag → FAIL; `404` → FAIL; `503 basemap_unavailable` → FAIL; `200` + an empty body → FAIL. **Its sabotage** (the `validate()` call skipped) turns **exactly the `neg-crc` case** |
| **D2 dry runs** | `wrangler deploy --dry-run --config basemap.wrangler.toml --env staging` and `--env production` | each lists the **`STORE` R2 binding to `blipscope-basemap`**, and neither has a route |
| **C3's refusal** | `ALLOW_DIRTY=1 scripts/deploy.sh --builder staging`, with one untracked file planted in `proxy/src/` (removed after) | refuses with exit 1, **before any wrangler call** |
| **D2** | `deploy.sh --builder staging`, then `production` | each prints the **full 40-hex stamp** and the `STORE` binding, and exits 0. That is D2's only confirmation (Q3) |
| **D0** (after 19:28Z) | **first,** the commits that touch `proxy/` and are on main but not in production are listed (from `/healthz`'s commit). **Then** `deploy.sh production` | `confirm-deploy` reports the new commit; `/healthz` `basemap` is **`"not_configured"`**; the smoke's `/basemap` reads **PASS (inert)**; the rest of the smoke matches its last run's tallies, which are read and recorded before D0. **If a listed commit is held by a ruling** (anything R3 gates), stop and ask before deploying |
| **D0's rollback** | §9's rules | any fleet 5xx, 401 or 403 in [T, T + 15 min) → roll back to the version recorded before the deploy |

**Anything outside these rows:** stop and report, with no fix until a ruling.
