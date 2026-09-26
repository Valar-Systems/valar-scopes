#pragma once

// AN ALERT IS NEVER CONSUMED UNSEEN.
//
// Every visual alert class (emergency, military) fires its flash burst once per
// contact. The question is WHEN that once is spent, and the answer has to be the
// same for every class, so it lives here and nowhere else.
//
// Emergency used to spend it on first sight -- visible or not. A 7700 first seen
// outside the radar circle burned its flash unseen, then crossed into view and
// never flashed at all: the device knew, and showed nothing. Military already
// waited for visibility (AircraftManager::UpdateVisualAlerts). Two classes, two
// rules, one of them wrong -- so there is now one rule, and both call it.
//
// Pure, so test/host/test_alert_edge.cpp grades it with no board.

namespace alertedge {

/// Take this contact's one alert edge, if it is visible now and not already taken.
/// Returns true exactly once per contact: the first time it is seen. `fired`
/// stays false while the contact is out of view, so the edge is still waiting
/// when it arrives.
inline bool TakeVisibleEdge(bool& fired, bool visible)
{
    if (fired || !visible) return false;
    fired = true;
    return true;
}

} // namespace alertedge
