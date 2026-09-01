#!/usr/bin/env python3
"""Natural Earth 1:50m boundaries -> the kBorder[] tables in src/anim/StateBorders.inc.

Two layers, because one of them is not enough:

    ne_50m_admin_1_states_provinces_lines   state and provincial boundaries
    ne_50m_admin_0_boundary_lines_land      international boundaries

WHY THIS REPLACES A HAND TABLE. The first version of this data was 26 hand-authored
endpoint PAIRS -- two points per border, subdivided in lat/lon at draw time. That is
exactly right for a meridian or a parallel and cannot express anything else, so the
table had INVERTED COVERAGE: Colorado, Wyoming, Utah, Arizona and New Mexico drew
cleanly, and everything from the Cascades west drew nothing at all. On a SEA->BUR
flight -- the view a Pacific Northwest customer looks at every day -- the nearest
drawn state line was several hundred miles east of the route. It rendered well in
the region nobody is watching and not at all in the region the device sits in.

The borders it could not express are rivers and divides: the Columbia (OR/WA), the
Snake (OR/ID above 44.3), the Bitterroot crest (ID/MT), and every Canadian
provincial boundary except the 60th parallel and the prairie meridians.

WHY THE SECOND LAYER, AND HOW THAT WAS FOUND. The admin-1 `_lines` layer holds
INTERNAL boundaries only. Probing it for the four features this generator's
acceptance test names, one came back empty:

    Columbia OR/WA corridor ....... 16 raw vertices
    Snake OR/ID above 44.3N ....... 21 raw vertices
    Bitterroot ID/MT divide ....... 73 raw vertices
    California's southern edge .....  6 raw vertices  <- and all six are the short
                                                        CA/AZ stub either side of
                                                        Yuma, east of -114.73

    US/Mexico, Pacific to Colorado .. 0 raw vertices  <- ABSENT
    Alaska/Yukon at 141 W ........... 0 raw vertices  <- ABSENT

Both absences are international boundaries, so admin-1 is right to omit them and
the fix is the other layer rather than a hand-added row. Without it a device over
southern California draws Nevada, Arizona and the Colorado and then simply stops,
with no line where the country ends.

SELECTING FROM ADMIN-0 GEOGRAPHICALLY, BECAUSE THERE IS NOTHING ELSE TO SELECT ON.
This layer's features carry no country attribution at all in the current release --
every NAME is null, and there are no ADM0_A3_L/R fields -- so the filter is the
FEATURECLA plus a North America box. Kept only when EVERY vertex of a part is
inside the box, not any: Natural Earth splits international boundaries per country
pair, so a whole-part test takes US/Canada and US/Mexico entire and leaves
Mexico/Guatemala (14-18 N) out, with nothing clipped and no partial line ending in
mid-air. Six parts, 709 raw vertices, and every one of them is a boundary of the
United States or Canada.

NO DOUBLE-DRAW BETWEEN THE TWO LAYERS, MEASURED. 3.2 % of admin-0 vertices lie
within 0.02 deg of an admin-1 vertex, rising to 12 % only at a quarter-degree.
Those are the junctions -- where the Montana/North Dakota line terminates on the
49th parallel -- which is exactly what should be there. There is no duplicated
RUN, so the two layers do not decimate the same line twice and diverge.

THE _lines VARIANT CARRIES NO COASTLINE, VERIFIED BEFORE THIS WAS WRITTEN.
Probing the US+CAN features of ne_50m_admin_1_states_provinces_lines:

    CA Pacific coast 34-42N ....... 0 vertices
    FL peninsula coast 25-29N ..... 0 vertices
    Gulf coast TX/LA 27-30N ....... 1 vertex   <- 29.977,-93.794, the Sabine river
                                                  mouth: the TX/LA boundary
                                                  TERMINATING at the coast, not
                                                  following it
    CO/WY 41N corridor ............ 30 vertices  (control)
    OR/WA Columbia corridor ....... 49 vertices  (control)
    ID/MT divide corridor ......... 119 vertices (control)

That matters because the coastline set is 1:110m and this is 1:50m. Had this file
carried coast, every state border along water would have been drawn twice at two
generalisations and diverged visibly. It does not, so there is no double-draw.
`boundary_lines_LAND` excludes maritime boundaries for the same reason.

OUTPUT IS UNIT VECTORS, NOT LON/LAT, for the same reason as the coastlines: the
consumer is an orthographic globe whose inner loop must contain no trig. See
scripts/gen_coastlines.py for the full argument; this file follows its conventions
deliberately rather than inventing a second shape.

    python scripts/gen_state_borders.py \\
        --admin1=ne_50m_admin_1_states_provinces_lines.geojson \\
        --admin0=ne_50m_admin_0_boundary_lines_land.geojson \\
        --tol=0.15 > src/anim/StateBorders.inc
    touch src/anim/StateBorders.cpp     # <-- NOT OPTIONAL, see below

TWO REFUSALS, INHERITED FROM THE COASTLINE GENERATOR'S INCIDENTS RATHER THAN FROM
ITS PROSE. gen_coastlines.py carried TOL_DEG = 0.5 while the header it emitted said
0.15, so a re-run "at a finer tolerance" would have COARSENED the data 3.3x while
printing a header claiming otherwise. And SCons does not track a .inc as a
dependency of the .cpp that includes it, so a regenerated table silently links the
PREVIOUS data -- which cost a full measurement round on 2026-08-30.

So: --tol must be stated and must equal TOL_DEG, the emitted header quotes the value
that actually ran, and the script prints the touch command it requires afterwards.
"""

