# "Overhead now" card (v17)

**Status: spec, not built.** Builds after v16 is promoted, alongside #379 (time zone) and #377
(reboot cap). Line refs at `74866aa`. "AM" = `src/AircraftManager.cpp`.

## Customer view

When a plane passes overhead and nobody is using the device, a compact card appears for ~8 s:
**callsign, type, route, altitude**. Then the screen goes back to what it was showing.
- **Tap** the card: the full detail card opens.
- **Swipe** in any direction: it goes away.

## What exists today (facts this spec builds on)

- **"Overhead"** is `IsOverhead` (AM:10086-10094).
  - It is **horizontal distance only**: <= `overheadKm`, default 3 km, set as "lookup-dist" in the
    radar's unit (AM:1213-1219).
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
  frozen predictions then carry that number.

## Config

"**Show a card when a plane passes overhead**", key `lookup-card`.

**Is default ON right? Only behind "Look up!", and here is why.**
- "Look up!" is opt-in today, and `IsOverhead` has no altitude term. A global default-ON card
  would make the device **take the screen**, every minute at worst, for every airliner crossing a
  3 km circle at cruise. That would happen on units whose owners never asked for any overhead
  attention.
- That is spec 13.3's rule turned around (*Follow gets a screen; it never gets THE screen*). The
  card surfaces on a transition, like Follow's auto-surface, but for a far more frequent event.

**Recommendation:**
- The card is a **sub-option of "Look up!"**: shown and effective only when `lookup` is on, and
  **default ON within it**.
- Owners who asked for overhead attention get the card. New owners get no new screen-taking
  behaviour.
- `lookup-card` is a new key, so its default reaches existing devices with no `ConfigMigration`
  entry. The first save freezes it as rendered, per the rule at `include/ConfigMigration.h:9-27`.
- **If the review wants it on for everyone:** ship it only with the altitude filter (v18 spec)
  respected, or an altitude ceiling on the card itself, so cruise traffic does not trigger it.

## Telemetry (proposal, not a silent addition)

The usage report is a fixed eight-integer struct (`include/UsageReport.h:61-82`; the Worker pins
`USAGE_FIELD_COUNT = 8`, `proxy/src/metrics.ts:393`).

**Proposed:** one counter, **`overheadCards`**: cards shown. Taps on a card already count as
`cardOpens`.

It would ship in **one** v17 format change shared with double-tap's counter: 8 -> 10 integers.
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

> *"When a plane passes overhead, a card shows it for a few seconds. Tap it for details."*

The wording depends on the default decision above. If the card sits behind "Look up!", the line
says *"With Look up! on, ..."*.
