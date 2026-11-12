#!/usr/bin/env python3
"""image-compare.py -- #656's layout-invariant image comparison.

The rules are frozen in docs/image-compare.md (sections 3-5 and amendment 1). This file implements them and nothing
else; where the two disagree, the doc is the registration and this file is the bug.

  compare A B [--allow FILE] [--cxxfilt PATH]   two build stores (see STORE below); prints every finding and a verdict
  t0 STORE --date "Jan  1 2026" --time 00:00:00 WiFiManager's stamped strings carry the fixed SOURCE_DATE_EPOCH stamp
  selftest                                      the fixtures in test/fixtures/image-compare, each reading exactly

STORE: a directory holding firmware.elf, firmware.map (from the -Map relink, M0) and inputs.json:
  {"inputs": [{"path": <as the map's LOAD line spells it>, "sha256": <hex>, "copy": <path in the store, or null>}]}

Exit: 0 NO CHANGE or IN SCOPE, 1 OUT OF SCOPE, 3 UNDECIDED, 2 the instrument could not run (never a verdict).
"""
import fnmatch
import hashlib
import io
import json
import os
import re
import subprocess
import sys

try:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.relocation import RelocationSection
except ImportError:  # the rig, never a verdict
    print("image-compare: pyelftools is not installed (pip install pyelftools==0.33)", file=sys.stderr)
    sys.exit(2)

SHF_ALLOC, SHF_EXECINSTR, SHF_MERGE, SHF_STRINGS = 0x2, 0x4, 0x10, 0x20

# Xtensa relocation types (binutils include/elf/xtensa.h).
R_NONE, R_32, R_ASM_EXPAND, R_ASM_SIMPLIFY, R_SLOT0_OP = 0, 1, 11, 12, 20
ALLOWED_RELOCS = {R_NONE: "NONE", R_32: "32", R_ASM_EXPAND: "ASM_EXPAND", R_ASM_SIMPLIFY: "ASM_SIMPLIFY",
                  R_SLOT0_OP: "SLOT0_OP"}

# Section 3: compared = allocated, minus these. Literal and mergeable sections are not units (resolved by reference).
EXCLUDED = re.compile(r"^(\.eh_frame|\.xt\.prop|\.xt\.lit|\.xtensa\.info|\.comment|\.note|\.group|\.debug)")
CALL_LIMIT_LO, CALL_LIMIT_HI = -(1 << 19), (1 << 19) - 4


class Refusal(Exception):
    """A unit the rules cannot decide. Never 'same'."""


def is_literal(name):
    return name.startswith(".literal") or name.endswith(".literal")


def insn_len(b0):
    """Xtensa instruction length from the opcode's low nibble: 8..13 narrow (2), 0..7 wide (3), else refuse."""
    op0 = b0 & 0x0F
    if op0 < 8:
        return 3
    if op0 <= 13:
        return 2
    return None


def mask_operand(bs):
    """An instruction under R_XTENSA_SLOT0_OP with only its relocated operand cleared: the opcode and the registers
    stay compared. -> masked bytes. A format not listed here is refused, never guessed."""
    b = bytearray(bs)
    op0 = b[0] & 0x0F
    if op0 == 1:                      # L32R (RI16): imm16 in bytes 1-2
        b[1] = b[2] = 0
    elif op0 == 5:                    # CALLn: offset18 in bits 6-23
        b[0] &= 0x3F
        b[1] = b[2] = 0
    elif op0 == 6:
        n = (b[0] >> 4) & 3
        if n == 0:                    # J: offset18 in bits 6-23
            b[0] &= 0x3F
            b[1] = b[2] = 0
        elif n == 1:                  # BEQZ/BNEZ/BLTZ/BGEZ (BRI12): imm12 in bits 12-23
            b[1] &= 0x0F
            b[2] = 0
        else:                         # BRI8 branches and LOOP*: imm8 in bits 16-23
            b[2] = 0
    elif op0 == 7:                    # RRI8 branches: imm8 in bits 16-23
        b[2] = 0
    elif op0 == 0xC and len(b) == 2:  # BEQZ.N/BNEZ.N: imm6 in bits 4-5 and 12-15
        b[0] &= 0xCF
        b[1] &= 0x0F
    else:
        raise Refusal("a relocated instruction of a format the masks do not know (opcode nibble %d)" % op0)
    return bytes(b)


def _sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def canon(bs):
    """An instruction the linker may narrow or widen while it relaxes, decoded to (operation, operands...); else None.
    A wide instruction and its narrow form decode to the same tuple. Branch offsets are left out: relocated, and
    compared through their relocation."""
    op0 = bs[0] & 0x0F
    if len(bs) == 3:
        t, s_, r = bs[0] >> 4, bs[1] & 0x0F, bs[1] >> 4
        op1, op2, imm8 = bs[2] & 0x0F, bs[2] >> 4, bs[2]
        if bytes(bs) == bytes((0x80, 0x00, 0x00)):
            return ("ret",)
        if bytes(bs) == bytes((0x90, 0x00, 0x00)):
            return ("retw",)
        if bytes(bs) == bytes((0xF0, 0x20, 0x00)):
            return ("nop",)
        if op0 == 0 and op1 == 0 and op2 == 8:
            return ("add", r, s_, t)
        if op0 == 0 and op1 == 0 and op2 == 2 and s_ == t:
            return ("mov", r, s_)
        if op0 == 2 and r == 0xC:
            return ("addi", t, s_, _sext(imm8, 8))
        if op0 == 2 and r == 2:
            return ("l32i", t, s_, imm8 * 4)
        if op0 == 2 and r == 6:
            return ("s32i", t, s_, imm8 * 4)
        if op0 == 2 and r == 0xA:
            return ("movi", t, _sext((s_ << 8) | imm8, 12))
        if op0 == 6 and (bs[0] >> 4) & 3 == 1 and (bs[0] >> 6) in (0, 1):
            return ("beqz" if (bs[0] >> 6) == 0 else "bnez", s_)
        return None
    if len(bs) == 2:
        t, s_, r = bs[0] >> 4, bs[1] & 0x0F, bs[1] >> 4
        if op0 == 8:
            return ("l32i", t, s_, r * 4)
        if op0 == 9:
            return ("s32i", t, s_, r * 4)
        if op0 == 0xA:
            return ("add", r, s_, t)
        if op0 == 0xB:
            return ("addi", r, s_, -1 if t == 0 else t)
        if op0 == 0xC:
            if not t & 0x8:
                imm7 = ((t & 0x7) << 4) | r
                return ("movi", s_, imm7 - 128 if imm7 >= 96 else imm7)
            return ("beqz" if not t & 0x4 else "bnez", s_)
        if op0 == 0xD and r == 0:
            return ("mov", t, s_)
        if op0 == 0xD and r == 0xF and s_ == 0:
            return {0: ("ret",), 1: ("retw",), 3: ("nop",)}.get(t)
    return None


