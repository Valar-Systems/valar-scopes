"""The shapefile reader's host rows (docs/weatherscope-stage3.md A12.2): 19 named cases, each built by this file's own
writer from the spec, with the byte totals hand-computed from ESRI's July 1998 description and asserted against the
writer BEFORE the reader sees anything -- so a writer bug fails here, not silently in the reader.

    python -I tools/weatherscope/basemap/test_shp.py [<shp.py>]     # exit 0 only when every row passes

The optional argument is the reader to test (sabotage_shp.py passes a planted copy).
"""
import importlib.util
import io
import os
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))


def load(path):
    spec = importlib.util.spec_from_file_location("shp_under_test", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ---------------------------------------------------------------------------------------------------- the writer

RING = [(0.0, 0.0), (0.0, 10.0), (10.0, 10.0), (10.0, 0.0), (0.0, 0.0)]   # closed, clockwise (an outer ring)
LINE = [[(0.0, 0.0), (1.0, 1.0)], [(2.0, 2.0), (3.0, 1.0), (4.0, 3.0)]]


def content(rtype, parts):
    if rtype == 0:
        return struct.pack("<i", 0)
    pts = [p for part in parts for p in part]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    idx, k = [], 0
    for part in parts:
        idx.append(k)
        k += len(part)
    return (struct.pack("<i4dii", rtype, min(xs), min(ys), max(xs), max(ys), len(parts), len(pts))
            + struct.pack(f"<{len(idx)}i", *idx) + b"".join(struct.pack("<2d", *p) for p in pts))


def shp(ftype, recs, *, file_code=9994, length_delta=0, numbers=None, content_delta=None, extra=b""):
    """recs: [(rtype, parts) or None]; content_delta {record index: words added to its content-length field}."""
    body = b""
    for i, r in enumerate(recs):
        c = content(0, None) if r is None else content(*r)
        words = len(c) // 2 + (content_delta or {}).get(i, 0)
        num = numbers[i] if numbers else i + 1
        body += struct.pack(">ii", num, words) + c
    body += extra
    total = 100 + len(body)
    head = (struct.pack(">i", file_code) + b"\0" * 20 + struct.pack(">i", total // 2 + length_delta)
            + struct.pack("<ii", 1000, ftype) + b"\0" * 64)
    assert len(head) == 100
    return head + body


def dbf(fields, rows, flags, *, terminator=0x0D, eof=True, trim=0):
    """fields [(name, type, length)]; rows [[raw bytes per field]]; flags [b' ' or b'*' ...]."""
    hlen = 32 + 32 * len(fields) + 1
    rlen = 1 + sum(f[2] for f in fields)
    head = bytes([0x03, 124, 10, 28]) + struct.pack("<IHH", len(rows), hlen, rlen) + b"\0" * 20
    for name, ftype, flen in fields:
        head += name.encode("ascii").ljust(11, b"\0") + ftype.encode("ascii") + b"\0" * 4 + bytes([flen, 0]) + b"\0" * 14
    head += bytes([terminator])
    body = b""
    for flag, row in zip(flags, rows):
        body += flag + b"".join(v.ljust(f[2], b" ") if f[1] == "C" else v.rjust(f[2], b" ") for v, f in zip(row, fields))
    out = head + body + (b"\x1a" if eof else b"")
    return out[:len(out) - trim] if trim else out


def zipped(shp_b, dbf_b, cpg=b"UTF-8"):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("t.shp", shp_b)
        z.writestr("t.dbf", dbf_b)
        z.writestr("t.cpg", cpg)
    return buf


DB_FIELDS = [("FULLNAME", "C", 20), ("AWATER", "N", 14)]
DB_ROWS = [[b"Wickiup Reservoir", b"1500000"], [b"Elk Lake", b"2000000"]]


def main(argv):
    reader = load(argv[1] if len(argv) > 1 else os.path.join(HERE, "shp.py"))
    results = []

    def row(name, ok, detail):
        results.append((name, ok))
        print(f"  {'ok  ' if ok else 'FAIL'}  {name} {detail}")

    def refused(name, fn, want):
        try:
            fn()
            row(name, False, f"accepted; want {want}")
        except reader.Bad as e:
            row(name, e.status == want, f"refused {e.status} (want {want})")

    # The hand-computed totals (A12.2), against the writer, before the reader runs.
    sh1 = shp(5, [(5, [RING])])
    sh2 = shp(3, [(3, LINE)])
    sh3 = shp(5, [(5, [RING]), None, (5, [RING])])
    db1 = dbf(DB_FIELDS, DB_ROWS, [b" ", b"*"])
    writer = {"SH1": (len(sh1), 236), "SH2": (len(sh2), 240), "SH3": (len(sh3), 384), "DB1": (len(db1), 168)}
    bad = {k: v for k, v in writer.items() if v[0] != v[1]}
    if bad:
        print(f"REFUSED: the test's writer disagrees with the hand-computed totals {bad} (the rig)")
        return 2
    print("writer: SH1 236 B (118 words), SH2 240 B (120), SH3 384 B (192), DB1 168 B -- the hand-computed totals")

    t, recs = reader.read_shp(sh1)
    row("SH1", t == 5 and len(recs) == 1 and recs[0].type == 5 and recs[0].parts == [RING]
        and tuple(recs[0].box) == (0.0, 0.0, 10.0, 10.0), f"type {t}, {len(recs)} record(s), parts {len(recs[0].parts) if recs and recs[0] else '-'}")
    t, recs = reader.read_shp(sh2)
    row("SH2", t == 3 and len(recs) == 1 and recs[0].parts == LINE, f"parts {[len(p) for p in recs[0].parts] if recs and recs[0] else '-'}")
    t, recs = reader.read_shp(sh3)
    row("SH3", len(recs) == 3 and recs[0].parts == [RING] and recs[1] is None and recs[2].parts == [RING],
        f"{['None' if r is None else 'ring' for r in recs]}")

    refused("SH4", lambda: reader.read_shp(shp(5, [(5, [RING])], file_code=9995)), "kFileCode")
    refused("SH5", lambda: reader.read_shp(shp(5, [(5, [RING])], length_delta=1)), "kLength")
    refused("SH6", lambda: reader.read_shp(shp(5, [(5, [RING]), (5, [RING])], numbers=[1, 3])), "kRecordNumber")
    refused("SH7", lambda: reader.read_shp(shp(5, [(3, LINE)])), "kRecordType")
    p8 = bytearray(sh1)
    struct.pack_into("<i", p8, 100 + 8 + 44, 1)   # Parts[0] of record 1
    refused("SH8", lambda: reader.read_shp(bytes(p8)), "kParts")
    refused("SH9", lambda: reader.read_shp(shp(5, [(5, [RING]), (5, [RING])], content_delta={1: 8})), "kTruncated")
    p10 = bytearray(sh1)
    struct.pack_into("<i", p10, 32, 15)
    refused("SH10", lambda: reader.read_shp(bytes(p10)), "kType")
    refused("SH11", lambda: reader.read_shp(shp(5, [(5, [RING])], content_delta={0: 2}, extra=b"\0" * 4)), "kContentLength")

    tab = reader.read_dbf(db1)
    row("DB1", [f[:2] for f in tab.fields] == [("FULLNAME", "C"), ("AWATER", "N")]
        and tab.rows[0] == {"FULLNAME": "Wickiup Reservoir", "AWATER": 1500000} and tab.deleted == [False, True],
        f"rows {tab.rows}, deleted {tab.deleted}")
    pen = "Peñasco".encode("utf-8")
    assert len(pen) == 8
    tab = reader.read_dbf(dbf(DB_FIELDS, [[pen, b"1"]], [b" "]))
    row("DB2", tab.rows[0]["FULLNAME"] == "Peñasco", f"{tab.rows[0]['FULLNAME']!a}")
    refused("DB3", lambda: reader.read_dbf(dbf(DB_FIELDS, DB_ROWS, [b" ", b"*"], terminator=0x0E)), "kHeader")
    refused("DB4", lambda: reader.read_dbf(dbf(DB_FIELDS, DB_ROWS, [b" ", b"*"], eof=False, trim=1)), "kSize")
    refused("DB5", lambda: reader.read_dbf(dbf(DB_FIELDS, DB_ROWS, [b" ", b"x"])), "kFlag")
    refused("DB6", lambda: reader.read_dbf(dbf(DB_FIELDS, [[b"\xff", b"1"]], [b" "])), "kText")

    refused("LY1", lambda: reader.read_layer(zipped(shp(5, [(5, [RING]), (5, [RING])]),
                                                    dbf(DB_FIELDS, DB_ROWS + [[b"x", b"1"]], [b" "] * 3)), "t"), "kCount")
    refused("LY2", lambda: reader.read_layer(zipped(sh1, dbf(DB_FIELDS, DB_ROWS[:1], [b" "]), b"ISO-8859-1"), "t"), "kCodepage")

    passed = sum(1 for _, ok in results if ok)
    print(f"{len(results)} rows, {passed} passed, {len(results) - passed} failed")
    return 0 if passed == len(results) == 19 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
