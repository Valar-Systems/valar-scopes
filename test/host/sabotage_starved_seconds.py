"""The starved-seconds counter's sabotages (docs/starved-seconds-counter.md, SC1/SC2/SC4/SC5): each plants one
break into a COPY of the headers (or of support.html) and the named check must go red.

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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass and the colour proof (docs/weatherscope-stage3.md A12.50)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
METRICS = os.path.join(ROOT, "proxy", "src", "metrics.ts")
README = os.path.join(ROOT, "README.md")
SUPPORT = os.path.join(ROOT, "proxy", "pages", "support.html")

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
    ("S-SC5 the sentence removed from support.html", "@support.html",
     [("running. From\n       v19 the firmware adds one health number: how many seconds in that hour the device\n"
       "       couldn't open a secure connection, which describes the device and not you.\n", "running.\n")],
     "test_usage_report.cpp", "SC5: support.html"),
]


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def main():
    inc = os.path.join(ROOT, "include")
    real = [os.path.join(inc, h) for h in ("UsageReport.h", "StarvationPolicy.h")] + [SUPPORT]
    before = {h: sha(h) for h in real}
    ok = True
    with tempfile.TemporaryDirectory() as td:
        for i, (name, header, edits, test, must) in enumerate(SABOTAGES):
            tree = os.path.join(td, f"s{i}")
            shutil.copytree(inc, os.path.join(tree, "include"))
            os.makedirs(os.path.join(tree, "test", "host"))
            shutil.copy(os.path.join(ROOT, "test", "host", test), os.path.join(tree, "test", "host", test))
            support = SUPPORT
            if header.startswith("@"):
                support = hp = os.path.join(tree, header[1:])
                shutil.copy(SUPPORT, hp)
            else:
                hp = os.path.join(tree, "include", header)
            src = open(hp, encoding="utf-8").read().replace("\r\n", "\n")
            out = src
            for a, b in edits:
                sc.prove_colour(name, a, b)   # Q50: refuses a colour the panel cannot see
                assert out.count(a) == 1, f"{name}: anchor found {out.count(a)} times:\n{a}"
                out = out.replace(a, b)
            assert out != src, f"{name}: the copy did not change"
            open(hp, "w", encoding="utf-8", newline="\n").write(out)
            exe = os.path.join(tree, "t.exe")
            b = subprocess.run([CXX, *FLAGS, os.path.join(tree, "test", "host", test), "-o", exe], capture_output=True, text=True)
            if b.returncode != 0:
                print(f"{name}: did not compile (rig)\n{b.stderr}")
                return 2
            args = [exe, METRICS, README, support] if test == "test_usage_report.cpp" else [exe]
            r = subprocess.run(args, capture_output=True, text=True)
            fails = [l.strip() for l in r.stdout.splitlines() if "FAIL" in l]
            good = r.returncode == 1 and any(must in l for l in fails)
            print(f"{name}: exit {r.returncode}; {len(fails)} FAIL line(s); '{must}' red: {any(must in l for l in fails)} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = {h: sha(h) for h in before}
    print(f"real headers and support.html unchanged: {before == after}")
    return 0 if ok and before == after else 1


def anchor_entries():
    """Q51: every plant's anchor in the header (or support.html) it plants, for --anchors (no plant runs)."""
    out = []
    for name, header, edits, _test, _must in SABOTAGES:
        p = SUPPORT if header.startswith("@") else os.path.join(ROOT, "include", header)
        out += [(name, os.path.relpath(p, ROOT), sc.read(p), a) for a, _ in edits]
    return out

# --control (Q57, docs/weatherscope-stage3.md A12.61): this driver's unplanted run alone, each suite against its own
# count, nothing planted. run.sh runs it for every driver.
CONTROL_TABLE = {"test_starvation_policy": ("line", "test_starvation_policy: all checks passed"),
                 "test_usage_report": ("checks", 44)}


def control():
    inc = os.path.join(ROOT, "include")
    with tempfile.TemporaryDirectory() as td:
        for test in ("test_starvation_policy.cpp", "test_usage_report.cpp"):
            tree = os.path.join(td, os.path.splitext(test)[0])
            shutil.copytree(inc, os.path.join(tree, "include"))
            os.makedirs(os.path.join(tree, "test", "host"))
            shutil.copy(os.path.join(ROOT, "test", "host", test), os.path.join(tree, "test", "host", test))
            exe = os.path.join(tree, "t.exe")
            subprocess.run([CXX, *FLAGS, os.path.join(tree, "test", "host", test), "-o", exe], capture_output=True, text=True)
            subprocess.run([exe, METRICS, README, SUPPORT] if test == "test_usage_report.cpp" else [exe],
                           capture_output=True, text=True)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
