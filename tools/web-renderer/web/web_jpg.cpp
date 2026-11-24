// The device's drawJpg at zoom 1, over the device's own decoder (see web_jpg.h).
#include "web_jpg.h"
#include "lgfx_tjpgd.h"   // LovyanGFX 1.2.21 src/lgfx/utility, from the firmware's libdeps

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

struct Session {
    const uint8_t* data;
    size_t len, pos;
    int32_t x, y;                  // where the image's (0, 0) lands
    int32_t cx0, cy0, cx1, cy1;    // the window, already clipped to dst: [cx0, cx1) x [cy0, cy1)
    uint16_t* dst;
    int32_t dstW;
};

// draw_jpg's read_data over a memory buffer: copy, or skip when the decoder passes no buffer.
uint32_t In(void* dev, uint8_t* buf, uint32_t n)
{
    Session* s = static_cast<Session*>(dev);
    const size_t left = s->len - s->pos;
    if (n > left) n = (uint32_t)left;
    if (buf) std::memcpy(buf, s->data + s->pos, n);
    s->pos += n;
    return n;
}

// jpg_push_image: one decoded block, RGB888 (lgfx_tjpgd JD_FORMAT 0: R, G, B), pushed at (x + left, y + top) and
// clipped to the window -- pushImage clips to the clip rect draw_jpg's begin() set.
uint32_t Out(void* dev, void* bitmap, JRECT* r)
{
    Session* s = static_cast<Session*>(dev);
    const uint8_t* p = static_cast<const uint8_t*>(bitmap);
    const int32_t w = (int32_t)(r->right - r->left + 1), h = (int32_t)(r->bottom - r->top + 1);
    for (int32_t j = 0; j < h; ++j) {
        const int32_t py = s->y + (int32_t)r->top + j;
        for (int32_t i = 0; i < w; ++i, p += 3) {
            const int32_t px = s->x + (int32_t)r->left + i;
            if (px < s->cx0 || px >= s->cx1 || py < s->cy0 || py >= s->cy1) continue;
            s->dst[(size_t)py * (size_t)s->dstW + (size_t)px] = webjpg::Rgb565(p[0], p[1], p[2]);
        }
    }
    return 1;
}

}  // namespace

bool webjpg::Draw(const uint8_t* data, size_t len, int32_t x, int32_t y, int32_t maxW, int32_t maxH,
                  uint16_t* dst, int32_t dstW, int32_t dstH)
{
    if (!data || !dst || dstW <= 0 || dstH <= 0) return false;
    if (maxW <= 0) maxW = dstW - x;
    if (maxH <= 0) maxH = dstH - y;
    Session s{data, len, 0, x, y, std::max<int32_t>(x, 0), std::max<int32_t>(y, 0),
              std::min<int32_t>(x + maxW, dstW), std::min<int32_t>(y + maxH, dstH), dst, dstW};
    if (s.cx0 >= s.cx1 || s.cy0 >= s.cy1) return false;   // begin() refuses a window with nothing in it
    uint8_t* pool = static_cast<uint8_t*>(std::malloc(kPool));
    if (!pool) return false;
    lgfxJdec jd;
    bool ok = lgfx_jd_prepare(&jd, In, pool, (uint_fast16_t)kPool, &s) == JDR_OK;
    if (ok) ok = lgfx_jd_decomp(&jd, Out, 0) == JDR_OK;   // scale 0: JPEG_DIV_NONE, zoom 1
    std::free(pool);
    return ok;
}
