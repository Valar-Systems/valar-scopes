"""The frame bench's step-C exemption never reaches main.

C1's frame bench (docs/frame-time-bench.md, "C1's bench") builds weather-on images with the map OFF by construction, so
the step-C refusal in src/AircraftManager.h ("step B draws the map without step C") cannot apply to them. Its exemption,
BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT, lives on the bench branch bench/frame-bench-c1 ONLY: a FRAME_BENCH region defines
it and the refusal's #if tests it. A copy of that change in a PR to main would silently disarm the refusal for any
image that defines FRAME_BENCH, so this refuses a tree where either appears:
  - the macro anywhere in src/ or include/;
  - any FRAME_BENCH token on the refusal's own #if line (the line before its #error).
    python3 scripts/check_no_bench_exemption.py [tree]     exit 0 clean; 1 the exemption is present; 2 the refusal is missing
    python3 scripts/check_no_bench_exemption.py --selftest  each plant reads as designed"""
import os
import re
import shutil
import sys
import tempfile

MACRO = "BLIPSCOPE_MAP_REFUSAL_BENCH_EXEMPT"
ERROR = '#error "step B draws the map without step C: the sweep erases it. Land step C before WX_LAYER_SHIPS ships."'


def check(tree):
    hits = []
    for top in ("src", "include"):
        for d, _, names in os.walk(os.path.join(tree, top)):
            for n in names:
                if not n.endswith((".h", ".hpp", ".cpp", ".c")):
                    continue
                p = os.path.join(d, n)
                for i, l in enumerate(open(p, "rb").read().decode("utf-8", errors="replace").split("\n")):
                    if MACRO in l:
                        hits.append(f"{os.path.relpath(p, tree)}:{i + 1}")
    am = open(os.path.join(tree, "src", "AircraftManager.h"), "rb").read().decode("utf-8").replace("\r\n", "\n").split("\n")
    err = [i for i, l in enumerate(am) if l.strip() == ERROR]
    if len(err) != 1:
        print(f"check_no_bench_exemption: the step-C refusal's #error appears {len(err)} times; cannot check")
        return 2
    if re.search(r"FRAME_BENCH", am[err[0] - 1]):
        hits.append(f"src/AircraftManager.h:{err[0]}: the refusal's #if names FRAME_BENCH")
    if hits:
        print(f"check_no_bench_exemption: REFUSED, the frame bench's step-C exemption is in this tree: {hits[:4]}")
        return 1
    print("check_no_bench_exemption: ok, the step-C refusal carries no bench exemption")
    return 0


def selftest(tree):
    src = open(os.path.join(tree, "src", "AircraftManager.h"), "rb").read().decode("utf-8").replace("\r\n", "\n")
    lines = src.split("\n")
    e = next(i for i, l in enumerate(lines) if l.strip() == ERROR)
    region = ("// THE FRAME BENCH BEGIN\n#ifdef FRAME_BENCH\n#define " + MACRO + " 1\n#endif\n// THE FRAME BENCH END\n")
    plants = {
        "clean": (src, 0),
        "the macro defined in a FRAME_BENCH region": (region + src, 1),
        "the refusal's #if tests the macro": ("\n".join(lines[:e - 1] + [lines[e - 1] + " && !defined(" + MACRO + ")"]
                                                        + lines[e:]), 1),
        "the refusal's #if names FRAME_BENCH": ("\n".join(lines[:e - 1] + [lines[e - 1] + " && !defined(FRAME_BENCH)"]
                                                          + lines[e:]), 1),
        "the refusal removed": ("\n".join(lines[:e] + lines[e + 1:]), 2),
    }
    rc = 0
    for name, (text, want) in plants.items():
        d = tempfile.mkdtemp(prefix="nbe_")
        try:
            os.makedirs(os.path.join(d, "src"))
            open(os.path.join(d, "src", "AircraftManager.h"), "w", encoding="utf-8", newline="\n").write(text)
            got = check(d)
        finally:
            shutil.rmtree(d, ignore_errors=True)
        ok = got == want
        rc |= not ok
        print(f"selftest {'ok' if ok else 'FAIL'}: {name} -> exit {got} (want {want})")
    print("selftest PASSED" if rc == 0 else "selftest FAILED")
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    if sys.argv[1:2] == ["--selftest"]:
        sys.exit(selftest(sys.argv[2] if len(sys.argv) > 2 else "."))
    sys.exit(check(sys.argv[1] if len(sys.argv) > 1 else "."))
