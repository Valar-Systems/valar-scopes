# SCRATCH (map-server): writes 5d.2's fix to mirror/s4after_plants26.mts, after keeping the failed file as
# mirror/s4after_plants26.cc99aebe9af5.mts. The fix (rule (c)): sections are never matched by FIRST of type.
#   - a helper one(pkg, type) returns the section of that type and THROWS unless it occurs exactly once;
#   - the three lookups of the planted section (lines 71 and 81) use one();
#   - line 80's "other sections equal" asserts the SAME type sequence, then compares section i with section i.
# Every anchor must match exactly once. Refuses if the kept copy exists.
#   python -X utf8 -I mirror/mk_plants_fix.py
import os, shutil, sys

D = os.path.dirname(os.path.abspath(__file__))
p, keep = os.path.join(D, "s4after_plants26.mts"), os.path.join(D, "s4after_plants26.cc99aebe9af5.mts")
if os.path.exists(keep):
    print("refused: the kept copy exists"); sys.exit(2)
s = open(p, encoding="utf-8", newline="").read()
assert "\r" not in s
import hashlib
assert hashlib.sha256(s.encode("utf-8")).hexdigest()[:12] == "cc99aebe9af5", "not the failed file"
shutil.copy2(p, keep)
reps = [
    ("const clamp = (v: number) => Math.max(-QMAX, Math.min(QMAX, v));\n",
     "const clamp = (v: number) => Math.max(-QMAX, Math.min(QMAX, v));\n"
     "// 5d.2, rule (c): a section is taken by type ONLY when that type occurs exactly once; repeated types are paired by index.\n"
     "const one = (p: any, t: number) => { const s = p.sections.filter((x: any) => x.type === t); if (s.length !== 1) throw new Error(`type ${t} occurs ${s.length} times`); return s[0]; };\n"),
    ("  const sec = pkg.sections.find((s: any) => s.type === kind);\n",
     "  const sec = one(pkg, kind);\n"),
    ("  const others = before.sections.every((s: any) => s.type === kind || JSON.stringify(plain(s)) === JSON.stringify(plain(after.sections.find((t: any) => t.type === s.type))));\n",
     "  const typesEqual = JSON.stringify(before.sections.map((s: any) => s.type)) === JSON.stringify(after.sections.map((s: any) => s.type));\n"
     "  const others = typesEqual && before.sections.every((s: any, i: number) => s.type === kind || JSON.stringify(plain(s)) === JSON.stringify(plain(after.sections[i])));\n"),
    ("  const a = (before.sections.find((s: any) => s.type === kind)![kind === 1 ? \"rings\" : \"lines\"]) as any[], b = (after.sections.find((s: any) => s.type === kind)![kind === 1 ? \"rings\" : \"lines\"]) as any[];\n",
     "  const a = (one(before, kind)[kind === 1 ? \"rings\" : \"lines\"]) as any[], b = (one(after, kind)[kind === 1 ? \"rings\" : \"lines\"]) as any[];\n"),
]
for o, n in reps:
    assert s.count(o) == 1, o[:60]
    s = s.replace(o, n)
assert "sections.find(" not in s
open(p, "w", encoding="utf-8", newline="").write(s)
print("written: s4after_plants26.mts (the failed file kept as s4after_plants26.cc99aebe9af5.mts)")
