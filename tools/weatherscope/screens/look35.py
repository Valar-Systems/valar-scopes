"""The light theme's look, for Daniel (docs/weatherscope-stage3.md A12.19, A12.22): ref35's P rendering of the Bend scenes
from bend.bmpk by the format doc's §9, in the dark theme and in light Q39 candidates (a) and (b), with A12.22's frozen
values; and TH1-TH3 (TH4 is ref35 --k --check, R0 and R0b). Prints no coordinate.

    python -I tools/weatherscope/screens/look35.py <out dir>
"""
import collections, math, os, sys
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))
    spec.loader.exec_module(m)
    return m


geo35 = _load("geo35", os.path.join(HERE, "geo35.py"))
r = geo35.ref35
BEND = os.path.join(r.WX, "bend-loop")
PKG = os.path.join(r.WX, "basemap", "bend.bmpk")
K, OFFSET_PT = 111.0, -420   # §2's k; Pacific (PDT) on 2024-06-02

# --------------------------------------------------------------------------------------- the themes (A12.22, frozen)
PAPER, LIT = (234, 231, 223), (240, 237, 228)
LIGHT_INK = dict(bg=PAPER, band=PAPER, dim=(96, 92, 86), white=(38, 38, 40), label=(38, 38, 40), halo=(236, 233, 226),
                 ring=(150, 146, 138), ringtext=(96, 92, 86), home=(38, 38, 40), peak=(92, 70, 50),
                 orange=None)   # not ruled: A12.22 names no light orange, and no scene here draws one (a stale clock or a
                                # warning band would), so a use of it raises rather than inventing a colour


def ramp(shadow):
    return [tuple(int(math.floor(s + (l - s) * i / 36 + 0.5)) for s, l in zip(shadow, LIT)) for i in range(37)]


def light_pal(cand):
    pal = dict(r.PAL)
    pal[13] = (0, 0, 0)
    pal.update({5: (135, 135, 0), 6: (153, 127, 0), 7: (215, 120, 0)} if cand == "a" else {5: (135, 135, 0), 6: (155, 128, 0)})
    return pal


THEMES = {
    # dark: geo35.base_p's P base exactly -- terrain (g, g, g + 2) with g = 10 + level, and its water, river and road
    "dark": dict(ink=r.DARK_INK, pal=r.PAL, nothing=r.BLACK, terrain=[(10 + i, 10 + i, 12 + i) for i in range(37)],
                 water=geo35.WATER_FILL, river=geo35.WATER_LINE, road=geo35.ROAD),
    "light-a": dict(ink=LIGHT_INK, pal=light_pal("a"), nothing=PAPER, terrain=ramp((201, 198, 189)),
                    water=(171, 204, 237), river=(96, 146, 206), road=(178, 150, 112)),
    "light-b": dict(ink=LIGHT_INK, pal=light_pal("b"), nothing=PAPER, terrain=ramp((168, 165, 156)),
                    water=(171, 204, 237), river=(96, 146, 206), road=(178, 150, 112)),
}


