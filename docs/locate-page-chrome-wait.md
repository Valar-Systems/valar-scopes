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

---

# 2026-11-13: P1 goes BLIND after the click -- the check waits for the page's answer (predictions first, full rigour)

**Why.** `test (proxy)` failed twice on #696, the sea-path code PR:
- head `95d9cb561981` at 13:04Z (job 131902251666);
- head `44349515fd0b` at 13:26Z (job 131913178406).

Both times `--selftest` row **P1** ("sendBeacon plant, CSP removed") read **exit 2** (BLIND, anchor MISSING) where it wants 1.
- **What the log shows:** Chrome started and the click landed (`clicked=true`). The popup still showed its ask
  (`popup said: Share this browser's location ...`), and the opener received nothing (`messages=[]`).
- **Every other row passed.** #696 changes nothing the page or the check reads.
- **What it looks like:** the anchor row, whose override is removed, prints the same shape. `getCurrentPosition` had not
  answered inside the fixed 2.5 s post-click window.

**The survey** (read 2026-11-13 13:50Z, by the session that owns this check). It covers every `workers` run with a
`test (proxy)` log, 48 runs from 2026-11-11 19:09Z on. P1's time is measured from the step start; the image comes from
the job's "Runner Image" block.

| image | runs | P1 BLIND | P1 time min / median / max |
|---|---|---|---|
| ubuntu-24.04 20261101.360.1 | 34 | 0 | 4.0 / 10.0 / 33.5 s |
| ubuntu-24.04 20261111.127.1 | 1 | 0 | 21.5 s |
| ubuntu-24.04 20261111.370.1 | 13 | 2 | 3.7 / 11.2 / 39.0 s |

- **Every other row takes ~3.3 s in every run.** So the first case pays a cold cost of 0-35 s.
- **P1's time does not separate the two outcomes.** On 2026-11-11 23:24Z (image 360, job 130855228698), P1 took 33.5 s and
  **passed**.
- **Both BLINDs are on image 370, back to back:** the last two runs.
- **What the log cannot say:** where the cold cost goes (Chrome's port, the popup's attach, or the page's answer). The
  check prints no phase times.

## The change

1. **Every case prints a `timing:` line.** It shows launch-to-port, port-to-popup, popup-to-click and click-to-answer,
   the popup's final state, and the whole case.
2. **The post-click wait ends on the page's own answer.**
   - **Answers:** `sent`, `denied`, `unavailable` or `timeout`.
   - **Bound:** at most `LOCATE_TIMEOUT_MS + 2 s` (12 s), read from the module. The page's own timeout therefore always
     gets its chance to answer.
   - **Late-leak window:** 2.5 s, counted **from the answer**, or from the end of the wait if no answer came.
   - **Cost:** a fast answer costs what it did before.
   - **What does not change:** the anchor rule (the opener must receive the sentinel, or BLIND) and the leak rules.
3. **New row S1: the answer delayed 5 s, beacon planted, CSP removed; wants exit 1.**
   - **How the delay is made:** the page's `getCurrentPosition` is wrapped so that its success callback runs 5 s late.
   - **What it proves:** under the old 2.5 s window, this row reads today's CI symptom, on purpose.
4. **New row, "P1 again, last": the same plant, run as the final case.** A cold first row and a P1-specific defect then
   read differently.
5. **Privacy: the anchor row no longer lets Chrome ask the OS for a position.** The change in item 2 forces this one.
   - **The defect:** with no override at all, the anchor row lets Chrome consult the machine's real location provider. A
     12 s window on a developer PC is long enough for a REAL fix to arrive. The check also printed received messages
     raw on any anchor miss.
   - **The fix:** the row now sends `Emulation.setGeolocationOverride` with no coordinates, which emulates "position
     unavailable". Received messages are printed as their type, their origin and whether they carry the sentinel. A
     value is never printed.
   - **What the row now exercises:** an **error answer** where it used to exercise **silence**. That changes the gate's own
     anchor row, so it is held to full rigour.
   - **Where silence is still covered:** **W4**, a one-time local control, recorded below with its output.
   - **The pre-change control:** a local baseline of the unchanged check (2026-11-13 13:51Z) passed. Its anchor row
     received nothing, and its output holds 0 position fields (counted, not printed).

## Predictions (frozen before the code)

| | check | predicted |
|---|---|---|
| **W1** | the source | the answer wait is `LOCATE_TIMEOUT_MS + 2000` from the module; the wait ends on sent / denied / unavailable / timeout; the late window is 2.5 s from the answer; every case prints `timing:`; the anchor row sends the override with no coordinates; no output line can print a message's latitude or longitude |
| **W2** | `--selftest`, locally | **SELFTEST PASSED, 10 rows.** S1: exit 1, answer 5.0-6.0 s, state `sent`. The anchor row: exit 2, state `unavailable`, answer under 1 s. "P1 again, last": exit 1. The other seven rows: as before. Wall time 30-45 s (the baseline was 26 s) |
| **W3** | **red-first:** W2 with the answer wait forced back to 2,500 ms (the old rule); the edit asserted applied | red set **exactly {S1}**: exit 2 (want 1), `popup said:` the ask, no messages -- today's CI symptom, made on purpose |
| **W4** | **silence:** S1's delay pushed to 14,000 ms, past the 12 s wait | red set **exactly {S1}**: exit 2, BLIND, state the ask -- an answer that never comes is still BLIND |
| **W5** | the beacon plant emptied (the one constant P1, S1 and "P1 again" share) | red set **exactly {P1, S1, P1 again}**, each exit 0 (want 1) |
| **W6** | privacy, over the saved outputs of W2-W5 and W7 | lines carrying a `"lat"` field: **0**; the anchor row's state is `unavailable` in every run |
| **W7** | the check without `--selftest`, locally | V0 anchor OK, exit 0; V1 opener received 0 location messages, exit 0; overall **exit 0** |
| **W8** | CI on this PR, read twice by full sha, on the 370 image | `workers` green; P1's verdict read against the table below |

**W8's outcome table** (one run per push; P1's `timing:` line decides):

