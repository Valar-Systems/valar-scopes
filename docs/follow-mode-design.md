# Follow mode — design note

**Status: sketch. Nothing here is built, and nothing here should be built yet.**
Blipscope has not launched: the store is draft, the print order is pending, v8 is
in the release train and the fragmentation issue is open. Firmware for this queues
behind the pilot shipping. This exists so the reasoning is captured while it is
fresh, and so the measurements that decide the shape of the feature are written
down before anyone is tempted to skip them.

Written 2026-08-24.

---

## 1. What it is, and the moment that sells it

Two requests, from two people:

- an airline pilot who wants his wife to see him travel around the country;
- a student pilot whose family wants to watch his lesson from home.

These look like two features. They are one feature in two range regimes, and the
seam between them is the whole demo.

**In range it is a radar. Out of range it becomes a compass.**

A student's first solo cross-country crawls to the edge of the scope, drops off
it — and the screen turns into an arrow pointing at him, ninety miles out, with
the distance counting up and then, eventually, down. Nothing else on the desk
does that. It is the most compelling thing in this design and everything else
should be arranged so that it survives.

That transition is also why this is not two products. One config field names an
aircraft; the device decides how to draw it based on whether it can currently see
it. The airline case is the student case with the aircraft permanently past the
edge.

**Stage 1, which is what this document specifies, is the in-range half only.** It
is fully useful alone — it completely serves the student — and it needs no new
data path, no new upstream and no Worker change. Stage 2 (the compass) is blocked
on the licensing questions in §10 and should not be designed around until they
are answered.

---

## 2. Why the existing trail cannot be reused

`TrackedAircraft` already has a trail, and it is a good one. It is also the wrong
object for this, in three independent ways.

`src/models/TrackedAircraft.h` holds 60 points, age-capped at 90 seconds, with a
minimum interval tuned so every feed cadence produces the same 89-second span.
That tuning is deliberate and correct: it is a **motion cue**, whose job is to
show which way a blip is going.

A flight track is a different object:

| | existing trail | follow track |
|---|---|---|
| purpose | which way is it going | what did he fly today |
| span | 90 s, fixed | a whole lesson (1–2 h) |
| sampling | time-based, cadence-normalised | distance-based (§3.1) |
| lifetime | dies with the contact | must outlive dropouts |
| owner | `TrackedAircraft` | `AircraftManager` |

The third row forces the decision. `AircraftManager.cpp:2249` evicts a contact
absent past a grace window of roughly 30 seconds and rebuilds it from scratch on
reappearance, discarding the trail. For a trainer at 1,000 ft AGL — whose
coverage is line-of-sight to ground receivers and therefore intermittent by
nature — that eviction is not an edge case. It is the normal operating condition.

So the track is owned by the manager, keyed by resolved ICAO hex, and survives the
contact table entirely.

---

## 3. The track: sampling, memory, and allocation discipline

### 3.1 Decimate by distance, not time

The existing trail's three rules (dedupe / min-interval / age-expiry) exist to
hold *duration* constant across feed cadences. Here duration is the thing we want
to vary — a flight is as long as it is — and what we want to hold constant is
visual fidelity per pixel.

    append the fix if   great_circle(fix, lastKept) >= TRACK_MIN_SEP_M

`TRACK_MIN_SEP_M` starts at **150 m**. At the tightest zoom a pixel is a few
hundred metres, so anything closer is drawing on top of itself. The rule has a
pleasant property: straight cruise legs compress to almost nothing while turns,
circuits and manoeuvres keep their full shape — because shape is exactly where
consecutive fixes differ in *direction* rather than distance.

It also self-bounds without a timer. 1024 points at 150 m minimum separation is
about 150 km of flown path, comfortably a whole lesson.

### 3.2 What it costs, at worst case

    struct TrackPoint { float lat; float lon; uint16_t sec; };   // 10 B, pads to 12
    TRACK_CAPACITY = 1024
                                                  1024 x 12 = 12,288 B

**12 KB, and that is the worst case rather than the typical case** — it is the
size of the buffer whether or not it is full, which is the point of §3.3.

### 3.3 ONE allocation, at follow-enable, never grown

