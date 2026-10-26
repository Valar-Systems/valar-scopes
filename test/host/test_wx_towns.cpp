// Weatherscope's town subset (src/weatherscope/WxTowns.h) through the label rule (wxlogic::LabelText) --
// docs/weatherscope-stage3.md A8.14, D6(b). For each town, LabelText(name, buf, 32) must give a non-empty label of
// printable ASCII (0x20-0x7E) with no lowercase letter, of EXACTLY strlen(name) - 5 bytes: the " city" dropped and
// nothing truncated. A name that fails is reported, never coerced. D6(a), regeneration, is make_towns.py --check.
//
//     test_wx_towns
//
// Its sabotages: python -I test/host/sabotage_wx_towns.py. Prints names and labels -- no coordinate.

#include <cstdio>
#include <cstdlib>  // before WxLogic.h, which calls strtod without including it (A8.15: a follow-up for its next touch)
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
        bool printable = true, upper = true;
        for (size_t k = 0; k < n; ++k) {
            const unsigned char ch = (unsigned char)label[k];
            if (ch < 0x20 || ch > 0x7E) printable = false;
            if (ch >= 'a' && ch <= 'z') upper = false;
        }
        const bool exact = std::strlen(name) >= 5 && n == std::strlen(name) - 5;
        const bool ok = n > 0 && printable && upper && exact;
        ++checks;
        if (!ok) ++failures;
        std::printf("  %s D6:%u \"%s\" -> \"%s\"%s%s%s\n", ok ? "ok  " : "FAIL", i + 1, name, label,
                    printable ? "" : " (not printable ASCII)", upper ? "" : " (lowercase)",
                    exact ? "" : " (not exactly the name less \" city\")");
    }
    std::printf("%u towns; %d checks, %d failures\n", wxtowns::kCount, checks, failures);
    return failures ? 1 : 0;
}
