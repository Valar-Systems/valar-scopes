#pragma once

// What this binary IS -- the env it was built from, its version, and the
// compile-time features that change its behaviour.
//
// WHY THIS EXISTS. A wrong-env flash is externally indistinguishable from a
// hardware fault or an upstream outage. Flashing the non-cloud
// `blipscope-s3-128` onto a board that had been running `blipscope-s3-128-prodburn`
// produced three simultaneous symptoms -- no leaderboard submits (the submit code
// is inside #ifdef FEATURE_CLOUD_FEED and simply was not in the binary), stale
// data with the sweep cutting out (fell back to OpenSky, whose interval is
// MS_PER_DAY/dailyRequestBudget: minutes, not seconds), and photos that never
// loaded (no cloud enrichment) -- and every one of them was plausibly explained
// by a dense-sky capacity ceiling that did not exist. The diagnosis was coherent,
// which is exactly what stopped anyone re-checking the premise.
//
// So identity is reported unconditionally, in two places that need no prior
// knowledge to read: the boot banner and the config page. Not a log line whose
// presence you have to already know about -- that is the failure mode this
// replaces.
//
// BUILD_ENV comes from $PIOENV in [common], so every env has it and a new env
// cannot forget to.

#include <Arduino.h>
#include "OtaUpdater.h"   // FW_VERSION

#ifndef BUILD_ENV
#define BUILD_ENV "unknown-env"   // building outside PlatformIO
#endif

namespace BuildIdentity {

    inline const char* Env() { return BUILD_ENV; }

    // The compile-time flags that change behaviour, as a compact "a+b+c" string.
    // Deliberately includes the NEGATIVE for the cloud feed: "no-cloud" is the
    // single most consequential thing this binary can be, and an absent token is
    // far easier to miss than a present one saying so.
    inline String Features()
    {
        String s;
        auto add = [&s](const char* f) { if (s.length()) s += "+"; s += f; };
#ifdef FEATURE_CLOUD_FEED
        add("cloud");
#else
        add("no-cloud");
#endif
#ifdef FEATURE_EAM
        add("eam");
#endif
#ifdef FEATURE_SPACE
        add("space");
#endif
#ifdef FEATURE_SEISMIC
        add("seismic");
#endif
#ifdef FEATURE_BIRDING
        add("birding");
#endif
#ifdef FEATURE_FISHING
        add("fishing");
#endif
#ifdef FEATURE_CLAUDESCOPE
        add("claudescope");
#endif
#ifdef FEATURE_SPEED
        add("speed");
#endif
#ifdef SOAK_TEST
        add("soak");
#endif
        return s;
    }

    // Where the cloud feed points, or "" when this build has none. The staging vs
    // production distinction is invisible on the glass and matters: a board on a
    // staging image reports to a board nobody is looking at.
    inline const char* CloudBase()
    {
#if defined(FEATURE_CLOUD_FEED) && defined(CLOUD_FEED_BASE)
        return CLOUD_FEED_BASE;
#else
        return "";
#endif
    }

    // One line, printed before anything else can fail. Read it first, always.
    inline void PrintBanner()
    {
        Serial.printf("\n[build] env=%s fw=v%d features=%s", Env(), (int)FW_VERSION,
                      Features().c_str());
        if (CloudBase()[0])
            Serial.printf(" cloud=%s", CloudBase());
        Serial.printf("\n[build] confirm this matches the image you intended BEFORE "
                      "diagnosing any symptom\n");
    }

    // "blipscope-s3-128-prodburn - v5 - cloud" for the config page's status row.
    inline String Summary()
    {
        return String(Env()) + " \xC2\xB7 v" + String((int)FW_VERSION) + " \xC2\xB7 " + Features();
    }

}
