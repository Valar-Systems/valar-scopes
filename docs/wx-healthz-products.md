# /healthz reports every weather product (predictions first, 2026-11-11)

**For:** the reviewer (the orchestrator), and blipscope-5c, which reads it for conflicts with its proxy work.
**Status:** predictions, frozen before any code. Nothing is written or deployed. Registered at `93977647`, then
amended for Q-HZ1, which was ruled yes at ~18:52Z: smoke's per-product line (H6, H6s, P3, §7). The rest of the
registration was reviewed as it stands.

## The ruling (the orchestrator, 2026-11-11 ~18:46Z), item 1, verbatim

> 1. The blind spot: yes, close it. A health surface that cannot express a deployed ingest vouches for it with a green
>    it never measured. That's CLAUDE.md's "green about the subsystem that isn't failing", and the nine OPERA ingests
>    deployed today are in exactly that position.
>    - Register a small change, predictions first: the health set covers every DEPLOYED product, each marked routed
>      true or false, with whatever liveness it can read cheaply (the latest stored frame's age, say). Derive it from
>      PRODUCTS and ROUTED; no second literal list. That's the derivation you proposed to weatherscope, applied here
>      first.
>    - Name each read's cost (Class B per /healthz call), and how often /healthz is called. It must not add a
>      per-request bill.
>    - A control that can fail: a product deployed but unrouted shows routed:false with a real age, and a planted stale
>      product reads stale.
>    - blipscope-5c reads it for conflicts (its proxy). Send it the registration after O11 window 2 closes; that window
>      is running now, on the production proxy's rows.
>    - Deploy: not while any O11 window is open. Through the scripts, under the claim, with the rollback target
>      recorded first. I'll give the slot.
>    - Until it lands, the nine are watched only by your own reads. Your 7-day re-read (~11-13) covers them; say so in
>      #641.

Item 2 of the same ruling sets the order against weatherscope's W4, which touches the same lines. This change goes
first, as agreed with weatherscope (~18:50Z). W4 drops its own health set and rebases on this one.

## 1. The gap

`/healthz`'s `wx` block is `wxHealth(env, PRODUCTS.filter((p) => ROUTED.has(p.id)))` (`proxy/src/index.ts:92`), so it
reports `cref` and `opuk` only. The nine OPERA ingests deployed on 2026-11-11 (`docs/wx-opera-stage3.md`) store frames
every five minutes, and `/healthz` can't express any of them. Today a green `/healthz` says nothing about them.

## 2. The change, one place each

| where | today | after |
|---|---|---|
| `proxy/src/index.ts:92` | `wxHealth(env, PRODUCTS.filter((p) => ROUTED.has(p.id)))` | `wxHealth(env, PRODUCTS, ROUTED)`: every row, and the routed set beside it |
| `proxy/src/wx/handler.ts` `wxHealth` | `frameAgeS` per product (plus `coveredShare` for OPERA), read one after another | the same fields, plus `routed` and `stale`, read in parallel (`Promise.all`) |
| `proxy/src/wx/products.ts:96-100` | ROUTED's comment: "the products /wx/cref may be answered from, and the ones /healthz reports" | the comment loses "and the ones /healthz reports". **ROUTED's members do not change**, so `wx-region.test.ts:172-175` is untouched |

**Each entry, in `PRODUCTS`' order:**
- **`frameAgeS`**: unchanged. It is the latest record's age, or `null` when there is no record, never a made-up age.
- **`coveredShare`**: unchanged, OPERA rows only.
- **`routed`**: `ROUTED.has(id)`, true or false.
- **`stale`**:
  - `true` when `now - epoch > p.maxAgeS`, measured on the unrounded age. That is the endpoint's own `wx_stale` rule
    (`handler.ts:68`), so `/healthz` and `/wx/<id>` can never disagree on what stale means. `maxAgeS` is 600 s for
    `cref` and 1,800 s for the OPERA rows.
  - `null` when there is no record: no frame is not a stale frame, and the verdict is never made up.

**There is no second list.** The set is `PRODUCTS`, and `routed` is read from `ROUTED`. A product that joins `ROUTED`
flips to `routed: true` in the same deploy. A new row, such as weatherscope's `shsr`, appears with no further code.

**What "deployed" can and cannot mean here.** The proxy can't see Cloudflare's deployments, so the set is every
registered product. That leaves three states:
- a row whose ingest has never stored reads `frameAgeS: null, stale: null`. That covers both "not deployed yet" and
  "deployed, never stored", and **those two look the same**, a limitation stated here rather than hidden;
- a deployed ingest that stops storing reads `stale: true` once its age passes `maxAgeS`;
- a healthy one reads `stale: false`.

**Existing consumers of the block, every one in the repo:**
- `proxy/scripts/wx-opuk-verdict.sh`, sourced by `smoke-prod.sh`, cuts `"opuk":\{[^}]*\}` and reads `frameAgeS` and
  `coveredShare`. Two new flat fields don't change what it cuts, and its selftest gains a body in the new shape (H5).
- `proxy/test/wx-endpoint.test.ts:158` pins the two-key shape with `toEqual`. H1 replaces it.
- `proxy/test/wx-opera.test.ts:629-631` (K9) reads `opuk.coveredShare` and asserts `cref` has none. That stays true,
  and K9 is unchanged.
- `tools/wx/r12_prod.sh` prints the block (P0), ungraded.

Nothing else in `proxy/`, `scripts/`, `tools/` or a workflow reads it (`git grep` for `frameAgeS`, `coveredShare`,
`wxHealth` and `"wx":`, docs excluded).

## 3. Cost: what each `/healthz` call reads, and how often it is called

- **Per call:** one R2 `get` of `wx/<id>/latest` per product, one Class B operation each. Today that is 2 a call; after
  the change it is 11, so **+9 Class B a call**. Nothing is cached (`readLatest`, `handler.ts:24`).
- **How often:** production's Analytics Engine dataset (`blipscope_proxy`, route `/healthz`, counted with
  `_sample_interval`) read on 2026-11-11 at ~18:50Z.
  - **118 calls in the last 7 days:** 30, 15, 28, 30, 0, 0, 0 and 15 by day, 11-04 to 11-11.
  - **Every call carried no device model:** they come from tools (smoke runs, deploy confirmations, R reads), never
    from a device.
  - **The anchor:** the same dataset held 40,496 to 61,778 points on every one of those days, and
    `/api/v1/blipscope/blips` 242,285 over the 7. So the three zero days are days without a call, not a dead dataset.