# ---------------------------------------------------------------- archives and objects

def ar_members(data):
    """GNU ar members as (name, bytes), in archive order. Duplicate names are kept, in order."""
    if data[:8] != b"!<arch>\n":
        raise Refusal("not an ar archive")
    pos, longnames, out = 8, b"", []
    while pos + 60 <= len(data):
        hdr = data[pos:pos + 60]
        name = hdr[:16].decode("latin-1").rstrip()
        size = int(hdr[48:58].decode("latin-1").strip())
        body = data[pos + 60:pos + 60 + size]
        if name in ("/", "/SYM64/"):
            pass
        elif name == "//":
            longnames = body
        else:
            if name.startswith("/") and name[1:].isdigit():
                off = int(name[1:])
                end = longnames.index(b"/\n", off)
                name = longnames[off:end].decode("latin-1")
            elif name.endswith("/"):
                name = name[:-1]
            out.append((name, body))
        pos += 60 + size + (size & 1)
    return out


class Obj:
    """One relocatable object, parsed once: its units, its literal pools, its names for relocation targets."""

    def __init__(self, input_id, data):
        self.input_id = input_id
        self.raw = data
        self._sig = {}
        self.elf = ELFFile(io.BytesIO(data))
        self.secs = list(self.elf.iter_sections())
        self.symtab = self.elf.get_section_by_name(".symtab")
        self.syms = list(self.symtab.iter_symbols()) if self.symtab else []
        self.relocs = {}  # section index -> [(offset, type, symidx, addend)]
        for s in self.secs:
            if isinstance(s, RelocationSection):
                if not s.is_RELA():
                    self.relocs.setdefault(s["sh_info"], []).append(("REL", None, None, None))
                    continue
                lst = self.relocs.setdefault(s["sh_info"], [])
                for r in s.iter_relocations():
                    lst.append((r["r_offset"], r["r_info_type"], r["r_info_sym"], r["r_addend"]))
        self.units = {}       # ident -> section index
        self.sec_ident = {}   # section index -> ident
        self.ambiguous = set()
        first = {}
        rank = {"STT_FUNC": 0, "STT_OBJECT": 1, "STT_NOTYPE": 2}
        for sym in self.syms:
            t, shndx = sym["st_info"]["type"], sym["st_shndx"]
            if not isinstance(shndx, int) or sym["st_value"] != 0 or t not in rank or not sym.name:
                continue
            if sym.name.startswith(".L"):
                continue
            cur = first.get(shndx)
            if cur is None or rank[t] < rank[cur["st_info"]["type"]]:
                first[shndx] = sym
        self.sym_binding = {}
        for i, s in enumerate(self.secs):
            if not self.is_unit(i):
                continue
            sym = first.get(i)
            ident = sym.name if sym is not None else "sec:" + s.name
            self.sym_binding[i] = sym["st_info"]["bind"] if sym is not None else "STB_LOCAL"
            if ident in self.units:
                self.ambiguous.add(ident)
            self.units[ident] = i
            self.sec_ident[i] = ident

    def is_unit(self, i):
        s = self.secs[i]
        if not (s["sh_flags"] & SHF_ALLOC) or s["sh_size"] == 0:
            return False
        if s["sh_type"] not in ("SHT_PROGBITS", "SHT_NOBITS", "SHT_INIT_ARRAY", "SHT_FINI_ARRAY",
                                "SHT_PREINIT_ARRAY"):
            return False
        if EXCLUDED.match(s.name) or is_literal(s.name) or (s["sh_flags"] & SHF_MERGE):
            return False
        return True

    def binding(self, ident):
        return self.sym_binding[self.units[ident]]

    def kind(self, ident):
        return "func" if self.secs[self.units[ident]]["sh_flags"] & SHF_EXECINSTR else "data"

    def sec_bytes(self, i):
        s = self.secs[i]
        return b"" if s["sh_type"] == "SHT_NOBITS" else s.data()

    def addend(self, sec_i, off, rtype, addend):
        """R_XTENSA_32 keeps part of its addend in place (the probe, amendment 1): RELA addend + the word."""
        if rtype == R_32:
            data = self.sec_bytes(sec_i)
            return addend + int.from_bytes(data[off:off + 4], "little")
        return addend

    def name_target(self, from_i, symidx, addend, refs, depth=0):
        """Section 3's table. Adds what it names to refs (for the referrer graph)."""
        sym = self.syms[symidx]
        t, bind, shndx = sym["st_info"]["type"], sym["st_info"]["bind"], sym["st_shndx"]
        if t != "STT_SECTION":
            if bind in ("STB_GLOBAL", "STB_WEAK") or not isinstance(shndx, int):
                refs.add(("G", sym.name))
                return "sym:%s%+d" % (sym.name, addend)
            if shndx in self.sec_ident:
                refs.add(("U", self.input_id, self.sec_ident[shndx]))
            return "local:%s%+d" % (sym.name, addend)
        if not isinstance(shndx, int):
            raise Refusal("a section symbol with no section")
        if shndx == from_i:
            return "self%+d" % addend
        s = self.secs[shndx]
        flags = s["sh_flags"]
        if (flags & SHF_MERGE) and (flags & SHF_STRINGS):
            data = s.data()
            if not 0 <= addend < len(data):
                raise Refusal("a string offset outside its section")
            end = data.find(b"\0", addend)
            if end < 0:
                raise Refusal("an unterminated string")
            return "str:" + data[addend:end].hex()
        if flags & SHF_MERGE:
            n = s["sh_entsize"] or 4
            data = s.data()
            if not 0 <= addend <= len(data) - n:
                raise Refusal("a constant offset outside its section")
            return "cst:" + data[addend:addend + n].hex()
        if is_literal(s.name):
            if depth:
                raise Refusal("a literal that names a literal")
            for (off, rtype, sidx, add) in self.relocs.get(shndx, []):
                if off == addend and rtype == R_32:
                    return "lit(" + self.name_target(shndx, sidx, self.addend(shndx, off, rtype, add), refs,
                                                     depth + 1) + ")"
            data = s.data()
            if not 0 <= addend <= len(data) - 4:
                raise Refusal("a literal offset outside its pool")
            return "lit:0x%08x" % int.from_bytes(data[addend:addend + 4], "little")
        if shndx in self.sec_ident:
            ident = self.sec_ident[shndx]
            if ident in self.ambiguous:
                raise Refusal("a target unit whose name repeats in its object: %s" % ident)
            refs.add(("U", self.input_id, ident))
            return "unit:%s%+d" % (ident, addend)
        raise Refusal("UNNAMED target: %s%+d" % (s.name, addend))

    def signature(self, ident):
        """(size, masked bytes, named relocations, refs) -- or Refusal."""
        if ident not in self._sig:
            try:
                self._sig[ident] = self._signature(ident)
            except Refusal as e:
                self._sig[ident] = e
        if isinstance(self._sig[ident], Refusal):
            raise self._sig[ident]
        return self._sig[ident]

    def _signature(self, ident):
        if ident in self.ambiguous:
            raise Refusal("its name repeats in its object")
        i = self.units[ident]
        s = self.secs[i]
        if s["sh_type"] == "SHT_NOBITS":
            return (s["sh_size"], b"", (), frozenset())
        data = bytearray(s.data())
        named, refs = [], set()
        for (off, rtype, symidx, add) in self.relocs.get(i, []):
            if off == "REL":
                raise Refusal("a REL (not RELA) relocation section")
            if rtype not in ALLOWED_RELOCS:
                raise Refusal("relocation type %d in a compared section" % rtype)
            if rtype == R_32:
                data[off:off + 4] = b"\0\0\0\0"
            elif rtype == R_SLOT0_OP:
                n = insn_len(data[off])
                if n is None:
                    raise Refusal("an opcode nibble of %d under a relocation" % (data[off] & 0xF))
                data[off:off + n] = mask_operand(data[off:off + n])
            if rtype == R_NONE:
                continue
            tgt = self.name_target(i, symidx, self.addend(i, off, rtype, add), refs)
            named.append((off, ALLOWED_RELOCS[rtype], tgt))
        return (s["sh_size"], bytes(data), tuple(sorted(named)), frozenset(refs))


