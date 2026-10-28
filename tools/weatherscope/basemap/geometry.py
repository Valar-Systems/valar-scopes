"""The BMPK frame's geometry (docs/basemap-package-format.md §2, §6 and §13): projection, clipping, simplification and
quantisation, in §2's order. Pure Python, double throughout; every rounding is floor(v + 0.5) written out (§1).

The readings this module makes that §2, §6 and §13 do not pin are named where they are made, as "READING", so they
can be raised for both builders rather than discovered in a golden comparison.
"""
import math

K = 111.0


def project(lon, lat, lat0, lon0, c0):
    """§2: (E, S) in km, east and SOUTH positive."""
    return ((lon - lon0) * K * c0, (lat0 - lat) * K)


# ------------------------------------------------------------------------------------- clipping to the square (§2)

def _lb(a, b, r):
    """Liang-Barsky: the part of segment a->b inside [-r, r]^2 (closed), as ((p, u0), (q, u1)), or None."""
    x0, y0 = a
    dx, dy = b[0] - x0, b[1] - y0
    u0, u1 = 0.0, 1.0
    for p, q in ((-dx, x0 + r), (dx, r - x0), (-dy, y0 + r), (dy, r - y0)):
        if p == 0:
            if q < 0:
                return None
        else:
            t = q / p
            if p < 0:
                if t > u1:
                    return None
                if t > u0:
                    u0 = t
            else:
                if t < u0:
                    return None
                if t < u1:
                    u1 = t
    # READING: an endpoint the clip did not move is the source vertex itself, never re-derived from u, so an
    # in-square vertex keeps its exact projected value.
    p = a if u0 == 0.0 else (x0 + u0 * dx, y0 + u0 * dy)
    q = b if u1 == 1.0 else (x0 + u1 * dx, y0 + u1 * dy)
    return (p, u0), (q, u1)


def clip_line(pts, r):
    """RD5: each in-square run of a line is its own polyline, in along-line order."""
    runs, cur = [], None
    for a, b in zip(pts, pts[1:]):
        c = _lb(a, b, r)
        if c is None:
            if cur:
                runs.append(cur)
            cur = None
            continue
        (p, u0), (q, u1) = c
        if cur is None or u0 > 0.0:
            if cur:
                runs.append(cur)
            cur = [p]
        cur.append(q)
        if u1 < 1.0:
            runs.append(cur)
            cur = None
    if cur:
        runs.append(cur)
    return runs


def clip_ring(ring, r):
    """Sutherland-Hodgman against the square, closed. `ring` without its closing repeat; returns the same.
    READING: the edges in the order west (x >= -r), east (x <= r), north (y >= -r), south (y <= r); the output starts
    where the algorithm's last edge pass starts, as the textbook form does."""
    out = list(ring)
    for axis, sign in ((0, -1), (0, 1), (1, -1), (1, 1)):
        if not out:
            return []
        src, out = out, []
        lim = sign * r

        def inside(p):
            return p[axis] >= lim if sign < 0 else p[axis] <= lim

        def cross(p, q):
            t = (lim - p[axis]) / (q[axis] - p[axis])
            o = 1 - axis
            v = [0.0, 0.0]
            v[axis] = lim
            v[o] = p[o] + t * (q[o] - p[o])
            return (v[0], v[1])

        prev = src[-1]
        for cur in src:
            if inside(cur):
                if not inside(prev):
                    out.append(cross(prev, cur))
                out.append(cur)
            elif inside(prev):
                out.append(cross(prev, cur))
            prev = cur
    return out


# ------------------------------------------------------------------------------------------- simplification (§6)

def _seg_dist(p, a, b):
    """RD7: the distance from p to the SEGMENT a-b, clamped at its ends; RD6: to the point when a == b; RD31's form."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    if dx == 0.0 and dy == 0.0:
        ex, ey = p[0] - a[0], p[1] - a[1]
    else:
        t = ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy)
        t = 0.0 if t < 0.0 else 1.0 if t > 1.0 else t
        ex, ey = p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy)
    return math.sqrt(ex * ex + ey * ey)   # RD31: sqrt(x*x + y*y), x first; never hypot


def dp_keep(pts_m, eps_m):
    """Douglas-Peucker over a path in metres: the indices kept, ascending. The split is the FIRST vertex at the
    maximum distance (§6); a vertex is kept when its distance is > eps (RD7)."""
    n = len(pts_m)
    if n <= 2:
        return list(range(n))
    keep = [False] * n
    keep[0] = keep[n - 1] = True
    stack = [(0, n - 1)]
    while stack:
        i, j = stack.pop()
        best, at = -1.0, -1
        for k in range(i + 1, j):
            d = _seg_dist(pts_m[k], pts_m[i], pts_m[j])
            if d > best:
                best, at = d, k
        if at >= 0 and best > eps_m:
            keep[at] = True
            stack.append((at, j))
            stack.append((i, at))
    return [k for k in range(n) if keep[k]]


def simplify_line(pts_km, eps_m):
    m = [(x * 1000.0, y * 1000.0) for x, y in pts_km]
    return [pts_km[k] for k in dp_keep(m, eps_m)]


def simplify_ring(ring_km, eps_m):
    """RD6: on the path v0 .. vn-1, v0; the closing v0 is then dropped again (the ring is closed implicitly)."""
    if len(ring_km) < 3:
        return list(ring_km)
    path = list(ring_km) + [ring_km[0]]
    m = [(x * 1000.0, y * 1000.0) for x, y in path]
    kept = dp_keep(m, eps_m)
    return [path[k] for k in kept[:-1]]


# ---------------------------------------------------------------------------------------------- quantisation (§2)

class OutOfRange(Exception):
    pass


def quantise(pt_km, r_mm):
    """§2: floor(E_mm / R_mm * 32767 + 0.5). A value past +-32767 is a builder bug: refused, never clamped."""
    out = []
    for v in pt_km:
        q = math.floor(v * 1e6 / r_mm * 32767 + 0.5)
        if q < -32767 or q > 32767:
            raise OutOfRange(q)
        out.append(q)
    return out[0], out[1]


def dedupe(qpts, ring=False):
    """§2 step 5: consecutive duplicates dropped; for a ring, also a last vertex equal to the first (READING: the
    implicit closing edge makes that a consecutive duplicate)."""
    out = []
    for p in qpts:
        if not out or out[-1] != p:
            out.append(p)
    if ring:
        while len(out) > 1 and out[-1] == out[0]:
            out.pop()
    return out


def in_disc_length_m(pts_km, r_km):
    """RD8: the raw length of a line inside the disc of radius r (closed), in metres."""
    total = 0.0
    rr = r_km * r_km
    for a, b in zip(pts_km, pts_km[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        aa = dx * dx + dy * dy
        if aa == 0.0:
            continue
        bb = 2 * (a[0] * dx + a[1] * dy)
        cc = a[0] * a[0] + a[1] * a[1] - rr
        disc = bb * bb - 4 * aa * cc
        if disc <= 0.0:
            continue
        s = math.sqrt(disc)
        t0, t1 = max(0.0, (-bb - s) / (2 * aa)), min(1.0, (-bb + s) / (2 * aa))
        if t1 > t0:
            total += (t1 - t0) * math.sqrt(aa) * 1000.0
    return total


def shoelace2(qpts):
    """Twice the signed shoelace area of integer vertices (exact)."""
    s = 0
    for (x0, y0), (x1, y1) in zip(qpts, qpts[1:] + qpts[:1]):
        s += x0 * y1 - x1 * y0
    return s
