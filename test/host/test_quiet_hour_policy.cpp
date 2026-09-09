// Host test for the quiet-hour reboot schedule.
//
// The two hazards of any local-time scheduler are a DOUBLE-FIRE on an hour that
// happens twice and a SKIP on an hour that does not happen at all. On this
// device both arrive by the same route -- the customer editing `tz-offset` --
// because main.cpp calls configTime(0, 0, ...) and there are no DST rules to
// move the offset on their own. So every case below drives the offset directly,
// which is exactly how the real thing changes.
//
// Timings are minutes-resolution on purpose: the loop calls Step() every few
// seconds, and a schedule that only works when sampled on the hour boundary is
// not a schedule.
#include <cstdio>
#include <vector>
#include <string>
#include "../../include/QuietHourPolicy.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

using namespace quiet;

// 2026-09-08T00:00:00Z, a Tuesday. All fixtures are offsets from this.
static constexpr uint32_t T0 = 1788912000UL;

/// Drive the policy minute by minute from `fromMin` to `toMin` (UTC minutes
/// after T0) at a fixed offset, collecting every non-None decision as
/// "<name>@<local hour>:<local minute>".
struct Driver {
    State s;
    long offsetSec = 0;
    uint32_t uptimeMs = 0;
    std::vector<std::string> log;

    void run(uint32_t fromMin, uint32_t toMin, int stepMin = 1)
    {
        for (uint32_t m = fromMin; m < toMin; m += (uint32_t)stepMin) {
            const uint32_t epoch = T0 + m * 60;
            uptimeMs += (uint32_t)stepMin * 60000u;
            const Decision d = Step(s, epoch, offsetSec, uptimeMs);
            if (d != Decision::None) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s@d%d h%02d",
                              DecisionName(d), (int)LocalDay(epoch, offsetSec),
                              LocalHour(epoch, offsetSec));
                log.emplace_back(buf);
            }
        }
    }
    size_t fires() const { return log.size(); }
    std::string joined() const
    {
        std::string o;
        for (size_t i = 0; i < log.size(); ++i) { if (i) o += " | "; o += log[i]; }
        return o;
    }
};

