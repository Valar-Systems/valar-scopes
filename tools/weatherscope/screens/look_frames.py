"""The light candidates' bench select (docs/weatherscope-stage3.md A12.27): the Bend screens as ROLE-INDEX frames, one
byte a pixel naming the role that drew it, and one 64-entry palette per look (dark, light (a), light (b)) -- for the
look viewer (src/weatherscope/look/), which shows them on the glass exactly. Prints no coordinate.

    python -I tools/weatherscope/screens/look_frames.py <out dir>

Writes <out dir>/look-frames.bin (17 x 57,600), look-pal.bin (3 x 64 x RGB888, in LOOKS order) and look-expect.json
(each (look, frame)'s expected sprite sha256: RGB565, high byte first, as palette_ref.py derives the bench sprite).
Checks LF1-LF4 and exits 1 on any failure.

  F0      Bend dry, synthetic-clear, its state line -- A12.23's "Bend dry"
  F1      Bend rain, shsr-20240602-200600, its state line -- A12.23's "Bend rain"
  F2-F16  the Bend loop's 15 frames, oldest first, with A9.10's loop band
"""
import hashlib, json, os, sys
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
ROLES = [f"t{i}" for i in range(37)] + ["nothing", "water", "river", "road"] + [f"c{k}" for k in range(1, 14)] + list(INK_ROLES)
assert len(ROLES) == 64
IDX = {name: i for i, name in enumerate(ROLES)}
SENT = {name: (i, 200, 17) for i, name in enumerate(ROLES)}   # one colour a role; no two alike
FROM_SENT = {c: i for i, c in enumerate(SENT[n] for n in ROLES)}


def theme_colours(theme):
    """{role: RGB888} for a look, from look35's THEMES (A12.22's frozen values; dark is geo35's P base)."""
    th = look.THEMES[theme]
    c = {f"t{i}": th["terrain"][i] for i in range(37)}
    c.update(nothing=th["nothing"], water=th["water"], river=th["river"], road=th["road"])
    c.update({f"c{k}": th["pal"][k] for k in range(1, 14)})
    c.update({k: th["ink"][k] for k in INK_ROLES})
    return c


def frames():
    """[(name, h, codes, state, loop)] for F0..F16."""
    out = []
    for name in ("synthetic-clear.wxc", "shsr-20240602-200600.wxc"):
        h, codes = r.decode(os.path.join(look.BEND, name))
        line, col = r.state_rule(h, codes, 0)
        out.append((name, h, codes, dict(line=line, colour=col), None))
    names = sorted(n for n in os.listdir(look.BEND) if n.startswith("shsr-") and n.endswith(".wxc"))
    assert len(names) == 15, names
    loop = [(n,) + r.decode(os.path.join(look.BEND, n)) for n in names]
    newest = loop[-1][1]["epoch"]
    for i, (n, h, codes) in enumerate(loop):
        mins = (newest - h["epoch"]) // 60
        out.append((n, h, codes, {}, dict(age="NOW" if mins <= 0 else "%d MIN AGO" % mins, index=i)))
    return out


_pkg = {}


def package(view):
    key = (view.home, view.rLat, view.rLon)
    if key not in _pkg:
        _pkg[key] = look.package_scene(view)
    return _pkg[key]


def render(f, colours):
    """One frame, every role in `colours` ({role: RGB}); look35.render's path, with the loop band where F2-F16 have it."""
    name, h, codes, state, loopband = f
    view = r.View(h, 60)
    img, peaks, places = package(view)
    base_of = {0: colours["nothing"], 38: colours["water"], 39: colours["river"], 40: colours["road"]}
    base_of.update({1 + i: colours[f"t{i}"] for i in range(37)})
    base = [[base_of[v] for v in row] for row in img]
    pal = dict(r.PAL)
    pal.update({k: colours[f"c{k}"] for k in range(1, 14)})
    ink = {k: colours[k] for k in INK_ROLES}
    ink["orange"] = None   # not ruled (A12.23); no frame here draws it
    clock = r.clock_text(h["epoch"], look.OFFSET_PT)
    st = dict(state, timeText=clock, timeInk=r.DIMTXT)
    scr, _ = r.compose(h, codes, view, st, places, lambda t: t[3], clock, r.Rounder("P"), base=base, peaks=peaks,
                       glyph=geo35.glyph_p, peak_log=[], pal=pal, ink=ink, loop=loopband)
    return scr.px


