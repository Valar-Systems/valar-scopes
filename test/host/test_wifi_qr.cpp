// Host test for the setup screen's Wi-Fi QR (include/SetupQr.h, v15 item 6).
//
// The panel can show ONE name on ONE board. This sweeps every edition's name
// against every panel size, because "the name fits" is a population claim and a
// name that silently shrinks to nothing is invisible on the glass by construction.
//
// Inputs are taken from the OTHER side, not transcribed: the product names are
// parsed out of include/DeviceIdentity.h and the panel sizes out of
// include/variants/*.h, so a new edition or a new panel is swept the day it lands.
//
// The measurer here is the 6x8 GLCD arithmetic; on the device PickNameScale gets
// LovyanGFX's own textWidth/fontHeight. Same rule, two measurers.
//
// Explicit classes ("[ \t]", "[0-9]") rather than \s / \d: this MinGW libstdc++'s std::regex matches
// NOTHING with those classes (found when the BLIND control fired on a 0-row parse).
//
// NOT named test_setup_*: Windows' installer detection demands elevation for an
// unmanifested 32-bit exe with "setup" in its name, so the MinGW binary refused to
// start ("Permission denied" from bash) and the rig looked like a failing test.
//
// Usage: test_wifi_qr <repo root>     Exit: 0 ok, 1 a check failed, 2 blind.
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "../../include/SetupQr.h"

static int failures = 0;
static void check(bool ok, const std::string& what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); ++failures; }
}

