#pragma once
#include <stdint.h>
#include <Preferences.h>

/*
 * THE APPLIED BACKLIGHT LEVEL, CARRIED ACROSS A REBOOT.
 *
 * THE BUG THIS EXISTS FOR. Night dim is DERIVED, not stored. `configuredBrightness`
 * is the customer's day level; the night level is computed as `/5` on a 20-second
 * cadence in AircraftManager's dim pass. So nothing anywhere persists what the
 * panel is ACTUALLY showing -- and main.cpp's first-light line said, in full:
 *
 *     // full brightness for the boot screen until AircraftManager applies the saved level
 *     tft.setBrightness(255);
 *
 * That is correct for a board being switched on in a lit room, and it is a bright
 * flash in a dark bedroom at 03:00 -- caused by the quiet-hour reboot, a feature
 * whose entire purpose is to be invisible. The comment above the line stated the
 * behaviour accurately; it just described a hazard rather than preventing one.
 *
 * WHY THIS REMEMBERS ON CHANGE RATHER THAN AT THE REBOOT. Stashing at the moment
 * of deferral would be tighter, but it would mean plumbing the display level into
 * every caller that can reboot -- the quiet hour, the reachability watchdog's
 * rung 3, any future one -- and the watchdog lives in a translation unit that has
 * no business knowing about the panel. Recording it where it CHANGES means every
 * reboot benefits, including a crash, a brownout and a customer pulling the plug,
 * and no reboot path has to remember to participate.
 *
 * THE WEAR IS NOTHING. The applied level changes when the board crosses dusk or
 * dawn, and when the customer edits the setting: order two writes a day. NVS is
 * rated ~100k cycles, so ~137 years. Guarded anyway by writing only on an actual
 * change, which the caller already tracks to avoid redundant setBrightness calls.
 *
 * ERRING DIM IS SAFE; ERRING BRIGHT IS THE BUG. If the stored value is stale --
 * the customer raised the day level while the board was off -- the panel comes up
 * at the old, lower level for the fraction of a second before the first dim
 * evaluation (LoadConfig sets lastBrightnessCheck = 0, so that happens promptly).
 * The failure mode of this feature is therefore "briefly dimmer than it should
 * be", which is the direction nobody notices, rather than "briefly brighter",
 * which is the one that wakes somebody up.
 *
 * ONE PLACE KNOWS THE KEY. main.cpp reads it before AircraftManager exists and
 * AircraftManager writes it; a second copy of the namespace/key string in either
 * would be two places that drift, which is this project's most repeated defect.
 */
namespace brightcarry {

constexpr const char* NS  = "disp";
constexpr const char* KEY = "bright";

/**
 * Record the level actually applied to the panel. Call ONLY when it changed.
 *
 * LOOP TASK ONLY (NVS).
 */
inline void Remember(uint8_t applied)
{
    if (applied == 0) return; // 0 is the "nothing stored" sentinel; never store it
    Preferences p;
    if (!p.begin(NS, false)) return; // telemetry-grade: never a reason to fail a frame
    p.putUChar(KEY, applied);
    p.end();
}

/**
 * The level to bring the panel up at, or 0 when nothing has been recorded.
 *
 * NOT cleared on read, deliberately -- unlike the one-shot OTA report. This is
 * standing state describing the panel, not an event: a second reboot before the
 * dim pass has run again must still come up dim.
 *
 * LOOP TASK ONLY (NVS), and called before the display is drawn to.
 */
inline uint8_t Recall()
{
    Preferences p;
    if (!p.begin(NS, true)) return 0; // read-only; absent namespace is not an error
    const uint8_t v = p.getUChar(KEY, 0);
    p.end();
    return v;
}

} // namespace brightcarry
