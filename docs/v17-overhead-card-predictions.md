# Overhead card with owner controls: predictions, frozen before any code (2026-10-11)

Spec: [v17-overhead-card.md](v17-overhead-card.md) ("Owner controls"). Frozen before the first line
of code; its sha256 is recorded in the PR. Later findings go in a dated addendum below the line.
**Bench boards only: COM18 (or COM16 Missileer once registered). COM6 and COM5 are off limits.**
**An unreadable result is a FAIL**, never a pass: each check names the positive control that makes
it readable.

## Instruments (they ship; anonymous: a pass number per boot, the setting, a distance -- no aircraft id, no position)

- `[ovh] edge pass=<n> r=<setting><unit> d_km=<x.x>`: the first frame of a pass inside the setting,
  the ONE predicate the ring, the push and the card share (`IsOverhead`);
- `[ovhcard] show pass=<n> d_km=<x.x>` when a card appears, and `[ovhcard] skip pass=<n> why=<off|busy|spacing|screen|alert>`
  when the edge did not become a card;
- `[lookup] ring pass=<n>` and `[lookup] push pass=<n>` when the ring first draws / the push is queued;
- the radii instrument (`[overhead] cross r=...`) stays in, as the traffic control.

## The four owner-control checks (the review's), each with its sabotage

- **(a) OFF survives a reboot.** Toggle OFF on the config page, Save, reboot (power cut), then 30 min
  of live traffic on COM18. **Pass:** 0 `[ovhcard] show`, 0 `[lookup] ring` and 0 `[lookup] push`
  lines in those 30 min, AND the config page reads OFF after the reboot. **Readable only if** the
  radii instrument logs at least one `cross r=<setting>` line in the same 30 min (traffic was there to
  be shown); otherwise FAIL (unreadable). **Sabotage S-a:** skip the NVS write of `ovh-on`; after the
  reboot the default (ON) returns and cards appear -> (a) fails.
- **(b) The distance setting is the distance.** Captured real traffic (workstation-only, never
  committed or printed) replayed on the host through the card policy (`include/OverheadPolicy.h`) at
  settings 1 and 3. **Pass:** cards(1) is STRICTLY lower than cards(3), and every card's trigger
  distance is <= the setting in km. **Readable only if** cards(3) >= 1; otherwise FAIL.
  **Sabotage S-b:** hard-code 3 (ignore the setting) -> cards(1) == cards(3) -> (b) fails.
- **(c) One predicate.** For a given setting, over 30 min of live traffic, the set of pass numbers with
  `[lookup] ring` equals the set with `[ovh] edge` (and so does `[lookup] push` when a topic is set).
  Every card is a subset of the edges (idle and spacing filter it). **Readable only if** at least one
  `[ovh] edge` line; otherwise FAIL. **Sabotage S-c:** give the ring/push its own constant (5) ->
  the sets differ -> (c) fails.
- **(d) Factory reset restores ON, 3.** Set OFF and 1 on the config page, Save; factory reset on the
  device; then the config page reads **ON** and **3**. **Readable only if** the page read OFF and 1
  before the reset (the values were really stored). **Sabotage S-d:** add `ovh-on` to the
  factory-reset preserve list -> the page reads OFF after the reset -> (d) fails.

## Host tests (`test/host/test_overhead.cpp`, `test/host/test_config_migration.cpp`)

- the radius: `RadiusKm(index, unit)` for 1, 2, 3, 5 in mi and km; a unit change keeps the index;
- migration rev 7: `lookup` present and on -> `ovh-on=true`; `lookup-alert` on -> true; both off ->
  false; `lookup` absent -> `ovh-on` absent (default ON); distances 0.5 -> 1, 2.5 -> 2, 4 -> 3,
  6 -> 5, 10 -> 5 (nearest; a tie to the smaller);
- the card policy: one card per pass; >= 60 s between cards; no card when not idle; no card with OFF.

## The card's own rules (from the spec, unchanged)

- one card per pass, idle only (no touch for 30 s), >= 60 s between cards;
- an `ALERT_BENCH` emergency during a card: the card closes and the flash runs;
- Details Off, local source: zero `/enrich` rows for the device at the Worker over the window (the
  wire count, from the other side of the contract) -- sabotage: drop the Off check -> a row appears.

## Outside the set: stop and report

- a card, ring or push with `ovh-on` OFF;
- a card whose `d_km` exceeds the setting;
- a `[lookup] ring` for a pass with no `[ovh] edge`, or the reverse;
- after a factory reset, anything but ON and 3;
- a migrated v16 device whose Look up! was off showing a card.

---

*Addenda below this line, dated, never edits above it.*

## Addendum 2026-10-12: the phone push is its own opt-in (re-frozen before any code changes)

