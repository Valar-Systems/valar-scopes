"""image-compare's sabotage driver (docs/image-compare.md sections 9-14): plants defects in a copy of
scripts/image-compare.py, and each must turn red EXACTLY the selftest cases it declares.

  python test/host/sabotage_image_compare.py              every plant, against its declaration
  python test/host/sabotage_image_compare.py --anchors    every plant's anchor present exactly once (run.sh, Q51)
  python test/host/sabotage_image_compare.py --control    the unplanted selftest alone (run.sh, Q57)

EVERY plant declares EVERY case the selftest grades (test/host/image_compare_selftest.py, case_labels()), each as red or
as unaffected. The driver REFUSES to run (exit 2) while any case is missing from any plant's declaration, sits in both
halves of one, or is not a case the selftest grades. (The reviewer's ruling, 18:10:27Z: P7 and then P12 each kept a set
that was not re-derived when a case was added. Adding a case now fails this driver until every plant has declared it.)

History of the sets, kept: P1's fixture was fixed by bytes; P9 was re-registered as P9b; P7's and P12's sets were
re-derived after their first runs read OFF on a newly added case. Exit 0 all as declared; 1 a plant off or a stranded
anchor; 2 the rig or a refused declaration; 3 skipped (no pyelftools).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sabotage_common as sc  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
TARGET = os.path.join("scripts", "image-compare.py")

C = ["null", "body", "body allow=f", "grow allow=g", "gc", "local", "local allow=f", "relax", "pick", "strpool", "offset",
     "rodata", "rodata allow=f1", "rodata allow=f1,f2", "eh", "ehtype"]
R1, R2, R3, R4, R5, R6 = ("R1 unknown relocation type", "R2 stale input", "R3 size without a long-call site",
                          "R4 a narrowing that changes the operation", "R5 a flip whose reach never crossed",
                          "R6 a register change under a relocation")
T = ["T0 t0", "T0 t0bad"]
R8, R9 = "R8 a relocation to a different target", "R9 an object with no property table"
# Amendment 1f's six cases (18:39Z). Each plant below names them, red or unaffected, line by line: P1-P13 were checked
# against each one and none of them can turn any of the six red, so each of those plants lists them as unaffected.
F1 = ["fill", "fillchg", "swap", "swapop", R8, R9]

# (label, anchor, replacement, red, unaffected) -- both halves written out, every case named in one of them.
PLANTS = [
    ("P1 strings named by offset", 'return "str:" + data[addend:end].hex()', 'return "str@%d" % addend',
     ["strpool"],
     ["null", "body", "body allow=f", "grow allow=g", "gc", "local", "local allow=f", "relax", "pick", "offset",
      "rodata", "rodata allow=f1", "rodata allow=f1,f2", "eh", "ehtype", R1, R2, R3, R4, R5, R6] + T + F1),
    ("P2 any width change accepted", "if ci is None or ci != cf:", "if False:",
     [R4],
     C + [R1, R2, R3, R5, R6] + T + F1),
    ("P3 no reach-crossing check", "if in_call_range(long_pc, long_t) or not in_call_range(call_pc, call_t):",
     "if False:",
     [R5],
     C + [R1, R2, R3, R4, R6] + T + F1),
    ("P4 recorded sha believed", "                st.input_bytes(p)\n", "                pass\n",
     [R2],
     C + [R1, R3, R4, R5, R6] + T + F1),
    ("P5 an unknown type allowed", 'R_SLOT0_OP: "SLOT0_OP"}', 'R_SLOT0_OP: "SLOT0_OP", 17: "DIFF8"}',
     [R1],
     C + [R2, R3, R4, R5, R6] + T + F1),
    ("P6 the rule as first registered (no narrowing)",
     "            ci, cf = canon(inp[i:i + li]), canon(fin[j:j + lf])\n",
     "            raise Refusal(\"width\")\n",
     ["relax", R4, R5],
     ["null", "body", "body allow=f", "grow allow=g", "gc", "local", "local allow=f", "pick", "strpool", "offset",
      "rodata", "rodata allow=f1", "rodata allow=f1,f2", "eh", "ehtype", R1, R2, R3, R6] + T + F1),
    ("P7 an allowed referrer not counted", "if rs and all(r in in_scope or allowed(r[1]) for r in rs):",
     "if rs and all(r in in_scope for r in rs):",
     ["local allow=f", "rodata allow=f1,f2"],
     ["null", "body", "body allow=f", "grow allow=g", "gc", "local", "relax", "pick", "strpool", "offset",
      "rodata", "rodata allow=f1", "eh", "ehtype", R1, R2, R3, R4, R5, R6] + T + F1),
    ("P8 the whole instruction masked", "    return bytes(b)\n", "    return bytes(len(b))\n",
     [R6],
     C + [R1, R2, R3, R4, R5] + T + F1),
    ("P9b MOVI counted as an offset", "r in (0, 1, 2, 4, 5, 6, 9):", "r in (0, 1, 2, 4, 5, 6, 9, 0xA):",
     ["local", "local allow=f"],
     ["null", "body", "body allow=f", "grow allow=g", "gc", "relax", "pick", "strpool", "offset",
      "rodata", "rodata allow=f1", "rodata allow=f1,f2", "eh", "ehtype", R1, R2, R3, R4, R5, R6] + T + F1),
    ("P11 GC strict (no fixed point)", "if all(w in changed or (id(side), w) in passed for w in who):",
     "if all(w in changed for w in who):",
     ["gc"],
     ["null", "body", "body allow=f", "grow allow=g", "local", "local allow=f", "relax", "pick", "strpool", "offset",
      "rodata", "rodata allow=f1", "rodata allow=f1,f2", "eh", "ehtype", R1, R2, R3, R4, R5, R6] + T + F1),
    ("P12 a data unit's referrers not printed", "        refs = named_by(key, oa, ob) if kind == \"data\" else \"\"",
     "        refs = \"\"",
     ["ehtype", "rodata", "rodata allow=f1", "rodata allow=f1,f2"],
     ["null", "body", "body allow=f", "grow allow=g", "gc", "local", "local allow=f", "relax", "pick", "strpool",
      "offset", "eh", R1, R2, R3, R4, R5, R6] + T + F1),
    ("P13 the exception table not folded", "                    self.paired_lsda.add(i)\n", "                    pass\n",
     ["eh", "ehtype"],
     ["null", "body", "body allow=f", "grow allow=g", "gc", "local", "local allow=f", "relax", "pick", "strpool",
      "offset", "rodata", "rodata allow=f1", "rodata allow=f1,f2", R1, R2, R3, R4, R5, R6] + T + F1),
    # amendment 1f's plants, each red set derived case by case at 18:39Z, before the first run
    ("P14 a width swap's operation not compared", "        if ca is None or ca != cb:", "        if False:",
     ["swapop"],
     C + [R1, R2, R3, R4, R5, R6] + T + ["fill", "fillchg", "swap", R8, R9]),
    ("P15 the fill walk's relocation names not compared",
     "        if sorted(rel_a.get(pa, [])) != sorted(rel_b.get(pb, [])):", "        if False:",
     # ^ re-derived at 18:39Z (date -u read 18:39:52Z) after its first run read OFF: body's 10000 -> 10001 is movi.n 16 -> 17 plus addmi
     #   0x2700 (immediates, read from the disassembly), not a literal, so P15 cannot touch it. My codegen guess, again.
     [R8],
     ["body", "body allow=f", "null", "grow allow=g", "gc", "local", "local allow=f", "relax", "pick", "strpool", "offset",
      "rodata", "rodata allow=f1", "rodata allow=f1,f2", "eh", "ehtype", R1, R2, R3, R4, R5, R6] + T
     + ["fill", "fillchg", "swap", "swapop", R9]),
    ("P16 a missing property table read as one instruction region",
     "        raise Refusal(\"no property table covers %s\" % obj.secs[i].name)",
     "        return [(0, obj.secs[i][\"sh_size\"], \"insn\")]",
     [R9],
     C + [R1, R2, R3, R4, R5, R6] + T + ["fill", "fillchg", "swap", "swapop", R8]),
    ("P17 the fill counts not printed",
     "            fills.append(\"FILL func %s %s [%s] -- %s\" % (oa.binding(key[1])[4:].lower(), dm(key[1]), key[0], detail))",
     "            fills.append(\"FILL func %s %s [%s] -- %s\" % (oa.binding(key[1])[4:].lower(), dm(key[1]), key[0], \"\"))",
     ["fill", "swap"],
     C + [R1, R2, R3, R4, R5, R6] + T + ["fillchg", "swapop", R8, R9]),
    ("P10 the offsets label never given", "                k = offsets_only(sa, sb) if",
     "                k = 0 and offsets_only(sa, sb) if",
     ["offset"],
     ["null", "body", "body allow=f", "grow allow=g", "gc", "local", "local allow=f", "relax", "pick", "strpool",
      "rodata", "rodata allow=f1", "rodata allow=f1,f2", "eh", "ehtype", R1, R2, R3, R4, R5, R6] + T + F1),
]


def case_labels(repo):
    sys.path.insert(0, os.path.join(repo, "test", "host"))
    try:
        import importlib
        import image_compare_selftest as t
        importlib.reload(t)
        return t.case_labels()
    finally:
        sys.path.pop(0)


def declarations_ok(labels):
    """The refusal: every plant names every case exactly once, red or unaffected."""
    bad = []
    want = set(labels)
    for label, _, _, red, ok in PLANTS:
        red, ok = set(red), set(ok)
        if red & ok:
            bad.append("%s: declared both red and unaffected: %s" % (label, sorted(red & ok)))
        missing = want - red - ok
        extra = (red | ok) - want
        if missing:
            bad.append("%s: does not declare %s" % (label, sorted(missing)))
        if extra:
            bad.append("%s: declares cases the selftest does not grade: %s" % (label, sorted(extra)))
    return bad


def red_set(out):
    return {m.group(1) for m in re.finditer(r"^FAIL (.+?): exit", out, re.M)}


def control():
    """The unplanted selftest alone, run as CI runs it."""
    try:
        import elftools  # noqa: F401
    except ImportError:
        raise sc.Skip("no pyelftools: the selftest cannot run here")
    subprocess.run([sys.executable, os.path.join(REPO, TARGET), "selftest"], capture_output=True, text=True)


def control_table():
    return {"image-compare.py": ("line", "image-compare selftest: %d cases, 0 failed" % len(case_labels(REPO)))}


def anchor_entries():
    text = sc.read(os.path.join(REPO, TARGET))
    return [(label, TARGET, text, old) for label, old, _new, _red, _ok in PLANTS]


def main():
    labels = case_labels(REPO)
    bad = declarations_ok(labels)
    if bad:
        print("REFUSED: %d plant declaration(s) do not cover the selftest's %d cases:" % (len(bad), len(labels)))
        for b in bad:
            print("   " + b)
        return 2
    print("declarations: %d plants x %d cases, every case declared by every plant" % (len(PLANTS), len(labels)))
    src = open(os.path.join(REPO, TARGET), newline="").read()
    allok = True
    for label, old, new, red, _ok in PLANTS:
        want = set(red)
        n = src.count(old)
        if n != 1:
            print("%s: ANCHOR count %d -- not applied, nothing read" % (label, n))
            allok = False
            continue
        planted = src.replace(old, new)
        assert planted != src
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, "scripts"))
            os.makedirs(os.path.join(tmp, "test", "host"))
            with open(os.path.join(tmp, TARGET), "w", newline="") as f:
                f.write(planted)
            shutil.copy(os.path.join(REPO, "test", "host", "image_compare_selftest.py"), os.path.join(tmp, "test", "host"))
            shutil.copytree(os.path.join(REPO, "test", "fixtures", "image-compare", "stores"),
                            os.path.join(tmp, "test", "fixtures", "image-compare", "stores"))
            r = subprocess.run([sys.executable, os.path.join(tmp, TARGET), "selftest"], capture_output=True, text=True)
        got = red_set(r.stdout)
        crashed = "Traceback" in r.stderr or "Traceback" in r.stdout
        ok = got == want and not crashed
        allok &= ok
        print("%s: %s red=%s%s" % (label, "AS DECLARED" if ok else "OFF", sorted(got), " CRASH" if crashed else ""))
        if not ok:
            print("   want=%s exit=%d" % (sorted(want), r.returncode))
            print("   " + (r.stderr or "")[-400:])
    print("ALL AS DECLARED" if allok else "NOT ALL AS DECLARED")
    return 0 if allok else 1


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, control_table()))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
