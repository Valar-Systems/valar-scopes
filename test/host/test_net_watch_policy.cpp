// Host test for the reachability watchdog's ladder.
//
// GRADED AGAINST THE REHEARSAL THAT FOUND THE DEFECT. bench-logs/
// ota-fault-com16-2026-09-03.log, COM16, OTA_FAULT_AT_PCT: 75 consecutive
// "Host is unreachable" transport failures from 19:52:50Z to 20:02:27Z --
// 9 min 37 s -- with WiFi.status() reporting WL_CONNECTED throughout, the
// existing 10-minute supervisor never arming, and zero reboots.
//
// The whole point of the ladder living in a pure header is that it can be
// proved here. The bench rehearsal (docs/ota-control-plan.md) proves the
// ACTIONS do what they claim on real hardware; this proves the DECISIONS, over
// case counts and timings no bench run could cover in an afternoon.
#include <cstdio>
#include <vector>
#include <string>
#include "../../include/NetWatchPolicy.h"

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

using namespace netwatch;

// ---------------------------------------------------------------------------
// A tiny driver: feed the policy a scripted sequence of ticks and collect every
// non-None action, so a whole episode can be asserted as ONE list rather than
// as a handful of substring checks. CLAUDE.md's rule about comparing whole
// outputs instead of fragments -- an assertion that "reboot appears somewhere"
// passes just as happily against a ladder that fired all three stages at once.
struct Driver {
    State s;
    uint32_t okTotal = 0, failTotal = 0, nowMs = 0;
    std::vector<std::string> log;

    /// Advance `ms` with no request activity at all.
    void idle(uint32_t ms) { step(ms, 0, 0); }
    /// Advance `ms` during which `n` requests failed at the transport layer.
    void fail(uint32_t ms, uint32_t n) { step(ms, 0, n); }
    /// Advance `ms` during which `n` requests completed a round trip.
    void ok(uint32_t ms, uint32_t n) { step(ms, n, 0); }

    void step(uint32_t ms, uint32_t nOk, uint32_t nFail, bool associated = true)
    {
        nowMs += ms;
        okTotal += nOk;
        failTotal += nFail;
        const Action a = Step(s, okTotal, failTotal, nowMs, associated);
        if (a != Action::None) log.emplace_back(ActionName(a));
    }

    std::string joined() const
    {
        std::string out;
        for (size_t i = 0; i < log.size(); ++i) { if (i) out += " "; out += log[i]; }
        return out;
    }
};

// The rehearsal's own cadence: a request roughly every 7 s, all failing.
static void wedgeFor(Driver& d, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 7000) d.fail(7000, 1);
}

