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

6. **Logbook depth** — **IMPLEMENTED 2026-07-16** (Logbook v2): per-type first-seen date
   + sighting count, per-airline first-seen date, lifetime records (highest / fastest /
   closest ever with callsign + date, plausibility-bounded), all in the same debounced
   NVS pattern with legacy-blob migration; a compact "Best" line joins the Stats
   LIFELIST block. ("Rarest catch" needs global rarity data — deferred to the cloud.)
7. **Daily/session stats** — **IMPLEMENTED 2026-07-16**: a TODAY block on Stats with
   contacts-since-midnight, peak simultaneous count, busiest hour, and a 24-bar hourly
   sparkline. RAM-only (no flash wear; resets at local midnight/reboot, NTP-gated), and
   the whole Stats screen is now clock-guarded so blocks drop by priority on 240 px panels.
8. **"Airports seen"** — **IMPLEMENTED 2026-07-16**: fourth lifelist set (300-code cap),
   fed from route endpoints at both enrichment apply points (cloud + adsbdb), shown on
   the Stats LIFELIST block.
9. **Logbook export / web view** — **IMPLEMENTED 2026-07-16**: `GET /logbook.json` on the
   config server (full lifelist: types with first-seen dates + counts, airlines,
   countries, airports, records; ISO dates), read straight from NVS so it's async-task
   safe (≤1 debounce interval stale), linked from the config page's logbook section.

## Tier 3 — Bigger differentiators

10. **Airport overlay on the radar** — **IMPLEMENTED 2026-07-16** (baked ~260-entry
    major-airport table in `include/Airports.h`, dim markers + IATA codes under the
    aircraft layer, `airports` display toggle default on). **Long-tail follow-up also
    SHIPPED 2026-07-16**: `GET /v1/airports?lat&lon&r` serves the full OurAirports
    dataset (public domain; 48k airports pre-tiled into KV by `npm run
    ingest:airports`, priority-sorted L>M>S, capped at 60) and cloud devices fetch it
    once the location is known (daily refresh, `FetchKind::Airports` on the shared
    fetch task). While loaded it supersedes the baked table; small fields hide at wide
    zooms so the face never clutters. The baked majors stay as the BYO/offline fallback.
    **Follow-up (user request 2026-07-17):** an `airports-min` config select — **All /
    Medium+large / Large only** — filtering device-side on the `kind` field already on
    the wire (no proxy change). Driver: a busy-GA area shows ~20 small strips when the
    user only cares about the 2 with scheduled service; the zoom rule alone doesn't
    express that preference.
11. **Config apply without restart** — **ALREADY SHIPPED** (verified 2026-07-16): every
    web save raises `ConsumeConfigChanged` and `main.cpp` re-runs `Initialise()` live on
    the loop task, no reboot. The roadmap entry came from a stale header comment, now fixed.
12. **Receiver-health on the Stats screen** — **IMPLEMENTED 2026-07-16**: a FEED block
    (source, honest data age incl. server lag, STALE flag, poll cadence, hard-fail
    count), space-guarded so the small panel never collides. Makes a quietly failing
    feed diagnosable from the device.
13. **Night clock mode** — **IMPLEMENTED 2026-07-16** (config `night-clock`, default off):
    at solar night with an empty sky, the radar face becomes a big seven-segment clock;
    any traffic instantly restores the radar. The EAM seven-segment renderer was promoted
    to shared code (`include/SevenSegment.h` + `src/SevenSegment.cpp`, namespace
    `sevenseg`, EAM-compat shim).

## Proxy-side (no firmware change; already accepted in proxy/README.md)

- **Populate the stock-photo library** — **58 TYPES LIVE ON STAGING 2026-07-16**: the
  `npm run harvest` tool (Commons extmetadata → license-gated manifest rows) shipped
  three batches same-day — Tier-1 military, military batch 2 (Apache/Texan II/Chinook/
  C-130H/T-38/Osprey/P-8/Super Hornet; ~65% of the mil fleet photo-covered), and the
  civil long tail (737 + A320neo families, E-Jets, CRJs, Q400, widebodies, GA/bizjet/
  helos). Remaining: further long tail by proxy-log traffic, production ingest at launch.
- **Military enrichment deepening** — **P0 + P1 SHIPPED 2026-07-16** (empty-meta
  negative TTL; mil-block + dbFlags operator floor at serve time, `proxy/src/military.ts`);
  **deployed to staging + smoke-tested 2026-07-16**. **P2 static airframe dataset
  SHIPPED 2026-07-16** (license review passed: Mictronics/aircraft-database, ODC-By 1.0;
  `mil:<hex>` KV side table + `npm run ingest:mildb`, ~17.3k typed military airframes —
  type resolution also unlocks the existing military stock photos). Remaining: P3
  callsign-prefix fill.

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

### Scoring design (settled 2026-07-16)

