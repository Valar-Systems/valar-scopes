"""LC (docs/weatherscope-stage3.md A12.5, A10.14, A10.18): a stored package's licence file carries the SRTM credit, SRTM's
§3.15 notice with the year 2000, and the note on joerd's 2014 update. --selftest runs the control first: the notice
deleted from a copy must turn LC red, or LC proves nothing.

    python -I tools/weatherscope/basemap/check_licence.py <bend.LICENSE.txt> [--selftest]
"""
import sys

CREDIT = "SRTM data courtesy of the U.S. Geological Survey"
NOTICE = "© COPYRIGHT 2000 BY THE UNITED STATES GOVERNMENT. NO COPYRIGHT CLAIMED UNDER TITLE 17 U.S.C."
NOTE = "joerd records several updates, including in 2014"


def lc(text):
    """[(what, present)] for each required line."""
    lines = text.replace("\r\n", "\n").split("\n")
    return [("credit", CREDIT in lines), ("notice with 2000", NOTICE in lines), ("2014 note", any(NOTE in l for l in lines))]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    text = open(argv[1], encoding="utf-8").read()
    if "--selftest" in argv:
        without = text.replace(NOTICE + "\n", "")
        assert without != text, "the control did not apply: the notice line was not found to delete"
        red = [w for w, ok in lc(without) if not ok]
        if red != ["notice with 2000"]:
            print(f"LC selftest FAILED: with the notice deleted, LC reports {red or 'nothing missing'}")
            return 2
        print("LC selftest: the notice deleted from a copy turns LC red (only that row), as it must")
    rows = lc(text)
    for what, ok in rows:
        print(f"  {'ok  ' if ok else 'FAIL'}  LC {what}")
    return 0 if all(ok for _, ok in rows) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
