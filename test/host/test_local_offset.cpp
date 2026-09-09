// Host test for the local-time offset derivation.
//
// WRITTEN AFTER THE SECOND PATH LOST THE FALLBACK, 2026-09-08. AircraftManager
// had resolved this correctly for a long time; the quiet-hour scheduler needed
// the same quantity in main.cpp, re-derived it, kept the explicit branch and
// dropped the longitude fallback. Every board with no `tz-offset` set therefore
// resolved to UTC, and its "03:00 local" reboot was scheduled for 03:00 UTC --
// 20:00 Pacific, the exact complaint the feature exists to remove.
//
// So the cases below are not about arithmetic. Each one pins a branch that a
// re-derivation is likely to drop, and the FIRST is the branch that was
// actually dropped.
#include <cstdio>
#include <cstring>
#include "../../include/LocalOffset.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

using localoffset::Resolve;

int main()
{
    std::printf("local offset\n");

    // ---- THE BRANCH THAT WAS LOST ------------------------------------------
    {
        // Bend, Oregon: the bench's own longitude. An unset zone must NOT
        // resolve to UTC.
        check(Resolve("", -121.29) == -8 * 3600, "unset tz falls back to the longitude zone (Bend -> -8)");
        check(Resolve(nullptr, -121.29) == -8 * 3600, "...and a null pointer is the same as unset");
        check(Resolve("", -121.29) != 0, "CONTROL: the fallback is not zero -- the defect this file exists for");
    }

    // ---- an explicit setting always wins -----------------------------------
    {
        check(Resolve("-7", -121.29) == -7 * 3600, "an explicit zone beats the longitude estimate");
        check(Resolve("0", -121.29) == 0, "an explicit ZERO is honoured, not treated as unset");
        check(Resolve("5.5", 77.2) == 5 * 3600 + 1800, "half-hour zones survive (India +5.5)");
        check(Resolve("-3.5", -52.7) == -(3 * 3600 + 1800), "...including negative half-hours (Newfoundland)");
        check(Resolve("14", 179.0) == 14 * 3600, "the +14 extreme is representable");
    }

    // ---- the fallback across the globe -------------------------------------
    {
        check(Resolve("", 0.0) == 0, "Greenwich resolves to 0");
        check(Resolve("", 151.2) == 10 * 3600, "Sydney -> +10");
        check(Resolve("", -74.0) == -5 * 3600, "New York -> -5");
        check(Resolve("", 139.7) == 9 * 3600, "Tokyo -> +9");
        // ROUNDING, NOT TRUNCATION, and this is the half of the planet where it
        // matters: -112.5 / 15 = -7.5. Truncation toward zero gives -7; the
        // nearest zone is -8 (or -7, but consistently, and lround is what the
        // original used).
        check(Resolve("", -112.5) == (long)lround(-112.5 / 15.0) * 3600,
              "the fallback rounds rather than truncating");
        check(Resolve("", -112.4) == -7 * 3600, "just east of the boundary -> -7");
        check(Resolve("", -117.6) == -8 * 3600, "just west of it -> -8");
    }

    // ---- garbage is not silently treated as unset --------------------------
    {
        // A customer who typed something meant something. Falling back to the
        // longitude here would silently override an intent we failed to parse;
        // 0 is what the original String::toFloat gives, and matching it keeps
        // this a refactor rather than a behaviour change smuggled in beside a
        // bug fix.
        check(Resolve("abc", -121.29) == 0, "unparseable tz yields 0, NOT the fallback");
        check(Resolve(" ", -121.29) == 0, "a blank-but-present tz likewise");
    }

    // ---- the bench board's actual situation --------------------------------
    {
        // What the 2026-09-08 boot print reported, and what it must report now.
        const long before = 0;                       // the defect
        const long after  = Resolve("", -121.2858);  // the fix
        check(before == 0, "the old path gave UTC...");
        check(after == -8 * 3600, "...and the fixed one gives -8, which is Bend");
        // 22:47 UTC is 14:47 local at -8. The print said "local hour now 22";
        // it must now say 14.
        const int hourUtc = 22;
        const int hourLocal = (int)(((hourUtc * 3600L + after) / 3600L + 24) % 24);
        check(hourLocal == 14, "so the boot print's local hour goes 22 -> 14");
    }

    if (failures == 0) std::printf("PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
