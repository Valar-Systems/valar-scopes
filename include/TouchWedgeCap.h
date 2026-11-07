#pragma once
// The touch-wedge reboot cap's DRIVER, one implementation for the radar and Weatherscope
// (docs/weatherscope-stage3.md A12.119, A12.120; the cap itself: docs/v15-touch-wedge-cap.md).
//
// TouchWedgePolicy.h DECIDES (the constants, the chain, the resets, the rung's order). This
// RUNS those decisions: once at boot, every loop, on a real touch, and on every frame's draw.
// It used to live in AircraftManager.cpp, which made a second edition a copy. Everything a
// device does -- NVS, the cause stamp, the restart, the serial line, the two "not now"
// questions -- goes through Io, so the whole driver runs on the host (test_touch_wedge_cap.cpp).
// The device's Io is TouchWedgeDevice.h.
//
// C++11: it compiles under run.sh's -std=c++11.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include "TouchWedgePolicy.h"

// The timers in force. The overrides are BENCH-ONLY and #error without TOUCH_WEDGE_BENCH, which
// check-no-bench-hooks.sh keeps out of every shipping image, so a shortened test value cannot
// ride into a release. Here, not in an edition, so no edition can take one without the bench
// flag. The 90 s outage bound lives in TouchWatchdog.cpp and is not overridden.
#if (defined(TOUCH_WD_REBOOT_IDLE_MS) || defined(TOUCH_WD_HEALTHY_MS)) && !defined(TOUCH_WEDGE_BENCH)
#error "TOUCH_WD_* overrides are bench-only (TOUCH_WEDGE_BENCH); shipping builds use touchwedge::*"
#endif

namespace touchwedge {

#ifdef TOUCH_WD_REBOOT_IDLE_MS
constexpr uint32_t IDLE_IN_FORCE_MS = TOUCH_WD_REBOOT_IDLE_MS;
#else
constexpr uint32_t IDLE_IN_FORCE_MS = REBOOT_IDLE_MS;
#endif
#ifdef TOUCH_WD_HEALTHY_MS
constexpr uint32_t HEALTHY_IN_FORCE_MS = TOUCH_WD_HEALTHY_MS;
#else
constexpr uint32_t HEALTHY_IN_FORCE_MS = HEALTHY_RESET_MS;
#endif

/// THE IDLE RULE, the radar's own expression: the watchdog's wedge has outlived its outage
/// bound, touch is not already given up on, and nothing has stamped the edition's activity
/// clock for idleMs. The clock is the edition's: every touched sample, Initialise (boot and
/// every config save) and the bench keys. A finger the wedged chip cannot report is invisible
/// to any rule -- what the device can know is "no touch reported for idleMs".
inline bool RebootDue(bool wedged, const State& s, uint32_t now, uint32_t lastActivityMs, uint32_t idleMs)
{
    return wedged && !s.unavailable && now - lastActivityMs >= idleMs;
}

/// What the driver needs from a device. The host test's fake records every call.
class Io {
public:
    virtual ~Io() {}
    virtual uint8_t ReadRun() = 0;            // NVS touch-wd/run; 0 when absent
    virtual bool WriteRun(uint8_t run) = 0;    // false: NVS could not be opened, nothing stored
    virtual void StampCause() = 0;            // the next boot reports SW_TOUCHWD
    virtual void Restart() = 0;               // never returns on a device
    virtual bool FlashWriteActive() = 0;      // an OTA image is being written (OtaUpdater)
    virtual bool TryHoldSave() = 0;           // take the settings-save gate for good (SaveLock.h)
    virtual void Print(const char* text) = 0; // verbatim to the serial log; text carries its own line end
};

/// The strip's four colours (RGB888, as lgfx::color888 packs them). Each edition passes its own:
/// the radar its fixed ones (RADAR_INK), Weatherscope its theme's roles (WxTouchStrip.h).
struct StripInk { uint32_t ground, border, headline, body; };
constexpr StripInk RADAR_INK = { 0x000000UL, 0xFFB000UL, 0xFFB000UL, 0xFFFFFFUL };

/// The strip's box and three lines, over whatever screen is up -- DrawTouchUnavailable's body,
/// moved here with its colours made a parameter. Three lines high, placed by the chord rule so
/// the whole box is on the glass (StripTopY). Nothing on it asks to be tapped: touch is what's
/// broken. Canvas is BandCanvas on a device and a recording fake on the host.
template <class Canvas>
inline void DrawStrip(Canvas& c, const Strip& st, const StripInk& ink, int screenSize)
{
    const int C = screenSize / 2 - 1;
    c.setTextSize(1);
    const int th = (int)c.fontHeight();
    const int rowH = th + 2;
    const int w = (int)c.textWidth(st.line3) + 12;
    const int boxH = 3 * rowH + 6;
    const int boxTop = StripTopY(w, boxH, screenSize);
    const int y0 = boxTop + 4;
    c.fillRect(C - w / 2, boxTop, w, boxH, ink.ground);
    c.drawRect(C - w / 2, boxTop, w, boxH, ink.border);
    const char* lines[3] = { st.line1, st.line2, st.line3 };
    for (int i = 0; i < 3; ++i) {
        c.setTextColor(i == 0 ? ink.headline : ink.body);
        c.drawString(lines[i], C - (int)c.textWidth(lines[i]) / 2, y0 + i * rowH);
    }
}

class Cap {
public:
    /// Once per boot: the stored run, and reset 2 -- a power cycle starts clean.
    void Boot(Io& io, bool powerOn)
    {
        const uint8_t stored = io.ReadRun();
        stored_ = stored;
        st_ = AtBoot(stored, powerOn);
        Printf(io, "[touch-wd] boot: run=%u (stored %u, reset %s)%s\n", (unsigned)st_.run, (unsigned)stored,
               powerOn ? "POWERON" : "not power-on",
               st_.unavailable ? " -- touch unavailable, no touch reboots this boot" : "");
        if (st_.run != stored) Store(io, st_.run, "power-on");   // reset 2
    }

