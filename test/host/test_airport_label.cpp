// The v20 airport-label sweep (docs/v20-label-collision.md, row a). A POPULATION, not one case: it counts where the
// airport code lands (right / left / dropped) and whether it lands on traffic text or an aircraft symbol, under the
// OLD rule (blind, always right) and under airportlabel::Place.
//
// The expected side of every scene is derived HERE, independently of the header (right iff the right box is clear;
// left iff right is blocked and left is clear; dropped iff both are blocked), so the identity row cannot simply agree
// with whatever Place does.
//
// The geometry is the device's:
//   - the font is 6 x 8 px a character at size 1, and a label row is fontHeight() + 1 = 9 px;
//   - LabelLayout puts row k of a contact at (x, y) at (x+5, y+5+9k);
//   - the NEAR tag is at (x - width - 9, y - 4);
//   - the airport code is at (mx+5, my-3) when right.
#include <cstdio>
#include "../../include/AirportLabel.h"

using airportlabel::Box;
using airportlabel::Obstacles;
using airportlabel::Placement;
using airportlabel::Rect;
using airportlabel::Side;

static int failures = 0;
static void row(bool cond, const char* what)
{
    printf("  %s %s\n", cond ? "ok  " : "BAD ", what);
    if (!cond) ++failures;
}

static const int CW = 6, CH = 8, ROW = 9;
static const int CODE_W = 3 * CW, CODE_H = CH;        // "BDN"
static const int LABEL_CHARS = 7;                     // e.g. a callsign over a type

static Rect LabelBox(int x, int y, int lines) { return Box(x + 5, y + 5, LABEL_CHARS * CW, ROW * lines - 1); }
static Rect NearTag(int x, int y)             { return Box(x - 4 * CW - 9, y - 4, 4 * CW, CH); }
static Rect Symbol(int x, int y)              { return Box(x - 6, y - 6, 13, 13); }   // the default dart, tip 6 px

typedef Obstacles<16> List;

struct Counts {
    long scenes = 0, right = 0, left = 0, dropped = 0;
    long overText = 0, overSymbol = 0, identityMiss = 0;
    long beforeOverText = 0, beforeOverSymbol = 0;
};

static bool Overlaps(const List& l, const Rect& r) { return l.Hits(r); }

static void Scene(Counts& c, int mx, int my, const List& text, const List& symbols)
{
    ++c.scenes;
    // The OLD rule, written here, not taken from the header: blind, always right.
    const Rect old = Box(mx + 5, my - 3, CODE_W, CODE_H);
    if (Overlaps(text, old))    ++c.beforeOverText;
    if (Overlaps(symbols, old)) ++c.beforeOverSymbol;

    const Placement p = airportlabel::Place(mx, my, CODE_W, CODE_H,
                                            [&](const Rect& b) { return text.Hits(b); },
                                            [&](const Rect& b) { return symbols.Hits(b); });

    // The expected side, derived independently.
    const Rect r = Box(mx + 5, my - 3, CODE_W, CODE_H);
    const Rect l = Box(mx - 5 - CODE_W, my - 3, CODE_W, CODE_H);
    const bool rBlocked = Overlaps(text, r) || Overlaps(symbols, r);
    const bool lBlocked = Overlaps(text, l) || Overlaps(symbols, l);
    const Side want = !rBlocked ? Side::Right : (!lBlocked ? Side::Left : Side::Dropped);
    if (p.side != want) ++c.identityMiss;

    if (p.side == Side::Right)   ++c.right;
    if (p.side == Side::Left)    ++c.left;
    if (p.side == Side::Dropped) { ++c.dropped; return; }
    const Rect placed = Box(p.x, p.y, CODE_W, CODE_H);
    if (Overlaps(text, placed))    ++c.overText;
    if (Overlaps(symbols, placed)) ++c.overSymbol;
}

// Population 1: one contact at every offset from the mark; 2- and 3-line labels, with and without the NEAR tag.
static Counts OneContact(int face)
{
    Counts c;
    const int mx = face / 2, my = face / 2;
    for (int lines = 2; lines <= 3; ++lines)
        for (int nearTag = 0; nearTag <= 1; ++nearTag)
            for (int dx = -48; dx <= 48; dx += 2)
                for (int dy = -32; dy <= 32; dy += 2) {
                    const int x = mx + dx, y = my + dy;
                    List text, symbols;
                    text.Add(LabelBox(x, y, lines));
                    if (nearTag) text.Add(NearTag(x, y));
                    symbols.Add(Symbol(x, y));
                    Scene(c, mx, my, text, symbols);
                }
    return c;
}

// Populations 2 and 3: the same sweep with a second contact B fixed so that its label covers ONE candidate.
//   - B on the LEFT (bx = mx-50): its label spans mx-45..mx-3, so it covers the left candidate (mx-23..mx-5) and not
//     the right (from mx+5). LEFT therefore CANNOT occur here: AL16 asserts that construction, scene by scene.
//   - B on the RIGHT (bx = mx+2): its label spans mx+7..mx+49, covering the right candidate and not the left. Right
//     is always blocked, so LEFT is exercised with two contacts (AL17).
// `bBlocksLeft` counts the scenes where B's own label lands on the left candidate.
static Counts TwoContacts(int face, int bdx, long* bBlocksLeft)
{
    Counts c;
    const int mx = face / 2, my = face / 2;
    const int bx = mx + bdx, by = my - 8;
    const Rect leftCandidate = Box(mx - 5 - CODE_W, my - 3, CODE_W, CODE_H);
    for (int dx = -48; dx <= 48; dx += 2)
        for (int dy = -32; dy <= 32; dy += 2) {
            const int x = mx + dx, y = my + dy;
            List text, symbols;
            text.Add(LabelBox(x, y, 2));
            text.Add(LabelBox(bx, by, 2));
            symbols.Add(Symbol(x, y));
            symbols.Add(Symbol(bx, by));
            if (bBlocksLeft && overlapcount::IntersectArea(LabelBox(bx, by, 2), leftCandidate)) ++*bBlocksLeft;
            Scene(c, mx, my, text, symbols);
        }
    return c;
}

