# `SYNC_WAIT_MS`, set from the 2.5 s target (a proposal, predictions first)

## The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-06)

From ~04:22Z:

> - Q-S2: the 2.5 s target stands, graded on the server's ms with nearest-rank p95 over the ten. KSEA's 2,530 ms is
>   recorded off, so that row is already off. Read the nine for the other rows as they come.
> - After the slot, the SYNC_WAIT_MS proposal: set it from the target, with its own predictions, as its own PR. D4's
>   production plan waits for it.

From ~04:32Z:

> 6. The 2.5 s row stays off (KSEA 2,530, KSFO 2,570). Keep reading it. The SYNC_WAIT_MS proposal comes after the slot,
>    as ruled.

## What the first request is made of (the slot's ten, `docs/basemap-a1.md` §9)

A cold query's first request does **everything before the race, then the race**. The race is the build against
`SYNC_WAIT_MS` (2,000). All of the pre-race work runs in sequence:
1. the store id, from KV (`cacheTtl` 60);
2. the ETag;
3. the Cache API match;
4. the package's R2 read, then its `.err` read;
5. the lock's create-once put.

**Every cold build took longer than the wait** (wall 4.2-8.4 s), so every first request was answered by the timer:
**its server `ms` is `SYNC_WAIT_MS` plus the pre-race work.**

| | KDFW | KMSP | KORD | KATL | KBOS | KJFK | KLAX | KMIA | KSEA | KSFO |
|---|---|---|---|---|---|---|---|---|---|---|
| first request, server ms | 2,250 | 2,307 | 2,310 | 2,317 | 2,336 | 2,397 | 2,419 | 2,473 | 2,530 | 2,570 |
| minus 2,000: the pre-race work | 250 | 307 | 310 | 317 | 336 | 397 | 419 | 473 | 530 | 570 |

**The pre-race work, by nearest-rank p95 over the ten (the maximum): 570 ms.** The spread is 250 to 570 ms.

**The alternative reading, and why the data does not exclude it:** part of the gap could be time after the timer fires,
in writing the 202, rather than before the race. The table cannot separate the two, since both land in `ms`. **It does
not matter for this proposal:** either way the cost is paid on every timer-answered request, so it subtracts from the
target in the same way.

## The proposal

**`SYNC_WAIT_MS` = the target, minus the measured p95 of the rest, minus a margin.**
- The margin is the measured spread, rounded up (320 ms, then up to the next 100).
- That gives 2,500 − 570 − 330 = **1,600 ms**.

**What it costs: nothing today.**
- `SYNC_WAIT_MS` decides only whether a build that finishes **inside** the wait is answered 200 at once, rather than 202
  and a retry.
- No cold build in the slot came close: the fastest wall was 4,237 ms.
- **The build itself is unaffected.** It runs on under `waitUntil` either way (Cloudflare's limit is 30 s after the
  response), and the slot's longest build ended 6.4 s after its 202.

**The alternatives, not proposed now:**

| | what | why not now |
|---|---|---|
| **S2** | read the package and its `.err` in parallel, and so shrink the pre-race work itself | a code change in the request path, with its own rows. It is worth proposing on its own if the margin proves thin |
| **S3** | `SYNC_WAIT_MS` = 0: always answer 202 once a build starts | it would give up the 200-at-once case entirely, the day builds get faster than the wait |
| **S4** | keep 2,000 and move the target | the target is ruled |

## The code change, and what it moves

- **One line in `proxy/src/basemap/handler.ts`:** `SYNC_WAIT_MS = 1_600`.
- **No test pins the value.** The two rows that touch the wait set `basemapTiming.syncWaitMs = 50` and restore
  `SYNC_WAIT_MS` afterwards (read: `test/basemap-endpoint.test.ts`).
- **No sabotage plant anchors on it.** The one plant near it anchors on the race line, which does not change (read:
  `scripts/sabotage-basemap.mjs`).
- **Predicted for the code PR:**
  - the whole suite unchanged at main's count;
  - the basemap driver's baselines and every set unchanged;
  - CI as a `proxy/` PR (34 check runs: 31 success, 3 skipped), with the baseline named then.

## The reading that grades it (staging, in a slot, after the code merges and deploys)

**A fresh registered set: ten cold queries never built before**, at the slot's parameters: KPHX, KLAS, KSLC, KPDX, KSAN,
KIAH, KMCO, KCLT, KDTW, KSTL.
- **Each gets the first request, and the second 2 s later.**
  - **Since the wait is now 1,600 ms,** the second request lands about 400 ms after the first was answered, while the
    build is still running.
- **Then the retry after `Retry-After`,** and the +50 s request: the same shape and the same instruments as §9.

