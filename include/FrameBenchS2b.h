// THE FRAME BENCH, S2b (docs/frame-time-bench.md, "S2b's registration: RP5's run on the v20 sibling"): RP5's scene
// table and its constants, on the v20 sibling, included by FrameBench.h INSIDE namespace framebench and ONLY under
// -DFRAME_BENCH_S2B (env blipscope-s3-128-framebench-v20-s2b, slug-less, never published). Every other bench env
// never reads this file, so its preprocessed text is the same as at 8fb80fef41d1 (S2B-ISO proves it from the images).
// Each constant is RP5's (b417d80cd4c5) unless its comment names the difference. Nothing here prints anything.
#pragma once
#if defined(FRAME_BENCH_S2B)

#ifndef FRAME_BENCH_HOST
#error "FRAME_BENCH_S2B needs FRAME_BENCH_HOST: set the untracked env var; scripts/framebench_host.py passes it in"
#endif
#if !defined(FRAME_BENCH_REC_B) || FRAME_BENCH_REC_B < 1 || FRAME_BENCH_REC_B > 8
#error "FRAME_BENCH_S2B needs FRAME_BENCH_REC_B in 1..8 (the survey's choice)"
#endif

// THE BAR'S RUNGS (the owner, 2026-11-12 23:31Z): the six scenes are the two homes at these radii. ONE constant: the
// scene table and its reader derive from it (FB-R6). 10 mi is reported beside it, never in the worst-of-six.
static const uint8_t kRungs[] = {25, 50, 100};
constexpr uint8_t kReportedMi = 10;
constexpr uint8_t kShippingRadiusMi = 100;   // an unset "radius" reads 100, an unset unit "mi": /blips r=161, limit 40

// THE AIRPORTS (the recording plan, RP2 and Q-RC3), at 2 dp, equal to include/Airports.h's rows (FB-R5 holds the copy to
// it). Index 0 is KSFO. Only ever returned to Initialise() and compared to a request; never printed.
static const char* const kAptCode[] = {"SFO", "ATL", "ORD", "DFW", "DEN", "LAX", "JFK", "LAS", "CLT"};
static const char* const kAptLat[] = {"37.62", "33.64", "41.98", "32.90", "39.86", "33.94", "40.64", "36.08", "35.21"};
static const char* const kAptLon[] = {"-122.38", "-84.43", "-87.90", "-97.04", "-104.67", "-118.41", "-73.78",
                                      "-115.15", "-80.94"};
constexpr int kNumApt = sizeof(kAptCode) / sizeof(kAptCode[0]);
static_assert(kNumApt == 9, "KSFO and Q-RC3's eight candidates");

#include "framebench_rec_gen.h"   // GENERATED from the two pinned recordings (scripts/framebench_rec.py), untracked
static_assert(sizeof(kRungs) / sizeof(kRungs[0]) == 3, "the six: two homes x three rungs (the owner, 23:31Z)");

// RP5's 18 scenes, unchanged: CTRL-IL (F3: I-6's S0, packed Bend/40/25, local), THE SIX (KSFO and the choice at kRungs),
// the 10 mi rows, the packed stress rows (Bend 40/60 at kRungs), LIVE-APT at both airports (F2), LIVE-OWN.
// The sibling's Scene is {kind, pkg, rungMi, role, home, level}: rungMi holds RP5's rung, and home and level exist only
// under FRAME_BENCH_S2B. pkg and role are FT's and stay 0 here.
#define FB_S2B_SCENE(kind, home, level, rung) {kind, 0, (uint8_t)(rung), GRADED, (uint8_t)(home), (uint8_t)(level)}
static const Scene kScenes[] = {
    FB_S2B_SCENE(CTRL_IL, 0, 40, 25),   // I-6's S0 (Bend/40/25): the control's own configuration, not a rung of the bar
    FB_S2B_SCENE(REPLAY, 0, 0, kRungs[0]), FB_S2B_SCENE(REPLAY, 0, 0, kRungs[1]), FB_S2B_SCENE(REPLAY, 0, 0, kRungs[2]),
    FB_S2B_SCENE(REPLAY, FRAME_BENCH_REC_B, 0, kRungs[0]), FB_S2B_SCENE(REPLAY, FRAME_BENCH_REC_B, 0, kRungs[1]),
    FB_S2B_SCENE(REPLAY, FRAME_BENCH_REC_B, 0, kRungs[2]),
    FB_S2B_SCENE(REPLAY, 0, 0, kReportedMi), FB_S2B_SCENE(REPLAY, FRAME_BENCH_REC_B, 0, kReportedMi),
    FB_S2B_SCENE(FIXED, 0, 40, kRungs[0]), FB_S2B_SCENE(FIXED, 0, 40, kRungs[1]), FB_S2B_SCENE(FIXED, 0, 40, kRungs[2]),
    FB_S2B_SCENE(FIXED, 0, 60, kRungs[0]), FB_S2B_SCENE(FIXED, 0, 60, kRungs[1]), FB_S2B_SCENE(FIXED, 0, 60, kRungs[2]),
    // (a)+ (2026-11-17): LIVE-APT at kRungs[0], configured at the shipping radius and zoomed (ShipsThenZooms)
    FB_S2B_SCENE(LIVE_APT, 0, 0, kRungs[0]), FB_S2B_SCENE(LIVE_APT, FRAME_BENCH_REC_B, 0, kRungs[0]),
    FB_S2B_SCENE(LIVE_OWN, 0, 0, 0),
};
#undef FB_S2B_SCENE
static_assert(sizeof(kScenes) / sizeof(kScenes[0]) == 18, "RP5's registered run is 18 scenes");

