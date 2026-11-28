"""THE v20 SIBLING's FT packages (docs/frame-time-bench.md, "FT on the sibling", accepted at fd5ae9b1a08e): the one
reader of a tracked pin list and the UNTRACKED package directory (FRAME_BENCH_PKG_DIR), shared by the FT envs'
pre-build step and the host rows. It prints names, booleans and counts only -- never a coordinate.
  FT-PKG  each pinned file is present, its length and sha256 equal its pin, and it opens with the "BMPK" magic
  FT-CTR  each package's own centre (lat0Micro, lon0Micro: signed little-endian int32 at offsets 8 and 12, as
          include/BasemapPackage.h's Validate reads them) sits at exactly 2 dp, a multiple of 10,000 micro-degrees
    python -I scripts/ft_packages.py --check <pins> <dir>      exit 0 every pin ok; 1 a pin off; 2 STOP
    python -I scripts/ft_packages.py --selftest <pins> <dir>   the plants each read exactly their row red
A pin line is `<POINT> <variant> <sha256> <length>`; the file is <dir>/<POINT>-<variant>.bmpk."""
import hashlib
import os
import re
import struct
import sys

PIN = re.compile(r"^([A-Z]{3,8}) ([A-Za-z0-9-]{1,16}) ([0-9a-f]{64}) ([0-9]{1,9})$")   # the variant as its file names it


class Stop(Exception):
    pass


def read_pins(path):
    pins, seen = [], set()
    for n, raw in enumerate(open(path, encoding="utf-8").read().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        m = PIN.match(line)
        if not m:
            raise Stop(f"{os.path.basename(path)}:{n}: not a pin line")
        key = (m.group(1), m.group(2))
        if key in seen:
            raise Stop(f"{os.path.basename(path)}:{n}: {key[0]} {key[1]} pinned twice")
        seen.add(key)
        pins.append({"point": m.group(1), "variant": m.group(2), "sha": m.group(3), "len": int(m.group(4))})
    if not pins:
        raise Stop(f"{os.path.basename(path)}: no pins")
    return pins


def file_of(d, pin):
    return os.path.join(d, f"{pin['point']}-{pin['variant']}.bmpk")


def judge(pin, data):
    """(FT-PKG, FT-CTR, detail) for one pin and its bytes (None: the file is absent). Booleans only."""
    if data is None:
        return False, False, "file absent"
    len_ok = len(data) == pin["len"]
    sha_ok = hashlib.sha256(data).hexdigest() == pin["sha"]
    magic_ok = data[:4] == b"BMPK"
    ctr = False
    if len(data) >= 16:
        lat0, lon0 = struct.unpack_from("<ii", data, 8)
        ctr = lat0 % 10000 == 0 and lon0 % 10000 == 0
    pkg = len_ok and sha_ok and magic_ok
    return pkg, ctr, f"length {len_ok}, sha256 {sha_ok}, magic {magic_ok}; centre at 2 dp {ctr}"


def load(d, pin):
    p = file_of(d, pin)
    return open(p, "rb").read() if os.path.isfile(p) else None


def check(pins_path, d):
    pins = read_pins(pins_path)
    off = 0
    for pin in pins:
        pkg, ctr, detail = judge(pin, load(d, pin))
        print(f"  {'ok  ' if pkg else 'RED '} FT-PKG  {'ok  ' if ctr else 'RED '} FT-CTR  "
              f"{pin['point']} {pin['variant']} ({pin['sha'][:12]}): {detail}")
        off += (not pkg) + (not ctr)
    print(f"{len(pins)} pins, {off} row(s) off")
    return 0 if off == 0 else 1


def selftest(pins_path, d):
    pins = read_pins(pins_path)
    pin = pins[0]
    data = load(d, pin)
    if data is None:
        raise Stop("the first pin's file is absent: the selftest cannot plant")
    rc = 0

    def case(name, p, b, want_pkg, want_ctr):
        nonlocal rc
        pkg, ctr, _ = judge(p, b)
        ok = pkg == want_pkg and ctr == want_ctr
        print(f"selftest {'ok' if ok else 'FAIL'}: {name}: FT-PKG {pkg}, FT-CTR {ctr} "
              f"(want {want_pkg}, {want_ctr})")
        rc |= 0 if ok else 1

    case("CLEAN, the first pin as it is", pin, data, True, True)
    flipped = bytearray(data)
    flipped[len(flipped) // 2] ^= 0x01
    case("PLANT, one byte flipped mid-file", pin, bytes(flipped), False, True)
    case("PLANT, the pin's length one more", dict(pin, len=pin["len"] + 1), data, False, True)
    moved = bytearray(data)
    lat0, = struct.unpack_from("<i", moved, 8)
    struct.pack_into("<i", moved, 8, lat0 + 1)
    case("PLANT, the centre 1 micro-degree off 2 dp (re-pinned to its own sha)",
         dict(pin, sha=hashlib.sha256(bytes(moved)).hexdigest()), bytes(moved), True, False)
    case("PLANT, the file absent", pin, None, False, False)
    print("selftest PASSED" if rc == 0 else "selftest FAILED")
    return rc


def main(argv):
    try:
        if len(argv) == 3 and argv[0] == "--check":
            return check(argv[1], argv[2])
        if len(argv) == 3 and argv[0] == "--selftest":
            return selftest(argv[1], argv[2])
        print(__doc__)
        return 2
    except Stop as e:
        print(f"STOP: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
