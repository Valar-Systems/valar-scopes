#pragma once
// The "Terrain shading" setting (docs/blipscope-map-client-steps.md, Step D, D1; the owner's ruling, 2026-11-12):
// the key map-terrain and its four values. Pure C++11, no Arduino, so test/host/test_map_terrain.cpp grades exactly
// what the device and the config page resolve.
//
//   value     S2: land + (shade - mid) x gain, per channel
//   off       no terrain: the land is the theme's land colour (Q-T2: the terrain layer is not drawn)
//   subtle    gain 0.075, mid 128: today's map, the Dark row's own gain
//   strong    gain 0.45, mid 180: THE DEFAULT. Level ground stores shade 180, so flat land keeps the land colour
//   xstrong   gain 0.45, mid 128
//
// EXACT LOWERCASE MATCH ONLY. Anything else -- unset, "", "STRONG", "subtle " with a space, "loud" -- reads as Strong,
// the default the owner chose. No published image ever wrote this key (DO2's search), so every board reads it unset.
// A later change of the default needs a ConfigMigration entry (CLAUDE.md: a default only reaches keys never saved).
#include <string.h>

namespace mapterrain {

enum class Level { Off, Subtle, Strong, Xstrong };

// The S2 inputs a level resolves to. `drawn` false clears the terrain layer from the build (Q-T2), so nothing is
// sampled and `gain` and `mid` are never read for Off. Off carries SUBTLE'S inputs (the Dark row's gain, mid 128), not
// gain 0 (the orchestrator, 2026-11-19 ~01:36Z): with gain 0 a build that wrongly left the layer on would draw land
// colour everywhere and look exactly like Off, so D3's plant "Off with the terrain layer left on" could never be seen.
struct Shading {
    bool drawn;
    double gain;
    int mid;
};

constexpr Level kDefault = Level::Strong;

inline Level Classify(const char* stored)
{
    if (stored == nullptr) return kDefault;
    if (strcmp(stored, "off") == 0) return Level::Off;
    if (strcmp(stored, "subtle") == 0) return Level::Subtle;
    if (strcmp(stored, "strong") == 0) return Level::Strong;
    if (strcmp(stored, "xstrong") == 0) return Level::Xstrong;
    return kDefault;   // unset, empty or unknown
}

inline Shading Of(Level l)
{
    switch (l) {
    case Level::Off: return Shading{false, 0.075, 128};   // Subtle's inputs, the layer cleared (see Shading)
    case Level::Subtle: return Shading{true, 0.075, 128};
    case Level::Xstrong: return Shading{true, 0.45, 128};
    case Level::Strong: break;
    }
    return Shading{true, 0.45, 180};
}

/** The S2 inputs for the stored value `stored` (nullptr or "" when the key is absent). */
inline Shading Resolve(const char* stored) { return Of(Classify(stored)); }

/** The stored value that names a level: what the page's options send and the bench's key line prints. */
inline const char* Name(Level l)
{
    switch (l) {
    case Level::Off: return "off";
    case Level::Subtle: return "subtle";
    case Level::Xstrong: return "xstrong";
    case Level::Strong: break;
    }
    return "strong";
}

}  // namespace mapterrain
