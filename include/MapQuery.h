#pragma once
// The basemap request for the configured view (docs/blipscope-map-client-steps.md, step A; F7 in
// docs/blipscope-basemap-client.md). Pure C++11, no Arduino: AircraftManager builds its request here,
// test/host/test_map_fetch.cpp grades it (CA1-CA3, and CA10's zoom row), and test/host/sabotage_map_fetch.py plants one
// break per property into a copy of this file.
//
// BUILT FROM THE STORED CONFIG, never from the view's derived radii: the distance the owner typed, times the unit's exact
// factor, in double. units::FromKm is float, so the radius the radar shows can read 99.99999 mi, and the request must be
// exact to the mm. The radar's ladder decides only WHICH steps exist, by its count: a ladder re-derived in double misses
// the radar's 25 nmi step at 25.5 nmi (registered 2026-11-11), and the levels exist to serve the radar's own steps.
//
//   Rv     = min(distance x factor, 222 km), in mm: floor(km x 1e6 + 0.5)
//   r      = min(222, ceil(Rv_km + 0.8))                 the view plus the 2-dp offset, in whole km (F7)
//   eps    = max(1000, floor(Rv_mm / 800 + 0.5)) mm
//   layers = water,rivers,roads,places[,terrain]          no peaks (Q-MC7 b); terrain only with levels (the route
//                                                         refuses terrain without them)
//   levels = one per top-three ladder step, 1:1 with the screen: px = step / 120, m = ceil(0.8 km / px),
//            N = 240 + 2m, R = floor(N px / 2 + 0.5); kept when N <= 1,024 and 1 km <= R <= 250 km; sorted by R
//   cap    = 32,768 + sum(8 + N^2) bytes                  GetBytes' limit; 32,768 is CAP_BYTES in budget.ts (CA3)
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "RadarZoom.h"

namespace mapquery {

constexpr double kMaxRKm = 222.0;            // MAX_R_KM in proxy/src/basemap/query.ts
constexpr double kMarginKm = 0.8;            // r: the view plus the 2-dp offset's 0.785 km
constexpr double kLevelMarginMm = 800000.0;  // a level's margin past its step, the same 0.8 km
constexpr uint32_t kEpsMinMm = 1000;
constexpr double kEpsDivisor = 800.0;
constexpr double kScreenPx = 120.0;          // the radius spans 120 px: a level is 1:1 with the screen
constexpr uint32_t kLevelBasePx = 240;
constexpr uint32_t kLevelNMax = 1024;
constexpr double kLevelRMinMm = 1000000.0, kLevelRMaxMm = 250000000.0;
constexpr uint32_t kCapBytes = 32768;
constexpr int kLevelsMax = 3;
constexpr size_t kParamsMax = 192;

struct Level {
    uint32_t radiusMm = 0;
    uint32_t n = 0;
};

struct Request {
    uint32_t rvMm = 0;                 // the configured radius, in mm
    int rKm = 0;
    uint32_t epsMm = 0;
    int nLevels = 0;
    Level levels[kLevelsMax];          // sorted by radius
    uint32_t capBytes = 0;
    char params[kParamsMax] = {0};     // r=&eps=&layers=[&levels=]: everything the request sends but lat and lon
};

/** A distance in km, capped at 222 km, in whole mm. Not a positive number -> 0. */
inline uint32_t Mm(double km)
{
    if (!(km > 0)) return 0;
    const double c = km < kMaxRKm ? km : kMaxRKm;
    return (uint32_t)floor(c * 1e6 + 0.5);
}

/** The level 1:1 with a ladder step of `stepMm`, or false when the route would refuse it. */
inline bool LevelFor(uint32_t stepMm, Level* out)
{
    if (stepMm == 0) return false;
    const double px = stepMm / kScreenPx;
    const uint32_t m = (uint32_t)ceil(kLevelMarginMm / px);
    if (m > kLevelNMax) return false;
    const uint32_t n = kLevelBasePx + 2 * m;
    const double r = floor(n * px / 2 + 0.5);
    if (n > kLevelNMax || r < kLevelRMinMm || r > kLevelRMaxMm) return false;
    out->radiusMm = (uint32_t)r;
    out->n = n;
    return true;
}

/** The request for a stored `distance` in the owner's unit (`kmPerUnit` is the unit's exact factor: 1, 1.609344 or
 *  1.852). `ladder` is the radar's own (radarzoom::BuildLadder of the radius its rings show); only its step count is
 *  read. `viewStep` is the radar's current zoom step, taken so that CA10 can hold the contract that a zoom changes
 *  nothing: nothing below reads it. */
inline void Build(double distance, double kmPerUnit, const radarzoom::Ladder& ladder, int viewStep, Request* q)
{
    (void)viewStep;
    *q = Request();
    q->rvMm = Mm(distance * kmPerUnit);
    const int r = (int)ceil(q->rvMm / 1e6 + kMarginKm);
    q->rKm = r > (int)kMaxRKm ? (int)kMaxRKm : r;
    const uint32_t eps = (uint32_t)floor(q->rvMm / kEpsDivisor + 0.5);
    q->epsMm = eps < kEpsMinMm ? kEpsMinMm : eps;

    // The ladder's steps in the owner's unit: its first n - 1 candidates, then the configured radius (RadarZoom.h).
    const int n = ladder.n < 1 ? 0 : (ladder.n > radarzoom::MAX_STEPS ? radarzoom::MAX_STEPS : ladder.n);
    double steps[radarzoom::MAX_STEPS] = {0};
    for (int i = 0; i + 1 < n; ++i) steps[i] = radarzoom::CANDIDATES[i];
    if (n > 0) steps[n - 1] = distance;
    const int top = n - 1;   // THE LEVELS' STEPS: the ladder's top three
    for (int i = top; i >= 0 && i > top - 3; --i) {
        Level l;
        if (q->nLevels < kLevelsMax && LevelFor(Mm(steps[i] * kmPerUnit), &l)) q->levels[q->nLevels++] = l;
    }
    for (int i = 1; i < q->nLevels; ++i)   // sorted by radius, as the route stores them
        for (int j = i; j > 0 && q->levels[j].radiusMm < q->levels[j - 1].radiusMm; --j) {
            const Level t = q->levels[j];
            q->levels[j] = q->levels[j - 1];
            q->levels[j - 1] = t;
        }

    q->capBytes = kCapBytes;
    for (int i = 0; i < q->nLevels; ++i) q->capBytes += 8 + q->levels[i].n * q->levels[i].n;

    int w = snprintf(q->params, kParamsMax, "r=%d&eps=%u&layers=water,rivers,roads,places%s", q->rKm,
                     (unsigned)q->epsMm, q->nLevels ? ",terrain" : "");
    for (int i = 0; i < q->nLevels && w > 0 && (size_t)w < kParamsMax; ++i)
        w += snprintf(q->params + w, kParamsMax - (size_t)w, "%s%u:%u", i ? "," : "&levels=",
                      (unsigned)q->levels[i].radiusMm, (unsigned)q->levels[i].n);
}

}  // namespace mapquery
