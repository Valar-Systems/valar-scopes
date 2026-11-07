// The touch-wedge cap's DRIVER, shared by the radar and Weatherscope: include/TouchWedgeCap.h, include/SaveLock.h
// and src/weatherscope/WxTouchStrip.h. Plan and predictions: docs/weatherscope-stage3.md A12.119-A12.121.
// The policy underneath (TouchWedgePolicy.h) keeps its own test, test_touch_wedge.cpp, unchanged.
//
// SABOTAGES (test/host/sabotage_touch_wedge_cap.py; each set frozen in A12.119/A12.120):
//   S-CR1 the cap removed (the rung ignores OnRung's refusal AND RebootDue drops !unavailable) -> {CR1, CR2, CR4, CR28}
//   S-CR2 the idle rule removed (RebootDue's idle term true)                                    -> {CR5, CR6}
//   S-CR3 the screen not drawn (Cap::Draw no longer calls DrawStrip)                            -> {CR18, CR19, CR20}
//   S-CR4 the hold ignored (the hold check returns true)                                        -> {CR9, CR10, CR11}
//   S-CR5 the refused save's message dropped (sent empty)                                       -> {CR30, CR31}
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "TouchWedgeCap.h"
#include "SaveLock.h"
#include "WxTouchStrip.h"

