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

---

## Boot-reason path — Worker half DEPLOYED 2026-09-08, live check BLOCKED

`recordBoot()` + `X-Blip-Boot`, merged as `1cd5c00` and deployed to production
(`/healthz` confirms the commit). 417 tests pass, including the pass condition
stated as an assertion: **a boot with no update available produces a reason row.**

Post-deploy regression check against live fleet traffic: 19 Worker invocations,
all `ok`, every request 200.

### What is NOT verified, and why it stops here

The pre-registered requirement was *deployed and verified live before any
firmware sends the field.* The first half is done. **The second cannot be.**

`recordBoot` is called only on the authenticated path — deliberately, matching
`recordUsage`, so an anonymous caller cannot spend the Analytics Engine budget.
Verifying it live therefore needs an authenticated request carrying the header,
and:

- the operator device key on this machine is **stale** — production returns 401.
  Discriminated rather than assumed: the `enr:dev:` row for that device is
  PRESENT, and a known-good bench device's row is PRESENT too (the control), so
  the device is enrolled and the KEY is what no longer matches. Consistent with
  the 2026-08-31 rotation;
- deriving a fresh one needs `DEVICE_KEY_SECRET`, a Worker secret that cannot be
  read back;
- and no bench board can send the header, because the firmware half does not
  exist yet — which is the whole point of doing the Worker first.

So the ordering guarantee is **partially unmet** and the options are:

1. **Issue a working device key.** Closes the live check properly, before any
   flash, exactly as specified. Daniel's to issue, by file, never through chat.
2. **Accept the live check happening when the firmware lands.** The first board
   to send the header is then simultaneously the first test of the parser. That
   is weaker — it is the arity trap's shape, with device and Worker changing
   together — but the blast radius is small: a parser that drops rows loses
   telemetry, it does not break serving, and `recordBoot` is wrapped so it
   cannot.

**Not chosen here.** Recorded so the gap is visible rather than discovered later
as a green check that never ran.

### Why the boot reason is retired on DELIVERY, when its neighbours are not

**Recorded here, beside the gate that reads it, because the next person to touch
this will notice it is the odd one out and will be right to ask.**

Its two neighbours are deliberately best-effort, and correctly so:

- `UsageStore::Take` commits its delta BEFORE the request leaves
  (`reported = total; Persist()`), so a failed check-in loses that hour;
- `TakeOtaMemReport` clears on read and says so in its header: *"a report lost to
  a failed request is not retried"*.

Both are right. A lost usage delta heals in the next hour's report; a lost OTA
report is re-covered by the next update cycle. Neither loss is systematic.

**This field's loss WOULD be systematic, and that is the whole argument.** A boot
reason occurs once per boot and is gone if dropped — and the boots most worth
reporting are the ones that follow a network problem, whose first check-in is
therefore the one most likely to fail.

> A best-effort boot reason loses **precisely the population it exists to
> measure**, and reports a healthy-looking fleet while doing it.

That is the same defect as the OTA-report coupling this path was built to escape,
one layer further out: there the reason only arrived when an update did; here it
would only arrive when the network was already fine. Both are silent, both bias
the data toward "nothing is wrong", and neither shows up as an error anywhere.

**The rule this generalises to**, worth applying to any future telemetry: it is
safe to drop a sample at random, and never safe to drop one *because of the
condition being sampled*. Ask of any best-effort field — **what makes delivery
fail, and is it correlated with what I am measuring?** For a usage counter the
answer is no. For anything that reports a fault, it is almost always yes.

**Caught by a live attempt, not by review.** The defect was in the code as
written and was found when Daniel paused a board's traffic at the router: the
reporting boot's first check-in would have failed while the pause was still on,
and a correct reboot would have produced no row at all. `include/BootReportPolicy.h`
and its host test are the fix; the retry's own hazard (two rows for one boot) is
pinned separately, so neither flag covers for the other.

### GATE B1 — first contact. PRE-REGISTERED 2026-09-08, before the firmware exists

