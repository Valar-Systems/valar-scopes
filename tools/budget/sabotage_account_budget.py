#!/usr/bin/env python3
"""Red first (docs/account-budget.md section 4): each control in scripts/account_budget.py --selftest, and in
tools/budget/build_budget.py --selftest (the kv-days refusals and --miss a, 2026-11-15), must FAIL when the logic it
guards is broken. Every plant is applied to a COPY of its file, asserted to have applied (its needle occurs exactly once
and the text changes), and the copy's selftest must fail with EXACTLY the predicted set of controls -- a whole-set
comparison, so an unexpected extra failure is a finding, not a pass.

  python3 tools/budget/sabotage_account_budget.py        exit 0 every plant read as predicted, 1 otherwise
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(os.path.dirname(HERE)), "scripts", "account_budget.py")
SRC_B = os.path.join(HERE, "build_budget.py")

PLANTS = [  # (name, needle, replacement, the set of controls predicted to FAIL)
    ("S0 none (anchor control)", None, None, set()),
    ("S1 FLAG_AT raised to 0.90", "FLAG_AT, ANCHOR_TOL = 0.80, 0.01", "FLAG_AT, ANCHOR_TOL = 0.90, 0.01", {"P1"}),
    ("S2 every allowance flagged", 'else "FLAG" if share >= FLAG_AT else ""', 'else "FLAG" if share >= FLAG_AT else "FLAG"', {"C-flags", "P2"}),
    ("S3 unknown allowance accepted", 'if r["allowance"] not in al:', "if False:", {"P3"}),
    # 9.12 (2026-11-15): an unbounded tolerance also passes P7g's and P7k's gaps, so their rows join P4's.
    ("S4 anchor tolerance unbounded", "FLAG_AT, ANCHOR_TOL = 0.80, 0.01", "FLAG_AT, ANCHOR_TOL = 0.80, 1e9", {"P4", "P7g", "P7k"}),
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
    # Re-registered 2026-11-17 (section 10.1, after a MISS): section 10's rows run through weights() too, so a broken
    # whole-month weight also fails M-ST1 and M-ST2, and a broken segment weight also fails M-ST2.
    ("S-MONTH a whole-month state weighs 1, not days/30", "else [(st, days / 30)])", "else [(st, 1.0)])", {"M1", "M-ST1", "M-ST2"}),
    ("S-SEG a segment weighs 1, not its hours / 720", '[(x, h / STEADY_H) for x, h in segments(s, p, st, m, v["states"])]',
     '[(x, 1.0) for x, h in segments(s, p, st, m, v["states"])]', {"M2", "M-ST2"}),
    # Section 10 (registered 2026-11-17 before its code): R2 storage is per GB-month. With the 30/days factor removed,
    # a per-month allowance scales with the month again, so both December rows fail; M-ST0 (30 days) stays green.
    ("S-STOR a per-month allowance scaled by the month again", '            pm = 30 / days if al.get("per_month") else 1.0',
     "            pm = 1.0", {"M-ST1", "M-ST2"}),
    # 9.12, the anchor's rounding allowance (ruled 2026-11-15), predicted here before the first run:
    ("S7a the rounding allowance at k = 1 too", "if k != 1:", "if True:", {"P7k"}),
    ("S7b no rounding allowance", "tol = max(tol, 0.5 * (len(vals) + 1))", "tol = tol", {"P7"}),
    ("S7c the rounding bound one unit wider", "0.5 * (len(vals) + 1)", "0.5 * (len(vals) + 3)", {"P7g"}),
    ("S7d a read with totals and no k accepted",
     'if rd["totals"] and (isinstance(k, bool) or not isinstance(k, (int, float)) or not k > 0):', "if False:", {"P7n"}),
    # Section 11 (registered 2026-11-18 before its code): an allowance in force only in some months. Each existing
    # plant's set is unchanged; section 11 names, plant by plant, why none reaches the M-LG rows.
    ("S-LG the in-force test always true", 'if not in_force(al, m or b["month"]):', "if False:",
     {"M-LG1", "M-LG2", "M-LG3", "M-LG5", "M-LG6"}),
    ("S-LGV the months window not validated", 'if "months" in v:', "if False:", {"M-LG4"}),
]

# The builder's kv-days refusals and B2's rows (ruled 2026-11-15), each set predicted here before the first run.
BUILDER_PLANTS = [
    ("K-S0 none (anchor control)", None, None, set()),
    ("K-S1 without --miss, (A) outside passes", 'if not a_in and miss != "a":', "if False:", {"K1"}),
    ("K-S2 --miss a ignores the other bands", "for n, v, (lo, hi) in others if not lo <= v <= hi]",
     "for n, v, (lo, hi) in others if False]", {"K2"}),
    ("K-S3 --miss a accepted with (A) in its band", '        if a_in:\n            stop(3, f"--miss a refused: {a_name}',
     '        if False:\n            stop(3, f"--miss a refused: {a_name}', {"K3"}),
    ("K-S4 --miss a without its S5/P5 cross-check", "if s5 != A_S5[2] or p5 < A_P5[2]:", "if False:", {"K4s", "K4p"}),
    ("K-S5 --miss takes any value", 'argv[2] == "--miss" and argv[3] == "a"', 'argv[2] == "--miss"', {"K5"}),
    ("K-S5u an unknown flag accepted", 'argv[2] == "--miss" and argv[3] == "a"', 'argv[3] == "a"', {"K5u"}),
    ("K-S6 a two-argument SystemExit (the process exits 1)", "    raise SystemExit(code)", "    raise SystemExit(code, msg)", {"K6"}),
    ("K-S7 the background rows count 11-14", "d not in A_DAYS]", "d != A_DAY]", {"K7"}),
    ("K-S8 (A) on its registered day alone", 'A_DAYS = ("2026-11-13", "2026-11-14")', "A_DAYS = (A_DAY,)", {"K7", "K8", "K9"}),
    ("K-S9 the miss note dropped", "        a_note += (", "        a_note_unused = (", {"K9"}),
    ("K-S10 the (A) line never reads MISS", '"MISS (recorded, not graded)" if (miss == "a" and n == a_name) else "OUTSIDE"',
     '"OUTSIDE" if False else "OUTSIDE"', {"K10"}),
]


def failed_controls(out):
    got = set()
    for line in out.splitlines():
        if not line.startswith("FAIL "):
            continue
        m = re.match(r"FAIL ((?:P|M|R|K)[\w-]*):", line)   # P1..P6, P-ONE(-ctl|-steady|-none), M1..M3, R1, R2; K0..K10
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
    off = 0
    targets = [(SRC, "account_budget.py", [sys.executable], PLANTS),
               (SRC_B, "build_budget.py", [sys.executable, "-I", "-B"], BUILDER_PLANTS)]
    sources = {path: open(path, encoding="utf-8").read() for path, _, _, _ in targets}
    runs = [(sources[path], base, py, plant) for path, base, py, plants in targets for plant in plants]
    with tempfile.TemporaryDirectory() as d:
        for src, base, py, (name, needle, repl, want) in runs:
            text = src
            if needle is not None:
                n = src.count(needle)
                if n != 1:
                    print(f"RIG {name}: the needle occurs {n} times, not once -- the plant cannot be trusted"); off += 1; continue
                text = src.replace(needle, repl)
                if text == src:
                    print(f"RIG {name}: the plant changed nothing"); off += 1; continue
            copy = os.path.join(d, base)
            with open(copy, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)
            r = subprocess.run(py + [copy, "--selftest"], capture_output=True, text=True)
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
