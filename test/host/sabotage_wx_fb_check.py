"""The frame checker's two gaps and their follow-ups (docs/wx-device.md, "The checker's two gaps" and "The checker's
follow-ups", 2026-10-30): each plant breaks one rule in an IN-MEMORY copy of scripts/wx-fb-check.py, run as if it were
the real file (so it reads the real source), and the selftest must turn red in exactly the cases named. The fixture
plants write a moved COPY of the measured screen-dot fixture into a temporary directory and point the copy of the
checker at it. Neither the real checker nor the real fixture is written; their sha256 are compared before and after.
An unplanted run must pass first, or nothing here means anything.

    python -I test/host/sabotage_wx_fb_check.py      exit 0 = as predicted, 1 = not, 2 = rig

  S-hl-colour      every 0xF81F set apart by colour, not by geometry    -> exactly H2
  S-hl-off         the highlight not set apart at all                   -> exactly H1
  S-dot-anywhere   a dot's pixels set apart without the whole dot       -> exactly D2
  S-dot-slot       the checker's dots placed for SCREEN_COUNT, not - 1  -> exactly D1 (its first run, before the
                   fixture, turned nothing red: the miss stays on record)
  S-dot-slot'      the fixture's slot 2 moved 1 px                      -> exactly D1; its control, the copy unmoved,
                                                                           nothing red
  S-dot-silent     a dot-touched marker passed and not counted          -> exactly D1
  S-label-pass     a label-touched marker passed and not counted        -> exactly L1 and D2
  S-label-all      every weather neighbour read as a label's            -> exactly L2, "no outline, look A", "the old
                                                                           r=4 disc outline"
"""
import contextlib
import hashlib
import io
import json
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass (main's #506; docs/wx-device.md, "CI's anchors pass")

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CHECKER = os.path.join(ROOT, "scripts", "wx-fb-check.py")
FIXTURE = os.path.join(ROOT, "test", "host", "fixtures", "wx_screen_dots.json")

HL_BLOCK = ('                    if c == src["hl"]["colour"]:\n'
            '                        if centres is None: centres = highlight_centres(f, src)\n'
            '                        if in_highlight((x, y), centres, src):\n'
            '                            hl += 1; continue   # the NEAR/HIGH/FAST highlight, by its geometry (ruled 2026-10-30)\n')
FIXTURE_LINE = 'DOT_FIXTURE = os.path.join(ROOT, "test", "host", "fixtures", "wx_screen_dots.json")\n'
SABOTAGES = {
    "S-hl-colour": (["H2"], [(HL_BLOCK,
        '                    if c == src["hl"]["colour"]:\n'
        '                        hl += 1; continue   # SABOTAGE S-hl-colour: set apart by colour\n')]),
    "S-hl-off": (["H1"], [(HL_BLOCK, "                    pass   # SABOTAGE S-hl-off\n")]),
    "S-dot-anywhere": (["D2"], [("        if all(at(f, *p) == c for p in s): out |= s\n",
                                 "        out |= {p for p in s if at(f, *p) == c}   # SABOTAGE S-dot-anywhere\n")]),
    "S-dot-slot": (["D1"], [("    shown, dots = count - 1, []\n", "    shown, dots = count, []   # SABOTAGE S-dot-slot\n")]),
    "S-dot-silent": (["D1"], [('            if dot_wet: causes.append("screen dot")\n',
                               "            pass   # SABOTAGE S-dot-silent\n")]),
    "S-label-pass": (["L1", "D2"], [('            if wet: causes.append("label")\n',
                                     "            pass   # SABOTAGE S-label-pass\n")]),
    "S-label-all": (["L2", "no outline, look A", "the old r=4 disc outline"],
                    [('            if wet and not all(at(f, *p) in src["label"] for p in wet):\n',
                      "            if False:   # SABOTAGE S-label-all\n")]),
}
# The fixture plants: (name, wanted red cases, slot-2 x shift). Shift 0 is the control: the same route, unmoved.
FIXTURE_PLANTS = [("S-dot-slot'", ["D1"], 1), ("S-dot-slot' control", [], 0)]


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def red_cases(text):
    """Run the selftest of a checker whose source is `text`, as the real file; its exit and the labels of its red cases."""
    ns = {"__name__": "wxfb_sabotage", "__file__": CHECKER}
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        exec(compile(text, CHECKER, "exec"), ns)
        rc = ns["selftest"](ns["load_source"]())
    return rc, [m.group(1) for m in re.finditer(r"^  FAIL (.+?): ", buf.getvalue(), re.M)], buf.getvalue()


def short(label):
    """A case's name as the predictions use it: its code (H1, D2, ...) when it has one, else the whole label."""
    m = re.match(r"([HDL]\d) ", label)
    return m.group(1) if m else label


def plant(text, name, edits):
    for old, new in edits:
        if text.count(old) != 1:
            raise RuntimeError(f"{name}: the anchor occurs {text.count(old)} times")
        text = text.replace(old, new)
    return text


def main():
    before = (sha(CHECKER), sha(FIXTURE))
    src = sc.read(CHECKER)
    rc, reds, out = red_cases(src)
    if rc != 0 or reds:
        print(f"control (the real checker): the selftest is not green ({reds}) -- the rig is broken\n{out[-800:]}")
        return 2
    print("control (the real checker): the selftest passes")
    bad = 0
    runs = [(name, want, edits) for name, (want, edits) in SABOTAGES.items()]
    with tempfile.TemporaryDirectory() as td:
        for name, want, dx in FIXTURE_PLANTS:
            d = json.load(open(FIXTURE, encoding="utf-8"))
            d["dots"][2]["x"] += dx
            path = os.path.join(td, name.replace("'", "p").replace(" ", "-") + ".json")
            json.dump(d, open(path, "w", encoding="utf-8"))
            runs.append((name, want, [(FIXTURE_LINE, f"DOT_FIXTURE = {path!r}   # SABOTAGE {name}\n")]))
        for name, want, edits in runs:
            try:
                planted = plant(src, name, edits)
            except RuntimeError as e:
                print(f"  RIG {e}"); return 2
            assert planted != src, f"{name}: the sabotage did not apply"
            rc, reds, _ = red_cases(planted)
            got = sorted(short(r) for r in reds)
            ok = got == sorted(want) and rc == (1 if want else 0)
            bad += not ok
            print(f"  {'ok  ' if ok else 'FAIL'} {name}: red {got} (want {sorted(want)}), selftest exit {rc}")
    after = (sha(CHECKER), sha(FIXTURE))
    print(f"real scripts/wx-fb-check.py and the fixture unchanged: {before == after} ({after[0][:12]}, {after[1][:12]})")
    print("WX FB CHECK SABOTAGES AS PREDICTED" if not bad and before == after else
          f"WX FB CHECK SABOTAGES: {bad} NOT AS PREDICTED")
    return 0 if not bad and before == after else 1


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    text = sc.read(CHECKER)
    out = [(name, "scripts/wx-fb-check.py", text, a) for name, (_, edits) in SABOTAGES.items() for a, _ in edits]
    return out + [(name, "scripts/wx-fb-check.py", text, FIXTURE_LINE) for name, _, _ in FIXTURE_PLANTS]


if __name__ == "__main__":
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
