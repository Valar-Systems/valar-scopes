#pragma once

#include "QuietHourPolicy.h"

// The quiet hour's update decision, held while a window or curtain this remote controls is moving.
//
// WHY A HOLD AND NOT A SKIP. An update only ever runs at boot (main.cpp: "THIS IS NOW THE ONLY PATH
// THAT TOUCHES THE NETWORK FOR AN UPDATE"); the quiet hour arms a reboot that lands there. While the
// remote reboots and updates it cannot send STOP, so it must not choose a moment when something it
// controls is moving. quiet::Step records its own fire -- a decision it returns is never offered
// again -- so a skipped decision would be lost for the day. Held, it arms the first loop nothing is
// moving.
//
// Pure (no Arduino), so the hold is host-testable. main.cpp is its one caller, Remote builds only.
namespace remote {

// `fired` is this loop's quiet::Step result; `hold` is "something is moving or was just commanded";
// `held` is the caller's state, None between episodes. Returns the decision to arm NOW, else None.
inline quiet::Decision HoldUpdate(quiet::Decision fired, bool hold, quiet::Decision& held)
{
    if (fired != quiet::Decision::None) held = fired;   // a newer fire replaces an older held one
    if (hold || held == quiet::Decision::None) return quiet::Decision::None;
    const quiet::Decision arm = held;
    held = quiet::Decision::None;
    return arm;
}

}  // namespace remote