# ---------------------------------------------------------------- the store, the map, the image

class Store:
    def __init__(self, path):
        self.path = path
        for need in ("inputs.json", "firmware.map", "firmware.elf"):
            if not os.path.isfile(os.path.join(path, need)):
                raise Refusal("not a store (no %s): %s" % (need, path))
        rec = os.path.join(path, "record.json")  # written by image-compare-build.ps1; the fixtures have none
        if os.path.isfile(rec):
            with open(rec, encoding="utf-8-sig") as f:
                rec_m0 = json.load(f).get("m0") or {}
            if rec_m0.get("tieExit") != 0 or rec_m0.get("mapAfterStart") is not True:  # amendment 1b's M0
                raise Refusal("the store's M0 did not hold: %s" % path)
            if m0(os.path.join(path, "firmware.elf"), os.path.join(path, "firmware.map"), lambda _l: None) != 0:
                raise Refusal("the store's map no longer ties to its ELF: %s" % path)
        with open(os.path.join(path, "inputs.json"), encoding="utf-8-sig") as f:
            self.inputs = {e["path"]: e for e in json.load(f)["inputs"]}
        self.map = parse_map(os.path.join(path, "firmware.map"))
        with open(os.path.join(path, "firmware.elf"), "rb") as f:
            self.elf_bytes = f.read()
        self.elf = ELFFile(io.BytesIO(self.elf_bytes))
        self._raw, self._parsed = {}, {}

    def input_bytes(self, path):
        e = self.inputs.get(path)
        if e is None:
            raise Refusal("an input the map loads but inputs.json does not record: %s" % path)
        src = os.path.join(self.path, e["copy"]) if e.get("copy") else (e.get("abs") or path)
        with open(src, "rb") as f:
            data = f.read()
        if hashlib.sha256(data).hexdigest() != e["sha256"]:
            raise Refusal("an input whose bytes no longer match its recorded sha256: %s" % path)
        return data

    def members(self, path):
        """input path -> [(input_id, raw bytes)]: one for an object, one per member (in order) for an archive."""
        if path not in self._raw:
            data = self.input_bytes(path)
            if data[:8] == b"!<arch>\n":
                seen, out = {}, []
                for name, body in ar_members(data):
                    k = seen.get(name, 0)
                    seen[name] = k + 1
                    out.append(("%s(%s)#%d" % (path, name, k), body))
                self._raw[path] = out
            else:
                self._raw[path] = [(path, data)]
        return self._raw[path]

    def obj(self, iid, raw):
        """Parsed once, and only when asked: an archive's members are thousands, and most are never read."""
        if iid not in self._parsed:
            self._parsed[iid] = Obj(iid, raw)
        return self._parsed[iid]

    def objects(self, path):
        return [(iid, self.obj(iid, raw)) for iid, raw in self.members(path)]

    def read(self, addr, n):
        for s in self.elf.iter_sections():
            a = s["sh_addr"]
            if (s["sh_flags"] & SHF_ALLOC) and s["sh_type"] == "SHT_PROGBITS" and a <= addr and addr + n <= a + s["sh_size"]:
                return s.data()[addr - a:addr - a + n]
        raise Refusal("no section of the image holds 0x%x+%d" % (addr, n))

    def functions(self):
        out = []
        st = self.elf.get_section_by_name(".symtab")
        for sym in st.iter_symbols():
            if sym["st_info"]["type"] == "STT_FUNC" and sym["st_size"] > 0 and isinstance(sym["st_shndx"], int):
                out.append((sym.name, sym["st_info"]["bind"], sym["st_value"], sym["st_size"]))
        return out