The ruling: `ovh-on` governs the SCREEN only, i.e. the card and the cyan ring. The push is the
`lookup-alert` checkbox, relabelled "Also send to my phone (ntfy)", default OFF, nested under the
switch. It fires only when `ovh-on` AND the box are both true, and a topic is set. Same single
radius; no second radius anywhere. HA (MQTT) is not governed by `ovh-on`. The text above this line
is unchanged and still hashes ca6cfdda32f3. Where an item below restates a check, the item below is
the one graded.

**Migration rev 7, amended.** `ovh-on` <- the old `lookup` (ring) state only. Never saved (no `lookup`
key) -> nothing written, so the default (ON) applies. `lookup-alert` is left exactly as stored; the
migration never writes it. The host table (`test/host/test_config_migration.cpp`):

| v16 `lookup` | v16 `lookup-alert` | `ovh-on` written | `lookup-alert` after |
|---|---|---|---|
| true | false | true | false (as stored) |
| false | true | **false** | true (as stored) |
| true | true | true | true (as stored) |
| false | false | false | false (as stored) |
| absent | absent | nothing (default ON applies) | absent |

Row 2 is the one the old OR rule decides differently: OR writes true. It is also the open
consequence in the spec. In v16 that owner received overhead pushes, because v16's push ignored the
ring (AM:10151). Under "push needs the switch" they stop. That is built as stated, pending a ruling.

**The push predicate is pure and host-tested.** `overhead::PushFires(on, box, topicSet)` in
`include/OverheadPolicy.h`, true only for (true, true, true). All 8 combinations are in the host test.

**Instruments.** `[lookup] push pass=<n>` is logged only when PushFires holds and the POST queued.
Push counts are also taken from the wire: a throwaway bench topic, polled at ntfy.sh, with **counts
only** (bodies never printed). The device count and the wire count must agree.

**(a), amended: OFF survives a reboot, with the push box ticked.** Same as above, but the window runs
with the push checkbox **ON** and the bench topic set, so "0 push" is the switch's doing and not the
box's. Pass: 0 `[ovhcard] show`, 0 `[lookup] ring`, 0 `[lookup] push`, and 0 messages at the topic.
Readable only if at least one `cross r=<setting>` line falls in the window.

**(c), replaced: the ring and the card fire on one set; the push fires on it only when ticked.** Two
30 min live windows at one setting, the same session, the bench topic set:
- **(c1) box ON.** The set of passes with `[lookup] ring` == the set with `[ovh] edge` == the set with
  an `[ovhcard]` line (show or skip: the card DECIDES on every edge; idle and spacing then filter
  it). The set with `[lookup] push` == that same set. The wire count == the push line count.
- **(c2) box OFF.** Ring set == edge set == card-decision set, as in (c1), and **0** `[lookup] push`
  lines, **0** messages at the topic.
- **Readable only if** each window has at least one `[ovh] edge`. A window with none is a FAIL
  (unreadable), not a pass.
- **Sabotage S-c1: give the push its own constant radius (5 in the device unit, setting 3).** In
  (c1) the push set != the edge set -> FAIL. Readable only if the window has at least one pass with
  `cross r=5` and no `cross r=3`. Otherwise this run did not exercise the sabotage: report "not
  applied" and extend the window.
- **Sabotage S-c2: tie the push to `ovh-on` alone, ignoring the box.** In (c2) there are
  `[lookup] push` lines and messages at the topic -> FAIL.

**(e), new: a v16 owner with ring on, push off and a topic gets zero overhead pushes.**
- **Method.** Host replay (`test/host/replay_overhead.cpp`, the firmware's own `OverheadFromLegacy`
  and `PushFires`). The v16 config is `lookup=true`, `lookup-alert=false`, topic set, no `ovh-on`,
  cfg-rev 6. It is migrated, then every overhead edge of the replay is run through the push decision.
- **The replay input** is the r=3 crossings of the 24 h radii run: workstation-only, anonymous
  (radius, idle, altitude, uptime). Once (b)'s capture exists, it is replayed too.
- **Pass:** 0 pushes.
- **Control (must hold, or the zero is unreadable):** the same replay with `lookup-alert=true`
  gives pushes == edges, and edges >= 1.
- **Sabotage as written, "the old OR rule" in the migration:** **predicted NOT to fail (e).** For
  ring on, OR and ring-only both write `ovh-on=true`. The pushes come from the push GATE, not the
  migration. Running it shows 0 pushes. That is reported as "this sabotage cannot bite on this
  config", not counted as a pass of the sabotage.
- **The sabotage that does bite (e): the old rev-7 design as a whole** (OR migration + push follows
  `ovh-on` alone). Pushes == edges (about the measured passes/day) -> FAIL.
- **(e2), where the OR rule itself bites:** v16 ring off, push on, topic set. Pass: `ovh-on` written
  false; 0 cards, 0 rings, 0 pushes over the replay. Under the OR rule, `ovh-on` is true and cards
  and rings appear -> FAIL.

