# Follow Mode — consolidated design and visual specification

**Status: sketch. Nothing here is built, and nothing here should be built yet.**
Blipscope has not launched: the store is draft, the print order is pending, v8 is in
the release train, and the fragmentation issue (#245) is open. Firmware for this queues
behind the pilot shipping.

## 0. Provenance and precedence

This document merges two independently written specifications that described the same
feature without referencing each other:

| source | branch / path | lines | covers |
|---|---|---|---|
| `docs/follow-mode-design.md` | `docs/follow-mode-design` @ `7020595` | 683 | behaviour, state, memory, lifecycle, copy, licensing |
| `follow-mode-visual-spec.md` | delivered as a file, never committed | 470 | geometry, faces, scale, gesture, on-device selection, local regime |

Neither supersedes the other. **Behaviour, memory and lifecycle come from the design
note; layout, scale and interaction come from the visual spec.** Where they genuinely
disagreed, §2 states both positions rather than silently picking one — a contradiction
between two specs is a finding, not an inconvenience.

Confidence markers are preserved from both sources and are load-bearing:

- **[MEASURED]** — read out of this repo or from a bench capture
- **[PROPOSAL]** — a design decision open to argument
- **[UNKNOWN]** — could not be determined; establish before building on it
- **CORRECTED / RESOLVED** — a previously stated premise that turned out false, kept
  visible rather than quietly edited, so nothing is built from the old version

---

## 1. What it is, and the moment that sells it

Two requests, from two people:

- an airline pilot who wants his wife to see him travel around the country;
- a student pilot whose family wants to watch his lesson from home.

These look like two features. They are one feature in two range regimes, and the seam
between them is the whole demo.

**In range it is a radar. Out of range it becomes a compass.**

A student's first solo cross-country crawls to the edge of the scope, drops off it —
and the screen turns into an arrow pointing at him, ninety miles out, with the distance
counting up and then, eventually, down. Nothing else on the desk does that. It is the
most compelling thing in this design and everything else should be arranged so that it
survives.

That transition is also why this is not two products. One config field names an
aircraft; the device decides how to draw it based on whether it can currently see it.
The airline case is the student case with the aircraft permanently past the edge.

### 1.1 Which regime ships first — DECIDED

Both source documents reached this independently, from different directions. Promoting
it from two suggestions to one decision:

**The local / flight-school regime ships first.**

The design note reached it from the dependency on the mirror: *"that makes Follow's
landed detection depend on the mirror existing, which is a real new dependency and is
why the local/flight-school regime that needs no geography at all is worth shipping
first."* The visual spec reached it by enumerating what local needs from outside the
device: no routes, no airport table, no coastline dataset, no new Worker surface, no
licence question. It runs on ADS-B positions the device already receives and the trail
buffer specified in §4.

The airline regime needs route data, an airport table replacement, and geography.
Local can ship while that work is still in flight. **The trail draw-cost measurement
(§18.1) is the only gate on it.**

---

## 2. Conflicts and corrections requiring a decision

This section is the reason the merge was worth doing. Read it before anything else.

### C1 — The mirror is KV, not D1, and it may need to be both **[UNKNOWN]**

The design note says "Mirror into D1" (§10 step 2) and describes field elevation
riding "out of the D1 mirror" (§4.1). **Both are stale.** The storage decision was
reversed on new information and the mirror is Cloudflare Workers KV. As of 2026-08-26 it is
loaded and verified in **staging AND production**: 619,103 route keys in each,
`meta:routes` reporting 1,575 shards, sentinel provenance proved, sampled keys
byte-identical, the diff path exercised at 0 changed keys, and a full per-shard
enumeration passing 1575/1575 on staging. Production carries rule rev 2 (§C5a).

This is not a simple find-and-replace, and that is the point of flagging it:

- **Routes are a key lookup.** `rt:BAW117` → `{"o":"LHR","d":"JFK"}`. Exact key, no
  query, cached at edge. KV is correct and is what shipped.
- **The airports overlay is a query.** §7.3 below requires it "server-capped and
  priority-sorted" — pick the N nearest or most significant fields to a location. KV
  cannot do that; it can only fetch by exact key. That is a D1 shape.

So the mirror may legitimately be **split across both stores**, which is a defensible
design and not the contradiction it looks like — but it has not been decided, and
Follow's landed detection depends on the answer. Decide before §5's landed threshold
is built.

> ### RECOMMENDATION 2026-08-27: `ap:<CODE>` keys in KV. No D1. **[FOR DANIEL]**
>
> **Flagged before building, not after.** Stage 2's arc and globe both need endpoint
> coordinates, so this is on the critical path — but it is a delivery question and it
> is cheaper to argue than to migrate.
>
> **The premise above is falsified by shipped code.** C1 rejects KV because "it can
> only fetch by exact key... That is a D1 shape." But `/api/v1/blipscope/airports`
> already answers exactly that nearest-N, priority-sorted query, **out of KV**, and
> has since 2026-07-16: `npm run ingest:airports` pre-tiles OurAirports into 1°
> `apt:<lat>:<lon>` keys and the handler walks the tiles the radius circle touches,
> distance-filters, sorts large > medium > small then nearest, and caps at 60.
> Pre-tiling is how a key-value store answers a spatial query, and we are already
> doing it in production.
>
> So the choice is not "key lookup vs query". It is "one store or two", and the
> reasons for one:
>
> | | |
> |---|---|
> | **What Follow needs** | `code → {lat, lon, elev}`. An exact key. The single thing KV is best at. |
> | **What §7.3 needs** | nearest-N — already solved on KV, in production, by the `apt:` tiles. |
> | **Size** | 34,128 rows. The `rt:` mirror is **619,103** keys and is loaded and verified in both environments. This is 5% of a thing we have already done twice. |
> | **Operations** | one ingest, one verifier, one failure mode. The route load taught us that the verifier is the expensive part to get right (`IGO7J` — one key in 619,103, found only by per-shard enumeration). A second store means writing that discipline a second time, or not writing it. |
> | **Refresh** | the CC0 corpus refreshes as one unit. Two stores means two things that can be half-refreshed, and a skew nobody would notice. |
>
> **Two key families, both KV, both from the same corpus:**
>
>     ap:<CODE>            -> {"lat":..,"lon":..,"alt":..}   exact lookup (Follow, C5)
>     apt:<lat>:<lon>      -> tile of nearby fields           spatial (already shipped)
>
> `ap:` also closes **C5**: `AltitudeFeet` rides the same row, so landed detection and
> the local face's AGL readout arrive with it. That is §19 item 7 done by the same
> ingest rather than as separate work.
>
> **What would change my mind:** a genuine ranked query the tiles cannot express —
> "the 20 busiest fields in this country", say. Nothing in Follow or §7.3 asks for
> one. If that arrives, D1 can be added *for that query only*, with `ap:` staying
> where it is; the reverse (starting on D1 and discovering the edge cache mattered)
> is the expensive direction.
>
> **§17 constraint, which shapes the request and is not negotiable.** The device must
> fetch `ap:` **by airport code, never by callsign** (C2's resolution). That means the
> followed aircraft's route must NOT be requested from the Worker by its callsign —
> doing so would put the follow target in an outbound payload, which is precisely
> what §17 forbids and what `scripts/check_follow_privacy.py` guards.
>
> The route arrives instead through the path that already exists: while the aircraft
> is **in contact** it is an ordinary visible contact and its route comes back with
> normal enrichment, indistinguishable from any other aircraft's. The device caches
> it for the flight. Out of contact, nothing is requested — which is also the only
> time the arc face needs it, so the ordering works out.
>
> **Residual, stated so it is a decision and not an oversight:** two `ap:` fetches for
> a specific pair of codes shortly after a follow is configured are correlatable with
> *a route*, though never with *an aircraft*. That is the same class as the airports
> overlay the device already fetches hourly, and it carries no identifier §17 lists.
> If that is judged too much, the mitigation is to fold the pair into the existing
> overlay fetch rather than to move stores.

### C2 — Callsign validation contradicts the privacy invariant **[BLOCKING]**

The sharpest conflict, and neither document could see it alone.

The design note's §11 states the invariant plainly: **the follow target must never
leave the device.** Not in the leaderboard submission, the enrollment payload, *any
feed or enrich request*, OTA telemetry, or serial output. It asks for a host test over
extracted pure builders to enforce it, because a rule living in a comment gets violated
in six months.

The visual spec's on-device selection flow requires exactly the thing that invariant
forbids. Step 3 of the picker: *"the Worker resolves the assembled callsign against
flights currently airborne or scheduled, and the device shows the resolved route before
committing."* And under new surface: *"a Worker endpoint to resolve and validate a
callsign, and to list an airline's active flights."*

That is the follow target, in an outbound request, tied to device identity. The
validation step and the invariant cannot both ship as written.

**Three ways out, none free:**

1. **Resolve on-device.** The picker assembles from a cached airline list and commits
   without validation. Keeps the invariant intact; loses "you cannot follow a flight
   that does not exist," which was the feature's whole argument for validation-before-
   commitment rather than an error afterwards.
2. **Validate anonymously.** The lookup goes out unauthenticated and uncorrelated —
   no device identity, no enrollment header, nothing that ties the callsign to this
   customer. Weakens the invariant from "never leaves" to "leaves unlinkably," which
   is a real distinction but a much harder one to test, and the host test in §17 can
   no longer be a simple string-absence assertion.
3. **Drop the on-device picker.** Follow is configured in the browser, which the
   product's own setup philosophy already points at, plus the two cheaper wins in
   §12.3 that need no entry mechanism at all. The invariant survives untouched.

Option 3 is the smallest and probably right for v1; the picker was already argued as
lower value than "Follow this one" on the card plus recents. **Decide explicitly.**

### C3 — ntfy carries the tail number by design, and that is an exception, not a violation

§17's invariant must not be read as absolute or the feature's core function looks like
a breach of it. A topic carrying **"N4523K is airborne"** is a named person's movements
published to a third-party service. That is the product working as intended.

The mitigation is §14.1's generated topic, not silence. State the exception in the
invariant itself so the test asserts the right thing: the follow target must not appear
in any payload **except the ntfy notification body**, which is the one channel the owner
explicitly opted into by naming an aircraft.

### C4 — Pre-departure has a state but no screen **[UNKNOWN]**

Both documents found this and neither finished it.

The design note's state table has `WAITING` — "target set, nothing seen yet" — with
transitions out to `GROUND` and `AIRBORNE`, and nothing about what it shows. The visual
spec found it from the other end: *"the normal entry point is a flight that has not
departed yet — which is neither `IN_CONTACT` nor any of the three loss states. It is a
fourth kind of absence: not yet."*

It matters more than its size suggests. **This is the state the owner sees first.** You
set up a follow the night before, then look at the device. If `WAITING` renders as an
empty face or, worse, as one of the loss states, the feature looks broken at exactly the
moment someone has just finished configuring it.

It needs a face: the route, the scheduled departure, a countdown, and a transition to
`IN_CONTACT` on first contact. For the local regime it needs less — the aircraft is
either at the field or it is not — but it still needs to say so.

> ### PROPOSAL 2026-08-27 — the pre-departure face. **COPY IS A DRAFT FOR DANIEL.**
>
> Designed first in stage 2 because it is a decision and decisions are cheapest to
> change. Everything below is renderable from what the device already has.
>
> **THE COUNTDOWN IS CUT, AND THAT IS THE MAIN DESIGN CALL.** C4 asks for "the
> scheduled departure, a countdown" — and we have no schedule data. Not in the CC0
> corpus (routes are `o`/`d` codes, no times), not on the wire, not anywhere we
> licence. A countdown would have to be invented, and §6 principle 2 is *never imply
> certainty we do not have*. Inventing a departure time on the one screen whose whole
> job is to say "nothing has happened yet" would be the worst possible place for it:
> the owner would watch it reach zero and conclude the device is wrong.
>
> If schedule data is ever licensed, the countdown is additive and this face has the
> slot for it. Until then it says what it knows.
>
> **What it must do, in order.** This is the state the owner sees *first*, seconds
> after configuring a follow. It must (1) prove the device understood them, (2) prove
> it is working, and (3) tell them to do nothing. An empty face fails all three, and
> rendering it as one of the loss states fails worse than empty.
>
> **Airline regime** — the arc face in its *not-started* state, not a different
> screen. The full arc dim, both airport codes drawn, no marker. It previews what
> they will see, which is the cheapest possible way to prove the device understood
> the configuration.
>
>     screen   WAITING FOR DEPARTURE
>              LHR -> JFK
>              Nothing heard yet today. This screen changes on
>              its own when he takes off.
>
> **Local regime** — the local face with rings and the home marker, no track.
>
>     screen   WAITING FOR DEPARTURE
>              at EGYD
>              Nothing heard yet today. This screen changes on
>              its own when he takes off.
>
> The third line is the load-bearing one and is the same in both. It answers the
> question the owner actually has — *"is it broken, or is he just not flying?"* — and
> it tells them there is no action to take. Same register as §6's absence copy: name
> the mechanism, and make the sentence about our equipment rather than about him.
>
> **`WAITING` must never borrow a loss state's word.** Not "no contact", not "signal",
> not "lost". Nothing has been lost; nothing has started. This is the fourth kind of
> absence the visual spec named — *not yet* — and it gets its own vocabulary.
>
> **Transition** is already built: `Machine::OnFix` moves `Waiting` → `Ground` or
> `Airborne` on the first fix, and `HandleFollowTransition` surfaces the screen on the
> `Airborne` edge. Nothing new in the machine — this is a face for a state that
> already exists, which is the §19 rule that stage 2 is a rendering layer.
>
> **THE STALE-FOLLOW QUESTION, which C4 does not raise and this face makes urgent.**
> A tail that is sold, re-registered, or simply mistyped sits in `WAITING` **forever**,
> and the copy above cheerfully says "changes on its own" every day for a year. §21
> already lists a *"this aircraft has not flown in N days"* nudge as an open question;
> this face is where it lands.
>
> **Draft: after 7 days with no fix ever seen**, the third line is replaced by
>
>     Nothing heard from N4523K in 7 days. Check the tail number
>     on the config page.
>
> Seven days is a guess and is marked as one (§18.3). It wants to be longer than a
> holiday and shorter than a season; a weekly flyer who takes a fortnight off should
> not be told their device is misconfigured. **Only counted when a fix has NEVER been
> seen** — an aircraft that flew last month and is parked is a different situation and
> gets the post-flight card, which is already true and already better.
>
> **[FOR DANIEL]** three calls: the copy register, cutting the countdown, and whether
> 7 days is the right nudge.

> ### BUILT 2026-08-27 — the pre-departure face. **And C4's two-face proposal
> could not be implemented as written, for a reason worth keeping.**
>
> C4 asks for the arc face's not-started state in the airline regime and the
> local face's rings in the local one. **The regime is unknowable at that
> moment.** §7.1 infers it from how far the aircraft got from home — and in
> `WAITING` it has never been seen, so there is no flight, no extent and no
> regime. Picking one anyway means picking a face that is confidently wrong half
> the time, on the screen whose entire job is honest nothing-yet. That is the
> same shape as the countdown this proposal already cut.
>
> So the face is **regime-agnostic**: a dim ring, the target, and the copy. It
> also cannot preview a route it has not been told — the codes arrive on a
> tracked contact, and asking a server for a route **by callsign** would be an
> outbound request whose existence names the follow target, which C2 forbids. If
> a route survives from a previous flight the codes *are* drawn, which is the
> preview C4 wanted at no cost.
>
> **The 7-day nudge is built and its clock is in the `follow-log` NVS namespace,
> not `config`.** `millis()` would restart it every power cycle, so a device
> rebooted weekly could never reach seven days. It is deliberately not a fourth
> writer to the `config` namespace: the elimination in
> [nvs-config-flip-2026-08-27.md](nvs-config-flip-2026-08-27.md) turns on there
> being exactly three, and a clock is not worth making the next unexplained flip
> harder to diagnose. Armed once, on the first pass with a target and no fix
> ever seen; disarmed on the first fix; the namespace is already cleared when the
> target changes, so a new tail restarts the clock for free. A device whose clock
> has never synced does not arm — that costs a nudge and never fires a wrong one.


### C5 — Field elevation: the WORKAROUND is dead; the DELIVERY is not built

The design note originally said `include/Airports.h` carries no elevation, generalised
that to airport data in general, and built a self-calibrating learn-over-two-flights
workaround on the gap. That premise was false, and the note corrects it in place.

**We have field elevation for 34,128 airports** — `airports.csv` in the CC0
`vradarserver/standing-data` set carries `AltitudeFeet` on every row:

```
Code,Name,ICAO,IATA,Location,CountryISO2,Latitude,Longitude,AltitudeFeet
4CA0,Lapd Hooper Heliport,4CA0,,Los Angeles,US,34.043301,-118.247002,302
```

Consequences, all deletions: the self-calibrating mechanism is removed; landed detection
gets a real threshold (`geoAltitude` against published field elevation) instead of a
number the device had to earn; the **first** flight is detected as well as the tenth;
"airport not plottable" disappears as a state.

The visual spec's §13 still carried an instruction to "correct the note before building
from it." **That instruction is discharged** — it is done — and is dropped here so
nobody actions it twice.

**But only the workaround deletion is settled.** Checked against the built pipeline
on 2026-08-26: `scripts/ingest-routes.ts` emits `rt:` keys from `routes/` and nothing
else. No airport family is written to KV at all, so `AltitudeFeet` exists in the
corpus and is reachable by **no running code**.

So this section resolves exactly one claim — *the self-calibrating
learn-over-two-flights mechanism stays deleted*, because the data exists and is ours
and the device must never have to earn a number we already have. It does **not**
establish that landed detection has a threshold available to it today. Delivering
that data is a build item (§19 item 7) and is C1's concrete form: `ap:` keys if a
lookup by code suffices, the D1 side if §7.3's priority-sorted overlay forces a query.

**Stage 1 is not gated on it.** The local regime's home field is one airport, which
can ride the existing config flow — so nothing about local-first waits for this
decision.

> **Confirmed by the build, 2026-08-27, and with a sharper split than this
> section assumed.** C5 is really *two* deliverables, and only one is missing:
>
> | | stage 1 | how |
> |---|---|---|
> | **which field** | **available** | nearest code from the airports data already on the device — no new dataset, no Worker surface |
> | **its elevation** | missing | needs the `ap:` family this section describes (§19 item 7) |
>
> Treating them as one thing is what produced the `HomeContext.known` defect
> recorded at §5: the *position* half was withheld because the *elevation* half
> was unavailable, and every pattern dropout read `SIGNAL LOST` as a result. The
> two are independent and are now two flags.
>
> So what C5's delivery actually buys, concretely: AGL readouts on the local
> face, `APPROACH_LOST`, and landed-by-profile. Nothing else in stage 1 waits on
> it.

### C6 — Follow is one screen with several faces, not several screens

The design note says Follow is "a fourth screen (Radar / List / Stats / Follow)." The
visual spec describes an arc face, a globe face, a local face and a post-flight card.
Read together these could produce four navigation entries, which would be wrong.

**One screen slot. The face is chosen by regime and state, never by the user.** §7 is
the routing table.

### C7 — Everything else agrees

Worth recording, because agreement reached independently is evidence:

- Post-flight persistence: 128 decimated points to NVS. Both, identically.
- `include/Airports.h` (~250 curated) superseded by the 34,128-row CSV. Both.
- Route data from CC0 `vradarserver/standing-data`, self-mirrored, never adsbdb. Both.
- Follow never takes the radar. Both.
- The trail draw-cost measurement gates the feature's shape. Both, as the first gate.
- The four contact states, with the same names and the same expected/unexpected
  distinction. Arrived at separately (§5.2).

---

## 3. Why the existing trail cannot be reused

`TrackedAircraft` already has a trail, and it is a good one. It is also the wrong object
for this, in three independent ways.

`src/models/TrackedAircraft.h` holds 60 points, age-capped at 90 seconds, with a minimum
interval tuned so every feed cadence produces the same 89-second span. That tuning is
deliberate and correct: it is a **motion cue**, whose job is to show which way a blip is
going.

A flight track is a different object:

| | existing trail | follow track |
|---|---|---|
| purpose | which way is it going | what did he fly today |
| span | 90 s, fixed | a whole lesson (1–2 h) |
| sampling | time-based, cadence-normalised | distance-based (§4.1) |
| lifetime | dies with the contact | must outlive dropouts |
| owner | `TrackedAircraft` | `AircraftManager` |

The third row forces the decision. `AircraftManager.cpp:2249` evicts a contact absent
past a grace window of roughly 30 seconds and rebuilds it from scratch on reappearance,
discarding the trail. For a trainer at 1,000 ft AGL — whose coverage is line-of-sight to
ground receivers and therefore intermittent by nature — that eviction is not an edge
case. **It is the normal operating condition.**

So the track is owned by the manager, keyed by resolved ICAO hex, and survives the
contact table entirely.

---

## 4. The track: sampling, memory, and allocation discipline

### 4.1 Decimate by distance, not time

    append the fix if   great_circle(fix, lastKept) >= TRACK_MIN_SEP_M

`TRACK_MIN_SEP_M` starts at **150 m**. At the tightest zoom a pixel is a few hundred
metres, so anything closer is drawing on top of itself. The rule has a pleasant property:
straight cruise legs compress to almost nothing while turns, circuits and manoeuvres keep
their full shape — because shape is exactly where consecutive fixes differ in *direction*
rather than distance.

It also self-bounds without a timer. 1024 points at 150 m minimum separation is about
150 km of flown path, comfortably a whole lesson.

### 4.2 What it costs, at worst case

    struct TrackPoint { float lat; float lon; uint16_t sec; };   // 10 B, pads to 12
    TRACK_CAPACITY = 1024
                                                  1024 x 12 = 12,288 B

**12 KB, and that is the worst case rather than the typical case** — it is the size of
the buffer whether or not it is full, which is the point of §4.3.

### 4.3 ONE allocation, at follow-enable, never grown

This device has an **open, unexplained fragmentation problem** (#245): roughly 24 KB of
erosion over 11 hours, with the handshake #4 anomaly still the only cause-shaped lead.
Adding allocation behaviour to this codebase on the strength of first-principles
reasoning would be exactly the wrong lesson to draw from that investigation.

So the discipline is specified here rather than left to implementation:

- **One** `heap_caps_malloc(TRACK_CAPACITY * sizeof(TrackPoint), MALLOC_CAP_SPIRAM)` at
  the moment follow is enabled.
- **Never** reallocated. Fixed-size ring from birth; it wraps rather than grows.
- **Freed only** when follow is disabled — not on landing, not on new-flight, not on
  eviction. A new flight resets the write index; it does not touch the allocation.
- **If the allocation fails, the feature degrades to notification-only and says so on
  screen.** It must not retry in the loop. A periodic retry of a large allocation under
  fragmentation is itself a fragmentation source, and it turns a clean degradation into
  a slow decay.

The distinction that matters: **a track that grows is a fragmentation source; a track
allocated once and reused is not.** The first is a stream of differently-sized blocks
interleaved with TLS handshakes. The second is one block that either exists or does not.

### 4.4 Verify it, do not assume it

"PSRAM is invisible" is an assumption. Well-supported — the 2026-08-09 measurement showed
a 240×240 backbuffer plus two photo sprites moving `psram_free` by 73,532 B and leaving
the internal heap untouched — but still an assumption, in a codebase with an open issue
that consists precisely of memory behaving in a way nobody predicted.

The instrumentation already exists, from the #250 work:

- `psram_free` — should drop by ~12 KB at follow-enable and then stay flat
- `tlsmem=psram/internal/fallback` — the fallback counter is the tell. If the PSRAM
  allocation silently lands on the internal heap, this is where it shows.
- `largest` / `allocFail` — must be unchanged across a multi-hour follow soak

**Acceptance criterion: a follow-enabled board and a control board, soaked side by side,
must show the same internal-heap trajectory.**

### 4.5 Drawing the track

Points are stored as lat/lon, like the existing trail, so reprojection under pan, zoom
and `radar-up` rotation (`AircraftManager.cpp:3226`) comes free.

- Draw the track **beneath** everything else, in one dim distinct colour.
- **Exempt it from the sweep's phosphor fade.** A radar return decays because it is a
  *return*; the track is a *record*. Holding it steady while returns pulse around it is
  not decoration — it tells you at a glance which marks are live and which are history.
- Trail brightness fades toward the tail — recency, which is native to a radar and needs
  no colour. **Keep colour for state semantics only.** Altitude gets a number or a thin
  profile strip; it does not get a colour ramp.

**CORRECTED 2026-08-26 -- the frame reference quoted below was the wrong number.**
This section cited 27.5-31.1 ms as "under full load with overlay and trails".
The code note it came from says the opposite: that range was measured with
`overlay/trails/fade/scanline OFF`, and is the retracted 2026-08-02 artefact.
The honest all-on figure was 46-48 ms -- and even that was measured on boards
whose per-aircraft info labels had been silently switched off by the same
scripted POST, so stock config is ~60-68 ms at n=30-40. The budget was
re-baselined 60 -> 85 ms accordingly (#264). **Follow was never measured
against a real budget until that was fixed.**

**MEASURED 2026-08-26 on the s3-128: track draw mean 4.30 ms, max 5.5 ms at
cap 256, on a full 1024/1024 buffer, stable over thousands of frames, with
`psram_free` flat and `allocFail` 0. Verdict per 18.1: TRACK PRODUCT.**

**The draw cost is the number that decides what this feature is.** The frame budget note
at `AircraftManager.cpp:1531` records 27.5–31.1 ms under full load with overlay and
trails. Projecting and drawing up to 1024 extra segments per frame could blow that
outright. Mitigation is a draw-time cap of ~256 segments with adaptive stride.

If that number is bad, **this is a notification product rather than a track product** — a
very different and much smaller feature, and one worth knowing about before building the
wrong one. See §18.

---

## 5. The state machine

> **BUILT 2026-08-26 in `include/FollowState.h`, with two deviations from what
> is written below. Both are recorded here rather than only in the code,
> because a spec that disagrees with the build is how the next reader gets
> misled.**
>
> 1. **`HomeContext.known` ships `false`.** The AGL reasoning in 5.3 needs the
>    published field elevation, which per C5 is a LOOKUP that is not built.
>    Rather than reason from a wrong threshold, the machine degrades to its
>    position-free arms and says so. When C5 lands, setting `known` is the
>    whole change.
>
>    **CORRECTED 2026-08-27 while building the local face, and the correction
>    is worth more than the original note.** That single flag was a defect, not
>    a deviation. `InsideHome()` — a question about WHERE he is — was gated on
>    it, so with the elevation unknown every position argument fell through and
>    **every pattern dropout came out `SIGNAL_LOST`**: precisely the failure 10
>    names in bold, *"getting this backwards makes the device look broken every
>    single circuit"*, shipped by the mechanism the note above described as a
>    safe degradation.
>
>    The device has always known where home is — it is the configured location,
>    the point the radar is centred on. Two independent facts had been folded
>    into one flag, so `HomeContext` now carries two:
>
>    | flag | question | stage 1 |
>    |---|---|---|
>    | `positionKnown` | where is home? | **true** whenever a location is set |
>    | `elevationKnown` | how high is the field? | false until C5's delivery half |
>
>    The reading rule: an argument about **where** needs `positionKnown`; an
>    argument about **height above ground** needs `elevationKnown`, and without
>    it the machine declines the argument rather than substituting MSL. At a
>    3,460 ft field a 1,000 ft circuit reads 4,460 ft, which is above every
>    threshold in `Tuning` and silently wrong. `AglFt()` returns **NaN** rather
>    than a plausible number, so a caller that forgets produces something
>    visibly broken instead of something quietly off by the field elevation.
>
>    Consequently `LANDED`-by-profile and `APPROACH_LOST` are unavailable in
>    stage 1 and `NO_COVERAGE` is not — which is the honest reading of 5.1's
>    own sentence, *"NO_COVERAGE is a POSITION argument."*
>
>    **This is graded, not asserted.** `test/host/test_follow_state.cpp` pins
>    it with the pre-fix world as a control: it asserts that with
>    `positionKnown` false the circuit dropout comes out `SIGNAL_LOST`, so
>    re-conflating the flags fails the suite. Rehearsed red before being
>    believed.
>
> 1b. **`AlertTitle` formats into a caller buffer** instead of returning a
>    `String`. That removed the last Arduino dependency from the file, which is
>    what makes the host test above possible at all — and an alert path that
>    does not allocate is better on the device regardless.
> 2. **The copy is ASCII.** 6 writes middots; there is no `setFont()` anywhere
>    in the radar draw path, so the glyph set is the default font's and a
>    UTF-8 middot arrives as two bytes of garbage. Same finding as
>    `include/RouteLabel.h`. The words are unchanged; only the separators are.
>
> The module is PURE -- no members, no display, no `millis()` -- which is the
> extraction 17 names as the prerequisite for the privacy test. That test can
> now land alongside the config surface rather than after it.

### 5.1 Absence is three states, not one

The core of the design and the thing most likely to be got wrong.

A naive state machine has one absence state called something like `LOST`, treated as an
error condition. That is wrong here for a reason about people rather than code: **absence
is the normal operating condition of this feature**, and the three kinds of absence mean
completely different things to the person watching.

| state | means | expected? |
|---|---|---|
| `NO_COVERAGE` | he is somewhere ground receivers do not reach | **yes** |
| `SIGNAL_LOST` | he should be visible and is not | no |
| `APPROACH_LOST` | last seen descending toward the home field | yes, usually |

The device can tell these apart. `NO_COVERAGE` is a position argument — over ocean, or
far from anywhere with receivers. `APPROACH_LOST` is a profile argument — descending,
slowing, inside the home radius. `SIGNAL_LOST` is what is left over.

Collapsing them into one state is what makes this feature frightening instead of
reassuring, and no amount of good copy rescues a state machine that cannot tell expected
absence from unexpected absence.

### 5.2 Lifecycle states and display states

The two source documents used different granularities and both were right for their
purpose. The mapping is explicit so nothing is built from half of it:

| lifecycle state (design) | display state (visual) | face |
|---|---|---|
| `IDLE` | — | screen hidden entirely |
| `WAITING` | **not yet** — see C4, needs design | pre-departure face **[UNKNOWN]** |
| `GROUND` | `IN_CONTACT`, on ground | active face, `ON THE GROUND` |
| `AIRBORNE` | `IN_CONTACT` | active face, nominal |
| `NO_COVERAGE` | `NO_COVERAGE` | active face, degraded |
| `SIGNAL_LOST` | `SIGNAL_LOST` | active face, degraded |
| `APPROACH_LOST` | `APPROACH_LOST` | active face, benign |
| `LANDED` | post-flight | post-flight card (§11) |

### 5.3 Transitions

| state | entered when | leaves to |
|---|---|---|
| `IDLE` | no follow target set | — |
| `WAITING` | target set, nothing seen yet | `GROUND`, `AIRBORNE` |
| `GROUND` | fixes arriving, `onGround` true | `AIRBORNE` |
| `AIRBORNE` | `AIRBORNE_CONFIRM_FIXES` consecutive airborne fixes | `LANDED`, the three absence states |
| `NO_COVERAGE` | absent, last position in a known-gap region | `AIRBORNE` |
| `SIGNAL_LOST` | absent > `TRACK_LOST_MS`, should have been visible | `AIRBORNE`, `LANDED` |
| `APPROACH_LOST` | absent, last fixes descending toward home field | `AIRBORNE`, `LANDED` |
| `LANDED` | `onGround` sustained, or low+slow inside home radius | `AIRBORNE` (new flight) |

Constants, all of which are **guesses until a real flight is logged** (§18.3):

- `AIRBORNE_CONFIRM_FIXES` (2–3) — so one spurious fix cannot announce a takeoff
- `LANDED_CONFIRM_MS`
- `TRACK_LOST_MS` (start ~3 min)
- `NEW_FLIGHT_GAP_MS` — after which a resumed track is a new flight, not the same one

Per-fix data available, unchanged: `onGround`, `baroAltitude`, `geoAltitude`, `velocity`,
`trueTrack`, `verticalRate`, `positionSource`.

Note that per C5 the "learned home field" is now a **lookup rather than a calibration** —
the threshold arrives at boot instead of after two flights. The state machine gets
simpler rather than different.

### 5.4 The rail

**A lost signal must never be reported as a landing.** Getting *"he's down safely"* when
the truth is *"we stopped hearing him"* is the worst thing this product could do, and it
is the failure mode the whole state machine is arranged to prevent.

`LANDED` therefore fires only on confident evidence: sustained `onGround`, or low-and-slow
inside the home radius. Everything else routes to one of the three absence states and is
worded as such.

---

## 6. The copy

**This is the product, not the rendering.** A hobbyist radar with a coverage gap is a
shrug. A device someone's spouse is watching that goes dark mid-Atlantic is frightening,
and "we designed the gap in" is only true if the words on the screen are right.

Three principles:

1. **Name the mechanism.** "No receivers here" is calming because it explains. "Signal
   lost" alone is not.
2. **Never imply certainty we do not have.** No landing claim without evidence.
3. **Different words for different states.** If `NO_COVERAGE` and `SIGNAL_LOST` read the
   same, the state machine's work is wasted.

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

Hedged deliberately: says what was seen, says what that usually means, does not claim to
know.

    ntfy   title  N4523K — last seen on approach
           body   Signal lost at 1,200 ft over the field, 15:16.
                  Coverage near the ground is patchy, so this usually
                  means landed.

    screen        ON APPROACH — SIGNAL LOST
                  Last seen 15:16  ·  1,200 ft  ·  over the field
                  Low-level coverage is patchy. This usually means landed.

### Signal lost — unexpected

Screen only by default (§15). No phone alert unless asked for.

    screen        SIGNAL LOST
                  Last seen 14:52  ·  4,500 ft  ·  12 mi NE
                  He is out of receiver range, not off the radar.

That last line converts an alarming absence into a statement about *our* equipment rather
than about him.

### No coverage — expected absence

    With a known route:

    screen        OVER THE ATLANTIC
                  No ground receivers out here — this is expected.
                  Next contact expected around 18:40, near Ireland.

    Without a route:

    screen        NO COVERAGE
                  Ground receivers do not reach where he is now.
                  He will reappear on the far side.

Note the deliberate absence of the word "lost" in both. Different state, different words.

> ### BUILT 2026-08-27 — the copy is now regime-dependent, as this section's own
> stage-1 note predicted.
>
> `FollowState.h` carried a note reading: *"NoCoverage is worded for the LOCAL
> regime, which is the only one that ships... so this becomes regime-dependent at
> that point. It is one string today because there is one regime today, not
> because the two agree."* The arc face is that point.
>
> **Two regimes, one switch** — `Headline(State, Regime = Local)` and
> `Explanation(State, Regime = Local)`. Deliberately not a parallel
> `HeadlineAirline()`: that is the same fact written twice, the shape
> `RouteLabel.h` exists to remove. The default keeps every stage-1 caller and
> assertion meaning what it meant.
>
> | state | local | airline |
> |---|---|---|
> | `NO_COVERAGE` | `BELOW COVERAGE` | `NO COVERAGE` |
> | `APPROACH_LOST` | `ON APPROACH - SIGNAL LOST` | `BELOW COVERAGE` |
>
> The airline `APPROACH_LOST` chip borrows the local regime's `NO_COVERAGE`
> words on purpose: in each regime the phrase names the same physical thing, and
> the two regimes never appear on one screen. Asserted pairwise-distinct within
> each regime, plus a control that the parameter is wired to anything at all
> (`SIGNAL LOST` and `AIRBORNE` must be *identical* across regimes).
>
> **One line of §6 is deliberately not built.** The no-coverage worked example
> ends *"Next contact expected around 18:40, near Ireland."* We cannot say that:
> it needs a model of where receiver coverage resumes, which we neither have nor
> licence, and a time derived from it — a number invented on the one screen whose
> job is to explain an absence honestly. Same call as cutting C4's countdown,
> same reason (principle 2). *"He will reappear on the far side"* says what we do
> know. A test asserts the airline no-coverage copy **contains no digit**, so the
> sentence cannot quietly come back.


### Pre-departure **[DRAFTED 2026-08-27 — approved]**

Written at C4 and approved with the countdown cut. The headline is
`WAITING FOR DEPARTURE`; the load-bearing line is *"Nothing heard yet today.
This screen changes on its own when he takes off."* **`WAITING` never borrows a
loss state's word** — nothing has been lost, nothing has started, and a test
asserts the string "lost" appears in neither line.

---

## 7. Display: which face, when

### 7.0 The panel

| | |
|---|---|
| Display | 1.28" round IPS, 240 × 240, curved cover glass |
| Touch | capacitive |
| Frame budget | 27–31 ms **[MEASURED]** |
| Angular resolution | ~0.8° per pixel at disc centre, i.e. **~89 km** **[MEASURED]** |

That last row governs all geography. Any coastline detail finer than ~89 km is sub-pixel
before it is drawn. Do not spend memory or draw time on detail the panel cannot resolve.

### 7.1 Routing table

One screen slot (C6). The face is chosen, never picked:

| condition | face |
|---|---|
| no follow target | screen hidden entirely |
| target set, not yet seen, no previous flight | pre-departure (C4) |
| target set, not yet seen, previous flight on file | post-flight card (§11) |
| local regime, any live state | local face (§10) |
| airline regime, both codes resolve, great-circle ≥ **4,000 km** | globe face (§9) |
| airline regime, route known | arc face (§8) |
| **airline regime, NO route** | **local face (§10)** — see below |
| `LANDED` until next takeoff | post-flight card (§11) |

Regime is inferred, not configured: an aircraft that stays inside the home radius is
local; one that leaves it is not.

**AMENDED 2026-08-27 — the route-less long-range row.** This table used to send
"airline regime, otherwise" to the arc face unconditionally, which assumed a route
always exists. It does not: a GA cross-country is past the home radius with nothing
filed, and it was getting a dim empty ring. The local face's auto-scaled rings and
its track are strictly more informative for that flight, so it is the fallback.
The arc is chosen when it has something to draw.

Note also that the regime is read from the flight's **furthest extent**, which never
falls within a flight and resets with the next one. Feeding it the live separation
would flap the face once per circuit at exactly the radius where an aircraft spends
most of its time.

### 7.2 Reuse, do not rebuild

`src/anim/FlightAnimation.cpp` already contains a working orthographic globe renderer on
this panel. Its header states it was "written to be lifted into the real flight
director." Lift it.

**[MEASURED]** from that source:

- Coastlines: Natural Earth 1:110m, spherical Douglas–Peucker at 0.5°, **1,306 vertices**
- Storage: `GeoVec { int16_t x, y, z }` unit vectors in `Coastlines.inc` — ~7.6 KB
- Projection: `GlobePt()`, orthographic, dot-product basis with a `zz > 0.0f` visibility test
- Cost: **262 ns/vertex → 0.34 ms** for the entire globe
- Line pixels: **1.06 µs/px** — pixels dominate, not vertex count
- `constexpr float kGlobeR = 119.0f`, `constexpr float kGlobeTilt = 30.0f`, both fixed

**Do not cache the globe into a sprite.** The source has already measured this:

> "AND NOTHING IS CACHED. A 240x240 PSRAM sprite costs 6.24 ms/frame to blit, which is
> more than drawing the whole globe live."

Redrawing is ~18× cheaper than blitting a cached copy. This also applies to the Follow
track: do not assume a PSRAM framebuffer is a win anywhere on this panel until measured.

**Flash is not the constraint. [MEASURED]** `platformio.ini`'s `[filters]` section already
prescribes the path in — its comment reads: "THE DAY A PRODUCT CONSUMES IT, that env stops
applying this fragment and re-includes `+<anim/>` — exactly how the edition dirs work
above. Nothing about the module has to change for that."

| | |
|---|---|
| Module fully linked | 34,777 B — of which 8,508 B is coastline vertex data |
| Included but unreferenced (`--gc-sections`) | **568 B** |
| App partition (`partitions-s3-16mb-bignvs.csv`) | 6,553,600 B, 26.7% used, 4.58 MB free |
| Module as share of partition | **0.53%** |

The `[filters]` comment's "tight on flash" premise cites `min_spiffs`, which
`[env:blipscope-s3-128]` overrides. That premise is stale **for this SKU** — it may still
hold for others, so the fragment keeps earning its place elsewhere. Do not delete it; just
stop applying it to the radar env when Follow consumes the module.

### 7.3 Airports overlay

Field elevation for 34,128 airports cannot ship on the device (2.38 MiB), so it rides the
existing `/api/v1/blipscope/airports` overlay endpoint — server-capped and priority-sorted.
**Which store backs that endpoint is open — see C1.** The priority-sort is a query, which
is a D1 shape, while routes are a key lookup already living in KV.

---

## 8. Arc face — the airline default

A round panel affords two independent circular readings. Spend both, and keep the centre
clear.

> ### BUILT 2026-08-27 — the arc face. Verdict: **§8 renders as specified, with two
> recorded deviations and one colour question left for the bench.**
>
> `AircraftManager::DrawFollowArcFace`, plus `include/FollowArc.h` for every
> number it draws. 428 host checks over the arithmetic, 0 failures, rehearsed
> red three times (a raw `atan2` bearing, an unclamped progress, a naive
> lat/lon midpoint — 3, 12 and 23 failures respectively).
>
> **Every radius and text row is the spec's 240 px figure × `SCREEN_SIZE/240`.**
> Hardcoding them is the mistake CLAUDE.md names by hand, and on the 412 px
> SPD2010 it would draw the whole face inside the middle half of the glass with
> a bezel ring floating in the centre.
>
> **One angle convention, and it is `FollowArc.h`'s.** LovyanGFX has `fillArc`
> and it would be cheaper than stepping. It is not used: the band would then be
> placed by LovyanGFX's convention while the marker, the codes and the wedge are
> placed by `ArcAngleDeg` — two implementations of one fact. If they disagreed by
> a few degrees the marker would sit *beside* the arc, and in a photograph that
> reads as a **progress** error, sending the next reader to `ProgressAlong`,
> which is correct and graded. The dashes need per-step control anyway.
>
> **Two things the build found are now SPEC rather than deviations, amended
> 2026-08-27.** The primary-slot rule (always a magnitude, the label names it) is
> written into the centre-stack section above, because it generalises past this
> face. And §7.1's routing table has a new row: "airline regime, NO route" falls
> to the **local face**, not to an arc with nothing on it — a GA cross-country is
> past the home radius with nothing filed, and rings-and-track beat a dim empty
> ring. Both were found by building it; neither is carried as a deviation.
>
> **The degradation is a tested path, not a hope.** An unresolved code still
> *draws* — the codes are strings first and coordinates second — so a missing
> airport costs the marker and nothing else. A four-letter ICAO code (the mirror
> carries both forms) misses on purpose rather than matching its first three
> characters against some other field.
>
> **[FOR DANIEL] the one open question is a colour.** §8 is emphatic that
> `APPROACH_LOST` must use the accent, not the warning colour — "getting this one
> wrong alarms someone watching a family member land". §10 says nothing about the
> local face's colour for it, and that face has already been eyeballed with amber.
> Changing it unreviewed would be worse than asking, so the arc face uses green
> and the local face keeps amber, from one mapping with one parameter. **Judge
> them side by side in the absence-copy session** — no argument in a comment
> settles how a colour reads.
>
> **Cost: instrumented, NOT yet measured.** `[follow] arc=` reports µs and stroke
> count on its own line — deliberately *not* folded into `followDrawUs`, which
> would report "the track cost 4 ms" on a frame where the track was never drawn.
> No board was free to measure on (COM4 is running the #264 stock-config capture;
> COM119/COM16 are on the #245 A/B and are not to be disturbed), so what follows
> is arithmetic, labelled as such: worst case is `NO_COVERAGE` at low progress —
> 471 strokes of unflown-plus-flown plus ~235 dashed, each a 5 px radial line,
> ≈ 3,530 px. Against `FlightAnimation.cpp`'s own **measured** 1.06 µs/px that is
> **≈ 3.7 ms plus ~706 call overheads**. Read the real number off the serial line
> before quoting it.


### Geometry **[PROPOSAL]**

- **Panel ring** r = 118.5, stroke 3
- **Route arc** r = 100, stroke 5, round caps. Sweeps **clockwise from 135° to 405°**
  (270° total), where 0° is 3 o'clock and angle increases clockwise. Origin sits at 7:30,
  destination at 4:30, leaving the bottom clear for text.
- **Arc fill**: unflown portion in a dim neutral; flown portion in the active accent
- **Aircraft marker** on the arc at `135° + 270° × progress`, r ≈ 5, faint 9.5 halo
- **Airport codes** inside the arc at r = 84, at each arc end
- **Bearing wedge** on the bezel at r = 105–115, a filled triangle ~4.6° half-width,
  pointing at the true bearing from the user's configured coordinates
- **Cardinal ticks** at r = 107–112 for N/E/S/W, with a small "N" glyph at r = 97

The cardinal ticks are not decoration. A pointer with no reference frame is meaningless;
if the ticks are cut, cut the wedge too.

### Centre stack **[PROPOSAL]**

| y | content | style |
|---|---|---|
| 86 | callsign | mono 10, letterspaced, dim |
| 124 | **primary readout** — always a MAGNITUDE | condensed 34, accent |
| 139 | readout label — names the magnitude | mono 8, letterspaced |
| 150–167 | state chip (when degraded) | outlined pill, 8 px |
| 158 | altitude (when nominal) | mono 9 |

Altitude and the state chip occupy the same slot deliberately: when anything degrades,
altitude is what gets displaced, and the layout does not jump.

**THE PRIMARY SLOT ALWAYS HOLDS A MAGNITUDE, AND THE LABEL ALWAYS NAMES IT.**
Amended 2026-08-27, and it generalises beyond this face. A slot that holds `1h12` in
three states and the phrase `ON APPROACH` in the fourth has to either overflow the
chord or shrink the type — and a stack whose type size changes with state is the
same jump the shared altitude/chip slot above exists to prevent. The label line is
already there to say what the number is, so the state's word goes to it:

| state | primary | label |
|---|---|---|
| `IN_CONTACT` | `1h12` | `TO ARRIVAL` |
| `NO_COVERAGE` | `1h12`, dimmed | `EST. ARRIVAL` |
| `SIGNAL_LOST` | `24m` | `SINCE CONTACT` |
| `APPROACH_LOST` | `12 mi` | `ON APPROACH` |
| no route resolved | `210 mi` | `AWAY` |

The state's *word* is not lost — it is on the chip, which is where a state belongs.

### How the four contact states are drawn

**`IN_CONTACT`** — Position live and current. Solid marker, solid arc, solid wedge.
Primary readout is **time to arrival**. **The state line stays empty.** An instrument that
announces "OK" every second teaches you to ignore it.

**`NO_COVERAGE`** — expected. Position is dead-reckoned.
- Marker goes **hollow**
- Arc **ahead of the marker** turns dashed, in the warning colour, showing the estimate
- Bearing wedge dims to ~40% opacity
- Primary readout **dims** and its label becomes an estimate
- Chip: `NO COVERAGE`, plus a region name where known

Three simultaneous signals that the position is inferred. Nothing here is a fault.

**`SIGNAL_LOST`** — unexpected.
- **No dashed projection.** We do not know, so we do not draw.
- Arc and marker freeze at the last known fix, in the warning colour
- Primary readout **switches** from time-to-arrival to **time since last contact**
- Chip: `SIGNAL LOST`

The honest headline is now how stale the picture is, not when they land.

**`APPROACH_LOST`** — benign.
- **Not** the warning colour. Use the active accent.
- Arc nearly complete, marker hollow, destination code highlighted
- Primary readout becomes `ON APPROACH` with distance out
- Chip explains itself: `BELOW COVERAGE`, sub-line "expected at this range"

Getting this one wrong alarms someone watching a family member land. **It is the single
most important state in the feature.**

---

## 9. Globe face and scale selection

Same panel, geography instead of an arc. An orthographic projection of a sphere **is a
circle**, so on a round panel it fills the glass with nothing cropped. No rectangular
display can claim that. This is the one place where the hardware's shape is an advantage.

> ### BUILT 2026-08-27 — the globe, and the extraction that had to come first.
>
> **The extraction.** `include/GlobeProjection.h` + `src/anim/Coastlines.cpp` now
> hold the projection and the coastline set; `FlightAnimation.cpp` calls them
> instead of its own copy. One implementation, two consumers — §7.2's actual
> intent, reached by exporting rather than re-including, because everything it
> named was in an anonymous namespace.
>
> **The one design change is the one the blocker pointed at.** `MakeBasis()`
> returns a basis and `Project()` takes one; no globals live in the shared
> header. The animation keeps a *cache* of its basis (its scenario changes only
> in `SetScenario`, which clears the flag) — that is a cache of a pure function,
> not the latch it used to be, and Follow builds its own per route.
>
> **"Behaviour-preserving" is asserted, not claimed.** The expected values in
> `test/host/test_globe_proj.cpp` were *printed by the pre-extraction code*: the
> GOLF-07 basis to nine decimals and nine real coastline vertices projected
> through it. 37 checks. Four red probes fix what they bite on: flipping the
> screen-y sign fails 11, tilting the other way fails 14, truncating `d2r` to
> `0.01745` fails 14 — and making `d2r` *more* accurate fails **nothing**,
> because it moves a vertex by ~4e-5 px against a 1e-3 px tolerance. The pins
> catch anything that moves the picture and are deliberately blind to anything
> that does not.
>
> **A safety net that was believed to exist does not.** `missileer-s3-146` builds
> green in CI and contains **no animation code at all** — every EAM env applies
> `${filters.anim_off}`. Confirmed by grepping its ELF for `FlightAnimation`,
> `kCoast` and `GOLF-07` (all absent) against an anchor that is present. So a
> refactor of the module could have broken every env that actually contains it
> while the Missileer row stayed green. `animtest-s3-128` — the only env that
> compiles *and renders* `src/anim/` — was absent from CI and now has a slug-less
> row, the same fix and the same reasoning as the 1.75" AMOLED.
>
> **Composition, as specified**: disc r = 94 at (120, 102), top line at y = 26 on
> a backing plate, readout at y = 223, centred on the great-circle midpoint,
> route solid behind and dashed ahead, aircraft at its **real ADS-B position**
> rather than interpolated onto the line, terminator with the night side shaded.
>
> **Tilt is 0, departing from the module's 30°.** The animation tilts so a
> near-meridional missile arc bows across the disc instead of running down its
> spine. §9 asks for something incompatible: *"centre the globe on the
> great-circle midpoint so both endpoints are visible"* — and a tilt moves the
> midpoint off centre by construction. The composition rule wins.
>
> **The equation of time is omitted from the terminator**, deliberately: it
> reaches ±4° of longitude, under 1.7 px at r = 94. A correction smaller than the
> line drawing it is not a correction.
>
> **Flash, measured on the artifact.** Re-including `+<anim/>` in the radar env
> cost **+568 B** before anything referenced it — which is §7.2's own measured
> figure for the unreferenced module, arrived at independently. With the globe
> face referencing it, the radar image goes from 1,782,655 to **1,797,359 B
> (+14,704)**, i.e. 0.22% of the 6.25 MB app partition. The Missileer image is
> unchanged at 1,608,019 B, for the reason above: it never contained the module.
>
> **Cost: instrumented, still NOT measured on hardware.** `[follow] arc=` reports
> µs and the vertex count for whichever face drew. §7.2's reference is 0.34 ms
> for 1,306 vertices of projection; this face adds ~7,000 day/night sign tests
> for the night fill and a 96-step route polyline. No board is free (COM4 is on
> the #264 capture; COM119/COM16 on the #245 A/B), so there is no honest total to
> quote — read it off the serial line in the bench session.

> ### FINDINGS 2026-08-27 — before building it. **The threshold is closed; §7.2's
> "lift it" is blocked on the module's shape, which is a bigger job than a
> re-include.**
>
> #### 1. The threshold, argued rather than chosen: **4,000 km**
>
> The screen span of a route centred on the disc is `2·R·sin(θ/2)`, where θ is
> its great-circle angle. **That formula reproduces all three of §9's measured
> rows exactly** at R = 119 — DEN→DEL 196.8 px (measured 197), SEA→LAX 28.6
> (measured 29), PDX→SEA 3.89 (measured 3.9) — which is the control for using it
> at the composition's actual R = 94.
>
> Solving `2·94·sin(θ/2) = 60` gives θ = 37.2°, i.e. **4,139 km**. It is rounded
> **down to 4,000 km**, and the rounding is the argument: the US transcon family
> straddles the exact figure — JFK→LAX is 3,974 km (57.7 px) and SFO→JFK is
> 4,152 km (60.2 px). Those two look identical on glass and would get different
> faces. A customer following two transcons seeing two different faces is a worse
> outcome than 2 px of arc, and §9 says "roughly 60 px" precisely because the
> number is soft. 4,000 km is 57.9 px.
>
> | route | km | px @ R=94 | face |
> |---|---:|---:|---|
> | PDX→SEA | 208 | 3.1 | arc |
> | SFO→LAX | 544 | 8.0 | arc |
> | LHR→BCN | 1,147 | 16.9 | arc |
> | JFK→ORD | 1,187 | 17.5 | arc |
> | DFW→ORD | 1,291 | 19.0 | arc |
> | SEA→LAX | 1,537 | 22.6 | arc |
> | JFK→DEN | 2,609 | 38.2 | arc |
> | JFK→LAX | 3,974 | 57.7 | **globe** (by the rounding) |
> | SFO→JFK | 4,152 | 60.2 | globe |
> | LHR→DXB | 5,498 | 78.6 | globe |
> | LHR→JFK | 5,540 | 79.2 | globe |
> | SFO→NRT | 8,227 | 113.1 | globe |
> | DEN→DEL | 12,404 | 155.4 | globe |
>
> #### 2. The cases that would have wanted the regional chart — and they are the
> majority
>
> Every row above between **~500 km and 4,000 km** renders 8–38 px of globe arc:
> too small to read as a route, and far too large for the regional chart's
> ~413 km window at r ≈ 3,700. **Neither of §9's two scales serves them.** That
> band is SEA→LAX, DFW→ORD, JFK→ORD, LHR→BCN, JFK→DEN — most of what an airline
> follow will actually be.
>
> This is not an argument for a third scale. It is the reason the **arc face is
> the airline default and the globe is the long-haul upgrade**, exactly as §8's
> title says: below the threshold a 270° arc is strictly more legible than 20 px
> of great circle, and the regional chart only becomes the better answer once its
> fine coastline dataset exists (§9, out of scope for this pass).
>
> #### 3. §7.2's "re-include `+<anim/>` and lift `GlobePt()`" **cannot work as
> written**
>
> Checked in the source rather than assumed. `GlobePt`, `GeoVec`, `kCoast`,
> `BuildGlobeBasis` and the `gGlobe` basis all live inside
> `namespace missileer { namespace flight { namespace { … } } }` — an **anonymous
> namespace**, i.e. internal linkage. `FlightAnimation.h` exposes only the
> `FlightAnimation` class (`Begin`/`Advance`/`Render`/`SetScenario`). Re-including
> the module compiles it; **nothing in it becomes callable.**
>
> Two further blockers behind that one:
>
> - `GlobePt` reads a **file-global basis that latches** (`gGlobeReady`), built by
>   `BuildGlobeBasis()` from the missile scenario's `gLaunchLat/gAimLat` globals.
>   Follow needs a basis per route, so a parameter has to replace a global.
> - `kCoast` is declared against a `GeoVec` defined in that same anonymous scope,
>   so the data cannot be reached without the type.
>
> **So the globe is an extraction, not a re-include:** the projection and the
> coastline data have to be exported to a shared surface that both the animation
> and Follow call — one implementation, per §7.2's actual intent — and that means
> editing shipping Missileer code. Doing it is right; doing it *unverified* is
> not, and **no board is free** (COM4 is on the #264 capture, COM119/COM16 on the
> #245 A/B). The extraction is therefore its own increment, with the animation
> re-checked on glass when a board frees up.
>
> Nothing above changes what §9 asks for. It changes what building it costs, and
> that was worth knowing before starting rather than halfway through.


### Composition **[PROPOSAL]**

- Globe disc **r = 94, centred at (120, 102)** — pulled up and shrunk from the full 119 so
  the readout gets a band of its own below it. The alternative is text over ocean, which
  works on some routes and fails on others.
- Top line at y = 26: callsign · route, over a dark backing plate
- Primary readout at y ≈ 223, below the disc
- Centre the globe on the **great-circle midpoint** so both endpoints are visible
- Route: dashed ahead, solid behind, aircraft drawn at its **real ADS-B position**, not
  interpolated onto the line. The gap between the two is the actual routing and is more
  interesting than a bead on a wire.
- Terminator: shade the night side, draw the boundary as a thin warm line. "They are
  flying into the night" is information an arc cannot carry.

### Worked example **[MEASURED]**

DEN → DEL is a **111.6° great circle with its midpoint at 83.9°N, 88.6°E** — over the
pole, north of Siberia. The arc face can only say "42%." This is the case that justifies
the feature.

### Scale — the hard part

The scale that makes DEN → DEL beautiful makes PDX → SEA invisible. **[MEASURED]**, all
at r = 119:

| Route | Distance | Arc on screen |
|---|---|---|
| DEN → DEL | 12,406 km | 197 px |
| SEA → LAX | 1,537 km | 29 px |
| PDX → SEA | 208 km | **3.9 px** |

Most aircraft overhead are short-haul or GA. A fixed-radius globe fails the majority case.

`FlightAnimation.cpp` already documents that **tilt cannot rescue this**:

> "Do not. It cannot work, and the geometry says so in closed form... phi is a real lever
> — it just has a ceiling... The two scenarios that need help are exactly the two the
> ceiling binds on: no tilt reaches even 16 px on SHORT."

That comment is about tilt. **Radius is a different lever and it does work.**

### Two fixed scales, chosen automatically **[PROPOSAL]**

| Mode | Radius | Use when | Character |
|---|---|---|---|
| **Globe** | 94–119 | long-haul | limb visible, reads as a planet |
| **Regional chart** | ~3,700 | short/medium | limb off-screen, reads as a chart |

At r = 3,700 a 208 km route spans **121 px** **[MEASURED]** and the visible window is
about 3.7° ≈ 413 km.

**Do not build a continuous zoom.** Two scales, selected by great-circle distance, no user
control. The threshold is **[DECIDED 2026-08-27: 4,000 km]** — derived from
`2·R·sin(θ/2) = 60 px` at R = 94, then rounded down so the US transcon family does not
straddle it. See the findings box at the head of §9.

### Regional chart needs different data

The 0.5° decimation is sized to be ~1 px at r = 119. At r = 3,700 those same vertices are
**32 px apart** — Puget Sound renders as a triangle. A regional view needs a finer set.

**You do not need a worldwide fine set.** The user tells us where they are during setup and
we run a Worker. Ship fine coastline, state and border data for a window around their
location, fetched once and refreshed over OTA. The Pacific Northwest at intermediate
resolution is **2,585 coastline points plus 1,180 state-boundary points** **[MEASURED]** —
call it 15 KB for someone's entire flying-visible world.

Draw political lines **dimmer than coastline**, and as a separate pass. At regional scale
near Portland the Washington–Oregon border *is* the Columbia River, so the two reinforce
each other rather than competing — but only if the coast stays dominant.

---

## 10. Local face — the flight school regime

**Units.** Every distance Follow renders — the local face's range readout, the
arc face's primary readout (§8), the post-flight card (§11) and any ntfy body —
uses the device's configured distance unit via `include/DisplayUnits.h`. Follow
introduces no unit of its own and no second conversion; a follow readout in
different units from the radar behind it would read as a bug.

`nmi` exists as of 2026-08-26 and is the natural **suggestion** for anyone setting
up a follow — a feature built for watching a specific pilot should speak that
pilot's unit. Make it a suggestion in the UI copy next to the follow field, not a
changed default: the device-wide default is `mi`, it is set for the whole product,
and Follow silently repointing it would surprise someone who never asked.

**This is the regime that ships first (§1.1).**

### The radar card already names this regime, and Follow should reuse the phrase

As of the CC0 route mirror (#260), a route whose origin equals its destination is
**data, not a defect** — 39 such callsigns exist in the 619,103-row table and every
one is a real circular flight: RAF Cranwell circuits, Nice sightseeing runs, survey
patterns. The rev-3 endpoint rule guarantees the property directly (`o == d` **iff**
every leg is the same field), so the device can trust `o == d` as *"this aircraft
came back to where it started"* without any further check. That is what makes the
render decision safe to make on-device from two three-letter codes.

The radar's detail card therefore renders those as **`Local flight: EGYD`** rather
than `EGYD -> EGYD`, which read as the manufactured self-loop the rule exists to
prevent. Two notes for whoever builds this face:

- **Follow's local face should use the same words.** A pilot doing circuits sees
  `Local flight` on the card that launched the follow; a different phrase on the
  follow face would read as a different concept. This is one of the §7.2
  "reuse, do not rebuild" cases, at the level of copy rather than code.
- **The airport is a CODE, not a name.** The device carries `include/Airports.h`
  — ~250 IATA codes and coordinates, no names — and §10 above is explicit that the
  local face adds no dataset. So it is `Local flight: EGYD`, never
  `Local flight: Cranwell`. If names are ever wanted they come from the Worker as
  an enrich field, not from a baked table; that is a separate decision with a
  flash cost, and nothing in Follow needs it.

`ON THE GROUND` and `Local flight` are not the same statement and must not merge:
one is where the aircraft is now, the other is what the whole flight was.

### Why the arc face does not transfer

A Cessna doing circuits at the local field has **no origin/destination pair, no progress,
and no meaningful bearing** — it is right there, and it comes back every four minutes. A
progress arc from 7:30 to 4:30 has nothing to represent.

### Why geography does not transfer either

A circuit is roughly 2–5 km across. Fitting 5 km to 120 px needs a radius around
**153,000 px**, at which one pixel is about 20 m. Coastlines are meaningless at that
scale; the reference you would actually want is runways and taxiways, which the device
does not carry and should not.

**So the local face draws no map at all.** This is good news: no dataset, no Worker
delivery, no zoom dilemma, no licence question. **The trail is the picture.**

### Composition **[BUILT 2026-08-27]**

A radar scope, which is what the product already is:

- **Home field** as a fixed marker — the anchor the whole view is built around. Per C5
  this is a lookup: which field, and its published elevation, both known at boot.
- **Range rings**, auto-scaled to the track's bounding box, with live labels (1 / 2 / 5 km)
- **The track**, decimated by distance per §4.1
- **Current position** with heading
- **Readouts**: altitude **AGL** (real threshold, per C5) and **circuit count**

> **What shipped, and the two places it is honestly less than the list above.**
> `AircraftManager::DrawFollowLocalFace`, with the arithmetic in
> `include/FollowGeometry.h` (pure, host-tested).
>
> **The half of C5 that *is* available today was used.** "Which field" needs no
> new dataset: the airports overlay is already fetched for the radar and
> `include/Airports.h` is baked on every board, so the marker carries the nearest
> field's **code** within 12 km — and `HOME` when nothing is close enough to
> claim, rather than a wrong code under the anchor of the whole view. It is the
> code and never a name, per the note above. Resolved once, dropped when the
> configured location changes.
>
> 1. **Altitude is MSL, labelled `ft MSL`, not AGL.** C5's delivery half does not
>    exist, so the field elevation is unknown; the render says the number the
>    feed carries and names it correctly. An AGL-shaped number would be wrong by
>    the field elevation — 3,460 ft at Bend — and a readout claiming a trainer is
>    at 4,400 ft AGL in the circuit is worse than one that declines to say. When
>    C5 lands this becomes one branch.
> 2. **No circuit count.** §11 defers it in the same breath §10 asks for it, and
>    §18.3 says every constant is a guess until a real lesson is logged. A wrong
>    count is a *claim* the customer has no way to check, so the slot is empty
>    rather than confident. The reasoning is written where someone would go to
>    add it — the bottom of `FollowGeometry.h`.
>
> **The ladder is walked in the customer's unit, not in kilometres.** A step that
> is round in km is 0.62 / 1.24 / 3.11 in miles, and a ring labelled 3.11 is a
> ring nobody reads. One conversion out and one back, both through
> `include/DisplayUnits.h`.
>
> **Screen integration, per C6 and §13.3.** One slot, hidden entirely when the
> follow field is empty — so the swipe cycle and the page dots walk *visible*
> screens rather than counting to four, and a collection customer who never uses
> Follow inherits nothing. It auto-surfaces for a 20 s dwell on **takeoff and
> landing only** (never on an absence — a dropout is the normal operating
> condition at pattern altitude, and a screen that jumped on every one would be
> the device looking broken every circuit in a louder way than copy could fix),
> and a swipe cancels the dwell. The followed contact gets its own ring on the
> radar in the track's blue — deliberately not the watchlist amber, because
> "watchlisted" and "this is the aeroplane my son is flying" are not the same
> statement.
>
> **BENCH EYEBALL PASS 2026-08-27 (Daniel).** Composition reads correctly — home
> diamond centred, auto-scaled rings with labels, the track, the aircraft marker
> with heading ring, `AIRBORNE` with the callsign above. Six findings, all
> addressed:
>
> 1. **The bottom readout ran off both ends of the curve.** A round panel has no
>    edge to clip against — glyphs run off the glass and it looks identical to a
>    bug. At `SCREEN_SIZE-26` the chord is **118 px, nineteen characters**, and
>    `-900 ft MSL  67 kt  148mi` is twenty-five. This was the *second* surface
>    with the defect (the Stats SSID row was the first), so the rule moved to
>    `include/DiscGeometry.h` and both call sites use the one copy. The readout
>    now appends fields **while they still fit** rather than ellipsising one long
>    string — a truncated `14...` reads as a value, an absent field does not.
> 2. **`-900 ft MSL` printed under an `AIRBORNE` headline without complaint.**
>    Bounds alone do not catch it: −900 ft is legal somewhere (Bar Yehuda is a
>    real airfield at −1,266 ft). What catches it is that the altitude and the
>    state **contradict each other** — so `ReportableAltFt` declines when
>    airborne and at-or-below sea level, and the row renders `-- ft MSL`. Same
>    rule as `AglFt()` returning NaN: decline over a plausible wrong number.
> 3. **The chosen ring scale is now on serial** (`[follow] rings=3 x 50.00mi
>    outer=241.4km`), so a real 1–5 mi circuit can be checked against a number
>    rather than measured off a photograph.
> 4. **The trail reading as dots is the scale, not a defect** — and the serial
>    line now says so instead of leaving it to be inferred. `seg_px` is the mean
>    on-screen distance between consecutive *drawn* points. The decimation is
>    150 m of **flown path** (§4.1), so at the synthetic 150 mi ring that is
>    kilometres per pixel and the laps separate; at a 2 mi circuit ring the same
>    buffer draws a connected racetrack, which is what §11's keepsake needs.
>    Watch `seg_px` on the first real lesson rather than trusting this paragraph.
> 5. **The bench self-enable masked the §4.3 disable path.** It fired on every
>    `Initialise`, and `Initialise` re-runs on every config save — so clearing
>    the follow field fell straight back to the synthetic target and the
>    allocation was never freed. Now **armed once per boot**: one auto-enable so
>    the face has something to draw, after which an empty field means the owner
>    cleared it. The disable path is reachable on the bench image.
> 6. **The absence copy could not be judged** without pulling the router.
>    `FOLLOW_BENCH` now takes a serial key to force the displayed state
>    (`1`–`5`, `n` to cycle, `0` to release). Display only — `followMachine` is
>    untouched and the transitions stay exactly as the host suite grades them.
>    §6 is the emotional core of the feature and it is the one part that can only
>    be judged by a person looking at the panel.
>
> Orientation was confirmed correct (the device was physically rotated 90° for
> the photographs).

> **Not reachable on a shipping build yet**, and that is the correct sequencing
> rather than an omission: the `follow` config field is §19 item 5. The bench env
> (`follow-bench-s3-128`) self-enables and fills a synthetic 1024-point track, so
> the face's geometry can be looked at exactly the way the §18.1 draw cost was.

### Auto-scaling is correct here, and only here

§9 says do not build continuous zoom. That rule is about **sampled geography**, where
zooming past the decimation exposes it. Here the reference is **generated** — rings drawn
at whatever radius the data needs — so continuous auto-fit has no failure mode. Scale the
view to the track and label the rings honestly. The two rules do not conflict; they apply
to different kinds of reference.

### States differ, and one matters a lot

At pattern altitude, network ADS-B coverage is patchy and dropouts are constant and
benign. **A local dropout must not use the alarming copy.** It is the local analogue of
`APPROACH_LOST`: expected, not a fault. `BELOW COVERAGE`, not `SIGNAL LOST`. Getting this
backwards makes the device look broken every single circuit.

`ON THE GROUND` is a real state here and needs saying rather than inferring from a frozen
dot.

### The receiver connection

This is the regime that most needs a local ADS-B receiver — the product page already says
a receiver picks up "aircraft too low or close for the networks to cover," and circuits at
the local field are exactly that traffic. When a followed local aircraft drops coverage
repeatedly, that is honest, useful context for suggesting a receiver.

**Put that on the config page, not on the device.** A scope that nags is a scope people
stop looking at.

---

## 11. The post-flight card

The answer to "the screen is empty most of the week." On `LANDED`, freeze a summary and
show it until the next takeoff: duration, max altitude, top speed, furthest point — and
the shape of the flight.

> **BUILT 2026-08-27**, bench-unverified. `include/FollowLog.h` (the store) plus
> `follow::FlightStats` in `FollowState.h` (the numbers, pure and host-tested)
> and `AircraftManager::DrawFollowPostFlightCard`.
>
> **One write per flight by construction, not by debounce.** The save happens on
> the `LANDED` transition and nowhere else, so there is nothing to tune and
> nothing to trust. Logbook debounces because it is written continuously by a
> running sky; a flight ends once. What makes it safe to write at all is §5.4's
> rail — `LANDED` fires only on confident evidence, so a card can never appear
> for a flight that merely stopped being heard.
>
> **The last slot is reserved.** The stride is computed against `POINTS - 1` and
> the final track point is always appended, because it is where he touched down
> and this section draws it filled. On a full 1024-point buffer the stride is 9
> and the walk ends at index 1017 — without the reservation the destination
> marker sits about a kilometre short of the runway, which is invisible once
> drawn: a track that ends *near* the field looks exactly like one that ends
> *at* it.
>
> **Two things the numbers get right that are easy to get wrong**, both pinned by
> host tests with the wrong answer as the control:
>
> - The clock starts at the first **airborne** fix. An aeroplane that sat on the
>   apron with its transponder on for forty minutes did not fly for forty
>   minutes.
> - The maxima never fall back. He lands low and slow, so the last fix of every
>   flight is the smallest one — an end-of-flight snapshot would record a top
>   speed of 45 kt for every flight ever made.
>
> Plus the `millis()` wrap, which is right by unsigned subtraction and does not
> look it.
>
> **Altitude is MSL, labelled.** Same reason as the local face, and more
> sharply here: a souvenir is the one face with nothing live beside it to
> contradict a wrong figure. **The date is quoted only when the clock was real** —
> an unsynced device records epoch 0 and the card says "time not known" rather
> than 1970.
>
> **North-up always.** `radar-up` deliberately does not apply: the card is a
> record, not a view out of a window, and rotating a souvenir by a setting made
> for live traffic would make the same flight look different on two devices.
>
> **The card is cleared when the follow target changes to a different
> aircraft.** A previous aeroplane's flight is not this aeroplane's history, and
> a souvenir attributed to the wrong aircraft is the single way this face can be
> wrong — which is the entire reason it is allowed to persist at all.

Redrawn as **shape rather than position**. No arc, no bearing, no live data — nothing here
can be wrong, which is why it is the only part of Follow that should survive a power
cycle. Origin hollow, destination filled, duration and distance below.

### Where the shape lives

The live track buffer is reset on the new-flight transition, so the card cannot read from
it. **Chosen: persist a 128-point decimated copy to NVS**, own namespace `follow-log`,
mirroring `Logbook`'s discipline — bounded store, debounced write, one write per flight.

    128 points x 8 B (lat/lon as scaled int32) = 1,024 B

The alternative was to drop shape and show four numbers. Rejected because the shape *is*
the emotional payload: a racetrack of circuits is the picture that says "he practised
landings today" without a word of text. Four numbers is a readout; the shape is a
souvenir.

128 points is enough to read a circuit pattern at card size, and small enough to sit
inside the existing NVS budget without competing with the config namespace.

**Local version:** not a great-circle track — the **pattern shape**, plus the circuit
count. Six touch-and-goes drawn as six overlapping racetracks is a better keepsake than
any number.

**Circuit counting is deferred.** It is the most charming number in the feature and the
one most likely to be wrong on first contact with real data. Build the state machine so
the altitude history exists, then look at an actual logged lesson before deciding what a
circuit is.

---

## 12. Choosing what to follow

### 12.1 The constraint

A callsign is 4–8 alphanumeric characters. A full keyboard on a 240 px round panel gives
~25 px keys against a ~40 px fingertip, on a display with no corners. **Do not build a
keyboard.**

### 12.2 Primary path: the browser **[PROPOSAL]**

The product already configures in a browser — "join the hotspot, open its address, paste
in your coordinates." Typing `UAL2119` on a phone keyboard takes three seconds. **Add a
Follow field to the existing config page.** This costs almost nothing and it is where the
product's own setup philosophy already points.

### 12.3 Two cheaper wins, worth doing first

- **"Follow" on the aircraft card.** Anything already on the radar can be followed with
  one tap. No new input mechanism at all. This covers "follow that one overhead"
  completely.
- **Recents.** The people you follow repeat — the same relatives on the same routes. A
  short recents list makes the second follow a single tap and removes entry from the
  common case entirely.

Between them these two probably cover most real use, which is an argument for building
them **before** any picker rather than after.

### 12.4 On-device picker — BLOCKED on C2 **[PROPOSAL]**

The round panel has an affordance a rectangle doesn't, and this repo has already probed
it: `GestureProbe.cpp` defines a bezel band at `kBandInner = 0.72f`,
`kBandOuter = 1.02f`, and the game build runs a press-drag arc along it via the D3
recogniser. **That ring is a rotary input**, and a rotary input is the correct instrument
for picking from an ordered set. Setting a watch, not typing.

Three steps, no free-text entry: airline (scrolled, ordered by what the device has
actually seen overhead) → flight number digits, tap centre to commit each → confirm.

**The confirm step is what C2 blocks.** It sends the assembled callsign to the Worker for
validation, which the privacy invariant forbids. Resolve C2 before building this.

**[UNKNOWN]** whether D3 can be lifted out of the game build, or whether the bezel
recogniser needs reimplementing for the radar firmware.

### 12.5 Card integration — tap the route row **[PROPOSAL]**

The aircraft card already prints the route as a line of text. **Make that line the
control.** Tap it, the map opens at the scale the distance selects. Tap again to return.

Why not a swipe: a swipe is invisible until someone is told it exists, and the product
ships with a quick-start card, not a manual. The user tapped an aircraft to learn about
it; tapping the route to see the route is the same move.

**It also gates itself.** No route filed means no row, means no affordance, means no empty
map and no error state to design. A swipe would still exist and would still have to fail
politely at something.

Requirements:

- **Touch target ≈ 200 × 42 px** — the whole row plus padding, not the glyphs. The ink is
  ~13 px tall; a fingertip needs about 40. Get this wrong and it works for whoever tested
  it and for nobody else.
- **It must look tappable.** Route row in the accent colour rather than label grey, plus a
  chevron at the right edge.
- **Scale is automatic.** No zoom control, no reason to ask.

**Before building [UNKNOWN]:** establish what the aircraft card responds to *today* — tap
where, swipe which directions, how is it dismissed. `TouchPoll.h` is raw touch presence
only (`Skipped / Idle / Touched`) and `GestureProbe.cpp` explicitly refuses to interpret
gestures, pointing at the D3 recogniser in the game build. If tap-anywhere already
dismisses the card, the row must swallow that event, and that changes the design.

---

## 13. Follow and the collection game

The insert card is being printed around collection. Follow is the opposite emotional
product — breadth and novelty versus one specific airplane — and they share one screen.

### 13.1 Does the followed aircraft count toward collection?

**Yes, with no special-casing at all.** Claiming is per *type*, once (`Logbook`'s
`claimDay`), so a followed C172 contributes exactly one type claim ever, however many
times it flies. The odometer and per-type sighting count tick each session, which is
honest.

One wrinkle: a trainer flying overhead daily will likely own the "closest contact ever"
record permanently. Left alone deliberately — a lifetime record set by your brother's
airplane is charming, not broken.

The reasoning is about explainability rather than implementation cost: **the insert card
has to state the collection rules in about one sentence**, and two mechanics that quietly
modify each other cannot be described that briefly.

### 13.2 Does an active follow suppress or reorder other alerts?

**Reorder, never suppress.** `ProcessAlerts` currently runs emergency > overhead >
watchlist/military, one per tick. Follow slots in as its own class:

    emergency  >  follow  >  overhead  >  watchlist / military

Follow outranks everything except emergency because it is rare (twice a flight) and it is
the thing the owner asked for **by name**. Emergency still outranks it because that is a
safety event about somebody else's aeroplane.

Suppressing a customer's other alerts because they also turned this on would be a silent
degradation of a feature they explicitly enabled — the same class of defect as the early
return in `ProcessAlerts` that made the ntfy topic look broken.

### 13.3 Who wins the screen?

**Follow gets a screen. It never gets *the* screen.**

- Follow is a **fourth screen** (Radar / List / Stats / Follow) — one slot, faces chosen
  per §7.1.
- **Hidden entirely when no follow target is set**, the way the other editions skip empty
  feeds. A collection customer who never uses this must not inherit a dead screen.
- It auto-surfaces **only on a state transition** — takeoff, landing — for a dwell period,
  then returns to wherever the owner was.
- While the followed aircraft is in range it gets a **distinct ring on the radar**, so it
  is findable without leaving the collection view.

So when the followed aircraft is airborne and something rare flies over: the rare aircraft
keeps its NEW highlight, the followed aircraft keeps its ring, and neither steals the
display. The only thing that ever takes the screen is a follow *transition*, twice a
flight.

---

## 14. Config surface

A new `<details>` block of its own, not crammed into Watchlist & alerts.

| key | type | purpose |
|---|---|---|
| `follow` | text — tail / callsign / hex | the aircraft to follow |
| `follow-track` | toggle | draw the long track |
| `follow-up` | toggle | ntfy on airborne |
| `follow-down` | toggle | ntfy on landed |
| `follow-lost` | toggle | ntfy on `SIGNAL_LOST` |

Matching reuses the `WatchClass::Specific` path in `MatchesWatchlist` — identity already
outranks type there, which is the semantics we want.

### 14.1 The ntfy topic becomes device-generated

Follow changes what an ntfy topic *is*. A topic carrying "military flyover" is a hobby
feed. A topic carrying **"N4523K is airborne"** is a named person's movements, published
to a service where anyone who guesses the topic can read it.

A user-typed topic will be short and guessable, because that is what people type.

So: **the device generates the topic, and the field ships pre-filled rather than blank.**

- Generated at first boot from `esp_random()` — the hardware RNG, explicitly **not**
  anything seeded from `millis()`. A fleet booting through a similar sequence would
  produce a correlated, guessable set of topics, which is precisely the failure this
  prevents.
- Format `blip-<10 chars base32>`, roughly 50 bits. Nobody but the owner ever types it.
- **Persisted at generation**, not at first save, so the page shows a stable value across
  reloads.
- The owner can still overwrite it — some people legitimately want one shared topic across
  devices — but the default is generated.
- A "regenerate" affordance beside it, since the honest advice on a leaked topic is to
  change it.
- The warning is **inline, next to the field**, not only on the support page. The mistake
  is made at the config page, so that is where the warning belongs.

---

## 15. Defaults, and why each one

New keys freeze the moment anyone saves the form, and setting a location is a whole-form
save that every device must do. **So today's defaults are the defaults for everyone who
ever owns this**, and changing one later costs a `cfg-rev` bump plus a migration in
`include/ConfigMigration.h`. Cheap now, expensive in a month.

| key | default | why |
|---|---|---|
| `follow` | empty | Gates everything. Empty means the Follow screen is hidden and no new behaviour reaches anyone who did not ask for it. |
| `follow-track` | **on**, conditionally | If you have named an aircraft, the track is the reason to look at the device. **Conditional on the §18.1 draw-cost measurement** — if the track is expensive this becomes "on while the Follow screen is visible", which is a behaviour change rather than a default change. |
| `follow-up` | on | Cheap emotionally, and it sets the expectation that makes the landing message legible. A landing alert with no takeoff alert arrives without context. |
| `follow-down` | on | The message the entire feature exists to send. |
| `follow-lost` | **off** | The asymmetry is the argument. A missed lost-alert costs mild worry; an unwanted one costs panic. The screen always shows the state; the phone only if asked. |

`follow-up` and `follow-down` defaulting on is only reasonable *because* consent here is
explicit and specific: someone typed an aircraft identifier into a field labelled Follow.
That is not an inferred preference.

---

## 16. Data and provenance

### 16.1 Routes and airports — RESOLVED 2026-08-25

The licensing question is retired rather than answered, which is a better outcome than the
answer would have been.

**Why "prefer routeset" was a dead end.** adsb.lol's `/api/0/routeset` has returned
`201 Created` with an empty body since 2026-07-08 — seven weeks — and it is not a contract
change. Their live OpenAPI (at `/api/openapi.json`; the conventional `/openapi.json` is a
404) still documents the endpoint, undeprecated, with exactly the request shape we send and
a documented 200. A deliberately invalid body also returns 201/empty where the documented
behaviour is 422 — so nothing is parsing requests and the application is not being reached
at all. Broken upstream, silently, and we are a paying sponsor who is not contacting them
again about it.

**The route data does not need their API.** It is published as static files built from
`vradarserver/standing-data`, which carries **CC0 1.0 Universal** — verified by reading
`LICENSE` (7,048 B), which names *"a database"* and *"including without limitation
commercial purposes"* explicitly. Public domain dedication, no permission needed, no rate
limit to negotiate.

**The plan, approved 2026-08-25:**

1. **Build the CSVs ourselves from the sharded CC0 source** (`routes/schema-01/[A-Z]/…`),
   NOT from `vrs-standing-data.adsb.lol`. That combined CSV is a build artifact of the same
   operation whose API died silently for seven weeks; depending on it reintroduces exactly
   the dependency being removed, and it would fail the same quiet way.
2. **Mirror it ourselves** — 619,103 routes, 34,128 airports. Daily refresh on
   `Last-Modified`, staged load, atomic cutover. A failed refresh never touches live data.
   **Store: KV for routes; the airports overlay is undecided — see C1.** (The design note's
   "D1" is stale.)
3. **Freshness AND a row-count band** on `/healthz` and in smoke-prod. Age alone would
   repeat the routeset failure: a successful fetch of a truncated file refreshes the
   timestamp while destroying the data. Plus a canary through the live path — prove
   presence is observable before believing an absence.
4. **adsbdb leaves the stack entirely**, both halves — the Worker fallback *and* the
   firmware's direct calls at `AircraftManager.cpp:338` and `:386`. Those never appeared in
   the measured 1,170/day because they originate from customer home IPs, so the exposure
   was understated. BYO is about **positions** — a local receiver, one-second updates, a
   radar that survives an internet outage. It was never about enrichment. Those two call
   sites point at our Worker, and cards degrade during an outage while the radar keeps
   working, which is already exactly how photos behave.
5. Cite CC0 upstream as the licence basis and keep the `/credits` entry even though CC0
   requires no attribution.

**Status as of 2026-08-25:** mirror loaded and verified in staging — 619,103 route keys,
1,575 shards, sentinel provenance proved, diff path exercised at 0 changed keys. Production
ingest pending. **The firmware image must contain no adsbdb call site before flashing.**

### 16.2 Airport table

`include/Airports.h` currently holds ~250 curated world airports as
`{ float lat; float lon; char code[4]; }`, ~3 KB. Its header warns that "a WRONG position
is a bug," which is the correct instinct: when an endpoint is missing, **show the codes and
admit the map cannot be drawn — never guess a position.**

The 34,128-row CSV supersedes it, which removes the "not plottable" state entirely (C5).

---

## 17. The privacy invariant needs a test, not a discipline

**The follow target must never leave the device**, except in the ntfy notification body,
which is the one channel the owner explicitly opted into by naming an aircraft (C3).

A tail number tied to a named person is a different class of data from a type code. It must
not appear in:

- the leaderboard submission
- the enrollment payload
- any feed or enrich request
- OTA / operational telemetry headers
- serial output (the Wi-Fi password incident is the precedent)

A rule that lives only in a comment gets violated in six months by someone who does not
know it exists. So it gets a check.

**Strong form — a host test over extracted pure builders.** The payload builders currently
live inside `AircraftManager.cpp` and need Arduino, so they are not host-testable as
written. Extracting each into a pure function that takes its inputs and returns a string is
a prerequisite — and is independently good design, which is why it is the recommended path
rather than a tax. The test sets a distinctive follow value and asserts it appears in none
of the produced payloads.

**Weak form — a grep guard in CI**, asserting the follow field name appears in no
translation unit that builds an outbound body. Cheap, catches the careless case. It is a
**weaker check and must be labelled as one**: it tests the source rather than the artifact,
which is the exact substitution this repo has been bitten by before.

Recommendation: build the strong form. If the extraction turns out large, land the grep
guard first so the window is covered, and say plainly in the PR that it is the weaker check.

> **A GUARD LANDED 2026-08-27, AND IT FOUND A REAL LEAK ON ITS FIRST RUN.**
>
> `scripts/check_follow_privacy.py`, in CI and in the host suite. **This is the
> WEAK form** and its own header says so: it reads the source, not the artifact,
> which is the exact substitution this repo has been bitten by before. The strong
> form is still owed.
>
> What it found: `Serial.printf("[follow] target=%s", followTarget.c_str())`,
> shipped in the stage-1 commit, in a file whose header states the target must
> never be printed. 17 lists serial output among the forbidden places with the
> Wi-Fi password incident as the precedent. Nobody caught it by rereading the
> line, including the person who wrote the header two commits later.
>
> **Two checks, and only one is an assertion:**
>
> | | rule | allow list |
> |---|---|---|
> | per statement | the target and an outbound sink in the same statement | **none** |
> | per function | touches the target anywhere, reaches a sink anywhere | two, and they are a REVIEW RECORD |
>
> The second deliberately over-flags -- `Initialise` is enormous and contains
> both for unrelated reasons -- and that is the point: the list is what somebody
> looked at, compared whole rather than by substring absence.
>
> **The guard was wrong three times before it was right, and every one of them
> was caught by its own anchor control** (the branch that fails when the review
> list names functions the scanner can no longer find). Recorded because the
> third is the one that matters:
>
> 1. Brace-depth attribution drifted on braces inside string literals; the scan
>    reported 2 functions instead of 12, which without the anchor would have read
>    as a very clean pass.
> 2. A regex written through a shell heredoc lost a backslash and compiled with a
>    literal **backspace** on the end. `grep` showed the line looking perfectly
>    correct, because a terminal renders a backspace by not rendering it.
> 3. **The per-line check passed against the actual bug.** The selftest planted
>    the violation on one line; the real one is a `printf` with four arguments,
>    so the format string and `followTarget.c_str()` sit on consecutive lines.
>    Rehearsing against the REAL shape -- not the imagined one -- is what caught
>    it, and the fix was to join statements rather than lines. This is the launch
>    gate's `#charlie-retired` again, in Python, inside the tool written to
>    prevent that class of mistake.
>
> Consequence for the code, worth stating because it looks like fussiness: the
> `[follow]` health line lifts `followTarget.length()` into a local before the
> `printf`. The rule is absolute -- the token never appears in a statement that
> reaches a sink -- because a rule with *"unless it is only the length"* in it
> needs a reader to judge `.length()` against `.substring()`, and a rule that
> needs judgement is one that gets judged wrong.
>
> **The rig for the strong form also exists as of 2026-08-27.**
> `test/host/test_follow_state.cpp` runs in `test/host/run.sh` against
> `FollowState.h` and `FollowGeometry.h`, both of which now compile with
> `-I src/game` and **nothing else on the include path** — so the purity claim is
> enforced by the same gate as everything else in that suite rather than asserted
> in a comment. Putting `#include <Arduino.h>` back into either header stops the
> suite compiling.
>
> What remains for §17 itself is the **payload builders**, which still live
> inside `AircraftManager.cpp`. The test to write sets a distinctive follow value
> and asserts it appears in none of them. One sanctioned exception is already
> pinned by the existing suite: `AlertTitle` carries the tail, per C3, and the
> test asserts it does — an invariant with no positive case is one nobody can
> tell from a broken extractor.
>
> Serial output is in scope and is already respected: the `[follow]` health line
> prints the state, the home code and the two knowledge flags, and deliberately
> **not the target**.

**C2 is a live conflict with this section.** Resolve it before the picker is built.

---

## 18. What to measure, and when

Not this week. v8 is in the release train and COM119/COM16 are mid-soak on the TLS PSRAM
A/B. **No board comes off a soak for a feature that is not launching.**

When hardware frees up, in this order:

1. **Draw cost of a 256-segment track** against the 27–31 ms budget, on the s3-128, under
   a full contact table. This decides whether Follow is a track product or a notification
   product. Everything else is contingent on it. Geography is already known at 0.34 ms;
   the track is not.
2. **A follow-enabled vs control heap soak**, read against `psram_free`, the `tlsmem`
   fallback count, `largest` and `allocFail` (§4.4). Confirms the allocation discipline
   behaves as specified rather than as reasoned.
3. **Log one real training flight** through the existing feed and look at the dropout
   pattern before fixing `TRACK_LOST_MS`, `AIRBORNE_CONFIRM_FIXES` and the home-radius
   threshold. Every constant in §5.3 is a guess until that exists, and guessing them from
   first principles is how you get a state machine that is elegant and wrong.

---

## 19. Build order

Local regime first (§1.1). Within it:

1. **Measure the track draw cost** (§18.1). Everything depends on it.
2. **The state machine and the copy** — the feature is the states, not the picture.
   Includes the pre-departure state once C4 is designed. **BUILT**; the copy became
   regime-dependent in stage 2 exactly as its own note predicted (box at §6).
3. **Local face** (§10) — no external data, no Worker surface, no licence question.
   **BUILT 2026-08-27**, bench-unverified. See the box at §10 for what shipped and
   the two readouts that are honestly less than the list there.
4. **Post-flight card, local version** (§11). **BUILT 2026-08-27**, bench-unverified.
   See the box at 11. Circuit counting stays deferred.
5. **Config surface and the generated ntfy topic** (§14). **BUILT 2026-08-27** —
   its own `<details>` block, five keys, the §15 defaults, and the topic generated
   from `esp_random()` at first boot with a regenerate affordance beside it. The
   three alert toggles fire; `APPROACH_LOST` rides the *landed* toggle rather than
   the *lost* one, because it is the probably-landed case and somebody who declined
   the alarming alert must not receive it under a different name.
6. **The privacy test** (§17), landed alongside or before anything that builds a payload.
   **WEAK FORM BUILT 2026-08-27** — see the box at §17. It found a real leak on its
   first run. The strong form (a host test over extracted pure payload builders) is
   still owed.

Then the airline regime, gated on the production mirror cutover:

7. **Emit an airport family from the ingest** — the delivery half of C5, and C1's
   concrete form. `AltitudeFeet` for 34,128 fields is in the CC0 corpus and no
   running code reads it: the ingest writes `rt:` keys only. Landed detection needs
   `geoAltitude` against published field elevation, so this precedes anything that
   claims to detect a landing away from home.
   **Decide the store with the access pattern, not by symmetry with routes:** `ap:`
   keys in KV if lookup-by-code is sufficient, the D1 side if §7.3's priority-sorted
   nearest-N overlay forces a query. KV cannot answer nearest-N.
   *Not required for stage 1* — the local regime's home field is a single airport
   carried by the existing config flow.
8. **Arc face and the four contact states** (§8). **BUILT 2026-08-27**,
   bench-unverified, against the **baked ~250-airport table** rather than step 7 —
   the majors cover the overwhelming majority of airline city pairs, an unresolved
   code degrades to an honest code-only arc, and `LookupAirport` is the one seam
   `ap:` widens later without touching the face. C4's pre-departure face landed
   with it. See the box at §8 for the two deviations and the one colour question
   left for the bench.
9. **Post-flight card, great-circle version.**
10. **Globe face**, long-haul only (§9). **BUILT 2026-08-27**, bench-unverified.
    Required extracting the projection and coastline set out of `src/anim/` first
    (§7.2's "lift it" was not a re-include — everything it named had internal
    linkage). Threshold 4,000 km, argued at §9. Pinned against pre-extraction
    output in `test/host/test_globe_proj.cpp`.
11. **Regional chart** plus the Worker-delivered regional dataset.
12. **Card integration**, once the gesture question is answered (§12.5).
13. **On-device picker** — CLOSED for stage 1 by the C2 decision (option 3). Reopen
    only with a resolution that keeps §17 testable as string-absence.

Steps 8–13 are worthless without step 2. **Ship honest states before pretty ones.**

---

## 20. Non-goals

- Do **not** cache geography into a PSRAM sprite. Measured slower.
- Do **not** write a new projection. `GlobePt()` exists and is correct.
- Do **not** let Follow or the globe take the radar. Fourth screen, never the first.
- Do **not** build continuous zoom, page dots, or a zoom control for sampled geography.
  (Auto-fit for the local face is a different thing — §10.)
- Do **not** show an empty globe. Absence gets a state or gets no affordance.
- Do **not** build a keyboard on the round panel.
- Do **not** build this feature on adsbdb.

---

## 21. Open questions

Carried from both sources, plus what the merge produced.

**From the merge (§2):**

- **C1** — does the airports overlay live in D1 while routes live in KV?
- **C2** — callsign validation vs the privacy invariant. **Blocking for the picker.**
- **C4** — pre-departure needs a face and copy.

**Carried forward:**

- Does the home-field approach survive a student who trains at two fields? Probably wants a
  small set rather than a single point.
- What happens when the followed tail is sold, or the school reassigns the aircraft? The
  device would happily follow a stranger. Worth a "this aircraft has not flown in N days"
  nudge rather than silence.
- Airline regime: how is today's flight number set for a pilot whose callsign changes
  daily? The charming answer is a link he opens from his phone before pushback, hanging off
  the enrollment identity — but that depends on the airline regime existing, and it
  intersects C2.
- Does `follow` want to accept more than one aircraft? Two siblings both learning, a family
  with two pilots. The data structures do not care; the screen does.
- The globe/regional scale threshold **[UNKNOWN]** — argue the number, do not pick it.
- Whether D3 can be lifted out of the game build **[UNKNOWN]**.
- What the aircraft card responds to today **[UNKNOWN]** — blocks §12.5.
