// Host test for the radar's collision counter.
//
// THE FIXTURE IS THE REAL GEOMETRY, not a convenient one. Every rectangle below
// is computed from the actual draw calls in DrawRadar at text size 1, where the
// default font is six pixels wide and eight tall and the label's line height is
// fontHeight + 1 = 9:
//
//     label row k     (x + 5,  y + 5 + 9k)      LabelLayout
//     NEW flag        (x + 11, y + 6)           the claimable badge
//     MIL/SPC/HELI    (x + 11, y - 3)           the special-class tag
//     NEAR/HIGH/FAST  (x - w - 9, y - 4 - 9s)   the highlight stack, s = slot
//
// If any of those move, these numbers stop matching and this file is the thing
// that says so -- which is the point of writing the arithmetic down rather than
// asserting that "some overlap was found".
//
// WHY A HOST TEST AND NOT A LOOK AT THE GLASS. A collision is a population
// property, not a rendering: the question is how often and how badly across a
// whole sky, and the panel shows one frame of whatever happens to be flying.
// The converse holds too, and this file cannot see it -- gold on green at the
// rim is a picture, and no count here will ever notice.
#include <cstdio>
#include "../../include/OverlapCount.h"

using overlapcount::Rect;
using overlapcount::IntersectArea;

static int failures = 0;

static void eq(uint32_t got, uint32_t want, const char* what)
{
    if (got != want) {
        printf("  FAIL  %s: got %lu, want %lu\n", what,
               (unsigned long)got, (unsigned long)want);
        ++failures;
    } else {
        printf("  ok    %s = %lu\n", what, (unsigned long)got);
    }
}

static void check(bool cond, const char* what)
{
    if (!cond) { printf("  FAIL  %s\n", what); ++failures; }
    else       { printf("  ok    %s\n", what); }
}

// The draw sites, as functions, so the fixture cannot drift from the note above
// without somebody editing the same line twice.
static Rect LabelRow(int x, int y, int rows, int w)
{
    return Rect{ (int16_t)(x + 5), (int16_t)(y + 5),
                 (int16_t)(x + 5 + w), (int16_t)(y + 5 + 9 * rows) };
}
static Rect NewFlag(int x, int y)
{
    return Rect{ (int16_t)(x + 11), (int16_t)(y + 6),
                 (int16_t)(x + 29), (int16_t)(y + 14) };
}
static Rect SpecialTag(int x, int y)
{
    return Rect{ (int16_t)(x + 11), (int16_t)(y - 3),
                 (int16_t)(x + 29), (int16_t)(y + 5) };
}
static Rect Highlight(int x, int y, int w, int slot)
{
    const int ty = y - 4 - 9 * slot;
    return Rect{ (int16_t)(x - w - 9), (int16_t)(ty),
                 (int16_t)(x - 9), (int16_t)(ty + 8) };
}

