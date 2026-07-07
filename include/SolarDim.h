#pragma once

#include <math.h>
#include <time.h>

// Low-precision solar elevation (degrees) at lat/lon for a UTC epoch -- enough to
// drive the night auto-dim (sun below -0.833 deg = civil horizon). One shared copy
// for every edition manager, so they all dim identically and a fix lands once; the
// radar keeps its separate NOAA-equation implementation in AircraftManager, and the
// Fishing edition's solunar math in src/fishing/Solunar.cpp is deliberately its own.
inline float SunElevationDeg(double latDeg, double lonDeg, time_t utc)
{
    const auto d2r = [](double d) { return d * M_PI / 180.0; };
    const double n = (double)utc / 86400.0 - 10957.5; // days since J2000.0 (2000-01-01 12:00 UTC)
    double L = fmod(280.460 + 0.9856474 * n, 360.0);
    double g = fmod(357.528 + 0.9856003 * n, 360.0);
    const double lambda = L + 1.915 * sin(d2r(g)) + 0.020 * sin(d2r(2 * g));
    const double eps = 23.439 - 0.0000004 * n;
    const double lam = d2r(lambda), e = d2r(eps);
    const double dec = asin(sin(e) * sin(lam));
    const double ra = atan2(cos(e) * sin(lam), cos(lam)); // radians
    double gmst = fmod(18.697374558 + 24.06570982441908 * n, 24.0);
    if (gmst < 0) gmst += 24.0;
    const double lstDeg = gmst * 15.0 + lonDeg;
    const double Hdeg = lstDeg - ra * 180.0 / M_PI;
    const double H = d2r(Hdeg);
    const double lat = d2r(latDeg);
    double sinEl = sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(H);
    if (sinEl > 1) sinEl = 1;
    if (sinEl < -1) sinEl = -1;
    return (float)(asin(sinEl) * 180.0 / M_PI);
}
