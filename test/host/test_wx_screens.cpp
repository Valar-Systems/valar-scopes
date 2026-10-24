// Weatherscope 3.3d's screen layer (src/weatherscope/WxScreens.h) against the truth table frozen in
// docs/weatherscope-stage3.md A8.20: 19 named cases. Its sabotages: python -I test/host/sabotage_wx_screens.py.
//
//     test_wx_screens                       the 19 cases
//     test_wx_screens --layout <loop dir>   prints each scenario screen's overlay rectangles and compared-pixel count,
//                                           from the real frames and towns, the way the board builds them (A8.21)
//
// Prints no coordinate: rectangles are screen pixels.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "WxCrop.h"
#include "WxLogic.h"
#include "WxRender.h"
#include "WxScenarios.h"
#include "WxScreens.h"
#include "WxTowns.h"

using namespace wxscreens;

namespace {

int checks = 0, failures = 0;
void check(const char* name, bool ok, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name, what.c_str());
}
std::string R(const Rect& r)
{
    char b[48];
    std::snprintf(b, sizeof b, "(%d, %d, %d, %d)", r.x, r.y, r.w, r.h);
    return b;
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
        c.index = 3;
        Step(c, 0, true);
        const bool a = Step(c, 30000, true);
        check("CY6", a && c.index == 0, "Warning -> index " + std::to_string(c.index));
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

    std::printf("== HW: the headline wrap ==\n");
    {
        char l[2][kMaxLine];
        const int n = Wrap("PRECIP WITHIN 15 MI", 13, l, 2);
        check("HW1", n == 2 && std::strcmp(l[0], "PRECIP WITHIN") == 0 && std::strcmp(l[1], "15 MI") == 0,
              std::to_string(n) + " lines: \"" + l[0] + "\"" + (n > 1 ? std::string(", \"") + l[1] + "\"" : ""));
    }
    {
        char l[2][kMaxLine];
        const int n = Wrap("RAIN NOW", 13, l, 2);
        check("HW2", n == 1 && std::strcmp(l[0], "RAIN NOW") == 0, std::to_string(n) + " line: \"" + l[0] + "\"");
    }

    std::printf("== TB: a text box ==\n");
    {
        const Rect r = TextBox(strlen("RAIN NOW"), 2, 184);
        check("TB1", r == Rect{72, 184, 96, 16}, R(r));
    }

    std::printf("== LF: a label against the fixed overlays ==\n");
    {
        Item fixed[1];
        fixed[0].r = Rect{100, 100, 20, 20};
        const bool crossing = ClearOf(Rect{110, 110, 30, 8}, fixed, 1);
        const bool clear = ClearOf(Rect{10, 10, 30, 8}, fixed, 1);
        check("LF1", !crossing && clear, std::string("crossing kept ") + std::to_string(crossing) + ", clear kept " +
                                             std::to_string(clear));
    }

    std::printf("== UN: the union ==\n");
    {
        const Rect r[2] = {{0, 0, 10, 10}, {5, 5, 10, 10}};
        const long u = UnionArea(r, 2);
        check("UN1", u == 175, std::to_string(u) + " px");
    }
}

bool ReadFile(const std::string& path, std::vector<uint8_t>* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out->clear();
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->insert(out->end(), buf, buf + n);
    std::fclose(f);
    return true;
}

// A8.21's tables: each scenario screen built from the real frames and towns, exactly as the board will.
int LayoutMode(const std::string& dir)
{
    struct Frame { std::vector<uint8_t> bytes, codes; wxcrop::Header h; };
    auto load = [&](const char* name, Frame* f) {
        if (!ReadFile(dir + "/" + name, &f->bytes)) return false;
        if (wxcrop::Validate(f->bytes.data(), f->bytes.size(), &f->h) != wxcrop::Status::Ok) return false;
        f->codes.assign(f->h.Cells(), 0);
        return wxcrop::DecodeInto(f->bytes.data(), f->bytes.size(), f->codes.data(), f->codes.size()) == wxcrop::Status::Ok;
    };
    Frame app, dry;
    if (!load("shsr-20240521-212400.wxc", &app) || !load("shsr-20240521-220000.wxc", &dry)) {
        std::printf("cannot load the scenario frames (rig)\n");
        return 2;
    }
    static wxrender::View v;
    wxrender::BuildView(dry.h, &v);   // D3: one geometry for all 15
    std::vector<wxlogic::LabelBox> scratch(wxtowns::kCount);
    std::vector<int> scratchTown(wxtowns::kCount);
    const wxscen::Kind kinds[4] = {wxscen::Kind::Dry, wxscen::Kind::Approaching, wxscen::Kind::HereNow,
                                   wxscen::Kind::Warning};
    const char* kindName[4] = {"dry", "approaching", "herenow", "warning"};
    for (int k = 0; k < 4; ++k) {
        const Frame& f = kinds[k] == wxscen::Kind::Dry ? dry : app;
        wxscen::Scenario s;
        if (!wxscen::Build(kinds[k], f.h, f.codes.data(), &s)) { std::printf("Build failed (rig)\n"); return 2; }
        const wxlogic::Choice c = s.Choose();
        Texts t;
        BuildTexts(s, c, &t);
        LatLon box[2];
        WarningBox(s, box);
        static Layout L;
        BuildLayout(L, c.screen, t, v, c.screen == wxlogic::Screen::Warning ? box : nullptr, wxtowns::kTowns,
                    wxtowns::kCount, scratch.data(), scratchTown.data());
        std::vector<Rect> rs;
        for (int i = 0; i < L.n; ++i) rs.push_back(L.items[i].r);
        const long u = UnionArea(rs.data(), (int)rs.size());
        std::printf("LAYOUT %s items=%d fixed=%d towns=%d union=%ld compared=%ld\n", kindName[k], L.n, L.nFixed,
                    L.n - L.nFixed, u, 57600 - u);
        for (int i = 0; i < L.n; ++i) {
            const Item& it = L.items[i];
            const char* kind = it.kind == ItemKind::Text ? "text" : it.kind == ItemKind::Marker ? "marker"
                             : it.kind == ItemKind::Edge ? "edge" : "town";
            std::printf("RECT %s %d %d,%d,%d,%d %s \"%s\"\n", kindName[k], i, it.r.x, it.r.y, it.r.w, it.r.h, kind,
                        it.text);
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--layout") == 0) return LayoutMode(argv[2]);
    if (argc != 1) { std::printf("usage: test_wx_screens [--layout <loop dir>]\n"); return 2; }
    Cases();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