This device has an **open, unexplained fragmentation problem**: roughly 24 KB of
erosion over 11 hours, with the handshake #4 anomaly still the only cause-shaped
lead. Adding allocation behaviour to this codebase on the strength of
first-principles reasoning would be exactly the wrong lesson to draw from that
investigation.

So the discipline is specified here rather than left to implementation:

- **One** `heap_caps_malloc(TRACK_CAPACITY * sizeof(TrackPoint), MALLOC_CAP_SPIRAM)`
  at the moment follow is enabled.
- **Never** reallocated. The buffer is a fixed-size ring from birth; it wraps
  rather than grows.
- **Freed only** when follow is disabled — not on landing, not on new-flight, not
  on eviction. A new flight resets the write index; it does not touch the
  allocation.
- **If the allocation fails, the feature degrades to notification-only and says so
  on screen.** It must not retry in the loop. A periodic retry of a large
  allocation under fragmentation is itself a fragmentation source, and it turns a
  clean degradation into a slow decay.

The distinction that matters, stated plainly because it is the entire reason this
section exists: **a track that grows is a fragmentation source; a track allocated
once and reused is not.** The first is a stream of differently-sized blocks
interleaved with TLS handshakes. The second is one block that either exists or
does not.

### 3.4 Verify it, do not assume it

"PSRAM is invisible" is an assumption. It is a well-supported one — the
2026-08-09 measurement showed a 240x240 backbuffer plus two photo sprites moving
`psram_free` by 73,532 B and leaving the internal heap untouched — but it is still
an assumption, and this codebase has an open issue that consists precisely of
memory behaving in a way nobody predicted.

The instrumentation to check it against already exists, from the #250 work:

- `psram_free` — should drop by ~12 KB at follow-enable and then stay flat
- `tlsmem=psram/internal/fallback` — the fallback counter is the tell. If the
  PSRAM allocation silently lands on the internal heap, this is where it shows.
- `largest` / `allocFail` — must be unchanged across a multi-hour follow soak

**Acceptance criterion: a follow-enabled board and a control board, soaked side by
side, must show the same internal-heap trajectory.** Same A/B shape as the current
TLS soak, and cheap to run once boards are free.

### 3.5 Drawing

Points are stored as lat/lon, like the existing trail, so reprojection under pan,
zoom and `radar-up` rotation (`AircraftManager.cpp:3226`) comes free.

Two rendering decisions:

- Draw the track **beneath** everything else, in one dim distinct colour.
- **Exempt it from the sweep's phosphor fade.** A radar return decays because it
  is a *return*; the track is a *record*. Holding it steady while returns pulse
  around it is not decoration — it tells you at a glance which marks are live and
  which are history.

**The draw cost is the number that decides what this feature is.** The frame
budget note at `AircraftManager.cpp:1531` records 27.5–31.1 ms under full load
with overlay and trails. Projecting and drawing up to 1024 extra segments per
frame could blow that outright. Mitigation is a draw-time cap of ~256 segments
with adaptive stride.

If that number is bad, **this is a notification product rather than a track
product** — a very different and much smaller feature, and one worth knowing about
before building the wrong one. Measure it first; see §12.

---

## 4. The state machine

### 4.1 Field elevation — CORRECTED 2026-08-25

> **This section previously said the opposite, and the workaround it invented is
> deleted.** The original text read: *"`include/Airports.h` is about 250 major
> internationals, IATA codes, and carries no elevation… So 'descended to field
> elevation at KPAE' is not implementable as specified. The device learns the
> field instead of looking it up… after one or two flights there is a home
> position and a ground altitude, self-calibrated."*
>
> That premise was false. It was true of `include/Airports.h`, and I generalised
> from the only airport table I had looked at to airport data in general.

**We have field elevation for 34,128 airports.** The `airports.csv` in the
CC0 `vradarserver/standing-data` set carries:

```
Code,Name,ICAO,IATA,Location,CountryISO2,Latitude,Longitude,AltitudeFeet
4CA0,Lapd Hooper Heliport,4CA0,,Los Angeles,US,34.043301,-118.247002,302
```

