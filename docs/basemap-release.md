# The release variant as the real builder (predictions first)

**A registration, pushed before the code.** The basemap builder lets each store read go once the build has taken it, instead of holding
every read until the loop ends. The readings behind it are in `docs/basemap-dense.md` (#622).

## The rulings it carries out

- **~04:39Z, Nov 11 (Q-RA2):** "If the variant becomes the real builder, that's its own PR and review."
- **~05:35Z:** workerd evidence belongs to the variant's own PR: one staging request, at KLGA (as #622's record states it).
- **~17:30Z, the orchestrator (Q-DP4), verbatim:**

  > Q-DP4: yes. The release variant's PR (with its one staging request), then the Q-W4 code PR, then D4's production plan.
  > - For the variant PR's registration: the staging request was ruled at KLGA, but the variant's largest local build is KJFK (64.6 MB,
  >   cell phase). Propose there whether KJFK joins it, and I'll rule.

## What changes

**One function,** `readInOrder` in `proxy/src/basemap/build.ts`. Each read's slot is cleared as the read is handed over:

```ts
// before
yield await pending[i]!;
// after
const v = await pending[i]!;
pending[i] = undefined;
yield v;
```

`pending`'s element type gains `| undefined`, and the doc comment says why. **Nothing else changes:**
- the same reads are issued at the same moments, in the same order;
- `decodeCell` copies every number and string into new arrays, so no view into a read survives it, and letting a read go frees all of it.

## Why, from #622's record

- **v0 holds every read until the loop ends.** At KLGA at the map client's query, all 60.91 MiB of cells are held by cell 16, and the build
  peaks there: **99.55 MiB (104.4 MB).**
- **The variant reads 60.02 MiB (62.9 MB), spread 0.03, in the ladder phase** (ruled ~05:26Z).
  - The plant, which keeps every read, restored v0's cell samples (PL1). So the cut is the reads it lets go.
