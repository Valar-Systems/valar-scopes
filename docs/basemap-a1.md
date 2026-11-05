# D4 A1: "202, retry" with stored packages, the store id from KV (O1), and parallel reads (C1)

**Status: predictions, frozen and pushed before any code.** This is A1's PR as ruled (`docs/basemap-cold-build.md`,
~18:25Z), with O1 (`docs/basemap-d4-plan.md`, ~04:30Z) and C1 inside it. The code follows these predictions once they
are accepted.

## The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-05 ~18:25Z, Q-CB1)

> Q-CB1: A1, "202, retry" with stored packages, together with O1 as ruled (the store id from KV names the package key).
> In A1's PR, predictions first:
> - Packages go where the store's own objects never are: their own bucket, or a prefix the store never uses. The store
>   stays read-only to the builder. Name which, with its binding.
> - Keys stay bm/v1/pkg/<storeId>/<digest>, with no coordinate, so MP3 holds.
> - A second request for a query whose build is still running gets 202 without starting a second build, or the plan
>   states what allowing it costs.
> - waitUntil's limit against a 12-17 s build is checked against Cloudflare's documentation and shown on staging before
>   it's relied on.
> - C1's parallel reads go inside A1, to shorten the background build.
> - The device side (202 and Retry-After) goes into your basemap client's plan as a requirement. No firmware calls
>   /basemap yet.

## 1. Each condition, answered

**Where packages go: their own bucket.**
- **Names:** `blipscope-basemap-pkg-staging` (staging) and `blipscope-basemap-pkg` (production, at D4).
- **The binding:** `BASEMAP_PKG`, in the **proxy only**. It reads and writes.
- **Isolation:** the builder never binds it, and it never binds the store bucket `blipscope-basemap`, which stays
  read-only to the builder (MP2 is unchanged).
- **Old packages:** when the store changes, packages under an old store id are dead. A lifecycle rule to expire them
  is a follow-up, not part of this PR.

**Keys.**
- **The package:** `bm/v1/pkg/<storeId>/<digest>`.
- **A remembered `no_coverage` or `too_large`:** `bm/v1/pkg/<storeId>/<digest>.err`.
- **The lock:** `bm/v1/pkg-lock/<storeId>/<digest>`.
- `<digest>` is `queryDigest(q)`, 24 hex characters. **No coordinate is in any key, so MP3 holds.** It is extended over
  these keys (row MS8).

**A second request while a build runs: 202, with no second build.**
- **How the lock is taken:** the first request creates the lock with R2's create-once write (`onlyIf` with
  `If-None-Match: *`). Cloudflare documents this as atomic: "at most one request succeeds".
- **Who builds:** only the request that created the lock builds. Any other request that finds a lock younger than
  `LOCK_STALE_MS = 45_000` gets **202, `in-flight`**, and no build starts.
- **Taking over a stale lock:** a lock older than that means its build died. The 45 s is longer than the most a build
  can live: the 2 s wait plus `waitUntil`'s 30 s. Such a lock is replaced with a conditional write on its own ETag, and
  only one taker wins.
- **What duplicates remain, stated:** before the KV key exists (below), a request knows no store id, so it cannot name
  a lock. On that path two concurrent first requests can each build. Each duplicate costs one build: 2.3-4.1 s of CPU,
  53 reads and 15.2 MB. That lasts only until `bm:store` is written.

**`waitUntil`'s limit, from Cloudflare's documentation (read 2026-11-05):**
- "`ctx.waitUntil()` can extend execution for up to 30 seconds after the response is sent or the client disconnects …
  shared across all `waitUntil()` calls within the same request. If any Promises have not settled after 30 seconds,
  they are canceled."
- A cancellation logs "waitUntil() tasks did not complete within the allowed time after invocation end and have been
  cancelled."

**How the build fits inside it:**
- A build starts at the request and is raced against `SYNC_WAIT_MS = 2_000`. If it finishes inside the wait, the answer
  is synchronous. If not, the request answers 202 and the build continues under `waitUntil`, so it has at most about
  32 s in all.
- **Observed builds:** 7.6-22.2 s, sequential. C1 shortens them.
- **On staging,** the cancellation line is counted in the proxy's Workers Logs, predicted at **0**, before anything relies
  on it (§4).

