// FactoryReset — the two destructive tiers, in one place.
//
// ===========================================================================
// WHY THIS IS A MODULE AND NOT THREE CALL SITES
//
// Before this file there were THREE reset paths -- the config page's
// /reset-wifi, the Stats screen's hold, and BootTouchToForget -- and each one
// did its own `wm.resetSettings()` + `ForgetFastAp()` inline. They agreed, but
// only because someone kept them agreeing; nothing structural said they had to.
// Adding a second, larger tier to three separate copies is how one of them ends
// up clearing less than it claims.
//
// So: the tiers are named here, the clearing happens here, and the log line is
// emitted here. A caller REQUESTS a tier; it does not perform one.
//
// ---------------------------------------------------------------------------
// BY NAMESPACE. NEVER nvs_flash_erase().
//
// `nvs_flash_erase()` would take the whole partition -- including the Wi-Fi
// driver's own storage and, more importantly, "cloud-key-fac", the factory
// identity written once by provision-device.py and deliberately unreachable
// from the settings form. A customer cannot recover that value, which is
// exactly why the form cannot write it, and it is exactly why a blanket erase
// is the wrong instrument. Every clear below names its namespace.
//
// ---------------------------------------------------------------------------
// LOOP TASK ONLY.
//
// NVS writes from the async web task would race the loop task's own writers
// (Logbook persists on a debounce, the config form saves on POST). Every entry
// point therefore sets a REQUESTED tier and returns; main.cpp consumes it and
// calls Perform() from the loop. That is the pattern the old bool already used,
// kept because it was right.
// ===========================================================================

#ifndef BLIPSCOPE_FACTORY_RESET_H
#define BLIPSCOPE_FACTORY_RESET_H

#include <Arduino.h>
#include <functional>

namespace factoryreset
{
    /// Ordered by destructiveness, so `max(a, b)` is meaningful when two entry
    /// points request in the same pass and the larger must win.
    enum class Tier : uint8_t
    {
        None = 0,
        /// Network credentials only. The logbook, the location, the leaderboard
        /// identity and the opt-in all survive.
        Wifi = 1,
        /// Everything this device knows about its owner.
        Factory = 2,
    };

    inline Tier Larger(Tier a, Tier b) { return (uint8_t)a >= (uint8_t)b ? a : b; }

    inline const char* TierName(Tier t)
    {
        switch (t) {
            case Tier::Wifi:    return "wifi";
            case Tier::Factory: return "factory";
            default:            return "none";
        }
    }

    /// The NVS namespace holding THIS edition's logbook, or nullptr where the
    /// edition has none.
    ///
    /// Compiled per edition rather than cleared as a list, and that is a
    /// deliberate refusal: a radar build clearing "eam-log" would be reaching
    /// into a namespace belonging to firmware that is not running, which is the
    /// "took something with it" failure this whole file is arranged to avoid.
    /// A board reflashed across editions keeps the other edition's namespace,
    /// and that is the safe direction to be wrong in.
    inline const char* LogbookNamespace()
    {
#if defined(FEATURE_EAM)
        return "eam-log";
#elif defined(FEATURE_FISHING)
        return "fi-log";
#elif defined(FEATURE_SPACE)
        return "sp-log";
#elif defined(FEATURE_SEISMIC) || defined(FEATURE_BIRDING) || defined(FEATURE_CLAUDESCOPE) \
    || defined(FEATURE_SPEED)
        return nullptr;  // these editions persist no logbook
#else
        return "logbook";  // radar
#endif
    }

    /// Clear for `tier`, print the audit line, and return.
    ///
    /// `forgetWifiCredentials` is injected rather than called directly so this
    /// TU does not depend on WiFiManager -- main.cpp owns the instance and hands
    /// in a one-line lambda. It is invoked for BOTH tiers.
    ///
    /// Does NOT reboot. The caller decides when, because it also owns the screen
    /// the customer is reading while it happens.
    void Perform(Tier tier, const std::function<void()>& forgetWifiCredentials);

}  // namespace factoryreset

#endif  // BLIPSCOPE_FACTORY_RESET_H
