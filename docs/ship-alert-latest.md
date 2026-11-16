# ship-alert reads each shipping leg's latest result, not a marker (registration; predictions before the code)

**What happened (2026-11-12).** On main `71156714ba44`, run 43266574187, the s3-128 leg failed in attempt 1 on a
network reset. The leg uploaded its `shipfail-s3-128` marker, and a re-run of the failed jobs turned the leg green in
attempt 2. But artifacts belong to the RUN, not the attempt: ship-alert downloaded `shipfail-*` again, found attempt
1's marker, and failed. Main read red, and `scripts/merge-by-rule.sh` refused every merge. The immediate unblock (delete
that one artifact, re-run ship-alert once) is the owner's call, kept separate from this page.

**Why not attempt-scoped marker names.** Naming the marker `shipfail-<slug>-a<attempt>` and reading only this attempt's
names goes FALSE-GREEN. Re-run ship-alert alone over a leg that is still red, and the new attempt has no marker. That
is the reassuring direction, and it is the same shape as today's bug: a guard reading something other than the thing it
guards. Accepted by the reviewer, 17:45Z.

## The change

| file | what |
|---|---|
| `scripts/ship-alert.py` (new) | the decision. Its inputs are the build matrix (`needs.skus.outputs.matrix`) and the run's jobs from the jobs API. A shipping leg is a row with a slug, and its job is exactly `build (<env>, <slug>)`. Per leg, the execution with the highest `run_attempt` decides: **success** is green; **failure / timed_out** is red; **anything else, or no job, or two jobs in one attempt** is INSTRUMENT. Exit 0 green, 1 red, 2 instrument |
| the firmware workflow, `build` | the two marker steps go: nothing reads them any more |
| the firmware workflow, `ship-alert` | `needs: [skus, build]` and `permissions: actions: read, contents: read`. It reads `jobs?filter=latest` for its own run, then runs the script. **On a push to main:** exit 1 sends the ntfy alert and stays red, as today, and fails loud with no `NTFY_TOPIC`; exit 2 fails the job with the script's reason. **On a pull request (new): report only.** It runs the same read and the same decision and prints them. It fails only on exit 2, because a red shipping leg already makes a PR red. So the wiring (the permission, the API, the job names) is proven on every PR, not first on main, where a broken read would block every merge |
| the firmware workflow, `sku-readers` | one step: `python3 scripts/ship-alert.py --selftest`. It is a step, not a job, so the job count doesn't change for that |

## The selftest's cases (in the script; fixtures only, no network)

| case | jobs (by attempt) | expected |
|---|---|---|
| **SA1** transient, re-run | the shipping leg failure (1), success (2) | **0** green |
| **SA2** persists | failure (1), failure (2) | **1** red |
| **SA3** ship-alert alone re-run over a red leg | the leg failure (1) only; ship-alert at (2) | **1** red |
| **SA4** a build-only leg red | `build (blipscope-pro-s3-21)` failure; the shipping leg success | **0** silent |
| **SA5** a shipping row with no job | the matrix has the slug row; the API has no `build (<env>, <slug>)` | **2** instrument |
| **SA6** the reviewer's case: only a slug-LESS row's job, with a name sharing the shipping env's prefix | `build (blipscope-s3-128-wxlook)` failure; the shipping leg success | **0** silent: an exact name, never a prefix |
| **SA7** two executions of one leg in the same attempt | failure (1) and success (1) | **2** instrument |
| **SA8** no shipping row in the matrix | every row build-only | **2** instrument: a matrix with nothing to guard is a matrix this job can't read, never a green |
| **SA9** a skipped shipping leg | the leg's latest is `skipped` | **2** instrument: a leg that never built is not known to be fine |

## Predictions

**Local, before the push:**

