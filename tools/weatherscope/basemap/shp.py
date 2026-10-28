"""A minimal ESRI shapefile reader for the BMPK host tool (docs/weatherscope-stage3.md A12.1): shape types 0 (null),
3 (PolyLine) and 5 (Polygon), dBase III .dbf, and the .cpg -- what TIGER's AREAWATER, LINEARWATER, PRISECROADS,
COUNTY and STATE files use. The spec read: ESRI's "Shapefile Technical Description" (July 1998). Anything else is
refused by name, never guessed at; the checks run in A12.1's pinned order, so the first that fails names the refusal.

    python -I tools/weatherscope/basemap/shp.py <tiger.zip>     # prints the layer's counts and RF1-RF3 (A12.3)
"""
import struct
import sys
import zipfile


class Bad(Exception):
    def __init__(self, status):
        super().__init__(status)
        self.status = status


class Record:
    """One non-null record: its shape type, its header box (xmin, ymin, xmax, ymax), and its parts, each a list of
    (x, y) points in file order."""

    __slots__ = ("type", "box", "parts")

    def __init__(self, type_, box, parts):
        self.type, self.box, self.parts = type_, box, parts


def read_shp(b):
    """(file shape type, records), a null record returned as None in its place."""
    if len(b) < 100:
        raise Bad("kTruncated")
    if struct.unpack_from(">i", b, 0)[0] != 9994:
        raise Bad("kFileCode")
    if struct.unpack_from("<i", b, 28)[0] != 1000:
        raise Bad("kVersion")
    if struct.unpack_from(">i", b, 24)[0] * 2 != len(b):
        raise Bad("kLength")
    ftype = struct.unpack_from("<i", b, 32)[0]
    if ftype not in (3, 5):
        raise Bad("kType")
    records, i, want = [], 100, 1
    while i < len(b):
        if i + 8 > len(b):
            raise Bad("kTruncated")
        number, words = struct.unpack_from(">ii", b, i)
        if number != want:
            raise Bad("kRecordNumber")
        start, end = i + 8, i + 8 + words * 2
        if words < 0 or end > len(b):
            raise Bad("kTruncated")
        if words * 2 < 4:
            raise Bad("kContentLength")
        rtype = struct.unpack_from("<i", b, start)[0]
        if rtype not in (0, ftype):
            raise Bad("kRecordType")
        if rtype == 0:
            if words * 2 != 4:
                raise Bad("kContentLength")
            records.append(None)
        else:
            if words * 2 < 44:
                raise Bad("kContentLength")   # too short for its own fixed fields
            box = struct.unpack_from("<4d", b, start + 4)
            nparts, npoints = struct.unpack_from("<ii", b, start + 36)
            if nparts < 1 or npoints < 0:
                raise Bad("kParts")
            if 44 + 4 * nparts > words * 2:
                raise Bad("kContentLength")   # cannot even hold its parts array
            idx = list(struct.unpack_from(f"<{nparts}i", b, start + 44))
            if idx[0] != 0 or any(idx[k] >= idx[k + 1] for k in range(nparts - 1)) or idx[-1] >= npoints:
                raise Bad("kParts")
            if words * 2 != 44 + 4 * nparts + 16 * npoints:
                raise Bad("kContentLength")
            pts = struct.unpack_from(f"<{2 * npoints}d", b, start + 44 + 4 * nparts)
            xy = [(pts[2 * k], pts[2 * k + 1]) for k in range(npoints)]
            bounds = idx[1:] + [npoints]
            records.append(Record(rtype, box, [xy[s:e] for s, e in zip(idx, bounds)]))
        i = end
        want += 1
    return ftype, records


class Table:
    """A .dbf: fields [(name, type, length)], rows [dict], and deleted[i] for row i."""

    __slots__ = ("fields", "rows", "deleted")

    def __init__(self, fields, rows, deleted):
        self.fields, self.rows, self.deleted = fields, rows, deleted


