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
//   5. Invalid entries are dropped from the query, never sent (and the config page marks them).
//   6. The 8-aircraft cap keeps the built-in four first, then the user's.
//   7. OpenSky's 429 + X-Rate-Limit-Retry-After-Seconds: back off until the reset, and say
//      "OpenSky limit reached, resuming HH:MM" (src/eam/OpenSkyLimit.h).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../../src/eam/AbncpIdentity.h"
#include "../../src/eam/AbncpWatch.h"
#include "../../src/eam/OpenSkyLimit.h"

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
        expect(abncp::CanonicalWatchHex<std::string>("XYZ") == "xyz", "canonicalising does not validate: 'XYZ' -> 'xyz' (the query does)");
        expect(abncp::CanonicalWatchHex<std::string>(nullptr).empty(), "a null entry is empty");
    }

    // ------------------------------------------------- rule 5: invalid entries are never sent
    printf("  ---- rule 5: invalid entries are dropped, never sent\n");
    expect(abncp::IsWatchHex("abc9f3") && abncp::IsWatchHex("000000") && abncp::IsWatchHex("adfeb3"), "six hex digits are valid");
    expect(!abncp::IsWatchHex("abc9f") && !abncp::IsWatchHex("abc9f30"), "five or seven digits are not");
    expect(!abncp::IsWatchHex("abcxyz") && !abncp::IsWatchHex("ABC9F3") && !abncp::IsWatchHex("~abc9f"),
           "non-hex, upper case (not canonical) and '~' are not");
    expect(!abncp::IsWatchHex("") && !abncp::IsWatchHex(nullptr), "empty and null are not");
    {
        const List q = Query(List{ "xyz" });
        expect(q.size() == 4 && HasBuiltinsOnceNoDupes(q), "one bad entry alone: still a valid request, the four present");
    }
    {
        const List q = Query(List{ "abc9f3", "xyz", "12345", "1234567", "~zzzzzz", "a1b2c3" });
        expect(q.size() == 6, "two good + four bad entries: 2 + 4 = 6 sent");
        expect(Count(q, "abc9f3") == 1 && Count(q, "a1b2c3") == 1 && HasBuiltinsOnceNoDupes(q), "...the good ones and the four");
        bool allValid = true;
        for (const std::string& h : q) allValid = allValid && abncp::IsWatchHex(h.c_str());
        expect(allValid, "...and every value sent is six hex digits");
    }
    {
        // The splitter the device AND the config page use, and what the page marks.
        const List t = abncp::SplitWatchList<std::string, List>("abc9f3, xyz;~ADFEB3\t12345\n\r a1b2c3,,");
        expect(t.size() == 5 && t[0] == "abc9f3" && t[1] == "xyz" && t[2] == "~ADFEB3" && t[3] == "12345" && t[4] == "a1b2c3",
               "split on , ; space tab CR LF, empties skipped, raw tokens kept");
        const List bad = abncp::InvalidWatchEntries<std::string, List>("abc9f3, xyz;~ADFEB3\t12345\n\r a1b2c3,,");
        expect(bad.size() == 2 && bad[0] == "xyz" && bad[1] == "12345", "the page marks exactly xyz and 12345 (as typed)");
        expect(abncp::SplitWatchList<std::string, List>(nullptr).empty() && abncp::SplitWatchList<std::string, List>(" ,; ").empty(),
               "null or separators-only: no entries");
    }

    // --------------------------------------------- rule 6: the cap keeps the built-ins first
    printf("  ---- rule 6: the 8-state cap keeps the built-in four first\n");
    {
        // A long user list, all airborne, with one E-4B returned LAST.
        const List resp{ "a00001", "a00002", "a00003", "a00004", "a00005",
                         "a00006", "a00007", "a00008", "a00009", "a0000a", "adfeb4" };
        const std::vector<size_t> keep = abncp::SelectWithinCap(resp, 8);
        bool e4bKept = false;
        for (size_t k : keep) e4bKept = e4bKept || resp[k] == "adfeb4";
        expect(keep.size() == 8, "11 states, cap 8: 8 kept");
        expect(e4bKept, "the E-4B returned 11th survives the cap");
        expect(!keep.empty() && resp[keep[0]] == "adfeb4", "...and is kept FIRST (it becomes the headline row)");
        expect(keep.size() == 8 && keep[1] == 0 && keep[7] == 6, "...then the user's, in response order (a00001..a00007)");
    }
    {
        const List resp{ "a00001", "adfeb6", "a00002", "a00003", "adfeb3", "a00004", "a00005",
                         "a00006", "adfeb5", "a00007", "a00008", "adfeb4" };
        const std::vector<size_t> keep = abncp::SelectWithinCap(resp, 8);
        int e4b = 0;
        for (size_t i = 0; i < keep.size() && i < 4; ++i) e4b += abncp::IsBuiltinHex(resp[keep[i]].c_str()) ? 1 : 0;
        expect(keep.size() == 8 && e4b == 4, "all four E-4Bs among 12: all four kept, first");
    }
    {
        const List resp{ "~adfeb4", "adfeb4x", "a00001" };
        const std::vector<size_t> keep = abncp::SelectWithinCap(resp, 8);
        expect(keep.size() == 3 && keep[0] == 0 && keep[1] == 1 && keep[2] == 2,
               "'~adfeb4' / 'adfeb4x' are not built-ins: no reordering, nothing dropped under the cap");
        expect(abncp::SelectWithinCap(List(), 8).empty(), "no states: nothing kept");
    }

    // ---------------------------------------------- rule 7: OpenSky's 429 and Retry-After
    printf("  ---- rule 7: OpenSky 429 / X-Rate-Limit-Retry-After-Seconds\n");
    expect(std::strcmp(osklimit::kRemainingHeader, "X-Rate-Limit-Remaining") == 0 &&
           std::strcmp(osklimit::kRetryAfterHeader, "X-Rate-Limit-Retry-After-Seconds") == 0,
           "the header names are OpenSky's, verbatim from its REST docs");
    expect(osklimit::ParseSeconds("3600") == 3600 && osklimit::ParseSeconds(" 42 ") == 42 && osklimit::ParseSeconds("0") == 0,
           "Retry-After parses whole seconds");
    expect(osklimit::ParseSeconds("") == -1 && osklimit::ParseSeconds(nullptr) == -1 && osklimit::ParseSeconds("-5") == -1 &&
           osklimit::ParseSeconds("1.5") == -1 && osklimit::ParseSeconds("abc") == -1 && osklimit::ParseSeconds("999999999") == -1,
           "absent, signed, fractional, garbage or absurd: not a wait (-1)");
    expect(osklimit::LimitDelayMs(429, "3600", 300000, 600000) == 3600000u, "429 + 3600 s: the next poll in exactly 3600 s");
    expect(osklimit::LimitDelayMs(429, "10", 300000, 600000) == 300000u, "429 + 10 s: never sooner than the 300 s interval");
    expect(osklimit::LimitDelayMs(429, "", 300000, 600000) == 600000u, "429 with no header: the ordinary failure back-off");
    expect(osklimit::LimitDelayMs(429, nullptr, 300000, 1000) == 300000u, "...but never sooner than the interval");
    expect(osklimit::LimitDelayMs(429, "500000", 300000, 600000) == 172800000u, "a Retry-After past 2 days is held to 2 days");
    expect(osklimit::LimitDelayMs(200, "3600", 300000, 600000) == 0u && osklimit::LimitDelayMs(500, "3600", 300000, 600000) == 0u,
           "not a 429: no limit wait (the normal schedule / back-off applies)");
    {
        char b[32];
        osklimit::FormatResume(b, sizeof(b), 1791900270L, 0);  // 2026-10-13 14:04:30Z
        expect(std::strcmp(b, "resuming 14:05Z") == 0, "14:04:30Z shows 14:05Z (rounded UP, never earlier than the retry)");
        osklimit::FormatResume(b, sizeof(b), 1791900240L, 0);  // 14:04:00Z exactly
        expect(std::strcmp(b, "resuming 14:04Z") == 0, "14:04:00Z exactly shows 14:04Z");
        osklimit::FormatResume(b, sizeof(b), 1791935970L, 0);  // 23:59:30Z
        expect(std::strcmp(b, "resuming 00:00Z") == 0, "23:59:30Z shows 00:00Z (wraps at midnight)");
        osklimit::FormatResume(b, sizeof(b), 0, 90000u);
        expect(std::strcmp(b, "resuming in 2 min") == 0, "no clock: 90 s left shows 'in 2 min' (rounded up)");
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
    // Fails if the cap stopped favouring the built-ins: each one, returned after 20 others.
    int survived = 0;
    for (const abncp::Airframe& a : abncp::kE4bAirframes) {
        List resp;
        for (int i = 0; i < 20; ++i) { char h[8]; std::snprintf(h, sizeof(h), "b%05x", i); resp.push_back(h); }
        resp.push_back(a.hex);
        for (size_t k : abncp::SelectWithinCap(resp, 8)) survived += (resp[k] == a.hex) ? 1 : 0;
    }
    expect(survived == 4, "every table hex survives the 8-cap when returned after 20 other states");

    if (failures) {
        printf("abncp identity: %d FAILED\n", failures);
        return 1;
    }
    printf("abncp identity: all passed\n");
    return 0;
}
