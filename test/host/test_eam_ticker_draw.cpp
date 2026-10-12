// Host test for ROADMAP E6, the draw half: the Latest EAM screen (the ticker,
// EamManager::DrawTicker) with messages of 31, 75, 290 and 500 characters.
//
// WHAT RUNS. The REAL LovyanGFX 1.2.21 (from .pio/libdeps): LGFX_Sprite at the
// device's colour depth (16 bpp, main.cpp) and size (240 and 412 px, one band:
// BANDED_RENDER is false on every SKU), its default font and drawString/textWidth
// -- the only calls DrawTicker makes with message text. LovyanGFX has no headless
// platform on Windows, so eam_host/lgfx_platform.cpp supplies the six functions
// its framebuffer platform declares (clock, delay, GPIO no-ops); none of them is
// on the drawing path.
//
// WHAT IS TRANSCRIBED, AND WHY THAT IS PINNED. DrawTicker itself is a member of
// EamManager, which drags in the whole device (config server, touch, ntfy, USB,
// game); it cannot be compiled here. So its geometry and loop are restated below,
// and every restated line is PINNED: this test reads src/eam/EamScreens.cpp and
// requires each line verbatim inside DrawTicker. Change DrawTicker and this test
// goes BLIND (exit 2) until it is re-transcribed -- it can never silently grade
// an old copy. The groups drawn are the naive whitespace split of the text, which
// test_eam_long_message.cpp proves equal to what EamModels stores.
//
// WHAT EACH RESULT MEANS -- written 2026-10-11, before the first run:
//   * no byte outside the sprite buffer changes (guard bands, all builds) and no
//     sanitizer report (Linux builds): the draw path does not overrun with a long
//     message. A guard hit or a sanitizer report: it does -- the finding E6 exists
//     to catch.
//   * every frame of the scroll draws pixels, and every group is drawn in some
//     frame: nothing goes missing vertically. A frame with no pixels, or a group
//     never drawn, is a message part the screen can never show.
//   * the visible-character counts are REPORTED, not graded: "whatever it shows
//     today is fine". A station copy is ONE group, centred, so the prediction is
//     that only ~34 characters (1.28") / ~60 (1.46") of a long one are visible,
//     taken from its middle.
//   * none of the above (a crash, an exit the rig does not know): stop and decide.
//
// Exit: 0 pass, 1 a check failed, 2 BLIND (fixtures, or DrawTicker no longer matches).
#include <LovyanGFX.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../include/DiscGeometry.h"

static int failures = 0;

static void expect(bool ok, const std::string& what)
{
    if (!ok) { printf("  FAIL  %s\n", what.c_str()); failures++; }
    else printf("  ok    %s\n", what.c_str());
}