**(b) and (d)** unchanged. **(a) and (d) run after the radii run ends** (~20:45 2026-10-12), or on
COM16 (Missileer, 90706931E794) once it is registered via `bench-flash.ps1`. No flash, reboot, power
cut or factory reset on COM18 before then. COM6 and COM5 remain off limits.

**Outside the set, added (stop and report):**
- any `[lookup] push` or topic message while the box is OFF, or while `ovh-on` is OFF;
- a push for a pass with no `[ovh] edge`;
- device push count != wire count;
- a migrated device whose `lookup-alert` value changed;
- in (e), a non-zero push count with the as-written OR sabotage. That would mean the gate is not
  what this addendum says it is.

## Addendum 2026-10-12 (2): decisions 1 and 2 -- re-frozen before any code changes

Everything above this heading is unchanged (it hashed eefea8139ac6). Where an item here restates
one above, this one is graded.

**Decision 1: the migration is OR again.** `ovh-on` <- (`lookup` == true) OR (`lookup-alert` == true).
Never saved (no `lookup` key) -> nothing written (default ON). `lookup-alert` is left exactly as
stored. The push still needs `ovh-on` AND its own box. Every v16 owner keeps every overhead push
they had; ring-off owners who had the push on gain the ring and the card (accepted).

| v16 `lookup` | v16 `lookup-alert` | `ovh-on` written | `lookup-alert` after | keys written |
|---|---|---|---|---|
| true | false | true | false (as stored) | 1 |
| false | true | **true** | true (as stored) | 1 |
| true | true | true | true (as stored) | 1 |
| false | false | false | false (as stored) | 1 |
| absent | absent | nothing (default ON) | absent | 0 |

**Decision 2: the HA state field follows `ovh-on`; the per-aircraft events do not.** Firmware:
`overheadActive = overhead::HaStateFollowsSwitch(showOverhead)`. The per-aircraft event (AM:10609)
stays unconditional. Help line, exactly: *"Home Assistant's per-aircraft events have their own
switch."*

**The replay host case** (`test/host/test_overhead_replay.cpp`, in run.sh, synthetic edges). It
composes the firmware's own `MigrateOverheadSwitch`, `SwitchOn`, `DecideCard`, `PushFires` and
`HaStateFollowsSwitch` over 8 overhead edges: 6 idle, 2 touched, some within 60 s of each other,
so the card policy shows exactly 4 cards when on. It models the wiring; bench check (c) is what
checks the wiring. The 13 checks:
- **(e)** ring on, push off, topic set: R1 `ovh-on` written true; R2 **0 pushes**; R3 CONTROL: with
  the push box on, pushes == edges == 8; R13 cards == 4.
- **(e2)** ring off, push on, topic set: R4 `ovh-on` written **true**; R5 pushes == edges (they keep
  every push); R6 rings == edges (they gain the ring, accepted); R7 `lookup-alert` still true.
- **HA:** ring off, push off, MQTT on: R8 `ovh-on` written false; R9 **0 HA state rises**; R10
  per-aircraft events == edges (independent of the switch); R11 CONTROL: ring on -> HA state
  rises == edges.
- **New unit:** never saved, topic set: R12 the switch reads ON (rings == edges) and **0 pushes**
  (the box defaults off).

**Sabotages, predicted failure counts per binary** (each planted, proven planted, restored):

| sabotage | test_config_migration | test_overhead | test_overhead_replay |
|---|---|---|---|
| S1 ring-only rule (OverheadFromLegacy ignores `lookup-alert`) | 1 (row 2) | 0 | 3 (R4, R5, R6) |
| S2 the migration also writes `lookup-alert`="false" | 6 | 0 | 3 (R3, R5, R7) |
| S3 push follows `ovh-on` alone (PushFires ignores its box) | 0 | 1 | 2 (R2, R12) |
| S4 HA state independent of `ovh-on` | 0 | 1 | 1 (R9) |

Any other count is outside the set: stop and report.

**Tonight's bench (the reviewer's order, nothing earlier):**
1. the radii grade at 20:47;
2. `v17-lp-before.bin` (2bfababf6e6c) -> Daniel's 30 "before" taps;
3. the #403 image, named by base + commits + sha -> (a) OFF + power cut and (d) factory reset.

**Known limits of (a) on tonight's image, stated now:**
- The card itself is not built yet, so `[ovhcard]` cannot appear in any image. "0 `[ovhcard] show`" is
  vacuous and is **not graded tonight**. It is graded when the card lands.
- Tonight's (a) grades the switch on what exists: 0 `[lookup] ring`, 0 `[lookup] push`, 0 messages at
  the bench topic, the page reading OFF after the power cut, and at least 1 `[ovh] edge` in the
  window (readability).
- S-a and S-d each need a sabotaged image plus a second power cut or factory reset. They run only if
  Daniel has the time, otherwise on COM16 once it is registered. Until then (a) and (d) rest on
  their readability controls, and that is how they are reported.
