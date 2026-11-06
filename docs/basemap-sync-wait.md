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
