"""Grade the light-look viewer's board run (docs/weatherscope-stage3.md A12.32) against look_frames.py's expectations.

    python -I tools/weatherscope/board/grade_look.py <run dir> <look-expect.json>

  LK-BOOT   "[build] env=weatherscope-look-s3-128" and "[look] ready=1 frames=20 looks=3 roles=66"
  LK-HASH   every drawn screen's sha equal to the host's for its (look, frame), and all 60 pairs drawn
  LK-LOOP   per look: 15 records; intervals 70,000 +- 5,000 us, spread <= 10,000; expand_us <= 15,000
Exits 0 only if every row holds. Prints no coordinate.
"""
import json, os, sys

LOOK = {"dark": "dark", "a": "light-a", "b": "light-b"}


def kv(line):
    return dict(t.split("=", 1) for t in line.split() if "=" in t)


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    lines = [l for _, l in json.load(open(os.path.join(argv[1], "lines.json")))]
    expect = json.load(open(argv[2]))["expect"]
    ok, rows = True, []

    def row(name, good, text):
        nonlocal ok
        ok &= bool(good)
        rows.append(f"  {'ok  ' if good else 'FAIL'}  {name}: {text}")

    env = [l for l in lines if l.startswith("[build] env=")]
    ready = [kv(l) for l in lines if l.startswith("[look] ready=")]
    row("LK-BOOT", any("env=weatherscope-look-s3-128" in l for l in env) and ready and ready[-1].get("ready") == "1"
        and ready[-1].get("frames") == "20" and ready[-1].get("looks") == "3" and ready[-1].get("roles") == "66",
        f"{env[-1][:60] if env else 'no [build] line'}; {ready[-1] if ready else 'no [look] ready line'}")
    drawn = [kv(l) for l in lines if l.startswith("[look] look=") or l.startswith("[look] lrec ")]
    seen, bad = set(), []
    for d in drawn:
        key = (LOOK.get(d.get("look"), "?"), d.get("frame"))
        if expect.get(key[0], {}).get(key[1]) == d.get("sha"):
            seen.add(key)
        else:
            bad.append(key)
    row("LK-HASH", not bad and len(seen) == 60,
        f"{len(drawn)} screens reported, {len(seen)} distinct (look, frame) pairs equal to the host's; "
        f"{len(bad)} unequal {sorted(set(bad))[:6]}")
    for l in ("dark", "a", "b"):
        recs = [kv(x) for x in lines if x.startswith(f"[look] lrec look={l} ")]
        starts = [int(r["start_us"]) for r in recs]
        iv = [b - a for a, b in zip(starts, starts[1:])]
        ex = [int(r["expand_us"]) for r in recs]
        good = (len(recs) == 15 and iv and all(65000 <= v <= 75000 for v in iv) and max(iv) - min(iv) <= 10000
                and max(ex) <= 15000)
        row(f"LK-LOOP {l}", good, f"{len(recs)} records; intervals {min(iv) if iv else '-'}-{max(iv) if iv else '-'} us; "
            f"expand_us {min(ex) if ex else '-'}-{max(ex) if ex else '-'}")
    print("\n".join(rows))
    print("viewer: " + ("every row holds" if ok else "OFF-PREDICTION: stop and report"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
