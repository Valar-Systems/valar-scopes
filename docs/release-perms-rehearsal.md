# Rehearsing the release jobs' write blocks, in runs no device can see (registration; nothing run)

**Written 2026-11-17 by the p4 session, for the orchestrator's review. No tag, no release and no workflow change is
made until it is approved.** The owner, 16:44Z: "token default: switching it to read now. Before v20's cut, rehearse
the three release jobs' write blocks in a run that publishes nothing customers can see."

#746 (squash `13a6dab876ce`) gave every workflow its own `permissions:` block. It moved the firmware workflow's
top-level `contents: write` down to its three release jobs: `build` (`gh release upload`), `version`
(`gh release upload version.txt`, and `gh release edit --prerelease` on its failure path) and `promote`
(`gh release edit --prerelease=false`). No pull-request run exercises those jobs, so this is the rehearsal before v20
needs them.

## 1. What a published prerelease runs, read from main `13a6dab876ce`

- **Two workflows fire on `release: published`:** the firmware workflow and web-renderer. A prerelease's `published`
  event fires both, and they run the workflow files AT THE TAGGED COMMIT. So the tag must sit on a commit that
  carries #746.
- **The firmware workflow on a release event:**
  - every unconditional job runs: config-form, follow-privacy, icao-country, range-parity, sku-readers,
    tag-workflow, card-gestures, release-envs, adsbdb-gate, publish-receipts, runners, host-tests-leg (2 legs),
    host-tests, skus, customer-entry, ota-identity;
  - `build` (12 legs) runs;
  - `ship-alert` is SKIPPED, by its `if:` (pull_request or a push to main only);
  - `version` runs (`if: always() && github.event_name == 'release'`);
  - `promote` WAITS (below).
- **What `build` writes on a release event**, slug-ful legs only (`skus.py --shipping-envs` = blipscope-s3-128, slug
  `s3-128`): its "Attach to release" step uploads, TO THIS RELEASE'S TAG, firmware-s3-128.bin,
  firmware-s3-128.factory.bin, flash-manifest-s3-128.json, bootloader-s3-128.bin, partitions-s3-128.bin,
  boot_app0-s3-128.bin and factory-scan-s3-128.json. That is 7 assets, plus a `pubok-s3-128` receipt artifact,
  which is not an asset.
- **What `version` writes:**
  - after the receipt gate passes: `version.txt`, holding FW_VERSION from the tagged source (`19` on main today);
  - if the gate fails: `gh release edit <tag> --prerelease`, the demote. It is a PATCH to the release, the SAME
    endpoint and scope `promote` uses.
- **web-renderer's `publish`** (job-level `contents: write`) uploads only `blipscope-web-*` files to this release's tag.
  It reads every other asset on the release before and after, to prove it changed none.
- **`promote`:** `environment: release`. Its protection rule, read today through the API, is `required_reviewers:
  [daniel-frenkel]` with no wait timer. It moves `latest`, which IS customer-visible: `gh release edit --prerelease=false`
  makes the rehearsal a full release, and `latest` is the moment the fleet starts downloading. **So `promote` MUST NOT
  run, and it provably does not: it waits for the owner's review.** The run is cancelled (`gh run cancel`) once `version`
  has finished, or the owner rejects the review. Either way no step of `promote` executes. A precondition row (P0)
  re-reads the protection rule right before each publish, and the plan STOPS if the rule is gone.

## 2. What the rehearsal can and cannot prove, stated before it runs

- **REH-A, the success path:** `build`'s `gh release upload` and `version`'s `gh release upload version.txt`, each under
  its own job-level `contents: write` with the repository default at read. web-renderer's `publish` does the same.
- **REH-B, the edit path:** `version`'s demote, `gh release edit --prerelease` (a PATCH), under the same block. That is
  the exact API call and scope `promote` needs.
- **`promote` itself is NOT run in either.** Its block is proven present and exact by `check_workflow_permissions.py`,
  which runs on every push. Its scope is proven sufficient for its call by REH-B's PATCH. **Stated plainly:** no run
  before v20 exercises `promote`'s own job. v20's release does, and that stays the watch item in #746.
- **REH-B also closes an open item in CLAUDE.md:** "the new gate's blocking direction has never fired" in the real
  wiring. A slug-ful leg that fails must leave `version` failing and version.txt withheld.

## 3. The vehicle, step by step

**P0, the preconditions, all read-only, immediately before each publish. Any one False and nothing is published.**
1. The repository default token is read: `gh api repos/{o}/{r}/actions/permissions/workflow` returns
   `default_workflow_permissions == "read"`. If it were write, a passing job would prove nothing about its own block.
   It read `read` at 16:53Z.
2. The `release` environment still requires review, with daniel-frenkel as reviewer.
3. The release behind `latest` is v19 (`gh api .../releases/latest`), and `latest/download/version.txt` reads `19`.
   Two reads, 60 s apart.
