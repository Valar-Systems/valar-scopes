"""C2's host render (docs/weatherscope-stage2.md, 2.1): the bytes the board's 240 x 240 sprite must hold for the
KDFW frame in the ruled palette, derived INDEPENDENTLY of src/weatherscope/WxPalette.h.

    python -I test/fixtures/wx/palette_ref.py [--out sprite.bin]

Rain comes from the pinned file (NWSReflectivity-metpy-v1.6.3.tbl): code 1 = its 15 dBZ entry dimmed to 70 %
(ruled, addendum 4) with (v * 70 + 50) // 100, codes 2-12 = its 20-70 dBZ entries, code 13 = white (ruled). Dry and off-disc are black,
as the harness draws them. Each pixel is LovyanGFX's color565 -- (r >> 3) << 11 | (g >> 2) << 5 | b >> 3, which
TRUNCATES -- stored big-endian, which is how a 16-bit LGFX_Sprite holds it (swap565_t). Prints the sha256 the
board's WX,sprite_sha256 line must equal.
"""
import ast
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PIN = "f73f0d6264d420cb55efe79d58415ca056391a0cc607fa0efa061f3331418e2f"   # scripts/device-id-pinned-files.txt
raw = open(os.path.join(HERE, "NWSReflectivity-metpy-v1.6.3.tbl"), "rb").read().replace(b"\r\n", b"\n")  # autocrlf
assert hashlib.sha256(raw).hexdigest() == PIN, "the table is not the pinned one -- refusing to render from it"
tbl = [tuple(round(c * 255) for c in ast.literal_eval(l)) for l in raw.decode().splitlines() if l.strip()]
assert len(tbl) == 15 and tbl[2] == (0, 0, 246), "not the pinned table"
DIM = 70   # addendum 4: Daniel's look ruled 70 % for rain code 1
rain = {1: tuple((v * DIM + 50) // 100 for v in tbl[2]), 13: (255, 255, 255)}
rain.update({c: tbl[c + 1] for c in range(2, 13)})

def rgb(code):
    if code in rain:
        return rain[code]
    if code in (0, 255):
        return (0, 0, 0)
    raise SystemExit(f"code {code} has no colour here: this render covers the KDFW frame's codes only")

screen = open(os.path.join(HERE, "kdfw-60mi.screen.bin"), "rb").read()
assert len(screen) == 57600
out = bytearray()
for code in screen:
    r, g, b = rgb(code)
    v = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3
    out += bytes((v >> 8, v & 0xFF))
sha = hashlib.sha256(out).hexdigest()
if "--out" in sys.argv:
    open(sys.argv[sys.argv.index("--out") + 1], "wb").write(out)
print(f"codes on the screen: {sorted(set(screen))}")
print(f"expected sprite: {len(out)} bytes, sha256 {sha}")