**C1, parallel reads, inside the build:**
- **The concurrency:** the 12 cells, and the 36 tiles with terrain-sources, are fetched **up to 6 at a time** (a fixed
  bound).
- **The order:** results are processed in the same order as today, so **the package bytes are unchanged.** The golden
  rows MD3a, MD4 and MG0b guard that, and they stay as they are.

**The device side:** goes into the Blipscope basemap client's plan as two requirements. That plan does not exist yet,
and no firmware calls `/basemap`:
1. a request must never wait on a build: it relies on the 202 and stays within the shared `Get()`'s 5 s;
2. it handles **202 by retrying after `Retry-After` seconds**.

## 2. O1: the store id from KV

**The key:** `bm:store` in `ENRICH_KV`, which staging and production already bind. Its value is the store id, 12 hex
characters.

**The writer:**
- **The ingest, after the seal:** `d1.ts`, after `publishStore` returns `sealed: true`. It is **never** written on a
  builder 200, as ruled (KV allows one write a second per key).
- **The already-sealed store `4c0875738f38`:** a one-time `wrangler kv key put --remote`, in staging first (Q-A1b).

**The reader:** the proxy's `/basemap` handler reads `get("bm:store", { cacheTtl: 30 })`. A value that is not 12 hex is
ignored, as if absent.

**What wins:** the store id from a builder 200 still teaches the isolate (as today), and a store id from the builder's
own answer wins over KV's for what is stored.

## 3. The handler, and the label each path writes on the request line (`bm`)

| path | condition | answer | `bm` |
|---|---|---|---|
| unchanged | no `BASEMAP` binding, or a bad query | 503 / 400 | none |
| revalidation, isolate | the isolate knows the store id, and `If-None-Match` matches | 304 | `304-known` (as today) |
| revalidation, KV | the isolate does not know it, KV does, and `If-None-Match` matches | 304 | **`304-kv`** |
| Cache API | the store id is known, and this data centre holds the copy | 200 | `hit` (as today) |
| stored | the store id is known, and `BASEMAP_PKG` holds the package | 200 (the copy also goes to the Cache API) | **`stored`** |
| stored error | the store id is known, and a `.err` is held | 404 / 422 | **`stored-error`** |
| build under the lock | the store id is known, nothing is stored, and this request took the lock | 200 / 404 / 422 if the build ends within 2 s; else **202, `Retry-After: 15`** | `known-miss` (as today's name) |
| in flight | a fresh lock is held by another request | **202, `Retry-After: 15`** | **`in-flight`** |
| no store id | neither the isolate nor KV knows it | today's synchronous build, and the result is then stored | `unknown-store` (as today) |
| no package bucket | `BASEMAP_PKG` unbound (production until D4) | **exactly today's behaviour**, never a 202 | today's labels |

## 4. The rows, frozen

**Where they go:**
- in `proxy/test/basemap-endpoint.test.ts`, a new `describe` "MS" placed **before** MP, so MP1-MP3 cover it;
- **MX1** beside it;
- **MI8** in `basemap-ingest.test.ts`.

**What MW and MK keep:** no `bm:store` in their KV, and their builds finish inside 2 s. So every existing MW and MK row
keeps its current assertions and labels.

| row | asserts |
|---|---|
| **MS1** | KV holds the store id. A **fresh** isolate's `If-None-Match` with that ETag gets 304, `304-kv`, and **0** builder calls |
| **MS2** | a package held in `BASEMAP_PKG` under its key. A fresh isolate (KV id) gets 200 with **those bytes**, `stored`, and 0 builder calls. The next request there is `hit` |
| **MS3** | a builder slower than 2 s. The first request gets **202** with `Retry-After: 15` and `known-miss`. After the background build, the package is stored under its key and the lock key is gone |
| **MS4** | with a fresh lock held, a second request gets **202, `in-flight`**, and the builder call count does not move |
| **MS5** | a lock older than 45 s: the request takes it over, and the builder is called **once** |
| **MS6** | a build finishing inside 2 s gets 200 synchronously, and the package is stored. A fresh isolate then reads it as `stored` |
| **MS7** | a `no_coverage` build finishing after the 202 is stored as `.err`. The retry gets **404, `stored-error`**, with 0 builder calls |
| **MS8** | every key written to `BASEMAP_PKG` matches `^bm/v1/(pkg\|pkg-lock)/[0-9a-f]{12}/[0-9a-f]{24}(\.err)?$`. No coordinate appears in any key or log line (`0.37`, `0.59`, 370000, 590000). CONTROL: at least 3 keys written |
| **MS9** | `BASEMAP_PKG` unbound: today's behaviour exactly (MW9's shape: 200, 1 call, 5 reads), and no 202 |
| **MX1** | C1: a store stub that delays each read sees **between 2 and 6** reads at once, and the package equals the sequential build's **byte for byte** |
| **MI8** | after a **sealed** publish, KV `bm:store` holds the store id. After an **unsealed** one, it is unset |