**Decision: option 2, tightened.** The first firmware to send `X-Blip-Boot` will
be the v11 candidate on a bench board, and that flash is not merely *allowed* to
be the parser's first test — it **is** the test, named, watched, and scored
against readings written now.

That is what separates this from the arity trap. There, device and Worker changed
together and nobody was looking; the failure surfaced later as absent data that
looked like a quiet fleet. Here the Worker shipped first and alone, and the first
device to speak to it does so **with a production tail already running and the
outcomes already written down**. The exposure is identical; the observation is
not.

**Method.** Start `wrangler tail --env production` BEFORE the flash and leave it
running. Flash the v11 candidate to one bench board. The board must boot with **no
update available** — that is the whole point, and it is the default state since
the bench boards sit at `latest`.

| observation | verdict |
|---|---|
| a `boot`-indexed AE row appears, `reason` matching the board's actual reset, on a boot with **no update available** | **(a) PASS — gate closed.** This is the exact case the old design silently dropped |
| the board boots and the tail shows its request returning **200**, but no `boot` row appears | **(b) PARSER OR FIELD DEFECT.** The device spoke and the Worker did not record. Check the header name against the firmware source first — that is the failure the wire tests exist for, and if it got past them the transcription drifted |
| the tail shows **no request carrying the header at all** | **(c) FIRMWARE-SIDE DEFECT.** The Worker is not implicated; the device never sent it. Distinguishes cleanly from (b) precisely because the tail shows the request either way |
| a row appears but the reason is wrong for the boot that happened (e.g. `SW` after a power cycle) | **(d) FAIL** — the value is being produced, but from the wrong source. See the `UNKNOWN` vs `UNKNOWN_0` split in the O6 table for how these differ |
| rows appear for boots that did not happen, or more than one per boot | **(e) FAIL** — the one-shot discipline is broken; this is a fleet-wide AE cost bug as well as a data one |
| none of the above | **(f) stop and decide** |

**(b) and (c) are the pair worth having.** Without the tail they are the same
observation — "no row" — and they have opposite owners. The tail is what makes
the gate diagnostic rather than merely pass/fail, and it costs one terminal
window opened before the flash instead of after.

**Follow-on, not part of the gate:** once firmware exists that sends the header,
`smoke-prod.sh` should grep the three header names out of the firmware source and
assert them against the live Worker, the way it already greps the enrol URLs.
That converts `test/header-contracts.test.ts` from a transcription into a
derivation. It cannot be written until the other side exists, which is why it is
here and not done.

## GATE B2 — the v11 candidate flash. PRE-REGISTERED 2026-09-08, BEFORE FLASHING

**One flash, four gates.** This is also Gate B1 (the first `X-Blip-Boot` sender),
so the production tail starts BEFORE the flash, per B1's method.

### What is observable immediately, and what has to wait

Bend is UTC-7, so local 03:00 is **10:00Z** — roughly eighteen hours after a
flash this evening. Splitting the readings by when they can be taken, so that
"we did not see it" is never confused with "it cannot be seen yet":

| reading | when |
|---|---|
| B1 boot-reason row | first check-in, seconds after boot |
| **B2a** the schedule prints what it resolved | boot log |
| **B2b** brightness carried (serial) | boot log, and only meaningful after a boot that happened while dimmed |
| **B2c** the quiet hour actually fires | the 10:00Z window, next morning |
| **B2d** the cap still holds | same window |
| **B3** no visible flash (glass) | Daniel, in the dark, at the 10:00Z window |

### B2a — the schedule says what it resolved, at boot

Added for exactly this: without it the schedule is invisible until it fires, and
"it did not fire" cannot be told from "it is set for a different hour" without
losing a day to find out.

| observation | verdict |
|---|---|
| `[quiet] armed: reboot at local 03:00, tz-offset=-7 (-25200 s) -- local hour now N` where N matches wall-clock Bend | **(a) PASS** |
| `tz-offset=unset` | **(b) NOT A CODE FAILURE — a CONFIG one.** The board would reboot at 03:00 UTC = 20:00 local, which is the exact complaint this feature exists to fix. Set the offset on the board and re-read before proceeding |
| local hour disagrees with wall clock | **(c) FAIL** — check the sign of the offset first; a west-of-Greenwich sign error lands exactly `2 x offset` hours out |
| `clock unsynced` on the line | **(d) INCONCLUSIVE** — re-read after NTP lands; the fallback is correct behaviour, not a result |

