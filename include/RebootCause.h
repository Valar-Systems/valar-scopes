#pragma once

#include <stdint.h>

// WHY a deferred or stamped reboot happened, and the suffix it adds to the reported
// reset reason ("SW" -> "SW_NETWD"). ONE map, read by both places that compose the
// reason (OtaUpdater.cpp: the OTA record and TakeBootReasonReport) -- they used to
// carry a copy each, and a second copy is the one that goes stale.
//
// The Worker sanitises reasons to [\w.-] and caps them at 16 characters, so a suffix
// must keep "SW" + suffix within 16 (test_touch_wedge.cpp checks every one).
namespace rebootcause {

constexpr uint8_t NONE        = 0;
constexpr uint8_t OTA_CHECK   = 1;   // the deferred daily update check
constexpr uint8_t NET_WEDGE   = 2;   // NetWatchdog: the network stayed unreachable
constexpr uint8_t TOUCH_WEDGE = 3;   // the touch supervisor's last rung (TouchWedgePolicy.h)

inline const char* Suffix(uint8_t cause)
{
    switch (cause) {
        case NET_WEDGE:   return "_NETWD";
        case TOUCH_WEDGE: return "_TOUCHWD";
        default:          return "";   // an update check is an ordinary SW; unknown adds nothing
    }
}

} // namespace rebootcause
