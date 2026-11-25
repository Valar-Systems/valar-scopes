// THE FRAME BENCH (REG-W-sep Corrections 3 and 4; bench/fp-port-wsep only, never merged). ONE copy of W's WHOLE
// per-contact path, in its as-built shape. Included by src/WsepA.cpp, src/WsepB.cpp and src/WsepI.cpp, each of which
// names WSEP_NS and is empty outside the separation image.
//
// The two shipping headers are included INSIDE that namespace, their text untouched. So this copy owns its own
// markerdraw::Aircraft, outlinew::Raster and MaskSink functions and its own stamp tables, with `noinline` kept, and
// shares no symbol with the shipping path or with another copy. The system headers they include are included first,
// so only their own text lands in the namespace. Arduino.h comes first, as in AircraftManager.cpp, so `radians` is
// Arduino's in both.
//
// Not a header: no include guard, and nothing but the three bench sources includes it.
#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include "BandCanvas.h"
#include "models/TrackedAircraft.h"

namespace WSEP_NS {
#include "OutlineW.h"
#include "MarkerDraw.h"

// W's per-contact call as the radar's call site makes it (AircraftManager.cpp, DrawRadar's outline), with the marker
// lambda empty: the caller ends the outline span and draws the marker, as on the 8-copy path.
__attribute__((noinline)) void WContact(BandCanvas& backbuffer, int x, int y, const TrackedAircraft& tracked,
                                        int radarUpDeg, bool displayTriangles)
{
    (void)outlinew::Draw(
        backbuffer, x, y, 0u,
        [&](outlinew::Mask& m, int cx, int cy) {
            if (displayTriangles)
                markerdraw::Aircraft(m, cx, cy, tracked.state.category, tracked.state.trueTrack, radarUpDeg, 0u);
            else m.fillCircle(cx, cy, 3, 0u);
        },
        [&]() {});
}
}  // namespace WSEP_NS
