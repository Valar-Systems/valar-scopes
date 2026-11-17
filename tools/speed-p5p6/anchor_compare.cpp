// Board addendum 2's device anchor, the host tool (docs/speed-p5p6.md section 7, on weatherscope/speed-p5p6).
//
//   anchor_compare --selftest        the compare core's rows (A-*); exit 0 every row ok, 1 a row failed
//   anchor_compare --selftest-file   the file mode's verdict rows (F-*); exit 0 every row ok, 1 a row failed
//   anchor_compare --emit-synthetic <dir>   writes bg.fb, 8copy.fb, 8copy-flip.fb, lines.txt (F-PASS's inputs)
//   anchor_compare <bg.fb> <8copy.fb> <anchor-lines> <S_bg> <S_8copy>
//                                    section 7d's file mode: exit 0 PASS, 1 FAIL, 2 VOID (anchor_file.h)
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

}  // namespace

int main(int argc, char** argv)
{
    const std::string a1 = argc >= 2 ? argv[1] : "";
    if (argc == 2 && a1 == "--selftest") return SelfTest();
    if (argc == 2 && a1 == "--selftest-file") return SelfTestFile();
    if (argc == 3 && a1 == "--emit-synthetic") return EmitSynthetic(argv[2]);
    if (argc == 6) return FileMode(argv);
    std::printf("usage: anchor_compare --selftest | --selftest-file | --emit-synthetic <dir> | "
                "<bg.fb> <8copy.fb> <anchor-lines> <S_bg> <S_8copy>\n");
    return 2;
}
