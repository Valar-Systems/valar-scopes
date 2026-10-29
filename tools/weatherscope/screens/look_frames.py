"""The light candidates' bench select (docs/weatherscope-stage3.md A12.27, A12.29, A12.30): the screens as ROLE-INDEX
frames, one byte a pixel naming the role that drew it, and one 66-entry palette per look (dark, light (a), light (b)),
for the look viewer (src/weatherscope/look/), which shows them on the glass exactly. Prints no coordinate.

    python -I tools/weatherscope/screens/look_frames.py <out dir>

Writes <out dir>/look-frames.bin (20 x 57,600), look-pal.bin (3 x 66 x RGB888, in LOOKS order) and look-expect.json
(each (look, frame)'s expected sprite sha256: RGB565, high byte first, as palette_ref.py derives the bench sprite).
Checks LF1', LF2-LF4 and C3b; exits 1 on any failure. sabotage_look_frames.py plants into run() in memory.

  F0      Bend dry, synthetic-clear, its state line -- A12.23's "Bend dry"
  F1      Bend rain, shsr-20240602-200600, its state line -- A12.23's "Bend rain"
  F2-F16  the Bend loop's 15 frames, oldest first, with A9.10's loop band
  F17     Dallas approaching, no package -- A12.23's supplementary scene
  F18     a synthetic Bend crop with a 20 x 20 px patch of each code 1-15, terrain on (14 drawn as the hatch)
  F19     the same crop, terrain off (water, rivers and roads kept)
"""
import collections, hashlib, json, os, sys
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))
    spec.loader.exec_module(m)
    return m


look = _load("look35", os.path.join(HERE, "look35.py"))
r, geo35 = look.r, look.geo35
LOOKS = ("dark", "light-a", "light-b")
INK_ROLES = ("bg", "band", "dim", "white", "label", "halo", "ring", "ringtext", "home", "peak")
ROLES = ([f"t{i}" for i in range(37)] + ["nothing", "water", "river", "road"] + [f"c{k}" for k in range(1, 14)] +
         list(INK_ROLES) + ["nocov", "hatch"])   # 64 and 65 appended (A12.30): the 64 earlier indices keep their places
assert len(ROLES) == 66
IDX = {name: i for i, name in enumerate(ROLES)}
SENT = {name: (i, 200, 17) for i, name in enumerate(ROLES)}   # one colour a role; no two alike
FROM_SENT = {SENT[n]: i for i, n in enumerate(ROLES)}
NFRAMES = 20
LOOP_FRAMES = range(2, 17)
# F18/F19's patches (A12.30): (code, x0, y0), each 20 x 20 px
PATCHES = ([(1 + i, 36 + 24 * i, 50) for i in range(7)] + [(8 + i, 48 + 24 * i, 74) for i in range(6)] +
           [(14, 80, 140), (1, 100, 140), (15, 120, 140)])


def theme_colours(theme):
    """{role: RGB888} for a look, from look35's THEMES (A12.22's frozen values, A12.29's no coverage and hatch)."""
    th = look.THEMES[theme]
    c = {f"t{i}": th["terrain"][i] for i in range(37)}
    c.update(nothing=th["nothing"], water=th["water"], river=th["river"], road=th["road"], nocov=th["nocov"],
             hatch=th["hatch"])
    c.update({f"c{k}": th["pal"][k] for k in range(1, 14)})
    c.update({k: th["ink"][k] for k in INK_ROLES})
    return c


def patch_crop():
    """synthetic-clear's header and an in-memory code grid with PATCHES: a cell takes a patch's code when its projected
    centre lies inside the patch, else inside the patch grown by 1 px (the first match, in PATCHES order)."""
    h, codes = r.decode(os.path.join(look.BEND, "synthetic-clear.wxc"))
    view = r.View(h, 60)
    codes = list(codes)
    for row in range(h["rows"]):
        lat = h["latNw"] - row * h["cellLat"]
        for col in range(h["cols"]):
            x, y = view.project(lat, h["lonNw"] + col * h["cellLon"])
            hit = next((k for k, x0, y0 in PATCHES if x0 <= x < x0 + 20 and y0 <= y < y0 + 20), None)
            if hit is None:
                hit = next((k for k, x0, y0 in PATCHES if x0 - 1 <= x < x0 + 21 and y0 - 1 <= y < y0 + 21), None)
            if hit is not None:
                codes[row * h["cols"] + col] = hit
    return h, codes