static bool ReadFile(const std::string& path, std::string& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

static std::string FirstString(const std::string& body, const char* key)
{
    const std::string pat = std::string("\"") + key + "\":\"";
    const size_t p = body.find(pat);
    if (p == std::string::npos) return "";
    const size_t s = p + pat.size();
    return body.substr(s, body.find('"', s) - s);
}

static std::vector<std::string> Split(const std::string& t)
{
    std::vector<std::string> g;
    size_t i = 0;
    while (i < t.size()) {
        while (i < t.size() && t[i] == ' ') i++;
        size_t j = i;
        while (j < t.size() && t[j] != ' ') j++;
        if (j > i) g.push_back(t.substr(i, j - i));
        i = j;
    }
    return g;
}

// Same spacing rule as test_eam_long_message.cpp's Spaced(): 6-character groups.
static std::string Spaced(const std::string& src, int len)
{
    std::string s;
    size_t k = 0;
    while ((int)s.size() < len) {
        const bool gap = s.size() % 7 == 6 && (int)s.size() != len - 1;
        s += gap ? ' ' : src[k++ % src.size()];
    }
    return s;
}

// ======================================================= the transcription and its pins
// Each of these must appear, verbatim after trimming, inside DrawTicker.
static const char* kPins[] = {
    "c.setTextSize(1);",
    "const int lh = c.fontHeight() + 2;",
    "CenterText(c, header, (int)(SCREEN_SIZE * 0.12), palette.dim);",
    "const int bodyTop = (int)(SCREEN_SIZE * 0.28);",
    "const int bodyBot = (int)(SCREEN_SIZE * 0.90);",
    "const std::vector<String>& groups = m.groups;",
    "const int avail = bodyBot - bodyTop;",
    "const int contentH = (int)groups.size() * lh;",
    "if (contentH > avail) {",
    "const int span = contentH - avail + lh; // scroll a little past the end before wrapping",
    "tickerScroll = (tickerScroll + 1) % (span > 0 ? span : 1);",
    "const int y = bodyTop + i * lh - offset;",
    "if (y < bodyTop - lh || y > bodyBot) continue;",
    "c.drawString(groups[i], SCREEN_SIZE_DIV_2 - c.textWidth(groups[i]) / 2, y);",
};
// ...and CenterText, in EamManager.cpp.
static const char* kCenterTextPin = "c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);";

static std::string Trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r");
    size_t e = s.find_last_not_of(" \t\r");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

static bool HasLine(const std::string& src, const char* line)
{
    size_t p = 0;
    while (p < src.size()) {
        size_t e = src.find('\n', p);
        if (e == std::string::npos) e = src.size();
        if (Trim(src.substr(p, e - p)) == line) return true;
        p = e + 1;
    }
    return false;
}

// One frame of DrawTicker's header + body at scroll `offset`, as transcribed above.
struct Frame {
    int lh, bodyTop, bodyBot, avail, contentH, span;
};

static Frame Geometry(lgfx::LGFX_Sprite& c, int S, size_t groups)
{
    Frame f;
    f.lh = c.fontHeight() + 2;
    f.bodyTop = (int)(S * 0.28);
    f.bodyBot = (int)(S * 0.90);
    f.avail = f.bodyBot - f.bodyTop;
    f.contentH = (int)groups * f.lh;
    f.span = f.contentH > f.avail ? f.contentH - f.avail + f.lh : 0;
    return f;
}

// Draws one frame; marks which groups were drawn. Returns the number of body lines drawn.
static int DrawFrame(lgfx::LGFX_Sprite& c, int S, const Frame& f, const std::string& header,
                     const std::vector<std::string>& groups, int offset, std::vector<char>& drawn)
{
    const int half = S / 2;
    c.setTextColor(0xFFFFu);
    c.drawString(header.c_str(), half - c.textWidth(header.c_str()) / 2, (int)(S * 0.12));
    int lines = 0;
    for (int i = 0; i < (int)groups.size(); ++i) {
        const int y = f.bodyTop + i * f.lh - offset;
        if (y < f.bodyTop - f.lh || y > f.bodyBot) continue;
        c.drawString(groups[i].c_str(), half - c.textWidth(groups[i].c_str()) / 2, y);
        drawn[i] = 1;
        lines++;
    }
    return lines;
}

static long LitPixels(const uint16_t* px, int S, int y0, int y1)
{
    long n = 0;
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < S; ++y)
        for (int x = 0; x < S; ++x) n += px[y * S + x] != 0;
    return n;
}

struct Result {
    int frames = 0, emptyFrames = 0, groupsNeverDrawn = 0;
    bool guardsIntact = true;
    int widest = 0, inSquare = 0, inDisc = 0, firstInDisc = -1;
};

// THE GUARD BANDS: the sprite draws into the middle of a buffer we own; the bytes
// either side are a pattern that no drawing may change. Works on every compiler;
// AddressSanitizer (where present) watches the createSprite() run as well.
static const size_t kGuard = 4096;
static const unsigned char kPattern = 0xA5;

