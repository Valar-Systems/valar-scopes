#pragma once
// The device's drawJpg, for the web build (_handoffs/p4/web-photos/REG-web-photos.txt, C4): LovyanGFX 1.2.21's
// LGFXBase::draw_jpg at zoom 1, top_left datum and no offsets -- the jpg_push_image path, the only one the firmware
// calls (src/AircraftManager.cpp :11409) -- over the device's OWN decoder, lgfx_tjpgd.c, compiled from the firmware's
// own libdeps, never copied. So a photo the device cannot decode (a progressive JPEG, a truncated body, an HTML page)
// fails here too, and one it can decode lands as the same RGB565 pixels, its Bayer dither (JD_BAYER) included.
//
// std and lgfx_tjpgd.h only, so test/host can build it on its own.
#include <cstddef>
#include <cstdint>

namespace webjpg {

// The pool draw_jpg hands the decoder (LGFXBase.cpp: sz_pool = 3900). A JPEG needing more fails, as on the device.
constexpr size_t kPool = 3900;

// RGB565 from 8-bit channels, truncating -- LovyanGFX's color565 and the shim's, so a raster pixel and a vector
// colour of the same value agree.
constexpr uint16_t Rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Decode `data` and write each decoded block at (x + left, y + top) into `dst` (dstW x dstH, row-major RGB565),
// clipped to the window [x, x + maxW) x [y, y + maxH) and to dst. maxW or maxH <= 0 means "to dst's edge", as
// LovyanGFX reads a 0. False on any decoder error; the blocks drawn before it stay drawn, as they do on the sprite.
bool Draw(const uint8_t* data, size_t len, int32_t x, int32_t y, int32_t maxW, int32_t maxH,
          uint16_t* dst, int32_t dstW, int32_t dstH);

}  // namespace webjpg
