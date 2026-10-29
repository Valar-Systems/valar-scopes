"""DN-E's independent reference (docs/weatherscope-stage3.md A11.4, A12.43, A12.45): the sun's geometric elevation by
NOAA's Julian-century algorithm -- the NOAA Solar Calculator spreadsheet's equations, transcribed here -- written as
test/fixtures/daynight/noaa-elev.txt for test/host/test_daynight.cpp to hold include/SolarDim.h's SunElevationDeg
against. A DIFFERENT formula from SolarDim.h's (fractional Julian centuries, the equation of time, the obliquity's
nutation term), so agreement between them is evidence about both.

Geometric: no refraction, as SunElevationDeg has none (the -0.833 deg threshold carries refraction and the
semi-diameter).

The grid (A12.43): 2026-01-01T00:00Z plus every 183 days to 2036, every 97 minutes, latitudes -60, -30, 0, 30 and 60,
longitudes -150, -30 and 90 -- synthetic whole degrees, never a real location. 4,950 rows of
    utc lat lon elev_microdeg
where elev_microdeg is the elevation in millionths of a degree, rounded half up: no decimal point in the file.

    python -I tools/weatherscope/daynight_ref.py --write | --check
"""
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "test", "fixtures", "daynight", "noaa-elev.txt")
T0 = 1767225600   # 2026-01-01T00:00:00Z
LATS = (-60, -30, 0, 30, 60)
LONS = (-150, -30, 90)


def elevation(lat, lon, utc):
    """NOAA Solar Calculator, geometric elevation in degrees, for a UTC epoch (time zone 0)."""
    r, dg = math.radians, math.degrees
    jd = utc / 86400.0 + 2440587.5
    T = (jd - 2451545.0) / 36525.0                                     # Julian centuries from J2000.0
    L0 = (280.46646 + T * (36000.76983 + T * 0.0003032)) % 360.0       # geometric mean longitude
    M = 357.52911 + T * (35999.05029 - 0.0001537 * T)                  # geometric mean anomaly
    e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T)             # orbital eccentricity
    C = (math.sin(r(M)) * (1.914602 - T * (0.004817 + 0.000014 * T))   # equation of centre
         + math.sin(r(2 * M)) * (0.019993 - 0.000101 * T) + math.sin(r(3 * M)) * 0.000289)
    omega = 125.04 - 1934.136 * T
    app = L0 + C - 0.00569 - 0.00478 * math.sin(r(omega))              # apparent longitude
    mean_obl = 23 + (26 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60) / 60
    obl = mean_obl + 0.00256 * math.cos(r(omega))                      # corrected obliquity
    dec = math.asin(math.sin(r(obl)) * math.sin(r(app)))               # declination
    y = math.tan(r(obl / 2)) ** 2
    eot = 4 * dg(y * math.sin(2 * r(L0)) - 2 * e * math.sin(r(M)) + 4 * e * y * math.sin(r(M)) * math.cos(2 * r(L0))
                 - 0.5 * y * y * math.sin(4 * r(L0)) - 1.25 * e * e * math.sin(2 * r(M)))   # minutes
    tst = ((utc % 86400) / 60.0 + eot + 4 * lon) % 1440.0              # true solar time, minutes
    ha = tst / 4 - 180 if tst / 4 >= 0 else tst / 4 + 180              # hour angle, degrees
    cz = math.sin(r(lat)) * math.sin(dec) + math.cos(r(lat)) * math.cos(dec) * math.cos(r(ha))
    return 90.0 - dg(math.acos(max(-1.0, min(1.0, cz))))


def text():
    rows = []
    for day in range(0, 4018, 183):
        for minute in range(0, 1440, 97):
            utc = T0 + day * 86400 + minute * 60
            for lat in LATS:
                for lon in LONS:
                    rows.append("%d %d %d %d" % (utc, lat, lon, int(math.floor(elevation(lat, lon, utc) * 1e6 + 0.5))))
    head = ["# DN-E's reference (docs/weatherscope-stage3.md A12.43, A12.45): the sun's geometric elevation by NOAA's",
            "# Julian-century algorithm, written by tools/weatherscope/daynight_ref.py --write, checked by its --check.",
            "# utc lat lon elev_microdeg -- whole-degree synthetic points; the elevation in millionths of a degree.",
            "# rows %d" % len(rows)]
    return "\n".join(head + rows) + "\n", len(rows)


def main(argv):
    t, n = text()
    if "--write" in argv:
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        open(OUT, "w", encoding="ascii", newline="\n").write(t)
        print(f"wrote {os.path.relpath(OUT, ROOT)}: {n} rows")
        return 0
    if "--check" in argv:
        committed = open(OUT, encoding="ascii").read().replace("\r\n", "\n")
        same = committed == t
        print(f"{os.path.relpath(OUT, ROOT)}: {'identical' if same else 'DIFFERS'} on regeneration ({n} rows)")
        return 0 if same else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
