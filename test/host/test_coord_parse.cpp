// Host test for the config page's coordinate parsing.
//
// WHY IT EXISTS, and it is a customer-found defect rather than a theory. On
// 2026-09-17 the fresh-boot acceptance stalled at step 2 on an iPhone: the
// latitude and longitude boxes carried inputmode="decimal", and the iOS decimal
// keypad has no minus key. The hint beneath them said "No minus key? Write
// 121.315 W" -- and that keypad has no W either. The advice was unreachable on
// the one device it was written for, so a western longitude could not be entered
// at all without pasting.
//
// The fix is inputmode="text". This file guards the half of it that can rot
// silently: that every spelling the hint now promises actually parses, and that
// the minus form and the letter form land on the SAME number. A hint that
// promises an equivalence the parser does not implement is the same defect
// again, one layer down.
//
// IT GRADES THE SHIPPING PARSER, not a copy. include/CoordParse.h is compiled
// here as-is, against a minimal Arduino String (test/host/arduino_shim) -- see
// that header for exactly what the shim can and cannot catch.
#include <cstdio>
#include <cmath>
#include "../../include/CoordParse.h"

static int failures = 0;

static void near(double got, double want, const char* what)
{
    // 1e-6 deg is ~11 cm, the precision CoordParse::Format stores at.
    if (std::fabs(got - want) > 1e-6) {
        printf("  FAIL  %s: got %.6f, want %.6f\n", what, got, want);
        ++failures;
    } else {
        printf("  ok    %s = %.6f\n", what, got);
    }
}

static void check(bool cond, const char* what)
{
    if (!cond) { printf("  FAIL  %s\n", what); ++failures; }
    else       { printf("  ok    %s\n", what); }
}

static double lon(const char* s)
{
    double v = 0.0;
    if (!CoordParse::Parse(String(s), false, v)) { printf("  FAIL  longitude did not parse: %s\n", s); ++failures; return 1e9; }
    return v;
}

static double lat(const char* s)
{
    double v = 0.0;
    if (!CoordParse::Parse(String(s), true, v)) { printf("  FAIL  latitude did not parse: %s\n", s); ++failures; return 1e9; }
    return v;
}

