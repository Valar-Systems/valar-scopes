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
        "CHANGED data local A+B sec:.rodata [src/rod.c.o] -- OUT (named by: f1, f2)",
        "verdict: OUT OF SCOPE: sec:.rodata (named by: f1, f2)"]),
    ("rodata", ["f1"], 1, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/rod.c.o",
        "functions whose address moved: 0",
        "CHANGED data local A+B sec:.rodata [src/rod.c.o] -- OUT (named by: f1, f2)",
        "verdict: OUT OF SCOPE: sec:.rodata (named by: f1, f2)"]),
    ("rodata", ["f1", "f2"], 0, [
        "inputs: 2 loaded, 1 differ",
        "  differs: src/rod.c.o",
        "functions whose address moved: 0",
        "CHANGED data local A+B sec:.rodata [src/rod.c.o] -- in scope (named by: f1, f2)",
        "verdict: IN SCOPE (1 changed units)"]),
]


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


def run_case(mod, a, b, allow, tmp):
    lines = []
    allow_path = None
    if allow is not None:
        allow_path = os.path.join(tmp, "allow.txt")
        with open(allow_path, "w") as f:
            f.write("\n".join(allow) + "\n")
    rc = mod.compare(a, b, allow_path, None, lines.append)
    return rc, lines


def run(mod):
    fails = 0
    total = 0

    def grade(label, rc, lines, want_rc, want_lines):
        nonlocal fails, total
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
            grade("%s%s" % (case, " allow=" + ",".join(allow) if allow else ""), rc, lines, want_rc, want)

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
        grade("R3 size without a long-call site", rc, lines, 3, [
            "inputs: 7 loaded, 0 differ",
            "functions whose address moved: 0",
            "UNDECIDED ext: size %d -> %d: its size differs and it has no long-call site" % (size, size + 2),
            "verdict: UNDECIDED (1)"])

        # R4: a narrow form that is NOT the same operation (mov.n a10 -> mov.n a11) -> UNDECIDED
        st = copy_case(tmp, "relax/a")
        elf = os.path.join(st, "firmware.elf")
        _, _, lo = symbol_entry_offset(mod, elf, "lo_caller")
        patch(elf, elf_file_offset(mod, elf, lo + 3), bytes([0xAD, 0x02]), bytes([0xBD, 0x02]))
        rc, lines = run_case(mod, st, os.path.join(STORES, "relax", "b"), None, tmp)
        grade("R4 a narrowing that changes the operation", rc, lines, 3, [
            "inputs: 7 loaded, 0 differ",
            "functions whose address moved: 7",
            "UNDECIDED lo_caller: size 12 -> 16: an instruction changed width, not to its own narrow form, at input +3",
            "verdict: UNDECIDED (1)"])

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
        grade("R5 a flip whose reach never crossed", rc, lines, 3, [
            "inputs: 7 loaded, 0 differ",
            "functions whose address moved: 7",
            "UNDECIDED lo_caller: size 12 -> 16: site 0 flipped, but its reach did not cross the call limit",
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

        # T0, both directions
        for case, want_rc, want in [
                ("t0", 0, ["T0: 2 fixed stamp(s) in 1 input(s) built in the tree, 0 not the fixed stamp"]),
                ("t0bad", 1, ["T0: 1 fixed stamp(s) in 1 input(s) built in the tree, 1 not the fixed stamp",
                              "  date 'Nov 12 2026' in src/WiFiManager.c.o"])]:
            lines = []
            rc = mod.t0(os.path.join(STORES, case), "Jan  1 2026", "00:00:00", lines.append)
            grade("T0 " + case, rc, lines, want_rc, want)

    print("image-compare selftest: %d cases, %d failed" % (total, fails))
    return 1 if fails else 0