34,128 rows against the 250 hand-curated entries in `include/Airports.h`, and
`AltitudeFeet` on every one. A flight school's field is in it. So is the
heliport above.

**Consequences, which are all deletions:**

- The self-calibrating learn-over-two-flights mechanism is **removed**. It
  existed only to synthesise a number we can now look up.
- Landed detection gets a **real threshold** — `geoAltitude` against the field's
  published elevation — instead of a value the device had to earn over its first
  two flights and could get wrong on either.
- The **first** flight is detected as well as the tenth. The old design was
  silently degraded until it had calibrated, which is exactly the period a new
  owner is watching most closely.
- "Airport not plottable" disappears as a state.

Available per fix, unchanged: `onGround`, `baroAltitude`, `geoAltitude`,
`velocity`, `trueTrack`, `verticalRate`, `positionSource`.

**What still needs deciding, and is NOT urgent** (Follow does not start until a
board frees up): §4.3 and §4.4 below still speak of a "learned home field" and a
"learned home radius". Those are now a *lookup* rather than a calibration, and
the state machine gets simpler rather than different — the threshold arrives at
boot instead of after two flights. That pass happens when Follow starts; the
false premise is corrected here so nothing is built from it in the meantime.

**Where the elevation comes from at runtime.** 2.38 MiB cannot ship on the
device, so this rides the existing `/api/v1/blipscope/airports` overlay endpoint
(already server-capped and priority-sorted) out of the D1 mirror — see §10. That
makes Follow's landed detection depend on the mirror existing, which is a real
new dependency and is why the local/flight-school regime that needs no geography
at all is worth shipping first.

### 4.2 Absence is three states, not one

This is the core of the design and the thing most likely to be got wrong.

A naive state machine has one absence state called something like `LOST`, treated
as an error condition. That is wrong here for a reason about people rather than
code: **absence is the normal operating condition of this feature**, and the three
kinds of absence mean completely different things to the person watching.

| state | means | expected? |
|---|---|---|
| `NO_COVERAGE` | he is somewhere ground receivers do not reach | **yes** |
| `SIGNAL_LOST` | he should be visible and is not | no |
| `APPROACH_LOST` | last seen descending toward the learned home field | yes, usually |

The device can tell these apart. `NO_COVERAGE` is a position argument — over
ocean, or far from anywhere with receivers. `APPROACH_LOST` is a profile argument
— descending, slowing, inside the learned home radius. `SIGNAL_LOST` is what is
left over.

Collapsing them into one state is what makes this feature frightening instead of
reassuring, and no amount of good copy rescues a state machine that cannot tell
expected absence from unexpected absence.

### 4.3 States and transitions

| state | entered when | leaves to |
|---|---|---|
| `IDLE` | no follow target set | — |
| `WAITING` | target set, nothing seen yet | `GROUND`, `AIRBORNE` |
| `GROUND` | fixes arriving, `onGround` true | `AIRBORNE` |
| `AIRBORNE` | `AIRBORNE_CONFIRM_FIXES` consecutive airborne fixes | `LANDED`, the three absence states |
| `NO_COVERAGE` | absent, last position in a known-gap region | `AIRBORNE` |
| `SIGNAL_LOST` | absent > `TRACK_LOST_MS`, should have been visible | `AIRBORNE`, `LANDED` |
| `APPROACH_LOST` | absent, last fixes descending toward learned home | `AIRBORNE`, `LANDED` |
| `LANDED` | `onGround` sustained, or low+slow inside learned home radius | `AIRBORNE` (new flight) |

Constants, all of which are **guesses until a real flight is logged** (§12):

- `AIRBORNE_CONFIRM_FIXES` (2–3) — so one spurious fix cannot announce a takeoff
- `LANDED_CONFIRM_MS`
- `TRACK_LOST_MS` (start ~3 min)
- `NEW_FLIGHT_GAP_MS` — after which a resumed track is a new flight, not the same one

### 4.4 The rail

**A lost signal must never be reported as a landing.** Getting *"he's down
safely"* when the truth is *"we stopped hearing him"* is the worst thing this
product could do, and it is the failure mode the whole state machine is arranged
to prevent.

