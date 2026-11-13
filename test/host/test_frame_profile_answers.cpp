// docs/frame-profile.md section 0 (e): the answered-key row's logic, on the host. FrameProfileScenes.h's ProfileOverride
// sets one bit per DISTINCT target key per scene, and the device prints the popcount. Each case states its expected
// count; the two plants must read 20, the control 21.
//   g++ -std=c++17 -Wall -Wextra -I include test/host/test_frame_profile_answers.cpp -o /tmp/tfpa && /tmp/tfpa
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace framebench {
enum Kind : uint8_t { C1 = 0 };
struct Scene {   // map-client's shape at bench/frame-bench-levels 40f02c0fa812
    Kind kind;
    uint8_t level = 40;
    bool wx = true;
};
#include "FrameProfileScenes.h"
}  // namespace framebench

using namespace framebench;
static int g_fail = 0, g_checks = 0;

static void check(bool ok, const char* what)
{
    ++g_checks;
    if (!ok) ++g_fail;
    printf("%s %s\n", ok ? "  ok  " : "  OFF ", what);
}

static int Answer(int scene, int skip, int twice)
{
    g_answered[scene] = 0;
    for (int i = 0; i < kTargetKeys; ++i) {
        if (i == skip) continue;
        ProfileOverride(kTargetKey[i], scene);
        if (i == twice) ProfileOverride(kTargetKey[i], scene);
    }
    return Distinct(g_answered[scene]);
}

int main()
{
    check(kTargetKeys == 21, "21 target keys");
    check(Answer(0, -1, -1) == 21, "CONTROL: every key once reads 21");
    check(Answer(1, 7, -1) == 20, "PLANT 1: one key dropped reads 20");
    check(Answer(2, 7, 3) == 20, "PLANT 2: one key answered twice and another dropped reads 20");
    check(Answer(3, -1, 0) == 21, "every key once, one twice, reads 21 (a save mid-scene)");
    check(ProfileOverride("latitude", 4) == nullptr && g_answered[4] == 0, "a non-target key: nullptr, no bit");
    check(strcmp(ProfileOverride("infotext", 5), "true") == 0, "infotext answers \"true\"");
    check(strcmp(ProfileOverride("trail", 5), "true") == 0, "trail answers \"true\"");
    check(strcmp(ProfileOverride("wx-look", 5), "") == 0, "wx-look answers \"\" (look A)");
    check(strcmp(ProfileOverride("info-type", 5), "") == 0, "info-type answers \"\" (defaultOn)");
    check(ProfileOverride("infotext", 99) != nullptr, "an out-of-range scene still answers, and sets no bit");
    printf("%d checks, %d off\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
