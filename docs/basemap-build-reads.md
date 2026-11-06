# The builder's read instrument (D4 §3: BB1-BB3)

**Status: predictions, frozen and pushed before any code.** This builds the instrument proposed in
`docs/basemap-d4-plan.md` §3, as ruled. It ships to **staging only**, and its staging deploy waits for a slot.

## The ruling, verbatim (reviewer chat, pasted by Daniel, 2026-11-05 ~04:30Z)

> Q-D4b: yes. Build the builder's read instrument as proposed: counts and milliseconds only, BB1-BB3, each with its
> sabotage, predictions as frozen, staging only. Ask me for a slot before its staging deploy.

## The change (planned; none of it is written yet)

**`proxy/src/basemap/worker.ts`:**
- **The reader:** `r2Reader(bucket, stat?)` counts each read and times it into `stat`. It records the reads, the bytes,
  the total read time and the longest single read.
- **The line:** after `build()` returns, whatever its outcome, the builder writes **one** line:

      {"evt":"bm_build","status":<n>,"reads":<n>,"bytes":<n>,"readMs":<n>,"maxReadMs":<n>,"wallMs":<n>}

  Every value except `evt` is an integer. **It takes nothing from the query.**
- **Refusals:** a `405`, `builder_unstamped` or `bad_query` returns before any build, and writes no line.
- **The header comment:** "it logs nothing about the request" stays true. The line is about the build's reads, not the
  request.

**`proxy/basemap.wrangler.toml`:** `[env.staging.observability] enabled = true`, `head_sampling_rate = 1`, **staging
only**, the same as the proxy's staging config.
- **Found before any code:** Workers Logs holds **0** events from `blipscope-basemap-staging` over MM4's 15 builds
  (00:46-00:58Z). The same query against the proxy in the same window finds its 15 lines.
- So without this, the staging reading below would be BLIND.

**Tests:** three rows in `proxy/test/basemap-endpoint.test.ts`, in a new `describe` before MP, so MP1-MP3 run over them
too.

**The driver,** `proxy/scripts/sabotage-basemap.mjs`:
- three sabotages;
- its `caseOf` learns `BB<n>` ids;
- S-PWRITE is re-anchored (below).

## Three corrections to #539's frozen text, found before any run

1. **BB3's plant.** #539 says "the query's `lat2` added to the line". The builder's query has no `lat2`: it carries
   micro-degrees (`lat0Micro`). So the plant adds `lat0Micro`, and **BB3 checks both forms**: the strings `0.37` and
   `0.59`, and the values 370000 and 590000. It also checks the line's exact key set.
2. **S-BB1's set.** #539 says "one read left uncounted: BB1 reads 4 against 5". The plant leaves the `current` read
   uncounted, and that read is also one of `no_coverage`'s three. So **BB2 reads 2 against 3 as well, and the set is
   {BB1, BB2}.**
3. **The names.** The driver's `caseOf` grades only `M`-prefixed ids, so `BB1` would grade as UNNAMED. `caseOf` gains
   `BB\d+`, and the names stay as #539 froze them.
   - **Its control:** `baseline[all]` sees exactly 3 more cases.
   - Every existing sabotage keeps its set.

## The rows, frozen

| row | the request | predicted |
|---|---|---|
| **BB1** | a fresh isolate, MW's query, a 200 | that request's **one** `bm_build` line has `status` 200 and **`reads` equal to the store stub's own count (5, MW9's)**. CONTROL: `bytes` > 0. Every value is an integer, and `maxReadMs` ≤ `readMs` |
| **BB2** | MW12's query (`lat=10`), `404 no_coverage` | its one line has `status` 404 and **`reads` 3** |
| **BB3** | every `bm_build` line the file has logged by then | the keys are exactly `evt`, `status`, `reads`, `bytes`, `readMs`, `maxReadMs` and `wallMs`; every value except `evt` is an integer. No line holds `0.37` or `0.59`, and no value is 370000 or 590000. CONTROL: at least 3 lines checked |

**Counts.** Main's whole suite, read at `f04a49b` (whose `proxy/` is main's), is 68 files passed and 1 skipped (69), and
**897 tests passed and 1 skipped (898)**. Predicted after the change:
- **900 passed, 1 skipped (901)**, in the same 69 files.
- Every other row still passes.

**The search for text this change adds or removes:**
- The tests that read log lines are MP1 (the origin) and the MK rows. The MK rows filter on `evt:"req"`, so a
  `bm_build` line is not one of theirs.
