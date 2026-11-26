# D4: the map on in production

**Status:** a plan, frozen before D4 runs. D4 changes production only on the owner's OK, given by message. Nothing here has run.
It builds on [basemap-deploy.md](basemap-deploy.md) (the D4 row), [basemap-d4-plan.md](basemap-d4-plan.md) (the prerequisites
plan of 2026-11-05) and [basemap-a1.md](basemap-a1.md) (the stored packages and their 30-day rule).

## What D4 changes in production

- **The builder,** `blipscope-basemap`, is redeployed from D4's commit. It has no route: only the proxy's binding reaches it.
- **The proxy gains two bindings,** in this PR: `BASEMAP` (the builder) and `BASEMAP_PKG` (the bucket `blipscope-basemap-pkg`).
  **The bindings are the switch.** Without them, `/basemap` answers `503 not_configured`, as it has since D0.
- **The secret `BASEMAP_NAME_KEY`** (K1), put on first. A package bucket without it is treated as no bucket, with no error.
- **The bucket `blipscope-basemap-pkg`,** with the lifecycle rule `bm-pkg-30d` (a package is deleted 30 days after upload),
  checked by `proxy/scripts/check-pkg-lifecycle.mjs`.
- **One KV key** in production's `ENRICH_KV`: `bm:store = 33d6dc41c749`, the census store already verified on staging.
- **Not changed:** the census store and its pointer `current`. Both builders read the one sealed bucket `blipscope-basemap`, and
  `current` already names `33d6dc41c749`.
- **The builder's logs in production:** none (the owner's choice A). Its own build line (B) is written but left off. Switching
  it on is one line and a builder deploy, on the owner's word only.

## Prerequisites

Each is printed at D0. If any one is missing, there is no D4.

1. **Row 5 on staging,** as predicted (done 2026-11-24).
2. **D3's end and its result,** read and accepted. D3 is the map client's memory check on staging. If D3 fails, a memory failure
   above all, D4 is re-planned around that result.
