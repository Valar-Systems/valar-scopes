#pragma once

#include <stdint.h>

// SWIPE-TO-ZOOM ON THE RADAR (v16). Daniel: "at the default radius, aircraft near
// the centre stack on top of each other and I can't tell which one is overhead" --
// zooming IN is the need, getting back to the default is secondary.
//
// A VIEW SCALE ONLY. The fetch box (radLat/radLon, the cloud r=, the OpenSky box)
// never changes with zoom; the radar projection, the ring labels, the airport cull
// and the tap hit-test read a separate view radius. Alerts stay on the CONFIGURED
// circle: they are screen-level, so zoom must not change which fire or when.
//
// This file is the pure half (host-tested in test/host/test_radar_zoom.cpp): the
// ladder, one step along it, and the idle test. AircraftManager owns the state.
namespace radarzoom {

// THE SHIPPED IDLE RETURN: back to the configured radius after 10 minutes with no
// touch (the spec). Z5 was measured on a 30 s TEST build via -DRADAR_ZOOM_IDLE_MS, and
// that override refuses to compile without ALERT_BENCH (AircraftManager.cpp), which
// check-no-bench-hooks.sh keeps out of every shipping image. test_radar_zoom.cpp fails
// if this is anything but 600000 ms. So a test value cannot ride into a release.
constexpr uint32_t IDLE_RETURN_MS = 10UL * 60UL * 1000UL;

constexpr int MAX_STEPS = 5;                         // 4 candidates + the configured radius
constexpr float CANDIDATES[] = { 5.0f, 10.0f, 25.0f, 50.0f };   // in the user's radius unit

// A candidate this close to the configured radius is the SAME step, not a second
// one: the configured radius reaches us through km/degree conversions and arrives
// as 49.99997 or 50.00002, and "50, 50" would be a ladder with a dead step in it.
constexpr float SAME_STEP_EPS = 0.5f;

struct Ladder {
    float step[MAX_STEPS];
    int n = 0;
    int Top() const { return n - 1; }                // index of the configured radius
};

// {5, 10, 25, 50} strictly below the configured radius, then the configured radius
// itself as the outermost step. Strictly increasing and unique by construction.
inline Ladder BuildLadder(float configured)
{
    Ladder l;
    for (float c : CANDIDATES)
        if (c < configured - SAME_STEP_EPS && l.n < MAX_STEPS - 1)
            l.step[l.n++] = c;
    l.step[l.n++] = configured;
    return l;
}

enum class Edge { None, Max, Min };                  // Max = most zoomed in, Min = the default

// One step. dir -1 zooms IN (toward index 0), +1 zooms OUT (toward Top). Clamps at
// both ends and NEVER wraps; at an end it returns the same index and says which.
inline int Step(int idx, int dir, const Ladder& l, Edge& edge)
{
    edge = Edge::None;
    const int next = idx + (dir < 0 ? -1 : 1);
    if (next < 0)        { edge = Edge::Max; return 0; }
    if (next > l.Top())  { edge = Edge::Min; return l.Top(); }
    return next;
}

// millis()-wrap safe, and SIGNED: lastTouch can be a tick newer than `now` when the
// touch poll stamped it after this frame read the clock (the card idle-close bug).
// 32-bit on purpose: millis() is 32 bits on the device, and an `unsigned long` is
// 64 bits on a Linux host, where the wrap would never happen.
inline bool IdleExpired(uint32_t now, uint32_t lastTouch, uint32_t timeoutMs)
{
    return (int32_t)(now - lastTouch) >= (int32_t)timeoutMs;
}

} // namespace radarzoom
