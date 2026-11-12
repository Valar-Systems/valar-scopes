# Bench boards in the fleet reads (registration; predictions before the code)

**Commissioned** by the owner, 2026-11-12 (the Pro 2.1 bring-up, #676): "Once it's on Wi-Fi, it's labelled a bench
board in the fleet reads, so v19's week of readings doesn't count it." The 2.1 enrolled and went on Wi-Fi at about
19:06Z, so from its first check-in it has a `fw:` row. v19's step 8 reads the fleet daily through
`scripts/reconcile-fleet.py`, and day 2 is 2026-11-13. This must merge before that read.

## The change

`scripts/reconcile-fleet.py` gains a bench-board list, read and applied the same way as the production checks' test
identity:
- **Where the ids live:** one device id per line (`#` comments and blank lines allowed), in the file
  `BLIPSCOPE_BENCH_DEVICES` names, else `~/.config/blipscope/bench-devices`. It is never in the repo and never the token
  file: the file holds ids only, no keys.
- **Refusals, each exit 2, never a skip:** a file inside a git working tree; a line that isn't an id (`ID_RE`, as
  everywhere in this script). An absent file means no bench boards.
- **Applied once, right after the test identity is left out** (`kv_ids`). Both reads downstream take that list: the
  KV-only reconcile, and A3.1's behind/current counts, whose `detail` is built from `kv_ids`.
- **One line of output, counts only:** `bench boards: <where read>; <n> row(s) left out as bench, not fleet`. An id is
  never printed.

## Predictions

**The selftest (`scripts/reconcile-fleet.selftest.py`, which CI's workers job runs).** Fixtures only, with EXAMPLE ids:

| row | the fixture | predicted |
|---|---|---|
| **BX1** | KV holds the two units, the test identity and a bench id; the CSV holds the two units; the bench file names the bench id | exit 0, `kv-only=0`, the bench line reads `1 row(s) left out`, and the bench id is printed nowhere |
| **S-BX** (plant) | the same, with the bench-exclusion line removed, in memory | exit 1, `kv-only=1`: the bench id comes back as an unaccounted-for unit |
| **BX2** | the bench file inside a directory that has a `.git` | refused, exit 2, and no reconcile line printed |
| **BX3** | a bench file with one line that isn't an id | refused, exit 2 |
| **BX4** (control) | `BLIPSCOPE_BENCH_DEVICES` names a file that doesn't exist | as today: `bench boards: none configured`, TX2's summary unchanged |
| every existing row (TX2, S-TXRECON, R1 and its five plants, U1, U1-ctl) | unchanged | as before, `SELFTEST PASSED` |

**Rehearsed red first:** the new rows run against main's script, before the change, must read BX1 OFF (no bench
exclusion, so `kv-only=1`) and S-BX BLIND (its anchor exists only after the change).

**CI:** `scripts/reconcile-fleet*.py` is in workers' paths, so the firmware workflow runs 30 (28 success / 2 skipped,
ship-alert running report-only since #675), workers 3, device-ids 1, no-traffic-captures 1: **35 = 33 / 2 / 0**.

**The live proof is the day-2 read (2026-11-13).** Its output carries `bench boards: read from
~/.config/blipscope/bench-devices; 1 row(s) left out as bench, not fleet`, provided the 2.1's `fw:` row exists by
then. If the row isn't there yet, the count reads 0 and says so; that is not a failure.

**Outside the set:** any other reading means stop and decide.
