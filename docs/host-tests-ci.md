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
- The job's first version did not build an image: `pio pkg install -e blipscope-s3-128` was meant to
  fetch the two things the suite reads from PlatformIO, the xtensa cross-compiler (a missing one is
  `fail=1`) and LovyanGFX's fonts under `.pio/libdeps` (a missing one is BLIND, exit 2). It does not
  fetch the compiler (see Results), so the job builds `blipscope-s3-128` instead.
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
0. **H3.0, before the PUT (added after the Weatherscope session's review, before any H3 reading).**
   `branches/main/protection` reads **404 "Branch not protected"**, and `rulesets` returns **0 rulesets**,
   each read twice ≥ 30 s apart, immediately before the PUT. A PUT replaces the whole protection object,
   so this is what makes the PUT a creation and the rollback `DELETE` a removal of this rule alone. If
   either read shows anything else, stop: the PUT would overwrite someone's rule.
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

## Results, first heads (2026-10-20)

**H1 at `c7c8d17`: OFF-PREDICTION.** Two checks went red where H1 said every other check was unchanged.

1. **`sku-readers` failed.** The reader sweep refuses any file that names `firmware.yml` and isn't on its
   reviewed list, and this doc names it. The guard was correct and the miss was this PR's: I had run
   `skus.py --validate` before pushing but not `--check-readers`. Fixed in `36c2b4c`, which adds the doc
   to `ALLOWED_MENTIONS` as prose, the sweep's documented remedy.
2. **`host-tests` failed, exit 1.** The only FAIL line is `FAIL: no xtensa toolchain found`. Every
   other line matches the Windows calibration:
   - `compiler: /usr/bin/g++`;
   - 356 `ok` lines, which is 361 less the five cross-compile lines;
   - all WX crop and WX render checks passed;
   - the purity gate fired;
   - 0 BLIND.

   **Cause:** `pio pkg install` did install `toolchain-xtensa-esp-elf@14.2.0+20260121`, but that
   registry zip is a 1,601-byte stub: `package.json` plus a `tools.json` pointing at the real 176 MB
   tarball per OS. pioarduino fetches the compiler only during a build.

   **Alternative named:** the binary is present but lost its exec bit. **Excluded:** the zip holds no
   binary at all; it was downloaded and listed.

   **Why the calibration missed it:** this machine's copy of the package was populated by earlier
   builds, so the calibration's environment lacked the property under test.

   **The rig refused correctly.** A missing cross-compiler is FAIL, not a skip, and CI went red.

**H2 at `fe5f2c1` (#441, stacked on `c7c8d17`): OFF-PREDICTION, because of the same install defect.**
- Two FAIL lines, the cross half and `FAIL  A3  a zero-length long run appended (00 00): Ok (want ZeroRun)`,
  where "exactly one" was frozen.
- Exit 1, `WX CROP TESTS FAILED`, `all WX render checks passed`, last suite line `HOST TESTS FAILED`.
- **355** `ok` lines = H1's 356 − 1. Line for line, the suite output differs from H1's in exactly the
  A3 case, `A3 11/12` and `WX CROP TESTS FAILED`.
- So the part H2 exists for, a decoder regression turning `host-tests` red on exactly the predicted
  check, was observed on CI. The second FAIL line is H1's defect, inherited.

## Predictions after the fix (frozen before its first run)

The fix installs with `pio run -e blipscope-s3-128`, a full build, the way every build leg gets the
toolchain. It costs about 4.5 min: main's last s3-128 build step took 4 min 34 s.

**H1′. The fixed head.**
- `host-tests` concludes **success**, and its build step succeeds.
- The suite log contains, in order:
  - `compiler: /usr/bin/g++`;
  - `cross:    /home/runner/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++`;
  - the five `ok   <TU>.cpp compiles for the ESP32-S3` lines;
  - `all WX crop checks passed`;
  - `all WX render checks passed`;
  - the last suite line, `ALL HOST TESTS PASSED`.
- 0 FAIL lines, 0 BLIND, and **361** `ok` lines: H1's 356 plus the five cross lines.
- Every other check run concludes as on `c7c8d17`, except `sku-readers`, which now passes.

**H2′. The sabotage, stacked on the fixed head.** A new draft PR, so no branch is force-pushed; #441
is closed once this is graded.
- `host-tests` concludes **failure**, exit 1.
- **Exactly one** FAIL line: `FAIL  A3  a zero-length long run appended (00 00): Ok (want ZeroRun)`.
- `WX CROP TESTS FAILED`, `all WX render checks passed`, last suite line `HOST TESTS FAILED`.
- **360** `ok` lines = H1′ − 1.
- Every other check run concludes as on H1′'s head.

H3 is unchanged. The outside-the-set rule still applies: stop, report, and decide.

## Results after the fix (2026-10-20, ruled to run)

**H1′ at `fa6daae`: AS PREDICTED.** [job](https://github.com/Valar-Systems/valar-scopes/actions/runs/39494194135/job/118874838145)
- `host-tests` succeeded. Its build step succeeded (4 min 25 s).
- The suite log has every frozen line, in order:
  - `compiler: /usr/bin/g++`;
  - `cross:    /home/runner/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++`;
  - the five `compiles for the ESP32-S3` lines;
  - `all WX crop checks passed`;
  - `all WX render checks passed`;
  - `ALL HOST TESTS PASSED`.
- 0 FAIL lines and 0 BLIND, and **361** `ok` lines in the suite log.
  - *Counted from the suite's first line, `compiler:`.* The whole job log has 369, because the build
    step adds 8 lines of the config-page size report (`ok 68,847 B Configure Blipscope`, and so on). The
    frozen count was for the suite log; both counts are stated here so neither reading is hidden.
- 33 check runs: 30 success and 3 skipped, all from app 15368. Name by name, they differ from `c7c8d17`
  in exactly `host-tests` and `sku-readers`, both now success.

**H2′ at `4df31cc` (#442): AS PREDICTED.** [job](https://github.com/Valar-Systems/valar-scopes/actions/runs/39494251216/job/118875033221)
- `host-tests` failed, `Process completed with exit code 1.`
- **Exactly one** FAIL line: `FAIL  A3  a zero-length long run appended (00 00): Ok (want ZeroRun)`.
- `WX CROP TESTS FAILED`, `all WX render checks passed`, last suite line `HOST TESTS FAILED`.
- **360** `ok` lines in the suite log (368 in the job log, the same 8 from the build step).
- 33 check runs. Name by name, they differ from H1′'s head in exactly `host-tests`, failure. The
  weatherscope harness leg built the sabotaged header.

So a deliberate regression in the shipped WXC1 decoder turns `host-tests` red on exactly the
predicted check, with every other check unaffected. H3, the rule, follows the merge.

## H3 results (2026-10-20): AS PREDICTED, every step

#440 merged as `788ad44` (squash, no `--admin`), then H3 ran as frozen. Each reading is two reads at
least 30 s apart; `UNKNOWN` did not count as a reading. Record on #440:
https://github.com/Valar-Systems/valar-scopes/pull/440#issuecomment-6303952573

| step | reading |
|---|---|
| **H3.0** before the PUT | `branches/main/protection` 404 "Branch not protected" and `rulesets` 0, at 22:50:38Z and 22:51:14Z |
| **H3.1** proof PR #444 (S-A3a), before the rule | CI complete: 33 checks, 1 failure (`host-tests`, exactly the A3 line, exit 1, 360 ok). **UNSTABLE** at 22:49:54Z and 22:50:30Z |
| **H3.2** the rule (PUT 22:51:27Z), read back | `checks=[{"context":"host-tests","app_id":15368}]`, `strict:false`, `enforce_admins:false`, no review requirement, no push restrictions |
| **H3.3** #444, no new commit | **BLOCKED** at 22:51:45Z and 22:52:21Z. Only the rule changed between H3.1 and H3.3 |
| **H3.5** #415 (never ran `host-tests`) | **BLOCKED** at 22:52:21Z and 22:52:56Z. A 22:51:45Z read was UNKNOWN, and a 22:52:41Z read came only 20 s after the one before, so neither counted |
| **H3.4** #444 after the revert commit `029b0c3` | 33 checks, 30 success, 3 skipped, `host-tests` green. **CLEAN** at 23:16:46Z and 23:17:22Z |
| **H3.6** | #444 closed unmerged and `ci/h3-proof` deleted. No merge command was ever run against it |

- **Observed, not in the frozen text:** the classic rule's defaults also disable force pushes to `main`
  and deleting `main` (`allow_force_pushes:false`, `allow_deletions:false`).
- **Also observed:** `main`'s own push run of `host-tests` on `788ad44` was green, which is the
  `strict:false` safety net. Accepted by the reviewer, 2026-10-20.

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
- **What the rule cannot do (stated so nobody over-reads it).** Every session merges with the same
  admin token, so the rule binds our merges only through the convention of never passing `--admin`.
  The real gate stays the merge procedure: the reviewer's "approved to merge at <SHA>", plus CI counted
  against the total. To make the convention observable, **every merge report states `host-tests`'
  conclusion on the merged head, and that `--admin` was not used.**
- **Unsettled:** whether an admin merging through the REST API bypasses the rule with
  `enforce_admins: false`. Probably yes. Not tested, and not needed for anything above.
- **`strict: false`** means a PR that was green on an older base can still turn `main` red after merging.
  `main`'s own push run of `host-tests` is what catches that, so a red `host-tests` on `main` is read and
  acted on by whoever merged last.
