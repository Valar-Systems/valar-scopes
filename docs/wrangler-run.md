# wrangler-run: every wrangler call through one helper, and the checks that make it so

## Why

wrangler 4.x appends every message it logs to a debug log under `%USERPROFILE%\.wrangler\logs` (`~/.wrangler/logs`). That
includes a KV key list (key names, some of them device ids), a secret's name, and command output. There is no switch to
turn it off; `WRANGLER_LOG_PATH` only moves it (wrangler 4.35.0, `src/utils/log-file.ts`). On 2026-11-12 one session's
count-only key list still left six `lb:dev:` names in that folder, and 110 id-bearing logs were deleted on the owner's
word.

The owner's ruling (2026-11-13 06:01Z): **"(a), the script helper plus a check that every script uses it."**

## What it is

- **`proxy/scripts/wrangler-run.mjs`**, the helper. It makes a fresh temp dir (`wrangler-log-*`) for each call, sets
  `WRANGLER_LOG_PATH` inside it, runs the project's own `node_modules/wrangler`, and removes the dir in a `finally`.
  - The caller's cwd, environment, stdin, stdout and stderr pass straight through, and the exit status is wrangler's.
  - It prints nothing of its own on success: callers parse wrangler's stdout, and `activation-lib.ts` filters its
    stderr against a known-noise list. `WRANGLER_RUN_SHOW=1` prints `wrangler-run: temp log removed`.
  - A failed removal always prints its path, and still exits with wrangler's status, so an uploaded deploy never
    reads as failed.
  - Its selftest (R1-R7) runs against a stand-in wrangler and turned red under a plant with the removal deleted.
  - Local controls on the real binary: K1, wrangler 4.35.0 wrote its log where `WRANGLER_LOG_PATH` pointed. K2,
    through the helper, `~/.wrangler/logs` went 34,085 -> 34,085 (read twice before, static), with 0 temp dirs left.
- **`scripts/check_wrangler_calls.py`**, the in-tree check. It refuses a direct call in `scripts/`, `tools/`,
  `proxy/scripts/` and `.github/`, by five patterns: runner, argv, entry, command and action.
  - Comment lines pass.
  - A line that only looks like a call (a test string, a sentence in a message) passes only by an entry in
    `scripts/wrangler-call-allow.txt`, keyed by its path and the SHA-256 of the line. A stale entry is refused.
  - On main at `b7e7fdd9` it read **76 direct calls in 33 files**.
- **The claim hook** (`scripts/claim-hook.py`), for what a session types. See parts H1 and H2 below.

## THE LIMIT THAT STAYS A RULE

**The helper keeps key names out of wrangler's debug log. It does not keep them off the screen.** A key list printed
through the helper still reaches the terminal, the session transcript and anything that captures stdout. So:

> **Device-prefix keys (`enr:dev:`, `fw:`, `lb:dev:`, `act:`, anything keyed by a device id) are read only through a
> count-only reader**, one that prints counts and never a name, whether or not the helper is used.

No check enforces this. It is the reviewer's rule (2026-11-13), written here beside the helper because the helper
is where someone will be tempted to forget it.

## The order of migration (the reviewer, 2026-11-13 ~06:45Z)

- **Now, in this PR:** the files no open critical-path branch is changing (the list is in the PR).
- **PR B (the deploy path, a staging rehearsal on a slot from the reviewer):** `deploy.sh`, `deploy-inputs.mjs`,
  `rotate-staging-provision-token.sh`, and `tools/wx`'s deploy, rollback and cycle scripts. Until then, their lines are
  allowlist entries reading "migrates in PR B (staging rehearsal)", so the check forces PR B. The helper also joins
  `deploy-inputs.mjs`'s machinery list (DB17) in PR B.
- **After the sea-path PR** (map-server's, v20's critical path): `proxy/scripts/basemap/d1.ts`, with entries reading
  "migrates after the sea-path PR".
- **`tools/wx/*` and the `ingest-*` scripts:** only after the reviewer clears the exact lines with weatherscope and
  uk-data.

## The claim hook: parts H1 and H2, with predictions (frozen 2026-11-13, before the hook is touched)

**H1. The deployer lock must see the helper.** `GATED` is `\bwrangler(?:\.cmd)?\s+(deploy|secret put|...)`, so
`node proxy/scripts/wrangler-run.mjs deploy` does not match it, and the helper would be a path around the deploy claim.

- `GATED` and `ROLLBACK` are widened to `wrangler(?:\.cmd|-run(?:\.mjs)?)?`.
- CL12 gains helper-form cases:
  - unnamed deploy: refused (2);
  - deploy by the claim holder: allowed (0);
  - `--dry-run`: allowed (0);
  - rollback while another session holds the claim: allowed (0) and logged.
- S-CL-HOOK's anchor follows the new `GATED` line.

