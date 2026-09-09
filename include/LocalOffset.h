#pragma once
#include <stdint.h>
#include <math.h>

/* ===========================================================================
 * LOCAL TIME OFFSET: ONE DERIVATION, BECAUSE THERE WERE TWO.
 *
 * THE DEFECT THIS EXISTS FOR (2026-09-08). AircraftManager has resolved the
 * offset the same way for a long time:
 *
 *     utcOffsetSec = tzStr.isEmpty() ? (long)lround(lon / 15.0) * 3600
 *                                    : (long)(tzStr.toFloat() * 3600.0f);
 *
 * The quiet-hour scheduler needed the same quantity in main.cpp, where the app
 * manager is a different class per edition. So it was RE-DERIVED -- and the
 * re-derivation kept the explicit branch and dropped the fallback:
 *
 *     const long tzSec = tz.isEmpty() ? 0L : (long)(tz.toFloat() * 3600.0f);
 *
 * That is this project's most repeated shape, in a diff I wrote the same day I
 * added an entry about it: a second path that establishes something NARROWER
 * than the first. Not a copy that drifted -- a copy that was born wrong, and
 * whose wrongness is invisible because both branches look complete.
 *
 * The consequence was not subtle. A board with no `tz-offset` set resolves to
 * UTC, so its "03:00 local" quiet-hour reboot fires at 03:00 UTC -- 20:00
 * Pacific, in front of the customer, which is the exact complaint the feature
 * was built to remove. It would have shipped on 50 units.
 *
 * WHY THE FALLBACK IS SOUND HERE, stated so nobody "fixes" it back to zero.
 * Longitude is REQUIRED for the radar to work at all, so it is present on every
 * functioning unit -- this is not a guess that might be unavailable. It is
 * nominal solar time, so it can be off by up to ~2 h against a political zone
 * (and ignores DST, which this firmware has none of anyway). A CLOCK cannot
 * tolerate that; a once-a-day REBOOT SCHEDULE can. Worst realistic case moves a
 * 03:00 reboot to somewhere between midnight and 06:00, which is still the
 * quiet part of the night. Zero, by contrast, puts it at 20:00 for the entire
 * western hemisphere.
 *
 * AN EXPLICIT SETTING ALWAYS WINS. The fallback applies only to an empty
 * string; a customer who sets their zone gets exactly what they set.
 * ======================================================================== */

namespace localoffset {

/**
 * Seconds to add to UTC for local time.
 *
 * `tz` is the raw `tz-offset` config value ("" when unset); `lon` is the
 * configured longitude in degrees. Returns the explicit setting when there is
 * one, and the nominal solar zone otherwise.
 *
 * Takes a `const char*` rather than an Arduino String so the rule is
 * host-testable; both callers already have one to hand.
 */
inline long Resolve(const char* tz, double lon)
{
    if (tz && tz[0] != '\0') {
        // atof rather than String::toFloat so this compiles on a host. Both
        // parse a leading decimal and yield 0 on garbage, which is the same
        // answer an unparseable zone should get: treat it as unset would be
        // worse, because a customer who typed something meant something.
        return (long)(atof(tz) * 3600.0);
    }
    // 15 degrees per hour. lround, NOT a truncating cast: at lon = -121.3 the
    // truncation would give -8 h either way here, but at lon = -112.5 it is the
    // difference between -7 and -8, and truncation toward zero is wrong for
    // exactly half the planet.
    return (long)lround(lon / 15.0) * 3600L;
}

} // namespace localoffset
