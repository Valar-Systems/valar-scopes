"""The legend's bench check (docs/weatherscope-stage2.md, addendum 7): EVERY SWATCH ON THE BOARD EQUALS THE PALETTE.

    python -I test/fixtures/wx/legend_check.py <COMx> <out.txt> [--png legend.png]
    python -I test/fixtures/wx/legend_check.py --fb <snapshot.bin> <serial-lines.txt> [--png legend.png]   (stage 3, A8.20)
    ... [--theme dark|light]   (3.5t t5, A12.66: the theme the legend was drawn in; dark when absent)

One open of the port (capture_stage1.capture: DTR/RTS low, never reattached). Sends 'L'; the board draws screen 7,
prints its swatch list (WXL lines) and dumps the legend sprite's OWN bytes as 240 tagged rows (WXS, 480 bytes of
big-endian RGB565 each, zlib CRC-32). Then:
  L4  the dump: every row present once, valid base64, 480 bytes, CRC good -- otherwise REFUSED (exit 2, the rig)
  L2  coverage: rows ordered top to bottom are the ruled groups, holding exactly rain 1-4 | 5-7 | 8-10 | 11-13,
      snow 1-6, no coverage 14, no data 15; nothing for dry
  L1  every pixel of every swatch equals the colour the FINAL palette gives its code, derived HERE, independently of
      WxPalette.h: rain from the pinned table (code 1 TEAL (0, 150, 135) by ruling, A10.16; 13 white), snow and the greys from addendum 6's dated
      table, "no data" as the hatch pattern exactly. Expected colours go through LovyanGFX's truncating color565.
  L5  (addendum 8) the background: outside the swatches and the label boxes (WXT lines) every pixel is dry 0x0000;
      inside a label box only 0x0000 or 0xFFFF, and no box is empty
  L6  (addendum 8) the inner radius on the board's own pixels: every non-dry pixel's centre within 112 px of (120, 120)
Exit 0 only when L1, L2, L4, L5 and L6 all hold.
--theme light (3.5t t5, A12.66) grades the light legend: its table derived HERE from A12.43 sections 1-2 as A12.44
ruled them (light (b): rain 1 teal, 5 (135, 135, 0), 6 (155, 128, 0), 13 black, the rest the pinned table's; snow by
S-M; no coverage (93, 93, 93); the hatch (124, 124, 124) on paper), never from WxTheme.h; L5's ground is paper and a label
box holds only paper or the label ink (38, 38, 40); L6 measures the non-paper pixels.
"""
import ast, base64, hashlib, os, re, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from capture_stage1 import capture   # the single-open capture, shared

N, ROW_BYTES = 240, 480
WXS = re.compile(r"^WXS,(\d+),([0-9a-f]{8}),([A-Za-z0-9+/=]*)$")
WXT = re.compile(r"^WXT,(\d+),(-?\d+),(-?\d+),(\d+),(\d+)$")
INNER_RADIUS = 112   # addendum 8, frozen
WXL = re.compile(r"^WXL,(\d+),(rain|snow|nocov|nodata),(\d+),(-?\d+),(-?\d+),(\d+),(\d+)$")
PIN = "f73f0d6264d420cb55efe79d58415ca056391a0cc607fa0efa061f3331418e2f"

raw_tbl = open(os.path.join(HERE, "NWSReflectivity-metpy-v1.6.3.tbl"), "rb").read().replace(b"\r\n", b"\n")
assert hashlib.sha256(raw_tbl).hexdigest() == PIN, "the table is not the pinned one"
TBL = [tuple(round(c * 255) for c in ast.literal_eval(l)) for l in raw_tbl.decode().splitlines() if l.strip()]
RAIN = {1: (0, 150, 135), 13: (255, 255, 255)}   # code 1 teal: Daniel's 2026-10-28 decision (A10.16), as ruled
RAIN.update({c: TBL[c + 1] for c in range(2, 13)})
SNOW = {1: (0, 110, 210), 2: (66, 129, 218), 3: (98, 149, 226), 4: (124, 169, 234), 5: (148, 190, 242), 6: (170, 212, 250)}
NOCOV, HATCH, DRY = (40, 40, 40), (72, 72, 72), (0, 0, 0)
INK = (255, 255, 255)   # the legend's label text in dark: the white role
# 3.5t t5 (A12.66): light (b), from A12.43 sections 1-2 with A12.44 -- never from WxTheme.h
PAPER = (234, 231, 223)
LIGHT_RAIN = dict(RAIN)
LIGHT_RAIN.update({5: (135, 135, 0), 6: (155, 128, 0), 13: (0, 0, 0)})
LIGHT_SNOW = {1: (0, 108, 206), 2: (0, 95, 177), 3: (0, 83, 149), 4: (0, 70, 121), 5: (0, 57, 94), 6: (0, 43, 66)}
THEMES = {"dark": dict(rain=RAIN, snow=SNOW, nocov=NOCOV, hatch=HATCH, ground=DRY, ink=INK),
          "light": dict(rain=LIGHT_RAIN, snow=LIGHT_SNOW, nocov=(93, 93, 93), hatch=(124, 124, 124), ground=PAPER,
                        ink=(38, 38, 40))}
