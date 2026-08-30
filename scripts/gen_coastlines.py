#!/usr/bin/env python3
"""Natural Earth 1:110m land -> the kCoast[] tables in src/anim/Coastlines.inc.

WHY A GENERATOR AND NOT HAND-EDITED ARRAYS. The tables this replaces were 126
hand-drawn whole-degree points for the entire world, ported from the look
target. They had the Gulf of Mexico as land (North America ran Panama straight
to Florida), no Hudson Bay, no British Isles, no Japan, no Madagascar. That is
not a tracing error anyone can nudge out -- it is a point budget of ~21 per
continent -- and the fix is real data, decimated, with the decimation written
down so it can be re-run.

OUTPUT IS UNIT VECTORS, NOT LON/LAT, because the consumer is an orthographic
globe and the inner loop must contain no trig. A vertex is three int16 (the unit
vector x32767); per frame it is a 3x3 rotate, a z>0 test to cull the far
hemisphere, and x,y taken as the screen position -- that IS orthographic
projection. The trig all happens here, once, at build time.

SIMPLIFICATION IS SPHERICAL, tolerance in DEGREES OF ARC, because on a globe
there is no privileged axis: at R=110 px one pixel subtends 1/110 rad = 0.52
deg everywhere on the disc (foreshortening near the limb only ever makes a
feature smaller, never larger). The equirectangular version of this script had
to simplify in projected pixels because a degree there was worth 0.61 px across
and 1.36 px down; on a sphere that asymmetry is gone, which is one of several
ways the globe is the simpler target.

TWO RULES FROM THE FLAT-MAP ERA ARE GONE, both non-problems on a sphere:
  * Antarctica was dropped for spanning 360 deg of longitude, which a
    single-ring scanline fill on an equirectangular map cannot express. A globe
    has no seam and no fill, so Antarctica is just more coastline.
  * The antimeridian guard is gone for the same reason -- there is no seam to
    cross. (Its lesson is worth keeping anyway: the first version tested TOTAL
    longitude span and rejected Eurasia, which runs Portugal to the Bering
    Strait across 197 deg without crossing anything. Only the step between
    ADJACENT vertices ever meant anything.)

  python scripts/gen_coastlines.py ne_110m_land.geojson > src/anim/Coastlines.inc

Source: https://github.com/nvkelso/natural-earth-vector (public domain).
REGENERATING THIS DATA REQUIRES TOUCHING THE TU THAT INCLUDES IT.

    python scripts/gen_coastlines.py ne_50m_land.geojson > src/anim/Coastlines.inc
    touch src/anim/Coastlines.cpp        # <-- NOT OPTIONAL

SCons does not track Coastlines.inc as a dependency of Coastlines.cpp, so a
regenerated .inc alone does NOT trigger a recompile: the build succeeds, reports
no error, and silently links the PREVIOUS data.

This cost a full measurement round on 2026-08-30. Three coastline densities were
built and compared, and all three produced the shipped build -- the tell was that
two of the image sizes were byte-identical for vertex counts differing by more
than a thousand. Without that coincidence the numbers would have been reported as
"density barely affects frame cost", which is both wrong and plausible.

"""

import json
import math
import sys

TOL_DEG = 0.5    # spherical Douglas-Peucker tolerance; ~1 px at R=110
MIN_DEG = 1.5    # drop a landmass whose angular diameter is under ~3 px

D2R = math.pi / 180.0


