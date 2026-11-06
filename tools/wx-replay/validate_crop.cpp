// RC1 (docs/wx-device.md, "The replay route"): the device's own WXC1 parser, include/WxCrop.h, on each replay crop.
// Prints, per file, the parser's verdict, the frame time, the coarsening and rows x cols -- never the header's
// position. Exit 0 only if every file decodes whole.
//   g++ -std=c++11 -Wall -Wextra -Werror -I include tools/wx-replay/validate_crop.cpp -o validate_crop
//   ./validate_crop <crop.wxc1>...
#include <stdio.h>
#include <vector>
#include "WxCrop.h"

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: validate_crop <crop.wxc1>...\n"); return 2; }
    int bad = 0;
    static uint8_t cells[1u << 20];
    for (int i = 1; i < argc; ++i) {
        FILE* f = fopen(argv[i], "rb");
        if (!f) { printf("%s: cannot open\n", argv[i]); ++bad; continue; }
        std::vector<uint8_t> b;
        int c;
        while ((c = fgetc(f)) != EOF) b.push_back((uint8_t)c);
        fclose(f);
        wxcrop::Header h;
        const wxcrop::Status s = wxcrop::DecodeInto(b.data(), b.size(), cells, sizeof cells, &h);
        unsigned echo = 0;
        if (s == wxcrop::Status::Ok)
            for (uint32_t k = 0; k < h.Cells(); ++k) echo += wxcrop::IsEcho(cells[k]) ? 1 : 0;
        const char* name = argv[i];
        for (const char* p = argv[i]; *p; ++p) if (*p == '/' || *p == '\\') name = p + 1;
        printf("%s: %s, %u B, frame %lu, coarsen %u, %u x %u, echo cells %u\n", name,
               s == wxcrop::Status::Ok ? "Ok" : "REFUSED", (unsigned)b.size(), (unsigned long)h.frameEpoch,
               (unsigned)h.coarsen, (unsigned)h.rows, (unsigned)h.cols, echo);
        if (s != wxcrop::Status::Ok) ++bad;
    }
    return bad ? 1 : 0;
}
