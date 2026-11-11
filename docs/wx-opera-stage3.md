# OPERA beyond the UK, stage 3: the ten ingests to production (predictions first, 2026-11-11)

**For:** the reviewer chat. **Status:** predictions, frozen before anything deploys. **Nothing has deployed.** It
deploys only after review, after #635 merges ahead of it in the order, and after an announcement through Daniel.

## The ruling (reviewer chat, pasted by Daniel, 2026-11-11 ~05:49Z), verbatim

> Q-LR is closed by Daniel's decision, made on your table. (a) doesn't get under 1M with per-product Workers, and (b)
> needs a proxy deploy after v19 for only ~6% headroom, which Missileer's growth will use up. Neither change is made. No
> KV move, and cref stays as it is.
> - The ten deploy as they stand, through ingest_deploy.sh, each under its own claim. Predictions first, announced
>   before.
> - After the deploy, read the whole account's R2 Class A and report the monthly run rate.
> - Daniel checks his first invoice. If Cloudflare bills the Class A overage as a whole million ($4.50 a month), it
>   comes back. If it bills proportionally (about $0.20), it stays.

The rest of the rollout stays as `docs/wx-opera-europe.md` §8 item 4 froze it: "each ingest Worker first, its `latest`
read, then the proxy's dispatch; one deployer at a time, rollback targets recorded first." **This stage is the ingests
only.** No product joins `ROUTED` here, so **no device's answer changes** (S3-6 checks that). Each product joins `ROUTED`
later, in a proxy deploy of its own (Q-ROUTED (a)'s third condition), registered separately.

## 1. What deploys, and in what order

The ten are `PRODUCTS`' OPERA rows on main (`proxy/src/wx/products.ts`), each `[env.<id>]` of
`proxy/wx-ingest-opera.wrangler.toml`, deployed by `tools/wx/ingest_deploy.sh` from a clean checkout of main.

| order | product | Worker | cells (M) | single-build median, expected | upper bound (x 1.6) |
|---|---|---|---|---|---|
| 1 | `opmt` | blipscope-wx-ingest-opmt (**new**) | 0.19 | 95 ms (M2, measured) | **152 ms** |
| 2 | `opsw` | blipscope-wx-ingest-opsw (**new**) | 2.06 | 257 ms (M6's fit) | **411 ms** |
| 3 | `opce` | blipscope-wx-ingest-opce (**new**) | 2.16 | 265 ms (fit) | **425 ms** |
| 4 | `opib` | blipscope-wx-ingest-opib (**new**) | 2.29 | 277 ms (fit) | **443 ms** |
| 5 | `opis` | blipscope-wx-ingest-opis (**new**) | 2.44 | 290 ms (fit) | **465 ms** |
| 6 | `opse` | blipscope-wx-ingest-opse (**new**) | 2.53 | 298 ms (fit) | **477 ms** |
| 7 | `opba` | blipscope-wx-ingest-opba (**new**) | 2.58 | 303 ms (fit) | **484 ms** |
| 8 | `opwe` | blipscope-wx-ingest-opwe (**new**) | 3.22 | 314.5 ms (M1, measured) | **503 ms** |
| 9 | `opsn` | blipscope-wx-ingest-opsn (**new**) | 3.46 | 405 ms (M1, measured) | **648 ms** |
| 10 | `opuk` | blipscope-wx-ingest-opuk (**redeployed**) | 3.10 | 364.5 ms (M1's `opux`, the same isles grid, measured) | **583 ms** |

- **Smallest first, `opuk` last,** because only `opuk` is live: its frame is read today by every UK point.
- **`opuk`'s redeploy changes it:** main's `opuk` row builds on #623's isles grid. That is 1,954 x 1,585 cells, west to
  15.71 W, where production's 4da941e4 builds on the 1,455 x 1,515 fixture grid. The isles rule's points need the
  margin, for instance western Ireland's crops. Its `X-Wx-Max-Age` (720) and its rule are unchanged.
- **The upper bounds are expected x 1.6, with no floor,** the form M1 was re-registered in (2026-11-07). The fit is
  M6's (76.9 ms + 87.4 ms per million cells). Where the M stage measured a product, its measured median is used.

**The new Workers, announced before they exist, with their expected monthly cost** (`docs/wx-account-cost.md` §6).
Together the ten add **$0.97-$1.19 a month** at today's fleet, Read P. The cost is mostly CPU (about 2.6-3.5 M ms a
product), plus 17,280 Class A writes a product. Under Read A they add **$5.59**, because they take the account's
Class A past 1 M. That is the case Daniel checks on his first invoice.

## 2. Per product, in order: one claim, one deploy, one read-back

```sh
BLIPSCOPE_SESSION=uk-data python scripts/session-claim.py take deploy --covers wx-ingest-<id>:production --until +20m
BLIPSCOPE_SESSION=uk-data bash tools/wx/ingest_deploy.sh wx-ingest-opera.wrangler.toml <id>
# S3-1 and S3-2 below, then:
BLIPSCOPE_SESSION=uk-data python scripts/session-claim.py release deploy
```

The next product starts only after S3-2 has read for this one.

| row | what | predicted |
|---|---|---|
| **S3-0** (before, the rollback target) | `npx wrangler deployments status -c wx-ingest-opera.wrangler.toml -e <id>` | a new product: **no Worker** (the API's not-found, 10007). That is the control that the Worker is new, and its stop is its `-inert` env. `opuk`: **one version, `4da941e4`, at 100 %**, recorded in full as `md_rollback.sh`'s target (`OPUK_ROLLBACK_TO`) |
| **S3-1** (the read-back) | `ingest_deploy.sh`'s own read-back, and `npx wrangler versions view <new id> -c ... -e <id> --json` | **exit 0**; one version at 100 %; bindings exactly `WX` -> blipscope-wx; vars `PRODUCT` = `<id>` and `WX_INGEST` = "on"; cron `* * * * *`; `cpu_ms` 12000 |
| **S3-2** (the first storing check, and `latest`) | `r3p.py lines blipscope-wx-ingest-<id>` from the deploy, at ABR 1; then `r3_list.py wx/<id>/` | a storing check (`stored` or `backfilled`) **within 15 minutes** of the deploy. That is the propagation time R3′ measured for a cron. Its line carries `pruneKeys` 900 or 899. **`wx/<id>/latest` exists** and names a frame `r3_list.py` lists |

## 3. After the tenth, read together

Each product's window runs from its deploy for 70 minutes. Every Logs read is in 15-minute pieces at ABR 1, as ruled
for R3′. A sampled read is re-read once over a narrower window. A second sampled read is BLIND, and BLIND fails the row.

| row | what | predicted |
|---|---|---|
| **S3-3** (CPU) | `r3p.py cpu blipscope-wx-ingest-<id> <id>` over its 70 minutes | single-build median **at or under its bound in §1**; max **<= 6,000 ms** (half the cap: a new product's first build makes its map, about 3.1 s in the M stage); **0 `exceededCpu`, 0 `exceededMemory`** |
| **S3-4** (frames) | the storing checks over its first full hour after its first storing check | **>= 10 frames stored.** Every error line is an OPERA listing timeout or R2's internal error (10001): the two kinds the M stage and R3′ saw. Their count per hour is printed |
| **S3-5** (prune) | `r3_list.py wx/<id>/frames/` at +70 minutes, read twice 10 minutes apart | **<= 13 frames**; every storing line's `pruneKeys` is 900 or 899 |
| **S3-6** (no device's answer changes) | `tools/wx/r12_prod.sh` (#636) at the 40 label points and the 9 R2 points, after the tenth | **as R1-P and R2-P read today:** the 5 isles points and the 9 get `opuk` with 720, and the 35 get cref's all-14. `ROUTED` is unchanged, so the nine new products answer no one |
| **S3-7** (Class A, as ruled) | `tools/wx/account_cost_read.py read` (#635), 24 hours after the tenth: the last 24 hours' Class A x 30, and the 7-day run rate when it holds 7 days | the 24 h x 30 run rate in **0.95-1.10 M**: today's 0.862 M plus 9 x 17,280, plus maps and lost races. **Reported to Daniel, as ruled.** Over 1 M is expected, and that is the invoice question |
| escape | anything outside a row | see §4 |

## 4. When a row reads outside

- **A new product:** its `-inert` env is deployed at once, through `ingest_deploy.sh <id>-inert` under the claim, so
  its ingest stops (`WX_INGEST` off). The sequence stops and I report. The Worker is left in place: deleting it is
  Daniel's call.
- **`opuk`:** `tools/wx/md_rollback.sh "<the row>"` with `OPUK_ROLLBACK_TO` set to S3-0's recorded version, at once. A
  rollback is never refused, and it logs itself in `rollbacks.log`. Then report.
- **Products already deployed and reading as predicted** keep running. Their ingests serve no one until they join
  `ROUTED`.

## 5. CI for this PR (predicted before the push)

It changes this doc only.
- **Predicted: 32 check runs: 29 success, 3 skipped, 0 failed.**
- **The baseline** is #635's finished head run, a docs and `tools/wx/` change: 32 = 29/3/0.
- **No job-set change since.**
