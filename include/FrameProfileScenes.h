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

#if defined(FRAME_PROFILE_GAINS) && FRAME_PROFILE_GAINS == 5
// G5 (REG-gains-AB.txt (A) A1): the profile + bench/speed-p5. Every scene level 40, weather on. O-CTRL first, beside
// 1a-OLD1; each cut OLD, NEW, OLD, NEW; 1c-CHECK untimed; the marks-off arms LAST (a marks-off arm leaves the buffers'
// dirty sets unmaintained, and nothing after S17 reads them).
static const Scene kScenes[] = {
    {OUTLINE_8COPY, 40, true},
    {OUTLINE_8COPY, 40, true}, {OUTLINE_MASK, 40, true}, {OUTLINE_8COPY, 40, true}, {OUTLINE_MASK, 40, true},
    {SWEEP_OLD, 40, true},     {SWEEP_LUT, 40, true},    {SWEEP_OLD, 40, true},     {SWEEP_LUT, 40, true},
    {CLEAR_CHECK, 40, true},
    {CLEAR_OLD, 40, true},     {CLEAR_DIRTY, 40, true},  {CLEAR_OLD, 40, true},     {CLEAR_DIRTY, 40, true},
    {CLEAR_OLD, 40, true},     {CLEAR_NOMARK, 40, true}, {CLEAR_OLD, 40, true},     {CLEAR_NOMARK, 40, true},
};
static const char* const kArmName[] = {"O-CTRL", "1a-OLD1", "1a-NEW1", "1a-OLD2", "1a-NEW2", "1b-OLD1", "1b-NEW1",
                                       "1b-OLD2", "1b-NEW2", "1c-CHECK", "1c-OLD1", "1c-NEW1", "1c-OLD2", "1c-NEW2",
                                       "MK-ON1", "MK-OFF1", "MK-ON2", "MK-OFF2"};
constexpr bool kLabelsPlant[] = {false, false, false, false, false, false, false, false, false, false, false, false,
                                 false, false, false, false, false, false};
// O-CTRL's plant, per scene index: FrameProfile.h's PlantOutline() spins 2.0 ms inside the outline span once a frame.
constexpr bool kOutlinePlant[] = {true, false, false, false, false, false, false, false, false, false, false, false,
                                  false, false, false, false, false, false};
#elif defined(FRAME_PROFILE_GAINS) && FRAME_PROFILE_GAINS == 6
// G6 (REG-gains-AB.txt (A) A2): the profile + bench/speed-p6. Timed scenes level 40, weather on; 6-CHECK untimed
// first; P-CTRL (A0a's plant, unchanged) beside 6-OLD1; OLD, NEW, OLD, NEW; the two still arms untimed, weather off,
// last (weatherscope's kinds turn the sweep and the trails off by kind and hold the positions).
static const Scene kScenes[] = {
    {LABELS_CHECK, 40, true},
    {LABELS_TODAY, 40, true},
    {LABELS_TODAY, 40, true}, {LABELS_CACHE, 40, true}, {LABELS_TODAY, 40, true}, {LABELS_CACHE, 40, true},
    {LABELS_TODAY_STILL, 40, false}, {LABELS_CACHE_STILL, 40, false},
};
static const char* const kArmName[] = {"6-CHECK", "P-CTRL", "6-OLD1", "6-NEW1", "6-OLD2", "6-NEW2", "6-STILL-OLD",
                                       "6-STILL-NEW"};
constexpr bool kLabelsPlant[] = {false, true, false, false, false, false, false, false};
constexpr bool kOutlinePlant[] = {false, false, false, false, false, false, false, false};
#elif defined(FRAME_PROFILE_GAINS)
#error "FRAME_PROFILE_GAINS is 5 (G5, bench/speed-p5) or 6 (G6, bench/speed-p6)"
#else
static const Scene kScenes[] = {
    {C1, 20, false}, {C1, 40, false}, {C1, 60, false},
    {C1, 20, true},  {C1, 40, true},  {C1, 40, true},  {C1, 60, true},
};
static const char* const kArmName[] = {"P0@20", "P0@40", "P0@60", "P1@20", "P1@40", "P-CTRL@40", "P1@60"};
// P-CTRL's plant, per scene index: FrameProfile.h's PlantLabels() spins 5.0 ms inside lbl_draw once a frame.
constexpr bool kLabelsPlant[] = {false, false, false, false, false, true, false};
constexpr bool kOutlinePlant[] = {false, false, false, false, false, false, false};
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
