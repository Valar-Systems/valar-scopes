"""Runs test_wx_loop.cpp (D2-D4) and make_loop.mts --check (D1) against SABOTAGED copies of test/fixtures/wx/loop/.
Each must fail EXACTLY the set frozen in docs/weatherscope-stage3.md A8.13, and nothing else.

    python -I test/host/sabotage_wx_loop.py [--d1 <rv460/proxy> <grib-dir>]

Method, fixed before any run:
  - The committed fixture is never edited: every plant goes into a fresh copy, and the fixture's hashes are compared
    before and after the whole run.
  - Each plant asserts that the copy differs from the original after it is applied.
  - The unmodified copy runs first, as the control: exit 0, nothing failing.
  - S-D1 runs only with --d1 (it needs the GRIB download and main's shared code, both outside this repo).
Prints no coordinate.
"""
import hashlib, json, os, re, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass and the colour proof (docs/weatherscope-stage3.md A12.50)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOOP = os.path.join(ROOT, "test", "fixtures", "wx", "loop")
TEST = os.path.join(ROOT, "test", "host", "test_wx_loop.cpp")
MAKE = os.path.join(ROOT, "tools", "weatherscope", "loop", "make_loop.mts")
KDFW = os.path.join(ROOT, "test", "fixtures", "wx", "kdfw-60mi.wxc")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
P = "shsr-20240521-"


def f(t):
    return P + t + ".wxc"


def tree_hash(d):
    h = hashlib.sha256()
    for n in sorted(os.listdir(d)):
        h.update(n.encode() + b"\0" + open(os.path.join(d, n), "rb").read())
    return h.hexdigest()


def xor_byte(d, name, off, mask):
    p = os.path.join(d, name)
    b = bytearray(open(p, "rb").read())
    b[off] ^= mask
    open(p, "wb").write(bytes(b))


def add_u32(d, name, off, delta):
    p = os.path.join(d, name)
    b = bytearray(open(p, "rb").read())
    v = int.from_bytes(b[off:off + 4], "little", signed=True) + delta
    b[off:off + 4] = v.to_bytes(4, "little", signed=True)
    open(p, "wb").write(bytes(b))


def manifest_edit(d, fn):
    p = os.path.join(d, "MANIFEST.json")
    m = json.load(open(p))
    fn(m["frames"])
    open(p, "w", newline="\n").write(json.dumps(m, indent=1) + "\n")


def set_codes(frames):
    by = {x["name"]: x for x in frames}
    by[f("211200")]["codesSha256"] = by[f("211600")]["codesSha256"]


def swap(frames):
    i = [x["name"] for x in frames].index(f("214800"))
    assert frames[i + 1]["name"] == f("215200")
    frames[i], frames[i + 1] = frames[i + 1], frames[i]


# name -> (plant on the copied dir, or None; the test-source edit, or None; the exact failing set)
SABOTAGES = {
    "S-D2a": (lambda d: xor_byte(d, f("212400"), 32, 0x10), None, {"D2:" + P + "212400"}),
    "S-D2b": (lambda d: manifest_edit(d, set_codes), None, {"D2:" + P + "211200"}),
    "S-D2c": (None, ("b.size() <= 32768", "b.size() <= 3000"),
              {"D2:" + P + t for t in ("212000", "212400", "212800", "213200", "213600", "214000", "214400",
                                        "214800", "215200", "215600", "220000")}),
    "S-D3": (lambda d: add_u32(d, f("214000"), 12, 1), None, {"D3:" + P + "214000"}),
    "S-D4a": (lambda d: add_u32(d, f("213200"), 8, 100), None, {"D4:" + P + "213200", "D4:" + P + "213600"}),
    "S-D4b": (lambda d: manifest_edit(d, swap), None,
              {"D4:" + P + "215200", "D4:" + P + "214800", "D4:" + P + "215600"}),
}


def build(td, name, src):
    p = os.path.join(td, name + ".cpp")
    open(p, "w", encoding="utf-8", newline="\n").write(src)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + os.path.join(ROOT, "include"), p, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        sys.exit(2)
    return exe


