"""Weatherscope 3.5a's SYNTHETIC all-dry crop for the stored Clear scenario (docs/weatherscope-stage3.md Q15, A9.10 SC):
the newest loop frame's 32-byte header, byte for byte, then every cell code 0 as ONE long run (a 0x00 byte and the
run's LEB128 length). Synthetic by construction: no radar ever produced it, and its name says so.

    python -I tools/weatherscope/loop/make_clear.py            writes test/fixtures/wx/loop/synthetic-clear.wxc
    python -I tools/weatherscope/loop/make_clear.py --check    regenerates in memory; SC1 (ring_ref.py's decoder: all
                                                               rows x cols cells, all code 0) and SC2 (its header equals
                                                               the source frame's); exit 1 on any difference

SC1's other half (WxCrop.h's decoder) is test_wx_k35.cpp's. Prints no coordinate.

3.5b's Bend loop (docs/weatherscope-stage3.md A12.12) uses the same rule on its own newest frame:
    python -I tools/weatherscope/loop/make_clear.py --loop test/fixtures/wx/bend-loop --source shsr-20240602-200600.wxc [--check]
Dallas's paths stay the defaults.
"""
import hashlib, importlib.util, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
LOOP = os.path.join(ROOT, "test", "fixtures", "wx", "loop")
SOURCE = os.path.join(LOOP, "shsr-20240521-220000.wxc")
OUT = os.path.join(LOOP, "synthetic-clear.wxc")
sys.dont_write_bytecode = True


def leb128(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def build():
    src = open(SOURCE, "rb").read()
    header = src[:32]
    rows, cols = struct.unpack_from("<HH", header, 28)
    return header + b"\x00" + leb128(rows * cols), rows * cols


def ring_ref():
    path = os.path.join(ROOT, "test", "fixtures", "wx", "ring_ref.py")
    spec = importlib.util.spec_from_file_location("ring_ref", path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def main(argv):
    global LOOP, SOURCE, OUT
    if "--loop" in argv:
        LOOP = os.path.join(ROOT, argv[argv.index("--loop") + 1])
        SOURCE = os.path.join(LOOP, argv[argv.index("--source") + 1])
        OUT = os.path.join(LOOP, "synthetic-clear.wxc")
    data, cells = build()
    if "--check" not in argv:
        open(OUT, "wb").write(data)
        print(f"wrote {os.path.relpath(OUT, ROOT)}: {len(data)} B, {cells} cells, sha256 {hashlib.sha256(data).hexdigest()}")
        return 0
    ok = True
    committed = open(OUT, "rb").read()
    same = committed == data
    print(f"regenerated: {'identical' if same else 'DIFFERS'} ({hashlib.sha256(committed).hexdigest()[:12]})")
    ok &= same
    *_, rows, cols, codes = ring_ref().decode(committed)
    sc1 = rows * cols == cells and len(codes) == cells and codes.count(0) == cells
    print(f"  {'ok  ' if sc1 else 'FAIL'} SC1 (ring_ref.py): {len(codes)} cells, {codes.count(0)} of them code 0")
    ok &= sc1
    sc2 = committed[:32] == open(SOURCE, "rb").read()[:32]
    print(f"  {'ok  ' if sc2 else 'FAIL'} SC2: the 32-byte header equals {os.path.basename(SOURCE)}'s: {sc2}")
    ok &= sc2
    print("make_clear --check: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
