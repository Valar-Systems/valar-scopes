#!/usr/bin/env python3
"""Two object trees compared at the code level (docs/speed-p5p6.md section 8b): objects paired by relative path; for
each pair, every ALLOCATED section (SHF_ALLOC, derived from the flags) by name, size and bytes (SHT_NOBITS: size), and
the relocations against those sections as (offset, type, symbol NAME, addend). Non-allocated sections (debug,
.comment, symbol/string tables, the .rela sections themselves) are excluded by construction.

    objcmp.py <treeA> <treeB> [--except <file>]   exit 0 identical (bar the frozen exceptions); 1 an unexplained
                                                  difference; 2 an object dropped or added; 3 a parse error
    objcmp.py --selftest <tree>                    red first on copies of <tree>: O-TEXT, O-DROP, O-NONALLOC, O-RELA

An exceptions file holds lines `<relative object path> <section>`: that object's differences are excused in that
section ONLY (its cause is written in the record, not here).
"""
import os
import shutil
import struct
import sys
import tempfile

SHF_ALLOC = 0x2
SHT_NOBITS, SHT_SYMTAB, SHT_REL, SHT_RELA = 8, 2, 9, 4


class Elf:
    def __init__(self, data):
        if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
            raise ValueError('not a little-endian ELF32')
        self.d = data
        shoff, = struct.unpack_from('<I', data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x2E)
        self.sh = []
        for i in range(shnum):
            name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from('<10I', data, shoff + i * shentsize)
            self.sh.append(dict(name=name, type=typ, flags=flags, off=off, size=size, link=link, info=info, entsize=entsize))
        stro = self.sh[shstrndx]['off']
        for s in self.sh:
            s['nm'] = self.cstr(stro + s['name'])

    def cstr(self, o):
        e = self.d.index(b'\0', o)
        return self.d[o:e].decode('latin-1')

    def alloc(self):
        """{name: (size, bytes or None for NOBITS)} for every SHF_ALLOC section."""
        out = {}
        for s in self.sh:
            if s['flags'] & SHF_ALLOC:
                body = None if s['type'] == SHT_NOBITS else self.d[s['off']:s['off'] + s['size']]
                out.setdefault(s['nm'], []).append((s['size'], body))
        return out

    def symname(self, symtab, idx):
        s = self.sh[symtab]
        o = s['off'] + idx * 16
        name, value, size, info, other, shndx = struct.unpack_from('<IIIBBH', self.d, o)
        if (info & 0xF) == 3 and shndx < len(self.sh):   # STT_SECTION: by its section's name
            return 'section:' + self.sh[shndx]['nm']
        return self.cstr(self.sh[s['link']]['off'] + name)

    def relocs(self):
        """{target section name: sorted [(offset, type, symbol name, addend)]} for allocated targets."""
        out = {}
        for s in self.sh:
            if s['type'] not in (SHT_REL, SHT_RELA):
                continue
            tgt = self.sh[s['info']] if s['info'] < len(self.sh) else None
            if tgt is None or not (tgt['flags'] & SHF_ALLOC):
                continue
            ent = 12 if s['type'] == SHT_RELA else 8
            rows = []
            for k in range(s['size'] // ent):
                o = s['off'] + k * ent
                if ent == 12:
                    off, info, add = struct.unpack_from('<IIi', self.d, o)
                else:
                    (off, info), add = struct.unpack_from('<II', self.d, o), 0
                rows.append((off, info & 0xFF, self.symname(s['link'], info >> 8), add))
            out.setdefault(tgt['nm'], []).extend(rows)
        return {k: sorted(v) for k, v in out.items()}


def objects(tree):
    out = set()
    for root, ds, fs in os.walk(tree):
        for f in fs:
            if f.endswith('.o'):
                out.add(os.path.relpath(os.path.join(root, f), tree).replace('\\', '/'))
    return out


def compare_pair(pa, pb):
    """[] if identical, else a list of 'section: what' strings."""
    a, b = Elf(open(pa, 'rb').read()), Elf(open(pb, 'rb').read())
    diffs = []
    sa, sb = a.alloc(), b.alloc()
    for n in sorted(set(sa) | set(sb)):
        if n not in sa or n not in sb:
            diffs.append((n, 'present in one object only'))
        elif sa[n] != sb[n]:
            what = 'size' if [x[0] for x in sa[n]] != [x[0] for x in sb[n]] else 'bytes'
            if what == 'bytes':
                x, y = sa[n][0][1] or b'', sb[n][0][1] or b''
                first = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), None)
                what = 'bytes from 0x%X' % first if first is not None else 'bytes'
            diffs.append((n, what))
    ra, rb = a.relocs(), b.relocs()
    for n in sorted(set(ra) | set(rb)):
        if ra.get(n) != rb.get(n):
            diffs.append((n, 'relocations'))
    return diffs