int main()
{
    std::printf("net watchdog ladder\n");

    // ---- the trigger needs BOTH conditions ---------------------------------
    {
        // Ten failures, but crammed into 20 s. A burst against one dead upstream
        // must not be read as a dead network.
        Driver d;
        for (int i = 0; i < 10; ++i) d.fail(2000, 1);
        check(d.log.empty(), "10 failures inside 20 s must NOT fire (window not met)");
        check(d.s.failRun == 10, "the run is still counted while below the window");
    }
    {
        // Long enough, but only three failures -- a device that barely asked.
        Driver d;
        d.fail(200000, 1);
        d.fail(200000, 1);
        d.fail(200000, 1);
        check(d.log.empty(), "3 failures over 10 min must NOT fire (run not met)");
    }
    {
        // Neither counter moves for an hour: an idle device cannot wedge on
        // silence, which is the one way a traffic-based trigger could go wrong
        // in the direction that reboots healthy boards.
        Driver d;
        for (int i = 0; i < 60; ++i) d.idle(60000);
        check(d.log.empty(), "an hour of no requests at all must NOT fire");
        check(d.s.stage == Stage::Healthy, "and the stage stays healthy");
    }

    // ---- the rehearsal, replayed -------------------------------------------
    {
        Driver d;
        wedgeFor(d, 4 * 60 * 1000);
        check(d.log.empty(), "at 4 min into the wedge nothing has fired yet");
        wedgeFor(d, 2 * 60 * 1000); // now past 5 min
        check(d.joined() == "reconnect", "first action past both thresholds is the CHEAPEST stage");

        // The real episode ran 9 min 37 s. Walk the rest of it.
        wedgeFor(d, 95 * 1000);
        check(d.joined() == "reconnect radio-reset", "escalates to the radio reset after the grace");
        wedgeFor(d, 95 * 1000);
        check(d.joined() == "reconnect radio-reset reboot",
              "and asks for a reboot once the radio reset has not helped");
    }

    // ---- recovery at every stage stands the ladder down --------------------
    {
        const char* names[] = { "reconnect", "radio-reset", "reboot" };
        for (int stopAt = 0; stopAt < 3; ++stopAt) {
            Driver d;
            wedgeFor(d, 6 * 60 * 1000);
            for (int i = 0; i < stopAt; ++i) wedgeFor(d, 95 * 1000);
            // One successful round trip is enough.
            d.ok(7000, 1);
            std::string want;
            for (int i = 0; i <= stopAt; ++i) { if (i) want += " "; want += names[i]; }
            want += " recovered";
            check(d.joined() == want, "a single success stands the ladder down");
            check(d.s.stage == Stage::Healthy && d.s.failRun == 0 && d.s.cycles == 0,
                  "and resets the run, the stage and the cycle count");
        }
    }

    // ---- a reboot that was REFUSED must back off, never loop ---------------
    {
        // Step() returning Reboot does not mean a reboot happened: the 24 h cap,
        // an unsynced clock or unavailable NVS all refuse it, and control comes
        // back here. This is the case that turns an unreachable network into a
        // reboot loop if it is got wrong, so it is asserted rather than assumed.
        Driver d;
        wedgeFor(d, 6 * 60 * 1000);
        wedgeFor(d, 95 * 1000);
        wedgeFor(d, 95 * 1000);
        check(d.joined() == "reconnect radio-reset reboot", "ladder walked to the reboot");
        wedgeFor(d, 95 * 1000);
        check(d.joined() == "reconnect radio-reset reboot backoff",
              "a refused reboot leads to BACKOFF, not to another reboot");
        check(d.s.cycles == 1, "one full ladder recorded");

        const uint32_t backoffAtMs = d.s.stageAtMs;

        // Twenty more minutes of the same dead network must produce NOTHING.
        const size_t before = d.log.size();
        wedgeFor(d, 20 * 60 * 1000);
        check(d.log.size() == before, "backoff is quiet: 20 min of failures, no new action");

        // ...and then the ladder re-arms at the CHEAPEST stage.
        //
        // Advanced until the log moves rather than for a computed duration. The
        // first draft asserted an exact action list after a fixed 11 minutes and
        // failed -- not because the policy was wrong but because the re-arm left
        // enough of that wedge to escalate again inside it. An assertion whose
        // correctness depends on my arithmetic about the fixture is testing my
        // arithmetic. This asserts the two properties that actually matter.
        while (d.log.size() == before && d.nowMs - backoffAtMs < 2 * BACKOFF_RETRY_MS)
            d.fail(7000, 1);
        check(d.log.size() == before + 1 && d.log.back() == "reconnect",
              "the ladder re-arms at the CHEAPEST stage, not at the reboot");
        check(d.nowMs - backoffAtMs >= BACKOFF_RETRY_MS,
              "and not before the backoff interval has elapsed");
    }

    // ---- THE CONTROL: WL_CONNECTED changes nothing -------------------------
    {
        // The defect was a supervisor that trusted this flag. The fix is not
        // "trust it less" -- it is that no decision here reads it at all, and
        // that is provable rather than assertable in a comment: run the same
        // episode twice with opposite association and compare the WHOLE action
        // list. Same technique as StarvationPolicy's ignored `ballastHeld`.
        std::string out[2];
        for (int which = 0; which < 2; ++which) {
            const bool associated = (which == 0);
            Driver d;
            for (uint32_t t = 0; t < 14 * 60 * 1000u; t += 7000) d.step(7000, 0, 1, associated);
            out[which] = d.joined();
        }
        check(out[0] == out[1], "the ladder is IDENTICAL whether or not WL_CONNECTED is set");
        check(!out[0].empty(), "CONTROL: and it is not identical merely by both being empty");
        std::printf("  associated=true  -> %s\n", out[0].c_str());
        std::printf("  associated=false -> %s\n", out[1].c_str());
    }

    // ---- association IS recorded, for the diagnosis ------------------------
    {
        Driver d;
        for (uint32_t t = 0; t < 6 * 60 * 1000u; t += 7000) d.step(7000, 0, 1, /*associated=*/true);
        check(d.s.associatedAtAction, "associated-but-unroutable is recorded (this defect)");
        Driver e;
        for (uint32_t t = 0; t < 6 * 60 * 1000u; t += 7000) e.step(7000, 0, 1, /*associated=*/false);
        check(!e.s.associatedAtAction, "and so is fell-off-the-AP (the old watchdog's case)");
    }

    // ---- the counter saturates rather than wrapping ------------------------
    {
        check(SaturatingAdd(65535, 1) == 65535, "the failure run saturates at 0xFFFF");
        check(SaturatingAdd(65000, 1000) == 65535, "...including across a large delta");
        check(SaturatingAdd(0, 3) == 3, "CONTROL: and otherwise just adds");
    }

    // ---- a success and a failure in the SAME tick means the network works ---
    {
        // Several tasks make requests concurrently, so one tick can observe
        // both counters moving. Any success proves reachability, so the run
        // must clear -- the opposite reading would let one failing endpoint
        // hold the ladder armed forever on a healthy network.
        Driver d;
        wedgeFor(d, 6 * 60 * 1000);
        const size_t before = d.log.size();
        d.step(7000, 1, 3);
        check(d.log.size() == before + 1 && d.log.back() == "recovered",
              "ok+fail in one tick reads as RECOVERED, not as continued failure");
        check(d.s.failRun == 0, "and the run is cleared");
    }

    if (failures == 0) std::printf("PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
