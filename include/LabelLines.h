#pragma once

/* ============================================================================
 * WHAT THE RADAR WRITES BESIDE A BLIP -- decided here, once, for both callers.
 *
 * THE DEFECT THIS EXISTS FOR is visible in a photograph of a 240 px disc with
 * every info field ticked: fifteen lines of green text per contact, overlapping
 * each other and every neighbouring aircraft, none of it readable. The config
 * page happily offers all fifteen, and nothing between the checkbox and the
 * glass said "three of these will actually fit".
 *
 * TWO CALLERS, AND THAT IS THE OTHER HALF. AircraftManager::DrawAircraftInfo
 * walks the field table and draws; AircraftLabelBox walks THE SAME TABLE AGAIN
 * to compute the collision rectangle, and says so in its own comment: "Same
 * field walk as DrawAircraftInfo". A cap applied in the draw loop alone would
 * leave the box reserving height for lines that are never drawn -- which is
 * exactly the Stats sparkline defect, where a gap was charged for a row that
 * never rendered. So the selection lives in one function and both callers ask
 * it, rather than each applying the rule correctly and separately.
 *
 * PURE, so the order can be asserted on the host. The population question here
 * -- "with everything enabled, which three appear, and in what order?" -- is one
 * a photograph can only sample.
 *
 * THE PRIORITY IS BY KEY, NOT BY INDEX. Indices into AIRCRAFT_INFO_FIELDS would
 * make a future reorder of that table silently re-rank the radar, and nothing
 * would fail. A key that stops existing is a resolution that finds nothing,
 * which the test can see.
 * ==========================================================================*/

#include <cstring>

namespace labellines {

/// The most lines the radar will draw beside one contact.
///
/// Three, measured rather than chosen: at the 1.28" disc's text size a fourth
/// line puts the block taller than the spacing between two contacts at cruise
/// separation, so labels start reading as each other's.
constexpr int MAX_LINES = 3;

/// Draw order, by config key. Everything after these falls back to the field
/// table's own order, so a field nobody ranked still has a defined position.
///
/// Altitude is BAROMETRIC: it is the one that ships on, it is what ATC and
/// every other readout means by "altitude", and geometric is the specialist's
/// field. Both remain selectable; only the ranking prefers one.
constexpr const char* PRIORITY[] = {
    "info-type",      // what it is -- the single most useful word at a glance
    "info-callsign",  // who it is
    "info-baroalt",   // how high
    "info-speed",     // how fast
};
constexpr int PRIORITY_COUNT = (int)(sizeof(PRIORITY) / sizeof(PRIORITY[0]));

/// Choose at most MAX_LINES fields to draw, in draw order.
///
/// `keys[i]`     the config key of field i, in the caller's table order
/// `willDraw[i]` field i is enabled AND formats to something (an enabled field
///               that formats to "" must not consume one of the three)
/// `out[]`       receives field indices, in the order they should be drawn;
///               must have room for MAX_LINES
///
/// Returns how many were chosen, 0..MAX_LINES.
inline int Select(const char* const* keys, const bool* willDraw, int fieldCount, int* out)
{
    int n = 0;
    if (!keys || !willDraw || !out || fieldCount <= 0) return 0;

    const auto already = [&](int idx) {
        for (int k = 0; k < n; ++k)
            if (out[k] == idx) return true;
        return false;
    };

    // 1. the ranked fields, in the order they are ranked
    for (int p = 0; p < PRIORITY_COUNT && n < MAX_LINES; ++p) {
        for (int i = 0; i < fieldCount; ++i) {
            if (!willDraw[i] || !keys[i]) continue;
            if (std::strcmp(keys[i], PRIORITY[p]) != 0) continue;
            if (!already(i)) out[n++] = i;
            break;
        }
    }
    // 2. then the table's own order, for whatever room is left
    for (int i = 0; i < fieldCount && n < MAX_LINES; ++i) {
        if (!willDraw[i] || already(i)) continue;
        out[n++] = i;
    }
    return n;
}

}  // namespace labellines