int main()
{
    std::printf("quiet-hour schedule\n");

    // ---- the arithmetic, before anything depends on it ---------------------
    {
        // A negative offset is the common case (the Americas) and is where a
        // truncating division lands a day out. Pinned directly because every
        // other assertion in this file is built on it.
        check(LocalHour(T0, 0) == 0, "UTC midnight is hour 0 at offset 0");
        check(LocalHour(T0, -8 * 3600) == 16, "...and 16:00 the previous day at -8");
        check(LocalDay(T0, -8 * 3600) == LocalDay(T0, 0) - 1, "a negative offset can move the DAY back");
        check(LocalHour(T0 + 3 * 3600, -8 * 3600) == 19, "-8 at 03:00 UTC is 19:00 local");
        check(LocalHour(T0, 5 * 3600 + 1800) == 5, "a half-hour offset (India, +5.5) works");
        check(LocalHour(T0 + 14 * 3600, 14 * 3600) == 4, "+14 wraps past midnight correctly");
    }

    // ---- the ordinary day --------------------------------------------------
    {
        Driver d; d.offsetSec = 0;
        d.run(0, 24 * 60);
        check(d.fires() == 1, "one fire in a day");
        check(d.joined().find("quiet-hour") == 0, "and it is the quiet-hour decision");
        check(d.log[0].find("h03") != std::string::npos, "at local hour 3");
    }
    {
        // The same day at -8, where the quiet hour falls at 11:00 UTC. Nothing
        // about the rule may depend on the UTC hour.
        Driver d; d.offsetSec = -8 * 3600;
        d.run(0, 24 * 60);
        check(d.fires() == 1, "one fire in a day at offset -8");
        check(d.log[0].find("h03") != std::string::npos, "still at LOCAL hour 3");
    }
    {
        Driver d; d.offsetSec = 0;
        d.run(0, 5 * 24 * 60);
        check(d.fires() == 5, "five fires in five days, no more and no fewer");
    }

    // ---- boot seeding ------------------------------------------------------
    {
        // Booting INSIDE the quiet hour must not immediately re-propose the
        // reboot the board has just performed.
        Driver d; d.offsetSec = 0;
        d.run(3 * 60 + 30, 24 * 60);   // first tick at 03:30
        check(d.fires() == 0, "a board booting at 03:30 does not fire that day");
        d.run(24 * 60, 48 * 60);
        check(d.fires() == 1, "...and fires normally the next day");
    }
    {
        // Booting OUTSIDE it must not suppress the same day's window -- the
        // failure mode of seeding on the day unconditionally.
        Driver d; d.offsetSec = 0;
        d.run(2 * 60, 24 * 60);        // first tick at 02:00
        check(d.fires() == 1, "a board booting at 02:00 still fires at 03:00 THE SAME DAY");
    }

    // ---- HAZARD 1: the hour that happens twice -----------------------------
    {
        // "Fall back": at 05:00 local the customer edits tz-offset from 0 to -2,
        // so 03:00 local comes round a second time the same evening.
        Driver d; d.offsetSec = 0;
        d.run(0, 5 * 60);                       // through the real 03:00
        check(d.fires() == 1, "fired once before the offset edit");
        d.offsetSec = -2 * 3600;                // the edit
        d.run(5 * 60, 24 * 60);
        check(d.fires() == 1, "the repeated 03:00 does NOT fire again");
    }
    {
        // The nastier variant: the edit pushes local time back across midnight,
        // so the local DAY index decrements and the day-key alone would read the
        // repeated hour as a brand-new day.
        Driver d; d.offsetSec = 2 * 3600;
        d.run(0, 2 * 60);                       // local 02:00-04:00, fires
        check(d.fires() == 1, "fired at local 03:00 with a +2 offset");
        d.offsetSec = -4 * 3600;                // now local time is the PREVIOUS day
        d.run(2 * 60, 24 * 60);
        check(d.fires() == 1, "a backwards edit ACROSS MIDNIGHT does not double-fire");
    }

    {
        // THE CASE THAT MAKES MIN_INTERVAL_S LOAD-BEARING, and it was missing
        // until a sabotage run found it: deleting that guard left the whole file
        // green, which meant the same-day guard was doing all the work and the
        // interval guard was decoration.
        //
        // The same-day key fails when the repeated window lands on a DIFFERENT
        // local day, and an offset edit big enough to do that is not exotic --
        // it is a device that MOVES. Los Angeles (-8) to Auckland (+12) is a
        // 20-hour swing that a traveller performs with one config save.
        //
        // Fire at local 03:00 on day 0 at -8. Edit to +12 ten minutes later:
        // local time jumps forward 20 h, so the next local 03:00 arrives only
        // FOUR HOURS later and on the following local day. The day key waves it
        // through; the interval guard is the only thing standing between the
        // customer and two reboots in an afternoon.
        Driver d; d.offsetSec = -8 * 3600;
        d.run(0, 11 * 60 + 30);                  // local 03:00 falls at 11:00 UTC
        check(d.fires() == 1, "fired at local 03:00 with a -8 offset");

        d.offsetSec = 12 * 3600;                 // the move: -8 -> +12
        d.run(11 * 60 + 30, 20 * 60);
        check(d.fires() == 1,
              "a 20 h offset jump does NOT fire again, though the local DAY has changed");
    }

    // ---- HAZARD 2: the hour that never happens -----------------------------
    {
        // "Spring forward": at 02:59 local the offset moves +2, so 03:00 local
        // is skipped entirely. Without a catch-up the board waits another day.
        Driver d; d.offsetSec = 0;
        d.run(0, 2 * 60 + 59);
        check(d.fires() == 0, "nothing yet at 02:59");
        d.offsetSec = 2 * 3600;                 // 02:59 -> 04:59, hour 3 never occurs
        d.run(2 * 60 + 59, 24 * 60);
        check(d.fires() == 0, "the skipped hour does not fire, as expected");
        // The next quiet hour is ~21 h later. CATCHUP_S is 25 h, so the ordinary
        // schedule should reach it first -- the catch-up is for when it does not.
        d.run(24 * 60, 48 * 60);
        check(d.fires() == 1, "the following day's quiet hour fires normally");
    }
    {
        // THE CATCH-UP, with a construction that genuinely delays the window past
        // 25 h -- the first draft of this case did not, and the code was right to
        // fire a quiet hour at 22.5 h instead. Recorded because the corrected
        // version is easy to mistake for a workaround.
        //
        // Fire normally at day 0 03:00. Then at day 1 02:59 local the customer
        // moves the offset BACK two hours (a real edit: someone correcting their
        // zone, or "falling back"), so local time returns to 00:59 and the day's
        // 03:00 is pushed two hours further out. The next window therefore lands
        // 26 h after the last fire, and the catch-up is what covers the gap.
        Driver d; d.offsetSec = 0;
        d.run(0, 4 * 60);
        check(d.fires() == 1, "baseline fire at day 0 03:00");

        d.run(4 * 60, 26 * 60 + 59);            // up to day 1 02:59 local
        check(d.fires() == 1, "no second fire yet -- the window has not come round");

        d.offsetSec = -2 * 3600;                 // the edit: local 02:59 -> 00:59
        d.run(26 * 60 + 59, 32 * 60);
        check(d.fires() == 2, "a second decision arrives during the delayed window");
        check(d.log[1].find("catchup") == 0,
              "and it is reported AS a catch-up, not as a quiet hour");
    }

    // ---- the unsynced clock ------------------------------------------------
    {
        State s;
        uint32_t up = 0;
        int fired = 0;
        for (uint32_t m = 0; m < 60 * 60; ++m) { // 60 h of minutes
            up += 60000;
            if (Step(s, 1000, /*offset*/ 0, up) == Decision::Fallback) ++fired;
        }
        check(fired == 2, "an unsynced clock falls back to the uptime timer (2 in 60 h)");
        // CONTROL: the same run with a sane clock must NOT produce Fallback --
        // otherwise the assertion above is satisfied by a function that always
        // returns Fallback.
        State s2; uint32_t up2 = 0; int fb = 0;
        for (uint32_t m = 0; m < 60 * 60; ++m) {
            up2 += 60000;
            if (Step(s2, T0 + m * 60, 0, up2) == Decision::Fallback) ++fb;
        }
        check(fb == 0, "CONTROL: a synced clock never takes the fallback");
    }

    // ---- a sampling cadence that never lands on the hour boundary ----------
    {
        // The loop does not tick on the minute. A schedule that needs to observe
        // exactly hh:00:00 would work in this test file and fail on the board.
        Driver d; d.offsetSec = 0;
        d.run(0, 24 * 60, 7);   // every 7 minutes, never aligned to the hour
        check(d.fires() == 1, "fires once when sampled every 7 minutes");
    }

    if (failures == 0) std::printf("PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