def unit(lon, lat):
    c = math.cos(lat * D2R)
    return (c * math.cos(lon * D2R), c * math.sin(lon * D2R), math.sin(lat * D2R))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def simplify(vs, tol_rad):
    """Douglas-Peucker on the sphere; returns the surviving indices.

    The distance being thresholded is CROSS-TRACK ANGLE: the angle between the
    point and the great circle through the segment's endpoints, which is
    asin(|p . n|) for n the normalised segment normal. That is the honest
    spherical analogue of point-to-line distance, and unlike a lon/lat
    approximation it does not fall apart near the poles -- where a lot of this
    dataset's coastline is.

    Iterative rather than recursive: a 1,300-point ring is past the default
    recursion limit.
    """
    n = len(vs)
    if n < 3:
        return list(range(n))
    keep = [False] * n
    keep[0] = keep[n - 1] = True
    stack = [(0, n - 1)]
    while stack:
        a, b = stack.pop()
        if b <= a + 1:
            continue
        nrm = cross(vs[a], vs[b])
        m = math.sqrt(dot(nrm, nrm))
        best, bi = -1.0, -1
        for i in range(a + 1, b):
            if m < 1e-12:
                # Endpoints coincident or antipodal: no great circle is defined,
                # so fall back to angular distance from the first endpoint.
                d = math.acos(max(-1.0, min(1.0, dot(vs[i], vs[a]))))
            else:
                d = abs(math.asin(max(-1.0, min(1.0, dot(vs[i], nrm) / m))))
            if d > best:
                best, bi = d, i
        if best > tol_rad:
            keep[bi] = True
            stack.append((a, bi))
            stack.append((bi, b))
    return [i for i in range(n) if keep[i]]


def main(path):
    geo = json.load(open(path, encoding='utf-8'))
    tol = TOL_DEG * D2R
    rings = []

    for feat in geo['features']:
        g = feat['geometry']
        polys = ([g['coordinates']] if g['type'] == 'Polygon'
                 else list(g['coordinates']))
        for poly in polys:
            # Ring 0 only. Later rings are holes (inland seas); the renderer
            # strokes coastlines and does not fill, so a hole is just a second
            # coastline it has nowhere to put.
            ring = poly[0]
            if len(ring) < 4:
                continue
            vs = [unit(float(c[0]), float(c[1])) for c in ring]

            # Angular size, from the centroid. Below ~3 px of diameter a
            # landmass is smaller than the stroke drawn around it.
            cxx = sum(v[0] for v in vs) / len(vs)
            cyy = sum(v[1] for v in vs) / len(vs)
            czz = sum(v[2] for v in vs) / len(vs)
            cm = math.sqrt(cxx * cxx + cyy * cyy + czz * czz)
            if cm < 1e-9:
                continue
            c = (cxx / cm, cyy / cm, czz / cm)
            rad = max(math.acos(max(-1.0, min(1.0, dot(v, c)))) for v in vs)
            if rad * 2.0 < MIN_DEG * D2R:
                continue

            keep = simplify(vs, tol)
            if len(keep) < 4:
                continue
            rings.append({'vs': [vs[i] for i in keep], 'rad': rad})

    # Biggest first: draw order, so a small island beside a continent is
    # stroked after it and cannot be lost under a later stroke.
    rings.sort(key=lambda r: -r['rad'])

    out = ['// GENERATED by scripts/gen_coastlines.py -- do not hand-edit.',
           '// Natural Earth 1:110m land, spherical Douglas-Peucker at %.2f deg.' % TOL_DEG,
           '// Unit vectors x32767. See the script header for why this shape.',
           '']
    total = 0
    largest = 0
    for n, r in enumerate(rings, 1):
        vs = r['vs']
        total += len(vs)
        largest = max(largest, len(vs))
        out.append('const GeoVec kCoast%d[] = {' % n)
        line = '   '
        for x, y, z in vs:
            tok = ' {%d,%d,%d},' % (round(x * 32767), round(y * 32767), round(z * 32767))
            if len(line) + len(tok) > 96:
                out.append(line)
                line = '   '
            line += tok
        if line.strip():
            out.append(line)
        out.append('};')
    out.append('const Coastline kCoast[] = {')
    for n in range(1, len(rings) + 1):
        out.append('    {kCoast%d, (int)(sizeof(kCoast%d) / sizeof(GeoVec))},'
                   % (n, n))
    out.append('};')

    sys.stdout.write('\n'.join(out) + '\n')
    sys.stderr.write(
        'rings      : %d\nvertices   : %d  (was 126 lon/lat points)\n'
        'largest    : %d\nflash      : %.1f KB  (6 bytes/vertex)\n'
        % (len(rings), total, largest, total * 6 / 1024.0))


if __name__ == '__main__':
    main(sys.argv[1])
