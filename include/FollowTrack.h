#pragma once

// Follow Mode -- the flight track buffer.  Spec: docs/follow-mode-consolidated.md §4.
//
// A flight track is NOT the existing trail. `TrackedAircraft` already carries a
// good one -- 60 points, age-capped at 90 s -- but that is a MOTION CUE whose job
// is to show which way a blip is going, and it dies with the contact.
// `AircraftManager` evicts a contact absent past ~30 s and rebuilds it from
// scratch, discarding the trail. For a trainer at 1,000 ft AGL, whose coverage is
// line-of-sight to ground receivers, that eviction is the NORMAL OPERATING
// CONDITION, not an edge case. So the track is owned by the manager, keyed by
// identity, and outlives the contact table entirely. (§3)
//
// =============================================================================
// WHY THIS FILE IS HEADER-ONLY
//
// Seven other products build from this repo and each drops the radar TUs by name
// in its `build_src_filter`. A new .cpp under src/ would need an exclusion added
// to all seven, and the one that got missed would link a buffer it never uses.
// A header in include/ is compiled only where it is included -- which is the
// radar path and nowhere else -- so there is no filter to keep in sync. Same
// reasoning as include/DisplayUnits.h and include/RouteLabel.h.
//
// =============================================================================
// §4.3 -- ONE ALLOCATION, AT FOLLOW-ENABLE, NEVER GROWN
//
// This device has an open, unexplained fragmentation problem (#245): roughly
// 24 KB of erosion over 11 hours. Adding allocation behaviour to this codebase on
// the strength of first-principles reasoning would be exactly the wrong lesson to
// draw from that investigation. So the discipline is mechanical, and the class
// enforces it rather than documenting it:
//
//   * ONE heap_caps_malloc(MALLOC_CAP_SPIRAM) at the moment follow is enabled.
//   * NEVER reallocated. A fixed ring from birth; it wraps rather than grows.
//   * Freed ONLY on disable -- not on landing, not on a new flight, not on
//     eviction. ResetFlight() moves the write index and does not touch the
//     allocation.
//   * If the allocation fails the feature degrades to notification-only, says so,
//     and DOES NOT RETRY. A periodic retry of a large allocation under
//     fragmentation is itself a fragmentation source: it turns a clean
//     degradation into a slow decay. `degraded` latches for exactly that reason,
//     and only an explicit Disable() clears it -- a person editing the config
//     page is not a loop.
//
// The distinction that matters: a track that GROWS is a fragmentation source; a
// track allocated once and reused is not. The first is a stream of
// differently-sized blocks interleaved with TLS handshakes. The second is one
// block that either exists or does not.
//
// =============================================================================
// §4.4 -- VERIFY IT, DO NOT ASSUME IT
//
// "PSRAM is invisible to the internal heap" is well-supported (the 2026-08-09
// measurement moved psram_free by 73,532 B for a backbuffer plus two sprites and
// left the internal heap untouched) and it is still an ASSUMPTION, in a codebase
// with an open issue that consists precisely of memory behaving unpredictably.
//
// So Enable() proves where the block landed instead of trusting the flag it asked
// with: esp_ptr_external_ram() on the returned pointer, and the psram_free delta
// printed either way. A 12 KB block that quietly landed on the INTERNAL heap is
// not a smaller version of the same feature -- it is the fragmentation source
// this design exists to avoid, so it is freed and the feature degrades.
//
// That check is cheap and it is the one that cannot be added later: by the time a
// soak shows drift, the allocation that caused it is months old.

#include <Arduino.h>

#include <esp_heap_caps.h>
#if __has_include(<esp_memory_utils.h>)
#include <esp_memory_utils.h>
#define FOLLOW_HAS_PTR_CHECK 1
#endif

#include <cmath>
#include <cstdint>

namespace follow {

// 4 + 4 + 2 = 10 B, padded to 12 by alignment. 1024 x 12 = 12,288 B, and that is
// the WORST case and the typical case both, because the buffer is the same size
// whether or not it is full. That is the entire point of §4.3.
struct TrackPoint {
    float    lat;
    float    lon;
    uint16_t sec; // seconds since the track started; wraps at ~18 h
};

class Track {
public:
    // 1024 points at 150 m minimum separation is about 150 km of flown path --
    // comfortably a whole lesson. The buffer self-bounds without a timer.
    static constexpr size_t   CAPACITY   = 1024;
    // §4.1: decimate by DISTANCE, not time. At the tightest zoom a pixel is a few
    // hundred metres, so anything closer is drawing on top of itself. The rule has
    // a pleasant property -- straight cruise legs compress to almost nothing while
    // turns and circuits keep their full shape, because shape is where consecutive
    // fixes differ in DIRECTION rather than distance.
    static constexpr float    MIN_SEP_M  = 150.0f;
    static constexpr size_t   BYTES      = CAPACITY * sizeof(TrackPoint);

