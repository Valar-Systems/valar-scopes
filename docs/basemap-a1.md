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
| **MS8** | every key written to `BASEMAP_PKG` matches `^bm/v1/(pkg\|pkg-lock)/[0-9a-f]{12}/[0-9a-f]{24}(\.err)?$`. No coordinate appears in any key or log line (`0.37`, `0.59`, 370000, 590000). CONTROL: at least 3 keys written. *Under S-MS1 (KV skipped) it stays green: its covered first request teaches the isolate the store id, so its second request takes the known path without KV (ruled ~20:45Z).* |
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

## The code, and the corrections found before any registered run (2026-11-05)

The rulings are the ~19:05Z acceptance of #551's predictions, and Q-A1a-c. **Each change below was found while writing
the code or computing the sabotages' sets, before the suite or any driver ran as a registered reading.**

1. **KV's `cacheTtl` is 60, not 30.**
   - The local runtime this repo tests with (`wrangler` 4.35.0) refuses 30: "Invalid cache_ttl of 30. Cache TTL must
     be at least 60."
   - That refusal, caught by the read, came back as "no store id", and **every MS row that needs one failed** (MS1-MS4,
     MS6, MS7). Those failures are the control that the read works.
   - Cloudflare's runtime has accepted 30 since January 2026, but tests cannot exercise it, so the handler uses 60.
   - **The cost:** a new store id is seen up to about 60 s after it is written, against about 30 s. Stores change only on
     a re-ingest.
2. **MS1 and MS2 build nothing first.** MS1 computes the ETag with `etagOf`, and MS2 puts a package built by `build()`
   straight into the bucket. A first build through the endpoint would leave a Cache API copy, and MS2's request would
   then read `hit`, not `stored`.
3. **MS6 removes the Cache API copy between its two requests.** It stands in for "another data centre", where the
   Cache API does not hold the package.
4. **MX1 compares whole outcomes, a thrown error included**, not "ok". A plant that changes what the build makes
   (budget, terrain) then changes both builds alike, and MX1 keeps testing only C1.
5. **S-MS1's set is {MS1, MS2, MS3, MS4, MS6, MS7, MS8}, not {MS1}.** With the KV read skipped, no fresh isolate knows
   the store id. So every MS path that needs one falls back to today's synchronous build. MS5 still builds once, and
   MS9 never reads KV.
6. **S-MS8's set is {MS3, MS6, MS8}, not {MS8}.** The plant moves the package's key, and MS3 and MS6 read that key back.
7. **Three existing sabotages grow,** with the old sets kept on record in the driver:
   - **S-MKKMISS** {MK4, MK6} → **{MK4, MK6, MS3}**: MS3's 202 is labelled `known-miss`.
   - **S-MKHIT** {MK3, MK6} → **{MK3, MK6, MS2}**: MS2's second request is a `hit`.
   - **S-PLOG** {MP1} → **{MP1, MS8}**: MS8 reads every line for the origin.

**Every other existing sabotage keeps its set.** Each was checked against the new rows:
- the MS rows' labels and keys are their own;
- MX1 compares outcomes;
- MI8 is in the ingest's set alone.

**The development runs, not registered readings:**
- The basemap files read 162 passed, 1 skipped (151 before, plus the 11 new rows).
- Both typechecks are clean.

