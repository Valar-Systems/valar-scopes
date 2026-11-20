#!/usr/bin/env python3
"""The pre-ship image-level text check (step 6b, docs/blipscope-map-client-steps.md "## 6b").

Which strings does a candidate build ADD against a reference build, read from the boundaries the compiler already wrote
in each object file, not from the linked image:

  T1  every PROGBITS section flagged mergeable-strings (SHF_MERGE|SHF_STRINGS, the .rodata.str1.N family), split on NUL.
  T2  every OBJECT symbol in a non-T1 .rodata* section, read by its own extent; text only if the bytes before the first
      NUL are valid UTF-8 with no control character but tab, CR and LF, at least one NUL follows, only NULs come after
      it, and there is at least one character. A symbol any of whose bytes lies under a relocation is never text
      (a pointer slot in a relocatable xtensa object holds its target's offset in place).
  T3  each added string is looked for in the candidate's firmware.bin (s + NUL). Not found = "compiled, not linked",
      reported apart and never counted as added.

Every printed string is masked: IPv4-shaped, hex runs of 16 or more, MAC-shaped, decimals with 3 or more places.
Structure comes from the toolchain's own readelf; bytes are read at the offsets it reports. Standard library only.

    strings_elf.py --readelf <readelf> --ref <objdir> --cand <objdir> --bin <candidate firmware.bin>
    strings_elf.py --selftest [--readelf <readelf>]

Exit: 0 read; 2 RIG or a refusal. --selftest: 0 every row holds; 1 a row is off; 2 RIG.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unicodedata


class Rig(Exception):
    """The instrument cannot read this input: never a reading."""


# ---------------------------------------------------------------------------------------------------- readelf

SEC_RE = re.compile(r'^\s*\[\s*(\d+)\]\s+(\S*)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)'
                    r'\s+([A-Za-z]*)\s+(\d+)\s+(\d+)\s+(\d+)\s*$')
SYM_RE = re.compile(r'^\s*(\d+):\s+([0-9a-f]+)\s+(0x[0-9a-f]+|\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s*(.*)$')
RELHDR_RE = re.compile(r"^Relocation section '([^']+)' at offset 0x[0-9a-f]+ contains (\d+) entr")
RELROW_RE = re.compile(r'^([0-9a-f]+)\s+([0-9a-f]+)\s+(\S+)')
RELOC_SPAN = 4   # a relocation covers 4 bytes from its offset (R_XTENSA_32 on data)


def readelf(tool, args, path):
    env = dict(os.environ, LC_ALL="C")
    try:
        p = subprocess.run([tool] + args + [path], capture_output=True, env=env)
    except OSError as e:
        raise Rig("readelf %s could not run: %s" % (tool, type(e).__name__))
    if p.returncode != 0:
        raise Rig("readelf %s exited %d on %s" % (" ".join(args), p.returncode, os.path.basename(path)))
    return p.stdout.decode("utf-8", "replace")


def read_object(tool, path):
    """Sections (with their bytes), OBJECT symbols and relocation offsets per target section, from readelf."""
    sections = {}
    for line in readelf(tool, ["-S", "-W"], path).split("\n"):
        m = SEC_RE.match(line)
        if m:
            idx = int(m.group(1))
            sections[idx] = {"name": m.group(2), "type": m.group(3), "off": int(m.group(5), 16),
                             "size": int(m.group(6), 16), "es": int(m.group(7), 16), "flags": m.group(8),
                             "info": int(m.group(10))}
    if not sections:
        raise Rig("no section headers parsed from %s" % os.path.basename(path))
    with open(path, "rb") as f:
        raw = f.read()
    for s in sections.values():
        if s["type"] == "NOBITS":
            s["data"] = b""
            continue
        s["data"] = raw[s["off"]:s["off"] + s["size"]]
        if len(s["data"]) != s["size"]:
            raise Rig("section %s of %s: %d bytes read, %d reported" % (s["name"], os.path.basename(path),
                                                                       len(s["data"]), s["size"]))
    symbols = []
    for line in readelf(tool, ["-s", "-W"], path).split("\n"):
        m = SYM_RE.match(line)
        if m and m.group(4) == "OBJECT" and m.group(7).isdigit():
            size = int(m.group(3), 16) if m.group(3).startswith("0x") else int(m.group(3))
            symbols.append({"value": int(m.group(2), 16), "size": size, "shndx": int(m.group(7)),
                            "name": m.group(8).strip()})
    by_name = {s["name"]: i for i, s in sections.items()}
    relocs = {}
    hdr, want, got = None, 0, 0
    for line in readelf(tool, ["-r", "-W"], path).split("\n") + [""]:
        m = RELHDR_RE.match(line)
        if m or (hdr is not None and not line.strip() and got >= want):
            if hdr is not None and got != want:
                raise Rig("relocation section %s of %s: %d rows parsed, %d reported" % (hdr, os.path.basename(path),
                                                                                       got, want))
            hdr = None
        if m:
            hdr, want, got = m.group(1), int(m.group(2)), 0
            if hdr not in by_name:
                raise Rig("relocation section %s not among the section headers" % hdr)
            target = sections[by_name[hdr]]["info"]
            relocs.setdefault(target, [])
            continue
        if hdr is not None:
            r = RELROW_RE.match(line)
            if r:
                relocs[sections[by_name[hdr]]["info"]].append(int(r.group(1), 16))
                got += 1
    if hdr is not None and got != want:
        raise Rig("relocation section %s of %s: %d rows parsed, %d reported" % (hdr, os.path.basename(path), got, want))
    return sections, symbols, relocs


# ---------------------------------------------------------------------------------------------------- the rules

def is_t1(sec):
    return sec["type"] == "PROGBITS" and "M" in sec["flags"] and "S" in sec["flags"]


def is_text(b):
    """Q-6b-1 (ruled) and Q-6b-2: UTF-8 text, no control but tab/CR/LF, then at least one NUL, then only NULs."""
    i = b.find(b"\0")
    if i < 1:
        return False                          # no NUL, or no character before it
    if b[i:].strip(b"\0"):
        return False                          # something after the NUL other than NULs
    try:
        t = b[:i].decode("utf-8")
    except UnicodeDecodeError:
        return False
    return all(c in "\t\r\n" or unicodedata.category(c) != "Cc" for c in t)


def t1_strings(sec):
    if sec["es"] != 1:
        raise Rig("a wide strings section (%s, entsize %d), not handled" % (sec["name"], sec["es"]))
    return [p for p in sec["data"].split(b"\0") if p]


def t2_strings(sections, symbols, relocs):
    """Returns ([(text, section name)], skipped for relocations)."""
    out, skipped = [], 0
    for sym in symbols:
        sec = sections.get(sym["shndx"])
        if sec is None or not sec["name"].startswith(".rodata") or is_t1(sec) or sym["size"] == 0:
            continue
        lo, hi = sym["value"], sym["value"] + sym["size"]
        if any(o < hi and o + RELOC_SPAN > lo for o in relocs.get(sym["shndx"], ())):
            skipped += 1
            continue
        b = sec["data"][lo:hi]
        if len(b) != sym["size"]:
            raise Rig("symbol %s runs past its section %s" % (sym["name"], sec["name"]))
        if is_text(b):
            out.append((b[:b.find(b"\0")], sec["name"]))
    return out, skipped


def collect(tool, objdir):
    """{string bytes: [(tier, object, section)]}, object count, relocation-skipped count."""
    if not os.path.isdir(objdir):
        raise Rig("not a directory: %s" % objdir)
    objs = sorted(os.path.join(d, f) for d, _, fs in os.walk(objdir) for f in fs if f.endswith(".o"))
    if not objs:
        raise Rig("no objects under %s" % objdir)
    found, skipped = {}, 0
    for path in objs:
        rel = os.path.relpath(path, objdir).replace("\\", "/")
        sections, symbols, relocs = read_object(tool, path)
        for sec in sections.values():
            if is_t1(sec):
                for s in t1_strings(sec):
                    found.setdefault(s, []).append(("T1", rel, sec["name"]))
        t2, k = t2_strings(sections, symbols, relocs)
        skipped += k
        for s, secname in t2:
            found.setdefault(s, []).append(("T2", rel, secname))
    return found, len(objs), skipped


# ---------------------------------------------------------------------------------------------------- masks, report

MASKS = [
    ("MAC", re.compile(r'(?<![0-9A-Fa-f:])[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}(?![0-9A-Fa-f:])')),
    ("IPV4", re.compile(r'(?<![\d.])\d{1,3}(?:\.\d{1,3}){3}(?![\d.])')),
    ("HEX", re.compile(r'[0-9A-Fa-f]{16,}')),
    ("DEC", re.compile(r'\d+\.\d{3,}')),
]


def mask(text):
    hit = False
    for kind, rx in MASKS:
        text, n = rx.subn("[MASKED-%s]" % kind, text)
        hit = hit or n > 0
    return text, hit


def show(b):
    t = b.decode("utf-8", "replace")
    out = []
    for c in t:
        if c == "\t":
            out.append("\\t")
        elif c == "\r":
            out.append("\\r")
        elif c == "\n":
            out.append("\\n")
        elif unicodedata.category(c) == "Cc":
            out.append("\\x%02x" % ord(c))
        else:
            out.append(c)
    return "".join(out)


def compare(tool, ref, cand, binpath):
    rf, nr, _ = collect(tool, ref)
    cf, nc, skipped = collect(tool, cand)
    try:
        with open(binpath, "rb") as f:
            image = f.read()
    except OSError as e:
        raise Rig("bin unreadable: %s" % type(e).__name__)
    added, notlinked = [], []
    for s in sorted(set(cf) - set(rf)):
        tiers = sorted(set(t for t, _, _ in cf[s]))
        where = sorted(cf[s])[0]
        row = ("+".join(tiers), where[1], where[2], s)
        (added if (s + b"\0") in image else notlinked).append(row)
    removed = len(set(rf) - set(cf))
    lines, masked = [], 0
    for label, rows in (("ADDED", added), ("NOT-LINKED", notlinked)):
        for tier, obj, sec, s in sorted(rows, key=lambda r: (r[1], r[2], r[3])):
            text, hit = mask(show(s))
            masked += hit
            lines.append('%s %s %s %s "%s"' % (label, tier, obj, sec, text))
    t1 = sum(1 for r in added if "T1" in r[0].split("+"))
    t2 = sum(1 for r in added if r[0] == "T2")
    head = ("strings_elf: ref %d objects, cand %d objects | ref %d strings | cand %d strings | added %d (T1 %d, T2 %d) | "
            "compiled, not linked %d | removed %d | masked %d | T2 skipped for relocations %d"
            % (nr, nc, len(rf), len(cf), len(added), t1, t2, len(notlinked), removed, masked, skipped))
    return {"added": added, "notlinked": notlinked, "removed": removed, "masked": masked, "skipped": skipped,
            "lines": [head] + lines}


# ---------------------------------------------------------------------------------------------------- selftest objects

SHF_ALLOC, SHF_EXEC, SHF_MERGE, SHF_STRINGS, SHF_INFO_LINK = 0x2, 0x4, 0x10, 0x20, 0x40
EM_XTENSA, R_XTENSA_32 = 94, 1


def write_elf(path, secs):
    """secs: [(name, kind, bytes, [(symbol, offset, size)], [reloc offsets])], kind in text|str|ro.
    Writes an ELF32 LE ET_REL xtensa object: .rodata.str1.1-style sections flagged AMS (entsize 1), OBJECT symbols,
    and a .rela.<sec> per section with relocations (R_XTENSA_32 against the first strings section's symbol)."""
    shstr, strtab = bytearray(b"\0"), bytearray(b"\0")

    def name_in(tab, n):
        off = len(tab)
        tab.extend(n.encode() + b"\0")
        return off

    hdrs = [None]                                         # [ (name, type, flags, data, link, info, align, es) ]
    for name, kind, data, _, _ in secs:
        flags = {"text": SHF_ALLOC | SHF_EXEC, "str": SHF_ALLOC | SHF_MERGE | SHF_STRINGS, "ro": SHF_ALLOC}[kind]
        hdrs.append([name, 1, flags, bytes(data), 0, 0, 1 if kind == "str" else 4, 1 if kind == "str" else 0])
    sym_index = len(hdrs)
    syms = [struct.pack("<IIIBBH", 0, 0, 0, 0, 0, 0)]
    secsym = {}
    for i in range(1, sym_index):                        # one section symbol per section
        secsym[i] = len(syms)
        syms.append(struct.pack("<IIIBBH", 0, 0, 0, 3, 0, i))
    for i, (_, _, _, objs, _) in enumerate(secs, start=1):
        for sname, off, size in objs:
            syms.append(struct.pack("<IIIBBH", name_in(strtab, sname), off, size, 1, 0, i))
    strsec = next((i for i, s in enumerate(secs, start=1) if s[1] == "str"), 1)
    rela = []
    for i, (name, _, _, _, offs) in enumerate(secs, start=1):
        if offs:
            data = b"".join(struct.pack("<IIi", o, (secsym[strsec] << 8) | R_XTENSA_32, 0) for o in offs)
            rela.append([".rela" + name, 4, SHF_INFO_LINK, data, None, i, 4, 12])
    hdrs.append([".symtab", 2, 0, b"".join(syms), None, len(syms), 4, 16])
    symtab_idx = len(hdrs) - 1
    hdrs.append([".strtab", 3, 0, bytes(strtab), 0, 0, 1, 0])
    hdrs[symtab_idx][4] = len(hdrs) - 1
    for r in rela:
        r[4] = symtab_idx
        hdrs.append(r)
    hdrs.append([".shstrtab", 3, 0, b"", 0, 0, 1, 0])
    names = [0] + [name_in(shstr, h[0]) for h in hdrs[1:]]
    hdrs[-1][3] = bytes(shstr)
    body, offs = bytearray(), [0]
    for h in hdrs[1:]:
        while (52 + len(body)) % max(h[6], 1):
            body.append(0)
        offs.append(52 + len(body))
        body.extend(h[3])
    while (52 + len(body)) % 4:
        body.append(0)
    shoff = 52 + len(body)
    sh = bytearray(b"\0" * 40)
    for i, h in enumerate(hdrs[1:], start=1):
        sh.extend(struct.pack("<10I", names[i], h[1], h[2], 0, offs[i], len(h[3]), h[4], h[5], h[6], h[7]))
    ident = b"\x7fELF" + bytes([1, 1, 1, 0]) + b"\0" * 8
    ehdr = ident + struct.pack("<HHIIIIIHHHHHH", 1, EM_XTENSA, 1, 0, 0, shoff, 0, 52, 0, 0, 40, len(hdrs), len(hdrs) - 1)
    with open(path, "wb") as f:
        f.write(ehdr + bytes(body) + bytes(sh))


def _dotted(parts):
    return ".".join(parts)


def selftest(tool):
    E = [b"wifi joined", b"fetching aircraft...", b"screen: list", b"brightness auto on", b"ota check skipped",
         b"touch ok"]
    B1 = b"radar ready"
    LPOS = b"map tiles: loaded ok"
    ARR = b"array text, 20 chars"
    DEG = "outside 21°C".encode("utf-8")
    DEAD = b"dead path, never linked"
    MIP = ("host " + _dotted(["192", "0", "2", "7"])).encode()
    MHEX = ("id " + "ab" * 9).encode()
    MMAC = ("mac " + ":".join(["02", "00", "00", "00", "00", "01"])).encode()
    MDEC = ("at " + _dotted(["0", "1234"])).encode()
    V1, V2 = b"fw v20", ("ver " + _dotted(["1", "2", "3"])).encode()

    def strsec(extra=(), drop=()):
        lits = [s for s in E + [B1] if s not in drop] + list(extra)
        return b"".join(s + b"\0" for s in lits)

    offs = []
    acc = 0
    for s in E:
        offs.append(acc)
        acc += len(s) + 1
    rows, bad = [], 0
    if offs != [0x00, 0x0C, 0x21, 0x2E, 0x41, 0x53]:
        raise Rig("selftest layout: E1..E6 offsets %s, not the registered 00 0c 21 2e 41 53" % [hex(o) for o in offs])

    def obj(name, data, size=None):
        return (name, data, len(data) if size is None else size)

    def base(extra_lits=(), drop=(), extra_secs=(), first=()):
        secs = list(first) + [(".text", "text", b"\x36\x41\x00\x1d\xf0\x00" * 4, [], [])]
        secs.append((".rodata.str1.1", "str", strsec(extra_lits, drop), [], []))
        secs.append((".rodata.kbase", "ro", b"base array\0\0", [("kbase", 0, 12)], []))
        secs.append((".rodata.nums", "ro", b"\x01\0\0\0\x02\0\0\0", [("nums", 0, 8)], []))
        return secs + list(extra_secs)

    def image(extra=(), drop=(), prefix=b""):
        linked = [s for s in E + [B1] if s not in drop] + [b"base array"] + list(extra)
        return prefix + b"\x00" * 16 + b"".join(s + b"\0" + b"\x90\x90\x90" for s in linked)

    def ptr(slots):
        return b"".join(struct.pack("<I", o) for o in slots)

    def ro(name, data):
        return (".rodata." + name, "ro", data, [(name, 0, len(data))], [])

    tbl = ptr([offs[2], offs[0], offs[1], offs[3], offs[4], offs[5]])
    cases = [
        # name, candidate sections, extra objects {file: sections}, bin, expected (added [(text, tier)], notlinked, removed)
        ("S-NULL", base(), {}, image(), ([], [], 0)),
        ("S-POS", base(extra_lits=[LPOS]), {}, image([LPOS]), ([(LPOS, "T1")], [], 0)),
        ("S-ARR", base(extra_secs=[ro("k", ARR + b"\0" * 4)]), {}, image([ARR]), ([(ARR, "T2")], [], 0)),
        ("S-ARR-DEG", base(extra_secs=[ro("k2", DEG + b"\0" * 3)]), {}, image([DEG]), ([(DEG, "T2")], [], 0)),
        ("S-PTR", base(extra_secs=[(".rodata.tbl", "ro", tbl, [("tbl", 0, 24)], [0, 4, 8, 12, 16, 20])]), {}, image(),
         ([], [], 0)),
        ("S-PTR1", base(extra_secs=[(".rodata.one", "ro", ptr([offs[2]]), [("one", 0, 4)], [0])]), {}, image(),
         ([], [], 0)),
        ("S-ADDR", base(first=[(".text.dummy", "text", b"\x36\x41\x00" * 400, [], [])]), {},
         image(prefix=b"\x36\x41\x00\x1d" * 1024), ([], [], 0)),
        ("S-DEAD", base(), {"dead.o": [(".text", "text", b"\x36\x41\x00", [], []),
                                       (".rodata.str1.1", "str", DEAD + b"\0", [], [])]}, image(),
         ([], [DEAD], 0)),
        ("S-REM", base(drop=[B1]), {}, image(drop=[B1]), ([], [], 1)),
        ("S-MASK", base(extra_lits=[MIP, MHEX, MMAC, MDEC, V1, V2]), {}, image([MIP, MHEX, MMAC, MDEC, V1, V2]),
         ([(MIP, "T1"), (MHEX, "T1"), (MMAC, "T1"), (MDEC, "T1"), (V1, "T1"), (V2, "T1")], [], 0)),
        ("R-BADUTF8", base(extra_secs=[ro("r1", b"\xff\xfeAB\0\0\0\0")]), {}, image(), ([], [], 0)),
        ("R-CTRL", base(extra_secs=[ro("r2", b"ab\x01cd\0\0\0")]), {}, image(), ([], [], 0)),
        ("R-C1", base(extra_secs=[ro("r3", "ab\u0085c".encode("utf-8") + b"\0\0\0")]), {}, image(), ([], [], 0)),
        ("R-AFTER", base(extra_secs=[ro("r4", b"abc\0x\0\0\0")]), {}, image(), ([], [], 0)),
        ("R-NONUL", base(extra_secs=[ro("r5", b"abcd")]), {}, image(), ([], [], 0)),
        ("R-EMPTY", base(extra_secs=[ro("r6", b"\0\0\0\0")]), {}, image(), ([], [], 0)),
        ("R-TABS", base(extra_secs=[ro("r7", b"a\tb\r\nc\0\0")]), {}, image([b"a\tb\r\nc"]),
         ([(b"a\tb\r\nc", "T2")], [], 0)),
    ]
    mask_lines = [
        'ADDED T1 main.o .rodata.str1.1 "host [MASKED-IPV4]"',
        'ADDED T1 main.o .rodata.str1.1 "id [MASKED-HEX]"',
        'ADDED T1 main.o .rodata.str1.1 "mac [MASKED-MAC]"',
        'ADDED T1 main.o .rodata.str1.1 "at [MASKED-DEC]"',
        'ADDED T1 main.o .rodata.str1.1 "fw v20"',
        'ADDED T1 main.o .rodata.str1.1 "%s"' % V2.decode(),
    ]
    work = tempfile.mkdtemp(prefix="strings_elf_selftest_")
    try:
        # S-RE: the anchor -- readelf reads back exactly what was written
        a = os.path.join(work, "anchor")
        os.makedirs(a)
        write_elf(os.path.join(a, "main.o"), base())
        sections, symbols, _ = read_object(tool, os.path.join(a, "main.o"))
        names = sorted(s["name"] for i, s in sections.items() if i)
        want = sorted([".text", ".rodata.str1.1", ".rodata.kbase", ".rodata.nums", ".symtab", ".strtab", ".shstrtab"])
        str1 = [s for s in sections.values() if s["name"] == ".rodata.str1.1"]
        ok = (names == want and sorted(s["name"] for s in symbols) == ["kbase", "nums"]
              and len(str1) == 1 and str1[0]["data"] == strsec() and is_t1(str1[0]))
        if not ok:
            raise Rig("S-RE: readelf did not read back the written object (sections %s)" % names)
        rows.append(("S-RE", True, "sections %d, OBJECT symbols %d, .rodata.str1.1 bytes equal" % (len(names), len(symbols))))
        for name, csecs, extra, img, (want_added, want_nl, want_rem) in cases:
            d = os.path.join(work, name)
            ref, cand = os.path.join(d, "ref"), os.path.join(d, "cand")
            os.makedirs(ref)
            os.makedirs(cand)
            write_elf(os.path.join(ref, "main.o"), base())
            write_elf(os.path.join(cand, "main.o"), csecs)
            for fname, fsecs in extra.items():
                write_elf(os.path.join(cand, fname), fsecs)
            binp = os.path.join(d, "firmware.bin")
            with open(binp, "wb") as f:
                f.write(img)
            r = compare(tool, ref, cand, binp)
            got_added = [(row[3], row[0]) for row in r["added"]]
            got_nl = [row[3] for row in r["notlinked"]]
            ok = sorted(got_added) == sorted(want_added) and sorted(got_nl) == sorted(want_nl) and r["removed"] == want_rem
            if name == "S-MASK":
                ok = ok and r["masked"] == 4 and sorted(r["lines"][1:]) == sorted(mask_lines)
            detail = "added %s, not-linked %d, removed %d" % ([show(s) for s, _ in got_added], len(got_nl), r["removed"])
            rows.append((name, ok, detail))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for name, ok, detail in rows:
        if not ok:
            bad += 1
        print("  SELFTEST %-10s %s: %s" % (name, "holds" if ok else "OFF", mask(detail)[0]))
    print("SELFTEST PASSED" if bad == 0 else "SELFTEST FAILED (%d)" % bad)
    return 0 if bad == 0 else 1


# ---------------------------------------------------------------------------------------------------- main

def main(argv):
    args = {}
    it = iter(argv)
    for a in it:
        if a == "--selftest":
            args["selftest"] = True
        elif a in ("--readelf", "--ref", "--cand", "--bin"):
            args[a[2:]] = next(it, None)
        else:
            print(__doc__)
            return 2
    tool = args.get("readelf") or "readelf"
    try:
        if args.get("selftest"):
            return selftest(tool)
        if not all(args.get(k) for k in ("ref", "cand", "bin")):
            print(__doc__)
            return 2
        r = compare(tool, args["ref"], args["cand"], args["bin"])
    except Rig as e:
        print("RIG: %s" % mask(str(e))[0])
        return 2
    for line in r["lines"]:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
