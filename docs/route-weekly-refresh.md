# The weekly route refresh: a plan (no code before the reviewer's review)

**Status: PLAN, accepted in direction by the reviewer at 2026-11-13 17:04:02Z, on seven conditions (below).** Nothing
here runs yet. The one-off refresh it follows is `docs/route-refresh-2026-11.md`, piece (A). The 10-day alert that
makes it urgent is piece (B).

**Accepted as written:**
- the key-level writer, which is mandatory;
- deletes only from the mirror's own proven snapshots;
- seal last by construction;
- G1-G6, each red-first, with removed shards as their own case;
- a refused delete means no seal;
- no override on a scheduled run;
- the schedule: Tuesday 05:47Z, staging then production through `needs:`.

**The conditions:**
1. **Code is registered before it is written.** The key-level writer, the delete path and `--no-seal`/`--seal-only`
   each get red-first rows, then CI, then a staging rehearsal, then production. It writes production on a schedule,
   so it gets full rigour.
2. **The first run is attended,** through `workflow_dispatch` with the registered overrides (G3, G4, G5), never the
   schedule.
   - Its delete count (about 830, as a count) and its write count are predicted.
   - Afterwards the deleted keys are verified ABSENT, because a bulk delete can drop silently too.
3. **The deadline:** the first run finishes before the 10-day alert fires, about 11-24 01:30Z if (A)'s production run
   lands after 11-14 01:30Z. If it slips, the alert firing is correct.
4. **Who reads it, ruled:**
   - a failed or refused run opens or updates a GitHub issue labelled `route-refresh`;
   - `reconcile-fleet.py` prints the count of open `route-refresh` issues;
   - ntfy to the owner's phone is offered to him as an option, not built by default.
5. **Cost is checked on staging first:** a key-level staging run's writes are predicted, then read from analytics,
   before production is scheduled.
6. **`ROUTES_FREEZE_UNTIL`:** the plan states who sets it and how, and a red-first test proves that a set freeze
   actually skips the run. A freeze that is quietly ignored is worse than none.
7. **The test identity as CI secrets is the owner's call** (a credential), and has been put to him.
   - Until he answers, (C) runs with the live-path read-back off, and that step reports **"not run"**. It is never
     skipped silently.

## 0. The requirement

The owner, 2026-11-13 15:35Z:

> "Then plan the weekly refresh that nothing runs today. It must remove routes the source has dropped (the 644 since
> August), not keep them, since a wrong route is worse than none."

**The deadline.**
- (B) makes `/healthz` call the copy stale once it is more than 10 days old.
- (A)'s production seal is expected early on 11-14, so the alert fires from about 11-24.
- (C) must have finished a successful run before then. If it slips, the alert firing is correct, and the limit is not
  loosened to hide it.

## 1. What the source does, measured (counts only; `standing-data` snapshots compared offline)

| span | keys added | values changed | keys dropped | writes a key-level writer needs | writes the shard-level writer needs |
|---|---|---|---|---|---|
| 1 day (11-11 to 11-12) | 130 | 314 | 0 | 444 | 244,026 (111 shards) |
| 1 week (10-29 to 11-05) | 4,064 | 9,803 | 4 | 13,867 | 503,141 (373 shards) |
| 1 week (11-05 to 11-12) | 1,041 | 8,983 | 3 | 10,024 | 448,363 (314 shards) |
| 79 days (08-25 to 11-12) | 10,680 | 26,106 | **827** | 36,786 | 568,226 (634 shards) |

**The 79 days' 827 drops come in two kinds:**
- **715 from shards that still exist.** One shard alone lost 627 keys, 94% of it. Only three shards lost more than 10%.
- **112 from 5 shards that were removed entirely**, the largest holding 50 keys.

In the two ordinary weeks, no surviving shard lost more than 0.3% of its keys, and no shard was removed.

**Why the owner's figure was 644.** The current ingest prints a NET per-shard shrink, over shards that still exist. A
shard that gains some callsigns and loses others hides its losses, and a removed shard is never visited at all. The
exact set difference is 827.

## 2. The design

### 2.1 What runs it, and where

- **A new workflow, `.github/workflows/routes-refresh.yml`**, scheduled weekly, plus `workflow_dispatch` (an `env`
  input and a `dry-run` input).
  - Proposed time: **Tuesday 05:47Z**, an hour after the source's daily 04:49Z commit. That keeps it off Monday, when
    `refresh-data.yml` writes the same two namespaces.
  - `concurrency: routes-refresh`: two runs never overlap.
