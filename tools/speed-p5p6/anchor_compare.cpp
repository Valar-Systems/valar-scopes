// Board addendum 2's device anchor, the host tool (docs/speed-p5p6.md section 7, on weatherscope/speed-p5p6).
//
//   anchor_compare --selftest        the compare core's rows (A-*); exit 0 every row ok, 1 a row failed
//   anchor_compare --selftest-file   the file mode's verdict rows (F-*); exit 0 every row ok, 1 a row failed
//   anchor_compare --emit-synthetic <dir>   writes bg.fb, 8copy.fb, 8copy-flip.fb, lines.txt (F-PASS's inputs)
//   anchor_compare <bg.fb> <8copy.fb> <anchor-lines> <S_bg> <S_8copy>
//                                    section 7d's file mode: exit 0 PASS, 1 FAIL, 2 VOID (anchor_file.h)
//   anchor_compare --selftest-box    section 7g's excluded boxes (X-*); exit 0 every row ok, 1 a row failed
//   anchor_compare --equal <g1.fb> <g2.fb> <g3.fb>
//                                    a scene's byte-equality OUTSIDE the excluded boxes (7g): exit 0 equal, 1 not,
//                                    2 a grab of the wrong length
//   anchor_compare --selftest-ab     section 10's marker A/B rows (B-*); exit 0 every row ok, 1 a row failed
//   anchor_compare --ab <M.fb> <P.fb> <M-lines> <P-lines> <S>
//                                    section 10's marker A/B (anchorfile::JudgeAB): exit 0 no px differs outside the
//                                    boxes, 1 differences only inside the region, 2 any outside it, 3 VOID
//   anchor_compare --selftest-pair   15c (3)'s rows (PR-*); exit 0 every row ok, 1 a row failed
//   anchor_compare --pair <a.fb> <b.fb>
//                                    15c (3): two grabs equal outside the boxes: exit 0 equal, 1 not, 2 a bad grab
//
// Build (from the repo root): g++ -std=c++11 -Wall -Wextra -Werror -O1 -Iinclude -Itest/host
//                             tools/speed-p5p6/anchor_compare.cpp -o anchor_compare
//
// Rows (each prints "ok" or "FAIL" and its id; tools/speed-p5p6/sabotage_anchor.py reads the ids):
//   A-565        LovyanGFX's rgb888 -> rgb565 truncation, pinned on four colours
//   A-BE         a /diag/fb body decodes big-endian; a body of the wrong length is refused
//   A-SCENE      CONTROL: the self-test's contacts include an overlapping pair and a marker clipped at x = 0
//   A-SELF       the port composited onto a synthetic background, against the same contacts drawn on that background
//                by a separate route, reads mismatch 0 over a non-empty region
//   A-CMP        one footprint pixel changed in the "device" frame reads mismatch 1
//   A-SHIFT      one host contact moved 1 px reads mismatch > 0
//   A-REGION     a pixel at distance exactly 3 from every footprint pixel is NOT compared (changing it reads 0)
//   A-REGION-IN  a pixel at distance exactly 2 IS compared (changing it reads 1)
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "anchor_core.h"
#include "anchor_file.h"