| outcome | P1 | means |
|---|---|---|
| **O1** | exit 1, answer **over** 2.5 s | the slow answer happened and the wait absorbed it: the fix, observed on CI |
| **O2** | exit 1, answer **at most** 2.5 s | not reproduced this run; the fix is unproven on CI but harmless, and the evidence rests on W3 |
| **O3** | exit 2, state `timeout` | Chrome never answered the override within 10 s. Not a slow answer, so this PR is not the fix: **stop and report** |
| **O4** | exit 2, state the ask after 12 s | the page neither answered nor timed out: **stop and report** |
| **O5** | exit 1, but "P1 again, last" fails | the defect is P1's own, not a cold first row: **stop and report** |

**Outside the set:** any other row fails, the job fails elsewhere, or the run lands on an image other than 370 (then it
is reported, and how to get a 370 run is asked, not improvised). The instrument is unreliable at this scale: stop
and decide.

**Amended 2026-11-13 ~14:00Z, before the code was written or anything was run.** Writing the wait loop showed two
frozen rows that would test a race, not the rule. Both rows are corrected here, and nothing has been observed yet.

- **W4's delay moves from 14,000 ms to 17,000 ms.** When no answer comes, the late window runs 2.5 s from the END of the
  12 s wait, so the messages are read at about 14.5 s after the click. A 14 s answer would land inside that window and
  read as an anchor OK: the race, not silence. 17 s is past wait plus window. W4's prediction is otherwise unchanged:
  red set **exactly {S1}**, exit 2, BLIND, state the ask.
- **W3's "old rule" restores BOTH halves of the old rule:**
  - the 2,500 ms bound;
  - the late window counted from the CLICK. Under the old rule, the messages were read 2.5 s after the click whatever
    happened.

  Forcing only the bound would leave a window ending about 5 s after the click, exactly when S1's delayed answer
  arrives. Both edits are asserted applied. W3's prediction is unchanged: red set **exactly {S1}**.
