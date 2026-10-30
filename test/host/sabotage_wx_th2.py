"""3.5t step t4's reference sabotage (docs/weatherscope-stage3.md A12.60), failing EXACTLY its frozen set:

  S-TH2  ref35.py's two warning band lines drawn in WHITE, not ink["white"], in a copy that th2.py loads by path
         (--ref35): TH2' fails the warning scene alone; stale-60, loop-07, bloop-14-C and the CONTROL pass.

    python -I test/host/sabotage_wx_th2.py [--anchors | --control]

Method: the copy is ref35.py with its ROOT line pointed at this tree (the copy lives in a temp dir, and ROOT names the
fixtures it loads at import), and the plant on top of that. The copy with ROOT alone runs first, as the control: th2.py
passes all 5. The plant asserts its anchor occurs once, that the copy differs, and the colour proof (Q50): light's white
role (38, 38, 40) becomes dark's (255, 255, 255). A th2.py run that is not 5 checks is the RIG, never a result. The real
tools/weatherscope/screens is hashed before and after.
"""
import hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass, the --control pass and the colour proof (A12.50, A12.61)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCREENS = os.path.join(ROOT, "tools", "weatherscope", "screens")
REF35 = os.path.join(SCREENS, "ref35.py")
TH2 = os.path.join(SCREENS, "th2.py")
ROOT_LINE = ("ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))\n")
BAND = ('        scr.text(int(C - text_w(warning["band1"]) / 2), 197, warning["band1"], 1, ink["white"])\n'
        '        scr.text(int(C - text_w(warning["band2"]) / 2), 207, warning["band2"], 1, ink["white"])\n')


def tree_sha():
    h = hashlib.sha256()
    for p in sorted(os.path.join(d, f) for d, _, fs in os.walk(SCREENS) for f in fs if "__pycache__" not in d):
        h.update(os.path.relpath(p, ROOT).encode() + b"\0" + open(p, "rb").read())
    return h.hexdigest()


def plant(text, old, new, name, pairs=None):
    sc.prove_colour(name, old, new, pairs)   # Q50: refuses a colour the panel cannot see
    assert text.count(old) == 1, (name, old[:60], text.count(old))
    out = text.replace(old, new)
    assert out != text, name
    return out


def rooted(text):
    return plant(text, ROOT_LINE, "ROOT = %r   # SABOTAGE COPY: this tree\n" % ROOT, "ROOT")


def run_th2(td, label, text):
    """The failing TH2' rows of th2.py run through `text` as ref35.py, or None (the RIG)."""
    copy = os.path.join(td, label, "ref35.py")
    os.makedirs(os.path.dirname(copy))
    open(copy, "w", encoding="utf-8", newline="\n").write(text)
    r = subprocess.run([sys.executable, "-I", "-B", TH2, "--ref35", copy], capture_output=True, text=True)
    m = re.search(r"^(\d+) checks, (\d+) failures$", r.stdout, re.M)
    if r.returncode not in (0, 1) or not m or int(m.group(1)) != 5:
        print(f"{label}: th2.py did not run as 5 checks (exit {r.returncode}): the RIG\n{r.stdout[-600:]}{r.stderr[-600:]}")
        return None
    return {x.group(1) for x in re.finditer(r"^\s*FAIL TH2' (\S+?):", r.stdout, re.M)}   # th2.py: "TH2' <scene>: ..."


def main():
    before = tree_sha()
    ok = True
    base = rooted(sc.read(REF35))
    planted = plant(base, BAND, BAND.replace('ink["white"])', 'WHITE)   # SABOTAGE'), "S-TH2",
                    pairs=[((38, 38, 40), (255, 255, 255))])   # the literal stands in for light's white role
    with tempfile.TemporaryDirectory() as td:
        ctl = run_th2(td, "control", base)
        good_c = ctl == set()
        ok &= good_c
        print(f"control (the copy, ROOT alone): failing {sorted(ctl) if ctl is not None else 'RIG'} -> "
              f"{'AS PREDICTED' if good_c else 'OFF-PREDICTION'}")
        got = run_th2(td, "S-TH2", planted)
    frozen = {"warning-60"}
    good = got is not None and got == frozen
    ok &= good
    print(f"S-TH2: failing {sorted(got) if got is not None else 'RIG'}, predicted {sorted(frozen)} -> "
          f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
    after = tree_sha()
    ok &= after == before
    print(f"the real tools/weatherscope/screens unchanged: {after == before} ({after[:12]})")
    print(f"1 sabotage: {'ALL AS PREDICTED' if ok else 'NOT ALL AS PREDICTED'}")
    return 0 if ok else 1


def anchor_entries():
    """Q51: S-TH2's two anchors in ref35.py -- the band lines it plants and the ROOT line its copy repoints."""
    rel = os.path.relpath(REF35, ROOT)
    return [("S-TH2", rel, sc.read(REF35), BAND), ("S-TH2", rel, sc.read(REF35), ROOT_LINE)]

# --control (Q57, docs/weatherscope-stage3.md A12.61): this driver's unplanted run alone, th2.py against its count,
# nothing planted. run.sh runs it for every driver.
CONTROL_TABLE = {"th2.py": ("checks", 5)}


def control():
    with tempfile.TemporaryDirectory() as td:
        run_th2(td, "control", rooted(sc.read(REF35)))


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
