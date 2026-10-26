"""Grades ring_probe.py's trace against docs/weatherscope-stage3.md A9.34's N1-N5 (or S-FIRE's frozen set).

    python -I tools/weatherscope/board/grade_ring.py <run dir> [--sabotage]

Per press (one 'R' block after a 'P'):
  base  the median push-start interval of the 16 frames before the first held (H) frame
  N1    every frame from the first H frame up to and including the first frame drawn at 1000 permille: dt <= base + 5,000
  N2    that frame has no rebuild (no R flag) at the old radius; the NEXT frame is the rebuild (R flag, new radius) with
        dt >= base + 30,000
  N3    every frame after the rebuild frame while held: dt <= base + 5,000 and ring 1000
  N4    every build record: |decode + scen + from + compose + sha + print - total| <= 500, with a scene and a radius
  N5    among the board's lines since the previous 'R' block: exactly one "[wx] zoom hold" line, and exactly one
        "[wx] screen=" line after it, naming the new radius
Without --sabotage every row must hold. With --sabotage (S-FIRE) N1 and N2 must fail in every press and N3-N5 hold.
"""
import re, statistics, sys

FRAME = re.compile(r"\[wx\] frame (\d+) start=(\d+) dt=(\d+) ring=(\d+) flags=(.)(.)(.) scene=(\S+) mi=(\d+)")
BUILD = re.compile(r"\[wx\] build \d+ why=(\S+) scene=(\S+) mi=(\d+)->(\d+) decode=(\d+) scen=(\d+) from=(\d+) "
                   r"compose=(\d+) sha=(\d+) print=(\d+) total=(\d+) prints=(\d+) short=(\d+)")
SCREEN = re.compile(r"\[wx\] screen=scenario (\S+) R=(\d+)")


def board_lines(path):
    out = []
    for raw in open(path, encoding="utf-8"):
        line = raw.rstrip("\n").split(" ", 1)[1] if " " in raw else raw.rstrip("\n")
        out.append(line)
    return out


def blocks(lines):
    """Each 'R' print: (index of '[wx] builds', builds, frames, index of its last frame line)."""
    res, i = [], 0
    while i < len(lines):
        if lines[i].startswith("[wx] builds n="):
            j, builds, frames = i + 1, [], []
            while j < len(lines) and (lines[j].startswith("[wx] build ") or lines[j].startswith("#")):
                m = BUILD.search(lines[j])
                if m:
                    builds.append(m.groups())
                j += 1
            if j < len(lines) and lines[j].startswith("[wx] frames n="):
                j += 1
                while j < len(lines) and (lines[j].startswith("[wx] frame ") or lines[j].startswith("#")):
                    m = FRAME.search(lines[j])
                    if m:
                        frames.append(m.groups())
                    j += 1
            res.append((i, builds, frames, j))
            i = j
        else:
            i += 1
    return res


