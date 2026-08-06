#!/usr/bin/env python3
"""Natural Earth 1:110m land -> the kLand[] tables in src/anim/FlightAnimation.cpp.

WHY A GENERATOR AND NOT HAND-EDITED ARRAYS. The tables this replaces were 126
hand-drawn whole-degree points for the entire world, ported from the look
target. They had the Gulf of Mexico as land (North America ran Panama straight
to Florida), no Hudson Bay, no British Isles, no Japan, no Madagascar. That is
not a tracing error anyone can nudge out -- it is a point budget of ~21 per
continent -- and the fix is real data, decimated, with the decimation written
down so it can be re-run.

SIMPLIFICATION IS IN PROJECTED PIXELS, NOT DEGREES, which is the only tolerance
that means anything here: the map is 220 px for 360 deg of longitude and 180 px
for 132 deg of latitude, so a degree is worth 0.61 px across and 1.36 px down.
A tolerance in degrees would over-decimate one axis and under-decimate the other
by a factor of two.

THIS MAKES THE OUTPUT PROJECTION-SPECIFIC. If Project() ever changes -- an
azimuthal equidistant map centred on the launch point has been proposed -- re-run
this, because 1 px of tolerance under one projection is not 1 px under another.
That is the intended workflow: regenerate, do not re-tune.

STORED IN HUNDREDTHS OF A DEGREE as int16 (max 18000, fits). At 0.01 deg a point
is accurate to 0.006 px across and 0.014 px down -- far below anything the panel
can show, so the quantisation is free.

  python scripts/gen_coastlines.py ne_110m_land.geojson > coastlines.inc

Source: https://github.com/nvkelso/natural-earth-vector (public domain).
"""
import json
import sys

TOL_PX = 1.0      # Douglas-Peucker tolerance, PROJECTED PIXELS
MIN_EXTENT = 3.0  # drop a landmass smaller than this on both axes


# The projection from FlightAnimation.cpp, in 240-space. Kept identical on
# purpose: simplifying against a different one silently changes the tolerance.
def proj(lon, lat):
    return ((lon + 180.0) / 360.0 * 220.0 + 10.0,
            (72.0 - lat) / 132.0 * 180.0 + 30.0)


def simplify(xs, ys, tol):
    """Douglas-Peucker over parallel arrays; returns the surviving indices.

    Iterative rather than recursive -- a 1,200-point ring is well past the
    default recursion limit and this runs in CI eventually.
    """
    n = len(xs)
    if n < 3:
        return list(range(n))
    keep = [False] * n
    keep[0] = keep[n - 1] = True
    stack = [(0, n - 1)]
    while stack:
        a, b = stack.pop()
        if b <= a + 1:
            continue
        ax, ay, bx, by = xs[a], ys[a], xs[b], ys[b]
        dx, dy = bx - ax, by - ay
        seg = (dx * dx + dy * dy) ** 0.5
        best, bi = -1.0, -1
        for i in range(a + 1, b):
            px, py = xs[i], ys[i]
            if seg < 1e-9:
                d = ((px - ax) ** 2 + (py - ay) ** 2) ** 0.5
            else:
                d = abs(dy * px - dx * py + bx * ay - by * ax) / seg
            if d > best:
                best, bi = d, i
        if best > tol:
            keep[bi] = True
            stack.append((a, bi))
            stack.append((bi, b))
    return [i for i in range(n) if keep[i]]


def main(path):
    geo = json.load(open(path, encoding='utf-8'))
    rings = []

    for feat in geo['features']:
        g = feat['geometry']
        polys = ([g['coordinates']] if g['type'] == 'Polygon'
                 else list(g['coordinates']))
        for poly in polys:
            # Ring 0 only. Later rings are holes (inland seas); the renderer is a
            # single-ring scanline fill and has nowhere to put them.
            ring = poly[0]
            if len(ring) < 4:
                continue
            lons = [float(c[0]) for c in ring]
            lats = [float(c[1]) for c in ring]
            pts = [proj(lo, la) for lo, la in zip(lons, lats)]
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            w = max(xs) - min(xs)
            h = max(ys) - min(ys)
            # Below this it is a speck: it costs a scanline fill and four bytes a
            # point to draw something smaller than the stroke around it.
            if w < MIN_EXTENT and h < MIN_EXTENT:
                continue

            # BELOW THE MAP. The projection covers lat 72..-60 (y 30..210), so a
            # ring whose northernmost point is already past the bottom edge is
            # entirely off it. In practice this is Antarctica, which the look
            # target also omits -- at this projection it is a smear along the
            # bottom that the round face crops anyway, and it is the one ring
            # that spans the full 360 deg of longitude, which the renderer's
            # single-ring scanline fill cannot express.
            if min(ys) > 205.0:
                sys.stderr.write('  dropped (below map): %d pts, lat max %.1f\n'
                                 % (len(ring), max(lats)))
                continue

            # ANTIMERIDIAN. FillGeo has no wrap guard -- only the coastline
            # STROKE does -- so a ring that crosses the seam would fill straight
            # across the map.
            #
            # THE TEST IS THE CONSECUTIVE STEP, NOT THE TOTAL SPAN. A total-span
            # test rejects Eurasia, which runs Portugal to the Bering Strait --
            # 197 deg of perfectly ordinary longitude, no seam crossing, 1,299
            # points, and by far the largest landmass on the map. A ring only
            # wraps if two ADJACENT vertices jump more than 180 deg apart, which
            # is the seam and nothing else. Natural Earth clips at +-180, so
            # nothing in 110m land trips this; if something ever does it needs
            # splitting here, not a guard bolted onto the renderer.
            step = max(abs(lons[i + 1] - lons[i]) for i in range(len(lons) - 1))
            if step > 180.0:
                sys.stderr.write('  WARNING crosses antimeridian: %d pts, max step %.1f\n'
                                 % (len(ring), step))
                continue
            keep = simplify(xs, ys, TOL_PX)
            if len(keep) < 4:
                continue
            rings.append({'pts': [(lons[i], lats[i]) for i in keep],
                          'area': w * h})

    # Biggest first: fill order is draw order, so a small island beside a
    # continent cannot be swallowed by the continent's fill.
    rings.sort(key=lambda r: -r['area'])

    out = []
    total = 0
    largest = 0
    for n, r in enumerate(rings, 1):
        pts = r['pts']
        total += len(pts)
        largest = max(largest, len(pts))
        out.append('const GeoPt kLand%d[] = {' % n)
        line = '   '
        for lo, la in pts:
            tok = ' {%d,%d},' % (round(lo * 100), round(la * 100))
            if len(line) + len(tok) > 92:
                out.append(line)
                line = '   '
            line += tok
        if line.strip():
            out.append(line)
        out.append('};')
    out.append('const Landmass kLand[] = {')
    for n in range(1, len(rings) + 1):
        out.append('    {kLand%d, (int)(sizeof(kLand%d) / sizeof(GeoPt))},'
                   % (n, n))
    out.append('};')

    sys.stdout.write('\n'.join(out) + '\n')
    sys.stderr.write(
        'landmasses : %d\npoints     : %d  (was 126)\n'
        'largest    : %d  -> kMaxPolyPts must be at least this\n'
        'flash      : %.1f KB\n'
        % (len(rings), total, largest, total * 4 / 1024.0))


if __name__ == '__main__':
    main(sys.argv[1])
