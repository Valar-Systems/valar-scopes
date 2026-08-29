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

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