namespace {

using anchor::Contact;
using anchor::Frame;
using anchor::Img;
using anchor::W;

int failures = 0;
void Row(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s  %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

std::string Num(long v) { return std::to_string(v); }

// Never 0 and never in Img::Background()'s range (1..997), so a repaint shows on every pixel it touches.
std::unique_ptr<Img> SyntheticBg()
{
    std::unique_ptr<Img> b = anchor::NewImg();
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x) b->p[y * W + x] = (uint16_t)(0x8000 | ((x * 31 + y * 17) % 4093));
    return b;
}

Frame Scene()
{
    Frame f;
    f.up = 0;
    f.zoomed = false;
    f.k.push_back(Contact{4, 30.0f, 100, 100, false, anchor::To565(0x00FF00)});    // an A3 dart
    f.k.push_back(Contact{4, 200.0f, 104, 102, false, anchor::To565(0x00FFFF)});   // overlapping it
    f.k.push_back(Contact{0, 0.0f, 160, 60, true, anchor::To565(0xFFFFFF)});       // a dot
    f.k.push_back(Contact{4, 90.0f, 1, 120, false, anchor::To565(0x00FF00)});      // clipped by the left edge
    return f;
}

// The "device": bg, then each contact drawn by OldContact directly -- not through anchor::Composite.
std::unique_ptr<Img> DeviceFrame(const Img& bg, const Frame& f)
{
    std::unique_ptr<Img> d = anchor::NewImg();
    for (int i = 0; i < W * W; ++i) d->p[i] = bg.p[i];
    p5port::Canvas c(*d, 0, 0, W - 1, W - 1);
    for (const Contact& k : f.k) p5port::OldContact(c, f, k);
    return d;
}

std::unique_ptr<Img> Copy(const Img& s)
{
    std::unique_ptr<Img> d = anchor::NewImg();
    for (int i = 0; i < W * W; ++i) d->p[i] = s.p[i];
    return d;
}

// The first pixel in `in` and not in `out` (-1 if none).
int FirstIn(const std::vector<uint8_t>& in, const std::vector<uint8_t>& out)
{
    for (int i = 0; i < W * W; ++i)
        if (in[i] && !out[i]) return i;
    return -1;
}

int SelfTest()
{
    // A-565
    {
        const bool ok = anchor::To565(0x00FF00) == 0x07E0 && anchor::To565(0xFFFFFF) == 0xFFFF &&
                        anchor::To565(0x5A5A5A) == 0x5ACB && anchor::To565(0) == 0;
        Row(ok, "A-565", "rgb888 -> rgb565 truncates: 00FF00 07E0, FFFFFF FFFF, 5A5A5A 5ACB, 0 0");
    }
    // A-BE
    {
        std::vector<uint8_t> body((size_t)W * W * 2, 0);
        body[0] = 0x07;
        body[1] = 0xE0;
        std::unique_ptr<Img> img = anchor::NewImg();
        const bool dec = anchor::Decode(body, *img);
        std::vector<uint8_t> shortBody(body.begin(), body.end() - 1);
        std::unique_ptr<Img> img2 = anchor::NewImg();
        const bool refused = !anchor::Decode(shortBody, *img2);
        Row(dec && img->p[0] == 0x07E0 && refused, "A-BE", "bytes 07 E0 decode to 07E0; a body 1 byte short is refused");
    }

    const Frame s = Scene();
    std::unique_ptr<Img> bg = SyntheticBg();
    std::unique_ptr<Img> dev = DeviceFrame(*bg, s);
    const std::vector<uint8_t> t = anchor::Footprint(s);
    const std::vector<uint8_t> d1 = anchor::Dilate(t, 1), d2 = anchor::Dilate(t, 2), d3 = anchor::Dilate(t, 3);

    // A-SCENE (control)
    {
        Frame a, b;
        a.k.push_back(s.k[0]);
        b.k.push_back(s.k[1]);
        const std::vector<uint8_t> ta = anchor::Footprint(a), tb = anchor::Footprint(b);
        long both = 0, col0 = 0;
        for (int i = 0; i < W * W; ++i) both += ta[i] && tb[i];
        for (int y = 0; y < W; ++y) col0 += t[y * W];
        Row(both > 0 && col0 > 0, "A-SCENE",
            "CONTROL: contacts 0 and 1 share " + Num(both) + " footprint px; " + Num(col0) + " footprint px at x = 0");
    }
    // A-SELF
    {
        const anchor::Result r = anchor::Anchor(*bg, *dev, s);
        Row(r.mismatch == 0 && r.region > 0, "A-SELF",
            "mismatch " + Num(r.mismatch) + " (want 0) over region " + Num(r.region) + " (want > 0)");
    }
    // A-CMP
    {
        const int i0 = FirstIn(t, std::vector<uint8_t>((size_t)W * W, 0));
        std::unique_ptr<Img> d = Copy(*dev);
        if (i0 >= 0) d->p[i0] ^= 0x0001;
        const anchor::Result r = anchor::Anchor(*bg, *d, s);
        Row(i0 >= 0 && r.mismatch == 1, "A-CMP", "one footprint px changed: mismatch " + Num(r.mismatch) + " (want 1)");
    }
    // A-SHIFT
    {
        Frame moved = s;
        moved.k[0].x += 1;
        const anchor::Result r = anchor::Anchor(*bg, *dev, moved);
        Row(r.mismatch > 0, "A-SHIFT", "contact 0 moved 1 px on the host: mismatch " + Num(r.mismatch) + " (want > 0)");
    }
    // A-REGION
    {
        const int i3 = FirstIn(d3, d2);
        std::unique_ptr<Img> d = Copy(*dev);
        if (i3 >= 0) d->p[i3] ^= 0x0001;
        const anchor::Result r = anchor::Anchor(*bg, *d, s);
        Row(i3 >= 0 && r.mismatch == 0, "A-REGION",
            (i3 >= 0 ? "a px at distance 3 changed: mismatch " + Num(r.mismatch) + " (want 0)"
                     : std::string("no px at distance exactly 3 exists")));
    }
    // A-REGION-IN
    {
        const int i2 = FirstIn(d2, d1);
        std::unique_ptr<Img> d = Copy(*dev);
        if (i2 >= 0) d->p[i2] ^= 0x0001;
        const anchor::Result r = anchor::Anchor(*bg, *d, s);
        Row(i2 >= 0 && r.mismatch == 1, "A-REGION-IN",
            (i2 >= 0 ? "a px at distance 2 changed: mismatch " + Num(r.mismatch) + " (want 1)"
                     : std::string("no px at distance exactly 2 exists")));
    }
    std::printf("anchor_compare selftest: %d FAIL\n", failures);
    return failures ? 1 : 0;
}

// ---- the file mode (section 7d) ----

using anchorfile::Line;
using anchorfile::Verdict;
constexpr int kSBg = 2, kS8 = 3;   // B4''s scene indices (section 7's list)

std::vector<uint8_t> Body(const Img& img)
{
    std::vector<uint8_t> b((size_t)W * W * 2);
    for (int i = 0; i < W * W; ++i) {
        b[2 * i] = (uint8_t)(img.p[i] >> 8);
        b[2 * i + 1] = (uint8_t)(img.p[i] & 0xFF);
    }
    return b;
}

std::vector<Line> LinesOf(const Frame& f)
{
    std::vector<Line> v;
    for (size_t i = 0; i < f.k.size(); ++i) {
        const Contact& k = f.k[i];
        v.push_back(Line{(int)i, k.x, k.y, (int)std::lround(k.track * 10.0f), k.cat, k.dot ? 1 : 0, f.up,
                         (unsigned)k.color, 0u});
    }
    return v;
}

// One print as the device writes it, behind a seal-style timestamp; count < 0 means "the number of lines".
std::string Render(int s, int pass, const std::vector<Line>& k, int count = -1)
{
    char buf[200];
    std::string out;
    std::snprintf(buf, sizeof buf, "2026-11-17T00:%02d:00Z [speed-p5] anchor S%d p%d contacts %d\n", 10 + 2 * s + pass,
                  s, pass, count < 0 ? (int)k.size() : count);
    out += buf;
    for (const Line& l : k) {
        std::snprintf(buf, sizeof buf,
                      "2026-11-17T00:%02d:01Z [speed-p5] anchor S%d p%d c%d x %d y %d trk %d cat %d dot %d up %d "
                      "col %04x ovl %03x\n",
                      10 + 2 * s + pass, s, pass, l.c, l.x, l.y, l.trk, l.cat, l.dot, l.up, l.col, l.ovl);
        out += buf;
    }
    return out;
}

// The other lines a sealed trace carries, which the file mode must ignore.
const char* const kNoise = "2026-11-17T00:00:00Z [speed-p5] scratch bytes 128 mismatches 0\n"
                           "2026-11-17T00:00:01Z [health] heap free=1 largest=1 free8=1 psram_free=1 tlsOk=1\n"
                           "2026-11-17T00:00:02Z [speed-a2] heap margin S5 p0 50048\n"
                           "2026-11-17T00:20:00Z [speed-p5] clear S4 frames 100 mismatched-px 0\n";

std::string Text(const std::vector<Line>& b0, const std::vector<Line>& b1, const std::vector<Line>& k0,
                 const std::vector<Line>& k1)
{
    return std::string(kNoise) + Render(kSBg, 0, b0) + Render(kSBg, 1, b1) + Render(kS8, 0, k0) + Render(kS8, 1, k1);
}

void FRow(const char* id, const Verdict& v, int wantCode, const std::string& part)
{
    const bool ok = v.code == wantCode && v.reason.find(part) != std::string::npos;
    Row(ok, id, "exit " + std::to_string(v.code) + " (want " + std::to_string(wantCode) + "), '" + v.reason +
                    "' (want '" + part + "')");
}

int SelfTestFile()
{
    const Frame s = Scene();
    std::unique_ptr<Img> bg = SyntheticBg();
    std::unique_ptr<Img> dev = DeviceFrame(*bg, s);
    const std::vector<uint8_t> bgB = Body(*bg), devB = Body(*dev);
    const std::vector<Line> k = LinesOf(s), none;
    // F-PASS (with the noise lines), F-FAIL
    {
        const Verdict v = anchorfile::Judge(bgB, devB, Text(none, none, k, k), kSBg, kS8);
        Row(v.code == 0 && v.reason == "PASS" && v.mismatch == 0 && v.pairs >= 1 && v.clipped >= 1, "F-PASS",
            "exit " + std::to_string(v.code) + " '" + v.reason + "' mismatch " + std::to_string(v.mismatch) +
                " pairs " + std::to_string(v.pairs) + " clipped " + std::to_string(v.clipped) + " region " +
                std::to_string(v.region) + " (want exit 0, PASS, 0, >= 1, >= 1)");
        const std::vector<uint8_t> t = anchor::Footprint(s);
        std::unique_ptr<Img> d = Copy(*dev);
        d->p[FirstIn(t, std::vector<uint8_t>((size_t)W * W, 0))] ^= 0x0001;
        const Verdict f = anchorfile::Judge(bgB, Body(*d), Text(none, none, k, k), kSBg, kS8);
        Row(f.code == 1 && f.mismatch == 1, "F-FAIL",
            "one footprint px changed: exit " + std::to_string(f.code) + " mismatch " + std::to_string(f.mismatch) +
                " (want exit 1, mismatch 1)");
    }
    // F-STILL: p1 differs from p0 in one contact's x
    {
        std::vector<Line> k1 = k;
        k1[0].x += 1;
        FRow("F-STILL", anchorfile::Judge(bgB, devB, Text(none, none, k, k1), kSBg, kS8), 2, "A-STILL");
    }
    // F-OVL, F-TRK, F-UP: one contact, in both prints
    {
        std::vector<Line> kk = k;
        kk[1].ovl = 0x001;
        FRow("F-OVL", anchorfile::Judge(bgB, devB, Text(none, none, kk, kk), kSBg, kS8), 2, "ovl");
        kk = k;
        kk[0].trk = 305;
        FRow("F-TRK", anchorfile::Judge(bgB, devB, Text(none, none, kk, kk), kSBg, kS8), 2, "trk");
        kk = k;
        kk[0].up = 90;
        FRow("F-UP", anchorfile::Judge(bgB, devB, Text(none, none, kk, kk), kSBg, kS8), 2, "up");
    }
    // F-BG: the BG scene carries a contact; F-EMPTY: the 8COPY scene none
    {
        std::vector<Line> one(1, k[2]);
        one[0].c = 0;   // a one-contact print numbers it c0, or the order check would refuse it first
        FRow("F-BG", anchorfile::Judge(bgB, devB, Text(one, one, k, k), kSBg, kS8), 2, "BG");
        FRow("F-EMPTY", anchorfile::Judge(bgB, devB, Text(none, none, none, none), kSBg, kS8), 2, "8COPY empty");
    }
    // F-NOPAIR (the dot and the clipped dart only), F-NOCLIP (the pair and the dot only)
    {
        Frame np = s, nc = s;
        np.k = std::vector<Contact>{s.k[2], s.k[3]};
        nc.k = std::vector<Contact>{s.k[0], s.k[1], s.k[2]};
        const std::vector<Line> lp = LinesOf(np), lc = LinesOf(nc);
        FRow("F-NOPAIR", anchorfile::Judge(bgB, Body(*DeviceFrame(*bg, np)), Text(none, none, lp, lp), kSBg, kS8), 2,
             "no overlapping pair");
        FRow("F-NOCLIP", anchorfile::Judge(bgB, Body(*DeviceFrame(*bg, nc)), Text(none, none, lc, lc), kSBg, kS8), 2,
             "no edge-clipped");
    }
    // F-GRAB: a BG grab one byte short
    {
        std::vector<uint8_t> shortB(bgB.begin(), bgB.end() - 1);
        FRow("F-GRAB", anchorfile::Judge(shortB, devB, Text(none, none, k, k), kSBg, kS8), 2, "bytes");
    }
    // F-MALFORMED, F-COUNT, F-NOPASS
    {
        FRow("F-MALFORMED",
             anchorfile::Judge(bgB, devB, Text(none, none, k, k) + "2026-11-17T00:30:00Z [speed-p5] anchor S3 p0 c9 x 1 y 2\n",
                               kSBg, kS8),
             2, "malformed");
        const std::string counted = std::string(kNoise) + Render(kSBg, 0, none) + Render(kSBg, 1, none) +
                                    Render(kS8, 0, k, (int)k.size() + 1) + Render(kS8, 1, k);
        FRow("F-COUNT", anchorfile::Judge(bgB, devB, counted, kSBg, kS8), 2, "count");
        const std::string nop1 = std::string(kNoise) + Render(kSBg, 0, none) + Render(kSBg, 1, none) + Render(kS8, 0, k);
        FRow("F-NOPASS", anchorfile::Judge(bgB, devB, nop1, kSBg, kS8), 2, "missing print");
        // F-ORDER: the draw order is part of the picture; lines numbered out of their listed order are refused
        std::vector<Line> sw = k;
        sw[0].c = 1;
        sw[1].c = 0;
        FRow("F-ORDER", anchorfile::Judge(bgB, devB, Text(none, none, sw, sw), kSBg, kS8), 2, "out of order");
    }
    std::printf("anchor_compare selftest-file: %d FAIL\n", failures);
    return failures ? 1 : 0;
}

bool WriteFile(const std::string& path, const std::string& data)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}
bool ReadFile(const std::string& path, std::string& out)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

