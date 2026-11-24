"""W2 and W3 for the web build's photo (_handoffs/p4/web-photos/REG-web-photos.txt, as amended by AMENDMENT 2).

    python3 test/host/web_jpg_fidelity.py --libdeps <.pio/libdeps/blipscope-s3-128> [--keep DIR]

Builds test/host/web_jpg_cli.cpp over tools/web-renderer/web/web_jpg.cpp and the DEVICE'S OWN decoder
(<libdeps>/LovyanGFX/src/lgfx/utility/lgfx_tjpgd.c), makes its fixtures with Pillow, and grades:
  W2  each 4:4:4 baseline fixture (240, 480) decodes (true), and its RGB565 pixels match the REFERENCE within
      max |d| <= 2 LSB and mean |d| <= 0.5 LSB, per channel. The reference: Pillow decodes to YCbCr (libjpeg's own
      Huffman, dequantisation and IDCT -- the independent part); the decoder's documented Bayer offset is added to Y
      at (y & 3, x & 3); its documented integer conversion (FP_SHIFT 8) and 0..255 clip give RGB; color565 truncates.
      CONTROL: the same comparison against the reference shifted one pixel right must FAIL the band (on max).
      Progressive, truncated-at-half and an HTML body -> false.
      REPORTED, not graded: a 4:2:0 fixture's max / mean against the same reference.
  W3  clip and margin, as the device: a 2x fixture drawn into the panel-sized sprite equals EXACTLY the top-left of
      its own full decode; a half-size fixture lands top-left, and every other pixel keeps the seed (the fill).
Exit 0 every row as predicted, 1 a row off, 2 the rig (a build or a run that did not happen)."""
import io
import os
import random
import shutil
import subprocess
import sys
import tempfile

try:
    from PIL import Image
except ImportError:          # the rig, never a row: a missing Pillow is not a failed decode
    print("rig: Pillow is not installed for this Python")
    sys.exit(2)

CC = os.environ.get("CC") or "gcc"
CXX = os.environ.get("CXX") or "g++"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
WEB = os.path.join(ROOT, "tools", "web-renderer", "web")
BAYER = [0, 4, 1, 5, -2, 2, -1, 3, 1, 5, 0, 4, -1, 3, -2, 2]   # lgfx_tjpgd.c :133, JD_BAYER 1
MAX_LSB, MEAN_LSB = 2, 0.5


