# Flush the logbook before every planned reboot (v17 bug fix)

**Status: spec, not built.** Split out of `docs/v18-today-stats.md` at review: it is a defect in
shipping firmware, not a feature. Line refs at `74866aa`. "AM" = `src/AircraftManager.cpp`.

## The defect

- The logbook persists to NVS **at most every 10 minutes**:
  `PERSIST_INTERVAL_MS = 10 min` (`src/Logbook.h:324`), from `MaybePersist()` on every `Update`
  (AM:1530-1532).
- `PersistNow()` is called **only when the logbook is switched off** (AM:1176-1179).
- **Nothing flushes it before a planned `ESP.restart()`.** So every planned reboot can discard up
  to 10 minutes of claims and new sightings.
- **That happens every night.** The 03:00 quiet-hour reboot fired on 14 of 14 nights on every
  always-on device (#377's telemetry).
- The other planned reboots lose the same window:
  - the touch-wedge rung (AM, the `RebootPlan` steps);
  - the preventive weekly reboot (AM:1500-1505);
  - the network watchdog's reboot rung, which goes through `DeferRebootWithCause` (`NetWatchdog.cpp:132`);
  - `main.cpp:724` ("Wi-Fi lost", in `loop()` with the app running);
  - the restart inside `httpUpdate.update()` after an OTA download (`OtaUpdater.cpp:399`,
    `rebootOnUpdate(true)`). That one needs `rebootOnUpdate(false)` and an explicit
    `PlannedRestart` after a successful update.
- A claim made at 02:55 is gone at 03:00, and the customer's Collection page shows it was never
  made.

## Fix

**One function, `PlannedRestart(const char* why)`**, through which every planned restart goes:
1. `logbook.PersistNow()` if the logbook is on and dirty.
2. `usageStore` persist (the same hazard: hourly counters lose up to an hour).
3. `Serial.flush()`.
4. `ESP.restart()`.

- `DeferRebootWithCause` (`OtaUpdater.cpp:229`, its restart at `:269`) calls it through a hook
  registered at `setup()`, so `OtaUpdater` takes no dependency on the app manager. That one call
  covers the quiet hour, the fleet floor and the network watchdog, which all defer through it.
- The touch-wedge `RebootPlan` gains a `Flush` step **before** `Restart`. Its order test
  (`test/host/test_touch_wedge.cpp`) is extended, not loosened.
- **A failed flush never blocks the reboot.** It logs `[logbook] PERSIST FAILED before restart`
  and proceeds. A reboot that waits on flash is a worse failure than ten lost minutes.
- **Exempt by name, in an allowlist with a reason:**
  - the factory and Wi-Fi reset tiers (`main.cpp:682`): they wipe NVS anyway, so flushing first
    is wasted wear;
  - the setup-time restarts (`main.cpp:473`, `:540`, `:557`): they run in `setup()` before the
    logbook is loaded, so there is nothing to flush.
  - The touch-wedge rung (AM:1471) and the preventive reboot (AM:1503) are **not** exempt.

**A check that runs, not a rule that is remembered:** a CI grep refuses any bare `ESP.restart()`
in `src/` outside `PlannedRestart` and the named allowlist. That is CLAUDE.md's *second path* entry
applied to this hazard. The next reboot path cannot be added without either going through the
flush or being visibly exempted.

**Wear:** at most one extra logbook write per planned reboot (~1-2 per day), against the ~144 per
day the 10-minute cadence already makes. Negligible.

## Prediction to freeze before code

On COM18, a bench env with a short quiet hour (`-DBLIPSCOPE_QUIET_HOUR`, already
bench-overridable, `QuietHourPolicy.h:61-67`), logbook on:
1. Claim an aircraft (or let a new type be sighted) **less than 10 minutes after** the last
   `[logbook] persisted` line.
2. Let the quiet-hour reboot fire.

**Expected after the boot:**
- `[logbook] loaded N types` includes that claim;
- `/logbook.json` lists it with today's date;
- the log shows `[logbook] persisted` **after** `[quiet] quiet-hour -> deferring` and **before** the
  ROM `rst:` banner.

**Today's firmware, same steps:** the claim is absent after the reboot. That is the defect,
reproduced first as the baseline.

**Outside the set** (stop and report): the claim is present on today's firmware (no defect to
fix), or absent on the fix.

## Sabotage, shown red then undone

**Remove the flush from `PlannedRestart`** (keep the restart). The same bench steps must show the
claim **lost** after the reboot, and the CI grep must still pass (the path exists; it just does
nothing). That proves the bench test, not the grep, is what catches a hollow flush.

**Second sabotage:** add a bare `ESP.restart()` in a new path. The CI grep must refuse it.

## Telemetry

None. A bug fix.

## Card

None.