| row | predicted |
|---|---|
| **L1** | `python scripts/ship-alert.py --selftest`: every case as in the table; the last line `SELFTEST PASSED`; exit 0 |
| **L2** (control) | a copy of the script whose decision always returns 0: the selftest FAILS on exactly SA2, SA3, SA5, SA7, SA8 and SA9; exit 1 |
| **L3** | the decision run on run 43266574187's real jobs list (read-only, `filter=latest`, today) with today's matrix: **0** green, `build (blipscope-s3-128, s3-128)` success at attempt 2. That is exactly the case the old job read red |
| **L4** | `scripts/skus.py --check-readers`: exit 0. The new files never name the workflow file, so the reader set doesn't change |
| **L5** | `check_ota_url.py --check-wiring` and `check_ota_identity.py --check-wiring` on the edited workflow: exit 0 both (they find steps by name, and none of theirs moved) |

**CI on this PR:**
- The firmware workflow's 30 jobs: ship-alert now RUNS on the PR (report only) and passes, where it was skipped. That
  gives **30 = 28 / 2 / 0** (version and promote skipped).
- device-ids 1 and no-traffic-captures 1. No workers run (no `proxy/` and no workers-path script), and no web-renderer
  run.
- **Total: 32 = 30 / 2 / 0.**
- ship-alert's log on this PR reads the report-only line with `1 shipping leg read` and
  `build (blipscope-s3-128, s3-128): success`.
- sku-readers' log carries `SELFTEST PASSED`.

**After this merges, every PR's firmware tally is 30 = 28 / 2 / 0, not 27 / 3.** A docs-only PR becomes 32 = 30/2/0,
a src change 34 = 32/2/0 (with #668: 38 = 36/2/0), and a workers change adds 3. Every session predicting CI needs this
line. The reviewer is told at merge.

**The first push to main after merge:** ship-alert reads green on its own run, with `1 shipping leg` success. It is
the live proof of the alerting branch's wiring, the half no PR run can exercise.

**Outside the set:** an INSTRUMENT on this PR's ship-alert means the wiring is wrong (the permission, the name format,
or the API shape). That is read and fixed before merge, which is the reason for the report-only mode. Any other reading
not in these tables means stop and decide.

**Size:** about 0.5 d.

## Results, local (2026-11-12 ~17:55Z)

This page was written before any row below ran, but it is committed together with them. It was not pushed alone
first, and that is stated here rather than implied.

| row | read | verdict |
|---|---|---|
| **L1** | SA1 0, SA2 1, SA3 1, SA4 0, SA5 2, SA6 0, SA7 2, SA8 2, SA9 2; `SELFTEST PASSED`; exit 0 | as predicted |
| **L2** | the always-green copy: OFF on exactly SA2, SA3, SA5, SA7, SA8 and SA9; `SELFTEST: 6 case(s) off`; exit 1 | as predicted |
| **L3** | run 43266574187, `filter=latest` (30 jobs): `build (blipscope-s3-128, s3-128): success (attempt 2)`, `green: 1 shipping leg(s) read, all success`, exit 0. With `filter=all`, both attempts' executions of the leg present (2): the same verdict, attempt 2 deciding | as predicted. The old job read this run red |
| **L4** | `reader sweep OK: 16 reviewed mention(s), no parser but skus.py.`, exit 0 | as predicted |
| **L5** | `check_ota_url.py --check-wiring` exit 0; `check_ota_identity.py --check-wiring` exit 0 | as predicted |

## Two notes from the review (the reviewer, 17:55Z)

**What the graded evidence is.** L1-L5 above were committed together with this page, not pre-registered, so they are
not pre-registered evidence. The graded evidence for this change is this PR's CI (32 = 30/2/0, ship-alert's
report-only line, sku-readers' `SELFTEST PASSED`) and the first push to main after it merges (ship-alert green on
its own run, `1 shipping leg(s) read, all success`).

**A new way for a PR to go red for a GitHub-side reason.** On a pull request, ship-alert now reads the jobs API. If
that read fails (a 5xx, a rate limit, a network reset), the script reads INSTRUMENT and the PR's ship-alert goes red.
That is by design: it never reads an unseen leg as green. But nothing in the PR's code caused it. The GitHub-side
re-run rule applies: quote the run, the job, the step and the error to the reviewer first, then `--failed` once per
job per sha. An INSTRUMENT whose error is about this repo's own wiring (403 on `actions: read`, no job named
`build (<env>, <slug>)`) is not GitHub-side, and is fixed rather than re-run.
