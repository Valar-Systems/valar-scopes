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

## The ruling (from the orchestrator, 2026-11-11 ~18:20Z), verbatim (its #639 part)

> - RL0's stop at cea27d8bce2d is on record: an instrument failure, your miss.
> - Q-RV3: yes. Re-run RL0 to RL2 from the start with your two changes and nothing else.
>   - The hooked copies also take proxy/src/wx/crop.ts, byte-identical to its commit.
>   - The import-only check must be shown able to fail before it's trusted. Run it first on the OLD hooked copy, without crop.ts:
>     predicted to die on ../wx/crop. Then run it on the new copies: predicted to refuse the unknown point, per builder.
>   - Commit this re-registration before anything runs.
>   - This is RL0's first re-run. Another stop on RL0 goes to the owner (the retry cap).
> - Q-RV1: yes, KJFK joins KLGA in the staging request, for the reasons you gave. Register both in SG-A to SG-F before merge.
> - Q-RV2: you run the staging deploy yourself, after #639 merges, through proxy/scripts/deploy.sh under the claim. Never a typed
>   wrangler command, not even --dry-run. Before it starts:
>   - Read staging's /healthz commit and record it as the rollback target.
>   - SG-A to SG-F frozen and committed.
>   - Ask blipscope-5c (blipscope-93) whether it has anything in flight on staging or holds a proxy claim. Ask weatherscope
>     (blipscope-61) whether it depends on staging's current build (cfg:fleet rev 2 lives there).
>   - Announce the start to me, to blipscope-5c and to weatherscope.
>   - The slot: any time none of them objects. If deploy.sh has no staging path, stop and ask me.
> - #639's merge: #639 merges ONLY after my approval at its exact sha, and only through scripts/merge-by-rule.sh. A green CI, a
>   finished read, or the PR being next in order is not an approval. [...] When RL0 to RL2 read as predicted, send me the head and your
>   CI read; I review from git and approve at a sha, or I don't. If your head moves after an approval, the approval lapses.

## RL0 to RL2, re-registered (Q-RV3): registered before it runs

**This is RL0's first re-run.** Another stop on RL0 goes to the owner (the retry cap).

**What changes: two things, and nothing else.**
1. **`crop.ts`.** `proxy/src/wx/crop.ts` goes into each hooked copy's `src/wx/`, taken from that copy's own commit (`7a9b4168df46` for
   the PR's, `8f1a878f518f` for main's) and byte-identical to its blob.
   - Its only import is `import type { GridGeom } from "./grid"`. A type-only import is erased at runtime, so nothing more is needed.
   - A check refuses the step if anything else in the copies moves: the sha256 of every other file, before and after.
2. **The import check, before any build:**
   - **IC0, shown able to fail first:** on the hooked copies as they stand (no `crop.ts`), one import-only child per builder, given the
     unknown point `XXXX`. **Predicted: each dies on `../wx/crop`:** exit 1, no RESULT line, and its stderr names `../wx/crop`.
   - **Then `crop.ts` is added** (step 1).
   - **IC1:** the same child per builder on the new copies. **Predicted: each refuses the point:** exit 2, with one RESULT line whose
     error is `unknown point XXXX`.

**Unchanged, byte for byte:**
- the drivers `dr-child4.mts` (sha256 `a3dde2bd9473`) and `dr-rv.mts` (`cd071294893a`), last written at 17:48:19Z and 17:48:54Z,
  before the stopped run;
- the hooked builders mk-hooks.mjs made (`build.ts` `f60ae55f45d9` for the PR's, `bc22989950b4` for main's), checked again first;
- **every row, prediction and bound registered at `4b3933f6c1c1`:** RL0, RL1, RL2 and the every-process controls.

**The wrapper** `rv-rerun.sh` (sha256 `d91ec648e1d6`) runs IC0, the `crop.ts` step, IC1, then `dr-rv.mts`, stopping at the first off
reading.

| outcome | meaning | next |
|---|---|---|
| IC0: a child doesn't die on `../wx/crop` | the import check can't be shown to fail, so IC1 would prove nothing | **stop** |
| IC1: a child dies, or refuses something other than the point | the copies still miss something | **stop** |
| RL0 off | as registered, and the retry cap applies | **stop; it goes to the owner** |
| RL1 or RL2 off | as registered | **stop** |
| **every row as predicted** | the PR's builder is the variant #622 measured | the head and the CI read go to the orchestrator for its review |
| **anything else** | the instrument is suspect | **stop and report** |

## The staging rows, updated (Q-RV1, Q-RV2): registered before the merge

**KJFK joins KLGA (Q-RV1).** SG-A to SG-F apply to each of the two queries. RL2's ladders for both become SG-A's and SG-F's prediction,
frozen here once RL2 reads.

**The deploy (Q-RV2):** map-server runs it after #639 merges, with `proxy/scripts/deploy.sh --builder staging` under the deploy claim
`builder:staging`. The variant runs in the builder Worker (`basemap.wrangler.toml`, `src/basemap/worker.ts`), and `deploy.sh` has a
staging path for it (`--builder`). No typed wrangler command.