static void Print(const char* name, int face, const Counts& c)
{
    printf("    %-13s face %3d: scenes %ld | right %ld left %ld dropped %ld | over-text %ld over-symbol %ld | "
           "BEFORE over-text %ld over-symbol %ld | identity misses %ld\n",
           name, face, c.scenes, c.right, c.left, c.dropped, c.overText, c.overSymbol, c.beforeOverText,
           c.beforeOverSymbol, c.identityMiss);
}

int main()
{
    printf("airport label: the code yields to traffic text and symbols; the mark always stays\n");

    // CONTROL: no obstacle at all, so every label goes right.
    {
        List none;
        long right = 0, n = 0;
        for (int dx = -48; dx <= 48; dx += 2) {
            const Placement p = airportlabel::Place(120 + dx, 120, CODE_W, CODE_H,
                                                    [&](const Rect& b) { return none.Hits(b); },
                                                    [&](const Rect& b) { return none.Hits(b); });
            ++n;
            if (p.side == Side::Right && p.x == 120 + dx + 5 && p.y == 117) ++right;
        }
        row(right == n, "AL1 CONTROL: no obstacle -> every label RIGHT at (mx+5, my-3)");
    }

    // The two read-only queries.
    {
        overlapcount::Frame<2> f;
        row(!f.Hits(Box(0, 0, 10, 10)), "AL2 Frame::Hits on an empty frame is false");
        f.Add(Box(0, 0, 10, 10));
        row(f.Hits(Box(5, 5, 10, 10)) && !f.Hits(Box(10, 0, 5, 5)),
            "AL3 Frame::Hits: an overlap is true, a shared edge is false");
        const uint32_t pairs = f.Pairs();
        (void)f.Hits(Box(0, 0, 10, 10));
        row(f.Pairs() == pairs && f.Count() == 1, "AL4 Frame::Hits counts nothing and adds nothing");
        f.Add(Box(100, 100, 5, 5));
        f.Add(Box(200, 200, 5, 5));                                  // past the cap of 2: dropped
        row(f.Dropped() == 1 && f.Hits(Box(500, 500, 1, 1)), "AL5 Frame::Hits is TRUE once anything was dropped");
        Obstacles<1> o;
        o.Add(Box(0, 0, 1, 1));
        o.Add(Box(50, 50, 1, 1));
        row(o.Overflow() && o.Hits(Box(900, 900, 1, 1)), "AL6 Obstacles::Hits is TRUE once the list overflowed");
    }

    long bLeft = 0;
    const Counts p1a = OneContact(240), p1b = OneContact(960);
    const Counts p2a = TwoContacts(240, -50, &bLeft), p2b = TwoContacts(960, -50, nullptr);
    const Counts p3a = TwoContacts(240, 2, nullptr), p3b = TwoContacts(960, 2, nullptr);
    Print("one contact", 240, p1a);
    Print("one contact", 960, p1b);
    Print("two, B left", 240, p2a);
    Print("two, B left", 960, p2b);
    Print("two, B right", 240, p3a);
    Print("two, B right", 960, p3b);

    row(p1a.beforeOverText > 0 && p2a.beforeOverText > 0 && p3a.beforeOverText > 0,
        "AL7 BEFORE (the old rule): over-text > 0 in every population");
    row(p1a.beforeOverSymbol > 0, "AL8 BEFORE: over-symbol > 0");
    row(p1a.overText == 0 && p2a.overText == 0 && p3a.overText == 0, "AL9 AFTER: over-text == 0 exactly");
    row(p1a.overSymbol == 0 && p2a.overSymbol == 0 && p3a.overSymbol == 0, "AL10 AFTER: over-symbol == 0 exactly");
    row(p1a.identityMiss == 0 && p2a.identityMiss == 0 && p3a.identityMiss == 0,
        "AL11 AFTER: every scene's side is the independent rule's (right / left / dropped)");
    row(p1a.right + p1a.left + p1a.dropped == p1a.scenes && p2a.right + p2a.left + p2a.dropped == p2a.scenes &&
            p3a.right + p3a.left + p3a.dropped == p3a.scenes,
        "AL12 AFTER: right + left + dropped == scenes");
    row(p1a.left > 0, "AL13 AFTER: left > 0 (the left candidate is used)");
    row(p2a.dropped > 0, "AL14 AFTER: dropped > 0 with B on the left");
    row(p1a.scenes == p1b.scenes && p1a.right == p1b.right && p1a.left == p1b.left && p1a.dropped == p1b.dropped &&
            p2a.right == p2b.right && p2a.left == p2b.left && p2a.dropped == p2b.dropped &&
            p3a.right == p3b.right && p3a.left == p3b.left && p3a.dropped == p3b.dropped,
        "AL15 faces 240 and 960 read identical counts (the placement is relative to the mark)");
    row(bLeft == p2a.scenes && p2a.left == 0,
        "AL16 B on the left blocks the left candidate in EVERY scene, so left == 0 there by construction");
    row(p3a.left > 0 && p3a.right == 0, "AL17 B on the right: right is always blocked and LEFT is used (two contacts)");

    printf(failures ? "\nairport label: %d FAILURE(S)\n" : "\nairport label: all good\n", failures);
    return failures ? 1 : 0;
}