| row | predicted |
|---|---|
| **the first request: 202 `known-miss`, under 2.5 s at p95** (server `ms`, nearest-rank over the ten) | **at most 2,170 ms** (1,600 + 570) if the pre-race work reads as it did. Under 2,500 unless it grows past 900 ms |
| the second: 202 `in-flight`, with no second build | 10 of 10 |
| the retry: 200 `stored` (or `stored-error` for a `too_large`, as pre-ruled ~04:32Z), under 2.5 s at p95 | as §9 |
| +50 s: 200, `stored` or `hit`, never `in-flight` | as §9 |
| cancellation lines | **0**, anchored as §9 and read with the fixed counter |
| builds, packages, locks | one build per query; one package or one `.err` per query; 0 locks |

**Outside the set:** stop and report. That includes any first request over 2,500 ms, which would put the pre-race work
outside what the ten showed.

## Questions

- **Q-SW1:** 1,600 ms, derived as above?
- **Q-SW2:** the registered ten for the reading?
- **Q-SW3:** S2 (parallel pre-race reads) proposed on its own later, only if the reading shows the margin thin?

**CI for this docs-only PR, predicted: 31 check runs: 28 success, 3 skipped (`promote`, `ship-alert`, `version`), 0
failed.** The baseline is #564's finished docs-only run (31 = 28/3), which postdates `84409651`, the last change to the
set of jobs.

## The code (predictions first)

### The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-06 ~15:07Z)

> 4. #566, approved to merge at 793b418322c0:
>    - Q-SW1: yes, 1,600 ms.
>    - Q-SW2: yes, the fresh ten, first requests at most 2,170 ms, graded on the server's ms at nearest-rank p95.
>    - Q-SW3: yes, but only if the margin proves thin.
> 6. Q-Q1, the order: D4's prerequisites first, each with predictions first:
>    1. the SYNC_WAIT_MS code, then its ten on staging;

### The change

**One constant:** `SYNC_WAIT_MS = 1_600` in `proxy/src/basemap/handler.ts`, with its comment naming this derivation.
Nothing else changes.

### Predictions

**Read first:**
- **No commit since main `62944845` touches `proxy/src` or `proxy/test`.** So main's whole-suite count read there still
  holds: **73 files (72 passed, 1 skipped); 976 tests: 975 passed, 1 skipped.**
- **The basemap driver's figures** are those recorded for A1 (`docs/basemap-a1.md`):
  - `baseline[all]` 159 cases in 17 files, `baseline[ingest]` 11, `baseline[mv]` 31;
  - 74 runs, all 71 sabotages as predicted.

| check | predicted |
|---|---|
| `tsc --noEmit` (`tsconfig.json` and `tsconfig.scripts.json`) | clean |
| **`sabotage-basemap.mjs`, whole** (gated as ruled: TIME_WAIT under 2,000, no other `vitest` or `sabotage-` process) | **unchanged:** baselines 159 / 11 / 31; **74 runs, 0 runtime failures, all 71 sabotages AS PREDICTED**, C-LOADFAIL BLIND. **No test or plant reads the value** (§ "The code change, and what it moves") |
| **`sabotage-smoke-basemap.mjs`, whole** (gated) | CONTROL **975 passed, 1 skipped, of 976**; S-SQ {SQ1, SQ2}; S-SQP {SQ-C}; each sabotage run counting 976; no raw output kept; ALL AS REGISTERED. **The TIME_WAIT margin under the 15,000 ceiling is reported** (ruled ~15:07Z) |
| `sabotage-credits.mjs` | **not run:** no input it reads changes (no credit, page or attribution file) |

**CI, baseline named: 34 check runs: 31 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.**
- **The baseline:** #565's finished run, a `proxy/` PR.
- It postdates `84409651`, the last change to the set of jobs. Main's last workflow change is `259e8688` (#558), steps
  only.

**After the merge:** the staging deploy of this commit and the fresh ten, as registered above. Each needs its own slot,
which I'll ask for.

**Outside the set:** stop and report.

### Read (2026-11-06, `a4edc83b`, Windows)

**The first attempt (15:53Z) was an instrument failure, not a reading,** recorded as ruled (~16:31Z, Q-CRLF).
- **What happened:** the basemap driver read `baseline[ingest]`, then crashed before any plant: `S-ISIZE: anchor found 0
  times`.
- **The cause:** this worktree's files had been re-written with CRLF by plain `git switch`es, since the system Git
  config sets `core.autocrlf=true`. The driver's anchors end in `\n`.
- **The fix:**
  - the 114 affected files (LF in the index, CRLF in the worktree) were re-written with
    `git -c core.autocrlf=false -c core.eol=lf`, with no shared config changed;
  - **the bytes, read in Python:** 0 CR in all 114;
  - **the anchors:** 71 of 71 plant cleanly, checked with the driver's own table and `plant()`.

