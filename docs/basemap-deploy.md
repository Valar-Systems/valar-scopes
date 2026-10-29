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
