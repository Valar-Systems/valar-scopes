// K13 (docs/wx-opuk.md, Q7): one opuk crop through the device's weather layer to an image, on the host -- looked at,
// not graded. Built against the device branch's headers (include/WxCrop.h, include/WxLayer.h, include/WxS2.h and
// src/weatherscope/WxPalette.h at the commit named in docs/wx-opuk.md), which this branch does not carry:
//
//   git archive <wx-device sha> include src/weatherscope | tar -x -C <dir>
//   g++ -std=c++11 -O1 -I<dir>/include tools/wx/render_opuk_crop.cpp -o render
//   render <crop.wxc> <homeLat> <homeLon> <rKm> <size> <out.ppm>
//   python -I tools/wx/ppm_to_png.py <out.ppm> <out.png>
//
// The view is ProjectAt's (the firmware's VIEW, which test/wx-crop.test.ts holds equal to proxy/src/wx/crop.ts):
// 111 km a degree, each half-extent clamped to [0.001, 2.0] degrees, cos(lat) floored at 0.01; no rotation. Off the
// round screen and the background (code 0) are black. Look A, the shipping image's. Prints counts, never a position.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "WxCrop.h"
#include "WxLayer.h"
#include "WxS2.h"

int main(int argc, char** argv)
{
    if (argc != 7) { std::printf("usage: render <crop.wxc> <homeLat> <homeLon> <rKm> <size> <out.ppm>\n"); return 2; }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::printf("cannot read %s\n", argv[1]); return 2; }
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);

    std::vector<uint8_t> codes(406 * 406);
    wxcrop::Header h;
    const wxcrop::Status s = wxcrop::DecodeInto(data.data(), data.size(), codes.data(), codes.size(), &h);
    if (s != wxcrop::Status::Ok) { std::printf("decode %s\n", wxcrop::StatusName(s)); return 2; }

    const double lat = std::atof(argv[2]), lon = std::atof(argv[3]), rKm = std::atof(argv[4]);
    const int size = std::atoi(argv[5]);
    double cosLat = std::cos(lat * M_PI / 180.0);
    if (cosLat < 0.01) cosLat = 0.01;
    const auto clamp = [](double d) { return d < 0.001 ? 0.001 : d > 2.0 ? 2.0 : d; };
    wxlayer::View v;
    v.homeLat = lat; v.homeLon = lon;
    v.rLat = clamp(rKm / 111.0); v.rLon = clamp(rKm / (111.0 * cosLat));
    v.size = size;

    std::vector<uint8_t> px((size_t)size * size);
    wxlayer::Build(h, codes.data(), v, px.data());

    FILE* o = std::fopen(argv[6], "wb");
    if (!o) { std::printf("cannot write %s\n", argv[6]); return 2; }
    std::fprintf(o, "P6\n%d %d\n255\n", size, size);
    long echo = 0, nocov = 0, dry = 0, off = 0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const uint8_t c = px[(size_t)y * size + x];
            if (c == wxlayer::kOffDisc) ++off; else if (c >= 1 && c <= 13) ++echo; else if (c == 14) ++nocov; else if (c == 0) ++dry;
            const uint32_t rgb = c == wxlayer::kOffDisc ? 0 : wxs2::Pixel888(c, x, y, wxs2::Look::A);
            const uint8_t p[3] = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb};
            std::fwrite(p, 1, 3, o);
        }
    std::fclose(o);
    std::printf("crop %ux%u k=%u; %dx%d px: echo %ld, no coverage %ld, dry %ld, off the disc %ld\n",
                h.rows, h.cols, h.coarsen, size, size, echo, nocov, dry, off);
    return 0;
}
