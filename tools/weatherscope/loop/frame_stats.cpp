// A10.4's per-frame criteria, measured by the DEVICE's own code (docs/weatherscope-stage3.md A10.4): for each WXC1
// crop given, its nearest echo (wxlogic::NearestEchoMi, from the crop's home, exactly as the state line measures it)
// and, at the 60 mi view, the on-disc map pixels of rows 24-193 (wxrender::CodeAt) and how many of them are dry.
//
//   g++ -std=c++11 -O2 -I include -I src/weatherscope tools/weatherscope/loop/frame_stats.cpp -o frame_stats
//   frame_stats <a.wxc> [<b.wxc> ...]     ->  one line per file: "<nearest mi, or -1> <on-disc> <dry>"
//
// The anchor: the on-disc count in rows 24-193 is the same for every crop, 36,828 (A10.3, counted by CodeAt's own
// centre test). Any other count means the rows or the disc test moved, and the tool refuses (exit 2).
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "WxCrop.h"
#include "WxLogic.h"
#include "WxRender.h"

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: frame_stats <a.wxc> [...]\n");
        return 2;
    }
    for (int a = 1; a < argc; ++a) {
        FILE* f = fopen(argv[a], "rb");
        if (!f) {
            fprintf(stderr, "cannot open %s\n", argv[a]);
            return 2;
        }
        std::vector<uint8_t> b;
        uint8_t buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
        fclose(f);
        wxcrop::Header h;
        if (wxcrop::Validate(b.data(), b.size(), &h) != wxcrop::Status::Ok) {
            fprintf(stderr, "%s: not a valid WXC1 crop\n", argv[a]);
            return 2;
        }
        std::vector<uint8_t> codes((size_t)h.rows * h.cols);
        if (wxcrop::DecodeInto(b.data(), b.size(), codes.data(), codes.size(), &h) != wxcrop::Status::Ok) {
            fprintf(stderr, "%s: did not decode\n", argv[a]);
            return 2;
        }
        wxrender::View v;
        wxrender::BuildView(h, &v);
        const double nearest = wxlogic::NearestEchoMi(h, codes.data(), wxlogic::LatLon{v.homeLat, v.homeLon});
        long onDisc = 0, dry = 0;
        for (int y = 24; y <= 193; ++y)
            for (int x = 0; x < wxrender::kSize; ++x) {
                const uint8_t c = wxrender::CodeAt(h, codes.data(), v, x, y);
                if (c == wxrender::kOffDisc) continue;
                ++onDisc;
                if (c == 0) ++dry;
            }
        if (onDisc != 36828) {
            fprintf(stderr, "ANCHOR: %s has %ld on-disc pixels in rows 24-193, not 36,828\n", argv[a], onDisc);
            return 2;
        }
        printf("%.6f %ld %ld\n", nearest, onDisc, dry);
    }
    return 0;
}