int EmitSynthetic(const std::string& dir)
{
    const Frame s = Scene();
    std::unique_ptr<Img> bg = SyntheticBg();
    std::unique_ptr<Img> dev = DeviceFrame(*bg, s);
    std::unique_ptr<Img> flip = Copy(*dev);
    flip->p[FirstIn(anchor::Footprint(s), std::vector<uint8_t>((size_t)W * W, 0))] ^= 0x0001;
    const std::vector<Line> k = LinesOf(s), none;
    auto str = [](const std::vector<uint8_t>& b) { return std::string(b.begin(), b.end()); };
    const bool ok = WriteFile(dir + "/bg.fb", str(Body(*bg))) && WriteFile(dir + "/8copy.fb", str(Body(*dev))) &&
                    WriteFile(dir + "/8copy-flip.fb", str(Body(*flip))) &&
                    WriteFile(dir + "/lines.txt", Text(none, none, k, k));
    std::printf("emit-synthetic: %s\n", ok ? "written" : "FAILED");
    return ok ? 0 : 2;
}

int FileMode(char** a)
{
    std::string bgS, devS, text;
    if (!ReadFile(a[1], bgS) || !ReadFile(a[2], devS) || !ReadFile(a[3], text)) {
        std::printf("VOID: an input file is unreadable\n");
        return 2;
    }
    const Verdict v = anchorfile::Judge(std::vector<uint8_t>(bgS.begin(), bgS.end()),
                                        std::vector<uint8_t>(devS.begin(), devS.end()), text, std::atoi(a[4]),
                                        std::atoi(a[5]));
    if (v.code == 2 && v.contacts == 0 && v.pairs == 0) {
        std::printf("VOID: %s\n", v.reason.c_str());
        return 2;
    }
    std::printf("anchor: contacts %d, overlapping pairs %d, edge-clipped %d, region %ld px, mismatch %ld -> %s%s\n",
                v.contacts, v.pairs, v.clipped, v.region, v.mismatch, v.code == 2 ? "VOID: " : "", v.reason.c_str());
    return v.code;
}