**Then the frozen checks, every reading as predicted:**

| check | read | verdict |
|---|---|---|
| `tsc --noEmit`, both configs | clean | as predicted |
| **`sabotage-basemap.mjs`, whole** (wait clear 16:34:43Z: TIME_WAIT 42, no other `vitest` or `sabotage-` process; exit 0 at 17:24:41Z) | baselines **11 / 31 / 159** (17 files); **74 runs, 0 runtime failures; all 71 sabotages AS PREDICTED**, C-LOADFAIL BLIND among them; highest TIME_WAIT at a run's start 7,770 | as predicted |
| **`sabotage-smoke-basemap.mjs`, whole** (wait clear 17:26:38Z: TIME_WAIT 655; exit 0 at 17:32:27Z) | CONTROL **975 passed, 1 skipped, of 976** (start 85, peak 13,557). S-SQ {SQ1, SQ2} (start 388, peak 13,547). S-SQP {SQ-C} (start 427, peak 13,557). Each counted 976. No raw output kept. ALL AS REGISTERED | as predicted |

**The TIME_WAIT margin** (ruled ~15:07Z: reported with each driver run, and raised under 1,000): the peak was 13,557, so
**1,443 under the 15,000 ceiling**.

**The tree was clean after each run.**

## The fresh ten on staging (2026-11-06, 18:11-18:21Z): the target met; the first-request row off

### The rulings, verbatim (reviewer chat, pasted by Daniel)

From ~17:45Z:

> Q-SLOT -> granted, after the merge and its main CI:
> - Deploy staging from #572's merge commit, by name. Record the rollback target first.
> - Before deploying, name the Worker-input diff from staging's current source commit. If it holds more than #572's
>   handler.ts change, list those commits and confirm each was merged and approved.
> - Rows as frozen: first requests at most 2,170 ms (server ms, nearest-rank p95 over the ten, which is their maximum),
>   and the other rows as in section 9.
> - Staging only, so it may overlap blipscope-5c's 18:45Z bench window. Announce start and end.
> - Step 0's code still waits for the ten's reading.

From ~18:26Z:

> Q-SW4 -> yes on both. The first-request row is OFF and stays on record as your miss: the prediction assumed the
> pre-race work's earlier maximum, and it read 314-718 ms. The target is met: nearest-rank p95 2,318 ms, under 2,500.
> Every other row as registered.
>
> Q-SW3 -> yes, 182 ms is thin. The pre-race spread alone was about 400 ms, and its maximum rose from 570 to 718 ms
> between two sets of ten. S2 is wanted before D4: read the package and its .err in parallel, predictions first, then a
> fresh ten on staging in its own slot.
> - Order: step 0's code first (on D4's critical path, predictions registered), then S2. Both before D4.

### Before the reading

- **The slot,** announced at 18:11Z and ended at 18:21Z, staging only. It started after #572 merged (`551fd030`) and main's
  CI on it read 34 check runs: 32 success, 2 skipped, 0 failed.
