// The Follow label sanitiser, and the regression that made it worth testing.
//
// A charset filter allowing only A-Z0-9 shipped to a bench board on 2026-08-29
// and was caught on glass. It mangles every hyphenated registration in the
// world: G-ABCD -> GABCD, D-AIBL -> DAIBL, VH-OQA -> VHOQA. The output still
// looks like a legitimate identifier, which is what makes it dangerous -- an
// overflowing string is obviously wrong, a silently corrupted one is not.
//
// So the hyphenated cases below are the point of this file. Everything else is
// scaffolding around them.

#include "../../include/FollowLabel.h"

#include <cstdio>
#include <cstring>
#include <string>

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL  %s\n", what); }
}

static std::string San(const char* in)
{
    char buf[64];
    follow::SanitiseLabel(in, buf, sizeof(buf));
    return std::string(buf);
}

static void expect(const char* in, const char* want)
{
    const std::string got = San(in);
    const bool ok = (got == want);
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FAIL  \"%s\" -> \"%s\", expected \"%s\"\n",
                    in, got.c_str(), want);
    }
}

int main()
{
    std::printf("== follow label sanitiser ==\n");

    // ---- THE REGRESSION. Hyphens are part of a registration, not punctuation
    // to be tidied away. Each of these is a real aircraft registration format.
    std::printf("  ---- hyphenated registrations survive intact\n");
    expect("G-ABCD",  "G-ABCD");    // United Kingdom
    expect("D-AIBL",  "D-AIBL");    // Germany
    expect("VH-OQA",  "VH-OQA");    // Australia
    expect("F-GSTB",  "F-GSTB");    // France (no hyphen after the first letter
                                    // in some formats -- still must not change)
    expect("ZK-NZQ",  "ZK-NZQ");    // New Zealand
    expect("PH-BFA",  "PH-BFA");    // Netherlands

    // The exact string that exposed it on the bench.
    expect("bench-unknown", "BENCH-UNKNOWN");

    // ---- and the control: prove the test can SEE the old behaviour ----------
    // If someone reinstates the A-Z0-9-only filter, the cases above fail. This
    // asserts the reverse explicitly, so the reason they exist survives even if
    // the list above is ever "tidied".
    {
        const std::string g = San("G-ABCD");
        check(g != "GABCD",
              "CONTROL: G-ABCD must NOT collapse to GABCD (the 2026-08-29 bug)");
        check(g.find('-') != std::string::npos,
              "CONTROL: the hyphen is still present in the output");
    }

    // ---- case folding ------------------------------------------------------
    std::printf("  ---- lowercase is folded up, digits pass\n");
    expect("baw117",  "BAW117");
    expect("N123AB",  "N123AB");
    expect("g-abcd",  "G-ABCD");

    // ---- characters that genuinely carry no meaning here -------------------
    std::printf("  ---- meaningless characters are dropped, not substituted\n");
    expect("BAW 117",       "BAW117");     // the feed space-pads flight IDs
    expect("BAW/117",       "BAW117");
    expect("  G-ABCD  ",    "G-ABCD");
    expect("<script>",      "SCRIPT");
    expect("",              "");

    // ---- NO length cap here: FitToDisc truncates visibly, by width ----------
    // A second cap in the sanitiser was silent, cruder, and fired first, which
    // made the good one unreachable. Anything inside the cost bound passes
    // through whole and the panel decides what fits.
    std::printf("  ---- no silent shortening inside the cost bound\n");
    expect("ABCDEFGHIJKL", "ABCDEFGHIJKL");           // 12, longer than any
                                                      // callsign, still intact
    check(San("ABCDEFGHIJKL").size() == 12,
          "a 12-char identifier is not shortened by the sanitiser");

    // ---- the cost bound marks itself when it bites -------------------------
    std::printf("  ---- the cost bound is visible when reached\n");
    {
        const std::string longish(follow::LABEL_COST_BOUND + 10, 'A');
        const std::string got = San(longish.c_str());
        check(got.size() <= follow::LABEL_COST_BOUND + 3,
              "output stays within the cost bound plus its marker");
        check(got.rfind("...") == got.size() - 3,
              "truncation at the cost bound is VISIBLE (trailing ...)");
    }
    {
        // Exactly at the bound: nothing was dropped, so nothing is marked.
        const std::string atBound(follow::LABEL_COST_BOUND, 'A');
        const std::string got = San(atBound.c_str());
        check(got == atBound,
              "a string exactly at the cost bound is unmarked and unchanged");
    }

    // ---- buffer discipline -------------------------------------------------
    std::printf("  ---- never writes past the buffer, always terminates\n");
    {
        char small[4];
        std::memset(small, 'X', sizeof(small));
        const size_t n = follow::SanitiseLabel("G-ABCD", small, sizeof(small));
        check(n < sizeof(small), "respects a small buffer");
        check(small[sizeof(small) - 1] == '\0' || std::strlen(small) < sizeof(small),
              "NUL-terminated in a small buffer");
    }
    {
        char one[1];
        one[0] = 'X';
        check(follow::SanitiseLabel("G-ABCD", one, sizeof(one)) == 0,
              "a 1-byte buffer writes only the NUL");
        check(one[0] == '\0', "and terminates it");
    }
    check(follow::SanitiseLabel("G-ABCD", nullptr, 0) == 0, "null buffer is safe");
    {
        char b[8];
        check(follow::SanitiseLabel(nullptr, b, sizeof(b)) == 0, "null input is safe");
        check(b[0] == '\0', "and yields an empty string");
    }

    // ---- THE GLOBE HEADER: MEASURE, THEN FIT OR OMIT -----------------------
    //
    // Reported from the bench as "LHR -..." with the destination gone. The old
    // header was a concatenation handed to a width-fitter, and the ellipsis --
    // which is honest on a lone identifier -- lies in a composite: it attaches
    // to whichever component came last, so the first reads whole and the rest
    // read as absent.
    //
    // The measuring stays on the device; this grades the POLICY. Every case
    // below is checked twice over: once for which form it picks, and once for
    // the invariant that matters more than any individual choice -- NO FORM
    // EVER CONTAINS A PARTIAL COMPONENT, because none of them is built by
    // cutting.
    std::printf("  ---- the globe header fits or omits, never cuts\n");
    {
        using follow::HeaderForm;
        using follow::HeaderFormFor;

        // Codes at the route's ends: the header names the flight and nothing else.
        check(HeaderFormFor(true,  false, false, false) == HeaderForm::LabelOnly,
              "codes at the ends -> the header is the flight alone");
        check(HeaderFormFor(true,  true,  true,  true)  == HeaderForm::LabelOnly,
              "... and that does not change just because more would fit");

        // Everything fits on one row: say it all.
        check(HeaderFormFor(false, true,  true,  true)  == HeaderForm::Combined,
              "everything fits -> one row with both");

        // THE REPORTED CASE. BENCH-LHR-JFK + LHR -> JFK is 25 chars against 23.
        check(HeaderFormFor(false, false, true,  true)  == HeaderForm::RouteThenLabel,
              "combined overflows -> route on the header, label whole beneath it");

        // CONTROL: the old behaviour was to cut the combined string. There is no
        // form that means "cut", so the assertion is that the overflow case does
        // NOT resolve to Combined -- the one outcome that would put a truncated
        // composite on the glass.
        check(HeaderFormFor(false, false, true,  true)  != HeaderForm::Combined,
              "CONTROL: an overflowing combined string is never chosen anyway");

        // A label too long to fit whole ANYWHERE does not take the codes down
        // with it. Dropping the flight leaves a route; dropping the route leaves
        // a globe with an unnamed line on it.
        check(HeaderFormFor(false, false, true,  false) == HeaderForm::RouteOnly,
              "an unfittable label is omitted; the codes stay");
        check(HeaderFormFor(false, false, true,  false) != HeaderForm::Nothing,
              "CONTROL: ... and it does not give up on the whole header");

        // The honest end of the ladder. No real pair of codes reaches it, and it
        // is asserted anyway: an unreachable branch that returns something wrong
        // is how it stops being unreachable.
        check(HeaderFormFor(false, false, false, true)  == HeaderForm::Nothing,
              "not even the route fits -> draw nothing, do not draw something cut");
        check(HeaderFormFor(false, false, false, false) == HeaderForm::Nothing,
              "... and likewise with nothing else available either");

        // EXHAUSTIVE, because the rule is 16 inputs wide and small enough to
        // enumerate. The invariant: the codes survive in every form reachable
        // with labelAtEnds false and routeFits true, which is the whole point of
        // the change -- the codes appear nowhere else on the face in that branch.
        {
            int codesLost = 0, forms[5] = {0, 0, 0, 0, 0};
            for (int i = 0; i < 16; ++i) {
                const bool ends = (i & 8) != 0, comb = (i & 4) != 0;
                const bool rt   = (i & 2) != 0, lab  = (i & 1) != 0;
                const HeaderForm f = HeaderFormFor(ends, comb, rt, lab);
                forms[(int)f]++;
                if (!ends && rt && f == HeaderForm::Nothing) ++codesLost;
            }
            check(codesLost == 0,
                  "with the codes unshown at the ends and a route that fits, "
                  "no input drops them");
            check(forms[(int)HeaderForm::LabelOnly] == 8,
                  "CONTROL: half the inputs are labelAtEnds and take the short path");
        }
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