# --------------------------------------------------------------------------------------- the package by §9
def package_scene(view, rl_mm=98_974_656):
    """(index image 240 x 240 by A10.2's table: 0 nothing, 1-37 terrain level + 1, 38 water, 39 river, 40 road;
    peaks [(name, lat, lon)] in rank order; places [(name, lat, lon, pop)])."""
    b = open(PKG, "rb").read()
    pkg = geo35.decode_bmpk(b)
    lat0m, lon0m, rmm, _ = pkg["header"]
    lat0, lon0 = lat0m / 1e6, lon0m / 1e6
    c0 = max(math.cos(lat0 * (math.pi / 180)), 0.01)

    def ll(q):
        e, s = q[0] / 32767 * rmm / 1e6, q[1] / 32767 * rmm / 1e6
        return lat0 - s / K, lon0 + e / (K * c0)

    def xy(q):
        la, lo = ll(q)
        return view.project(la, lo)

    sec = dict()
    levels = {}
    for t, items in pkg["sections"]:
        if t == 6:
            levels[items[0]] = items
        else:
            sec[t] = items
    img = [[0] * r.SIZE for _ in range(r.SIZE)]
    rl, n, vals = levels[rl_mm]
    pix = 2 * rl / 1e6 / n
    for y in range(r.SIZE):
        lat_p = view.home[0] + (r.C - (y + 0.5)) / r.C * view.rLat
        for x in range(r.SIZE):
            lon_p = view.home[1] + ((x + 0.5) - r.C) / r.C * view.rLon
            E, S = (lon_p - lon0) * K * c0, (lat0 - lat_p) * K
            i, j = math.floor(E / pix + n / 2), math.floor(S / pix + n / 2)
            if 0 <= i < n and 0 <= j < n:
                img[y][x] = 1 + (36 * vals[j * n + i]) // 255
    for ring in sec.get(1, []):
        for (x, y) in geo35.centre_fill([xy(q) for q in ring]):
            img[y][x] = 38
    rnd = r.Rounder("P")
    for code, items in ((39, [pts for _, pts in sec.get(2, ([], []))[1]]), (40, [pts for _, pts in sec.get(3, [])])):
        for pts in items:
            p = [xy(q) for q in pts]
            for a, c in zip(p, p[1:]):
                nn = int(max(abs(c[0] - a[0]), abs(c[1] - a[1]))) + 1
                for t in range(nn + 1):
                    xx, yy = rnd.point(a[0] + (c[0] - a[0]) * t / nn, a[1] + (c[1] - a[1]) * t / nn, "line")
                    if r.in_map(xx, yy):
                        img[yy][xx] = code
    peaks = [(name, *ll((qx, qy))) for qx, qy, elev, name in sec.get(4, [])]
    places = [(name, *ll((qx, qy)), pop) for qx, qy, pop, name in sec.get(5, [])]
    return img, peaks, places


def colours(img, th):
    pal = {0: th["nothing"], 38: th["water"], 39: th["river"], 40: th["road"]}
    pal.update({1 + i: c for i, c in enumerate(th["terrain"])})
    return [[pal[v] for v in row] for row in img]


# --------------------------------------------------------------------------------------- the scenes
def render(scene, theme, drop_town=None):
    th = THEMES[theme]
    if scene.startswith("bend"):
        h, codes = r.decode(os.path.join(BEND, "synthetic-clear.wxc" if scene == "bend-dry" else "shsr-20240602-200600.wxc"))
        view = r.View(h, 60)
        img, peaks, places = package_scene(view)
        base = colours(img, th)
        towns = places
        pop = lambda t: t[3]
        clock = r.clock_text(h["epoch"], OFFSET_PT)
        line, col = r.state_rule(h, codes, 0)
    else:   # dallas-approaching, supplementary: no package (the base is the theme's "nothing")
        names = sorted(n for n in os.listdir(r.LOOP) if n.startswith("shsr-") and n.endswith(".wxc"))
        frames = [r.decode(os.path.join(r.LOOP, n)) for n in names]
        near = [r.nearest_echo(hh, cc, r.centre(hh), None) for hh, cc in frames]
        ia = max(i for i, e in enumerate(near) if e and e[0] <= 15.0)
        h, codes = frames[ia]
        view = r.View(h, 60)
        img, peaks = None, None
        base = [[th["nothing"]] * r.SIZE for _ in range(r.SIZE)]
        towns = r.load_towns_now()
        pop = lambda t: t[3]
        clock = r.clock_text(h["epoch"])
        line, col = r.state_rule(h, codes, 0)
    if drop_town:   # TH1's control: a placed town removed must change the placement
        towns = [t for t in towns if r.label_name(t[0]) != drop_town]
    state = dict(line=line, colour=col, timeText=clock, timeInk=r.DIMTXT)
    plog = []
    scr, placed = r.compose(h, codes, view, state, towns, pop, clock, r.Rounder("P"), base=base, peaks=peaks,
                            glyph=geo35.glyph_p, peak_log=plog, pal=th["pal"], ink=th["ink"])
    return scr, placed, plog, img


def png(scr, path, scale=1):
    from PIL import Image
    im = Image.frombytes("RGB", (r.SIZE, r.SIZE), scr.rgb888())
    if scale > 1:
        im = im.resize((r.SIZE * scale, r.SIZE * scale), Image.NEAREST)
    im.save(path)


