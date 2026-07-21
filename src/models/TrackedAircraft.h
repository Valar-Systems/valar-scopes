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
    bool freshCatch = false;        // this sighting added a brand-new type/airline to the logbook
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

    // Recent display positions for the fading trail, stored as geographic
    // coordinates (so they reproject correctly) in a ring buffer sampled once a
    // second. ~TRAIL_CAPACITY seconds of history.
    struct TrailPoint { float lat; float lon; };
    static constexpr int TRAIL_CAPACITY = 60;
    static constexpr unsigned long TRAIL_SAMPLE_MS = 1000;
    TrailPoint trail[TRAIL_CAPACITY];
    int trailWrite = 0;                 // index of the next slot to overwrite
    int trailCount = 0;                 // valid points so far (<= TRAIL_CAPACITY)
    unsigned long lastTrailSample = 0;

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

        const float blendSpeed = 0.15f; // lower = slower, higher = faster
        blendAlpha = min(blendAlpha + deltaSeconds * blendSpeed, 1.0f);
    }

    // Append the current display position to the trail at most once per
    // TRAIL_SAMPLE_MS; call every frame (it self-throttles).
    void SampleTrail() {
        unsigned long now = millis();
        if (trailCount > 0 && now - lastTrailSample < TRAIL_SAMPLE_MS)
            return;
        lastTrailSample = now;

        auto [lat, lon] = GetDisplayPosition();
        trail[trailWrite] = { lat, lon };
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