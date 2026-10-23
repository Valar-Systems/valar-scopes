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

**Outside the set:** stop and report.
