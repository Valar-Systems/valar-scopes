// Host test for the command-post identity rules (src/eam/AbncpIdentity.h, compiled as-is).
//
// The rulings it grades (Daniel, 2026-10-12):
//   1. "The ABNCP watchlist matches type B742 plus the E-4B registrations from public
//      sources" -- ruled AND, because B742 is every 747-200 and the VC-25As are B742 too.
//   2. Recognition on the device is a cited hex table, ADFEB3..ADFEB6: the hex is in the table
//      AND (the type is B742 or E4, or no type is given). "YES, type E4 counts too."
//   3. Rule 1 stays as a second check wherever a registration exists, unchanged (B742 only).
#include <cstdio>
#include "../../src/eam/AbncpIdentity.h"

static int failures = 0;

static void expect(bool ok, const char* what)
{
    if (!ok) { printf("  FAIL  %s\n", what); failures++; }
    else printf("  ok    %s\n", what);
}

int main()
{
    using abncp::IdentifyE4b;
    using abncp::IsE4b;

    // ------------------------------------------------------------------ rule 2: the hex table
    printf("  ---- rule 2: the cited hex table\n");
    expect(IdentifyE4b("adfeb3", "B742", ""), "ADFEB3 (73-1676) typed B742 is an E-4B");
    expect(IdentifyE4b("adfeb4", "B742", ""), "ADFEB4 (73-1677) typed B742 is an E-4B");
    expect(IdentifyE4b("adfeb5", "B742", ""), "ADFEB5 (74-0787) typed B742 is an E-4B");
    expect(IdentifyE4b("adfeb6", "B742", ""), "ADFEB6 (75-0125) typed B742 is an E-4B");

    // No type stated: the hex alone decides (every OpenSky row).
    expect(IdentifyE4b("adfeb3", "", ""), "a table hex with no type is an E-4B (OpenSky's case)");
    expect(IdentifyE4b("ADFEB4", nullptr, nullptr), "upper-case hex, null type, null registration");
    expect(IdentifyE4b(" adfeb5 ", "  ", ""), "surrounding spaces; a blank type is no type");

    // E4 (how adsbdb and hexdb.io type these airframes) counts too, on a table hex.
    expect(IdentifyE4b("adfeb3", "E4", ""), "ADFEB3 typed E4 is an E-4B");
    expect(IdentifyE4b("adfeb4", "E4", ""), "ADFEB4 typed E4 is an E-4B");
    expect(IdentifyE4b("adfeb5", "e4", ""), "ADFEB5 typed e4 (lower case) is an E-4B");
    expect(IdentifyE4b("adfeb6", "E4", ""), "ADFEB6 typed E4 is an E-4B");

    // A stated type that is neither B742 nor E4 names nothing, even on a table hex.
    expect(!IdentifyE4b("adfeb3", "B752", ""), "a table hex typed B752 is NOT named");
    expect(!IdentifyE4b("adfeb6", "C2", ""), "a table hex typed C2 is NOT named");
    expect(!IdentifyE4b("adfeb4", "E6", ""), "a table hex typed E6 is NOT named");
    expect(!IdentifyE4b("adfeb5", "E4B", ""), "a table hex typed E4B (not a designator here) is NOT named");

    // The other 747-200s: B742, military, and NOT in the table -- under B742 or E4.
    expect(!IdentifyE4b("adfdf8", "B742", ""), "VC-25A 82-8000 (ADFDF8, B742) is NOT an E-4B");
    expect(!IdentifyE4b("adfdf9", "B742", ""), "VC-25A 92-9000 (ADFDF9, B742) is NOT an E-4B");
    expect(!IdentifyE4b("adfdf8", "", ""), "VC-25A hex with no type is NOT an E-4B");
    expect(!IdentifyE4b("adfdf8", "E4", ""), "VC-25A hex typed E4 is NOT an E-4B");

    // A non-table hex typed E4 names nothing; only the registration rule (B742, unchanged) could.
    expect(!IdentifyE4b("ae0451", "E4", ""), "a non-table hex typed E4 is NOT named");
    expect(!IdentifyE4b("abcdef", "E4", ""), "an arbitrary non-table hex typed E4 is NOT named");
    expect(!IdentifyE4b("abcdef", "E4", "73-1676"), "non-table hex, E4 + an E-4B registration: NOT named (rule 1 needs B742)");
    expect(IdentifyE4b("abcdef", "B742", "73-1676"), "non-table hex, B742 + an E-4B registration: named by rule 1");

    // The old placeholder table: four C-2As.
    expect(!IdentifyE4b("ae0451", "", ""), "ae0451 (C-2A 162157) is NOT an E-4B");
    expect(!IdentifyE4b("ae0452", "", ""), "ae0452 (C-2A 162159) is NOT an E-4B");
    expect(!IdentifyE4b("ae0453", "", ""), "ae0453 (C-2A 162160) is NOT an E-4B");
    expect(!IdentifyE4b("ae0454", "", ""), "ae0454 (C-2A) is NOT an E-4B");
    expect(!IdentifyE4b("ae0451", "B742", ""), "ae0451 is not named even when typed B742");

    // The hex match is exact: a '~' (non-ICAO TIS-B id), a prefix, an extension, punctuation.
    expect(!IdentifyE4b("~adfeb3", "", ""), "~adfeb3 (a TIS-B track id) is NOT the airframe");
    expect(!IdentifyE4b("adfeb", "", ""), "a hex prefix does not match");
    expect(!IdentifyE4b("adfeb33", "", ""), "a hex extension does not match");
    expect(!IdentifyE4b("adf-eb3", "", ""), "punctuation inside a hex does not match");
    expect(!IdentifyE4b("", "", ""), "an empty row names nothing");
    expect(!IdentifyE4b(nullptr, nullptr, nullptr), "a null row names nothing");

    // ------------------------------------------- rule 1: B742 AND a public registration
    printf("  ---- rule 1: B742 AND a public registration (and the second check)\n");
    expect(IsE4b("B742", "73-1676"), "73-1676 typed B742 is an E-4B");
    expect(IsE4b("B742", "73-1677"), "73-1677 typed B742 is an E-4B");
    expect(IsE4b("B742", "74-0787"), "74-0787 typed B742 is an E-4B");
    expect(IsE4b("B742", "75-0125"), "75-0125 typed B742 is an E-4B");
    expect(IsE4b("b742", "731676"), "no hyphen, lower-case type still match");
    expect(!IsE4b("B742", "82-8000"), "VC-25A 82-8000 (B742) is NOT an E-4B");
    expect(!IsE4b("B742", "92-9000"), "VC-25A 92-9000 (B742) is NOT an E-4B");
    expect(!IsE4b("B742", ""), "B742 with no registration is NOT an E-4B");
    expect(!IsE4b("", "73-1676"), "an E-4B registration with no type is NOT an E-4B by rule 1");
    expect(!IsE4b("E4", "73-1676"), "rule 1 is unchanged: E4 + a registration is not rule 1 (B742 only)");
    expect(!IsE4b("B742", "73-167"), "a registration prefix does not match");
    expect(!IsE4b("B742", "73-16760"), "a registration extension does not match");
    expect(!IsE4b("B742", "---"), "punctuation only is not a registration");

    // Rule 1 through the row classifier.
    expect(IdentifyE4b("", "B742", "73-1676"), "no hex, B742 + 73-1676: named by rule 1");
    expect(IdentifyE4b("adfeb3", "", "73-1676"), "table hex, no type, an E-4B registration: named");
    expect(IdentifyE4b("adfeb3", "B742", "73-1676"), "everything agrees: named");
    expect(IdentifyE4b("adfeb3", "E4", "73-1676"), "table hex + E4 + an E-4B registration: named (rule 2)");
    // The second check: a stated registration that is not one of the four names nothing.
    expect(!IdentifyE4b("adfeb3", "B742", "82-8000"), "table hex + B742 but registration 82-8000: NOT named");
    expect(!IdentifyE4b("adfeb3", "E4", "82-8000"), "table hex + E4 but registration 82-8000: NOT named");
    expect(!IdentifyE4b("adfeb3", "", "162157"), "table hex but a C-2A registration: NOT named");
    expect(!IdentifyE4b("", "", "73-1676"), "a registration alone (no type, no hex) is NOT named");
    expect(!IdentifyE4b("adfdf8", "B742", "82-8000"), "the VC-25A as a full row is NOT named");

    // ------------------------------------------------------------------------- controls
    // Fail if the table were emptied or swapped (every positive above fails too; these state it
    // directly), or if E4 stopped counting. The "typed B752 / C2 / E6 / E4B" cases above fail if
    // the type check were dropped from rule 2.
    printf("  ---- controls\n");
    expect(sizeof(abncp::kE4bAirframes) / sizeof(abncp::kE4bAirframes[0]) == 4, "the table holds exactly four airframes");
    int named = 0;
    for (const abncp::Airframe& a : abncp::kE4bAirframes) {
        named += IdentifyE4b(a.hex, "", "") ? 1 : 0;
        named += IdentifyE4b(a.hex, "B742", "") ? 1 : 0;
        named += IdentifyE4b(a.hex, "E4", "") ? 1 : 0;
        named += IdentifyE4b("", "B742", a.registration) ? 1 : 0;
    }
    expect(named == 16, "every table row is named by its hex (no type, B742, E4) AND by B742 + its registration");

    if (failures) {
        printf("abncp identity: %d FAILED\n", failures);
        return 1;
    }
    printf("abncp identity: all passed\n");
    return 0;
}