`LANDED` therefore fires only on confident evidence: sustained `onGround`, or
low-and-slow inside the learned home radius. Everything else routes to one of the
three absence states and is worded as such.

---

## 5. The copy

**This is the product, not the rendering.** A hobbyist radar with a coverage gap
is a shrug. A device someone's spouse is watching that goes dark mid-Atlantic is
frightening, and "we designed the gap in" is only true if the words on the screen
are right. So the strings are specified here, before anything is built.

Three principles they all follow:

1. **Name the mechanism.** "No receivers here" is calming because it explains.
   "Signal lost" alone is not.
2. **Never imply certainty we do not have.** No landing claim without evidence.
3. **Different words for different states.** If `NO_COVERAGE` and `SIGNAL_LOST`
   read the same, the state machine's work is wasted.

### Airborne

    ntfy   title  N4523K is airborne
           body   Off at 14:06.

    screen        AIRBORNE
                  14:06  ·  2,400 ft  ·  climbing

### Landed — confident

    ntfy   title  N4523K is down
           body   Landed 15:18. 1 h 12 m in the air.

    screen        ON THE GROUND
                  Landed 15:18  ·  1 h 12 m

### Approach lost — the probably-landed case

The case that would otherwise produce the silence someone worries into. Hedged
deliberately: it says what was seen, says what that usually means, and does not
claim to know.

    ntfy   title  N4523K — last seen on approach
           body   Signal lost at 1,200 ft over the field, 15:16.
                  Coverage near the ground is patchy, so this usually
                  means landed.

    screen        ON APPROACH — SIGNAL LOST
                  Last seen 15:16  ·  1,200 ft  ·  over the field
                  Low-level coverage is patchy. This usually means landed.

### Signal lost — unexpected

Screen only by default (§9). No phone alert unless asked for.

    screen        SIGNAL LOST
                  Last seen 14:52  ·  4,500 ft  ·  12 mi NE
                  He is out of receiver range, not off the radar.

That last line is doing real work. It converts an alarming absence into a
statement about *our* equipment rather than about him.

### No coverage — expected absence (stage 2)

The oceanic case. The best version **pre-empts** the gap rather than explaining it
afterwards, which requires route data — see §10.

    With a known route:

    screen        OVER THE ATLANTIC
                  No ground receivers out here — this is expected.
                  Next contact expected around 18:40, near Ireland.

    Without a route:

    screen        NO COVERAGE
                  Ground receivers do not reach where he is now.
                  He will reappear on the far side.

Note the deliberate absence of the word "lost" in both. It is a different state
and it gets different words, because it is a different thing.

---

## 6. Follow and the collection game

The insert card is being printed around collection. Follow is the opposite
emotional product — breadth and novelty versus one specific airplane — and they
share one screen. Three decisions, made here rather than left implied.

### 6.1 Does the followed aircraft count toward collection?

**Yes, with no special-casing at all.**

The existing model already handles it. Claiming is per *type*, once (`Logbook`'s
`claimDay`), so a followed C172 contributes exactly one type claim ever, however
many times it flies. The odometer and per-type sighting count tick each session,
which is honest.

One wrinkle: a trainer flying overhead daily will likely own the "closest contact
ever" record permanently. Left alone deliberately — a lifetime record set by your
brother's airplane is charming, not broken.

The reasoning for no special-casing is about explainability rather than
implementation cost: **the insert card has to state the collection rules in about
one sentence**, and two mechanics that quietly modify each other cannot be
described that briefly.

### 6.2 Does an active follow suppress or reorder other alerts?

**Reorder, never suppress.**

`ProcessAlerts` currently runs emergency > overhead > watchlist/military, one per
tick. Follow events slot in as their own class:

    emergency  >  follow  >  overhead  >  watchlist / military

Follow outranks everything except emergency because it is rare (twice a flight)
and it is the thing the owner asked for **by name**. Emergency still outranks it
because that is a safety event about somebody else's aeroplane.

Nothing is suppressed. Suppressing a customer's other alerts because they also
turned this on would be a silent degradation of a feature they explicitly enabled
— the same class of defect as the early return in `ProcessAlerts` that made the
ntfy topic look broken.

### 6.3 Who wins the screen?

