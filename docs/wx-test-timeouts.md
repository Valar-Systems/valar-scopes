# Two weather tests near vitest's 5 s default: explicit timeouts (predictions first)

**The ruling (reviewer chat, pasted by Daniel, 2026-11-04 ~20:00Z), item 2, verbatim:**
> 2. The timeouts get a fix, not only a re-run. K4' also hit vitest's 5 s default in my own run earlier today, so it sits
>    near the limit. Small PR, predictions first: each of the two tests' durations, read from CI's logs on both legs,
>    and an explicit timeout with margin for each (or a faster test). Their checks and sabotages are unchanged.

**What happened.** Main's CI at `1beba4e` (#521's merge, run 41897250440) failed on its first attempt: two tests timed
out at vitest's 5,000 ms default, with no wrong answer. The re-run of the failed job passed.

## The two tests' durations, read from CI's own logs (`tools/wx/ci_durations.py`), every run that ran them

"Both legs" read as the pull-request runs and main's push runs. Times in ms, from vitest's slow-test lines in the
`test (proxy)` job's log.

| run | event | K4′'s sabotage (`wx-opera.test.ts`, "SABOTAGE: tilesY for tilesX fails cell for cell") | WS2 (`wx-crop.test.ts`, "200 / 200 crops through the worker are byte-identical to the flat reference") |
|---|---|---|---|
| `1ccc7f1` | push | 3,122 | 2,043 |
| `edfe52f` | pull request | 3,265 | 2,231 |
| `04da4bc` | pull request | 3,228 | 2,711 |
| `1beba4e` attempt 1 | push | **5,383** (timed out) | **5,627** (timed out) |
| `1beba4e` attempt 2 | push | 3,996 | 3,450 |
| `e8dfa45` | push | 3,270 | 2,518 |

- **The worst readings:** 5,383 ms (K4′) and 5,627 ms (WS2), both on the slow attempt.
- **The usual readings:** 3.1-4.0 s and 2.0-3.5 s: 41-80 % of the default. A slow runner pushes them past it.

## The fix: an explicit timeout with margin on each; nothing else changes

- **K4′'s sabotage** takes the file's own `SLOW` (180,000 ms), as its other heavy tests do: **33×** its worst reading.
- **WS2's 200-crop test** takes an explicit **60,000 ms**: **10.7×** its worst reading. `wx-crop.test.ts` has no
  timeout constant today, so the number is written at the test with a comment citing these readings.
- **No check, assertion or sabotage changes.** A faster K4′ (counting unreadable tiles instead of catching one
  exception per cell) was considered and not taken: it would rewrite the code under the check, and the ruling keeps
  the checks unchanged.

## Predictions, frozen before the code

| | check | predicted |
|---|---|---|
| **TO1** | the diff | it touches exactly the two tests' closing lines (a timeout argument added) and comments, and nothing else under `proxy/` |
| **TO2** | both test files locally | pass, with the same assertions |
| **TO3** | **the sabotage:** the K4′ test's timeout planted at 1 ms, run locally | **that test times out (red),** which shows the explicit argument governs. Restored from a byte copy, sha checked |
| **TO4** | CI on the PR head, **counted only from the finished run** | **41 check-runs: 38 success, 3 skipped** (`version`, `promote`, `ship-alert`), **0 failed.** Both tests pass, with their durations printed (not graded) |

**Outside the set:** any other reading means stop and report.