- **Staging first, then production,** with `needs: staging`. This deliberately differs from `refresh-data.yml`'s
  `fail-fast: false`: production never runs after a staging failure or refusal.
- **A freeze for recordings.** The job honours a repository variable `ROUTES_FREEZE_UNTIL` (an ISO time). Before that
  time it writes nothing and says so in the step summary. Whoever schedules a recording sets it, because a route
  change mid-recording alters the cards being recorded.

### 2.2 Credentials: the owner's gate

| what | where it comes from |
|---|---|
| KV writes | the existing repository secret `CLOUDFLARE_API_TOKEN` (Workers KV Storage: Edit), which `refresh-data.yml` already uses every Monday. No new KV credential. |
| the live-path read-back (the ingest's sentinel and canaries) | the **test identity**, which today exists only in the token file (`resolveTestIdentity` has no CI path). It needs three new secrets (`test-device`, `test-key`, `test-key-staging`), written by the job into a 0600 token file outside the workspace. **Putting the test identity into CI is the owner's decision.** Without it, the job can prove its writes only by direct KV reads, which the ingest's rule 3 calls the weaker proof. |

### 2.3 The writer: key-level, not shard-level

At the shard-level writer's 448,000-503,000 writes a week, two environments come to **3.9-4.3 M writes a month, about
$17-20 a month over the allowance** with no other change. A key-level writer needs **10,000-14,000 a week per
environment**.

So the weekly job writes only added and changed keys, and deletes only dropped ones:
- **The new source is pinned by commit.** It is resolved once, at the start of the run, through the GitHub API, then
  downloaded by that sha. Its commit and its tarball's sha256 go into the new meta (`sourceCommit`, `sourceSha256`).
- **The previous source** is the commit recorded in the current meta, downloaded by its sha.
- **It is PROVEN to be what the mirror wrote** by re-hashing every shard under the current rule and comparing with
  `meta:routes`' `shards`. Every shard must be equal, or the run deletes nothing.
- **The changes:**
  - writes = keys added, plus keys whose value changed;
  - deletes = keys in the previous snapshot that are absent from the new one, plus any `pendingDeletes` carried in meta
    from a refused run.
- **The delete list never comes from a KV listing of `rt:`.** That prefix also holds the Worker's TTL'd runtime rows
  (`rt:<cs>:<bucket>`, and bare `rt:<cs>` negative entries), and only the mirror's rows are ours to delete.
- **The first run has no `sourceCommit` in meta.** Its previous snapshots are given explicitly: `dff309eb` (08-25) and
  `24ce5424` (A's source). Each has already been proven against a manifest:
  - **dff309eb:** the pre-(A) manifests, saved 2026-11-13 16:53Z. Production matched on 1,575/1,575 rule-3 hashes,
    staging on 1,575/1,575 plain hashes, and the counts matched in both.
  - **24ce5424:** (A)'s own manifest, after (A).

### 2.4 Seal last, by construction

- **`--no-seal`** writes and deletes, and never touches `meta:routes`.
- **`--seal-only`** writes meta and nothing else. It refuses unless the same job holds a verify receipt for the same
  `sourceCommit` and environment.
- **The job runs:**
  1. the ingest with `--no-seal`;
  2. the verify;
  3. a repair, which rewrites the named keys (`--force-shard`, or a new `--force-key`);
  4. the verify again;
  5. `--seal-only`.
  A run that stops anywhere before step 5 leaves the previous meta, so `/healthz` keeps ageing and (B)'s alert stays
  honest.
- **The verify gains one check: every deleted key must now be ABSENT.** Presence of the new keys alone does not cover
  a delete that silently failed.
- **Red-first:** a forced verify failure (a planted missing key) must leave `meta:routes` byte-identical. The control
  is that the same run without the plant seals.

### 2.5 The deletion guards

Each guard is red-first: a synthetic source plants the failure, the guard must refuse, and a control without the plant
must pass. **A scheduled run can never override a guard.** An override exists only for an attended
`workflow_dispatch` or local run, names what it allows, and is registered beforehand.

| guard | refuses when | measured against |
|---|---|---|
| **G1 completeness** | new usable rows are under 99% of the previous snapshot's, or outside the ingest's 400,000-900,000 band | weeks gain rows; a truncated fetch reads far below. **A truncated source refuses everything, writes included, never "delete everything".** |
| **G2 anchors** | any of 50 anchor callsigns is missing from the new source. The anchors are fixed and committed, chosen by a stated rule from callsigns present in every snapshot since 08-25 | its control: all 50 are present in the previous snapshot |
| **G3 surviving-shard share** | a shard still present would lose more than 25% of its keys AND more than 20 keys | ordinary weeks: 0.3% at most. The 79 days' one event (94%, 627 keys) would refuse |
| **G4 removed shards** | more than 3 shards removed in one run, or more than 200 of their keys | ordinary weeks: none. The 79 days: 5 shards, 112 keys, which would refuse |
| **G5 run cap** | more than 250 deletes in one scheduled run | ordinary weeks: 3-4 |
| **G6 key shape and provenance** | a delete key that does not match `^rt:[A-Z0-9]{2,8}$` (so it can never be a bucketed runtime row or a `rt:ZZ` sentinel), or that is not in the proven previous snapshot | the ledger is the snapshot, never a listing |

- **When a guard refuses:** G1 refuses the whole run. G2-G6 refuse its deletes. Either way the run does not seal.
  That is deliberate: the owner ruled that a wrong route is worse than none, so a run that cannot delete must not
  reset the age as if it had.
- **The first run trips G3, G4 and G5.** It is attended and registered, with overrides naming one shard's loss, five
  removed shards and a cap of 1,000.
  - **Predicted deletes, as a count only:** 827, plus the about 3-4 a week dropped since 11-12, less any that return.
  - **Predicted writes:** the key-level diff from 24ce5424 to the source of that day, about 10,000-14,000 a week elapsed,
    per environment.

## 3. Cost

| per run, per environment | count |
|---|---|
| writes (key-level) | 10,000-14,000 a week elapsed; plus 1 meta and 1 sentinel |
| deletes | 3-4 in an ordinary week (deletes have their own 1,000,000 a month) |
| lists | one verify listing, about 640 pages |
| reads | the meta, plus a value sample in the verify (about 200) |

- **A month at the current baseline:** 4.3 runs x 2 environments x 14,000 = up to 120,000 writes, on a baseline of
  0.57-0.70 M (four or five Mondays of `refresh-data`). That stays under 1 M: **$0 a month**, no 80% flag.
- **November, if (C) first runs before 11-24:** it adds about 20,000-60,000 writes to (A)'s month, about +$0.10-0.30 on
  top of (A)'s $4.50. That is about 50% of the $10 allowance, under the 80% flag unless OPERA's ten return.
- GitHub Actions minutes cost nothing on this public repository.

## 4. Who reads each run, and when

| what happened | what shows it | when a person sees it |
|---|---|---|
| a run succeeded | `meta:routes` re-sealed; `/healthz` `routes.ageDays` 0-7 | whenever anyone reads `/healthz`, smoke or reconcile |
| a run failed or refused | the job goes red, with counts only in its step summary. GitHub emails the account that last changed the schedule | only if that email is read |
| the same, three days on | the copy passes 10 days, so `/healthz` reads `stale: true` and (B) turns smoke's line FAIL; `reconcile-fleet.py` prints `<== STALE` | **at the next deploy's smoke, or the next fleet question.** Neither is scheduled |

**The gap, stated:** nothing pushes a failed run to a person on a schedule. **Proposed, for the ruling:** on any failure
or refusal the job opens, or comments on, one GitHub issue labelled `route-refresh`, with counts only. The owner's
ntfy topic is the alternative, but it is the products' channel and the owner's call.

## 5. The order, if this plan is approved

1. A code PR, registration first:
   - the key-level mode;
   - `--no-seal` and `--seal-only`;
   - the verify's absence check;
   - G1-G6;
   - the workflow, with the schedule disabled;
   - each guard red-first, with a sabotage driver whose expected red set is stated.
2. **A dry run of both environments**, attended (`--dry-run` prints counts only and writes nothing).
3. **The first run, attended and registered:** staging, then production, with the overrides in section 2.5.
4. **The schedule enabled** after the first run's verify PASS in both environments, before 11-24.

## 6. For the reviewer

- **Credentials (the owner):** the test identity as CI secrets (section 2.2), or the weaker direct-read proof without
  it.
- **The push reader** (section 4): a GitHub issue, or ntfy.
- **The schedule:** Tuesday 05:47Z, and the `ROUTES_FREEZE_UNTIL` variable.
- **The bounds:** 25% and 20 keys, 3 shards and 200 keys, 250 deletes, and 99% completeness. Each is set from the
  measured weeks above, not tuned to pass.
