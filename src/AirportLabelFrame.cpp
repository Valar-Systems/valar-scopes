// The frame's airport-label state and the decision, in their own translation unit (include/AirportLabelFrame.h).
// Pure: no canvas and no Arduino types, so test/host/test_airport_label_frame.cpp links this very file.
#include "AirportLabelFrame.h"

#include <vector>

namespace airportlabel {

namespace {
Obstacles<kObstacleCap> g_obstacles;
std::vector<Mark> g_marks;
}  // namespace

void FrameReset()
{
    g_obstacles.Reset();
    g_marks.clear();   // capacity is kept: one allocation, reused every frame
}

void AddObstacle(const Rect& r)
{
    g_obstacles.Add(r);
}

void AddMark(int x, int y, const char* code)
{
    g_marks.push_back(Mark{ (int16_t)x, (int16_t)y, code, Placement{ Side::Dropped, 0, 0 } });
}

void ReserveMarks(size_t n)
{
    g_marks.reserve(n);   // reserve never shrinks: a smaller list later keeps the capacity it has
}

void Decide(int h, int screenSize, WidthFn width, void* wctx, TextHitsFn textHits, const void* tctx)
{
    // Against everything the traffic pass recorded: the counter's label and badge boxes (textHits), and this unit's
    // symbols and other traffic text. A candidate off the glass is blocked too: dropped, never cut at the rim.
    for (Mark& m : g_marks)
        m.at = Place(m.x, m.y, width(m.code, wctx), h, screenSize,
                     [&](const Rect& b) { return textHits(b, tctx); },
                     [&](const Rect& b) { return g_obstacles.Hits(b); });
}

int MarkCount()
{
    return (int)g_marks.size();
}

const Mark& MarkAt(int i)
{
    return g_marks[(size_t)i];
}

int ObstacleCount()
{
    return g_obstacles.Count();
}

bool ObstacleOverflow()
{
    return g_obstacles.Overflow();
}

}  // namespace airportlabel
