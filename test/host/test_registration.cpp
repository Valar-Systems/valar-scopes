// Host test for the tail-number predicate.
//
// The two ways of being wrong here are NOT symmetric, and the test is built
// around that rather than around coverage:
//
//   * a MISS costs one enrichment request that returns nothing -- the status
//     quo for every non-ICAO contact today, so a miss makes nothing worse.
//   * a FALSE POSITIVE sends a registry lookup for a callsign that was never a
//     tail, and inflates the enrichNonIcaoTail ratio that decides whether the
//     whole feature is worth building. A predicate that flatters itself would
//     argue for its own construction.
//
// So the rejection cases outnumber the acceptance cases, and the airline shapes
// are named individually instead of being represented by one example.
#include <cstdio>
#include <cstring>
#include "../../include/Registration.h"

static int failures = 0;

static void check(bool cond, const char* what)
{
    if (!cond) { printf("  FAIL  %s\n", what); ++failures; }
}

static void accepts(const char* cs)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "accepts %s", cs);
    check(registration::LooksLikeRegistration(cs), msg);
}

static void rejects(const char* cs, const char* why)
{
    char msg[160];
    snprintf(msg, sizeof(msg), "rejects %-10s (%s)", cs, why);
    check(!registration::LooksLikeRegistration(cs), msg);
}

int main()
{
    printf("registration: tail-number predicate\n");

    // --- the three legal shapes, at both ends of each ---
    accepts("N1");        accepts("N99999");     // 1..5 digits
    accepts("N1A");       accepts("N9999Z");     // digits + one letter
    accepts("N1AA");      accepts("N999ZZ");     // digits + two letters
    accepts("N998JS");    // the contact that prompted this
    accepts("N9QX");      // and the one before it
    accepts("N628TS");

    // --- length: at most five characters follow the N ---
    rejects("N123456", "six digits");
    rejects("N99999A", "five digits plus a letter is six characters");
    rejects("N9999ZZ", "four digits plus two letters is six characters");
    rejects("N", "nothing after the N");

    // --- the first character after N is a digit, and never 0 ---
    rejects("NA1", "letter immediately after the N");
    rejects("N0123", "N0 is not issued");
    rejects("N0", "N0 is not issued");

    // --- I and O are not suffix letters; they read as 1 and 0 ---
    rejects("N1I", "I is not a suffix letter");
    rejects("N1O", "O is not a suffix letter");
    rejects("N1AI", "I in the second suffix position");

    // --- letters come after digits, never before or between ---
    rejects("N1A2", "digit after a letter");
    rejects("N1AAA", "three suffix letters");

    // --- THE CASES THAT COST SOMETHING: airline callsigns. Named one by one,
    //     because "an airline callsign" as a single example is the kind of
    //     coverage that passes while the shape it stands for slips through.
    rejects("UAL123", "United, leading letter is not N");
    rejects("DAL42", "Delta");
    rejects("SWA1234", "Southwest");
    rejects("NKS181", "Spirit -- STARTS WITH N, and this is the case the "
                      "digit-after-N rule exists for");
    rejects("NAX45", "Norwegian, also N-leading");
    rejects("N", "bare N");

    // --- junk, and the null a caller can hand us ---
    rejects("", "empty");
    rejects("N1-AA", "punctuation");
    rejects("n998js", "lowercase is not normalised here, by design");
    check(!registration::LooksLikeRegistration(nullptr), "rejects nullptr");

    // --- CONTROL: the predicate must be capable of saying yes. An all-rejecting
    //     function would pass every case above.
    check(registration::LooksLikeRegistration("N998JS"),
          "CONTROL: it still accepts a real tail");

    printf(failures ? "\nregistration: %d FAILURE(S)\n" : "\nregistration: all good\n", failures);
    return failures ? 1 : 0;
}