def grade_press(builds, frames, prior):
    rows = {}
    fr = [dict(dt=int(f[2]), ring=int(f[3]), held=f[4] == "H", fired=f[5] == "F", rebuilt=f[6] == "R", mi=int(f[8]))
          for f in frames]
    first_h = next((i for i, f in enumerate(fr) if f["held"]), None)
    if first_h is None or first_h < 16:
        return {"N1": "BLIND (no press or under 16 frames before it)"}
    base = statistics.median([f["dt"] for f in fr[first_h - 16:first_h]])
    old_mi = fr[first_h - 1]["mi"]
    first_full = next((i for i in range(first_h, len(fr)) if fr[i]["ring"] == 1000), None)
    if first_full is None:
        return {"N1": "BLIND (no 1000 permille frame)"}
    n1_bad = [i for i in range(first_h, first_full + 1) if fr[i]["dt"] > base + 5000]
    rows["N1"] = (not n1_bad, f"base {base}; frames {first_h}..{first_full}; over base+5000: "
                               f"{[(i, fr[i]['dt']) for i in n1_bad]}")
    nxt = first_full + 1
    ok2 = (not fr[first_full]["rebuilt"] and fr[first_full]["mi"] == old_mi and nxt < len(fr) and fr[nxt]["rebuilt"]
           and fr[nxt]["mi"] != old_mi and fr[nxt]["dt"] >= base + 30000)
    rows["N2"] = (ok2, f"first 1000 frame {first_full}: rebuilt={fr[first_full]['rebuilt']} mi={fr[first_full]['mi']} "
                       f"(old {old_mi}); next: " + (f"rebuilt={fr[nxt]['rebuilt']} mi={fr[nxt]['mi']} dt={fr[nxt]['dt']}"
                                                    if nxt < len(fr) else "none"))
    rb = next((i for i in range(first_h, len(fr)) if fr[i]["rebuilt"]), None)
    if rb is None:
        rows["N3"] = (False, "no rebuild frame")
    else:
        after = [i for i in range(rb + 1, len(fr)) if fr[i]["held"]]
        bad3 = [i for i in after if fr[i]["dt"] > base + 5000 or fr[i]["ring"] != 1000]
        rows["N3"] = (bool(after) and not bad3, f"rebuild frame {rb} dt={fr[rb]['dt']}; {len(after)} held frames after; "
                                                f"bad {[(i, fr[i]['dt'], fr[i]['ring']) for i in bad3]}")
    bad4 = []
    for b in builds:
        parts = sum(int(x) for x in b[4:10])
        if abs(parts - int(b[10])) > 500 or not b[1] or not b[2]:
            bad4.append(b)
    rows["N4"] = (bool(builds) and not bad4, f"{len(builds)} builds; bad {bad4}; " +
                  "; ".join(f"{b[0]} {b[1]} {b[2]}->{b[3]} decode={b[4]} scen={b[5]} from={b[6]} compose={b[7]} "
                            f"sha={b[8]} print={b[9]} total={b[10]} short={b[12]}" for b in builds))
    zooms = [i for i, l in enumerate(prior) if l.startswith("[wx] zoom hold")]
    screens_after = [SCREEN.search(l) for l in prior[zooms[0] + 1:]] if len(zooms) == 1 else []
    screens_after = [m for m in screens_after if m]
    new_mi = fr[rb]["mi"] if rb is not None else None
    ok5 = len(zooms) == 1 and len(screens_after) == 1 and int(screens_after[0].group(2)) == new_mi
    rows["N5"] = (ok5, f"zoom-hold lines {len(zooms)}; screen lines after it "
                       f"{[(m.group(1), m.group(2)) for m in screens_after]}; new radius {new_mi}")
    return rows


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    sab = "--sabotage" in argv
    lines = board_lines(argv[1] + "/trace.txt")
    bl = blocks(lines)
    ok, prev_end, presses = True, 0, 0
    for (start, builds, frames, end) in bl:
        prior = [l for l in lines[prev_end:start] if not l.startswith("#")]
        had_press = any(f[4] == "H" for f in frames)
        prev_end = end
        if not had_press:
            continue
        presses += 1
        rows = grade_press(builds, frames, prior)
        print(f"press {presses}:")
        for name in ("N1", "N2", "N3", "N4", "N5"):
            r = rows.get(name)
            if r is None or isinstance(r, str):
                print(f"  {name} {r or 'BLIND'}")
                ok = False
                continue
            want = not (sab and name in ("N1", "N2"))
            good = r[0] == want
            ok &= good
            print(f"  {'ok  ' if good else 'OFF '} {name} {'holds' if r[0] else 'fails'} "
                  f"({'predicted' if good else 'NOT predicted'}): {r[1]}")
    print(f"{presses} presses graded; " + ("AS PREDICTED" if ok and presses else "OFF-PREDICTION: STOP"))
    return 0 if ok and presses else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
