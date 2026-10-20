#pragma once
// The deferred-reboot cap, as a pure function (no Arduino, no NVS), so the firmware and the host test
// call the same code. DeferRebootWithCause (src/OtaUpdater.cpp) is the only firmware caller.
//
// WHY A SCHEDULED REBOOT HAS ITS OWN, SHORTER MINIMUM (#377). The quiet-hour reboot is the fleet's only
// daily update check, and consecutive quiet hours are exactly 86,400 s apart -- the cap's own boundary.
// Held to the full 24 h, a decision that ran one second late last night refuses tonight (gap 86,399), and
// an offset moved an hour earlier (DST spring-forward, #379) refuses the next night (gap 82,800). Each
// refusal loses that night's update check with no retry. quiet::MIN_INTERVAL_S (20 h) exists precisely
// "so a legitimate next-day fire is never suppressed by a few minutes of jitter"; a 24 h cap downstream
// undid that margin. So a scheduled deferral needs the SHORTER of the quiet minimum and the cap --
// 20 h in a shipping build, and the bench override (BLIPSCOPE_REBOOT_MIN_S) when one is compiled in,
// so the gate test still exercises this exact comparison.
//
// UNSCHEDULED deferrals -- the net-wedge rung and the fleet-config floor -- keep the full cap: they are
// the ones a fault could fire repeatedly, and the cap is what stops that becoming a reboot loop.
//
// `scheduled` is a parameter of every caller with no default, so a new caller has to say which it is.
#include <stdint.h>
#include "QuietHourPolicy.h"

namespace rebootcap {

// The minimum gap this deferral needs since the last stamped reboot. `capS` is the build's cap
// (REBOOT_MIN_INTERVAL_S: 24 h, or the bench override).
inline uint32_t MinGapS(bool scheduled, uint32_t capS)
{
    if (!scheduled) return capS;
    return quiet::MIN_INTERVAL_S < capS ? quiet::MIN_INTERVAL_S : capS;
}

// true = the reboot may happen. `last` is the stamped epoch of the last deferred reboot (0 = none yet);
// `now` is the current epoch. A clock that went backwards (now < last) is allowed, as it always was --
// refusing on it would strand the device until the clock caught up.
inline bool Allows(uint32_t now, uint32_t last, bool scheduled, uint32_t capS)
{
    return !(last != 0 && now >= last && (now - last) < MinGapS(scheduled, capS));
}

}  // namespace rebootcap
