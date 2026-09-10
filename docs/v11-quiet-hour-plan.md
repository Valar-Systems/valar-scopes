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
| `tz-offset=unset` | **(b) SUPERSEDED 2026-09-08 — see the re-registration below.** This row was written when `main.cpp` resolved an unset zone to `0L`, and it correctly called that a config problem. The `LocalOffset.h` fix makes an unset zone fall back to the longitude, so "unset" is now a normal, correct state and this verdict would mis-read it as a defect |
| local hour disagrees with wall clock | **(c) FAIL** — check the sign of the offset first; a west-of-Greenwich sign error lands exactly `2 x offset` hours out |
| `clock unsynced` on the line | **(d) INCONCLUSIVE** — re-read after NTP lands; the fallback is correct behaviour, not a result |

#### B2a RE-REGISTERED 2026-09-09, for the bench-hour-22 flash

Two things moved since the table above was written: the tz fallback landed
(`77e822d`), and the quiet hour became bench-overridable (`31360f9`). The bench
board has **no `tz-offset` set** and is at longitude -121.29, so the expected
line changes shape entirely. Registered before the flash:

| observation | verdict |
|---|---|
| `tz-offset=unset (-28800 s)`, hour 22, `** BENCH OVERRIDE **` present, and `local hour now N` matching wall-clock Bend | **(a) PASS** |
| `(-25200 s)` — i.e. -7 h | **(b) NOT A PASS, AND NOT A FAILURE EITHER.** It means `tz-offset` survived as `-7` rather than being unset, so the fallback branch was never exercised and B2a establishes nothing about it. Read the config, then decide |
| `(0 s)` with `unset` | **(c) FAIL** — the fallback did not land on the artifact. The host suite passes, so this is a build/flash problem, not a logic one: confirm which image is running before touching the code |
| hour 3, or no `** BENCH OVERRIDE **` | **(d) FAIL — THE FLASH DID NOT TAKE.** The shipping default is 3 and the marker is absent only when the flag is undefined. This is the reading that catches "the upload failed and the old image is still running", which has happened twice on this board |
| anything else | **(e) STOP.** Do not improvise a reading at the moment the result is visible |

Note that **(d) is the anchor control for the flash itself.** It is the only row
that distinguishes a new image from the old one, which matters more than usual
here: the previous two upload attempts on this board both failed, and a failed
flash is silent from the serial side — the board keeps running and keeps
logging.


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
  boot, so the fallback branch was UNOBSERVED here until a capture was attached
  early enough to catch it (it was, 2026-09-09 — see Q2 below; "unreachable" was
  the wrong word). It is covered by the host
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

## The flash procedure itself was the defect: an unconditional watcher

**Recorded because this is a recurring shape, not a one-off clumsiness.** It cost
this gate a full day.

The bench sequence is three steps: kill the serial recorder, upload, start the
recorder again. On 2026-09-08 the third step ran **unconditionally**, and that is
the whole bug:

| | what happened |
|---|---|
| attempt 1 | upload failed. The recorder was started anyway and took `COM119` |
| attempt 2 | failed *because* of the recorder attempt 1 had started — a held port fails esptool in ~10 s |
| after | the board was left wedged; B2a and B3 both slipped a day |
| recovery | an **accidental** one. An overnight computer restart power-cycled USB and freed it |

**The second attempt was worse than the first, and the first attempt is what made
it so.** A failed flash is not self-limiting here — it actively degrades the
conditions for the retry.

Three separate things in this repo already knew a piece of this and none of them
ran:

- `scripts/bench-capture.ps1` documents the kill-and-wait half in a comment
  ending *"Bit me twice."* It covers the step **before** the upload. Nothing
  covered the step after.
- CLAUDE.md's *"a plausible measurement from a build that never landed"* says
  outright: **read the flash's exit status and stop on it.**
- The same file's oldest rule says a check that runs beats a rule that is
  written.

So the fix is not another paragraph. It is
[scripts/bench-flash.ps1](../scripts/bench-flash.ps1), which owns the whole
sequence and **gates the recorder launch on `$LASTEXITCODE`**. On a non-zero
exit it prints why, leaves the port free, and exits with the upload's own status.
It also refuses to start at all if the port never becomes openable — a poll that
takes the handle, not a sleep, because killing the holder returns before Windows
releases it.

**Which entry in CLAUDE.md this is.** It is *"when you add a second path,
enumerate what the FIRST one establishes"*, with the paths being the two
outcomes of a command rather than two call sites. The success path establishes
"a new image is on the board, and the port is free for a recorder". The failure
path establishes **neither**, and the code ran as though it established both.
`bench-capture.ps1`'s comment is the tell the table describes: a hazard stated in
prose on one path, beside another path that does not mention it.

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

## B2a RESULT, 2026-09-09 08:42 PDT: EXPLICIT PATH OBSERVED; FALLBACK HOST-TESTED ONLY

**The registered reading did not run.** B2a was registered against an unset
`tz-offset` resolving to -28800 from longitude. What ran was the EXPLICIT path:
Daniel set -7 and the board honoured it. Those are different branches of
`localoffset::Resolve`, and only one of them was exercised on hardware.

The row is therefore NOT relabelled and the gate is NOT closed:

| branch | status |
|---|---|
| explicit `tz-offset` honoured | **observed on hardware** |
| unset — longitude fallback | **host-tested only** (`test_local_offset.cpp`) |

The real B2a is registered below as **B2a-REAL**, and it reaches the fallback
through the customer's own first-run path rather than in isolation.

Daniel power-cycled COM119 (`Blipscope-31D918`, 192.168.86.32) and set the clock
offset to -7. The capture caught **both** boots, which is more than was asked for
and is the reason two separate things got closed at once:

```
[boot] reset reason=POWERON
[quiet] armed: reboot at local 22:00  ** BENCH OVERRIDE **, tz-offset=0 (+0 s)
        -- local hour now -1 (clock unsynced -- falling back to the 24 h timer)

[boot] reset reason=USB
[quiet] armed: reboot at local 22:00  ** BENCH OVERRIDE **, tz-offset=-7 (-25200 s)
        -- local hour now 8
```

