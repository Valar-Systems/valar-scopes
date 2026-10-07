// The touch-wedge reboot cap (v16), the pure half: include/TouchWedgePolicy.h and
// include/RebootCause.h. Spec: docs/v15-touch-wedge-cap.md.
//
// SABOTAGES this file must catch (each was shown red, then undone):
//   (b) the counter moved to the ota-boot namespace  -> "its own namespace" fails
//   (c) the run written AFTER the restart            -> "written before the restart" fails
//   a shipped constant changed (CAP 3 -> 4)          -> the pinning check fails
#include <cstdio>
#include <cstring>
#include "../../include/TouchWedgePolicy.h"
#include "../../include/RebootCause.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

int main()
{
    using namespace touchwedge;
    std::printf("touch-wedge cap (TouchWedgePolicy.h)\n");

    // ---- the shipped constants: a shortened bench value must never ship -------------
    check(REBOOT_IDLE_MS == 600000UL, "REBOOT_IDLE_MS is 600000 ms (10 min)");
    check(CAP == 3, "N = 3");
    check(HEALTHY_RESET_MS == 3600000UL, "the healthy reset is 60 min");

    // ---- its own namespace (spec 2: a dropped telemetry read must not clear the cap) --
    check(std::strcmp(NVS_NS, "touch-wd") == 0 && std::strcmp(NVS_NS, "ota-boot") != 0 &&
          std::strcmp(NVS_NS, "ota-mem") != 0,
          "the counter has its own namespace, never ota-boot or ota-mem");

    // ---- the chain: three reboots, then touch unavailable --------------------------
    {
        uint8_t stored = 0;
        int reboots = 0;
        for (int boot = 0; boot < 6; ++boot) {
            State s = AtBoot(stored, /*powerOn=*/false);
            if (s.unavailable) break;
            const RungResult r = OnRung(s);
            if (r.action != Action::Reboot) break;
            stored = r.newRun;
            ++reboots;
        }
        std::printf("  chain: %d touch reboots, stored run %u\n", reboots, (unsigned)stored);
        check(reboots == 3 && stored == 3, "run 0->1->2->3 across three touch reboots, then no more");
        const State s4 = AtBoot(stored, false);
        check(s4.unavailable, "the boot after the third reboot is touch unavailable");
        check(OnRung(s4).action == Action::EnterUnavailable && OnRung(s4).newRun == 3,
              "at the cap the rung gives up on touch instead of rebooting");
        check(!AtBoot(2, false).unavailable, "CONTROL: run 2 is not yet unavailable");

        // The cap is checked twice: at boot (above) and at the rung. Each alone stops a 4th
        // reboot, so a sabotage of ONE is invisible on the bench -- this pins the rung's own.
        State unclassified; unclassified.run = CAP;   // the boot did NOT mark it unavailable
        check(OnRung(unclassified).action == Action::EnterUnavailable,
              "the rung refuses at the cap even when the boot did not classify it");
        State below; below.run = CAP - 1;
        check(OnRung(below).action == Action::Reboot, "CONTROL: one below the cap still reboots");
    }

    // ---- reset 2: a power cycle starts clean; a soft reset keeps the cap -----------
    check(AtBoot(3, /*powerOn=*/true).run == 0 && !AtBoot(3, true).unavailable,
          "a POWERON boot at run 3 starts clean");
    check(AtBoot(3, /*powerOn=*/false).run == 3 && AtBoot(3, false).unavailable,
          "CONTROL: an SW boot at run 3 stays unavailable (the cap survives a soft reset)");

    // ---- reset 1: a real touch ------------------------------------------------------
    {
        State s = AtBoot(3, false);
        const State kept = s;                                    // CONTROL: no touch, nothing changes
        check(kept.run == 3 && kept.unavailable, "CONTROL: without a touch the run is kept");
        s = OnRealTouch(s);
        check(s.run == 0 && !s.unavailable, "a real touch clears the run and touch unavailable");
    }

    // ---- reset 3: an hour without the wedge ----------------------------------------
    {
        State s; s.run = 2;
        check(HealthyResetDue(s, /*wedgeSeen=*/false, HEALTHY_RESET_MS), "60 min wedge-free resets run 2");
        check(!HealthyResetDue(s, /*wedgeSeen=*/true, HEALTHY_RESET_MS * 2),
              "CONTROL: a boot where the wedge held never resets, however long it runs");
        check(!HealthyResetDue(s, false, HEALTHY_RESET_MS - 1), "CONTROL: a millisecond short does not");
        State z; z.run = 0;
        check(!HealthyResetDue(z, false, HEALTHY_RESET_MS), "CONTROL: run 0 needs no reset (no write)");
    }

    // ---- the rung's order: write, stamp, THEN restart -------------------------------
    {
        const Plan p = RebootPlan();
        int write = -1, stamp = -1, restart = -1;
        for (int i = 0; i < p.n; ++i) {
            if (p.step[i] == Step::WriteRun)   write = i;
            if (p.step[i] == Step::StampCause) stamp = i;
            if (p.step[i] == Step::Restart)    restart = i;
        }
        check(p.n == 3 && write >= 0 && stamp >= 0 && restart >= 0, "the plan has each step once");
        check(write < restart, "the counter is written before the restart");
        check(stamp < restart, "the cause is stamped before the restart");
        check(restart == p.n - 1, "the restart is the last step");
    }

    // ---- the strip --------------------------------------------------------------------
    {
        const char* id = "0000000000000000";   // EXAMPLE -- not a real device
        const Strip st = StripFor(id);
        check(std::strstr(st.line2, "support@valarsystems.com") != nullptr, "the strip carries the support address");
        check(std::strstr(st.line3, id) != nullptr && std::strncmp(st.line3, "Device ID: ", 11) == 0,
              "the strip carries the device id, labelled as on the config page");
        check(std::strlen(st.line3) > std::strlen("Device ID: "), "CONTROL: the id row is not empty");
        check(std::strlen(st.line1) > 0 && std::strstr(st.line1, "Touch unavailable") != nullptr,
              "the strip says what is wrong");
    }

    // ---- the reported reason --------------------------------------------------------
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "SW%s", rebootcause::Suffix(rebootcause::TOUCH_WEDGE));
        check(std::strcmp(buf, "SW_TOUCHWD") == 0, "a touch reboot reports SW_TOUCHWD");
        std::snprintf(buf, sizeof(buf), "SW%s", rebootcause::Suffix(rebootcause::NET_WEDGE));
        check(std::strcmp(buf, "SW_NETWD") == 0, "CONTROL: a network reboot still reports SW_NETWD");
        check(rebootcause::Suffix(rebootcause::OTA_CHECK)[0] == '\0' && rebootcause::Suffix(0)[0] == '\0',
              "an update check and no cause add nothing");
        bool fits = true;
        for (uint8_t c = 0; c < 8; ++c) if (2 + std::strlen(rebootcause::Suffix(c)) > 16) fits = false;
        check(fits, "every SW+suffix fits the Worker's 16-character reason cap");
    }

    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
