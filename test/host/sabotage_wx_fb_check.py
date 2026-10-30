"""The frame checker's two gaps, closed as ruled (docs/wx-device.md, "The checker's two gaps", 2026-10-30): each plant
breaks one rule in an IN-MEMORY copy of scripts/wx-fb-check.py, run as if it were the real file (so it reads the real
source), and the selftest must turn red in exactly the cases named. The real file is never written; its sha256 is
compared before and after. An unplanted run must pass first, or nothing here means anything.

    python -I test/host/sabotage_wx_fb_check.py      exit 0 = as predicted, 1 = not, 2 = rig

  S-hl-colour     every 0xF81F set apart by colour, not by geometry   -> exactly H2
  S-hl-off        the highlight not set apart at all                  -> exactly H1
  S-dot-anywhere  a dot's pixels set apart without the whole dot      -> exactly D2
  S-dot-slot      the dots placed for SCREEN_COUNT, not - 1           -> exactly D1
  S-label-pass    a label-touched marker passed silently              -> exactly L1 and D2 (amended before any run)
  S-label-all     every weather neighbour read as ambiguous           -> exactly L2, "no outline, look A", "the old
                                                                         r=4 disc outline"
"""
import contextlib
import hashlib
import io
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass (main's #506; docs/wx-device.md, "CI's anchors pass")

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CHECKER = os.path.join(ROOT, "scripts", "wx-fb-check.py")

HL_BLOCK = ('                    if c == src["hl"]["colour"]:\n'
            '                        if centres is None: centres = highlight_centres(f, src)\n'
            '                        if in_highlight((x, y), centres, src):\n'
            '                            hl += 1; continue   # the NEAR/HIGH/FAST highlight, by its geometry (ruled 2026-10-30)\n')
SABOTAGES = {
    "S-hl-colour": (["H2"], [(HL_BLOCK,
        '                    if c == src["hl"]["colour"]:\n'
        '                        hl += 1; continue   # SABOTAGE S-hl-colour: set apart by colour\n')]),
    "S-hl-off": (["H1"], [(HL_BLOCK, "                    pass   # SABOTAGE S-hl-off\n")]),
    "S-dot-anywhere": (["D2"], [("        if all(at(f, *p) == c for p in s): out |= s\n",
                                 "        out |= {p for p in s if at(f, *p) == c}   # SABOTAGE S-dot-anywhere\n")]),
    "S-dot-slot": (["D1"], [("    shown, dots = count - 1, []\n", "    shown, dots = count, []   # SABOTAGE S-dot-slot\n")]),
    "S-label-pass": (["L1", "D2"], [("                    apart += 1\n",
                                     "                    pass   # SABOTAGE S-label-pass: passed silently\n")]),
    "S-label-all": (["L2", "no outline, look A", "the old r=4 disc outline"],
                    [('                if all(at(f, *p) in src["label"] for p in wet):\n',
                      "                if True:   # SABOTAGE S-label-all\n")]),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def red_cases(text):
    """Run the selftest of a checker whose source is `text`, as the real file; the labels of its red cases."""
    ns = {"__name__": "wxfb_sabotage", "__file__": CHECKER}
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        exec(compile(text, CHECKER, "exec"), ns)
        rc = ns["selftest"](ns["load_source"]())
    reds = [m.group(1) for m in re.finditer(r"^  FAIL (.+?): ", buf.getvalue(), re.M)]
    return rc, reds, buf.getvalue()


def short(label):
    """A case's name as the predictions use it: its code (H1, D2, ...) when it has one, else the whole label."""
    m = re.match(r"([HDL]\d) ", label)
    return m.group(1) if m else label


def main():
    before = sha(CHECKER)
    src = sc.read(CHECKER)
    rc, reds, out = red_cases(src)
    if rc != 0 or reds:
        print(f"control (the real checker): the selftest is not green ({reds}) -- the rig is broken\n{out[-800:]}")
        return 2
    print("control (the real checker): the selftest passes")
    bad = 0
    for name, (want, edits) in SABOTAGES.items():
        planted = src
        for old, new in edits:
            if planted.count(old) != 1:
                print(f"  RIG {name}: the anchor occurs {planted.count(old)} times"); return 2
            planted = planted.replace(old, new)
        assert planted != src, f"{name}: the sabotage did not apply"
        rc, reds, _ = red_cases(planted)
        got = sorted(short(r) for r in reds)
        ok = rc == 1 and got == sorted(want)
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: red {got} (want {sorted(want)}), selftest exit {rc}")
    after = sha(CHECKER)
    print(f"real scripts/wx-fb-check.py unchanged: {before == after} ({after[:12]})")
    print("WX FB CHECK SABOTAGES AS PREDICTED" if not bad and before == after else
          f"WX FB CHECK SABOTAGES: {bad} NOT AS PREDICTED")
    return 0 if not bad and before == after else 1


def anchor_entries():
    """Every plant's anchor in the file it plants, for run.sh's --anchors pass (no plant runs)."""
    text = sc.read(CHECKER)
    return [(name, "scripts/wx-fb-check.py", text, a) for name, (_, edits) in SABOTAGES.items() for a, _ in edits]


if __name__ == "__main__":
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
