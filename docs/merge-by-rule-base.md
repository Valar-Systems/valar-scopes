# `merge-by-rule.sh` merges into the PR's own base (predictions first, 2026-11-11)

**For:** the reviewer (the orchestrator), and every session that merges. **Status:** the registration, frozen before the code.
**Ruled 20:52:10Z:** a base argument, defaulting to `main`; a PR whose base isn't the stated one is refused before any claim;
everything that reads "main" reads that base instead.

## 1. Why

`gh pr merge` merges a PR into **its own base**. The script reads `main` as H (`scripts/merge-by-rule.sh:43`, `:48`) and checks
the squash's parent against it (`:54-56`). On a PR into any other branch, such as v19's `release/v19`, it would wait on
`main`'s CI and merge into the other branch, and only then report "STOP: the squash is not the approved merge". The script
merges first and objects second, which is the wrong order for a gate.

## 2. The change, in `scripts/merge-by-rule.sh` only

- **A fourth argument, the base,** defaulting to `main`: `merge-by-rule.sh <pr> <head> <branch> [<base>]`. Every existing call
  passes three arguments and keeps its meaning.
- **Before the CI wait and before any claim,** after the fetch, three refusals, each saying that nothing was merged and no
  claim was taken:
  - a base that isn't a plain branch name;
  - a base with no branch on `origin`;
  - a PR whose `baseRefName` (read with `gh pr view`) isn't the stated base, including a failed read.
- **H, the CI wait, "still H", parent == H and merge-tree** all use `refs/heads/<base>`. The messages name the base, for
  example "release/v19 still 3a322758eadc: merging".
- **`MERGE_DRY=1`:** every read and check above runs, H's CI is waited on, and the script prints `DRY: would take the merge
  claim and squash #<pr> into <base> at H <sha>` and exits 0. It does this before the claim is taken, so a dry run can't
  merge.
- **Unchanged:**
  - the `MERGE_CLAIM_CHECK_ONLY` block. The new checks come after it, so CL11's stubbed run still makes 0 `gh` calls;
  - `take_claim`, byte for byte, which is S-CL-MERGE's anchor;
  - the squash, `--match-head-commit`, the claim's release, and the branch deletion.

## 3. The rows (each run against real PRs; the refusals and `MERGE_DRY` stop before any claim)

| row | the run | predicted |
|---|---|---|
| **MB1** (main unchanged) | `MERGE_DRY=1` on an open PR into `main`, three arguments | `DRY: ... into main at H <main's head>`, where H equals `git ls-remote origin refs/heads/main` at that moment |
| **MB2** (the release branch) | `MERGE_DRY=1` on #651, with base `release/v19` | `DRY: ... into release/v19 at H 3a322758eadc` |
| **MB3** (a base mismatch) | #651 with three arguments (stated `main`, actual `release/v19`) | `STOP: #651's base is 'release/v19', not the stated 'main'`, exit 1. No merge claim of mine afterwards (`session-claim.py show`), and #651 still OPEN |
| **MB4** (no such base) | an open PR, base `release/no-such-branch` | `STOP: no branch release/no-such-branch on origin`, exit 1, no claim |
| **MB5** (the claim rig) | `python3 scripts/session-claim.py --selftest` and `--sabotage` | as on `main` today: CL11 passes, and S-CL-MERGE turns exactly {CL11} red |

## 4. The plants (in a temporary copy of the script beside the real one; each edit asserted applied; the copy deleted after, tree checked clean)

| plant | the edit | red |
|---|---|---|
| **P-NOCHECK** | the `baseRefName` refusal line removed | **MB3:** its input then reaches `DRY: ... into main`, so the mismatch is no longer refused |
| **P-HMAIN** | H read from `refs/heads/main` again | **MB2:** it prints `main`'s head, not `3a322758eadc` |

## 5. CI, predicted before each push

Both commits, this registration and then the code, touch only `docs/` and `scripts/merge-by-rule.sh`. No path-filtered
workflow takes them. So each predicts the docs-only shape: **32 = 29 success / 3 skipped / 0 failed**. `device-ids` runs MB5's
selftest and sabotage on every PR.

## 5a. The results, before the push (2026-11-11; every run `MERGE_DRY=1`, nothing merged, the merge claim free after)

| row | read |
|---|---|
| MB2 | `DRY: would take the merge claim and squash #651 into release/v19 at H 3a322758eadc` (20:56Z) |
| MB3 | `STOP: #651's base is 'release/v19', not the stated 'main'; nothing merged, no claim taken` |
| MB4 | `STOP: no branch release/no-such-branch on origin; nothing merged, no claim taken` |
| MB1 | `DRY: ... squash #646 into main at H 525cf7d1725d`; `main` held at that sha throughout (22:18-22:21Z) |
| P-NOCHECK | MB3's input reaches `DRY: ... #651 into main at H 525cf7d1725d`: **MB3 red**, as registered |
| P-HMAIN | MB2's input prints `DRY: ... #651 into release/v19 at H 525cf7d1725d`, main's head: **MB2 red**, as registered |
| MB5 | on the committed LF bytes plus this script: every selftest row passes, and all seven plants are as predicted (S-CL-MERGE {CL11}) |

**Two earlier attempts at MB1 and the plants were stopped and are not readings.** In one, `main`'s CI was not green. In the
other, `main` moved under the runs (21:46:02Z).

**A Windows trap, noted for the record and not new.** In a Windows checkout with `core.autocrlf=true`, `session-claim.py`
and `claim-hook.py` are CRLF on disk. Five of the sabotage driver's anchors are LF, so they match nothing there, and the
driver reads OFF. That is a property of the checkout, not of this change. `git archive` applies the same conversion, so the
LF copy has to come from `git cat-file`.

## 6. After it merges

- It merges into `main` by the current script, unchanged, since its base is `main`, after the reviewer's approval at its sha.
- The reviewer announces the new argument to every session.
- Its first real use is #651, into `release/v19`.

**Out of the set, at any step:** stop and report. Nothing is improvised.
