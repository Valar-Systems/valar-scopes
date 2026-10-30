// K1, K2 (docs/wx-device.md, "The look as a shipping setting"): include/WxLookSelectHtml.h and wxs2::LookFromStored.
// The labels are written out here in Daniel's words, NOT read from the header's constants, so a changed label cannot
// pass by changing both. Rows are tagged [K1] [K2] for test/host/sabotage_wx_look_select.py. Exit: 0 ok, 1 a row failed.
#include <cstdio>
#include <string>

#include "WxLookSelectHtml.h"

static int failures = 0;
static void row(bool ok, const char* tag, const char* what)
{
    std::printf("  %s  [%s] %s\n", ok ? "ok  " : "FAIL", tag, what);
    if (!ok) ++failures;
}

static int count(const std::string& h, const std::string& needle)
{
    int n = 0;
    for (size_t at = h.find(needle); at != std::string::npos; at = h.find(needle, at + 1)) ++n;
    return n;
}

int main()
{
    std::printf("WxLookSelectHtml.h and wxs2::LookFromStored: the look as a shipping setting\n");
    const std::string kA = ">Brighter rain, outlined planes</option>";   // Daniel's wording, 2026-10-29
    const std::string kB = ">Fainter rain, no outlines</option>";
    struct Case { const char* stored; char want; };
    const Case cases[] = {{"", 'A'}, {"A", 'A'}, {"B", 'B'}, {"x", 'A'}};
    bool k1 = true;
    for (const Case& c : cases) {
        const std::string h = wxlookselect::Html(c.stored);
        const bool one = count(h, "<option") == 2 && count(h, " selected") == 1 && count(h, "name=\"wx-look\"") == 1 &&
                         h.find("<option value=\"A\"") < h.find("<option value=\"B\"") && count(h, kA) == 1 &&
                         count(h, kB) == 1 && h.find('%') == std::string::npos &&
                         h.find(c.want == 'A' ? "<option value=\"A\" selected>" : "<option value=\"B\" selected>") !=
                             std::string::npos;
        if (!one) std::printf("    stored \"%s\": %s\n", c.stored, h.c_str());
        k1 = k1 && one;
    }
    row(k1, "K1", "two options, A then B, Daniel's labels exactly; A selected for \"\", \"A\" and \"x\", B for \"B\"; no '%'");

    using wxs2::Look;
    const bool k2 = wxs2::LookFromStored("") == Look::A && wxs2::LookFromStored("A") == Look::A &&
                    wxs2::LookFromStored("B") == Look::B && wxs2::LookFromStored("b") == Look::A &&
                    wxs2::LookFromStored("x") == Look::A && wxs2::LookFromStored("BB") == Look::A &&
                    wxs2::LookFromStored(nullptr) == Look::A;
    row(k2, "K2", "LookFromStored: \"B\" is B; \"\", \"A\", \"b\", \"x\", \"BB\" and null are A");

    std::printf(failures ? "WX LOOK SELECT TESTS FAILED\n" : "all WX look select checks passed\n");
    return failures ? 1 : 0;
}
