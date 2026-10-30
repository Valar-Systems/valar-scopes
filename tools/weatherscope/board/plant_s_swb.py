"""Plants S-SWB (docs/weatherscope-stage3.md A12.66 section 5, Q63) into a SCRATCH worktree's WeatherscopeManager.cpp:
the theme carried into the basemap's key (EnsureBase's MakeBaseKey takes the package length plus `which`), so a theme
switch on a Bend scenario rebuilds the base -- BaseWhy names it "package". Never run on the real tree: the sabotaged
image is built from the scratch worktree, flashed for S-SWB's BSW pass only (run_b35t.py --bsw-only, graded with
grade_b35t.py --swb), and t5's own image is reflashed after it.

    python -I tools/weatherscope/board/plant_s_swb.py <scratch worktree>
    python -I tools/weatherscope/board/plant_s_swb.py --anchors        (Q51: the anchor in the real tree, nothing planted)
"""
import hashlib, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REAL = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
REL = os.path.join("src", "weatherscope", "WeatherscopeManager.cpp")
EDITS = [
    ("    const wxscreens::BaseKey k = wxscreens::MakeBaseKey(h, b.pk.crc32, b.pk.size, R, wxbase::kMaskAll, terrain);\n",
     "    const wxscreens::BaseKey k = wxscreens::MakeBaseKey(h, b.pk.crc32, b.pk.size + (uint32_t)which, R, wxbase::kMaskAll,\n"
     "                                                        terrain);   // SABOTAGE (S-SWB): the theme in the base key\n"),
]


def read(root):
    raw = open(os.path.join(root, REL), "rb").read()
    return raw, b"\r\n" in raw, raw.decode("utf-8").replace("\r\n", "\n")


def main(argv):
    if len(argv) == 2 and argv[1] == "--anchors":
        _, _, s = read(REAL)
        bad = [a for a, _ in EDITS if s.count(a) != 1]
        for a in bad:
            print(f"  FAIL S-SWB: {REL}: anchor found {s.count(a)} times, need exactly 1: {a.strip()[:80]!r}")
        print(f"ANCHORS plant_s_swb.py: 1 plant(s), {len(EDITS)} anchor(s), "
              + ("all present exactly once" if not bad else f"{len(bad)} STRANDED"))
        return 1 if bad else 0
    if len(argv) != 2:
        print(__doc__)
        return 2
    root = os.path.abspath(argv[1])
    if root == REAL:
        sys.exit("REFUSED: that is the real tree; S-SWB goes into a scratch worktree only")
    _, crlf, s = read(root)
    before = s
    for a, b in EDITS:
        assert s.count(a) == 1, f"anchor found {s.count(a)} times, need exactly 1:\n{a}"
        s = s.replace(a, b)
    assert s != before, "the copy is identical -- the plant did not apply"
    out = s.replace("\n", "\r\n") if crlf else s
    open(os.path.join(root, REL), "wb").write(out.encode("utf-8"))
    print(f"S-SWB planted in {os.path.join(root, REL)}: sha256 {hashlib.sha256(out.encode('utf-8')).hexdigest()[:12]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
