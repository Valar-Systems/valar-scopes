# CI: a newer PR head cancels its older head's runs (predictions first)

## Why (measured 2026-10-23, 16:10Z)

- **Weatherscope's #467 pushed 8 heads between 15:33 and 16:00Z.** Each gets a full `firmware` run: 32 jobs, each about
  4 minutes (median 3.8).
- **Nothing cancels a superseded PR run,** so all 8 queued behind one another. The repo had 9 queued runs. Main's
  push run for `26510c0` and #461's runs waited behind them.
- **The cap is account-wide.** Concurrent jobs are shared across the organisation's repos. The one `firmware` run
  then in progress peaked at 15 jobs at once, and took from 15:39 to past 16:11 just to start all 32.
- **It was not an incident:** githubstatus.com reported Actions operational. The one open incident was Dependabot
  alerts.

## The change

The six workflows that run on `pull_request` each get one workflow-level block:

```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.event_name == 'pull_request' && format('pr-{0}', github.event.pull_request.number) || github.run_id }}
  cancel-in-progress: ${{ github.event_name == 'pull_request' }}
```

The six are `device-ids`, `firmware`, `no-traffic-captures`, `print-cards`, `provisioner` and `workers`.

- **A PR's newer head cancels its older head's run** of the same workflow.
- **Anything else** (a push to main, a release, a dispatch) is grouped by its own `run_id`. It can never queue behind,
  or be cancelled by, another run.
- **Untouched:** `photos` and `photo-drift`, which have their own groups and never run on PRs, and `refresh-data`,
  which runs on a schedule.

**What a completed run on an old head established, and why losing it costs nothing.** The merge rule counts checks
on the **exact head being merged**. A cancelled run belongs to a head that is no longer the PR's head, so it is never
counted. The release path (`firmware`'s `version` job and its publish receipts) runs on the `release` event, so it
is grouped by `run_id` and is never cancelled.

## Predictions

| | check | predicted |
|---|---|---|
| **C1** (CI, the artifact) | `scripts/check_workflow_concurrency.py` reads every workflow | every workflow with a `pull_request` trigger carries exactly this `group` and `cancel-in-progress`. Every other workflow carries no new block. *Selftest sabotages:* the block removed from one workflow; `cancel-in-progress: true`; the group without its `run_id` fallback. Each turns the check red, naming the workflow |
| **C2** (live, on this PR) | two pushes to this branch less than 60 s apart, both carrying the block, read through the runs API by `head_sha` | every PR workflow's run for the **first** head ends **`cancelled`**. Every run for the **second** head completes. *Control:* today's #467 heads before this change: 7 superseded `firmware` runs are queued or in progress, **none cancelled**. So the instrument can see "not cancelled" |
| **C3** (after merge) | the merge's own push run on main | **not cancelled**, and every PR workflow completes on it |

**C2 and path filters.** `print-cards`, `provisioner` and `workers` filter by path. For `pull_request`, GitHub evaluates paths against
the PR's whole diff, and both heads carry all six workflow files, so all six run on both heads.

**Outside the set:** stop and report.

**C2's second head.** This line is the commit that supersedes the first, for C2 (pushed less than 60 s after it).

## The `sku-readers` miss on `42dce6c`, and the ruling

**The miss.** On `42dce6c`, `firmware` / `sku-readers` failed. The only file the sweep flagged was
`scripts/check_workflow_concurrency.py`: one selftest plant names the matrix workflow's file. **Outside the set:** no
C row predicted it. The checker had been run before the push; the repo-wide sweep had not. (The `print-cards` push run
is a second miss, ruled and recorded on #469 with its cause.)

**The ruling (reviewer chat, pasted by Daniel, 2026-10-23), verbatim:** "(a), worded truthfully. The only file the sweep
flags on 42dce6c is scripts/check_workflow_concurrency.py (I ran it). Add it to ALLOWED_MENTIONS with the note 'reads
every workflow's on: and concurrency blocks, never the matrix'. It's a reviewed non-matrix reader, like
check-tag-workflow.sh, not prose. Push that as #469's next head and bring the head back for approval."

**This head does exactly that.** `skus.py`'s `ALLOWED_MENTIONS` gains the checker, with that note. The checker is
unchanged, so its plant still names the file and the entry is not stale.

- **Not used:** the local alternative (moving the plant, and allowlisting this doc as prose) was set aside by the
  ruling. It is kept on the branch `backup/469-local-f1-2026-10-23`.
- **This section avoids the matrix workflow's literal file name,** because this doc is not on the list.

| | check | predicted |
|---|---|---|
| **F1′** | `python3 scripts/skus.py --check-readers` | passes |
| **F2′** | C1's checker and its selftest | unchanged: the checker passes, and the three plants are red on the matrix workflow, `workers.yml` and `device-ids.yml` respectively |
| **F3′** | CI at this head | **0 failures**, every check counted |