// ---- section 7g's excluded boxes ----

// 7e's frozen contact list (x, y, trk x 10), all cat 4, up 0: the X-COST row's input.
Frame Frozen7e()
{
    static const int c[20][3] = {{102, 119, 0},    {209, 106, 3510}, {117, 165, 2730}, {222, 181, 310},
                                 {127, 46, 960},   {114, 92, 2580},  {221, 120, 1800}, {99, 172, 1120},
                                 {130, -9, 950},   {77, 47, 2400},   {116, 81, 850},   {23, 61, 2110},
                                 {154, 177, 2390}, {98, 119, 1800},  {100, 213, 2820}, {74, 140, 1560},
                                 {107, 242, 2760}, {197, 109, 3520}, {105, 91, 620},   {162, 217, 660}};
    Frame f;
    f.up = 0;
    f.zoomed = false;
    for (const auto& k : c) f.k.push_back(Contact{4, (float)(k[2] / 10), k[0], k[1], false, 0x07E0u});
    return f;
}

// THE FIXTURE'S BOXES are where the FIRMWARE draws (7g's file:line reading), as LITERALS -- never anchor's constants:
// a plant that moves kClockBox must not move the fixture with it (grade_heap2's off-prediction, 03:06Z).
const anchor::Box kFwClock = {105, 134, 210, 217};
const anchor::Box kFwIndicator = {99, 141, 221, 227};

