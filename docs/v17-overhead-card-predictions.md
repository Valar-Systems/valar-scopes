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