**Follow gets a screen. It never gets *the* screen.**

The radar is the collection surface and the printed card describes it. A follow
that took over the radar would break the product the card is selling. So:

- Follow is a **fourth screen** (Radar / List / Stats / Follow).
- It is **hidden entirely when no follow target is set**, the way the other
  editions skip empty feeds. A collection customer who never uses this must not
  inherit a dead screen.
- It auto-surfaces **only on a state transition** — takeoff, landing — for a dwell
  period, then returns to wherever the owner was. Same pattern as the editions'
  dwell rotation.
- While the followed aircraft is in range it gets a **distinct ring on the radar**,
  so it is findable without leaving the collection view.

So when the followed aircraft is airborne and something rare flies over: the rare
aircraft keeps its NEW highlight, the followed aircraft keeps its ring, and
neither steals the display from the other. The only thing that ever takes the
screen is a follow *transition*, which happens twice a flight.

---

## 7. The post-flight card

The answer to "the screen is empty most of the week". On `LANDED`, freeze a
summary and show it until the next takeoff: duration, max altitude, top speed,
furthest point — and the shape of the flight.

### Where the shape lives

The live track buffer is reset on the new-flight transition, so the card cannot
read from it. Two options, and this note picks one rather than leaving it implied:

**Chosen: persist a 128-point decimated copy to NVS**, own namespace `follow-log`,
mirroring `Logbook`'s discipline — bounded store, debounced write, one write per
flight.

    128 points x 8 B (lat/lon as scaled int32) = 1,024 B

The alternative was to drop shape and show four numbers. Rejected because the
shape *is* the emotional payload: a racetrack of circuits is the picture that says
"he practised landings today" without a word of text. Four numbers is a readout;
the shape is a souvenir.

128 points is enough to read a circuit pattern at card size, and small enough to
sit inside the existing NVS budget without competing with the config namespace.

**Circuit counting is deferred.** It is the most charming number in the feature
and the one most likely to be wrong on first contact with real data. Build the
state machine so the altitude history exists, then look at an actual logged lesson
before deciding what a circuit is.

---

## 8. Config surface

A new `<details>` block of its own, not crammed into Watchlist & alerts.

| key | type | purpose |
|---|---|---|
| `follow` | text — tail / callsign / hex | the aircraft to follow |
| `follow-track` | toggle | draw the long track |
| `follow-up` | toggle | ntfy on airborne |
| `follow-down` | toggle | ntfy on landed |
| `follow-lost` | toggle | ntfy on `SIGNAL_LOST` |

Matching reuses the `WatchClass::Specific` path in `MatchesWatchlist` — identity
already outranks type there, which is the semantics we want.

### 8.1 The ntfy topic becomes device-generated

Follow changes what an ntfy topic *is*. A topic carrying "military flyover" is a
hobby feed. A topic carrying **"N4523K is airborne"** is a named person's
movements, published to a service where anyone who guesses the topic can read it.

A user-typed topic will be short and guessable, because that is what people type.

So: **the device generates the topic, and the field ships pre-filled rather than
blank.**

- Generated at first boot from `esp_random()` — the hardware RNG, explicitly
  **not** anything seeded from `millis()`. A fleet of devices booting through a
  similar sequence would produce a correlated, guessable set of topics, which is
  precisely the failure this is meant to prevent.
- Format `blip-<10 chars base32>`, roughly 50 bits. Nobody but the owner ever
  types it.
- **Persisted at generation**, not at first save, so the page shows a stable value
  across reloads.
- The owner can still overwrite it — some people legitimately want one shared topic
  across devices — but the default is generated.
- A "regenerate" affordance beside it, since the honest advice on a leaked topic is
  to change it.
- The warning is **inline, next to the field**, not only on the support page. The
  mistake is made at the config page, so that is where the warning belongs.

---

## 9. Defaults, and why each one

New keys freeze the moment anyone saves the form, and setting a location is a
whole-form save that every device must do. So today's defaults are the defaults for
everyone who ever owns this, and changing one later costs a `cfg-rev` bump plus a
migration in `include/ConfigMigration.h`. Cheap now, expensive in a month.

