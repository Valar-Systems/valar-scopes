#!/usr/bin/env python3
"""Red first (docs/account-budget.md section 4): each control in scripts/account_budget.py --selftest must FAIL when
the logic it guards is broken. Every plant is applied to a COPY of the checker, asserted to have applied (its needle
occurs exactly once and the text changes), and the copy's selftest must fail with EXACTLY the predicted set of
controls -- a whole-set comparison, so an unexpected extra failure is a finding, not a pass.

  python3 tools/budget/sabotage_account_budget.py        exit 0 every plant read as predicted, 1 otherwise
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(os.path.dirname(HERE)), "scripts", "account_budget.py")

PLANTS = [  # (name, needle, replacement, the set of controls predicted to FAIL)
    ("S0 none (anchor control)", None, None, set()),
    ("S1 FLAG_AT raised to 0.90", "FLAG_AT, ANCHOR_TOL = 0.80, 0.01", "FLAG_AT, ANCHOR_TOL = 0.90, 0.01", {"P1"}),
    ("S2 every allowance flagged", 'else "FLAG" if share >= FLAG_AT else ""', 'else "FLAG" if share >= FLAG_AT else "FLAG"', {"C-flags", "P2"}),
    ("S3 unknown allowance accepted", 'if r["allowance"] not in al:', "if False:", {"P3"}),
    ("S4 anchor tolerance unbounded", "FLAG_AT, ANCHOR_TOL = 0.80, 0.01", "FLAG_AT, ANCHOR_TOL = 0.80, 1e9", {"P4"}),
    ("S5 render always current", "    return have == render(b)", "    return True", {"P5"}),
    # Instrument failure #1 (2026-11-14): anchored on the inner clause alone, this plant was semantically the clean
    # code -- the outer clause still dropped every excluded row -- and P6 rightly stayed green. It now takes both lines.
    ("S6 excluded rows summed",
     'if r["allowance"] == a and r["state"] != EXCLUDED\n                      for st, wt in w[r["product"]] if st == r["state"])',
     'if r["allowance"] == a\n                      for st, wt in w[r["product"]] if st == r["state"] or r["state"] == EXCLUDED)', {"P6"}),
    # 9.3's registered sabotage. With the value-equals-product check off, P-ONE's x 30 row reads clean, so P-ONE fails:
    # exactly {P-ONE} ("turns P-ONE's row green" in 9.3's words).
    ("S-ONE value == per_event x events unchecked", 'if abs(r["value"] - want) > 1e-9 * max(abs(want), 1.0):', "if False:", {"P-ONE"}),
    # Added with the month code, predicted here before the first run (2026-11-14):
    ("S-MONTH a whole-month state weighs 1, not days/30", "else [(st, days / 30)])", "else [(st, 1.0)])", {"M1"}),
    ("S-SEG a segment weighs 1, not its hours / 720", '[(x, h / STEADY_H) for x, h in segments(s, p, st, m, v["states"])]',
     '[(x, 1.0) for x, h in segments(s, p, st, m, v["states"])]', {"M2"}),
]


def failed_controls(out):
    got = set()
    for line in out.splitlines():
        if not line.startswith("FAIL "):
            continue
        m = re.match(r"FAIL ((?:P|M|R)[\w-]*):", line)   # P1..P6, P-ONE(-ctl|-steady|-none), M1..M3, R1, R2
        if m:
            got.add(m.group(1))
        elif line.startswith("FAIL CONTROL: the clean budget's flags"):
            got.add("C-flags")
        elif line.startswith("FAIL CONTROL: the clean synthetic budget anchors"):
            got.add("C-anchor")
        else:
            got.add("UNPARSED: " + line[:60])
    return got


def main():
    src = open(SRC, encoding="utf-8").read()
    off = 0
    with tempfile.TemporaryDirectory() as d:
        for name, needle, repl, want in PLANTS:
            text = src
            if needle is not None:
                n = src.count(needle)
                if n != 1:
                    print(f"RIG {name}: the needle occurs {n} times, not once -- the plant cannot be trusted"); off += 1; continue
                text = src.replace(needle, repl)
                if text == src:
                    print(f"RIG {name}: the plant changed nothing"); off += 1; continue
            copy = os.path.join(d, "account_budget.py")
            with open(copy, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)
            r = subprocess.run([sys.executable, copy, "--selftest"], capture_output=True, text=True)
            got = failed_controls(r.stdout)
            want_exit = 0 if not want else 1
            ok = got == want and r.returncode == want_exit
            print(f"{'as predicted' if ok else 'OFF':12s} {name}: failed {sorted(got) or 'none'} (predicted {sorted(want) or 'none'}), exit {r.returncode}")
            if not ok:
                off += 1
                print(r.stdout[-800:], r.stderr[-400:])
    print("SABOTAGE " + ("ALL AS PREDICTED" if not off else f"OFF: {off}"))
    return 0 if not off else 1


if __name__ == "__main__":
    sys.exit(main())
