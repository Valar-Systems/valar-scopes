"""B3 (docs/bssid-guard.md): the PowerShell and Python redaction rule sets give the SAME output on every vector, and
that output is the expected one. Two implementations of one rule drift unless something compares them.

    python3 scripts/check_redaction_vectors.py              exit 0 equal, 1 a difference, 2 rig (no pwsh)
    python3 scripts/check_redaction_vectors.py --selftest   removing the BSSID rule from EITHER side turns it red
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VECTORS = os.path.join(ROOT, "scripts", "redact-vectors.txt")
# Placeholders, built here so no committed file carries a literal MAC after "bssid" (check_bssid.py refuses that).
TOKENS = {"{MAC1}": ":".join(["AA", "BB", "CC", "DD", "EE", "F1"]), "{MAC2}": "-".join(["aa", "bb", "cc", "dd", "ee", "f1"])}


def vectors():
    out = []
    for line in open(VECTORS, encoding="utf-8").read().splitlines():
        if not line or line.startswith("#"):
            continue
        inp, exp = line.split("\t")
        for tok, mac in TOKENS.items():
            inp, exp = inp.replace(tok, mac), exp.replace(tok, mac)
        out.append((inp, exp))
    return out


def run_python(py_file, inputs):
    ns = {}
    exec(compile(open(py_file, encoding="utf-8").read(), py_file, "exec"), ns)
    return [ns["protect_line"](s) for s in inputs]


def run_pwsh(ps_file, inputs):
    pwsh = shutil.which("pwsh") or shutil.which("powershell")
    if not pwsh:
        return None
    tmp = tempfile.mkdtemp(prefix="redact-")
    try:
        src = os.path.join(tmp, "in.txt")
        open(src, "w", encoding="utf-8", newline="\n").write("\n".join(inputs) + "\n")
        cmd = (f". '{ps_file}'; [Console]::OutputEncoding = [Text.Encoding]::UTF8; "
               f"Get-Content -Encoding UTF8 '{src}' | ForEach-Object {{ Protect-Line $_ }}")
        r = subprocess.run([pwsh, "-NoProfile", "-NonInteractive", "-Command", cmd], capture_output=True,
                           text=True, encoding="utf-8", errors="replace")
        if r.returncode != 0:
            print(f"RIG: pwsh failed: {r.stderr[-400:]}"); sys.exit(2)
        return r.stdout.replace("\r\n", "\n").rstrip("\n").split("\n")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def compare(ps_file, py_file):
    vs = vectors()
    inputs = [i for i, _ in vs]
    ps, py = run_pwsh(ps_file, inputs), run_python(py_file, inputs)
    if ps is None:
        print("RIG: no pwsh/powershell on PATH; the PowerShell side cannot be checked"); sys.exit(2)
    bad = []
    for (inp, exp), a, b in zip(vs, ps, py):
        if not (a == exp and b == exp):
            bad.append((inp, exp, a, b))
    if len(ps) != len(vs):
        bad.append(("<count>", str(len(vs)), str(len(ps)), str(len(py))))
    return len(vs), bad


def main(argv):
    ps_file = os.path.join(ROOT, "scripts", "redact-line.ps1")
    py_file = os.path.join(ROOT, "scripts", "redact_line.py")
    if argv[1:2] == ["--selftest"]:
        return selftest(ps_file, py_file)
    n, bad = compare(ps_file, py_file)
    for inp, exp, a, b in bad:
        print(f"FAIL {inp!r}\n     expected {exp!r}\n     pwsh     {a!r}\n     python   {b!r}")
    print(f"redaction vectors: {n} lines, {'both implementations equal and as expected' if not bad else f'{len(bad)} wrong'}")
    return 1 if bad else 0


def selftest(ps_file, py_file):
    n, bad = compare(ps_file, py_file)
    if bad:
        print("RIG: the real files must pass before the selftest means anything"); return 2
    fails = 0
    tmp = tempfile.mkdtemp(prefix="redact-sab-")
    try:
        for side in ("pwsh", "python"):
            src = ps_file if side == "pwsh" else py_file
            text = open(src, encoding="utf-8").read()
            lines = text.split("\n")
            kept = [l for l in lines if not re.search(r"bssid\|bss", l) or l.lstrip().startswith("#")]
            if len(kept) == len(lines):
                print(f"  RIG: the BSSID rule was not found in {side}'s file"); return 2
            dst = os.path.join(tmp, os.path.basename(src))
            open(dst, "w", encoding="utf-8").write("\n".join(kept))
            _, b = compare(dst if side == "pwsh" else ps_file, dst if side == "python" else py_file)
            ok = any("bssid" in inp.lower() or "bss " in inp for inp, *_ in b)
            fails += not ok
            print(f"  {'ok  ' if ok else 'FAIL'} the BSSID rule removed from the {side} side: {len(b)} vector(s) red")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("redaction vectors SELFTEST PASSED" if not fails else f"redaction vectors SELFTEST: {fails} NOT AS PREDICTED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
