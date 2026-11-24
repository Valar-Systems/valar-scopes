// A host build of the web build's drawJpg (tools/web-renderer/web/web_jpg.cpp) over the device's own decoder, for
// test/host/web_jpg_fidelity.py (_handoffs/p4/web-photos/REG-web-photos.txt, W2/W3).
//
//   web_jpg_cli <in.jpg> <x> <y> <maxW> <maxH> <dstW> <dstH> <seed565> <out.raw>
//
// Writes dstW x dstH RGB565, little-endian, row-major, seeded with <seed565> before the draw, and prints
// "drawJpg <0|1>". Exit 0 when it ran (whatever drawJpg returned), 2 on a usage or file error.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "web_jpg.h"

int main(int argc, char** argv)
{
    if (argc != 10) { std::fprintf(stderr, "usage: web_jpg_cli in x y maxW maxH dstW dstH seed565 out\n"); return 2; }
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    std::vector<uint8_t> in;
    for (int c; (c = std::fgetc(f)) != EOF;) in.push_back((uint8_t)c);
    std::fclose(f);
    const int x = std::atoi(argv[2]), y = std::atoi(argv[3]), mw = std::atoi(argv[4]), mh = std::atoi(argv[5]);
    const int w = std::atoi(argv[6]), h = std::atoi(argv[7]);
    if (w <= 0 || h <= 0) { std::fprintf(stderr, "bad dst size\n"); return 2; }
    std::vector<uint16_t> dst((size_t)w * (size_t)h, (uint16_t)std::strtoul(argv[8], nullptr, 0));
    const bool ok = webjpg::Draw(in.data(), in.size(), x, y, mw, mh, dst.data(), w, h);
    std::FILE* o = std::fopen(argv[9], "wb");
    if (!o) { std::fprintf(stderr, "cannot write %s\n", argv[9]); return 2; }
    for (uint16_t v : dst) { std::fputc(v & 0xFF, o); std::fputc(v >> 8, o); }
    std::fclose(o);
    std::printf("drawJpg %d\n", ok ? 1 : 0);
    return 0;
}