- **At all 13 points, on the variant** (Q-DR3's DR0-copy): 38.72-61.58 MiB (40.6-64.6 MB). The largest is KJFK, in the cell phase.
- **The packages don't change.** Q-DR3's DR0 read the variant byte-identical to the repo's builder at the 9 points that fit, and with the
  same ladder at the 4 New York points.

## CI

- **No test in the workers pool can see memory:** it has no `gc()`. What CI pins is that nothing else moved: MX1 (the same result whatever
  order the reads arrive in), the golden row, and every MW row.
- **The proxy suite, predicted: 1,039 passed, 1 skipped, in 75 files,** the same as main's last proxy run (`d46c25c43244`). No test is
  added, and nothing else in `proxy/` has changed on main since.
- **Check runs, predicted: 35 = 32 success / 3 skipped (`promote`, `ship-alert`, `version`) / 0 failed.**
  - The baseline is #628's run at `1db0659e9a50` (01:24-01:38Z Nov 11), a PR that touched only `proxy/`. Touching `proxy/` adds the
    workers workflow's jobs to the 32 a docs-only PR runs.
  - It postdates the last change to the set of jobs. The only later workflow change, `e2c34daf` (#633), adds steps, not jobs.

## The local rows, on the PR's own builder (registered before they run)

**The instrument is Q-DR3's:** each build in its own Node process with `--expose-gc`; the controls C1, C2′ and C3 first; then settled
samples at the build's hooks (each cell, each tile, then tiles, terrain, layers and ladder) and once at the end. The bound is 0.785 MiB,
twice the largest spread between processes at one sample (0.392), measured for this configuration.

**The builder under test carries no hooks, so it runs as a scratch copy:**
- `proxy/src/basemap/` from the PR's code commit, with **only the seven sampling lines** added to `build.ts`;
- before anything runs, `diff` against the PR's `build.ts` must show those seven lines and nothing else, and every other file must be
  byte-identical to the PR's;
- **main's builder gets the same seven lines** for the control. The two hooked copies must then differ only in `readInOrder`.

The store is the local copy of `4c0875738f38`; the query is the map client's (r=162, ε 201,168 mm, no peaks, three levels). **Nothing
runs on staging.**

| row | what | predicted |
|---|---|---|
| **RL0** (the control that can fail) | main's builder at KLGA, one process | the build's largest **in the cell phase, at cell 16, within 0.785 of 99.51 MiB**; 422 with DR0's ladder (249,684 / 205,868 / 186,014 / 41,061) |
| **RL1** | the PR's builder at all 13 points, one process each | **packages byte-identical to Nov 8's DR0** at the 9 (KBOS `ce5b1ea7589e`, KMIA `000dde33cc42`, KSFO `399cb059f84a`, KSEA `69ff5b17e2ee`, KORD `b54fa953fef7`, KDTW `8976db34bf1a`, KMSP `863f7f2955a7`, KIND `f740fbc3f99e`, KJAC `b3f428e5860e`); **422 with #620's ladders** at the 4 (KJFK 238,588 / 196,364 / 176,124 / 40,366; KLGA as RL0; KEWR 256,169 / 210,479 / 189,029 / 41,370; Midtown 253,966 / 208,947 / 188,359 / 41,610); **each build's largest within 0.785 of Q-DR3's DR0-copy figure, in the same phase** (below) |
| **RL2** | the PR's builder at KLGA and KJFK at **ε 201,169 mm**, one more than the client's | 422, each with a non-increasing ladder ending over 32,768. **The ladders are not predicted:** they become the staging rows' prediction |
| **every process** | | C1 +32 ± 1; C2′ +32 ± 1, then within 1; C3 within 1; every sample settled; every object local; no build over 96 MB |

**Q-DR3's DR0-copy figures, the predictions for RL1** (MiB; the ladder phase unless named):

| KBOS | KJFK | KLGA | KEWR | Midtown | KMIA | KSFO | KSEA | KORD | KDTW | KMSP | KIND | KJAC |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 56.63 | 61.58, **cell** | 59.98 | 60.80 | 58.06 | 39.34 | 39.18 | 49.12 | 39.85 | 47.10 | 47.24 | 43.96 | 38.72 |

**Graded per build, stopping at the first off reading.** A child that crashes or prints no result is an instrument failure, and stops it.

| outcome | meaning | next |
|---|---|---|
| **RL0 and RL1 hold** (predicted) | the PR's builder is the variant #622 measured | CI, review, the merge in its turn; then the staging rows |
| RL0 off | the control can't separate the two builders, so RL1 would mean nothing | **stop**; RL1 is not run |
| RL1: a package or a ladder differs | the PR changes what is built | **stop** |
| RL1: a memory figure off | the PR's line is not the variant that was measured | **stop** |
| **anything else** | the instrument is suspect | **stop and report** |

## The staging rows (after merge; registered now, RL2's ladders frozen here before the request)

**When:** after the merge, once staging's builder (`blipscope-basemap-staging`) runs the merged commit. The deploy goes through
`deploy.sh` under the claim, in a slot, by whoever is ruled (Q-RV2). This session has no claim hook and deploys nothing unless ruled.

**Before the request:**
- **Staging's store id is read** (a read, nothing written). If it isn't `4c0875738f38`, RL2's ladders don't predict it: **stop**.
- **The ε is 201,169 mm.** Q-MEM2 already stored KLGA's answer at the client's ε, so the same query would be answered from the store
  without a build. One millimetre gives a fresh digest.

**The request:** KLGA (ruled), and KJFK if Q-RV1 says so, through staging's route as the test identity.
- **The script is a copy of Q-MEM2's scratch `fx-request.sh`** with its ε changed from 201168 to 201169. A diff must show that one value
  and nothing else.
- A 202 is retried after its `Retry-After`, at most three times.
- **The builder's tail is read alongside.** Its pid is recorded at launch, and only that tree is stopped.

| outcome, per query | what is seen | meaning |
|---|---|---|
| **SG-A** (predicted) | 422 `too_large`; the tail shows **one build, outcome Ok**, with a `bm_build` ladder equal to RL2's | **workerd built it on the variant inside 128 MB,** with the same answer. The margin isn't shown |
| SG-B | the tail says **Exceeded Memory** | the variant doesn't hold in workerd at that point. **Stop;** D4 waits |
| SG-C | the tail says **Exceeded CPU** (staging's `cpu_ms` is 14,530) | a different limit; memory is unanswered. **Stop and report** |
| SG-D | a 200 | one millimetre can't make it fit. **Stop** |
| SG-E | a stored 422 with **no build in the tail** | the digest wasn't fresh. **Stop and report** |
| SG-F | SG-A, but the ladder differs from RL2's | staging didn't run the build measured here. **Stop** |
| **anything else**, including a tail that shows nothing | | **stop and report** |

**What this cannot see:** the margin under 128 MB, since workerd reports no peak; and whether the isolate was cold.

## KJFK: proposed to join the staging request (Q-RV1)

- **It is the variant's largest local build:** 61.58 MiB (64.6 MB).
- **It is the only point whose peak is in the cell phase,** where letting reads go helps least.
- **It has never been built on staging at r=162.** KLGA has, under v0 (Q-MEM2).
- **On Nov 8, before the release,** KJFK's first build in a process read 97.3 MiB (102.0 MB), the cold-build outlier.
- **The cost:** one more build on staging and one more stored `.err`. KLGA's took 98 reads, 74.4 MB read, wall 8.2 s.

## The order

1. **This registration,** pushed before the code.
2. **The code commit,** then RL0 to RL2 locally, read into this file.
3. **CI, review, and the merge in its turn,** through `scripts/merge-by-rule.sh`.
4. **The staging deploy, as ruled, then the staging rows,** read into a docs follow-up.

## Questions

- **Q-RV1:** KJFK joins KLGA in the staging request (recommended)?
- **Q-RV2:** the staging deploy after the merge: who runs it, through `deploy.sh` under the claim, and in which slot?

## The local rows, read (2026-11-11, 17:49:03Z): stopped at RL0, an instrument failure, before any build

The run used the PR's code at `7a9b4168df46`, registered at `4b3933f6c1c1`. **One child ran, and the parent stopped itself (exit 1).
Nothing was graded.** The tree stayed clean, and nothing ran on staging.

- **The hooked copies passed every check first:**
  - each `build.ts` is its blob plus the seven hook lines;
  - every other file is byte-identical to its blob, and the same in the PR and on main;
  - the two hooked builders differ only in `readInOrder`, by exactly the PR's change.
- **The cause is the driver's own query parser.**
  - The child imports `parseQuery` from the hooked copy's `query.ts`, which imports `../wx/crop`.
  - The hooked copy holds only `proxy/src/basemap/`, so the child died on that import, before any control or build ran.
  - I reproduced it without a build, with a child given an unknown point: the same `Cannot find module '../wx/crop'`.
- **The builder is not involved.** `build.ts` and the seven files it imports stay inside `proxy/src/basemap/`.
- **My miss, with its cause:** the driver's imports were never run before the registered run. A child given an unknown point would have
  shown this in a second.

**Proposed (Q-RV3):**
1. The hooked copies also take `proxy/src/wx/crop.ts` from the same commit, byte-identical to it.
2. Before any build, the parent runs one import-only child per builder, with an unknown point. Each must refuse the point with its own
   error line, not die.
3. Then RL0 to RL2 run again from the start, with every row, prediction and bound as registered above.

- **Q-RV3:** re-run RL0 to RL2 from the start, with these two changes and nothing else?
