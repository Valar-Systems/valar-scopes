"""Plants S-TW (docs/weatherscope-stage3.md A12.84, A12.87) into a SCRATCH worktree's WeatherscopeManager.cpp: the touch
supervisor's OnPoll call removed, its 10-minute [health] line kept. On the board the heartbeat must still print, with
probes=0/0 on every line -- the control that shows the line separates "runs" from "never runs".

    python -I tools/weatherscope/board/plant_s_tw.py <scratch worktree>    plants, asserting the edit applied
    python -I tools/weatherscope/board/plant_s_tw.py --anchors             (Q51: the anchor in this tree, nothing planted)
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
REL = os.path.join("src", "weatherscope", "WeatherscopeManager.cpp")
ANCHOR = ("    if constexpr (variant::TOUCH_WATCHDOG)\n"
          "        if (poll != TouchPoll::Skipped) TouchWatchdog::OnPoll(tft, poll == TouchPoll::Touched, true);\n")
PLANT = "    // SABOTAGE (S-TW): the OnPoll call removed; the heartbeat stays\n"


def read(root):
    raw = open(os.path.join(root, REL), "rb").read()
    return raw, b"\r\n" in raw, raw.decode("utf-8").replace("\r\n", "\n")


def main(argv):
    if len(argv) == 2 and argv[1] == "--anchors":
        _, _, s = read(ROOT)
        n = s.count(ANCHOR)
        if n != 1:
            print(f"  FAIL S-TW: {REL}: anchor found {n} times, need exactly 1")
        print(f"ANCHORS plant_s_tw.py: 1 plant(s), 1 anchor(s), {'all present exactly once' if n == 1 else 'NOT all present'}")
        return 0 if n == 1 else 1
    if len(argv) != 2:
        print(__doc__)
        return 2
    root = argv[1]
    if os.path.abspath(root) == os.path.abspath(ROOT):
        print("REFUSED: S-TW plants into a scratch worktree, never this one")
        return 2
    raw, crlf, s = read(root)
    assert s.count(ANCHOR) == 1, f"anchor found {s.count(ANCHOR)} times, need exactly 1"
    new = s.replace(ANCHOR, PLANT)
    assert new != s and "TouchWatchdog::OnPoll" not in new and "[health] touch-wd" in new
    open(os.path.join(root, REL), "wb").write((new.replace("\n", "\r\n") if crlf else new).encode("utf-8"))
    print("planted S-TW: OnPoll removed, the heartbeat kept")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
