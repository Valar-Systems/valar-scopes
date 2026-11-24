#pragma once
// The card's photo on the web build (_handoffs/p4/web-photos/REG-web-photos.txt, C2/C5): which fetched bytes came
// from which path, and which photo the last presented frame carried -- what web_photo_shown() tells the page, so the
// page can draw that photo's credit beside the canvas.
#include <cstddef>
#include <cstdint>
#include <string>

namespace lgfx { struct DisplayList; }

namespace webphoto {

uint64_t Hash(const uint8_t* data, size_t len);                    // FNV-1a 64

// C2: Get() fetched these bytes for this path (the path as the firmware asked, under the photo prefix).
void NoteFetched(const std::string& path, const std::string& bytes);
// The path whose fetched bytes are exactly these, or "" -- drawJpg names its source by its own bytes.
std::string PathOf(const uint8_t* data, size_t len);

// C5: the frame just handed to the page. Its rasters name the photo it shows, read from what was DRAWN.
void Presented(const lgfx::DisplayList& frame);

}  // namespace webphoto
