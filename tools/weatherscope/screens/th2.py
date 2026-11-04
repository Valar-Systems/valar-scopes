"""Weatherscope 3.5t t4's TH2' (docs/weatherscope-stage3.md A12.60): ref35's palette closure in Light. Four light scenes,
each drawn through ref35's compose in look35's light theme, must hold no pixel outside that theme's colours -- its inks,
its rain palette, nothing, water, river, road and the terrain ramp (look35's TH2 set):
  TH2' warning-60     the Dallas warning             TH2' loop-07       Dallas loop frame 7
  TH2' stale-60       the Dallas stale               TH2' bloop-14-C    Bend loop frame 14, every layer and the terrain
  TH2' CONTROL        the dark warning render against the light colours: strays (> 0)

    python -I tools/weatherscope/screens/th2.py [--ref35 <path>]

--ref35 draws every scene through that copy of ref35.py (sabotage_wx_th2.py's S-TH2 plants there); its ROOT must name
this tree. Exit 0 all pass, 1 any fail, 2 the rig. Prints no coordinate.
"""
import collections, os, sys
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(path))
    spec.loader.exec_module(m)
    return m


def strays(scr, th):
    allowed = ({v for v in th["ink"].values() if v is not None} | {th["nothing"], th["water"], th["river"], th["road"]}
               | set(th["terrain"]) | set(th["pal"].values()))
    return collections.Counter(p for row in scr.px for p in row if p not in allowed)


def main(argv):
    path = argv[argv.index("--ref35") + 1] if "--ref35" in argv else os.path.join(HERE, "ref35.py")
    try:
        k35b = _load("k35b", os.path.join(HERE, "k35b.py"))
        r = _load("ref35_th2", path)   # every scene below is drawn through this module
        look = k35b.look
        light = look.THEMES["light"]
        k35b.r, k35b.THEME = r, (lambda: light)   # k35b's Bend row draws through the same module, in Light
        lit = {name: scr for name, scr, _ in r.k_rows(light)[0]}
        dark = {name: scr for name, scr, _ in r.k_rows(None)[0]}
        names = sorted(n for n in os.listdir(look.BEND) if n.startswith("shsr-") and n.endswith(".wxc"))
        if len(names) != 15:
            raise RuntimeError(f"{len(names)} Bend loop frames, not 15")
        h, c = r.decode(os.path.join(look.BEND, names[14]))
        if h["epoch"] != max(r.decode(os.path.join(look.BEND, n))[0]["epoch"] for n in names):
            raise RuntimeError("Bend loop frame 14 is not the newest")
        lit["bloop-14-C"] = k35b.row("bloop-14-C", h, c, 60, 15, True, dict(age="NOW", index=14))[1]
    except Exception as e:
        print(f"TH2': the RIG -- {type(e).__name__}: {e}")
        return 2
    checks = failures = 0
    print("== TH2': ref35's palette closure, four light scenes ==")
    for name in ("warning-60", "stale-60", "loop-07", "bloop-14-C"):
        s = strays(lit[name], light)
        checks += 1
        failures += bool(s)
        print(f"  {'ok  ' if not s else 'FAIL'} TH2' {name}: {sum(s.values())} pixels outside the light theme's colours"
              + ("" if not s else f" {dict(s.most_common(5))}"))
    n = sum(strays(dark["warning-60"], light).values())
    checks += 1
    failures += n == 0
    print(f"  {'ok  ' if n > 0 else 'FAIL'} TH2' CONTROL: the dark warning render against the light colours: {n} pixels "
          f"outside (must be > 0)")
    print(f"{checks} checks, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