**What the registered run is predicted to read** (unchanged from #551, with the corrections above):
- **The whole suite:** 911 passed, 1 skipped (912), in 69 files.
- **`sabotage-basemap.mjs`:**
  - `baseline[all]` **159 cases in 17 files**, `baseline[ingest]` 11, and `baseline[mv]` 31;
  - **74 runs**: 71 sabotages and 3 baselines;
  - every sabotage AS PREDICTED (C-LOADFAIL BLIND); exit 0.
- **`sabotage-smoke-basemap.mjs`,** after TIME_WAIT is under 2,000: CONTROL 911 passed, 1 skipped, of 912, with
  S-SQ {SQ1, SQ2} and S-SQP {SQ-C}.
- **`sabotage-credits.mjs`,** after the same wait: CONTROL 35 of 35, with its four sets.
- **CI on the code push:** 33 check runs: 30 success, 3 skipped, 0 failed.

**Outside the set:** stop and report.

## The registered run, first pass (2026-11-05, 19:28-~20:25Z): stopped at two off readings, a third read after

**As predicted:**
- **Typechecks:** clean.
- **The whole suite:** **911 passed, 1 skipped (912)**.

**`sabotage-basemap.mjs`: the off readings.**
1. **S-ISIZE: OFF**, failing {MI7, MI8} against {MI7}.
   - S-ISIZE makes `verifyStore` compare sizes only. The new row's unsealed case is MI7's same-size corruption, so under
     the plant the store seals and `bm:store` is written.
   - **My miss:** the ingest family was not checked against the new row.
2. **`baseline[all]`: OFF**, 158 cases against 159.
   - **The cause:** the new row's id, **MI8, was already `basemap-regions.test.ts`'s** ("each county reads its
     AREAWATER and LINEARWATER…"). The driver grades by id, so the two rows counted once, and a red on either would have
     been indistinguishable.
   - **My miss:** MS, MX and MO were checked across the tree, but MI only in the ingest file.

**What was stopped:** the chain stopped the smoke and credits drivers. The basemap driver finished its restores, and
the tree was clean after.

**Read after the stop, not acted on:**
- 69 of 71 sabotages AS PREDICTED, among them:
  - S-PLOG {MP1, MS8}, S-MKHIT {MK3, MK6, MS2} and S-MKKMISS {MK4, MK6, MS3}, at their re-registered sets;
  - every S-MS but S-MS1;
  - S-MX1 and S-MI8.
- 74 runs, 0 runtime failures.

**A third off reading, S-MS1:** failing {MS1, MS2, MS3, MS4, MS6, MS7}, against the corrected set, which also held MS8.
**MS8 stayed green.**
- **The cause, my miss:** MS8's first (covered) request builds, and that build teaches the isolate the store id
  (`learnStore`).
- So its second request takes the known path, lock then `.err`, even with KV skipped. The "at least 3 keys" control
  holds.

### Ruled on the first two (reviewer chat, pasted by Daniel, ~20:05Z)

> A1's run: both offs are recorded as your prediction misses, and stopping with the restores finished was right.
> - Accepted: your row becomes MI9, after you check the whole test tree that MI9 is unused. S-MI8 becomes S-MI9 {MI9}.
> - Re-registered, miss kept: S-ISIZE {MI7, MI9}. MI9 going red under a sizes-only verify is correct.
> - baseline[all]: predicted 159.
> - One addition before the rerun: the driver refuses to run if any row id appears in two rows anywhere in the tree, and
>   prints the duplicate. Its sabotage: plant a duplicate id in a copy, and the driver must refuse. This class of miss
>   merges two rows silently, so it should be caught by the tool, not by review.
> - Then rerun the whole registered run from the suite onward, as before. A1 merges only after #540 is live in
>   production, as ruled.

### Done, as ruled

**The rename.** MI9 was checked across the whole test tree first: **0 uses**. The row is now **MI9**, S-MI8 is now
**S-MI9 {MI9}**, and **S-ISIZE is {MI7, MI9}**, with {MI7} kept on record in the driver.

**The duplicate-id guard,** in `sabotage-basemap.mjs`:
- **When:** before any baseline.
- **What it reads:** every **literal** test title under `test/`.
- **What it refuses (exit 2, each duplicate printed):** an id that names rows in two files, or two different `<ID>:`
  rows in one file. Sub-cases `<ID> (a): …` are one row.
- **What it does not see:** titles built at run time (`it.each`, template expressions). The baselines' case counts catch
  those, as they caught MI8.
- **Read-only, before it went in:**
  - on the tree as it was, it reported exactly **MI8: in basemap-ingest.test.ts and basemap-regions.test.ts**: the real
    collision, a free control that it can see one;
  - after the rename, **0 duplicates over 92 ids**.

**Its sabotage, `--selftest-dups`:**
- **The control:** the tree as it is must show 0 duplicates.
- **The plant:** a copy of `basemap-ingest.test.ts`, written beside it, must be refused, every duplicate naming the copy.
- **Cleanup:** the copy is then removed and its absence checked.
- **Predicted:** **control 0; the copy gives exactly 6 duplicates** (MI2, MI6, MI-column, MI-roundtrip, MI7, MI9: the
  file's literal ids); **all name the copy; the copy is removed; exit 0.**

**Open, for a ruling before the rerun: S-MS1's set.** Proposed, with the miss kept on record: **{MS1, MS2, MS3, MS4, MS6,
MS7}**. MS8 keeps its own S-MS8, and S-PLOG's set covers its logs.

### Ruled on S-MS1, and the rerun (reviewer chat, pasted by Daniel, ~20:45Z)

> Q-S1: yes. S-MS1 is re-registered as {MS1, MS2, MS3, MS4, MS6, MS7}, with the miss kept: MS8's covered first request
> teaches the isolate the store id, so its second request takes the known path without KV. Note that in the doc beside
> MS8.
> The duplicate-id guard is accepted, with its scope stated (literal titles only; the baselines' counts cover generated
> ones) and the old tree's MI8 collision as its free control.
> Rerun from the suite onward with your four predictions. If all read as predicted, push A1's PR with CI predicted at
> 33: 30 success, 3 skipped, 0 failed, and send it for review. #540 is now live in production, so A1's merge is no
> longer held for it. Both A1 and #548 change the proxy, so I'll set their merge order when both are ready.

**The rerun's four predictions, frozen before it starts:**
1. **`--selftest-dups`:**
   - control **0** duplicates;
   - the planted copy gives **exactly 6** duplicates, all naming the copy;
   - the copy is removed; exit 0.
2. **The whole suite:** **911 passed, 1 skipped (912)**.
3. **`sabotage-basemap.mjs`:**
   - the guard passes;
   - `baseline[all]` **159 cases in 17 files**, `baseline[ingest]` **11**, and `baseline[mv]` 31;
   - **74 runs**;
   - every sabotage AS PREDICTED (C-LOADFAIL BLIND); exit 0.
4. **The smoke and credits drivers,** each after TIME_WAIT is under 2,000:
   - smoke: CONTROL **911 passed, 1 skipped, of 912**, with S-SQ {SQ1, SQ2} and S-SQP {SQ-C};
   - credits: CONTROL 35 of 35, with its four sets.

## The rerun (2026-11-05, 20:35-21:38Z): as predicted through the basemap driver; the smoke driver's two plants unreadable

**As predicted:**
1. **`--selftest-dups`:** control 0; the planted copy gave 6 duplicates, all naming the copy; the copy was removed; exit 0.
2. **Typechecks:** clean. **The whole suite:** **911 passed, 1 skipped (912)**.
3. **`sabotage-basemap.mjs`** (20:38-21:28Z), exit 0:
   - the guard passed;
   - `baseline[all]` **159 in 17 files**, `baseline[ingest]` **11**, `baseline[mv]` 31;
   - **74 runs, 0 runtime failures**;
   - **all 71 sabotages AS PREDICTED**, among them S-ISIZE {MI7, MI9}, S-MS1 {MS1, MS2, MS3, MS4, MS6, MS7}, S-MI9
     {MI9}, S-PLOG {MP1, MS8}, S-MKHIT {MK3, MK6, MS2}, S-MKKMISS {MK4, MK6, MS3}, and C-LOADFAIL BLIND.

**Off: `sabotage-smoke-basemap.mjs`** (TIME_WAIT 162 at 21:32:06Z), exit 1:
- CONTROL **911 passed, 1 skipped, of 912**, as predicted;
- then **S-SQ and S-SQP: "INSTRUMENT FAILURE (timed out false, summary parsed false)"**, so "2 NOT AS REGISTERED".
- These are instrument failures, never reds. The chain did not start the credits driver, and the tree was clean after.

**Two explanations, and the data rules out neither:**
- **(a) Port exhaustion from the driver's own back-to-back whole-suite runs.** Its gate is under 8,000 at each run's
  start only (A2.12). **TIME_WAIT read 16,715 at 21:38:02Z**, just after.
- **(b) Another session's process on the PC.** It started **21:35:32Z**, with a command line matching `vitest` or
  `sabotage-`, under a shell snapshot that is not this session's. Only its name and start time were read, and it was not
  touched (G6). It overlapped the driver's two sabotaged runs.

The driver throws away the raw output of a run it cannot parse, the A2.12 follow-up, so which of (a) and (b) it was cannot
be read.

### Ruled on the smoke driver's off reading (reviewer chat, pasted by Daniel, 2026-11-06 ~00:56Z)

> 1. Q-R1, with a corrected CI prediction (point 2): re-read the smoke driver once, then the credits driver once, then
>    push A1's PR.
>    - The likely cause: blipscope-5c started a whole-suite run on the same PC at about that time, and its first run
>      died of port exhaustion (ConnectEx 1225 on all 71 files). That fits (a) and (b) together. It isn't proven, since
>      your raw output is gone. Two whole-suite runs at once was my scheduling miss.
>    - Conditions: TIME_WAIT under 2,000 (polled, capped at 20 minutes), and no other session's vitest or sabotage-
>      process running, by name and start time only. If either holds you past the cap, stop and report.
>    - If the re-read is off again, stop: the A2.12 follow-up comes first.
> 2. A1's CI is 34: 31 success, 3 skipped, 0 failed, not 33. `ota-identity` came in with #552 after the run you
>    predicted from; #548's finished run reads 34 on the same workflows. Rule for all sessions now: a CI baseline must
>    postdate main's last workflow change, named with the prediction.
> 3. The A2.12 follow-up (keep the raw output of an unparseable run; hold the TIME_WAIT gate over the whole run, each
>    with its own check) is its own small PR after A1's push.
> 4. Order: #548 merges and deploys first. A1's PR is reviewed on its merge with main including #548. A1 deploys only
>    after #548's production deploy is verified, with one deployer at a time.

### The re-read (2026-11-06, 00:58-01:06Z): as predicted

**The wait.** The PC was clear at 00:58:43Z: TIME_WAIT 24, and no process with `vitest` or `sabotage-` in its command
line. That was read from Windows' process list (id, name and start time only), with a control that the probe sees a
process.

**`sabotage-smoke-basemap.mjs`,** exit 0:
- CONTROL **911 passed, 1 skipped, of 912**;
- S-SQ {SQ1, SQ2} at TIME_WAIT 4,831, and S-SQP {SQ-C} at 333;
- "ALL AS REGISTERED".

**`sabotage-credits.mjs`** (clear again at 01:05:56Z), exit 0:
- CONTROL 35 of 35;
- S-CV-BLANKET, S-SA, S-RD and S-QC1 at their sets and counts;
- "ALL AS REGISTERED".

The tree was clean after.

**Every registered reading of A1's run has now read as predicted, so the code is pushed.**

**CI, predicted for the code push, with its baseline named (the ~00:56Z rule): 34 check runs: 31 success, 3 skipped
(`promote`, `ship-alert`, `version`), 0 failed.**
- **The baseline:** #548's finished run (head `f98e9b18`): 34, with 31 success and 3 skipped.
- **Main's last workflow change** is `12569fa0` (21:37:41Z). #548's run started at 21:35:36Z, two minutes earlier, so it
  does not strictly postdate it.
- **Why the count still holds:** `12569fa0` adds two **steps** to the existing `device-ids` job and no job, so the count is
  unchanged. Read from its diff.

## 8. After the merge: the conditions on A1, predictions first

### The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-06 ~02:40Z), points 3 to 5

> 3. #559 (A1) is approved to merge at 8656dbc0 (CI 34: 31/3/0, as predicted). Mark it ready. Before merging, wait until
>    main's CI on its current head has finished. Squash with --match-head-commit 8656dbc0, no --admin.
> 4. Three conditions on A1. None blocks this merge:
>    a. Keyed names. queryDigest is an unkeyed SHA-256 of the normalised query, whose centre is quantised to 0.01°.
>       Enumerating that grid (about 6.5 × 10^8 points per parameter set) takes minutes on a laptop. So every pkg, err
>       and lock name in BASEMAP_PKG maps back to a requester's location to about 1 km, with an upload time, which
>       defeats MP1/MP3's purpose in a persistent, listable store.
>       - Before any production BASEMAP_PKG binding: one keyed digest (HMAC-SHA-256 under a server secret; propose
>         which) for object names, ETags and cache keys alike.
>       - Check: the plain SHA-256 of the query key appears in no object name or ETag. Sabotage: the unkeyed digest
>         turns it red.
>       - Nothing shipped holds a basemap ETag, so no device re-downloads.
>    b. Retention, Daniel's decision (2026-11-06): stored packages are kept for 30 days, then deleted automatically.
>       - An R2 lifecycle rule, 30 days from upload: on blipscope-basemap-pkg-staging now, and on production's bucket at
>         D4. Read it back after it's set.
>       - A check that reads the rule, with a sabotage (the rule absent turns it red).
>       - This is Daniel's OK for that deletion rule only.
>    c. The staging slot is the first test of R2's real create-once semantics, since miniflare isn't R2. Register rows
>       that show:
>       - a cold query takes the lock, builds, stores, and the lock object is gone afterwards;
>       - a second request inside the build gets 202 in-flight;
>       - no query stays in-flight past LOCK_STALE_MS.
>       If the first row fails (If-None-Match: * not honoured), stop and roll staging back.
> 5. The slot: A1's staging deploy opens once Daniel pastes blipscope-5c's "#548 staging verified" line. It may overlap
>    the UK session's R3′, which is production-only and shares no Worker with it. Production stays one deployer at a time.

### #559's merge: main moved under it, so the ruled condition did not hold at the moment of merge

**What I did:**
- At 03:00:23Z I read main's head `7b70745a` as finished: 31 check runs, 29 success, 2 skipped, two reads 60 s apart.
- At 03:01:07Z I registered the prediction for main's push CI on #559, then marked #559 ready.
- At 03:01:44Z I merged it at `8656dbc0`. The squash commit is `9b511dc274c3`.

**What moved in between:** two other merges landed on main.
- `259e8688` (#558) at 03:00:29Z;
- `9db83163` (#556, the opuk split) at 03:01:01Z.

So the squash went onto `9db83163`, whose CI had not finished.

**My miss:** I did not re-read main's head immediately before merging. The condition is about main's **current** head,
and a read 80 s old is not a reading of it. This is the "read it twice" rule in this repo's `CLAUDE.md`, applied to a
base branch.

**What it changed, read from the artifacts:**
- **The squash tree equals the 3-way merge of `9db83163` and `8656dbc0`** (`git merge-tree`, tree `ca5be9462394`). It is
  A1, unaltered, on top of the two commits.
- **No file is touched both by A1 and by those two commits.**
- **#558 adds a step to `device-ids`, not a job,** so the set of jobs is unchanged since `84409651` (#552), and the
  registered main-CI prediction stands.
- **Main's push CI on `9b511dc2` is the first test of the combination,** and it is read below.

**#560 is held** until the reviewer rules on this. The procedure I propose for it: wait for main's head H to finish its CI,
then, in the same shell command as the merge, re-read `origin/main` and merge only if it is still H.

### 4b. Retention: the 30-day lifecycle rule (staging now; production's bucket at D4)

**Read first (03:03:31Z, `wrangler r2 bucket lifecycle list blipscope-basemap-pkg-staging`, exit 0):** one rule, R2's
default. `Default Multipart Abort Rule`: enabled, all prefixes, "Abort incomplete multipart uploads after 7 days".

**The command:**

    npx wrangler r2 bucket lifecycle add blipscope-basemap-pkg-staging bm-pkg-30d --expire-days 30 -y

- **No prefix,** so the rule covers every object in the bucket: the packages, their `.err` records, and any lock left
  behind.
- **`add`, not `set`.** `set` replaces the whole configuration and would drop R2's default rule.
- **What it sends,** read from wrangler 4.35.0's source: a rule `{ id: "bm-pkg-30d", enabled: true, conditions: {},
  deleteObjectsTransition: { condition: { type: "Age", maxAge: 2592000 } } }`, added to the existing rules.

**The check: `node scripts/check-pkg-lifecycle.mjs <bucket>`.**
- **OK (exit 0)** only when an **enabled** rule expires objects **after exactly 30 days**, its prefix covers
  `bm/v1/pkg/`, and no other enabled expiry rule covers that prefix.
- **MISSING (exit 1)** when no such rule exists. **CONFLICT (exit 1)** when one does, but a second expiry also covers the
  prefix.
- **BLIND (exit 2) unless wrangler both exited 0 and printed a result,** either a rule or its "There are no lifecycle
  rules" line.
  - Wrangler prints `Listing lifecycle rules for bucket '…'` **before** it fetches, so that line alone proves nothing.
  - A listing for a different bucket is BLIND too.

**Predictions, in the order they run:**

| step | predicted |
|---|---|
| **1. `--selftest`** (planted listings in wrangler's format; no wrangler call) | **12 of 12 as registered**, exit 0: C-OK OK. **L-ABSENT (the ruled sabotage: the rule absent) MISSING.** L-31 MISSING; L-OFF (disabled) MISSING; L-PREFIX (`bm/v2/`) MISSING; C-PREFIX (`bm/v1/`) OK; L-SECOND (a 7-day expiry beside ours) CONFLICT; L-NONE ("no lifecycle rules") MISSING. B-EXIT (exit 1 with a good listing) BLIND; B-AUTH BLIND; B-EMPTY (the listing line only) BLIND; B-BUCKET (another bucket's listing) BLIND |
| **2. the check on the real bucket, before the add, twice, a minute apart** | **MISSING, exit 1, both times,** listing only `Default Multipart Abort Rule`. This is the ruled sabotage's state on the real bucket, so the check is seen to go red against R2 itself, not only against a plant |
| **3. the add** | exit 0 |
| **4. the check after the add, twice, a minute apart** | **OK, exit 0, both times:** `Default Multipart Abort Rule` unchanged, and `bm-pkg-30d`: enabled Yes; (all prefixes); "Expire objects after 30 days" |

**Outcomes:**

| reading | then |
|---|---|
| all as predicted | record; at D4 the same command and check run on production's bucket |
| a selftest line off | stop before any wrangler call |
| step 2 not MISSING, or other rules present | stop; no add |
| the add exits non-zero | stop and report; no retry |
| step 4 not OK | stop and report. No second add and no removal without a ruling |
| **anything else** | stop and decide |

**Read (2026-11-06, registered at `f7bf74af`, 03:10:15Z; every step as predicted):**

| step | read | verdict |
|---|---|---|
| **1. `--selftest`** | 12 of 12 as registered, exit 0. L-ABSENT read MISSING; the four B- plants read BLIND | as predicted |
| **2. before the add** (03:10:29Z and 03:11:31Z) | **MISSING, exit 1, both times;** the only rule listed was `Default Multipart Abort Rule` | as predicted |
| **3. the add** (03:11:44Z) | exit 0, with an empty stderr: "Added lifecycle rule 'bm-pkg-30d' to bucket 'blipscope-basemap-pkg-staging'." | as predicted |
| **4. after the add** (03:11:53Z and 03:12:54Z) | **OK, exit 0, both times:** `Default Multipart Abort Rule` unchanged, and `bm-pkg-30d`: enabled Yes; (all prefixes); "Expire objects after 30 days" | as predicted |

**The rule is on `blipscope-basemap-pkg-staging` now.** Production's bucket gets the same command and check at D4.

### 4c. The slot's create-once rows (registered now, read in the slot)

These are added to section 6's set. They run first, before the other nine airports.

| row | what it shows | predicted |
|---|---|---|
| **SR1** (first) | **create-once is honoured.** KSEA, alone: two requests launched together, then the retry | one **202 `known-miss`** (it took the lock) and one **202 `in-flight`** (R2 refused its create). **Exactly 1 builder invocation** in KSEA's window. The retry reads **200 `stored`**. Then KSEA's **package reads present** and its **lock reads absent** |
| **SR2** | a second request inside the build | for each of the other nine, the request about 2 s after the first reads **202 `in-flight`**, with no second builder invocation (as section 6) |
| **SR3** | nothing stays in-flight past `LOCK_STALE_MS` | for each of the 10, a request **50 s after its first** reads **200 `stored`**. No `in-flight` line for any query comes later than 45 s after that query's first request |
| **SR4** | no lock is left | after the reading, for each of the 10: the **package reads present** and the **lock reads absent** |

**If SR1 fails, stop and roll staging back** (proxy and builder, to the versions recorded before the deploy).
- **The failure shapes:** two `known-miss` answers, two builder invocations, or a lock present after the build.
- **What each means:** `If-None-Match: *` was not honoured, or the lock was never deleted.

**The probe for SR1 and SR4:** `wrangler r2 object get <bucket>/<key> --remote --pipe`, per key.
- **Present** means exit 0 with bytes. **Absent** means exit 1 with wrangler's exact "The specified key does not exist."
  **Anything else is BLIND.** Both readings are from wrangler 4.35.0's source.
- `--remote` is required: without it wrangler reads a local bucket. `--pipe` is required: without it wrangler writes the
  object to a file named by its key.
- **The package read is the anchor:** a lock reads "absent" only for a query whose package read "present" in the same
  pass.
- **The keys come from the handler's own `parseQuery`, `queryDigest`, `pkgKey` and `lockKey`,** imported, never
  re-implemented.
- **Output:** one boolean pair per airport. No key, digest or coordinate is printed.

### 4a. Keyed names: the proposal (Q-A1d), for a ruling before any code

**K1, recommended: a new Worker secret `BASEMAP_NAME_KEY`** on the proxy, one per environment.
- **Its value:** 32 random bytes, generated in-session and sent to `wrangler secret put` on stdin. It is never printed
  and has no human copy.
- **Why no copy is needed:** losing or rotating it costs only cold builds. Packages are derived data. The old names stop
  being read, and the 30-day rule (4b) removes them.

**K2: derive it from an existing secret** (for example HKDF over `DEVICE_KEY_SECRET` with a label).
- It needs no new secret.
- **Not recommended:** it couples a device-key rotation to the map store, and production's `DEVICE_KEY_SECRET` has no
  held copy.

**The PR, after this one, predictions first:**
- One digest, `HMAC-SHA-256(BASEMAP_NAME_KEY, the normalised query key)`, for every pkg, err and lock name, the ETag and
  the Cache API key.
- **The check:** the plain SHA-256 of the query key appears in no object name, ETag or cache key, with a control that
  the keyed one does appear.
- **Its sabotage:** the unkeyed digest turns it red.
- **With `BASEMAP_PKG` bound but no key, the proxy does not store.** It fails closed, as MS9 does when the bucket is
  unbound.

**Staging and devices:**
- **The slot's reading can run on staging under unkeyed names**, since the ruling gates only the production binding.
  Those staging objects then age out under 4b.
- **No device holds a basemap ETag,** so no device re-downloads (the ruling's own note).

### CI for this PR, predicted

**34 check runs: 31 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.**
- **The baseline:** #560's finished run (a `proxy/` PR, 34 = 31/3, first check run about 01:52Z). It postdates the last
  change to the set of jobs, `84409651` (#552).
- **Main's last workflow change** is `259e8688` (#558), which adds a step only.
