#pragma once

#include "Aircraft.h"

struct TrackedAircraft {
    Aircraft state;
    unsigned long lastSeen;

    // blending state
    float blendFromLat = 0.0f;
    float blendFromLon = 0.0f;
    float blendAlpha = 1.0f;  // 1.0 = blend complete, no interpolation active

    unsigned long lastTick = 0;

    // adsbdb.com enrichment: type/operator/registration aren't in the OpenSky
    // feed, so they're looked up once per aircraft by ICAO address and cached
    // here. NotFetched -> the manager may queue a lookup; Fetching -> a request is
    // outstanding on the enrichment task (don't re-queue); Fetched -> a definitive
    // answer arrived (which can still leave the strings empty for aircraft adsbdb
    // doesn't know), so it's never looked up again.
    // Dead-reckoning horizon. Past this a contact is frozen, not flying -- see
    // PredictPosition. AircraftManager::CurrentStaleStage keys its NoData stage off
    // this exact value so the two can never disagree.
    static constexpr float MAX_DR_SECONDS = 600.0f;

    enum class MetadataState : uint8_t { NotFetched, Fetching, Fetched };
    MetadataState metadataState = MetadataState::NotFetched;
    // A transient lookup failure returns the aircraft to NotFetched; without a
    // cooldown the manager re-picks the same aircraft every cycle (a tight retry
    // storm). This holds it off until millis() passes the deadline (0 = ready now).
    unsigned long metadataRetryAfter = 0;
    // Cloud mode: an all-empty /v1/enrich response is either the proxy still
    // warming its caches (retry shortly) or a genuinely unknown aircraft (stop
    // asking). Bounded retries tell them apart; at the cap the empty answer is
    // accepted as Fetched.
    uint8_t enrichAttempts = 0;
    bool watchNotified = false;     // a flyover alert has been sent for this tracking session
    bool overheadNotified = false;  // a "look up" overhead alert has been sent this session
    bool milFlashFired = false;     // visual-alert flash burst fired for this military contact
    bool emgFlashFired = false;     // visual-alert flash burst fired for this emergency squawk
    bool emgNotified = false;       // an emergency-squawk ntfy alert has been sent this session
    bool overheadToneFired = false; // the overhead alert tone has sounded this session (HAS_AUDIO)
    // MQTT event dedupe (Home Assistant "events, not state"): one bit per event
    // class already fired for this contact this session, so an automation fires
    // once per aircraft, not every poll. Bit0 watchlist, 1 emergency, 2 military,
    // 3 overhead. Independent of the ntfy notified flags above.
    uint8_t mqttEventFlags = 0;
    // CLAIMABLE, not "new" (v4, 2026-08-03). This used to mean "this sighting was
    // the first time the type/airline was ever seen", latched once at first
    // sighting. It now means "this aircraft's TYPE has not been claimed yet", a
    // property of the type rather than of the sighting -- so a C-5 that went past
    // unattended overnight still shows NEW the next time one appears, and stops
    // only when somebody actually opens its card. Recomputed from the logbook
    // whenever the type is known, never latched.
    bool claimable = false;
    bool claimFired = false;        // the claim confirmation has been shown for this contact
    String typeCode = "";    // adsbdb icao_type, e.g. "B738"
    String typeName = "";    // adsbdb full model, e.g. "Boeing 737-800"
    String operatorName = "";
    String registration = "";
    String photoUrl = "";    // adsbdb url_photo_thumbnail (BYO) or the proxy stock photo (cloud); fetched/decoded on inspect
    bool photoRepresentative = false; // cloud stock photo is a generic type shot -> card captions "representative photo"

    // Flight route, looked up by callsign from adsbdb only when the aircraft is
    // inspected (detail view). routeCallsign records the callsign the route was
    // resolved for -- including a "" result for unknown routes -- so it isn't
    // re-queried until the callsign changes.
    String routeOrigin = "";
    String routeDest = "";
    String routeCallsign = "";
    // Like metadataRetryAfter: a transient route failure sets this so the detail
    // path doesn't re-request the route every frame while adsbdb is unreachable.
    unsigned long routeRetryAfter = 0;