**Counts.** Main's whole suite at `867db6a` is **900 passed, 1 skipped (901)**, from #541's run. **#549 (`dd1246e`)
touched no proxy file**, which is re-read before the code push. Predicted after the change:
- **911 passed, 1 skipped (912)**, in the same 69 files;
- every existing row unchanged.

## 5. The sabotages, frozen (added to `sabotage-basemap.mjs`)

| sabotage | the plant | predicted failing set |
|---|---|---|
| **S-MS1** | the KV read skipped | {MS1} |
| **S-MS2** | the stored-package read skipped | {MS2, MS6} |
| **S-MS3** | the build always awaited, never raced against the wait | {MS3, MS7} |
| **S-MS4** | the lock ignored when held | {MS4} |
| **S-MS5** | a stale lock treated as fresh | {MS5} |
| **S-MS7** | errors never stored | {MS7} |
| **S-MS8** | the query's `lat0Micro` added to the package key | {MS8} |
| **S-MS9** | the bucket's absence not checked (a throw) | {MS9} |
| **S-MX1** | the reads made one at a time | {MX1} |
| **S-MI8** | KV written before the verify | {MI8} |

**Every existing sabotage keeps its set.** The anchors that this change edits are re-anchored and registered at the code
push, as S-W304LATE and S-PWRITE were.

**The drivers:**
- `baseline[all]` grows by exactly the 11 new rows: **159 cases**;
- the smoke and credits drivers run after TIME_WAIT is under 2,000, as before;
- every driver must read as predicted before the code push.

## 6. Staging, registered now (read after merge, in a slot)

**The deploys, in order:**
1. **The buckets and the KV write first:** create `blipscope-basemap-pkg-staging`, and write `bm:store` once in
   staging's `ENRICH_KV` (Q-A1a, Q-A1b).
2. **Then the proxy and the builder** (C1) to staging, from the merge commit by name, with each rollback target recorded
   first.

**The set, registered now: 10 cold queries** at 10 public airports, none built before: KSEA, KSFO, KLAX, KORD, KJFK,
KATL, KDFW, KMIA, KBOS, KMSP. Each gets:
- **the first request,** which predicts **202** (`known-miss`) in **under 2.5 s at p95**;
- **a second request about 2 s later,** which predicts **202, `in-flight`**, with no second builder call for that query;
- **a retry after `Retry-After`,** which predicts **200, `stored`**, in **under 2.5 s at p95**.

**Totals predicted:**
- **exactly 10** builder invocations;
- **0** cancellation lines ("waitUntil() tasks did not complete …") in the proxy's Workers Logs;
- **10** packages and **0** locks left in the bucket afterwards.

**Outside the set:** stop and report.

## CI, frozen (after #549)

**#549 (`dd1246e`) retired the eight `build (…-s3-146)` legs**, so every count from earlier runs is stale. #549's own
finished PR run (head `306f83736b70`) is **30 check runs: 27 success, 3 skipped**: the always-on set for a PR that
touches no `proxy/` file.
- **This predictions-only push (docs):** **30: 27 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.**
- **The code push** adds `workers`' `manifest-drift`, `test (proxy)` and `test (dashboard)`: **33: 30 success, 3 skipped,
  0 failed.**

Each is read from a finished run only.

## 7. Questions for the ruling

- **Q-A1a:** may I create `blipscope-basemap-pkg-staging` (`wrangler r2 bucket create`)? It is the production
  bucket's twin at D4.
- **Q-A1b:** the one-time `bm:store` write for store `4c0875738f38` in staging's `ENRICH_KV`, before the staging
  reading?
- **Q-A1c:** `SYNC_WAIT_MS` 2,000, `Retry-After` 15 s and `LOCK_STALE_MS` 45,000, as frozen above?
