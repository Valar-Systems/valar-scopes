#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "DiscGeometry.h"

// THE TOUCH-WEDGE REBOOT CAP (v16; spec docs/v15-touch-wedge-cap.md).
//
// The last rung of the touch supervisor reboots when the controller has been off
// the bus past the 90 s outage bound and nobody has touched the device for 10 min.
// On a board whose chip NEVER answers, that is true on every boot, ~600 s after
// start, forever: one fleet unit rebooted 30 times in 30 days, every 615 s, with
// nothing on screen to say why or who to call.
//
// So: after CAP consecutive touch reboots with zero touch events, stop rebooting and
// show "touch unavailable" with the support address and the device id. The radar
// keeps running underneath -- a partial failure must not become a total one.
//
// This file is the pure half: the shipped constants (pinned by test/host/
// test_touch_wedge.cpp), the decisions, the order of the rung's side effects, and the
// strip's text. AircraftManager owns the state and does the NVS and the restart.
namespace touchwedge {

// ---- the shipped constants (each pinned by a host test) --------------------------
constexpr uint32_t REBOOT_IDLE_MS   = 10UL * 60UL * 1000UL;   // touch idle before the rung may reboot
constexpr uint8_t  CAP              = 3;                       // N: reboots before touch unavailable
constexpr uint32_t HEALTHY_RESET_MS = 60UL * 60UL * 1000UL;   // a wedge-free boot this long resets the run

// Its OWN namespace -- never ota-boot or ota-mem. A dropped telemetry read must never
// be able to clear a reboot cap, the same reason OTA keeps its namespaces apart.
constexpr const char* NVS_NS  = "touch-wd";
constexpr const char* NVS_KEY = "run";

struct State {
    uint8_t run = 0;           // consecutive touch reboots, persisted
    bool unavailable = false;  // touch is given up on for this boot
};

// At boot. A power cycle is what a customer tries first, and it gets a fresh start;
// a software reset (including our own touch reboots) carries the run, so the cap
// survives the very reboots it counts.
inline State AtBoot(uint8_t storedRun, bool powerOn)
{
    State s;
    s.run = powerOn ? 0 : storedRun;
    s.unavailable = s.run >= CAP;
    return s;
}

enum class Action { Reboot, EnterUnavailable };
struct RungResult { Action action; uint8_t newRun; };

// The rung fired (wedged past the outage bound AND idle for REBOOT_IDLE_MS). Below the
// cap it reboots one more time; at the cap it gives up on touch instead.
inline RungResult OnRung(const State& s)
{
    if (s.unavailable || s.run >= CAP) return { Action::EnterUnavailable, s.run };
    return { Action::Reboot, (uint8_t)(s.run + 1) };
}

// A real touch event: the controller answered, so the chain is broken.
inline State OnRealTouch(State s)
{
    s.run = 0;
    s.unavailable = false;
    return s;
}

// An hour without the wedge: a boot that has run HEALTHY_RESET_MS with the rung's
// condition never holding. Without this, a unit that glitches once, recovers after one
// reboot and then sits untouched would carry run=1 into a glitch weeks later and creep
// toward the cap across unrelated, recovered episodes.
inline bool HealthyResetDue(const State& s, bool wedgeSeenThisBoot, uint32_t uptimeMs,
                            uint32_t healthyMs = HEALTHY_RESET_MS)
{
    return s.run != 0 && !wedgeSeenThisBoot && uptimeMs >= healthyMs;
}

// ---- the rung's side effects, IN ORDER ------------------------------------------
// Stamp first, then reboot (like DeferRebootWithCause): power lost between the two
// leaves one reboot uncounted, which is harmless; the other order can count a reboot
// that never happened -- or, written after ESP.restart(), never count any at all.
// AircraftManager executes exactly this list, so the order the test grades IS the
// order the device runs.
enum class Step : uint8_t { WriteRun, StampCause, Restart };
struct Plan { Step step[3]; int n; };
inline Plan RebootPlan() { return { { Step::WriteRun, Step::StampCause, Step::Restart }, 3 }; }

// ---- the strip ----------------------------------------------------------------
// Three lines, because a 240 px round face cannot hold the spec's one long sentence;
// the substance is unchanged: what is wrong, who to contact, and the one identifier
// that finds the unit (the same string the config page shows under "Device ID:").
constexpr const char* SUPPORT = "support@valarsystems.com";
struct Strip { char line1[24]; char line2[32]; char line3[48]; };
inline Strip StripFor(const char* deviceId)
{
    Strip s;
    snprintf(s.line1, sizeof(s.line1), "Touch unavailable");
    snprintf(s.line2, sizeof(s.line2), "%s", SUPPORT);
    snprintf(s.line3, sizeof(s.line3), "Device ID: %s", deviceId ? deviceId : "");
    return s;
}

// Where the strip's box goes: the HIGHEST row at which the whole box clears the round
// glass, by the one chord rule every round-face caller uses (DiscGeometry.h).
//
// A fixed offset is what this replaced, and it was wrong on glass (2026-10-07): the box
// sat at y=26, where a 240 px disc is ~150 px wide, so its 174 px top corners were cut by
// the bezel -- 44 border pixels off the glass, read from /diag/fb, and plain in a photo.
// The text inside was fine, which is why it read as cosmetic; but a box whose corners are
// missing looks broken on the one screen that tells a customer something is.
inline int StripTopY(int boxW, int boxH, int screenSize)
{
    const int c = screenSize / 2;
    for (int y = 0; y + boxH <= c; ++y)   // the strip stays in the upper half
        if (discgeom::ChordWidthPx(y, boxH, screenSize) >= boxW) return y;
    return c - boxH / 2;   // wider than any upper-half row: centre it, where the glass is widest
}

} // namespace touchwedge