static Result Sweep(int S, float size, const std::string& header, const std::vector<std::string>& groups,
                    bool ownBuffer, bool pokeGuard = false)
{
    Result r;
    lgfx::LGFX_Sprite c;
    std::vector<unsigned char> mem;
    uint16_t* px = nullptr;
    if (ownBuffer) {
        mem.assign((size_t)S * S * 2 + 2 * kGuard, kPattern);
        c.setBuffer(mem.data() + kGuard, S, S, 16);
        px = reinterpret_cast<uint16_t*>(mem.data() + kGuard);
    } else {
        c.setColorDepth(16);
        px = static_cast<uint16_t*>(c.createSprite(S, S));
        if (!px) { r.guardsIntact = false; return r; }
    }
    c.setTextSize(size);
    const Frame f = Geometry(c, S, groups.size());
    std::vector<char> drawn(groups.size(), 0);
    const int frames = f.span > 0 ? f.span : 1;   // tickerScroll visits every offset in [0, span)
    for (int off = 0; off < frames; ++off) {
        c.fillScreen(0);
        const int lines = DrawFrame(c, S, f, header, groups, off, drawn);
        r.frames++;
        if (lines == 0 || LitPixels(px, S, f.bodyTop - f.lh, f.bodyBot + f.lh) == 0) r.emptyFrames++;
    }
    for (char d : drawn) r.groupsNeverDrawn += !d;

    // What a reader can see of the widest group in frame 0: characters wholly inside
    // the square, and wholly inside the disc at that line's row (DiscGeometry's
    // chord, with the repo's 8 px text inset).
    size_t wi = 0;
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i].size() > groups[wi].size()) wi = i;
    const int adv = c.textWidth("W");
    const int w = c.textWidth(groups[wi].c_str());
    const int x0 = S / 2 - w / 2;
    const int y = f.bodyTop + (int)wi * f.lh;   // frame 0
    const int chord = discgeom::ChordWidthPx(y, c.fontHeight(), S);
    r.widest = (int)groups[wi].size();
    for (int k = 0; k < r.widest; ++k) {
        const int a = x0 + adv * k, b = a + adv;
        if (a >= 0 && b <= S) r.inSquare++;
        if (a >= S / 2 - chord / 2 && b <= S / 2 + chord / 2) {
            r.inDisc++;
            if (r.firstInDisc < 0) r.firstInDisc = k;
        }
    }

    if (pokeGuard && ownBuffer) mem[kGuard + (size_t)S * S * 2] ^= 0xFF;   // the control: one byte past the end
    if (ownBuffer)
        for (size_t i = 0; i < kGuard; ++i)
            if (mem[i] != kPattern || mem[kGuard + (size_t)S * S * 2 + i] != kPattern) { r.guardsIntact = false; break; }
    return r;
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: %s <repo root>\n", argv[0]); return 2; }
    const std::string root = argv[1];

    // ---------------------------------------------------------------- the pins first
    std::string screens, manager;
    if (!ReadFile(root + "/src/eam/EamScreens.cpp", screens) || !ReadFile(root + "/src/eam/EamManager.cpp", manager)) {
        printf("BLIND: cannot read src/eam/EamScreens.cpp / EamManager.cpp\n");
        return 2;
    }
    const size_t b = screens.find("void EamManager::DrawTicker(");
    const size_t e = b == std::string::npos ? b : screens.find("\nvoid EamManager::", b + 1);
    if (b == std::string::npos || e == std::string::npos) {
        printf("BLIND: DrawTicker not found in EamScreens.cpp\n");
        return 2;
    }
    const std::string ticker = screens.substr(b, e - b);
    bool pinned = true;
    for (const char* pin : kPins)
        if (!HasLine(ticker, pin)) { printf("BLIND: DrawTicker no longer has: %s\n", pin); pinned = false; }
    if (!HasLine(manager, kCenterTextPin)) { printf("BLIND: CenterText no longer reads: %s\n", kCenterTextPin); pinned = false; }
    if (!pinned) {
        printf("BLIND: the transcription below no longer matches DrawTicker -- re-transcribe, then re-pin\n");
        return 2;
    }
    printf("  ok    %u DrawTicker lines + CenterText pinned verbatim\n", (unsigned)(sizeof(kPins) / sizeof(kPins[0])));

    // -------------------------------------------------------------------- the inputs
    struct Msg { std::string name, text; };
    std::vector<Msg> msgs;
    for (int len : {31, 75, 290, 500}) {
        char name[40];
        std::snprintf(name, sizeof(name), "test/fixtures/eam-long/latest-%03d.json", len);
        std::string body;
        if (!ReadFile(root + "/" + name, body)) { printf("BLIND: %s missing\n", name); return 2; }
        const std::string t = FirstString(body, "text");
        if ((int)t.size() != len) { printf("BLIND: %s: first text is not %d chars\n", name, len); return 2; }
        msgs.push_back({std::to_string(len) + " station", t});
        msgs.push_back({std::to_string(len) + " spaced", Spaced(t, len)});
    }

    // ------------------------------------------------------------------ today's ticker
    printf("\n-- DrawTicker at text size 1 (today), every scroll offset, real LovyanGFX --\n");
    printf("    %-12s %5s | %6s %6s %6s | %s\n", "message", "panel", "frames", "groups", "widest",
           "chars of the widest line wholly visible: in the square / inside the disc");
    for (int S : {240, 412}) {
        for (const Msg& m : msgs) {
            const std::vector<std::string> g = Split(m.text);
            size_t chars = 0;
            for (const std::string& s : g) chars += s.size();
            // DrawTicker's header at its longest: frequency, age, and a PARTIAL "N+ ch".
            const std::string header = "11175 kHz  -  59m ago  -  " + std::to_string(chars) + "+ ch";
            const Result own = Sweep(S, 1.0f, header, g, true);
            const Result alloc = Sweep(S, 1.0f, header, g, false);
            printf("    %-12s %5d | %6d %6u %6d | %d / %d (from char %d)\n", m.name.c_str(), S, own.frames,
                   (unsigned)g.size(), own.widest, own.inSquare, own.inDisc, own.firstInDisc);
            expect(own.guardsIntact && alloc.guardsIntact && own.emptyFrames == 0 && own.groupsNeverDrawn == 0,
                   m.name + " @" + std::to_string(S) + ": no write outside the sprite, every frame drawn (" +
                       std::to_string(own.frames) + "), every group drawn");
        }
    }

    // --------------------------------------------------- beyond today: sizes 2 and 3
    // PR #350 moves the EAM screens to size 2 (3 on the 412 px panel). Not today's
    // code, so only the overrun question is asked: does the library stay in bounds.
    printf("\n-- the same, text size 2 and 3 (not today's ticker; overrun check only) --\n");
    for (int S : {240, 412})
        for (float size : {2.0f, 3.0f}) {
            const Msg& m = msgs[6];   // 500 station
            const Result r = Sweep(S, size, "500 ch", Split(m.text), true);
            expect(r.guardsIntact && r.emptyFrames == 0,
                   "500 station @" + std::to_string(S) + " size " + std::to_string((int)size) + ": in bounds, " +
                       std::to_string(r.inDisc) + " chars inside the disc");
        }

    // --------------------------------------------- the library's own bound on width
    // LGFXBase::draw_string accumulates the pen in an int16_t (sumX), and returns it.
    // Past 32,767 px the pen wraps. Prove the bound is real in THIS build, then show
    // the longest copy the backend allows is inside it at the largest size used.
    {
        lgfx::LGFX_Sprite c;
        c.setColorDepth(16);
        c.createSprite(240, 240);
        c.setTextSize(1);
        const std::string over(6000, 'X');                 // 36,000 px at 6 px a glyph
        const std::string cap(500, 'X');                   // valar-eam-feed MAX_COPY
        const int wOver = c.textWidth(over.c_str());
        const int dOver = (int)(int16_t)c.drawString(over.c_str(), 0, 100);
        expect(wOver > 32767 && dOver != wOver,
               "control: a 6000-char line overflows draw_string's int16 pen (textWidth " + std::to_string(wOver) +
                   ", pen " + std::to_string(dOver) + ")");
        c.setTextSize(3);
        const int wCap = c.textWidth(cap.c_str());
        const int dCap = (int)c.drawString(cap.c_str(), 0, 100);
        expect(wCap <= 32767 && dCap == wCap,
               "500 chars at size 3 = " + std::to_string(wCap) + " px: inside the int16 pen (it wraps past " +
                   std::to_string(32767 / (wCap / 500)) + " chars at size 3)");
    }

    // ------------------------------------------------------------------ the control
    {
        const Result r = Sweep(240, 1.0f, "x", Split(msgs[6].text), true, true);
        expect(!r.guardsIntact, "control: one byte changed past the sprite IS reported");
    }

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
