"""Host reference for Weatherscope Stage 1: what the 240 x 240 screen must show for one WXC1 crop.

    python -I test/fixtures/wx/render_ref.py <spike-checkout> <crop.wxc> [--png out.png] [--flip-ns]

Decodes with the spike's independent Python decoder (tools/wx/a3_check.py decode(), eccodes stubbed), then
maps every screen pixel back to a crop cell with the radar's own projection (AircraftManager::ProjectAt):
  - home = the crop's centre, from its header (NW cell centre + (n-1)/2 cells);
  - rLat = R / 111.0 and rLon = R / (111.0 * max(cos(home lat), 0.01)), R = 60 mi; no 2.0-degree clamp
    (60 mi is far below it, and Weatherscope has no OpenSky box to protect);
  - x = 120 + dLon / rLon * 120 and y = 120 - dLat / rLat * 120, so pixel (x, y) is sampled at its centre
    (x + 0.5, y + 0.5); the cell index is floor(v + 0.5), worked out per row and per column in double
    precision, which is separable because Stage 1 is north-up;
  - a pixel outside the disc (centre distance > 120) is 255; a pixel inside whose cell falls outside the
    crop is 14 (no coverage: there is no data there, and it is never drawn as dry).
Prints the buffer's sha256 and its statistics -- never a coordinate. --flip-ns renders the Stage 1
sabotage (latitude sign flipped) so its predicted failure is computed, not guessed.
"""
import hashlib
import importlib.util
import math
import os
import struct
import sys
import types

SIZE, C, R_KM = 240, 120.0, 60 * 1.609344
OFF = 255
# Provisional Stage 1 palette (NOT ruled): NWS reflectivity steps for codes 1-13, black for dry,
# grey for no coverage, dark violet for missing. The device check compares CODES, not colours.
PALETTE = {0: (0, 0, 0), 1: (3, 0, 244), 2: (2, 253, 2), 3: (1, 197, 1), 4: (0, 142, 0), 5: (253, 248, 2),
           6: (229, 188, 0), 7: (253, 149, 0), 8: (253, 0, 0), 9: (212, 0, 0), 10: (188, 0, 0),
           11: (248, 0, 253), 12: (152, 84, 198), 13: (253, 253, 253), 14: (60, 60, 68), 15: (110, 0, 110),
           OFF: (0, 0, 0)}


def load_decoder(spike):
    sys.modules.setdefault("eccodes", types.ModuleType("eccodes"))
    spec = importlib.util.spec_from_file_location("a3_check", os.path.join(spike, "tools", "wx", "a3_check.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.decode


def render(path, decode, flip_ns=False):
    _, lat_nw, lon_nw, cell_lat, cell_lon, codes = decode(path)
    raw = open(path, "rb").read()
    lat_nw_u, lon_nw_u = struct.unpack_from("<ii", raw, 12)          # exact header integers
    cl_u, cw_u = struct.unpack_from("<II", raw, 20)
    rows, cols = codes.shape
    lat_nw, lon_nw, cell_lat, cell_lon = lat_nw_u / 1e6, lon_nw_u / 1e6, cl_u / 1e6, cw_u / 1e6
    home_lat = lat_nw - (rows - 1) / 2 * cell_lat
    home_lon = lon_nw + (cols - 1) / 2 * cell_lon
    r_lat = R_KM / 111.0
    r_lon = R_KM / (111.0 * max(math.cos(math.radians(home_lat)), 0.01))
    sign = -1.0 if flip_ns else 1.0
    row_of = []
    for y in range(SIZE):
        lat = home_lat + sign * (C - (y + 0.5)) / C * r_lat
        row_of.append(math.floor((lat_nw - lat) / cell_lat + 0.5))
    col_of = []
    for x in range(SIZE):
        lon = home_lon + ((x + 0.5) - C) / C * r_lon
        col_of.append(math.floor((lon - lon_nw) / cell_lon + 0.5))
    buf = bytearray(SIZE * SIZE)
    outside_crop = 0
    for y in range(SIZE):
        for x in range(SIZE):
            if (x + 0.5 - C) ** 2 + (y + 0.5 - C) ** 2 > C * C:
                buf[y * SIZE + x] = OFF
                continue
            r, c = row_of[y], col_of[x]
            if 0 <= r < rows and 0 <= c < cols:
                buf[y * SIZE + x] = int(codes[r, c])
            else:
                buf[y * SIZE + x] = 14
                outside_crop += 1
    return bytes(buf), outside_crop


def stats(buf):
    disc = [v for v in buf if v != OFF]
    echo = sum(1 for v in disc if 1 <= v <= 13)
    q = {}
    for y in range(SIZE):
        for x in range(SIZE):
            v = buf[y * SIZE + x]
            if v == OFF:
                continue
            k = ("N" if y < C else "S") + ("W" if x < C else "E")
            e, n = q.get(k, (0, 0))
            q[k] = (e + (1 <= v <= 13), n + 1)
    hist = {}
    for v in disc:
        hist[v] = hist.get(v, 0) + 1
    return len(disc), echo, q, hist


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    spike, path = argv[1], argv[2]
    decode = load_decoder(spike)
    buf, oob = render(path, decode, flip_ns="--flip-ns" in argv)
    n, echo, q, hist = stats(buf)
    print(f"buffer sha256 {hashlib.sha256(buf).hexdigest()}")
    print(f"disc pixels {n}; echo {echo} ({100 * echo / n:.1f}%); pixels whose cell is outside the crop: {oob}")
    print("quadrants: " + "  ".join(f"{k} {100 * e / t:.0f}%" for k, (e, t) in sorted(q.items())))
    print("codes on the disc: " + " ".join(f"{k}:{v}" for k, v in sorted(hist.items())))
    if "--png" in argv:
        from PIL import Image
        out = argv[argv.index("--png") + 1]
        img = Image.new("RGB", (SIZE, SIZE))
        img.putdata([PALETTE[v] for v in buf])
        img.resize((SIZE * 2, SIZE * 2), Image.NEAREST).save(out)
        print(f"wrote {os.path.basename(out)} (2x, provisional palette)")
    if "--compare-flip" in argv:
        flip, _ = render(path, decode, flip_ns=True)
        disc = [i for i in range(SIZE * SIZE) if buf[i] != OFF]
        same = sum(1 for i in disc if buf[i] == flip[i])
        e_idx = [i for i in disc if 1 <= buf[i] <= 13]
        e_same = sum(1 for i in e_idx if buf[i] == flip[i])
        print(f"SABOTAGE (N/S flipped) vs reference: {100 * same / len(disc):.1f}% of disc pixels equal; "
              f"{100 * e_same / max(1, len(e_idx)):.1f}% of echo pixels equal; sha {hashlib.sha256(flip).hexdigest()[:16]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
