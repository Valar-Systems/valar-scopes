#pragma once
// The cloud feed's poll cadence and the preventive weekly reboot, as pure rules (docs/web-kiosk-airfield.md).
//
// kWeb is true only in the web build (BLIPSCOPE_WEB_RENDERER, set by tools/web-renderer/build.sh and never by a
// PlatformIO env). So a device image can never turn the kiosk on, and the web build never reboots itself, by
// construction rather than by a setting. The night test is a function, called last and only where the code before
// these helpers called it: the solar calculation is not something every device frame should pay for.
#include <cstring>

namespace pollcadence {

#ifdef BLIPSCOPE_WEB_RENDERER
constexpr bool kWeb = true;
#else
constexpr bool kWeb = false;
#endif

// config "kiosk": exactly "true" turns it on, and only in the web build.
inline bool KioskOn(const char* stored) { return kWeb && stored && std::strcmp(stored, "true") == 0; }

// A touch within idleAfter is active, and it outranks night: someone watching at 2 AM gets the good cadence. Otherwise
// night, by the solar clock and only with a valid clock; otherwise idle. A web kiosk polls at the active rate always.
template <class NightFn>
inline unsigned long IntervalMs(unsigned long activeMs, unsigned long idleMs, unsigned long nightMs, unsigned long idleAfterMs,
                                unsigned long msSinceTouch, bool clockValid, bool kiosk, NightFn nightNow)
{
    if ((kWeb && kiosk) || msSinceTouch <= idleAfterMs) return activeMs;
    if (clockValid && nightNow()) return nightMs;
    return idleMs;
}

// The preventive weekly reboot (2026-07-10): only when nobody is watching -- past the uptime, no card open, a valid clock,
// 10 min with no touch, and solar night. Never in the web build: a browser tab cannot restart, and on an always-on screen
// the check would log on every night-time frame.
template <class NightFn>
inline bool PreventiveRebootDue(unsigned long nowMs, unsigned long rebootAfterMs, bool inDetail, bool clockValid,
                                unsigned long msSinceTouch, NightFn nightNow)
{
    if (kWeb) return false;
    return nowMs >= rebootAfterMs && !inDetail && clockValid && msSinceTouch >= 10UL * 60UL * 1000UL && nightNow();
}

}  // namespace pollcadence
