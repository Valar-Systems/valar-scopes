"""image-compare.py's selftest (docs/image-compare.md, step 2): every fixture case and every refusal, each graded on
its WHOLE output and its exit code -- never a fragment (CLAUDE.md: an assertion that matches a fragment is a filter).

The stores are test/fixtures/image-compare/stores, made by that directory's gen.sh from its src/. Mutations are made
on copies in a temporary directory, and each one is asserted to have applied before its case is read.
"""
import hashlib
import json
import os
import shutil
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
STORES = os.path.join(HERE, "..", "fixtures", "image-compare", "stores")

# The cases. Each one's meaning was fixed when its fixture was built (gen.sh); its numbers are the fixture's own:
# the relax case's reaches are 0x40070000 - 0x40000060 and 0x40090000 - 0x40000064, and the grow case moves 6
# functions (f's new literal lands ahead of entry: the linker places literals).
CASES = [
    ("null", None, 0, [
        "inputs: 7 loaded, 0 differ",
        "functions whose address moved: 0",
        "verdict: NO CHANGE"]),
    ("body", None, 1, [
        "inputs: 7 loaded, 1 differ",
        "  differs: src/f.c.o",
        "functions whose address moved: 0",
        "CHANGED func global A+B f [src/f.c.o] -- OUT",
        "verdict: OUT OF SCOPE: f"]),
    ("body", ["f"], 0, [
        "inputs: 7 loaded, 1 differ",
        "  differs: src/f.c.o",
        "functions whose address moved: 0",
        "CHANGED func global A+B f [src/f.c.o] -- in scope",
        "verdict: IN SCOPE (1 changed units)"]),
    ("grow", ["g"], 1, [
        "inputs: 7 loaded, 1 differ",
        "  differs: src/f.c.o",
        "functions whose address moved: 6",
        "CHANGED func global A+B f [src/f.c.o] -- OUT",
        "verdict: OUT OF SCOPE: f"]),
    ("gc", None, 1, [
        "inputs: 7 loaded, 1 differ",
        "  differs: src/f.c.o",
        "GC h [src/libh.a(h.c.o)#0], kept in B only; named only by CHANGED or GC units: f",
        "GC h2 [src/libh.a(h2.c.o)#0], kept in B only; named only by CHANGED or GC units: h",
        "functions whose address moved: 4",
        "CHANGED func global A+B f [src/f.c.o] -- OUT",
        "verdict: OUT OF SCOPE: f"]),
    ("local", None, 1, [
        "inputs: 7 loaded, 1 differ",
        "  differs: src/f.c.o",
        "functions whose address moved: 0",
        "CHANGED func local A+B helper [src/f.c.o] -- OUT",
        "verdict: OUT OF SCOPE: helper"]),
    ("local", ["f"], 0, [
        "inputs: 7 loaded, 1 differ",
        "  differs: src/f.c.o",
        "functions whose address moved: 0",
        "CHANGED func local A+B helper [src/f.c.o] -- in scope",
        "verdict: IN SCOPE (1 changed units)"]),
    ("relax", None, 0, [
        "inputs: 7 loaded, 0 differ",
        "RELAX lo_caller: n=1 rA=1 rB=0 narrowed A/B=1/0 widened A/B=0/0 dsize=+4; "
        "site 0: call in A, reach +458656; long in B, reach +589724",
        "functions whose address moved: 7",
        "verdict: NO CHANGE"]),
    ("pick", None, 0, [
        "inputs: 3 loaded, 0 differ",
        "PICK _Z3inli: src/pa.cpp.o -> src/pb.cpp.o, the two copies the same",
        "functions whose address moved: 2",
        "verdict: NO CHANGE"]),
    # t1's strings move inside the object's shared pool (in-place addends 0,6 -> 5,11); named by content, t1 is the
    # same and has no line. t0 is NEW but folded away by the compiler, so it is reported and not linked.
    ("strpool", None, 1, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/tables.c.o",
        "not linked: NEW t0 [src/tables.c.o]",
        "functions whose address moved: 0",
        "CHANGED func global A+B use [src/tables.c.o] -- OUT",
        "verdict: OUT OF SCOPE: use"]),
    # a member inserted ahead of b: get's one load moves from offset 4 to 8, and nothing else in it changes. (body's
    # literal and local's addi are constants, not offsets: both stay plain CHANGED above.)
    ("offset", None, 1, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/off.c.o",
        "functions whose address moved: 0",
        "CHANGED (load/store offsets only: 1) func global A+B get [src/off.c.o] -- OUT",
        "verdict: OUT OF SCOPE: get"]),
    # amendment 1d: f1's and f2's struct constants share the object's unsplit .rodata. f1's changes, neither function's
    # code does: the change is attributed to ALL the unit's referrers, never to a guessed one, and the verdict names them.
    ("rodata", None, 1, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/rod.c.o",
        "functions whose address moved: 0",
        "CHANGED func global A+B f1 [src/rod.c.o] -- OUT",
        "CHANGED data local A+B sec:.rodata [src/rod.c.o] -- OUT (named by: f1, f2)",
        "verdict: OUT OF SCOPE: f1, sec:.rodata (named by: f1, f2)"]),
    ("rodata", ["f1"], 1, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/rod.c.o",
        "functions whose address moved: 0",
        "CHANGED func global A+B f1 [src/rod.c.o] -- in scope",
        "CHANGED data local A+B sec:.rodata [src/rod.c.o] -- OUT (named by: f1, f2)",
        "verdict: OUT OF SCOPE: sec:.rodata (named by: f1, f2)"]),
    ("rodata", ["f1", "f2"], 0, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/rod.c.o",
        "functions whose address moved: 0",
        "CHANGED func global A+B f1 [src/rod.c.o] -- in scope",
        "CHANGED data local A+B sec:.rodata [src/rod.c.o] -- in scope (named by: f1, f2)",
        "verdict: IN SCOPE (2 changed units)"]),
    # amendment 1e: a function's exception table is part of the function. eh: handle grows and its table's call-site
    # offsets move -- handle alone. ehtype: only the catch type changes (EA -> EB) and the code is byte-identical --
    # handle still reads CHANGED, which is why the table is folded in rather than excluded like .eh_frame.
    ("eh", None, 1, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/eh.cpp.o",
        "functions whose address moved: 8",
        "CHANGED func global A+B _Z6handlei [src/eh.cpp.o] -- OUT",
        "verdict: OUT OF SCOPE: _Z6handlei"]),
    ("ehtype", None, 1, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/eh.cpp.o",
        "functions whose address moved: 0",
        "CHANGED func global A+B _Z6handlei [src/eh.cpp.o] -- OUT",
        "GONE data weak A _ZTI2EA [src/eh.cpp.o] -- OUT (named by: _Z6handlei)",
        "NEW data weak B _ZTI2EB [src/eh.cpp.o] -- OUT (named by: _Z6handlei)",
        "GONE data weak A _ZTS2EA [src/eh.cpp.o] -- OUT (named by: _ZTI2EA)",
        "NEW data weak B _ZTS2EB [src/eh.cpp.o] -- OUT (named by: _ZTI2EB)",
        "verdict: OUT OF SCOPE: _Z6handlei, _ZTI2EA (named by: _Z6handlei), _ZTI2EB (named by: _Z6handlei), "
        "_ZTS2EA (named by: _ZTI2EA), _ZTS2EB (named by: _ZTI2EB)"]),
    # amendment 1f: the assembler's fill, read ONLY from the objects' own property tables. fill: the same source, B
    # assembled with -Wa,--no-target-align -- FILL, printed with its counts, never silently same. fillchg: that, plus one
    # real change -- CHANGED. swap: one move, narrow in A and wide in B -- FILL with the swap counted. swapop: B's wide
    # instruction is a different operation -- CHANGED.
    ("fill", None, 0, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/fp.c.o",
        "FILL func global pick [src/fp.c.o] -- fill bytes A/B=1/0; width swaps: none; 34 instructions",
        "functions whose address moved: 1",
        "verdict: NO CHANGE"]),
    ("fillchg", None, 1, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/fp.c.o",
        "functions whose address moved: 1",
        "CHANGED func global A+B pick [src/fp.c.o] -- OUT",
        "verdict: OUT OF SCOPE: pick"]),
    ("swap", None, 0, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/sw.S.o",
        "FILL func global sw [src/sw.S.o] -- fill bytes A/B=0/0; width swaps: mov 2->3 x1; 7 instructions",
        "functions whose address moved: 0",
        "verdict: NO CHANGE"]),
    ("swapop", None, 1, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/sw.S.o",
        "functions whose address moved: 0",
        "CHANGED func global A+B sw [src/sw.S.o] -- OUT",
        "verdict: OUT OF SCOPE: sw"]),
    # amendment 1g: a static initializer's exception table (.text.startup.<name>) is part of it. The initializer is kept
    # by no reference at this fixture's link, so it reads at the object layer: one line, not two.
    ("inittry", None, 0, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/si.cpp.o",
        "not linked: CHANGED _GLOBAL__sub_I_g_val [src/si.cpp.o]",
        "functions whose address moved: 0",
        "verdict: NO CHANGE"]),
    # amendment 1h': an unsplit section's targets named by content. rodshift: f0's constant lands ahead of f1's -- f1
    # moved, not changed (no line). rodown: f1's own constant changed -- CHANGED. rodswap: two byte-identical constants
    # swap places -- content cannot tell them apart, so both read UNDECIDED, never same.
    ("rodshift", None, 1, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/rs.S.o",
        "functions whose address moved: 2",
        "NEW func global B f0 [src/rs.S.o] -- OUT",
        "CHANGED data local A+B sec:.rodata [src/rs.S.o] -- OUT (named by: f0, f1)",
        "verdict: OUT OF SCOPE: f0, sec:.rodata (named by: f0, f1)"]),
    ("rodown", None, 1, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/rs.S.o",
        "functions whose address moved: 0",
        "CHANGED func global A+B f1 [src/rs.S.o] -- OUT",
        "CHANGED data local A+B sec:.rodata [src/rs.S.o] -- OUT (named by: f1)",
        "verdict: OUT OF SCOPE: f1, sec:.rodata (named by: f1)"]),
    ("rodswap", None, 3, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/rs.S.o",
        "functions whose address moved: 0",
        "UNDECIDED f1 [src/rs.S.o]: an unsplit-section constant whose bytes repeat in its section",
        "UNDECIDED f2 [src/rs.S.o]: an unsplit-section constant whose bytes repeat in its section",
        "verdict: UNDECIDED (2)"]),
]