int main()
{
    printf("overlap count: badges are on the glass, so badges are in the count\n");

    // =====================================================================
    // 1. ONE CONTACT -- the badge-over-label case the counter was blind to.
    // =====================================================================
    printf("\n-- one contact at (100,100), a 60px one-line label --\n");
    const Rect label = LabelRow(100, 100, 1, 60);    // 105,105 .. 165,114
    const Rect flag  = NewFlag(100, 100);            // 111,106 .. 129,114
    const Rect mil   = SpecialTag(100, 100);         // 111, 97 .. 129,105
    const Rect near1 = Highlight(100, 100, 24, 0);   //  67, 96 ..  91,104

    // Eighteen pixels of gold, eight rows tall, printed through the first line
    // of the label -- on every claimable contact, every frame, by construction.
    eq(IntersectArea(label, flag), 144, "NEW flag over its OWN label");
    check(IntersectArea(label, flag) == IntersectArea(flag, label),
          "CONTROL: the intersection is symmetric");

    // THE SHARED-EDGE CASE. The tag ends at y=105 and the label starts at
    // y=105. With a <= in IntersectArea this would report a collision for every
    // military, special and helicopter contact on the scope, and not one of
    // them would be real.
    eq(IntersectArea(label, mil), 0, "MIL tag stops exactly where the label starts");

    // The highlight stack is drawn to the LEFT of the marker and the label to
    // the right, so a contact's own NEAR tag never touches its own label.
    eq(IntersectArea(label, near1), 0, "NEAR tag misses its own label");
    eq(IntersectArea(flag, mil),    0, "NEW and MIL do not stack on each other");

    {
        overlapcount::Frame<16> f;
        f.Reset();
        f.Add(label); f.Add(flag); f.Add(mil); f.Add(near1);
        eq(f.Pairs(), 1,   "one contact, one collision");
        eq(f.Px(),    144, "and 144 px of it");
        eq((uint32_t)f.Count(), 4, "CONTROL: all four rectangles were recorded");
        eq(f.Dropped(), 0, "CONTROL: nothing was dropped");
    }

    // =====================================================================
    // 2. TWO CONTACTS -- what the saved frame actually showed: a tag lying
    //    across a NEIGHBOUR's text, which no per-contact check can see.
    // =====================================================================
    printf("\n-- a second contact at (60,90), close enough to be written on --\n");
    const Rect labelB = LabelRow(60, 90, 1, 60);     // 65,95 .. 125,104

    eq(IntersectArea(labelB, near1), 192, "A's NEAR tag across B's label");
    // AND THE ONE THAT MAKES THE POINT: the MIL tag misses its own label by
    // design and still lands on the neighbour's. "Does this badge collide?" has
    // no answer that does not involve the other contacts.
    eq(IntersectArea(labelB, mil),    98, "A's MIL tag across B's label");
    eq(IntersectArea(labelB, label),   0, "the two labels themselves do not touch");
    eq(IntersectArea(labelB, flag),    0, "A's NEW flag misses B entirely");

    {
        overlapcount::Frame<16> f;
        f.Reset();
        f.Add(label); f.Add(flag); f.Add(mil); f.Add(near1); f.Add(labelB);
        eq(f.Pairs(), 3,   "three colliding pairs in the frame");
        eq(f.Px(),    434, "totalling 434 px (144 + 192 + 98)");
    }

    // =====================================================================
    // 3. THE COUNTER CAN REPORT ZERO. A counter that always finds something
    //    is not measuring anything.
    // =====================================================================
    printf("\n-- an uncrowded sky --\n");
    {
        overlapcount::Frame<16> f;
        f.Reset();
        f.Add(LabelRow(20, 20, 1, 40));
        f.Add(LabelRow(160, 190, 1, 40));
        eq(f.Pairs(), 0, "two contacts far apart collide not at all");
        eq(f.Px(),    0, "and no area either");
        eq((uint32_t)f.Count(), 2, "CONTROL: they were both recorded");
    }
    {
        overlapcount::Frame<16> f;
        f.Reset();
        eq(f.Pairs(), 0, "an empty frame counts nothing");
        f.Add(LabelRow(100, 100, 3, 60));
        eq(f.Pairs(), 0, "one rectangle cannot collide with itself");
    }

    // =====================================================================
    // 4. SATURATION IS REPORTED, NOT SWALLOWED.
    //
    //    The device sizes its frame so this cannot happen (static_assert at
    //    the call site). It is tested anyway: a cap that has never been
    //    reached is a cap nobody has watched behave, and the failure it would
    //    produce -- a LOW overlap count in a BUSY sky -- is the exact reading
    //    that would be taken as "the display is coping fine".
    // =====================================================================
    printf("\n-- a frame too small for its sky --\n");
    {
        overlapcount::Frame<2> f;
        f.Reset();
        f.Add(label);   // stored
        f.Add(flag);    // stored, and collides with the label
        f.Add(mil);     // past the cap
        eq((uint32_t)f.Count(), 2, "only two rectangles fit");
        eq(f.Dropped(), 1, "and the third is COUNTED AS DROPPED, not ignored");
        eq(f.Pairs(), 1, "the dropped rectangle still scored against what fit");
    }

    // =====================================================================
    // 5. Reset really resets. The frame is reused every frame, so a leak here
    //    would make the count climb monotonically until reboot.
    // =====================================================================
    printf("\n-- the frame is reused --\n");
    {
        overlapcount::Frame<16> f;
        f.Reset();
        f.Add(label); f.Add(flag);
        check(f.Pairs() > 0, "CONTROL: there was something to clear");
        f.Reset();
        eq(f.Pairs(), 0, "Reset clears the pair count");
        eq(f.Px(), 0, "Reset clears the area");
        eq((uint32_t)f.Count(), 0, "Reset clears the rectangles");
        eq(f.Dropped(), 0, "Reset clears the drop count");
    }

    printf(failures ? "\noverlap count: %d FAILURE(S)\n" : "\noverlap count: all good\n",
           failures);
    return failures ? 1 : 0;
}