4. No OTA observation window and no board sitting is open. The orchestrator names the slot.

**REH-A.**
- Tag `rehearsal-perms-a` on main's head at that moment. The head is recorded, and it must carry #746.
- Published with `gh release create rehearsal-perms-a --prerelease`, title "REHEARSAL A -- not a release (release-job
  permissions; deleted within the hour)", and a note saying the same.
- Then: wait for both workflow runs. Once `version` has finished and `promote` shows waiting, cancel the firmware run
  (or the owner rejects the review). Read everything. Clean up.

**REH-B.**
- Tag `rehearsal-perms-b` on a THROWAWAY commit on a throwaway branch `rehearsal/perms-b`: main plus ONE deliberate
  break, `-DREHEARSAL_PERMS_BREAK` in the shipping env's build flags, with an `#error` under that macro in one
  source file. The commit's diff is those two lines.
- Envs that extend the shipping env break too. They are slug-less, so they publish nothing and cannot hold the gate.
- Published as a prerelease the same way, titled "REHEARSAL B -- not a release".
- Then: wait for both runs, read everything, clean up. Delete the branch too.
- `promote` is skipped by its own `if:` (`needs.version.result == 'success'` is false), so nothing waits on the
  owner here.

## 4. Predictions, frozen now

**REH-A:**
- **The firmware run:**
  - every unconditional job: success;
  - `build`: 12 of 12 success, and the s3-128 leg's "Attach to release" step ran;
  - `ship-alert`: skipped;
  - `version`: success. "Every publishing SKU must have published" passes, "Gate failed -- demote" is skipped, and
    "Publish version.txt" runs;
  - `promote`: waiting for review, then cancelled. Its log shows NO "Promote this release to latest" step.
- **web-renderer:** `build` success; `publish` success, with its before/after asset lists equal.
- **Assets on `rehearsal-perms-a`, and ONLY there:** the 7 `s3-128` files above, `version.txt` (= `19`), and the
  `blipscope-web-*` set.
- **THE CONTROL that the rehearsal could fail:** each job's "Set up job" log section prints its token's permissions
  ("GITHUB_TOKEN Permissions").
  - `build`, `version` and web-renderer's `publish` print **Contents: write**.
  - Every other job prints **Contents: read**.
  - A rehearsal that was not exercising the job-level blocks (a default still at write) would print write in ALL of
    them, and would be read as such: OUTSIDE THE SET.

**REH-B:**
- **The firmware run:**
  - `build`: the s3-128 leg FAILS at its compile, so no `pubok-s3-128` is written. The other legs read as their flags
    make them, and each slug-less failure is listed by name.
  - `version`: FAILS at "Every publishing SKU must have published". Then "Gate failed -- demote" RUNS `gh release edit
    rehearsal-perms-b --prerelease` and prints "demoted: prerelease=true". "Publish version.txt" does not run.
  - `promote`: skipped.
- **web-renderer:** `build` and `publish` success, the same as in A.
- **Assets on `rehearsal-perms-b`:** the `blipscope-web-*` set only. No firmware file and no `version.txt`.
- **The control:** `version` prints Contents: write. Its PATCH succeeded under it, with the default at read.

**In both, customer-facing, read BEFORE (P0.3), DURING (right after `version` finishes) and AFTER cleanup, two reads
60 s apart each time:**
- `releases/latest` is v19;
- `latest/download/version.txt` = `19`;
- each rehearsal release reads `prerelease=true` at every read until it is deleted.

**Visible, and named rather than hidden:** the repository is public. Each prerelease appears on the Releases page for
its lifetime (under an hour), and accounts watching the repository's releases get a notification. Both titles say
"REHEARSAL, not a release". No device reads a release by its tag: the fleet's OTA reads `releases/latest/download`
only.

## 5. Cleanup, verified

- For each rehearsal: `gh release delete <tag> --cleanup-tag --yes`. Then:
  - `gh api repos/{o}/{r}/releases/tags/<tag>` answers 404;
  - `git ls-remote --tags origin <tag>` is empty;
  - for B, `git ls-remote origin rehearsal/perms-b` is empty.
- Then P0.3 again, twice, 60 s apart.

## 6. Outside the set, and the stop

**Anything customer-visible changing is OUTSIDE THE SET:**
- `releases/latest` not v19, or `version.txt` through `latest` not `19`;
- a rehearsal release reading `prerelease=false`;
- any step of `promote` running.

**The stop:**
- If a rehearsal release ever reads `prerelease=false`, first `gh release edit <tag> --prerelease`. That switch was
  measured LOAD-BEARING for moving `latest` back, as recorded on the demote step in the firmware workflow.
- Then delete the release and its tag.
- Re-read `latest` twice.
- Report. Nothing further runs until the orchestrator rules.

**Any other result outside the predictions** (a job reading differently, a permissions line not as predicted): stop
after cleanup and report, with the run's log read before believing either side.