**Before it starts:**
1. **Staging's `/healthz` is read** (one GET). Its `commit` is the proxy's, and its `basemap.commit` is the builder's, as of the last
   build in that isolate. **`basemap.commit` is the builder's rollback target.** It is null until a build has run in that isolate; if it
   is null, the builder's current commit can't be read without a build, so **stop and ask**.
2. **RL2's ladders,** frozen and committed here.
3. **blipscope-5c (blipscope-93):** anything in flight on staging, or a proxy claim? **weatherscope (blipscope-61):** does it depend on
   staging's current build?
4. **The start is announced** to the orchestrator, blipscope-5c and weatherscope.
5. **The slot:** any time none of them objects.

**After the deploy:**
- `deploy.sh`'s output is the weak confirmation, since the builder has no `/healthz` of its own.
- **The proof is D3's binding test:** after the SG requests, staging's `/healthz` `basemap.commit` must equal the deployed stamp, and its
  `basemap.store` must be `4c0875738f38`. If either differs, SG-F's meaning applies: staging didn't run the build measured here. **Stop.**
- **Rollback is not automatic.** An off reading stops and goes to the orchestrator; the recorded rollback target is for when it's ruled.

- **Q-RV4:** the SG rows read the builder's outcome from its tail, `wrangler tail blipscope-basemap-staging` (read-only), as Q-MEM2 did.
  That is a typed wrangler command. May it run? Without it, SG-B (Exceeded Memory) and SG-C (Exceeded CPU) can't be told apart: both reach
  the route as a 503 or as 202s.

## RL0 to RL2, re-registered, read (2026-11-11, 18:27:50-18:27:55Z): stopped at RL0 again, by my registration's own contradiction

The re-registration was pushed at 18:27:44Z (`c698304da9c8`). **The wrapper stopped itself (exit 1) after one build.** The error file is
empty, the tree stayed clean, and nothing ran on staging. The drivers' sha256 read as registered first.

| step | read | |
|---|---|---|
| hooked copies | 22 files; the hooked `build.ts` files as mk-hooks made them; no `src/wx` | as registered |
| **IC0**, the PR's and main's | exit 1, no RESULT line, stderr names `../wx/crop`, for both | **as predicted** |
| `crop.ts` | added to both copies, 6,493 B, sha256 `efb15a0e3de8`, the same in the PR and on main; nothing else moved | as registered |
| **IC1**, the PR's and main's | exit 2, one RESULT line, `unknown point XXXX`, for both | **as predicted** |
| **RL0**, main's builder at KLGA | 422 with DR0's ladder (249,684 / 205,868 / 186,014 / 41,061); largest **99.18 MiB (104.0 MB) in the cell phase, at cell 16 of 21** | **stopped before it was graded** |

**What stopped it.**
- The parent checks every build against "no build over 96 MB" before it grades the build's row, and RL0's build read 104.0 MB.
- **RL0's own registered prediction is over that line:** 99.51 MiB is 104.3 MB. The registration contradicts itself. Its "every process: no
  build over 96 MB" row can't hold for RL0, whose job is to read main's builder holding every read.
- Read against RL0's row, every part falls inside it: |99.18 − 99.51| = 0.33 ≤ 0.785, at cell 16, in the cell phase, with DR0's ladder.
  **This is reported, not graded:** the run never graded it.
- RL0's phases (MiB): cell 99.18, tile 61.13, tiles 55.76, terrain 56.00, layers 59.11, ladder 59.16, end 8.04.

**My miss, with its cause:** I carried Q-DR3's 96 MB stop into "every process" without reading it against RL0's own prediction. In Q-DR3
every build was a candidate builder, so the stop fitted every build. Here RL0 is a control built to read high. Checking each row's
prediction against every other row before freezing them would have caught it.

**This is RL0's second stop, so it goes to the owner** (the retry cap, ruled ~18:20Z). Nothing runs until the owner decides.

**Proposed, for the owner:** the 96 MB stop applies to the PR's builder only (RL1 and RL2), not to RL0's control. RL0 is graded exactly as
registered, then RL0 to RL2 run again from the start. Nothing else changes; `crop.ts` stays in the copies, and IC0 has already shown the
import check can fail.

## The owner's decision (relayed by the orchestrator, 2026-11-11 ~18:55Z)

> #639: (a).

The orchestrator's ruling with it, verbatim:

> 2. #639: (a). The 96 MB every-build stop applies to the PR's builder only (RL1, RL2), not to RL0's control. RL0 is graded exactly as
>    registered. Re-run from the start, nothing else changed.
>    - Commit that re-registration before anything runs.
>    - Retry cap: this re-run is by the owner's ruling. Any further stop on RL0 goes back to him.
>    - #639 merges only after my approval at its exact sha, through merge-by-rule.sh.

## RL0 to RL2, re-registered under (a): registered before it runs

