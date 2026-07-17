# Blipscope (Aviation) — Feature Roadmap & Ideas

Product roadmap for the **Aviation radar edition**. Captured from the 2026-07-16 firmware
review; update it as items ship or priorities change. (Edition-level roadmap — new
Editions — lives in [README.md](README.md#more-editions-on-the-way); proxy work items
live in [proxy/README.md](proxy/README.md#work-queue).)

The review's core finding: the firmware architecture is sound, and the cheap value is in
**data we already fetch, compute, or ship hardware for but never surface to the customer**.

---

## Tier 1 — Quick wins ("alerts & polish" release)

Small diffs, immediate perceived value. Ship together as one minor release.

1. **Visual alert system for military + emergency contacts** — **IMPLEMENTED
   2026-07-16** (`UpdateVisualAlerts`/`DrawVisualAlert` in `AircraftManager.cpp`;
   config keys `mil-visual`, `emg-visual`, `visual-night`; defaults: emergency = ring,
   military = off). Ring pulse bench-verified on the Kit S3 (s3-128) 2026-07-16; the
   flash burst and the night-dim override still want a bench pass before release. *(Priority was raised
   because the launch SKU is a 1.28" board without audio, so on-screen alerting is
   the primary attention channel.)* Per-class selector — **off / ring pulse / full
   flash** — for military and emergency-squawk contacts:
   - *Ring pulse (recommended default when enabled):* a color-coded band around the
     outer bezel (orange = military, red = emergency) pulsing ~1 Hz. Radar stays fully
     readable; the overlay pattern already exists (LOOK UP ring, emergency ping ring).
   - *Full flash:* 2–3 full-screen color pulses **edge-triggered when the contact first
     appears** (per-aircraft dedupe, same pattern as `watchNotified`), then settle to
     the ring pulse. Never a sustained strobe; stay well under 3 flashes/sec
     (photosensitivity / WCAG).
   - *Backlight pulse:* modulate `configuredBrightness` instead of drawing — zero
     render cost, works on every SKU, good as the gentle night variant.
   - *Night behavior toggle:* alerts override auto-dim, or respect it (bedroom vs.
     office device).
   Emergency squawks keep the always-on ping ring as the baseline; the selector adds
   intensity above it.
2. **ntfy alert for emergency squawks** — **IMPLEMENTED 2026-07-16** (config `emg-alert`,
   default off; one-shot per tracking session via the existing `QueueNtfyPost` path).
3. **Distance column in the List screen** — **IMPLEMENTED 2026-07-16** (callsign / type /
   distance / altitude columns, distance in the radar's unit).
4. **Surface route data outside the detail card** — **IMPLEMENTED 2026-07-16** as a new
   "Route" aircraft-info field (`info-route`, default off) drawn as `ORG>DST` on radar
   labels; cloud mode fills it from background enrichment, BYO/adsbdb after first inspect.
5. **Distinct alert tones** — **IMPLEMENTED 2026-07-16** (s3-146 / s3-21 only; inert on
   speaker-less SKUs). Chirp-pattern sequencer: new contact 1×40 ms, watchlist 2×40,
   military 2×70, overhead 3×40, emergency 4×80; master `tones` toggle (default on).

## Tier 2 — Logbook v2 (the headline feature of the next major version)

The Stats screen is entirely instantaneous and the logbook stores only set membership.
For a product whose emotional hook is plane-spotting, **history is the retention feature**.

6. **Logbook depth** — first-seen date per type/airline, per-type sighting counts,
   lifetime record holders (highest / fastest / closest ever, with callsign + date),
   "rarest catch". Fits the existing debounced-NVS-write pattern with compact counters.
7. **Daily/session stats** — today's total contacts, peak simultaneous count, busiest
   hour, a small 24-h sparkline on Stats. All computable from data already flowing
   through `AircraftManager::Update`; needs a ring of hourly counters (RAM + NVS).
8. **"Airports seen"** as a fourth lifelist category, fed from the route enrichment we
   already perform.
9. **Logbook export / web view** — a read-only `/logbook` page (or JSON/CSV download) on
   the already-running async config server. Makes the lifelist portable and shareable.

## Tier 3 — Bigger differentiators

10. **Airport overlay on the radar** — **IMPLEMENTED 2026-07-16** (baked ~250-entry
    major-airport table in `include/Airports.h`, dim markers + IATA codes under the
    aircraft layer, `airports` display toggle default on). Follow-up: a
    `/v1/airports?lat&lon&r` cloud endpoint for the long tail of small fields.
11. **Config apply without restart** — **ALREADY SHIPPED** (verified 2026-07-16): every
    web save raises `ConsumeConfigChanged` and `main.cpp` re-runs `Initialise()` live on
    the loop task, no reboot. The roadmap entry came from a stale header comment, now fixed.
12. **Receiver-health screen for local-feed users.** Feed message rate,
    aircraft-with-position count, last-fetch age — all already known to the fetch path.
    Turns Blipscope into a monitor for the customer's own ADS-B receiver.
13. **Night clock mode.** When auto-dim says night and the sky is empty, show a clock
    face instead of a dead radar. The EAM edition's seven-segment renderer
    (`src/eam/SevenSegment.cpp`) can be promoted to shared code. Makes the device useful
    24 h/day.

## Proxy-side (no firmware change; already accepted in proxy/README.md)

- **Populate the stock-photo library.** Pipeline shipped 2026-07-16; cloud mode is
  photo-less until the harvest runs (known regression vs adsbdb mode).
- **Military enrichment deepening.** Military contacts — the most exciting catches —
  currently render near-empty detail cards. P0 cache fix → mil-block operator floor →
  static airframe dataset.

## Idea backlog (unscheduled)

- **Public spotting leaderboard** — see the full concept below.
- Watchlist match alert sound distinct per entry class.
- HA/MQTT: publish watchlist/emergency hits as Home Assistant *events*, not just state.
- ~~Compass rose / north-up vs. track-up toggle~~ — **shipped 2026-07-16 as "window-up"**:
  config `radar-up` sets the compass bearing at the top of the screen (0 = north-up), so
  the radar matches the view out the user's window.
- "Aircraft of the day" / notable-catch summary card (gamification, pairs with Logbook v2).
- Multi-location profiles (home/work) — probably niche; revisit on demand.
- 1.75" AMOLED premium SKU (466×466 + mic) — hardware roadmap; env stub already in
  `platformio.ini`, variant header missing.

### Deliberately not pursuing (reviewed and parked)

- IMU gestures (shake/tilt navigation) — touch already covers navigation; novelty per
  effort is poor. The Stats tilt readout stays as-is.
- Features on the s3-128's extra hardware (RTC / SD / WS2812 LEDs) — that board is
  bench-only until the touch-wedge gate passes.
- More radar-view eye candy — the sweep/trails/fade layer is already rich; marginal
  value is now in data and history, not pixels.

---

## Concept: public spotting leaderboard

**The pitch:** make spotting competitive — who has seen the most aircraft, the most
unique types, the most airlines/countries. A public web leaderboard with lifetime and
monthly-season boards. Pairs naturally with Logbook v2 (the device already tallies
exactly the numbers a leaderboard needs).

**Feasibility: yes — and we already own the server.** The Cloudflare Worker
([proxy/](proxy/)) already fronts the fleet, has KV, auth, rate limiting, and a
precedent for public unauthenticated pages (`GET /credits`). No new infrastructure
class is required.

### What "an account" actually needs to be

Not a full account system. The minimum viable identity is:

- **Device identity:** the device already derives a unique name (`Blipscope-A1B2C3`,
  MAC-based). A leaderboard ID can be derived the same way (salted hash of MAC so the
  raw MAC never leaves the device).
- **Display name:** one new config-page field ("Leaderboard name") + an opt-in toggle.
  Claim-on-first-submit (first device to submit a name owns it, name pinned to its
  device ID in KV/D1). No email, no password, no PII.
- Later, if desired: link devices to a real valarsystems.com account to merge multiple
  devices — not needed for v1.

### Architecture sketch

```
device (opt-in) ──POST /v1/leaderboard (hourly, ~100 B: deviceId, name,
                     {types, airlines, countries, contacts})──> Worker ──> D1 (or KV)
browser ──GET /leaderboard (public HTML, like /credits)──> Worker
```

- **Firmware delta (small):** config toggle + name field; one queued POST per hour from
  the existing enrichment/ntfy task carrying the logbook tallies. Off by default.
- **Worker delta (moderate):** one authed submit endpoint (same `X-Blip-Key` + rate
  limits), a D1 table (`deviceId, name, tallies, updatedAt`), a public rendered
  leaderboard page + JSON. Monthly seasons = snapshot deltas by month.
- **Cost:** ~24 requests/device/day — noise next to the poll traffic in the existing
  cost model.

### The two honest problems (and the plan)

1. **Cheating.** The firmware is open source and v1 ships one *shared* fleet key, so
   the submit endpoint is spoofable by anyone who extracts the key. Mitigations, in
   order: (a) server-side plausibility caps (tallies can only grow, bounded growth rate,
   counts sanity-checked against what a real sky produces); (b) a **"verified" tier for
   cloud-feed devices** — the Worker serves those devices their blips, so it can
   sanity-check claimed growth against traffic it actually served; local-receiver
   devices show as "unverified" (or self-reported ☂ badge); (c) the real fix is
   **per-device keys**, which proxy v1 explicitly deferred — the leaderboard is the
   feature that eventually justifies them. Start honor-system with caps; a desk-gadget
   leaderboard doesn't need bank-grade integrity on day one.
2. **Privacy.** Our stated stance is operational-telemetry-only with an architectural
   opt-out ([README.md](README.md#privacy--telemetry)). The leaderboard must be
   **opt-in, off by default**, send **counts only — never location, never aircraft
   lists**, and the README privacy section gets a new paragraph describing exactly what
   an opted-in device sends. Local-receiver users who opt in knowingly open one
   narrow channel; everyone else stays at zero.

### Sequencing

Leaderboard **after Logbook v2** — it submits the numbers Logbook v2 creates, and the
deeper lifelist (per-type counts, records) is what makes the competition interesting.
Rough order: Logbook v2 → Worker submit endpoint + public page (staging) → firmware
opt-in toggle → season mechanics.
