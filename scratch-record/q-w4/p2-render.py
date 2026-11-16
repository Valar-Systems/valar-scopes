# SCRATCH, read-only: part 2's RENDERER and its counts (docs/basemap-dense-code.md, #652, part 2). FillRing's rule (A10.2) as the device
# applies it (src/weatherscope/WxBasemap.h:80, each ring on its own, the water mask their union); package units placed as Mapper::ToScreen
# places them (WxBasemap.h:48, :53-56: the package's centre and cosine, then the home's view, WxRender.h:6), rotated as the map client's
# AffineFor rotates (include/MapLayer.h: x = C + u cos + w sin, y = C - u sin + w cos). 240 x 240, C = 120.
#   kb1  <synth json> [ref json]   24 views (r, r/2, r/4 x 0..315 deg by 45) of the drawn sea rings against a reference raster of the
#        SAME quantised outer and islands by even-odd, unbridged (the json's q, or ref json's q for a plant). Prints differing pixels per view,
#        their total, and each view's island pixels (in the outer, not in the reference).
#   kb8  <p2 json> <home lat> <home lon>   the closest zoom (8.04672 km): undrawn pixels within 1 px of a 1-degree cell edge whose
#        neighbours 2 px either side, across the edge, are both drawn sea.
#   kb4  <p2 json> <store dir>   on the package's 162 km level grid (242 px over 162,275,520 mm): the sea mask against the store's own
#        terrain (z9 Terrarium tiles): the share of mask pixels at <= 0 m, and the share of <= 0 m pixels outside TIGER's water the mask
#        covers; then the same with the sea rings moved 2 km toward the home (inland), the control.
#   kb5  <p2 json> <home lat> <home lon> <out prefix>   writes <prefix>.rings (pixel coordinates, the closest zoom, north-up) for the C++
#        harness, and <prefix>.py.bin (240 x 240 bytes) from this renderer, for KB5's comparison.
#   pic  <p2 json> <home lat> <home lon> <png>   the closest zoom, north-up: land, water, and sea drawn in its own colour.
# Prints counts only.  python -I p2-render.py <mode> ...
import json, math, sys
import numpy as np

N, C = 240, 120.0
def fill_ring(xs, ys, n=N):
    m = np.zeros((n, n), bool)
    if len(xs) < 3: return m
    xa, ya = np.asarray(xs, float), np.asarray(ys, float); xb, yb = np.roll(xa, -1), np.roll(ya, -1)
    # The crossing from each edge's LOWER-y endpoint, as WxBasemap.h's FillRing computes it from f3fee349061a (#662; the 06:24:25Z
    # ruling): (ya < yb) ? xa + (py - ya) * (xb - xa) / (yb - ya) : xb + (py - yb) * (xa - xb) / (ya - yb), the same operations in order.
    low = ya < yb
    x0, y0, x1, y1 = np.where(low, xa, xb), np.where(low, ya, yb), np.where(low, xb, xa), np.where(low, yb, ya)
    for y in range(max(0, int(math.floor(ya.min())) - 1), min(n, int(math.ceil(ya.max())) + 2)):
        py = y + 0.5
        hit = (ya > py) != (yb > py)
        if not hit.any(): continue
        cx = np.sort(x0[hit] + (py - y0[hit]) * (x1[hit] - x0[hit]) / (y1[hit] - y0[hit]))
        for k in range(0, len(cx) - 1, 2):
            lo, hi = max(math.ceil(cx[k] - 0.5), 0), min(math.ceil(cx[k + 1] - 0.5), n)
            if hi > lo: m[y, lo:hi] = True
    return m

def mapper(lat0p, lon0p, rmm, home_lat, home_lon, r_view_km, rot_deg=0.0):
    c0p = max(math.cos(math.radians(lat0p)), 0.01); c0h = max(math.cos(math.radians(home_lat)), 0.01)
    k = rmm / 1e6 / 32767.0; rlat = r_view_km / 111.0; rlon = r_view_km / (111.0 * c0h)
    cs, sn = math.cos(math.radians(rot_deg)), math.sin(math.radians(rot_deg))
    def f(ring):
        xs, ys = [], []
        for qx, qy in ring:
            lat, lon = lat0p - qy * k / 111.0, lon0p + qx * k / (111.0 * c0p)
            u, w = (lon - home_lon) / rlon * C, (home_lat - lat) / rlat * C
            xs.append(C + u * cs + w * sn); ys.append(C - u * sn + w * cs)
        return xs, ys
    return f

def union(rings, f):
    m = np.zeros((N, N), bool)
    for r in rings: m |= fill_ring(*f(r))
    return m