void PaintBox(Img& img, const anchor::Box& b, uint16_t v)
{
    for (int y = b.y0; y <= b.y1; ++y)
        for (int x = b.x0; x <= b.x1; ++x) img.p[y * W + x] = v;
}

int SelfTestBox()
{
    // X-COST: the frozen list loses exactly 5 footprint px (all c14's) and keeps a 3,713 px region (3,736 less 23).
    {
        const Frame f = Frozen7e();
        const std::vector<uint8_t> t = anchor::Footprint(f), r = anchor::Region(f);
        long removed = 0, region = 0;
        for (int i = 0; i < W * W; ++i) {
            removed += t[i] && anchor::Excluded(i % W, i / W);
            region += r[i];
        }
        Row(removed == 5 && region == 3713, "X-COST",
            "7e's list: footprint px removed " + Num(removed) + " (want 5), region " + Num(region) + " (want 3713)");
    }
    // c14 alone, the contact under the clock
    Frame c14;
    c14.up = 0;
    c14.zoomed = false;
    c14.k.push_back(Contact{4, 282.0f, 100, 213, false, 0x07E0u});
    const std::vector<uint8_t> d14 = anchor::Dilate(anchor::Footprint(c14), anchor::kDilate);
    int yStar = -1;
    for (int y = kFwClock.y0; y <= kFwClock.y1 && yStar < 0; ++y)
        if (d14[y * W + 104] && d14[y * W + 105]) yStar = y;
    // X-C14 (control): c14's dilated region reaches across the clock box's left edge, so X-EDGE and X-CLOCK test it
    Row(yStar >= 0, "X-C14", "CONTROL: c14's dilated region covers x 104 and 105 on clock row " + Num(yStar));
    std::unique_ptr<Img> bg = SyntheticBg();
    std::unique_ptr<Img> dev = DeviceFrame(*bg, c14);
    // X-EDGE: x 104 (outside the box) is compared, x 105 (its first column) is not
    {
        std::unique_ptr<Img> a = Copy(*dev), b = Copy(*dev);
        if (yStar >= 0) {
            a->p[yStar * W + 104] ^= 0x0001;
            b->p[yStar * W + 105] ^= 0x0001;
        }
        const long m104 = anchor::Anchor(*bg, *a, c14).mismatch, m105 = anchor::Anchor(*bg, *b, c14).mismatch;
        Row(yStar >= 0 && m104 == 1 && m105 == 0, "X-EDGE",
            "a px changed at x 104 reads " + Num(m104) + " (want 1); at x 105 reads " + Num(m105) + " (want 0)");
    }
    // X-CLOCK: the clock drawn over c14 with a different minute than the BG grab's reads PASS
    {
        std::unique_ptr<Img> bgGrab = Copy(*bg), devClock = Copy(*dev);
        PaintBox(*bgGrab, kFwClock, 0x1111);
        PaintBox(*devClock, kFwClock, 0x2222);   // drawn AFTER the contacts, as DrawClock is
        std::unique_ptr<Img> host = anchor::Composite(*bgGrab, c14);
        long whole = 0;
        for (int i = 0; i < W * W; ++i) whole += host->p[i] != devClock->p[i];
        const anchor::Result r = anchor::Anchor(*bgGrab, *devClock, c14);
        Row(whole > 0 && r.mismatch == 0, "X-CLOCK",
            "CONTROL: the frames differ in " + Num(whole) + " px (want > 0), all in the box; mismatch " +
                Num(r.mismatch) + " (want 0)");
    }
    // X-EQUAL, X-EQUAL-IND, X-EQUAL-OUT: a scene's equality outside the boxes
    {
        std::unique_ptr<Img> g1 = SyntheticBg(), g2 = Copy(*g1), g3 = Copy(*g1), g4 = Copy(*g1);
        PaintBox(*g2, kFwClock, 0x2222);
        PaintBox(*g4, kFwIndicator, 0x3333);
        g3->p[212 * W + 135] ^= 0x0001;
        Row(anchor::EqualOutside(*g1, *g2), "X-EQUAL", "the whole clock box changed: equal outside the boxes");
        Row(anchor::EqualOutside(*g1, *g4), "X-EQUAL-IND", "the whole indicator box changed: equal outside the boxes");
        Row(!anchor::EqualOutside(*g1, *g3), "X-EQUAL-OUT", "one px at x 135, y 212 (just right of the clock box): NOT equal");
    }
    std::printf("anchor_compare selftest-box: %d FAIL\n", failures);
    return failures ? 1 : 0;
}