def frames():
    """F0..F19 as dicts: name, kind ('bend' with the package, 'dallas' without), h, codes, state, loop, terrain."""
    out = []
    for name in ("synthetic-clear.wxc", "shsr-20240602-200600.wxc"):
        h, codes = r.decode(os.path.join(look.BEND, name))
        out.append(dict(name=name, kind="bend", h=h, codes=list(codes), state=None, loop=None, terrain=True))
    names = sorted(n for n in os.listdir(look.BEND) if n.startswith("shsr-") and n.endswith(".wxc"))
    assert len(names) == 15, names
    loop = [(n,) + r.decode(os.path.join(look.BEND, n)) for n in names]
    newest = loop[-1][1]["epoch"]
    for i, (n, h, codes) in enumerate(loop):
        mins = (newest - h["epoch"]) // 60
        out.append(dict(name=n, kind="bend", h=h, codes=list(codes), state={}, terrain=True,
                        loop=dict(age="NOW" if mins <= 0 else "%d MIN AGO" % mins, index=i)))
    dnames = sorted(n for n in os.listdir(r.LOOP) if n.startswith("shsr-") and n.endswith(".wxc"))
    dframes = [r.decode(os.path.join(r.LOOP, n)) for n in dnames]
    near = [r.nearest_echo(hh, cc, r.centre(hh), None) for hh, cc in dframes]
    ia = max(i for i, e in enumerate(near) if e and e[0] <= 15.0)   # look35's Dallas approaching
    out.append(dict(name=dnames[ia], kind="dallas", h=dframes[ia][0], codes=list(dframes[ia][1]), state=None, loop=None,
                    terrain=False))
    h, codes = patch_crop()
    for terrain in (True, False):
        out.append(dict(name="patches", kind="bend", h=h, codes=codes, state=None, loop=None, terrain=terrain))
    assert len(out) == NFRAMES
    return out


_pkg = {}


def package(view):
    key = (view.home, view.rLat, view.rLon)
    if key not in _pkg:
        _pkg[key] = look.package_scene(view)
    return _pkg[key]


def render(f, colours):
    """One frame, every role in `colours` ({role: RGB}); look35.render's path, with the loop band, the terrain flag and
    the themed no coverage and hatch (A12.29, A12.30)."""
    h, codes = f["h"], f["codes"]
    view = r.View(h, 60)
    if f["kind"] == "bend":
        img, peaks, places = package(view)
        base_of = {0: colours["nothing"], 38: colours["water"], 39: colours["river"], 40: colours["road"]}
        base_of.update({1 + i: colours[f"t{i}"] if f["terrain"] else colours["nothing"] for i in range(37)})
        base = [[base_of[v] for v in row] for row in img]
        towns, clock = places, r.clock_text(h["epoch"], look.OFFSET_PT)
    else:
        base = [[colours["nothing"]] * r.SIZE for _ in range(r.SIZE)]
        peaks, towns, clock = None, r.load_towns_now(), r.clock_text(h["epoch"])
    pal = dict(r.PAL)
    pal.update({k: colours[f"c{k}"] for k in range(1, 14)})
    pal.update({0: colours["nothing"], 14: colours["nocov"], 15: colours["hatch"]})
    ink = {k: colours[k] for k in INK_ROLES}
    ink["orange"] = None   # not ruled (A12.29); no frame here draws it
    if f["state"] is None:
        line, col = r.state_rule(h, codes, 0)
        st = dict(line=line, colour=col)
    else:
        st = dict(f["state"])
    st.update(timeText=clock, timeInk=r.DIMTXT)
    scr, _ = r.compose(h, codes, view, st, towns, lambda t: t[3], clock, r.Rounder("P"), base=base, peaks=peaks,
                       glyph=geo35.glyph_p if peaks else None, peak_log=[], pal=pal, ink=ink, loop=f["loop"],
                       terrain=f["terrain"])
    return scr.px


