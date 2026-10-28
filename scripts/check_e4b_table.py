"""The Missileer's E-4B table holds the four E-4Bs and none of the C-2As it once held (docs/missileer-e4b-table.md).

    python scripts/check_e4b_table.py                 check src/eam/EamModels.cpp
    python scripts/check_e4b_table.py --file PATH     check another copy of it (e.g. main's, as a control)
    python scripts/check_e4b_table.py --elf A.elf ... scan built firmware: every E-4B string present, no C-2A
    python scripts/check_e4b_table.py --selftest      prove the check can fail, including on the real file

Exit 0 = as required, 1 = a violation, 2 = the rig (the table cannot be found, or found twice). A rig failure is
never reported as a pass.

The four addresses and their serials are cited in the comment above the table (from #409): ADFEB3 73-1676,
ADFEB4 73-1677, ADFEB5 74-0787, ADFEB6 75-0125. ae0451..ae0454 are US Navy C-2A Greyhounds."""
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "eam", "EamModels.cpp")
WANT = ["adfeb3", "adfeb4", "adfeb5", "adfeb6"]
C2A = ["ae0451", "ae0452", "ae0453", "ae0454"]
TABLE = re.compile(r"const\s+char\s*\*\s*const\s+kE4bHexes\s*\[\s*\]\s*=\s*\{([^}]*)\}\s*;")


def check_text(text):
    """(code, message) for a copy of EamModels.cpp."""
    found = TABLE.findall(text)
    if len(found) != 1:
        return 2, f"RIG: the kE4bHexes initializer appears {len(found)} times, not once"
    got = re.findall(r'"([^"]*)"', found[0])
    bad = [h for h in got if h.lower() in C2A]
    if bad:
        return 1, f"FAIL: the table holds C-2A address(es) {bad}: {got}"
    if got != WANT:
        return 1, f"FAIL: the table is {got}, not exactly {WANT}"
    return 0, f"ok: the table is exactly {got}"


def check_elf(path):
    data = open(path, "rb").read()
    if b"E-4B" not in data:
        return 2, f"RIG: {os.path.basename(path)}: the control string \"E-4B\" is absent, so this scan cannot be trusted"
    missing = [h for h in WANT if not re.search(rb"(?<![0-9A-Za-z])" + h.encode() + rb"\x00", data)]
    present = [h for h in C2A if h.encode() in data]
    name = os.path.basename(path)
    if missing or present:
        return 1, f"FAIL: {name}: E-4B strings missing {missing or 'none'}; C-2A strings present {present or 'none'}"
    return 0, f"ok: {name}: all four E-4B strings present, no C-2A string"


def selftest():
    good = 'x;\nconst char* const kE4bHexes[] = { "adfeb3", "adfeb4", "adfeb5", "adfeb6" };\ny;\n'
    cases = [
        ("the four, in order", good, 0),
        ("one C-2A put back", good.replace('"adfeb6"', '"ae0454"'), 1),
        ("upper-case C-2A", good.replace('"adfeb3"', '"AE0451"'), 1),
        ("a fifth entry", good.replace('"adfeb6"', '"adfeb6", "adfeb7"'), 1),
        ("one missing", good.replace(', "adfeb6"', ""), 1),
        ("reordered", good.replace('"adfeb3", "adfeb4"', '"adfeb4", "adfeb3"'), 1),
        ("no table (rig)", "nothing here", 2),
        ("two tables (rig)", good + good, 2),
    ]
    ok = True
    for name, text, want in cases:
        code, _ = check_text(text)
        print(f"  selftest {'ok  ' if code == want else 'FAIL'} {name}: exit {code} (want {want})")
        ok &= code == want
    # The ruled sabotage, on the REAL file's text, in memory: one C-2A address put back. The file is never written.
    before = hashlib.sha256(open(SRC, "rb").read()).hexdigest()
    real = open(SRC, encoding="utf-8").read()
    code0, msg0 = check_text(real)
    planted = real.replace('"adfeb6"', '"ae0454"', 1)
    applied = planted != real
    code1, _ = check_text(planted)
    after = hashlib.sha256(open(SRC, "rb").read()).hexdigest()
    print(f"  sabotage (real file, adfeb6 -> ae0454, in memory): applied={applied}, exit {code1} (want 1); "
          f"real file unchanged={before == after} ({after[:12]})")
    ok &= applied and code1 == 1 and before == after
    # The real file itself must pass for the selftest to mean anything about it.
    print(f"  the real file: {msg0}")
    ok &= code0 == 0
    print("SELFTEST PASSED" if ok else "SELFTEST FAILED")
    return 0 if ok else 1


def main(argv):
    if argv[:1] == ["--selftest"]:
        return selftest()
    if argv[:1] == ["--elf"]:
        if len(argv) < 2:
            print("RIG: --elf needs at least one file"); return 2
        worst = 0
        for p in argv[1:]:
            code, msg = check_elf(p)
            print(msg); worst = max(worst, code)
        return worst
    path = argv[1] if argv[:1] == ["--file"] and len(argv) > 1 else SRC
    code, msg = check_text(open(path, encoding="utf-8").read())
    print(msg)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