    // REPORTED FIXES for the fading trail -- not display positions -- stored as
    // geographic coordinates (so they reproject correctly) in a ring buffer, one
    // entry per distinct fix.
    //
    // THIS USED TO SAMPLE GetDisplayPosition() ONCE A SECOND, and that is what
    // made stale data look broken. The display position is dead-reckoned AND
    // blended, so on a stale feed the trail recorded, in order: the extrapolated
    // guess, then the renderer sweeping the marker SIDEWAYS onto the true fix
    // when it finally arrived, then the resumed track. That lateral excursion is
    // a path no aircraft ever flew -- it is the renderer catching up -- and it
    // drew as a zig-zag that got uglier the longer the drought ran.
    //
    // Recording the FIX removes the artifact by construction rather than
    // smoothing it away: every point is a position the aircraft actually
    // reported. During a drought the trail simply stops growing while the marker
    // dead-reckons on, which reads as "the trail paused" instead of "the
    // aircraft convulsed". The draw loop already joins the newest point to the
    // live marker, so the trail stays attached with no gap.
    //
    // NB the obvious fix -- skip sampling while blendAlpha < 1 -- would have
    // stopped trails almost entirely: blendSpeed 0.15 needs ~6.7 s to complete
    // and the active cloud poll is 5 s, so blendAlpha is reset before it ever
    // reaches 1.0 and that condition is almost never true.
    //
    // TRAIL LENGTH IS AN AGE, NOT A COUNT, and it has to be: sampling is
    // fix-driven, so a pure count cap would tie trail DURATION to poll cadence
    // and give the shortest trail in the best mode. A local receiver polls at
    // 1 Hz, so 16 fixes there is a 16-second trail; the 5 s cloud poll would
    // draw 80 s from the same buffer.
    //
    // Duration is held constant by three rules together -- an age cap alone is
    // not enough, because at 1 Hz a 90 s window wants 90 points and the buffer
    // holds 60, so capacity would bind first and cadence would leak back in:
    //
    //   1. dedupe          -- only a CHANGED fix is ever appended
    //   2. min interval    -- at most one point per TRAIL_MIN_INTERVAL_S
    //   3. age expiry      -- points older than TRAIL_MAX_AGE_S are dropped
    //
    // The interval must satisfy MAX_AGE / INTERVAL <= CAPACITY, or capacity binds
    // first and cadence leaks straight back in. 90 / 2 = 45 points worst case
    // against a 60-entry buffer, so AGE is always the limit that fires and the
    // duration is identical in every mode. Everything is whole SECONDS
    // deliberately: at a 2 s floor sub-second precision buys nothing, and a
    // seconds/millis mix is exactly where an off-by-1000 hides.
    //
    // Simulated over a 10-minute run (see the PR): span is 89 s in every mode
    // that can fill the window, and the point COUNT absorbs the cadence instead.
    //
    //   local receiver 1 Hz   -> point every 2 s,  45 points, 89 s
    //   cloud active   5 s    -> point every 5 s,  18 points, 89 s
    //   OpenSky        ~10 s  -> point every 10 s,  9 points, 89 s
    //   cloud idle     15 s   -> point every 15 s,  6 points, 89 s
    //   cloud night    60 s   -> point every 60 s,  1 point,  <=60 s  (see below)
    //
    // THE NIGHT POLL IS A GENUINE EXCEPTION, not a policy failure: at 60 s only
    // one or two fixes exist inside a 90 s window at all, so there is no history
    // to draw and no sampling rule can invent one. It degenerates to a 1-2 point
    // stub. Acceptable -- that cadence only runs when the room is dark and the
    // display is dimmed -- but it is the one mode where trail length still tracks
    // the feed. Raising MAX_AGE past 120 s would fix it and cost trail relevance
    // everywhere else; not worth it.
    //
    // Timestamps are SECONDS since boot in a uint16 rather than millis in a
    // uint32: 4 bytes/point over a 60-entry buffer is 14 KB across a full
    // contact table on a device that counts contiguous heap. It wraps every
    // 18.2 h, which is harmless here because elapsed is computed by unsigned
    // subtraction and every point is expired at 90 s -- an age this arithmetic
    // could misread would have to survive 18 hours in a buffer that drops it
    // after ninety seconds.
    struct TrailPoint { float lat; float lon; uint16_t sec; };
    static constexpr int      TRAIL_CAPACITY       = 60;
    static constexpr uint16_t TRAIL_MAX_AGE_S      = 90;
    static constexpr uint16_t TRAIL_MIN_INTERVAL_S = 2; // >= ceil(MAX_AGE / CAPACITY)
    static_assert(TRAIL_MAX_AGE_S / TRAIL_MIN_INTERVAL_S <= TRAIL_CAPACITY,
                  "trail would fill by COUNT before AGE -- duration becomes poll-cadence dependent");
    TrailPoint trail[TRAIL_CAPACITY];
    int trailWrite = 0;                 // index of the next slot to overwrite
    int trailCount = 0;                 // valid points so far (<= TRAIL_CAPACITY)

