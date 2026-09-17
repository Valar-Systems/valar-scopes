#pragma once

/* ============================================================================
 * READING THE GLASS OVER THE WIRE.
 *
 * WHY THIS EXISTS. Three display claims in one week needed a phone camera to
 * settle: a sparkline bar drawn through a heading, a QR code whose modules were
 * too small to scan, and a label drawn off the edge of the disc. Every one was a
 * question about pixels, and the only instrument was somebody pointing a camera
 * at a 1.28 inch circle in a dark room.
 *
 * NO SECOND BUFFER IS ALLOCATED. `BANDED_RENDER` is false on every SKU, so
 * BAND_H == SCREEN_SIZE and the "band" sprite is already the whole frame, in
 * PSRAM, at 16bpp -- 115,200 bytes that exist whether or not anyone reads them.
 * This is a pointer to that, not a copy of it.
 *
 * THE SEQUENCE COUNTER IS THE HONEST PART. AsyncWebServer callbacks run on the
 * async_tcp task while the frame is drawn on the loop task, so a read can catch
 * the buffer mid-redraw and return a torn frame -- top half new, bottom half
 * previous. That is usually fine for a diagnostic and occasionally misleading,
 * which is the worst combination: a torn capture of a label at the rim could be
 * read as a rendering bug that is not there.
 *
 * Locking would fix it by stalling the draw loop for the length of a 115 KB
 * socket write, which trades a cosmetic fault for a real one. So the frame is
 * NOT locked; the counter is sampled either side of the read and the answer
 * says whether it moved. A torn frame is labelled rather than prevented, and a
 * caller that cares can simply ask again.
 * ==========================================================================*/

#include <cstdint>

// LGFX_Sprite is a using-alias for lgfx::v1::LGFX_Sprite, not a class, so it
// cannot be forward-declared -- `class LGFX_Sprite;` is a hard error. The
// header comes in whole; every translation unit that uses this already has it.
#include "LGFX.h"

namespace framebuf {

/// The live backbuffer, or nullptr before setup() has created it.
///
/// Never owns it: main.cpp allocates the sprite and keeps it for the life of
/// the device, and this is a borrowed view for the web handler.
LGFX_Sprite* Backbuffer();

/// Frames completed since boot. Read it, read the pixels, read it again: if the
/// value moved, the pixels came from two different frames.
///
/// Monotonic and wrapping. A reader compares for EQUALITY, never for order, so
/// the wrap at 2^32 costs one possibly-mislabelled frame every 2.7 years at
/// 50 fps and needs no special case.
uint32_t Sequence();

/// Called by the render loop once a whole frame has been pushed.
void FrameDone();

/// Publish the backbuffer. Called once from setup(), after createSprite().
void Register(LGFX_Sprite* sprite);

}  // namespace framebuf