The second boot is the config save restarting the board. It satisfies every
literal cell of row (a) EXCEPT the one the row existed to exercise — the offset
came from an explicit setting, not from the longitude fallback:

| registered | observed |
|---|---|
| hour 22 | 22 |
| `** BENCH OVERRIDE **` present | present |
| ~~unset -> `-28800 s` from longitude~~ | **NOT RUN** — `-7 (-25200 s)`, explicit |
| local hour matches wall clock | 8, against a wall clock of 08:43 PDT |

**Row (d), the anchor control for the flash itself, is excluded**: the marker is
present and the hour is not 3, so the image running is the one that was uploaded
and not the pre-flash image. That mattered more than usual here — two later
upload attempts failed, and a failed flash is silent from the serial side.

Firing arithmetic, stated rather than assumed: offset -25200 s means `LocalHour`
reaches 22 when UTC reaches 05:00, i.e. **05:00Z on 2026-09-10 = 22:00 PDT
tonight**. The 24 h cap expires 22:47:58Z today, about six hours before that, so
the cap cannot refuse it.

### Q2's unsynced-clock branch was observed on hardware, by accident

This document said of Q2:

> The bench boards sync NTP within seconds of boot, so the fallback branch is
> unreachable here. It is covered by the host suite and by no bench observation.

**"Unreachable" was the wrong word, and the note above is corrected to
"unobserved until a capture was attached early enough."** The POWERON boot
printed `local hour now -1 (clock unsynced -- falling back to the 24 h timer)`.
The branch was always reachable — it is live for the few seconds before NTP
lands. Nothing about the system changed; only the instrument's position did.

That is not pedantry. **"Unreachable" is a claim about the SYSTEM and closes the
question. "Unobserved" is a claim about the INSTRUMENT and leaves it open** — which
was the true state, and the one that eventually got the reading.

What this closes and what it does not: the **print** is confirmed, so the policy
does take the fallback path on a real unsynced clock. The **firing** is still
unobserved, and remains so, because the clock synced before any decision was due.
Stated separately because they are different claims.

### And the new brightness field is live

```
[health] frame ... interval=5000ms  bright=255/255
```

255/255 with no `NIGHT` marker, at 08:43 in daylight, which is correct. The field
is what makes B2b readable tonight without scrolling for an edge-triggered
`[dim]` line, and it is what would have answered the "is it actually dimmed?"
question objectively yesterday.

## B2a-REAL — PRE-REGISTERED 2026-09-09 09:0x PDT, BEFORE THE FIX IS FLASHED

One verification closing both open items: the config-page blocker (auto must be
an absent key) and the fallback branch B2a never exercised. Reaching the fallback
**through the customer's own first-run path** rather than by hand is the point — a
fallback that only works when a developer clears a key is not the fallback the
fleet gets.

### The procedure, which is the customer's

1. **Clear** `tz-offset`: load the page, empty the field, save. (This is also the
   first exercise of the new remove-on-empty path.)
2. **Confirm absent, not "":** and this needed an instrument that did not exist.
   The page renders `value=''` for BOTH an absent key and one stored as `""`, and
   the placeholder shows either way, so the two are indistinguishable from the
   page. `GetStoredString` collapses them too. **The invariant the whole config
   fix turns on was unobservable from outside**, which is this repo's oldest
   failure mode wearing a new hat: a fix whose key property cannot be seen is one
   nobody can verify, and a verification that cannot fail proves nothing.

   So `ConfigurationWebServer::HasStoredKey()` was added and the boot print now
   reports THREE states rather than two:

   | print | meaning |
   |---|---|
   | `tz-offset=unset` | key ABSENT — "auto"; the longitude fallback runs |
   | `tz-offset=empty` | key PRESENT and blank — resolves identically, so invisible in behaviour; a state the page must never produce |
   | `tz-offset=<value>` | an explicit setting |

   `unset` keeps the exact token this gate was pre-registered against, so the
   registration below is unaffected by the added discrimination.
3. **Walk the first-run save with the field UNTOUCHED**: re-post the whole form
   exactly as rendered, including the `cfg-form` marker that makes it a
   whole-form save. This is the step that used to manufacture `"0"`.
4. **Reboot** and read `[quiet] armed:`.

Step 3 must be a WHOLE-FORM post. `SaveToggle` writes `"false"` for any absent
checkbox when `cfg-form` is present, so a partial post would be a different code
path from the customer's and would prove nothing about theirs.

### THE ARITHMETIC TRAP, REGISTERED BEFORE THE READING RATHER THAN EXPLAINED AFTER

The instruction for this gate says "a local hour matching wall clock". **It will
not match, and that is a PASS.** Bend is on PDT (UTC-7) in September. The
longitude fallback is nominal solar time and knows nothing about DST:

    lround(-121.29 / 15) = -8  ->  -28800 s  ->  UTC-8, i.e. PST

