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

## The D0 + D2 PR, run: one row off-prediction (MW18's own control); stopped for a ruling (2026-10-29)

**Where it ran:**
- The code is `296a05d`, rebased onto main after #486 merged (`ca30b88`).
- The worktree's files the harness reads are LF (checked: 0 with a CR).

**Two changes to tests, made before any run and outside what the rows above say:**
- **MW15's stub builder now sends a valid `x-basemap-commit`.** Without it, C4's commit check would answer `500` before `validate()` is reached. S-WNOVALIDATE would then stop reddening MW15: the new guard would be covering the old one's test.
- **MW19's "nothing cached" is checked as `/healthz`'s `store` being null,** not the whole object. Its `commit` field is MH2's to test, so S-HMADEUP stays exactly {MH2}.

| row | result |
|---|---|
| typechecks | **both exit 0** |
| MH1, MH2, MH3 | **as predicted** |
| MW19 | **as predicted** (and MW15 still passes) |
| every other basemap row | **0 failing:** 139 passed, MD2 skipped as before |
| the smoke selftest | **as predicted:** `PASS PASS FAIL FAIL FAIL FAIL FAIL`; under its sabotage exactly the kCrc case turns |
| D2 dry runs | **as predicted:** each lists `env.STORE (blipscope-basemap) R2 Bucket`, and neither has a route. **The artifact:** both bundles hold `makeBuilder("UNSTAMPED")` as the default export, and no bare `BUILD_COMMIT` identifier (its one remaining mention is inside `build.ts`'s error string) |
| C3's refusal | **as predicted:** `ALLOW_DIRTY=1 deploy.sh --builder staging`, with an untracked file planted in `proxy/src/`, refused with exit 1 and no wrangler output at all. The plant was removed; the tree is clean |
| **MW18** | **OFF-PREDICTION:** it fails |
| the full pass | **not run.** Its baseline needs MW18 green |

**MW18's cause:** its own **control**.
- **What the control does:** it runs the stamped builder (`makeBuilder(COMMIT)`) over MW's synthetic store first, to show the four requests get past the stamp.
- **Why it threw:** the first request asked for every layer (mask 63). MW's store holds only water and roads, so `build()` threw `layer rivers read with no files`.
- **What it means:** the test stopped there. **The refusal it exists for never ran.**

**The alternative, and why the data excludes it:** that the default export is stamped in the test build, so it built instead of refusing.
- **`buildstamp.test.ts` passes on this tree.** It asserts `BUILD_COMMIT` is `"UNSTAMPED"` under test.
- **The bundle agrees:** the dry-run bundle's default export is `makeBuilder("UNSTAMPED")`.
- **So the throw can only be the stamped control's.**

**Found on the way, not a defect in production:**
- **What `build()` does:** it **throws** (it does not return a status) when a requested layer has no source files among the regions the square meets.
- **Why production cannot reach it:** on the sealed store, 3,144 county regions carry water and rivers and 51 state regions carry roads and peaks; places do not come through regions. A covered origin always meets its own county's and state's regions, and MS0's 31 cells all built with every vector layer requested.
- **So only a synthetic store can reach it.** On record; no change proposed.

**Proposed fix** (committed locally, **not pushed** until a ruling):
- **The change:** MW18's four requests keep §5.4's four shapes (r97 with terrain, r97, r161, r222) but ask only for the layers MW's store holds.
  - Mask 5 (water, roads) for r97, r161 and r222.
  - Mask 37 (water, roads, terrain) with the three levels for r97 with terrain. With no terrain tiles, that build returns `503` at the missing tile; it does not throw.
- **Nothing else changes.**

**Frozen predictions for the fix, if ruled:**

| row | predicted |
|---|---|
| MW18 | **passes.** The stamped control reads the store on each of the four shapes; the default export answers all four `503 builder_unstamped` with **0 store reads** |
| S-WSTAMP | **exactly {MW18}** |
| the full pass | **50 of 50 AS PREDICTED** (the 46, then S-WSTAMP {MW18}, S-WNOCOMMIT {MW19}, S-HMADEUP {MH2} and S-HNOCACHE {MH3}). C-LOADFAIL AS PREDICTED; baselines ingest 10 in 1, mv 31 in 1, all 137 in 17; 0 runtime failures |
| CI on the PR's head | every check finished, none failing |

## Rulings, verbatim (source: reviewer chat, pasted by Daniel, 2026-10-30)

> 1. #486: merged as ruled. I checked the squash: its parent plus the approved head, exactly.
>
> 2. #505, MW18: the fix as proposed. The control asks only for the layers MW's store holds, and keeps §5.4's four shapes. The first reading stays on record as the miss, with its cause: the control threw before the refusal ran, so the row never exercised its subject. Push with --force-with-lease, then the frozen MW18, S-WSTAMP {MW18}, 50 of 50 and CI, then #505 up for review.
>
> 3. build()'s throw on a requested layer with no files: turn "only synthetic stores reach it" into a reading before D4 (the production route). A census over the sealed store's own index, from the local verified copy: every region holds at least one file for each layer the builder reads from it. If any region doesn't, name it and stop; build() then returns a status instead of throwing, as its own change.
>
> 4. D0: the carried proxy/ commits are cleared to ship with it, #499's /wx changes included (UK points answer 503 wx_stale until R3, as ruled for R2). wx-ingest stays undeployed until R3. D0 comes after #505's review and merge, outside any replay window, with its smoke run.

## The region census (item 3): predictions, frozen before its script

**What the builder reads from a region:**
- `build()` reads **water, rivers, roads and peaks** from the regions whose box meets the square (RD-P6). Places and population come from the whole manifest, not from regions.
- `regionsFor` builds two kinds of region:
  - **a county region** takes that county's `tl_2024_<GEOID>_areawater` (water) and `tl_2024_<GEOID>_linearwater` (rivers);
  - **a state region** takes `tl_2024_<FIPS>_prisecroads` (roads) and `DomesticNames_<USPS>_Text` (peaks).
- **So "each layer the builder reads from it" is** {water, rivers} for a county region and {roads, peaks} for a state region.

**The census:** `scripts/basemap-census-regions.ts`.
- **What it reads:** the sealed store's `index.json` and `manifest.json` through `assembleStore`, the same local verified copy (every object checked by sha256) that `basemap-grade-store.ts` uses. It prints the store id and the index's sha256, so the reading names the bytes it read.
- **What it checks:** every region holds either ≥1 water **and** ≥1 rivers file and no other layer, **or** ≥1 roads **and** ≥1 peaks file and no other layer.
- **What it does with any other region:** it **names it** (its index, its box, the files it holds) and exits 1.
- **The rest of its output:** identifiers and counts, never a coordinate beyond a named region's box.

| row | predicted |
|---|---|
| **R-CENSUS** on the sealed store | **3,195 regions: 3,144 county regions** (each ≥1 water and ≥1 rivers file, nothing else) **and 51 state regions** (each ≥1 roads and ≥1 peaks file, nothing else); **0 regions of any other shape**; exit 0 |
| **its control, C-CENSUS** | the same census over a copy of the index with one county region's rivers file removed: it **names that county** and exits 1 |
| **anything else** | a count other than 3,144 and 51, a region of another shape, or a store it cannot read (BLIND): **stop and report.** If a region lacks a layer, `build()` returning a status instead of throwing becomes its own change, as ruled |

**What R-CENSUS does not settle, named now so it is not mistaken for the whole argument.**
- **What it shows:** every region carries its layers.
- **What it does not show:** that every square the route accepts meets a region of each kind.
- **The gap:** coverage is per 0.01° cell. An origin in a covered cell could sit up to about 1.4 km from the nearest state polygon. At `r = 1` the square reaches only about 1 km, so near a coast or border it might meet no state region, or no county region.
- **On record,** with a reading proposed in the report. Not run here.

## The 2026-10-30 rulings carried out: MW18 as frozen; the census as frozen; S-PWRITE off-prediction, stopped for a ruling

**Pushed with lease as `b1a7d29`:** the run record, the MW18 fix, then this ruling's record with the census's predictions.

| row | result |
|---|---|
| MW18 | **as frozen:** passes |
| CI on `b1a7d29` | **38 checks, all finished: 35 passed, 3 skipped** |
| the full pass on `b1a7d29` | **49 of 50 AS PREDICTED**, including S-WSTAMP {MW18}, S-WNOCOMMIT {MW19}, S-HMADEUP {MH2} and S-HNOCACHE {MH3}. Baselines ingest 10 in 1, mv 31 in 1, all 137 in 17; C-LOADFAIL AS PREDICTED (BLIND, now 61 not run); 54 runs, 0 runtime failures |
| **S-PWRITE** | **OFF-PREDICTION:** failing **{MP2, MW18}** against its frozen {MP2} |
| **R-CENSUS** | **as frozen:** store `4c0875738f38`, `index.json` sha256 `63a9c1429db1`, **3,195 regions: 3,144 county regions** (≥1 water and ≥1 rivers file, nothing else), **51 state regions** (≥1 roads and ≥1 peaks file, nothing else), **0 of any other shape**; exit 0 |
| **C-CENSUS** | **as frozen:** it planted region 0 (county 01001) without its rivers file. The census named exactly that region (`holds {water}`, its box, `tl_2024_01001_areawater.zip`) and exited 1 |

**The census answers item 3's question for the regions:** every region holds each layer the builder reads from it. The small-r edge named above is not covered by it, and stays on record.

**The first launch of the pass did not run:** my own command `cd`'d into a directory it was already in, so the log path was never set. That is an instrument failure, not a reading. The pass above is the re-run.

**S-PWRITE's cause:** MW18's control runs the stamped builder over **a store stub of its own, with only `get`**. S-PWRITE makes the builder call `STORE.put` before building, so MW18 throws `TypeError: env.STORE.put is not a function`. This was confirmed by planting S-PWRITE alone and running MW18, with `worker.ts` restored from a byte copy and its sha256 checked.

**The alternative, and why the data excludes it:** that MW18 fails for some reason of its own.
- **Its failures across the pass:** MW18 passes in the baseline and in every other sabotage's run, including the other three in `worker.ts`'s neighbourhood (S-WSTAMP fails exactly MW18, and S-WNOCOMMIT, S-HMADEUP and S-HNOCACHE do not touch it).
- **The direct test:** it fails under S-PWRITE alone, with that error.

**What it exposed, which is the real finding:**
- **The audit MP2 relies on:** MP2 asserts "no store write across every MW row". It counts writes through `builderOver`'s stub.
- **The path that missed it:** MW18 built its own stub instead, so **MW18's builder runs were never inside MP2's audit**. It is a second path that does not go through the first path's guard.
- **The A2.16 re-check missed it too.** When MW18 was added, I re-checked the frozen sets by reasoning, not against MW18's own scaffolding.

**Proposed fix** (committed locally, **not pushed** until a ruling):
- **The change:** MW18's store stub gains `put` and `delete` that count into the same `allWrites` and `shims` MP2 reads, as `builderOver`'s stub does. Nothing else changes.
- **Its effect:** under S-PWRITE, the write is counted (MP2 fails, as it must) and MW18 runs to its end (it does not throw).
- **The alternative, not recommended:** re-registering S-PWRITE as {MP2, MW18}. It would grade a TypeError as a signal, and leave MW18 outside MP2's audit.

**Frozen predictions for the fix, if ruled:**

| row | predicted |
|---|---|
| S-PWRITE | **exactly {MP2}** |
| MW18 and MP2 | **both pass**; MP2 now audits MW18's builder runs, with 0 writes |
| the full pass | **50 of 50 AS PREDICTED**, C-LOADFAIL AS PREDICTED, 0 runtime failures |
| CI on the PR's head | every check finished, none failing |

## Rulings, verbatim (source: reviewer chat, pasted by Daniel, 2026-10-30, the second message of the day)

> 1. S-PWRITE: the fix as proposed. MW18's stub gains put and delete, counting into the same audit MP2 reads, so MP2 now audits MW18's builder runs. Re-registering S-PWRITE as {MP2, MW18} stays not done, for your reason. The first reading stays on record as the miss, with its cause: MP2's audit had a second path around it, and the re-check reasoned about MW18 instead of reading its setup. The void first launch stays on record too.
> 2. The census: accepted as read. 3,195 regions; every county region holds water and rivers, every state region roads and peaks; the control names 01001.
> 3. The open gap: yes, measure it before D4. Sweep the covered cells at the route's smallest accepted r, and count the squares that meet no region of a kind. If the count is 0, record it and the gap closes. If it isn't, name the cells and stop: the builder then handles that case without throwing, as its own change with predictions (a status, or the layer built empty, your proposal), before D4.
> 4. On this yes: push the record, the census and the fix; then the full pass at 50 of 50, CI on the new head, and #505 up for review. D2 and D0 follow its merge, as ruled.

## The small-r sweep (item 3): predictions, frozen before its script

**The terms, each taken from the builder's own code:**
- **The accepted origins** are exactly the cells `coverage.bin` lists. `build()` refuses an origin unless `covered(cov, h, c)` holds for its own 0.01° cell, `h` and `c` in integer hundredths of a degree.
- **The smallest accepted r is 1 km.** `quantise` makes `rKm = max(1, round(r))`.
- **Why r = 1 is the worst case:** the square is `squareBox(makeFrame(h·10⁴, c·10⁴, r·10⁶, ε), r·10⁶)`, and squares nest as r grows. If every r = 1 square meets a region of each kind, every larger square does too.
- **A region of each kind** means a county region (water, rivers) and a state region (roads, peaks), as R-CENSUS read them. RD-P6's test is `boxMeets`.

**The sweep:** `scripts/basemap-sweep-small-r.ts`.
- **What it reads:** the sealed store through `assembleStore`, as the census does. It prints the store id and the sha256 of `index.json` and `coverage.bin`.
- **What it does:** it visits every covered origin once, builds its r = 1 square with the builder's own `makeFrame` and `squareBox`, and tests that square against every region with `boxMeets`.
- **What it counts:** the squares that meet **no county region**, and those that meet **no state region**.
- **Its self-check:** the number of origins visited must equal the sum of `coverage.bin`'s ranges.

| row | predicted, and what each result means |
|---|---|
| **R-SWEEP** on the sealed store | **Both counts 0** → recorded, and **the gap closes**. **Either count above 0** → the cells are **named**, and I **stop**: the builder then handles that case without throwing, as its own change with predictions, before D4 |
| **its control, C-SWEEP** | the same sweep with **Texas's state region (FIPS 48) removed** from an in-memory copy: **more than 0** squares meet no state region, **every one inside Texas's box**, and the county count is unchanged from R-SWEEP |
| **anything else** | an origin count that differs from the coverage file's own sum, a store it cannot read, or a control that does not turn: **BLIND, stop and report** |

**Where it lands:** in the D3 PR, before D4, so #505's head does not move again. These predictions are pushed with #505 first.

### The small-r sweep, run (2026-10-30): as predicted; the gap closes

**The script** is `scripts/basemap-sweep-small-r.ts` at `19e5261`. It was committed after these predictions (pushed in `b0aa078`) and run on the sealed store.

| row | result |
|---|---|
| **R-SWEEP** | **as predicted: both counts 0, so the gap closes.** Store `4c0875738f38`; `index.json` sha256 `63a9c1429db1` (the census's); `coverage.bin` sha256 `5cf7169484c2` (the sealed file's). **11,675,779 covered origins visited, equal to the coverage file's own sum.** All 10,270 coverage ranges agree with `covered()` at both ends. At r = 1 km, **0 squares meet no county region and 0 meet no state region**; exit 0 |
| **C-SWEEP** | **as predicted:** with Texas's state region removed from an in-memory copy, **515,962 squares meet no state region, every one inside Texas's box (0 outside)**, and the county count is unchanged at 0; exit 1 |

**What this establishes, with R-CENSUS:**
- **R-CENSUS:** every region carries the layers the builder reads from it.
- **R-SWEEP:** at the smallest square the route accepts, every accepted origin's square meets a region of each kind, and squares nest as r grows.
- **So on the sealed store,** `build()`'s throw on a requested layer with no files **cannot be reached through the route**. Only a synthetic store reaches it, which is now a reading rather than an argument.

**What would reopen it:** a new store, since the census and the sweep read this one by its sha256.

## #505: approved, merged; D2 deployed (2026-10-30)

### The ruling, verbatim (source: reviewer chat, pasted by Daniel, 2026-10-30)

> #505: approved at b0aa07876293.
> - The rows read as frozen: the full pass 50 of 50 with S-PWRITE exactly {MP2}; the census; R-SWEEP at r = 1 with 0 squares meeting no county or state region over all 11,675,779 covered origins, and C-SWEEP's control finding Texas's 515,962. build()'s throw is unreachable through the route on the sealed store, tied to its sha256; a new store reopens it, as you say.
> - I read the code against the rulings: the builder's compile-time stamp (makeBuilder, "UNSTAMPED" by default in every env, so an unstamped bundle refuses every build); deploy.sh --builder with the full 40-hex stamp, a dirty tree refused with no ALLOW_DIRTY, and its own output called the weak confirmation; /healthz's basemap from the last builder 200 only, null before, and builder_invalid on a bad commit header; the smoke's /basemap verdict through the proxy's own validate(), printing no coordinates.
> - One note for the record, no change: staging and production builders bind the same sealed bucket. That's fine for a read-only builder whose writes MP2 audits to zero, but it means staging must never get a writing build.
> - CI's total on that head: 38, of which 35 passed and 3 skipped. Main has moved by #506 only, with no file in common, and I checked that the squash applies cleanly. Merge with a clean squash, --match-head-commit, no --admin, the head named in the record. If the head moves, the approval lapses. Record main's CI on the merge commit.
>
> Then D2 (deploy.sh --builder: staging, then production), and D0 on production outside any replay window, with its smoke run and the carried commits listed first. Then the D3 PR with the sweep, and D4 after it. wx-ingest stays undeployed until R3.

**The note is carried as a rule:** staging and production builders bind the same sealed bucket, so **staging must never get a writing build.** MP2 is what holds every build to zero writes, and it runs on every PR touching the builder.

### The merge

- **Before merging:** the head was still `b0aa07876293`, main had moved by #506 only, and the squash applied cleanly to main.
- **The merge:** squashed as **`9f44a1a273b5`** at 04:11:34Z, with `--match-head-commit b0aa0787…` and no `--admin`. The merge message names the head and its readings.
- **Checked on main afterwards:**
  - `proxy/`, both docs and `workers.yml` are **byte-identical to the approved head**;
  - every other difference is #506's 35 files, byte-identical to #506's commit;
  - `feat/basemap-deploy` was deleted, with no PR based on it.
- **Main's CI on the merge commit:** **38 checks, all finished: 36 passed, 2 skipped.** All four workflows (workers, firmware, device-ids, no-traffic-captures) succeeded. A push to main skips a different set than a PR run.

### D2: the builder Worker, deployed (as predicted)

**Where from:** `scripts/deploy.sh --builder <env>`, run from a clean checkout detached at `9f44a1a`.
- **The preflight:** no `CLOUDFLARE_API_TOKEN` in the shell or at user level (both booleans). The OAuth login is on the pinned account, with `workers_scripts (write)`.
- **Main's moves:** main gained one commit (#507) between the merge and the deploy, touching nothing in `proxy/`, and `deploy.sh` said so without blocking.

| env | result |
|---|---|
| staging, 04:21:23Z | **as predicted:** stamped with the full `9f44a1a273b54a6890253c2126b17dc3a742c76c`; `env.STORE (blipscope-basemap) R2 Bucket`; no route ("No deploy targets"); exit 0. Version **`98800bfe-c74f-4d12-8616-b7f073deccec`**, listed by `wrangler deployments list` at 100% |
| production, 04:21:40Z | **as predicted:** the same stamp, binding and no route; exit 0. Version **`9ba96cf6-f1b0-4d92-ade3-dcf2f4d37e38`**, at 100% |

**What this proves, and what it does not (Q3):**
- **The weak confirmation is all D2 has.** The stamp and the binding come from the deploy's own output, and the version from Cloudflare's deployment list.
- **The proof is D3:** after one `/basemap` build through the bound staging proxy, `/healthz`'s `basemap.commit` must read `9f44a1a273b54a6890253c2126b17dc3a742c76c`.
- **Until then nothing can reach either builder.** Neither has a route, and no proxy binds them.

### D0's go, received after its deadline (2026-10-30)

**The ruling, verbatim (source: reviewer chat, pasted by Daniel, 2026-10-30):**

> D0 hasn't happened: production's /healthz still reads c06dc17, with no basemap field. If my "D0: go" from earlier didn't reach you, this is it: D0 now, as your plan and pre-registered rows say, finished before 05:30Z (10:30 PM PDT). E2:08 has a window from 06:00Z. Your smoke runs use E2:08's own credentials, and one starved E2:08's feed for 90 s today, so none may run inside an E2:08 window.

- **It reached me at 17:27Z,** twelve hours after its deadline. The earlier "D0: go" it mentions never reached this session.
- **D0 was not started:** the deadline had passed, and E2:08's window from 06:00Z has no end I can see. A smoke run inside it is ruled out.
- **D0 waits for a fresh slot.** Everything it needs is prepared, so the slot needs about 30 minutes.

### D0's slot, verbatim (source: reviewer chat, pasted by Daniel, 2026-10-30, ~17:40Z)

> D0: GO, in a fixed slot.
> - Slot: 20:00-21:30Z today (1:00-2:30 PM PDT). Start between 20:00Z and 20:55Z; finished, or rolled back, by 21:30Z.
> - If this reaches you after 20:55Z, or you can't start in time: the same slot on the next day, every day until D0 is done.
>   blipscope-5c keeps E2:08 out of any window from 19:45Z to 21:45Z every day until I lift it.
> - Run it as you listed: baseline smoke -> deploy.sh production with its /healthz confirmation -> the 15-minute watch -> the
>   post-deploy smoke. Predictions as you registered them. Anything off them: immediate rollback to c960f98f-..., then report.
> - Credentials for D0: the token file's pair (E2:08's), both values from the file, with the shell's own pair kept out of
>   the run. Say how you made sure of that.
> - Record each smoke run's start and end (UTC, to the minute) in the D0 record. Why: by the code, every smoke run writes
>   firmware changes into E2:08's fleet record. The /v1/enrich photo-variant check sends X-Blip-FW 6, then 7, under E2:08's
>   pair; index.ts runs recordFleetFirmware on every authenticated request; fleet.ts appends a change whenever the value
>   differs. The minutes let anyone reading that record later explain those entries.
>
> The shell's other pair: say where it comes from (Windows user variables, this session's own environment, or a profile
> script), and which it is: the old synthetic beefbeefbeefbeef, E2:08's id, or another id. Labels and booleans only, never a
> value. Don't use it.
>
> D3: carry on with its predictions (the staging binding, /credits, the sweep).
>
> AFTER D0 (D0 does not wait on this): a separate test identity for the production checks. Ruled by the reviewer.
> Why: smoke-prod.sh and watch-upstream.sh sign in as E2:08. Besides the fleet entries above, their requests count as E2:08's
> in the metrics, in its dev:<id> bucket, and in any "production records E2:08's /blips" read. A leak of the PC's copy could
> only be contained by revoking E2:08, because re-enrolment returns the same key (docs/bench-key-rotation.md, the 2026-08-31
> correction). This is the drift that doc's 2026-08-28 finding recorded. beefbeefbeefbeef can't come back: production
> refuses it (fake-id list, since 08-31), which is likely how the checks drifted back onto real boards' keys.
> Plan it as its own small PR, predictions first, each with a sabotage:
> 1. The id: fresh and random, in the device-id shape, not on the fake-id list, and not any board's (not derived from a real
>    MAC). It lives only in Daniel's token file: never committed, printed or logged.
> 2. The mint: name the route (the enrolment page, or the bench provisioning route). Give Daniel at most two plain steps for
>    any human part. The key goes straight into the token file. It never appears in a chat or in any tool's output.
> 3. The checks run only as the test identity. The token file names it in its own fields, and smoke-prod.sh and
>    watch-upstream.sh refuse any other pair, from the variables or the file, E2:08's included, before their first request.
>    Sabotage: E2:08's pair -> refused, 0 requests sent.
> 4. It is not a unit. The fleet records and the dashboard counts Daniel uses to estimate customer units leave it out,
>    without its id appearing in the public repo. Sabotage: exclusion removed -> the count rises by one.
> 5. E2:08's key leaves the PC. List every reader of the token file's blip-key/blip-device. If none needs E2:08's own key
>    once the checks move, say which two lines Daniel removes himself, and when.
> 6. Any server-side part deploys staged, in the reserved slot, and is verified after.