- **The rollback target, recorded first:** proxy staging `4790510d-899e-4f63-847b-7079ae658ff9` (A1's deploy). **It was
  not used.**
- **The Worker-input diff from staging's running commit `9b511dc2`,** read from each Worker's own bundle list (a dry run
  with a metafile) at `551fd030`:
  - **the proxy's 55 inputs:** **one commit, #572**, changing one file, `handler.ts`: the constant 2,000 → 1,600 and its
    comment;
  - **the builder's 13 inputs:** **0 commits**, so it was not redeployed and stays on `cf2142c3`.
- **Deployed:** proxy staging **`d4b73151-0a02-4869-b881-0152debb92c9`**, from `551fd030` by name. `confirm-deploy` read
  `/healthz` = `551fd030`.

### The ten, read

Server `ms` and colo come from each request's own log line. Every request went through the test identity, with headers
on stdin.

| airport | first: `known-miss`, ms (colo) | second: `in-flight`, ms | retry, ms | +50 s, ms | the one build: reads, MB, wall ms | package / lock |
|---|---|---|---|---|---|---|
| KPHX | **2,318** (DFW; the first request after the deploy) | 380 | `stored` 189 | `hit` 36 | 59, 12.2, 3,532 | present / absent |
| KLAS | 2,055 (DFW) | 356 | `stored` 201 | `hit` 33 | 49, 12.4, 3,026 | present / absent |
| KSLC | 2,007 (DFW) | 407 | `stored` 311 | `hit` 27 | 62, 14.6, 3,762 | present / absent |
| KPDX | 1,916 (DFW) | 345 | `stored` 216 | `hit` 13 | 58, 20.3, 3,143 | present / absent |
| KSAN | **2,176** (SJC) | 278 | `stored` 283 | `stored` 159 | 46, 13.8, 2,370 | present / absent |
| KIAH | **2,315** (SJC) | 430 | `stored` 336 | `hit` 20 | 49, 16.0, 2,790 | present / absent |
| KMCO | 1,914 (SJC) | 285 | `stored` 302 | `stored` 139 | 50, 24.1, 4,411 | present / absent |
| KCLT | 2,045 (DFW) | 324 | `stored` 210 | `hit` 20 | 53, 42.1, 3,902 | present / absent |
| KDTW | 2,128 (SJC) | 275 | `stored` 185 | `hit` 206 | 63, 14.3, 5,175 | present / absent |
| KSTL | 1,942 (SJC) | 290 | `stored` 158 | `hit` 48 | 56, 18.2, 3,783 | present / absent |

| row | read | verdict |
|---|---|---|
| **the first request: 202 `known-miss`, at most 2,170 ms** (server `ms`, nearest-rank p95 over the ten = the maximum) | 10 of 10 `known-miss`; **p95 2,318 ms** (KPHX 2,318, KIAH 2,315, KSAN 2,176 over 2,170) | **off, my miss** (ruled ~18:26Z). The prediction assumed the pre-race work's earlier maximum (570 ms); it read **314 to 718 ms** |
| **the 2.5 s target** | 2,318 ms | **met**: 182 ms under. Ruled thin, so S2 is wanted before D4 |
| the second: 202 `in-flight`, with no second build | 10 of 10; **exactly 10 builds**, one per query, all 200 | as registered |
| the retry: 200 `stored`, under 2.5 s at p95 | 10 of 10; p95 336 ms | as registered |
| +50 s: 200, `stored` or `hit`, never `in-flight` | 8 `hit`, 2 `stored` | as registered |
| cancellation lines | **0** (anchor 40 request lines; a clean exit) | as registered |
| packages and locks | 10 packages present, 0 locks | as registered |

**Two readings of the off, and the data does not separate them:**
- **a cold isolate after the deploy:** KPHX was the first request after it;
- **plain variance in the pre-race KV and R2 reads:** KIAH, the sixth request, ran as long, and the high values came from
  both colos.

**Cold builds took 2.4 to 5.2 s of wall time here,** against 4.2 to 8.4 s in §9's set. The places and colos differ, so
this is not a controlled comparison.

**State after the slot:**
- staging runs proxy `d4b73151` (`551fd030`) and builder `cf2142c3` (`9b511dc2`);
- the bucket's new objects expire under the 30-day rule;
- the instruments, as run, are in `docs/basemap-a1/instruments/` (`a1-phase.sh`'s `ten` phase, and `a1-r2probe.mts`'s
  ten).

**Next, as ruled:** step 0's code, then S2. Both land before D4.

**CI for this records PR (docs only), predicted: 31 check runs: 28 success, 3 skipped (`promote`, `ship-alert`,
`version`), 0 failed.** The baseline is #569's finished docs-only run (31 = 28/3), which postdates `84409651`, the last
change to the set of jobs.

## S2: the package and its `.err` read in parallel (predictions first)

### The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-06 ~18:26Z)

> Q-SW3 -> yes, 182 ms is thin. The pre-race spread alone was about 400 ms, and its maximum rose from 570 to 718 ms
> between two sets of ten. S2 is wanted before D4: read the package and its .err in parallel, predictions first, then a
> fresh ten on staging in its own slot.
> - Order: step 0's code first (on D4's critical path, predictions registered), then S2. Both before D4.

### The change

**In `handler.ts`, a known store id's miss reads its package and its `.err` with one `Promise.all`.** It no longer reads
the `.err` only after the package read misses. Everything after the reads is unchanged:
- a valid package answers `stored`;
- a remembered 404 or 422 answers `stored-error`;
- otherwise the create-once lock, then the race.

**What it saves:** one sequential R2 read on every cold first request. The pre-race work's spread (314 to 718 ms across the
fresh ten) is what made the margin thin.

**What it costs, stated because a registered property changes.**
- `docs/basemap-cold-build.md` says each retry is served *"from one R2 read"*. Under S2 a stored request makes **two**
  reads (the package and its `.err`), in parallel.
- Its latency is the slower of the two.
- One more R2 Class B read per stored request: $0.36 per million beyond the free 10 million a month.
- **That line becomes "from one round of R2 reads".**

**The alternative, not proposed now:**
- **What it is:** keep a 404 or 422 under the package's own key, as a small JSON record in place of a package.
- **What it would give:** one read answers all three outcomes, and the stored path stays at one read.
- **Why not now:** it changes A1's key layout (`.err` goes) and MS7 and MS8's rows, which is more than the ruling asked.
  It is named here so that it is a choice, not a default.

