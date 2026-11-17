#!/usr/bin/env python3
"""Two firmware .bin images, every differing byte accounted for (docs/speed-p5p6.md section 8, and CLAUDE.md's
"two builds of the same source are NOT byte-identical" accounting): the only bytes two builds of one source may differ
in are the five build-stamp regions.

  SHA-ELF   esp_app_desc_t.app_elf_sha256, 0xB0..0xCF (32 B)
  SHA-IMG   the trailing image SHA-256, the last 32 bytes
  CHECKSUM  the image checksum byte, just before it (len - 33)
  STAMP     a byte inside an HH:MM:SS time string (WiFiManager's __TIME__ stamps) at the SAME offset in both images

    bincmp.py <a.bin> <b.bin>    exit 0 every differing byte accounted; 1 some are not (printed); 2 the sizes differ
    bincmp.py --selftest         exit 0 every row as predicted
"""
import re
import sys

SHA_ELF = (0xB0, 0xD0)          # [start, end)
STAMP = re.compile(rb'\d\d:\d\d:\d\d')


def stamp_spans(data):
    return [(m.start(), m.end()) for m in STAMP.finditer(data)]


def compare(a, b):
    """(code, counts {region: n}, unaccounted offsets)."""
    if len(a) != len(b):
        return 2, {}, []
    n = len(a)
    regions = {'SHA-ELF': 0, 'SHA-IMG': 0, 'CHECKSUM': 0, 'STAMP': 0}
    sa, sb = set(stamp_spans(a)), set(stamp_spans(b))
    both = sa & sb   # a stamp at the same offset in both images
    in_stamp = set()
    for s, e in both:
        in_stamp.update(range(s, e))
    bad = []
    for i in range(n):
        if a[i] == b[i]:
            continue
        if SHA_ELF[0] <= i < SHA_ELF[1]:
            regions['SHA-ELF'] += 1
        elif i >= n - 32:
            regions['SHA-IMG'] += 1
        elif i == n - 33:
            regions['CHECKSUM'] += 1
        elif i in in_stamp:
            regions['STAMP'] += 1
        else:
            bad.append(i)
    return (0 if not bad else 1), regions, bad


def main(pa, pb):
    a, b = open(pa, 'rb').read(), open(pb, 'rb').read()
    code, regions, bad = compare(a, b)
    if code == 2:
        print(f'SIZES DIFFER: {len(a)} vs {len(b)} (delta {len(b) - len(a):+d})')
        return 2
    total = sum(regions.values()) + len(bad)
    print(f'size {len(a)} both; {total} bytes differ: ' + ', '.join(f'{k} {v}' for k, v in regions.items())
          + f', UNACCOUNTED {len(bad)}')
    if bad:
        print('  unaccounted offsets (first 20): ' + ', '.join(f'0x{o:X}' for o in bad[:20]))
    print('ACCOUNTED: every differing byte is a build stamp' if code == 0 else 'NOT ACCOUNTED')
    return code


def selftest():
    import random
    rnd = random.Random(8)
    base = bytearray(rnd.getrandbits(8) for _ in range(4096))
    base[1000:1008] = b'18:46:38'
    base[2000:2008] = b'18:47:17'
    bad = 0

    def row(cond, what):
        nonlocal bad
        bad += not cond
        print(f"  {'ok  ' if cond else 'FAIL'}  {what}")

    def mut(*edits):
        b = bytearray(base)
        for off, val in edits:
            b[off] = val
        return bytes(b)

    c, r, u = compare(bytes(base), bytes(base))
    row(c == 0 and sum(r.values()) == 0 and not u, 'B-SAME: identical images exit 0 with 0 bytes')
    flip = lambda o: (o, base[o] ^ 0xFF)
    shaed = mut(*[flip(o) for o in range(0xB0, 0xD0)], *[flip(o) for o in range(4096 - 32, 4096)], flip(4096 - 33))
    c, r, u = compare(bytes(base), shaed)
    row(c == 0 and r == {'SHA-ELF': 32, 'SHA-IMG': 32, 'CHECKSUM': 1, 'STAMP': 0},
        f'B-SHA: the three hash regions exit 0, counted 32/32/1 (got {r})')
    t = bytearray(base)
    t[1000:1008] = b'19:18:54'
    t[2000:2008] = b'19:19:35'
    c, r, u = compare(bytes(base), bytes(t))
    row(c == 0 and r['STAMP'] == 10 and not u, f'B-TIME: two re-stamped HH:MM:SS strings exit 0, 10 STAMP bytes (got {r})')
    c, r, u = compare(bytes(base), mut(flip(3000)))
    row(c == 1 and u == [3000], 'B-OUT: one byte outside every region exits 1, naming it')
    c, r, u = compare(bytes(base), mut(flip(0xAF)))
    c2, r2, u2 = compare(bytes(base), mut(flip(0xD0)))
    row(c == 1 and c2 == 1, 'B-EDGE: 0xAF and 0xD0 (one byte either side of SHA-ELF) each exit 1')
    c, r, u = compare(bytes(base), bytes(base) + b'\x00')
    row(c == 2, 'B-SIZE: one byte longer exits 2')
    # a time string in only ONE image is not a stamp: a code change that happens to produce digits is not excused
    one = bytearray(base)
    one[3500:3508] = b'12:34:56'
    c, r, u = compare(bytes(base), bytes(one))
    row(c == 1, 'B-STAMP-ONE: an HH:MM:SS in only one image is not accounted (exit 1)')
    print('SELFTEST PASSED' if not bad else f'SELFTEST FAILED ({bad})')
    return 0 if not bad else 1


if __name__ == '__main__':
    if sys.argv[1:] == ['--selftest']:
        sys.exit(selftest())
    if len(sys.argv) == 3:
        sys.exit(main(sys.argv[1], sys.argv[2]))
    print(__doc__)
    sys.exit(2)
