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

## 6. Read 2026-11-11: S3-0 to S3-6, every row as predicted (predictions at 72ab4c64)

**Before the first deploy:**
- **The merges**, each through `scripts/merge-by-rule.sh`, each squash's parent equal to H and its tree equal to
  merge-tree's:

  | PR | merged | squash |
  |---|---|---|
  | #635 | 14:57:30Z | `9673f7d0fb8b` |
  | #636 | 15:11:06Z | `7fe549234233` |
  | #637 | 15:32:43Z | `5ab18cd8f9aa` |

- **The announcement** was the reviewer's every-session block of ~14:53Z. The owner was told at the start, ~15:00Z.
- **The deploy checkout** was main at `9673f7d0`, detached and clean. Its `proxy/package-lock.json` was unchanged since
  its `node_modules` was installed. Before each product, the cycle confirmed that `proxy/` still matched `origin/main`;
  0 files differed every time.

**S3-0 (read 14:58:31-14:58:51Z): as predicted.**
- The nine new products: `This Worker does not exist on your account. [code: 10007]`.
- `opuk`: one version, **`4da941e4-145c-4120-a097-8b56852b29a9` at 100 %**, deployed 2026-11-07T02:40:57Z. That is
  `OPUK_ROLLBACK_TO`.
- Before `opuk` deployed, `md_rollback.sh --dry` with that target passed. It named `blipscope-wx-ingest-opuk`, held
  `4da941e4`, and its control (the top level) read 10007.

**Per product, in the frozen order (S3-1 and S3-2):**
- Each product ran under its own claim, `wx-ingest-<id>:production`. Each claim was released after its S3-2, and the
  next product started only after S3-2 had read.
- "Deployed" is `ingest_deploy.sh`'s deploy end. S3-2's limit is 900 s from it.

| # | product | deployed | version | S3-1 | S3-2: first storing check | `pruneKeys` | `latest` |
|---|---|---|---|---|---|---|---|
| 1 | `opmt` | 15:00:15Z | `915dfa07` | as predicted | 15:10:58Z, +643 s | 900 | listed (3 frames) |
| 2 | `opsw` | 15:13:44Z | `aaf12881` | as predicted | 15:23:16Z, +572 s | 900 | listed (2) |
| 3 | `opce` | 15:24:19Z | `166b5974` | as predicted | 15:30:20Z, +361 s | 900 | listed (3) |
| 4 | `opib` | 15:31:50Z | `4de3159a` | as predicted | 15:38:13Z, +383 s | 900 | listed (2) |
| 5 | `opis` | 15:39:21Z | `69ea0cff` | as predicted | 15:44:19Z, +298 s | 900 | listed (4) |
| 6 | `opse` | 15:45:51Z | `54babaab` | as predicted | 15:51:25Z, +334 s | 900 | listed (2) |
| 7 | `opba` | 15:52:21Z | `b8dd7c8f` | as predicted | 15:59:10Z, +409 s | 900 | listed (2) |
| 8 | `opwe` | 15:59:52Z | `5e42945a` | as predicted | 16:06:30Z, +398 s | 900 | listed (2) |
| 9 | `opsn` | 16:07:25Z | `7139b24a` | as predicted | 16:12:51Z, +326 s | 900 | listed (2) |
| 10 | `opuk` | 16:23:33Z | `7c828e17` | as predicted | 16:36:00Z, +747 s | 900 | listed (11) |

- **S3-1 means:** exit 0; one version at 100 %; the bindings exactly `WX` -> blipscope-wx, `PRODUCT` = the product and
  `WX_INGEST` = on; `cpu_ms` 12000; and the deploy's `schedule: * * * * *`.
- **One change to timing, not to order:** `opuk` was held about 10 minutes after `opsn`'s S3-2, until `opmt`'s full
  after-window read had come back as predicted. That put the first complete S3-3 to S3-5 reading of a production
  ingest in hand before the live grid changed.
- **`opuk`, printed, not graded.** The frozen S3-2 row does not say which version must store, but Workers Logs records
  it (`logver.py`). Every storing line after the deploy is `7c828e17`'s. The old version, `4da941e4`, ran **one**
  invocation after the deploy, which read `already-stored`, and none since.
- **OPERA paused, printed, not graded.** From 16:24Z to 16:35Z every check of every product read `already-stored`.
  Some S3-5 listings below are missing a five-minute slot from that window.

**S3-3 to S3-5 (each graded at max(deploy + 72 min, first storing check + 62 min)): all ten, every row as predicted.**
- S3-3's window is the 70 minutes from the deploy.
- S3-4's hour starts at the first storing check.
- S3-5's listings were read 10 minutes apart.
- Every Logs read was at ABR 1, and none needed the narrower re-read.

