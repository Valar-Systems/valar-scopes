"""RK's independent reference (docs/weatherscope-stage3.md A8.2): the distance, in miles, from the KDFW crop's centre to
its nearest echo cell (code 1-13), in the map's metric -- planar, 111 km per degree, longitude scaled by the cosine
of the centre's latitude.

    python -I test/fixtures/wx/ring_ref.py           writes test/fixtures/wx/kdfw-60mi.ring.txt
    python -I test/fixtures/wx/ring_ref.py --check   exit 0 only if the committed file equals a fresh computation

Its own WXC1 decoder, written in Python from the wire format, not from WxCrop.h's code. THE CONTROL: the decoded codes
must hash to MANIFEST.json's codesSha256 for kdfw-60mi.wxc before any distance is believed. Prints no coordinate.
"""
import hashlib
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CROP = os.path.join(HERE, "kdfw-60mi.wxc")
OUT = os.path.join(HERE, "kdfw-60mi.ring.txt")
KM_PER_DEG, KM_PER_MI = 111.0, 1.609344


def decode(raw):
    magic, ver, coarsen, flags, reserved, epoch, lat_nw, lon_nw, cell_lat, cell_lon, rows, cols = \
        struct.unpack_from("<4sBBBBIiiIIHH", raw, 0)
    if magic != b"WXC1" or ver != 1:
        raise SystemExit("not a WXC1 v1 crop")
    codes = bytearray()
    i = 32
    while i < len(raw):
        b = raw[i]
        i += 1
        code, run = b >> 4, b & 15
        if run == 0:                       # a long run: its length follows as LEB128
            run, shift = 0, 0
            while True:
                x = raw[i]
                i += 1
                run |= (x & 0x7F) << shift
                shift += 7
                if not x & 0x80:
                    break
        codes += bytes([code]) * run
    if len(codes) != rows * cols:
        raise SystemExit(f"decoded {len(codes)} cells, header says {rows * cols}")
    return lat_nw / 1e6, lon_nw / 1e6, cell_lat / 1e6, cell_lon / 1e6, rows, cols, bytes(codes)


def nearest_echo_mi():
    raw = open(CROP, "rb").read()
    lat_nw, lon_nw, cell_lat, cell_lon, rows, cols, codes = decode(raw)
    want = next(f["expect"]["codesSha256"] for f in json.load(open(os.path.join(HERE, "MANIFEST.json")))["fixtures"]
                if f["name"] == "kdfw-60mi.wxc")
    got = hashlib.sha256(codes).hexdigest()
    if got != want:
        raise SystemExit(f"CONTROL FAILED: decoded codes {got[:12]} != MANIFEST {want[:12]}; no distance is believed")
    home_lat = lat_nw - (rows - 1) / 2 * cell_lat
    home_lon = lon_nw + (cols - 1) / 2 * cell_lon
    kx, ky = KM_PER_DEG * math.cos(math.radians(home_lat)), KM_PER_DEG
    best = None
    for r in range(rows):
        dy = (lat_nw - r * cell_lat - home_lat) * ky
        for c in range(cols):
            if 1 <= codes[r * cols + c] <= 13:
                dx = (lon_nw + c * cell_lon - home_lon) * kx
                d2 = dx * dx + dy * dy
                if best is None or d2 < best:
                    best = d2
    return got, (-1.0 if best is None else math.sqrt(best) / KM_PER_MI)


def main(argv):
    sha, mi = nearest_echo_mi()
    text = f"{mi:.6f}\n"
    print(f"decode control: codes sha256 {sha[:12]}... matches MANIFEST.json")
    print(f"nearest echo from the crop's centre: {mi:.6f} mi")
    if argv[1:] == ["--check"]:
        committed = open(OUT).read() if os.path.exists(OUT) else ""
        ok = committed == text
        print("committed file " + ("matches" if ok else f"DIFFERS: {committed.strip()!r}"))
        return 0 if ok else 1
    open(OUT, "w", newline="\n").write(text)
    print(f"wrote {os.path.relpath(OUT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
