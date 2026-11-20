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
  [the owner]` with no wait timer. It moves `latest`, which IS customer-visible: `gh release edit --prerelease=false`
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
2. The `release` environment still requires review, with the owner as reviewer.
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

## 7. Who consumes release assets: none would pick up a rehearsal's (swept 2026-11-17 16:56Z-16:58Z, read-only)

The orchestrator's check before any slot: name every consumer of release assets. One that would take an asset from a
prerelease, or by a tag pattern, stops the plan. Swept: valar-scopes main `13a6dab876ce` (every file but docs, for
`releases/latest`, `releases/download`, `/releases`, `gh release download|view|list`, the releases API, `tag_name`,
`prerelease`), and the local clones of the site, the app, the site API, the flasher, the provisioner, the remote, the
screen and nine other repos. Docs and prose are left out. A hit in them is not a consumer.

| consumer | where | what it reads | a rehearsal asset? |
|---|---|---|---|
| **the fleet's OTA** | `src/OtaUpdater.cpp:106` (`OTA_RELEASE_BASE`) | `releases/latest/download/...` only. Per its own comment (`:93-94`), that resolves to the newest **non-draft, non-prerelease** release | **no** |
| the OTA rule's guard | `scripts/check_ota_url.py:60,121` | refuses any shipping image with a pinned `/releases/download/` URL | n/a (a check) |
| bench envs | `platformio.ini:518` (`pregate-s3-128-v5`), `:1242` (`ota-preflight-v6`) | ONE named tag each, never `rehearsal-perms-*` | **no** |
| fleet monitoring | `scripts/ota-download-count.sh:53,112`, `scripts/ota-latest-probe.sh:52-53`, `scripts/reconcile-fleet.py:208` | the `releases/latest` API, or `latest/download` | **no** |
| release-night verifier | `scripts/verify-release.sh:96-97` (and its named-`$TAG` reads `:77,126`) | its "previous release" keeps `isPrerelease == false` only; otherwise the tag it is given | **no**, and it is run by hand on release night only |
| the two release workflows | firmware workflow, web-renderer | `github.event.release.tag_name`: the event's OWN tag | only the rehearsal itself (intended) |
| **the site's renderer** | `valarscopes-site/public/blipscope/scope/v19/*`, with `PROVENANCE.json:8` naming `releases/tag/v19`; `public/blipscope/wasm.js:14,114-116` imports the vendored files | files VENDORED into the site from v19 by name; no runtime fetch from GitHub | **no** |
| **the app's renderer** | `valarscopes-app/renderer.json:3` (`releases/tag/v19`, with the manifest's SHA-256 pinned), `public/renderer/v19/` | vendored from v19 by name, byte-checked against the pinned manifest | **no** |
| the flasher | `valar-flasher/valar_flasher.py:160` (`releases/latest`), `:222` | latest, or ONE tag a person names in a local `products.local.json` | **no**, unless someone types the rehearsal's tag into their own config |
| other clones | `Blipscope-provisioner`, `Blipscope-remote` | older copies of this repo: the same latest-only OTA and the same pinned bench tags | **no** |
| no hit at all | `valarscopes-site-api`, `valarscopes-screen`, `Skyscope`, `Streetscope`, `missileer-watch`, `Glasscalibur`, `valar-scopes-claim`, `ledger`, `mcp-hub`, `book-v2`, `valar-motion`, `radar-camera` | -- | -- |

`valar-eam-feed`'s single hit (`test/fixtures/game/PIN.json:2`) is a README string about a valar-scopes PR, not a
fetch. **Result: no consumer takes a release asset by "newest release" including prereleases, or by a tag pattern. The
plan does not stop on this check.** **Its limit:** it covers the repositories cloned on this PC. A consumer living only
somewhere else, such as a third party fetching by tag, cannot be seen from here, and neither can the Releases page's
human visitors (section 4).

## 8. AMENDED for Thursday 11-19 (the owner, 16:59Z, via the orchestrator; written below the frozen text)

**The owner's words, verbatim:** "rehearsal: ok as visible, Thu. Both marked prerelease and titled "Test, not a
release"; confirm before publishing that no customer device's update check can see them; assets built by CI only; both
releases and their tags deleted right after. / promote: can it be rehearsed by re-promoting v19 (already latest)? If p4
shows from the code that this changes nothing for devices, do that in Thursday's slot. Otherwise as is: its first real
run is v20, with me present, and a failure there promotes nothing."

**What changes in sections 3-6:**
- **TITLES:** both releases are titled exactly "Test, not a release" and published with `--prerelease`.
- **P0 gains a step, at the moment before EACH publish: the device-visibility confirmation, printed into the run log,
  not stated from memory.**
  - The line `#define OTA_RELEASE_BASE ".../releases/latest/download"` is read from `src/OtaUpdater.cpp:106` at the
    commit being tagged. Its own comment (`:93-94`) says that resolves only to the newest non-draft, non-prerelease
    release.
  - `gh api .../releases/latest` reads v19, and `releases/latest/download/version.txt` reads `19`, two reads 60 s
    apart. Each read is printed with its date -u stamp.
  - Any read off means nothing is published.
- **ASSETS BUILT BY CI ONLY:** `gh release create <tag> --prerelease --title "Test, not a release" --notes ...` is run
  with NO files, so the release is created empty. Nothing is uploaded by hand at any point. The prediction gains a row:
  each rehearsal's asset list equals exactly what its workflow runs uploaded (section 4's lists), and nothing else.
- **CLEANUP "right after":** each release and its tag are deleted as soon as its jobs finish, not at the end of the
  slot. A is deleted once `version` has finished, `promote` has been cancelled and web-renderer's `publish` has
  finished. B is deleted once `version`'s demote and `publish` have finished. Each deletion is verified (the API 404s
  and `ls-remote` shows no tag), then `latest` and `version.txt` are re-read twice, 60 s apart.

### 8.1 The promote question, answered from the code (main `13a6dab876ce`, read today)

- **What `promote` DOES**, line by line in the firmware workflow:
  - it runs only on a release event, after a successful `version` (`if:` at :793), behind `environment: release` at
    :795, which needs the owner's review;
  - `actions/checkout`;
  - "version.txt on this release must equal FW_VERSION" (:805): a READ (`curl` of THIS release's own
    `releases/download/<tag>/version.txt`), compared with the tagged source. It writes nothing;
  - "Promote this release to latest" (:827): ONE write, `gh release edit "$tag" --prerelease=false`. No `--latest`
    (measured inert), no upload, no asset touched;
  - "Report the result" (:835): a READ, `gh release view --json isPrerelease`.
- **Re-promoting v19, already latest, would change nothing devices fetch.** The only write sets
  `prerelease=false` on a release that already has it. No asset is uploaded, so no byte at `latest/download/*` changes
  and `version.txt` stays `19`. The `latest` pointer already IS v19.
  - The only workflows that listen for releases take `release: published` only, so an edit fires none.
  - Devices read `latest/download` only (`src/OtaUpdater.cpp:106`), and nothing there changes.
- **BUT IT IS NOT AVAILABLE AS A REHEARSAL OF #746's BLOCK, for two reasons from the code:**
  1. **No trigger reaches v19.** The firmware workflow's triggers on main are `push` (main), `pull_request` and
     `release: published`. There is no `workflow_dispatch`, so `promote` runs only on a NEW release being published.
     v19 is already published, and un-publishing and re-publishing it would move `latest` off v19, which is
     customer-visible.
  2. **A re-run of v19's own run would test the OLD block.** A re-run executes the workflow file at the run's original
     commit: v19's tag, `7b943ae0b5d1`. In that file `contents: write` is TOP-LEVEL (line 16) and `promote` (line 744)
     carries no block of its own. A green re-run would prove the pre-#746 permissions, and nothing about the block
     this rehearsal exists to test.
- **The alternatives, and what excludes each:**
  - **(a)** Let REH-A's `promote` run. It would make the "Test, not a release" release `latest`, which every device's
    update check reads. Excluded by "publishes nothing customers can see".
  - **(b)** A `workflow_dispatch` path that runs `promote` against a named tag. That is a change to the release
    workflow itself. Excluded here: "no workflow change until approved", and the owner named his fallback instead.
- **So the owner's fallback stands, and REH-C is NOT added.** `promote`'s first real run is v20's release, with the
  owner present. A failure there promotes nothing: `promote` failing leaves the release a prerelease and `latest` on
  v19, as the job's own comment records ("Rejecting leaves the release a prerelease and `latest` where it was").
- What Thursday DOES prove for `promote`'s scope is REH-B's PATCH: `version`'s demote runs `gh release edit` under a
  job-level `contents: write` with the default at read. That is the same API and the same scope. The static check
  proves `promote`'s block present and exact on every push.

### 8.2 The slot

**Proposed: Thursday 2026-11-19, 15:00Z-16:30Z** (A, then B, each about 25 min plus cleanup). The orchestrator moves it
away from any deploy or OTA observation window on its calendar. Neither rehearsal needs a board, and A's `promote` is
cancelled by p4, so neither needs the owner.

### 8.3 Before Thursday (the orchestrator's review of 445b05bbacf1)

- Three mentions of the reviewer's handle replaced by the role (privacy rule); no prediction changes.
- **REH-A's `promote` sends the owner a "review pending" notice** when it starts waiting on the `release`
  environment. THE CONDITION: p4 cancels that run (`gh run cancel <id>`) within 5 min of `promote` entering
  "waiting", and prints the cancel with its date -u stamp. The orchestrator tells the owner beforehand (in the digest,
  and at T-30 on Thursday) not to approve it. If `promote` ever runs anyway, the recovery is section 6's step:
  `gh release edit <tag> --prerelease` first, then the reads.

## 9. REH-A, RAN (Thursday 2026-11-19, 15:00:04Z-15:19:26Z). Records of what ran; the text above stands as written

- **The runner:** `_handoffs/p4/reh/reh.sh` `bcc38fbab0d8`, frozen before it ran. Its P0 dry run was ALL TRUE at
  13:23-13:24Z, and its read stage was shown able to see token lines on v19's runs.
- **P0 on main `c53597508355`:** ALL TRUE at 14:56:18Z and again immediately before the publish at 15:00:03Z. Each
  read: `OTA_RELEASE_BASE` at `src/OtaUpdater.cpp:106` = `.../releases/latest/download`; the default token `read`; the
  `release` environment's required reviewer is the owner's account (printed as a boolean only); the commit's CI
  finished with 0 failures; latest = v19, `prerelease=false`, `latest/download/version.txt` = 19, twice 60 s apart.
- **Published 15:00:04Z:** `rehearsal-perms-a`, "Test, not a release", created EMPTY (`prerelease=true`, 0 assets).
  Runs: firmware `44549849503`, web-renderer `44549849267`.
- **AS PREDICTED:**
  - **THE CONTROL, each job's `GITHUB_TOKEN` line read from its log:** the 12 build legs, `version` and
    web-renderer's `publish` show `Contents: write`. Every other job shows `Contents: read` (21 jobs). `promote` has no
    block, because it never ran.
  - The 12 build legs succeeded. `ship-alert` was skipped. `version` succeeded: its receipt gate passed, the demote
    was skipped, and `version.txt` was published ("19").
  - **THE CANCEL:** `promote` showed waiting at 15:15:23Z; `gh run cancel` was submitted at 15:15:28Z (exit 0), at
    most 28 s after it began waiting. `promote` ended cancelled with 0 steps run, so "Promote this release to latest"
    never executed.
  - **Assets:** exactly 14 (the 7 `s3-128` files, `version.txt`, 6 `blipscope-web-*`), uploaded by CI only.
  - **Cleanup, 15:18:19-15:19:26Z:** delete exit 0; the release by tag 404; the tag 0 refs; latest v19 and 19, read
    twice 61 s apart.
- **OFF, a prediction miss:** "every unconditional job: success" failed for `host-tests`. Its ubuntu-26.04 leg was
  still running when the run was cancelled, so the cancel stopped it ("Host tests" cancelled, 15:15:43Z), and the
  aggregate failed at "Both legs must pass". A real test failure is excluded: the leg's only non-success step reads
  cancelled, the 24.04 leg passed, and both passed on main's own push CI. The prediction forgot that cancelling a run
  cancels every job still running.
- **A SIDE EFFECT the plan never foresaw:** the tag sat on main's HEAD, so the release run's check-runs attached to
  main's head commit `c53597508355` (`host-tests` failure; `host-tests (ubuntu-26.04)` and `promote` cancelled),
  beside its green push CI. Any check reading that commit's check-runs would read a failure that is not main's. The
  run was kept as REH-A's evidence (the reviewer refused deleting it).
- **The reviewer's ruling:** REH-B did not run that day. Its P0 runs on B's parent, main's head, so P0's CI read would
  have seen that failure.

## 10. The P0.ci fix (2026-11-19), registered, proven, then wired

- **The fix** (`_handoffs/p4/reh/REG-p0ci-fix.txt` `58b79363e13e`, with AMENDMENT 1 registered before any code): P0
  reads the commit's own CI from its WORKFLOW RUNS with `event == push` AND `head_branch == main`, both re-checked
  per run. The reviewer's read found the amendment's gap: **a tag push is also a push event on the same commit**
  (REH-A's tag push started `print-cards` there), so `event == push` alone was not enough.
- **The helper** `p0ci.py` `b9acecc31b0f`: its selftest passed 9 of 9 (S1-S7, plus two reversals: today's case under the
  old logic BLOCKs; a failing tag-push run under the event-only logic BLOCKs).
- **Live, read-only on `c53597508355`:** the old check-runs logic BLOCKs (71 check-runs, 3 failed or cancelled); the
  fix PASSes (5 of 6 runs counted; the tag push's `print-cards` is not).
- **Wired:** `reh.sh` `3683aa224dc8` (only P0.ci's two lines changed; the old script kept). Its P0 dry run on that
  commit was ALL TRUE.

## 11. REH-B, RAN (Friday 2026-11-20, 05:30:11Z-05:45:47Z)

- **T-15 (04:55Z):** main `bc4edd0f31ed`, its own push CI PASS by `p0ci.py new` (5 of 5). B was RE-CREATED from that
  head by `rehb_break.py` `80d79368194f`: commit `9a911172f468`, platformio.ini +1 (the flag in the shipping env
  only), `src/main.cpp` +3 (`#ifdef` / `#error` / `#endif`). `src/OtaUpdater.cpp`'s blob is identical at B and its
  parent.
- **P0 on B's parent:** ALL TRUE, 05:29:02-05:30:07Z. Branch `rehearsal/perms-b` pushed at 05:30:10Z. Published at
  05:30:11Z: `rehearsal-perms-b` at `9a911172f468`, empty, `prerelease=true`. Runs: firmware `44681440741`,
  web-renderer `44681440730`, complete at 05:42:15Z.
- **AS PREDICTED (frozen, plus the additions accepted before the run):**
  - **EXACTLY ONE build leg failed,** `build (blipscope-s3-128, s3-128)` at its compile. The other 11 succeeded
    (`animtest-s3-128` takes the flag, but its source filter drops `main.cpp`).
  - **`version` RAN and FAILED** at "Every publishing SKU must have published". "Gate failed -- demote" RAN and
    printed `demoted: prerelease=true`. "Extract" and "Publish version.txt" were skipped. **So the PATCH
    (`gh release edit --prerelease`) succeeded under `version`'s job-level `contents: write`, with the repository
    default at read.** That is the same call and scope `promote` needs.
  - `promote` was skipped by its own `if`; `ship-alert` was skipped. web-renderer succeeded.
  - **Tokens:** `version`, `publish` and the s3-128 leg show `Contents: write`.
  - **Assets:** only the 6 `blipscope-web-*` files; no firmware, no `version.txt`.
  - **Cleanup, 05:44:16-05:45:24Z:** the release by tag 404; the tag 0 refs; the branch 0 refs; latest v19 and 19,
    read twice 61 s apart.
  - **Main untouched:** B's runs attach to the throwaway commit. Main's head `bc4edd0f31ed` carries 0 failed or
    cancelled check-runs (05:45:47Z).
- **OFF, unpredicted:** `host-tests` failed, on both legs and the aggregate. Both legs fail at "Build blipscope-s3-128
  (installs the cross-compiler and the libraries)": the host-test jobs BUILD the shipping env, so the deliberate
  `#error` reaches them too. The prediction counted only the build matrix. A consequence of the deliberate break,
  not a defect.

## 12. What REH settled, and what it did not

- **Settled:** #746's job-level blocks are effective under a read default. The release jobs write; nothing else
  can. The receipt gate's BLOCKING direction has now fired in the real wiring: a failed slug-ful leg left `version`
  failing, the release demoted, and `version.txt` withheld. The cancel path holds `promote` safely.
- **Not settled, as section 2 said:** `promote`'s own job has not run. Its first real run is v20's release, with the
  owner present. A failure there promotes nothing.
- **THE LESSON, for any future rehearsal:** never tag main's head. Tag a THROWAWAY commit, as REH-B did, or the
  release's runs land on main's commit. A tag push is itself a push event on its commit, so a CI read must filter
  by branch as well as by event.