mode = sys.argv[1]
if mode == "kb1":
    d = json.load(open(sys.argv[2])); ref = json.load(open(sys.argv[3])) if len(sys.argv) > 3 else d
    sea = [d["water"][i] for i in d["seaIdx"]]
    q = ref["q"][0]
    # BRIDGES (the 80164e6096c3 table's clause): a ring edge whose reverse is also an edge of the same ring, in package units (a slit walked
    # out and back). Each view also counts the differing pixels whose centre is MORE than 1 px from every bridge segment ("off-bridge").
    def bridges_of(ring):
        n = len(ring) - 1 if len(ring) > 1 and ring[0] == ring[-1] else len(ring)
        E = [(tuple(ring[i]), tuple(ring[(i + 1) % n])) for i in range(n)]; S = set(E)
        return [e for e in E if (e[1], e[0]) in S]
    bridges = [b for r in sea for b in bridges_of(r)]
    def offbridge(mask, f):
        ys_, xs_ = np.nonzero(mask)
        if not len(xs_): return 0
        if not bridges: return int(len(xs_))
        px, py_ = xs_ + 0.5, ys_ + 0.5; dmin = np.full(len(px), np.inf)
        for a, b in bridges:
            (ax, bx), (ay, by) = f([a, b]); dx, dy = bx - ax, by - ay; L = dx * dx + dy * dy
            t = np.zeros(len(px)) if L == 0 else np.clip(((px - ax) * dx + (py_ - ay) * dy) / L, 0, 1)
            dmin = np.minimum(dmin, np.hypot(px - ax - t * dx, py_ - ay - t * dy))
        return int((dmin > 1.0).sum())
    total, toff, rows = 0, 0, []
    for rv in (162.0, 81.0, 40.5):
        for rot in range(0, 360, 45):
            f = mapper(0.0, 0.0, d["radiusMm"], 0.0, 0.0, rv, rot)
            drawn = union(sea, f)
            outer = fill_ring(*f(q["outer"]))
            refm = outer.copy()
            for h in q["holes"]: refm ^= fill_ring(*f(h))
            diff = int((drawn ^ refm).sum()); island = int((outer & ~refm).sum()); off = offbridge(drawn ^ refm, f)
            total += diff; toff += off; rows.append([rv, rot, diff, island, off])
    print(json.dumps({"mode": "kb1", "views": rows, "total_diff": total, "total_offbridge": toff, "bridges": len(bridges), "islands_q": len(q["holes"])}))