### Rows

| row | what | predicted |
|---|---|---|
| **MS1 to MS9** | A1's rows, unchanged | green: every outcome is the same |
| **MS10** (new) | a known miss over a bucket whose `get` is delayed 100 ms and records when each read starts and ends | **the `.err` read starts before the package read resolves**: the two overlap. The answer is unchanged (202 `known-miss` under a shortened wait) |

**The plant:**
- **S-S2SEQ:** the reads made sequential again, so the `.err` read starts only after the package read resolves. Red set
  **{MS10}**.
- **Re-anchored, like step 0's four:** **S-MS2** ("the stored package never read") anchors the package-read line S2
  rewrites. It moves onto the new line with the same defect, and keeps its set {MS2, MS6}. Every other plant is checked
  with the anchor check, and a compile check of the planted file (the guard approved ~19:43Z, done by hand until its PR
  lands), before any run.

**Counts:**
- **The suite:** main's count at the branch base, plus 1 (MS10).
- **`baseline[all]`:** +1.
- **The driver:** one run more (S-S2SEQ), with every set as registered.
- **The smoke driver's CONTROL** follows the suite.

### The staging reading (in its own slot, after merge and deploy)

**A fresh registered ten, never built before,** at the slot's parameters: KPIT, KBWI, KMEM, KOKC, KABQ, KTUS, KRNO,
KBOI, KGEG and KOMA. **The same shape and instruments as the last ten.**

| row | predicted |
|---|---|
| **the first request: 202 `known-miss`** (server `ms`, nearest-rank p95 over the ten = the maximum) | **at most 2,318 ms**, the last ten's p95: S2 can only remove work. **Under the 2.5 s target** |
| the pre-race work (`ms` − 1,600, per request) | **reported, never graded:** its spread is the thing being measured, and two sets of ten have already moved its maximum |
| the second, the retry, +50 s, the builds, cancellations, packages and locks | as the last ten |

**Outside the set:** stop and report.

**CI for the code PR:** a `proxy/` PR, **34 check runs: 31 success, 3 skipped, 0 failed**. The baseline is named when
the PR opens, from the latest finished `proxy/` PR run that postdates `84409651`.

### Corrections found while writing the code, before any run (2026-11-06)

**These are registered before any run, each with its cause. The section above is kept as written.**

**1. S-MS2's set grows {MS2, MS6} → {MS2, MS6, MS10}.**
- **What I wrote above:** that it "keeps its set". That was wrong.
- **Why:** with the stored package never read, only the `.err` read happens, and MS10's control (both reads seen)
  fails.

**2. Three more existing plants' sets grow by MS10,** from evaluating every handler and query plant against it:

| plant | its set grows to | why MS10 goes red |
|---|---|---|
| **S-MKKMISS** | {MK4, MK6, MS3, **MS10**} | MS10's first request is labelled `known-miss` |
| **S-MS1** | {MS1, MS2, MS3, MS4, MS6, MS7, **MS10**} | a fresh isolate without KV's store id takes the unknown-store path, which builds synchronously: 200, not 202 |
| **S-MS3** | {MS3, MS7, **MS10**} | never raced, the first request waits for its build: 200, not 202 |

**Every other plant leaves MS10 green.** MS10 grades only the first request, which is answered before the build
completes.

**3. The guard, done by hand until its PR lands.**
- **The anchor check:** 72 of 72 sabotages plant cleanly against S2's code.
- **TypeScript's parser,** in memory: the unplanted `handler.ts`, S-MS2's plant and S-S2SEQ's plant all parse.

**4. `tsc --noEmit` is clean** for the code and MS10. **No test has run.** S2 runs after step 0's checks, as ruled.

### Merged with main, the guard's scope ruled, and frozen before the run (2026-11-06)

**The ruling, verbatim** (reviewer chat, pasted by Daniel, ~23:28Z):

> Q-DUP: (a). The guard's scope becomes the files the driver runs, basemap-*.test.ts, registered before it runs:
> - --selftest-dups still refuses a planted basemap-zz-dupcopy, and a new control shows that a duplicate outside basemap-* is
>   not refused.
> - The wx session's MP3 stays as it is.
>
> A gap your reasoning leaves open: the smoke driver runs the whole suite and grades failures by bare id prefix, with the
> file name dropped (FAIL_LINE). A namesake of SQ1:, SQ2: or SQ-C: in any other file would be counted as the real row, and
> could read "as registered" when the real row didn't fail.
> - Before S2's smoke run: read and record that SQ1:, SQ2: and SQ-C: each title exactly one row across all of test/. That's
>   a read, not code.
> - Later, in its own small PR: the smoke driver refuses to start unless each red prefix titles exactly one row across
>   test/, with a selftest plant in a non-basemap file. Add it to the follow-up list.
>
> Then freeze S2's count at 999 (998 passed, 1 skipped) and run, as you plan.

