// THE FRAME PROFILE's scene list (docs/frame-profile.md section 0, A0a), owned by p4. Included by FrameBench.h INSIDE
// namespace framebench, under FRAME_PROFILE, in place of C1's lists (map-client's split, bench/frame-bench-levels at
// 40f02c0fa812): Scene is { Kind kind; uint8_t level; bool wx; }, and every profile scene uses Kind C1, so it has no
// push delay and no loop +10, and C1's Kind arrays are never read past their end.
//
// THE ARMS, in run order: the weather-off arms first, then weather on, with P-CTRL next to P1@40 so its control is
// adjacent. Bit 2 of each record (the crop shown) reads whether weather was really off or on in each arm: before S0
// the board's own wx-layer applies, so a weather buffer may already exist when P0@20 starts.
//   S0 P0@20   S1 P0@40   S2 P0@60   S3 P1@20   S4 P1@40   S5 P-CTRL@40 (+5.0 ms in labels)   S6 P1@60
#pragma once

// THE PORT's two lists (PORT-SPEC b8c558fe87b9 amendment 1 B, the exact strings; bench/fp-port, never merged). Every
// arm level 40, weather on, the map not forced. A0a's list is not built on the port (FrameBench.h refuses it).
#if FRAME_PROFILE_GAINS == 10
// 1b-MAP: S-CTRL (SWEEP_LUT with +1.0 ms spun inside the sw_blend span, the reader's control S-1 against 1bM-NEW1),
// then OLD, NEW, OLD, NEW. G-MAP's line follows every arm's profile end line.
static const Scene kScenes[] = {
    {SWEEP_LUT, 40, true}, {SWEEP_OLD, 40, true}, {SWEEP_LUT, 40, true}, {SWEEP_OLD, 40, true}, {SWEEP_LUT, 40, true},
};
static const char* const kArmName[] = {"S-CTRL", "1bM-OLD1", "1bM-NEW1", "1bM-OLD2", "1bM-NEW2"};
constexpr bool kLabelsPlant[] = {false, false, false, false, false};
constexpr bool kSweepPlant[] = {true, false, false, false, false};
constexpr bool kOutlinePlant[] = {false, false, false, false, false};
constexpr bool kGMap = true;
#elif FRAME_PROFILE_GAINS == 11
// W (bench/fp-port-w = the port + W's PR): W-CHECK untimed first (REG-W amendment 1); O-CTRL (OUTLINE_8COPY with
// +2.0 ms inside the outline span, as G5's S0, the control O-1 against W-OLD1); then OLD, NEW, OLD, NEW.
static const Scene kScenes[] = {
    {OUTLINE_WCHECK, 40, true}, {OUTLINE_8COPY, 40, true}, {OUTLINE_8COPY, 40, true},
    {OUTLINE_W, 40, true},      {OUTLINE_8COPY, 40, true}, {OUTLINE_W, 40, true},
};
static const char* const kArmName[] = {"W-CHECK", "O-CTRL", "W-OLD1", "W-NEW1", "W-OLD2", "W-NEW2"};
constexpr bool kLabelsPlant[] = {false, false, false, false, false, false};
constexpr bool kSweepPlant[] = {false, false, false, false, false, false};
constexpr bool kOutlinePlant[] = {false, true, false, false, false, false};
constexpr bool kGMap = false;
#else
#error "the port's lists are 10 (1b-MAP) and 11 (W)"
#endif

// THE DRAWING KEYS (docs/frame-profile.md section 0 (e)): the keys that touch a phase P5/P6 targets (outline, labels,
// the sweep blend, weather, trails), answered in every scene at v20's defaults, the image's own parse in Initialise().
// infotext / triangle / trail are answered "true", not "": they parse as `if (!x.isEmpty()) member = ...`, so an empty
// answer would keep the BOARD's boot-time value (the KEEP-current trap). scanline and fade parse "" as on, but are
// answered "true" too, the same value. wx-look "" is look A. The info-* keys parse `stored.isEmpty() ? defaultOn : ..`,
// so "" is each field's compiled default. wx-layer is the scene's own (map-client's Override).
static const char* const kTargetKey[] = {
    "infotext", "triangle", "trail", "scanline", "fade", "wx-look",
    "info-callsign", "info-type", "info-operator", "info-reg", "info-route", "info-icao", "info-country",
    "info-speed", "info-vrate", "info-baroalt", "info-geoalt", "info-heading", "info-squawk", "info-category",
    "info-possrc",
};
static const char* const kTargetAnswer[] = {
    "true", "true", "true", "true", "true", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
};
constexpr int kTargetKeys = sizeof(kTargetKey) / sizeof(kTargetKey[0]);
static_assert(kTargetKeys == 21, "section 0 (e) registers 21 target keys");
static_assert(sizeof(kTargetAnswer) / sizeof(kTargetAnswer[0]) == (size_t)kTargetKeys, "one answer per target key");

// Per scene, a bit per DISTINCT target key answered (the orchestrator's refinement: a key read twice cannot hide a
// missing one). Written by ProfileOverride, read and printed by FrameProfile.h's Commit once per scene in the settle.
inline uint32_t g_answered[sizeof(kScenes) / sizeof(kScenes[0])] = {};

// Called only from p4's region in ConfigurationWebServer::GetStoredString, after map-client's, and only in a scene (the
// caller gates on InScene(), which is declared after this file). nullptr: not a drawing key, the board's own applies.
inline const char* ProfileOverride(const char* key, int scene)
{
    for (int i = 0; i < kTargetKeys; ++i) {
        if (strcmp(key, kTargetKey[i]) != 0) continue;
        if (scene >= 0 && scene < (int)(sizeof(g_answered) / sizeof(g_answered[0]))) g_answered[scene] |= 1u << i;
        return kTargetAnswer[i];
    }
    return nullptr;
}
inline int Distinct(uint32_t bits)
{
    int n = 0;
    for (; bits; bits &= bits - 1) ++n;
    return n;
}
