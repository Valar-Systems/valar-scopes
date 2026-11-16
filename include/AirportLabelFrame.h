#pragma once
// THE FRAME'S AIRPORT-LABEL STATE (v20, docs/v20-label-collision.md), owned by its own translation unit,
// src/AirportLabelFrame.cpp, and reached only through the calls below.
//
// WHY ITS OWN UNIT (#719 row (d), 2026-11-16): the label fix's runtime pieces cost about nothing on the frame bench, but
// its code cost about +3.8 ms a frame while present, and adding code to AircraftManager.cpp's hot functions was measured
// to change functions it never touched. So the state lives here, not in AircraftManager (whose layout stays as it was),
// and the decision (Place, the obstacle list, the counter's Hits) compiles here, not in AircraftManager.cpp. What stays
// there is calls. Nothing in this header is inline.
//
// WHAT IT HOLDS: what an airport CODE must not land on beyond the overlap counter's own boxes (each aircraft symbol, plus
// the traffic text the counter does not hold), each recorded at the call that drew it; and the airports whose MARK this
// frame drew, with where each code goes, decided once on the first band so every band draws the same decision.
#include <stddef.h>
#include <stdint.h>
#include "AirportLabel.h"

namespace airportlabel {

/// Per contact at most a symbol and four texts (the emergency descriptor, LOOK UP, the watchlist and pinned
/// callsigns); DrawRadar asserts this against MAX_AIRCRAFT. Past it the list answers "blocked" (Obstacles::Hits).
constexpr int kObstacleCap = 5 * 64 + 8;

struct Mark {
    int16_t x, y;        // the mark's centre
    const char* code;    // stable for the frame: the baked table or the loaded cloud list
    Placement at;        // Right / Left / Dropped; Dropped until Decide runs
};

/// The first band of a frame: forget last frame's obstacles and marks (the marks' capacity is kept).
void FrameReset();
/// Record a box the traffic pass drew and the counter does not hold.
void AddObstacle(const Rect& r);
/// Record an airport mark drawn this frame, its code not yet placed.
void AddMark(int x, int y, const char* code);
/// Capacity up front, off the draw path, so AddMark never reallocates (review F4).
void ReserveMarks(size_t n);

/// The code's width in px on the caller's canvas, and "does this box land on the counter's text?".
typedef int (*WidthFn)(const char* code, void* ctx);
typedef bool (*TextHitsFn)(const Rect& box, const void* ctx);
/// Place every mark's code (Place: right, else left, else dropped; on the glass and clear of text and obstacles).
void Decide(int h, int screenSize, WidthFn width, void* wctx, TextHitsFn textHits, const void* tctx);

int MarkCount();
const Mark& MarkAt(int i);
int ObstacleCount();
bool ObstacleOverflow();

}  // namespace airportlabel
