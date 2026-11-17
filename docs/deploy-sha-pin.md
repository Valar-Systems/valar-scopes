# Deploy scripts pin the commit a plan names (predictions first)

**The owner's standing rule, 2026-11-17 17:50Z (relayed by the reviewer), verbatim:** "every deploy plan names the exact
commit sha it ships. A merge that touches the deployed code after the plan is written forces a re-check, enforced by a
check in the deploy script, not by memory."

**Why it is needed.** On 2026-11-17 a merge (#744) landed after the #736 deploy plan was written. The plan's D0.3 caught
it only because a person re-listed the carried commits. Separately, `deploy.sh`'s only override for a tree behind main is
`ALLOW_BEHIND=1`, a blanket flag that names nothing. And `tools/wx/ingest_deploy.sh` has no git check at all: no
dirty-tree refusal, no behind check and no stamp.

**Status.** The design was accepted in principle by the reviewer. This branch is **not merged before Wednesday 2026-11-18's
deploy chain** (radar, logs, route) has closed, because both scripts are deploy machinery in the deploy sets, and a merge
would itself force every Wednesday plan to be re-checked. The exception is the owner ruling that deploys wait for it.

## 1. The design

**Both scripts, `proxy/scripts/deploy.sh` and `tools/wx/ingest_deploy.sh`:**

- **`EXPECT_SHA` is required.** It is the full 40-hex sha the deploy plan names. The script refuses when it is unset or
  malformed, or when `git rev-parse HEAD` differs from it. Each check runs after the deploy claim (so the claim's own
  selftest, CL10, is untouched) and before anything that deploys.
- **`ALLOW_BEHIND=1` is retired.** It is refused, with a message naming its replacement.
- **`ALLOW_BEHIND_SHAS` replaces it:** a comma-separated list of full 40-hex shas. When the tree is behind origin/main in
  what the target ships, the deploy goes ahead only if the BLOCKING commits equal the named list EXACTLY:
  - a blocking commit that is not named refuses (a merge after the plan);
  - a named commit that is not blocking refuses (the plan is stale);
  - a name that is not a full 40-hex sha refuses.
- **Commits behind that touch nothing the target ships need no naming**, as today.
- **Rollbacks are untouched.** They go through `wrangler-run.mjs rollback`, never through these scripts, and are never
  refused.

**`deploy.sh`.** The blocking set is `deploy-inputs.mjs`'s own decision. `check` gains one machine-readable line,
`blocking-full: <sha> <sha> ...`, listing the full shas of the commits it already prints as blocking. When the bundler
cannot answer, today's fallback (all of `proxy/`) gives the set, from `git rev-list HEAD..origin/main -- proxy/`.

**`ingest_deploy.sh` gains:**
- the dirty-tree refusal: any `git status --porcelain -- proxy/ tools/wx/` output refuses, with no escape;
- `git fetch origin main`;
- the same EXPECT_SHA and ALLOW_BEHIND_SHAS rules.

Its blocking set is the interim one: every commit in `HEAD..origin/main` touching `proxy/`, `tools/wx/ingest_deploy.sh`
or `scripts/session-claim.py`. That over-blocks, because any proxy change needs naming. It is the safe direction, until
`deploy-inputs.mjs` has an ingest target.

## 2. The harnesses, rows frozen before the code

Each harness runs in a mirror tree in a temporary folder. The mirror is a git repository with a bare `origin`, and holds
byte copies of the script and its machinery. wrangler is a FAKE, run by the helper as `WRANGLER_RUN_BIN`. The fake records
each call. For `deploy --dry-run` it writes a metafile naming `src/index.ts`. For `secret list` it lists
`DEVICE_KEY_SECRET`. For `deploy` it prints "fake deploy". `confirm-deploy.sh` is a stub that records its arguments and
reaches no network. No row touches a real claim, a real Worker or a repo file.

**Every row is graded on three things:**
- the exit code;
- whether the upload call was recorded;
- a message token printed only by the new code.

So **every row is red on today's scripts, and green after the change.** That is the red-first prediction.

**Commits in the mirror:**
- C0: the base, and HEAD;
- C1 and C2: on origin/main after C0, each touching `proxy/src/index.ts`, so they block;
- CD: on origin/main after C0, touching only `docs/x.md`, so it does not block.

### 2a. `deploy.sh` (`proxy/scripts/check-deploy-sha.sh`)

| row | setup | predicted |
|---|---|---|
| ES1 | `EXPECT_SHA` unset; origin/main = C0 | exit 1; no upload; "EXPECT_SHA is not set" |
| ES2 | `EXPECT_SHA=abc123` | exit 1; no upload; "EXPECT_SHA must be a full 40-hex sha" |
| ES3 | `EXPECT_SHA` = a real commit that is not HEAD | exit 1; no upload; "HEAD is not EXPECT_SHA" |
| ES4 | `EXPECT_SHA` = HEAD; origin/main = C0 | exit 0; the upload is recorded; "pinned: HEAD is EXPECT_SHA" |
| AB1 | HEAD pinned; origin/main = C1; `ALLOW_BEHIND=1` | exit 1; no upload; "ALLOW_BEHIND=1 is retired" |
| AB2 | HEAD pinned; origin/main = C1; `ALLOW_BEHIND_SHAS`=C1 | exit 0; the upload is recorded; "named and behind: <C1:12>" |
| AB3 | HEAD pinned; origin/main = C2 (C1 then C2); `ALLOW_BEHIND_SHAS`=C1 | exit 1; no upload; "behind and not named: <C2:12>" |
| AB4 | HEAD pinned; origin/main = C1; `ALLOW_BEHIND_SHAS`=C1,C0 | exit 1; no upload; "named but not behind: <C0:12>" |
| AB5 | HEAD pinned; origin/main = C1; `ALLOW_BEHIND_SHAS`=C1's 12-hex prefix | exit 1; no upload; "ALLOW_BEHIND_SHAS must list full 40-hex shas" |
| AB6 | HEAD pinned; origin/main = CD (docs only); no `ALLOW_BEHIND_SHAS` | exit 0; the upload is recorded; "pinned: HEAD is EXPECT_SHA" |

### 2b. `ingest_deploy.sh` (`tools/wx/check_ingest_deploy.sh`, extended)

The existing rows ID1-ID13 keep their predictions. Their mirror becomes a git repository and they run with `EXPECT_SHA`
set to HEAD, which is a change to their SETUP only. The new rows run against `wx-ingest-opera.wrangler.toml opuk`, under a
held claim:

| row | setup | predicted |
|---|---|---|
| IS1 | `EXPECT_SHA` unset | exit 1; no deploy call; "EXPECT_SHA is not set" |
| IS2 | `EXPECT_SHA=abc123` | exit 1; no deploy call; "EXPECT_SHA must be a full 40-hex sha" |
| IS3 | `EXPECT_SHA` = a real commit that is not HEAD | exit 1; no deploy call; "HEAD is not EXPECT_SHA" |
| IS4 | pinned; origin/main = C0 | exit 0; deploy + status calls; "pinned: HEAD is EXPECT_SHA" |
| IS5 | pinned; an uncommitted change under `proxy/` | exit 1; no deploy call; "uncommitted changes" |
| IB1 | pinned; origin/main = C1; `ALLOW_BEHIND=1` | exit 1; no deploy call; "ALLOW_BEHIND=1 is retired" |
| IB2 | pinned; origin/main = C1; `ALLOW_BEHIND_SHAS`=C1 | exit 0; deploy + status calls; "named and behind: <C1:12>" |
| IB3 | pinned; origin/main = C2; named C1 | exit 1; no deploy call; "behind and not named: <C2:12>" |
| IB4 | pinned; origin/main = C1; named C1,C0 | exit 1; no deploy call; "named but not behind: <C0:12>" |
| IB5 | pinned; origin/main = C1; named C1's 12-hex prefix | exit 1; no deploy call; "ALLOW_BEHIND_SHAS must list full 40-hex shas" |
| IB6 | pinned; origin/main = CD (docs only) | exit 0; deploy + status calls; "pinned: HEAD is EXPECT_SHA" |

**Red first:**
- On today's `ingest_deploy.sh`, every new row is red. It has none of these checks or tokens.
- ID1-ID13 stay as predicted: the new setup adds an environment variable that today's script ignores.

### 2c. Plants (sabotage), each in the mirror's copy only

Each plant must turn EXACTLY its registered rows red. An unmodified copy is the control: every row as predicted.

| plant | the edit | predicted red |
|---|---|---|
| S-ES1 | the "not set" refusal removed | {ES1} (unset then fails the format check, with a different token) |
| S-ES2 | the format check removed | {ES2} (a malformed sha then fails the HEAD comparison, with a different token) |
| S-ES3 | the HEAD comparison removed | {ES3} (it proceeds) |
| S-AB1 | the retired-flag refusal removed | {AB1} (ALLOW_BEHIND=1 alone then fails as "behind and not named") |
| S-AB3 | the "not named" refusal removed | {AB3} (it proceeds) |
| S-AB4 | the "not behind" refusal removed | {AB4} (it proceeds) |
| S-AB5 | the full-sha format check removed | {AB5} (the prefix then fails as not named / not behind, with a different token) |

The same seven plants are made in `ingest_deploy.sh`, with I-prefixed names and rows, plus S-IS5 (the dirty-tree
refusal removed, predicted red {IS5}).

**Outside the set:** any other red, or any rig failure (a copy that is not byte-identical, a plant whose anchor is not
exactly one line, a git step that fails). Stop and report; no re-run before the reviewer's ruling.

## 3. CI

- `check-deploy-sha.sh` (rows and plants) runs in the device-ids job, beside `check_ingest_deploy.sh`.
- `deploy-inputs.mjs`'s own selftests stay as they are.
- The new `blocking-full:` line is exercised by the `deploy.sh` harness through the REAL `deploy-inputs.mjs`.
- The CI prediction is stated in the PR before the push.

## 4. Record

_Filled in below this line as each step reads, without editing anything above it._

**2026-11-17, `deploy.sh` half (the session that built it):**

- **Red first, 18:55Z, as predicted.** `check-deploy-sha.sh` was run against TODAY's `deploy.sh`: all 10 rows OFF, because
  today's script prints none of the pin's tokens. The raw readings also show the hazard the rule closes:
  - ES1-ES4, AB1 and AB6 reach the upload today, so an unset or wrong commit is not checked, and `ALLOW_BEHIND=1` waves
    a blocking commit through;
  - AB2-AB5 are refused by today's blanket behind guard.

  The rig ran the REAL `deploy-inputs.mjs` against the fake wrangler.
- **A design detail changed before any commit (accepted by the reviewer).** Section 1 says `check` "gains one
  machine-readable line, `blocking-full:`". Piping `check`'s output to read that line would have changed the exact
  `deploy.sh` line that `deploy-inputs.mjs`'s own P-CALLEE plant anchors on. So instead, `deploy.sh` exports
  `DEPLOY_INPUTS_BLOCKING_OUT=<a temp file>`, `deploy-inputs.mjs` writes the blocking commits' full shas there, and that
  line stays byte-identical (its count is 1).
- **Green, 18:58Z, as predicted:** 10 of 10 rows.
- **Plants, attempt 1, 18:59Z: OFF, a RIG defect, outside the set.** The control was all as predicted, but each of the 7
  plants reddened its own row PLUS {ES4, AB2, AB3, AB4, AB6}.
  - Cause: each plant edits the mirror's TRACKED `deploy.sh`, which reads as dirt. `deploy.sh`'s own dirty-tree guard
    then refused before the pin was reached.
  - The alternative, a code defect, is excluded: the unplanted copy is all green, and the extra set is identical across
    all 7 plants, including plants on lines those rows never reach.
  - Stopped and reported. No re-run before the ruling.
- **The ruling (reviewer).** In the MIRROR ONLY, `git update-index --assume-unchanged proxy/scripts/deploy.sh` is run once
  at setup, and one control row is added, predicted before attempt 2:
  - **DC1:** EXPECT_SHA = HEAD, nothing behind, one line appended to a DIFFERENT tracked file (`proxy/src/index.ts`);
  - predicted: exit 1, no upload, "proxy/ has uncommitted changes".

  It proves the rig hides only the planted script's own edits and does not switch the dirty-tree guard off. The plants'
  registered sets are unchanged, because no plant touches a line DC1 reaches.
- **Disclosed while building the harness:** a heredoc ate a backslash in one inserted line, the DC1 dirt line. It came
  out as a two-line string, functionally the same. It was replaced with a backslash-free `echo` before any run, and the
  harness was re-read.
- **Attempt 2, 19:01-19:03Z, as predicted:**
  - rows: 11 of 11, DC1 included;
  - plants: the CONTROL (an unmodified copy) had every row as predicted, and S-ES1, S-ES2, S-ES3, S-AB1, S-AB3, S-AB4
    and S-AB5 each reddened exactly its registered row.