| product | S3-3 single-build median, ms (bound) | max of all, ms (<= 6,000) | outcomes | S3-4 frames, errors | S3-5 listings (<= 13) |
|---|---|---|---|---|---|
| `opmt` | 89.5 (152) | 369 | 59 ok | 15, 0 | 12, 12 |
| `opsw` | 267 (411) | 2,089 | 61 ok | 15, 0 | 12, 9 |
| `opce` | 309 (425) | 2,330 | 65 ok | 13, 0 | 10, 12 |
| `opib` | **436.5 (443)** | 2,147 | 64 ok | 15, 0 | 12, 12 |
| `opis` | 294 (465) | **4,525** | 66 ok | 15, 0 | 12, 11 |
| `opse` | 334 (477) | 2,429 | 65 ok | 15, 0 | 12, 12 |
| `opba` | 306 (484) | 3,506 | 64 ok | 15, 0 | 12, 13 |
| `opwe` | 406 (503) | 3,030 | 64 ok | 15, 0 | 12, 12 |
| `opsn` | 322 (648) | 3,610 | 65 ok | 15, 0 | 12, 13 |
| `opuk` | 341.5 (583) | 3,294 | 70 ok | 15, 0 | 12, 12 |

Every storing line carried `pruneKeys` 900. There were 0 `exceededCpu`, 0 `exceededMemory` and **0 error lines** in
any window.

Printed, not graded:
- **`opib` reads 6.5 ms under its bound.** It is 1.58 x M6's expected 277 ms, against the frozen x 1.6. The fit
  under-reads `opib` by more than it does the others. Against their expected values in §1: `opce` 1.17 x, `opsw`
  1.04 x, `opwe` 1.29 x M1's measured 314.5. `opsn` reads 0.80 x its measured 405.
- **The largest single invocation is `opis`'s at 4,525 ms,** two frames stored in one run. That is 75 % of S3-3's
  6,000 ms and 38 % of the 12,000 ms cap.
- **`opuk`'s first build on the isles grid built its map** (`map: built`) at **3,294 ms**; the M stage measured about
  3.1 s. Every later build read the map from R2, with a median of 339 ms. `opuk`'s 70 records include the old
  version's one late invocation.

**S3-6 (16:37:37-16:38:06Z, production on `68dc169a`): as predicted.**
- The R2 count was re-anchored first: **48,721**. The table held 6,921,706 points, equal to its header's. Western
  Ireland 30,488, the Channel Islands 234, France's coast 17,999.
- **The 5 isles points:** 5 of 5 at 200, `opuk`, 720.
- **The 35 others:** 35 of 35 at 200, `cref`, byte 32 `0xE0`, no `X-Wx-Max-Age`.
- **The R2 sample:** 9 of 9 at 200, `opuk`, 720.
- `ROUTED` is unchanged, so the nine new products answer no one.
- The point file lived in the scratch folder and was deleted after the read.

Printed, not graded:
- **Two of western Ireland's three R2 crops now read byte 32 `0x00`;** both read `0xE0` in R2-P's read that
  morning. This is consistent with the isles grid now reaching the west margin, and it is printed, not graded.
- **`opuk`'s `coveredShare` reads 0.577,** against 0.751 on the old grid at R2-P's P0.

## 7. The instruments, now in `tools/wx/`

They ran from the session's scratch folder. The copies committed here differ from the run copies **in paths only**:
the scratch paths became paths relative to the checkout, plus an `S3_OUT` folder for outputs, and the usage lines name
`tools/wx/`. Each was controlled in both directions before it graded anything, **except `s32_wait.sh`'s 900 s limit**.
That limit was first planted after the ten had read, from the repo copy, and it failed as it should; every S3-2 above
read well inside it (298-747 s). The committed copies were re-controlled from this checkout on 2026-11-11 at
18:22-18:26Z.

| instrument | what it does | controls before use | re-controlled from the repo copy |
|---|---|---|---|
| `s3_cycle.sh` | one product: claim, `ingest_deploy.sh`, S3-1, S3-2, release; anything off stops with the claim held | its cron-line regex matched `opmt`'s real deploy output; planted (`*/5`), with the plant asserted to have applied, it read MISSING | not re-run (it deploys); its changed lines are the same path lines exercised in `s32_wait.sh` and `s3_after_at.sh` |
| `s31_check.py` | S3-1 against the frozen record | `opmt`'s real record passed; judged as `opsw`, with another version id, and with the cron line missing, it read OFF | `opmt` passed; as `opsw`, OFF |
| `s32_wait.sh` | S3-2: polls Workers Logs at ABR 1, then `latest_check.py` | its `latest` half is `latest_check.py`'s; the limit itself was first planted from the repo copy (below) | `opmt`'s first storing check again: 15:10:58Z, +643 s, exit 0. Planted with the deploy time moved 5 minutes earlier, the same line read 958 s against 900: exit 1 |
| `latest_check.py` | `wx/<id>/latest` exists and names a listed frame | live `opuk` passed; undeployed `opmt` failed | `opuk` passed; a product with no frames failed |
| `s3_after.py` | S3-3 to S3-5 against the frozen bounds | clean `opuk` history passed; a planted bound (100) and frame cap (5) read OFF | the same plant read OFF |
| `s3_after_at.sh` | waits until a product's after-window time, then `s3_after.py` | (wraps the above) | `opmt`'s window again: S3-3 and S3-4 exactly as graded at 16:13Z |
| `logver.py` | which script version wrote a Worker's lines | | the old version's 1 invocation, as before |
| `classa_24h.py` | S3-7's Class A over one 24 h window, x 30 | below | the same window again: 863,790 |
| `classa_hourly.py` (added with S3-7) | one bucket's Class A by hour over the same window, for attribution | its hours sum exactly to `classa_24h.py`'s `blipscope-wx` (5,314 a day); a bucket that does not exist exits 2 with "NO ROWS", never a zero | written after S3-7's two reads, from the scratch copy that attributed them; controlled from the repo copy at 16:47Z |