**One change, and nothing else:**
- **The parent is `dr-rv2.mts`** (sha256 `128d53f16713`). It is `dr-rv.mts` with one line changed: the 96 MB stop reads
  `if (which === "pr" && mb > 96)`. It applies to the PR's builder's builds (RL1, RL2), not to RL0's control.
- A check refuses the run unless `diff` between the two drivers shows that one line and nothing else.

**Unchanged:**
- the child `dr-child4.mts` (`a3dde2bd9473`);
- the hooked builders (`build.ts` `f60ae55f45d9` and `bc22989950b4`), with `crop.ts` as its blob in both, all checked first;
- **every row, prediction and bound registered at `4b3933f6c1c1`.** RL0 is graded exactly as registered: the build's largest in the cell
  phase, at cell 16, within 0.785 of 99.51 MiB, with DR0's ladder.

**Before any build, IC1 runs again** on the copies as they stand. Each builder's child must refuse the unknown point. IC0 can't run
again, because `crop.ts` is in the copies now; it already showed the check can fail.

**The wrapper** is `rv-rerun2.sh` (sha256 `480de0f033db`): the checks, IC1, then `dr-rv2.mts`, stopping at the first off reading.

**This re-run is by the owner's ruling. Any further stop on RL0 goes back to him.**

| outcome | meaning | next |
|---|---|---|
| **every row as predicted** | the PR's builder is the variant #622 measured | the head and the CI read go to the orchestrator for its review |
| IC1 off | the copies changed | **stop** |
| **RL0 off** | the control, as registered | **stop; it goes to the owner** |
| RL1 or RL2 off, or a PR build over 96 MB | as registered | **stop** |
| **anything else** | the instrument is suspect | **stop and report** |

## RL0 to RL2 under (a), read (2026-11-11, 18:59:55-19:00:07Z): RL0 as predicted; stopped at RL1, KJFK's memory, lower than predicted

The re-registration was pushed at 18:59:49Z (`bbd13fa1`). The wrapper's sha256 and every check read as registered. **Three builds ran,
and the parent stopped itself (exit 1).** The error file is empty, and nothing ran on staging.

| row | read | |
|---|---|---|
| IC1, the PR's and main's | exit 2, `unknown point XXXX`, for both | as predicted |
| **RL0**, main's builder at KLGA | 422 with DR0's ladder; **99.54 MiB in the cell phase, at cell 16 of 21** | **as predicted** (99.51 ± 0.785, cell 16) |
| RL1 KBOS | `ce5b1ea7589e`, Nov 8's DR0; 56.67 MiB in the ladder phase | as predicted (56.63 ± 0.785) |
| **RL1 KJFK** | 422 with #620's ladder, as predicted. **Memory: 58.52 MiB in the ladder phase** | **off: predicted 61.58 ± 0.785, in the cell phase** |

**What differs is the cell loop, and only the cell loop.** Q-DR3's figures came from the scratch copy with its release knob on. The
same copy, at the same point, read:

| phase (MiB) | KJFK, the copy (Q-DR3) | KJFK, the PR | KBOS, the copy | KBOS, the PR |
|---|---|---|---|---|
| cell | **61.58** | **55.27** | **50.87** | **47.23** |
| tile, tiles, terrain | 55.25, 55.14, 55.39 | 55.28, 55.18, 55.42 | 53.35, 53.23, 53.48 | 53.42, 53.30, 53.55 |
| layers, ladder | 58.47, 58.51 | 58.49, 58.52 | 56.53, 56.63 | 56.59, 56.67 |

- **After the cell loop, the PR and the copy agree to 0.07 MiB at both points.** Inside it, the PR holds 6.31 MiB less at KJFK and 3.64 MiB
  less at KBOS.
- **At KBOS the peak is in the ladder phase either way,** so its row held. At KJFK the copy's peak was in the cell loop, so the PR's lower
  cell loop moved its peak to the ladder phase, and the row went off.
- **The candidate, not established:** the copy wrote the release line inside `if (RELEASE) { … } else …`, and the PR writes it as three
  plain statements. #622's record already found the copy keeping one consumed read reachable during the cell loop (the cell-11 exception).
  A different code shape could let that read go. **The alternatives, and why the data leans against them:**
  - the load on this machine (another session's long PlatformIO rebuild): every sample is taken after a forced, settled `gc()`, and the
    phases after the loop agree to 0.07 MiB, so load doesn't explain a 6 MiB difference confined to the cell loop;
  - the copy's empty ε×4 and ε×8 sets: they exist in every phase, so they can't account for a difference in one phase.
  Neither is excluded by a measurement.

**This stop is RL1's, not RL0's,** so it goes to the orchestrator. **Proposed (Q-RV5), read-only and registered first:**
1. **The copy at KJFK again,** in Q-DR3's own driver, now: does it still read about 61.6 MiB in the cell phase? This tests the
   environment.
2. **A plant:** the PR's hooked builder with its release line rewritten in the copy's `if` form and nothing else. Does it read the copy's
   61.6? This tests the code shape.
3. **Then, by what those show,** re-register RL1 with the PR's own figures, or report the cause if neither explains it.