// ---- section 10's marker A/B ----

constexpr int kSMark = 0;   // MARK60, the only scene in SPEED_MARK_BENCH's list

std::string TextAB(const std::vector<Line>& p0, const std::vector<Line>& p1)
{
    return std::string(kNoise) + Render(kSMark, 0, p0) + Render(kSMark, 1, p1);
}

void BRow(const char* id, const anchorfile::AbVerdict& v, int wantCode, const std::string& part, long wantIn = -1,
          long wantOut = -1)
{
    const bool ok = v.code == wantCode && v.reason.find(part) != std::string::npos && (wantIn < 0 || v.in == wantIn) &&
                    (wantOut < 0 || v.out == wantOut);
    Row(ok, id, "exit " + Num(v.code) + " (want " + Num(wantCode) + "), in " + Num(v.in) + " out " + Num(v.out) +
                    " boxed " + Num(v.boxed) + ", '" + v.reason + "' (want '" + part + "')");
}

int SelfTestAB()
{
    const Frame s = Scene();
    std::unique_ptr<Img> bg = SyntheticBg();
    std::unique_ptr<Img> m = DeviceFrame(*bg, s);
    const std::vector<uint8_t> mB = Body(*m);
    const std::vector<Line> k = LinesOf(s), none;
    const std::string kt = TextAB(k, k);
    const std::vector<uint8_t> t = anchor::Footprint(s);
    const std::vector<uint8_t> zero((size_t)W * W, 0), d1 = anchor::Dilate(t, 1), d2 = anchor::Dilate(t, 2),
                               d3 = anchor::Dilate(t, 3), d10 = anchor::Dilate(t, 10);
    // P = M with pixels flipped at the given indices (a negative index flips nothing)
    auto flip = [&](std::initializer_list<int> at) {
        std::unique_ptr<Img> p = Copy(*m);
        for (int i : at)
            if (i >= 0) p->p[i] ^= 0x0001;
        return Body(*p);
    };
    // the first pixel outside every box, in `in` and not in `out`
    auto firstOpen = [&](const std::vector<uint8_t>& in, const std::vector<uint8_t>& out) {
        for (int i = 0; i < W * W; ++i)
            if (in[i] && !out[i] && !anchor::Excluded(i % W, i / W)) return i;
        return -1;
    };
    std::vector<uint8_t> all((size_t)W * W, 1);
    const int iMark = firstOpen(t, zero), i2 = firstOpen(d2, d1), i3 = firstOpen(d3, d2), iFar = firstOpen(all, d10);
    // B-PICKS (control): every pixel the rows below flip exists
    Row(iMark >= 0 && i2 >= 0 && i3 >= 0 && iFar >= 0, "B-PICKS",
        "CONTROL: a footprint px " + Num(iMark) + ", distance 2 " + Num(i2) + ", distance 3 " + Num(i3) +
            ", distance > 10 " + Num(iFar) + " (each >= 0)");
    BRow("B-IDENT", anchorfile::JudgeAB(mB, mB, kt, kt, kSMark), 0, "", 0, 0);
    BRow("B-MARK", anchorfile::JudgeAB(mB, flip({iMark}), kt, kt, kSMark), 1, "", 1, 0);
    BRow("B-EDGE2", anchorfile::JudgeAB(mB, flip({i2}), kt, kt, kSMark), 1, "", 1, 0);
    BRow("B-EDGE3", anchorfile::JudgeAB(mB, flip({i3}), kt, kt, kSMark), 2, "", 0, 1);
    BRow("B-BG", anchorfile::JudgeAB(mB, flip({iFar}), kt, kt, kSMark), 2, "", 0, 1);
    BRow("B-BOTH", anchorfile::JudgeAB(mB, flip({iMark, iFar}), kt, kt, kSMark), 2, "", 1, 1);
    // B-CLOCK, B-IND: a whole box repainted in P is never judged (the firmware's boxes, as literals)
    {
        std::unique_ptr<Img> pc = Copy(*m), pi = Copy(*m);
        PaintBox(*pc, kFwClock, 0x2222);
        PaintBox(*pi, kFwIndicator, 0x3333);
        const anchorfile::AbVerdict vc = anchorfile::JudgeAB(mB, Body(*pc), kt, kt, kSMark);
        const anchorfile::AbVerdict vi = anchorfile::JudgeAB(mB, Body(*pi), kt, kt, kSMark);
        BRow("B-CLOCK", vc, 0, "", 0, 0);
        BRow("B-IND", vi, 0, "", 0, 0);
        Row(vc.boxed > 0 && vi.boxed > 0, "B-BOXED",
            "the repaints were seen and set aside: boxed " + Num(vc.boxed) + " and " + Num(vi.boxed) + " (each > 0)");
    }
    // the VOID rows
    {
        std::vector<Line> kx = k;
        kx[0].x += 1;
        BRow("B-ASAME", anchorfile::JudgeAB(mB, mB, kt, TextAB(kx, kx), kSMark), 3, "A-SAME");
        std::vector<Line> kc = k;
        kc[2].col ^= 0x0001;
        BRow("B-ASAME-COL", anchorfile::JudgeAB(mB, mB, kt, TextAB(kc, kc), kSMark), 3, "A-SAME");
        std::vector<Line> k3(k.begin(), k.end() - 1);
        BRow("B-ASAME-N", anchorfile::JudgeAB(mB, mB, kt, TextAB(k3, k3), kSMark), 3, "A-SAME");
        BRow("B-STILL-M", anchorfile::JudgeAB(mB, mB, TextAB(k, kx), kt, kSMark), 3, "A-STILL");
        BRow("B-STILL-P", anchorfile::JudgeAB(mB, mB, kt, TextAB(k, kx), kSMark), 3, "A-STILL");
        std::vector<Line> ko = k;
        ko[1].ovl = 0x001;
        BRow("B-OVL", anchorfile::JudgeAB(mB, mB, TextAB(ko, ko), TextAB(ko, ko), kSMark), 3, "ovl");
        std::vector<Line> ku = k;
        ku[0].up = 90;
        BRow("B-UP", anchorfile::JudgeAB(mB, mB, TextAB(ku, ku), TextAB(ku, ku), kSMark), 3, "up");
        std::vector<uint8_t> shortB(mB.begin(), mB.end() - 1);
        BRow("B-GRAB", anchorfile::JudgeAB(mB, shortB, kt, kt, kSMark), 3, "bytes");
        BRow("B-EMPTY", anchorfile::JudgeAB(mB, mB, TextAB(none, none), TextAB(none, none), kSMark), 3, "empty");
        const std::string p0only = std::string(kNoise) + Render(kSMark, 0, k);
        BRow("B-MISSING", anchorfile::JudgeAB(mB, mB, kt, p0only, kSMark), 3, "missing print");
    }
    std::printf("anchor_compare selftest-ab: %d FAIL\n", failures);
    return failures ? 1 : 0;
}

