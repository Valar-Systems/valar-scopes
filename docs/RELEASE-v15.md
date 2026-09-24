# v15 — the release list

**Cut v15 only when all four items below are on `main` AND the print cards are done.** "On main"
means `git ls-tree origin/main` shows the change, not that a PR is open or green (see CLAUDE.md,
*a green signal is about process*). Nothing else rides v15: an item not on this list waits for v16.

Order is priority, highest first.

| # | item | spec | status |
|---|---|---|---|
| 1 | Touch-wedge reboot cap + "touch unavailable" | [v15-touch-wedge-cap.md](v15-touch-wedge-cap.md) | spec accepted; not built |
| 2 | Follow flag in the usage report | this file, §2 | not built |
| 3 | ntfy removal | not yet written in the repo | not built |
| 4 | `nmi` → `NM` | not yet written in the repo | not built |
| — | Print cards | PR #340 | in review; note is one line, 2.7px short of the 0.3in trim margin (decision pending) |

## 1. Touch-wedge reboot cap

After 3 consecutive wedge-triggered reboots with zero touch events, stop rebooting. Show a
persistent strip with the device ID and support@valarsystems.com, and report boot reason
`SW_TOUCHWD`. Full spec, including the NVS counter and its three resets:
[v15-touch-wedge-cap.md](v15-touch-wedge-cap.md).

## 2. Follow flag: the usage report always says "not configured"

**The bug.** `UsageStore::SetFollowEnabled()` (`include/UsageStore.h:67`) has **no callers**, so
`followEnabled` stays `false` and every usage report sends `0` in field 7. Measured: 0 of 1,493
reports in 30 days had it set, including on units where Follow is configured. The flag is
correct by construction (a boolean, never the target); it is simply never written.

**The fix.** Call `SetFollowEnabled()` wherever `followTarget` changes: when it is **set**,
**cleared**, and **loaded at boot** (`src/AircraftManager.cpp` around 895–911, where
`followTarget = want`). The value passed is `!followTarget.isEmpty()`, never the string.

**Test (host).** A configured follow target puts `1` in field 7 of `usage::Format()`, and an empty
one puts `0`. The test drives the real call sites' path rather than calling the setter directly,
so **removing the setter call makes it fail**. That is the regression this bug is.

**Dashboard.** Label the Usage page's Follow column **"unreliable before v15"** until the fleet
is on 15. Every report from firmware below 15 carries `0` whatever the device's configuration.

## 3. ntfy removal

Spec not yet in the repo. Needs one before it can be built.

## 4. `nmi` → `NM`

Spec not yet in the repo. `include/DisplayUnits.h` already calls out the trap to avoid: a missed
site renders statute miles under a nautical label, and the stored unit string must survive a
downgrade.