### B2b — brightness carried across the reboot (serial)

Only meaningful for a reboot that happens **while the board is dimmed**, which
the 10:00Z window supplies and a manual evening flash does not.

| observation | verdict |
|---|---|
| the first brightness applied after the reboot equals the level in force before it, and no full-bright value precedes it | **(a) PASS** |
| the dim level appears only after a `255` | **(b) FAIL** — the flash happens and is then corrected, which IS the bug |
| nothing carried; comes up at 255 | **(c) FAIL** — `brightcarry::Recall()` returned 0, so nothing ever wrote it. Check both apply sites record |

### B3 — no visible flash (glass, Daniel)

**Serial cannot answer this and B2b passing does not close it.** The backlight
can come up at full before any `setBrightness` executes at all — a real flash
with a perfectly clean log. Requires a person watching a dimmed board in a dark
room through the 10:00Z reboot.

| observation | verdict |
|---|---|
| the screen goes dark and returns dim, no brightening at any point | **(a) PASS — B3 closed** |
| any brightening, however brief | **(b) FAIL** — and B2b's verdict is then irrelevant; the panel is the authority |

### What this flash does NOT establish

- **Q2 (unsynced-clock fallback).** The bench boards sync NTP within seconds of
  boot, so the fallback branch is unreachable here. It is covered by the host
  suite and by no bench observation, and that is stated rather than implied.
- **The sibling editions' offset — POST-LAUNCH BACKLOG, ruled out of v11 scope
  2026-09-08.** `tz-offset` is the radar's key; the siblings store theirs under
  their own (`fi-tz-offset`, `cl-tz-offset`, ...), so on those builds the quiet
  hour resolves to 03:00 UTC — 19:00–20:00 Pacific, the exact complaint this
  feature removes.

  **All 50 launch units are the radar edition** (confirmed by Daniel), so no
  customer can meet this. It is a BENCH-ONLY limitation, and scoping it into v11
  would mean touching eight manager classes to fix something nobody can
  encounter.

  It becomes real the moment ONE non-radar edition ships, and the trigger is
  therefore a shipping decision rather than a date. Closing it needs either one
  shared config key or one shared accessor. See "Post-launch backlog" below.

## The tz defect, and the boot print that caught it on its first firing

**Found 2026-09-08 at 22:47:59Z, by the line added earlier the same day to make
the schedule visible before it fires.**

```
[quiet] armed: reboot at local 03:00, tz-offset=0 (+0 s) -- local hour now 22
```

Local hour 22 while the board's wall clock read 15:47. `tz-offset` was unset, and
the quiet-hour path resolved that to **UTC** — so its "03:00 local" reboot was
scheduled for 03:00 UTC, **20:00 Pacific**, in front of the customer. The exact
complaint the feature was built to remove, on all 50 launch units.

### It was a code defect, not a config question

`AircraftManager.cpp` has always resolved this correctly:

```cpp
utcOffsetSec = tzStr.isEmpty() ? (long)lround(lon / 15.0) * 3600
                               : (long)(tzStr.toFloat() * 3600.0f);
```

The quiet-hour scheduler needed the same quantity in `main.cpp`, where the app
manager is a different class per edition. It was **re-derived**, kept the
explicit branch, and dropped the fallback:

```cpp
const long tzSec = tz.isEmpty() ? 0L : (long)(tz.toFloat() * 3600.0f);
```

Second path, narrower than the first — and written the same day an entry about
that family went into CLAUDE.md. Not a copy that drifted; **a copy that was born
wrong**, and invisible because both branches look complete.

### The fix is one derivation, and BOTH the decision and the print read it

`include/LocalOffset.h` is now the only implementation, with
`AircraftManager` and `main.cpp` as callers. In `main.cpp` a single
`QuietHourOffsetSec()` feeds the boot print **and** the firing decision.