# Amendment 1i (sections 21-22): LINK-ONLY's controls. callswap: one call target swapped at the same length -- the
# owner's control, a real change. rodread: read-only data changed at the same size -- CHANGED, and (L2 below) never
# LINK-ONLY for a function that reads it.
CALLSWAP_LINES = [
    "inputs: 3 loaded, 1 differ",
    "  differs: src/cs.S.o",
    "functions whose address moved: 0",
    "CHANGED func global A+B caller [src/cs.S.o] -- OUT",
    "verdict: OUT OF SCOPE: caller"]
RODREAD_CHANGED = [
    "CHANGED data local A+B ltbl [src/rr.c.o] -- OUT (named by: rl)",
    "CHANGED data global A+B tbl [src/rr.c.o] -- OUT (named by: no kept unit)"]
CASES += [
    ("callswap", None, 1, CALLSWAP_LINES),
    ("rodread", None, 1, [
        "inputs: 3 loaded, 1 differ",
        "  differs: src/rr.c.o",
        "functions whose address moved: 0"] + RODREAD_CHANGED + [
        "verdict: OUT OF SCOPE: ltbl (named by: rl), tbl (named by: no kept unit)"]),
]
# The stated limit every LINK-ONLY line ends with (1i', the reviewer's ruling 19:33Z).
TRUST = "non-call bytes trusted to the link (store built from M0, inputs hashed)"
# 1i'' (section 24): every verdict line ends by counting the functions in both images that a walk ran on (W) and the
# rest (U), whose image bytes are all trusted to the link. The text is written here, not taken from the comparator.
VERDICT_TRUST = ("; trusted to the link (store built from M0, inputs hashed): %d walked (call sites checked, other "
                 "bytes trusted), %d unwalked (all bytes trusted)")
