// Weatherscope 3.5a's K rows (docs/weatherscope-stage3.md A9.10, A9.18, A9.23): the 25 screens built through the
// DEVICE's path -- wxscen::Build, wxscenes::FromScenario / FromLoopFrame, then wxscreens::Compose (the scenarios) or
// ComposeLayered (the 15 loop frames, through one Layer, as the firmware) into an RGB565 buffer -- each held
// to tools/weatherscope/screens/ref35.py's independent rendering (test/fixtures/wx/k35.ref.txt) at 57,600 of 57,600
// pixels (equal sha256 of the big-endian RGB565 frame), with its time, band texts and placed labels. Also AR1's drawn
// pixel set (A9.14) and SC1/SC2's C++ half (the synthetic clear crop through WxCrop.h). 3.5t t4 (A12.60) adds the same
// 25 screens in Light against ref35.py --k --theme light (k35l.ref.txt, beside the loop dir), and LY: each screen's
// placement, Dark against Light, with LY-X its control. 79 checks.
//
//     test_wx_k35 <loop dir> <k35.ref.txt> [--dump <dir>]     --dump writes each frame for ref35.py --k --diff
//
// Its sabotages: python -I test/host/sabotage_wx_k35.py. Prints hashes, texts and screen pixels: no coordinate.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "WxCrop.h"
#include "WxLogic.h"
#include "WxRaster.h"
#include "WxScenarios.h"
#include "WxScenes.h"
#include "WxScreens.h"
#include "WxTowns.h"

typedef std::vector<uint8_t> Bytes;