That pairing is the point rather than tidiness: fixing only the print would have
produced **an instrument reporting correctly while the behaviour stayed wrong**,
which is worse than the honest zero it replaced — the failure would then have
been invisible to the one line built to expose it.

Longitude is required for the radar to function, so the fallback is available on
every working unit. It is nominal solar time and can be ~2 h off a political
zone; a clock cannot tolerate that and a once-a-day reboot schedule can. Worst
realistic case moves 03:00 to somewhere between midnight and 06:00. Zero puts it
at 20:00 for the entire western hemisphere.

### Pre-registered, then rehearsed red

A board with `tz-offset` unset must print a **non-zero** resolved offset matching
its longitude, and a local hour matching wall clock. Bend is -121.29, so -8 h,
and the print's "local hour now 22" must become **14**.

`test/host/test_local_offset.cpp` pins that, and the sabotage was run twice:
returning `0L` outright fails to COMPILE (`lon` unused under `-Werror`), which
proves the edit landed but not that the test detects behaviour; a second version
that still reads `lon` and returns 0 compiles and fails **ten assertions**,
including the named control that the fallback is not zero.

### Third instance this session of an observability line earning its place on its first firing

1. `evt:"upstream_skip"` with a reason — added because a disabled source and a
   latched breaker were both silence; it distinguished them in the first
   production tail after deploy.
2. `[netwd] armed:` printing the full ladder every boot — so a stage that never
   fires can be told from one that cannot.
3. **This one.** `[quiet] armed:` existed only because "it did not fire" could
   not otherwise be told from "it is set for a different hour" without losing a
   day. It caught a shipped-defect-in-waiting the first time it ran, roughly
   seven hours after being written.

None of the three found what it was aimed at. Each found something else, on the
first firing, because it made a previously invisible quantity visible at a moment
somebody was already looking.

## Operational item for Daniel — NOT a v11 gate

**The operator device key on this workstation is stale.** Production returns 401;
the `enr:dev:` row is present and a control device's row is present too, so the
device is enrolled and the key no longer matches — consistent with the
2026-08-31 rotation.

Minting a replacement touches `DEVICE_KEY_SECRET`, which is Daniel's alone to
handle, and it must arrive **by file, never through chat**.

**It does not gate v11.** It costs the ability to run `smoke-prod.sh` and to make
authenticated probes by hand — both real, neither on the release path. Recorded
separately so it is not carried as release risk, and so it does not quietly
become the reason a check gets skipped.

### And a note for O6

Once this path is live AND the firmware sends it, a future O6-style check needs
**no prerelease scaffold**: the reason arrives on an ordinary check-in whether or
not an update was available. The pinned-prerelease setup in the O6 plan is a
workaround for the OTA-report coupling and expires with it.

---

## Post-launch backlog

Not v11. Recorded here rather than in a comment because a limitation that lives
only beside the code that causes it is one nobody finds when the condition that
makes it matter finally arrives.

| item | becomes real when | closing it |
|---|---|---|
| **Sibling-edition quiet hour** — the seven non-radar editions resolve the quiet hour at 03:00 UTC because `tz-offset` is the radar's key | the first non-radar unit ships to a customer | one shared tz key, or one accessor across the eight manager classes |
| **Retire the OTA-report `rst` suffix** — superseded by `X-Blip-Boot` on every boot | the fleet is entirely on v11+, read from the `fw:` listing, NOT from memory | delete the suffix and its parser branch; keep the tests for the old arity until the listing is clean |
| **Derive the header names from the firmware** — `test/header-contracts.test.ts` transcribes `X-Blip-Boot`/`X-Blip-Usage`/`X-Blip-OTA-Mem` rather than deriving them | now — the firmware half exists as of `b01524b` | `smoke-prod.sh` greps them out of `CloudFeed.cpp` and fetches each against the live Worker, as it already does for the enrol URLs |

The third is doable immediately and is the strong form of a check that currently
exists only in its weak form. It is listed here rather than done inside v11
because it touches the production smoke test, and that is not a thing to change
in the same window as a release.
