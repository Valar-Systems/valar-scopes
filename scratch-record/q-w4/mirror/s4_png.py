# SCRATCH (map-server): S4-2's pictures and the renderer control's pixel diff. Pure Python (zlib), no image library.
#   python -X utf8 -I s4_png.py png  <frame.rgb565> <out.png>     240 x 240 RGB565 (little-endian) -> PNG (RGB888)
#   python -X utf8 -I s4_png.py diff <a.rgb565> <b.rgb565>        prints "DIFF <n> of 57600 pixels"
# RGB565 -> RGB888 by the fixed expansion r8 = round(r5 * 255 / 31), g8 = round(g6 * 255 / 63), b8 = round(b5 * 255 / 31).
# The pictures go to the owner UNOPENED: this tool writes them and prints their sha256, and nothing here displays them.
import hashlib, struct, sys, zlib

S = 240


def frame(p):
    b = open(p, "rb").read()
    if len(b) != S * S * 2:
        print(f"INSTRUMENT: {p} is {len(b)} B, not {S * S * 2}"); raise SystemExit(3)
    return struct.unpack(f"<{S * S}H", b)


def png(src, dst):
    px = frame(src)
    rows = bytearray()
    for y in range(S):
        rows.append(0)
        for x in range(S):
            v = px[y * S + x]
            rows += bytes((round(((v >> 11) & 31) * 255 / 31), round(((v >> 5) & 63) * 255 / 63), round((v & 31) * 255 / 31)))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", S, S, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")
    open(dst, "wb").write(out)
    print(f"PNG {dst.replace(chr(92), '/').split('/')[-1]} {len(out)} B sha256 {hashlib.sha256(out).hexdigest()[:12]}")


def diff(a, b):
    pa, pb = frame(a), frame(b)
    print(f"DIFF {sum(1 for x, y in zip(pa, pb) if x != y)} of {S * S} pixels")


if __name__ == "__main__":
    {"png": png, "diff": diff}[sys.argv[1]](sys.argv[2], sys.argv[3])