namespace {

// SHA-256, as in test_wx_crop.cpp and test_wx_loop_render.cpp.
namespace sha {
const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
uint32_t R(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
std::string Hex(const uint8_t* data, size_t len)
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    Bytes m(data, data + len);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    const uint64_t bits = (uint64_t)len * 8;
    for (int i = 7; i >= 0; --i) m.push_back((uint8_t)(bits >> (8 * i)));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)m[off + 4 * i] << 24) | ((uint32_t)m[off + 4 * i + 1] << 16) |
                   ((uint32_t)m[off + 4 * i + 2] << 8) | m[off + 4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = R(w[i - 15], 7) ^ R(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = R(w[i - 2], 17) ^ R(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = k + (R(e, 6) ^ R(e, 11) ^ R(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const uint32_t t2 = (R(a, 2) ^ R(a, 13) ^ R(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
    return std::string(out, 64);
}
}  // namespace sha

int checks = 0, failures = 0;
void check(const std::string& name, bool ok, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name.c_str(), what.c_str());
}

bool ReadFile(const std::string& path, Bytes* out)
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

// The board's representation: RGB565, big-endian, through color565's truncation.
class Rgb565 : public wxraster::Sink {
public:
    Bytes px = Bytes(240 * 240 * 2, 0);
    void Put(int x, int y, wxraster::Rgb c) override
    {
        const uint16_t v = (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
        px[2 * (y * 240 + x)] = (uint8_t)(v >> 8);
        px[2 * (y * 240 + x) + 1] = (uint8_t)(v & 0xFF);
    }
};
class Points : public wxraster::Sink {
public:
    std::set<std::pair<int, int>> set;
    void Put(int x, int y, wxraster::Rgb) override { set.insert({x, y}); }
};

// A text as one token, through the board's own formatter (wxscenes::Token), as ref35.py's hx() writes it.
std::string Hx(const char* s)
{
    char b[160];
    wxscenes::Token(s, b, sizeof b);
    return b;
}
const char* Ink(wxtheme::Ink c) { return wxscenes::InkName(c); }

struct Frame {
    std::string name;
    Bytes bytes, codes;
    wxcrop::Header h;
};
bool Load(const std::string& path, Frame* f)
{
    if (!ReadFile(path, &f->bytes)) return false;
    if (wxcrop::Validate(f->bytes.data(), f->bytes.size(), &f->h) != wxcrop::Status::Ok) return false;
    f->codes.assign(f->h.Cells(), 0xEE);
    return wxcrop::DecodeInto(f->bytes.data(), f->bytes.size(), f->codes.data(), f->codes.size()) == wxcrop::Status::Ok;
}

// One row's lines, in k35.ref.txt's format and order.
std::vector<std::string> Lines(const std::string& name, const wxscreens::ScreenIn& in, const wxscreens::ScreenOut& out,
                               const Rgb565& img)
{
    std::vector<std::string> l;
    l.push_back("ROW " + name + " " + sha::Hex(img.px.data(), img.px.size()));
    l.push_back("TIME " + name + " " + Hx(out.time) + " " + Ink(out.timeInk));
    if (in.warning) {
        l.push_back("BAND " + name + " 1 " + Hx(in.wt.band1));
        l.push_back("BAND " + name + " 2 " + Hx(in.wt.band2));
    } else if (in.loop) {
        l.push_back("BAND " + name + " L " + Hx(in.loopAge) + " " + std::to_string(in.loopIndex));
    } else {
        l.push_back("BAND " + name + " S " + Hx(in.state) + " " + Ink(in.stateInk));
    }
    for (int i = 0; i < out.nLabels; ++i) {
        const wxscreens::Placed& p = out.labels[i].at;
        char b[96];
        std::snprintf(b, sizeof b, " %c %d %d %d %d", p.side, p.box.x0, p.box.y0, p.box.x1, p.box.y1);
        l.push_back("LABEL " + name + " " + Hx(out.labels[i].label) + b);
    }
    return l;
}

// 3.5t t4 (A12.60): LY's two parts. The LABEL lines (each placed town: label, side, box), and the map-band pixels drawn
// in the theme's label or peak ink, less the home marker's -- the pixels the DARK frame draws in dark's white (rain 13
// is white in dark too, and leaving it out of both masks hides no label: labels draw over it).
std::vector<std::string> Labels(const std::vector<std::string>& lines)
{
    std::vector<std::string> out;
    for (const auto& l : lines)
        if (l.compare(0, 6, "LABEL ") == 0) out.push_back(l);
    return out;
}

uint16_t P565(wxraster::Rgb c) { return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }
uint16_t At(const Rgb565& img, int x, int y)
{
    return (uint16_t)((img.px[2 * (y * 240 + x)] << 8) | img.px[2 * (y * 240 + x) + 1]);
}

std::set<int> InkMask(const Rgb565& img, const Rgb565& dark, const wxtheme::Theme& th)
{
    std::set<int> m;
    const uint16_t home = P565(wxtheme::kDark.white), a = P565(th.label), b = P565(th.peak);
    for (int y = wxraster::kTopBand; y < wxraster::kBotBand; ++y)
        for (int x = 0; x < 240; ++x) {
            if (At(dark, x, y) == home) continue;
            const uint16_t v = At(img, x, y);
            if (v == a || v == b) m.insert(y * 240 + x);
        }
    return m;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 3 && !(argc == 5 && std::strcmp(argv[3], "--dump") == 0)) {
        std::printf("usage: test_wx_k35 <loop dir> <k35.ref.txt> [--dump <dir>]\n");
        return 2;
    }
    const std::string dir = argv[1];
    const std::string dump = argc == 5 ? argv[4] : "";

    // the reference, grouped by row; '#' lines are its provenance
    Bytes refBytes;
    if (!ReadFile(argv[2], &refBytes)) { std::printf("cannot read %s (the RIG)\n", argv[2]); return 2; }
    std::map<std::string, std::vector<std::string>> ref;
    std::vector<std::string> refOrder;
    std::string ar1Ref;
    {
        std::istringstream is(std::string(refBytes.begin(), refBytes.end()));
        std::string line;
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line.compare(0, 4, "AR1 ") == 0) { ar1Ref = line; continue; }
            std::istringstream ls(line);
            std::string tag, row;
            ls >> tag >> row;
            if (!ref.count(row)) refOrder.push_back(row);
            ref[row].push_back(line);
        }
    }

    // the 15 loop frames (A8.10: 21:04Z to 22:00Z, 4 min apart) and A8.10's picks, as the firmware makes them
    std::vector<Frame> frames(15);
    for (int i = 0; i < 15; ++i) {
        const int t = 21 * 60 + 4 + 4 * i;
        char n[40];
        std::snprintf(n, sizeof n, "shsr-20240521-%02d%02d00.wxc", t / 60, t % 60);
        frames[i].name = n;
        if (!Load(dir + "/" + n, &frames[i])) { std::printf("cannot load %s (the RIG)\n", n); return 2; }
    }
    int pickA = -1, pickD = -1;
    for (int i = 0; i < 15; ++i) {
        if (wxlogic::EchoWithin(frames[i].h, frames[i].codes.data(), wxscen::Centre(frames[i].h))) pickA = i;
        else pickD = i;
    }
    if (pickA < 0 || pickD < 0) { std::printf("no approaching or no dry frame (the RIG)\n"); return 2; }
    std::printf("picks: approaching %s, dry %s\n", frames[pickA].name.c_str(), frames[pickD].name.c_str());

    std::printf("== SC: the synthetic clear crop, through WxCrop.h ==\n");
    Frame clear;
    const bool loaded = Load(dir + "/synthetic-clear.wxc", &clear);
    size_t zeros = 0;
    for (uint8_t c : clear.codes) zeros += c == 0;
    check("SC1", loaded && clear.codes.size() == 38520 && zeros == 38520,
          std::to_string(clear.codes.size()) + " cells, " + std::to_string(zeros) + " of them code 0");
    check("SC2", loaded && clear.bytes.size() >= 32 && std::memcmp(clear.bytes.data(), frames[14].bytes.data(), 32) == 0,
          "the 32-byte header against " + frames[14].name + "'s");

    // the ranking, once (as the firmware)
    static uint32_t pops[256];
    static int order[256];
    for (unsigned i = 0; i < wxtowns::kCount; ++i) pops[i] = wxtowns::kTowns[i].pop;
    wxscreens::RankOrder(pops, (int)wxtowns::kCount, order);

    std::printf("== K: 25 screens against ref35.py ==\n");
    static wxrender::View view;
    std::vector<std::pair<std::string, std::vector<std::string>>> got;
    // The loop rows go through the layer, as the firmware draws them (A9.23's H2): one Layer for the 15 frames.
    static wxscreens::Layer layer;
    auto render = [&](const std::string& name, const wxscreens::ScreenIn& in, bool layered) {
        Rgb565 img;
        static wxscreens::ScreenOut out;
        out = wxscreens::ScreenOut();
        if (layered) wxscreens::ComposeLayered(img, in, layer, -1, &out);
        else wxscreens::Compose(img, in, view, &out);
        got.push_back({name, Lines(name, in, out, img)});
        if (!dump.empty()) {
            FILE* f = std::fopen((dump + "/" + name + ".rgb565").c_str(), "wb");
            if (f) { std::fwrite(img.px.data(), 1, img.px.size(), f); std::fclose(f); }
        }
    };
    struct Sc { const char* name; wxscen::Kind kind; const Frame* f; int R; };
    const Sc scs[10] = {
        {"dry-60", wxscen::Kind::Dry, &frames[pickD], 60},
        {"approaching-60", wxscen::Kind::Approaching, &frames[pickA], 60},
        {"herenow-60", wxscen::Kind::HereNow, &frames[pickA], 60},
        {"warning-60", wxscen::Kind::Warning, &frames[pickA], 60},
        {"stale-60", wxscen::Kind::Stale, &frames[pickA], 60},
        {"clear-60", wxscen::Kind::Clear, &clear, 60},
        {"approaching-30", wxscen::Kind::Approaching, &frames[pickA], 30},
        {"approaching-15", wxscen::Kind::Approaching, &frames[pickA], 15},
        {"dry-30", wxscen::Kind::Dry, &frames[pickD], 30},
        {"dry-15", wxscen::Kind::Dry, &frames[pickD], 15},
    };
    for (const Sc& c : scs) {
        static wxscen::Scenario s;
        if (!wxscen::Build(c.kind, c.f->h, c.f->codes.data(), &s)) { std::printf("Build %s failed (the RIG)\n", c.name); return 2; }
        static wxscreens::ScreenIn in;
        in = wxscreens::ScreenIn();
        wxscenes::FromScenario(s, c.f->h, c.f->codes.data(), c.R, order, &in);
        render(c.name, in, false);
    }
    for (int i = 0; i < 15; ++i) {
        static wxscreens::ScreenIn in;
        in = wxscreens::ScreenIn();
        wxscenes::FromLoopFrame(frames[i].h, frames[i].codes.data(), i, frames[14].h.frameEpoch, 60, order, &in);
        char n[16];
        std::snprintf(n, sizeof n, "loop-%02d", i);
        render(n, in, true);
    }
    for (const auto& g : got) {
        const auto it = ref.find(g.first);
        const std::vector<std::string> empty;
        const std::vector<std::string>& want = it == ref.end() ? empty : it->second;
        std::string why = "57,600 of 57,600 px, time, band and " + std::to_string(g.second.size() - 3 - (g.first == "warning-60")) +
                          " labels equal";
        bool ok = want == g.second;
        if (!ok) {
            size_t k = 0;
            while (k < want.size() && k < g.second.size() && want[k] == g.second[k]) ++k;
            why = "first difference, line " + std::to_string(k + 1) + ": want \"" +
                  (k < want.size() ? want[k] : std::string("(none)")) + "\", got \"" +
                  (k < g.second.size() ? g.second[k] : std::string("(none)")) + "\"";
        }
        check("K:" + g.first, ok, why);
    }
    if (refOrder.size() != got.size()) std::printf("the reference holds %u rows, this test %u\n", (unsigned)refOrder.size(), (unsigned)got.size());

    std::printf("== AR1's drawn set (A9.14) ==\n");
    {
        Points pts;
        wxscreens::DrawArrow(pts, wxscreens::MakeArrow(150.0, 100.0, 240), wxtheme::kDark.orangeGraphic);
        std::string line = "AR1";
        for (const auto& p : pts.set) line += " " + std::to_string(p.first) + "," + std::to_string(p.second);
        check("AR1PIX", line == ar1Ref, std::to_string(pts.set.size()) + " pixels" + (line == ar1Ref ? "" : ": " + line));
    }

    // ---------------------------------------------------------------------------------------- 3.5t t4 (A12.60)
    std::printf("== K-light: the 25 screens in Light, against ref35.py --k --theme light ==\n");
    std::map<std::string, std::vector<std::string>> lref;
    std::vector<std::string> lightOrder;
    {
        Bytes lb;
        const std::string lp = dir + "/../k35l.ref.txt";
        if (!ReadFile(lp, &lb)) { std::printf("cannot read %s (the RIG)\n", lp.c_str()); return 2; }
        std::istringstream is(std::string(lb.begin(), lb.end()));
        std::string line;
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#' || line.compare(0, 4, "AR1 ") == 0) continue;
            std::istringstream ls(line);
            std::string tag, row;
            ls >> tag >> row;
            if (!lref.count(row)) lightOrder.push_back(row);
            lref[row].push_back(line);
        }
    }
    // The same 25 inputs, built again as the firmware builds them; each theme's loop rows through its own Layer.
    struct Scene {
        std::string name;
        wxscreens::ScreenIn in;
        bool layered;
    };
    static std::vector<Scene> scenes;
    for (const Sc& c : scs) {
        static wxscen::Scenario s;
        if (!wxscen::Build(c.kind, c.f->h, c.f->codes.data(), &s)) { std::printf("Build %s failed (the RIG)\n", c.name); return 2; }
        scenes.push_back(Scene{c.name, wxscreens::ScreenIn(), false});
        wxscenes::FromScenario(s, c.f->h, c.f->codes.data(), c.R, order, &scenes.back().in);
    }
    for (int i = 0; i < 15; ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "loop-%02d", i);
        scenes.push_back(Scene{n, wxscreens::ScreenIn(), true});
        wxscenes::FromLoopFrame(frames[i].h, frames[i].codes.data(), i, frames[14].h.frameEpoch, 60, order, &scenes.back().in);
    }
    static wxscreens::Layer layerD, layerL;
    struct Shot {
        Rgb565 img;
        std::vector<std::string> lines;
        int firstTown = -1;
    };
    auto shoot = [&](const Scene& sc, wxtheme::Which which, wxscreens::Layer& lay, const int* ord, int nTowns) {
        static wxscreens::ScreenIn in;
        in = sc.in;
        in.which = which;
        if (ord) {
            in.order = ord;
            in.nTowns = nTowns;
        }
        Shot shot;
        static wxscreens::ScreenOut out;
        out = wxscreens::ScreenOut();
        if (sc.layered) wxscreens::ComposeLayered(shot.img, in, lay, -1, &out);
        else wxscreens::Compose(shot.img, in, view, &out);
        shot.lines = Lines(sc.name, in, out, shot.img);
        shot.firstTown = out.nLabels ? out.labels[0].town : -1;
        return shot;
    };
    std::vector<Shot> darkShots, lightShots;
    for (const Scene& sc : scenes) {
        darkShots.push_back(shoot(sc, wxtheme::Which::Dark, layerD, nullptr, 0));
        lightShots.push_back(shoot(sc, wxtheme::Which::Light, layerL, nullptr, 0));
    }
    for (size_t k = 0; k < scenes.size(); ++k) {
        const auto it = lref.find(scenes[k].name);
        const std::vector<std::string> empty;
        const std::vector<std::string>& want = it == lref.end() ? empty : it->second;
        const std::vector<std::string>& gotL = lightShots[k].lines;
        bool ok = want == gotL;
        std::string why = "57,600 of 57,600 px, time, band and " + std::to_string(Labels(gotL).size()) + " labels equal";
        if (!ok) {
            size_t j = 0;
            while (j < want.size() && j < gotL.size() && want[j] == gotL[j]) ++j;
            why = "first difference, line " + std::to_string(j + 1) + ": want \"" +
                  (j < want.size() ? want[j] : std::string("(none)")) + "\", got \"" +
                  (j < gotL.size() ? gotL[j] : std::string("(none)")) + "\"";
        }
        check("KL:" + scenes[k].name, ok, why);
    }

    std::printf("== LY: placement is colour-blind, Dark against Light ==\n");
    for (size_t k = 0; k < scenes.size(); ++k) {
        const std::vector<std::string> ld = Labels(darkShots[k].lines), ll = Labels(lightShots[k].lines);
        const std::set<int> md = InkMask(darkShots[k].img, darkShots[k].img, wxtheme::kDark);
        const std::set<int> ml = InkMask(lightShots[k].img, darkShots[k].img, wxtheme::kLight);
        const bool same = ld == ll && md == ml;
        check("LY:" + scenes[k].name, same, std::to_string(ld.size()) + " town labels, " + std::to_string(md.size()) +
                                                " label/peak-ink px" + (same ? ", equal in both themes" : "; DIFFER"));
    }
    {   // LY-X, the control: dry-60 in Light, ranked without its first placed town, must differ from the dark one
        const int first = darkShots[0].firstTown;
        static int ord2[256];
        int n2 = 0;
        for (unsigned i = 0; i < wxtowns::kCount; ++i)
            if (order[i] != first) ord2[n2++] = order[i];
        const Shot x = shoot(scenes[0], wxtheme::Which::Light, layerL, ord2, n2);
        const bool labels = Labels(x.lines) != Labels(darkShots[0].lines);
        const bool mask = InkMask(x.img, darkShots[0].img, wxtheme::kLight) != InkMask(darkShots[0].img, darkShots[0].img,
                                                                                       wxtheme::kDark);
        check("LY-X", first >= 0 && labels && mask, std::string(scenes[0].name) + " without its first placed town: labels " +
                                                        (labels ? "differ" : "EQUAL") + ", mask " + (mask ? "differs" : "EQUAL"));
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    if (checks != 79 || refOrder.size() != 25 || lightOrder.size() != 25) {
        std::printf("the RIG: %d checks (want 79), %u and %u reference rows (want 25 and 25)\n", checks,
                    (unsigned)refOrder.size(), (unsigned)lightOrder.size());
        return 2;
    }
    return failures ? 1 : 0;
}