**The shell's other pair, as asked (labels and booleans only):**
- **Where it comes from: this session's own environment.**
  - `BLIP_KEY` and `BLIP_DEVICE` are absent at Windows machine level and at user level.
  - No bash profile, PowerShell profile, Claude Code settings file or VS Code settings file mentions either.
  - They are present only in this session's process.
  - **My inference, labelled as such:** they were inherited at launch from the user variables as they stood when VS Code started, before the pair moved to the token file.
- **Which id it is: the old synthetic `beefbeefbeefbeef`.** It is on `scripts/device-id-allowlist.txt` and in `proxy/src/fakeids.ts`, so production refuses it. It is **not** E2:08's id. Its key is 64 characters.
- **It is not used.**

**How D0's runs are kept to the token file's pair:** `d0-smoke.sh` (scratchpad).
- **What it does:** it reads both `blip-key` and `blip-device` from the token file, then launches `smoke-prod.sh` under `env -u BLIP_KEY -u BLIP_DEVICE BLIP_KEY=<file's> BLIP_DEVICE=<file's>`.
- **Its proof, before any request:** a child launched the same way sees the file's key and device (`yes`, `yes`) and not the shell's (`no`, `no`). Otherwise the runner refuses.
- **Its control:** the same runner, sabotaged to pass the shell's pair through, printed `REFUSED` and exited 2 with no request sent.
- **What it records:** each run's start and end, in UTC to the minute.