TH = THEMES["dark"]   # set by main() from --theme
GROUPS = [("Light rain", "rain", [1, 2, 3, 4]), ("Moderate", "rain", [5, 6, 7]), ("Heavy", "rain", [8, 9, 10]),
          ("Very heavy / possible hail", "rain", [11, 12, 13]), ("Snow, light -> heavy", "snow", [1, 2, 3, 4, 5, 6]),
          ("No radar coverage", "nocov", [14]), ("No data", "nodata", [15])]

def c565(rgb):
    r, g, b = rgb
    v = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3
    return bytes((v >> 8, v & 0xFF))

def expected(kind, code, x, y):
    if kind == "rain": return c565(TH["rain"][code])
    if kind == "snow": return c565(TH["snow"][code])
    if kind == "nocov": return c565(TH["nocov"])
    return c565(TH["hatch"] if (x + y) % 4 == 0 else TH["ground"])

def png(path, sprite):
    rows = b""
    for y in range(N):
        rows += b"\x00"
        for x in range(N):
            v = sprite[2 * (y * N + x)] << 8 | sprite[2 * (y * N + x) + 1]
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            rows += bytes(((r * 527 + 23) >> 6, (g * 259 + 33) >> 6, (b * 527 + 23) >> 6))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))

def main_fb(argv):
    """--fb <snapshot.bin> <serial-lines.txt>: the edition's legend (docs/weatherscope-stage3.md A8.20). The image is
    one graded /diag/fb snapshot (RGB565BE, 240 x 240); the snapshot rules there replace L4, the serial dump's
    integrity. The lines are the board's own, as printed when the legend opened (WXL swatches, WXT label boxes)."""
    snap = open(argv[2], "rb").read()
    if len(snap) != N * N * 2:
        print(f"REFUSED: the snapshot is {len(snap)} bytes, not {N * N * 2} (the rig)"); return 2
    lines = [l.strip() for l in open(argv[3], encoding="ascii", errors="replace").read().replace("\r", "").split("\n")]
    if not any(WXL.match(l) for l in lines) or not any(WXT.match(l) for l in lines):
        print("REFUSED: no WXL/WXT lines in the board's output (the rig)"); return 2
    print(f"L4 (replaced): one graded /diag/fb snapshot, {len(snap)} bytes, sha256 {hashlib.sha256(snap).hexdigest()[:12]}")
    return grade(snap, lines, argv)


def main(argv):
    global TH
    if "--theme" in argv:
        i = argv.index("--theme")
        if i + 1 >= len(argv) or argv[i + 1] not in THEMES:
            print("REFUSED: --theme takes dark or light (the rig)"); return 2
        TH = THEMES[argv[i + 1]]
        del argv[i:i + 2]
    if TH is THEMES["light"]:
        print("theme: light (A12.43 sections 1-2 with A12.44)")
    if len(argv) >= 2 and argv[1] == "--fb":
        if len(argv) < 4:
            print(__doc__); return 2
        return main_fb(argv)
    if len(argv) < 3:
        print(__doc__); return 2
    raw = capture(argv[1], argv[2], b"L")
    lines = [l.strip() for l in raw.decode("ascii", errors="replace").replace("\r", "").split("\n")]
    if "WX,end" not in lines:
        print(f"CAPTURE FAILED: no WX,end ({len(raw)} bytes). The instrument, not the board."); return 2
    rows, bad, dup = {}, [], []
    for l in lines:
        m = WXS.match(l)
        if not m: continue
        y, crc = int(m.group(1)), int(m.group(2), 16)
        try: data = base64.b64decode(m.group(3), validate=True)
        except Exception: bad.append(y); continue
        if not 0 <= y < N or len(data) != ROW_BYTES or zlib.crc32(data) != crc: bad.append(y); continue
        if y in rows: dup.append(y); continue
        rows[y] = data
    missing = [y for y in range(N) if y not in rows and y not in bad]
    print(f"L4 dump: {len(rows)}/{N} rows good; missing {missing}; corrupt {bad}; duplicated {dup}")
    if missing or bad or dup:
        print("L4: REFUSED -- nothing assembled or graded"); return 2
    sprite = b"".join(rows[y] for y in range(N))
    board_sha = next((l.split(",", 2)[2] for l in lines if l.startswith("WX,legend_sha256,")), "?")
    print(f"   sprite sha256 {hashlib.sha256(sprite).hexdigest()[:16]}; the board's own {board_sha[:16]} -> "
          f"{'consistent' if hashlib.sha256(sprite).hexdigest() == board_sha else 'INCONSISTENT'}")
    return grade(sprite, lines, argv)