# (W, U) per case, filed in section 24 before the code: N from each pair's two images, counted by a separate script.
TRUSTED = {}
for _labels, _wu in [
        (["null", "R6 a register change under a relocation", "body", "body allow=f", "grow allow=g", "gc", "local",
          "local allow=f", "local relocs", "gc relocs", "R1 unknown relocation type", "R2 stale input",
          "R11 a local name that repeats refuses",
          "R13 a call aimed at nothing, in a function never walked (the limit)"], (0, 7)),
        (["R3 size without a long-call site", "R10 unreadable object code is never LINK-ONLY", "relax",
          "R4 a narrowing that changes the operation", "R5 a flip whose reach never crossed",
          "R12 a call aimed at nothing, in a function accounted RELAX"], (1, 6)),
        (["pick", "callswap", "rodread", "L1 the call swap, in front of LINK-ONLY"], (0, 4)),
        (["strpool", "offset", "swapop", "rodshift", "rodown"], (0, 2)),
        (["rodata", "rodata allow=f1", "rodata allow=f1,f2"], (0, 5)),
        (["eh", "ehtype"], (0, 8)),
        (["fill"], (1, 2)),
        (["fillchg", "R8 a relocation to a different target", "R9 an object with no property table", "rodswap"], (0, 3)),
        (["swap"], (1, 1)),
        (["inittry"], (0, 1)),
        (["L2 read-only data, in front of LINK-ONLY"], (3, 1))]:
    for _l in _labels:
        assert _l not in TRUSTED, _l
        TRUSTED[_l] = _wu


def copy_case(tmp, case):
    """A private copy per mutation: no two mutations share a directory."""
    dst = os.path.join(tempfile.mkdtemp(dir=tmp), case.replace("/", "_"))
    shutil.copytree(os.path.join(STORES, case), dst)
    return dst


def reseal(store, rel):
    """Re-record one input's sha256 after a mutation, so the mutation is read rather than refused as stale."""
    p = os.path.join(store, "inputs.json")
    with open(p) as f:
        j = json.load(f)
    with open(os.path.join(store, rel), "rb") as f:
        h = hashlib.sha256(f.read()).hexdigest()
    for e in j["inputs"]:
        if e["path"] == rel:
            e["sha256"] = h
    with open(p, "w") as f:
        json.dump(j, f, indent=1)


def patch(path, offset, old, new):
    """A mutation, asserted: the bytes there were `old`, and are `new` after."""
    with open(path, "r+b") as f:
        f.seek(offset)
        got = f.read(len(old))
        assert got == old, "mutation anchor: %s at 0x%x reads %s, not %s" % (path, offset, got.hex(), old.hex())
        f.seek(offset)
        f.write(new)
        f.seek(offset)
        assert f.read(len(new)) == new
    return True