| key | default | why |
|---|---|---|
| `follow` | empty | Gates everything. Empty means the Follow screen is hidden and no new behaviour reaches anyone who did not ask for it. |
| `follow-track` | **on**, conditionally | If you have named an aircraft, the track is the reason to look at the device; a follow without it is a notification you had to find a second toggle to upgrade. **Conditional on the §3.5 draw-cost measurement** — if the track is expensive this becomes "on while the Follow screen is visible", which is a behaviour change rather than a default change. |
| `follow-up` | on | Cheap emotionally, and it sets the expectation that makes the landing message legible. A landing alert with no takeoff alert arrives without context. |
| `follow-down` | on | The message the entire feature exists to send. |
| `follow-lost` | **off** | The asymmetry is the argument. A missed lost-alert costs mild worry; an unwanted one costs panic. The screen always shows the state; the phone only if asked. |

`follow-up` and `follow-down` defaulting on is only reasonable *because* consent
here is explicit and specific: someone typed an aircraft identifier into a field
labelled Follow. That is not an inferred preference.

---

## 10. Stage 2 is blocked on licensing — and part of it is already live

Stage 2 wants route data: origin, destination, and ideally a predicted
reappearance point for the oceanic copy in §5. Before designing around that
source it gets the same written diligence adsb.fi and adsb.lol got, because this
is a commercial product.

### What is actually true today

More complicated than "we use adsbdb":

- **The device, on BYO builds**, calls `api.adsbdb.com` directly for aircraft
  metadata and routes (`AircraftManager.cpp:338`, `:386`).
- **The Worker** calls adsbdb as a **route and metadata fallback**
  (`proxy/src/upstreams/adsbdb.ts`) behind `ROUTE_ADSBDB_ENABLED`, which
  **defaults on** — only the literal string `"false"` disables it. The production
  environment sets it explicitly to `"true"`, confirmed in the binding list
  printed by the 2026-08-24 deploy, so this is on deliberately rather than by
  omission.
- The primary route source is adsb.lol's routeset API
  (`proxy/src/upstreams/adsb_lol.ts:87`), inside the ecosystem we already pay for.
- `proxy/README.md:194` already records adsbdb as "legally shaky for a commercial
  product" — though about photo hotlinking specifically, not routes.
- `proxy/README.md:185` records the decision to leave adsbdb precisely to stay
  "inside one licensed ecosystem". It then came back as a route fallback.

### The finding, which is not really about Follow

**The Worker is redistributing adsbdb-sourced route and metadata to customer
devices today, commercially, with no recorded written permission** — while both
other upstreams in the same chain got exactly that diligence. Follow does not
create this. It makes it more load-bearing.

I attempted to read adsbdb's terms directly and could not: the site returned no
readable terms content and the search results were generic. **So this note does not
contain an answer and must not be read as one.** The honest state is that the
question is open and has not been asked.

### RESOLVED 2026-08-25 — we own the data instead of asking for it

The question is retired rather than answered, which is a better outcome than the
answer would have been.

**Why "prefer routeset" was a dead end.** adsb.lol's `/api/0/routeset` has
returned `201 Created` with an empty body since 2026-07-08 — seven weeks — and
it is not a contract change. Their live OpenAPI (at `/api/openapi.json`; the
conventional `/openapi.json` is a 404) still documents the endpoint,
undeprecated, with exactly the request shape we send and a documented 200. The
diagnosis is that a **deliberately invalid body also returns 201/empty** where
the documented behaviour is 422 — so nothing is parsing requests and the
application is not being reached at all. It is broken upstream, silently, and we
are a paying sponsor who is not contacting them again about it.

**The route data does not need their API.** It is published as static files
built from `vradarserver/standing-data`, which carries **CC0 1.0 Universal** —
verified by reading `LICENSE` (7,048 B), which names *"a database"* and
*"including without limitation commercial purposes"* explicitly. Public domain
dedication, no permission needed, no rate limit to negotiate.

**The plan, approved 2026-08-25:**

1. **Build the CSVs ourselves from the sharded CC0 source**
   (`routes/schema-01/[A-Z]/…`), NOT from `vrs-standing-data.adsb.lol`. That
   combined CSV is a build artifact of the same operation whose API died
   silently for seven weeks; depending on it reintroduces exactly the dependency
   being removed, and it would fail the same quiet way.