- **MP1 stays green:** the line's values are integers, which cannot contain `0.37`.
- No test reads `worker.ts`'s source, or counts log lines.

## The sabotages, frozen

| sabotage | the plant | predicted failing set |
|---|---|---|
| **S-BB1** | the read of `bm/v1/current` left uncounted | **{BB1, BB2}** |
| **S-BB2** | the line written only on a 200 | **{BB2}** |
| **S-BB3** | `lat0Micro` added to the line | **{BB3}** |

**An existing sabotage, re-anchored: S-PWRITE.** Its anchor is the `build(r2Reader(env.STORE), ...)` line, which this
change edits. The anchor follows the new line. **Its set stays {MP2}:** the plant's write happens before the build, and
no BB row reads writes.

**Every other sabotage in the driver: unchanged.**
- MW9 (5 reads) and MW12 (3 reads) are in no frozen set except S-PETAG's MW9, and that one is about the ETag's shape. So
  no existing plant changes a read count.
- BB3 never reads an ETag.

**The drivers:**
- **`sabotage-basemap.mjs`:**
  - `baseline[all]` at **148 cases in 17 files** (main's 145, plus BB1-BB3);
  - **64 runs**: 61 sabotages and 3 baselines;
  - every sabotage AS PREDICTED (C-LOADFAIL "AS PREDICTED, BLIND"); exit 0.
- **`sabotage-smoke-basemap.mjs`,** after TIME_WAIT is under 2,000 (polled, capped at 20 minutes): CONTROL 900
  passed, 1 skipped, of 901; S-SQ {SQ1, SQ2}; S-SQP {SQ-C}.
- **`sabotage-credits.mjs`,** after the same wait: CONTROL 35 of 35, with its four sets unchanged.
- **Not run, with the reason:**
  - `sabotage-basemap-mc.mjs`: the sealed store's MC rows, from an ingest cache that this change does not touch.
  - `sabotage-tistarve.mjs`: Linux only; the test-identity selftest.
  - `sabotage-wx-plan-s.py`: it plants `src/wx/ingest-worker.ts` and `src/wx/ingest-dev-worker.ts`, and runs
    `test/wx-ingest-dev.test.ts` alone. None of those change here.

**CI**, as #534's set: **41 check runs: 38 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.** #537
added steps to `workers`, not jobs. The predictions-only push is the docs-only 38: 35 success, 3 skipped, 0 failed.

## The staging reading (registered now; read after the slot)

**The deploy:**
- `deploy.sh --builder staging` from main with this PR. **The proxy is not redeployed.**
- The builder's current staging version is read just before the deploy, as the rollback target.

**The requests:**
- As the test identity, with Weatherscope's query at KDEN: **three builds, each more than 60 s after the last**, so each
  reaches the builder.
- The proxy's own MK label says so: `unknown-store` each time.

**The instrument and its anchor control:**
- The builder's Workers Logs is read for `evt:"bm_build"` in those minutes.
- **The control:** each build's line must be found, with `status` 200. A build without its line is BLIND: stop.

**What it means:**

| the reading | means |
|---|---|
| `reads` exactly 57, all three times | (i): the R2 dataset under-counted MM4's builds. The count is the code's |
| `reads` varying between builds | (ii): builds differ. Find why before any parallel-read change |
| `readMs` under half of `wallMs` | the reads are not where the wall goes. The parallel-read decision is re-argued |
| none of the above | the instrument is not yet trustworthy: stop and decide |

**Read beside it, not graded:**
- R2's own count for the same minutes (`r2OperationsAdaptiveGroups`);
- the builder's GraphQL wall and CPU.

**The 2.5 s target is not graded here.** As ruled, it is the p95 of cold-build wall over a staging set registered
before it is read. That comes with the parallel-read change.

**Outside the set:** stop and report.

## The run (2026-11-05, 04:47-05:43Z): every prediction as frozen

**The code's own checks:**
- **Typecheck:** both configs clean.
- **The whole suite:** **900 passed, 1 skipped (901)**, 68+1 files, exit 0. Main's baseline at the same tree was 897 + 1.

**`sabotage-basemap.mjs`, the whole driver** (04:52-05:33Z, exit 0):
- Baselines: 10, 31 and **148 cases in 17 files**.
- **64 runs, 0 runtime failures.** The highest TIME_WAIT at a run's start was 7,945; held for ports 1,449 s.
- **All 61 sabotages AS PREDICTED**, among them:
  - S-BB1 {BB1, BB2}, S-BB2 {BB2}, S-BB3 {BB3};
  - S-PWRITE {MP2} on its new anchor;
  - S-W304LATE {MK2, MK6, MW10};
  - C-LOADFAIL BLIND.

**`sabotage-smoke-basemap.mjs`,** after TIME_WAIT 26 (05:35Z), exit 0:
- CONTROL **900 passed, 1 skipped, of 901**;
- S-SQ {SQ1, SQ2}; S-SQP {SQ-C};
- "ALL AS REGISTERED".

**`sabotage-credits.mjs`,** after TIME_WAIT 264 (05:42Z), exit 0:
- CONTROL 35 of 35;
- S-CV-BLANKET, S-SA, S-RD and S-QC1 at their sets and counts;
- "ALL AS REGISTERED".

The tree was clean after.

### The slot, ruled (reviewer chat, pasted by Daniel, 2026-11-05 ~05:10Z)

> #541's builder staging deploy and reading: the slot is 16:45-17:00Z, on these conditions:
> - #541 is approved and merged first. Send its report (CI read from a finished run, the sabotage drivers' results) for
>   my review before 16:30Z. If it isn't merged by then, the slot moves to after T5''s 19:05Z read.
> - Deploy from the merge commit by name, record the builder's rollback target first, and read the three KDEN builds as
>   registered. Anything outside the set: stop and report.
> - The slot is yours alone. If the UK's Plan S still has phases left to run after 16:45Z, it resumes once you report
>   done or at 17:00Z, whichever comes first.

## The staging reading (2026-11-05, 16:55-17:21Z): outside the set; stopped

**The relabel, as ruled (Q-BB1).** `readMs` and `maxReadMs` are **"read spans, including the CPU since the previous
read"**. In Workers, `Date.now()` advances only at I/O, so each span also holds the CPU work done since the read before
it. The spans add up to the build's wall. The measurement of waiting is **the builder's wall minus its GraphQL CPU, per
build**.

### The rulings, verbatim (reviewer chat, pasted by Daniel)

From ~15:08Z, the slot moved:

> Your builder slot moves: it starts when Daniel pastes blipscope-5c's "the PC is free" line, or at 16:55Z, whichever
> comes first, and it lasts at most 15 minutes. Everything else stands as registered: #541 merged first, rollback target
> first, the deploy from the merge commit by name, three KDEN builds more than 60 s apart, and done reported when
> finished. The 17:00-19:00Z Bend quiet block doesn't constrain you, because your builds touch nothing in Bend.

From ~17:12Z, after the refusal below:

> (A). I read it too: from 048ed7b to main, 0 builder inputs changed (src/basemap, basemap.wrangler.toml, the package
> files, tsconfig). The skipped commits are #543's two dev-Worker files, plus firmware and tools.
> - Deploy 048ed7b by name with ALLOW_BEHIND=1, builder only. Record the skipped commits and that 0-file builder diff
>   beside the rollback target (98800bfe).
> - New slot: 15 minutes from when you receive this. The UK's dev-Worker phases may run alongside: it's a different
>   Worker and shares nothing. Everything else is as registered: three KDEN builds more than 60 s apart, each bm_build
>   line found with status 200 as the anchor, graded by the frozen table, done reported.
> - The refusal stays in the record. deploy.sh's behind-check reads all of proxy/, so it fires on files the builder
>   doesn't use. Follow-up: propose narrowing it per target (proxy inputs vs builder inputs), with a sabotage.

From ~17:28Z, on the reading:

> #541's reading: recorded as outside the set, and stopping was right. Staging stays on proxy 3b12b9ea and builder
> 46addd75, with 98800bfe as the rollback target.
>
> - Q-BB1: accept the subtraction as the measurement: wall minus the builder's GraphQL CPU, per build. readMs is
>   relabelled in the doc as "read spans, including the CPU since the previous read", because Date.now() advances only
>   at I/O. No new per-read instrument for now. The next change is graded end to end on wall time.
> - Q-BB2: yes. Re-register the read count as 53, the builder's own count, which BB1 proves exact. The miss stays on
>   record. R2's per-minute dataset (57, 51, missing) is not used for per-build counts again.
> - What the reading changes for D4: CPU alone was 2.28-4.11 s per build, so parallel reads by themselves can't reach
>   the 2.5 s p95 target. Each build also read 15.2 MB to make one package. The next proposal, predictions first,
>   covers both: the reads' waiting (about 80% of wall) and the CPU and bytes behind it. If neither gets there, bring the
>   alternatives for a decision: pre-building, a longer device timeout, or a "202, retry" answer.
> - The deploy.sh behind-check narrowing stays a follow-up.

### Merged, and main's CI

- **#541** merged as `048ed7b` (approved at `4a0c455`, CI 41: 38 success, 3 skipped, 0 failed).
- **Main's CI on `048ed7b`** (15:25:46Z): **41 check runs: 39 success, 2 skipped, 0 failed.** It has the same 41 names
  as `20a0323`'s run, as predicted.

### The slot

**The start.** The slot began at **16:55:18Z**. No "the PC is free" line had come.

**The rollback target, read first:** **`98800bfe-c74f-4d12-8616-b7f073deccec`**, D2's staging builder (2026-10-30).

**The refusal, which stays in the record.**
- At 16:55:39Z, `./scripts/deploy.sh --builder staging` at `048ed7b` refused, exit 1: "this tree is 1 commit(s) behind
  origin/main in proxy/ … would REVERT these: 788dab3".
- Main had moved after #541's merge: `788dab3` (#543), `d9b1f19` (#542) and `975816d` (#544).
- **The builder-input diff,** `048ed7b..975816d` over `proxy/src/basemap`, `basemap.wrangler.toml`, the package files
  and `tsconfig`, is **0 files**. #543's `proxy/` files are `scripts/check-wx-plant.mjs` and
  `wx-ingest-dev.wrangler.toml`. #542 and #544 are firmware and tools.
- I stopped, with nothing deployed, and reported. Ruled (A).

**The deploy, as ruled.**
- At 17:16:53Z, re-read first: main still `975816d`, the same three skipped commits, 0 builder-input files.
- `ALLOW_BEHIND=1 ./scripts/deploy.sh --builder staging` at `048ed7b`, exit 0.
- **Builder version `46addd75-64dc-4570-bb9a-f01583bb3574`**, stamped `048ed7be9466cbbfb0f01b303d6826d525187b6f`.
- The proxy was not redeployed. `CLOUDFLARE_API_TOKEN` read absent in the shell and at user level (a boolean).

**The three KDEN builds.** As the test identity, each 75 s after the previous response. Stderr held 3 identity lines
naming fields only, and nothing else.

| | sent | HTTP | proxy `bm`, data centre | `bm_build`: reads | bytes | read spans / wallMs | longest span | builder wall, CPU (GraphQL) | wall − CPU |
|---|---|---|---|---|---|---|---|---|---|
| K1 | 17:17:09Z | 200, BMPK | `unknown-store`, SJC | **53** | 15,215,034 | 11,368 / 11,368 | 483 | 12.45 s, 2.28 s | 10.2 s |
| K2 | 17:18:37Z | 200, BMPK | `unknown-store`, SJC | **53** | 15,215,034 | 16,482 / 16,485 | 2,716 | 17.37 s, 4.11 s | 13.3 s |
| K3 | 17:20:11Z | 200, BMPK | `unknown-store`, LAX | **53** | 15,215,034 | 14,670 / 14,670 | 1,658 | 15.70 s, 2.79 s | 12.9 s |

**The anchor control held.** Each build's `bm_build` line was found, with status 200, and the proxy labelled each one
`unknown-store`.

**Graded against the frozen table: outside the set.**
- `reads` was **53** three times, not 57.
- The read spans were not under half of the wall: they **equal** it.
- So the reading is "none of the above". **Stopped.** No conclusion was acted on, and no rollback was needed: every
  answer was correct.

**What the ruling made of it:**
- **The read count is re-registered as 53**, the builder's own count, which BB1 proves exact. The "57" row stays on
  record as the miss. It was R2's per-minute figure for MM2's build, not the build's.
- **R2's per-minute dataset is not used for per-build counts again.** It read 57 and 51 for K1's and K2's minutes, and
  K3's minute was missing at 17:21Z.
- **The read-span row** could not fire, by construction, as above. Waiting is wall minus CPU: **10.2-13.3 s, about 80% of
  wall**, over 53 one-at-a-time reads.

**Where the environments stand:**
- **Staging:** proxy `3b12b9ea`, builder `46addd75`. The builder's rollback target is `98800bfe`.
- **Production:** unchanged, with no binding.

**Next, as ruled:** a proposal, predictions first, that covers both the reads' waiting and the CPU (2.28-4.11 s) and
bytes (15.2 MB) behind each build, against the 2.5 s p95 target. If neither gets there, the alternatives come back for a
decision: pre-building, a longer device timeout, or "202, retry".
