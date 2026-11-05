# The opuk split: opuk in its own Worker, and R3′

**For:** the reviewer, and whoever runs R3′. **Status:** predictions first. This file is committed before any of the
PR's code. **Nothing deploys from this PR:** R3′ waits for review, merge and a slot from the reviewer.

## Why

R3 (2026-11-04/05) rolled `opuk` back at 00:18:33Z. Three invocations ran 5.4-6.1 s of CPU under `cpu_ms` 5000, and
each was an invocation where both `cref` and `opuk` built a frame. Plan S then measured each product alone on
Cloudflare's runtime, through a staging copy (record: `docs/wx-opuk.md` on `docs/wx-opuk-r3`):
- **`opuk` alone:** median 2,055 ms, max 3,123 ms (S4). Production's join read 2,161 and 2,948.
- **`cref` alone, single-build:** median 1,186 ms, max 2,548 ms (S3). Production's baseline was median 1,328, max
  3,621.
- **Margins, ruled:**
  - identical work reads up to about **1.6x** apart (S1: 7,939-12,693 ms);
  - an invocation that builds twice, a backfill, costs about **2x**;
  - about **1 in 60 to 1 in 90** invocations vanish without a record (S3);
  - `workersInvocationsAdaptive` sums can be inflated by a sampled row (S4: one firing read as 10). So every GraphQL
    figure carries its `sampleInterval`, and CPU bars read Workers Logs invocation records.
- **The CPU-limit gate is live:** S2 planted overruns, and both instruments saw them (`exceededCpu` 10/10,
  `exceededResources` 10/10).
- **Cron changes take up to 15 min** to start or stop (measured 268.7-592 s to start, 323-336 s to stop). Version
  changes are immediate.

## 1. What this PR changes

- **The `cref` Worker** (`blipscope-wx-ingest`, `proxy/wx-ingest.wrangler.toml`, `main` unchanged):
  - its `scheduled` checks **`cref` only**, through `checkAll`'s product list (`CREF_WORKER`);
  - `checkAll`'s default stays every product, so PS1 is unchanged;
  - **`cpu_ms` 5000 -> 12000.**
- **The `opuk` Worker** (`blipscope-wx-ingest-opuk`), new:
  - **`proxy/wx-ingest-opuk.wrangler.toml`**: `main` = `src/wx/ingest-opuk-worker.ts`; cron `* * * * *`;
    **`cpu_ms` 12000**; its only binding is `WX` -> **`blipscope-wx`**; var **`WX_INGEST` = `"on"`**; observability on;
    `workers_dev = false`; no route;
  - **`[env.inert]`**: the same Worker name, the cron **kept**, `WX_INGEST` = `"off"`, the same binding. **Stopping
    `opuk` is `deploy -e inert`, a version change.**
  - **`src/wx/ingest-opuk-worker.ts`**: production's `:00` / `:30` shape.
    - `WX_INGEST` = `"on"`: `checkAll` over **`opuk` only** (`OPUK_WORKER`).
    - Anything else, or absent: one `wx_ingest_off` line per check, and no bucket or upstream is touched.

**Sizing `cpu_ms`** (ruled ~21:30Z):

| Worker | measured single-build max | x2 backfill | x1.6 spread | `cpu_ms` |
|---|---|---|---|---|
| `cref` | 3,621 | 7,242 | 11,587 | **12,000** |
| `opuk` | 3,123 | 6,246 | 9,994 | **12,000** |

**`cref`'s headroom row:** today's 5,000 sits below a measured two-build `cref` backfill (5,733 ms, S3). 12,000 is under
the documented 30,000 cap for a sub-hourly Cron Trigger. It is budgeted as `cpu_ms` itself, never as the grace S2's
2,100-at-100 suggests.

## 2. The PR's checks, predicted before the code

A new test file, `proxy/test/wx-ingest-split.test.ts`, holds **4 tests**. Each test runs a deploy entry's real
`scheduled` handler, with `scheduledTime` 31 s in the past (so there is no wait), a global `fetch` that answers 404,
and `console.log` captured.