**The merge.** Main at `4bbfdb8ffd55` (#585) went into S2 as `1e779fd27c53`, with no conflicts. On the merged tree: `tsc` is
clean on both configs, all 75 anchors are found once each, and 74 planted files parse. C-LOADFAIL does not parse, by design.

**The guard's scope, read before the change and predicted after it.** `--selftest-dups` on the old scope printed control 1
duplicate (`MP3`), the planted copy 7, and exit 1. That reading proves the check can fail.

| `--selftest-dups` on the new scope | predicted |
|---|---|
| control | **0 duplicates** |
| the copy outside `basemap-*` | **0 duplicates: not refused** |
| the planted `basemap-zz-dupcopy` | **6 duplicates, all naming the copy** (the file's 6 distinct ids, by the guard's own regex) |
| both copies removed, exit | **true, exit 0** |

**The read before the smoke run.** On this tree, across all of `test/`, `SQ1:`, `SQ2:` and `SQ-C:` each title exactly one
row, all in `smoke-basemap-query.test.ts` (lines 47, 55 and 79). `SQ` appears nowhere else in a text file except the word
`SQUARE` in a comment, so no title built at run time can reach them.

**The counts, frozen.** Read from CI on `4bbfdb8ffd55`: the `workers` run 42309616163 reads 997 passed, 1 skipped (998).

| check | predicted |
|---|---|
| **`sabotage-basemap.mjs`, whole** | baselines **11 / 31 / 168** (17 files); **78 runs** (75 sabotages and 3 baselines), 0 runtime failures, **every one AS PREDICTED** with C-LOADFAIL BLIND among them, every set as registered; exit 0 |
| **`sabotage-smoke-basemap.mjs`, whole** | CONTROL **998 passed, 1 skipped, of 999**, 0 failed. S-SQ **{SQ1, SQ2}**. S-SQP **{SQ-C}**. Each run counts 999. No raw output kept. TIME_WAIT margin reported against the 15,000 ceiling |

**Anything else, including another count or a set that differs: stop, no fix, report.**

**How the run is gated:** the second driver starts only on the first's exit 0, after the usual clear check. If either has to
be stopped, its child pids are recorded first and the whole tree goes with `taskkill /T`.

**Added to the follow-up list, as ruled:** a small PR in which the smoke driver refuses to start unless each red prefix
titles exactly one row across `test/`, with a selftest plant in a non-basemap file.

### Read (2026-11-06/07, `a5fabe27ef88`): as frozen

| check | read | verdict |
|---|---|---|
| **`--selftest-dups`** | control 0; outside copy 0; planted copy 6, all naming the copy; copies removed; exit 0 | as predicted |
| **`sabotage-basemap.mjs`, whole** (wait clear 23:31:46Z: TIME_WAIT 37, no other `vitest` or `sabotage-` process; exit 0 at 00:22:53Z) | baselines **11 / 31 / 168** (17 files). **78 runs, 75 sabotages all AS PREDICTED,** with C-LOADFAIL BLIND among them. S-S2SEQ {MS10}; S-MS2 {MS10, MS2, MS6}; S-MS3 {MS10, MS3, MS7}; S-MS1 and S-MKKMISS with MS10, as registered. 0 runtime failures. Highest TIME_WAIT at a run's start 7,923 (gate 8,000); held for ports 1,537 s | as predicted |
| **`sabotage-smoke-basemap.mjs`, whole** (wait clear 00:25:06Z: TIME_WAIT 10; exit 0 at 00:31:16Z) | CONTROL **998 passed, 1 skipped, of 999**, 0 failed (start 10, peak 13,554). S-SQ **{SQ1, SQ2}** (start 373, peak 13,553). S-SQP **{SQ-C}** (start 1,968, peak 13,556). Both restored byte-identical. **ALL AS REGISTERED**, so each run counted 999 | as predicted |

- **The tree afterwards:** porcelain 0.
- **The TIME_WAIT margin:** the highest peak, 13,556, is 1,444 under the driver's 15,000 ceiling. Not under 1,000.

**CI for the code PR, predicted: 35 check runs: 32 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.** The
baseline is #585's finished run (35 = 32/3), a `proxy/` PR that postdates `caf63cc9`, the last change to the set of jobs.

**The suite on CI, predicted: 1,012** (1,011 passed, 1 skipped). CI runs the merge ref, so that is main's 1,011 (`workers`
on `387b93bbe5d3`, with no proxy source or test change since) plus MS10. This branch has not merged main again, so the 999
read above stands for this tree. The merge is clean, and none of main's newer commits touch the basemap's files.

