#pragma once

// Follow Mode -- the post-flight record.  Spec: docs/follow-mode-consolidated.md §11.
//
// =============================================================================
// WHAT THIS IS FOR
//
// "The answer to 'the screen is empty most of the week.'"
//
// A followed aeroplane flies for an hour on a Saturday. Every other hour of the
// week the Follow screen has nothing live to say, and a face that says nothing
// is a face nobody looks at. So on LANDED the flight is frozen -- how long, how
// high, how fast, how far, and THE SHAPE OF IT -- and that stays on the screen
// until the next takeoff.
//
// §11 is explicit about why the shape and not just the numbers:
//
//   "The alternative was to drop shape and show four numbers. Rejected because
//    the shape IS the emotional payload: a racetrack of circuits is the picture
//    that says 'he practised landings today' without a word of text. Four
//    numbers is a readout; the shape is a souvenir."
//
// =============================================================================
// THE ONLY PART OF FOLLOW THAT SURVIVES A POWER CYCLE, AND WHY THAT IS SAFE
//
// Everything else in Follow is live data with a freshness question attached: a
// position is stale or it is not, a state is current or it is not, and restoring
// any of it across a reboot would mean showing something that might have stopped
// being true while the device was off.
//
// A finished flight has no such question. "No arc, no bearing, no live data --
// NOTHING HERE CAN BE WRONG, which is why it is the only part of Follow that
// should survive a power cycle." It is a record of something that already
// happened, and it is exactly as true after the power cut as before it.
//
// =============================================================================
// STORAGE DISCIPLINE
//
// Own NVS namespace, mirroring Logbook's rules -- bounded store, ONE WRITE PER
// FLIGHT. There is no debounce here and none is needed: Logbook debounces
// because it is written continuously by a running sky, and a flight ends once.
//
//     128 points x 8 B (lat/lon as scaled int32) = 1,024 B
//
// 128 points is enough to read a circuit pattern at card size and small enough
// to sit inside the existing NVS budget without competing with the config
// namespace that shares the partition. It is also the number both source
// documents arrived at independently (C7).
//
// Header-only for the same reason as FollowTrack.h: seven products build from
// this repo and each drops the radar TUs by name in its build_src_filter, so a
// new .cpp under src/ would need an exclusion added to all seven and the one
// that got missed would link a store it never uses.

#include <Arduino.h>
#include <Preferences.h>

#include <cmath>
#include <cstdint>

#include "FollowTrack.h"

namespace follow {

// Degrees x 1e6: about 0.11 m, which is four orders of magnitude finer than the
// 150 m decimation the points arrived at, and it fits int32 with room (180e6 vs
// 2.1e9). Chosen over float purely so the on-flash record has a fixed, stated
// meaning rather than an IEEE representation somebody has to reason about.
constexpr float LOG_SCALE = 1e6f;

#pragma pack(push, 1)
struct LogPoint {
    int32_t lat;
    int32_t lon;
};

// Written as one blob. Packed and versioned because it goes to flash: a struct
// whose layout the compiler is free to change is a record that silently stops
// parsing after an unrelated edit.
struct FlightRecord {
    uint8_t  version     = 1;
    uint16_t points      = 0;
    uint32_t durationSec = 0;
    int32_t  maxAltMslFt = 0;
    uint16_t topSpeedKt  = 0;
    uint16_t furthestKmX10 = 0;  // km x 10 -- 0.1 km resolution to 6,553 km
    uint32_t landedEpoch = 0;    // 0 = the clock was not synced; say so, do not guess
};
#pragma pack(pop)

class Log {
public:
    static constexpr size_t   POINTS = 128;
    static constexpr uint8_t  VERSION = 1;
    static constexpr const char* NS = "follow-log";

    bool Has() const { return rec.points > 0; }
    const FlightRecord& Record() const { return rec; }
    size_t Size() const { return rec.points; }

