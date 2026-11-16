#!/usr/bin/env python3
"""Red first (docs/token-file-python.md section 3): each of tools/token_file.py's controls must FAIL when the rule it
guards is broken. Every plant goes into a COPY of the helper, is asserted to apply (its needle occurs exactly once, and
the text changes), and the copy's --selftest must fail EXACTLY the predicted set: a whole-set comparison. T4 is POSIX
only: on Windows its plant is reported as not exercised (run this under Linux, as CI does, to see it red).

  python3 tools/token_file_sabotage.py        exit 0 every plant read as predicted, 1 otherwise
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "token_file.py")

PLANTS = [  # (name, needle, replacement, the controls predicted to FAIL)
    ("S0 none (anchor control)", None, None, set()),
    ("S1 an unset variable yields a value",
     '    if not path:\n        raise TokenFileError(f"{TOKEN_FILE_ENV} is not set',
     '    if not path:\n        return ("analytics-read=X\\n", "<plant>")\n        raise TokenFileError(f"{TOKEN_FILE_ENV} is not set', {"T1"}),
    ("S2 a missing file is opened anyway",
     "    if not os.path.exists(path):\n        raise TokenFileError(f\"{TOKEN_FILE_ENV} points at {path}, which does not exist.",
     "    if False:\n        raise TokenFileError(f\"{TOKEN_FILE_ENV} points at {path}, which does not exist.", {"T2"}),
    ("S3 a git working tree is not detected",
     '        if os.path.exists(os.path.join(d, ".git")):\n            return True',
     '        if os.path.exists(os.path.join(d, ".git")):\n            return False', {"T3"}),
    ("S4 permissions are not checked (POSIX)", "        if mode & 0o077:", "        if False:", {"T4"}),
    ("S5 a bare line is skipped",
     '            raise TokenFileError(f"{where}: line {i + 1} is not key=value (content not shown). {FORMAT_HELP}")',
     "            continue", {"T5"}),
    ("S6 a duplicate key is accepted", "        if found is not None:", "        if False:", {"T6"}),
    ("S7 a missing key returns a value", '    if found is None:\n        raise', '    if found is None:\n        return "MISSING"\n        raise', {"T7"}),
    ("S8 an empty key is accepted", "    if not found:\n        raise", "    if False:\n        raise", {"T8"}),
    ("S9 the value is altered", "    return found\n", "    return found + \" \"\n", {"T9"}),
    ("S10 a refusal shows the line", "is not key=value (content not shown)", "is not key=value ({line})", {"T10"}),
]


def failed(out):
    return {m.group(1) for m in re.finditer(r"^FAIL (T\d+):", out, re.M)}


def main():
    src = open(SRC, encoding="utf-8").read()
    off = 0
    with tempfile.TemporaryDirectory() as d:
        for name, needle, repl, want in PLANTS:
            text = src
            if needle is not None:
                n = src.count(needle)
                if n != 1:
                    print(f"RIG {name}: the needle occurs {n} times, not once"); off += 1; continue
                text = src.replace(needle, repl)
                if text == src:
                    print(f"RIG {name}: the plant changed nothing"); off += 1; continue
            if "T4" in want and os.name == "nt":
                print(f"{'not run':12s} {name}: T4 is POSIX only, and this is Windows; CI runs it on Linux")
                continue
            # Nested two levels down: the helper takes its grandparent as "the repo", and the selftest's own temporary
            # files (in the system temp folder) must not fall inside that, or every refusal passes for the wrong reason.
            os.makedirs(os.path.join(d, "repo", "tools"), exist_ok=True)
            copy = os.path.join(d, "repo", "tools", "token_file.py")
            with open(copy, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)
            r = subprocess.run([sys.executable, "-I", copy, "--selftest"], capture_output=True, text=True)
            got = failed(r.stdout)
            ok = got == want and r.returncode == (1 if want else 0)
            print(f"{'as predicted' if ok else 'OFF':12s} {name}: failed {sorted(got) or 'none'} (predicted {sorted(want) or 'none'}), exit {r.returncode}")
            if not ok:
                off += 1
                print(r.stdout[-600:], r.stderr[-400:])
    print("SABOTAGE " + ("ALL AS PREDICTED" if not off else f"OFF: {off}"))
    return 0 if not off else 1


if __name__ == "__main__":
    sys.exit(main())
