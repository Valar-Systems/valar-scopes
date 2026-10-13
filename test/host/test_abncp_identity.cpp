// Host test for the command-post identity rules (src/eam/AbncpIdentity.h, compiled as-is).
//
// The rulings it grades (Daniel, 2026-10-12):
//   1. "The ABNCP watchlist matches type B742 plus the E-4B registrations from public
//      sources" -- ruled AND, because B742 is every 747-200 and the VC-25As are B742 too.
//   2. Recognition on the device is a cited hex table, ADFEB3..ADFEB6: the hex is in the table
//      AND (the type is B742 or E4, or no type is given). "YES, type E4 counts too."
//   3. Rule 1 stays as a second check wherever a registration exists, unchanged (B742 only).
//   4. "OpenSky mode always includes the four built-in E-4B hexes (ADFEB3-ADFEB6) in its
//      watchlist query, alongside the user's own hexes." (src/eam/AbncpWatch.h, the same
//      template the firmware instantiates with Arduino String.)
#include <cstdio>
#include <string>
#include <vector>
#include "../../src/eam/AbncpIdentity.h"
#include "../../src/eam/AbncpWatch.h"

static int failures = 0;

static void expect(bool ok, const char* what)
{
    if (!ok) { printf("  FAIL  %s\n", what); failures++; }
    else printf("  ok    %s\n", what);
}

typedef std::vector<std::string> List;

static List Query(const List& user) { return abncp::OpenSkyWatchQuery<std::string>(user); }

static int Count(const List& q, const char* hex)
{
    int n = 0;
    for (const std::string& h : q) n += (h == hex) ? 1 : 0;
    return n;
}

// Every built-in exactly once, and nothing in the list twice.
static bool HasBuiltinsOnceNoDupes(const List& q)
{
    for (const abncp::Airframe& a : abncp::kE4bAirframes)
        if (Count(q, a.hex) != 1) return false;
    for (size_t i = 0; i < q.size(); ++i)
        for (size_t j = i + 1; j < q.size(); ++j)
            if (q[i] == q[j]) return false;
    return true;
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

    // ---------------------------------------------- rule 4: the OpenSky query's built-in four
    printf("  ---- rule 4: the OpenSky query always carries the four built-ins\n");
    {
        const List q = Query(List());
        expect(q.size() == 4, "an EMPTY user list still sends 4 hexes");
        expect(HasBuiltinsOnceNoDupes(q), "...and they are ADFEB3..ADFEB6, once each");
        expect(q.size() == 4 && q[0] == "adfeb3" && q[1] == "adfeb4" && q[2] == "adfeb5" && q[3] == "adfeb6",
               "...lower-case, in table order");
    }
    {
        const List q = Query(List{ "", "  ", "~", "\t" });
        expect(q.size() == 4 && HasBuiltinsOnceNoDupes(q), "blank / '~'-only entries add nothing; the four remain");
    }
    {
        const List user{ "abc9f3", "a1b2c3", "ae5006" };
        const List q = Query(user);
        expect(q.size() == user.size() + 4, "a user list of 3 gives 3 + 4 = 7");
        expect(q.size() == 7 && q[0] == "abc9f3" && q[1] == "a1b2c3" && q[2] == "ae5006",
               "...the user's own entries first, in their order");
        expect(HasBuiltinsOnceNoDupes(q), "...then the four, once each");
    }
    {
        const List user{ "abc9f3" };
        expect(Query(user).size() == 5, "a user list of 1 gives 1 + 4 = 5");
    }
    {
        // The user also typed some of the four: in any case, with a '~', with whitespace.
        const List user{ "ADFEB3", "~adfeb4", " adfeb5\t", "~ADFEB6 " };
        const List q = Query(user);
        expect(q.size() == 4, "the user typed all four (ADFEB3, ~adfeb4, ' adfeb5', '~ADFEB6 '): still 4, no duplicates");
        expect(HasBuiltinsOnceNoDupes(q), "...each of the four exactly once");
    }
    {
        const List user{ "abc9f3", "AdFeB3", "ABC9F3", "~abc9f3" };
        const List q = Query(user);
        expect(q.size() == 5, "abc9f3 three ways + AdFeB3: abc9f3 once, the four once = 5");
        expect(Count(q, "abc9f3") == 1 && HasBuiltinsOnceNoDupes(q), "...no address sent twice");
        expect(q[0] == "abc9f3" && q[1] == "adfeb3", "...the user's first spelling sets the position");
    }
    {
        // Canonical form is what is SENT: OpenSky's icao24 is the bare address.
        expect(abncp::CanonicalWatchHex<std::string>("  ~ABC9F3\t") == "abc9f3", "'  ~ABC9F3\\t' is sent as 'abc9f3'");
        expect(abncp::CanonicalWatchHex<std::string>("xyz") == "xyz", "a non-hex entry is sent as typed (lower-cased), not dropped");
        expect(abncp::CanonicalWatchHex<std::string>(nullptr).empty(), "a null entry is empty");
    }

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
    // Fails if the built-ins were dropped from the query, or the query stopped using the table.
    int carried = 0;
    for (const abncp::Airframe& a : abncp::kE4bAirframes)
        carried += Count(Query(List()), a.hex) + Count(Query(List{ "abc9f3" }), a.hex);
    expect(carried == 8, "every table hex is in the query, with an empty user list and with one entry");

    if (failures) {
        printf("abncp identity: %d FAILED\n", failures);
        return 1;
    }
    printf("abncp identity: all passed\n");
    return 0;
}
