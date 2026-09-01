// The state and province border data, owned by exactly one translation unit --
// the same arrangement as src/anim/Coastlines.cpp, and for the same reason.
//
// WHAT THIS REPLACED, because the replacement is the point. Until this file
// existed the borders were 26 hand-authored endpoint PAIRS in
// include/StateBorders.h, subdivided in lat/lon at draw time. Two points can
// express a meridian or a parallel and nothing else, so the table had INVERTED
// COVERAGE: Colorado, Wyoming, Utah, Arizona and New Mexico drew cleanly, and
// everything from the Cascades west drew nothing at all. It rendered well in the
// region nobody is watching and not at all in the region the device sits in.
//
// The borders two points cannot express are the rivers and the divides -- the
// Columbia (OR/WA), the Snake (OR/ID above 44.3), the Bitterroot crest (ID/MT),
// California's southern edge, and every Canadian provincial boundary that is not
// the 60th parallel or a prairie meridian.
//
// The .inc names bare `GeoVec` and `Coastline`, so it is included INSIDE
// namespace globeproj where those resolve to the shared definitions -- exactly
// as the coastline data is. No aliases, no edits to the generated file.
//
// STRAIGHT CHORDS, NOT LAT/LON SUBDIVISION, and that is a measurement rather
// than a preference. The hand table HAD to interpolate in lat/lon: two endpoints
// on a parallel describe a SMALL circle, and a geodesic between them bows north
// of it by ~30 km across a 10 degree span. Douglas-Peucker removes the problem at
// the source, because it only drops a vertex whose deviation from the chord is
// under the tolerance -- so the kept polyline is within 0.15 deg of the original
// by construction, whatever shape the original had.
//
// Measured over all 324 segments of this set: the largest disagreement between
// the great-circle chord and the lat/lon path is 0.098 deg, on the Manitoba/
// Ontario line, and no segment exceeds 0.15 deg. At the R=500 clamp one pixel is
// 0.115 deg, so the worst case is 0.85 px. These therefore draw through the same
// loop as the coastline under the same rule, and the subdivision is gone rather
// than retained "to be safe" -- a step count of 40 per segment over 324 segments
// would have cost more projections than the whole coastline.
//
// A build that never calls Borders() drops the 2.7 KB under --gc-sections.

#include "../../include/GlobeProjection.h"

namespace globeproj {
namespace {
#include "StateBorders.inc"
}  // namespace

// THE BUILD FAILS HERE rather than the map rendering almost right.
//
// Both chunk tables are .bss and share one budget (see MAX_CH). The border table
// is sized EXACTLY by the count the generator computed from the data it emitted,
// so no border chunk can ever be dropped -- there is no overflow branch to take.
// What can still go wrong is the two tables together outgrowing the budget, and
// a denser border set is the likely way in: chunks scale with vertices, and the
// generator prints its chunk count on every run.
//
// Caught at compile time, with the numbers in the message, because the runtime
// alternative is a clamp -- and a clamped table renders a map that is almost
// right, which is the one failure that does not show up on glass.
static_assert(BORDER_CHUNK_COUNT + COAST_MAX_CH <= MAX_CH,
              "border + coastline chunk tables exceed MAX_CH in "
              "include/GlobeProjection.h. Raise the budget deliberately and "
              "check the .bss cost first -- 32 B per chunk.");

const Coastline* Borders() { return kBorders; }
int BorderCount() { return (int)(sizeof(kBorders) / sizeof(kBorders[0])); }

const ChunkCap* BorderChunks(int& count)
{
    static ChunkCap chunks[BORDER_CHUNK_COUNT];
    static int built = -1;
    if (built < 0)
        built = BuildChunks(Borders(), BorderCount(), /*closed=*/false,
                            chunks, BORDER_CHUNK_COUNT);
    count = built;
    return chunks;
}

}  // namespace globeproj
