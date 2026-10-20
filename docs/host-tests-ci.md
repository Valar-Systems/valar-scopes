# Host tests in CI, and required on main

Ruled 2026-10-20: `test/host/run.sh` runs on every PR, it is a required check on `main`, and it is proved
with one deliberately failing host test. The weather work is behind this: `include/WxCrop.h`, the WXC1
decoder the Blipscope weather layer and Weatherscope share, is graded by the host suite and nowhere else.

This file freezes what each run will show **before any CI run of the job exists**. It is committed
ahead of the workflow change, so the commit order is the proof.

## What changes

- `.github/workflows/firmware.yml` gains a job `host-tests`. firmware.yml runs on every `pull_request`
  (no path or branch filter, so stacked PRs too) and every push to `main`. The job is its own job, not a
  step in a build leg, because a branch rule needs one stable check name, and leg names carry the slug
  and change with `skus.yml`.
- The job does not build an image. `pio pkg install -e blipscope-s3-128` fetches the two things the
  suite reads from PlatformIO: the xtensa cross-compiler (a missing one is `fail=1`) and LovyanGFX's
  fonts under `.pio/libdeps` (a missing one is BLIND, exit 2).
- `test/host/run.sh` also looks for the cross-compiler under its Linux name (no `.exe`). This is
  453c0b9 from #392, carried here. #392's other half, the Missileer bench-hook scan, stays in #392.
- After merge: a classic branch-protection rule on `main` that requires `host-tests` from GitHub
  Actions (app id 15368) and nothing else. See H3.

## Calibration (run BEFORE these predictions were written)

So the reader knows which numbers came from where. All runs were local, on Windows MinGW, at `main`
`122e118` plus the Linux-spelling fix, after `pio pkg install -e blipscope-s3-128` in a fresh worktree:

| run | exit | lines | FAIL lines | `ok` lines | BLIND | last line |
|---|---|---|---|---|---|---|
| unsabotaged | 0 | 889 | 0 | 361 | 0 | `ALL HOST TESTS PASSED` |
| S-A3a planted in `include/WxCrop.h` | 1 | 889 | 1 | 360 | 0 | `HOST TESTS FAILED` |

- A FAIL line is one matching `(^|[[:space:]])FAIL([[:space:]]|:)`. That pattern deliberately misses a
  test's own description containing "FAILED" (the game-outcome case).
- An `ok` line is one matching `^[[:space:]]*ok[[:space:]]`.
- The sabotaged run differs from the unsabotaged one in exactly four lines:
  - the A3 zero-length-run case, now FAIL;
  - `A3 11/12`;
  - `WX CROP TESTS FAILED`;
  - the last line.

**The deliberate failure is S-A3a.** It deletes the decoder's zero-length-run refusal
(`if (v == 0) return Status::ZeroRun;`). It is `test/host/sabotage_wx_crop.py`'s own anchor, verbatim,
and its effect was pre-registered in `docs/weatherscope-stage1.md`. It is a regression in code the
weather layer ships, not an edited expectation.

Linux evidence before this PR: #392's run on 2026-10-12 (base 27a5252) found `/usr/bin/g++` and the
Linux cross-compiler, and ended `ALL HOST TESTS PASSED`. That predates StarvationPolicy, WxCrop and
WxRender.

## Predictions (frozen)

**H1. This PR's head, unsabotaged.**
- The `host-tests` check run (app `github-actions`) concludes **success**.
- The "Host tests" step's log contains, in this order:
  - `compiler: /usr/bin/g++`;
  - `cross:    /home/runner/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++`;
  - five `ok   <TU>.cpp compiles for the ESP32-S3` lines;
  - `all WX crop checks passed`;
  - `all WX render checks passed`;
  - the last suite line, `ALL HOST TESTS PASSED`.
- 0 FAIL lines, 0 `BLIND`, and **361** `ok` lines, the same as the Windows calibration.
- Every other check run concludes as on `main` `122e118`: 30 success, 2 skipped (`version`,
  `promote`).

**H2. The deliberate failure, before the rule.**
- Setup: a draft PR stacked on this branch, titled DO NOT MERGE. Its one commit is S-A3a in
  `include/WxCrop.h`, a one-line diff.
- `host-tests` concludes **failure**. The step ends with `Process completed with exit code 1.`
  (1 = a test failed; 2 would be the rig).
- **Exactly one** FAIL line, and it is
  `FAIL  A3  a zero-length long run appended (00 00): Ok (want ZeroRun)`.
- The log also shows `WX CROP TESTS FAILED`, `all WX render checks passed`, and the last suite line
  `HOST TESTS FAILED`.
- `ok` lines = H1's count − 1.
- Every other check run concludes as on H1's head. The weatherscope harness leg compiles the
  sabotaged header and still builds.

**H3. The rule, after this PR merges.** Every reading in H3 counts only once two reads ≥ 30 s apart
agree; `UNKNOWN` (GitHub still computing) is not a reading.
1. Before the rule, a proof PR is opened into `main`. It is non-draft, titled DO NOT MERGE, and its one
   commit is S-A3a. Once CI completes, `mergeStateStatus` = **UNSTABLE**: a failing check that nothing
   requires.
2. The rule is applied:
   `PUT branches/main/protection` with
   `required_status_checks: {strict: false, checks: [{context: "host-tests", app_id: 15368}]}`,
   `enforce_admins: false`, `required_pull_request_reviews: null`, `restrictions: null`.
   Read back, it shows exactly that check with that app id, `strict: false`, and enforce_admins off.
   The app id is pinned so that a commit status named `host-tests` posted by anything else cannot
   satisfy the rule.
3. The same proof PR, with no new commit, reads **BLOCKED**. Only the rule changed between 1 and 3.
4. A second commit on the proof PR reverts S-A3a. `host-tests` concludes success, and
   `mergeStateStatus` = **CLEAN**. So the rule blocks on the failure, not on everything.
5. #415 (open, non-draft, into `main`, last pushed 2026-10-14, so it has never run `host-tests`) reads
   **BLOCKED** after the rule. That is the side effect stated below, observed rather than assumed.
6. The proof PR is closed unmerged and its branch deleted. No merge command is ever run against it.

**Outside the set.** Any reading not listed above means stop. The job, the rule and the tests are not
edited to make a reading come true; the raw reading goes to the reviewer, and the next step is decided
then. One branch is pre-registered:
- If H1 is as predicted except that the `ok` count differs from 361, that is a platform difference.
  It is reported with the section it came from, and H2 is graded against H1's own count.

## What the rule does to everyone else (stated before it is applied)

- **Open PRs into `main`.** The 14 open non-draft PRs' last CI predates the job. Each reads BLOCKED
  (`host-tests` "Expected") until a new push or a rebase runs it. A re-run of an old run does not help,
  because it uses the old workflow file. Drafts are untouched until they are marked ready.
- **Direct pushes to `main`.** Daniel's pushes, and the photo dashboard's (Daniel's fine-grained token,
  fast-forward only), are made as an admin. With `enforce_admins: false`, admins bypass the rule, so
  both keep working.
- **Rollback trigger.** If either direct push is refused, the rule comes off at once with one call,
  `gh api -X DELETE repos/Valar-Systems/valar-scopes/branches/main/protection`, and it is reported.
- **Merges via `gh pr merge` without `--admin`** respect the rule. That is the point: this repo's merges
  are made that way.