| id | the check | predicted |
|---|---|---|
| **FT1** | the `cref` Worker's entry (`ingest-worker.ts`'s default export) | its lines' products are exactly **`["cref", "cref"]`** (`:00`, `:30`) |
| **FT2a** | the `opuk` entry, `WX_INGEST` `"on"` | exactly **`["opuk", "opuk"]`** |
| **FT2b** | the `opuk` entry, `WX_INGEST` `"off"`, then absent, with a bucket and a fetch that throw if touched | exactly two `wx_ingest_off` lines each, nothing touched |
| **FX** | **no two deploy targets across `wx-ingest.wrangler.toml` and `wx-ingest-opuk.wrangler.toml` check the same product.** The tomls are read as text (a virtual module, as `committed-credits` is). Each target is the top level plus every `[env.*]`, with wrangler's rules: `main` inherits, vars don't. Each target's own entry runs with its own vars, and the products it checks are collected | every product appears in **at most one** target. **Control:** the union is exactly `{cref, opuk}`, or the test fails as BLIND. Expected: top `cref` -> {cref}; opuk top -> {opuk}; opuk `inert` -> {} |

**Sabotages** (`proxy/scripts/sabotage-wx-split.py`). Each is planted, run on the new file alone, and restored by bytes
with a sha check. The whole failing list is compared:

