// Board addendum 2's device anchor, the host tool (docs/speed-p5p6.md section 7, on weatherscope/speed-p5p6).
//
//   anchor_compare --selftest     exit 0 every row ok, 1 a row failed
//   anchor_compare <anything else> exit 2: the input format (the anchor line's fields) waits on the reviewer's ruling
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
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "anchor_core.h"

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

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--selftest") return SelfTest();
    std::printf("anchor_compare: the input format waits on the reviewer's ruling (the anchor line's fields); only "
                "--selftest runs\n");
    return 2;
}
