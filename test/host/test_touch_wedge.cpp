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

    // ---- ruling 3: the hour reset clears the strip; the wedge holding never does -----
    {
        State s = AtBoot(3, /*powerOn=*/false);
        check(StripShown(s), "CONTROL: an SW boot at run 3 shows the strip");
        check(HealthyResetDue(s, /*wedgeSeen=*/false, HEALTHY_RESET_MS), "an unavailable boot that never saw the wedge is due the hour reset");
        s = OnHealthyReset(s);
        check(!StripShown(s) && s.run == 0, "the hour reset clears the strip and the counter");

        // On-device evidence for the other half: 2026-10-07, COM18 ran 2 h in touch
        // unavailable with RebootRecommended() holding (rebootRec=1 on 237 consecutive
        // health lines) and the strip never cleared. Here it is for any uptime.
        const State held = AtBoot(3, false);
        bool everDue = false;
        const uint32_t uptimes[] = { HEALTHY_RESET_MS, 2 * HEALTHY_RESET_MS, 24UL * 3600000UL, 0xFFFFFFFFUL };
        for (uint32_t t : uptimes)
            if (HealthyResetDue(held, /*wedgeSeen=*/true, t)) everDue = true;
        check(!everDue && StripShown(held), "the strip never clears by the hour reset while the wedge holds, at any uptime");
    }

    // ---- ruling 4: on Connect the strip leaves the QR and the URL alone ------------
    {
        using namespace connectlayout;
        const int S = 240, GLYPH_W = 6, GLYPH_H = 8;   // the s3-128's font: 6x8 (the 174 px box = 27 chars * 6 + 12)
        const Strip st = StripFor("0000000000000000");   // EXAMPLE -- not a real device
        const ConnectRows rows = ConnectRowsFor(st);
        std::printf("  QR worst-case bottom y=%d, URL y=%d\n", QrBottomY(), URL_Y);
        check(QrBottomY() <= URL_Y, "the largest QR Connect can draw ends above the URL row");
        bool clearOfQr = true, clearOfUrl = true, fits = true, allLines = true;
        for (const ConnectRow& r : rows.row) {
            if (r.y < QrBottomY()) clearOfQr = false;
            if (r.y < URL_Y + GLYPH_H + 2) clearOfUrl = false;
            const int w = (int)std::strlen(r.text) * GLYPH_W;
            const int chord = discgeom::ChordWidthPx(r.y, GLYPH_H, S);
            std::printf("  connect row y=%d  %3d px of %3d  \"%s\"\n", r.y, w, chord, r.text);
            if (w > chord) fits = false;
            if (r.text[0] == '\0') allLines = false;
        }
        check(clearOfQr, "no strip row on Connect reaches into the QR, at its largest");
        check(clearOfUrl, "no strip row on Connect covers the URL");
        check(fits, "every strip row on Connect fits the round glass at its height");
        check(allLines && rows.row[2].headline &&
              std::strstr(rows.row[0].text, "0000000000000000") && std::strstr(rows.row[1].text, "support@valarsystems.com"),
              "all three lines are there: the id, the support address, the headline");
        // CONTROL: the boxed strip, where it sits on every other screen, DOES overlap the QR --
        // which is why Connect needs its own placement at all.
        const int boxTop = StripTopY(174, 36, S);
        check(boxTop < QrBottomY() && boxTop + 36 > QR_CY - 66, "CONTROL: the boxed strip at its usual height overlaps Connect's QR");
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

    // ---- the strip's box is on the glass ------------------------------------------
    // The box as drawn on the s3-128, measured off /diag/fb on 2026-10-07: 174 x 36 px,
    // centred on x=119. Graded by an INDEPENDENT test -- are both top corners inside the
    // r=120 disc? -- not by the chord rule StripTopY itself uses.
    {
        const int W = 174, H = 36, S = 240, X0 = 119 - W / 2, X1 = X0 + W - 1;
        auto cornersOnGlass = [&](int top) {
            const double c = (S - 1) / 2.0, r = S / 2.0;
            const double dy = top - c, l = X0 - c, rr = X1 - c;
            return l * l + dy * dy <= r * r && rr * rr + dy * dy <= r * r;
        };
        const int top = StripTopY(W, H, S);
        std::printf("  strip box top y=%d (was 26)\n", top);
        check(cornersOnGlass(top), "the strip's top corners are on the round glass");
        check(top + H <= S / 2, "the strip stays in the upper half");
        check(!cornersOnGlass(26), "CONTROL: the old fixed placement (y=26) is off the glass, as seen on glass");
        check(discgeom::ChordWidthPx(top - 1, H, S) < W, "it is the HIGHEST row that fits, not merely a low one");
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
