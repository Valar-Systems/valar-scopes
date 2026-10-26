// Weatherscope's town subset (src/weatherscope/WxTowns.h) through the label rule (wxlogic::LabelText) --
// docs/weatherscope-stage3.md A8.14 D6(b), re-registered under Q13 in A9.13: for each town, LabelText(name, buf, 32)
// must give a non-empty label of printable ASCII (0x20-0x7E) that is BYTE-EQUAL to the name less its trailing
// " city" (title case kept; nothing truncated). A name that fails is reported, never coerced. Also PJ2 (A9.10): ranked
// by population (0 = no figure, ranking as 1,000), Dallas is first and Fort Worth second. D6(a)/PJ1, regeneration, is
// make_towns.py --check; PJ3 is check_pop.py.
//
//     test_wx_towns
//
// Its sabotages: python -I test/host/sabotage_wx_towns.py <zip> <csv>. Prints names and labels -- no coordinate.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "WxLogic.h"
#include "WxTowns.h"

int main()
{
    int checks = 0, failures = 0;
    std::printf("== D6(b): every town's label ==\n");
    for (unsigned i = 0; i < wxtowns::kCount; ++i) {
        const char* name = wxtowns::kTowns[i].name;
        char label[32];
        wxlogic::LabelText(name, label, sizeof label);
        const size_t n = std::strlen(label);
        bool printable = true;
        for (size_t k = 0; k < n; ++k) {
            const unsigned char ch = (unsigned char)label[k];
            if (ch < 0x20 || ch > 0x7E) printable = false;
        }
        const size_t nn = std::strlen(name);
        const bool exact = nn >= 5 && std::strcmp(name + nn - 5, " city") == 0 && n == nn - 5 &&
                           std::strncmp(label, name, n) == 0;
        const bool ok = n > 0 && printable && exact;
        ++checks;
        if (!ok) ++failures;
        std::printf("  %s D6:%u \"%s\" -> \"%s\"%s%s\n", ok ? "ok  " : "FAIL", i + 1, name, label,
                    printable ? "" : " (not printable ASCII)", exact ? "" : " (not byte-equal to the name less \" city\")");
    }

    std::printf("== PJ2: the two most populous ==\n");
    {
        unsigned first = 0, second = 0;
        auto rank = [](unsigned i) { return wxtowns::kTowns[i].pop ? wxtowns::kTowns[i].pop : 1000u; };
        for (unsigned i = 1; i < wxtowns::kCount; ++i)
            if (rank(i) > rank(first)) first = i;
        if (first == 0) second = 1;
        for (unsigned i = 0; i < wxtowns::kCount; ++i)
            if (i != first && rank(i) > rank(second)) second = i;
        const bool ok = std::strcmp(wxtowns::kTowns[first].name, "Dallas city") == 0 &&
                        std::strcmp(wxtowns::kTowns[second].name, "Fort Worth city") == 0;
        ++checks;
        if (!ok) ++failures;
        std::printf("  %s PJ2 first \"%s\" (%u), second \"%s\" (%u)\n", ok ? "ok  " : "FAIL", wxtowns::kTowns[first].name,
                    (unsigned)wxtowns::kTowns[first].pop, wxtowns::kTowns[second].name, (unsigned)wxtowns::kTowns[second].pop);
    }
    std::printf("%u towns; %d checks, %d failures\n", wxtowns::kCount, checks, failures);
    return failures ? 1 : 0;
}