3. **The deploy-path pin** (#755) on main: `deploy.sh` refuses any commit but the plan's.
4. **(B), the builder's logs block** (#790), on main and then proven on staging in its own slot, after the staging hold lifts.
5. **This PR,** approved at its head. It merges inside D4's window, as the window's first write (S4-0).
6. **The post-D4 logs read,** registered: a 60-minute window with a staging trigger inside it, read twice. The production
   builder's commit comes from the deployed stamp and S4-7's fresh build, never from `/healthz`.
7. **D4's budget line:** the reads, rows and table that [account-budget.md](account-budget.md) section 12 registered, done
   before the owner's message. This PR registers the product `basemap-production` in state `off` and names the bucket and the
   builder in the budget's reader (see "The budget" below).
8. **The owner's OK, as a message.** Production cannot be rehearsed before its own switch-on, so the OK is a message, not a
   sitting. The OK names the day.
9. **The booking:** outside any replay window and any session that grades production, with one production deployer under the
   deploy claim. The latest start, cutoff and rollback bound are set at booking, and cannot be set before S4-0b's time is
   measured (below).

## D0, read-only

Printed whole. Any line other than predicted means no deploy, and it is reported.

- HEAD is D4's commit, equal to the remote's, with a clean tree. The deploy pin's checks exit 0 for both the proxy and the builder.
- Production `/healthz`: its commit is production's current proxy, and `basemap` reads `"not_configured"`.
- The shared `current`: byte-identical to row 5's 26-byte body.
- Production's `bm:store` (predicted absent), the secret's name (a boolean, predicted absent) and the bucket by name (predicted
  absent).
- The rollback targets, read before any write: P0 and B0, the newest production versions of the proxy and the builder, each one
  version at 100%, from `deployments list`.

## The slot

Production only. Staging is not touched.

- **S4-0, the merge.** This PR is merged by the merge rule at its approved head. Main is re-read in the merge command, and the
  squash's parent and tree are checked.
- **S4-0b, the reference packages.**
  - The fresh-build proof (S4-7) compares each package with a reference built offline and stamped with D4's commit.
  - That commit is the squash made at S4-0, so the references can only be built after S4-0.
  - They must be finished before S4-7. They may overlap S4-1 to S4-6, since building them writes nothing in production.
  - Their sha list is printed, and acknowledged by sha.
  - **Its time is a stated unknown.** It is measured on staging first, once the staging hold lifts, and the booking's bounds are
    set from that measurement.
- **S4-1, the secret.** 32 random bytes go to `BASEMAP_NAME_KEY` on stdin, never printed and never in a file. Then its name is
  read as present, a boolean.
- **S4-2, the bucket.** Created, its lifecycle rule `bm-pkg-30d` added, and `check-pkg-lifecycle.mjs` reads OK.
- **S4-3, the store key.** `bm:store = 33d6dc41c749`, read twice at least 60 seconds apart.
- **S4-4, the builder.** `deploy.sh --builder production`, pinned to D4's commit. It is still unreachable.
- **S4-5, the proxy.** `deploy.sh production`, pinned. Its confirmation reads `/healthz`'s commit as D4's. **This is the switch.**
- **S4-6, a wait** of at least 60 seconds, the store key's cache life.
- **S4-7, the fresh-build proof** (D4-R1, D4-R4), as the production test identity, at the registered points:
  - attempt 1 is `202` at every point, because the bucket is new and empty and the cache cold; then `200`;
  - each package's sha256 equals S4-0b's reference;
  - the ETag and PROVENANCE name the store `33d6dc41c749`;
  - the builder line names D4's commit in all 40 hex, graded with its controls.

  A first-attempt `200` at a point nobody has asked production for is outside the set.
- **S4-8, V1 to V4,** from [basemap-route.md](basemap-route.md)'s D3 row, re-based for production:
  - **V1:** `/healthz`'s `basemap` block, as information only (D4-R3). Nulls are counted. A non-null store or commit other than
    predicted, on an isolate whose top-level commit is D4's, is a STOP.
  - **V2:** the smoke's `/basemap` check passes "wired".
  - **V3:** the same request with `If-None-Match` returns `304`.
  - **V4:** EGLL returns `404 no_coverage`.
- **S4-9, the production smoke** as the test identity: its `/basemap` check, and its `/credits` checks once they are on main.
- **S4-10, the cold-build wall,** read after the fact from the analytics: the walls of S4-7's builds, their p95 against the
  prerequisites plan's 2.5 s target, and the device's 5 s wait beside it. **It is information.** Above the target, it is recorded
  and carried to the map client, whose fetch timeout it bears on. It never triggers a rollback.

### The rules the slot quotes ([basemap-dense-code.md](basemap-dense-code.md))

- **D4-R1.** The bound builder is proven ONLY by the builder line of a package from a FRESH build in D4's window (attempt 1 =
  202), equal to the deployed stamp in all 40 hex. Never by `/healthz` `basemap.commit`.
- **D4-R2.** The builder's rollback target is read before the deploy from `deployments list`. Never from `/healthz`, which reads
  null on any isolate that has not built.
- **D4-R3.** `/healthz`'s basemap block is information: nulls counted; a non-null store or commit other than expected, on an
  isolate whose top-level commit is the new proxy, is a STOP.
- **D4-R4.** Production has no binding until D4, so its first builds are fresh by construction. The attempt-1 202 is still
  graded, because a stored or cached answer is exactly the case the rule exists for.

## Outcomes

- **S4-1 to S4-9 all as predicted:** the map is on in production; production and staging both read store `33d6dc41c749`; the
  slot ends.
- **Any line inside the slot other than predicted:** STOP, then the rollback.
- **Anything else:** outside the set. STOP and decide.

## The rollback

In order, each step read back:

1. **R-1, the proxy back to P0.** The binding goes: `/healthz` reads `"not_configured"` and `/basemap` answers
   `503 not_configured`, read three times.
2. **R-2, the builder back to B0.**
3. **R-3, production's `bm:store` deleted,** read back absent.

The secret, the bucket and its packages stay in place, unbound and inert; removing them is the owner's call. The shared `current`
is **not** touched, because staging reads it too. The merged bindings stay on main after a rollback, so production proxy deploys
from main hold until a revert PR merges.

## After the slot