import json
import math
import sys

TOL_DEG = 0.15   # spherical Douglas-Peucker tolerance -- MUST match the emitted header
D2R = math.pi / 180.0
KEEP_ADM0 = ("USA", "CAN")
MIN_PTS = 2

# The admin-0 selection. See the header for why this is a box and not a country
# filter, and why membership is tested on EVERY vertex of a part.
INTL_CLASS = "International boundary (verify)"

# THE 24 N FLOOR IS LOAD-BEARING AND IT IS THE ONLY THING KEEPING CENTRAL
# AMERICA OUT. This layer has no country attribution -- every NAME is null and
# there are no ADM0_A3_L/R fields -- so nothing but geometry distinguishes the
# US/Mexico border from the Mexico/Guatemala one.
#
# Measured, by dropping the floor to 12 N: seven more parts and 251 more
# vertices are selected -- Mexico/Guatemala, Guatemala/Belize,
# Honduras/Nicaragua, El Salvador and Haiti/Dominican Republic. None of them
# would fail a "is the Columbia present" probe. They would just be lines on the
# map: plausible, wrong, and silent. test/host/test_state_borders.cpp carries
# probes on four of them for exactly that reason.
NA_BOX = (24.0, 84.0, -172.0, -52.0)   # lat0, lat1, lon0, lon1

# The chunk table in include/GlobeProjection.h is sized at build time. Emitting more
# chunks than it holds must FAIL THE BUILD, not clamp -- a clamped table renders
# almost right, which is the failure that does not show on glass. The generator
# emits the count and the header static_asserts on it.
CHUNK_SEGMENTS = 48


def unit(lon, lat):
    c = math.cos(lat * D2R)
    return (c * math.cos(lon * D2R), c * math.sin(lon * D2R), math.sin(lat * D2R))


def perp_angle(p, a, b):
    """Angle from p to the great circle through a and b, in radians."""
    n = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    m = math.sqrt(sum(v * v for v in n))
    if m < 1e-12:
        d = sum(p[i] * a[i] for i in range(3))
        return math.acos(max(-1.0, min(1.0, d)))
    d = abs(sum(p[i] * n[i] for i in range(3))) / m
    return math.asin(max(-1.0, min(1.0, d)))