    void PointAt(size_t i, float& outLat, float& outLon) const
    {
        outLat = (float)pts[i].lat / LOG_SCALE;
        outLon = (float)pts[i].lon / LOG_SCALE;
    }

    // At boot. A namespace that does not exist yet is an empty log, not an
    // error -- which is the state of every device that has never followed
    // anything, i.e. all of them.
    void Load()
    {
        Preferences p;
        if (!p.begin(NS, /*readOnly=*/true))
            return;
        FlightRecord in{};
        if (p.getBytes("rec", &in, sizeof(in)) == sizeof(in) && in.version == VERSION &&
            in.points > 0 && in.points <= POINTS) {
            const size_t want = (size_t)in.points * sizeof(LogPoint);
            if (p.getBytes("pts", pts, want) == want) {
                rec = in;
                Serial.printf("[follow] last flight restored: %u pts, %lu s, %ld ft, %u kt\n",
                              (unsigned)rec.points, (unsigned long)rec.durationSec,
                              (long)rec.maxAltMslFt, (unsigned)rec.topSpeedKt);
            }
        }
        p.end();
    }

    // The one write, at the one moment. Decimates the live track by index --
    // NOT by distance a second time: the buffer was already decimated at 150 m
    // on the way in (§4.1), and re-decimating by distance here would throw away
    // exactly the turns that make a circuit legible while keeping the straight
    // legs that do not.
    void Save(const Track& track, const FlightRecord& summary)
    {
        const size_t n = track.Size();
        if (n < 2)
            return; // a flight with no shape is not a souvenir

        // THE LAST SLOT IS RESERVED, which is why the stride is computed against
        // POINTS-1 rather than POINTS. The final point is where he touched down
        // and §11 draws it filled; a decimation that dropped it puts the
        // destination marker wherever the arithmetic happened to land.
        //
        // On a full 1024-point buffer the stride is 9 and the walk ends at index
        // 1017 -- so without the reservation the marker would sit six samples,
        // roughly a kilometre, short of the runway. Cheap to get wrong and
        // invisible once drawn, since a track that ends near the field looks
        // exactly like a track that ends at it.
        const size_t stride = Track::StrideFor(n, POINTS - 1);
        size_t out = 0;
        for (size_t i = 0; i + 1 < n && out + 1 < POINTS; i += stride) {
            const TrackPoint& tp = track.At(i);
            pts[out].lat = (int32_t)lroundf(tp.lat * LOG_SCALE);
            pts[out].lon = (int32_t)lroundf(tp.lon * LOG_SCALE);
            ++out;
        }
        const TrackPoint& last = track.At(n - 1);
        pts[out].lat = (int32_t)lroundf(last.lat * LOG_SCALE);
        pts[out].lon = (int32_t)lroundf(last.lon * LOG_SCALE);
        ++out;

        rec = summary;
        rec.version = VERSION;
        rec.points = (uint16_t)out;

        Preferences p;
        if (!p.begin(NS, /*readOnly=*/false)) {
            Serial.println("[follow] post-flight record NOT saved: NVS namespace would not open");
            return;
        }
        p.putBytes("rec", &rec, sizeof(rec));
        p.putBytes("pts", pts, out * sizeof(LogPoint));
        p.end();
        Serial.printf("[follow] flight recorded: %u pts from %u, %lu s\n",
                      (unsigned)out, (unsigned)n, (unsigned long)rec.durationSec);
    }

    // Only ever called deliberately -- a follow target being changed to a
    // different aircraft. The previous aeroplane's flight is not this
    // aeroplane's history, and showing it would be the card's one way of being
    // wrong.
    void Clear()
    {
        rec = FlightRecord{};
        Preferences p;
        if (p.begin(NS, false)) { p.clear(); p.end(); }
    }

private:
    FlightRecord rec{};
    LogPoint     pts[POINTS]{};
};

} // namespace follow