def elf_file_offset(mod, elf_path, addr):
    with open(elf_path, "rb") as f:
        elf = mod.ELFFile(f)
        for s in elf.iter_sections():
            a = s["sh_addr"]
            if s["sh_type"] == "SHT_PROGBITS" and a <= addr < a + s["sh_size"]:
                return s["sh_offset"] + (addr - a)
    raise AssertionError("no section holds 0x%x" % addr)


def symbol_entry_offset(mod, elf_path, name):
    with open(elf_path, "rb") as f:
        elf = mod.ELFFile(f)
        st = elf.get_section_by_name(".symtab")
        for k, sym in enumerate(st.iter_symbols()):
            if sym.name == name:
                return st["sh_offset"] + k * st["sh_entsize"], sym["st_size"], sym["st_value"]
    raise AssertionError("no symbol " + name)


def reloc_entry_offset(mod, obj_path, relsec, want_type):
    with open(obj_path, "rb") as f:
        elf = mod.ELFFile(f)
        s = elf.get_section_by_name(relsec)
        for k, r in enumerate(s.iter_relocations()):
            if r["r_info_type"] == want_type:
                return s["sh_offset"] + k * s["sh_entsize"] + 4  # r_info, little-endian: its low byte is the type
    raise AssertionError("no relocation of type %d in %s" % (want_type, relsec))


def run_case(mod, a, b, allow, tmp, relocs=False):
    lines = []
    allow_path = None
    if allow is not None:
        allow_path = os.path.join(tmp, "allow.txt")
        with open(allow_path, "w") as f:
            f.write("\n".join(allow) + "\n")
    rc = mod.compare(a, b, allow_path, None, lines.append, relocs)
    return rc, lines


# --reloc-names (map-client's P-REDO amendment 1): after each plain CHANGED function, its named targets compared.
# local: helper's change is a constant, so its targets are unchanged. gc: f gains a call to h (its literal and the
# long-call marker both name h).
RELOC_CASES = [
    ("local", 1, [
        "inputs: 7 loaded, 1 differ",
        "  differs: src/f.c.o",
        "functions whose address moved: 0",
        "CHANGED func local A+B helper [src/f.c.o] -- OUT",
        "  relocations: unchanged",
        "verdict: OUT OF SCOPE: helper"]),
    ("gc", 1, [
        "inputs: 7 loaded, 1 differ",
        "  differs: src/f.c.o",
        "GC h [src/libh.a(h.c.o)#0], kept in B only; named only by CHANGED or GC units: f",
        "GC h2 [src/libh.a(h2.c.o)#0], kept in B only; named only by CHANGED or GC units: h",
        "functions whose address moved: 4",
        "CHANGED func global A+B f [src/f.c.o] -- OUT",
        "  relocations: +['sym:h+0', 'sym:h+0'] -[]",
        "verdict: OUT OF SCOPE: f"]),
]

REFUSALS = ["R1 unknown relocation type", "R2 stale input", "R3 size without a long-call site",
            "R4 a narrowing that changes the operation", "R5 a flip whose reach never crossed",
            "R6 a register change under a relocation", "R8 a relocation to a different target",
            "R9 an object with no property table", "R10 unreadable object code is never LINK-ONLY",
            "R11 a local name that repeats refuses", "R12 a call aimed at nothing, in a function accounted RELAX",
            "R13 a call aimed at nothing, in a function never walked (the limit)"]
LINK_CASES = ["L1 the call swap, in front of LINK-ONLY", "L2 read-only data, in front of LINK-ONLY"]
T0_CASES = ["T0 t0", "T0 t0bad"]


def case_label(case, allow):
    return "%s%s" % (case, " allow=" + ",".join(allow) if allow else "")


def case_labels():
    """Every case this selftest grades, by label: the list the sabotage driver holds every plant's declaration to."""
    return ([case_label(c, a) for c, a, _, _ in CASES] + [c + " relocs" for c, _, _ in RELOC_CASES] + REFUSALS
            + LINK_CASES + T0_CASES)


