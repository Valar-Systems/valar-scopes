"""The starved-seconds counter's sabotages (docs/starved-seconds-counter.md, SC1/SC2/SC4): each plants one break
into a COPY of the headers and the named check must go red.

    python -I test/host/sabotage_starved_seconds.py

Both tests include their headers by relative path (../../include/...), so -I cannot redirect them: the
script mirrors include/ and the test into a temporary tree and plants there. The real files are never
edited; each plant asserts its anchor occurs exactly once and that the copy changed, and the real headers'
sha256 are compared before and after. Exit 0 only when every sabotage turns its check red.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
METRICS = os.path.join(ROOT, "proxy", "src", "metrics.ts")

SABOTAGES = [
    # (name, header, [(anchor, replacement)], test, the FAIL text that must appear)
    ("S-SC1 the ninth field as text", "UsageReport.h",
     [('"%u,%u,%u,%u,%u,%u,%u,%u,%u",', '"%u,%u,%u,%u,%u,%u,%u,%u,s=%u",')],
     "test_usage_report.cpp", "the payload is digits and commas ONLY"),
    ("S-SC2a ticks counted, not seconds", "StarvationPolicy.h",
     [("        return ms / 1000u;\n", "        (void)ms;\n        return 1u;   // SABOTAGE S-SC2a: one per tick\n")],
     "test_starvation_policy.cpp", "SC2:"),
    ("S-SC2b only the edge counted", "StarvationPolicy.h",
     [("        if (!previousTickStarved) return 0;\n",
       "        if (!previousTickStarved) { remMs = 0; return 0; }\n"
       "        if (remMs == 1u) return 0;   // SABOTAGE S-SC2b: only the first starved interval counts\n"
       "        remMs = 1u; (void)previousTickMs; (void)nowMs; return 30u;\n")],
     "test_starvation_policy.cpp", "SC2:"),
    ("S-SC4 FIELD_COUNT 10", "UsageReport.h",
     [("constexpr size_t FIELD_COUNT = 9;", "constexpr size_t FIELD_COUNT = 10;")],
     "test_usage_report.cpp", "SC4:"),
]


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def main():
    inc = os.path.join(ROOT, "include")
    before = {h: sha(os.path.join(inc, h)) for h in ("UsageReport.h", "StarvationPolicy.h")}
    ok = True
    with tempfile.TemporaryDirectory() as td:
        for i, (name, header, edits, test, must) in enumerate(SABOTAGES):
            tree = os.path.join(td, f"s{i}")
            shutil.copytree(inc, os.path.join(tree, "include"))
            os.makedirs(os.path.join(tree, "test", "host"))
            shutil.copy(os.path.join(ROOT, "test", "host", test), os.path.join(tree, "test", "host", test))
            hp = os.path.join(tree, "include", header)
            src = open(hp, encoding="utf-8").read().replace("\r\n", "\n")
            out = src
            for a, b in edits:
                assert out.count(a) == 1, f"{name}: anchor found {out.count(a)} times:\n{a}"
                out = out.replace(a, b)
            assert out != src, f"{name}: the copy did not change"
            open(hp, "w", encoding="utf-8", newline="\n").write(out)
            exe = os.path.join(tree, "t.exe")
            b = subprocess.run([CXX, *FLAGS, os.path.join(tree, "test", "host", test), "-o", exe], capture_output=True, text=True)
            if b.returncode != 0:
                print(f"{name}: did not compile (rig)\n{b.stderr}")
                return 2
            args = [exe, METRICS] if test == "test_usage_report.cpp" else [exe]
            r = subprocess.run(args, capture_output=True, text=True)
            fails = [l.strip() for l in r.stdout.splitlines() if "FAIL" in l]
            good = r.returncode == 1 and any(must in l for l in fails)
            print(f"{name}: exit {r.returncode}; {len(fails)} FAIL line(s); '{must}' red: {any(must in l for l in fails)} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = {h: sha(os.path.join(inc, h)) for h in before}
    print(f"real headers unchanged: {before == after}")
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
