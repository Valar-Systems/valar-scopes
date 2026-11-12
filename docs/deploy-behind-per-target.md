# deploy.sh's behind-check, per target: the bundler says what each Worker ships (predictions first)

## The ruling, verbatim

From ~17:12Z on 2026-11-05 (reviewer chat, pasted by Daniel; recorded in `docs/basemap-build-reads.md`):

> - The refusal stays in the record. deploy.sh's behind-check reads all of proxy/, so it fires on files the builder
>   doesn't use. Follow-up: propose narrowing it per target (proxy inputs vs builder inputs), with a sabotage.

From ~02:40Z on 2026-11-06:

> 8. The deploy.sh follow-up predictions doc: go ahead. Nothing deploys.

## The defect

`deploy.sh` refuses when the tree is behind `origin/main` in **any** commit under `proxy/`, for both targets (the
proxy, and the basemap builder with `--builder`). It does not ask whether the commit touches anything that target
bundles.

**The instance:** on 2026-11-05 at ~17:05Z it refused the builder's staging deploy at `048ed7b` because of one commit,
`788dab3c` (#543).
- That commit touched `proxy/scripts/check-wx-plant.mjs` and `proxy/wx-ingest-dev.wrangler.toml`.
- **Neither Worker bundles either file.** The refusal cost a ruling and a second slot, and it was cleared with
  `ALLOW_BEHIND=1`.
- That is the escape the script's own comment warns about: "a guard that fires on unrelated work teaches people to
  bypass it".

**The hand list is the weak form.** The ruling's own reading of the builder's inputs was "src/basemap,
basemap.wrangler.toml, the package files, tsconfig".
- It was right that day, and it is a superset: the builder does not bundle `src/basemap/handler.ts` or `query.ts`.
- A hand list goes stale the first time an import moves, and nothing re-checks it.

## The information is in the bundler

`wrangler deploy --dry-run --outdir <tmp> --metafile <tmp>/meta.json` builds exactly what a deploy would ship, deploys
nothing, and writes esbuild's metafile. Its `inputs` are every file in the bundle.

**Read first (2026-11-06 ~03:15Z, tree `c9f82358`, wrangler 4.35.0, `--env staging`):** both dry-runs exited 0, wrote
only into the scratchpad, and left `proxy/` clean.

| target | inputs | under `src/basemap/` | not in it |
|---|---|---|---|
| **proxy** (`wrangler.toml`, `main = src/index.ts`) | **51**, all repo files; 0 from `node_modules` | 10: `budget`, `build`, `format`, `frame`, `handler`, `layers`, `provenance`, `query`, `store`, `terrain` | `src/basemap/worker.ts`, the wx ingest Workers' modules, `src/types.ts` (types only, erased) |
| **builder** (`basemap.wrangler.toml`) | **9**, all repo files; 0 from `node_modules` | 9: the eight shared ones, plus `worker.ts` | `handler.ts`, `query.ts`, `src/index.ts` |

**Neither config has module rules, assets, wasm, text or data imports, or dynamic imports.** So the metafile is the
whole code input set.
- **Added to each target's set by hand, conservatively:** its wrangler config, `tsconfig.json`, `package.json` and
  `package-lock.json`.
- **Why the package files count:** the lockfile pins wrangler, and with it the esbuild that does the bundling.

**Why the HEAD tree's set is enough** (the named alternative, and why it is excluded):
- **The alternative:** a commit on main could change main's bundle while touching no file in HEAD's set.
- **Why it cannot happen:**
  - a main commit that **edits or deletes** a bundled file touches that file, which is in the set;
  - a main commit that **adds** a file changes the bundle only if a bundled file imports it, or the config's `main`
    names it, so it also touches a file in the set;
  - changes to wrangler or esbuild arrive through the package files, which are in the set;
  - anything outside esbuild's view (module rules, assets, a `[build]` command) would have to be added in the config,
    which is in the set.
- **What is left out deliberately:** `deploy.sh` and every other script. They are not bundled. Today's rule blocks on
  them, and this one only notes them (Q-DB1).

## The change, proposed

1. **`proxy/scripts/deploy-inputs.mjs <proxy|builder> <env>`** runs the dry-run into an OS temp dir, never under
   `proxy/` (an untracked file there would trip `deploy.sh`'s own dirty check). It prints the target's set, one
   repo-relative path per line.
   - **It refuses (exit 2)** when the dry-run exits non-zero, when the metafile is missing or unparseable, or when the
     config's `main` entry is **not among the inputs**. That last check is the anchor: a set that does not hold the
     entry point is not that Worker's set.
2. **`deploy.sh`'s behind-check** counts `git rev-list HEAD..origin/main -- <the set>`.
   - **It blocks on that count,** with `ALLOW_BEHIND=1` unchanged.
   - **Commits behind under `proxy/` but outside the set are printed** as "outside <target>'s bundle: not blocking",
     each with its files.
3. **When `deploy-inputs.mjs` refuses, `deploy.sh` falls back to today's rule,** all of `proxy/`, and prints why.
   **It never blocks less than today.** A broken probe costs a refusal, never a revert.

## Rows, predicted

The decision is checked **read-only against real history** where history has the case, and with **planted commit file
lists** against the real sets where it does not. Both run in `deploy-inputs.mjs --selftest`, which needs no network and
does not deploy.

| row | input | predicted |
|---|---|---|
| **DB1** | **the 17:12Z refusal, replayed:** HEAD `048ed7b`, base `788dab3c` (#543), target **builder** | **not blocking**; one note, `788dab3c`, outside the builder's bundle, naming its two files |
| **DB1-C** | CONTROL: **today's rule** on the same pair | **blocks: 1 commit** behind in `proxy/`. This proves the replay reproduces the refusal |
| **DB2** | the same pair, target **proxy** | **not blocking** either: #543's files are in neither set |
| **DB3** | planted: one commit touching `src/basemap/worker.ts` | builder **blocks**; proxy does not |
| **DB4** | planted: `src/basemap/handler.ts` | proxy **blocks**; builder does not |
| **DB5** | planted: `src/basemap/build.ts` | **both block** |
| **DB6** | planted: `proxy/package-lock.json` | **both block** |
| **DB7** | planted: `proxy/wrangler.toml` | proxy **blocks**; builder does not |
| **DB8** | planted: `proxy/basemap.wrangler.toml` | builder **blocks**; proxy does not |
| **DB9** | planted: `proxy/scripts/deploy.sh` | **neither blocks**; both note it (Q-DB1) |
| **DB10** | planted: `proxy/src/wx/ingest-worker.ts` (another Worker) | **neither blocks** |
| **DB11** | the dry-run planted to fail, with one planted commit touching `src/index.ts` | `deploy-inputs` refuses; `deploy.sh` **falls back to all of `proxy/`**, says why, and **blocks** |
| **DB12** | a metafile planted without the config's `main`, with the same planted commit | refused (the anchor); falls back and **blocks**, as in DB11 |

**On DB1 and DB2:** they use today's tree's sets, not `048ed7b`'s. The verdict is the same either way, because #543's two
files were in neither bundle then and are in neither now (read from the tree at `048ed7b`, when built).

**The sabotages,** each planted in a **copy** of the script, as `sabotage-smoke-basemap.mjs` does since #560. Each
plant's red set is exact:

| plant | the defect | turns red |
|---|---|---|
| **P-WIDE** | the set replaced by `proxy/` (today's rule) | {DB1, DB2, DB3, DB4, DB7, DB8, DB9, DB10}: each now blocks a target it should not |
| **P-EMPTY** | a computed set ignored (empty), the refusal path untouched | {DB3, DB4, DB5, DB6, DB7, DB8}: every row whose block comes from the set, which is the dangerous direction. DB11 and DB12 stay green, because a refusal still falls back |
| **P-NOANCHOR** | the `main`-entry check removed | {DB12} |
| **P-NOFALLBACK** | a refusal treated as "nothing behind" | {DB11, DB12} |
| **P-NOPKG** | the package files left out of the set | {DB6} |

**What runs, and what doesn't:**
- No deploy runs, before or after the PR.
- **The first real use is the next `deploy.sh` run in a slot.** Its printed set and notes are read before it uploads
  anything, and recorded in that slot's report.

## Questions for the ruling

- **Q-DB1:** commits that touch only unbundled files under `proxy/` (scripts, tests, other Workers' configs) **stop
  blocking** and are only noted. That includes changes to `deploy.sh` itself. Accept?
- **Q-DB2:** the fallback, when the bundler cannot answer, is today's rule (block on all of `proxy/`). Accept, or refuse
  outright?
- **Q-DB3:** the dry-run adds one bundle build per deploy (seconds; measured when built). Accept?

## CI for this docs-only PR, predicted

**31 check runs: 28 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.**
- **The baseline:** #557's finished docs-only PR run (head `d155686c`): 31, with 28 success and 3 skipped. Its first check
  run started 2026-11-06T01:02:16Z, after `84409651` (#552), the last change to the set of jobs.
- **Main's last workflow change** is `259e8688` (#558), which adds a step only.
- **The older figure of 30 (27/3) is stale.** It predates #552, whose `ota-identity` job also runs on docs-only PRs.

**Outside the set:** stop and report.

## The ruling on Q-DB1 to Q-DB3 (2026-11-06), and the code's registration (2026-11-12), before any code

**The ruling, verbatim.** It is the reviewer chat of 2026-11-06 ~04:22Z (the reviewer was the owner then), pasted by the owner to the map
server, item 5. It was recorded at the time in that session's notes and **not in this file until now**:

> 5. #562 approved at c063c1a5e614; Q-DB1 (config/tsconfig/package files kept by hand), Q-DB2 (fallback blocks on all proxy/, fail
> closed), Q-DB3 accepted; code = its own PR, predictions first.

**Its terms, recorded in the same session:**

> Q-DB1: yes for tests, other Workers' files and scripts no deploy runs. NO for deploy.sh and what it runs (deploy-inputs.mjs, the deploy
> lock's check when it lands). DB9 becomes "both block", and deploy-inputs.mjs gets its own row. Q-DB2: yes (a bundler that can't answer
> falls back to today's rule). Q-DB3: yes; record the dry-run's measured time.

**Carried, not re-ruled:** the orchestrator, 2026-11-12 ~16:33Z, carried Q-DB1 from 11-06 as the owner's own ruling. It **re-affirmed
Q-DB2 and Q-DB3 at 16:31Z** ("one bundle build per deploy, with its measured duration printed on every run"). `docs/map-release-plan.md`
said these were "not yet given"; that was wrong when it was written.

### What changes from the proposal above

**A third input set per target: the DEPLOY MACHINERY.** These are the scripts `deploy.sh` runs for that target. A commit behind on any
of them **blocks** that target, as today's rule does for every file under `proxy/`. The list is kept by hand, read from `deploy.sh` on main
(c73aee2f1a5b), and **checked by a row,** so it cannot go stale silently:

| target | the machinery | why each is in it |
|---|---|---|
| **builder** | `proxy/scripts/deploy.sh`, `proxy/scripts/deploy-inputs.mjs`, `scripts/session-claim.py` | the script itself; the set it computes; the claim check, run before the builder's branch exits (l.48-61, exit at l.179) |
| **proxy** | the builder's three, plus `proxy/scripts/confirm-deploy.sh` and `proxy/scripts/healthz-commit.sh` | `confirm-deploy.sh` runs only on the proxy's path (l.288), and it sources `healthz-commit.sh` (its l.62) |

- `scripts/session-claim.py` lies **outside `proxy/`**. Today's rule never counts it, and the ruling does ("the deploy lock's check
  when it lands": #618 landed it).
- **`scripts/test-confirm-deploy.sh` is not machinery.** `deploy.sh` names it only in a comment (l.276).

**Q-DB3 as ruled:** `deploy-inputs.mjs` times its dry-run, and `deploy.sh` prints `deploy-inputs: <target> dry-run <N> ms` on every run.

### The rows, as now registered

DB1 to DB8, DB10, DB11 and DB12 are **unchanged** from the table above. Then:

| row | input | predicted |
|---|---|---|
| **DB9** (as ruled) | planted: `proxy/scripts/deploy.sh` | **both block** (it is machinery for both) |
| **DB13** | planted: `proxy/scripts/deploy-inputs.mjs` | **both block** |
| **DB14** | planted: `scripts/session-claim.py`, outside `proxy/` | **both block**. Today's rule blocks neither, so this row is new behaviour |
| **DB15** | planted: `proxy/scripts/confirm-deploy.sh` | **proxy blocks**; the builder does not, and notes it as outside its bundle and machinery |
| **DB16** | planted: `proxy/scripts/healthz-commit.sh` | **proxy blocks**; the builder does not |
| **DB17**, the list's completeness | every script path that `deploy.sh` CALLS or SOURCES. Calls are found mechanically; a comment line is not a call | **each is in its target's machinery list**: for the builder's path, the calls before its `exit 0`; for the proxy's, all of them |

**The sabotages, with their red sets now exact:**

| plant | the defect | turns red |
|---|---|---|
| **P-WIDE** | the bundle and machinery sets replaced by `proxy/`, which is today's rule | {DB1, DB2, DB3, DB4, DB7, DB8, DB10, DB14, DB15, DB16}. DB9 and DB13 stay green, because `proxy/` covers them for both targets |
| **P-EMPTY** | the bundle set empty, the machinery and the refusal path untouched | {DB3, DB4, DB5, DB6, DB7, DB8} |
| **P-NOMACH**, new | the machinery list ignored | {DB9, DB13, DB14, DB15, DB16} |
| **P-CALLEE**, new | a copy of `deploy.sh` that calls one more script, not in any list | {DB17} |
| **P-NOANCHOR** | the `main`-entry check removed | {DB12} |
| **P-NOFALLBACK** | a refusal treated as "nothing behind" | {DB11, DB12} |
| **P-NOPKG** | the package files left out of the set | {DB6} |

**Before the code is written:**
- The plant list and its red sets are frozen here. **A plant that turns red on any row outside its set, or misses one in it, stops
  the PR.**
- Each plant goes in a **copy** of the script, never in `deploy.sh` itself (#560's pattern).

**What runs, and what doesn't:**
- No deploy runs for this PR.
- `deploy-inputs.mjs --selftest` needs no network: it reads history read-only, plants file lists, and computes the real sets.
- **The first real use is the next `deploy.sh` run in a slot.** Before it uploads anything, its printed sets, notes and dry-run time
  are read and recorded in that slot's report.

**CI for the code PR:** predicted at the code PR, from the then-latest finished proxy-touching PR run (#562's 31 is stale).
**Outside the predicted set: stop and report.**
