"""PrecipFlag reader's host rows (docs/weatherscope-stage3.md A12.9.3): the anchor, the midsummer control, and the
2024-05-02 window as a positive, on named MRMS objects pinned by sha256 (fetched into <cache-dir> at first use).

    python -I tools/weatherscope/loop/test_precipflag.py <bend.bmpk> <cache-dir> [<precipflag.py>]

The optional last argument is the reader to test (sabotage_precipflag.py passes a planted copy). Exit 0 only when every
row passes. Prints no coordinate.
"""
import gzip
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PINS = {
    "20240502-055200": "c7fa2dc1fd521fc4c854afa50a4e4a1fb58689db1641cb512de64ac58c1f2f7f",
    "20240502-055600": "7580c96f24d0adbfcc8552a18d7e1f57c88a045bb9addfa316446c8de9baade0",
    "20240502-060000": "b4457421da5a6e082646a86479a0e0516647e3fdfe6870ff42122cc17045e51a",
    "20240502-060400": "db7fa26040165341adcaa8ca4f8bd4e1d392e1068f7bbccd04822b7a84f7679c",
    "20240502-060800": "a6598f0f37de26513d58f325fefb39cf7e8084bad6ab6896095595f979bfdd95",
    "20240502-061200": "04aa652e7f4a0a7d7a9582036e0a28342ee96f28262a82462cdc085f15090048",
    "20240502-061600": "d6328589484910650c550a9277b01de3ac2ed74d2e81a4597b69001a7e84f370",
    "20240502-062000": "3d10e5406066afd7588aec18921bbc3e21135448de44c40f3fb70e8676a269ca",
    "20240502-062400": "7f5a01e13c5d3f11b3214386b2a79898330aa972119b98aeb98bb13150b3eb6f",
    "20240502-062800": "f9ace429d8ce2e6e3ade9a79804c52dfed74bd09e9cd1f6f3de2171365d300c3",
    "20240502-063200": "1ff395ad495a191b6246d992cf921b46260d3764a724b04b721f6dfcd6c066f3",
    "20240502-063600": "60d150eba944cc6a8b5954b1058d3b724657ab9b99a83969b9b15c54fb27ea24",
    "20240502-064000": "ec0c45f31f046b6469dbb57228fe208d4d00964964040d8d739401fee623f706",
    "20240502-064400": "4c6807310818b5e5e79832e9e824bc0895676da7bfee9b8cedc598ad2b5e4218",
    "20240502-064800": "eff439fbf23b2b4fabe1ccb12b64389ec1c6faaab93a20091055dbaf79ec46ef",
    "20240805-000000": "1a93fb9a085529d5a93159d941cf73d79afc4007bc8380deb4718472a8f75d44",
}
WINDOW = [s for s in PINS if s.startswith("20240502-")]
SUMMER = "20240805-000000"


def load(path):
    spec = importlib.util.spec_from_file_location("pf_under_test", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    pf = load(argv[3] if len(argv) > 3 else os.path.join(HERE, "precipflag.py"))
    lat0, lon0 = pf.origin(argv[1])
    cache = argv[2]
    got = {}
    for stamp, want in PINS.items():
        f = pf.fetch(cache, stamp)
        if f is None or f[1] != want:
            print(f"REFUSED: {stamp} is {'missing' if f is None else 'sha256 ' + f[1][:12]}, not the pinned {want[:12]} (the rig)")
            return 2
        got[stamp] = f[0]
    results = []

    def row(name, ok, detail):
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'}  {name} {detail}")

    # PF1: the anchor. The real object is accepted; a copy whose grid says ni = 6999 is refused.
    first = WINDOW[0]
    real_ok = True
    try:
        pf.histogram(got[first], lat0, lon0)
    except pf.Refused:
        real_ok = False
    g = bytearray(gzip.decompress(got[first]))
    o = 16
    while struct.unpack_from(">IB", g, o)[1] != 3:
        o += struct.unpack_from(">I", g, o)[0]
    assert struct.unpack_from(">I", g, o + 30)[0] == 7000
    struct.pack_into(">I", g, o + 30, 6999)
    try:
        pf.histogram(gzip.compress(bytes(g)), lat0, lon0)
        copy_refused = False
    except pf.Refused:
        copy_refused = True
    row("PF1", real_ok and copy_refused, f"real accepted {real_ok}; ni = 6999 copy refused {copy_refused}")

    h = pf.histogram(got[SUMMER], lat0, lon0)
    snow = pf.snow_cells(got[SUMMER], lat0, lon0)
    row("PF2", snow == 0 and h.get(1, 0) == 3824 and h.get(6, 0) == 203,
        f"{SUMMER}: snow {snow}, warm stratiform {h.get(1, 0)}, convective {h.get(6, 0)} (want 0, 3,824, 203)")
    snow = pf.snow_cells(got[first], lat0, lon0)
    row("PF3", snow == 1574, f"{first}: snow {snow} (want 1,574)")
    total = sum(pf.snow_cells(got[s], lat0, lon0) for s in WINDOW)
    row("PF4", total == 19427, f"the window's {len(WINDOW)} times: snow {total} (want 19,427)")
    print(f"{len(results)} rows, {sum(results)} passed, {len(results) - sum(results)} failed")
    return 0 if all(results) and len(results) == 4 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