**H2. A hand-typed direct key read is refused** (the reviewer's optional part, taken). These are refused with exit 2,
and the message names the helper: a direct `wrangler kv key list`, `wrangler kv key get` or `wrangler tail`, typed
with `npx`, with `wrangler.cmd`, or with a hand-set `WRANGLER_LOG_PATH`. The helper forms are allowed. Out of the ruled
set and NOT refused: other subcommands (`kv namespace list`, `kv bulk get`, `d1 execute`, `r2 object get`). That is
the narrow ruling as given; the limit above still covers them.

- New row CL13 covers the cases. New plant S-CL-READ makes the refusal pattern match nothing.

| | prediction | outside the set |
|---|---|---|
| **HP1** | With CL12's new cases added and TODAY's `GATED`, `--selftest` reads **exactly CL12 red**. The unnamed helper-form deploy returns 0 where 2 is wanted; every other row passes | anything else: stop and report |
| **HP2** | With `GATED`/`ROLLBACK` widened: **every row passes** | |
| **HP3** | With CL13 added and TODAY's hook (no H2 code): **exactly CL13 red** (the direct forms return 0) | |
| **HP4** | With H2's pattern in: **every row passes** | |
| **HP5** | `--sabotage`: every plant **AS PREDICTED**: S-CL-HOOK {CL12} (its anchor updated), new S-CL-READ {CL13}, the other four unchanged | |
| **HP6** | `check_wrangler_calls.py` reads **0 direct and 0 stale** on the finished tree. Its `--selftest` passes, and a planted direct call in a scanned file reads 1 | |
| escape | none of the above, or a row red that the table does not name | the instrument is not trustworthy at this point: stop, report, decide |

### Graded (2026-11-13, in the order frozen above)

| | result |
|---|---|
| **HP1** | AS PREDICTED: exactly CL12 red. The unnamed helper-form deploy returned **0** where 2 was wanted, and the helper rollback was not logged |
| **HP2** | AS PREDICTED: every row passes |
| **HP3** | AS PREDICTED: exactly CL13 red. All five direct forms returned 0 |
| **HP4** | AS PREDICTED: every row passes |
| **HP5** | AS PREDICTED: all eight plants, with S-CL-HOOK {CL12} and S-CL-READ {CL13}. **Run from an LF export of the staged files**: on a Windows checkout with `core.autocrlf=true` the `.py` files are CRLF on disk and every `.py` anchor reads "found 0 times" (this predates this PR; CI checks out LF) |
| **HP6** | graded with the migration, below |

### HP6 and the real-tree controls (2026-11-13)

| | result |
|---|---|
| **HP6** | AS PREDICTED. On the finished tree the check reads **0 direct, 0 stale**: 12 call sites are migrated in this PR, and 64 findings sit behind 61 allowlist rows across 25 files, each with the reason it is owed. Its `--selftest` passes. One planted direct call in a tracked file (`scripts/verify-release.sh`) read **direct 1**, and after a byte-exact restore read 0 |
| **K3** | a real production KV get through the helper, on a ruled non-device key (`meta:airports`), printing a byte count only: exit 0, 1,035 bytes. `~/.wrangler/logs` read 34,085 twice before and 34,085 after, with 0 temp dirs left |

**Migrated in this PR:**
- `scripts/reconcile-fleet.py` (its "confirm by hand" line now prints a COUNT);
- `scripts/verify-release.sh`;
- `proxy/scripts/` `activation.ts`, `fleet-device-entry.ts`, `check-activation-gate.sh`, `check-manifest-drift.py`
  (its CI job gains `npm ci`, so it runs the lockfile's wrangler instead of downloading the latest),
  `check-pkg-lifecycle.mjs` and `photo-gaps.ts`;
- the runbook in `proxy/src/fleet.ts`, now a count and a present/absent.

**`CLAUDE.md`'s runbook line is NOT edited here.** This session does not edit `CLAUDE.md` on a request relayed by
another session. The owner (or a session he asks directly) applies it. In the evidence table, the row "this id is a
real device" names `wrangler kv key list --prefix "enr:dev:"`, which lists device ids to the screen. The proposed
replacement asks the one question as a boolean:

    node proxy/scripts/wrangler-run.mjs kv key get "enr:dev:<id>" --binding ENRICH_KV --env production --remote > /dev/null && echo present || echo absent

**Correction, 2026-11-13 (blipscope-5c), added below the proposal, which stands as written.** CLAUDE.md did not
get this one-liner. Measured on wrangler 4.35.0 with the fake id `enr:dev:0000000000000000` only, it gets two things
wrong:
- **It prints the key.** On a missing key, wrangler's error line on stderr carries the request URL, key included.
  `> /dev/null` hides only stdout.
- **Every failure reads "absent".** A fake token answers HTTP 401. A run from the repo root never reaches the API,
  because there is no `[env.production]` there. Both exit non-zero.

CLAUDE.md's `kv_has` (#704) is what replaced the row. It counts only an HTTP 404 as absent, never shows stderr, runs
from `proxy/`, and asks `meta:airports` first, an anchor in the same ENRICH_KV namespace. The same one-liner in
`proxy/src/fleet.ts`'s runbook now points at `kv_has`. Its key list gains `--binding ENRICH_KV`, without which wrangler
refuses before any API call.

### The clearances (2026-11-13, relayed before 07:00Z) and HP6 restated

weatherscope cleared its lines, after checking every branch, worktree and PR of its own:
`tools/wx/m_clock.py`, `s_clock.py`, `s_drive.py` (its docstring included) and `proxy/scripts/check-wx-plant.mjs`.

uk-data cleared its lines: the `ingest-*` scripts and `verify-airports.ts` / `verify-routes.ts`. Its #695 edits only
a comment in `ingest-routes.ts`, so whichever PR lands second rebases.

Both sets are migrated here: **23 more call sites**, each replacement counted against the inventory. That makes
**35 of the 76** migrated in this PR.

| | result |
|---|---|
| **HP6, restated** | **0 direct, 0 stale.** **41 findings** sit behind **39 rows** in **13 files**, all of these kinds: PR B, the deploy path (27 rows); the sea-path PR, `basemap/d1.ts` (1); prose (4); test strings (7). A planted direct call in a newly migrated file (`tools/wx/m_clock.py`) read **direct 1**, and after a byte-exact restore read 0 |
| **K4** | `check-wx-plant.mjs --selftest` through the helper ran the real `deploy --dry-run` builds of every wx target (PB1, PB2 and FB2 PASS; the three sabotages read FAIL as designed). `~/.wrangler/logs` read 34,092 before and after, with 0 temp dirs left |

## PR B: the deploy path, and its staging rehearsal

**Built (this PR):**
- `proxy/scripts/deploy.sh`: both deploys (the proxy's, and the builder's), `secret list`, and the three printed hints
  now go through the helper (`node scripts/wrangler-run.mjs`, run from `proxy/`, as before).
- `proxy/scripts/deploy-inputs.mjs`: the dry-run's argv is `node proxy/scripts/wrangler-run.mjs deploy ... --dry-run
  --outdir ...`, and its guard is unchanged. The helper joins DB17's machinery list, since `deploy.sh` now calls it on
  both paths.
- `proxy/scripts/rotate-staging-provision-token.sh`: `secret put` and `secret list`.
- Local results:
  - `deploy-inputs.mjs --selftest-guard`: DB18 AS PREDICTED.
  - `--selftest`: DB1-DB18 all AS PREDICTED, with the real dry-run builds going through the helper. `~/.wrangler/logs`
    read 34,093 before and after, with 0 temp dirs left.
  - `--selftest-plants`: every plant reddens exactly its registered set. Run from the LF blob: with
    `core.autocrlf=true` this file is CRLF on disk and six anchors read "found 0 times", the same trap HP5 met. The
    file was restored byte for byte.
- **Built later in this PR, after weatherscope's clearance (see below):** weatherscope's four deploy scripts (`tools/wx/ingest_deploy.sh`, `md_rollback.sh`, `s3_cycle.sh`,
  `check_ingest_deploy.sh`). weatherscope asked to be told BEFORE the rehearsal, so they wait for its clearance.
  `check_ingest_deploy.sh`'s rig fakes `npx` on PATH. Once `ingest_deploy.sh` calls the helper, that fake has to become
  a `WRANGLER_RUN_BIN` stand-in, the mechanism the helper's own selftest uses.

### The staging rehearsal: predictions (frozen 2026-11-13, before a slot is asked for)

**Scope.** One `deploy.sh staging` of the PROXY, from this PR's head rebased on main, inside a slot the reviewer
grants, with the deploy claim checked and taken first. The Worker bundle is main's: this PR changes scripts, not
`src/`.

**Not exercised (the record):**
- the builder's deploy line (the same helper form; exercised by DB1-DB18's dry runs only);
- `rotate-staging-provision-token.sh`, which would ROTATE a secret, so it is never run as a rehearsal;
- weatherscope's scripts. **Their planned FIRST exercise is a real run of the migrated `ingest_deploy.sh` on the DEV
  Worker (`wx-ingest-dev.wrangler.toml`)**, on a slot from the reviewer, after this PR is on main and before any
  production ingest deploy uses the helper (ruled at weatherscope's request; see the amendment below).

| | prediction | if not |
|---|---|---|
| **B0** | **The rollback id is read first.** `node scripts/wrangler-run.mjs deployments status --env staging --json`, through the helper, exits 0 and names the version(s) serving staging. Version ids are recorded (they name no device). `~/.wrangler/logs` is counted twice, at least 60 s apart, and must read the same both times | stop: nothing is deployed |
| **B1** | **The claim.** `session-claim.py take deploy --covers proxy:staging --until +45m` as `blipscope-5c` succeeds. Then `deploy.sh`'s own claim check passes, its behind-check reads 0 blocking, and `secret list` through the helper exits 0 with DEVICE_KEY_SECRET present | stop before the deploy; release the claim |
| **B2** | **The deploy through the helper exits 0**, and `deploy.sh` prints no `wrangler-run:` line (the helper is quiet on success) | stop; B0's id is the rollback |
| **B3** | **`~/.wrangler/logs` reads the same count after the deploy as before** (B0's two reads), and no `wrangler-log-*` dir is left in the temp folder | a NEW file in the logs folder: the deploy path wrote outside the helper. Find which call, then report |
| **B4** | **`confirm-deploy.sh` settles on the staged sha**: staging's `/healthz` reports this PR's head, within its window (30 tries x 10 s = 5 min, HEALTHZ_ATTEMPTS / HEALTHZ_INTERVAL defaults) | roll back to B0's id, through the helper (a rollback is never refused); report |
| **B5** | **The hook sees the helper form.** In this PR's tree, `scripts/claim-hook.py` is fed the Bash event for `node proxy/scripts/wrangler-run.mjs deploy --env staging`. Unnamed, it exits 2; named `blipscope-5c` while 5c holds the claim, it exits 0. This session's project dir has no live hook, so the event is fed to the script directly, exactly as the harness would | stop: the lock does not see the helper. Report |
| **B6** | **The end state:** the claim is released, staging serves the staged sha (B4), and the logs folder is unchanged | |
| **stop rule** | any B0-B2 red: nothing more is done. B3 or B4 red after the deploy landed: roll back to B0's version, through the helper, then read `/healthz` and report | |
| **escape** | none of the above, or a reading the table does not name | stop. Report with no reading beyond what was observed; the reviewer decides |

### Amendment to the rehearsal (ruled 2026-11-13 ~07:50Z): run AFTER the merge, from main

5c's standing deploy authority, the owner's own words, covers reviewed and MERGED code deployed from a clean checkout
of main. A staging deploy of this PR's unmerged branch is outside it, so the slot for that was not used and nothing was
touched. The reviewer ruled option (b):
- this PR merges on the reviewer's approval at its sha;
- a **HOLD** follows the merge: no production deploy of the proxy uses the new `deploy.sh` until B0-B6 pass on staging;
- B0-B6 run as frozen above, **from a clean checkout of main at the merge commit**, with B4's staged sha = the merge
  commit;
- the rollback target is B0's version id, through the helper;
- the hold is lifted on 5c's B6 report.

**Planned first exercise of weatherscope's scripts (ruled at weatherscope's request):** after this PR is on main, the
FIRST real run of the migrated `ingest_deploy.sh` is the DEV Worker (`wx-ingest-dev.wrangler.toml`), on a slot from the
reviewer. That run comes before any production ingest deploy uses the helper.

### weatherscope's scripts, migrated (cleared ~07:48Z) and the rig, before and after

The changes:
- `tools/wx/ingest_deploy.sh`, `md_rollback.sh` and `s3_cycle.sh`: each `npx wrangler` becomes `node
  scripts/wrangler-run.mjs`, run from `proxy/` as before.
- `check_ingest_deploy.sh`'s rig: the `npx` fake on PATH becomes a `WRANGLER_RUN_BIN` stand-in, a node script that
  records each call WHOLE as "wrangler <args>". The `npx` on PATH is now a TRAP: an unmigrated call is counted and exits
  97.
- A guard runs before any row: the variable must name the fake, and a probe through the mirror's helper must land in it.
- New plant IDs-f puts one unmigrated `npx wrangler deploy` back into the mirror's copy.

| | BEFORE (main `cd011676`'s rig and scripts) | AFTER (this PR) |
|---|---|---|
| rows ID1-ID13, the expected calls compared WHOLE | 13 of 13 as predicted | 13 of 13 as predicted |
| plants | IDs-a..IDs-e: 5 of 5 as registered | IDs-a..IDs-f: 6 of 6 as registered |
| IDs-f: an unmigrated `npx wrangler` planted back | (no such plant) | red {ID1 ID9 ID11 ID12 ID13} as registered; **npx trap hits 5, nothing real reached** |
| npx trap hits, unplanted | (the fake WAS npx) | **0** |
| guard: `WRANGLER_RUN_BIN` unset / not the fake / the fake / the fake's name with another file | (no such guard) | **G1 refused, G2 refused, G3 passed, G4 refused by the probe** |
| the fake token (`CLOUDFLARE_API_TOKEN=not-a-token-check-ingest-deploy`) | in force | **in force: yes**, printed on the same run |