MAP_SEC = re.compile(r"^ (\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$")
MAP_SEC_NAME = re.compile(r"^ (\S+)$")
MAP_SEC_CONT = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$")
MAP_LOAD = re.compile(r"^LOAD (.+)$")
MAP_MEMBER = re.compile(r"^(.*)\(([^()]*)\)$")


def parse_map(path):
    """GNU ld map: LOAD lines, the kept input sections with addresses, the discarded ones."""
    loads, kept, discarded, outsecs = [], [], set(), {}
    state, outsec, pending, pending_out = "head", None, None, None
    with open(path, encoding="latin-1") as f:
        for raw in f:
            line = raw.rstrip("\n").rstrip("\r")
            if line.startswith("Discarded input sections"):
                state = "discard"
                continue
            if line.startswith("Memory Configuration"):
                state = "mem"
                continue
            if line.startswith("Linker script and memory map"):
                state = "script"
                continue
            m = MAP_LOAD.match(line)
            if m:
                if m.group(1).strip() != "linker stubs":
                    loads.append(m.group(1).strip())
                continue
            if state not in ("discard", "script"):
                continue
            if line and not line.startswith(" "):
                toks = line.split()
                outsec = toks[0]
                pending = None
                pending_out = None
                if state == "script":
                    if len(toks) >= 3 and toks[1].startswith("0x") and toks[2].startswith("0x"):
                        outsecs[outsec] = (int(toks[1], 16), int(toks[2], 16))
                    elif len(toks) == 1:
                        pending_out = outsec  # a long name: its address and size wrap to the next line
                continue
            if pending_out is not None:
                toks = line.split()
                if len(toks) == 2 and toks[0].startswith("0x") and toks[1].startswith("0x"):
                    outsecs[pending_out] = (int(toks[0], 16), int(toks[1], 16))
                pending_out = None
            entry = None
            m = MAP_SEC.match(line)
            if m and not m.group(1).startswith("*"):
                entry = (m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4).strip())
                pending = None
            else:
                m = MAP_SEC_NAME.match(line)
                if m and not m.group(1).startswith("*") and not m.group(1).startswith("0x"):
                    pending = m.group(1)
                    continue
                m = MAP_SEC_CONT.match(line)
                if m and pending:
                    entry = (pending, int(m.group(1), 16), int(m.group(2), 16), m.group(3).strip())
                pending = None
            if entry is None:
                continue
            name, addr, size, src = entry
            if state == "discard" or outsec == "/DISCARD/":
                discarded.add((src, name))
            elif size and addr:
                kept.append((addr, size, src, name))
    kept.sort()
    return {"loads": loads, "kept": kept, "kept_set": {(s, n) for (_, _, s, n) in kept}, "discarded": discarded,
            "outsecs": outsecs}


def m0(elf_path, map_path, out):
    """Amendment 1b's M0: the map is this image's map. Every allocated, non-empty section of the ELF appears among the
    map's output sections with the same name, address and size. -> 0 tied, 1 not tied, 2 nothing to compare."""
    with open(elf_path, "rb") as f:
        elf = ELFFile(io.BytesIO(f.read()))
    secs = [(s.name, s["sh_addr"], s["sh_size"]) for s in elf.iter_sections()
            if (s["sh_flags"] & SHF_ALLOC) and s["sh_size"] > 0]
    outsecs = parse_map(map_path)["outsecs"]
    bad = [n for (n, a, z) in secs if outsecs.get(n) != (a, z)]
    out("M0: %d allocated ELF sections, %d tied to the map's output sections, %d not" % (len(secs), len(secs) - len(bad), len(bad)))
    for n in bad:
        out("  not tied: %s" % n)
    if not secs:
        return 2
    return 1 if bad else 0


def split_src(src):
    m = MAP_MEMBER.match(src)
    return (m.group(1), m.group(2)) if m else (src, None)


# ---------------------------------------------------------------- the comparison

class Side:
    """One build: its store, and (input path, member, section name) -> (input_id, Obj, section index)."""

    def __init__(self, store):
        self.store = store

    def locate(self, src, secname):
        path, member = split_src(src)
        raws = self.store.members(path)
        if member is not None:
            raws = [(iid, raw) for (iid, raw) in raws if iid.startswith("%s(%s)#" % (path, member))]
        cands = [(iid, self.store.obj(iid, raw)) for (iid, raw) in raws]
        hits = []
        for iid, o in cands:
            for i, s in enumerate(o.secs):
                if s.name == secname and o.is_unit(i):
                    hits.append((iid, o, i))
        if len(hits) != 1:
            raise Refusal("%d inputs match %s %s" % (len(hits), src, secname))
        return hits[0]