- **The post-D4 logs read,** at its window's close plus 10 and plus 20 minutes, with its staging trigger inside the window.
- **V6, the fleet's own traffic.**
  - **The instrument:** the analytics dataset, sample-weighted. The fleet is points carrying a device id. The bench board and the
    test identity are excluded, and neither is ever printed. Two additions are registered before D4, each with its selftest and
    red first: `/basemap`'s fleet count, and devices in B but not in A, printed as a count only.
  - **The windows:** T is S4-5's deploy exit, B = [T - 60 min, T) and A = [T + 5 min, T + 65 min). Each is read twice, at A's close
    plus 10 and plus 20 minutes, and compared row by row. A value that moves between the reads is outside the set.
  - **The controls,** read before V6 is believed:
    - V6-C1: B's fleet requests are above 0, and B has at least 4 devices;
    - V6-C2: "devices in B and not in an empty window" equals B's device count;
    - V6-C3: unfiltered 4xx over all production points in the 24 hours before T is above 0.

    Any control off means V6 is BLIND: no verdict, a STOP.
  - **The rows:**
    - V6-1: fleet 5xx in A is 0;
    - V6-2: fleet 4xx in A is 0, with B's printed beside it;
    - V6-3: devices in B but not in A is 0;
    - V6-4: fleet `/blips`, A over B, is 0.4 to 2.5;
    - V6-5: fleet `/basemap` requests in A are 0, graded in A only, because before D4's proxy `/basemap` was counted under
      "/other".
  - **Fail fast, inside the slot:** fleet 5xx, or fleet 401/403, in [T, T + 15 min), read at T + 20 min. Any above 0 means R-1 at once.
  - **The outcomes:**
    - V6-1 above 0, or any fleet 401/403 in A: R-1 at once, then STOP.
    - V6-2 above 0 with B's 4xx at 0: STOP; R-1 on the orchestrator's word.
    - V6-3 above 0: STOP, NOT an automatic rollback. A device going quiet for its own reasons (power, Wi-Fi, a customer) looks
      the same, and D4 changes no route a shipped device calls.
    - V6-4 outside its band, or V6-5 above 0: STOP.
    - Anything else (a control off, the reads disagreeing, B's 4xx above 0, a crash): STOP, no verdict.
  - **The deploy claim** is held until V6's second read.

## Couplings, named where they live

- **One `current` for both builders.** After D4, any write to `current`, including a staging rollback, changes production's
  builder too. Every later registration that writes `current` names production.
- **The bindings on main switch the map on** for any production proxy deploy from main. That is why this PR merges inside D4's
  window.
- **`/healthz`'s basemap block is per isolate:** information, never proof, never a rollback target.
- **A device's 5 s wait against a cold build** is the map client's requirement. D4 reports S4-10's walls beside it and changes
  nothing in the device.

## The budget

This PR registers the product `basemap-production` (states `off`, `on-before-v20`, `on-v20`) in state `off`, with no rows.
- The budget's reader (`tools/budget/budget_read.py`) now attributes the production builder and the package bucket to it.
- `scripts/check_budget_names.py` fails CI if any R2 bucket or Worker named in the wrangler configs has no budget row.
- Section 12's reads and rows, which give the figure, are a separate prerequisite (item 7).

## The owner's message

Sent before the slot. The owner answers in words. The cost figure comes from section 12's line, with its source line, as a range.
It is never typed from memory.

> D4 switches the map on in production. It adds these production resources: (1) a new storage bucket,
> blipscope-basemap-pkg, for built map packages, with a rule that deletes each package 30 days after it is stored; (2) a new
> secret, BASEMAP_NAME_KEY: random bytes nobody sees, so a stored package's name can't reveal the place it shows; (3) one
> setting, bm:store = 33d6dc41c749: the census map store, already verified on staging; (4) the production server's link to the
> map builder. That link is the switch. The builder keeps no logs in production (your choice A). Cost: [section 12's line].
> One thing changes for good: the map store's pointer is shared by staging and production, so after D4 every change to it is
> planned as a production change. If anything is wrong, the rollback removes the link in one step and devices get today's
> 'no map' answer. Reply 'D4: yes, <day>' or 'D4: no'.
