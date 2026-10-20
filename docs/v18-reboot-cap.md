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

## Amendment, 2026-10-20: grading rules for the v18 rollout, set before any v18 result exists

These are **rules for how the rollout is graded**, set at review before v18 is cut or promoted. They are not changes to the predictions above.

1. **A device offline at promote time is listed as silent, not graded.** The field prediction names 336f, which has made no request since **2026-10-17 18:41:48Z**. If it is still silent when v18 is promoted, it is listed under "silent" with its last report, and no criterion is graded against it.
2. **In the 24 h after a unit's update restart, its boot rows must be that restart plus its scheduled quiet-hour reboot(s) under item 3, and nothing else. Any other boot row in that span is a FAIL.** This is judged on each unit's own timeline, so a unit that updates in the evening is expected to make its quiet-hour reboot that same night. That is exactly what COM6 did in v17, where a fixed-window count of "exactly one boot row" failed it as worded.
3. **New criterion: the scheduled quiet-hour reboot inside the window**, the behaviour #421 changes.
   - **Pass:** for every always-on device, each scheduled quiet-hour reboot that falls inside the grading window happens: one `SW` boot row at the device's usual minute (±3 min) on every night the window covers. That includes the night after its update, and any night whose reboot comes less than 24 h but at least 20 h after the previous stamped one.
   - **A skipped quiet-hour night is a FAIL.** Its rows are reported as returned.
   - **Exemptions: a night is not graded, and is listed with its rows, if:**
     - the device made **no requests that night**; or
     - it logged an **unscheduled deferred reboot** (net wedge or fleet floor) **in the 20 h before that quiet hour**. The cap refuses that night by design (`test_quiet_cap.cpp`, control C).
   - **What it cannot see:** the 20 h minimum itself is not visible in telemetry, because `lastReb` is not reported. This criterion grades that no scheduled night is lost, which is the customer-visible half of the change.

## Bench board 6E:64 leaves the Blipscope fleet (2026-10-20)

Daniel released USB serial `907069326E64` (COM6 that day) to become the Weatherscope board. No v18 check needs it: Board #1's first-run leg, the #387 runs and the prerelease acceptance all run on E2:08. Its **last Blipscope state**, read before it was reflashed:

| | value | read from |
|---|---|---|
| firmware | `blipscope-s3-128 · v17 · cloud` | its config page's build string, 2026-10-20 16:43Z |
| device id | `04f8…` (prefix only) | its config page |
| last boot | 2026-10-20 11:00:28Z, `SW`: the scheduled quiet-hour restart | the Worker's boot rows (`scripts/fba-boot-rows.mjs`) |
| uptime | **5 h 44 m** at 16:44Z | now minus that boot |
| boots, 30 days | 34 `SW`, 6 `POWERON`, 7 `USB`; first row 2026-09-21 | the Worker's boot rows |
| heap | **starved at 4 h 41 m of uptime**: largest block 10.7-17.4 KB (under the 20 KB budget), 38 allocation failures, enrichment refused | a passive serial read at 15:41Z, log saved (#245 follow-up) |

**For the v18 rollout grade:** once reflashed, this device stops checking in as a Blipscope. Rule 1 above makes it offline (silent, not graded). If a Weatherscope image ever checks in under the same device id, its rows are not v18 Blipscope rows. **Proposed for the reviewer:** exclude device `04f8…` from the v18 grade from 2026-10-20.