def clip8(v):
    return 0 if v < 0 else 255 if v > 255 else v


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def pattern(n, seed):
    """High-frequency content: 8x8 checker tiles of saturated colours, and noise tiles."""
    rnd = random.Random(seed)
    im = Image.new("RGB", (n, n))
    px = im.load()
    cols = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255), (255, 255, 255), (0, 0, 0)]
    for ty in range(0, n, 16):
        for tx in range(0, n, 16):
            noise = rnd.random() < 0.5
            a, b = rnd.choice(cols), rnd.choice(cols)
            for y in range(ty, min(ty + 16, n)):
                for x in range(tx, min(tx + 16, n)):
                    if noise:
                        px[x, y] = (rnd.randrange(256), rnd.randrange(256), rnd.randrange(256))
                    else:
                        px[x, y] = a if ((x // 8) + (y // 8)) % 2 == 0 else b
    return im


def jpeg(im, **kw):
    buf = io.BytesIO()
    im.save(buf, "JPEG", **kw)
    return buf.getvalue()


def reference(data):
    """The reference RGB565 for a baseline JPEG (AMENDMENT 2)."""
    im = Image.open(io.BytesIO(data))
    im.draft("YCbCr", im.size)
    im.load()
    if im.mode != "YCbCr":
        raise SystemExit(f"rig: Pillow did not decode to YCbCr (mode {im.mode})")
    w, h = im.size
    ys, cbs, crs = (list(c.getdata()) for c in im.split())
    out = []
    for y in range(h):
        for x in range(w):
            i = y * w + x
            cb, cr = cbs[i] - 128, crs[i] - 128
            yy = ys[i] + BAYER[((y & 3) << 2) + (x & 3)]
            rr = (358 * cr) >> 8
            gg = (88 * cb + 182 * cr) >> 8
            bb = (453 * cb) >> 8
            out.append(rgb565(clip8(yy + rr), clip8(yy - gg), clip8(yy + bb)))
    return w, h, out


def channels(v):
    return (v >> 11) & 31, (v >> 5) & 63, v & 31


def band(a, b):
    """Per-channel (max, mean) |d| in LSB between two equal-length RGB565 lists."""
    sums, mx = [0, 0, 0], [0, 0, 0]
    for p, q in zip(a, b):
        for k, (u, v) in enumerate(zip(channels(p), channels(q))):
            d = abs(u - v)
            sums[k] += d
            mx[k] = max(mx[k], d)
    n = len(a)
    return mx, [s / n for s in sums]


class Rig:
    def __init__(self, libdeps, tmp):
        self.tmp = tmp
        tj = os.path.join(libdeps, "LovyanGFX", "src", "lgfx", "utility")
        if not os.path.isfile(os.path.join(tj, "lgfx_tjpgd.c")):
            raise SystemExit(f"rig: no lgfx_tjpgd.c under {tj}")
        self.exe = os.path.join(tmp, "web_jpg_cli")
        o = os.path.join(tmp, "lgfx_tjpgd.o")
        for cmd in ([CC, "-O2", "-c", os.path.join(tj, "lgfx_tjpgd.c"), "-I", tj, "-o", o],
                    [CXX, "-std=gnu++14", "-O2", "-Wall", "-I", WEB, "-I", tj, os.path.join(HERE, "web_jpg_cli.cpp"),
                     os.path.join(WEB, "web_jpg.cpp"), o, "-o", self.exe]):
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                raise SystemExit(f"rig: build failed: {' '.join(cmd[:3])}...\n{r.stderr[-2000:]}")

    def draw(self, data, x, y, mw, mh, w, h, seed=0):
        src, out = os.path.join(self.tmp, "in.jpg"), os.path.join(self.tmp, "out.raw")
        open(src, "wb").write(data)
        r = subprocess.run([self.exe, src, str(x), str(y), str(mw), str(mh), str(w), str(h), str(seed), out],
                           capture_output=True, text=True)
        if r.returncode != 0 or not r.stdout.startswith("drawJpg "):
            raise SystemExit(f"rig: the CLI did not run (exit {r.returncode}): {r.stderr[-500:]}")
        raw = open(out, "rb").read()
        px = [raw[i] | (raw[i + 1] << 8) for i in range(0, len(raw), 2)]
        return r.stdout.strip() == "drawJpg 1", px


def emit_fixtures(rig, out):
    """AMENDMENT 4 (c): card_photo.mjs's fixtures -- baseline 4:2:0 at quality 82, the library's own encoding
    (proxy/scripts/photo-render.ts :126) -- and the HOST-compiled decoder's RGB565 sha256 for each, which the
    WebAssembly decode must equal byte for byte."""
    import hashlib
    import json
    os.makedirs(out, exist_ok=True)
    table = {}
    for n, seed in ((240, 11), (480, 12)):
        data = jpeg(pattern(n, seed), quality=82, subsampling=2, progressive=False)
        ok, px = rig.draw(data, 0, 0, n, n, n, n)
        if not ok:
            raise SystemExit(f"rig: the host decoder refused its own fixture photo-{n}.jpg")
        raw = b"".join(bytes((v & 0xFF, v >> 8)) for v in px)
        name = f"photo-{n}.jpg"
        open(os.path.join(out, name), "wb").write(data)
        table[name] = {"size": n, "jpeg_sha256": hashlib.sha256(data).hexdigest(),
                       "rgb565_sha256": hashlib.sha256(raw).hexdigest(), "subsampling": "4:2:0", "quality": 82}
        print(f"  {name}: {len(data)} B, jpeg {table[name]['jpeg_sha256'][:12]}, host RGB565 {table[name]['rgb565_sha256'][:12]}")
    with open(os.path.join(out, "photo-decode.json"), "w", newline="\n") as f:
        json.dump(table, f, indent=1, sort_keys=True)
        f.write("\n")
    return 0


def main(argv):
    if "--libdeps" not in argv:
        print(__doc__)
        return 2
    libdeps = argv[argv.index("--libdeps") + 1]
    keep = argv[argv.index("--keep") + 1] if "--keep" in argv else None
    tmp = keep or tempfile.mkdtemp(prefix="webjpg-")
    os.makedirs(tmp, exist_ok=True)
    if "--emit-fixtures" in argv:
        try:
            return emit_fixtures(Rig(libdeps, tmp), argv[argv.index("--emit-fixtures") + 1])
        finally:
            if not keep:
                shutil.rmtree(tmp, ignore_errors=True)
    rows = []

    def row(rid, ok, text):
        rows.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'}  {rid} {text}")

    try:
        rig = Rig(libdeps, tmp)
        print("== W2 decode fidelity (AMENDMENT 2's reference; band max <= 2 LSB, mean <= 0.5 LSB per channel)")
        base240 = jpeg(pattern(240, 1), quality=90, subsampling=0)
        for name, n, data in (("F240", 240, base240), ("F480", 480, jpeg(pattern(480, 2), quality=90, subsampling=0))):
            ok, got = rig.draw(data, 0, 0, n, n, n, n)
            w, h, ref = reference(data)
            mx, mean = band(got, ref)
            inband = ok and (w, h) == (n, n) and max(mx) <= MAX_LSB and max(mean) <= MEAN_LSB
            row(f"W2-{name}", inband, f"drawJpg {int(ok)}; max |d| R/G/B {mx}, mean {[round(m, 3) for m in mean]}")
            if name == "F240":
                shifted = [ref[y * n + max(x - 1, 0)] for y in range(n) for x in range(n)]
                smx, smean = band(got, shifted)
                red = max(smx) > MAX_LSB or max(smean) > MEAN_LSB
                row("W2-CONTROL", red, f"against the reference shifted 1 px right: max {smx}, mean "
                    f"{[round(m, 3) for m in smean]} -> {'OUT of the band, as it must be' if red else 'IN the band: the band cannot see a shift'}")
        for rid, name, data in (("PROGRESSIVE", "progressive", jpeg(pattern(240, 1), quality=90, subsampling=0, progressive=True)),
                                ("TRUNCATED", "truncated at half", base240[:len(base240) // 2]),
                                ("HTML", "an HTML body", b"<!doctype html><html><body>Not found</body></html>")):
            ok, _ = rig.draw(data, 0, 0, 240, 240, 240, 240)
            row(f"W2-{rid}", not ok, f"{name}: drawJpg {int(ok)} (want 0)")
        f420 = jpeg(pattern(240, 1), quality=90, subsampling=2)
        ok, got = rig.draw(f420, 0, 0, 240, 240, 240, 240)
        _, _, ref = reference(f420)
        mx, mean = band(got, ref)
        print(f"  REPORTED  W2-F420 (4:2:0, not graded): drawJpg {int(ok)}; max |d| R/G/B {mx}, mean "
              f"{[round(m, 3) for m in mean]}")

        print("== W3 clip and margin, as the device (no scale)")
        big = jpeg(pattern(480, 3), quality=90, subsampling=0)
        _, full = rig.draw(big, 0, 0, 480, 480, 480, 480)
        ok, clipped = rig.draw(big, 0, 0, 240, 240, 240, 240)
        corner = [full[y * 480 + x] for y in range(240) for x in range(240)]
        row("W3-CLIP", ok and clipped == corner, f"a 480 photo into the 240 sprite: drawJpg {int(ok)}; equals its own "
            f"decode's top-left 240 x 240 exactly: {clipped == corner}")
        half = jpeg(pattern(120, 4), quality=90, subsampling=0)
        _, alone = rig.draw(half, 0, 0, 120, 120, 120, 120)
        ok, placed = rig.draw(half, 0, 0, 240, 240, 240, 240, seed=0)
        inside = [placed[y * 240 + x] for y in range(120) for x in range(120)]
        outside = [placed[y * 240 + x] for y in range(240) for x in range(240) if x >= 120 or y >= 120]
        row("W3-MARGIN", ok and inside == alone and all(v == 0 for v in outside),
            f"a 120 photo into the 240 sprite: drawJpg {int(ok)}; top-left equals its own decode: {inside == alone}; "
            f"the other {len(outside)} pixels keep the fill (0): {all(v == 0 for v in outside)}")
    finally:
        if not keep:
            shutil.rmtree(tmp, ignore_errors=True)
    off = rows.count(False)
    print(f"[web_jpg] {'ALL AS PREDICTED' if off == 0 else 'OFF'}: {len(rows)} rows, {off} off")
    return 0 if off == 0 else 1


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except SystemExit:
        raise
    except Exception as e:   # a crash is the instrument, never a red (the 11-04 ruling)
        print(f"rig: the driver crashed: {type(e).__name__}: {e}")
        sys.exit(2)