int AbMode(char** a)
{
    std::string mS, pS, mT, pT;
    if (!ReadFile(a[2], mS) || !ReadFile(a[3], pS) || !ReadFile(a[4], mT) || !ReadFile(a[5], pT)) {
        std::printf("VOID: an input file is unreadable\n");
        return 3;
    }
    const anchorfile::AbVerdict v =
        anchorfile::JudgeAB(std::vector<uint8_t>(mS.begin(), mS.end()), std::vector<uint8_t>(pS.begin(), pS.end()), mT,
                            pT, std::atoi(a[6]));
    if (v.code == 3) {
        std::printf("VOID: %s\n", v.reason.c_str());
        return 3;
    }
    std::printf("ab: contacts %d, region %ld px; px differing outside the boxes: %ld in the region, %ld outside it "
                "(%ld inside the boxes, not judged) -> exit %d, %s\n",
                v.contacts, v.region, v.in, v.out, v.boxed, v.code, v.reason.c_str());
    return v.code;
}

// ---- 15c (3): the still label arms' pair ----

int SelfTestPair()
{
    std::unique_ptr<Img> a = DeviceFrame(*SyntheticBg(), Scene());
    const std::vector<uint8_t> aB = Body(*a);
    const int lx = 166, ly = 57;   // where the dot contact (160, 60)'s label sits: outside both boxes
    // PR-PICK (control): the label pixel lies outside both excluded boxes
    Row(!anchor::Excluded(lx, ly), "PR-PICK", "CONTROL: the label px (166, 57) is outside the clock and indicator boxes");
    auto PRow = [&](const char* id, const anchorfile::PairVerdict& v, int want) {
        Row(v.code == want, id, "exit " + Num(v.code) + " (want " + Num(want) + "), in " + Num(v.in) + " out " +
                                    Num(v.out) + ", '" + v.reason + "'");
    };
    PRow("PR-IDENT", anchorfile::JudgePair(aB, aB), 0);
    std::unique_ptr<Img> lbl = Copy(*a);
    lbl->p[ly * W + lx] ^= 0x0001;   // THE 1-px LABEL PLANT: must read "not equal"
    PRow("PR-LABEL", anchorfile::JudgePair(aB, Body(*lbl)), 1);
    std::unique_ptr<Img> clk = Copy(*a);
    PaintBox(*clk, kFwClock, 0x2222);
    PRow("PR-CLOCK", anchorfile::JudgePair(aB, Body(*clk)), 0);
    std::vector<uint8_t> shortB(aB.begin(), aB.end() - 1);
    PRow("PR-GRAB", anchorfile::JudgePair(aB, shortB), 2);
    std::printf("anchor_compare selftest-pair: %d FAIL\n", failures);
    return failures ? 1 : 0;
}