So the printed local hour will be **exactly one hour BEHIND the wall clock** all
of September. That is `LocalOffset.h` behaving as documented ("nominal solar
time ... can be off by up to ~2 h against a political zone (and ignores DST)"),
not a sign error and not a defect.

Writing it down now because this is precisely the moment the repo's own rule
warns about: seeing `local hour now 8` against a 09:00 wall clock, and improvising
either "close enough" or "sign error" depending on which way the wind is blowing.

| observation | verdict |
|---|---|
| `tz-offset=unset (-28800 s)`, local hour == wall-clock hour **minus 1** | **(a) PASS** — fallback reached through the customer's path |
| `tz-offset=0 (+0 s)` | **(b) FAIL** — the fix did not take; the save still manufactures a zero |
| `tz-offset=unset` but `(+0 s)` | **(c) FAIL, DIFFERENT DEFECT** — the key is absent and `Resolve` still returned 0, which would mean the longitude is missing or unparseable, not the form |
| local hour == wall clock exactly | **(d) STOP** — it should be one behind. Something is applying DST, which this firmware has none of; the `configTime(0,0,...)` premise would be void |
| anything else | **(e) STOP.** Do not improvise |

### Rehearse red — REGISTERED BEFORE RUNNING

Restore the old behaviour and confirm the print returns to `tz-offset=0 (+0 s)`.
A fix whose sabotage does not visibly change the reading did not apply, and this
gate would then be measuring nothing.

**The sabotage has to reproduce the FIRST-RUN CONDITION, not just the old code,
and that is the part worth stating.** The original defect needed the location to
be unset AT RENDER TIME — that is what made `"".toFloat()` zero and put a `0` in
the box. This bench board has a location, so simply reverting the two lines would
render `-8`, post `-8`, store `-8`, and print `-8 (-28800 s)`: a reading identical
in every visible respect to the PASS above, from thoroughly broken code.

That is this repo's self-camouflaging failure exactly — the sabotage would look
like the fix working. So the sabotage forces the first-run condition too, by
computing the old default from an empty longitude rather than the stored one.

Registered expectations for the red build, before it is flashed:

| observation | meaning |
|---|---|
| page renders `value='0'` | the old default is back and manufacturing a zero |
| after an untouched whole-form save, `tz-offset=0 (+0 s)` | **RED CONFIRMED** — the sabotage applied and the fix is what prevents it |
| `local hour now` == the UTC hour (16 at 09:1x PDT) | corroborates: offset 0 means local IS UTC |
| `tz-offset=unset` still | **SABOTAGE DID NOT APPLY** — the rehearsal is broken, not the code |
| `tz-offset=-8 (-28800 s)` | the first-run condition was NOT forced; the reading is worthless because it matches the PASS |

### Consequence for B3, and the restore that has to follow

**Clearing `tz-offset` moves tonight's reboot.** At -28800 s, quiet hour 22 fires
at 22:00 PST = **23:00 PDT**, an hour after Daniel is watching. So B2a-REAL runs
NOW, and `tz-offset` is set back to `-7` before this evening. Recorded because an
un-restored bench state is how a gate gets watched at the wrong hour.

## B2a-REAL RESULT: PASS, 2026-09-09 09:04 PDT — fallback reached through the customer's path

Both open items closed by one verification, run on COM119 against the fix.

### The readings, in order

| step | reading |
|---|---|
| page renders, `tz-offset` explicit | `value='-7'  placeholder='Auto - UTC-8 from location'` |
| clear the field, whole-form save | `value=''`, key REMOVED |
| boot print after the clear | `tz-offset=unset (-28800 s) — local hour now 8` |
| **whole-form save, field UNTOUCHED** | posted `tz-offset=''` across 37 fields with `cfg-form` |
| **boot print after that save** | `tz-offset=unset (-28800 s) — local hour now 8` |

Wall clock at the reading was **09:04 PDT**, printed local hour **8**: one hour
behind, which is registered outcome **(a) PASS** and not a defect. The fallback
is nominal solar time and ignores DST, so at Bend in September it is PST while
the civil clock is PDT. Registered before the reading precisely so this could not
be improvised either way.

`unset` rather than `empty` is the load-bearing half: the key is ABSENT, so the
untouched save wrote nothing. Under the old code the same click stored `"0"`.

### Red rehearsal: CONFIRMED

The sabotage restored the unconditional `TrySaveParam` and the old
longitude-derived `value=`, with the first-run condition forced (the default
computed from an empty longitude, since this board has a location):

| observation | registered as |
|---|---|
| page rendered `value='0'` on an ABSENT key | the old default manufacturing a zero |
| untouched whole-form save then stored it | the defect, reproduced |
| `tz-offset=0 (+0 s) — local hour now 16` | **RED CONFIRMED**; 16 is the UTC hour at 09:1x PDT |

So the fix is load-bearing: remove it and the same customer click puts UTC back.
The sabotage was reverted with `git checkout -- <one path>`, and its absence
checked by grepping for its markers rather than assumed.

### What the verification COULD NOT SEE until an instrument was added

Halfway through, the check could not be written. "Auto" is the ABSENCE of the
key, and nothing could observe absence: the page renders `value=''` for both an
absent key and a stored `""`, the placeholder shows either way, and
`GetStoredString` returns `""` for both. **The invariant the fix turns on was
invisible from every surface.**

`HasStoredKey()` was added and the boot print now separates `unset` / `empty` /
`<value>`. Worth recording as its own event: the fix was already correct, and the
gate would have "passed" without being able to tell the passing world from one
where the save stored `""` instead of removing the key. That is a check that
cannot detect its own failure — caught here only because writing the verification
forced the question.

### Final bench state, and the restore

`tz-offset` set back to `-7`, confirmed on the artifact:

```
[quiet] armed: reboot at local 22:00  ** BENCH OVERRIDE **, tz-offset=-7 (-25200 s) -- local hour now 9
```

Local hour 9 against a 09:09 PDT wall clock — matching exactly, because an
explicit -7 IS PDT. So 22:00 local is 22:00 PDT and B3 is armed for tonight.
Recorder `b3-overnight` attached.

### STILL OPEN: already-configured units keep their zero

The fix reaches devices that have not yet been saved. **Any unit already through
first-run configuration still holds an explicit `tz-offset="0"`** and will still
reboot at 03:00 UTC. This bench board did, until it was cleared by hand above.

Whether the 50 launch units need a migration depends on whether any has been
configured, which is a fact about stock rather than code. The migration option is
written up under the blocker below; it is not implemented, and v11 scope as given
covers the first-run path only.

## GATE M1 — the tz-offset migration. PRE-REGISTERED 2026-09-09, BEFORE FLASHING

### Which board, and why COM16 is deliberately NOT flashed

Read off the two boards' own config pages before choosing:

| board | `tz-offset` field | longitude | stored "0"? |
|---|---|---|---|
| COM16 (`31E794`, .62) | `value='0'` | -121.2858 | **yes** |
| COM4 (.63) | `value='0'` | -121.2858 | **yes** |

Both qualify, because on the OLD firmware an ABSENT key renders the
longitude-derived `-8`; a rendered `0` can therefore only be a stored one. Three
of three bench boards carry it, which is the evidence that the installed
population needs reaching at all.

**COM4 is the subject. COM16 is left untouched as a negative control** — if
COM4's zero disappears and COM16's does not, the migration is what moved it
rather than anything ambient. COM119 is out of scope: it is armed for B3 tonight
and must not be disturbed.

### Registered readings

| observation | verdict |
|---|---|
| first boot prints `[quiet] migrated tz-offset "0" -> auto (derived -28800 s)` exactly once | **(a) PASS** |
| second boot prints NO migration line, and `[quiet] armed:` shows `tz-offset=unset (-28800 s)` | **(b) PASS — the one-shot holds** |
| the migration line appears on EVERY boot | **(c) FAIL** — `cfg-rev` is not being written, so this would re-fire forever |
| no migration line and `tz-offset=0` still | **(d) FAIL** — the predicate did not fire; check the derived offset first, since a longitude that fails to parse yields 0 and correctly suppresses it |
| `tz-offset=empty` rather than `unset` | **(e) FAIL** — the key was written blank instead of removed. Behaviourally identical, which is exactly why the print distinguishes them |
| COM16's stored "0" also disappears | **(f) STOP** — impossible; nothing was flashed to it. Something else is writing config |
| anything else | **(g) STOP.** Do not improvise |

**On the migrating boot the `[quiet] armed:` line above the migration line still
says `tz-offset=0 (+0 s)`, and that is correct rather than a bug.**
`configmigration::Apply()` runs after that print, and its position is
load-bearing (it must follow `configServer.Initialise()`). Only the PRINT lags;
the firing decision calls `QuietHourOffsetSec()` live on every tick, so the
schedule is right immediately. Registered so the stale line is not read as a
failure on the one boot it appears.

### Rehearse red: force the derived offset to 0

The host suite already proves the UTC-user control by sabotage — removing the
`derivedOffsetSec != 0` condition fails exactly one check, the named control, and
it was watched failing.

On hardware the equivalent is to make the DERIVED offset zero while the stored
value stays "0", so the two agree and the migration must decline. Setting the
board's longitude to ~0 does that. Registered expectation: **no migration line,
`tz-offset=0` retained**, proving the migration is conditional on disagreement
rather than firing on any stored zero.

## GATE M1 RESULT: PASS, 2026-09-09 09:2x PDT

| reading | observed | verdict |
|---|---|---|
| COM4 boot 1 | `[quiet] migrated tz-offset "0" -> auto (derived -28800 s)` | **(a) PASS** |
| COM4 boot 2 | no migration line; `[quiet] armed: ... tz-offset=unset (-28800 s) — local hour now 8` | **(b) PASS** — one-shot holds |
| COM16, not flashed | still `value='0'`, no placeholder | **control holds** — the migration moved COM4's zero, not something ambient |

The migrating boot's `[quiet] armed:` line said `tz-offset=0 (+0 s)` as
registered — `Apply()` runs after that print. Only the print lags; the firing
decision reads the offset live.

### Red rehearsal on hardware: CONFIRMED, and it is a true control

The host suite proves the predicate, and its `derivedOffsetSec != 0` condition
was sabotaged and **watched failing** — exactly one check, the named UTC-user
control. But a host test cannot prove that `Apply()` feeds the predicate the
REAL derived offset; a wiring bug there is invisible to it.

So COM16 was given London's longitude (`-0.1`, derived 0) while keeping its
stored `"0"`, and flashed with the same image:

| | COM4 | COM16 |
|---|---|---|
| stored `tz-offset` | `"0"` | `"0"` |
| longitude | -121.2858 — derived **-28800** | -0.1 — derived **0** |
| result | **migrated** | **declined**, `tz-offset=0` retained |

Same firmware, same stored value, one variable changed, outcome flips. The boot
demonstrably ran past the migration point (`config server listening`, `[cloud]
config rev=2`), so this is a decision rather than a boot that never got there.

COM16 was then restored (longitude back, zero cleared) and ends at
`tz-offset=unset (-28800 s)`. Its intermediate placeholder read `Auto - UTC+0
from location` at London and `Auto - UTC-8` after restoring, which incidentally
confirms the placeholder tracks the location.

## METHOD, not just result: two things worth reusing from this gate

Recorded here because both are general, and both were nearly missed.

### 1. If the verification cannot be written, that is a finding about the code

Halfway through B2a-REAL the check could not be expressed. "Auto" is the ABSENCE
of a key, and **nothing could observe absence**: the config page renders
`value=''` for an absent key and a stored `""` alike, the placeholder shows
either way, and `GetStoredString` returns `""` for both.

The reflex is to work around it — infer absence from behaviour, or accept a
weaker check. The right move was to treat the difficulty as the signal:
**the invariant the fix turned on was invisible from every surface**, so the gate
would have "passed" while unable to distinguish the fix from a save that stored
`""` instead of removing the key.

`HasStoredKey()` and the `unset` / `empty` / `<value>` split exist because
writing the verification forced the question. Generalised:

> When a check is awkward to write, ask whether the awkwardness is yours or the
> system's. A property that cannot be observed from outside is not a property
> anyone can rely on — including the next person to change it.

Note the shape it would have failed in: silently, in the reassuring direction,
with a green gate. `empty` behaves identically to `unset` TODAY, which is exactly
why it needed a name — a second undistinguished representation of one behaviour is
how the next divergence gets in without anyone noticing.

### 2. A sabotage must restore the CONDITION, not just the code

The obvious red rehearsal for the config fix was to revert the two lines. **It
would have produced a PASS-shaped reading from thoroughly broken code.**

The original defect needed the location unset AT RENDER TIME — that is what made
`"".toFloat()` zero and put a `0` in the box. The bench board has a location, so
a plain revert renders `-8`, stores `-8`, and prints `-8 (-28800 s)`: identical
in every visible respect to the fixed build's `unset (-28800 s)` reading, because
both resolve to the same offset by different routes.

So the sabotage forced the condition too, computing the old default from an empty
longitude. Only then did the print return to `0 (+0 s)`.

> A sabotage reproduces the STATE the defect needed, not only the lines that
> exploited it. Revert the code and you test the code path; restore the state and
> you test the defect.

The same idea produced M1's device-level control: don't sabotage the migration,
**change the one input the decision turns on** and require the outcome to flip.
A control that varies the input is stronger than one that breaks the code,
because it leaves the artifact under test intact.

## GATE F1 — the v11 fleet rollout. PRE-REGISTERED 2026-09-09, BEFORE PUBLISHING

**Staged, not published.** `FW_VERSION` is 11 and merged to `main`; the GitHub
Release is deliberately NOT created. Creating it is the single action that
publishes, and it happens **only on Daniel's word after tonight's B3 result**.

### Release-artifact checks, run on the v11 shipping build

| # | check | result |
|---|---|---|
| 1 | shipping ELF contains `** BENCH OVERRIDE **` | **0** — the claim |
| 2 | **positive control**: the SAME grep on the bench ELF | **1** — so the grep finds this string when it is there |
| 3 | anchor: shipping ELF contains `[quiet] armed` | 1 — the ELF is readable and holds quiet-hour strings |
| 4 | anchor: shipping ELF contains `migrated tz-offset` | 1 — the v11 discriminator |
| 5 | `scopes.valarsystems.com` | 3 |
| 6 | `scopes-staging` | 0 |
| 7 | negative control: a string that cannot exist | 0 — proves 0 is a reachable answer |

Check 2 is the one that earns the others. A bare "0 occurrences" is equally
consistent with "absent" and "my grep cannot see into this file", and separating
those two is the whole job of a release check.

**The override cannot escape by the other route either:** no `-quiethour` or
`-netfault` env appears in the CI matrix at all, so no bench env can produce a
`firmware-<slug>.bin`. Two independent exclusions — one on the artifact, one on
the pipeline.

### The three bench boards — serial available

| observation | verdict |
|---|---|
| each of COM4 / COM16 / COM119 takes v11 within 24 h of publish, via the deferred-reboot path | **(a) PASS** |
| `changes[]` in the `fw:` ledger shows `10 -> 11` for each | **(b) PASS** — corroborates from the Worker side |
| a boot row per device with **reset `SW`** | **(c) PASS** — a deferred reboot is a software restart; `POWERON` would mean somebody pulled power and the OTA path stays unproven |
| a device still on 10 after 24 h | **(d) FAIL** — check its contiguous heap FIRST: the update check needs the same large block enrichment does, so a fragmented unit loses the remote repair path. That is the `rej=97` finding, not a new one |
| `changes[]` shows `11 -> 10` | **(e) STOP** — a rollback, not a slow update |

### The friend's board — NO serial, so the reading is indirect

The only instrument is the boot-reason telemetry, and its evidence is a **timing
shift**. v10 has no local schedule at all, so its daily reboot drifts on the
uptime timer and its rows land wherever that unit happened to start. Under v11
the reboot is pinned to 03:00 LOCAL.

| observation | verdict |
|---|---|
| daily reason rows stop drifting and settle **near 10:00Z** | **(a) PASS** — 03:00 PDT is 10:00Z; the schedule is local-correct |
| rows settle near **03:00Z** | **(b) FAIL** — pinned to UTC, so that board is resolving offset 0 |
| rows keep drifting after its fw row shows 11 | **(c) FAIL** — v11 installed and the schedule not running |
| rows settle at some other FIXED hour | **(d) NOT A FAILURE — READ THE LONGITUDE.** 10:00Z assumes the board is Pacific. Any fixed hour means the schedule works; WHICH hour is set by that unit's own location |
| anything else | **(e) STOP** |

**WHAT (a) DOES NOT PROVE, stated now rather than argued later.** Rows settling
at 10:00Z is consistent with TWO worlds: the migration cleared a manufactured
`"0"`, **or** that board already carried a correct explicit offset and the
migration correctly declined. Both produce an identical reading, and nothing
available remotely separates them — there is no serial, and the migration line
is not telemetry.

So the honest claim from (a) is *"the schedule is local-correct on that unit"*,
**not** *"the migration ran there"*.

The asymmetry is what makes the reading worth taking anyway: outcome (b), rows at
03:00Z, WOULD prove the board is still pinned to UTC. **It can falsify the
migration having worked; it cannot confirm it.** That is a limit of the
instrument, not a gap to close by inference — confirming it would need the boot
reason to carry the resolved offset, which it does not.

### DO NOT PUBLISH BEFORE B3 — it is a requirement, not just an order of business

"After B3" reads like sequencing politeness. It is not: **publishing first would
corrupt B3.**

COM119's quiet hour fires at 22:00 PDT, and what that does is arm a deferred
reboot whose whole purpose is to run the update check on the way back up. So if
a v11 Release exists at that moment, the sequence Daniel is watching becomes:

1. the quiet-hour reboot — the one B3 is actually about;
2. the board comes up, checks for updates, finds v11;
3. it downloads and flashes it, and reboots AGAIN, about a minute later.

The reading B3 exists to take is whether the panel flashes to full brightness
across a reboot. A second, longer, OTA-driven reboot arriving right behind the
first one makes that observation ambiguous at exactly the moment it cannot be
repeated — it needs a person in a dark room, and the window is once a night.

So the Release is created **after** B3 reports, on Daniel's word. Written down
because the constraint lives in a different subsystem from the gate it protects,
which is the shape this repo keeps getting caught by.

### THE PUBLISH RUNBOOK — one sequence, on Daniel's word, morning of 2026-09-10

Written out so publishing is a single pass with nothing to decide mid-flight.
**Precondition: Daniel has reported B3.** Nothing below runs before that.

**1. The photo-square hard gate (RELEASING.md).** v11 is >= 7, so this applies.
Publishing against an unpublished square library removes photographs from every
card in the fleet by OTA, in one action, with no error anywhere.

```sh
cd proxy && npx tsx scripts/ingest-photos.ts --dry-run --env production
```

If it prints *"could not read the published manifest"*, **STOP** — the count that
follows is a comparison against nothing and reads the same in both worlds. A
clean dry run showing no pending square work is the pass.

**2. Create the Release.** This is the publishing action, and the only one.

```sh
gh release create v11 --title "v11 — ..." --notes-file <notes>
```

CI then builds every matrix SKU, attaches `firmware-<slug>.bin` for each, and
attaches `version.txt` containing `FW_VERSION`. Do not hand-upload assets; the
workflow names them to match what devices request.

**3. Verify the flip, on the artifact devices actually read.** Not the release
page, not the badge — the URL the firmware polls:

```sh
curl -s -L https://github.com/Valar-Systems/valar-scopes/releases/latest/download/version.txt
```

| reading | meaning |
|---|---|
| `11` | published and discoverable — F1's clock starts here |
| `10` | CI has not finished, or `version` was skipped. Check the publish receipts before assuming lag |
| 404 | the `version` job did not run. This is the v9 failure — a slug-less leg blocking the fleet's gate |

Read it **twice, a minute apart**, before believing it. A single read of a
propagating system is a coin flip.

**4. F1 reads over the following 24 h**, per the pre-registration above. Bench
boards from serial plus the `fw:` ledger; the remote board from boot-row timing
only, with the confirm/falsify asymmetry already stated.

**5. Hold the bench watchers through the whole window.**

| label | port | board |
|---|---|---|
| `b3-overnight` | COM119 | `Blipscope-31D918` |
| `f1-com4` | COM4 | `Blipscope-31E9D8` |
| `f1-com16` | COM16 | `Blipscope-31E794` |

Each mapping was read from a boot banner on that exact port on 2026-09-09 after
a flash to it. **They are detached processes: they survive this session but NOT a
host restart** — which is exactly what killed the previous set overnight.

That is survivable rather than fatal, and the reason is worth stating: **F1's
primary evidence is server-side.** The `fw:` ledger's `changes[]` and the boot
rows in Analytics Engine are written by the Worker and outlive any local process.
The serial captures are corroboration and per-event detail. Losing a watcher
costs the detail, not the gate.

### One pre-publish observation that F1 outcome (d) should be read against

COM4 is currently sitting at `largest=10740` with `[health] BUDGET BROKEN` and
`rej=98` — below the contiguous block a TLS handshake needs, with enrichment
already being declined.

Registered now, before publish, so it is not improvised later: **this is not
expected to block its update, and if it does, that is a real finding.** v10's
whole design is reboot-THEN-fetch precisely for this — the reboot defragments,
and the fetch happens on a fresh heap. So a fragmented board should still take
the OTA.

F1 outcome (d) therefore sharpens: a device still on 10 after 24 h is a failure
whose first question is not "was it fragmented" but **"did it reboot at all"**. If
it rebooted and still could not fetch, reboot-then-fetch has not solved what it
was built to solve, and that outranks v11.

### Publish-time gate inherited from RELEASING.md

v11 is >= 7, so the **photo square library must be published before the release**.
It has been since v7, but that gate is stated per-release for a reason:
publishing against an unpublished library removes photographs from every card in
the fleet, by OTA, in one action, with no error anywhere. Re-run the dry run
before creating the tag.

## B3 WAS VOIDED BY THE TOOL THAT VERIFIED IT — 2026-09-10

**Gate B3 ran on 2026-09-09 at 22:00 PDT against a board that was never dim.**
The quiet hour fired correctly and the reboot went through the deferred path, so
the log looks like a clean run. It was not a reading, because the precondition
was gone.

```
[quiet] quiet-hour -> deferring update check to reboot
rst:0xc (RTC_SW_CPU_RST)     [boot] reset reason=SW
[quiet] armed: ... local hour now 22
```

Across 3,030 health lines that night: every one `bright=255/255`, **zero `[dim]`
lines, zero `NIGHT` markers**. B3 outcome (d), VOID.

### The cause was the verification tooling, not the firmware

Three whole-form POSTs were sent to COM119 the previous afternoon to drive
B2a-REAL through the customer's path. They turned off **all 43 toggles**,
`autodim` among them.

| board | whole-form POSTs | checkboxes checked after |
|---|---|---|
| COM119 | 3 | **0 / 43** |
| COM16 | 2 | **0 / 43** |
| COM4 | **none** | **16 / 43** |

COM4 is what settles it: same batch, same firmware, same sky, never POSTed, and
it still had its toggles. And COM119 provably dimmed before the POSTs —
`[dim] brightness -> 51 (night)` at 2026-09-09T02:30:16Z.

### The mechanism, and why the mitigation did not help

The hazard was **known and explicitly mitigated**. A PARTIAL post is unsafe:
`SaveToggle()` writes `"false"` for any absent checkbox when the `cfg-form`
marker is present. That was reasoned about out loud, and a whole-form post was
chosen precisely to avoid it.

The whole-form post was built by parsing the rendered page with an attribute
regex requiring `name=value`. **The page renders a BARE `checked` attribute.** So
no checkbox was ever seen as checked, all 43 were omitted from the body, and
`SaveToggle` wrote `"false"` for every one.

> A mitigation was built, believed, and never verified to do the thing it
> claimed.

That is the entry *when you add a second path, enumerate what the FIRST one
establishes*, turned on a tool rather than on shipping code. "Whole-form" was
believed to establish "every control is represented". It established only "the
`cfg-form` marker is present".

### THE DISPROOF WAS IN ITS OWN OUTPUT, ON EVERY RUN

Each POST printed:

```
posting 37 fields; cfg-form=True
```

37, on a form with **43 checkboxes plus ~20 other inputs**. The correct number is
53. The tool was reporting, every single time, that it was submitting fewer
fields than the form has checkboxes — which is only possible if it was sending
none of them.

**The number was printed, read past, and used as evidence the post had worked.**
Same family as the void-firing-instrument entry, with the shortest possible
distance between the signal and the reader: it was on screen, in the output of
the command being run, at the moment of running it.

The tell generalises: **when a tool prints a count, the count has to be checked
against something.** A bare number is decoration. `posting 37 fields` means
nothing; `posting 53 fields; checkboxes 16 -> 16` cannot be wrong quietly.

### What replaced it

[scripts/bench-config.py](../scripts/bench-config.py), with the parse fixed
(optional attribute values, so bare booleans are captured) and two guards, both
watched firing:

- **Guard 1 — read the form back and assert.** After every POST it re-reads the
  rendered page and compares the checked set against what was intended, naming
  any box that did not stick.
- **Guard 2 — refuse a drop to zero.** A post that would leave 0 boxes checked
  from a nonzero baseline is refused with exit 3 unless `--allow-zero-checked` is
  passed. Unchecking everything is the shape of a parse bug, not an intention.

Rehearsed red: asking it to clear COM119 returns *"REFUSED: this post would leave
0/43 checked, down from 16"*, exits 3, and the board reads 16/43 afterwards —
the refusal happens before the POST, not after.

### Restored

All three boards read **16/43** with `autodim` on, confirmed by reading each
rendered form back rather than by trusting the writer. Daniel confirms COM4's
set is the bench standard and nothing on COM119 was deliberately different.

### What this cost, and what it did not

- **B3 must be re-run.** The 2026-09-09 window is gone.
- **B2b is unreadable from that night** — no dim to carry.
- **M1 and B2a-REAL stand.** Both read `tz-offset`, a text field the old parser
  handled correctly, and both were confirmed from the device's own boot print on
  serial rather than from the form.
- **v11 is unaffected.** This is bench configuration state; no shipped code is
  involved.

## GATE B3 RESULT: FAIL then PASS, 2026-09-10 — and the defect it caught

**B3 found a release blocker on its first valid run.** Brightness carryover, one
of the three things v11 ships, did not work on any reboot.

### Run 1, 12:00 PDT — FAIL

Daniel, on glass: *"the startup screen remained dim, but then the radar got very
bright for the first 3 seconds, then went dim."*

B3 outcome **(b) FAIL** (any brightening, however brief) and B2b outcome **(b)
FAIL** (the dim level appears only after a 255). The serial signature was a
`[dim] brightness -> 51 (night)` line AFTER the reboot — that line only prints on
a CHANGE, so its presence proves the panel had been moved off 51.

**The split in his description is what localised it.** The splash is drawn before
`AircraftManager::Initialise` and the radar after, so "splash dim, radar bright"
points at exactly one line.

### Two causes, and fixing either alone would have shipped a flash

| | cause | why it survives the other fix |
|---|---|---|
| 1 | `Initialise` ran `tft.setBrightness(configuredBrightness)`, discarding the carried level `main.cpp` had already applied at first light | the obvious one |
| 2 | `synced && isNightNow(...)` — an unsynced clock produced `night=false`, so the target became full brightness | fires on any cold boot before NTP lands, with cause 1 fixed |
| 3 | the 20 s guard ignored `lastBrightnessCheck = 0` | `now - 0 < 20000` is true at boot, so the first evaluation waited until 20 s uptime. **That delay was the width of the flash** |

Cause 2 is the [`ProgressAlong` clamp](../CLAUDE.md) shape: converting *"I do not
know"* into a plausible, reassuring value. An unsynced clock is not evidence of
daytime. With no verdict the panel now keeps whatever first light applied.

Cause 3 is prose that did not run, twice over: two call sites set
`lastBrightnessCheck = 0` meaning "evaluate now", one of them with the comment
*"re-evaluate dimming promptly after a reload"*, and the guard honoured neither.

**The carry's own comment already promised the fixed behaviour** — *"every reboot
benefits: quiet hour, watchdog, crash, power cut"* — which is what makes this a
broken promise rather than a missing feature. Daniel stated the requirement
independently as *"the screen should never brighten even during reset when it's
in dim mode on"*, and that is strictly wider than B3 was registered for. **The
registration was too narrow**: it scoped the reading to the quiet-hour reboot,
when `Initialise` runs on every boot. His prep-reset flashing is what showed the
scope was wrong.

### Run 2, 13:30 PDT — PASS

Same board, same bench build, the fix in:

```
[quiet] quiet-hour -> deferring update check to reboot
[ota] update check deferred to reboot (cause=1 largest=11764)
[boot] reset reason=SW
[ota] this boot was armed by a deferred update check
```

| reading | source | result |
|---|---|---|
| no brightening at any point | Daniel, dark room | **(a) PASS** |
| `[dim]` lines after the reboot | serial | **0** — the panel was never changed |
| first brightness after the reboot | serial | `bright=51/255 NIGHT` |
| a plain reset also stays dim | Daniel, 13:27 | **PASS** — the wider requirement |

**Zero `[dim]` lines is the discriminator, and it is worth keeping.** Before the
fix every boot printed exactly one, because the correction had something to
correct. Afterwards there is nothing to print. An absence is a weak signal in
general; it is a strong one here because the paired presence was observed on the
same board, same build, minutes earlier.

### What this says about the gate

B3 is the one reading in this programme a log cannot take, and it earned that
description. No test went red. No instrument reported anything wrong. The
`[dim]` line that gave the mechanism away reads as normal, healthy output — it
only becomes evidence once you know the panel should not have needed correcting.

It was found because a person looked at glass, and the fix was localised in
minutes because that person described **what** brightened (the radar, not the
splash) rather than only **that** it brightened.

### Process notes from the two failed attempts

Both cost Daniel a wait, and both were mine:

- **2026-09-09 22:00 — VOID.** My whole-form POSTs had turned off `autodim`; the
  board was never dim. See the entry above.
- **2026-09-10 12:30 — never fired.** I set `tz-offset=-6.25` and read it as
  6h25m; it is 6h**15**m, so the flash-boot landed at local 13:03, INSIDE the
  target hour, and the policy correctly seeded `lastFiredDay` and refused all
  day. The guard was right and I aimed it at the wrong minute.

The second is why the third attempt computed the offset with the machine and
asserted both halves before touching the board:

```
20:30:00Z + (-7.5h) -> local 13:00   (hour must be 13)      OK
at boot ~20:27Z     -> local 12:57   (hour must NOT be 13)  OK
```

and then confirmed it against the boot print rather than the intent:
`tz-offset=-7.5 (-27000 s) -- local hour now 12`.

## V11 BLOCKER, found 2026-09-09 by B2a landing on its escape hatch

**The longitude fallback added in `77e822d` is unreachable on any device
configured the ordinary way.** The fix is correct; almost nothing reaches it.

### How it surfaced

B2a was re-registered with four outcomes and a "none of the above -> STOP". The
hour-22 flash produced:

```
[quiet] armed: reboot at local 22:00  ** BENCH OVERRIDE **, tz-offset=0 (+0 s) -- local hour now 15
```

Not (a): the offset is `0`, not `-28800`. Not (c) either, and this is the whole
point — (c) was `(0 s)` **with `unset`**, and the line says `tz-offset=0`. The
print renders `tz.isEmpty() ? "unset" : tz.c_str()`, so a literal `0` means the
key is **present and explicitly zero**, which `LocalOffset.h` deliberately
honours ("a customer who typed something meant something", pinned in
`test_local_offset.cpp`). The code did exactly what it should. The registered set
was incomplete, the escape hatch caught it, and that is the mechanism working.

### The defect, which is in the config page and not in the policy

`ConfigurationWebServer.cpp` renders the field from the STORED longitude:

```cpp
const String longitude = prefs.getString("longitude", "");   // line 2285
...
const String tzOffset = prefs.isKey("tz-offset")
    ? prefs.getString("tz-offset", "0")
    : String((int)round(longitude.toFloat() / 15.0));        // line 2352
```

On a factory-fresh device the location has not been set yet, so `longitude` is
`""`, `"".toFloat()` is `0.0`, and the field renders **`0`**. Then:

1. the customer opens the config page for the first time;
2. they type their location — the same form, the same page load;
3. they press Save. The form posts every field, including the `tz-offset` they
   never looked at, carrying the `0` that was rendered before their location
   existed;
4. `TrySaveParam("tz-offset")` writes `"0"` unconditionally;
5. from that moment `tz-offset` is explicitly zero, forever.

No JS recomputes the field when the location changes — checked, there is no
assignment to it anywhere in the page.

**So the ordinary first-run flow bakes in UTC.** Quiet hour 03:00 "local" becomes
03:00 UTC = **20:00 PDT**, which is the exact complaint this feature was built to
remove, and the same 20:00 the v10 rollout hit.

### Naming the alternative, and why it does not save us

*Could the bench board's `"0"` have come from someone typing it, or from a save
made after the location was set?* Possibly — a save with a location present would
have rendered and stored `-8`. But the finding does not rest on how this board
got its value: the first-run path above is established by **reading the code**,
and the board is corroboration rather than proof. The alternative explains one
device; it does not explain away a code path every new customer walks.

### What this does NOT mean

- **Not a regression from `77e822d`.** Before that commit `main.cpp` resolved
  unset to `0L`, so this class of device behaved identically. The commit fixed a
  genuine second-path defect and is worth keeping.
- **Not a policy bug.** `QuietHourPolicy.h` and `LocalOffset.h` both behave
  correctly on every input. Honouring an explicit zero is right.
- **The config page's DEFAULT is right too** — the nominal zone from longitude is
  exactly what `LocalOffset.h` computes. It is evaluated at the wrong moment.

This is the CLAUDE.md entry *a default only reaches keys that were never saved*,
with a twist: there the default was frozen at its correct value, and here it is
frozen at a value that was only ever a placeholder for missing input.

### The options, none of them chosen yet

| | change | reaches already-configured units? |
|---|---|---|
| **1** | render the field EMPTY when the key is absent, with an `auto (from your location)` placeholder; an empty post stores `""` and `Resolve` takes the longitude branch | no |
| **2** | recompute the field in JS when lat/lon change | no |
| **3** | 1 or 2, plus a `ConfigMigration` that DELETES a `tz-offset` of exactly `"0"` on units whose longitude implies a different zone | yes |

Option 3 is the only one that helps a customer who has already configured a
device, and it is the one that needs a decision rather than an implementation:
deleting a stored `0` overrides someone who genuinely meant UTC. The saving grace
is that for a genuine UTC user the longitude fallback returns ~0 anyway (London
is lon ~0), so the two answers agree precisely where the risk is.

**Not implemented pending a scope call.** It affects the 50 launch units only if
any of them have already been through first-run configuration.

## GATE B3 — the dark-room look. PRE-REGISTERED 2026-09-08, BEFORE ANYONE WATCHES

**The only reading in this plan that no log can take.** Serial can prove the
value applied and the order it was applied in; it cannot prove the panel did not
flash, because the backlight can come up at full before any `setBrightness`
executes — a visible flash with a perfectly clean log. So B2b passing does not
close B3, and B3 failing overrides B2b.

### Setup

A bench build with `-DBLIPSCOPE_QUIET_HOUR=<evening hour>` so the reboot happens
when somebody is awake to watch it. The build announces itself:

```
[quiet] armed: reboot at local 04:00  ** BENCH OVERRIDE **, tz-offset=...
```

The board must be **night-dimmed at the moment of the reboot** — auto-dim on, and
either genuinely after dusk or with the configured brightness low enough that the
dim level is visibly different. **A reboot observed at full brightness proves
nothing**, because there is no step to see; that is a void run, not a pass.

### Readings

| observation | verdict |
|---|---|
| the screen goes dark and returns at the SAME dim level — no perceptible brightness step at any point | **(a) PASS — B3 closed** |
| any flash to full, however brief, at any point in the reboot | **(b) FAIL** — and B2b's serial verdict is irrelevant; the panel is the authority on this one |
| a step that is visible but not to full (e.g. dim → half → dim) | **(c) FAIL, and a DIFFERENT bug from (b)** — something is applying a third value. Do not merge with (b): they have different causes |
| the board was not dimmed when it rebooted | **(d) VOID — not a pass.** Nothing was under test. Re-run after dusk or with a lower configured brightness |

### Why (c) is listed separately

The tempting summary of (b) and (c) is "it flashed". They are different defects:
(b) is the carried value never being applied, (c) is it being applied and then
overridden by something else. Collapsing them would send the fix at the wrong
one — the same reason `route_stale` prints its verdict rather than a fixed
string.

### What B3 does NOT establish

That the carried value is CORRECT — only that no step is visible. A board that
carried the wrong dim level and came up steadily at that wrong level passes B3
and fails B2b. The two gates are complementary and neither substitutes for the
other, which is the whole reason there are two.

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