    ~Track() { Disable(); }

    Track() = default;
    Track(const Track&) = delete;            // one owner, one allocation
    Track& operator=(const Track&) = delete;

    bool Active()   const { return buf != nullptr; }
    bool Degraded() const { return degraded; }
    size_t Size()   const { return count; }
    bool Full()     const { return count >= CAPACITY; }

    // Oldest-first indexing, so callers walk 0..Size() in flight order and never
    // have to know the ring wrapped.
    const TrackPoint& At(size_t i) const
    {
        const size_t start = Full() ? head : 0;
        return buf[(start + i) % CAPACITY];
    }

    // ---- the one allocation ------------------------------------------------
    // Returns true if the track is usable. On false the caller degrades to
    // notification-only and MUST NOT call again in a loop; this is idempotent and
    // short-circuits, but the caller's own retry is the thing that must not exist.
    bool Enable()
    {
        if (buf) return true;
        if (degraded) return false; // latched: never retry a failed large alloc

        const uint32_t psramBefore = ESP.getFreePsram();
        const uint32_t heapBefore  = ESP.getFreeHeap();

        void* p = heap_caps_malloc(BYTES, MALLOC_CAP_SPIRAM);

        const uint32_t psramAfter = ESP.getFreePsram();
        const uint32_t heapAfter  = ESP.getFreeHeap();

        if (!p) {
            degraded = true;
            Serial.printf("[follow] track alloc FAILED (%u B from PSRAM); "
                          "degrading to notification-only, will not retry. "
                          "psram_free=%u heap_free=%u\n",
                          (unsigned)BYTES, (unsigned)psramAfter, (unsigned)heapAfter);
            return false;
        }

#ifdef FOLLOW_HAS_PTR_CHECK
        // WHERE IT LANDED, not where it was asked to land. A 12 KB block on the
        // internal heap is the fragmentation source §4.3 exists to avoid, so this
        // is a failure and not a degraded success.
        if (!esp_ptr_external_ram(p)) {
            heap_caps_free(p);
            degraded = true;
            Serial.printf("[follow] track alloc landed on the INTERNAL heap, not PSRAM "
                          "(%u B) -- freed and degrading to notification-only. "
                          "This is the #245 fragmentation source, not a smaller feature.\n",
                          (unsigned)BYTES);
            return false;
        }
#endif

        buf   = static_cast<TrackPoint*>(p);
        count = 0;
        head  = 0;
        appends = 0;
        rejectedNear = 0;
        startMs = millis();
        haveLast = false;

        // The §4.4 acceptance evidence, printed at the moment it is cheap to read:
        // psram_free should fall by ~12 KB and the internal heap should not move.
        Serial.printf("[follow] track ENABLED %u B in PSRAM  psram_free %u -> %u (-%d)  "
                      "heap_free %u -> %u (%+d)\n",
                      (unsigned)BYTES, (unsigned)psramBefore, (unsigned)psramAfter,
                      (int)(psramBefore - psramAfter),
                      (unsigned)heapBefore, (unsigned)heapAfter,
                      (int)heapAfter - (int)heapBefore);
        return true;
    }

    // The ONLY place the buffer is freed.
    void Disable()
    {
        if (!buf && !degraded) return;
        if (buf) {
            const uint32_t psramBefore = ESP.getFreePsram();
            heap_caps_free(buf);
            buf = nullptr;
            Serial.printf("[follow] track DISABLED, %u B returned  psram_free %u -> %u\n",
                          (unsigned)BYTES, (unsigned)psramBefore, (unsigned)ESP.getFreePsram());
        }
        count = 0;
        head = 0;
        haveLast = false;
        // Clearing the follow field is a person, not a loop, so it is allowed to
        // re-arm the one retry the discipline permits.
        degraded = false;
    }

    // A new flight resets the WRITE INDEX. It does not touch the allocation --
    // §4.3, and the reason the two are separate calls at all.
    void ResetFlight()
    {
        count = 0;
        head = 0;
        haveLast = false;
        startMs = millis();
    }

