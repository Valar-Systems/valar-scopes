"""GetBytes holds no size-cap decision of its own (bodycap_registration.txt T3): src/HttpRequestManager.cpp's GetBytes,
from its signature to its closing brace, compares nothing against `cap` itself, and calls bodycap::Fits and
bodycap::ReadChunked once each (include/BodyCap.h, which test_body_cap.cpp runs). A second, inline comparison would be a
path round the tested one.
CONTROL, every run: the same check on a copy of GetBytes with `if ((size_t)bodyLen > cap) {}` re-added after the Fits
call must FAIL. A check that passes its plant is the instrument failing.
    python -I test/host/check_body_cap.py [repo root]      exit 0 as stated, 1 a finding, 2 the rig
Prints counts and row names only."""
import os
import re
import sys

COMPARE = re.compile(r"(?:[<>]=?|[!=]=)\s*cap\b|\bcap\s*(?:[<>]=?|[!=]=)")
SIG = "HttpResult HttpRequestManager::GetBytes("


def get_bytes_body(text):
    start = text.find(SIG)
    if start < 0:
        return None
    end = text.find("\n}\n", start)
    return None if end < 0 else text[start:end + 3]


def judge(body):
    """(comparisons against cap, Fits calls, ReadChunked calls)."""
    return len(COMPARE.findall(body)), body.count("bodycap::Fits("), body.count("bodycap::ReadChunked(")


def main(argv):
    root = argv[0] if argv else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = os.path.join(root, "src", "HttpRequestManager.cpp")
    text = open(path, encoding="utf-8").read().replace("\r\n", "\n")
    body = get_bytes_body(text)
    if body is None:
        print("RIG: GetBytes not found in src/HttpRequestManager.cpp")
        return 2
    anchor = "if (!bodycap::Fits((size_t)bodyLen, cap)) {"
    if body.count(anchor) != 1:
        print("RIG: the Fits call's line is not in GetBytes exactly once; the control cannot be planted")
        return 2
    planted = body.replace(anchor, anchor + " if ((size_t)bodyLen > cap) {}", 1)
    real, plant = judge(body), judge(planted)
    print(f"BC-SRC GetBytes ({body.count(chr(10))} lines): comparisons against cap {real[0]} (want 0), "
          f"bodycap::Fits calls {real[1]} (want 1), bodycap::ReadChunked calls {real[2]} (want 1)")
    print(f"BC-SRC control (an inline `> cap` re-added): comparisons against cap {plant[0]} (want 1)")
    if plant[0] != 1:
        print("FAIL [BC-SRC-CTL] the control plant was not seen: the check is blind")
        return 2
    ok = real == (0, 1, 1)
    print(("ok   [BC-SRC]" if ok else "FAIL [BC-SRC]") + " GetBytes holds no cap decision of its own")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