def simplify(pts, tol):
    """Spherical Douglas-Peucker on unit vectors. Iterative: these lines are long."""
    if len(pts) < 3:
        return pts[:]
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        i, j = stack.pop()
        if j - i < 2:
            continue
        worst, wi = -1.0, -1
        for k in range(i + 1, j):
            d = perp_angle(pts[k], pts[i], pts[j])
            if d > worst:
                worst, wi = d, k
        if worst > tol:
            keep[wi] = True
            stack.append((i, wi))
            stack.append((wi, j))
    return [p for p, k in zip(pts, keep) if k]


def arg(name):
    for a in sys.argv[1:]:
        if a.startswith("--%s=" % name):
            return a.split("=", 1)[1]
    return None


def guard():
    stated = arg("tol")
    if stated is None:
        sys.exit(
            "REFUSED: pass --tol=%.2f to confirm the tolerance you intend.\n"
            "  TOL_DEG in this script is %.2f. State it explicitly so a run that\n"
            "  disagrees with the shipped data cannot be made by accident.\n"
            "  (gen_coastlines.py read 0.5 while its own output header said 0.15.)"
            % (TOL_DEG, TOL_DEG))
    try:
        v = float(stated)
    except ValueError:
        sys.exit("REFUSED: --tol=%s is not a number." % stated)
    if abs(v - TOL_DEG) > 1e-9:
        sys.exit(
            "REFUSED: --tol=%s disagrees with TOL_DEG=%.2f in this script.\n"
            "  One of the two is wrong. Change the constant deliberately, or pass\n"
            "  the value it actually has -- do not let them differ." % (stated, TOL_DEG))


def parts_of(feature):
    g = feature.get("geometry")
    if not g:
        return []
    if g["type"] == "LineString":
        return [g["coordinates"]]
    if g["type"] == "MultiLineString":
        return g["coordinates"]
    return []


def read_admin1(path):
    """State and provincial boundaries of the USA and Canada."""
    out = []
    for f in json.load(open(path, encoding="utf-8"))["features"]:
        if f["properties"].get("ADM0_A3") not in KEEP_ADM0:
            continue
        out += parts_of(f)
    return out


def read_admin0(path):
    """The international boundaries of North America, selected geographically."""
    la0, la1, lo0, lo1 = NA_BOX
    out = []
    for f in json.load(open(path, encoding="utf-8"))["features"]:
        if f["properties"].get("FEATURECLA") != INTL_CLASS:
            continue
        for part in parts_of(f):
            # EVERY vertex, not any: a whole-part test takes a country pair
            # entire or leaves it out, and never produces a line ending in
            # mid-air at the edge of the box.
            if part and all(la0 <= la <= la1 and lo0 <= lo <= lo1
                            for lo, la in part):
                out.append(part)
    return out