def run(exe, d, name):
    r = subprocess.run([exe, d], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        print(f"{name}: the test did not run (exit {r.returncode}): the RIG\n{r.stdout}{r.stderr}")
        sys.exit(2)
    return r.returncode, {m.group(1) for m in re.finditer(r"^\s*FAIL (\S+)", r.stdout, re.M)}, r.stdout


def copy_loop(td, name):
    d = os.path.join(td, name)
    shutil.copytree(LOOP, d)
    return d


def main(argv):
    d1 = argv[1:2] == ["--d1"]
    if d1 and len(argv) != 4:
        print(__doc__)
        return 2
    before = tree_hash(LOOP)
    src = open(TEST, encoding="utf-8").read().replace("\r\n", "\n")
    ok = True
    with tempfile.TemporaryDirectory() as td:
        exe = build(td, "real", src)
        rc, fails, _ = run(exe, copy_loop(td, "control"), "control")
        good = rc == 0 and not fails
        print(f"control (an unmodified copy): exit {rc}, failing {sorted(fails)} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        for name, (plant, edit, expect) in SABOTAGES.items():
            d = copy_loop(td, name)
            e = exe
            if plant:
                h0 = tree_hash(d)
                plant(d)
                assert tree_hash(d) != h0, f"{name}: the copy is unchanged -- the plant did not apply"
            if edit:
                assert src.count(edit[0]) == 1, f"{name}: anchor found {src.count(edit[0])} times, need exactly 1"
                s2 = src.replace(*edit)
                assert s2 != src, f"{name}: the test copy is unchanged"
                e = build(td, name, s2)
            rc, fails, out = run(e, d, name)
            good = rc == 1 and fails == expect
            short = sorted(x.replace(P, "") for x in fails)
            print(f"{name}: failing {short}, predicted {sorted(x.replace(P, '') for x in expect)} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            if name == "S-D2a":
                v = re.search(r"D2:" + P + r"212400 \S+ B \(manifest \d+, not graded\), Validate (\S+),", out)
                print(f"  (printed, not graded: Validate on the sabotaged 212400 says {v.group(1) if v else '?'})")
            ok &= good
        if d1:
            proxy, grib = argv[2], argv[3]
            d = copy_loop(td, "S-D1")
            h0 = tree_hash(d)
            xor_byte(d, f("212400"), 40, 0x01)
            assert tree_hash(d) != h0, "S-D1: the copy is unchanged -- the plant did not apply"
            npx = shutil.which("npx") or shutil.which("npx.cmd")
            r = subprocess.run([npx, "tsx", MAKE, ".", grib, KDFW, d, "--check"], cwd=proxy, capture_output=True, text=True)
            differs = set(re.findall(r"^\s+(\S+): DIFFERS", r.stdout, re.M))
            same = set(re.findall(r"^\s+(\S+): identical", r.stdout, re.M))
            good = r.returncode == 1 and differs == {f("212400")} and len(same) == 15 and "MANIFEST.json" in same
            print(f"S-D1: exit {r.returncode}, DIFFERS {sorted(differs)}, identical {len(same)} (manifest "
                  f"{'among them' if 'MANIFEST.json' in same else 'NOT among them'}) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            if not good:
                print(r.stdout + r.stderr)
            ok &= good
    after = tree_hash(LOOP)
    print(f"the committed test/fixtures/wx/loop/ unchanged: {before == after} ({after[:12]})")
    n = len(SABOTAGES) + (1 if d1 else 0)
    print(f"{n} sabotages: " + ("ALL AS PREDICTED" if ok and before == after else "OFF-PREDICTION: STOP"))
    return 0 if ok and before == after else 1


def anchor_entries():
    """Q51: what each plant targets, for --anchors (no plant runs): the frames it edits exist and reach the bytes it
    edits, the manifest names the frames it moves, and the test-source edit's anchor occurs once."""
    def frame(name, need):
        p = os.path.join(LOOP, name)
        try:
            data = open(p, "rb").read()
        except OSError:
            return (name, os.path.relpath(p, ROOT), None, None)
        def check(b, need=need):
            assert len(b) > need, f"{len(b)} bytes, the plant edits byte {need}"
        return (name, os.path.relpath(p, ROOT), data, check)
    mp = os.path.join(LOOP, "MANIFEST.json")
    def names(*want):
        def check(t):
            got = [x["name"] for x in json.loads(t)["frames"]]
            for w in want:
                assert got.count(w) == 1, f"{w} named {got.count(w)} times in MANIFEST.json"
        return check
    def adjacent(t):
        got = [x["name"] for x in json.loads(t)["frames"]]
        i = got.index(f("214800"))
        assert got[i + 1] == f("215200"), "214800 is not followed by 215200"
    mt = sc.read(mp)
    out = []
    for n, (plant_name, need) in {"S-D2a": ("212400", 32), "S-D3": ("214000", 15), "S-D4a": ("213200", 11)}.items():
        e = frame(f(plant_name), need)
        out.append((n,) + e[1:])
    out.append(("S-D2b", os.path.relpath(mp, ROOT), mt, names(f("211200"), f("211600"))))
    out.append(("S-D4b", os.path.relpath(mp, ROOT), mt, adjacent))
    out.append(("S-D2c", os.path.relpath(TEST, ROOT), sc.read(TEST), SABOTAGES["S-D2c"][1][0]))
    return out

# --control (Q57, docs/weatherscope-stage3.md A12.61): this driver's unplanted run alone, each suite against its own
# count, nothing planted. run.sh runs it for every driver.
CONTROL_TABLE = {"test_wx_loop": ("checks", 44)}


def control():
    with tempfile.TemporaryDirectory() as td:
        exe = build(td, "test_wx_loop", open(TEST, encoding="utf-8").read().replace("\r\n", "\n"))
        run(exe, copy_loop(td, "control"), "control")


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main(sys.argv))