    /// Every loop. `wedged` is TouchWatchdog::RebootRecommended(): the supervisor's re-init ladder
    /// has been failing for over 90 s. Reboot -- historically the one recovery a stuck chip always
    /// responded to -- but only once nobody is touching (RebootDue), never while a settings save or
    /// a flash write is in progress (Hold), and never more than CAP times in a row: after that the
    /// device shows "touch unavailable" and keeps running. The soft/hard re-init rungs keep going,
    /// and a real touch clears it all (OnRealTouch).
    void Tick(Io& io, uint32_t now, bool wedged, uint32_t lastActivityMs,
              uint32_t idleMs = IDLE_IN_FORCE_MS, uint32_t healthyMs = HEALTHY_IN_FORCE_MS)
    {
        if (wedged) seen_ = true;
        if (RebootDue(wedged, st_, now, lastActivityMs, idleMs)) {
            const RungResult r = OnRung(st_);
            if (r.action == Action::EnterUnavailable) {
                st_.unavailable = true;
                Printf(io, "[touch-wd] wedged again at run=%u: the cap holds -- touch unavailable, no reboot\n",
                       (unsigned)st_.run);
            } else if (!Hold(io)) {
                // Due, and held: the next loop asks again. Said once per held episode.
                if (!heldSaid_) io.Print("[touch-wd] reboot due, held: a settings save or a flash write is in progress\n");
                heldSaid_ = true;
            } else {
                Printf(io, "[touch-wd] wedged past the outage bound and idle: touch reboot %u of %u\n",
                       (unsigned)r.newRun, (unsigned)CAP);
                // The side effects IN THE PLAN'S ORDER -- the order test_touch_wedge grades.
                const Plan plan = RebootPlan();
                for (int i = 0; i < plan.n; ++i) {
                    switch (plan.step[i]) {
                    case Step::WriteRun:   Store(io, r.newRun, "touch reboot"); break;
                    case Step::StampCause: io.StampCause(); break;
                    case Step::Restart:
                        io.Print("[touch-wd] restarting (cause SW_TOUCHWD stamped)\r\n");
                        io.Restart();
                        break;
                    }
                }
                return;   // a device never gets here; on the host nothing else runs in a restarted boot
            }
        } else {
            heldSaid_ = false;
        }
        // An hour without the wedge (reset 3): a recovered unit must not creep toward the cap.
        if (HealthyResetDue(st_, seen_, now, healthyMs)) {
            const bool wasUnavailable = st_.unavailable;
            st_ = OnHealthyReset(st_);   // the counter AND the strip
            Store(io, 0, "an hour without the wedge");
            if (wasUnavailable) io.Print("[touch-wd] an hour without the wedge: leaving touch unavailable\r\n");
        }
    }

    /// Reset 1: the CHIP reported a touch, so the controller is alive and the chain is broken.
    /// Only the chip's own read -- never a bench or synthetic press, which proves nothing.
    void OnRealTouch(Io& io)
    {
        if (st_.run == 0 && !st_.unavailable) return;
        const bool wasUnavailable = st_.unavailable;
        st_ = touchwedge::OnRealTouch(st_);
        Store(io, 0, "a real touch");
        if (wasUnavailable) io.Print("[touch-wd] touch is back: leaving touch unavailable\r\n");
    }

    /// The strip, exactly when touch is unavailable, last over every screen.
    template <class Canvas>
    void Draw(Canvas& c, const char* deviceId, const StripInk& ink, int screenSize) const
    {
        if (!StripShown(st_)) return;
        DrawStrip(c, StripFor(deviceId), ink, screenSize);
    }

    bool Shown() const { return StripShown(st_); }
    const State& St() const { return st_; }
    uint8_t Stored() const { return stored_; }
    bool WedgeSeen() const { return seen_; }

private:
    // A flash write first: then the save gate is not taken for a reboot that is not happening.
    static bool Hold(Io& io) { return !io.FlashWriteActive() && io.TryHoldSave(); }

    void Store(Io& io, uint8_t run, const char* why)
    {
        if (run == stored_) return;   // wear: write only when the value changes
        if (!io.WriteRun(run)) {
            Printf(io, "[touch-wd] run %u -> %u NOT stored: NVS unavailable (%s)\n", (unsigned)stored_, (unsigned)run, why);
            return;
        }
        Printf(io, "[touch-wd] run %u -> %u (NVS write: %s)\n", (unsigned)stored_, (unsigned)run, why);
        stored_ = run;
    }

    __attribute__((format(printf, 2, 3))) static void Printf(Io& io, const char* fmt, ...)
    {
        char buf[160];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        io.Print(buf);
    }

    State st_;
    uint8_t stored_ = 0;     // what NVS holds, so a write happens only on change
    bool seen_ = false;      // the rung's condition held at least once this boot
    bool heldSaid_ = false;  // the "held" line already printed for this episode
};

} // namespace touchwedge