2. **Mirror into D1** — 619,103 routes, 34,128 airports. Daily cron on
   `Last-Modified`, staged load into `routes_new`, atomic swap. A failed refresh
   never touches the live table.
3. **Freshness AND a row-count band** on `/healthz` and in smoke-prod. Age alone
   would repeat the routeset failure: a successful fetch of a truncated file
   refreshes the timestamp while destroying the data. Plus a canary callsign
   through live `/v1/enrich` — prove presence is observable before believing an
   absence.
4. **adsbdb leaves the stack entirely**, both halves — the Worker fallback *and*
   the firmware's direct calls at `AircraftManager.cpp:338` and `:386`. Those
   never appeared in the measured 1,170/day because they originate from customer
   home IPs, so the exposure was understated. BYO is about **positions** — a
   local receiver, one-second updates, a radar that survives an internet outage.
   It was never about enrichment. Those two call sites point at our Worker, and
   cards degrade during an outage while the radar keeps working, which is
   already exactly how photos behave.
5. Cite CC0 upstream as the licence basis and keep the `/credits` entry even
   though CC0 requires no attribution.

---

## 11. The privacy invariant needs a test, not a discipline

**The follow target must never leave the device.** A tail number tied to a named
person is a different class of data from a type code, and it must not appear in:

- the leaderboard submission
- the enrollment payload
- any feed or enrich request
- OTA / operational telemetry headers
- serial output (the Wi-Fi password incident is the precedent)

A rule that lives only in a comment gets violated in six months by someone who does
not know it exists. So it gets a check.

**Strong form — a host test over extracted pure builders.** The payload builders
currently live inside `AircraftManager.cpp` and need Arduino, so they are not
host-testable as written. Extracting each into a pure function that takes its
inputs and returns a string is a prerequisite — and is independently good design,
which is why it is the recommended path rather than a tax. The test then sets a
distinctive follow value and asserts it appears in none of the produced payloads.

**Weak form — a grep guard in CI**, asserting the follow field name appears in no
translation unit that builds an outbound body. Cheap, and catches the careless
case. It is a **weaker check and must be labelled as one**: it tests the source
rather than the artifact, which is the exact substitution this repo has been bitten
by before.

Recommendation: build the strong form. If the extraction turns out to be large,
land the grep guard first so the window is covered, and say plainly in the PR that
it is the weaker check.

---

## 12. What to measure, and when

Not this week. v8 is in the release train and COM119/COM16 are mid-soak on the TLS
PSRAM A/B. **No board comes off a soak for a feature that is not launching.**

When hardware frees up, in this order:

1. **Draw cost of a 256-segment track** against the 27–31 ms budget, on the s3-128,
   under a full contact table. This decides whether Follow is a track product or a
   notification product. Everything else is contingent on it.
2. **A follow-enabled vs control heap soak**, read against `psram_free`, the
   `tlsmem` fallback count, `largest` and `allocFail` (§3.4). Confirms the
   allocation discipline behaves as specified rather than as reasoned.
3. **Log one real training flight** through the existing feed and look at the
   dropout pattern before fixing `TRACK_LOST_MS`, `AIRBORNE_CONFIRM_FIXES` and the
   home-radius threshold. Every constant in §4.3 is a guess until that exists, and
   guessing them from first principles is how you get a state machine that is
   elegant and wrong.

---

## 13. Open questions

- Does the learned-home-field approach survive a student who trains at two fields?
  Probably wants a small set rather than a single point.
- What happens when the followed tail is sold, or the school reassigns the
  aircraft? The device would happily follow a stranger. Worth a "this aircraft has
  not flown in N days" nudge rather than silence.
- Stage 2 only: how is today's flight number set for an airline pilot, whose
  callsign changes daily? The charming answer is a link he opens from his phone
  before pushback, hanging off the enrollment identity — but that is stage 3 and
  depends on stage 2 existing.
- Does `follow` want to accept more than one aircraft? Two siblings both learning,
  a family with two pilots. The data structures do not care; the screen does.
