// The card's photo on the web build (_handoffs/p4/web-photos/REG-web-photos.txt, C2-C5): the sprite that receives
// drawJpg holds real RGB565 pixels, decoded with the device's own decoder (web_jpg.h), and the page draws them from
// here by id. Everything the firmware never asks stops the build with a message rather than draw something the
// device would not (C4).
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include "web_jpg.h"
#include "web_photo.h"

namespace {

struct Seen {
    uint64_t hash;
    std::string path;
};
std::vector<Seen> g_seen;                       // the last few photos fetched: a card shows one at a time
constexpr size_t kSeenMax = 8;
std::map<int, lgfx::LGFX_Sprite*> g_rasters;    // id -> the sprite holding those pixels
int g_nextId = 1;
std::string g_shown;                            // the photo path the last presented frame carried, or ""

[[noreturn]] void Refuse(const char* what)
{
    std::fprintf(stderr, "web renderer: LGFX_Sprite::drawJpg %s -- the firmware never asks this; stopping\n", what);
    std::abort();
}

uint16_t From888(float rgb)
{
    const uint32_t c = (uint32_t)rgb;
    return webjpg::Rgb565((uint8_t)(c >> 16), (uint8_t)((c >> 8) & 0xFF), (uint8_t)(c & 0xFF));
}

}  // namespace

uint64_t webphoto::Hash(const uint8_t* data, size_t len)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len; ++i) { h ^= data[i]; h *= 1099511628211ull; }
    return h;
}

void webphoto::NoteFetched(const std::string& path, const std::string& bytes)
{
    const uint64_t h = Hash(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    for (Seen& s : g_seen)
        if (s.hash == h) { s.path = path; return; }
    if (g_seen.size() == kSeenMax) g_seen.erase(g_seen.begin());
    g_seen.push_back({h, path});
}

std::string webphoto::PathOf(const uint8_t* data, size_t len)
{
    const uint64_t h = Hash(data, len);
    for (const Seen& s : g_seen)
        if (s.hash == h) return s.path;
    return "";
}

void webphoto::Presented(const lgfx::DisplayList& frame)
{
    std::string shown;
    const std::vector<float>& v = frame.ops;
    for (size_t i = 0; i + 2 < v.size(); i += (size_t)v[i]) {
        if ((int)v[i + 1] != lgfx::OP_RASTER) continue;
        auto it = g_rasters.find((int)v[i + 2]);
        if (it != g_rasters.end() && !it->second->source().empty()) shown = it->second->source();
    }
    g_shown = shown;
}

// ---- the sprite's half (declared in shim/LovyanGFX.hpp) ----

bool lgfx::LGFX_Sprite::drawJpg(const uint8_t* data, uint32_t len, int32_t x, int32_t y, int32_t maxWidth,
                                int32_t maxHeight, int32_t offX, int32_t offY, float scale_x, float scale_y, int datum)
{
    if (scale_x != 1.0f || (scale_y != 0.0f && scale_y != 1.0f)) Refuse("with a zoom");
    if (datum != 0) Refuse("with a datum other than top_left");
    if (offX != 0 || offY != 0) Refuse("with an offset");
    if (depth_ != 16) Refuse("on a sprite whose colour depth is not 16");
    if (!created_ || w_ <= 0 || h_ <= 0) return false;
    if (px_.empty()) {
        // The first drawJpg: real pixels, seeded from what the sprite recorded before it -- nothing, or one fill.
        uint16_t seed = 0;
        const std::vector<float>& v = dl_.ops;
        if (!v.empty()) {
            if (v.size() != (size_t)v[0] || (int)v[1] != OP_FILL_SCREEN) Refuse("over vector content");
            seed = From888(v[2]);
        }
        dl_.clear();
        px_.assign((size_t)w_ * (size_t)h_, seed);
        rid_ = g_nextId++;
        g_rasters[rid_] = this;
    } else if (!dl_.ops.empty()) {
        Refuse("over vector content");
    }
    src_ = webphoto::PathOf(data, len);
    return webjpg::Draw(data, len, x, y, maxWidth, maxHeight, px_.data(), w_, h_);
}

bool lgfx::LGFX_Sprite::fillPixels(float rgb)
{
    if (px_.empty()) return false;
    std::fill(px_.begin(), px_.end(), From888(rgb));
    dl_.clear();
    return true;
}

void lgfx::LGFX_Sprite::dropPixels()
{
    if (rid_) g_rasters.erase(rid_);
    rid_ = 0;
    px_.clear();
    px_.shrink_to_fit();
    src_.clear();
}

extern "C" {
// The path of the photo the last presented frame showed, or "": the page draws that photo's credit (C5).
EMSCRIPTEN_KEEPALIVE const char* web_photo_shown() { return g_shown.c_str(); }
// A raster's RGB565 pixels by its id (OP_RASTER), or 0 for an id no sprite holds now.
EMSCRIPTEN_KEEPALIVE const uint16_t* web_raster_ptr(int id)
{
    auto it = g_rasters.find(id);
    return it == g_rasters.end() ? nullptr : it->second->pixels();
}
}