// The local homes of CTRL-IL and the packed rows, to 2 dp: only ever returned to Initialise(); nothing prints them.
static const char* const kS2bHomeName[] = {"Bend", "KSFO"};
static const char* const kS2bHomeLat[] = {"44.06", "37.62"};
static const char* const kS2bHomeLon[] = {"-121.31", "-122.38"};

// THE CUSTOMER DEFAULTS (the cloud reviewer's item 2, ruled 01:15:18Z): every scene but LIVE-OWN reads what a factory-
// fresh board reads, which is "" for every key. kOwnKeys stay the board's own (they draw nothing); the six KEEP-CURRENT
// keys, whose parse keeps the member's CURRENT value on "" (the board's saved one, after boot), answer the member's
// initializer instead. FB-R11 holds both lists to the source. v20's key set is v19's (86 literal keys, read 2026-11-16).
static const char* const kOwnKeys[] = {"cloud-url", "cloud-key", "cloud-key-fac", "opensky-id", "opensky-secret"};
static const char* const kKeepKeys[] = {"infotext", "triangle", "airports", "trail", "altcolor", "highlight"};
constexpr int kNumKeep = sizeof(kKeepKeys) / sizeof(kKeepKeys[0]);

constexpr uint32_t MEASURE_OWN_MS = 300000;   // LIVE-OWN: about 10 [health] windows
constexpr uint32_t MEASURE_APT_MS = 300000;   // LIVE-APT: 10 windows of 30 s, so XA has >= 8 a side (F2)

// THE MAP, BUILT IN (s2b_rerun_registration.amend3.txt; the owner, 2026-11-18 23:25Z: "S2b: built-in"). map-server's
// two packages for exactly the queries this image sends at KSFO and LAX, pinned in test/fixtures/frame-bench/
// s2b_pkg.pins and embedded by scripts/ft_prebuild.py. Index 0 is KSFO (home 0), 1 the second recording's airport,
// which must be LAX. LIVE-OWN, CTRL-IL and the packed rows install none (-1): LIVE-OWN's home is the board's own and
// is never printed (RULED), and the other two read the local feed, where the map is off by construction.
#if defined(MAP_BENCH)
static_assert(FRAME_BENCH_REC_B == 5, "the built-in second package is LAX's: S2b's recording B must be LAX");
extern const uint8_t kS2bPkgKsfoStart[] asm("_binary__pio_ftpkg_KSFO_S2B_s2b_bmpk_start");
extern const uint8_t kS2bPkgKsfoEnd[] asm("_binary__pio_ftpkg_KSFO_S2B_s2b_bmpk_end");
extern const uint8_t kS2bPkgLaxStart[] asm("_binary__pio_ftpkg_LAX_S2B_s2b_bmpk_start");
extern const uint8_t kS2bPkgLaxEnd[] asm("_binary__pio_ftpkg_LAX_S2B_s2b_bmpk_end");
constexpr int kS2bNumPkgs = 2;
// (c): printed once at boot under "[s2b-map]"; S2B-TAB holds these to the pin list (point-variant, sha256 prefix)
static const char* const kS2bPkgName[kS2bNumPkgs] = {"KSFO-S2B-s2b", "LAX-S2B-s2b"};
static const char* const kS2bPkgSha12[kS2bNumPkgs] = {"d94687ff5012", "a1813ea0c239"};
inline const uint8_t* S2bPkgStart(int i) { return i == 0 ? kS2bPkgKsfoStart : i == 1 ? kS2bPkgLaxStart : nullptr; }
inline size_t S2bPkgLen(int i)
{
    return i == 0 ? (size_t)(kS2bPkgKsfoEnd - kS2bPkgKsfoStart) : i == 1 ? (size_t)(kS2bPkgLaxEnd - kS2bPkgLaxStart) : 0;
}
// The package scene k installs: REPLAY and LIVE-APT at KSFO -> 0, at the second recording's airport -> 1; every other
// scene -> -1, none (B-NONE).
inline int S2bPkgOf(int k)
{
    const Scene& sc = kScenes[k];
    if (sc.kind != REPLAY && sc.kind != LIVE_APT) return -1;
    return sc.home == 0 ? 0 : sc.home == FRAME_BENCH_REC_B ? 1 : -1;
}
#endif

#endif  // FRAME_BENCH_S2B
