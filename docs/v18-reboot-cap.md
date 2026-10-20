# v18: the quiet-hour reboot is held to the quiet policy's 20 h minimum, not the 24 h cap (#377)

**Status: predictions frozen before code (this commit holds no code).**

## The defect

The quiet-hour reboot is the fleet's only daily update check. It asks `DeferRebootWithCause` for permission, and that refuses any reboot less than **86,400 s** after the last one. Consecutive quiet hours are exactly 86,400 s apart, so the nightly reboot sits **on** the cap's boundary:
- a decision that ran **1 s late** last night makes tonight's gap 86,399, which is refused;
- an offset moved an hour **earlier** makes the gap 82,800, which is refused. That is DST spring-forward once #379 ships.

Either way the night's update check is lost, with no retry until the next night. `quiet::MIN_INTERVAL_S` (20 h) was chosen *"so a legitimate next-day fire is never suppressed by a few minutes of jitter"*, and the 24 h cap downstream undoes it. Full analysis and fleet telemetry: issue #377.

## The fix

1. The comparison moves into a pure `include/RebootCap.h`. `DeferRebootWithCause` calls it, and so does the host test. There is no transcription.
2. A **scheduled** deferral (the quiet hour) needs **the shorter of `quiet::MIN_INTERVAL_S` and the cap**:
   - shipping builds: min(20 h, 24 h) = **20 h**;
   - bench builds with `BLIPSCOPE_REBOOT_MIN_S` (the gate test's shortened cap): the override, as today.
3. **Unscheduled** deferrals (the net-wedge rung, the fleet-config floor) keep the **full cap**.
4. Every caller states which kind it is: `scheduled` is a required parameter with no default, so a new caller cannot inherit the wrong rule silently.

## Predictions: host test `test/host/test_quiet_cap.cpp`

It drives the real `quiet::Step` across consecutive nights and calls the shipped `rebootcap::Allows`.

| check | the fix | **sabotage**: scheduled held to the full 24 h cap (today's rule) |
|---|---|---|
| **A** one second of jitter: last night's decision ran 1 s late (gap 86,399 s) | ok, rebooted | **FAIL**, refused |
| **B** the offset moved an hour earlier (gap 82,800 s) | ok, rebooted | **FAIL**, refused |
| **C** CONTROL: a quiet hour 1 h after another reboot | ok, refused | ok, refused |
| **D** CONTROL: 14 nights, no jitter | ok, 14 reboots | ok, 14 reboots |
| **E** CONTROL: an **unscheduled** deferral at 82,800 s refused, at 86,400 s allowed | ok | ok |
| **F** CONTROL: with a 60 s override cap, a scheduled deferral at 61 s allowed, at 59 s refused | ok | ok |

Exit codes: the fix exits 0. The sabotage exits 1 with **exactly A and B** failing. Any other pattern is outside the set: stop and report.

## Field prediction: the first night on a device running v18 (graded in the v18 rollout report)

- On its first full night on v18, each always-on device makes its quiet-hour reboot **at its usual minute**: 336f ~10:00:1xZ, ada1 and 04f8 ~11:00:1xZ while US DST lasts.
- That is one `SW` boot row **86,340-86,460 s** after the previous night's boot row. No night is skipped, and no `deferral refused` appears.
- In steady state the gap is ~86,400 s, far above the 20 h minimum, so the fix changes **nothing visible** on a normal night. It only matters on jitter and DST nights.
- **The 20 h minimum itself cannot be seen in telemetry.** The device's `lastReb` stamp is not reported, so this grades only that the steady state is unchanged.
- A device that skips a night, or reboots twice in one, is reported with its rows as returned.

**Customer line:** none. An owner notices nothing on a normal night. The fix removes rare missed nights for update checks, which no owner can observe.
