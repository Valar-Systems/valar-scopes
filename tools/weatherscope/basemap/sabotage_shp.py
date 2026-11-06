"""Runs test_shp.py against SABOTAGED copies of shp.py; each must fail EXACTLY the rows docs/weatherscope-stage3.md
A12.2 froze for it, and nothing else.

    python -I tools/weatherscope/basemap/sabotage_shp.py

The real reader is never edited: each plant goes into a copy in a temporary folder, every anchor must occur exactly
once, the copy must differ, and the real file's sha256 is compared before and after.

  S-PARTS  every record's points returned as one part, and the parts checks skipped -> exactly SH2, SH8
  S-NULL   null records skipped, not returned as None                                -> exactly SH3
  S-LEN    the file-length check skipped                                             -> exactly SH5
  S-DEL    a '*' row returned live (the flag still validated)                        -> exactly DB1
  S-UTF8   C fields decoded as Latin-1                                               -> exactly DB2, DB6
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
READER = os.path.join(HERE, "shp.py")
TEST = os.path.join(HERE, "test_shp.py")

SABOTAGES = {
    "S-PARTS": ([("            if idx[0] != 0 or any(idx[k] >= idx[k + 1] for k in range(nparts - 1)) or idx[-1] >= npoints:\n"
                  "                raise Bad(\"kParts\")\n", ""),
                 ("records.append(Record(rtype, box, [xy[s:e] for s, e in zip(idx, bounds)]))",
                  "records.append(Record(rtype, box, [xy]))  # SABOTAGE")],
                {"SH2", "SH8"}),
    "S-NULL": ([("            records.append(None)\n", "            pass  # SABOTAGE\n")], {"SH3"}),
    "S-LEN": ([("    if struct.unpack_from(\">i\", b, 24)[0] * 2 != len(b):\n        raise Bad(\"kLength\")\n", "")], {"SH5"}),
    "S-DEL": ([("        deleted.append(flag == 0x2A)", "        deleted.append(False)  # SABOTAGE")], {"DB1"}),
    "S-UTF8": ([("raw.decode(\"utf-8\", \"strict\")", "raw.decode(\"latin-1\")")], {"DB2", "DB6"}),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
    before = sha(READER)
    src = open(READER, encoding="utf-8").read()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        for name, (edits, want) in SABOTAGES.items():
            out = src
            for anchor, repl in edits:
                n = out.count(anchor)
                assert n == 1, f"{name}: anchor found {n} times:\n{anchor}"
                out = out.replace(anchor, repl)
            assert out != src, f"{name}: the copy did not change"
            planted = os.path.join(td, name + ".py")
            open(planted, "w", encoding="utf-8", newline="\n").write(out)
            r = subprocess.run([sys.executable, "-I", TEST, planted], capture_output=True, text=True)
            fails = {m.group(1) for m in re.finditer(r"^\s+FAIL\s+(\S+)", r.stdout, re.M)}
            crashed = r.returncode not in (0, 1) or "Traceback" in r.stderr
            good = not crashed and r.returncode == 1 and fails == want
            print(f"{name}: exit {r.returncode}; failing {sorted(fails)}, predicted {sorted(want)} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            if crashed:
                print(r.stderr[-800:])
            ok &= good
    after = sha(READER)
    print(f"real shp.py unchanged: {before == after} ({after[:12]})")
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