def read_dbf(b):
    if len(b) < 32:
        raise Bad("kTruncated")
    if b[0] != 0x03:
        raise Bad("kVersion")
    n, hlen, rlen = struct.unpack_from("<IHH", b, 4)
    fields, at = [], 32
    while True:
        if at >= len(b):
            raise Bad("kHeader")
        if b[at] == 0x0D:
            break
        if at + 32 > len(b):
            raise Bad("kHeader")
        name = bytes(b[at:at + 11]).split(b"\0", 1)[0].decode("ascii", "strict")
        fields.append((name, chr(b[at + 11]), b[at + 16]))
        at += 32
    if hlen != 32 + 32 * len(fields) + 1 or at != 32 + 32 * len(fields):
        raise Bad("kHeader")
    if rlen != 1 + sum(f[2] for f in fields):
        raise Bad("kHeader")
    body = hlen + n * rlen
    if len(b) not in (body, body + 1) or (len(b) == body + 1 and b[body] != 0x1A):
        raise Bad("kSize")
    rows, deleted = [], []
    for r in range(n):
        at = hlen + r * rlen
        flag = b[at]
        if flag not in (0x20, 0x2A):
            raise Bad("kFlag")
        deleted.append(flag == 0x2A)
        row, at = {}, at + 1
        for name, ftype, flen in fields:
            raw = bytes(b[at:at + flen])
            at += flen
            if ftype == "C":
                try:
                    row[name] = raw.decode("utf-8", "strict").rstrip(" ")
                except UnicodeDecodeError:
                    raise Bad("kText")
            elif ftype == "N":
                s = raw.decode("ascii", "replace").strip()
                if s == "":
                    row[name] = None
                else:
                    try:
                        row[name] = int(s) if s.lstrip("-").isdigit() else float(s)
                    except ValueError:
                        raise Bad("kNumber")
            else:
                row[name] = raw   # other types are carried as bytes; nothing the builder reads uses them
        rows.append(row)
    return Table(fields, rows, deleted)


def read_layer(zip_path, stem=None):
    """(file shape type, records, table) from one TIGER zip. The .cpg is checked first, then the .shp, then the .dbf,
    then the counts (A12.1)."""
    with zipfile.ZipFile(zip_path) as z:
        names = z.namelist()
        if stem is None:
            stems = sorted({n[:-4] for n in names if n.lower().endswith(".shp")})
            if len(stems) != 1:
                raise Bad("kLayer")
            stem = stems[0]
        try:
            cpg = z.read(stem + ".cpg")
        except KeyError:
            raise Bad("kCodepage")
        if cpg.decode("ascii", "replace").strip() != "UTF-8":
            raise Bad("kCodepage")
        ftype, records = read_shp(z.read(stem + ".shp"))
        table = read_dbf(z.read(stem + ".dbf"))
    if len(records) != len(table.rows):
        raise Bad("kCount")
    return ftype, records, table


def real_file_rows(ftype, records):
    """A12.3's RF1-RF3 over one file: (records, nulls, header boxes equal to their points', polygon parts closed of
    polygon parts)."""
    nulls = boxes_equal = parts = closed = 0
    for r in records:
        if r is None:
            nulls += 1
            continue
        xs = [p[0] for part in r.parts for p in part]
        ys = [p[1] for part in r.parts for p in part]
        if (min(xs), min(ys), max(xs), max(ys)) == tuple(r.box):
            boxes_equal += 1
        if ftype == 5:
            for part in r.parts:
                parts += 1
                if part[0] == part[-1]:
                    closed += 1
    return len(records), nulls, boxes_equal, parts, closed


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    try:
        t, recs, tab = read_layer(sys.argv[1])
    except Bad as e:
        print(f"REFUSED: {e.status}")
        sys.exit(1)
    n, nulls, eq, parts, closed = real_file_rows(t, recs)
    live = n - nulls
    print(f"type {t}; {n} records ({nulls} null); RF1 boxes equal {eq}/{live}; "
          f"RF2 parts closed {closed}/{parts}; {sum(tab.deleted)} deleted rows; fields {[f[0] for f in tab.fields]}")
    sys.exit(0 if eq == live and closed == parts else 1)
