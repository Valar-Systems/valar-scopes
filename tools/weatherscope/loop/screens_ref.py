"""3.3b's K2 reference (docs/weatherscope-stage3.md A8.16): what the 240 x 240 screen must show for each loop frame,
from render_ref.py's own render() and the spike's independent Python decoder -- not WxCrop.h, not WxRender.h.

    python -I tools/weatherscope/loop/screens_ref.py <spike-checkout>           writes test/fixtures/wx/loop/screens.ref.json
    python -I tools/weatherscope/loop/screens_ref.py <spike-checkout> --check   exit 0 only if the committed file is identical

Per frame: the sha256 of the 57,600-byte code buffer, and of its --flip-ns render (S-K2a's predicted set, computed by
the reference). THE ANCHOR, before any loop frame is believed: kdfw-60mi.wxc must render to Stage 1's B9 value.
Prints hashes and frame names -- no coordinate.
"""
import hashlib
import importlib.util
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
WX = os.path.join(ROOT, "test", "fixtures", "wx")
LOOP = os.path.join(WX, "loop")
OUT = os.path.join(LOOP, "screens.ref.json")
B9 = "344383b4cfae04eec5a7a844cfb5f1b9aaa4ce8fa15f30d1b956d4e86ac05225"   # docs/weatherscope-stage1.md, B9

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("render_ref", os.path.join(WX, "render_ref.py"))
render_ref = importlib.util.module_from_spec(spec)
spec.loader.exec_module(render_ref)


def main(argv):
    if len(argv) not in (2, 3) or (len(argv) == 3 and argv[2] != "--check"):
        print(__doc__)
        return 2
    spike = argv[1]
    commit = subprocess.run(["git", "-C", spike, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    a3_bytes = open(os.path.join(spike, "tools", "wx", "a3_check.py"), "rb").read()
    a3 = hashlib.sha256(a3_bytes.replace(b"\r\n", b"\n")).hexdigest()   # the git blob's hash on any checkout
    decode = render_ref.load_decoder(spike)
    anchor, _ = render_ref.render(os.path.join(WX, "kdfw-60mi.wxc"), decode)
    got = hashlib.sha256(anchor).hexdigest()
    if got != B9:
        sys.exit(f"ANCHOR FAILED: kdfw-60mi.wxc renders to {got[:12]}, B9 is {B9[:12]}; no loop frame is believed")
    print(f"anchor: kdfw-60mi.wxc renders to B9 ({B9[:12]}); spike {commit[:7]}, a3_check.py {a3[:12]}")
    frames = []
    for f in json.load(open(os.path.join(LOOP, "MANIFEST.json")))["frames"]:
        path = os.path.join(LOOP, f["name"])
        buf, oob = render_ref.render(path, decode)
        flip, _ = render_ref.render(path, decode, flip_ns=True)
        s, fs = hashlib.sha256(buf).hexdigest(), hashlib.sha256(flip).hexdigest()
        frames.append({"name": f["name"], "screenSha256": s, "flipSha256": fs})
        n, echo, _, _ = render_ref.stats(buf)
        print(f"  {f['name']}  {s[:12]}  flip {fs[:12]}{'' if s != fs else '  (FLIP EQUAL)'}  "
              f"echo {echo} of {n} disc px; {oob} px outside the crop")
    differ = sum(1 for x in frames if x["screenSha256"] != x["flipSha256"])
    print(f"frames whose flip differs: {differ} of {len(frames)}")
    if differ == 0:
        sys.exit("STOP (A8.16): no frame's flip differs, so S-K2a could not fail")
    ref = {
        "what": "3.3b K2: render_ref.py's render() per loop frame (A8.16); sha256 of the 240x240 code buffer",
        "renderRef": "test/fixtures/wx/render_ref.py",
        "spikeCommit": commit,
        "a3CheckSha256": a3,
        "anchor": {"crop": "kdfw-60mi.wxc", "screenSha256": B9},
        "frames": frames,
    }
    text = json.dumps(ref, indent=1) + "\n"
    if len(argv) == 3:
        committed = open(OUT).read() if os.path.exists(OUT) else ""
        ok = committed == text
        print("committed screens.ref.json " + ("matches" if ok else "DIFFERS"))
        return 0 if ok else 1
    open(OUT, "w", newline="\n").write(text)
    print(f"wrote {os.path.relpath(OUT, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