**Scoring is passive and derives entirely from the Logbook — never from user actions.**
Explicitly rejected: points for opening detail cards or any other interaction (instantly
grindable, and cloud devices background-enrich automatically anyway). You score by what
flies over your house and what your device logs; the only way to score more is to spot
more. That's the game.

**Spotter Score** (lifetime) = weighted lifelist:

| Category | Points | Why the weight |
|---|---|---|
| unique type | 10 × rarity | the core collectible |
| unique airline | 5 | secondary collectible |
| unique country | 25 | genuinely hard to grow |
| unique airport (route endpoints) | 2 | easy to grow, small spice |
| raw contacts | 0 | pure uptime/density — never scores |

**Rarity multiplier** (types only, computed server-side each season): a type seen by
<5% of opted-in devices scores ×5, <25% ×2, else ×1. This is the equalizer — a device
in rural Oregon can't out-volume one under the O'Hare approach, but a crop duster,
a warbird, or a C-17 on a odd routing is worth real points anywhere. Weights are
server-side only, tunable without firmware changes.

**Season score** (monthly): same formula over entries **new to your lifelist this
month**. Everyone restarts at 0 monthly — newcomers can win a season in their first
month while lifetime boards stay the long game.

**Radius fairness (settled 2026-07-17):** the configured radar radius would otherwise
be a score multiplier (bigger circle, more contacts). Fix: **a standardized scoring
radius (~50 km), applied in the background** — never touching the user's display
radius, which is a viewing preference (a small circle is exactly right for "what can
I walk outside and see"). In cloud mode an opted-in device polls `/v1/blips` at
`max(userRadius, SCORING_R)`; contacts beyond the user's radius are **scoring-only**
— the logbook counts them but they never appear on the radar or list screens. Devices
with a radius already ≥ 50 km score only from the inner 50 km. Side benefit: every
verified competitor observes the same-sized sky, which makes the server's
plausibility check sharper. BYO/local devices can't be normalized (their receiver is
their radius) — one more reason they compete as "unverified".

**Badges** (the "interesting for everyone" layer — non-competitive, derived
server-side from the same submission): Century Club (100 types), Widebody Collector,
Warbird Spotter (10 mil types), Globetrotter (15 countries), Streak (30 consecutive
submission days with a new entry), First! (first device in the fleet ever to log a
type — permanent credit on the type). Badge case shows on the public profile.

### Surfaces

- **Config page:** one new section — opt-in checkbox (default OFF) + "Spotter name"
  field (claim-on-first-submit; server suffixes collisions) + a link to the public
  board and to the privacy paragraph.
- **On the scope:** a compact LEADERBOARD block on the existing Stats screen (no new
  screen): `RANK #42 · 1,240 PTS · SEASON #17 ↑3`, clock-guarded like the other Stats
  blocks. Optionally later: a one-shot toast when rank improves.
- **Public web:** `GET /leaderboard` (like `/credits`, unauthenticated HTML + JSON):
  season board default, lifetime tab, per-category leaders (most types / airlines /
  countries / airports), badges + verified check on each row; `/leaderboard/<id>`
  profile with the badge case. Shows **percentile framing** ("top 12%") so mid-board
  devices see progress, not just distance from #1.

### Wire + storage

Hourly `POST /v1/leaderboard` (same auth/rate limits): `{id, name, counts{types,
airlines, countries, airports}, typeCodes[]}`. `id` = salted hash of MAC (raw MAC
never leaves the device). **`typeCodes` (the ICAO type list, ~1 KB) is the one list
sent** — needed for rarity scoring, verification, and badges; airports/airlines/
countries stay counts-only because those lists fingerprint the user's location.
Never position, never per-aircraft sightings, never timestamps of sightings. The
README privacy section documents exactly this before launch. Storage: KV rows
(`lb:dev:<id>`) + a cron-triggered Worker aggregating top-N boards into `lb:board`
(D1 only if/when the fleet outgrows that).

### Anti-cheat (v1: honor system + caps)

Counts must be monotonic; growth-rate caps (a real sky produces < ~40 new types/day);
type codes validated against the known-designator set; **verified tier** for
cloud-feed devices (the Worker can sanity-check claimed type growth against traffic it
actually served that device); outliers shadow-flagged for review, not auto-banned.
Per-device keys remain the real fix and the leaderboard is what eventually justifies
them.

### Sequencing

Logbook v2 ✅ (shipped 2026-07-16) unblocked this. Build order:
1. **Worker:** submit endpoint + KV store + public board page + season cron (staging).
2. **Firmware:** config opt-in + name field; hourly submit from the enrich/ntfy task
   (one queued POST, off-loop like every alert).
3. **Season mechanics + rarity weights** — server-side only, no firmware change.
4. **Badges + profiles** — server-side only.
5. Later: per-device keys, real accounts for multi-device merge.
