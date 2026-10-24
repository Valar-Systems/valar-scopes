// Weatherscope's scenario cycle and text helpers (src/weatherscope/WxScreens.h) against the truth table frozen in
// docs/weatherscope-stage3.md A8.20, re-registered by A9.10: 15 named cases (CY1-CY7, WT1-WT5, CK1-CK3). 3.5a's
// layout rules are test_wx_layout.cpp's. Its sabotages: python -I test/host/sabotage_wx_screens.py.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "WxScreens.h"

using namespace wxscreens;

namespace {

int checks = 0, failures = 0;
void check(const char* name, bool ok, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name, what.c_str());
}

void Cases()
{
    std::printf("== CY: the scenario cycle ==\n");
    {
        Cycle c;
        Step(c, 0, true);
        const bool a = Step(c, 30000, true);
        check("CY1", a && c.index == 1, "advanced " + std::to_string(a) + ", index " + std::to_string(c.index));
    }
    {
        Cycle c;
        Step(c, 0, true);
        const bool a = Step(c, 29999, true);
        check("CY2", !a && c.index == 0, "advanced " + std::to_string(a) + ", index " + std::to_string(c.index));
    }
    {
        Cycle c;
        bool any = false;
        Step(c, 0, false);
        for (uint32_t t = 1000; t <= 600000; t += 1000) any |= Step(c, t, false);
        check("CY3", !any && c.index == 0, "live feeds, 600 s: advanced " + std::to_string(any));
    }
    {
        Cycle c;
        Step(c, 0, true);
        Pause(c, true);
        const bool a = Step(c, 60000, true);
        check("CY4", !a && c.index == 0, "paused 60 s: advanced " + std::to_string(a));
    }
    {
        Cycle c;
        Step(c, 0, true);
        const bool a1 = Step(c, 10000, true);
        Pause(c, true);
        const bool a2 = Step(c, 60000, true);
        Pause(c, false);
        const bool a3 = Step(c, 79999, true);
        const bool a4 = Step(c, 80000, true);
        check("CY5", !a1 && !a2 && !a3 && a4 && c.index == 1,
              "at 10 s " + std::to_string(a1) + ", paused to 60 s " + std::to_string(a2) + ", 79.999 s " +
                  std::to_string(a3) + ", 80 s " + std::to_string(a4));
    }
    {
        Cycle c;
        c.index = 5;
        Step(c, 0, true);
        const bool a = Step(c, 30000, true);
        check("CY6", a && c.index == 0, "Clear -> index " + std::to_string(c.index));
    }
    {
        Cycle c;
        c.index = 3;
        Step(c, 0, true);
        const bool a = Step(c, 30000, true);
        check("CY7", a && c.index == 4, "Warning -> index " + std::to_string(c.index));
    }

    std::printf("== WT: the warning title ==\n");
    const char* events[5] = {"Tornado Warning", "Snow Squall Warning", "Severe Thunderstorm Warning",
                             "Flash Flood Warning", "Winter Storm Warning"};
    const char* want[5] = {"TORNADO", "SNOW SQUALL", "SEVERE T-STORM", "FLASH FLOOD", nullptr};
    const char* names[5] = {"WT1", "WT2", "WT3", "WT4", "WT5"};
    for (int i = 0; i < 5; ++i) {
        const char* l1 = nullptr;
        const char* l2 = nullptr;
        const bool ok = WarningTitle(events[i], &l1, &l2);
        const bool good = want[i] ? (ok && std::strcmp(l1, want[i]) == 0 && std::strcmp(l2, "WARNING") == 0) : !ok;
        check(names[i], good, std::string(events[i]) + " -> " + (ok ? std::string(l1) + " / " + l2 : "no title"));
    }

    std::printf("== CK: the clock ==\n");
    struct { const char* name; int64_t utc; const char* want; } ck[3] = {
        {"CK1", 1716326640, "4:24 PM"},    // 2024-05-21T21:24:00Z
        {"CK2", 1716267900, "12:05 AM"},   // 2024-05-21T05:05:00Z
        {"CK3", 1716310800, "12:00 PM"},   // 2024-05-21T17:00:00Z
    };
    for (auto& k : ck) {
        char out[16];
        ClockText(k.utc, -300, out, sizeof out);
        check(k.name, std::strcmp(out, k.want) == 0, std::string("\"") + out + "\"");
    }
}

}  // namespace

int main(int argc, char** argv)
{
    (void)argv;
    if (argc != 1) { std::printf("usage: test_wx_screens\n"); return 2; }
    Cases();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
