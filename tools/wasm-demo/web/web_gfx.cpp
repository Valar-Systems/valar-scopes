// Blipscope web demo: the display's half of the LovyanGFX stand-in -- the panel size, the touch the page
// reports, and the hand-off of each finished frame's display list to the page.
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "variants/Variant.h"
#include <emscripten.h>
#include "glcdfont.h"     // LovyanGFX's Font0 bitmap (Adafruit glcdfont, BSD): the page draws glyphs from it

namespace {
lgfx::DisplayList g_presented;     // the last frame pushed to the "panel"
bool g_touchDown = false;
int32_t g_touchX = 0, g_touchY = 0;
uint8_t g_brightness = 255;
float g_stats[8];
}

bool lgfx::LGFX_Device::init()
{
    w_ = h_ = variant::SCREEN_SIZE;
    return true;
}
void lgfx::LGFX_Device::setBrightness(uint8_t b) { brightness_ = b; g_brightness = b; }
bool lgfx::LGFX_Device::getTouch(int32_t* x, int32_t* y)
{
    if (!g_touchDown) return false;
    if (x) *x = g_touchX;
    if (y) *y = g_touchY;
    return true;
}

void lgfx::LGFX_Sprite::pushSprite(int32_t, int32_t)
{
    // The frame is done: it becomes what the page draws, and the sprite starts the next one empty.
    std::swap(g_presented, dl_);
    dl_.clear();
}

void lgfx::LGFX_Sprite::pushSprite(LGFX_Sprite* dst, int32_t x, int32_t y)
{
    // Composite this sprite's commands into dst, offset by (x, y), in paint order. Which fields are
    // coordinates depends on the op: [len, op, colour, ...].
    if (!dst) return;
    const std::vector<float>& v = dl_.ops;
    const size_t textBase = dst->dl_.text.size();
    dst->dl_.text += dl_.text;
    dst->dl_.pixelCalls += dl_.pixelCalls;
    dst->dl_.imageCalls += dl_.imageCalls + 1;
    for (size_t i = 0; i < v.size();) {
        const int len = (int)v[i];
        std::vector<float> o(v.begin() + i, v.begin() + i + len);
        const int op = (int)o[1];
        auto at = [&](int k, float d) { if (k < len) o[k] += d; };
        switch (op) {
            case OP_FILL_SCREEN: o = {7, (float)OP_FILL_RECT, o[2], (float)x, (float)y, (float)w_, (float)h_}; break;
            case OP_LINE: at(3, x); at(4, y); at(5, x); at(6, y); break;
            case OP_FILL_TRI: at(3, x); at(4, y); at(5, x); at(6, y); at(7, x); at(8, y); break;
            case OP_TEXT: at(4, x); at(5, y); o[8] += (float)textBase; break;
            default: at(3, x); at(4, y); break;   // every other op starts with its x, y
        }
        dst->dl_.ops.insert(dst->dl_.ops.end(), o.begin(), o.end());
        i += len;
    }
}

extern "C" {
EMSCRIPTEN_KEEPALIVE void demo_touch(int down, int x, int y) { g_touchDown = down != 0; g_touchX = x; g_touchY = y; }
EMSCRIPTEN_KEEPALIVE const float* demo_list_ptr() { return g_presented.ops.data(); }
EMSCRIPTEN_KEEPALIVE int demo_list_len() { return (int)g_presented.ops.size(); }
EMSCRIPTEN_KEEPALIVE const char* demo_text_ptr() { return g_presented.text.data(); }
EMSCRIPTEN_KEEPALIVE int demo_text_len() { return (int)g_presented.text.size(); }
EMSCRIPTEN_KEEPALIVE int demo_screen_size() { return variant::SCREEN_SIZE; }
EMSCRIPTEN_KEEPALIVE int demo_brightness() { return g_brightness; }
EMSCRIPTEN_KEEPALIVE const unsigned char* demo_font_ptr() { return font; }
// [pixel calls, image calls, ops, text bytes] for the last frame: what is NOT a vector shape.
EMSCRIPTEN_KEEPALIVE const float* demo_stats_ptr()
{
    g_stats[0] = (float)g_presented.pixelCalls;
    g_stats[1] = (float)g_presented.imageCalls;
    g_stats[2] = (float)g_presented.ops.size();
    g_stats[3] = (float)g_presented.text.size();
    return g_stats;
}
}