## The D3 PR: predictions, frozen before its code; two questions

**What the code will be:**
- **C1, `proxy/wrangler.toml`:** `[[env.staging.services]] binding = "BASEMAP", service = "blipscope-basemap-staging"`. Production gets no binding (that is D4's PR).
- **C2, `/credits`** (`renderCreditsHtml` in `photolicense.ts`) gains the basemap's lines beside the NOAA MRMS line:
  - the four US-government sources: TIGER/Line 2024, the 2025 Gazetteer, SUB-EST2024 and GNIS;
  - the terrain: AWS Terrain Tiles (Terrarium, zoom 9), **the four `CREDIT_BITS` texts verbatim**, and **SRTM's notice verbatim**.
  - Each line is HTML-escaped (the ETOPO1 line's `>` becomes `&gt;`).
- **C3, MA2,** a host row in `basemap-attribution.test.ts`. The credits page holds each `CREDIT_BITS` text and SRTM's notice, **read from `provenance.ts` and from `docs/basemap-attribution.md`** (the other side), not transcribed, and compared HTML-escaped.
- **The sweep** (`basemap-sweep-small-r.ts`) and its record, already on this branch.

| row | predicted |
|---|---|
| **MA2** | passes. **Its control:** the escaped ETOPO1 text contains `&gt;`, so the row cannot pass by matching the raw text |
| **S-ACREDIT** | the ETOPO1 line removed from the credits page: **exactly {MA2}** |
| **the full pass** | **51 of 51 AS PREDICTED** (the 50, plus S-ACREDIT); C-LOADFAIL AS PREDICTED; baselines ingest 10 in 1, mv 31 in 1, all 138 in 17; 0 runtime failures |
| **dry runs** | `wrangler deploy --dry-run --env staging` lists **`env.BASEMAP (blipscope-basemap-staging)`, a Worker**; `--env production` lists **no `BASEMAP`** |
| **D3, the deploy** (after merge, staged) | `deploy.sh staging`: its confirmation reads the stamp from staging's `/healthz`; `/healthz` `basemap` is **`{rev: 1, store: null, commit: null}`** before any build, which is MH2 live |
| **D3, the verification** | §9's **V1-V4**, then **MM2, MM4, MM5, MM6** as A1.7 froze them, through the staging proxy. **V1 is D2's proof:** `basemap.commit` must read `9f44a1a273b54a6890253c2126b17dc3a742c76c` |
| **D3's rollback rules** | §9's, unchanged. Any `500 builder_invalid` → roll back and stop. Builder `exceededCpu` or `exceededMemory` above 0 → production is not deployed until it is explained |

**Q-D3a. A staging credential: D3's verification has none.**
- **Why every row needs one:** V1-V4 and MM2-MM6 each need an authenticated `/basemap` request on staging, and V1 needs one build first.
- **Why E2:08's cannot be it:** **its credential is refused on staging by design**, because staging holds a different device-key secret (`wx-server.md`, P4: every FAIL there was a 401). The token file holds no staging pair.
- **Recommended:** the test identity ruled for after D0 is minted **on staging as well as production**, through the same route, and D3's checks run as it. The D3 PR can be reviewed and merged before that; **D3's deploy and verification wait for the credential.**
- **The alternative, not recommended:** a separate staging-only identity. It is a second identity to keep and exclude, for no gain.

**Q-D3b. The smoke's `/credits` half moves to D4's PR.**
- **Why:** `smoke-prod.sh` runs against production. Production carries the new `/credits` lines only once a production deploy includes the D3 code, which is D4's.
- **What would happen otherwise:** the half's five checks would read FAIL on every production smoke between D3 and D4. A permanently red check gets read past.
- **Recommended:** it lands with the production binding, in the D4 PR. MA2 holds the page's content in CI meanwhile.

**The order, if Q-D3a and Q-D3b are ruled as recommended:**
1. D0, in its slot.
2. The test-identity PR, minted on staging and production.
3. The D3 PR: review and merge.
4. D3's deploy and verification, as the test identity.
5. The D4 PR, with the smoke's `/credits` half.
6. D4, outside any replay window.

### Rulings before D0, verbatim (source: reviewer chat, pasted by Daniel, 2026-10-30 ~18:15Z and ~18:25Z)

> D0: go at 20:00Z as you laid out, with one change before it starts and one correction to the record.
>
> 1. No credential value on any command line. As described, d0-smoke.sh hands the file's values to `env` as NAME=value arguments. A value in an argument is in that process's command line, readable by any process listing for as long as the process lives (on Windows that can be the whole smoke run). Another session listing processes would print it, which is how the 2026-08-28 leak happened.
>    - Use `env -u BLIP_KEY -u BLIP_DEVICE` with no values, and let main's smoke-prod.sh read the token file itself (#500's path). Exporting the values inside a subshell also works.
>    - Re-run the proof and its control that way, booleans only.
>    - smoke-prod.sh's own curl calls still pass the key as a -H argument, briefly, on every request. That is accepted for D0 and fixed in the test-identity PR (item 7 below).
> 2. The record: "since then main has gained only non-proxy/ commits" isn't exact. One proxy/ commit has landed since 9f44a1a: #500 (d256ff0), which changes only scripts and a test (the token-file readers, smoke-prod.sh, watch-upstream.sh, confirm-deploy.sh's hint line). Nothing the Worker builds from has changed (proxy/src, wrangler.toml, the package files), so the Worker built from 9f44a1a equals main's. Record it that way, and say which tree's smoke-prod.sh ran.
>
> The shell's other pair: accepted as labelled. It stays unused and goes away when VS Code restarts.
>
> Q-D3a: as recommended. The test-identity PR mints the one identity on staging and on production, through the same route, and D3's deploy and verification run as it. The test-identity PR and the D3 PR go in parallel, each reviewed on its own. D3's deploy waits for the staging credential.
>
> Q-D3b: as recommended. The smoke's /credits half lands in D4's PR; MA2 holds the page in CI until then.
>
> The order: D0 -> (the test-identity PR and the D3 PR, in parallel) -> D3's deploy and verification, as the test identity -> the D4 PR with the /credits half -> D4, in the reserved slot, with its smokes run as the test identity.
>
> Added to the test-identity PR:
> 7. No credential on a command line. Today each curl gets the key as -H "X-Blip-Key: ...". Move it to stdin or a file descriptor (curl -H @- or -K -). Check: during a run, scan every process's command line in memory, printing only a boolean (false). Control: the old way prints true.
> 8. Staging: say which route mints there, and whether staging's enrolment needs a human step at all.

