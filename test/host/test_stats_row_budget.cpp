// Host test for the Stats face's vertical budget.
//
// The defect being pinned: rows were guarded and would not advance past the
// ceiling, but the inter-block gaps were bare `y += 6` statements and were not.
// A block whose heading did not fit still consumed its gap -- so whitespace was
// charged for content that was never drawn, and `y` could be pushed past the
// ceiling by gaps alone. Three different rows have been silently deleted by the
// budget over this face's life; this is the arithmetic underneath all three.
//
// It lives on the host because the bug was arithmetic, and arithmetic trapped
// inside a 300-line draw function is arithmetic nobody can check.
#include <cstdio>
#include "../../include/StatsRowPriority.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

using statsrows::Budget;

int main()
{
    std::printf("stats row budget\n");

    // The real s3-128 numbers: y starts at 48, lh 18, ceiling 172.
    const int Y0 = 48, LH = 18, CEIL = 172;

    // ---- ordinary rows ------------------------------------------------------
    {
        Budget b{ Y0, LH, CEIL, 0 };
        check(b.Take() == 48, "first row draws at the start");
        check(b.Take() == 66, "second row follows by one line height");
        check(b.y == 84, "y advances by lh per row");
    }

    // ---- THE BUG: a gap must not be charged for a row that never draws ------
    {
        Budget b{ CEIL - LH, LH, CEIL, 0 };   // room for exactly one more row
        b.Gap(6);
        check(b.Take() == -1, "a gap+row that will not fit is refused");
        check(b.y == CEIL - LH, "...and the REFUSED row consumed no y");
        check(b.pendingGap == 6, "...and the gap is still pending, not spent");

        // The old code would have done y += 6 here regardless, pushing y past
        // the ceiling using whitespace for a row that was never rendered.
        check(b.y + b.lh <= CEIL, "y never passes the ceiling via a gap alone");
    }

    // ---- a queued gap applies to the row that DOES fit ----------------------
    {
        Budget b{ 100, LH, CEIL, 0 };
        b.Gap(6);
        check(b.Take() == 106, "a pending gap is applied to the row that draws");
        check(b.pendingGap == 0, "...and is spent exactly once");
        check(b.Take() == 124, "the row after it gets no extra gap");
    }

    // ---- a heading must not be able to strand itself -----------------------
    // Seen on glass: "AIRCRAFT OF THE DAY" rendered with nothing beneath it,
    // because the block was guarded on ONE row -- exactly enough to draw a title
    // and lose the row that gives it meaning.
    {
        Budget b{ CEIL - LH, LH, CEIL, 0 };       // room for exactly one row
        check(b.Fits(), "one row fits");
        check(!b.FitsRows(2), "...but a heading plus its content does NOT");

        Budget c{ CEIL - 2 * LH, LH, CEIL, 0 };
        check(c.FitsRows(2), "two rows fit when there is room for two");
        check(!c.FitsRows(3), "...and three do not");
    }

    // A pending gap counts against a multi-row block too -- a block that would
    // fit only by ignoring its own leading gap does not fit.
    {
        Budget b{ CEIL - 2 * LH, LH, CEIL, 0 };
        check(b.FitsRows(2), "two rows fit with no gap queued");
        b.Gap(6);
        check(!b.FitsRows(2), "...and not once a 6 px gap is queued ahead of them");
    }

    // ---- CONTROL: the budget can actually refuse ---------------------------
    // Without this, a Take() that always succeeded would satisfy the positive
    // cases above and the whole guard would be decorative.
    {
        Budget b{ CEIL, LH, CEIL, 0 };
        check(b.Take() == -1, "CONTROL: a full face refuses a row");
        Budget c{ Y0, LH, CEIL, 0 };
        check(c.Take() >= 0, "CONTROL: ...but an empty one accepts");
    }

    // ---- the boundary, stated exactly --------------------------------------
    {
        Budget b{ CEIL - LH, LH, CEIL, 0 };
        check(b.Fits(), "a row ending exactly ON the ceiling fits");
        check(b.Take() == CEIL - LH, "...and draws");
        check(!b.Fits(), "the next one does not");
    }

    // ---- gaps do not accumulate --------------------------------------------
    // Two blocks skipped in a row must not queue 12 px of debt for the third.
    {
        Budget b{ 100, LH, CEIL, 0 };
        b.Gap(6);
        b.Gap(6);
        check(b.Take() == 106, "a second Gap replaces the first, it does not add");
    }

    // ---- the reserved count is what the renderer derives its ceiling from ---
    //
    // THESE TWO FIRED, AND THAT IS THE RECORD WORTH KEEPING. They were written
    // pinning RESERVED_ROWS == 2 and a ceiling of 172. When the address and the
    // reset control moved to the Connect screen they failed immediately -- on
    // the host, before a board was flashed, which is the whole reason a
    // geometry constant has a test at all. Updated, and still a tripwire.
    check(statsrows::RESERVED_ROWS == 0, "Stats reserves nothing: address and reset live on Connect");
    check(statsrows::ClockTopFor(210, LH) == 208, "the s3-128 ceiling is the clock row less the 2 px gap");
    // The budget the face actually got back by reserving nothing: two rows.
    check(statsrows::ClockTopFor(210, LH) - 172 == 2 * LH, "reserving nothing returned exactly two rows");

    if (failures == 0) std::printf("  ok\n");
    else               std::printf("  %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
