// An airport's mark and code on the radar, as boxes (docs/blipscope-map-client-steps.md, step B, CB8). ONE RULE, READ BY
// BOTH ITS USERS: AircraftManager::DrawAirports draws the mark and the code at these boxes, and the map's place names
// (include/MapPlaces.h) avoid exactly these boxes, so "a name never covers an airport code" cannot drift from what is
// drawn. check_map_draw.py's CB8 holds both call sites. Pure C++11, no Arduino.
#pragma once

namespace radarbox {

// Inclusive, as the Weatherscope's label boxes.
struct Box {
    int x0, y0, x1, y1;
};

// The mark: drawRect(x - 2, y - 2, 5, 5), a 5 x 5 outline. The code: drawString(code, x + 5, y - 3) in the default
// 6 x 8 font, so codeLen x 6 px wide and 8 px tall.
inline void Airport(int x, int y, int codeLen, Box* mark, Box* code)
{
    *mark = Box{x - 2, y - 2, x + 2, y + 2};
    *code = Box{x + 5, y - 3, x + 5 + 6 * codeLen - 1, y + 4};
}

}  // namespace radarbox