def run(ta, tb, exc):
    oa, ob = objects(ta), objects(tb)
    dropped, added = sorted(oa - ob), sorted(ob - oa)
    unexplained, excused, same = [], [], 0
    for rel in sorted(oa & ob):
        try:
            d = compare_pair(os.path.join(ta, rel), os.path.join(tb, rel))
        except Exception as e:  # noqa: BLE001
            print(f'PARSE ERROR {rel}: {e}')
            return 3
        if not d:
            same += 1
            continue
        for sec, what in d:
            (excused if (rel, sec) in exc else unexplained).append((rel, sec, what))
    print(f'objects: {len(oa & ob)} paired, {same} identical; dropped {len(dropped)}, added {len(added)}; '
          f'excused {len(excused)} section(s), unexplained {len(unexplained)}')
    for rel in dropped:
        print(f'  DROPPED {rel}')
    for rel in added:
        print(f'  ADDED {rel}')
    for rel, sec, what in excused:
        print(f'  excused   {rel} [{sec}] {what}')
    for rel, sec, what in unexplained:
        print(f'  UNEXPLAINED {rel} [{sec}] {what}')
    if dropped or added:
        return 2
    return 1 if unexplained else 0


def load_exc(path):
    exc = set()
    for ln in open(path, encoding='utf-8'):
        ln = ln.split('#')[0].strip()
        if ln:
            rel, sec = ln.split()
            exc.add((rel, sec))
    return exc


# ---- the self-test: planted changes in COPIES of a real object tree (red first, docs/speed-p5p6.md 8b) ----

def _pick(tree, want):
    """The first object (sorted) with a section `want(sec)` accepts: (rel path, Elf, section dict)."""
    for rel in sorted(objects(tree)):
        e = Elf(open(os.path.join(tree, rel), 'rb').read())
        for s in e.sh:
            if want(e, s):
                return rel, e, s
    return None


def _flip(path, off):
    b = bytearray(open(path, 'rb').read())
    b[off] ^= 0x01
    open(path, 'wb').write(bytes(b))


def selftest(tree):
    import io
    from contextlib import redirect_stdout
    bad = 0

    def row(cond, what):
        nonlocal bad
        bad += not cond
        print(f"  {'ok  ' if cond else 'FAIL'}  {what}")

    def quiet(ta, tb):
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = run(ta, tb, set())
        return code, buf.getvalue()

    tmp = tempfile.mkdtemp(prefix='objcmp-')
    try:
        def copy(name):
            d = os.path.join(tmp, name)
            shutil.copytree(tree, d)
            return d
        # CONTROL: the tree against itself
        code, _ = quiet(tree, tree)
        row(code == 0, 'O-SELF: the tree against itself exits 0')
        # O-TEXT: one byte inside an allocated .text* section
        d = copy('text')
        rel, e, s = _pick(d, lambda e, s: s['nm'].startswith('.text') and s['flags'] & SHF_ALLOC and s['type'] != SHT_NOBITS and s['size'] > 0)
        _flip(os.path.join(d, rel), s['off'])
        code, out = quiet(tree, d)
        row(code == 1 and f'UNEXPLAINED {rel} [{s["nm"]}]' in out, f'O-TEXT: one byte in {s["nm"]} of one object exits 1, naming it (got {code})')
        # O-DROP: one object deleted
        d = copy('drop')
        victim = sorted(objects(d))[0]
        os.remove(os.path.join(d, victim))
        code, out = quiet(tree, d)
        row(code == 2 and f'DROPPED {victim}' in out, f'O-DROP: one object deleted exits 2, naming it (got {code})')
        # O-NONALLOC: one byte in a non-allocated, non-relocation section (.comment preferred, else debug)
        d = copy('nonalloc')
        pick = _pick(d, lambda e, s: s['nm'] == '.comment' and s['size'] > 0) or \
            _pick(d, lambda e, s: s['nm'].startswith('.debug') and s['size'] > 0 and s['type'] != SHT_NOBITS)
        rel, e, s = pick
        _flip(os.path.join(d, rel), s['off'])
        code, _ = quiet(tree, d)
        row(code == 0, f'O-NONALLOC: one byte in {s["nm"]} (not allocated) exits 0 (got {code})')
        # O-RELA: one relocation against an allocated section re-pointed at another symbol with a different name
        d = copy('rela')
        done = None
        for rel in sorted(objects(d)):
            p = os.path.join(d, rel)
            e = Elf(open(p, 'rb').read())
            for s in e.sh:
                if s['type'] != SHT_RELA or s['size'] < 12 or not (e.sh[s['info']]['flags'] & SHF_ALLOC):
                    continue
                off, info, add = struct.unpack_from('<IIi', e.d, s['off'])
                symtab = e.sh[s['link']]
                cur = e.symname(s['link'], info >> 8)
                for k in range(1, symtab['size'] // 16):
                    if e.symname(s['link'], k) not in (cur, ''):
                        b = bytearray(e.d)
                        struct.pack_into('<I', b, s['off'] + 4, (k << 8) | (info & 0xFF))
                        open(p, 'wb').write(bytes(b))
                        done = (rel, e.sh[s['info']]['nm'])
                        break
                if done:
                    break
            if done:
                break
        code, out = quiet(tree, d)
        row(done is not None and code == 1 and f'UNEXPLAINED {done[0]} [{done[1]}] relocations' in out,
            f'O-RELA: one relocation re-pointed exits 1, naming its section\'s relocations (got {code})')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print('SELFTEST PASSED' if not bad else f'SELFTEST FAILED ({bad})')
    return 0 if not bad else 1


if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == '--selftest':
        sys.exit(selftest(a[1]))
    if len(a) >= 2 and a[0] != '--selftest':
        exc = load_exc(a[3]) if len(a) == 4 and a[2] == '--except' else set()
        sys.exit(run(a[0], a[1], exc))
    print(__doc__)
    sys.exit(2)