elif mode in ("kb8", "kb5", "pic"):
    d = json.load(open(sys.argv[2])); hl, hn = float(sys.argv[3]), float(sys.argv[4])
    f = mapper(d["lat0Micro"] / 1e6, d["lon0Micro"] / 1e6, d["radiusMm"], hl, hn, 8.04672, 0.0)
    sea = [d["water"][i] for i in d["seaIdx"]]; seaset = set(d["seaIdx"])
    other = [r for i, r in enumerate(d["water"]) if i not in seaset]
    S = union(sea, f)
    if mode == "kb8":
        c0h = max(math.cos(math.radians(hl)), 0.01); rlat, rlon = 8.04672 / 111.0, 8.04672 / (111.0 * c0h)
        seam, edges, px = 0, [], []
        for L in range(math.floor(hn - rlon), math.ceil(hn + rlon) + 1):  # meridians: vertical lines
            x = C + (L - hn) / rlon * C
            if not (2 <= x <= N - 3): continue
            edges.append(f"lon {L}")
            xi = int(math.floor(x))
            for y in range(N):
                for xc in (xi - 1, xi, xi + 1):
                    if 2 <= xc <= N - 3 and abs(xc + 0.5 - x) <= 1.0 and not S[y, xc] and S[y, xc - 2] and S[y, xc + 2]: seam += 1; px.append((xc, y))
        for A in range(math.floor(hl - rlat), math.ceil(hl + rlat) + 1):  # parallels: horizontal lines
            y = C - (A - hl) / rlat * C
            if not (2 <= y <= N - 3): continue
            edges.append(f"lat {A}")
            yi = int(math.floor(y))
            for x in range(N):
                for yc in (yi - 1, yi, yi + 1):
                    if 2 <= yc <= N - 3 and abs(yc + 0.5 - y) <= 1.0 and not S[yc, x] and S[yc - 2, x] and S[yc + 2, x]: seam += 1; px.append((x, yc))
        out = {"mode": "kb8", "edges_in_view": edges, "sea_px": int(S.sum()), "SEAM_CELL": seam}
        # KB8 (a), the owner's ruling of 2026-11-12 15:07Z: a seam pixel counts only where the UNSIMPLIFIED REFERENCE is also sea. The
        # reference is the store's own pieces (argv[5], sea-pieces.json: lon/lat, unsimplified, unplanted) of every 1-degree cell meeting
        # the view, each filled by the same FillRing rule (its outer XOR its islands) and unioned, on this same north-up screen. The old
        # count stays printed (SEAM_CELL); SEAM_CELL_TRUTH is (a)'s.
        if len(sys.argv) > 5:
            P = json.load(open(sys.argv[5])); T = np.zeros((N, N), bool); npieces = 0
            scr = lambda ring: ([C + (lon - hn) / rlon * C for lon, _ in ring], [C - (lat - hl) / rlat * C for _, lat in ring])
            for la0 in range(math.floor(hl - rlat), math.floor(hl + rlat) + 1):
                for lo0 in range(math.floor(hn - rlon), math.floor(hn + rlon) + 1):
                    for p in P.get(f"{la0}/{lo0}", []):
                        m = fill_ring(*scr(p["outer"]))
                        for h in p["holes"]: m ^= fill_ring(*scr(h))
                        T |= m; npieces += 1
            out.update({"reference_pieces": npieces, "reference_sea_px": int(T.sum()), "SEAM_CELL_TRUTH": sum(1 for x_, y_ in px if T[y_, x_])})
        print(json.dumps(out))
    elif mode == "kb5":
        with open(sys.argv[5] + ".rings", "w") as fh:
            rings = [f(r) for r in d["water"]]
            fh.write(f"{len(rings)}\n")
            for xs, ys in rings: fh.write(f"{len(xs)}\n" + "\n".join(f"{x!r} {y!r}" for x, y in zip(xs, ys)) + "\n")
        W = np.zeros((N, N), bool)
        for xs, ys in rings: W |= fill_ring(xs, ys)
        open(sys.argv[5] + ".py.bin", "wb").write(W.astype(np.uint8).tobytes())
        print(json.dumps({"mode": "kb5", "rings": len(rings), "water_px": int(W.sum())}))
    else:
        from PIL import Image
        T = union(other, f)
        img = np.zeros((N, N, 3), np.uint8); img[:] = (205, 190, 150); img[T] = (40, 90, 170); img[S] = (30, 70, 140); img[S & T] = (40, 90, 170)
        Image.fromarray(img).save(sys.argv[5])
        print(json.dumps({"mode": "pic", "sea_px": int(S.sum()), "tiger_px": int(T.sum())}))
elif mode == "kb5r":  # KB5's control (re-registered for run 10): the SAME Python FillRing render as kb5's .py.bin, read back from a .rings file
    L = open(sys.argv[2]).read().split("\n"); nr = int(L[0]); i = 1; W = np.zeros((N, N), bool)
    for _ in range(nr):
        m = int(L[i]); i += 1
        pts = [tuple(map(float, L[i + v].split())) for v in range(m)]; i += m
        W |= fill_ring([p[0] for p in pts], [p[1] for p in pts])
    open(sys.argv[3], "wb").write(W.astype(np.uint8).tobytes())
    print(json.dumps({"mode": "kb5r", "rings": nr, "water_px": int(W.sum())}))
