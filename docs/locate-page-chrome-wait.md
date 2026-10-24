# The locate-page check gives Chrome longer to start, and one second chance (predictions first)

**Why.** On 2026-10-24 00:07Z, `test (proxy)` failed on #479's head `ab437f2`. Case **A2** of `check-locate-page.mjs
--selftest` went **BLIND: "Chrome never opened its debugging port"**, while every other case started Chrome normally.
- **Measured BLIND rate:** **1 in the last 12 `workers` runs** of `test (proxy)`, read 2026-10-24.
- **The ruling** (reviewer chat, pasted by Daniel): the BLIND was the instrument, not the change. The script gives
  Chrome 10 s, and page content cannot affect the port.
- **The re-run** of that one job on the same head passed, A2 included.

**The ruling's follow-up (reviewer chat, pasted by Daniel, 2026-10-24):** "check-locate-page.mjs waits up to 30 seconds
for DevToolsActivePort instead of 10, and when the port never opens it relaunches Chrome once for that case before
reporting BLIND. BLIND stays red; the selftest's anchor case (the override removed) must still report BLIND. Record the
BLIND rate you measured (1 in 12 runs) in the PR."

## The change

`runVariant()` launches Chrome through one function that waits up to **30 s** (300 × 100 ms) for `DevToolsActivePort`.
If the port never opens, that Chrome is killed and a fresh one is launched, with a new profile, **once**. Only after
both launches fail does the case report `BLIND: Chrome never opened its debugging port (2 launches)`, still **exit 2**.
Nothing else in the check changes.

## Predictions

| | check | predicted |
|---|---|---|
| **L1** | the source | the wait is 30 s; at most 2 launches per case; a BLIND still returns 2 |
| **L2** | `check-locate-page.mjs --selftest`, locally and in CI | **passes**, and its anchor case "BLIND geolocation override removed" still reports BLIND (exit 2, as wanted) |
| **L3** | the relaunch path forced: `CHROME` set to an executable that never opens a debugging port (`node`, which rejects Chrome's flags and exits) | the check launches **twice**, then reports `BLIND: Chrome never opened its debugging port (2 launches)` and **exits 2**: BLIND stays red |
| **L4** | CI on this PR | **0 failures** |

**Outside the set:** stop and report.

## Results (local, before CI)

| | read | verdict |
|---|---|---|
| **L1** | the source: `PORT_WAIT_TRIES = 300` (x 100 ms = 30 s), `LAUNCHES = 2`. A relaunch kills the first Chrome and removes its profile; the case's `finally` cleans up the last. BLIND returns `{blind: ...}`, which `grade()` maps to 2 | **as predicted** |
| **L2** | `--selftest` with real Chrome: **SELFTEST PASSED**. P1-P3, A2-A5 as wanted, and the anchor **"BLIND geolocation override removed: exit 2 (want 2)"** | **as predicted** |
| **L3** | `CHROME` set to `node`, which never opens a port: each case printed `(Chrome opened no debugging port in 30 s; relaunching, 2 of 2)` and then `BLIND -- Chrome never opened its debugging port (2 launches)`. The check **exited 2** after 130 s for its two cases | **as predicted**: BLIND stays red |
| **L4** | CI on this PR | follows |