    // Radar "paint" state for the PPI sweep. With the sweep enabled, a blip's
    // drawn position is latched here the moment the rotating beam crosses its
    // bearing, and held until the next pass instead of gliding every frame; its
    // brightness then decays from that instant until the beam comes round again,
    // the way a phosphor scope persists a return. everPainted stays false until
    // the first pass so a fresh contact shows live + full-bright until the beam
    // first reaches it -- the latch is seamless there because the painted point
    // equals the live point at the instant of paint.
    float paintLat = 0.0f;
    float paintLon = 0.0f;
    unsigned long lastPaintMs = 0;
    bool everPainted = false;

    // first appearance, no blend needed
    TrackedAircraft(const Aircraft& ac, unsigned long now)
        : state(ac), lastSeen(now),
        blendFromLat(ac.latitude),
        blendFromLon(ac.longitude),
        blendAlpha(1.0f) {
    }

    // subsequent update — blend from current visual position
    void Update(const Aircraft& newState, unsigned long now) {
        // capture visual position at moment of update before switching state
        auto [curLat, curLon] = GetDisplayPosition();
        blendFromLat = curLat;
        blendFromLon = curLon;
        blendAlpha = 0.0f;  // restart blend

        state = newState;
        lastSeen = now;
    }

    void Tick() {
        unsigned long now = millis();
        float deltaSeconds = (now - lastTick) / 1000.0f;
        lastTick = now;

        // 0.25 -> the blend completes in 4 s, INSIDE the 5 s active cloud poll.
        //
        // It was 0.15, which needs ~6.7 s: longer than the poll, so every update
        // reset blendAlpha before it ever reached 1.0 and the marker was
        // permanently mid-correction, chasing a target it never caught. 0.20 was
        // rejected deliberately -- it completes in exactly the poll interval,
        // which is the boundary case of the same defect and would tip back into
        // it on any jitter or a slightly faster cadence.
        const float blendSpeed = 0.25f; // lower = slower, higher = faster
        blendAlpha = min(blendAlpha + deltaSeconds * blendSpeed, 1.0f);
    }

    // Append the latest REPORTED FIX to the trail, if it is a new one. Call every
    // frame (or every sweep pass): it self-dedupes, so there is no timer.
    //
    // Exact float compare is the right test here rather than a distance epsilon:
    // the feed quantizes position, so an unchanged fix is bit-identical, and
    // "the value did not change" is precisely the condition we want. A drought
    // therefore appends nothing at all, which is the whole point.
    void SampleTrail() {
        const uint16_t nowSec = (uint16_t)(millis() / 1000UL);

        // Expire FIRST, and unconditionally -- ageing must keep running through a
        // drought, when no new fix arrives to trigger it. Reducing trailCount
        // drops the oldest points, because TrailPointAt indexes back from
        // trailWrite. Oldest-first order means the first survivor ends the walk.
        while (trailCount > 0) {
            const int oldest = (trailWrite - trailCount + TRAIL_CAPACITY) % TRAIL_CAPACITY;
            if ((uint16_t)(nowSec - trail[oldest].sec) <= TRAIL_MAX_AGE_S)
                break;
            --trailCount;
        }

        const float lat = state.latitude;
        const float lon = state.longitude;
        if (trailCount > 0) {
            const int last = (trailWrite - 1 + TRAIL_CAPACITY) % TRAIL_CAPACITY;
            // Same fix as last time: nothing new was reported. Exact compare is
            // the right test -- the feed quantizes position, so an unchanged fix
            // is bit-identical and "the value did not change" is the condition.
            if (trail[last].lat == lat && trail[last].lon == lon)
                return;
            // A changed fix, but too soon: a 1 Hz local receiver would otherwise
            // fill the buffer in 60 s and cap the trail by COUNT, reintroducing
            // the cadence dependence this interval exists to remove.
            if ((uint16_t)(nowSec - trail[last].sec) < TRAIL_MIN_INTERVAL_S)
                return;
        }
        trail[trailWrite] = { lat, lon, nowSec };
        trailWrite = (trailWrite + 1) % TRAIL_CAPACITY;
        if (trailCount < TRAIL_CAPACITY)
            ++trailCount;
    }