int main()
{
    printf("coordinate parsing: the three spellings a customer actually types\n");

    // =====================================================================
    // 1. THE ACCEPTANCE CASE. The minus and the letter are the same place.
    // =====================================================================
    printf("\n-- the same longitude, spelled three ways --\n");
    const double minus  = lon("-121.286");
    const double letter = lon("121.286 W");
    near(minus,  -121.286, "\"-121.286\"");
    near(letter, -121.286, "\"121.286 W\"");
    check(minus == letter, "the minus form and the W form are BIT-IDENTICAL");

    // The pasted pair -- the form the hint tells people to use, and the one that
    // needed SplitPair before the device could read it at all.
    double pl = 0.0, pg = 0.0;
    check(CoordParse::SplitPair(String("44.058, -121.315"), pl, pg),
          "\"44.058, -121.315\" splits into a pair");
    near(pl,  44.058,   "  ... its latitude");
    near(pg, -121.315,  "  ... its longitude");
    check(pg == lon("-121.315"),
          "the split longitude equals the same number typed alone");

    // And the equivalence again, through the pair path: a pasted pair written
    // with letters must land where the same pair written with a minus lands.
    double ql = 0.0, qg = 0.0;
    check(CoordParse::SplitPair(String("44.058 N, 121.315 W"), ql, qg),
          "\"44.058 N, 121.315 W\" splits into a pair");
    check(ql == pl && qg == pg, "letters and minus agree through the pair path too");

    // =====================================================================
    // 2. THE OTHER SPELLINGS THE HINT PROMISES. A hint is a contract.
    // =====================================================================
    printf("\n-- everything else the hint claims works --\n");
    near(lat("44.058"),          44.058,    "plain decimal degrees");
    near(lat("44.058 N"),        44.058,    "hemisphere letter trailing");
    near(lat("N 44.058"),        44.058,    "hemisphere letter leading");
    near(lat("44 3 29.4 N"),     44.058167, "degrees minutes seconds");
    near(lon("121 18 55 W"),   -121.315278, "DMS, western");
    near(lat("-44.058"),        -44.058,    "southern as a minus");
    near(lat("44.058 S"),       -44.058,    "southern as a letter");
    check(lat("44.058 S") == lat("-44.058"), "S and minus agree, as N/W do");

    // The degree sign and prime marks people paste from map sites. These arrive
    // as UTF-8 bytes from the form POST, which is what Fold() is for.
    near(lat("44.058\xC2\xB0N"),           44.058, "44.058 degree-sign N");
    near(lon("121.315\xC2\xB0W"),       -121.315, "121.315 degree-sign W");
    near(lat("44\xC2\xB0 3\xE2\x80\xB2 29.4\xE2\x80\xB3 N"),
         44.058167, "degree / prime / double-prime");
    // U+2212 MINUS SIGN -- the one non-ASCII character whose meaning matters.
    near(lon("\xE2\x88\x92\x31\x32\x31.315"), -121.315, "unicode minus sign");

    // =====================================================================
    // 3. CONTROLS. A parser that accepts everything is not validating.
    // =====================================================================
    printf("\n-- what must be REFUSED --\n");
    double junk = 0.0;
    check(!CoordParse::Parse(String("Bend, Oregon"), true, junk), "a place name is refused");
    check(!CoordParse::Parse(String("97701"), true, junk),        "a ZIP code is out of range for a latitude");
    check(!CoordParse::Parse(String("121.315 W"), true, junk),    "a WEST value refused in the LATITUDE box");
    check(!CoordParse::Parse(String("44.058 N"), false, junk),    "a NORTH value refused in the LONGITUDE box");
    check(!CoordParse::Parse(String("91"), true, junk),           "91 is not a latitude");
    check(!CoordParse::Parse(String("181"), false, junk),         "181 is not a longitude");
    check(!CoordParse::Parse(String("44-3"), true, junk),         "\"44-3\" is a typo, not a coordinate");
    check(!CoordParse::Parse(String("44 70 N"), true, junk),      "70 minutes is a typo, not a coordinate");
    check(!CoordParse::Parse(String(""), true, junk),             "an empty box is not a location");
    check(!CoordParse::Parse(String("44.058, -121.315"), true, junk),
          "CONTROL: one box still refuses a PAIR -- that is SplitPair's job");

    double a = 0.0, b = 0.0;
    check(!CoordParse::SplitPair(String("Bend, Oregon"), a, b), "a pair of words is refused");
    check(!CoordParse::SplitPair(String("44.058"), a, b),       "a single number is not a pair");

    // CONTROL FOR THE WHOLE FILE. Every assertion above could be satisfied by a
    // parser that refuses everything, since most of them are refusals -- so one
    // that proves it still says yes.
    double ctl = 0.0;
    check(CoordParse::Parse(String("44.058"), true, ctl) && std::fabs(ctl - 44.058) < 1e-9,
          "CONTROL: the parser still ACCEPTS a plain valid coordinate");

    // =====================================================================
    // 4. Format round-trips, because it is what lands in NVS.
    // =====================================================================
    printf("\n-- what gets stored --\n");
    check(CoordParse::Format(-121.315) == "-121.315", "Format trims trailing zeros");
    check(CoordParse::Format(0.0) == "0",             "Format renders zero as 0, not -0");
    double rt = 0.0;
    check(CoordParse::Parse(CoordParse::Format(-121.315278), false, rt), "the stored form re-parses");
    near(rt, -121.315278, "  ... to the same number");

    printf(failures ? "\ncoordinate parsing: %d FAILURE(S)\n" : "\ncoordinate parsing: all good\n",
           failures);
    return failures ? 1 : 0;
}
