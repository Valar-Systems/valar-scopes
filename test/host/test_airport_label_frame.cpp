// The airport label's FRAME UNIT (src/AirportLabelFrame.cpp, include/AirportLabelFrame.h), linked as built: the state
// the label fix keeps out of AircraftManager (#719 row (d)) and the decision that runs there. test_airport_label.cpp
// grades Place itself; this grades the moved boundary: that what DrawRadar records reaches the decision, that the
// counter's text and the caller's width are what Decide uses, and that each frame starts clean.
//
// Every row begins with FrameReset() and reads the mark it added LAST, so a row does not depend on the ones before it
// except through what FrameReset is meant to clear. The order below is fixed: sabotage_airport_label_frame.py's red sets
// assume it.
//
// The geometry is the device's: a 240 px face, a 6 x 8 px font (the code "BDN" is 18 x 8), the code at (mx+5, my-3)
// when right and its right edge 5 px left of the mark when left.
#include <cstdio>
#include <cstring>
#include "../../include/AirportLabelFrame.h"

using namespace airportlabel;

static int failures = 0;
static void row(bool cond, const char* what)
{
    printf("  %s %s\n", cond ? "ok  " : "BAD ", what);
    if (!cond) ++failures;
}

static const int FACE = 240, H = 8;

// The caller's width: 6 px a character, unless the context names an override (ALF6's very wide code).
static int Width(const char* code, void* ctx)
{
    const int over = ctx ? *static_cast<int*>(ctx) : 0;
    return over ? over : 6 * (int)strlen(code);
}

// The counter's text: blocked iff the box overlaps the one rectangle the context names (none when null).
static bool Text(const Rect& b, const void* ctx)
{
    if (!ctx) return false;
    return overlapcount::IntersectArea(*static_cast<const Rect*>(ctx), b) > 0;
}

static Rect RightBox(int mx, int my, int w) { return Box(mx + 5, my - 3, w, H); }
static Rect LeftBox(int mx, int my, int w)  { return Box(mx - 5 - w, my - 3, w, H); }

static const Mark& Last() { return MarkAt(MarkCount() - 1); }
static bool Is(const Mark& m, Side s, int x, int y) { return m.at.side == s && m.at.x == x && m.at.y == y; }

int main()
{
    printf("airport label frame unit (src/AirportLabelFrame.cpp, linked as built)\n");
    static const char BDN[] = "BDN";

    FrameReset();
    row(MarkCount() == 0 && ObstacleCount() == 0 && !ObstacleOverflow(), "ALF1 a fresh frame holds no marks and no obstacles");

    FrameReset();
    AddMark(120, 120, BDN);
    Decide(H, FACE, Width, nullptr, Text, nullptr);
    row(Is(Last(), Side::Right, 125, 117), "ALF2 nothing recorded: the code goes right, at (mx+5, my-3)");

    FrameReset();
    AddMark(120, 120, BDN);
    row(Last().at.side == Side::Dropped, "ALF12 a mark is Dropped until Decide runs");

    FrameReset();
    static const char A1[] = "AAA", A2[] = "BBB";
    AddMark(60, 60, A1);
    AddMark(70, 70, A2);
    row(MarkCount() >= 2 && MarkAt(MarkCount() - 2).code == A1 && Last().code == A2 && Last().x == 70 && Last().y == 70,
        "ALF11 marks keep their order, their position and the very code pointer given");

    FrameReset();
    int wide = 200;
    AddMark(120, 120, BDN);
    Decide(H, FACE, Width, &wide, Text, nullptr);
    row(Last().at.side == Side::Dropped, "ALF6 the caller's width decides the box: 200 px fits neither side, so dropped");

    FrameReset();
    const Rect textOnRight = RightBox(120, 120, 18);
    AddMark(120, 120, BDN);
    Decide(H, FACE, Width, nullptr, Text, &textOnRight);
    row(Is(Last(), Side::Left, 97, 117), "ALF5 the counter's text on the right box: the code goes left");

    FrameReset();
    AddObstacle(RightBox(120, 120, 18));
    AddMark(120, 120, BDN);
    Decide(H, FACE, Width, nullptr, Text, nullptr);
    row(Is(Last(), Side::Left, 97, 117), "ALF3 a recorded obstacle on the right box: the code goes left");

    FrameReset();
    AddObstacle(RightBox(120, 120, 18));
    AddObstacle(LeftBox(120, 120, 18));
    AddMark(120, 120, BDN);
    Decide(H, FACE, Width, nullptr, Text, nullptr);
    row(Last().at.side == Side::Dropped, "ALF4 obstacles on both boxes: the code is dropped");

    FrameReset();
    AddObstacle(RightBox(140, 95, 18));
    AddMark(100, 140, BDN);
    AddMark(140, 95, BDN);
    AddMark(90, 170, BDN);
    Decide(H, FACE, Width, nullptr, Text, nullptr);
    {
        const int n = MarkCount();
        row(n >= 3 && Is(MarkAt(n - 3), Side::Right, 105, 137) && Is(MarkAt(n - 2), Side::Left, 117, 92)
                && Is(MarkAt(n - 1), Side::Right, 95, 167),
            "ALF10 every mark is decided: right, left (its right box recorded), right");
    }

    FrameReset();
    AddObstacle(RightBox(120, 120, 18));
    AddMark(120, 120, BDN);
    Decide(H, FACE, Width, nullptr, Text, nullptr);
    const bool blocked = Is(Last(), Side::Left, 97, 117);
    FrameReset();
    const bool clean = MarkCount() == 0 && ObstacleCount() == 0 && !ObstacleOverflow();
    AddMark(120, 120, BDN);
    Decide(H, FACE, Width, nullptr, Text, nullptr);
    row(blocked && clean && MarkCount() == 1 && Is(Last(), Side::Right, 125, 117),
        "ALF7 FrameReset forgets last frame: its obstacle and its marks are gone, so the same mark goes right");

    FrameReset();
    for (int i = 0; i < kObstacleCap; ++i) AddObstacle(Box(0, 0, 1, 1));
    AddMark(120, 120, BDN);
    Decide(H, FACE, Width, nullptr, Text, nullptr);
    row(!ObstacleOverflow() && Is(Last(), Side::Right, 125, 117),
        "ALF9 exactly kObstacleCap far-away obstacles: no overflow, the code goes right (ALF8's control)");

    FrameReset();
    for (int i = 0; i < kObstacleCap + 1; ++i) AddObstacle(Box(0, 0, 1, 1));
    AddMark(120, 120, BDN);
    Decide(H, FACE, Width, nullptr, Text, nullptr);
    row(ObstacleOverflow() && Last().at.side == Side::Dropped,
        "ALF8 one past kObstacleCap: the list overflows and answers blocked, so the code is dropped");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
