"""The synthetic BMPK fixtures (docs/weatherscope-stage3.md A10.11's PK set, and LN's `syn-order`).

    python -I tools/weatherscope/basemap/synth.py <out dir>            write them, and check each
    python -I tools/weatherscope/basemap/synth.py <out dir> --check    regenerate in memory; exit 1 on any byte that differs

Every fixture's contents are written out below as data, so another builder can reproduce `syn-min` byte for byte
(the map server's ME1). Each negative is `syn-min` with ONE mutation. The tool asserts that the mutation took (the bytes
differ from `syn-min`'s), that the CRC is right wherever it was recomputed, and that the reference validator
(validate.py) gives exactly the reason A10.11 freezes. Origin (0, 0): a test point, the location of nothing.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python -I leaves it out
import bmpk  # noqa: E402
from bmpk import WATER, RIVERS, ROADS, PEAKS, PLACES, TERRAIN, PROVENANCE
import validate

# ---- syn-min, as data -----------------------------------------------------------------------------------------------
LAT0, LON0 = 0, 0                 # micro-degrees
R_MM, TOL_MM = 97_000_000, 100_584
RING = [(-1000, -1000), (1000, -1000), (1000, 1000), (-1000, 1000)]
RIVER_NAMES = ["Test Riv"]
RIVER = (0, [(-2000, 0), (2000, 0)])
ROAD = (2, [(0, -2000), (0, 2000)])
PEAK = (500, 500, 1000, "Mount Test")
PLACE = (-500, 500, 1234, "Test city")
LEVEL = (1_000_000, 4, list(range(16)))
PROV = ["format: BMPK 1", "builder: synthetic"]


def sections(ring=RING, place=PLACE, water_body=None, roads_body=None, places_body=None, rivers_body=None,
             terrain_body=None, water_count=1):
    return [
        (WATER, water_count, water_body if water_body is not None else bmpk.water_body([ring])),
        (RIVERS, 1, rivers_body if rivers_body is not None else bmpk.rivers_body(RIVER_NAMES, [RIVER])),
        (ROADS, 1, roads_body if roads_body is not None else bmpk.roads_body([ROAD])),
        (PEAKS, 1, bmpk.peaks_body([PEAK])),
        (PLACES, 1, places_body if places_body is not None else bmpk.places_body([place])),
        (TERRAIN, 1, terrain_body if terrain_body is not None else bmpk.terrain_body(*LEVEL)),
        (PROVENANCE, len(PROV), bmpk.provenance_body(PROV)),
    ]


def pkg(secs, **kw):
    return bmpk.seal(bmpk.layout(secs, LAT0, LON0, R_MM, TOL_MM, **kw))


def gap_before_roads():
    """One zero byte between RIVERS and ROADS, outside every section; the later offsets and total moved by one."""
    secs = sections()
    start = bmpk.HEADER + bmpk.ENTRY * len(secs)
    entries, bodies, off = [], bytearray(), start
    for i, (typ, count, body) in enumerate(secs):
        if typ == ROADS:
            bodies += b"\0"
            off += 1
        entries.append((typ, count, off, len(body)))
        bodies += body
        off += len(body)
    total = start + len(bodies)
    hdr = b"BMPK" + struct.pack("<BBHiiIII", 1, len(secs), 0, LAT0, LON0, R_MM, TOL_MM, total) + b"\0\0\0\0"
    d = b"".join(struct.pack("<BBHIII", t, 0, 0, c, o, l) for t, c, o, l in entries)
    return bmpk.seal(bytearray(hdr + d + bodies))


def build():
    out = {}
    syn_min = pkg(sections())
    out["syn-min"] = (syn_min, "kOk")
    out["syn-unknown"] = (pkg(sections() + [(9, 1, b"\x01\x02\x03")]), "kOk")

    # syn-order: A10.11's LN fixture. Origin = home, R = the 60 mi view (96,560,640 mm), so q / 32767 * 120 is pixels.
    order = [
        (WATER, 1, bmpk.water_body([[(-5393, -5393), (5529, -5393), (5529, 5529), (-5393, 5529)]])),
        (RIVERS, 1, bmpk.rivers_body(["Test Riv"], [(0, [(-8137, -2676), (8246, -2676)])])),
        (ROADS, 1, bmpk.roads_body([(1, [(55, -8137), (55, 8246)])])),
        (TERRAIN, 1, bmpk.terrain_body(98_974_656, 246, [128] * (246 * 246))),
        (PROVENANCE, 2, bmpk.provenance_body(["format: BMPK 1", "builder: synthetic"])),
    ]
    out["syn-order"] = (bmpk.seal(bmpk.layout(order, 0, 0, 96_560_640, TOL_MM)), "kOk")

    neg = {}
    neg["short"] = (syn_min[:31], "kHeader")
    neg["magic"] = (pkg(sections(), magic=b"BMPX"), "kMagic")
    neg["version"] = (pkg(sections(), version=2), "kVersion")
    neg["flags"] = (pkg(sections(), flags=1), "kFlags")
    neg["total"] = (pkg(sections(), total_override=len(syn_min) + 1), "kTotal")
    # crc: the place's population 1,234 -> 1,235 (same varint length), the CRC left as syn-min's
    b = bytearray(bmpk.layout(sections(place=(-500, 500, 1235, "Test city")), LAT0, LON0, R_MM, TOL_MM))
    b[28:32] = syn_min[28:32]
    neg["crc"] = (bytes(b), "kCrc")
    neg["nsec0"] = (bmpk.seal(bmpk.layout([], LAT0, LON0, R_MM, TOL_MM)), "kSectionCount")
    neg["nsec33"] = (pkg(sections(), nsections=33), "kSectionCount")
    prov_len = len(bmpk.provenance_body(PROV))
    neg["bounds"] = (pkg(sections(), entry_overrides={6: {"length": prov_len + 1000}}), "kSectionBounds")
    secs = sections()
    roads_off = bmpk.HEADER + bmpk.ENTRY * 7 + len(secs[0][2]) + len(secs[1][2])
    neg["overlap"] = (pkg(sections(), entry_overrides={2: {"offset": roads_off - 1}}), "kOverlap")
    neg["gap"] = (gap_before_roads(), "kGap")
    neg["dup"] = (pkg(sections() + [(WATER, 1, bmpk.water_body([RING]))]), "kDuplicate")
    neg["noprov"] = (pkg(sections()[:6]), "kNoProvenance")
    neg["varint6"] = (pkg(sections(water_body=bytes([0x84, 0x80, 0x80, 0x80, 0x80, 0x00]) + bmpk.vertices(RING))),
                      "kVarint")
    neg["overlong"] = (pkg(sections(water_body=bytes([0x84, 0x00]) + bmpk.vertices(RING))), "kVarint")
    walk = bytes([2]) + bmpk.varint(2) + bmpk.svarint(0) + bmpk.svarint(-2000) + bmpk.svarint(32768) + bmpk.svarint(4000)
    neg["walk"] = (pkg(sections(roads_body=walk)), "kVertexRange")
    neg["ring2"] = (pkg(sections(water_body=bmpk.varint(2) + bmpk.vertices(RING[:2]))), "kVertexCount")
    neg["line1"] = (pkg(sections(roads_body=bytes([2]) + bmpk.varint(1) + bmpk.svarint(0) + bmpk.svarint(-2000))),
                    "kVertexCount")
    neg["count"] = (pkg(sections(water_count=2)), "kBody")
    bad_text = bmpk.svarint(-500) + bmpk.svarint(500) + bmpk.varint(1234) + bytes([200]) + b"Test city"
    neg["text"] = (pkg(sections(places_body=bad_text)), "kText")
    rn = bmpk.varint(1) + bmpk.text("Test Riv") + bmpk.varint(1) + bmpk.varint(2) + bmpk.vertices(RIVER[1])
    neg["rivername"] = (pkg(sections(rivers_body=rn)), "kRiverName")
    neg["roadclass"] = (pkg(sections(roads_body=bytes([3]) + bmpk.varint(2) + bmpk.vertices(ROAD[1]))), "kRoadClass")
    neg["terrainlen"] = (pkg(sections(terrain_body=bmpk.terrain_body(*LEVEL)[:-1])), "kTerrainLength")
    neg["terrainN0"] = (pkg(sections(terrain_body=struct.pack("<IHH", 1_000_000, 0, 0))), "kTerrainLength")
    neg["reserved"] = (pkg(sections(), entry_overrides={0: {"reserved": 1}}), "kReserved")
    assert len(neg) == 25, len(neg)
    for k, v in neg.items():
        out["neg-" + k] = v
    return out


def check_all(fx):
    syn_min = fx["syn-min"][0]
    bad = 0
    for name, (data, want) in fx.items():
        got, _ = validate.validate(data)
        took = name == "syn-min" or data != syn_min
        crc_note = ""
        if name not in ("neg-crc", "neg-short", "neg-total") and len(data) >= 32:
            crc_note = "" if bmpk.crc_ok(data) else " CRC-NOT-RESEALED"
        ok = got == want and took and not crc_note
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {name:18s} {len(data):6d} B  expect {want:15s} got {got}{crc_note}"
              f"{'' if took else ' MUTATION-DID-NOT-TAKE'}")
    return bad


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out, check = argv[1], "--check" in argv
    fx = build()
    bad = check_all(fx)
    if bad:
        print(f"{bad} fixture(s) are not what A10.11 says: nothing written")
        return 1
    if check:
        diff = [n for n, (d, _) in fx.items() if not os.path.exists(os.path.join(out, n + ".bmpk"))
                or open(os.path.join(out, n + ".bmpk"), "rb").read() != d]
        print("--check:", "every committed byte equal" if not diff else f"DIFFERS: {diff}")
        return 1 if diff else 0
    os.makedirs(out, exist_ok=True)
    for n, (d, _) in fx.items():
        open(os.path.join(out, n + ".bmpk"), "wb").write(d)
    print(f"wrote {len(fx)} fixtures to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