def same_inputs(a, b):
    """Every input both maps load, by path: equal sha256, or differing (to be compared unit by unit)."""
    la, lb = set(a.map["loads"]), set(b.map["loads"])
    findings = []
    if la != lb:
        for p in sorted(la ^ lb):
            findings.append("an input loaded on one side only: %s" % p)
    differ = []
    for p in sorted(la & lb):
        ea, eb = a.inputs.get(p), b.inputs.get(p)
        if ea is None or eb is None:
            findings.append("an input inputs.json does not record: %s" % p)
            continue
        stale = False
        for side, st in (("A", a), ("B", b)):  # the record is believed only once the bytes are seen to match it
            try:
                st.input_bytes(p)
            except (Refusal, OSError) as e:
                findings.append("%s (%s)" % (e, side))
                stale = True
        if not stale and ea["sha256"] != eb["sha256"]:
            differ.append(p)
    return differ, findings


def compare_units(A, B, path):
    """Every unit of one differing input, A against B: {(input_id, ident): (verdict, detail, obj_a, obj_b)}."""
    oa, ob = A.store.objects(path), B.store.objects(path)
    out = {}
    ia = {iid: o for iid, o in oa}
    ib = {iid: o for iid, o in ob}
    for iid in sorted(set(ia) | set(ib)):
        a, b = ia.get(iid), ib.get(iid)
        idents = set(a.units if a else ()) | set(b.units if b else ())
        for ident in sorted(idents):
            key = (iid, ident)
            if a is None or ident not in a.units:
                out[key] = ("NEW", "", None, b)
                continue
            if b is None or ident not in b.units:
                out[key] = ("GONE", "", a, None)
                continue
            try:
                sa, sb = a.signature(ident), b.signature(ident)
            except Refusal as e:
                out[key] = ("UNDECIDED", str(e), a, b)
                continue
            if sa[:3] == sb[:3]:
                out[key] = ("SAME", "", a, b)
            else:
                k = offsets_only(sa, sb) if a.kind(ident) == "func" else 0
                out[key] = ("CHANGED (load/store offsets only: %d)" % k if k else "CHANGED", "", a, b)
    return out


def ls_key(bs):
    """A load or store, decoded without its offset immediate: (form, op, registers); else None. ADDI/ADDMI/MOVI are
    left out on purpose: they carry constants as often as offsets, and a constant is not an offset."""
    op0 = bs[0] & 0x0F
    t, s_, r = bs[0] >> 4, bs[1] & 0x0F, bs[1] >> 4
    if len(bs) == 3 and op0 == 2 and r in (0, 1, 2, 4, 5, 6, 9):       # L8UI L16UI L32I S8I S16I S32I L16SI
        return ("rri8", r, t, s_)
    if len(bs) == 3 and op0 == 3 and r in (0, 4, 8, 0xC):              # LSI SSI LSIU SSIU
        return ("lsci", r, t, s_)
    if len(bs) == 2 and op0 in (8, 9):                                 # L32I.N S32I.N (imm4 in r)
        return ("n", op0, t, s_)
    return None


def offsets_only(sa, sb):
    """For a CHANGED function: the number of instructions that differ ONLY in a load/store offset, when that is the
    whole difference (same size, same named relocations, the same instruction boundaries); else 0."""
    if sa[0] != sb[0] or sa[2] != sb[2]:
        return 0
    da, db = sa[1], sb[1]
    i = n = 0
    while i < len(da):
        la, lb = insn_len(da[i]), insn_len(db[i])
        if la is None or la != lb or i + la > len(da):
            return 0
        if da[i:i + la] != db[i:i + la]:
            ka, kb = ls_key(da[i:i + la]), ls_key(db[i:i + la])
            if ka is None or ka != kb:
                return 0
            n += 1
        i += la
    return n


def callx_n(bs):
    """CALLX0/4/8/12 -> n (0..3), else None. op0=0, t=0xC|n, r=0, op1=op2=0."""
    if len(bs) < 3 or (bs[0] & 0x0F) != 0 or (bs[0] >> 4) < 0xC or (bs[1] >> 4) != 0 or bs[2] != 0:
        return None
    return (bs[0] >> 4) & 3


def call_n(bs):
    """CALL0/4/8/12 -> n, else None. op0=5."""
    if len(bs) < 3 or (bs[0] & 0x0F) != 5:
        return None
    return (bs[0] >> 4) & 3


def call_target(pc, bs):
    off = (bs[0] >> 6) | (bs[1] << 2) | (bs[2] << 10)
    if off & (1 << 17):
        off -= 1 << 18
    return (pc & ~3) + (off << 2) + 4


def l32r_addr(pc, bs):
    imm = bs[1] | (bs[2] << 8)
    return (((pc + 3) & ~3) + ((0xFFFC0000 | (imm << 2)) - (1 << 32))) & 0xFFFFFFFF


