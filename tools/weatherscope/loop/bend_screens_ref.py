"""3.5b step 8's B2-Bend reference (docs/weatherscope-stage3.md A12.55): what the board's code hash must be for each Bend
loop frame, by screens_ref.py's own method -- render_ref.py's render() and the spike's independent Python decoder, not
WxCrop.h, not WxRender.h -- on the 15 crops in test/fixtures/wx/bend-loop/.

    python -I tools/weatherscope/loop/bend_screens_ref.py <spike-checkout>           writes test/fixtures/wx/bend-loop/screens.ref.json
    python -I tools/weatherscope/loop/bend_screens_ref.py <spike-checkout> --check   exit 0 only if the committed file is identical

Per frame: the sha256 of the 57,600-byte code buffer at 60 mi. THE ANCHOR, before any frame is believed:
kdfw-60mi.wxc must render to Stage 1's B9 value, and the spike must be the commit Dallas's screens.ref.json names.
THE CONTROL: no Bend frame's hash may equal any Dallas frame's. Prints hashes and frame names -- no coordinate (the
manifest's centre is never read).
"""
import hashlib
import importlib.util
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
WX = os.path.join(ROOT, "test", "fixtures", "wx")
BEND = os.path.join(WX, "bend-loop")
OUT = os.path.join(BEND, "screens.ref.json")
DALLAS = os.path.join(WX, "loop", "screens.ref.json")
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
    dallas = json.load(open(DALLAS, encoding="utf-8"))
    if commit != dallas["spikeCommit"]:
        sys.exit(f"REFUSED: the spike is at {commit[:12]}; Dallas's reference was made at {dallas['spikeCommit'][:12]}")
    a3 = hashlib.sha256(open(os.path.join(spike, "tools", "wx", "a3_check.py"), "rb").read().replace(b"\r\n", b"\n")).hexdigest()
    decode = render_ref.load_decoder(spike)
    anchor, _ = render_ref.render(os.path.join(WX, "kdfw-60mi.wxc"), decode)
    got = hashlib.sha256(anchor).hexdigest()
    if got != B9:
        sys.exit(f"ANCHOR FAILED: kdfw-60mi.wxc renders to {got[:12]}, B9 is {B9[:12]}; no Bend frame is believed")
    print(f"anchor: kdfw-60mi.wxc renders to B9 ({B9[:12]}); spike {commit[:7]}, a3_check.py {a3[:12]}")
    frames = []
    for f in json.load(open(os.path.join(BEND, "MANIFEST.json"), encoding="utf-8"))["frames"]:
        buf, oob = render_ref.render(os.path.join(BEND, f["name"]), decode)
        s = hashlib.sha256(buf).hexdigest()
        frames.append({"name": f["name"], "frameEpoch": f["frameEpoch"], "screenSha256": s})
        n, echo, _, _ = render_ref.stats(buf)
        print(f"  {f['name']}  {s[:12]}  echo {echo} of {n} disc px; {oob} px outside the crop")
    dallas_shas = {x["screenSha256"] for x in dallas["frames"]}
    same = [x["name"] for x in frames if x["screenSha256"] in dallas_shas]
    print(f"control: Bend frames whose hash equals a Dallas frame's: {len(same)} (want 0)")
    if same or len({x["screenSha256"] for x in frames}) < 2:
        sys.exit("STOP: the Bend hashes cannot tell Bend from Dallas, or each other")
    ref = {
        "what": "3.5b B2-Bend: render_ref.py's render() per Bend loop frame at 60 mi (A12.55); sha256 of the 240x240 code buffer",
        "renderRef": "test/fixtures/wx/render_ref.py",
        "spikeCommit": commit,
        "a3CheckSha256": a3,
        "anchor": {"crop": "kdfw-60mi.wxc", "screenSha256": B9},
        "frames": frames,
    }
    text = json.dumps(ref, indent=1) + "\n"
    if len(argv) == 3:
        committed = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        ok = committed == text
        print("committed bend-loop/screens.ref.json " + ("matches" if ok else "DIFFERS"))
        return 0 if ok else 1
    open(OUT, "w", encoding="utf-8", newline="\n").write(text)
    print(f"wrote {os.path.relpath(OUT, ROOT)}: {len(frames)} frames")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