## S2's fresh ten on staging (2026-11-07, 02:52-03:03Z): as predicted

**The first request's p95 fell from 2,318 ms to 2,078 ms,** under the 2.5 s target and under the registered "at most
2,318 ms". Every other row read as the last ten.

### Before the reading

- **The slot,** granted ~01:06Z, announced at 02:52Z and ended at 03:03Z, staging only. It started after #589 merged
  (`fa660869a026`) and main's CI on it read 35 check runs: 33 success, 2 skipped, 0 failed; suite 1,012.
- **One deployer:** no `wrangler` or `deploy.sh` process on this machine, and staging's last deployment was this session's.
- **The rollback target, recorded first:** proxy staging `d4b73151-0a02-4869-b881-0152debb92c9`. **It was not used.**
- **The Worker-input diff from staging's running commit `551fd030`,** read from each Worker's own bundle list (a dry run with
  a metafile) at `fa660869a026`. **The proxy's 51 inputs: 5 files, 5 commits.**

| file | commit | what it does in the proxy's bundle |
|---|---|---|
| `src/basemap/handler.ts` | #589 | **S2: the only behaviour change** |
| `src/basemap/budget.ts`, `build.ts` | #585 | nothing: the proxy takes only `MASK`, and the bundle holds no `fitBudget` or `newLadder` |
| `src/wx/products.ts` | #577, `a19a0b6b` | nothing: a type and an unused constant; the bundle holds no `mapObjectKey` |
| `src/provision.ts` | `38e5bb48` | nothing: a comment |

- **The builder's 9 inputs:** #585 changed 3 of them. **It was not redeployed** and stays on `cf2142c3`. Step 0's own reading
  needs the builder, in its own slot.
- **Deployed:** proxy staging **`aaf26850-90e7-4f57-b53b-b60bc6d6bca7`**, from `fa660869a026` by name. `confirm-deploy` and two
  reads of `/healthz` gave `fa660869`.

### The ten, read

Server `ms` and colo come from each request's own log line. Every request went through the test identity, with headers on
stdin.

| airport | first: `known-miss`, ms (colo) | second: `in-flight`, ms | retry, ms | +50 s, ms | the one build: reads, MB, wall ms | package / lock |
|---|---|---|---|---|---|---|
| KPIT | **2,078** (SEA; the first request after the deploy) | 262 | `stored` 195 | `hit` 24 | 50, 29.8, 3,378 | present / absent |
| KBWI | 1,893 (SEA) | 243 | `stored` 190 | `hit` 23 | 54, 50.1, 4,619 | present / absent |
| KMEM | 1,869 (SEA) | 257 | `stored` 187 | `stored` 240 | 52, 24.2, 3,939 | present / absent |
| KOKC | 1,826 (SJC) | 263 | `stored` 545 | `stored` 131 | 53, 17.0, 3,032 | present / absent |
| KABQ | 1,794 (SJC) | 237 | `stored` 233 | `hit` 32 | 50, 10.4, 3,017 | present / absent |
| KTUS | 1,820 (SJC) | 295 | `stored` 292 | `hit` 10 | 49, 8.6, 2,021 | present / absent |
| KRNO | 1,792 (SJC) | 318 | `stored` 361 | `hit` 69 | 53, 16.2, 2,743 | present / absent |
| KBOI | 1,838 (SEA) | 308 | `stored` 321 | `stored` 145 | 60, 15.4, 3,723 | present / absent |
| KGEG | 1,794 (SEA) | 195 | `stored` 639 | `hit` 16 | 64, 16.1, 3,657 | present / absent |
| KOMA | 1,937 (SEA) | 288 | `stored` 382 | `stored` 132 | 66, 16.3, 4,158 | present / absent |

| row | read | verdict |
|---|---|---|
| **the first request: 202 `known-miss`** (nearest-rank p95 over the ten = the maximum) | 10 of 10; **2,078 ms**; the other nine 1,792-1,937 | **as predicted:** at most 2,318 ms, and under the 2.5 s target |
| the pre-race work (`ms` − 1,600) | 192-478 ms; the maximum was the first request after the deploy | **reported, not graded,** as registered. The last ten's maximum was 718 ms |
| the second request | 10 of 10 `in-flight`, 195-318 ms, with **exactly one build per query** | as predicted |
| the retry | 10 of 10 200 `stored` | as predicted |
| +50 s | 10 of 10 200: 4 `stored`, 6 `hit`, never `in-flight` | as predicted |
| builds | **10 `bm_build` lines,** all 200: 49-66 reads, 8.6-50.1 MB, wall 2,021-4,619 ms | as predicted |
| packages and locks | **10 packages present, 0 locks** | as predicted |
| cancellations | **0,** with 40 request lines in the window (anchor ok) | as predicted |

