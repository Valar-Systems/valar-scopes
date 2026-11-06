// WD5o: the dot marker's black outline covers every 8-neighbour of the dot (docs/wx-device.md, WD5 addendum 3).
// The construction is READ FROM src/AircraftManager.cpp's marker block, and rasterised with LovyanGFX's own
// fillCircle, ported line for line from LGFXBase.cpp:302-339 (the copy in .pio/libdeps). The bench cannot grade
// this: markers are triangles by default, so a bench at defaults never draws a dot.
//
//   test_wx_outline.exe <repo-root>     exit 0 pass, 1 a property failed, 2 the rig (the block could not be read)
// Sabotages: test/host/sabotage_wx_outline.py plants one break per property into a copy of AircraftManager.cpp.
#include <cstdio>
#include <regex>
#include <set>
#include <string>
#include <utility>

using Px = std::set<std::pair<int, int>>;

static int failures = 0;
static void check(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

// LGFXBase::fillCircle and fillCircleHelper(corners 3, delta 0), as written there.
static void FillCircle(Px& px, int x, int y, int r)
{
    auto hline = [&](int x0, int y0, int w) { for (int k = 0; k < w; ++k) px.insert({ x0 + k, y0 }); };
    auto rect = [&](int x0, int y0, int w, int h) { for (int j = 0; j < h; ++j) hline(x0, y0 + j, w); };
    hline(x - r, y, (r << 1) + 1);
    if (r <= 0) return;
    const int delta = 1;
    int f = 1 - r, ddF_y = -(r << 1), ddF_x = 1, i = 0;
    do {
        int len = 0;
        while (f < 0) { f += (ddF_x += 2); ++len; }
        i += len;
        f += (ddF_y += 2);
        if (len) rect(x - r, y + i - len + 1, (r << 1) + delta, len);
        hline(x - i, y + r, (i << 1) + delta);
        hline(x - i, y - r, (i << 1) + delta);
        if (len) rect(x - r, y - i, (r << 1) + delta, len);
    } while (i < --r);
}

// The dot's 8-neighbours that the outline leaves uncovered.
static int Gaps(const Px& dot, const Px& outline)
{
    Px border;
    for (const auto& p : dot)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (!dot.count({ p.first + dx, p.second + dy })) border.insert({ p.first + dx, p.second + dy });
    int gaps = 0;
    for (const auto& p : border) gaps += !outline.count(p);
    return gaps;
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_wx_outline.exe <repo-root>\n"); return 2; }
    FILE* f = std::fopen((std::string(argv[1]) + "/src/AircraftManager.cpp").c_str(), "rb");
    if (!f) { std::printf("FAIL: could not open src/AircraftManager.cpp (rig)\n"); return 2; }
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    const size_t a = s.find("const bool outline = WxOutlineNow();");
    const size_t e = a == std::string::npos ? a : s.find("// special contact", a);
    if (a == std::string::npos || e == std::string::npos) {
        std::printf("FAIL: could not find the marker block in src/AircraftManager.cpp (rig)\n");
        return 2;
    }
    const std::string block = s.substr(a, e - a);

    // The dot itself.
    std::smatch m;
    if (!std::regex_search(block, m, std::regex(R"(fillCircle\(x, y, (\d+), markerColor\))"))) {
        std::printf("FAIL: the block draws no dot (rig)\n");
        return 2;
    }
    const int dotR = std::stoi(m[1]);
    const size_t dotAt = (size_t)m.position(0);
    Px dot;
    FillCircle(dot, 0, 0, dotR);

    // The outline as the source draws it: 8 offset copies inside the -1..1 loops, or one centred black disc.
    Px outline;
    std::string how;
    size_t outlineAt = std::string::npos;
    const bool loops = block.find("for (int oy = -1; oy <= 1; ++oy)") != std::string::npos &&
                       block.find("for (int ox = -1; ox <= 1; ++ox)") != std::string::npos;
    if (loops && std::regex_search(block, m, std::regex(R"(fillCircle\(x \+ ox, y \+ oy, (\d+), 0u\))"))) {
        const int r = std::stoi(m[1]);
        outlineAt = (size_t)m.position(0);
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox)
                if (ox != 0 || oy != 0) FillCircle(outline, ox, oy, r);
        how = "8 offset copies at radius " + std::to_string(r);
    } else if (std::regex_search(block, m, std::regex(R"(fillCircle\(x, y, (\d+), 0u\))"))) {
        outlineAt = (size_t)m.position(0);
        FillCircle(outline, 0, 0, std::stoi(m[1]));
        how = "one centred disc at radius " + std::string(m[1]);
    } else {
        how = "no black disc found";
    }
    std::printf("the dot: radius %d, %u px; its outline in the source: %s\n", dotR, (unsigned)dot.size(), how.c_str());

    // Control: the construction this test exists to refuse (one disc 1 px larger) must show its gaps.
    Px old;
    FillCircle(old, 0, 0, dotR + 1);
    const int oldGaps = Gaps(dot, old);
    check(oldGaps == 8, "WD5o control", "a single disc 1 px larger leaves " + std::to_string(oldGaps) + " gaps (the test can see a gap: 8)");

    const int gaps = outline.empty() ? -1 : Gaps(dot, outline);
    check(gaps == 0, "WD5o", "the dot's outline leaves " + (gaps < 0 ? std::string("everything") : std::to_string(gaps)) +
                                 " of its 8-neighbours uncovered (want 0)");
    check(outlineAt != std::string::npos && outlineAt < dotAt, "WD5o order", "the black outline is drawn before the dot");

    std::printf("%s\n", failures ? "WX OUTLINE TESTS FAILED" : "all WX outline checks passed");
    return failures ? 1 : 0;
}
