"""Plants S-WAIT (docs/weatherscope-stage3.md A9.25 item 2, A9.26) into a SCRATCH worktree's ConfigurationWebServer.cpp:
/diag/fb's wait for a finished frame and its retry removed -- one immediate copy, with the verification and every
header kept. Never run on the real tree: the sabotaged image is built from the scratch worktree, flashed for the
S-WAIT pass only, and the shipping image (G) is reflashed after it. There is no bench switch in G.

    python -I tools/weatherscope/board/plant_s_wait.py <scratch worktree>
"""
import hashlib, os, sys

EDITS = [
    ("            while (!framebuf::PhaseFinished(framebuf::Phase()) && micros() - waitT0 < kWaitBoundUs) vTaskDelay(1);\n",
     "            // SABOTAGE (S-WAIT): no wait for a finished frame\n"),
    ("            if (finished || micros() - waitT0 >= kWaitBoundUs) break;\n",
     "            break;   // SABOTAGE (S-WAIT): one immediate copy, never a retry\n"),
]


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    root = os.path.abspath(argv[1])
    here = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    if root == here:
        sys.exit("REFUSED: that is the real tree; S-WAIT goes into a scratch worktree only")
    p = os.path.join(root, "src", "ConfigurationWebServer.cpp")
    raw = open(p, "rb").read()
    crlf = b"\r\n" in raw
    s = raw.decode("utf-8").replace("\r\n", "\n")
    before = s
    for a, b in EDITS:
        assert s.count(a) == 1, f"anchor found {s.count(a)} times, need exactly 1:\n{a}"
        s = s.replace(a, b)
    assert s != before, "the copy is identical -- the plant did not apply"
    out = s.replace("\n", "\r\n") if crlf else s
    open(p, "wb").write(out.encode("utf-8"))
    print(f"S-WAIT planted in {p}: sha256 {hashlib.sha256(out.encode('utf-8')).hexdigest()[:12]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