> The deploy: (b). Deploy from main's tip with no escape hatch, so the deploy and the smoke share one tree.
> - Re-registered before the run (not a miss): /healthz reports main's tip, as recorded immediately before deploy.sh runs, instead of 9f44a1a. The record shows that tip, and `git diff --stat 9f44a1a <tip>` reading 0 files over everything the Worker is built from: proxy/src, proxy/wrangler.toml, the package files, and the generators deploy.sh runs (embed-*.mjs).
> - If main moves after you record the tip (#510 may land before 20:00Z; it touches test/host only), re-record and re-check before deploying. If any of those Worker inputs differs from 9f44a1a, stop: that isn't D0.
> - Everything else as pre-registered, including the rollback target c960f98f-...
>
> The runner: accepted as rerun (file yes/yes, shell no/no; the control flips and refuses with exit 2; no request sent).
>
> For the test-identity PR, item 5: blipscope-5c's production-record reader (the one its RP rows use) now reads E2:08's device id from the token file. It needs E2:08's id, not its key. So the key can leave the PC while the id stays, under a field name that says it identifies whose records to read and is not a credential. List it with the other readers.

**Carried out before the slot:**
- **The runner is `d0-smoke2.sh`** (scratchpad), run from a clean checkout of main's tree.
  - It strips the shell's pair with `unset` inside a subshell, and sets `BLIPSCOPE_TOKEN_FILE` (a path).
  - It lets #500's `blip-creds.sh` re-run the smoke through `with-blip-creds.mts`, which passes the file's pair in the child's environment only.
  - **Its proof:** a probe down the same path reads `file-key=yes file-dev=yes shell-key=no shell-dev=no`.
  - **Its control:** with the shell's pair kept, it reads `file-key=no file-dev=no shell-key=yes shell-dev=yes` and refuses with exit 2. Neither sent a request.
- **The Worker's inputs, as checked:** `proxy/src`, `proxy/wrangler.toml`, `proxy/package.json`, `proxy/package-lock.json`, `proxy/tsconfig.json`.
  - **`deploy.sh` runs no generator.** Its four embed generators run by hand, and their outputs are committed under `proxy/src/*.generated.ts`, held by CI's `--check` steps.
  - **The check covers them anyway:** the four `embed-*.mjs` scripts and everything they read (`proxy/pages`, `proxy/fonts`, `include/DeviceIdentity.h`, `scripts/device-id-allowlist.txt`).
  - **Every path in the set exists,** so an empty diff cannot come from a mistyped path.
- **At 18:2xZ, main's tip was `bea882ad4475`:** `git diff --stat 9f44a1a bea882ad4475` over that set reads **0 files**. It is re-recorded and re-checked immediately before `deploy.sh` runs.

## The D3 PR, run: S-ANOTICE off-prediction; stopped for a ruling (2026-10-30)

**The code is `edbc5b2`.**

| row | result |
|---|---|
| MA1, MA2 | **as predicted:** both pass |
| typechecks | both exit 0 |
| dry runs | **as predicted:** staging lists `env.BASEMAP (blipscope-basemap-staging)`, a Worker; production lists no `BASEMAP` |
| the full pass, 18:23Z to 18:58Z | **50 of 51 AS PREDICTED**, including **S-ACREDIT {MA2}**. Baselines ingest 10 in 1, mv 31 in 1, all 138 in 17; C-LOADFAIL AS PREDICTED; 55 runs, 0 runtime failures |
| **S-ANOTICE** | **OFF-PREDICTION:** failing **{MA1, MA2}** against its frozen {MA1} |

**The cause:** S-ANOTICE deletes SRTM's notice from `docs/basemap-attribution.md`, and MA2, as written, **reads the notice from that file**.
- **So MA2 loses its input:** confirmed by planting S-ANOTICE alone, which gives `the attribution file's notice line: expected undefined to be defined` (the doc restored from a byte copy, sha256 checked).
- **MA2 fails for its input, not for the page.**
- **The A2.16 re-check should have caught it** when MA2 was added. I did not run S-ANOTICE against MA2's input. That is the same miss as S-PWRITE.

**The alternative, and why the data excludes it:** that the page lost the notice.
- S-ANOTICE edits only the doc.
- The page's notice is `SRTM_NOTICE` in `photolicense.ts`, untouched.
- The failure message names the doc's line.

**Proposed fix** (committed locally, **not pushed** until a ruling):
- **The change:** MA2 takes the notice from **MA1's own `NOTICE` constant**, the one transcription both rows share.
- **Its effect:** MA1 holds the file to it, MA2 holds the page to it. Each row tests its own subject, and S-ANOTICE stays {MA1}.
- **The four credits are unchanged:** they still come from the builder's `CREDIT_BITS`.
- **The alternative, not recommended:** re-registering S-ANOTICE as {MA1, MA2}. It would grade "MA2's input is missing" as a reading about the page, which is the kind of extra failure ruled out for S-PWRITE.

**Frozen predictions for the fix, if ruled:**

| row | predicted |
|---|---|
| S-ANOTICE | **exactly {MA1}** |
| S-ACREDIT | **exactly {MA2}** |
| MA1, MA2 | both pass |
| the full pass | **51 of 51 AS PREDICTED**, C-LOADFAIL AS PREDICTED, 0 runtime failures |
| CI on the PR's head | every check finished, none failing |

## D0: done in its slot, every prediction held (2026-10-30, 20:00-20:19Z)

**The tree:** main's tip, recorded at 20:00:38Z and again just before `deploy.sh`, was **`31cb44c5376b`** (#510, test/host only, landed after `bea882a`).
- `git diff --stat 9f44a1a 31cb44c` over the Worker's inputs (`proxy/src`, `proxy/wrangler.toml`, `proxy/package.json`, `proxy/package-lock.json`, `proxy/tsconfig.json`, the four `embed-*.mjs` and everything they read) reads **0 files**.
- **The one `proxy/` commit since `9f44a1a`** is #500 (`d256ff0`), scripts and a test only. So the Worker built from `31cb44c` equals the one built from `9f44a1a`.
- **The deploy and both smoke runs used that one tree:** a clean checkout of `31cb44c`, with `smoke-prod.sh` from it.

**Credentials:** both smoke runs went through `d0-smoke2.sh`. The shell's pair was removed with `unset` in a subshell, and #500's wrapper passed the token file's pair through the child's environment only. Its proof read `file-key=yes file-dev=yes shell-key=no shell-dev=no` before each run. No credential value was on any command line of mine (smoke-prod.sh's own `curl -H` aside, accepted for D0).

| step | time (UTC) | result |
|---|---|---|
| **the baseline smoke** | **20:00Z to 20:01Z** | PASS 34, FAIL 2, SKIPPED 1. FAILs: `/basemap` 404 (the route predates production) and `/healthz` `wx.opuk` (the Worker predates opuk). SKIPPED: `/v1/photo` (the live aircraft drawn had no stock photo) |
| **the expectation, written before the deploy** | 20:01:45Z | after D0: `/basemap` PASS (inert); `wx.opuk` PASS ("not ingesting yet": #499 carried, `wx-ingest` undeployed); `/v1/photo` PASS or SKIP by the aircraft drawn; every other check as the baseline. So 36/0/1 or 37/0/0 |
| **`deploy.sh production`** | 20:01:57Z to 20:02:08Z | exit 0; version **`2eb4aa5e-512a-4a8a-b586-8a77bfdfca4e`**, at 100% per `wrangler deployments list`; no `BASEMAP` binding listed; **`confirm-deploy`: `/healthz` reports `commit=31cb44c`**, as re-registered |
| **`/healthz` after** | 20:02Z | `commit: 31cb44c`, **`basemap: "not_configured"`**, `wx.opuk: {"frameAgeS":null,"coveredShare":null}` |
| **the watch, [20:02:08Z, 20:17:08Z)** | see below | **0 fleet 5xx, 0 401, 0 403** |
| **the post-deploy smoke** | **20:17Z to 20:17Z** | **PASS 37, FAIL 0, SKIPPED 0.** The only verdicts that changed from the baseline: `wx.opuk` FAIL → PASS ("not ingesting yet"), `/basemap` FAIL → PASS (`503 not_configured`, the inert answer), `/v1/photo` SKIPPED → PASS. Exactly the expectation written before the deploy |

**The watch, honestly.** The production tail closed three times on its own, with nothing on its stderr but the config warning, so the filter ended each time on end-of-stream. I restarted it each time.
- **Observed: 806 s of 900, in three spans,** all KEEP:
  - 20:02:17-20:05:55Z, 313 events;
  - 20:06:16-20:14:08Z, 633 events;
  - 20:14:25-20:16:21Z, 127 events.
- **Totals:** **1,073 events, 0 5xx, 0 401, 0 403.** One flagged event was a client-cancelled 200 on `/missileer/archive`.
- **Unobserved by the tail: 94 s,** at the start, between restarts and at the end.

**The second instrument closes the gaps.**
- **Why it can:** the proxy's own `METRICS` dataset records every request at status ≥ 400 unsampled (`metrics.ts`, `record()`).
- **The query:** over exactly [20:02:08Z, 20:17:08Z), route-indexed points only, counts and statuses only (never a blob). It read **701 points, 1,340 requests, 0 at 5xx, 401 or 403, and 0 `err5xx` points**.
- **Its control:** the same query over the two smoke windows finds the smokes' own refusals. The baseline's window has 2 × 403; the post-deploy window has 2 × 403 and 1 × 503, that smoke's own `/basemap` `not_configured`. So it can see a 403 and a 503.
- **A second control fired as designed:** its list of non-route families showed one I had not excluded (`sitefeed`, 78 points), so the counts were re-run on route-indexed points only.

**Two transient things, recorded:**
- **The watch's three disconnects,** above.
- **One `wrangler deployments list` failure:** Cloudflare's `internal server error [code: 10500]` on an older version's details. The retry read the active version at 100%.

**Rollback:** not needed. The target stays `c960f98f-6654-4c1d-bb58-3e981c77a271`.

**What D0 established:** production serves `/basemap` as the inert `503 not_configured` and reports `basemap: "not_configured"`. The route is in place and nothing can reach a builder until D4 binds one.

### D0 accepted; S-ANOTICE re-registered (source: reviewer chat, pasted by Daniel, 2026-10-30 ~21:05Z)

> D0: accepted as done, every prediction held.
> - Production is 31cb44c (version 2eb4aa5e-512a-4a8a-b586-8a77bfdfca4e), with no BASEMAP binding. The rollback target
>   stays c960f98f-... (c06dc17).
> - The watch's coverage is accepted as stated: 806 of 900 s by tail, and the 94 s closed by the proxy's own unsampled
>   >=400 metrics, whose control saw the smokes' own refusals. Record both, and the three tail reconnects.
> - The daily slot hold is lifted. D4 gets its own slot when its PR is approved.
>
> S-ANOTICE: your question hasn't reached me. Reading edbc5b2: MA2 reads SRTM's notice out of docs/basemap-attribution.md,
> so S-ANOTICE (the notice deleted from that file) now fails MA1 and MA2.
> - If that is the stop: re-register S-ANOTICE as {MA1, MA2}. Keep the original {MA1} and the miss on record, with the
>   reason: MA2 reads the notice from the file by design (both sides).
> - The lesson also goes on record: adding MA2 needed the A2.16 re-check of every frozen set before the full pass ran, and
>   that check would have caught this. Run it for every new row from now on.
> - If it is anything else, stop and send it to me.
>
> Then: the D0 record goes up with that change, the full pass goes with the D3 PR's approval request, and the
> test-identity PR runs in parallel.

**D0's coverage, as accepted:** 806 of 900 s by tail, in three spans, with **three tail reconnects** (20:05:55Z to 20:06:16Z, 20:14:08Z to 20:14:25Z, and the end at 20:16:21Z). The remaining 94 s were closed by `METRICS`' unsampled ≥ 400 points, whose control saw the smokes' own refusals. Both are recorded in the D0 section above.

**S-ANOTICE: it is that stop.**
- **The set is re-registered as `{MA1, MA2}`** in `sabotage-basemap.mjs`, with the reason beside it: MA2 reads the notice from the file, by design (both sides).
- **The original `{MA1}` and the miss stay on record** in the run section above.
- **My proposed fix is superseded.** It had MA2 take the notice from MA1's constant. It was committed locally and never pushed; it was dropped from the branch by this ruling. MA2 reads the notice from `docs/basemap-attribution.md`, as at `edbc5b2`.

**The lesson, as ruled:** adding MA2 needed the A2.16 re-check of every frozen set **before the full pass ran**, and that check would have caught this. **It runs for every new row from now on.** For MA2 it is now done by reading MA2's setup, not by reasoning:
- **MA2's inputs are three files:** `src/basemap/provenance.ts` (`CREDIT_BITS`), `docs/basemap-attribution.md` (the notice line) and `src/photolicense.ts` (the page).
- **Of the harness's 52 entries, three edit one of them:**
  - **S-ANOTICE** deletes the notice line: **{MA1, MA2}**, re-registered.
  - **S-MREV** edits `"4x4 mean of z9 pixels; "`, which is in `LEVELS_TEXT` (`provenance.ts` line 21), not in `CREDIT_BITS` (lines 14-19), so MA2 does not read it: it stays **{MA1, MD4, MG0b}**.
  - **S-ACREDIT** edits the page: **{MA2}**.
- **No other entry touches an MA2 input.**

**Predicted for the full pass on the pushed head:** **51 of 51 AS PREDICTED**, with S-ANOTICE {MA1, MA2}, S-ACREDIT {MA2} and S-MREV {MA1, MD4, MG0b}. C-LOADFAIL AS PREDICTED; baselines ingest 10 in 1, mv 31 in 1, all 138 in 17; 0 runtime failures.
