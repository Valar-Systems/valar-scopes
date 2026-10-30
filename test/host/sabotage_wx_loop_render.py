"""Runs test_wx_loop_render.cpp (K2) against SABOTAGED inputs. Each must fail EXACTLY the set frozen in
docs/weatherscope-stage3.md A8.16, and nothing else.

    python -I test/host/sabotage_wx_loop_render.py

  control  the real WxRender.h and the real screens.ref.json: nothing fails
  S-K2a    a copy of WxRender.h with Stage 1's N/S flip (`v->homeLat + 1.0 * (kCentre` -> `-1.0`): exactly the frames
           whose reference sha differs from their --flip-ns sha in screens.ref.json
  S-K2b    a copy of screens.ref.json with 213600's sha replaced by 214000's: exactly {K2:213600}

The real files are never edited: each plant asserts its anchor occurs once and that the copy differs, and both real
files' hashes are compared before and after.
"""
import hashlib, json, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass and the colour proof (docs/weatherscope-stage3.md A12.50)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RENDER = os.path.join(ROOT, "src", "weatherscope", "WxRender.h")
LOOP = os.path.join(ROOT, "test", "fixtures", "wx", "loop")
REF = os.path.join(LOOP, "screens.ref.json")
TEST = os.path.join(ROOT, "test", "host", "test_wx_loop_render.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
P = "shsr-20240521-"


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def build(td, name, render_text):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, "WxRender.h"), "w", encoding="utf-8", newline="\n").write(render_text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "src", "weatherscope"),
                        "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        sys.exit(2)
    return exe


def run(exe, ref, name):
    r = subprocess.run([exe, LOOP, ref], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        print(f"{name}: the test did not run (exit {r.returncode}): the RIG\n{r.stdout}{r.stderr}")
        sys.exit(2)
    return r.returncode, {m.group(1) for m in re.finditer(r"^\s*FAIL (K2:\d{6})", r.stdout, re.M)}


def main():
    before = (sha(RENDER), sha(REF))
    render_src = open(RENDER, encoding="utf-8").read().replace("\r\n", "\n")
    ref = json.load(open(REF))
    flip_set = {"K2:" + f["name"][len(P):len(P) + 6] for f in ref["frames"] if f["screenSha256"] != f["flipSha256"]}
    ok = True
    with tempfile.TemporaryDirectory() as td:
        exe = build(td, "control", render_src)
        rc, fails = run(exe, REF, "control")
        good = rc == 0 and not fails
        print(f"control: exit {rc}, failing {sorted(fails)} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good

        anchor = "v->homeLat + 1.0 * (kCentre"
        assert render_src.count(anchor) == 1, f"S-K2a: anchor found {render_src.count(anchor)} times"
        planted = render_src.replace(anchor, "v->homeLat + -1.0 * (kCentre")
        assert planted != render_src
        rc, fails = run(build(td, "S-K2a", planted), REF, "S-K2a")
        good = rc == 1 and fails == flip_set
        print(f"S-K2a: {len(fails)} failing, predicted the {len(flip_set)} whose flip differs; "
              f"sets {'equal' if fails == flip_set else 'DIFFER: ' + str(sorted(fails ^ flip_set))} -> "
              f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good

        by = {f["name"]: f for f in ref["frames"]}
        by[P + "213600.wxc"]["screenSha256"] = by[P + "214000.wxc"]["screenSha256"]
        assert json.load(open(REF)) != ref, "S-K2b: the copy is unchanged"
        p = os.path.join(td, "S-K2b-screens.ref.json")
        open(p, "w", newline="\n").write(json.dumps(ref, indent=1) + "\n")
        rc, fails = run(exe, p, "S-K2b")
        good = rc == 1 and fails == {"K2:213600"}
        print(f"S-K2b: failing {sorted(fails)}, predicted ['K2:213600'] -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
    after = (sha(RENDER), sha(REF))
    print(f"the real WxRender.h and screens.ref.json unchanged: {before == after}")
    print("2 sabotages: " + ("ALL AS PREDICTED" if ok and before == after else "OFF-PREDICTION: STOP"))
    return 0 if ok and before == after else 1


def anchor_entries():
    """Q51: S-K2a's anchor in WxRender.h, and the two frames S-K2b swaps in screens.ref.json, for --anchors."""
    def frames(t):
        got = [x["name"] for x in json.loads(t)["frames"]]
        for w in (P + "213600.wxc", P + "214000.wxc"):
            assert got.count(w) == 1, f"{w} named {got.count(w)} times"
    return [("S-K2a", os.path.relpath(RENDER, ROOT), sc.read(RENDER), "v->homeLat + 1.0 * (kCentre"),
            ("S-K2b", os.path.relpath(REF, ROOT), sc.read(REF), frames)]

if __name__ == "__main__":
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
