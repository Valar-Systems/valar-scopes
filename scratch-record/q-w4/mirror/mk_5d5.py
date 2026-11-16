# SCRATCH (map-server): writes 5d.5's change to mirror/s4after_grade26.py (6bf01deb7a67, kept as s4after_grade26.6bf01deb7a67.py): ONE
# uniform ELIGIBILITY rule from mirror/elig26.json, for every centre and both rows.
#   - INSTRUMENT check: elig26.json's samples equal s4_render2's own SEAM samples at every centre and rung (the same window);
#   - CONTROL CENTRES: 44007's water at 10 mi and 19123's rivers at 10 mi each >= 1, else OUTSIDE;
#   - S4-3 (SEAM): a centre/rung whose eligible WATER rings are 0 is N/A, with its count, not graded;
#   - S4-3b (BREAK): a centre/rung whose eligible RIVER polylines are 0 is N/A, with its count, not graded. This replaces 5d.4's
#     17005 special case: 17005 is N/A here only by the rule.
# Every anchor must match exactly once.
#   python -X utf8 -I mirror/mk_5d5.py
import hashlib, os, shutil, sys

D = os.path.dirname(os.path.abspath(__file__))
p = os.path.join(D, "s4after_grade26.py"); keep = os.path.join(D, "s4after_grade26.6bf01deb7a67.py")
s = open(p, encoding="utf-8", newline="").read()
assert "\r" not in s and hashlib.sha256(s.encode()).hexdigest()[:12] == "6bf01deb7a67", "not the 5d.4 grader"
if os.path.exists(keep): print("refused: the kept copy exists"); sys.exit(2)
reps = [
    ("import hashlib, math, os, re, subprocess, sys\n", "import hashlib, json, math, os, re, subprocess, sys\n"),
    ('    c3 = num(new.get("PLANT-44007", []), "SEAM", 10, "seampx")\n',
     '    # 5d.5: the uniform ELIGIBILITY rule (mirror/elig26.json, from elig26.mts)\n'
     '    E = json.load(open(os.path.join(S, "mirror", "elig26.json"), encoding="utf-8"))\n'
     '    smp = sum(E[g][str(mi)]["samples"] == num(new.get(f"AFTER-{g}", []), "SEAM", mi, "samples") for g in G for mi in MI)\n'
     '    print(f"ELIGIBILITY: elig26.json {sha(os.path.join(S, \'mirror\', \'elig26.json\'))}; window samples equal s4_render2\'s at {smp} of 18{\'\' if smp == 18 else \' -- INSTRUMENT\'}")\n'
     '    off += smp != 18\n'
     '    ce = E["44007"]["10"]["water"] >= 1 and E["19123"]["10"]["rivers"] >= 1\n'
     '    print(f"ELIGIBILITY CONTROL CENTRES: 44007 water at 10 mi {E[\'44007\'][\'10\'][\'water\']}; 19123 rivers at 10 mi {E[\'19123\'][\'10\'][\'rivers\']}; {\'both >= 1\' if ce else \'OUTSIDE: STOP\'}")\n'
     '    off += not ce\n'
     '    c3 = num(new.get("PLANT-44007", []), "SEAM", 10, "seampx")\n'),
    ('    seams = {(g, mi): num(new.get(f"AFTER-{g}", []), "SEAM", mi, "seampx") for g in G for mi in MI}\n',
     '    seams = {(g, mi): num(new.get(f"AFTER-{g}", []), "SEAM", mi, "seampx") for g in G for mi in MI if E[g][str(mi)]["water"] >= 1}\n'
     '    na3 = [(g, mi, E[g][str(mi)]["water"]) for g in G for mi in MI if E[g][str(mi)]["water"] == 0]\n'),
    ('        print(f"S4-3: AFTER seam pixels 0 at {18 - len(nz)} of 18 pictures; non-zero (FLAGGED to the owner): {sorted(nz.items())}")\n',
     '        print(f"S4-3: AFTER seam pixels 0 at {len(seams) - len(nz)} of {len(seams)} eligible pictures; N/A (0 eligible water rings): {[(g, mi) for g, mi, _ in na3]}; non-zero (FLAGGED to the owner): {sorted(nz.items())}")\n'),
    ('                if g == "17005":   # 5d.4: 0 of 158 river polylines inside the county, so these rows cannot fail: N/A, not a pass\n'
     '                    print(f"  S4-3b {g} {mi} mi: N/A (0 of 158 polylines inside the county)"); continue\n',
     '                if E[g][str(mi)]["rivers"] == 0:   # 5d.5: by the uniform rule; a row that cannot fail is N/A, not a pass\n'
     '                    print(f"  S4-3b {g} {mi} mi: N/A (eligible river polylines 0 of {E[g][str(mi)][\'riversTotal\']})"); continue\n'),
    ('        print(f"S4-3b: {15 - len(flags)} of 15 within T (17005\'s 3 rows N/A); FLAGGED to the owner: {flags}")\n',
     '        graded = sum(1 for g in G for mi in MI if E[g][str(mi)]["rivers"] >= 1)\n'
     '        print(f"S4-3b: {graded - len(flags)} of {graded} eligible within T; FLAGGED to the owner: {flags}")\n'),
]
for o, n in reps:
    assert s.count(o) == 1, o[:70]
    s = s.replace(o, n)
shutil.copy2(p, keep)
open(p, "w", encoding="utf-8", newline="").write(s)
print("written: s4after_grade26.py (kept as s4after_grade26.6bf01deb7a67.py)")
