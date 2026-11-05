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
