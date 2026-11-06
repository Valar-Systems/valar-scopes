"""A binary PPM (P6, maxval 255) to an 8-bit RGB PNG, with zlib and nothing else (K13, docs/wx-opuk.md).

Usage: python -I tools/wx/ppm_to_png.py <in.ppm> <out.png>
"""
import struct
import sys
import zlib

raw = open(sys.argv[1], "rb").read()
parts = raw.split(maxsplit=4)
if parts[0] != b"P6" or int(parts[3]) != 255:
    raise SystemExit("not a P6 PPM with maxval 255")
w, h = int(parts[1]), int(parts[2])
pix = parts[4]
if len(pix) != w * h * 3:
    raise SystemExit(f"pixel bytes {len(pix)} != {w * h * 3}")


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


rows = b"".join(b"\x00" + pix[y * w * 3:(y + 1) * w * 3] for y in range(h))
png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
open(sys.argv[2], "wb").write(png)
print(f"{w}x{h} png, {len(png)} B")
