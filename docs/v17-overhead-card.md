# "Overhead now" card (v17)

**Status: spec, not built; decisions recorded from review 2026-10-08.** Builds after v16 is promoted, alongside #379 (time zone) and #377
(reboot cap). Line refs at `74866aa`. "AM" = `src/AircraftManager.cpp`.

## Customer view

When a plane passes overhead and nobody is using the device, a compact card appears for ~8 s:
**callsign, type, route, altitude**. Then the screen goes back to what it was showing.
- **Tap** the card: the full detail card opens.
- **Swipe** in any direction: it goes away.

## What exists today (facts this spec builds on)

- **"Overhead"** is `IsOverhead` (AM:10086-10094).
  - It is **horizontal distance only**: <= `overheadKm`, set as "lookup-dist" **in the radar's unit**,
    default 3 (AM:1213-1219). **On a miles device that is 3 mi = 4.8 km, not 3 km**: every one of the
    163 lines of the pass-rate measurement reported `r_km=4.8` (corrected 2026-10-11).
  - It is **not an altitude test**. An airliner at 37,000 ft within 3 km is "overhead".
- **"Look up!" is opt-in.** `lookup` (the cyan ring) and `lookup-alert` (ntfy) both default
  **false** (`ConfigurationWebServer.cpp:2829-2831`).
- **The ring** is drawn every frame while the condition holds, on the **Radar face only**
  (AM:3629-3631, `DrawOverheadAlert` AM:5107-5120). It never takes the screen and logs nothing.
- **A "pass"** is one `TrackedAircraft` session. The flags reset when an aircraft drops out for
  longer than the grace window and is re-acquired (AM:3262-3280).

## Rules

1. **Trigger:** the first frame in a pass where `IsOverhead` holds. This is the same predicate as
   the ring, so the card never disagrees with it. The per-pass flag is `overheadCardDone` on
   `TrackedAircraft`, next to `overheadNotified`.
2. **Idle only:** `millis() - lastTouchActivityMs >= 30 s`.
   - `lastTouchActivityMs` is stamped on every touched sample (AM:8854).
   - If someone touched within 30 s, it falls back to today's behaviour (the ring only), and
     **that pass's card is spent, not deferred.** A card arriving later would describe an
     aircraft that has already moved on.
