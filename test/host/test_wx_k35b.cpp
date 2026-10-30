// Weatherscope 3.5b's Bend K rows and replay rows (docs/weatherscope-stage3.md A10.6, A12.41), built through the
// DEVICE's path -- bmpk::Validate, wxbase::Build, wxscenes::FromScenario / FromLoopFrame + FromPackage, then
// wxscreens::Compose (Bend dry) or ComposeLayered (the loop, through one Layer, as the firmware) -- each held to
// tools/weatherscope/screens/k35b.py's independent rendering (test/fixtures/wx/k35b.ref.txt) at 57,600 of 57,600
// pixels with its time, band and placed town labels; A12.53 adds Bend rain (the loop's last frame as a stored
// scenario, Kind::Dry, as the firmware builds it) at 60, 30 and 15 mi, B and C. Then the 90 replay rows (each loop
// frame through the layer equals its full composition, at 15, 30 and 60 mi, B and C), and two controls that prove both
// comparisons can fail. 3.5t t4 (A12.60) adds the 57 scenes in Light against k35b.py --theme light (k35bl.ref.txt, in
// the fixtures dir), LY (each scene's placement, Dark against Light), the 90 replay rows in Light, and RP-T's Bend half
// (one Layer, frames 0-7 Dark and 8-14 Light, each against its full composition in its own theme). 368 checks.
//
//     test_wx_k35b <test/fixtures/wx> <k35b.ref.txt> [--dump <dir>]     --dump writes each frame for k35b.py --diff
//
// Exit 0 all pass, 1 any fail, 2 the rig. Prints hashes, texts and screen pixels: no coordinate.

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

#include "BasemapPackage.h"
#include "WxBasemap.h"
#include "WxCrop.h"
#include "WxRaster.h"
#include "WxRender.h"
#include "WxScenarios.h"
#include "WxScenes.h"
#include "WxScreens.h"
#include "WxTowns.h"

typedef std::vector<uint8_t> Bytes;

