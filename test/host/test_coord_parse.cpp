// Host test for the config page's coordinate parsing.
//
// WHY IT EXISTS, and it is a customer-found defect rather than a theory. On
// 2026-09-17 the fresh-boot acceptance stalled at step 2 on an iPhone: the
// latitude and longitude boxes carried inputmode="decimal", and the iOS decimal
// keypad has no minus key. The hint beneath them said "No minus key? Write
// 98.753 W" -- and that keypad has no W either. The advice was unreachable on
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
    const double minus  = lon("-98.282");
    const double letter = lon("98.282 W");
    near(minus,  -98.282, "\"-98.282\"");
    near(letter, -98.282, "\"98.282 W\"");
    check(minus == letter, "the minus form and the W form are BIT-IDENTICAL");

    // The pasted pair -- the form the hint tells people to use, and the one that
    // needed SplitPair before the device could read it at all.
    double pl = 0.0, pg = 0.0;
    check(CoordParse::SplitPair(String("12.582, -98.753"), pl, pg),
          "\"12.582, -98.753\" splits into a pair");
    near(pl,  12.582,   "  ... its latitude");
    near(pg, -98.753,  "  ... its longitude");
    check(pg == lon("-98.753"),
          "the split longitude equals the same number typed alone");

    // And the equivalence again, through the pair path: a pasted pair written
    // with letters must land where the same pair written with a minus lands.
    double ql = 0.0, qg = 0.0;
    check(CoordParse::SplitPair(String("12.582 N, 98.753 W"), ql, qg),
          "\"12.582 N, 98.753 W\" splits into a pair");
    check(ql == pl && qg == pg, "letters and minus agree through the pair path too");

    // =====================================================================
    // 2. THE OTHER SPELLINGS THE HINT PROMISES. A hint is a contract.
    // =====================================================================
    printf("\n-- everything else the hint claims works --\n");
    near(lat("12.582"),          12.582,    "plain decimal degrees");
    near(lat("12.582 N"),        12.582,    "hemisphere letter trailing");
    near(lat("N 12.582"),        12.582,    "hemisphere letter leading");
    near(lat("12 34 56.7 N"),     12.582417, "degrees minutes seconds");
    near(lon("98 45 12 W"),   -98.753333, "DMS, western");
    near(lat("-12.582"),        -12.582,    "southern as a minus");
    near(lat("12.582 S"),       -12.582,    "southern as a letter");
    check(lat("12.582 S") == lat("-12.582"), "S and minus agree, as N/W do");

    // The degree sign and prime marks people paste from map sites. These arrive
    // as UTF-8 bytes from the form POST, which is what Fold() is for.
    near(lat("12.582\xC2\xB0N"),           12.582, "12.582 degree-sign N");
    near(lon("98.753\xC2\xB0W"),       -98.753, "98.753 degree-sign W");
    near(lat("12\xC2\xB0 34\xE2\x80\xB2 56.7\xE2\x80\xB3 N"),
         12.582417, "degree / prime / double-prime");
    // U+2212 MINUS SIGN -- the one non-ASCII character whose meaning matters.
    near(lon("\xE2\x88\x92\x39\x38.753"), -98.753, "unicode minus sign");

    // =====================================================================
    // 3. CONTROLS. A parser that accepts everything is not validating.
    // =====================================================================
    printf("\n-- what must be REFUSED --\n");
    double junk = 0.0;
    check(!CoordParse::Parse(String("Bend, Oregon"), true, junk), "a place name is refused");
    check(!CoordParse::Parse(String("97701"), true, junk),        "a ZIP code is out of range for a latitude");
    check(!CoordParse::Parse(String("98.753 W"), true, junk),    "a WEST value refused in the LATITUDE box");
    check(!CoordParse::Parse(String("12.582 N"), false, junk),    "a NORTH value refused in the LONGITUDE box");
    check(!CoordParse::Parse(String("91"), true, junk),           "91 is not a latitude");
    check(!CoordParse::Parse(String("181"), false, junk),         "181 is not a longitude");
    check(!CoordParse::Parse(String("44-3"), true, junk),         "\"44-3\" is a typo, not a coordinate");
    check(!CoordParse::Parse(String("44 70 N"), true, junk),      "70 minutes is a typo, not a coordinate");
    check(!CoordParse::Parse(String(""), true, junk),             "an empty box is not a location");
    check(!CoordParse::Parse(String("12.582, -98.753"), true, junk),
          "CONTROL: one box still refuses a PAIR -- that is SplitPair's job");

    double a = 0.0, b = 0.0;
    check(!CoordParse::SplitPair(String("Bend, Oregon"), a, b), "a pair of words is refused");
    check(!CoordParse::SplitPair(String("12.582"), a, b),       "a single number is not a pair");

    // CONTROL FOR THE WHOLE FILE. Every assertion above could be satisfied by a
    // parser that refuses everything, since most of them are refusals -- so one
    // that proves it still says yes.
    double ctl = 0.0;
    check(CoordParse::Parse(String("12.582"), true, ctl) && std::fabs(ctl - 12.582) < 1e-9,
          "CONTROL: the parser still ACCEPTS a plain valid coordinate");

    // =====================================================================
    // 4. Format round-trips, because it is what lands in NVS.
    // =====================================================================
    printf("\n-- what gets stored --\n");
    check(CoordParse::Format(-98.753) == "-98.753", "Format trims trailing zeros");
    check(CoordParse::Format(0.0) == "0",             "Format renders zero as 0, not -0");
    // 4 dp (~11 m), v15: precise on device. A 15-dp paste and a typed value store alike.
    check(CoordParse::Format(-98.753333) == "-98.7533", "Format stores 4 dp, not 6");
    check(CoordParse::Format(12.582448445829774) == "12.5824", "a 15-dp paste stores 4 dp");
    check(CoordParse::Format(-0.00004) == "0",        "a value that rounds to zero is 0, never -0");
    check(CoordParse::Format(-179.99996) == "-180",   "rounding carries across the integer");
    double rt = 0.0;
    check(CoordParse::Parse(CoordParse::Format(-98.753333), false, rt), "the stored form re-parses");
    near(rt, -98.7533, "  ... to the 4-dp number");

    // =====================================================================
    // 5. ParseLocation: what POST /location ("Use my location") will store.
    // =====================================================================
    printf("\n-- POST /location --\n");
    double sla = 0.0, slo = 0.0;
    check(CoordParse::ParseLocation(String("12.5824"), String("-98.7533"), sla, slo), "a real pair is accepted");
    check(!CoordParse::ParseLocation(String("0.0000"), String("0.0000"), sla, slo), "0,0 is REFUSED");
    check(!CoordParse::ParseLocation(String("0.00004"), String("-0.00003"), sla, slo), "a pair that ROUNDS to 0,0 is refused");
    check(CoordParse::ParseLocation(String("51.4779"), String("0"), sla, slo), "one zero axis (Greenwich) is fine");
    check(!CoordParse::ParseLocation(String(""), String("-98.7533"), sla, slo), "one box empty is refused");
    check(!CoordParse::ParseLocation(String("undefined"), String("-98.7533"), sla, slo), "garbage is refused");
    check(!CoordParse::ParseLocation(String("91"), String("10"), sla, slo), "an out-of-range latitude is refused");

    printf(failures ? "\ncoordinate parsing: %d FAILURE(S)\n" : "\ncoordinate parsing: all good\n",
           failures);
    return failures ? 1 : 0;
}