    int TrailSize() const { return trailCount; }

    // Trail points oldest (i = 0) to newest (i = TrailSize() - 1).
    std::pair<float, float> TrailPointAt(int i) const {
        const int idx = (trailWrite - trailCount + i + TRAIL_CAPACITY) % TRAIL_CAPACITY;
        return { trail[idx].lat, trail[idx].lon };
    }

    // Latch the current display position as the beam's freshly-painted return.
    void Paint() {
        auto [la, lo] = GetDisplayPosition();
        paintLat = la;
        paintLon = lo;
        lastPaintMs = millis();
        everPainted = true;
    }

    // 1.0 at the instant of paint, fading linearly to a dim floor over one sweep
    // period (periodMs) so a contact persists -- dimming -- until the next pass.
    float PaintBrightness(unsigned long periodMs) const {
        if (!everPainted) return 1.0f;
        const float age = (millis() - lastPaintMs) / (float)periodMs;
        float b = 1.0f - age;
        // The whole contact (marker + trail + label) is scaled by this, so the
        // floor keeps the dimmest state legible rather than only visible.
        constexpr float floor = 0.30f;
        if (b < floor) b = floor;
        if (b > 1.0f) b = 1.0f;
        return b;
    }

    std::pair<float, float> GetDisplayPosition() const {
        auto [deadLat, deadLon] = PredictPosition();

        if (blendAlpha >= 1.0f)
            return { deadLat, deadLon };

        // ease-in-out for smoother feel
        float t = blendAlpha * blendAlpha * (3.0f - 2.0f * blendAlpha);

        return {
            blendFromLat + t * (deadLat - blendFromLat),
            blendFromLon + t * (deadLon - blendFromLon)
        };
    }

    std::pair<float, float> PredictPosition() const {
        float dataAgeOnArrival = 0.0f;
        if (state.timePosition > 0 && state.lastContact > 0)
            dataAgeOnArrival = (float)(state.lastContact - state.timePosition);

        float localElapsed = (millis() - lastSeen) / 1000.0f;
        float dt = localElapsed + dataAgeOnArrival;

        // Cap the dead-reckoning horizon. Between polls dt is a few seconds (up to
        // a few minutes for OpenSky's credit-metered cadence), which DR is meant to
        // bridge. But if the feed dies entirely, nothing evicts the stale contact
        // (eviction only runs on a successful merge), so dt grows without bound and
        // extrapolates the position to absurd lat/lon that (a) vanish off-screen and
        // (b) overflow the int hit-test math downstream -- making an invisible ghost
        // tappable across the whole screen. 10 min is well past any legitimate poll
        // gap; beyond it the contact is gone, not still flying.
        // Named at class scope (below) so the display ladder can key its "NO DATA"
        // stage off the SAME number: the moment DR caps is the moment the sky stops
        // moving, and the UI must stop implying the picture is live at exactly that
        // instant, not at some separately-maintained threshold that can drift.
        if (dt > MAX_DR_SECONDS) dt = MAX_DR_SECONDS;

        float headingRad = radians(state.trueTrack);
        const float latMetersPerDeg = 111320.0f;
        float deltaLat = (state.velocity * dt * cos(headingRad)) / latMetersPerDeg;
        float deltaLon = (state.velocity * dt * sin(headingRad)) / (latMetersPerDeg * cos(radians(state.latitude)));

        return { state.latitude + deltaLat, state.longitude + deltaLon };
    }
};