- **So:** about +9 x 17 a day, roughly **+4.6 k Class B a month**, against the account's 4.52 M of 10 M included
  (`docs/wx-account-cost.md`). Even a once-a-minute monitor would add +0.39 M a month, still inside.
- **No device request reaches `/healthz`,** so the change adds **no per-request bill**.
- The eleven reads run in parallel, so the call's latency is about one R2 read, not eleven.

## 4. The tests (vitest, `proxy/`)

| row | what | predicted |
|---|---|---|
| **H1** (replaces `wx-endpoint.test.ts:158`) | `/healthz` with no frames | `wx` has exactly `PRODUCTS`' 11 ids, in `PRODUCTS`' order. Every entry has `frameAgeS: null, stale: null`. `routed` is `true` for exactly `cref` and `opuk`, written in the test as literals, and `false` for the other nine. The OPERA entries carry `coveredShare: null`, and `cref`'s carries no `coveredShare` |
| **H2** (the ruling's first control) | a frame stored for `opmt` (deployed, unrouted), 90 s old | `opmt`: `routed: false`, `frameAgeS` 90-119, `stale: false` |
| **H3** (the ruling's second control) | planted `latest` records: `opse` at 1,800 + 60 s old, `opba` at 1,800 - 60 s, `cref` at 600 + 60 s | `opse` `stale: true`, `opba` `stale: false`, `cref` `stale: true`, on both sides of the boundary for both kinds of product. The body's `{id: {routed, stale}}` projection also equals the one in the committed fixture `test/fixtures/wx/healthz-h3.json`. H6 feeds that same fixture to smoke's function, so the WARN lines below come from the state the real plant produced, not from a hand-written body |
| **H4** (kept) | "the newest frame's age when there is one" | unchanged: `cref.frameAgeS` 90-119 |
| **H5** | `wx-opuk-verdict.selftest.sh` gains one body in the new shape (`opuk` with `routed` and `stale`) | that body's verdict is PASS. The selftest compares the WHOLE verdict list, now 10 lines; every other line is unchanged |
| K9 | `wx-opera.test.ts:629-631` | unchanged and passing |
| **H6** (Q-HZ1, the smoke line) | the new `proxy/scripts/wx-products-lines.selftest.sh` grades `wx_products_section <healthz body>`, from the new `proxy/scripts/wx-products-lines.sh`, which `smoke-prod.sh` sources | see the cases below |

**H6's cases.** The selftest compares the WHOLE printed output of each case with an expected list, never a fragment.
- (a) **H3's fixture body:** one line per product, 11 lines in `PRODUCTS`' order, each `wx.<id> routed=<bool>
  age=<n|null> stale=<bool|null>`. Exactly two start with `WARN`: `opse`'s and `cref`'s. `opba`'s does not.
- (b) **H3s's body:** the same fixture with every `"stale":true` turned to `false` (the selftest asserts that edit
  applied). The same 11 lines, and **no** `WARN`.
- (c) **Today's production shape:** `cref` and `opuk` with `frameAgeS` only. Two lines, `routed=- stale=-`, and no
  `WARN`. smoke runs against the old shape until the deploy.
- (d) **`"wx":"not_configured"`:** one line, `wx not_configured`.
- (e) **The exit code.** Every case above returns status 0, stale or not. `smoke-prod.sh` calls the function exactly
  once, on a line of its own (`wx_products_section "$HEALTH_BODY"`, checked by `grep -cx` = 1), and that line touches
  neither `pass` nor `fail`. So **a WARN never changes smoke's PASS or FAIL count, and never changes its exit code.**
- (H6s) **The selftest's own plant,** as `wx-opuk-verdict.selftest.sh` does it: in a temporary copy, the WARN condition
  is removed, with the edit asserted to have applied. Case (a)'s output must then differ, and the selftest must say so.
  A plant that changes nothing fails the selftest.

**Sabotages:** a new driver, `proxy/scripts/sabotage-wx-healthz.py`, built like `sabotage-wx-split.py`. Each plant is an
exact one-place edit, refused unless its anchor matches once. The driver compares the WHOLE failed list and restores
by bytes and sha256. Each plant's failed set:

| plant | the edit | failed set |
|---|---|---|
| H1s | `index.ts` passes the old filtered list again | {H1, H2, H3} |
| H2s | `routed` read as `true` for every product | {H1, H2} |
| H3s | `stale` never `true` | {H3}. H3 fails on both the flags and the fixture projection, so the state that would produce WARN for `opse` and `cref` is never reached. H6 (b) shows the smoke side of the same plant: that body prints no `WARN` |

**The suite:** 1,039 passed and 1 skipped (1,040) in 75 files, from CI's workers run at `d46c25c4`, main's last
`proxy/` change; `proxy/` is unchanged since. H1 replaces one test, and H2 and H3 are new, so the prediction is
**1,041 passed, 1 skipped (1,042), in 75 files**. It runs under the suite rule: one whole-suite run at a time, and
TIME_WAIT under 2,000.

## 5. On production, after the deploy

| row | what | predicted |
|---|---|---|
| **P0** (before) | the rollback target: `npx wrangler deployments status` for the proxy, read-only | one version at 100 %, the one stamped `68dc169a`. Recorded in full before anything deploys |
| **P1** | `/healthz` | `commit` = the deployed sha. `wx` lists the 11 in order. `cref` and `opuk` read `routed: true`, and the nine read `routed: false` |
| **P2** | the same body | every product has a non-null `frameAgeS` under its `maxAgeS`, and `stale: false` |
| **P3** | `proxy/scripts/smoke-prod.sh` | passes, with its `wx-opuk` verdict PASS. Its new section prints 11 product lines and no `WARN`, since every product is fresh (P2). Its summary reads `FAIL: 0`, and the section adds nothing to either count |
| **P4** | `tools/wx/r12_prod.sh` at the 40 label points and the 9 R2 points | as R1-P and R2-P read today. `ROUTED` is unchanged, so no device's answer changes |

**If P2 reads a product stale.** OPERA pauses: every product read `already-stored` from 16:24Z to 16:35Z today. So a
single stale reading is not a reading: re-read once 10 minutes later.
- **Stale twice:** read that Worker's Logs (`tools/wx/r3p.py lines`).
  - If its last storing check agrees with the age, `/healthz` is telling the truth about an ingest that has stopped.
    That is a finding about the ingest, reported, not a fault in this change.
  - If the Logs disagree with the age, the change is wrong, and the proxy rolls back to P0's version.

**Escape:** anything outside a row: stop and report. A wrong P1 or P4 rolls back at once.

## 6. The deploy

- **Who deploys:** the orchestrator gives the slot and names the deployer. The proxy is blipscope-5c's.
- **When:** never while any O11 window is open.
- **How:** through `proxy/scripts/deploy.sh` only, under the deploy claim `proxy:production`, with P0 recorded first.
- **What it carries:** a deploy of main at that point carries only this change in `proxy/`. `git diff 68dc169a
  origin/main -- proxy/` is empty today, and the list is re-read before the deploy.
- **Until it lands,** the nine are watched only by my own reads. The 7-day re-read (~2026-11-13) covers them, and #641
  says so.

## 7. Who reads it: Q-HZ1, ruled yes, in this PR (the orchestrator, ~18:52Z)

> Q-HZ1: yes, in this PR. smoke-prod.sh is the only production check people actually run, so the per-product line
> belongs there.
> - One line per product: routed, frameAgeS, stale. Stale prints WARN, not FAIL. smoke's exit code must not change
>   because of a WARN; predict that explicitly.
> - Add it to the plants: under H3s ("stale never true") the WARN line must not appear. Under the real H3 plant it
>   must appear, for opse and cref, and not for opba.
> - Register these rows in the doc before the code, as for the rest.

**The reader:**
- `proxy/scripts/wx-products-lines.sh` defines `wx_products_section`. It reads `/healthz`'s `wx` object from the body
  `smoke-prod.sh` already fetches (`HEALTH_BODY`, one request, no new one), and prints one line per product. A stale
  product's line starts with `WARN`.
- `smoke-prod.sh` sources it beside `wx-opuk-verdict.sh` and calls it once under its own heading. Neither counter is
  touched.
- Its rows are H6, H6s and P3 above.
- The selftest runs in CI as one more step in `workers.yml`, beside `wx-opuk-verdict.selftest.sh`.

## 8. CI for this PR (predicted before each push)

- **This commit, the registration (docs only):** 32 check runs, 29 success, 3 skipped, 0 failed. The baseline is
  #637's finished head run, docs only: 32 = 29/3/0.
- **This amendment (docs only):** the same, 32 = 29/3/0.
- **The code commit (`proxy/` touched):** 35 check runs, 32 success, 3 skipped, 0 failed. The baseline is #627's
  finished head run (`abd02ea914cf`), a `proxy/` change: 35 = 32/3/0. The commit adds one step to `workers.yml`
  (H6's selftest), a step and not a job, so the count doesn't move.
- **No job-set change since #627.** #629 and #633 changed workflows, but by steps only:
  - #629: firmware's "No web-only key" and web-renderer's AF3 and KI4 steps;
  - #633: device-ids' two `check_ingest_deploy.sh` steps.
- Every CI step the code can reach is run locally before its push: the workers job's vitest suite and its scripts'
  selftests, plus `device-ids.yml`.
