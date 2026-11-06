"""MRMS PrecipFlag_00.00 within 60 mi of a package's origin (docs/weatherscope-stage3.md A12.8, A12.9): the Bend radar
pick's criterion (d), "no snow-flagged cell within 60 mi in any frame of the window". PrecipFlag is 8-bit PNG (GRIB2
template 5.41), which main's decoder (proxy/src/wx/mrms.ts) refuses by scope, so this is a small independent reader:
GRIB2 sections 3, 5 and 7, the PNG through Pillow.

The anchor: the grid must be the 7000 x 3500 CONUS grid at 0.01 deg, scanning 0 (north->south, west->east), or it is
refused. The window: cells whose centre is within 60 mi of the origin by WxLogic's planar metric (111 km a degree,
cos of the origin's latitude), <=. The origin is the package header's, printed nowhere.

    python -I precipflag.py <bend.bmpk> <cache-dir> <YYYYMMDD-HHMMSS> [...]     # one histogram line per time
    python -I precipflag.py <bend.bmpk> <cache-dir> --snow <YYYYMMDD-HHMMSS>     # "snow N", or "missing"
"""
import gzip
import hashlib
import io
import json
import math
import os
import struct
import sys
import urllib.error
import urllib.request

from PIL import Image

BUCKET = "https://noaa-mrms-pds.s3.amazonaws.com"
SNOW = 3
NAMES = {-3: "no coverage", 0: "none", 1: "warm strat rain", 3: "SNOW", 6: "conv rain", 7: "rain+hail",
         10: "cold strat rain", 91: "trop/strat", 96: "trop/conv"}
RADIUS_KM = 60 * 1.609344


class Refused(Exception):
    pass


def key_of(stamp):
    day = stamp[:8]
    return f"CONUS/PrecipFlag_00.00/{day}/MRMS_PrecipFlag_00.00_{stamp}.grib2.gz"


def fetch(cache, stamp):
    """(bytes, sha256), or None when the object does not exist. Cached under <cache>, re-hashed on every read."""
    key = key_of(stamp)
    path = os.path.join(cache, key.replace("/", "__"))
    meta = path + ".sha256"
    if os.path.exists(path) and os.path.exists(meta):
        b = open(path, "rb").read()
        if hashlib.sha256(b).hexdigest() != open(meta).read().strip():
            raise Refused(f"{key}: the cached copy no longer matches its sha256")
        return b, hashlib.sha256(b).hexdigest()
    try:
        b = urllib.request.urlopen(f"{BUCKET}/{key}", timeout=120).read()
    except urllib.error.HTTPError as e:
        if e.code in (403, 404):
            return None
        raise
    if b[:2] != b"\x1f\x8b":
        raise Refused(f"{key}: not gzip")
    os.makedirs(cache, exist_ok=True)
    open(path, "wb").write(b)
    open(meta, "w").write(hashlib.sha256(b).hexdigest() + "\n")
    return b, hashlib.sha256(b).hexdigest()


def _sm32(b, o):
    v = struct.unpack_from(">I", b, o)[0]
    return -(v & 0x7FFFFFFF) if v & 0x80000000 else v


def _sm16(b, o):
    v = struct.unpack_from(">H", b, o)[0]
    return -(v & 0x7FFF) if v & 0x8000 else v


def decode(g):
    """(grid, representation, png bytes) from an inflated GRIB2 message; refuses anything but 3.0 / 5.41."""
    if g[:4] != b"GRIB" or g[7] != 2:
        raise Refused("not GRIB2")
    o, grid, rep, png = 16, None, None, None
    while o < len(g) - 4:
        ln, num = struct.unpack_from(">IB", g, o)
        s = g[o:o + ln]
        if num == 3:
            if struct.unpack_from(">H", s, 12)[0] != 0:
                raise Refused("grid template is not 3.0")
            ni, nj = struct.unpack_from(">II", s, 30)
            la1, lo1 = _sm32(s, 46) / 1e6, _sm32(s, 50) / 1e6
            di, dj = struct.unpack_from(">II", s, 63)
            grid = dict(ni=ni, nj=nj, la1=la1, lo1=lo1 - 360 if lo1 > 180 else lo1, di=di / 1e6, dj=dj / 1e6, scan=s[71])
        elif num == 5:
            if struct.unpack_from(">H", s, 9)[0] != 41:
                raise Refused("data template is not 5.41")
            rep = dict(R=struct.unpack_from(">f", s, 11)[0], E=_sm16(s, 15), D=_sm16(s, 17), bits=s[19])
        elif num == 7:
            png = bytes(s[5:])
        o += ln
    if grid is None or rep is None or png is None:
        raise Refused("missing section 3, 5 or 7")
    return grid, rep, png


def histogram(gz, lat0, lon0):
    """{flag value: cells} over the cells within 60 mi of (lat0, lon0)."""
    grid, rep, png = decode(gzip.decompress(gz))
    if (grid["ni"], grid["nj"], grid["di"], grid["dj"], grid["scan"]) != (7000, 3500, 0.01, 0.01, 0):
        raise Refused(f"ANCHOR: grid {grid['ni']} x {grid['nj']}, di {grid['di']}, scan {grid['scan']}")
    img = Image.open(io.BytesIO(png))
    if img.size != (7000, 3500) or rep["bits"] != 8:
        raise Refused(f"ANCHOR: PNG {img.size}, {rep['bits']} bits")
    data = img.tobytes()
    w, h = img.size
    kx, lim = 111 * math.cos(lat0 * math.pi / 180), RADIUS_KM
    hist = {}
    r0 = int((grid["la1"] - lat0 - lim / 111 - 0.02) / grid["dj"])
    r1 = int((grid["la1"] - lat0 + lim / 111 + 0.02) / grid["dj"]) + 1
    for row in range(max(0, r0), min(h, r1)):
        dy = (grid["la1"] - row * grid["dj"] - lat0) * 111
        base = row * w
        for c in range(w):
            dx = (grid["lo1"] + c * grid["di"] - lon0) * kx
            if dx * dx + dy * dy <= lim * lim:
                v = round((rep["R"] + data[base + c] * 2 ** rep["E"]) / 10 ** rep["D"])
                hist[v] = hist.get(v, 0) + 1
    return hist


def snow_cells(gz, lat0, lon0):
    """The cells flagged snow within 60 mi: criterion (d) passes a frame only when this is 0."""
    return histogram(gz, lat0, lon0).get(SNOW, 0)


def origin(pkg_path):
    lat, lon = struct.unpack_from("<ii", open(pkg_path, "rb").read(), 8)
    return lat / 1e6, lon / 1e6


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    lat0, lon0 = origin(argv[1])
    cache = argv[2]
    if argv[3] == "--snow":
        f = fetch(cache, argv[4])
        print("missing" if f is None else f"snow {snow_cells(f[0], lat0, lon0)}")
        return 0
    for stamp in argv[3:]:
        f = fetch(cache, stamp)
        if f is None:
            print(f"{stamp}: missing")
            continue
        hist = histogram(f[0], lat0, lon0)
        precip = {k: n for k, n in sorted(hist.items()) if k not in (0, -3)}
        print(f"{stamp}: " + (", ".join(f"{NAMES.get(k, k)} {n}" for k, n in precip.items()) or "none")
              + f" | none {hist.get(0, 0)}, no coverage {hist.get(-3, 0)} | sha256 {f[1][:12]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