int PairMode(char** a)
{
    std::string s1, s2;
    if (!ReadFile(a[2], s1) || !ReadFile(a[3], s2)) {
        std::printf("VOID: a grab is unreadable\n");
        return 2;
    }
    const anchorfile::PairVerdict v = anchorfile::JudgePair(std::vector<uint8_t>(s1.begin(), s1.end()),
                                                            std::vector<uint8_t>(s2.begin(), s2.end()));
    std::printf("pair: %ld px differ inside the excluded boxes, %ld outside -> exit %d, %s\n", v.in, v.out, v.code,
                v.reason.c_str());
    return v.code;
}

int EqualMode(char** a)
{
    std::unique_ptr<Img> g[3];
    for (int i = 0; i < 3; ++i) {
        std::string s;
        g[i] = anchor::NewImg();
        if (!ReadFile(a[2 + i], s) || !anchor::Decode(std::vector<uint8_t>(s.begin(), s.end()), *g[i])) {
            std::printf("VOID: grab %d is unreadable or not %d bytes\n", i + 1, W * W * 2);
            return 2;
        }
    }
    bool eq = true;
    for (int i = 1; i < 3; ++i) {
        long in = 0, out = 0;
        for (int p = 0; p < W * W; ++p)
            if (g[0]->p[p] != g[i]->p[p]) (anchor::Excluded(p % W, p / W) ? in : out)++;
        std::printf("grab 1 vs grab %d: %ld px differ inside the excluded boxes, %ld outside\n", i + 1, in, out);
        eq = eq && anchor::EqualOutside(*g[0], *g[i]);
    }
    std::printf("%s\n", eq ? "EQUAL outside the excluded boxes" : "NOT EQUAL outside the excluded boxes");
    return eq ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
    const std::string a1 = argc >= 2 ? argv[1] : "";
    if (argc == 2 && a1 == "--selftest") return SelfTest();
    if (argc == 2 && a1 == "--selftest-file") return SelfTestFile();
    if (argc == 3 && a1 == "--emit-synthetic") return EmitSynthetic(argv[2]);
    if (argc == 2 && a1 == "--selftest-box") return SelfTestBox();
    if (argc == 5 && a1 == "--equal") return EqualMode(argv);
    if (argc == 2 && a1 == "--selftest-ab") return SelfTestAB();
    if (argc == 7 && a1 == "--ab") return AbMode(argv);
    if (argc == 2 && a1 == "--selftest-pair") return SelfTestPair();
    if (argc == 4 && a1 == "--pair") return PairMode(argv);
    if (argc == 6) return FileMode(argv);
    std::printf("usage: anchor_compare --selftest | --selftest-file | --emit-synthetic <dir> | "
                "<bg.fb> <8copy.fb> <anchor-lines> <S_bg> <S_8copy> | --selftest-box | --equal <g1> <g2> <g3> | "
                "--selftest-ab | --ab <M.fb> <P.fb> <M-lines> <P-lines> <S>\n");
    return 2;
}
