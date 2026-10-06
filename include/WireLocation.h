#pragma once

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// PRECISE ON DEVICE, COARSE ON THE WIRE (v15, 2026-10-06).
//
// The device keeps its location at 4 dp (~11 m: CoordParse::Format), because the
// overhead decision, the radar projection, airport distances and Follow all need
// it. A request that LEAVES the device never carries more than 2 dp (~1.1 km).
// Every location-bearing request goes through this file; nothing else formats a
// coordinate for the wire.
//
// Why each request needs nothing more than this:
//   /blips    the Worker snaps the centre to a 0.05 deg tile and r UP to a bucket,
//             then pads its upstream query by 4 km. The 2-dp cells nest exactly
//             inside those tiles (every tile edge is an x.xx5 rounding boundary),
//             so a 2-dp centre hits the SAME cache entry. r is NOT widened: one
//             rounding step on a 40 km radius would jump the 80 km bucket.
//   /airports the Worker caches by lat/lon to 0.1 deg; the device clips airports
//             to its own box.
//   /enrich   the AIRCRAFT's position, for the route corridor test (tens of km).
//             Rounded too: enrichment runs nearest-first, so the positions it asks
//             about cluster around the device.
//   OpenSky   a real box. Built from the ROUNDED centre plus the radius, widened by
//             one rounding step (0.01 deg lat, 0.01/cos(lat) deg lon), edges rounded
//             OUTWARD to 2 dp -- so no aircraft near the true edge is lost -- and the
//             results are clipped back to the TRUE box on the device.
namespace wireloc {

constexpr double STEP_DEG = 0.01;   // one 2-dp rounding step

// Half away from zero (C round), and never -0.
inline double Round2(double v)
{
    const double r = round(v * 100.0) / 100.0;
    return r == 0.0 ? 0.0 : r;
}

// A value already on the 2-dp grid, as the wire string. "-0.00" is written "0.00".
inline String Fmt2(double v)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%.2f", v);
    if (strcmp(buf, "-0.00") == 0) return String("0.00");
    return String(buf);
}

// The centre for /blips, /airports, and the aircraft position for /enrich.
inline String Center(double v) { return Fmt2(Round2(v)); }

struct Box { double lamin, lamax, lomin, lomax; };

// The OpenSky query box. radLat/radLon are the device's half-extents in degrees
// (radLon already scaled by cos(lat)).
inline Box OpenSkyBox(double lat, double lon, double radLat, double radLon)
{
    const double cLat = Round2(lat), cLon = Round2(lon);
    double c = cos(cLat * (3.14159265358979323846 / 180.0));
    if (c < 0.01) c = 0.01;                       // same pole guard as the projection
    const double padLon = STEP_DEG / c;
    Box b;
    b.lamin = floor((cLat - radLat - STEP_DEG) * 100.0) / 100.0;
    b.lamax = ceil ((cLat + radLat + STEP_DEG) * 100.0) / 100.0;
    b.lomin = floor((cLon - radLon - padLon) * 100.0) / 100.0;
    b.lomax = ceil ((cLon + radLon + padLon) * 100.0) / 100.0;
    if (b.lamin < -90.0)  b.lamin = -90.0;
    if (b.lamax >  90.0)  b.lamax =  90.0;
    if (b.lomin < -180.0) b.lomin = -180.0;
    if (b.lomax >  180.0) b.lomax =  180.0;
    return b;
}

// Inside the device's TRUE box (the clip the local feed has always used).
inline bool InTrueBox(double aLat, double aLon, double lat, double lon, double radLat, double radLon)
{
    return !(aLat < lat - radLat || aLat > lat + radLat || aLon < lon - radLon || aLon > lon + radLon);
}

} // namespace wireloc
