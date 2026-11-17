#!/usr/bin/env python3
"""One function's code in two objects (docs/speed-p5p6.md section 10, the reviewer's option (a), ruled 2026-11-17):
every ALLOCATED section whose name carries <needle> (the function's .text and .literal, its lambdas' too), compared by
the section-name set, then each section's size and bytes, then its relocations as (offset, type, symbol NAME, addend).
It reuses objcmp.py's ELF reader, so "allocated" and "relocation" mean exactly what section 8's compare meant.

    secmatch.py <A.o> <B.o> <needle>        exit 0 identical; 1 differs; 2 not found (no allocated section carries
                                            the needle in A, in B, or both); 3 a parse error
    secmatch.py --selftest <A.o> <needle>   red first, on copies of <A.o>:
        S-FOUND   CONTROL: the needle names at least one allocated section of <A.o>
        S-IDENT   <A.o> against a byte copy of itself reads 0
        S-BYTE    one byte flipped inside the first matched section reads 1
        S-RELOC   one relocation's addend changed in a matched section reads 1
        S-RENAME  every matched section's name changed in the section-name table reads 2 (not found)
        S-DROP    every matched section's header zeroed (a NULL entry, as a dropped section reads) reads 2
"""
import os
import shutil
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from objcmp import Elf, SHT_REL, SHT_RELA  # noqa: E402


def matched(elf, needle):
    return sorted(n for n in elf.alloc() if needle in n)


def compare(pa, pb, needle):
    """(code, lines): 0 identical, 1 differs, 2 not found, 3 parse error."""
    try:
        a, b = Elf(open(pa, 'rb').read()), Elf(open(pb, 'rb').read())
    except (OSError, ValueError, struct.error, IndexError) as e:
        return 3, [f"parse error: {e}"]
    na, nb = matched(a, needle), matched(b, needle)
    lines = [f"A: {len(na)} section(s), B: {len(nb)} section(s) carry '{needle}'"]
    if not na or not nb:
        return 2, lines + ["NOT FOUND in " + ("both" if not na and not nb else "A" if not na else "B")]
    if na != nb:
        return 1, lines + [f"the section-name sets differ: A only {sorted(set(na) - set(nb))}, "
                           f"B only {sorted(set(nb) - set(na))}"]
    sa, sb, ra, rb = a.alloc(), b.alloc(), a.relocs(), b.relocs()
    code = 0
    for n in na:
        sz = [s for s, _ in sa[n]]
        what = []
        if sz != [s for s, _ in sb[n]]:
            what.append(f"size {sz} vs {[s for s, _ in sb[n]]}")
        elif sa[n] != sb[n]:
            x, y = sa[n][0][1] or b'', sb[n][0][1] or b''
            first = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), None)
            what.append("bytes" + (" from 0x%X" % first if first is not None else ""))
        if ra.get(n, []) != rb.get(n, []):
            what.append(f"relocations ({len(ra.get(n, []))} vs {len(rb.get(n, []))})")
        lines.append(f"  {'DIFFERS' if what else 'same   '}  {n[:100]}  size {sz}, relocs {len(ra.get(n, []))}"
                     + (": " + "; ".join(what) if what else ""))
        if what:
            code = 1
    return code, lines


# ---- the selftest's plants, each on a byte copy ----

def _headers(d):
    shoff, = struct.unpack_from('<I', d, 0x20)
    shentsize, shnum, _ = struct.unpack_from('<HHH', d, 0x2E)
    return shoff, shentsize, shnum


def plant_byte(d, needle):
    e = Elf(bytes(d))
    for s in e.sh:
        if needle in s['nm'] and s['flags'] & 0x2 and s['type'] != 8 and s['size'] > 0:
            d[s['off']] ^= 0x01
            return True
    return False


def plant_reloc(d, needle):
    e = Elf(bytes(d))
    for s in e.sh:
        if s['type'] not in (SHT_REL, SHT_RELA) or s['info'] >= len(e.sh) or s['size'] == 0:
            continue
        if needle in e.sh[s['info']]['nm']:
            if s['type'] == SHT_RELA:
                o = s['off'] + 8   # the first entry's addend
                v, = struct.unpack_from('<i', d, o)
                struct.pack_into('<i', d, o, v + 4)
            else:
                o = s['off']       # REL has no addend: move the first entry's offset instead
                v, = struct.unpack_from('<I', d, o)
                struct.pack_into('<I', d, o, v + 4)
            return True
    return False


def plant_rename(d, needle):
    """Every section-name string carrying the needle gets its first needle char changed, in place (same length)."""
    e = Elf(bytes(d))
    shoff, shentsize, shnum = _headers(d)
    shstrndx, = struct.unpack_from('<H', d, 0x32)
    stro = e.sh[shstrndx]['off']
    n = 0
    for s in e.sh:
        if needle in s['nm']:
            at = stro + s['name'] + s['nm'].index(needle)
            d[at] = ord('X') if d[at] != ord('X') else ord('Y')
            n += 1
    return n > 0 and not matched(Elf(bytes(d)), needle)


def plant_drop(d, needle):
    e = Elf(bytes(d))
    shoff, shentsize, shnum = _headers(d)
    n = 0
    for i, s in enumerate(e.sh):
        if needle in s['nm'] and s['flags'] & 0x2:
            d[shoff + i * shentsize: shoff + (i + 1) * shentsize] = bytes(shentsize)
            n += 1
    return n > 0 and not matched(Elf(bytes(d)), needle)


def selftest(pa, needle):
    tmp = tempfile.mkdtemp(prefix="secm-")
    fails = 0

    def row(ok, rid, what):
        nonlocal fails
        fails += not ok
        print(f"  {'ok  ' if ok else 'FAIL'}  {rid}  {what}")
    try:
        src = open(pa, 'rb').read()
        found = matched(Elf(src), needle)
        row(len(found) >= 1, "S-FOUND", f"CONTROL: {len(found)} allocated section(s) carry '{needle}' (want >= 1)")
        cases = (("S-IDENT", None, 0), ("S-BYTE", plant_byte, 1), ("S-RELOC", plant_reloc, 1),
                 ("S-RENAME", plant_rename, 2), ("S-DROP", plant_drop, 2))
        for rid, plant, want in cases:
            d = bytearray(src)
            applied = True if plant is None else plant(d, needle)
            if plant is not None and bytes(d) == src:
                applied = False
            pb = os.path.join(tmp, rid + ".o")
            with open(pb, 'wb') as f:
                f.write(bytes(d))
            code, lines = compare(pa, pb, needle)
            row(applied and code == want, rid, f"plant {'applied' if applied else 'DID NOT APPLY'}; exit {code} "
                f"(want {want}): {lines[-1].strip()[:110]}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"secmatch selftest: {fails} FAIL")
    return 1 if fails else 0


def main(argv):
    if len(argv) == 3 and argv[0] == "--selftest":
        return selftest(argv[1], argv[2])
    if len(argv) == 3:
        code, lines = compare(argv[0], argv[1], argv[2])
        for ln in lines:
            print(ln)
        print({0: "IDENTICAL", 1: "DIFFERS", 2: "NOT FOUND", 3: "PARSE ERROR"}[code])
        return code
    print(__doc__)
    return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