def relax_walk(obj, sec_i, store, addr, size):
    """Amendment 1 (a) and 1a, one side: walk the final copy against the input.
    -> (n, r, sites, narrowed, widened) or Refusal."""
    inp = obj.sec_bytes(sec_i)
    rel_at, expand_at = set(), set()
    for (off, rtype, _s, _a) in obj.relocs.get(sec_i, []):
        if off == "REL":
            raise Refusal("a REL relocation section")
        if rtype == R_ASM_SIMPLIFY:
            raise Refusal("an ASM_SIMPLIFY site")
        if rtype == R_ASM_EXPAND:
            expand_at.add(off)
        elif rtype != R_NONE:
            rel_at.add(off)
    if not expand_at:
        raise Refusal("its size differs and it has no long-call site")
    fin = store.read(addr, size)
    i = j = r = narrowed = widened = 0
    sites = []
    while i < len(inp):
        li = insn_len(inp[i])
        if li is None:
            raise Refusal("an opcode nibble of %d in the walk" % (inp[i] & 0xF))
        if i in expand_at:
            nx = callx_n(inp[i + 3:i + 6])
            if li != 3 or (inp[i] & 0x0F) != 1 or nx is None:
                raise Refusal("a long-call site that is not an adjacent l32r/callx pair")
            if j + 3 <= len(fin) and call_n(fin[j:j + 3]) == nx:
                sites.append((len(sites), True, addr + j, call_target(addr + j, fin[j:j + 3])))
                r += 1
                i, j = i + 6, j + 3
                continue
            if j + 6 <= len(fin) and (fin[j] & 0x0F) == 1 and callx_n(fin[j + 3:j + 6]) == nx:
                lit = l32r_addr(addr + j, fin[j:j + 3])
                target = int.from_bytes(store.read(lit, 4), "little")
                sites.append((len(sites), False, addr + j, target))
                i, j = i + 6, j + 6
                continue
            raise Refusal("a long-call site that is neither relaxed nor kept")
        lf = insn_len(fin[j]) if j < len(fin) else None
        if lf is None or j + lf > len(fin):
            raise Refusal("the final copy ends inside the walk, or holds an opcode the walk cannot size")
        if lf == li:
            if i in rel_at:
                if mask_operand(fin[j:j + li]) != mask_operand(inp[i:i + li]):
                    raise Refusal("a relocated instruction changed more than its operand at +%d" % i)
            elif fin[j:j + li] != inp[i:i + li]:
                raise Refusal("an unrelocated instruction differs at input +%d" % i)
        else:  # amendment 1a: the linker narrows or widens an instruction to its own other form
            ci, cf = canon(inp[i:i + li]), canon(fin[j:j + lf])
            if ci is None or ci != cf:
                raise Refusal("an instruction changed width, not to its own %s form, at input +%d" % (
                    "narrow" if li == 3 else "wide", i))
            if li == 3:
                narrowed += 1
            else:
                widened += 1
        i, j = i + li, j + lf
    if j != len(fin):
        raise Refusal("the walk ends %d bytes before the function does" % (len(fin) - j))
    if len(fin) != len(inp) - 3 * r - narrowed + widened:
        raise Refusal("the size does not reconcile: %d != %d - 3x%d - %d + %d" % (len(fin), len(inp), r, narrowed, widened))
    return len(expand_at), r, sites, narrowed, widened


def reach(pc, target):
    return target - ((pc & ~3) + 4)


def in_call_range(pc, target):
    d = reach(pc, target)
    return CALL_LIMIT_LO <= d <= CALL_LIMIT_HI and d % 4 == 0


def demangler(path):
    cache = {}

    def dm(name):
        if not path or not name.startswith("_Z"):
            return name
        if name not in cache:
            try:
                cache[name] = subprocess.run([path, name], capture_output=True, text=True, check=True).stdout.strip()
            except (OSError, subprocess.CalledProcessError):
                cache[name] = name
        return cache[name]
    return dm


def src_of(iid):
    """An input id back to the map's spelling: 'path' or 'path(member)' (the '#k' occurrence index dropped)."""
    m = re.match(r"^(.*\(.*\))#\d+$", iid)
    return m.group(1) if m else iid


def kept_in(side, obj, ident, iid):
    """Is this unit in the image? Through the map, by its input and section name."""
    if obj is None or ident not in obj.units:
        return False
    return (src_of(iid), obj.secs[obj.units[ident]].name) in side.store.map["kept_set"]


def function_table(side):
    """Every function of the image: key -> [(size, (src, section), addr)]. Globals by name; locals by place + name."""
    import bisect
    kept_list = side.store.map["kept"]
    starts = [k[0] for k in kept_list]
    tab = {}
    for name, bind, addr, size in side.store.functions():
        k = bisect.bisect_right(starts, addr) - 1
        loc = None
        if k >= 0:
            s_addr, s_size, src, sec = kept_list[k]
            if s_addr <= addr < s_addr + s_size:
                loc = (src, sec)
        fkey = ("G", name) if bind in ("STB_GLOBAL", "STB_WEAK") else ("L", loc[0] if loc else None, name)
        tab.setdefault(fkey, []).append((size, loc, addr))
    return tab