namespace {

int checks = 0, failures = 0;
void check(const char* id, bool ok, const char* what)
{
    std::printf("  %s  %s: %s\n", ok ? "ok  " : "FAIL", id, what);
    ++checks;
    if (!ok) ++failures;
}

// A device the driver can run on: NVS survives simulated boots; every side effect is recorded.
struct FakeIo : touchwedge::Io {
    uint8_t nvs = 0;
    int writes = 0, stamps = 0, restarts = 0;
    bool flash = false;
    savelock::Gate gate;
    std::string ops;                  // "W1 S R " -- the side effects, in order
    std::vector<std::string> lines;   // every Print, verbatim
    uint8_t ReadRun() override { return nvs; }
    bool WriteRun(uint8_t run) override
    {
        nvs = run;
        ++writes;
        ops += "W" + std::to_string((unsigned)run) + " ";
        return true;
    }
    void StampCause() override { ++stamps; ops += "S "; }
    void Restart() override { ++restarts; ops += "R "; }
    bool FlashWriteActive() override { return flash; }
    bool TryHoldSave() override { return savelock::TryHoldForRestart(gate); }
    void Print(const char* text) override { lines.push_back(text); }
};

// A canvas that records what was drawn, with the s3-128's 6 x 8 font at size 1.
struct Op { char kind; int x, y, w, h; uint32_t color; std::string text; };
struct RecCanvas {
    std::vector<Op> ops;
    uint32_t ink = 0;
    int size = 1;
    void setTextSize(int s) { size = s; }
    int fontHeight() const { return 8 * size; }
    int textWidth(const char* s) const { return 6 * size * (int)std::strlen(s); }
    void fillRect(int x, int y, int w, int h, uint32_t c) { ops.push_back(Op{'F', x, y, w, h, c, ""}); }
    void drawRect(int x, int y, int w, int h, uint32_t c) { ops.push_back(Op{'R', x, y, w, h, c, ""}); }
    void setTextColor(uint32_t c) { ink = c; }
    void drawString(const char* s, int x, int y) { ops.push_back(Op{'S', x, y, 0, 0, ink, s}); }
};

struct FakeRequest {
    int status = 0, sends = 0;
    std::string type, body;
    void send(int s, const char* t, const char* b)
    {
        status = s;
        type = t;
        body = b;
        ++sends;
    }
};

// WCAG 2 with WCAG's own constants, and the panel's RGB565 round trip -- as test_wx_theme.cpp computes them.
double Lin(uint8_t v)
{
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double RelLum(wxtheme::Rgb c) { return 0.2126 * Lin(c.r) + 0.7152 * Lin(c.g) + 0.0722 * Lin(c.b); }
double Wcag(wxtheme::Rgb a, wxtheme::Rgb b)
{
    const double x = RelLum(a), y = RelLum(b);
    return ((x > y ? x : y) + 0.05) / ((x > y ? y : x) + 0.05);
}
wxtheme::Rgb P565(wxtheme::Rgb c)
{
    const int r = c.r >> 3, g = c.g >> 2, b = c.b >> 3;
    return wxtheme::Rgb{(uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)), (uint8_t)((b << 3) | (b >> 2))};
}
wxtheme::Rgb Unpack(uint32_t v) { return wxtheme::Rgb{(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; }

const uint32_t IDLE = touchwedge::REBOOT_IDLE_MS, HEALTHY = touchwedge::HEALTHY_RESET_MS;
const char* ID = "0000000000000000";   // EXAMPLE -- not a real device

} // namespace

int main()
{
    using namespace touchwedge;
    std::printf("touch-wedge cap driver (TouchWedgeCap.h, SaveLock.h, WxTouchStrip.h)\n");

    // ---- the chain, across simulated soft boots, as shipped (literal 3) -----------------------------------------------
    {
        FakeIo io;
        int restarts[6], unavailableAtBoot[6];
        for (int boot = 0; boot < 6; ++boot) {
            Cap cap;
            cap.Boot(io, /*powerOn=*/false);
            unavailableAtBoot[boot] = cap.St().unavailable;
            const int before = io.restarts;
            cap.Tick(io, IDLE, /*wedged=*/true, /*lastActivityMs=*/0, IDLE, HEALTHY);
            restarts[boot] = io.restarts - before;
        }
        std::printf("  chain: %d restarts, stored run %u, ops \"%s\"\n", io.restarts, (unsigned)io.nvs, io.ops.c_str());
        check("CR1", io.restarts == 3 && io.nvs == 3, "the loop test: across 6 soft boots, exactly 3 restarts; stored run 3");
        bool held = true;
        for (int b = 3; b < 6; ++b) if (!unavailableAtBoot[b] || restarts[b] != 0) held = false;
        check("CR2", held && unavailableAtBoot[3], "the 4th boot is unavailable; its due rung restarts nothing");
        check("CR4", io.ops == "W1 S R W2 S R W3 S R ", "the ops are exactly W1 S R, W2 S R, W3 S R, and nothing else");
        // CR28: the whole transcript, compared whole -- the radar's own format strings.
        const char* boot0 = "[touch-wd] boot: run=0 (stored 0, reset not power-on)\n";
        std::vector<std::string> want = { boot0 };
        for (unsigned k = 1; k <= 3; ++k) {
            char a[160], b[160], c[160];
            std::snprintf(a, sizeof a, "[touch-wd] wedged past the outage bound and idle: touch reboot %u of %u\n", k, 3u);
            std::snprintf(b, sizeof b, "[touch-wd] run %u -> %u (NVS write: touch reboot)\n", k - 1, k);
            std::snprintf(c, sizeof c, "[touch-wd] boot: run=%u (stored %u, reset not power-on)%s\n", k, k,
                          k == 3 ? " -- touch unavailable, no touch reboots this boot" : "");
            want.push_back(a);
            want.push_back(b);
            want.push_back("[touch-wd] restarting (cause SW_TOUCHWD stamped)\r\n");
            want.push_back(c);
        }
        want.push_back("[touch-wd] boot: run=3 (stored 3, reset not power-on) -- touch unavailable, no touch reboots this boot\n");
        want.push_back("[touch-wd] boot: run=3 (stored 3, reset not power-on) -- touch unavailable, no touch reboots this boot\n");
        check("CR28", io.lines == want, "the chain's lines are byte-equal to the radar's formats, whole");
    }
    {
        FakeIo io;
        io.nvs = 2;
        Cap cap;
        cap.Boot(io, false);
        cap.Tick(io, IDLE, true, 0, IDLE, HEALTHY);
        check("CR3", io.restarts == 1 && io.nvs == 3, "CONTROL: a boot at stored run 2 still restarts when due");
    }

    // ---- the idle rule -------------------------------------------------------------------------------------------------
    {
        const uint32_t now = 5000000;
        FakeIo a, b, c, d;
        Cap ca, cb, cc, cd;
        ca.Boot(a, false); cb.Boot(b, false); cc.Boot(c, false); cd.Boot(d, false);
        ca.Tick(a, now, true, now, IDLE, HEALTHY);
        check("CR5", a.restarts == 0, "wedged, last touch 0 ms ago: no restart");
        cb.Tick(b, now, true, now - (IDLE - 1), IDLE, HEALTHY);
        check("CR6", b.restarts == 0, "wedged, last touch idle - 1 ms ago: no restart");
        cc.Tick(c, now, true, now - IDLE, IDLE, HEALTHY);
        check("CR7", c.restarts == 1, "CONTROL: exactly idle ms ago: restart");
        cd.Tick(d, HEALTHY, false, 0, IDLE, HEALTHY);
        check("CR8", d.restarts == 0, "CONTROL: not wedged, idle for an hour: no restart");
    }

    // ---- the hold: a settings save, a flash write ---------------------------------------------------------------------
    {
        FakeIo io;
        Cap cap;
        cap.Boot(io, false);
        savelock::EnterSave(io.gate);   // a /save is writing
        cap.Tick(io, IDLE, true, 0, IDLE, HEALTHY);
        check("CR9", io.writes == 0 && io.stamps == 0 && io.restarts == 0 && !io.gate.heldForRestart,
              "a save holds the lock: no write, no stamp, no restart");
        savelock::ExitSave(io.gate);
        cap.Tick(io, IDLE + 50, true, 0, IDLE, HEALTHY);
        check("CR10", io.restarts == 1 && io.writes == 1, "across the held tick and the next (released) one: 1 restart, 1 write");
    }
    {
        FakeIo io;
        io.flash = true;
        Cap cap;
        cap.Boot(io, false);
        cap.Tick(io, IDLE, true, 0, IDLE, HEALTHY);
        check("CR11", io.restarts == 0 && !io.gate.heldForRestart,
              "a flash write is active: no restart, and the save lock is not taken");
    }

    // ---- the resets ----------------------------------------------------------------------------------------------------
    {
        FakeIo io;
        io.nvs = 2;
        Cap cap;
        cap.Boot(io, false);
        cap.OnRealTouch(io);
        check("CR12", io.nvs == 0 && io.writes == 1 && cap.St().run == 0 && !cap.Shown(),
              "a real touch at run 2: run 0 written, the strip off");
    }
    {
        FakeIo io;
        Cap cap;
        cap.Boot(io, false);
        cap.OnRealTouch(io);
        check("CR13", io.writes == 0, "a real touch at run 0: no write");
    }
    {
        FakeIo io;
        io.nvs = CAP;
        Cap cap;
        cap.Boot(io, /*powerOn=*/true);
        check("CR14", cap.St().run == 0 && io.nvs == 0 && io.writes == 1 && !cap.Shown(),
              "a power-on boot at stored CAP: run 0, one write of 0");
    }
    {
        FakeIo io;
        io.nvs = CAP;
        Cap cap;
        cap.Boot(io, false);
        check("CR15", cap.St().unavailable && cap.Shown() && io.writes == 0, "CONTROL: a soft boot at stored CAP: unavailable, no write");
    }
    {
        FakeIo io;
        io.nvs = CAP;
        Cap cap;
        cap.Boot(io, false);
        const bool before = cap.Shown();
        cap.Tick(io, HEALTHY, /*wedged=*/false, 0, IDLE, HEALTHY);
        check("CR16", before && !cap.Shown() && io.nvs == 0 && io.writes == 1,
              "an hour wedge-free at stored CAP: shown before, cleared after, 0 written once");
    }
    {
        FakeIo io;
        io.nvs = CAP;
        Cap cap;
        cap.Boot(io, false);
        cap.Tick(io, 1000, /*wedged=*/true, 0, IDLE, HEALTHY);
        cap.Tick(io, 2 * HEALTHY, false, 0, IDLE, HEALTHY);
        check("CR17", cap.Shown(), "CONTROL: the wedge was seen this boot: still shown at 2 hours");
    }

    // ---- the strip -----------------------------------------------------------------------------------------------------
    {
        const StripInk ink = { 0x010101UL, 0x020202UL, 0x030303UL, 0x040404UL };   // four distinguishable inks
        FakeIo io;
        io.nvs = CAP;
        Cap cap;
        cap.Boot(io, false);
        RecCanvas c;
        cap.Draw(c, ID, ink, 240);
        const Strip st = StripFor(ID);
        const bool five = c.ops.size() == 5 && c.ops[0].kind == 'F' && c.ops[1].kind == 'R' && c.ops[2].kind == 'S' &&
                          c.ops[3].kind == 'S' && c.ops[4].kind == 'S';
        check("CR18", five && c.ops[2].text == st.line1 && c.ops[3].text == st.line2 && c.ops[4].text == st.line3,
              "an unavailable cap draws one box and the three StripFor lines, in order");
        check("CR19", five && c.ops[0].color == ink.ground && c.ops[1].color == ink.border && c.ops[2].color == ink.headline &&
                          c.ops[3].color == ink.body && c.ops[4].color == ink.body,
              "the headline in ink.headline, the other lines in ink.body, the border in ink.border, the ground in ink.ground");
        bool box = five && c.ops[1].x == 32 && c.ops[1].y == 42 && c.ops[1].w == 174 && c.ops[1].h == 36;
        if (box) {   // an independent test of the glass: both top corners inside the r = 120 disc
            const double cx = 119.5, r = 120.0, dy = 42 - cx, l = 32 - cx, rr = 32 + 174 - 1 - cx;
            box = l * l + dy * dy <= r * r && rr * rr + dy * dy <= r * r;
        }
        if (five) std::printf("  strip box x=%d y=%d w=%d h=%d\n", c.ops[1].x, c.ops[1].y, c.ops[1].w, c.ops[1].h);
        check("CR20", box, "the box is 174 x 36 at y 42, its top corners on the glass");

        FakeIo io2;
        Cap fine;
        fine.Boot(io2, false);
        RecCanvas c2;
        fine.Draw(c2, ID, ink, 240);
        check("CR21", c2.ops.empty(), "CONTROL: an available cap draws nothing");

        RecCanvas c3;
        DrawStrip(c3, st, RADAR_INK, 240);
        check("CR22", c3.ops.size() == 5 && c3.ops[0].color == 0x000000UL && c3.ops[1].color == 0xFFB000UL &&
                          c3.ops[2].color == 0xFFB000UL && c3.ops[3].color == 0xFFFFFFUL && c3.ops[4].color == 0xFFFFFFUL,
              "the radar's ink gives the radar's colours: (0, 0, 0), (255, 176, 0) twice, (255, 255, 255)");
    }

    // ---- Weatherscope's ink: the theme's roles, at the text rule's contrast --------------------------------------------
    {
        struct Row { const char* id; const char* name; uint32_t ink, ground; double frozen565; };
        const StripInk dk = wxtheme::StripInkFor(wxtheme::kDark), lt = wxtheme::StripInkFor(wxtheme::kLight);
        const Row rows[4] = {
            {"CR23", "dark warn on bg", dk.headline, dk.ground, 11.6522},
            {"CR24", "dark fg on bg", dk.body, dk.ground, 17.9908},
            {"CR25", "light warn on bg", lt.headline, lt.ground, 4.6607},
            {"CR26", "light fg on bg", lt.body, lt.ground, 12.7167},
        };
        for (const Row& r : rows) {
            const double v888 = Wcag(Unpack(r.ink), Unpack(r.ground)), v565 = Wcag(P565(Unpack(r.ink)), P565(Unpack(r.ground)));
            char what[160];
            std::snprintf(what, sizeof what, "%s >= 4.5: RGB888 %.4f, RGB565 %.4f (frozen %.4f +- 0.0005)", r.name, v888, v565,
                          r.frozen565);
            check(r.id, v888 >= 4.5 && v565 >= 4.5 && std::fabs(v565 - r.frozen565) <= 0.0005, what);
        }
        using wxtheme::Pack888;
        auto roles = [&](const StripInk& k, const wxtheme::Theme& t) {
            return k.ground == Pack888(t.bg) && k.border == Pack888(t.uiWarn) && k.headline == Pack888(t.uiWarn) &&
                   k.body == Pack888(t.uiFg);
        };
        check("CR27", roles(dk, wxtheme::kDark) && roles(lt, wxtheme::kLight), "the ink is {bg, uiWarn, uiWarn, uiFg} of kDark and of kLight");
    }

    // ---- the save gate and the refusal ---------------------------------------------------------------------------------
    {
        savelock::Gate g;
        FakeRequest a, b, j;
        bool holdDuring = true, admittedA = false, admittedB = true;
        {
            savelock::SaveScope<FakeRequest> s(&a, savelock::Shape::Text, g);
            admittedA = s.Admitted();
            holdDuring = savelock::TryHoldForRestart(g);
        }
        const bool holdAfter = savelock::TryHoldForRestart(g);
        {
            savelock::SaveScope<FakeRequest> s(&b, savelock::Shape::Text, g);
            admittedB = s.Admitted();
        }
        {
            savelock::SaveScope<FakeRequest> s(&j, savelock::Shape::Json, g);
        }
        check("CR29", admittedA && !holdDuring && holdAfter && !admittedB && g.heldForRestart && !savelock::EnterSave(g),
              "the gate: a save refuses the restart hold; after it the hold is taken; then saves are refused, for good");
        std::printf("  refused /save: %d %s \"%s\"\n", b.status, b.type.c_str(), b.body.c_str());
        check("CR30", b.sends == 1 && b.status == 503 && b.type == "text/plain" &&
                          b.body == "NOT SAVED - the scope is restarting. Try again in a minute.",
              "a refused /save answers 503 text/plain with exactly the sentence");
        check("CR31", j.sends == 1 && j.status == 503 && j.type == "application/json" &&
                          j.body == "{\"ok\":false,\"error\":\"NOT SAVED - the scope is restarting. Try again in a minute.\"}",
              "a refused /location answers 503 application/json with exactly {\"ok\":false,\"error\":<the sentence>}");

        savelock::Gate g2;
        FakeRequest r;
        uint8_t inside = 0;
        {
            savelock::SaveScope<FakeRequest> s(&r, savelock::Shape::Text, g2);
            inside = g2.saving;
        }
        check("CR32", r.sends == 0 && inside == 1 && g2.saving == 0,
              "CONTROL: an admitted save sends nothing itself, and holds the gate until its scope ends");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