def run(mod):
    fails = 0
    total = 0
    known = case_labels()
    assert len(set(known)) == len(known), "a case label repeats"
    assert sorted(TRUSTED) == sorted(k for k in known if k not in T0_CASES), \
        "TRUSTED must file (W, U) for exactly the cases with a verdict line"

    def grade(label, rc, lines, want_rc, want_lines):
        nonlocal fails, total
        assert label in known, "a graded case missing from case_labels(): %s" % label
        if label in TRUSTED:  # 1i'': the verdict line's count, as filed
            assert want_lines[-1].startswith("verdict: "), label
            want_lines = want_lines[:-1] + [want_lines[-1] + VERDICT_TRUST % TRUSTED[label]]
        total += 1
        if rc == want_rc and lines == want_lines:
            print("PASS %s" % label)
            return
        fails += 1
        print("FAIL %s: exit %d (want %d)" % (label, rc, want_rc))
        for ln in lines:
            print("   got  | " + ln)
        for ln in want_lines:
            print("   want | " + ln)

    with tempfile.TemporaryDirectory() as tmp:
        for case, allow, want_rc, want in CASES:
            rc, lines = run_case(mod, os.path.join(STORES, case, "a"), os.path.join(STORES, case, "b"), allow, tmp)
            grade(case_label(case, allow), rc, lines, want_rc, want)

        for case, want_rc, want in RELOC_CASES:
            rc, lines = run_case(mod, os.path.join(STORES, case, "a"), os.path.join(STORES, case, "b"), None, tmp, True)
            grade(case + " relocs", rc, lines, want_rc, want)

        # R1: a relocation type the rules do not know, in a compared section -> UNDECIDED, never "same" or "changed"
        st = copy_case(tmp, "body")
        obj = os.path.join(st, "b", "src", "f.c.o")
        off = reloc_entry_offset(mod, obj, ".rela.text.f", mod.R_SLOT0_OP)
        patch(obj, off, bytes([mod.R_SLOT0_OP]), bytes([17]))
        reseal(os.path.join(st, "b"), "src/f.c.o")
        rc, lines = run_case(mod, os.path.join(st, "a"), os.path.join(st, "b"), None, tmp)
        grade("R1 unknown relocation type", rc, lines, 3, [
            "inputs: 7 loaded, 1 differ",
            "  differs: src/f.c.o",
            "functions whose address moved: 0",
            "UNDECIDED f [src/f.c.o]: relocation type 17 in a compared section",
            "verdict: UNDECIDED (1)"])

        # R2: an input whose bytes no longer match inputs.json -> refused, never compared
        st = copy_case(tmp, "body/b")
        obj = os.path.join(st, "src", "f.c.o")
        with open(obj, "ab") as f:
            f.write(b"\0")
        rc, lines = run_case(mod, os.path.join(STORES, "body", "a"), st, None, tmp)
        grade("R2 stale input", rc, lines, 3, [
            "inputs: 7 loaded, 0 differ",
            "functions whose address moved: 0",
            "UNDECIDED an input whose bytes no longer match its recorded sha256: src/f.c.o (B)",
            "verdict: UNDECIDED (1)"])

        # R3: a size difference in a function with no long-call site -> UNDECIDED (RELAX is earned, never assumed)
        st = copy_case(tmp, "null/b")
        elf = os.path.join(st, "firmware.elf")
        off, size, _ = symbol_entry_offset(mod, elf, "ext")
        patch(elf, off + 8, size.to_bytes(4, "little"), (size + 2).to_bytes(4, "little"))
        rc, lines = run_case(mod, os.path.join(STORES, "null", "a"), st, None, tmp)
        # 1i: ext's object is identical and it reads no data, so the walk's refusal is LINK-ONLY (made by patching the
        # image -- the blind spot section 21 states)
        grade("R3 size without a long-call site", rc, lines, 0, [
            "inputs: 7 loaded, 0 differ",
            "LINK-ONLY ext [src/ext.c.o] -- size %d -> %d; the walk: its size differs and it has no long-call site; "
            "object code equal; read-only data equal: 0 section(s); call sites checked A/B: 0/0; " % (size, size + 2)
            + TRUST,
            "functions whose address moved: 0",
            "verdict: NO CHANGE; LINK-ONLY: 1"])

        # R4: a narrow form that is NOT the same operation (mov.n a10 -> mov.n a11) -> UNDECIDED
        st = copy_case(tmp, "relax/a")
        elf = os.path.join(st, "firmware.elf")
        _, _, lo = symbol_entry_offset(mod, elf, "lo_caller")
        patch(elf, elf_file_offset(mod, elf, lo + 3), bytes([0xAD, 0x02]), bytes([0xBD, 0x02]))
        rc, lines = run_case(mod, st, os.path.join(STORES, "relax", "b"), None, tmp)
        # 1i': LINK-ONLY, and the stated limit where it bites: A's walk stops at +3, before its one call site (0/1)
        grade("R4 a narrowing that changes the operation", rc, lines, 0, [
            "inputs: 7 loaded, 0 differ",
            "LINK-ONLY lo_caller [src/relax.c.o] -- size 12 -> 16; the walk: an instruction changed width, not to its "
            "own narrow form, at input +3; object code equal; read-only data equal: 0 section(s); call sites checked "
            "A/B: 0/1; " + TRUST,
            "functions whose address moved: 7",
            "verdict: NO CHANGE; LINK-ONLY: 1"])

        # R5: a flipped site whose long side was in call range all along -> UNDECIDED (the reach must cross)
        st = copy_case(tmp, "relax/b")
        elf = os.path.join(st, "firmware.elf")
        _, _, lo = symbol_entry_offset(mod, elf, "lo_caller")
        l32r_pc = lo + 6
        with open(elf, "rb") as f:
            f.seek(elf_file_offset(mod, elf, l32r_pc))
            insn = f.read(3)
        lit = mod.l32r_addr(l32r_pc, insn)
        patch(elf, elf_file_offset(mod, elf, lit), (0x40090000).to_bytes(4, "little"), (0x40070000).to_bytes(4, "little"))
        rc, lines = run_case(mod, os.path.join(STORES, "relax", "a"), st, None, tmp)
        # 1i' (c): B's long call names hi_callee (0x40090000) and calls 0x40070000 -- never LINK-ONLY
        grade("R5 a flip whose reach never crossed", rc, lines, 3, [
            "inputs: 7 loaded, 0 differ",
            "functions whose address moved: 7",
            "UNDECIDED lo_caller: size 12 -> 16: site 0 flipped, but its reach did not cross the call limit; not "
            "LINK-ONLY: B's site 0 calls 0x40070000; its input names hi_callee, at 0x40090000",
            "verdict: UNDECIDED (1)"])

        # R6: only the destination register of a relocated l32r changes -> CHANGED (the mask clears the operand, never
        # the registers)
        st = copy_case(tmp, "null/b")
        obj = os.path.join(st, "src", "g.c.o")
        with open(obj, "rb") as f:
            elf = mod.ELFFile(f)
            text = elf.get_section_by_name(".text.g")
            data = text.data()
            sites = [r["r_offset"] for r in elf.get_section_by_name(".rela.text.g").iter_relocations()
                     if r["r_info_type"] == mod.R_SLOT0_OP and data[r["r_offset"]] & 0x0F == 1]
            off = text["sh_offset"] + sites[0]
            old = data[sites[0]]
        patch(obj, off, bytes([old]), bytes([old ^ 0x10]))
        reseal(st, "src/g.c.o")
        rc, lines = run_case(mod, os.path.join(STORES, "null", "a"), st, None, tmp)
        grade("R6 a register change under a relocation", rc, lines, 1, [
            "inputs: 7 loaded, 1 differ",
            "  differs: src/g.c.o",
            "functions whose address moved: 0",
            "CHANGED func global A+B g [src/g.c.o] -- OUT",
            "verdict: OUT OF SCOPE: g"])

        # R8 (1f): in a fill-only pair, B's literal now names a different symbol (ext -> pick) -> CHANGED, never FILL
        st = copy_case(tmp, "fill/b")
        obj = os.path.join(st, "src", "fp.c.o")
        with open(obj, "rb") as f:
            elf = mod.ELFFile(f)
            syms = list(elf.get_section_by_name(".symtab").iter_symbols())
            new_idx = next(k for k, s in enumerate(syms) if s.name == "pick")
            rs = elf.get_section_by_name(".rela.literal.pick")
            k, rel = next((k, r) for k, r in enumerate(rs.iter_relocations())
                          if r["r_info_type"] == mod.R_32 and syms[r["r_info_sym"]].name == "ext")
            off = rs["sh_offset"] + k * rs["sh_entsize"] + 4
            old = ((rel["r_info_sym"] << 8) | mod.R_32).to_bytes(4, "little")
        patch(obj, off, old, ((new_idx << 8) | mod.R_32).to_bytes(4, "little"))
        reseal(st, "src/fp.c.o")
        rc, lines = run_case(mod, os.path.join(STORES, "fill", "a"), st, None, tmp)
        grade("R8 a relocation to a different target", rc, lines, 1, [
            "inputs: 2 loaded, 1 differ",
            "  differs: src/fp.c.o",
            "functions whose address moved: 1",
            "CHANGED func global A+B pick [src/fp.c.o] -- OUT",
            "verdict: OUT OF SCOPE: pick"])

        # R9 (1f): B's object loses its property table (the section renamed) -> UNDECIDED, never FILL, never guessed
        st = copy_case(tmp, "fill/b")
        obj = os.path.join(st, "src", "fp.c.o")
        with open(obj, "rb") as f:
            raw = f.read()
        assert raw.count(b".xt.prop\0") >= 1
        with open(obj, "wb") as f:
            f.write(raw.replace(b".xt.prop\0", b".xt.qrop\0"))
        with open(obj, "rb") as f:
            assert b".xt.prop\0" not in f.read(), "the plant did not apply"
        reseal(st, "src/fp.c.o")
        rc, lines = run_case(mod, os.path.join(STORES, "fill", "a"), st, None, tmp)
        grade("R9 an object with no property table", rc, lines, 3, [
            "inputs: 2 loaded, 1 differ",
            "  differs: src/fp.c.o",
            "functions whose address moved: 1",
            "UNDECIDED pick [src/fp.c.o]: the fill walk: no property table covers .text.pick",
            "verdict: UNDECIDED (1)"])

        # ---- amendment 1i / 1i' (sections 21-23) ----
        def grow(store_dir, names):
            """B's image: each named function's size +2 (R3's method), so it stands in front of LINK-ONLY."""
            elf = os.path.join(store_dir, "firmware.elf")
            sizes = {}
            for fn in names:
                off, size, _ = symbol_entry_offset(mod, elf, fn)
                patch(elf, off + 8, size.to_bytes(4, "little"), (size + 2).to_bytes(4, "little"))
                sizes[fn] = size
            return sizes

        # L1 (the owner's control): the call swap, with caller in front of LINK-ONLY -- still CHANGED, never LINK-ONLY
        st = copy_case(tmp, "callswap/b")
        grow(st, ["caller"])
        rc, lines = run_case(mod, os.path.join(STORES, "callswap", "a"), st, None, tmp)
        grade("L1 the call swap, in front of LINK-ONLY", rc, lines, 1, CALLSWAP_LINES)

        # L2: rd and rl read read-only data that changed -- not LINK-ONLY; rk reads ktbl, unchanged -- LINK-ONLY
        st = copy_case(tmp, "rodread/b")
        z = grow(st, ["rd", "rk", "rl"])
        rc, lines = run_case(mod, os.path.join(STORES, "rodread", "a"), st, None, tmp)
        grade("L2 read-only data, in front of LINK-ONLY", rc, lines, 3, [
            "inputs: 3 loaded, 1 differ",
            "  differs: src/rr.c.o",
            "LINK-ONLY rk [src/rd.c.o] -- size %d -> %d; the walk: its size differs and it has no long-call site; "
            "object code equal; read-only data equal: 1 section(s); call sites checked A/B: 0/0; " % (z["rk"], z["rk"] + 2)
            + TRUST,
            "functions whose address moved: 0"] + RODREAD_CHANGED + [
            "UNDECIDED rd: size %d -> %d: its size differs and it has no long-call site; not LINK-ONLY: it reads "
            "read-only data that differs: tbl [src/rr.c.o]" % (z["rd"], z["rd"] + 2),
            "UNDECIDED rl: size %d -> %d: its size differs and it has no long-call site; not LINK-ONLY: it reads "
            "read-only data that differs: ltbl [src/rr.c.o]" % (z["rl"], z["rl"] + 2),
            "verdict: UNDECIDED (2); LINK-ONLY: 1"])

        # R10: identical objects whose code cannot be read (a type-17 relocation in both) -- never LINK-ONLY
        st = copy_case(tmp, "null")
        for side in ("a", "b"):
            obj = os.path.join(st, side, "src", "relax.c.o")
            off = reloc_entry_offset(mod, obj, ".rela.text.lo_caller", mod.R_SLOT0_OP)
            patch(obj, off, bytes([mod.R_SLOT0_OP]), bytes([17]))
            reseal(os.path.join(st, side), "src/relax.c.o")
        z = grow(os.path.join(st, "b"), ["lo_caller"])
        rc, lines = run_case(mod, os.path.join(st, "a"), os.path.join(st, "b"), None, tmp)
        grade("R10 unreadable object code is never LINK-ONLY", rc, lines, 3, [
            "inputs: 7 loaded, 0 differ",
            "functions whose address moved: 0",
            "UNDECIDED lo_caller: size %d -> %d: the walk ends 2 bytes before the function does; not LINK-ONLY: its "
            "object code cannot be read: relocation type 17 in a compared section" % (z["lo_caller"], z["lo_caller"] + 2),
            "verdict: UNDECIDED (1)"])

        # R11 (1i', ruled in): a target named by a local name another local shares -- refused. Both stores' f.c.o get
        # the same three patches: the literal names helper's own symbol (not .text.helper's section symbol), and the
        # FILE symbol becomes a local function named helper.
        st = copy_case(tmp, "local")
        for side in ("a", "b"):
            obj = os.path.join(st, side, "src", "f.c.o")
            with open(obj, "rb") as f:
                elf = mod.ELFFile(f)
                symtab = elf.get_section_by_name(".symtab")
                syms = list(symtab.iter_symbols())
                hk = next(k for k, s in enumerate(syms) if s.name == "helper" and s["st_info"]["type"] == "STT_FUNC")
                sk = next(k for k, s in enumerate(syms)
                          if s["st_info"]["type"] == "STT_SECTION" and s["st_shndx"] == syms[hk]["st_shndx"])
                fk = next(k for k, s in enumerate(syms) if s["st_info"]["type"] == "STT_FILE")
                rs = elf.get_section_by_name(".rela.literal.f")
                rk_ = next(k for k, r in enumerate(rs.iter_relocations())
                           if r["r_info_type"] == mod.R_32 and r["r_info_sym"] == sk)
                rel_off = rs["sh_offset"] + rk_ * rs["sh_entsize"] + 4
                file_ent = symtab["sh_offset"] + fk * symtab["sh_entsize"]
                helper_name, file_name = syms[hk]["st_name"], syms[fk]["st_name"]
            patch(obj, rel_off, ((sk << 8) | mod.R_32).to_bytes(4, "little"), ((hk << 8) | mod.R_32).to_bytes(4, "little"))
            patch(obj, file_ent, file_name.to_bytes(4, "little"), helper_name.to_bytes(4, "little"))
            patch(obj, file_ent + 12, bytes([0x04]), bytes([0x02]))  # st_info: local FILE -> local FUNC
            reseal(os.path.join(st, side), "src/f.c.o")
        rc, lines = run_case(mod, os.path.join(st, "a"), os.path.join(st, "b"), None, tmp)
        grade("R11 a local name that repeats refuses", rc, lines, 3, [
            "inputs: 7 loaded, 1 differ",
            "  differs: src/f.c.o",
            "functions whose address moved: 0",
            "CHANGED func local A+B helper [src/f.c.o] -- OUT",
            "UNDECIDED f [src/f.c.o]: a local name that repeats in its object: helper",
            "verdict: UNDECIDED (1)"])

        # R12 (the reviewer's addition, section 23): A's relaxed call re-aimed 0x100 further on, at nothing, at the same
        # size and window, still in range -- in a function accounted RELAX. Expected as 1i'' proposes (UNDECIDED);
        # section 23 predicts it reads RELAX until a ruling.
        st = copy_case(tmp, "relax/a")
        elf = os.path.join(st, "firmware.elf")
        _, lsize, lo = symbol_entry_offset(mod, elf, "lo_caller")
        with open(elf, "rb") as f:
            f.seek(elf_file_offset(mod, elf, lo))
            body = f.read(lsize)
        k, calls = 0, []
        while k < len(body):
            n = mod.insn_len(body[k])
            assert n is not None, "R12: an instruction the walk cannot size in lo_caller"
            if mod.call_n(body[k:k + 3]) is not None:
                calls.append(k)
            k += n
        assert len(calls) == 1, "R12: lo_caller holds %d calls, not 1" % len(calls)
        pc, old = lo + calls[0], body[calls[0]:calls[0] + 3]
        assert mod.call_target(pc, old) == 0x40070000, "R12: the call is not aimed at hi_callee"
        coff = (old[0] >> 6) | (old[1] << 2) | (old[2] << 10)
        coff = (coff + 0x40) & 0x3FFFF
        new = bytes([(old[0] & 0x3F) | ((coff & 3) << 6), (coff >> 2) & 0xFF, (coff >> 10) & 0xFF])
        assert mod.call_target(pc, new) == 0x40070100 and mod.in_call_range(pc, 0x40070100)
        with open(elf, "rb") as f:
            covered = [s.name for s in mod.ELFFile(f).get_section_by_name(".symtab").iter_symbols()
                       if s["st_info"]["type"] in ("STT_FUNC", "STT_OBJECT")
                       and s["st_value"] <= 0x40070100 < s["st_value"] + max(s["st_size"], 1)]
        assert not covered, "R12: 0x40070100 is not nothing: %s" % covered
        patch(elf, elf_file_offset(mod, elf, pc), old, new)
        rc, lines = run_case(mod, st, os.path.join(STORES, "relax", "b"), None, tmp)
        grade("R12 a call aimed at nothing, in a function accounted RELAX", rc, lines, 3, [
            "inputs: 7 loaded, 0 differ",
            "functions whose address moved: 7",
            "UNDECIDED lo_caller: size 12 -> 16: A's site 0 calls 0x40070100; its input names hi_callee, at 0x40070000",
            "verdict: UNDECIDED (1)"])

        # R13 (filed, section 24): the limit. null's lo_caller is 16 bytes in both images (its long call kept), so it is
        # never walked: B's literal re-aimed at nothing reads NO CHANGE, all of its bytes trusted to the link.
        st = copy_case(tmp, "null/b")
        elf = os.path.join(st, "firmware.elf")
        _, lsize, lo = symbol_entry_offset(mod, elf, "lo_caller")
        with open(elf, "rb") as f:
            f.seek(elf_file_offset(mod, elf, lo))
            body = f.read(lsize)
        k, lits = 0, []
        while k < len(body):
            n = mod.insn_len(body[k])
            assert n is not None, "R13: an instruction the walk cannot size in lo_caller"
            if n == 3 and body[k] & 0x0F == 1 and mod.callx_n(body[k + 3:k + 6]) is not None:
                lits.append(mod.l32r_addr(lo + k, body[k:k + 3]))
            k += n
        assert len(lits) == 1, "R13: lo_caller holds %d long calls, not 1" % len(lits)
        lit_off = elf_file_offset(mod, elf, lits[0])
        with open(elf, "rb") as f:
            covered = [s.name for s in mod.ELFFile(f).get_section_by_name(".symtab").iter_symbols()
                       if s["st_info"]["type"] in ("STT_FUNC", "STT_OBJECT")
                       and s["st_value"] <= 0x40100100 < s["st_value"] + max(s["st_size"], 1)]
        assert not covered, "R13: 0x40100100 is not nothing: %s" % covered
        patch(elf, lit_off, (0x40100000).to_bytes(4, "little"), (0x40100100).to_bytes(4, "little"))
        rc, lines = run_case(mod, os.path.join(STORES, "null", "a"), st, None, tmp)
        grade("R13 a call aimed at nothing, in a function never walked (the limit)", rc, lines, 0, [
            "inputs: 7 loaded, 0 differ",
            "functions whose address moved: 0",
            "verdict: NO CHANGE"])

        # T0, both directions
        for case, want_rc, want in [
                ("t0", 0, ["T0: 2 fixed stamp(s) in 1 input(s) built in the tree, 0 not the fixed stamp"]),
                ("t0bad", 1, ["T0: 1 fixed stamp(s) in 1 input(s) built in the tree, 1 not the fixed stamp",
                              "  date 'Nov 12 2026' in src/WiFiManager.c.o"])]:
            lines = []
            rc = mod.t0(os.path.join(STORES, case), "Jan  1 2026", "00:00:00", lines.append)
            grade("T0 " + case, rc, lines, want_rc, want)

    assert total == len(known), "graded %d cases, case_labels() lists %d" % (total, len(known))
    print("image-compare selftest: %d cases, %d failed" % (total, fails))
    return 1 if fails else 0
