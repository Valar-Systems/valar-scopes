"""BMPK v1 encoder (docs/basemap-package-format.md). Pure Python, no dependencies.

A package is built from a list of sections, each (type, count, body bytes), in directory order. `encode()` lays them
out contiguously after the 32-byte header and the 16-byte directory entries, fills totalBytes, and computes the CRC
over the whole package with the CRC field zeroed. Nothing here knows what a section means beyond its encoding helpers.
"""
import struct
import zlib

WATER, RIVERS, ROADS, PEAKS, PLACES, TERRAIN, PROVENANCE = 1, 2, 3, 4, 5, 6, 7
HEADER = 32
ENTRY = 16


def varint(v):
    """Unsigned LEB128, shortest form (format §1)."""
    assert 0 <= v < 1 << 35, v
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def svarint(v):
    """Zigzag (32-bit two's complement), then varint."""
    assert -(1 << 31) <= v < 1 << 31, v
    return varint(((v << 1) ^ (v >> 31)) & 0xFFFFFFFF)


def text(s):
    b = s.encode("utf-8")
    assert len(b) <= 255, s
    return bytes([len(b)]) + b


def vertices(pts):
    """`n` vertices: the first absolute, the rest as deltas (format §4). The count is the caller's."""
    out = bytearray(svarint(pts[0][0]) + svarint(pts[0][1]))
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        out += svarint(x1 - x0) + svarint(y1 - y0)
    for x, y in pts:
        assert -32767 <= x <= 32767 and -32767 <= y <= 32767, (x, y)
    return bytes(out)


def water_body(rings):
    out = bytearray()
    for r in rings:
        assert len(r) >= 3
        out += varint(len(r)) + vertices(r)
    return bytes(out)


def rivers_body(names, lines):
    """`lines`: (name index, vertices)."""
    out = bytearray(varint(len(names)))
    for n in names:
        out += text(n)
    for idx, pts in lines:
        assert len(pts) >= 2
        out += varint(idx) + varint(len(pts)) + vertices(pts)
    return bytes(out)


def roads_body(lines):
    """`lines`: (class 1 or 2, vertices)."""
    out = bytearray()
    for cls, pts in lines:
        assert cls in (1, 2) and len(pts) >= 2
        out += bytes([cls]) + varint(len(pts)) + vertices(pts)
    return bytes(out)


def peaks_body(peaks):
    """`peaks`: (qx, qy, elevation m, name), in rank order."""
    out = bytearray()
    for qx, qy, elev, name in peaks:
        out += svarint(qx) + svarint(qy) + svarint(elev) + text(name)
    return bytes(out)


def places_body(places):
    """`places`: (qx, qy, population, name), in source order."""
    out = bytearray()
    for qx, qy, pop, name in places:
        out += svarint(qx) + svarint(qy) + varint(pop) + text(name)
    return bytes(out)


def terrain_body(level_radius_mm, n, values):
    assert len(values) == n * n
    return struct.pack("<IHH", level_radius_mm, n, 0) + bytes(values)


def provenance_body(lines):
    """Lines joined by \n with NO trailing \n (RD-P4, ruled 2026-10-28 for both builders)."""
    return "\n".join(lines).encode("utf-8")


def layout(sections, lat0_micro, lon0_micro, radius_mm, tolerance_mm, version=1, flags=0, nsections=None,
           entry_overrides=None, total_override=None, magic=b"BMPK"):
    """The package's bytes with CRC zeroed, plus the CRC field's offset. `sections`: list of (type, count, body).
    The keyword arguments exist for the negative fixtures; a real package never passes them."""
    n = len(sections) if nsections is None else nsections
    start = HEADER + ENTRY * len(sections)
    entries, bodies, off = [], bytearray(), start
    for i, (typ, count, body) in enumerate(sections):
        e = dict(type=typ, flags=0, reserved=0, count=count, offset=off, length=len(body))
        if entry_overrides and i in entry_overrides:
            e.update(entry_overrides[i])
        entries.append(e)
        bodies += body
        off += len(body)
    total = start + len(bodies)
    hdr = magic + struct.pack("<BBHiiIII", version, n, flags, lat0_micro, lon0_micro, radius_mm, tolerance_mm,
                              total if total_override is None else total_override) + b"\0\0\0\0"
    assert len(hdr) == HEADER
    d = b"".join(struct.pack("<BBHIII", e["type"], e["flags"], e["reserved"], e["count"], e["offset"], e["length"])
                 for e in entries)
    return bytearray(hdr + d + bodies)


def seal(buf):
    """Writes the CRC-32 (IEEE, zlib's) of the whole package with the CRC field zeroed."""
    buf[28:32] = b"\0\0\0\0"
    crc = zlib.crc32(bytes(buf)) & 0xFFFFFFFF
    buf[28:32] = struct.pack("<I", crc)
    return bytes(buf)


def encode(sections, lat0_micro, lon0_micro, radius_mm, tolerance_mm):
    return seal(layout(sections, lat0_micro, lon0_micro, radius_mm, tolerance_mm))


def crc_ok(pkg):
    b = bytearray(pkg)
    want = struct.unpack_from("<I", b, 28)[0]
    b[28:32] = b"\0\0\0\0"
    return (zlib.crc32(bytes(b)) & 0xFFFFFFFF) == want