    // Returns true if the fix was kept. Rejection is the common case on a
    // straight leg and is not a failure.
    bool Append(float latDeg, float lonDeg)
    {
        if (!buf) return false;

        if (haveLast && SeparationM(latDeg, lonDeg, lastLat, lastLon) < MIN_SEP_M) {
            ++rejectedNear;
            return false;
        }

        const uint32_t secs = (millis() - startMs) / 1000u;
        buf[head] = TrackPoint{latDeg, lonDeg, (uint16_t)(secs & 0xFFFF)};
        head = (head + 1) % CAPACITY;
        if (count < CAPACITY) ++count;
        lastLat = latDeg;
        lastLon = lonDeg;
        haveLast = true;
        ++appends;
        return true;
    }

    // ---- draw budget (§4.5) -------------------------------------------------
    // The draw-time cap with adaptive stride. Segment count is what costs, so the
    // stride is chosen to keep drawn segments at or under the cap however full the
    // buffer is. Returns >= 1 always.
    static size_t StrideFor(size_t n, size_t segmentCap)
    {
        if (n <= segmentCap || segmentCap == 0) return 1;
        return (n + segmentCap - 1) / segmentCap; // ceil
    }

    uint32_t Appends()      const { return appends; }
    uint32_t RejectedNear() const { return rejectedNear; }

    // Equirectangular, which is what the rest of this file already uses for short
    // baselines (see AircraftManager::IsOverhead). At 150 m the great-circle
    // correction is far below the sampling threshold it is being compared to.
    static float SeparationM(float aLat, float aLon, float bLat, float bLon)
    {
        constexpr float M_PER_DEG = 111320.0f;
        const float dLat = (aLat - bLat) * M_PER_DEG;
        const float dLon = (aLon - bLon) * M_PER_DEG * cosf(radians(aLat));
        return sqrtf(dLat * dLat + dLon * dLon);
    }

#ifdef FOLLOW_BENCH
    // ---- bench only: the reason the first deliverable can produce a number ----
    //
    // §18.1 asks for the draw cost of a FULL track measured against the frame
    // budget, and that measurement is impossible to obtain honestly by waiting: a
    // real 1024-point track is 150 km of flown path, i.e. a couple of hours of a
    // real aeroplane being in the air near a real bench. Nobody would run that
    // before deciding whether the feature is worth building, so in practice the
    // number would come from a partly-filled buffer and be quietly optimistic.
    //
    // So the bench build synthesises the worst case directly: CAPACITY points,
    // every one of them INSIDE the visible scope, so every drawn segment is a real
    // clipped-in line rather than a cheap off-screen reject. A track that wandered
    // off the edge would measure faster than the truth.
    void FillSynthetic(float centreLat, float centreLon, float radiusKm)
    {
        if (!buf) return;
        ResetFlight();
        const float kmPerDegLat = 111.0f;
        const float kmPerDegLon = 111.0f * cosf(radians(centreLat));
        // An inward spiral: crosses the whole scope repeatedly, so segments span
        // real screen distances instead of clustering into one dense knot.
        for (size_t i = 0; i < CAPACITY; ++i) {
            const float t   = (float)i / (float)CAPACITY;
            const float ang = t * 2.0f * (float)M_PI * 9.0f; // nine laps
            const float r   = radiusKm * (0.97f - 0.9f * t);
            const float dLat = (r * sinf(ang)) / kmPerDegLat;
            const float dLon = (r * cosf(ang)) / kmPerDegLon;
            buf[head] = TrackPoint{centreLat + dLat, centreLon + dLon, (uint16_t)i};
            head = (head + 1) % CAPACITY;
            if (count < CAPACITY) ++count;
        }
        lastLat = buf[(head + CAPACITY - 1) % CAPACITY].lat;
        lastLon = buf[(head + CAPACITY - 1) % CAPACITY].lon;
        haveLast = true;
        Serial.printf("[follow] synthetic track filled: %u points inside %.0f km "
                      "(worst case -- every segment on screen)\n",
                      (unsigned)count, radiusKm);
    }
#endif

private:
    TrackPoint* buf = nullptr;
    size_t   count = 0;
    size_t   head  = 0;      // next write slot
    bool     degraded = false;
    bool     haveLast = false;
    float    lastLat = 0.0f;
    float    lastLon = 0.0f;
    uint32_t startMs = 0;
    uint32_t appends = 0;
    uint32_t rejectedNear = 0;
};

} // namespace follow
