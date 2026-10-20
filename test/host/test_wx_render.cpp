// Weatherscope Stage 1's renderer (src/weatherscope/WxRender.h) on the host, before any board.
//
//   test_wx_render.exe <repo>/test/fixtures/wx
//
// Decodes the KDFW crop with WxCrop.h, maps all 240 x 240 pixels with WxRender.h, and compares the
// result BYTE FOR BYTE with kdfw-60mi.screen.bin, which test/fixtures/wx/render_ref.py wrote from the
// independent Python decoder (its sha256 is prediction B9 in docs/weatherscope-stage1.md). This is an
// extra check, not in the frozen table: B9 itself is the BOARD's dump. Passing here means a mismatch on
// the board would be the board's (libm, float) and not the code's.
//
// Exit: 0 ok, 1 a check failed, 2 the rig is broken.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "WxCrop.h"
#include "WxRender.h"   // -I src/weatherscope, so a sabotaged copy can stand in

static int failures = 0;
static void check(bool ok, const std::string& what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

static bool ReadFile(const std::string& path, std::vector<uint8_t>* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->insert(out->end(), buf, buf + n);
    std::fclose(f);
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 2) { std::printf("usage: test_wx_render <repo>/test/fixtures/wx\n"); return 2; }
    const std::string dir = argv[1];
    std::printf("Weatherscope Stage 1 renderer (WxRender.h) vs render_ref.py\n");
    std::vector<uint8_t> crop, ref;
    if (!ReadFile(dir + "/kdfw-60mi.wxc", &crop) || !ReadFile(dir + "/kdfw-60mi.screen.bin", &ref) ||
        ref.size() != (size_t)wxrender::kSize * wxrender::kSize) {
        std::printf("FAIL: cannot read the KDFW crop or its reference screen (rig)\n");
        return 2;
    }
    wxcrop::Header h;
    std::vector<uint8_t> codes(36192);
    const wxcrop::Status s = wxcrop::DecodeInto(crop.data(), crop.size(), codes.data(), codes.size(), &h);
    check(s == wxcrop::Status::Ok, std::string("KDFW decodes: ") + wxcrop::StatusName(s));
    if (s != wxcrop::Status::Ok) return 1;

    static wxrender::View v;
    wxrender::BuildView(h, &v);
    std::vector<uint8_t> screen((size_t)wxrender::kSize * wxrender::kSize);
    int disc = 0, echo = 0, outside = 0, differ = 0, adjacent = 0;
    for (int y = 0; y < wxrender::kSize; ++y)
        for (int x = 0; x < wxrender::kSize; ++x) {
            const uint8_t c = wxrender::CodeAt(h, codes.data(), v, x, y);
            screen[(size_t)y * wxrender::kSize + x] = c;
            if (c != wxrender::kOffDisc) {
                ++disc;
                echo += wxcrop::IsEcho(c);
                const int r = v.row[y], k = v.col[x];
                outside += r < 0 || r >= h.rows || k < 0 || k >= h.cols;
            }
            const uint8_t want = ref[(size_t)y * wxrender::kSize + x];
            if (c != want) {
                ++differ;
                // the escape hatch's shape: does a neighbouring cell of the crop hold the reference's code?
                bool near = false;
                for (int dr = -1; dr <= 1; ++dr)
                    for (int dc = -1; dc <= 1; ++dc) {
                        const int r = v.row[y] + dr, k = v.col[x] + dc;
                        if (r >= 0 && r < h.rows && k >= 0 && k < h.cols && codes[(size_t)r * h.cols + k] == want) near = true;
                    }
                adjacent += near;
            }
        }
    check(differ == 0, std::to_string(57600 - differ) + "/57600 pixels identical to render_ref.py" +
                           (differ ? " (" + std::to_string(adjacent) + " of the " + std::to_string(differ) + " differences are one cell away)" : ""));
    check(disc == 45244, std::to_string(disc) + " disc pixels (reference: 45,244)");
    check(echo == 11025, std::to_string(echo) + " echo pixels (reference: 11,025, 24.4 %)");
    check(outside == 0, std::to_string(outside) + " disc pixels whose cell is outside the crop (reference: 0)");
    std::printf("%s\n", failures ? "WX RENDER TESTS FAILED" : "all WX render checks passed");
    return failures ? 1 : 0;
}