def rgb565_be(rgb):
    v = ((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3)
    return bytes((v >> 8, v & 0xFF))


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    out = argv[1]
    os.makedirs(out, exist_ok=True)
    fs = frames()
    ok = True
    # LF1: closure, from the sentinel render
    idx_frames = []
    for n, f in enumerate(fs):
        codes_seen = set(f[2])
        px = render(f, SENT)
        unmapped = sum(1 for row in px for p in row if p not in FROM_SENT)
        idx = bytes(FROM_SENT.get(p, 255) for row in px for p in row)
        good = unmapped == 0 and codes_seen <= set(range(0, 5))
        ok &= good
        idx_frames.append(idx)
        print(f"  {'ok  ' if good else 'FAIL'}  LF1 F{n} ({f[0]}): {unmapped} unmapped pixels; codes {sorted(codes_seen)}")
    pals = {t: theme_colours(t) for t in LOOKS}
    # LF2: F0 and F1 against look35's own renders (A12.23's pictures)
    for n, scene in ((0, "bend-dry"), (1, "bend-rain")):
        for t in LOOKS:
            want = look.render(scene, t)[0].px
            got = [pals[t][ROLES[i]] for i in idx_frames[n]]
            same = sum(1 for k in range(57600) if got[k] == want[k // 240][k % 240])
            ok &= same == 57600
            print(f"  {'ok  ' if same == 57600 else 'FAIL'}  LF2 F{n} {t}: {same} / 57,600 equal to look35's {scene}")
    # LF3: every loop frame through each palette against a direct render in that look's colours
    n3 = 0
    for n in range(2, 17):
        for t in LOOKS:
            want = render(fs[n], pals[t])
            got = [pals[t][ROLES[i]] for i in idx_frames[n]]
            same = sum(1 for k in range(57600) if got[k] == want[k // 240][k % 240])
            n3 += same == 57600
            if same != 57600:
                print(f"  FAIL  LF3 F{n} {t}: {same} / 57,600")
    ok &= n3 == 45
    print(f"  {'ok  ' if n3 == 45 else 'FAIL'}  LF3 {n3} of 45 loop renders equal through their palettes")
    # LF4: the expected sprites
    expect = {}
    for t in LOOKS:
        lut = [rgb565_be(pals[t][name]) for name in ROLES]
        expect[t] = {str(n): hashlib.sha256(b"".join(lut[i] for i in idx_frames[n])).hexdigest() for n in range(17)}
    distinct = len({(t, h) for t in LOOKS for h in expect[t].values()})
    print(f"  {'ok  ' if distinct == 51 else 'NOTE'}  LF4 51 expected sprite hashes ({distinct} distinct (look, hash) pairs)")
    frames_bin = b"".join(idx_frames)
    pal_bin = b"".join(bytes(pals[t][name]) for t in LOOKS for name in ROLES)
    open(os.path.join(out, "look-frames.bin"), "wb").write(frames_bin)
    open(os.path.join(out, "look-pal.bin"), "wb").write(pal_bin)
    json.dump(dict(looks=list(LOOKS), roles=ROLES, frames=[f[0] for f in fs], expect=expect,
                   sha256={"look-frames.bin": hashlib.sha256(frames_bin).hexdigest(),
                           "look-pal.bin": hashlib.sha256(pal_bin).hexdigest()}),
              open(os.path.join(out, "look-expect.json"), "w"), indent=1)
    print(f"look-frames.bin {len(frames_bin)} B sha256 {hashlib.sha256(frames_bin).hexdigest()[:12]}; "
          f"look-pal.bin {len(pal_bin)} B sha256 {hashlib.sha256(pal_bin).hexdigest()[:12]}")
    print("LF1-LF4: " + ("all hold" if ok else "OFF-PREDICTION: stop and report"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
