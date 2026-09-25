// Host test for the Missileer clock's ON WATCH count (src/eam/OnWatch.h, compiled as-is).
//
// The ruling it grades (2026-09-25): "shows the last value if the fetch fails, '--' only if
// it never had one", and a lone unit shows 1 -- which the SERVER provides (it counts the
// polling device), so the firmware must show the number it was given and add nothing.
#include <cstdio>
#include <cstring>
#include "../../src/eam/OnWatch.h"

static int failures = 0;

static void expect(bool ok, const char* what)
{
    if (!ok) { printf("  FAIL  %s\n", what); failures++; }
    else printf("  ok    %s\n", what);
}

static bool shows(const onwatch::Count& c, const char* want)
{
    char buf[12];
    return std::strcmp(onwatch::Text(c, buf, sizeof(buf)), want) == 0;
}

int main()
{
    using onwatch::Count;

    // Never had one.
    Count c;
    expect(!c.Known(), "a fresh count is not known");
    expect(shows(c, "--"), "never fetched shows --");

    // A response without the field (older server) is not zero.
    expect(!c.Apply(false, 0), "a missing field is not applied");
    expect(shows(c, "--"), "missing field before any value still shows --");

    // A lone unit: the server already counted it, so 1 is shown as 1 -- not 2.
    expect(c.Apply(true, 1), "a present value is applied");
    expect(shows(c, "1"), "a lone unit shows 1 (no local +1)");

    // Failures keep the last value. A failed fetch never calls Apply (EamFeedClient returns
    // before the Latest case); a parsed body without the field or with a negative is the
    // in-band version of the same thing.
    expect(!c.Apply(false, 0), "a later missing field is not applied");
    expect(shows(c, "1"), "last value kept across a missing field");
    expect(!c.Apply(true, -3), "a negative is not a reading");
    expect(shows(c, "1"), "last value kept across a negative");

    // Zero is a real answer once the server gives it (a device whose header was rejected).
    expect(c.Apply(true, 0), "zero is applied");
    expect(shows(c, "0"), "zero shows 0, not --");

    // Multi-digit, no clamp.
    c.Apply(true, 12);
    expect(shows(c, "12"), "12 shows 12");
    c.Apply(true, 10000);
    expect(shows(c, "10000"), "a large count is shown whole, never clamped");

    // Too small a buffer draws nothing, never "--" for a value it holds.
    char tiny[3];
    expect(std::strcmp(onwatch::Text(c, tiny, sizeof(tiny)), "") == 0, "a short buffer draws nothing, not --");

    if (failures) printf("on_watch: %d FAILED\n", failures);
    else printf("on_watch: all passed\n");
    return failures ? 1 : 0;
}
