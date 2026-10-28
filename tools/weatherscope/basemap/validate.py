"""A reference BMPK v1 validator (docs/basemap-package-format.md §8), in the order frozen in
docs/weatherscope-stage3.md A10.11, so each negative fixture has exactly one reason. Independent of the encoder: it
parses bytes only. Returns (status, info); info holds the decoded sections when the package is valid.

    python -I tools/weatherscope/basemap/validate.py <package.bmpk>
"""
import struct
import sys
import zlib

KNOWN = {1, 2, 3, 4, 5, 6, 7}


class Bad(Exception):
    def __init__(self, status):
        self.status = status


class Reader:
    def __init__(self, b):
        self.b, self.i = b, 0

    def left(self):
        return len(self.b) - self.i

    def u8(self):
        if self.i >= len(self.b):
            raise Bad("kBody")
        v = self.b[self.i]
        self.i += 1
        return v

    def varint(self):
        v, shift, n = 0, 0, 0
        while True:
            if self.i >= len(self.b):
                raise Bad("kBody")   # ran out mid-item: the body does not parse to its length
            byte = self.b[self.i]
            self.i += 1
            n += 1
            if n > 5:
                raise Bad("kVarint")
            v |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                if n > 1 and byte == 0:
                    raise Bad("kVarint")   # overlong: a redundant trailing zero group
                return v

    def svarint(self):
        z = self.varint() & 0xFFFFFFFF
        v = (z >> 1) ^ -(z & 1)
        return v

    def text(self):
        n = self.u8()
        if n > self.left():
            raise Bad("kText")
        try:
            s = self.b[self.i:self.i + n].decode("utf-8", errors="strict")
        except UnicodeDecodeError:
            raise Bad("kText")
        self.i += n
        return s


def vertices(r, n):
    x, y = r.svarint(), r.svarint()
    pts = [(x, y)]
    for _ in range(n - 1):
        x += r.svarint()
        y += r.svarint()
        pts.append((x, y))
    for px, py in pts:
        if not (-32767 <= px <= 32767 and -32767 <= py <= 32767):
            raise Bad("kVertexRange")
    return pts


def section(typ, count, body):
    r = Reader(body)
    out = []
    if typ == 1:
        for _ in range(count):
            n = r.varint()
            if n < 3:
                raise Bad("kVertexCount")
            out.append(vertices(r, n))
    elif typ == 2:
        names = [r.text() for _ in range(r.varint())]
        lines = []
        for _ in range(count):
            idx = r.varint()
            if idx >= len(names):
                raise Bad("kRiverName")
            n = r.varint()
            if n < 2:
                raise Bad("kVertexCount")
            lines.append((idx, vertices(r, n)))
        out = (names, lines)
    elif typ == 3:
        for _ in range(count):
            cls = r.u8()
            if cls not in (1, 2):
                raise Bad("kRoadClass")
            n = r.varint()
            if n < 2:
                raise Bad("kVertexCount")
            out.append((cls, vertices(r, n)))
    elif typ == 4:
        for _ in range(count):
            out.append((r.svarint(), r.svarint(), r.svarint(), r.text()))
    elif typ == 5:
        for _ in range(count):
            out.append((r.svarint(), r.svarint(), r.varint(), r.text()))
    elif typ == 6:
        if len(body) < 8:
            raise Bad("kTerrainLength")
        rl, n, res = struct.unpack_from("<IHH", body, 0)
        if n == 0 or len(body) != 8 + n * n:
            raise Bad("kTerrainLength")
        if res != 0:
            raise Bad("kReserved")
        return (rl, n, body[8:])
    elif typ == 7:
        return bytes(body)   # clients never parse PROVENANCE (format section 7)
    if r.left() != 0:
        raise Bad("kBody")
    return out


def validate(b):
    try:
        if len(b) < 32:
            raise Bad("kHeader")
        if b[:4] != b"BMPK":
            raise Bad("kMagic")
        version, nsec, flags = struct.unpack_from("<BBH", b, 4)
        if version != 1:
            raise Bad("kVersion")
        if flags != 0:
            raise Bad("kFlags")
        lat0, lon0, radius_mm, tol_mm, total, crc = struct.unpack_from("<iiIIII", b, 8)
        if total != len(b):
            raise Bad("kTotal")
        z = bytearray(b)
        z[28:32] = b"\0\0\0\0"
        if zlib.crc32(bytes(z)) & 0xFFFFFFFF != crc:
            raise Bad("kCrc")
        dir_end = 32 + 16 * nsec
        if not 1 <= nsec <= 32 or dir_end > total:
            raise Bad("kSectionCount")
        entries = [struct.unpack_from("<BBHIII", b, 32 + 16 * i) for i in range(nsec)]
        for typ, eflags, res, count, off, length in entries:
            if eflags != 0 or res != 0:
                raise Bad("kReserved")
            if off < dir_end or off + length > total:
                raise Bad("kSectionBounds")
        expect = dir_end
        for typ, eflags, res, count, off, length in entries:
            if off < expect:
                raise Bad("kOverlap")
            if off > expect:
                raise Bad("kGap")
            expect = off + length
        if expect != total:
            raise Bad("kGap")
        seen = {}
        for typ, *_ in entries:
            if typ in (1, 2, 3, 4, 5, 7) and typ in seen:
                raise Bad("kDuplicate")
            seen[typ] = True
        if 7 not in seen:
            raise Bad("kNoProvenance")
        decoded = []
        for typ, eflags, res, count, off, length in entries:
            body = b[off:off + length]
            if typ not in KNOWN:
                decoded.append((typ, None))   # skipped by its length
                continue
            decoded.append((typ, section(typ, count, body)))
        return "kOk", dict(lat0=lat0, lon0=lon0, radius_mm=radius_mm, tol_mm=tol_mm, sections=decoded)
    except Bad as e:
        return e.status, None


if __name__ == "__main__":
    st, info = validate(open(sys.argv[1], "rb").read())
    print(st)
    sys.exit(0 if st == "kOk" else 1)
