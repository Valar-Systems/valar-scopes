#include "FishingModels.h"

#include <algorithm>
#include <math.h>
#include <time.h>

// On-device sun/moon/solunar math for the Fishing edition (Reelscope). Computes sunrise/sunset,
// moonrise/moonset, moon phase + illumination, and the day's major/minor "bite windows" from the sun
// and moon position for the device's lat/lon -- entirely offline (NTP time is the only input).
//
// Solunar theory: fish feed hardest at lunar transit (moon on the meridian, overhead) and its
// anti-transit (moon underfoot) -- the two "major" periods (~2 h) -- and at moonrise/moonset -- the
// two "minor" periods (~1 h). We find those four instants by scanning the local day and place a
// window around each. Precision is a few minutes, which is plenty for a bite window.
//
// Sun uses the same low-precision model as the night auto-dim; moon uses Paul Schlyter's compact
// series (main perturbations only). Not survey-grade, but well within solunar usefulness.
namespace fishing {

namespace {

constexpr double RAD = M_PI / 180.0;

double Rev(double x) { return x - floor(x / 360.0) * 360.0; }

// Days since the J2000.0 epoch (matches the GMST term used across the editions).
double DaysJ2000(double tUnix) { return tUnix / 86400.0 - 10957.5; }

// Greenwich mean sidereal time, degrees.
double GmstDeg(double tUnix)
{
    double gmstH = fmod(18.697374558 + 24.06570982441908 * DaysJ2000(tUnix), 24.0);
    if (gmstH < 0) gmstH += 24.0;
    return gmstH * 15.0;
}

double ObliquityDeg(double tUnix) { return 23.4393 - 3.563e-7 * (tUnix / 86400.0 - 10956.0); }

// Sun's true ecliptic longitude (deg) -- low precision (equation-of-centre through the 2nd term).
double SunLonDeg(double tUnix)
{
    const double d = DaysJ2000(tUnix);
    const double L = Rev(280.460 + 0.9856474 * d);
    const double g = Rev(357.528 + 0.9856003 * d);
    return Rev(L + 1.915 * sin(g * RAD) + 0.020 * sin(2 * g * RAD));
}

// Sun altitude (deg) at a location and time -- the same model the editions use for night auto-dim.
double SunAltDeg(double latDeg, double lonDeg, double tUnix)
{
    const double d = DaysJ2000(tUnix);
    const double lambda = SunLonDeg(tUnix);
    const double eps = 23.439 - 0.0000004 * d;
    const double lam = lambda * RAD, e = eps * RAD;
    const double dec = asin(sin(e) * sin(lam));
    const double ra = atan2(cos(e) * sin(lam), cos(lam)) * 180.0 / M_PI;
    const double H = (GmstDeg(tUnix) + lonDeg - ra) * RAD;
    const double lat = latDeg * RAD;
    double s = sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(H);
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    return asin(s) / RAD;
}

// Moon geocentric ecliptic longitude/latitude (deg), Schlyter's series with the main perturbations.
void MoonEcliptic(double tUnix, double& lonOut, double& latOut)
{
    const double d = tUnix / 86400.0 - 10956.0; // Schlyter day number

    const double N = Rev(125.1228 - 0.0529538083 * d);
    const double i = 5.1454;
    const double w = Rev(318.0634 + 0.1643573223 * d);
    const double a = 60.2666;
    const double e = 0.054900;
    const double M = Rev(115.3654 + 13.0649929509 * d);
    const double Ms = Rev(356.0470 + 0.9856002585 * d);
    const double ws = Rev(282.9404 + 4.70935e-5 * d);

    // Eccentric anomaly (two Newton iterations is ample for the Moon's e).
    double E = M + (180.0 / M_PI) * e * sin(M * RAD) * (1 + e * cos(M * RAD));
    E = E - (E - (180.0 / M_PI) * e * sin(E * RAD) - M) / (1 - e * cos(E * RAD));

    const double xv = a * (cos(E * RAD) - e);
    const double yv = a * (sqrt(1 - e * e) * sin(E * RAD));
    const double v = Rev(atan2(yv, xv) * 180.0 / M_PI);
    const double r = sqrt(xv * xv + yv * yv);

    const double xh = r * (cos(N * RAD) * cos((v + w) * RAD) - sin(N * RAD) * sin((v + w) * RAD) * cos(i * RAD));
    const double yh = r * (sin(N * RAD) * cos((v + w) * RAD) + cos(N * RAD) * sin((v + w) * RAD) * cos(i * RAD));
    const double zh = r * (sin((v + w) * RAD) * sin(i * RAD));

    double lon = Rev(atan2(yh, xh) * 180.0 / M_PI);
    double lat = atan2(zh, sqrt(xh * xh + yh * yh)) * 180.0 / M_PI;

    // Perturbations (deg).
    const double Lm = Rev(N + w + M);   // Moon mean longitude
    const double Ls = Rev(Ms + ws);     // Sun mean longitude
    const double D = Rev(Lm - Ls);      // mean elongation
    const double F = Rev(Lm - N);       // argument of latitude

    lon += -1.274 * sin((M - 2 * D) * RAD);
    lon +=  0.658 * sin(2 * D * RAD);
    lon += -0.186 * sin(Ms * RAD);
    lon += -0.059 * sin((2 * M - 2 * D) * RAD);
    lon += -0.057 * sin((M - 2 * D + Ms) * RAD);
    lon +=  0.053 * sin((M + 2 * D) * RAD);
    lon +=  0.046 * sin((2 * D - Ms) * RAD);
    lon +=  0.041 * sin((M - Ms) * RAD);
    lon += -0.035 * sin(D * RAD);
    lon += -0.031 * sin((M + Ms) * RAD);
    lon += -0.015 * sin((2 * F - 2 * D) * RAD);
    lon +=  0.011 * sin((M - 4 * D) * RAD);

    lat += -0.173 * sin((F - 2 * D) * RAD);
    lat += -0.055 * sin((M - F - 2 * D) * RAD);
    lat += -0.046 * sin((M + F - 2 * D) * RAD);
    lat +=  0.033 * sin((F + 2 * D) * RAD);
    lat +=  0.017 * sin((2 * M + F) * RAD);

    lonOut = Rev(lon);
    latOut = lat;
}

// Moon altitude (deg) at a location and time (geocentric; good enough for rise/set with a small h0).
double MoonAltDeg(double latDeg, double lonDeg, double tUnix)
{
    double lon, lat;
    MoonEcliptic(tUnix, lon, lat);
    const double eps = ObliquityDeg(tUnix) * RAD;
    const double l = lon * RAD, b = lat * RAD;
    const double xe = cos(l) * cos(b);
    const double ye = sin(l) * cos(b) * cos(eps) - sin(b) * sin(eps);
    const double ze = sin(l) * cos(b) * sin(eps) + sin(b) * cos(eps);
    const double ra = atan2(ye, xe) * 180.0 / M_PI;
    const double dec = atan2(ze, sqrt(xe * xe + ye * ye));
    const double H = (GmstDeg(tUnix) + lonDeg - ra) * RAD;
    const double phi = latDeg * RAD;
    double s = sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(H);
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    return asin(s) / RAD;
}

// Linear-interpolate the crossing time between two samples straddling threshold h0.
long CrossTime(double t0, double a0, double t1, double a1, double h0)
{
    if (a1 == a0) return (long)t0;
    return (long)(t0 + (h0 - a0) / (a1 - a0) * (t1 - t0));
}

void PushWindow(std::vector<SolunarWindow>& v, long centre, long dayStart, long dayEnd, int halfSpanSec, bool major)
{
    if (centre <= 0) return;
    if (centre < dayStart || centre > dayEnd) return; // keep windows anchored to today
    SolunarWindow w;
    w.startEpoch = centre - halfSpanSec;
    w.endEpoch   = centre + halfSpanSec;
    w.major = major;
    v.push_back(w);
}

} // namespace

void ComputeSolunar(double lat, double lon, time_t nowUtc, long tzOffsetSec, SolunarDay& out)
{
    out = SolunarDay();
    if (nowUtc < 1600000000) return;

    // UTC epoch of local midnight, and the local day span we scan.
    const long dayStart = (long)nowUtc - (((long)nowUtc + tzOffsetSec) % 86400);
    const long dayEnd = dayStart + 86400;

    constexpr double SUN_H0 = -0.833;  // sun upper-limb refraction/semidiameter
    constexpr double MOON_H0 = 0.125;  // moon rise/set (approx parallax - refraction - semidiameter)
    constexpr int STEP = 300;          // 5-minute scan

    long sunrise = 0, sunset = 0, moonrise = 0, moonset = 0;
    long upperT = 0, lowerT = 0; // moon transit / anti-transit
    double maxMoonAlt = -1e9, minMoonAlt = 1e9;

    double prevSun = SunAltDeg(lat, lon, dayStart);
    double prevMoon = MoonAltDeg(lat, lon, dayStart);
    for (long t = dayStart + STEP; t <= dayEnd; t += STEP) {
        const double sun = SunAltDeg(lat, lon, t);
        const double moon = MoonAltDeg(lat, lon, t);

        if (!sunrise && prevSun < SUN_H0 && sun >= SUN_H0) sunrise = CrossTime(t - STEP, prevSun, t, sun, SUN_H0);
        if (!sunset  && prevSun >= SUN_H0 && sun < SUN_H0) sunset  = CrossTime(t - STEP, prevSun, t, sun, SUN_H0);
        if (!moonrise && prevMoon < MOON_H0 && moon >= MOON_H0) moonrise = CrossTime(t - STEP, prevMoon, t, moon, MOON_H0);
        if (!moonset  && prevMoon >= MOON_H0 && moon < MOON_H0) moonset  = CrossTime(t - STEP, prevMoon, t, moon, MOON_H0);

        if (moon > maxMoonAlt) { maxMoonAlt = moon; upperT = t; }
        if (moon < minMoonAlt) { minMoonAlt = moon; lowerT = t; }

        prevSun = sun;
        prevMoon = moon;
    }

    out.sunriseEpoch = sunrise;
    out.sunsetEpoch = sunset;
    out.moonriseEpoch = moonrise;
    out.moonsetEpoch = moonset;

    // Moon phase + illumination from the moon-sun elongation at local noon.
    const double tNoon = dayStart + 43200;
    double mlon, mlat;
    MoonEcliptic(tNoon, mlon, mlat);
    const double elong = Rev(mlon - SunLonDeg(tNoon)); // 0 = new, 180 = full
    out.moonPhase = (float)(elong / 360.0);
    out.moonIllum = (float)((1.0 - cos(elong * RAD)) / 2.0);

    // Major windows at transit/anti-transit (~2 h), minor windows at moonrise/set (~1 h).
    PushWindow(out.windows, upperT, dayStart, dayEnd, 3600, true);
    PushWindow(out.windows, lowerT, dayStart, dayEnd, 3600, true);
    PushWindow(out.windows, moonrise, dayStart, dayEnd, 1800, false);
    PushWindow(out.windows, moonset,  dayStart, dayEnd, 1800, false);
    std::sort(out.windows.begin(), out.windows.end(),
              [](const SolunarWindow& a, const SolunarWindow& b) { return a.startEpoch < b.startEpoch; });

    // Day rating: strongest near new/full, weakest at the quarters.
    const double phaseDist = fmin(fmin((double)out.moonPhase, fabs(out.moonPhase - 0.5)), fabs(out.moonPhase - 1.0));
    out.dayRating = (int)lround(1.0 + 3.0 * (1.0 - phaseDist / 0.25)); // 4 at new/full, 1 at quarter
    if (out.dayRating < 0) out.dayRating = 0;
    if (out.dayRating > 4) out.dayRating = 4;

    out.valid = true;
}

} // namespace fishing
