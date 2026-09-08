# v11 — quiet-hour reboot with brightness carryover

**PRE-REGISTERED 2026-09-08, BEFORE ANY CODE WAS WRITTEN.** The point of writing
this first is that the outcomes cannot then be chosen to fit whatever the build
turns out to do.

## What ships in v11

1. **The reachability watchdog** — verified in Run 4 (O1–O4 pass, O5 partial by
   argument, O6 pending). No further work.
2. **The quiet-hour reboot** — this document.
3. **The reset-reason reporting fix** — see the blocker below. NOT optional.

## The blocker that must land with it

The reboot cause currently reaches Analytics Engine only on boots that also
**perform an update**, because `NoteOtaAttempt()` — the only writer of the record
carrying `rst` — runs inside the "newer firmware available" branch. A board that
wedges and reboots nightly while already on the latest firmware reports nothing.

v11 makes that worse, not better: a quiet-hour reboot is a *daily* event on
*every* board, and almost none of those boots will have an update to fetch. So
v11 would ship a fleet that reboots itself nightly and cannot say why to anyone.

**The reset reason must move to a path that reports every boot before v11
ships.** Its verification is pre-registered separately once the shape is chosen;
it is named here so it cannot be quietly dropped.

## Why a quiet hour at all

The daily check is currently `millis() - lastOtaCheck > 24h` — uptime-based, so
it **drifts**: a board that boots at 14:00 reboots at 14:00 every day
thereafter, in the middle of the day, in front of the customer. Run 2 and the v10
rollout both fired at ~20:04Z and ~20:34Z for exactly this reason.

A reboot is about 60 s of black screen — measured at 58 s and 59 s, defer to
running-v10. Once a day, unattended, that is fine at 03:00 and not fine at 20:00.

## Design

**Schedule.** When the clock is synced, fire on the **edge** into a fixed local
quiet hour: `localHour != lastLocalHour && localHour == QUIET_HOUR`. Edge-
triggered rather than level-triggered so it attempts once per day rather than
every loop iteration for an hour — the same pattern the alert paths use
("edge-seeded at boot so the backlog never fires"). Local time comes from the
existing `tz-offset` config, which already falls back to a longitude estimate.

**The 24 h cap is unchanged and still shared.** It dedupes the edge and remains
the one implementation — the watchdog's rung 3, the quiet-hour reboot and the old
timer all go through `DeferRebootWithCause`.

**An unsynced clock falls back to the current drifting behaviour**, deliberately.
An unsynced board cannot know what hour it is locally, and refusing to check for
updates at all would be worse than checking at a bad time. Note this is the
opposite disposition to `DeferRebootWithCause`, which REFUSES on an unsynced
clock — there, the risk is an unbounded reboot loop; here, the risk is a board
that silently stops taking updates. Same input, different worst case, so
different default.

**Brightness carryover.** Night dim is *derived*, not stored: `configuredBrightness`
is the day level and the night level is computed as `/5` on a 20 s cadence. On
boot the panel is set to `configuredBrightness` — full day brightness — and the
dim only reasserts on the next evaluation. At 03:00 that is a bright flash in a
dark room, caused by the very feature meant to be invisible.

So the **applied** brightness is stashed at deferral and reapplied at boot before
anything is drawn.

## Pre-registered outcomes

### Q1 — the schedule fires once, at the right hour

| observation | verdict |
|---|---|
| exactly one deferral per day, in local hour `QUIET_HOUR` | **(a) PASS** |
| more than one attempt per day reaching NVS | **(b) FAIL** — level-triggered, not edge-triggered; an NVS-wear bug as well as a logic one |
| fires in the wrong local hour | **(c) FAIL** — check the sign of `utcOffsetSec` before blaming the schedule; a west-of-Greenwich sign error puts it exactly `2 x offset` hours out, which is the tell |
| never fires on a synced board | **(d) FAIL** |

### Q2 — an unsynced clock falls back rather than refusing

| observation | verdict |
|---|---|
| clock unsynced, the 24 h-from-boot timer still fires | **(a) PASS** |
| nothing fires at all | **(b) FAIL** — the board has stopped taking updates entirely, which is worse than the drift this feature removes |
| both paths fire | **(c) FAIL** — two schedules on one cap |

### Q3 — brightness carryover. TWO INSTRUMENTS, because neither sees the other's failure

**Q3a (serial — a population):** the first `setBrightness` after a quiet-hour
reboot equals the value applied before it, and no full-brightness call precedes
it.

| observation | verdict |
|---|---|
| stashed value reapplied, and it is the first brightness call of the boot | **(a) PASS** |
| reapplied, but after a `configuredBrightness` call | **(b) FAIL** — the flash happens and is then corrected, which is the bug |
| not reapplied | **(c) FAIL** |

**Q3b (glass — a rendering):** a person watches a dimmed board through a
quiet-hour reboot and reports whether the screen brightens at any point.

**Q3a cannot answer Q3b.** A log can prove the value and the ordering; it cannot
prove the panel did not flash, because the backlight may power on at full before
any `setBrightness` runs at all — a visible flash with a perfectly clean log.
**Q3b requires a person watching in a dark room** and is not satisfied by any
amount of serial evidence. Recorded now because it will be tempting to call Q3a
sufficient.

### Q4 — the cap still holds

| observation | verdict |
|---|---|
| one reboot per 24 h across all three callers | **(a) PASS** |
| the quiet hour gets its own cap | **(b) FAIL** — two guards on one rule |

### Q5 — none of the above

**Stop and decide.** Do not improvise a reading.

## What the host suite must cover, and what it cannot

**Host** (a pure schedule policy, mirroring `NetWatchPolicy.h`): the hour edge,
midnight wrap, the unsynced fallback, a negative `tz-offset`, and that a board
sitting inside the quiet hour for a full hour attempts exactly once.

**Not host-testable:** whether the panel flashes. See Q3b.