def main():
    guard()
    a1, a0 = arg("admin1"), arg("admin0")
    if not a1 or not a0:
        sys.exit("usage: gen_state_borders.py --admin1=<geojson> --admin0=<geojson> "
                 "--tol=%.2f\n"
                 "  BOTH are required. admin-1 alone leaves the US/Mexico border and\n"
                 "  the Alaska/Yukon meridian off the map entirely -- see the header."
                 % TOL_DEG)

    a1_parts, a0_parts = read_admin1(a1), read_admin0(a0)

    # WHAT THE BOX SELECTED, named by extent, on every run. A geographic filter
    # that reports only a count cannot be checked by reading its output -- six
    # parts is six parts whichever six they are, and the failure mode here is an
    # extra country arriving intact rather than a missing one.
    sys.stderr.write("\n  admin-0 parts selected (FEATURECLA + the %g..%g N box):\n"
                     % (NA_BOX[0], NA_BOX[1]))
    for part in sorted(a0_parts, key=len, reverse=True):
        la = [p[1] for p in part]
        lo = [p[0] for p in part]
        sys.stderr.write("    %4d v   lat %6.2f..%6.2f   lon %8.2f..%8.2f\n"
                         % (len(part), min(la), max(la), min(lo), max(lo)))

    # A BOUND SCOPED TO THE ADMIN-0 SELECTION, refused HERE rather than asserted
    # downstream -- because downstream cannot do it. The .inc merges both layers
    # and nothing in it records which line came from where, so a combined extent
    # bound carries ~9 degrees of northern slack: admin-1 reaches 78.69 N in
    # Nunavut while no admin-0 part passes 69.65 N. A spurious high-latitude
    # international part -- exactly what a geographic box admits -- slips straight
    # through a bound that loose.
    #
    # The international boundaries of the USA and Canada lie between the Rio
    # Grande mouth (25.87 N) and the Beaufort coast at 141 W (69.65 N). A part
    # outside that band is a selection failure, and the build must not proceed.
    for part in a0_parts:
        la = [q[1] for q in part]
        if max(la) > 70.5 or min(la) < 25.0:
            sys.exit(
                "REFUSED: an admin-0 part spans lat %.2f..%.2f, outside the\n"
                "  25.0..70.5 N band every US/Canada international boundary lies\n"
                "  in. The box let something through -- check NA_BOX and\n"
                "  FEATURECLA before widening this." % (min(la), max(la)))

    raw_parts = a1_parts + a0_parts
    tol = TOL_DEG * D2R

    lines = []
    raw_total = 0
    for part in raw_parts:
        raw_total += len(part)
        s = simplify([unit(x, y) for x, y in part], tol)
        if len(s) >= MIN_PTS:
            lines.append(s)

    # Longest first, so the most structurally important borders are stroked first
    # and cannot be lost under a later stroke -- same ordering rule as the coasts.
    lines.sort(key=lambda p: -len(p))

    kept = sum(len(p) for p in lines)
    chunks = sum(max(1, (len(p) - 1 + CHUNK_SEGMENTS - 1) // CHUNK_SEGMENTS) for p in lines)

    out = [
        "// GENERATED by scripts/gen_state_borders.py -- do not hand-edit.",
        "// Natural Earth 1:50m admin-1 lines (USA + CAN) + admin-0 land",
        "// boundary lines (North America), spherical Douglas-Peucker at",
        "// %.2f deg. Unit vectors x32767." % TOL_DEG,
        "// Neither layer carries coastline -- verified, see the script header.",
        "",
    ]
    for n, pts in enumerate(lines, 1):
        out.append("const GeoVec kBorder%d[] = {" % n)
        row = []
        for x, y, z in pts:
            row.append("{%d,%d,%d}" % (round(x * 32767), round(y * 32767), round(z * 32767)))
            if len(row) == 4:
                out.append("    " + ", ".join(row) + ",")
                row = []
        if row:
            out.append("    " + ", ".join(row) + ",")
        out.append("};")
    out.append("")
    out.append("const Coastline kBorders[] = {")
    for n, pts in enumerate(lines, 1):
        out.append("    { kBorder%d, %d }," % (n, len(pts)))
    out.append("};")
    out.append("")
    out.append("// Chunks this data will produce at CHUNK=%d. GlobeProjection.h" % CHUNK_SEGMENTS)
    out.append("// static_asserts its table against this, so a set too dense to fit")
    out.append("// FAILS THE BUILD rather than silently rendering almost right.")
    out.append("#define BORDER_CHUNK_COUNT %d" % chunks)
    print("\n".join(out))

    sys.stderr.write(
        "\n=============================================================\n"
        "  lines      : %d\n"
        "  raw verts  : %d\n"
        "  kept verts : %d  (%.1f%% after DP at %.2f deg)\n"
        "  flash      : %d B  (%.1f KB, 6 bytes/vertex)\n"
        "  chunks     : %d at CHUNK=%d\n"
        "\n"
        "  NOW RUN THIS OR THE BUILD WILL LINK THE OLD DATA:\n"
        "      touch src/anim/StateBorders.cpp\n"
        "  SCons does not track a .inc as a dependency of the .cpp.\n"
        "  Then CONFIRM the firmware image size CHANGED.\n"
        "=============================================================\n"
        % (len(lines), raw_total, kept, 100.0 * kept / max(1, raw_total), TOL_DEG,
           kept * 6, kept * 6 / 1024.0, chunks, CHUNK_SEGMENTS))


if __name__ == "__main__":
    main()