def compare(path_a, path_b, allow, cxxfilt, out):
    A, B = Side(Store(path_a)), Side(Store(path_b))
    dm = demangler(cxxfilt)
    differ, undecided = same_inputs(A.store, B.store)
    out("inputs: %d loaded, %d differ" % (len(set(A.store.map["loads"])), len(differ)))
    for p in differ:
        out("  differs: %s" % p)

    # 1. the object layer: every unit of every differing input (section 3)
    units = {}
    for p in differ:
        try:
            units.update(compare_units(A, B, p))
        except Refusal as e:
            undecided.append("input %s: %s" % (p, e))
    changed = {}
    for key, (v, detail, oa, ob) in sorted(units.items()):
        if v == "SAME":
            continue
        iid, ident = key
        ka, kb = kept_in(A, oa, ident, iid), kept_in(B, ob, ident, iid)
        if not (ka or kb):
            out("not linked: %s %s [%s]" % (v, dm(ident), iid))
            continue
        if v == "UNDECIDED":
            undecided.append("%s [%s]: %s" % (dm(ident), iid, detail))
            continue
        obj = ob if ob is not None else oa
        changed[key] = (v, obj.binding(ident), obj.kind(ident), ka, kb, oa, ob)

    # 2. the image layer: every function (name, size) difference earns one mechanism (section 4, amendment 1 (a))
    ta, tb = function_table(A), function_table(B)
    pairs, moved = [], 0
    lone_a = {k: v for k, v in ta.items() if k not in tb}
    lone_b = {k: v for k, v in tb.items() if k not in ta}
    for k in sorted(set(ta) & set(tb), key=repr):
        pairs.append((k, ta[k], tb[k]))
    # a local whose section was renamed (.iram1.N renumbers): pair it by input and name
    for k in sorted(lone_a, key=repr):
        if k[0] == "L":
            mates = [kb for kb in lone_b if kb[0] == "L" and kb[1:] == k[1:]]
            if len(mates) == 1:
                pairs.append((k, lone_a[k], lone_b.pop(mates[0])))
                continue
        pairs.append((k, lone_a[k], []))
    for k in sorted(lone_b, key=repr):
        pairs.append((k, [], lone_b[k]))

    relaxed, picks, gcs = [], [], []
    for fkey, ea, eb in pairs:
        name = dm(fkey[1] if fkey[0] == "G" else fkey[2])
        if len(ea) > 1 or len(eb) > 1:
            if sorted(x[0] for x in ea) != sorted(x[0] for x in eb):
                undecided.append("%s: its name repeats in one input, and the sizes differ" % name)
            continue
        if ea and eb and ea[0][2] != eb[0][2]:
            moved += 1
        if ea and eb and ea[0][0] == eb[0][0] and ea[0][1] == eb[0][1]:
            continue
        if (ea and not ea[0][1]) or (eb and not eb[0][1]):
            undecided.append("%s: no input section in the map holds it" % name)
            continue
        try:
            la = A.locate(*ea[0][1]) if ea else None
            lb = B.locate(*eb[0][1]) if eb else None
        except Refusal as e:
            undecided.append("%s: %s" % (name, e))
            continue
        ka = (la[0], la[1].sec_ident[la[2]]) if la else None
        kb = (lb[0], lb[1].sec_ident[lb[2]]) if lb else None
        if ka in changed or kb in changed:
            continue  # CHANGED: the object layer has it
        if (ka in units and units[ka][0] == "UNDECIDED") or (kb in units and units[kb][0] == "UNDECIDED"):
            continue  # UNDECIDED: the object layer has it
        if not (ea and eb):
            gcs.append((name, A if ea else B, ka if ea else kb))
            continue
        if ka != kb:  # PICK: a different copy is kept
            try:
                sa, sb = la[1].signature(ka[1]), lb[1].signature(kb[1])
            except Refusal as e:
                undecided.append("PICK %s: %s" % (name, e))
                continue
            if sa[:3] != sb[:3]:
                changed[kb] = ("CHANGED (PICK)", lb[1].binding(kb[1]), lb[1].kind(kb[1]), True, True, la[1], lb[1])
                continue
            picks.append("%s: %s -> %s, the two copies the same" % (name, ka[0], kb[0]))
            if ea[0][0] == eb[0][0]:
                continue
        try:  # the same input unit (or an equal copy) and a different size: RELAX, counted, or UNDECIDED
            na, ra, sites_a, nar_a, wid_a = relax_walk(la[1], la[2], A.store, ea[0][2], ea[0][0])
            nb, rb, sites_b, nar_b, wid_b = relax_walk(lb[1], lb[2], B.store, eb[0][2], eb[0][0])
            if na != nb:
                raise Refusal("the two inputs carry different long-call counts")
            flips = []
            for (k, rel_a, pc_a, t_a), (_k, rel_b, pc_b, t_b) in zip(sites_a, sites_b):
                if rel_a == rel_b:
                    continue
                long_pc, long_t = (pc_b, t_b) if rel_a else (pc_a, t_a)
                call_pc, call_t = (pc_a, t_a) if rel_a else (pc_b, t_b)
                if in_call_range(long_pc, long_t) or not in_call_range(call_pc, call_t):
                    raise Refusal("site %d flipped, but its reach did not cross the call limit" % k)
                flips.append("site %d: call in %s, reach %+d; long in %s, reach %+d" % (
                    k, "A" if rel_a else "B", reach(call_pc, call_t), "B" if rel_a else "A", reach(long_pc, long_t)))
            if ra == rb:
                raise Refusal("a size difference with no relaxed-site difference")
            relaxed.append("%s: n=%d rA=%d rB=%d narrowed A/B=%d/%d widened A/B=%d/%d dsize=%+d; %s" % (
                name, na, ra, rb, nar_a, nar_b, wid_a, wid_b, eb[0][0] - ea[0][0], "; ".join(flips)))
        except Refusal as e:
            undecided.append("%s: size %d -> %d: %s" % (name, ea[0][0], eb[0][0], e))

    # GC (section 4, amendment 1a (b)): every unit of that image naming it is CHANGED, or is itself a GC unit that
    # passes -- taken to a fixed point, so a chain counts only when it is rooted in a CHANGED unit
    refs = {(id(side), key): gc_referrers(side, key) for name, side, key in gcs}
    passed, grew = set(), True
    while grew:
        grew = False
        for name, side, key in gcs:
            k = (id(side), key)
            who, refused = refs[k]
            if k in passed or refused or not who:
                continue
            if all(w in changed or (id(side), w) in passed for w in who):
                passed.add(k)
                grew = True
    for name, side, key in gcs:
        who, refused = refs[(id(side), key)]
        names = ", ".join(dm(w[1]) for w in who) if who else "no unit at all"
        if (id(side), key) in passed:
            out("GC %s [%s], kept in %s only; named only by CHANGED or GC units: %s" % (
                name, key[0], "A" if side is A else "B", names))
        else:
            undecided.append("GC %s [%s]: named by %s%s" % (name, key[0], names, " (a unit could not be read)" if refused else ""))
    for p in picks:
        out("PICK " + p)
    for r in relaxed:
        out("RELAX " + r)
    out("functions whose address moved: %d" % moved)

    # 3. the verdict (section 5)
    pats = []
    if allow:
        with open(allow) as f:
            pats = [ln.strip() for ln in f if ln.strip() and not ln.startswith("#")]
    def allowed(ident):
        return any(fnmatch.fnmatchcase(dm(ident), p) for p in pats)
    in_scope = {k for k in changed if allowed(k[1])}
    grew = True
    while grew:  # a local unit is in scope when every unit naming it is (on the list, or itself in scope)
        grew = False
        for key, (v, bind, kind, ka, kb, oa, ob) in changed.items():
            if key in in_scope or bind != "STB_LOCAL":
                continue
            rs = local_referrers(key, oa, ob)
            if rs and all(r in in_scope or allowed(r[1]) for r in rs):
                in_scope.add(key)
                grew = True
    def named_by(key, oa, ob):
        """Amendment 1d: a changed data unit's referrers, the kept units of its own object on either side. An unsplit
        section is attributed to ALL of them, never to a guessed one."""
        iid, ident = key
        names = set()
        for side, obj in ((A, oa), (B, ob)):
            if obj is None:
                continue
            for r in local_referrers(key, obj if side is A else None, obj if side is B else None):
                if kept_in(side, obj, r[1], iid):
                    names.add(dm(r[1]))
        return " (named by: %s)" % ", ".join(sorted(names)) if names else " (named by: no kept unit)"
    outside = []
    for key in sorted(changed):
        v, bind, kind, ka, kb, oa, ob = changed[key]
        where = "A+B" if ka and kb else ("A" if ka else "B")
        refs = named_by(key, oa, ob) if kind == "data" else ""
        out("%s %s %s %s %s [%s] -- %s%s" % (v, kind, bind[4:].lower(), where, dm(key[1]), key[0],
                                            "in scope" if key in in_scope else "OUT", refs))
        if key not in in_scope:
            outside.append(dm(key[1]) + refs)
    for u in undecided:
        out("UNDECIDED " + u)
    if undecided:
        out("verdict: UNDECIDED (%d)" % len(undecided))
        return 3
    if not changed:
        out("verdict: NO CHANGE")
        return 0
    if outside:
        out("verdict: OUT OF SCOPE: " + ", ".join(outside))
        return 1
    out("verdict: IN SCOPE (%d changed units)" % len(changed))
    return 0


