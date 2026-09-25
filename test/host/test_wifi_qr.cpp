// Host test for the setup screen's Wi-Fi QR (include/SetupQr.h, v15 item 6).
//
// The panel can show ONE name on ONE board. This sweeps every edition's name
// against every panel size, because "the name fits" is a population claim and a
// name that silently shrinks to nothing is invisible on the glass by construction.
//
// Inputs are taken from the OTHER side, not transcribed: the product names are
// parsed out of include/DeviceIdentity.h, the panel sizes out of
// include/variants/*.h, and the FreeSans glyph widths out of LovyanGFX's own font
// headers (argv[2], found by run.sh under .pio/libdeps). Missing fonts = BLIND,
// never a skip: an easy skip is a hole in the one check that grades the layout.
//
// The measurers here are the fonts' own tables; on the device PickNameRank gets
// LovyanGFX's textWidth/fontHeight. Same rule, two measurers -- and where they
// disagreed once (the 5x7 font at 1.75: 168 px here, 160 on the glass) the model
// was fixed to match the device, never the other way round.
//
// Explicit classes ("[ \t]", "[0-9]") rather than \s / \d: this MinGW libstdc++'s std::regex matches
// NOTHING with those classes (found when the BLIND control fired on a 0-row parse).
//
// NOT named test_setup_*: Windows' installer detection demands elevation for an
// unmanifested 32-bit exe with "setup" in its name, so the MinGW binary refused to
// start ("Permission denied" from bash) and the rig looked like a failing test.
//
// Usage: test_wifi_qr <repo root> <LovyanGFX GFXFF font dir>
// Exit:  0 ok, 1 a check failed, 2 blind.
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
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
// ("always version 3, never beyond the renderer's cap"); a drift in QrRender.h
// shows up on the glass as a REFUSED line in the serial log.
static int VersionFor(size_t len)
{
    const size_t cap[] = { 17, 32, 53 };
    for (int v = 1; v <= 3; ++v) if (len <= cap[v - 1]) return v;
    return 0;
}

// One GFX font's metrics, from its LovyanGFX header: xAdvance per glyph 0x20..0x7E
// (GFXglyph = {bitmapOffset, width, height, xAdvance, xOffset, yOffset}) and the
// height LovyanGFX's fontHeight() reports.
//
// THE HEIGHT IS NOT yAdvance. The first version of this model used yAdvance (22 for
// FreeSansBold9pt7b); the device logged fontHeight() = 18 and a raise of 8, not 12.
// LovyanGFX derives it in GFXfont::getDefaultMetric (lgfx_fonts.cpp): the tallest
// ascent (-yOffset) plus the deepest descent (height - ascent) over glyphs
// first..last-1 -- the loop runs c < last - first, so '~' is not included.
struct GfxMetrics { std::vector<int> adv; int height = 0; };

static bool LoadGfx(const std::string& path, GfxMetrics& m)
{
    const std::string src = slurp(path);
    if (src.empty()) return false;
    std::regex g("\\{[ \t]*([0-9]+),[ \t]*([0-9]+),[ \t]*([0-9]+),[ \t]*([0-9]+),[ \t]*(-?[0-9]+),[ \t]*(-?[0-9]+)[ \t]*\\}");
    int ab = 0, bb = 0, i = 0;
    for (std::sregex_iterator it(src.begin(), src.end(), g), end; it != end; ++it, ++i) {
        m.adv.push_back(std::stoi((*it)[4]));
        if (i >= 94) continue;                         // LovyanGFX's loop stops before '~'
        const int a = -std::stoi((*it)[6]);            // above the baseline
        const int b = std::stoi((*it)[3]) - a;         // below it
        if (a > ab) ab = a;
        if (b > bb) bb = b;
    }
    m.height = ab + bb;
    return m.adv.size() == 95 && m.height > 0;         // ' '..'~', and a line height
}

static std::map<setupqr::NameStyle, GfxMetrics> gfx;