| sabotage | the plant | predicted failures (exactly) |
|---|---|---|
| **FT1s** | `CREF_WORKER` gains `opuk` | **FT1, FX** (`opuk` is then in two targets) |
| **FT2s** | the off branch falls through to the check | **FT2b, FX** (`inert` then checks `opuk`) |
| **FXs** (the ruling's) | `OPUK_WORKER` gains `cref` | **FT2a, FX** |

**The bundle and bindings check** (`check-wx-plant.mjs --selftest`, the existing CI step), extended:
- **PB1** also reads the `opuk` Worker's dry-run bundle: **0** plant markers, with the anchor `wx_ingest_error`
  present.
- **FB2:** the `opuk` toml's top level and `inert` each bind exactly one R2 bucket, `WX` -> `blipscope-wx`, with
  `WX_INGEST` `"on"` and `"off"` respectively.
- **FB2s:** a copy with `inert` bound to `blipscope-wx-dev` must FAIL on `inert` alone.
- PB2 and PB2s are unchanged: 7/7 dev targets, FAIL on `cref` alone.

**CI, read only from finished runs:**
- **Proxy:** base `3e0a4443e6b5` read 911 passed, 1 skipped (912), over 69 passed and 1 skipped files. Predicted
  **915 passed, 1 skipped (916)**, over **70 passed and 1 skipped files (71)**. If main moves, the base's own counts
  **+4 tests, +1 file**.
- **The plant selftest:** SELFTEST PASSED, with the extensions above.
- **Check-runs on the head: 41** (38 success, 3 skipped).
- Typecheck and typecheck:scripts green.

## 3. R3′ (registered in full here, before any deploy)

**Instruments:**
- **Workers Logs:** invocation records and check lines, **per Worker by `$metadata.service`**: `blipscope-wx-ingest`
  for `cref`, `blipscope-wx-ingest-opuk` for `opuk`.
- **`/healthz`'s `wx` field.**
- **`/wx` probes** as the test identity (`tools/wx/pd_wx.sh`).
- **R2 listings** (`tools/wx/r3_5b.py`).
- **The fleet** (`tools/wx/pd6_fleet.py`).
- **GraphQL figures** are printed with their `sampleInterval` and never graded alone.
- **Graded CPU sets exclude backfill:** an invocation that built more than one frame, or built a `backfilled` frame,
  prints beside its set.

**Order (ruled): `opuk` first, then `cref`.** Every deploy is from the merge commit's clean checkout. The rollback
target is read and recorded before each one.

| row | what | predicted |
|---|---|---|
| **R3′-0** | the cost, re-read just before the first deploy (`cost_read.py`, the same method as R3-0, plus the measured re-read) | **under $2 a month**; the measured figure about $0.53. Over $2: stop and ask Daniel |
| **R3′-1a** | `npx wrangler deploy -c wx-ingest-opuk.wrangler.toml` | exit 0; the deploy lists `schedule: * * * * *`; the version record shows exactly one R2 binding, `WX` -> `blipscope-wx`, `WX_INGEST` = `on`, no other binding, `cpu_ms` 12000 |
| **R3′-10** | the `opuk` Worker's first firing (a cron add) | a `scheduledTime` **within 900 s** of R3′-1a. Else stop: deploy `-e inert` and report |
| **R3′-3** | `/healthz` `wx.opuk`, at the first firing + 10 min | `frameAgeS` <= 1,800; **`coveredShare` in [0.70, 0.80]** |
| **R3′-4** | `/wx/cref` at LHR, KMCI and Bend, as the test identity, after R3′-3 | **LHR:** 200, `WXC1`, an ETag naming `opuk`, `X-Wx-Max-Age: 720`, `X-Wx-Frame` = `opuk`'s latest epoch. **KMCI and Bend:** 200, `WXC1`, an ETag naming `cref`, no `X-Wx-Max-Age` |
| **R3′-1b** | once R3′-10, R3′-3 and R3′-4 read as predicted: `npx wrangler deploy -c wx-ingest.wrangler.toml` | exit 0; `schedule: * * * * *` listed; the version record shows exactly one R2 binding, `WX` -> `blipscope-wx`, no vars, `cpu_ms` 12000. The rollback target, read before: **`582612da`** expected |
| **R3′-2** | **`cref`'s gate**: `/healthz` `wx.cref.frameAgeS` at +5, +10, +20, +30 and +60 min after R3′-1b; and the `cref` Worker's Logs over the first 30 full minutes after R3′-1b's deploy minute | **`frameAgeS` <= 600 at every read.** Log half: **at most 2 minutes missing one of `cref`'s checks, and at most 1 missing both** (the vanished-invocation margin). Minutes with a `wx_ingest_error` line are counted and printed. **A miss means roll back `cref` at once** |
| **R3′-9** | **FX at runtime**, over the same 30 min, by service | every product line in `blipscope-wx-ingest` is `cref`, and every one in `blipscope-wx-ingest-opuk` is `opuk` |
| **R3′-5a** | the `opuk` Worker's `pruned` field, at R3′-1a + 70 min | `pruned` appears only on checks that stored or backfilled. **The first storing check prunes exactly 12**: the stale frames R3 left, 12 listed at 21:13:50Z, epochs 23:10-00:05Z. Then 0 until frames reach an hour old, then at most 1 per storing check |
| **R3′-5b′** | as ruled ~23:00Z 2026-11-04: a listing at R3′-1a + 70 min, read twice 10 min apart | at most 13 frames; every epoch >= the last storing check's start - 3,600 s, except `latest`'s; the sabotage (+300 s) FAILs on the oldest. **No rollback for it alone** |
| **R3′-6** | **CPU, per Worker, from Workers Logs invocation records**, the first 2 h after each Worker's deploy | **the gate: 0 `exceededCpu`** on either Worker (Logs), and R3-6's instrument shows no `exceededResources` (`sampleInterval` printed). Any one rolls that Worker back. **`cref`, single-build:** median in **[740, 2,125] ms**, max <= **5,800**, the rest's median <= 100. **`opuk`, single-build:** median in **[1,280, 3,460] ms**, max <= **5,000**, the rest's median <= 100. (Bars are the measured medians divided and multiplied by 1.6, and the measured max x 1.6.) Printed: p99, backfill invocations, other outcomes |
| **R3′-7** | CloudFerro's answers in the first 2 h (the `opuk` Worker's `wx_ingest_error` lines) | printed, not graded: the count, the errors, the attempts |
| **R3′-8** | the fleet in the 30 min after R3′-1b, as PD6 | as PD6: 4xx 0, 5xx 0, two identical reads |

**Rollbacks:**
- **`cref`:** `npx wrangler rollback <the recorded target> -c wx-ingest.wrangler.toml`. The target is expected to be
  `582612da`. This is a version change.
- **`opuk`:** `npx wrangler deploy -c wx-ingest-opuk.wrangler.toml -e inert`. This is a version change: the next
  firing logs `wx_ingest_off`.
- **The stored `opuk` frames stay.** Deleting data is Daniel's.

**The escape:** anything else means stop. In production, roll back first the Worker that the off row is about.
- R3′-5b′ (as ruled) and R3′-7 (printed) roll nothing back alone.
- A row read off on the runtime spread names that spread as one alternative, as ruled ~17:28Z.