def rgb565_be(rgb):
    v = ((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3)
    return bytes((v >> 8, v & 0xFF))


_cache = {}


def cached(tag, fn):
    if tag not in _cache:
        _cache[tag] = fn()
    return _cache[tag]


def run(plant=None, quiet=False):
    """Every row, as {row name: (ok, text)}, with the generated frames and palettes. `plant` (sabotage only):
    ('lf1', n, cell) a crop cell set to 5 before the sentinel render; ('lf2', look, role, rgb) a palette entry;
    ('lf3', n, pixel, role) an index byte after LF1'; ('lf4', src, dst) a frame copied over another at LF4 only."""
    fs = list(cached("frames", frames))
    rows = collections.OrderedDict()
    say = (lambda s: None) if quiet else print
    tag = {n: "" for n in range(NFRAMES)}   # a planted crop is the frame's crop for every render of it
    if plant and plant[0] == "lf1":
        f = dict(fs[plant[1]], codes=list(fs[plant[1]]["codes"]))
        f["codes"][plant[2]] = 5
        fs[plant[1]], tag[plant[1]] = f, f"planted@{plant[2]}"   # one tag a plant: plants must not share renders
    idx = []
    for n, f in enumerate(fs):
        px = cached(("sent", n, tag[n]), lambda f=f: render(f, SENT))
        unmapped = sum(1 for row in px for p in row if p not in FROM_SENT)
        frame = bytes(FROM_SENT.get(p, 255) for row in px for p in row)
        idx.append(frame)
        cnt = collections.Counter(frame)
        rain = sorted(k for k in range(1, 14) if cnt[IDX[f"c{k}"]])
        nocov, hatch = cnt[IDX["nocov"]], cnt[IDX["hatch"]]
        crop = sorted(set(f["codes"]))
        if n <= 16:
            good = unmapped == 0 and set(rain) <= {1, 2, 3, 4} and nocov == 0 and hatch == 0
        elif n == 17:
            good = unmapped == 0 and nocov == 0 and hatch == 0 and 6 in rain and 7 in rain
        elif n == 18:
            good = unmapped == 0 and rain == list(range(1, 14)) and hatch > 0 and nocov == 0
        else:
            good = unmapped == 0 and rain == list(range(1, 14)) and hatch > 0 and nocov > 0
        rows[f"LF1' F{n}"] = (good, f"{unmapped} unmapped; drawn rain codes {rain}; no coverage {nocov} px, hatch line "
                                     f"{hatch} px; the crop's own codes {crop}")
    pals = {t: theme_colours(t) for t in LOOKS}
    if plant and plant[0] == "lf2":
        pals[plant[1]] = dict(pals[plant[1]], **{plant[2]: plant[3]})
    for n, scene in ((0, "bend-dry"), (1, "bend-rain"), (17, "dallas-approaching")):
        for t in LOOKS:
            want = cached(("look35", scene, t), lambda scene=scene, t=t: look.render(scene, t)[0].px)
            got = [pals[t][ROLES[i]] for i in idx[n]]
            same = sum(1 for k in range(57600) if got[k] == want[k // 240][k % 240])
            rows[f"LF2 F{n} {t}"] = (same == 57600, f"{same} / 57,600 equal to look35's {scene}")
    if plant and plant[0] == "lf3":
        b = bytearray(idx[plant[1]])
        b[plant[2]] = IDX[plant[3]]
        idx[plant[1]] = bytes(b)
    for n in list(LOOP_FRAMES) + [18, 19]:
        for t in LOOKS:
            want = cached(("direct", n, t, tag[n]), lambda n=n, t=t: render(fs[n], theme_colours(t)))
            got = [pals[t][ROLES[i]] for i in idx[n]]
            same = sum(1 for k in range(57600) if got[k] == want[k // 240][k % 240])
            rows[f"LF3 F{n} {t}"] = (same == 57600, f"{same} / 57,600 equal to the direct render")
    hidx = list(idx)
    if plant and plant[0] == "lf4":
        hidx[plant[2]] = hidx[plant[1]]
    expect = {}
    for t in LOOKS:
        lut = [rgb565_be(pals[t][name]) for name in ROLES]
        expect[t] = {str(n): hashlib.sha256(b"".join(lut[i] for i in hidx[n])).hexdigest() for n in range(NFRAMES)}
    distinct = len({(t, hh) for t in LOOKS for hh in expect[t].values()})
    rows["LF4"] = (distinct == 3 * NFRAMES, f"{3 * NFRAMES} expected sprite hashes, {distinct} distinct")
    for t in ("light-a", "light-b"):
        trio = (pals[t]["nothing"], pals[t]["hatch"], pals[t]["nocov"])
        rows[f"C3b {t}"] = (len(set(trio)) == 3, f"paper {trio[0]}, hatch line {trio[1]}, no coverage {trio[2]}")
    for k, (good, text) in rows.items():
        if not k.startswith("LF3") or not good:
            say(f"  {'ok  ' if good else 'FAIL'}  {k}: {text}")
    n3 = sum(1 for k, (g, _) in rows.items() if k.startswith("LF3") and g)
    say(f"  {'ok  ' if n3 == 51 else 'FAIL'}  LF3: {n3} of 51 equal through their palettes")
    return rows, idx, pals, expect, fs


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    out = argv[1]
    os.makedirs(out, exist_ok=True)
    rows, idx, pals, expect, fs = run()
    ok = all(g for g, _ in rows.values())
    frames_bin = b"".join(idx)
    pal_bin = b"".join(bytes(pals[t][name]) for t in LOOKS for name in ROLES)
    open(os.path.join(out, "look-frames.bin"), "wb").write(frames_bin)
    open(os.path.join(out, "look-pal.bin"), "wb").write(pal_bin)
    json.dump(dict(looks=list(LOOKS), roles=ROLES, frames=[f["name"] for f in fs], expect=expect,
                   sha256={"look-frames.bin": hashlib.sha256(frames_bin).hexdigest(),
                           "look-pal.bin": hashlib.sha256(pal_bin).hexdigest()}),
              open(os.path.join(out, "look-expect.json"), "w"), indent=1)
    print(f"look-frames.bin {len(frames_bin)} B sha256 {hashlib.sha256(frames_bin).hexdigest()[:12]}; "
          f"look-pal.bin {len(pal_bin)} B sha256 {hashlib.sha256(pal_bin).hexdigest()[:12]}")
    print("LF1'-LF4, C3b: " + ("all hold" if ok else "OFF-PREDICTION: stop and report"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
