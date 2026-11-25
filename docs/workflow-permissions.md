# Workflow permissions: every workflow explicit, so the token default can go to read

**Written 2026-11-17 by the p4 session, at the owner's request** (via the orchestrator, 15:07Z): "token default: read.
List the workflows that need write first, and give each its own block before I change the setting." This PR changes
no workflow's behaviour. It makes each workflow's GITHUB_TOKEN permissions explicit, so the repository default can be
switched to read afterwards without any workflow losing a scope it uses. The pattern is #739's (bench-tools): the
check goes in first and is seen red, then the blocks turn it green.

## 1. What each workflow writes, read from its steps

Read at main `6beb43104992`. A write is listed only where a step in the workflow (or a script it runs) uses the token
to change something. `actions/checkout`, `actions/cache` and the artifact actions need no scope beyond `contents:
read`. Secrets for Cloudflare, ntfy and the like are not the GITHUB_TOKEN and are not scopes.

| workflow (`name:`) | today | needs | the step that needs it |
|---|---|---|---|
| account-budget | no block | contents: read | checkout only |
| bench-tools | contents: read | contents: read | #739's block; unchanged |
| device-ids | no block | contents: read | checkout only (its `gh` mentions are comments) |
| firmware | contents: **write** at the TOP LEVEL, so every one of its jobs gets it | top level read; **job** `build`: contents: write | `gh release upload` (the release event's firmware assets) |
| | | job `version`: contents: write | `gh release edit --prerelease`; `gh release upload version.txt` |
| | | job `promote`: contents: write | `gh release edit --prerelease=false` |
| | job `ship-alert`: actions: read, contents: read | unchanged | `gh api .../actions/runs/<id>/jobs` |
| no-traffic-captures | no block | contents: read | checkout only |
| photo-drift | contents: read, checks: write | unchanged | `gh api repos/$REPO/check-runs` (a POST) |
| photos | contents: read, checks: write | unchanged | `gh api repos/$REPO/check-runs` (a POST) |
| print-cards | contents: read | unchanged | checkout and an artifact |
| provisioner | contents: read | unchanged | checkout only |
| refresh-data | no block | contents: read | Cloudflare tokens only; no GitHub API call |
| routes-refresh | contents: read, issues: write | unchanged | `proxy/scripts/routes-notify.ts`: `gh label create`, `gh issue list/comment/create` |
| web-renderer | contents: read; job `publish`: contents: write | unchanged | `gh release upload` |
| workers | no block | contents: read | Cloudflare tokens only (its `gh` mentions are comments) |

**What changes:** five workflows gain `permissions: contents: read`, and the firmware workflow's top-level write
moves down to the three jobs that write releases. Of its 20 jobs, 16 drop from contents: write to read
(ship-alert already had its own read block).

**Nothing is left unsettled from the files.** One gap is named, for the reader: the release-event jobs (firmware's
`build` upload, `version` and `promote`, and web-renderer's `publish`) and the dispatch- or schedule-only workflows
(photos, photo-drift, refresh-data, routes-refresh) are read from their steps, but no pull-request run exercises them.
The first real run of each after the merge does. The firmware workflow's release jobs keep exactly the scope they use
today (contents: write), so a release cannot lose a permission it has.

## 2. The check, and its red first

`scripts/check_workflow_permissions.py` registers every workflow's blocks, keyed by its `name:` line, and fails when a
workflow has no top-level block, when any block (top-level or job) differs from the registered one, when a workflow
is not registered, or when a registered name has no workflow. It reads with regexes like
`check_workflow_concurrency.py`, and runs in the same device-ids job, after it, so it adds no check-run.
`--selftest` plants into a copy:
- device-ids losing its block;
- the firmware top level gaining actions: write;
- web-renderer's publish job dropping to read;
- workers going inline write-all;
- a new unregistered workflow.

Each must turn exactly its own workflow red, with a clean control.

**Run locally on main's tree (before any workflow is edited), as predicted:** exit 1, red exactly account-budget,
device-ids, firmware (top level write against read; jobs build, promote and version without their blocks),
no-traffic-captures, refresh-data and workers. The other seven match as they stand.

## 3. Predictions, frozen before the first push

**Head 1 (the check and this record; no workflow block yet), the red half:**
- Triggered: the firmware workflow (32 check-runs, 2 of them skipped by design: version, promote), device-ids (1),
  no-traffic-captures (1) and bench-tools (1). That is **35 = 32 success + 2 skipped + 1 failure**.
- The failure is device-ids, at the permissions step, naming the six workflows above and no other.
- account-budget, workers, web-renderer, print-cards and provisioner are NOT triggered, because their path filters
  don't match.

**Head 2 (the blocks), the green half:**
- The six workflows edited. account-budget and workers are now triggered by their own files (1 and 3 check-runs).
- **39 = 37 success + 2 skipped + 0 failure.** The permissions step exits 0 ("13 registered, 0 red"), and the
  selftest passes.
- Every job of the firmware workflow on this PR runs with the new blocks: the 16 that drop to read, and `build` with
  write. So a job that needed write without registering it would turn red here.

**OUTSIDE THE SET:** any other count, split or red set. Then the run's log is read before believing either side.

## 4. After the merge

The orchestrator tells the owner the default can go to read. Every workflow now states its own scopes, so the switch
changes none of them. A workflow added later must register its block here, or device-ids goes red.
