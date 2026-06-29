#pragma once

#include <cstdint>

#include "BandCanvas.h"

// Real seven-segment renderer for the Zulu clock -- actual segment polygons, not a font, so it
// reads as an LED panel. Unlit segments are drawn as a faint "ghost"; lit segments get a soft
// bloom under the bright core. All geometry derives from the cell w/h, so it scales per panel.
namespace eam {

// Draw a single digit (0-9; any other value blanks the digit, ghost-only) in the cell at (x,y).
void DrawSevenSeg(BandCanvas& c, int x, int y, int w, int h, int digit,
                  uint32_t lit, uint32_t ghost, uint32_t bloom);

// Draw the clock colon (two dots) centred in a cell of width w, height h at (x,y).
void DrawColon(BandCanvas& c, int x, int y, int w, int h, bool lit,
               uint32_t litCol, uint32_t ghost);

// Segment thickness used for a given cell height (exposed so the clock can size spacing to match).
int SevenSegThickness(int h);

} // namespace eam