def main(argv):
    out = argv[1] if len(argv) > 1 else None
    if not out:
        print(__doc__)
        return 2
    os.makedirs(out, exist_ok=True)
    shots, ok = {}, True
    for scene in ("bend-dry", "bend-rain", "dallas-approaching"):
        for theme in (("dark", "light-a", "light-b") if scene != "dallas-approaching" else ("light-a", "light-b")):
            shots[(scene, theme)] = render(scene, theme)
            scr = shots[(scene, theme)][0]
            png(scr, os.path.join(out, f"{scene}-{theme}.png"))
            png(scr, os.path.join(out, f"{scene}-{theme}-x3.png"), 3)
    # TH1: placement is colour-blind. Towns by (label, side, box) as K records them; peaks by (name, anchor) from the
    # peak log, which carries no side or box (geo35 reads its 3-tuple); so also the pixels drawn in label or peak ink,
    # which hold every town dot and label, every peak glyph and label, whatever side each took.
    # The mask is the map band less the home marker: in light, label ink is also the home marker's and a near state
    # line's, so outside that region the colour no longer names the role.
    home = set(r.HOME)

    def text_mask(shot, theme):
        ink = THEMES[theme]["ink"]
        cols = {ink["label"], ink["peak"]}
        return frozenset((x, y) for y in range(r.TOP_BAND, r.BOT_BAND) for x in range(r.SIZE)
                         if (x, y) not in home and shot[0].px[y][x] in cols)

    def placement(shot, theme):
        return (tuple(shot[1]), tuple(shot[2]), text_mask(shot, theme))

    for scene in ("bend-dry", "bend-rain"):
        sets = {t: placement(shots[(scene, t)], t) for t in ("dark", "light-a", "light-b")}
        same = len(set(sets.values())) == 1
        ok &= same
        print(f"  {'ok  ' if same else 'FAIL'}  TH1 {scene}: towns {len(sets['dark'][0])}, peaks "
              f"{[n for n, _, _ in sets['dark'][1]]}, {len(sets['dark'][2])} label/peak-ink pixels -- "
              f"{'identical in dark, light (a), light (b)' if same else 'DIFFER'}")
    ref = placement(shots[("bend-dry", "light-a")], "light-a")
    gone = ref[0][0][0]
    ctl = placement(render("bend-dry", "light-a", drop_town=gone), "light-a")
    seen = [part for i, part in enumerate(("towns", "peaks", "mask")) if ctl[i] != ref[i]]
    good = seen == ["towns", "mask"]
    ok &= good
    print(f"  {'ok  ' if good else 'FAIL'}  TH1 CONTROL: without the first placed town ({gone}) the parts that change are "
          f"{seen} (expected ['towns', 'mask'])")
    # TH2: palette closure
    def strays(scr, theme):
        th = THEMES[theme]
        allowed = ({v for v in th["ink"].values() if v is not None} | {th["nothing"], th["water"], th["river"], th["road"]}
                   | set(th["terrain"]) | set(th["pal"].values()))
        return collections.Counter(p for row in scr.px for p in row if p not in allowed)

    for (scene, theme), (scr, _, _, _) in shots.items():
        if theme == "dark":
            continue
        stray = strays(scr, theme)
        ok &= not stray
        print(f"  {'ok  ' if not stray else 'FAIL'}  TH2 {scene} {theme}: {sum(stray.values())} pixels outside the theme's colours" +
              ("" if not stray else f" {dict(stray.most_common(5))}"))
    n_ctl = sum(strays(shots[("bend-dry", "dark")][0], "light-a").values())
    ok &= n_ctl > 0
    print(f"  {'ok  ' if n_ctl > 0 else 'FAIL'}  TH2 CONTROL: the dark bend-dry render against light (a)'s colours: "
          f"{n_ctl} pixels outside (must be > 0)")
    # TH3: (a) against (b) differ exactly at terrain levels below 36
    for scene in ("bend-dry", "bend-rain"):
        a, b = shots[(scene, "light-a")][0].px, shots[(scene, "light-b")][0].px
        ramp_a = set(THEMES["light-a"]["terrain"][:36])
        diff = {(x, y) for y in range(r.SIZE) for x in range(r.SIZE) if a[y][x] != b[y][x]}
        expect = {(x, y) for y in range(r.SIZE) for x in range(r.SIZE) if a[y][x] in ramp_a}
        good = diff == expect
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'}  TH3 {scene}: {len(diff)} pixels differ; {len(expect)} show a terrain level below 36" +
              ("" if good else f"; differ-not-terrain {len(diff - expect)}, terrain-not-differ {len(expect - diff)}"))
    print("TH1-TH3: " + ("all hold" if ok else "OFF-PREDICTION: stop and report"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