def grade(sprite, lines, argv):
    """L2, L1, L5 and L6 on a whole-screen RGB565BE image (the bench's dump, or a /diag/fb snapshot) and the board's
    WXL/WXT lines. Unchanged from the bench's checks; only lifted out so --fb can share them (stage 3, A8.20)."""
    sw = [m.groups() for m in (WXL.match(l) for l in lines) if m]
    sw = [(int(i), k, int(c), int(x), int(y), int(w), int(h)) for i, k, c, x, y, w, h in sw]
    # L2: rows by y, top to bottom, are the ruled groups in order
    ys = sorted({s[4] for s in sw})
    got = [(sorted({s[1] for s in sw if s[4] == y}), sorted(s[2] for s in sw if s[4] == y)) for y in ys]
    want = [([k], codes) for _, k, codes in GROUPS]
    l2 = got == want and len(sw) == 21
    print(f"L2 coverage: {len(sw)} swatches in {len(ys)} rows -> {'EXACTLY the ruled mapping' if l2 else 'NOT the ruled mapping'}")
    if not l2:
        for (name, k, codes), g in zip(GROUPS, got): print(f"     {name}: want {k} {codes}, got {g}")
    # L1: every pixel of every swatch
    fails = []
    for i, k, c, x0, y0, w, h in sw:
        bad_px = sum(1 for y in range(y0, y0 + h) for x in range(x0, x0 + w)
                     if sprite[2 * (y * N + x):2 * (y * N + x) + 2] != expected(k, c, x, y))
        if bad_px: fails.append((i, k, c, bad_px, w * h))
    print(f"L1 every swatch equals the palette: {21 - len(fails)}/21 swatches exact" + ("" if not fails else
          " -> MISMATCH: " + "; ".join(f"#{i} {k} {c}: {b}/{n} px" for i, k, c, b, n in fails)))
    # L5: the background is the theme's ground (dry); labels are the theme's ink on that ground, and non-empty
    boxes = [tuple(int(v) for v in m.groups()[1:]) for m in (WXT.match(l) for l in lines) if m]
    covered = set()
    for _, _, _, x0, y0, w, h in sw:
        covered.update((x, y) for y in range(y0, y0 + h) for x in range(x0, x0 + w))
    labels = set()
    for x0, y0, w, h in boxes:
        labels.update((x, y) for y in range(y0, y0 + h) for x in range(x0, x0 + w))
    px = lambda x, y: sprite[2 * (y * N + x)] << 8 | sprite[2 * (y * N + x) + 1]
    as_int = lambda rgb: int.from_bytes(c565(rgb), "big")
    ground, ink = as_int(TH["ground"]), as_int(TH["ink"])
    bg_bad = sum(1 for y in range(N) for x in range(N) if (x, y) not in covered and (x, y) not in labels and px(x, y) != ground)
    lab_bad = sum(1 for (x, y) in labels if (x, y) not in covered and px(x, y) not in (ground, ink))
    empty = sum(1 for x0, y0, w, h in boxes if not any(px(x, y) == ink for y in range(y0, y0 + h) for x in range(x0, x0 + w)))
    l5 = len(boxes) == 7 and bg_bad == 0 and lab_bad == 0 and empty == 0
    print(f"L5 background: {len(boxes)} label boxes; non-ground pixels outside swatches and labels: {bg_bad}; non-ink/ground "
          f"inside labels: {lab_bad}; empty label boxes: {empty} -> {'the ground, as ruled' if l5 else 'NOT as predicted'}")
    # L6: every non-ground pixel inside the inner radius
    lit = [(x, y) for y in range(N) for x in range(N) if px(x, y) != ground]
    far = max(((x + 0.5 - 120) ** 2 + (y + 0.5 - 120) ** 2) ** 0.5 for x, y in lit) if lit else 0.0
    l6 = far <= INNER_RADIUS
    print(f"L6 inner radius: {len(lit)} non-dry pixels; the farthest pixel centre is {far:.1f} px from the centre "
          f"(<= {INNER_RADIUS}) -> {'inside' if l6 else 'OUTSIDE'}")
    if "--png" in argv:
        png(argv[argv.index("--png") + 1], sprite); print(f"   PNG of the board's legend -> {argv[argv.index('--png') + 1]}")
    return 0 if (l2 and not fails and l5 and l6) else 1

if __name__ == "__main__":
    sys.exit(main(sys.argv))