3. **Rate:** one card per aircraft per pass, and **at least 60 s between any two cards**.
4. **Where it may appear:** over **Radar, List and Stats** only.

   **Never shown:**

   | state | variable | why |
   |---|---|---|
   | reset menu open | `resetMenu != Closed` (AM.h:217) | a destructive confirmation |
   | detail card open | `inDetail` (AM.h:233) | someone is reading a card (it closes itself after 3 min idle) |
   | Follow | `screen == Follow` (AM.h:146) | it has its own auto-surface and dwell |
   | Connect, including setup-taken | `screen == Connect`, `tookScreenForSetup` (AM.h:147-154) | **all of Connect, not just setup.** An idle owner may be pointing a phone at the QR. |
   | no location / night clock | `DrawNoLocation` / `NightClockActive` (AM:3344-3347) | no location means nothing is overhead; the night clock means an empty sky |

   **Touch unavailable:** the strip is drawn last (AM:3366), so nothing covers it. While the strip
   is up, the card is placed **in the lower half** (below `StripTopY`'s box, which ends at y=78),
   so the strip's required content is never covered.
5. **Precedence:** emergency flash > overhead card > everything else.
   - The card is **not shown while `visualAlertActive`** (AM:5229-5233).
   - If an emergency or military burst starts while the card is up, **the card closes**.
   - The visual alert is drawn after the screen (AM:3354-3366), so a flash would cover it anyway.
   - **The cloud feed carries no squawk** (`CloudFeed.cpp:168`), so emergency precedence is only
     reachable on OpenSky or a local receiver, and on the bench through `ALERT_BENCH`'s injected
     contact (AM:5936+).
6. **Tap and swipe:**
   - A tap on the card opens the full detail card for that aircraft (`selectedIcao`,
     `inDetail = true`). It counts as a card open (`usage::CardOpened`) because the customer asked.
   - Any swipe dismisses the card **and is consumed**: it does not also change screens or zoom.
7. **Night:** the card never changes brightness and never sets `visualAlertActive`. So it is
   drawn at whatever the backlight is, which at night is the auto-dim level (`UpdateBrightness`,
   AM:4544-4604). Dark background, the detail card's palette, no white fill.

## Card details source (`local-details`, AM:1247-1260)

The setting only applies when the data source is **local**. A cloud-source device always enriches
(`UseCloudEnrich`, AM.h:586-594).

- **Off:** the card shows only receiver data (callsign or hex, altitude, speed, distance) and
  makes **no `/enrich` request**. That is the same data the detail card shows with details Off
  (AM:10945-10959, 11070-11087).
- **Cloud:** if the aircraft is not yet enriched (`metadataState == NotFetched`), the card asks for
  **one** `/enrich`. That is the same request a tap would make, through the same path that applies
  cache hits (AM:9556-9565). An already-enriched aircraft costs nothing.

**Expected extra requests per device per day:**
- **Today's load**, from the Worker's own records over 7 days (Analytics Engine,
  `/api/v1/blipscope/enrich`): medians of **2,555** (`04f8`), **1,223** (`ada1`) and **1,083**
  (`336f`) `/enrich` requests per device per day. Almost all are **background enrichment**
  (`ProcessMetadataLookups`, AM:11103-11229), which takes the **nearest** un-enriched contact
  every 5 s whenever the fleet `enrich` level is Full (the default) and any field needs it. The
  logbook defaults on, so it does.
- An aircraft within 3 km **is** the nearest contact, so it has normally been enriched long before
  it is overhead. **Expected extra: ~0**, in the noise of a ~1,000-2,500/day baseline.
- **Worst case** (fleet `enrich` = off, or nothing needs metadata): one request per overhead pass.
- **The overhead pass rate is not in telemetry today.** The ring logs nothing, and the ntfy push
  goes to ntfy.sh, not the Worker. So the pass rate cannot be read off the fleet. **The first
  build step is to measure it**: a 24 h count of first-`IsOverhead` transitions on COM18. The
  frozen predictions then carry that number. **Decided at review: measure the pass rate first.**

## Owner controls (Daniel, decided 2026-10-11) -- replaces "default ON only behind Look up!"

The overhead card is **owner-controlled from the config page**. A new section, **"Overhead alerts"**:

| control | values | default |
|---|---|---|
| On / Off | one toggle, key `ovh-on` | **ON** |
| Distance | dropdown **1, 2, 3, 5** in the device's own unit (mi or km), key `lookup-dist` | **3** |
| "Also send to my phone (ntfy)" | checkbox nested under the toggle, key `lookup-alert` (the v16 key, relabelled) | **OFF** |

- **The switch governs the SCREEN; the phone push is its own opt-in (amended 2026-10-12, reversing
  2026-10-11).** `ovh-on` drives the **card and the cyan "Look up!" ring** (and the look-up tone on a
  board with a speaker; the tone already follows "ring or push", which now reduces to the switch).
  The push stays a separate checkbox, relabelled **"Also send to my phone (ntfy)"**, default **OFF**,
  drawn nested under the switch. **It fires only when `ovh-on` AND the checkbox are both true** (and
  a topic is set, as today). The old `lookup` checkbox leaves the page and is no longer read. Its NVS
  key is left in place, so a downgrade to v16 still behaves.
  - *What was reversed:* on 2026-10-11 the toggle drove the card, the ring **and the push** together
    (Daniel's answer, "ring + push + card").
  - *Why:* `lookup-alert` posts to the owner's `ntfy-topic`, the same topic owners use for military
    and emergency alerts. With the rev-7 migration as first written ("ring OR push -> ON") plus
    default ON for new units, an owner with a topic set would get on the order of the measured
    148 passes/day as phone pushes they never asked for.
- **One radius, still.** `lookup-dist` is **the only radius in the code**. The card, the ring and
  the push all ask `IsOverhead`, which asks `OverheadDistanceKm` once. The push has no radius of its
  own anywhere.
- **Off** means no card and no ring, and therefore no push (the push needs the switch). Everything
  else on the radar is unchanged.
- **Home Assistant (MQTT): two outputs, two rules (ruling 2026-10-12, decision 2).** Both use the
  same single radius.
  - **The per-aircraft `overhead` event (AM:10609) is independent of `ovh-on`.** It was always
    unconditional and stays so; HA's own switch (the MQTT settings) governs it. The config page
    says exactly that in one line: *"Home Assistant's per-aircraft events have their own switch."*
  - **The state payload's overhead field follows `ovh-on`** (AM:10426, 10439). In v16 it was gated
    on ring-or-push. With the OR migration below, `ovh-on` equals v16's ring-or-push for every
    existing device, so the HA state is unchanged for every current install.
  - *What was reversed:* an earlier draft (f883149) un-gated the state field, so that a broader help
    line would be true. That is reverted; the help line is narrowed to the events instead.
- **The 60 s minimum spacing between cards is an internal constant**, not on the page.
- **The unit:** if the owner changes mi <-> km, the stored index is kept ("3" stays 3 in the new unit),
  never converted. That is what `lookup-dist` already does.
- **Stored distances outside 1/2/3/5** (the old free field allowed 0.5 steps) snap to the nearest
  option at migration; a tie goes to the smaller (fewer cards).
- **Persistence:** both keys live in NVS namespace `config`, so they survive reboot and OTA, and a
  **factory reset clears them back to the defaults (ON, 3)** (`config` is cleared by name,
  `src/FactoryReset.cpp`; only `cloud-key-fac` is preserved).

**Who gets ON at the v17 OTA (Daniel's answer): new units ON; existing devices keep their state.**
A new key's default reaches every device that never wrote it -- including every v16 device in the
field -- so default ON alone would switch the card, the ring and the push ON for owners who had
"Look up!" off. So `cfg-rev` 6 -> 7 migrates (`include/ConfigMigration.h`), **as ruled 2026-10-12
(decision 1):**
- a device that has saved its configuration before (its `lookup` key exists) gets `ovh-on` written
  as **ON if `lookup` OR `lookup-alert` was on**, else OFF;
- a device that never saved (no `lookup` key: factory-fresh, or just factory-reset) gets nothing
  written, so the default, **ON**, applies;
- **`lookup-alert` is left exactly as stored.** The migration never writes it. It is the push
  checkbox now, so a v16 owner's push choice carries over as their push choice.

**Why OR: every v16 owner keeps every overhead push they had.** In v16 the push was independent of
the ring: it fired on `alertOverhead && IsOverhead` (AM:10151), whatever `lookup` said. A ring-only
rule would have migrated a ring-OFF/push-ON owner to `ovh-on` OFF. Since the push now needs the
switch, they would have silently stopped getting the pushes they chose. Under OR they keep them,
and also gain the ring and the card on screen. That cost is **accepted** (ruling).
- *History:* the rule was first written as OR (2026-10-11), changed to ring-only with the push
  opt-in (2026-10-12, faaa510), and returned to OR (2026-10-12, this ruling). The push opt-in itself
  stands: the push needs `ovh-on` AND its own box.

**The rate an owner gets (measured, 2026-10-09/10, COM18 at Daniel's site, 3 mi = 4.8 km):**
148.5 passes/day, 144.0 idle passes/day, **122.1 cards/day after the 60 s spacing** (an upper bound:
the log cannot see the other states that block a card); busiest hour 18; 75% below 10,000 ft. **The
same table at 1, 2, 3 and 5 is being measured** (`measure/v17-overhead-radii`, frozen
`ab3d10337782`, started 2026-10-11 20:45 PDT) and is reported, **not** written into the UI.

## Telemetry (proposal, not a silent addition)

The usage report is a fixed eight-integer struct (`include/UsageReport.h:61-82`; the Worker pins
`USAGE_FIELD_COUNT = 8`, `proxy/src/metrics.ts:393`).

**Proposed:** one counter, **`overheadCards`**: cards shown. Taps on a card already count as
`cardOpens`.

It ships in the **one** v17 format change, **decided as 8 -> 12** (`longPressZooms`,
`overheadCards`, `whatsNewOpened`, `whatsNewQr`; `docs/v17-whats-new.md`). The Worker accepts
exactly 8 or 12 and drops anything else, with a test that sends both and 9/10/11/13 controls.
`usage::Format` and its digits-and-commas test, `recordUsage`'s shape check, and the disclosures
(`README.md` Privacy & telemetry, `proxy/pages/support.html`) all change **in the same commit**,
per CLAUDE.md's telemetry entry. It counts THAT a card appeared, never which aircraft.

## Predictions to freeze before code (drafts; the numbers are filled from the 24 h count)

- On an overhead transition with no touch for >= 30 s: a card appears, logged
  `[overhead] card <n>`, with no identity in the line, and it is gone after ~8 s.
- With a touch within 30 s (bench key `k` stamps `lastTouchActivityMs`, AM:6012): no card; the
  ring only.
- **Details Off, local source**, on COM18 with `data-source=local` fed by
  `scripts/bench-photo-feed.py` and one synthetic aircraft placed inside the overhead distance:
  **zero** `/enrich` rows for the device in Analytics Engine over the window, and zero
  `enrichReqs` in the device's `[perf]` lines. The Worker-side count is the wire evidence, taken
  from the other side of the contract.
- Exactly one card per pass, even if the aircraft lingers inside the circle.
- An `ALERT_BENCH` emergency during a card: the card closes and the flash runs.

**Sabotage, each shown red then undone:**
- **Drop the idle check:** the "touched within 30 s" case shows a card.
- **Drop the Off check:** the Worker records an `/enrich` row for the device in the Off window.
  The device log alone would not be trusted for this.

## Card

Pending line for `docs/CARDS-README.md` (v17):

> *"When a plane passes overhead, a card shows it for a few seconds. Tap it for details. Turn it
> off or change the distance under Overhead alerts on the settings page."*
