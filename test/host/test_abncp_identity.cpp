// Host test for the command-post identity rule (src/eam/AbncpIdentity.h, compiled as-is).
//
// The ruling it grades (2026-10-12): "The ABNCP watchlist matches type B742 plus the E-4B
// registrations from public sources." Read as AND, because B742 is every 747-200 and the
// VC-25As are B742 too -- the cases below that would only pass under OR are the point.
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
    using abncp::IsE4b;

    // The four airframes, as the database spells them.
    expect(IsE4b("B742", "73-1676"), "73-1676 typed B742 is an E-4B");
    expect(IsE4b("B742", "73-1677"), "73-1677 typed B742 is an E-4B");
    expect(IsE4b("B742", "74-0787"), "74-0787 typed B742 is an E-4B");
    expect(IsE4b("B742", "75-0125"), "75-0125 typed B742 is an E-4B");

    // Spelling: punctuation, spaces and case are not identity.
    expect(IsE4b("b742", "731676"), "no hyphen, lower-case type still match");
    expect(IsE4b(" B742 ", " 75-0125 "), "surrounding spaces still match");

    // AND, not OR: the other 747-200s in the military feed.
    expect(!IsE4b("B742", "82-8000"), "VC-25A 82-8000 (B742) is NOT an E-4B");
    expect(!IsE4b("B742", "92-9000"), "VC-25A 92-9000 (B742) is NOT an E-4B");
    expect(!IsE4b("B742", ""), "B742 with no registration is NOT an E-4B");
    expect(!IsE4b("B742", nullptr), "B742 with a null registration is NOT an E-4B");

    // ...and the registration alone is not enough either.
    expect(!IsE4b("", "73-1676"), "an E-4B registration with no type is NOT named");
    expect(!IsE4b(nullptr, "73-1676"), "an E-4B registration with a null type is NOT named");
    expect(!IsE4b("E4", "73-1676"), "type E4 (adsbdb's spelling) is not the ruled B742");
    expect(!IsE4b("B744", "73-1676"), "another type with an E-4B registration is NOT an E-4B");

    // Near misses: a prefix or an extension of a registration is a different registration.
    expect(!IsE4b("B742", "73-167"), "a registration prefix does not match");
    expect(!IsE4b("B742", "73-16760"), "a registration extension does not match");
    expect(!IsE4b("B7420", "73-1676"), "a type extension does not match");
    expect(!IsE4b("B742", "---"), "punctuation only is not a registration");

    // The old table's addresses are not identities at all: they are C-2As, and a hex is never
    // read as a registration.
    expect(!IsE4b("B742", "ae0451"), "the old placeholder hex ae0451 (a C-2A) is not an E-4B");
    expect(!IsE4b("C2", "162157"), "the C-2A at ae0451, as its registry row states it, is not an E-4B");

    if (failures) {
        printf("abncp identity: %d FAILED\n", failures);
        return 1;
    }
    printf("abncp identity: all passed\n");
    return 0;
}