def local_referrers(key, oa, ob):
    """Units of the same object, on either side, whose relocations name this unit. An unreadable one counts."""
    iid, ident = key
    found = set()
    for obj in (oa, ob):
        if obj is None:
            continue
        for other in obj.units:
            if other == ident:
                continue
            try:
                refs = obj.signature(other)[3]
            except Refusal:
                found.add((iid, other))
                continue
            if ("U", iid, ident) in refs:
                found.add((iid, other))
    return found


def gc_referrers(side, key):
    """A function kept in one image only: every kept unit of that image naming it. -> ([(input_id, ident)], refused)"""
    iid, ident = key
    want_g, want_u = ("G", ident), ("U", iid, ident)
    needle = ident.encode()
    who, refused = [], False
    kept = side.store.map["kept_set"]
    for p in side.store.map["loads"]:
        for oid, raw in side.store.members(p):
            if oid != iid and needle not in raw:
                continue  # an object cannot name a global it holds no string for
            obj = side.store.obj(oid, raw)
            for other, i in obj.units.items():
                if (src_of(oid), obj.secs[i].name) not in kept or (oid, other) == key:
                    continue
                try:
                    refs = obj.signature(other)[3]
                except Refusal:
                    refused = True
                    continue
                if want_g in refs or (oid == iid and want_u in refs):
                    who.append((oid, other))
    return who, refused


# ---------------------------------------------------------------- T0

DATE_RE = re.compile(rb"[A-Z][a-z]{2} [ 0-9][0-9] [0-9]{4}")
TIME_RE = re.compile(rb"[0-9]{2}:[0-9]{2}:[0-9]{2}")


MONTHS = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")
STAMP_RE = re.compile(rb"([A-Z][a-z]{2} [ 0-9][0-9] [0-9]{4})( [0-9]{2}:[0-9]{2}:[0-9]{2})?")


def t0(path, date, time_, out):
    """T0' (amendment 1c): every input BUILT IN THE TREE (an inputs.json entry with a copy) has every __DATE__-shaped
    string equal to the fixed date, and every time beside one equal to the fixed time; at least one fixed stamp is
    found (the control). Precompiled package archives are outside it: their hashes are equal across builds."""
    st = Store(path)
    found, bad, where = 0, [], set()
    for p, e in sorted(st.inputs.items()):
        if not e.get("copy"):
            continue
        data = st.input_bytes(p)
        for m in STAMP_RE.finditer(data):
            d = m.group(1).decode()
            if d[:3] not in MONTHS:
                continue
            t = m.group(2).decode().strip() if m.group(2) else None
            if d != date:
                bad.append("date %r in %s" % (d, p))
            elif t is not None and t != time_:
                bad.append("time %r in %s" % (t, p))
            else:
                found += 1
                where.add(p)
    out("T0: %d fixed stamp(s) in %d input(s) built in the tree, %d not the fixed stamp" % (found, len(where), len(bad)))
    for b in bad:
        out("  " + b)
    if bad:
        return 1
    if found == 0:
        out("T0: no stamp found at all: the check cannot see the strings it exists to check")
        return 2
    return 0


# ---------------------------------------------------------------- main

def main(argv):
    out = print
    if len(argv) >= 3 and argv[0] == "compare":
        allow = cxxfilt = None
        rest = argv[3:]
        while rest:
            if rest[0] == "--allow" and len(rest) > 1:
                allow, rest = rest[1], rest[2:]
            elif rest[0] == "--cxxfilt" and len(rest) > 1:
                cxxfilt, rest = rest[1], rest[2:]
            else:
                print(__doc__)
                return 2
        try:
            return compare(argv[1], argv[2], allow, cxxfilt, out)
        except Refusal as e:
            out("verdict: UNDECIDED (the instrument refused: %s)" % e)
            return 3
    if len(argv) == 3 and argv[0] == "m0":
        return m0(argv[1], argv[2], out)
    if len(argv) == 6 and argv[0] == "t0" and argv[2] == "--date" and argv[4] == "--time":
        return t0(argv[1], argv[3], argv[5], out)
    if argv == ["selftest"]:
        here = os.path.dirname(os.path.abspath(__file__))
        sys.path.insert(0, os.path.join(here, "..", "test", "host"))
        import traceback
        try:
            import image_compare_selftest
            return image_compare_selftest.run(sys.modules[__name__])
        except Exception:  # a crash is the instrument failing, never a red
            traceback.print_exc()
            return 2
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