// Width/height of `s` in style `st`: the fonts' own tables for FreeSans, and the
// 5x7 font's whole-pixel advance (floor(6 * scale) per glyph) for the GLCD rungs.
static void Measure(const std::string& s, setupqr::NameStyle st, int* w, int* h)
{
    float glcd = 0.0f;
    switch (st) {
        case setupqr::NameStyle::Glcd175: glcd = 1.75f; break;
        case setupqr::NameStyle::Glcd15:  glcd = 1.5f;  break;
        case setupqr::NameStyle::Glcd125: glcd = 1.25f; break;
        case setupqr::NameStyle::Glcd1:   glcd = 1.0f;  break;
        default: break;
    }
    if (glcd > 0.0f) {
        *w = (int)s.size() * (int)(6.0f * glcd) + setupqr::DashPadPx(s.c_str());
        *h = (int)(8.0f * glcd);
        return;
    }
    const GfxMetrics& m = gfx[st];
    int sum = 0;
    for (char c : s) sum += m.adv[(unsigned char)c - 0x20];
    *w = sum + setupqr::DashPadPx(s.c_str());
    *h = m.height;
}

struct MeasureOf {
    std::string s;
    void operator()(setupqr::NameStyle st, int* w, int* h) const { Measure(s, st, w, h); }
};