**One figure beyond the earlier reads:** KBWI's build read 50.1 MB, the most any build has read so far (KJFK's 38.4 MB was the
earlier top). It does not change a row, and reads carry no egress charge. The client plan's load paragraph is updated.

**CI for this PR (docs-only), predicted: 32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`, `version`), 0
failed.** The baseline is #588's finished docs-only run (32 = 29/3), which postdates `caf63cc9`, the last change to the set of
jobs.

### Other Workers' deploys around the slot (added 2026-11-07, as the reviewer asked ~15:55Z)

**Nothing S2 read can depend on these deploys through a binding, code or stored data.** The one way left open is an account or
colo effect with no binding, which the ten cannot rule out (below).

**The deploys, from each Worker's own `wrangler deployments list`.** The slot ran 02:52-03:03Z, so two of the three came before
it opened.

| Worker | deployed | against the slot | what it shares with S2's path |
|---|---|---|---|
| `blipscope-wx-ingest-opuk` (production) | 02:40:57.668Z | 11 minutes before it | `blipscope-wx`, which `/basemap` never reads |
| `blipscope-wx-ingest-dev` | 02:42:13.681Z | 10 minutes before it | nothing |
| `blipscope-wx-ingest-dev` | 02:53:38.923Z | **inside it,** 56 s after S2's own (02:52:42.929Z) | nothing |

- **The other Workers:** `blipscope-proxy`, `blipscope-basemap`, `blipscope-wx-ingest` and `blipscope-dashboard` had no deploy
  between 02:35Z and 03:10Z.
- **The two Workers that served S2:** proxy staging's only deploy in that span was S2's own. Builder staging's last was
  2026-11-06 03:32:33Z.
- **At 02:53:38.923Z the ten had nothing in flight.** KPIT's retry had ended at 02:53:15Z, and its +50 s request came at
  02:53:48Z.

**What each reading rests on, from the four Workers' config files.** Those files have not changed since the slot.

| S2's reading | rests on | reachable by opuk or the dev Worker? |
|---|---|---|
| request `ms` and `bm` labels | proxy staging's code; `handler.ts` reads only `BASEMAP`, `BASEMAP_PKG` and `ENRICH_KV` | no |
| the store id | staging's `ENRICH_KV`; all 20 ETags in the output name one store | no: neither binds that namespace |
| builds | builder staging's code and `blipscope-basemap` | no: neither binds the bucket, and neither is a service either Worker calls |
| packages and locks | `blipscope-basemap-pkg-staging` | no: of the four Workers, only the proxy binds it |
| cancellations | proxy staging's own events | no |

- **The one shared resource is `blipscope-wx`,** bound by opuk and by proxy staging. In the code the slot ran, only `/wx` and
  `/healthz` read it. S2's 40 requests were all `/basemap`, and S2 used `/healthz` only for its commit.
- **The dev Worker** binds only `blipscope-wx-dev` and has no cron.

**What this does not exclude: an account or colo effect with no binding.** No mechanism for one is known, and the ten cannot
separate it:
- The nine first requests after 02:53:38Z read 1,792-1,937 ms, and KPIT's before it read 2,078 ms. KPIT was also the first request
  after S2's own deploy, so that split says nothing about the dev Worker.
- Such an effect would add time, so it could not have produced the pass: the row is a ceiling, at most 2,318 ms.
- **"Fell from 2,318 ms to 2,078 ms" above compares two different sets of airports and colos.** It describes the two sets and does
  not measure S2 alone. The graded row does not rely on it.

**Two corrections to "Before the reading" above, both my misses:**
- **"One deployer" reads wider than it was.** It was one read at 02:52:16Z, of this machine's processes and proxy staging's own
  list. It saw no other Worker's list and could not see a later deploy. No two deploys ran at once (S2's ran 02:52:37-02:52:45Z),
  but the slot's window did not hold one deployer.
- **The announcement did not reach the other session.** "Announced at 02:52Z" was a line in this session's chat. It reached the
  reviewer only with the report after the slot had ended, so the UK session could not have seen the start.

**For the combined slot:**
- **It starts on a go through the reviewer,** after blipscope-5c posts "#590 read back".
- **Its close reads every Worker's deployments in the window into the record,** eight Workers, with an unreadable list reported as
  unreadable, never as "none".

**CI for this addition (docs-only), predicted: 32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`, `version`), 0
failed.** The baseline is #592's finished docs-only run (32 = 29/3), which postdates `caf63cc9`, the last change to the set of
jobs. `c46af84f` touched a workflow since, adding steps only.
