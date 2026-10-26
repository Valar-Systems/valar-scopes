// Weatherscope 3.5a's layer replay (src/weatherscope/WxScreens.h, docs/weatherscope-stage3.md A9.21 item 3, A9.23 H1):
// every loop frame drawn through ComposeLayered must equal the full Compose of the same input at 57,600 of 57,600
// pixels, with the same labels -- the 15 frames at 60, 30 and 15 mi, without and then with the warning overlay: 90
// rows, through ONE Layer in that order, so a layer that is not rebuilt when it should be is caught.
//
//     test_wx_replay <loop dir>
//
// Its sabotages: python -I test/host/sabotage_wx_replay.py. Prints frame names and pixel counts: no coordinate.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "WxCrop.h"
#include "WxScenarios.h"
#include "WxScenes.h"
#include "WxScreens.h"
#include "WxTowns.h"

namespace {

constexpr int kRows = 90;

int checks = 0, failures = 0;
void check(const std::string& name, bool ok, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name.c_str(), what.c_str());
}

class Rgb565 : public wxraster::Sink {
public:
    std::vector<uint16_t> px = std::vector<uint16_t>(240 * 240, 0);
    void Put(int x, int y, wxraster::Rgb c) override
    {
        px[y * 240 + x] = (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
    }
};

struct Frame {
    std::vector<uint8_t> bytes, codes;
    wxcrop::Header h;
};
bool Load(const std::string& path, Frame* f)
{
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    f->bytes.clear();
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0) f->bytes.insert(f->bytes.end(), buf, buf + n);
    std::fclose(fp);
    if (wxcrop::Validate(f->bytes.data(), f->bytes.size(), &f->h) != wxcrop::Status::Ok) return false;
    f->codes.assign(f->h.Cells(), 0);
    return wxcrop::DecodeInto(f->bytes.data(), f->bytes.size(), f->codes.data(), f->codes.size()) == wxcrop::Status::Ok;
}

bool SameLabels(const wxscreens::ScreenOut& a, const wxscreens::ScreenOut& b)
{
    if (a.nLabels != b.nLabels) return false;
    for (int i = 0; i < a.nLabels; ++i) {
        const wxscreens::Placed& p = a.labels[i].at;
        const wxscreens::Placed& q = b.labels[i].at;
        if (std::strcmp(a.labels[i].label, b.labels[i].label) != 0 || p.side != q.side || p.box.x0 != q.box.x0 ||
            p.box.y0 != q.box.y0 || p.box.x1 != q.box.x1 || p.box.y1 != q.box.y1)
            return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: test_wx_replay <loop dir>\n");
        return 2;
    }
    std::vector<Frame> frames(15);
    for (int i = 0; i < 15; ++i) {
        const int t = 21 * 60 + 4 + 4 * i;
        char n[40];
        std::snprintf(n, sizeof n, "/shsr-20240521-%02d%02d00.wxc", t / 60, t % 60);
        if (!Load(std::string(argv[1]) + n, &frames[i])) {
            std::printf("cannot load %s (the RIG)\n", n);
            return 2;
        }
    }
    // the warning overlay, from the stored warning scenario on the approaching frame (A8.10)
    static wxscen::Scenario warn;
    if (!wxscen::Build(wxscen::Kind::Warning, frames[5].h, frames[5].codes.data(), &warn)) {
        std::printf("the warning scenario does not build (the RIG)\n");
        return 2;
    }
    static uint32_t pops[256];
    static int order[256];
    for (unsigned i = 0; i < wxtowns::kCount; ++i) pops[i] = wxtowns::kTowns[i].pop;
    wxscreens::RankOrder(pops, (int)wxtowns::kCount, order);
    static wxscreens::ScreenIn win;
    wxscenes::FromScenario(warn, frames[5].h, frames[5].codes.data(), 60, order, &win);

    static wxscreens::Layer layer;   // ONE layer, through all 90 rows, in this order
    static wxrender::View view;
    int rebuilds = 0;
    for (int w = 0; w < 2; ++w) {
        std::printf("== the loop frames, warning overlay %s ==\n", w ? "ON" : "off");
        for (int R : {60, 30, 15}) {
            for (int i = 0; i < 15; ++i) {
                static wxscreens::ScreenIn in;
                in = wxscreens::ScreenIn();
                wxscenes::FromLoopFrame(frames[i].h, frames[i].codes.data(), i, frames[14].h.frameEpoch, R, order, &in);
                if (w) {
                    in.warning = true;
                    for (int k = 0; k < 4; ++k) in.polygon[k] = win.polygon[k];
                    in.wt = win.wt;
                    in.arrow = win.arrow;
                    in.storm = win.storm;
                    in.fromDeg = win.fromDeg;
                }
                Rgb565 full, layered;
                static wxscreens::ScreenOut a, b;
                a = wxscreens::ScreenOut();
                b = wxscreens::ScreenOut();
                wxscreens::Compose(full, in, view, &a);
                rebuilds += wxscreens::ComposeLayered(layered, in, layer, 3, &b) ? 1 : 0;
                int equal = 0;
                for (int p = 0; p < 240 * 240; ++p) equal += full.px[p] == layered.px[p];
                char name[32];
                std::snprintf(name, sizeof name, "RP:%s-%d-%02d", w ? "warn" : "off", R, i);
                check(name, equal == 240 * 240 && SameLabels(a, b),
                      std::to_string(equal) + " of 57600 px equal; labels " + (SameLabels(a, b) ? "equal" : "DIFFER") +
                          "; layer valid " + std::to_string((int)layer.valid));
            }
        }
    }
    std::printf("layer rebuilds: %d (printed, not graded)\n", rebuilds);
    std::printf("%d checks, %d failures\n", checks, failures);
    if (checks != kRows) {
        std::printf("the RIG: %d rows, not %d\n", checks, kRows);
        return 2;
    }
    return failures ? 1 : 0;
}