int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("usage: test_wifi_qr <repo root> <GFXFF font dir>\n"); return 2; }
    const std::string root = argv[1], fontDir = argv[2];
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

    // ---- the ladder's order is what Rank() assumes ------------------------------
    for (int i = 0; i < setupqr::NAME_STYLE_COUNT; ++i)
        check(setupqr::Rank(setupqr::NAME_STYLES[i]) == i, "NAME_STYLES is in enum order");

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
    const bool fontsOk = LoadGfx(fontDir + "/FreeSansBold12pt7b.h", gfx[setupqr::NameStyle::SansBold12]) &&
                         LoadGfx(fontDir + "/FreeSansBold9pt7b.h",  gfx[setupqr::NameStyle::SansBold9]) &&
                         LoadGfx(fontDir + "/FreeSans9pt7b.h",      gfx[setupqr::NameStyle::Sans9]);
    // CONTROL: the parse saw what it sweeps. An empty sweep passes everything.
    if (products.size() < 8 || sizes.size() < 4 || !fontsOk ||
        std::find(products.begin(), products.end(), "Blipscope") == products.end()) {
        std::printf("  BLIND: parsed %zu product names, %zu panel sizes, fonts %s (%s)\n",
                    products.size(), sizes.size(), fontsOk ? "ok" : "MISSING", fontDir.c_str());
        return 2;
    }
    std::printf("  swept %zu editions x %zu panels\n", products.size(), sizes.size());

    // ---- the sweep ------------------------------------------------------------------
    // The longest advice line the title can carry ("NETWORK NOT FOUND", 17 chars at
    // scale 1): the placement is proven against it, not against "SETUP".
    const int TITLE_W = 17 * 6, TITLE_H = 8;
    const int target = setupqr::Rank(setupqr::NAME_STYLE_240);
    int placed = 0, refused = 0, atTarget = 0, glcdOnly = 0;
    for (const std::string& p : products) {
        const std::string name = p + "-FFFFFF";                 // a widest-case suffix
        n = setupqr::WifiPayload(name.c_str(), buf, sizeof(buf));
        check(VersionFor(n) == 3, name + ": version 3 (" + std::to_string(n) + " B)");
        for (int size : sizes) {
            const std::string at = name + " @" + std::to_string(size);
            const int side = 37 * setupqr::ModulePx(size);        // version 3 + quiet zone
            check(side <= (int)(size * 0.7071f), at + ": QR inside the inscribed square");
            const setupqr::Placement pl = setupqr::Place(size, side, TITLE_W, TITLE_H, MeasureOf{name});
            check(setupqr::CornersOnGlass(size, side, pl.raise), at + ": raised QR keeps its corners on the glass");
            check(pl.titleY >= 0 && TITLE_W <= discgeom::ChordWidthPx(pl.titleY, TITLE_H, size),
                  at + ": the title fits above the QR");
            check(pl.nameY == pl.cy + side / 2 + setupqr::GAP_PX, at + ": the name sits under the QR");
            if (pl.nameRank >= 0) ++placed; else ++refused;
            if (pl.nameRank >= 0 && pl.nameRank <= target) ++atTarget;
            if (pl.nameRank >= setupqr::Rank(setupqr::NameStyle::Glcd175)) {
                ++glcdOnly;
                std::printf("  note: %s falls back to %s\n", at.c_str(),
                            setupqr::StyleName(setupqr::NAME_STYLES[pl.nameRank]));
            }
            check(pl.nameRank >= 0, at + ": the name fits under the QR");
            // The fallback screen puts the name at the centre row: it must fit there too.
            check(setupqr::PickNameRank(MeasureOf{name}, size / 2, size) >= 0,
                  at + ": the name fits on the fallback screen");
        }
    }
    std::printf("  names placed %d, refused %d; at %s or better: %d; on a 5x7 rung: %d\n",
                placed, refused, setupqr::StyleName(setupqr::NAME_STYLE_240), atTarget, glcdOnly);

    // ---- the shipping pairs: every edition that exists on the 240 panel ------------
    // Blipscope and Missileer ship on the 1.28" board; both must get a REAL font there,
    // because the 5x7 font's 'c' and 'o' differ by one pixel.
    const int side240 = 37 * setupqr::ModulePx(240);
    for (const char* p : { "Blipscope", "Missileer" }) {
        const std::string name = std::string(p) + "-FFFFFF";
        const setupqr::Placement pl = setupqr::Place(240, side240, TITLE_W, TITLE_H, MeasureOf{name});
        check(pl.nameRank >= 0 && pl.nameRank <= target, name + " @240: in " +
              setupqr::StyleName(setupqr::NAME_STYLE_240) + " or better, never the 5x7 font");
    }

    // ---- the unit on the bench -------------------------------------------------------
    // Device, before the dash padding: "raise 8, ... name 154 px (h 18), chord 155 px"
    // (predicted raise 12 / h 22 from yAdvance -- a miss, and the model was fixed).
    // With DASH_PAD_PX (dash-pad-predictions): name 156 px, raise 9, chord 157 px.
    // "Blipscope-00E000" has the bench name's widths: FreeSans digits share one advance.
    check(side240 == 148, "240 px: 148 px QR at 4 px/module");
    const std::string bench = "Blipscope-00E000";
    int dw0 = 0, dw8 = 0, dh = 0;
    Measure("0", setupqr::NameStyle::SansBold9, &dw0, &dh);
    Measure("8", setupqr::NameStyle::SansBold9, &dw8, &dh);
    check(dw0 == dw8, "CONTROL: FreeSansBold9pt7b digits share one advance (the stand-in is exact)");
    int bw = 0, bh = 0;
    Measure(bench, setupqr::NameStyle::SansBold9, &bw, &bh);
    const setupqr::Placement b = setupqr::Place(240, side240, TITLE_W, TITLE_H, MeasureOf{bench});
    const int benchChord = discgeom::ChordWidthPx(b.nameY, bh, 240);
    const int centredChord = discgeom::ChordWidthPx(120 + side240 / 2 + setupqr::GAP_PX, bh, 240);
    std::printf("  bench name: %s, %d px (h %d); centred chord %d px; raised %d px -> chord %d px\n",
                setupqr::StyleName(b.nameRank >= 0 ? setupqr::NAME_STYLES[b.nameRank] : setupqr::NameStyle::Glcd1),
                bw, bh, centredChord, b.raise, benchChord);
    check(bw == 156, "bench name is 156 px in FreeSansBold9pt7b (154 + the dash's 2)");
    check(centredChord < bw, "CONTROL: centred, it does not fit -- the reason the QR is raised");
    check(b.nameRank == setupqr::Rank(setupqr::NameStyle::SansBold9), "bench name in FreeSansBold9pt7b");
    check(bh == 18, "bench name: fontHeight 18, as the device logged");
    check(b.raise == 9, "bench name: raised 9 px");
    check(benchChord == 157, "bench name: chord 157 px");

    // The 5x7 rung still agrees with what the device measured on 2026-09-25.
    int w175 = 0, h175 = 0;
    Measure("Blipscope-A1B2C3", setupqr::NameStyle::Glcd175, &w175, &h175);
    check(w175 == 160 + setupqr::DASH_PAD_PX, "the 5x7 model agrees with the device: 160 px at 1.75, + the dash's room");

    // CONTROL: the picker can refuse. A rule that has never said no is untested.
    check(setupqr::PickNameRank(MeasureOf{std::string(40, 'W')}, b.nameY, 240) == -1,
          "CONTROL: a 40-character name reports 'nothing fits'");
    // CONTROL: the corner proof can refuse.
    check(!setupqr::CornersOnGlass(240, side240, 60), "CONTROL: a 60 px raise puts a corner off the glass");

    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