elif mode == "kb4":
    from PIL import Image
    d = json.load(open(sys.argv[2])); sdir = sys.argv[3]
    lat0, lon0 = d["lat0Micro"] / 1e6, d["lon0Micro"] / 1e6; c0 = max(math.cos(math.radians(lat0)), 0.01)
    LV, n = 162_275_520 / 1e6, 242; pix = 2 * LV / n; k = d["radiusMm"] / 1e6 / 32767.0
    def lvl(ring, de=0.0, ds=0.0):  # package units -> level pixels, optionally moved by (de, ds) km
        return [(qx * k + de + LV) / pix for qx, qy in ring], [(qy * k + ds + LV) / pix for qx, qy in ring]
    seaset = set(d["seaIdx"])
    def masks(de=0.0, ds=0.0):
        S, T = np.zeros((n, n), bool), np.zeros((n, n), bool)
        for i, r in enumerate(d["water"]):
            if i in seaset: S |= fill_ring(*lvl(r, de, ds), n=n)
            else: T |= fill_ring(*lvl(r), n=n)
        return S, T
    tiles = {}
    def elev(lat, lon):
        X = (lon + 180.0) / 360.0 * 512 * 256; s = math.sin(math.radians(lat)); Y = (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * 512 * 256
        tx, ty = int(X // 256), int(Y // 256)
        if (tx, ty) not in tiles: tiles[(tx, ty)] = np.asarray(Image.open(f"{sdir}/terrain/9/{tx}/{ty}.png").convert("RGB"), dtype=np.int64)
        a = tiles[(tx, ty)][int(Y % 256), int(X % 256)]
        return a[0] * 256 + a[1] + a[2] / 256.0 - 32768.0
    E = np.zeros((n, n))
    for j in range(n):
        for i in range(n):
            e, s = (i + 0.5) * pix - LV, (j + 0.5) * pix - LV
            E[j, i] = elev(lat0 - s / 111.0, lon0 + e / (111.0 * c0))
    low = E <= 0.0
    # KB4 (A) (the owner's 16:24Z ruling, registered with (B') at cf2f65300e89): cover's base is the <= 0 m pixels outside TIGER's water
    # INSIDE the package's own square (its radius), where a package of that radius can draw; frac is unchanged (the mask scopes it).
    rKm = d["radiusMm"] / 1e6; ctr = (np.arange(n) + 0.5) * pix - LV
    Q = (np.abs(ctr)[None, :] <= rKm) & (np.abs(ctr)[:, None] <= rKm)
    def grade(S, T, inside=True):
        frac = float((S & low).sum() / S.sum()) if S.any() else 0.0
        base = low & ~T & (Q if inside else True)
        cover = float((S & base).sum() / base.sum()) if base.any() else 0.0
        return frac, cover
    S, T = masks(); frac, cover = grade(S, T); _, cover_whole = grade(S, T, inside=False)
    src = None
    if len(sys.argv) > 4:  # KB4 at Seattle (the owner, 18:55Z): the SOURCE's own mask on the same grid, for the loss column
        Pc = json.load(open(sys.argv[4], encoding="utf-8")); REF = np.zeros((n, n), bool)
        llr = lambda r: ([((lon - lon0) * 111.0 * c0 + LV) / pix for lon, lat in r], [((lat0 - lat) * 111.0 + LV) / pix for lon, lat in r])
        dla, dlo = rKm / 111.0 + 1, rKm / (111.0 * c0) + 1
        for key, pieces in Pc.items():
            la, lo = (int(x) for x in key.split("/"))
            if la + 1 < lat0 - dla or la > lat0 + dla or lo + 1 < lon0 - dlo or lo > lon0 + dlo: continue
            for pc in pieces:
                m = fill_ring(*llr(pc["outer"]), n=n)
                for hh in pc["holes"]: m &= ~fill_ring(*llr(hh), n=n)
                REF |= m
        REF &= Q; f_src, c_src = grade(REF, T); src = {"frac_le0": f_src, "cover": c_src, "px": int(REF.sum())}
    ys, xs = np.nonzero(S); ce, cs = (xs.mean() + 0.5) * pix - LV, (ys.mean() + 0.5) * pix - LV
    nrm = math.hypot(ce, cs) or 1.0; de, ds = -2.0 * ce / nrm, -2.0 * cs / nrm  # 2 km from the sea's centroid toward the home (the centre)
    S2, _ = masks(de, ds); frac2, cover2 = grade(S2, T)
    # KB4 re-registered (the 15:44:48Z ruling). (1) THE DECISIVE CONTROL: the sea mask's COMPLEMENT inside the build square (the package's
    # own radius): every pixel the sea rings leave, TIGER's water included, graded by the same two bounds. (2) THE SHIFT LADDER, reported:
    # the mask moved 2, 5, 10 and 20 km from the sea's centroid toward the home, and d*, the first step that breaks a bound (None if none).
    fracC, coverC = grade(Q & ~S, T)
    ladder, dstar = [], None
    for dk in (2.0, 5.0, 10.0, 20.0):
        Sd, _ = masks(-dk * ce / nrm, -dk * cs / nrm); fd, cd = grade(Sd, T); ladder.append([dk, fd, cd])
        if dstar is None and (fd < 0.95 or cd < 0.90): dstar = dk
    print(json.dumps({"mode": "kb4", "sea_px": int(S.sum()), "low_px": int(low.sum()), "frac_le0": frac, "cover": cover, "cover_whole_grid": cover_whole, "source": src,
                      "base_px": int((low & ~T & Q).sum()),
                      "moved_2km": {"de_km": round(de, 3), "ds_km": round(ds, 3), "frac_le0": frac2, "cover": cover2},
                      "complement": {"px": int((Q & ~S).sum()), "frac_le0": fracC, "cover": coverC}, "ladder": ladder, "d_star_km": dstar}))
else:
    print(json.dumps({"error": f"unknown mode {mode}"})); sys.exit(2)