namespace {

constexpr int kCases = 368;   // 21 + 6 + 30 K rows, 90 replay rows, 2 controls; t4: 57 K-light, 57 LY, 90 RP light, 15 RP-T
constexpr int kOffsetPt = -420;   // Pacific (PDT) on 2024-06-02 (A10.4)

// SHA-256, as in test_wx_k35.cpp.
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
inline uint32_t R(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
std::string Hex(const uint8_t* data, size_t len)
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t> m(data, data + len);
    const uint64_t bits = (uint64_t)len * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back((uint8_t)(bits >> (8 * i)));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)m[off + 4 * i] << 24 | (uint32_t)m[off + 4 * i + 1] << 16 | (uint32_t)m[off + 4 * i + 2] << 8 |
                   m[off + 4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = R(w[i - 15], 7) ^ R(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = R(w[i - 2], 17) ^ R(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (R(e, 6) ^ R(e, 11) ^ R(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const uint32_t t2 = (R(a, 2) ^ R(a, 13) ^ R(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
    return out;
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

std::string Hx(const char* s)
{
    char b[160];
    wxscenes::Token(s, b, sizeof b);
    return b;
}
const char* Ink(wxtheme::Ink c) { return wxscenes::InkName(c); }

struct Frame {
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

std::vector<std::string> Lines(const std::string& name, const wxscreens::ScreenIn& in, const wxscreens::ScreenOut& out,
                               const Rgb565& img)
{
    std::vector<std::string> l;
    l.push_back("ROW " + name + " " + sha::Hex(img.px.data(), img.px.size()));
    l.push_back("TIME " + name + " " + Hx(out.time) + " " + Ink(out.timeInk));
    if (in.loop) l.push_back("BAND " + name + " L " + Hx(in.loopAge) + " " + std::to_string(in.loopIndex));
    else l.push_back("BAND " + name + " S " + Hx(in.state) + " " + Ink(in.stateInk));
    for (int i = 0; i < out.nLabels; ++i) {
        const wxscreens::Placed& p = out.labels[i].at;
        char b[96];
        std::snprintf(b, sizeof b, " %c %d %d %d %d", p.side, p.box.x0, p.box.y0, p.box.x1, p.box.y1);
        l.push_back("LABEL " + name + " " + Hx(out.labels[i].label) + b);
    }
    return l;
}

// The package's part of one view: its places, ranking, peaks and basemap image (all for this radius and mask).
struct PkgView {
    wxscenes::PackagePlaces places;
    wxscenes::PackagePeaks peaks;
    int order[wxscenes::kMaxPlaces];
    Bytes img = Bytes(240 * 240, 0);
};

struct Scratch {
    std::vector<int32_t> qx, qy;
    std::vector<double> x, y, cross;
    wxbase::Scratch s;
    explicit Scratch(uint32_t n) : qx(n), qy(n), x(n), y(n), cross(n)
    {
        s.qx = qx.data();
        s.qy = qy.data();
        s.x = x.data();
        s.y = y.data();
        s.cross = cross.data();
        s.cap = n;
    }
};

bool Prepare(const bmpk::Package& pk, const wxcrop::Header& h, int mi, uint8_t mask, bool terrain, const Scratch& sc,
             PkgView* pv)
{
    wxrender::View v;
    wxrender::BuildViewKm(h, &v, mi * 1.609344);   // what Compose builds for this radius
    if (!pv->places.Load(pk, v)) return false;
    wxscreens::RankOrder(pv->places.Pops(), pv->places.Count(), pv->order);
    pv->peaks.Load(pk, v);
    return wxbase::Build(pk, v, mi, mask, terrain, sc.s, pv->img.data());
}

struct Variant {
    const char* name;
    uint8_t mask;
    bool terrain;
};
const Variant kVariants[7] = {{"towns", 0, false}, {"water", wxbase::kMaskWater, false},
                              {"rivers", wxbase::kMaskRivers, false}, {"roads", wxbase::kMaskRoads, false},
                              {"peaks", wxbase::kMaskPeaks, false}, {"B", wxbase::kMaskAll, false},
                              {"C", wxbase::kMaskAll, true}};

// 3.5t t4 (A12.60): LY's two parts, as test_wx_k35.cpp's -- the LABEL lines, and the map-band pixels in the theme's
// label or peak ink less the home marker's (the pixels the DARK frame draws in dark's white).
std::vector<std::string> LabelLines(const std::vector<std::string>& lines)
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
        std::printf("usage: test_wx_k35b <test/fixtures/wx> <k35b.ref.txt> [--dump <dir>]\n");
        return 2;
    }
    const std::string wx = argv[1];
    const std::string dump = argc == 5 ? argv[4] : "";

    Bytes refBytes;
    if (!ReadFile(argv[2], &refBytes)) { std::printf("cannot read %s (the RIG)\n", argv[2]); return 2; }
    std::map<std::string, std::vector<std::string>> ref;
    {
        std::istringstream is(std::string(refBytes.begin(), refBytes.end()));
        std::string line;
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            std::string tag, row;
            ls >> tag >> row;
            ref[row].push_back(line);
        }
    }

    Bytes pkgBytes;
    bmpk::Package pk;
    if (!ReadFile(wx + "/basemap/bend.bmpk", &pkgBytes) ||
        bmpk::Validate(pkgBytes.data(), pkgBytes.size(), &pk) != bmpk::Status::kOk) {
        std::printf("cannot load bend.bmpk (the RIG)\n");
        return 2;
    }
    static Scratch sc(pk.maxVertices + 1);
    static Frame clear;
    if (!Load(wx + "/bend-loop/synthetic-clear.wxc", &clear)) { std::printf("cannot load Bend's clear crop (the RIG)\n"); return 2; }
    static std::vector<Frame> frames(15);
    for (int i = 0; i < 15; ++i) {
        const int t = 19 * 60 + 10 + 4 * i;
        char n[48];
        std::snprintf(n, sizeof n, "/bend-loop/shsr-20240602-%02d%02d00.wxc", t / 60, t % 60);
        if (!Load(wx + n, &frames[i])) { std::printf("cannot load %s (the RIG)\n", n); return 2; }
    }
    static uint32_t pops[256];
    static int townOrder[256];
    for (unsigned i = 0; i < wxtowns::kCount; ++i) pops[i] = wxtowns::kTowns[i].pop;
    wxscreens::RankOrder(pops, (int)wxtowns::kCount, townOrder);

    auto dumpFrame = [&](const std::string& name, const Rgb565& img) {
        if (dump.empty()) return;
        FILE* f = std::fopen((dump + "/" + name + ".rgb565").c_str(), "wb");
        if (f) { std::fwrite(img.px.data(), 1, img.px.size(), f); std::fclose(f); }
    };
    auto grade = [&](const std::string& name, const std::vector<std::string>& got) {
        const auto it = ref.find(name);
        const bool same = it != ref.end() && it->second == got;
        std::string why = "57,600 of 57,600 px, time, band and " + std::to_string(got.size() - 3) + " label(s)";
        if (!same) {
            why = "DIFFERS from the reference";
            if (it == ref.end()) why += " (no reference row)";
            else
                for (size_t k = 0; k < got.size() || k < it->second.size(); ++k) {
                    const std::string a = k < got.size() ? got[k] : "-", b = k < it->second.size() ? it->second[k] : "-";
                    if (a != b) { why += ": device '" + a + "' against '" + b + "'"; break; }
                }
        }
        check(name, same, why);
    };

    std::printf("== K-BD: Bend dry, 60, 30 and 15 mi, every layer set ==\n");
    static wxscen::Scenario s;
    if (!wxscen::Build(wxscen::Kind::Dry, clear.h, clear.codes.data(), &s)) { std::printf("Build failed (the RIG)\n"); return 2; }
    static wxrender::View view;
    for (int mi : {60, 30, 15})
        for (const Variant& v : kVariants) {
            static PkgView pv;
            if (!Prepare(pk, clear.h, mi, v.mask, v.terrain, sc, &pv)) { std::printf("Prepare failed (the RIG)\n"); return 2; }
            static wxscreens::ScreenIn in;
            in = wxscreens::ScreenIn();
            wxscenes::FromScenario(s, clear.h, clear.codes.data(), mi, townOrder, &in, kOffsetPt);
            wxscenes::FromPackage(pk, pv.img.data(), v.mask, v.terrain, &pv.places, pv.order, &pv.peaks, &in);
            Rgb565 img;
            static wxscreens::ScreenOut out;
            out = wxscreens::ScreenOut();
            wxscreens::Compose(img, in, view, &out);
            char name[32];
            std::snprintf(name, sizeof name, "bdry-%d-%s", mi, v.name);
            dumpFrame(name, img);
            grade(name, Lines(name, in, out, img));
        }

    std::printf("== K-BR: Bend rain, the loop's last frame as a stored scenario, 60, 30 and 15 mi, B and C (A12.53) ==\n");
    {
        static wxscen::Scenario sr;
        if (!wxscen::Build(wxscen::Kind::Dry, frames[14].h, frames[14].codes.data(), &sr)) {
            std::printf("Build failed (the RIG)\n");
            return 2;
        }
        for (int mi : {60, 30, 15})
            for (int k = 5; k < 7; ++k) {
                const Variant& v = kVariants[k];
                static PkgView pv;
                if (!Prepare(pk, frames[14].h, mi, v.mask, v.terrain, sc, &pv)) { std::printf("Prepare failed (the RIG)\n"); return 2; }
                static wxscreens::ScreenIn in;
                in = wxscreens::ScreenIn();
                wxscenes::FromScenario(sr, frames[14].h, frames[14].codes.data(), mi, townOrder, &in, kOffsetPt);
                wxscenes::FromPackage(pk, pv.img.data(), v.mask, v.terrain, &pv.places, pv.order, &pv.peaks, &in);
                Rgb565 img;
                static wxscreens::ScreenOut out;
                out = wxscreens::ScreenOut();
                wxscreens::Compose(img, in, view, &out);
                char name[32];
                std::snprintf(name, sizeof name, "brain-%d-%s", mi, v.name);
                dumpFrame(name, img);
                grade(name, Lines(name, in, out, img));
            }
    }

    std::printf("== K-BL: the Bend loop at 60 mi, B and C, through one Layer ==\n");
    static wxscreens::Layer layer;
    static PkgView pvB, pvC;
    if (!Prepare(pk, frames[0].h, 60, wxbase::kMaskAll, false, sc, &pvB) ||
        !Prepare(pk, frames[0].h, 60, wxbase::kMaskAll, true, sc, &pvC)) {
        std::printf("Prepare failed (the RIG)\n");
        return 2;
    }
    static std::string cFrameSha[15];
    for (int i = 0; i < 15; ++i)
        for (int k = 5; k < 7; ++k) {
            const Variant& v = kVariants[k];
            PkgView& pv = v.terrain ? pvC : pvB;
            static wxscreens::ScreenIn in;
            in = wxscreens::ScreenIn();
            wxscenes::FromLoopFrame(frames[i].h, frames[i].codes.data(), i, frames[14].h.frameEpoch, 60, townOrder, &in,
                                    kOffsetPt);
            wxscenes::FromPackage(pk, pv.img.data(), v.mask, v.terrain, &pv.places, pv.order, &pv.peaks, &in);
            Rgb565 img;
            static wxscreens::ScreenOut out;
            out = wxscreens::ScreenOut();
            wxscreens::ComposeLayered(img, in, layer, -1, &out);
            char name[32];
            std::snprintf(name, sizeof name, "bloop-%02d-%s", i, v.name);
            dumpFrame(name, img);
            const std::vector<std::string> got = Lines(name, in, out, img);
            if (v.terrain) cFrameSha[i] = got[0].substr(got[0].rfind(' ') + 1);
            grade(name, got);
        }

    std::printf("== RP-B: the Bend loop through the layer equals its full composition, 15/30/60 x B/C ==\n");
    for (int mi : {15, 30, 60})
        for (int k = 5; k < 7; ++k) {
            const Variant& v = kVariants[k];
            static PkgView pv;
            if (!Prepare(pk, frames[0].h, mi, v.mask, v.terrain, sc, &pv)) { std::printf("Prepare failed (the RIG)\n"); return 2; }
            static wxscreens::Layer rl;
            for (int i = 0; i < 15; ++i) {
                static wxscreens::ScreenIn in;
                in = wxscreens::ScreenIn();
                wxscenes::FromLoopFrame(frames[i].h, frames[i].codes.data(), i, frames[14].h.frameEpoch, mi, townOrder,
                                        &in, kOffsetPt);
                wxscenes::FromPackage(pk, pv.img.data(), v.mask, v.terrain, &pv.places, pv.order, &pv.peaks, &in);
                Rgb565 full, layered;
                static wxscreens::ScreenOut o1, o2;
                o1 = wxscreens::ScreenOut();
                o2 = wxscreens::ScreenOut();
                wxscreens::Compose(full, in, view, &o1);
                wxscreens::ComposeLayered(layered, in, rl, -1, &o2);
                char name[40];
                std::snprintf(name, sizeof name, "brep-%d-%s-%02d", mi, v.name, i);
                long diff = 0;
                for (size_t p = 0; p < full.px.size(); p += 2) diff += full.px[p] != layered.px[p] || full.px[p + 1] != layered.px[p + 1];
                check(name, diff == 0 && o1.nLabels == o2.nLabels, "layered against full: " + std::to_string(diff) + " px differ");
            }
        }

    std::printf("== the controls ==\n");
    {   // K-X: each loop frame's C rendering against its B reference must differ (terrain under every dry pixel)
        int differ = 0;
        for (int i = 0; i < 15; ++i) {
            char name[32];
            std::snprintf(name, sizeof name, "bloop-%02d-B", i);
            const auto it = ref.find(name);
            if (it != ref.end() && !cFrameSha[i].empty() && it->second[0].substr(it->second[0].rfind(' ') + 1) != cFrameSha[i])
                ++differ;
        }
        check("K-X", differ == 15, std::to_string(differ) + " of 15 C frames differ from their B reference (want 15)");
    }
    {   // RP-X: B's layer replayed, unrebuilt, into a towns-only frame must differ from that frame's full composition
        static wxscreens::Layer lb;
        static PkgView pvT;
        if (!Prepare(pk, frames[0].h, 60, 0, false, sc, &pvT)) { std::printf("Prepare failed (the RIG)\n"); return 2; }
        static wxscreens::ScreenIn inB, inT;
        inB = wxscreens::ScreenIn();
        wxscenes::FromLoopFrame(frames[0].h, frames[0].codes.data(), 0, frames[14].h.frameEpoch, 60, townOrder, &inB, kOffsetPt);
        wxscenes::FromPackage(pk, pvB.img.data(), wxbase::kMaskAll, false, &pvB.places, pvB.order, &pvB.peaks, &inB);
        Rgb565 junk, stale, full;
        static wxscreens::ScreenOut o;
        o = wxscreens::ScreenOut();
        wxscreens::ComposeLayered(junk, inB, lb, -1, &o);   // builds B's layer
        inT = wxscreens::ScreenIn();
        wxscenes::FromLoopFrame(frames[0].h, frames[0].codes.data(), 0, frames[14].h.frameEpoch, 60, townOrder, &inT, kOffsetPt);
        wxscenes::FromPackage(pk, pvT.img.data(), 0, false, &pvT.places, pvT.order, &pvT.peaks, &inT);
        // the layered path by hand, WITHOUT the key check: the stale layer is replayed
        wxscreens::DrawMap(stale, wxtheme::kDark, *inT.h, inT.codes, lb.view, nullptr, inT.base, inT.terrain);
        wxraster::Fill(stale, 0, 0, 239, wxraster::kTopBand - 1, wxtheme::kDark.bg);
        wxraster::Fill(stale, 0, wxraster::kBotBand, 239, 239, wxtheme::kDark.bg);
        wxscreens::Replay(stale, lb);
        o = wxscreens::ScreenOut();
        wxscreens::DrawStripAndBand(stale, inT, &o, nullptr);
        o = wxscreens::ScreenOut();
        wxscreens::Compose(full, inT, view, &o);
        long diff = 0;
        for (size_t p = 0; p < full.px.size(); p += 2) diff += full.px[p] != stale.px[p] || full.px[p + 1] != stale.px[p + 1];
        check("RP-X", diff > 0, "a stale B layer in a towns-only frame: " + std::to_string(diff) + " px differ (want > 0)");
    }

    // ---------------------------------------------------------------------------------------- 3.5t t4 (A12.60)
    std::printf("== K-light: the 57 Bend scenes in Light, against k35b.py --theme light ==\n");
    std::map<std::string, std::vector<std::string>> lref;
    {
        Bytes lb;
        if (!ReadFile(wx + "/k35bl.ref.txt", &lb)) { std::printf("cannot read k35bl.ref.txt (the RIG)\n"); return 2; }
        std::istringstream is(std::string(lb.begin(), lb.end()));
        std::string line;
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            std::string tag, row;
            ls >> tag >> row;
            lref[row].push_back(line);
        }
    }
    struct Shot {
        Rgb565 img;
        std::vector<std::string> lines;
    };
    static std::vector<std::pair<std::string, std::pair<Shot, Shot>>> pairsDL;   // name -> (dark, light)
    auto gradeLight = [&](const std::string& name, const std::vector<std::string>& got) {
        const auto it = lref.find(name);
        const bool same = it != lref.end() && it->second == got;
        std::string why = "57,600 of 57,600 px, time, band and " + std::to_string(got.size() - 3) + " label(s)";
        if (!same) {
            why = "DIFFERS from the reference";
            if (it == lref.end()) why += " (no reference row)";
            else
                for (size_t k = 0; k < got.size() || k < it->second.size(); ++k) {
                    const std::string a = k < got.size() ? got[k] : "-", b = k < it->second.size() ? it->second[k] : "-";
                    if (a != b) { why += ": device '" + a + "' against '" + b + "'"; break; }
                }
        }
        check("KL:" + name, same, why);
    };
    auto scenario = [&](const std::string& name, const wxscen::Scenario& sn, const Frame& f, int mi, const Variant& v) {
        static PkgView pv;
        if (!Prepare(pk, f.h, mi, v.mask, v.terrain, sc, &pv)) return false;
        Shot shots[2];
        for (int t = 0; t < 2; ++t) {
            static wxscreens::ScreenIn in;
            in = wxscreens::ScreenIn();
            wxscenes::FromScenario(sn, f.h, f.codes.data(), mi, townOrder, &in, kOffsetPt);
            wxscenes::FromPackage(pk, pv.img.data(), v.mask, v.terrain, &pv.places, pv.order, &pv.peaks, &in);
            in.which = t ? wxtheme::Which::Light : wxtheme::Which::Dark;
            static wxscreens::ScreenOut out;
            out = wxscreens::ScreenOut();
            wxscreens::Compose(shots[t].img, in, view, &out);
            shots[t].lines = Lines(name, in, out, shots[t].img);
        }
        gradeLight(name, shots[1].lines);
        pairsDL.push_back({name, {shots[0], shots[1]}});
        return true;
    };
    for (int mi : {60, 30, 15})
        for (const Variant& v : kVariants) {
            char name[32];
            std::snprintf(name, sizeof name, "bdry-%d-%s", mi, v.name);
            if (!scenario(name, s, clear, mi, v)) { std::printf("Prepare failed (the RIG)\n"); return 2; }
        }
    {
        static wxscen::Scenario sr;
        if (!wxscen::Build(wxscen::Kind::Dry, frames[14].h, frames[14].codes.data(), &sr)) { std::printf("Build failed (the RIG)\n"); return 2; }
        for (int mi : {60, 30, 15})
            for (int k = 5; k < 7; ++k) {
                char name[32];
                std::snprintf(name, sizeof name, "brain-%d-%s", mi, kVariants[k].name);
                if (!scenario(name, sr, frames[14], mi, kVariants[k])) { std::printf("Prepare failed (the RIG)\n"); return 2; }
            }
    }
    {   // the loop: each theme through its own Layer, B and C alternating, as K-BL
        static wxscreens::Layer layD, layL;
        for (int i = 0; i < 15; ++i)
            for (int k = 5; k < 7; ++k) {
                const Variant& v = kVariants[k];
                PkgView& pv = v.terrain ? pvC : pvB;
                char name[32];
                std::snprintf(name, sizeof name, "bloop-%02d-%s", i, v.name);
                Shot shots[2];
                for (int t = 0; t < 2; ++t) {
                    static wxscreens::ScreenIn in;
                    in = wxscreens::ScreenIn();
                    wxscenes::FromLoopFrame(frames[i].h, frames[i].codes.data(), i, frames[14].h.frameEpoch, 60, townOrder,
                                            &in, kOffsetPt);
                    wxscenes::FromPackage(pk, pv.img.data(), v.mask, v.terrain, &pv.places, pv.order, &pv.peaks, &in);
                    in.which = t ? wxtheme::Which::Light : wxtheme::Which::Dark;
                    static wxscreens::ScreenOut out;
                    out = wxscreens::ScreenOut();
                    wxscreens::ComposeLayered(shots[t].img, in, t ? layL : layD, -1, &out);
                    shots[t].lines = Lines(name, in, out, shots[t].img);
                }
                gradeLight(name, shots[1].lines);
                pairsDL.push_back({name, {shots[0], shots[1]}});
            }
    }

    std::printf("== LY: placement is colour-blind, Dark against Light ==\n");
    for (const auto& e : pairsDL) {
        const Shot& d = e.second.first;
        const Shot& l = e.second.second;
        const std::vector<std::string> ld = LabelLines(d.lines), ll = LabelLines(l.lines);
        const std::set<int> md = InkMask(d.img, d.img, wxtheme::kDark), ml = InkMask(l.img, d.img, wxtheme::kLight);
        const bool same = ld == ll && md == ml;
        check("LY:" + e.first, same, std::to_string(ld.size()) + " town labels, " + std::to_string(md.size()) +
                                         " label/peak-ink px" + (same ? ", equal in both themes" : "; DIFFER"));
    }

    std::printf("== RP light: the Bend loop through the layer equals its full composition in Light, 15/30/60 x B/C ==\n");
    for (int mi : {15, 30, 60})
        for (int k = 5; k < 7; ++k) {
            const Variant& v = kVariants[k];
            static PkgView pv;
            if (!Prepare(pk, frames[0].h, mi, v.mask, v.terrain, sc, &pv)) { std::printf("Prepare failed (the RIG)\n"); return 2; }
            static wxscreens::Layer rlL;
            for (int i = 0; i < 15; ++i) {
                static wxscreens::ScreenIn in;
                in = wxscreens::ScreenIn();
                wxscenes::FromLoopFrame(frames[i].h, frames[i].codes.data(), i, frames[14].h.frameEpoch, mi, townOrder,
                                        &in, kOffsetPt);
                wxscenes::FromPackage(pk, pv.img.data(), v.mask, v.terrain, &pv.places, pv.order, &pv.peaks, &in);
                in.which = wxtheme::Which::Light;
                Rgb565 full, layered;
                static wxscreens::ScreenOut o1, o2;
                o1 = wxscreens::ScreenOut();
                o2 = wxscreens::ScreenOut();
                wxscreens::Compose(full, in, view, &o1);
                wxscreens::ComposeLayered(layered, in, rlL, -1, &o2);
                char name[48];
                std::snprintf(name, sizeof name, "RPL:brep-%d-%s-%02d", mi, v.name, i);
                long diff = 0;
                for (size_t q = 0; q < full.px.size(); q += 2) diff += full.px[q] != layered.px[q] || full.px[q + 1] != layered.px[q + 1];
                check(name, diff == 0 && o1.nLabels == o2.nLabels, "layered against full: " + std::to_string(diff) + " px differ");
            }
        }

    std::printf("== RP-T, Bend: one Layer, frames 0-7 in Dark and 8-14 in Light, each against its own theme's full composition ==\n");
    {
        static PkgView pv;
        if (!Prepare(pk, frames[0].h, 60, kVariants[5].mask, false, sc, &pv)) { std::printf("Prepare failed (the RIG)\n"); return 2; }
        static wxscreens::Layer lt;
        for (int i = 0; i < 15; ++i) {
            static wxscreens::ScreenIn in;
            in = wxscreens::ScreenIn();
            wxscenes::FromLoopFrame(frames[i].h, frames[i].codes.data(), i, frames[14].h.frameEpoch, 60, townOrder, &in,
                                    kOffsetPt);
            wxscenes::FromPackage(pk, pv.img.data(), kVariants[5].mask, false, &pv.places, pv.order, &pv.peaks, &in);
            in.which = i < 8 ? wxtheme::Which::Dark : wxtheme::Which::Light;
            Rgb565 full, layered;
            static wxscreens::ScreenOut o1, o2;
            o1 = wxscreens::ScreenOut();
            o2 = wxscreens::ScreenOut();
            wxscreens::Compose(full, in, view, &o1);
            wxscreens::ComposeLayered(layered, in, lt, -1, &o2);
            char name[32];
            std::snprintf(name, sizeof name, "RPT:bend-%02d", i);
            long diff = 0;
            for (size_t q = 0; q < full.px.size(); q += 2) diff += full.px[q] != layered.px[q] || full.px[q + 1] != layered.px[q + 1];
            check(name, diff == 0, std::string(i < 8 ? "Dark" : "Light") + ", layered against full: " + std::to_string(diff) + " px differ");
        }
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    if (checks != kCases) {
        std::printf("the RIG: %d cases ran, A12.41 and A12.60 froze %d\n", checks, kCases);
        return 2;
    }
    return failures ? 1 : 0;
}
