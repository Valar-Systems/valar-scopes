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

static const Scene kScenes[] = {
    {C1, 20, false}, {C1, 40, false}, {C1, 60, false},
    {C1, 20, true},  {C1, 40, true},  {C1, 40, true},  {C1, 60, true},
};
static const char* const kArmName[] = {"P0@20", "P0@40", "P0@60", "P1@20", "P1@40", "P-CTRL@40", "P1@60"};
// P-CTRL's plant, per scene index: FrameProfile.h's PlantLabels() spins 5.0 ms inside lbl_draw once a frame.
constexpr bool kLabelsPlant[] = {false, false, false, false, false, true, false};