static std::string slurp(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Byte-mode capacity at ECC LOW, versions 1..3, from the QR spec -- the same
// numbers QrRender.h uses. Transcribed ON PURPOSE for the claim being tested
// ("always version 3, never 2, never beyond the renderer's cap"); a drift in
// QrRender.h shows up on the glass as a REFUSED line in the serial log.
static int VersionFor(size_t len)
{
    const size_t cap[] = { 17, 32, 53 };
    for (int v = 1; v <= 3; ++v) if (len <= cap[v - 1]) return v;
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_wifi_qr <repo root>\n"); return 2; }
    const std::string root = argv[1];
    std::printf("setup screen Wi-Fi QR\n");

    // ---- the payload ----------------------------------------------------------
    char buf[96];
    size_t n = setupqr::WifiPayload("Blipscope-A1B2C3", buf, sizeof(buf));
    // T:nopass is REQUIRED: without it an iPhone camera join dropped 5 of 5 times
    // (2026-09-25). This assertion is what stops someone trimming it to save a version.
    check(std::string(buf, n) == "WIFI:T:nopass;S:Blipscope-A1B2C3;;", "the payload, exactly -- T:nopass kept");
    check(n == 34, "34 bytes");
    check(VersionFor(n) == 3, "version 3 at ECC LOW -- the 148 px symbol");

    n = setupqr::WifiPayload("a;b,c:d\\e\"f", buf, sizeof(buf));
    check(std::string(buf, n) == "WIFI:T:nopass;S:a\\;b\\,c\\:d\\\\e\\\"f;;", "specials are escaped");

    // Never a truncated payload: half an SSID is a DIFFERENT network.
    char tiny[20];
    check(setupqr::WifiPayload("Blipscope-A1B2C3", tiny, sizeof(tiny)) == 0, "too small a buffer -> 0");
    check(setupqr::WifiPayload("", buf, sizeof(buf)) == 0, "empty SSID -> 0");

    // ---- inputs from the other side ---------------------------------------------
    std::vector<std::string> products;
    {
        const std::string src = slurp(root + "/include/DeviceIdentity.h");
        std::regex re("define[ \t]+DEVICE_PRODUCT_NAME[ \t]+\"([^\"]+)\"");
        for (std::sregex_iterator it(src.begin(), src.end(), re), end; it != end; ++it)
            products.push_back((*it)[1]);
    }
    std::set<int> sizes;
    for (const char* v : { "s3_128", "s3_146", "s3_175_amoled", "s3_21" }) {
        const std::string src = slurp(root + "/include/variants/" + v + ".h");
        std::smatch m;
        if (std::regex_search(src, m, std::regex("SCREEN_SIZE[ \t]*=[ \t]*([0-9]+)")))
            sizes.insert(std::stoi(m[1]));
    }
    // CONTROL: the parse saw what it sweeps. An empty sweep passes everything.
    if (products.size() < 8 || sizes.size() < 4 ||
        std::find(products.begin(), products.end(), "Blipscope") == products.end()) {
        std::printf("  BLIND: parsed %zu product names, %zu panel sizes\n", products.size(), sizes.size());
        return 2;
    }
    std::printf("  swept %zu editions x %zu panels\n", products.size(), sizes.size());

    // ---- the sweep ------------------------------------------------------------------
    // LovyanGFX's scaled GLCD font advances a WHOLE number of pixels per glyph:
    // floor(6 * scale). The first version of this model used 6 * scale * len and
    // disagreed with the board -- it said 168 px for a 16-char name at 1.75, and the
    // device's own textWidth logged 160 (16 x 10). A model that disagrees with the
    // glass grades a layout the device does not draw.
    auto glcd = [](const std::string& s) {
        return [s](float sc, int* w, int* h) {
            *w = (int)s.size() * (int)(6.0f * sc);
            *h = (int)(8.0f * sc);
        };
    };
    // The longest advice line the title can carry ("NETWORK NOT FOUND", 17 chars at
    // scale 1): the placement is proven against it, not against "SETUP".
    const int TITLE_W = 17 * 6, TITLE_H = 8;
    int placed = 0, refused = 0, atTarget = 0;
    for (const std::string& p : products) {
        const std::string name = p + "-FFFFFF";                 // the widest hex
        n = setupqr::WifiPayload(name.c_str(), buf, sizeof(buf));
        check(VersionFor(n) == 3, name + ": version 3 (" + std::to_string(n) + " B)");
        for (int size : sizes) {
            const std::string at = name + " @" + std::to_string(size);
            const int side = 37 * setupqr::ModulePx(size);        // version 3 + quiet zone
            check(side <= (int)(size * 0.7071f), at + ": QR inside the inscribed square");
            const setupqr::Placement pl = setupqr::Place(size, side, TITLE_W, TITLE_H, glcd(name));
            check(setupqr::CornersOnGlass(size, side, pl.raise), at + ": raised QR keeps its corners on the glass");
            check(pl.titleY >= 0 && TITLE_W <= discgeom::ChordWidthPx(pl.titleY, TITLE_H, size),
                  at + ": the title fits above the QR");
            check(pl.nameY == pl.cy + side / 2 + setupqr::GAP_PX, at + ": the name sits under the QR");
            const float s = pl.nameScale;
            if (s >= 1.0f) ++placed; else ++refused;
            if (s >= setupqr::TARGET_SCALE) ++atTarget;
            check(s >= 1.0f, at + ": the name fits under the QR");
            // The fallback screen puts the name at the centre row: it must fit there too.
            check(setupqr::PickNameScale(glcd(name), size / 2, size) >= 1.0f,
                  name + " @" + std::to_string(size) + ": the name fits on the fallback screen");
        }
    }
    std::printf("  names placed %d, refused %d, at the target scale: %d\n", placed, refused, atTarget);

    // The unit on the bench, as the device logged it with Test T (2026-09-25):
    // "v3, side 148 px at 4 px/module, raise 7, name scale 1.75, name 160 px, chord 161 px".
    const int side240 = 37 * setupqr::ModulePx(240);
    check(side240 == 148, "240 px: 148 px QR at 4 px/module");
    const std::string bench = "Blipscope-A1B2C3";
    int w175 = 0, h175 = 0;
    glcd(bench)(1.75f, &w175, &h175);
    check(w175 == 160, "the model agrees with the device: 160 px at 1.75");
    // Centred, it does NOT fit at 1.75 -- the reason the QR is raised at all.
    const int centredNameY = 120 + side240 / 2 + setupqr::GAP_PX;
    const int centredChord = discgeom::ChordWidthPx(centredNameY, h175, 240);
    check(centredChord < w175, "CONTROL: centred, the 1.75 name does not fit the chord");
    const setupqr::Placement b = setupqr::Place(240, side240, TITLE_W, TITLE_H, glcd(bench));
    const int benchChord = discgeom::ChordWidthPx(b.nameY, h175, 240);
    std::printf("  bench name: centred chord %d px; raised %d px -> chord %d px vs name %d px, scale %.2f\n",
                centredChord, b.raise, benchChord, w175, (double)b.nameScale);
    check(b.nameScale == 1.75f, bench + " on 240 px: scale 1.75 (NAME_SCALE_240)");
    check(b.raise == 7, bench + " on 240 px: raised 7 px, as the device logged");
    check(benchChord == 161, bench + " on 240 px: chord 161 px, as the device logged");

    // CONTROL: the picker can refuse. A rule that has never said no is untested.
    check(setupqr::PickNameScale(glcd(std::string(40, 'W')), b.nameY, 240) == 0.0f,
          "CONTROL: a 40-character name reports 'does not fit'");
    // CONTROL: the corner proof can refuse -- raised by half the panel, a corner is off the glass.
    check(!setupqr::CornersOnGlass(240, side240, 60), "CONTROL: a 60 px raise puts a corner off the glass");

    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