## 8. S3-7: read 2026-11-12, inside the band, but NOT for the predicted reason

**The reading:** `classa_24h.py 2026-11-12T16:24:00Z` read **954,960 a month (0.955 M)** at 16:35:07Z and again at
16:45:05Z, identical: `missileer-live` 778,680, `blipscope-wx` 159,420, `missileer-clips` 16,560,
`blipscope-basemap-pkg-staging` 300 (new since the control). It is inside the frozen 0.95-1.10 M band, at its foot.
**It is not graded as a confirmation,** because the band was reached by two moves in opposite directions:
- **OPERA stopped publishing during the window.** Its open-radar bucket (`openradar-24h`) holds nothing after about
  20:05-20:10Z on 11-11: the composite's last key is `OPERA@20261111T2010`. The bucket also holds 21 national feeds,
  and none has any data for 11-12 (listed read-only at 16:28-16:29Z on 11-12, with the ingests' User-Agent). The ten's
  checks reported `no-frame` with 0 errors throughout. No public notice was found.
- **`blipscope-wx` by hour** (`classa_hourly.py`, below; its 25 hours sum exactly to 5,314 a day, x30 = 159,420):
  408-448 an hour at 17-19Z on 11-11 with OPERA running, then 145-200 an hour from 21Z with OPERA silent. So
  **`cref` alone is about 182 an hour (about 0.131 M a month), and the ten cost about 242 an hour while they ran
  (about 0.174 M a month).** The frozen arithmetic's 10 x 17,280 = 0.173 M is right. The alternative, "the ten cost
  less than predicted", is excluded: they ran at the predicted rate, then stopped.
- **`missileer-live` stepped up,** from about 982 an hour in the control to a flat 1,079-1,102 an hour across every full
  hour of this window. It is a step before the window, not a trend within it.
- **With OPERA running,** the same window gives about **1.10 M**, at the band's top, and over 1 M as the frozen row
  expected. **The ten's full-day Class A is unmeasured** until OPERA publishes again. The 7-day figure, about
  2026-11-18, will contain the outage too.
- **What it changes, for the owner:** the invoice question stands. Under Read A the account crosses 1 M while OPERA
  runs. While OPERA is silent, it sits under 1 M only for as long as the outage lasts.

**The nine are no longer watched only by my reads.** #643 deployed on 2026-11-12, to staging at 05:53Z and production
at 15:15Z (`docs/wx-healthz-deploy.md`). `/healthz` now lists all ten with `stale`, and smoke prints one line each.

**The plan, as it was frozen before the read:**

As ruled, S3-7 comes 24 hours after the tenth.
- **The window:** the read covers the 24 hours ending 2026-11-12T16:24:00Z. It is read twice, 10 minutes apart, and the
  readings are added here as a further commit.
- **Why a new instrument:** `account_cost_read.py` (#635) gives only the 7-day run rate (last 7 x 30/7), not the
  frozen row's "last 24 hours x 30". So S3-7 reads with `classa_24h.py`, which uses the same query and the same class
  set.
- **Its control:** the 24 hours before the first deploy (ending 14:58Z) read **863,790** a month, twice, 3.5 hours
  apart: `missileer-live` 706,830, `blipscope-wx` 140,580, `missileer-clips` 16,380. The 7-day figure was 0.862 M
  (`missileer-live` 0.702 M), so the two instruments read the same thing.
- **The arithmetic** is 0.864 M + 9 x 17,280 = **1.019 M**, inside the frozen 0.95-1.10 M.
- **The 7-day figure** follows when the window holds 7 days, about 2026-11-18.
- **The invoice question is the owner's.**

## 9. CI for the record PR (predicted before the push)

It adds sections 6 to 9 to this doc and eight files in `tools/wx/`. No file it touches is in `workers.yml`'s or
`web-renderer.yml`'s path filter.
- **Predicted: 32 check runs: 29 success, 3 skipped, 0 failed.**
- **The baseline** is #636's finished head run, a docs and `tools/wx/` change: 32 = 29/3/0. #637's, docs only, read the
  same.
- **No job-set change since:** `git diff 5ab18cd8 e0941a90 -- .github/` is empty.
- **The S3-7 commit** adds section 8's readings and `tools/wx/classa_hourly.py`, neither in a path filter. **Predicted
  again: 32 = 29/3/0.** The newest baseline is #661's head run (docs only, tested into `f3fee349`), which read 32 =
  29/3/0.
- **Run locally before the push:** `device-ids.yml`'s tree scans and `no-traffic-captures.yml`, over this tree.
