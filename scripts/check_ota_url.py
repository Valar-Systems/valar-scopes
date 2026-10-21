#!/usr/bin/env python3
"""No skus.yml image points OTA anywhere but releases/latest (docs/ota-url-check.md).

    python3 scripts/check_ota_url.py [--require-ota] <firmware.bin>
    python3 scripts/check_ota_url.py --selftest
    python3 scripts/check_ota_url.py --check-wiring

WHY THE IMAGE, NOT THE CONFIG. -DOTA_RELEASE_BASE pins a build's OTA to a tag
(src/OtaUpdater.cpp). It is meant for bench envs, and a shipping env that gained
it would publish an image whose devices follow whatever is clobbered onto that
tag -- passing every other gate, because nothing else reads the URL. A config
check would miss a define set in a variant header, a src_build_flags line or an
`extends`; the image is what ships, so the image is what is read.

THE DEFAULT IS READ FROM THE OTHER SIDE. The base this accepts is parsed out of
OtaUpdater.cpp's own `#define OTA_RELEASE_BASE "..."`, never restated here, so a
change to the fleet's URL moves the check with it.

Verdicts and exit codes:
  PASS    0  both default OTA URLs present, nothing pinned or foreign
  NO-OTA  0  no OTA URL of any kind (the image has no OTA client); FAIL with --require-ota
  FAIL    1  a /releases/download/ URL (a pinned tag); a version.txt or firmware- URL not
             built from the default base (an override to any host); or one default URL
             without the other
  BLIND   2  no "[build] env=" anchor (cannot see into the file), or the default base
             cannot be read from OtaUpdater.cpp
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OTA_SRC = os.path.join(ROOT, "src", "OtaUpdater.cpp")
WORKFLOW = os.path.join(ROOT, ".github", "workflows", "firmware.yml")
ANCHOR = b"[build] env="
STEP_NAME = "OTA URL is the fleet's"
URL_RE = re.compile(rb"https?://[\x21-\x7e]+")


def default_base(path=OTA_SRC):
    """The fleet's base, from OtaUpdater.cpp's #define. None unless exactly one."""
    try:
        src = open(path, encoding="utf-8").read()
    except OSError:
        return None
    found = re.findall(r'^\s*#define\s+OTA_RELEASE_BASE\s+"([^"]+)"\s*$', src, re.M)
    return found[0] if len(found) == 1 else None


def classify(data, base, require_ota=False):
    """(verdict, reasons) for one image's bytes."""
    if ANCHOR not in data:
        return "BLIND", ['no "[build] env=" anchor: this file cannot be read as an image']
    urls = sorted(set(u.decode("ascii") for u in URL_RE.findall(data)))
    ver, fw = base + "/version.txt", base + "/firmware-"
    reasons = []
    # Each rule has its own selftest case that only it catches, so dropping any one
    # of them is visible (docs/ota-url-check.md, OU1).
    reasons += ["pinned tag URL: " + u for u in urls if "/releases/download/" in u]
    reasons += ["version.txt not on the default base: " + u
                for u in urls if u.endswith("/version.txt") and u != ver]
    reasons += ["firmware URL not on the default base: " + u
                for u in urls if "/firmware-" in u and (u.endswith("-") or u.endswith(".bin"))
                and not u.startswith(fw)]
    has_ver, has_fw = ver in urls, any(u.startswith(fw) for u in urls)
    if has_ver != has_fw:
        reasons.append("only one of the two default OTA URLs is present (%s)" % (ver if has_ver else fw))
    if reasons:
        return "FAIL", reasons
    if has_ver and has_fw:
        return "PASS", ["OTA reads %s/{version.txt,firmware-*}" % base]
    if require_ota:
        return "FAIL", ["no OTA URL at all, but this image ships (--require-ota)"]
    return "NO-OTA", ["no OTA URL of any kind: this image has no OTA client"]


def scan(path, require_ota):
    base = default_base()
    if base is None:
        print("BLIND: cannot read exactly one #define OTA_RELEASE_BASE from %s" % OTA_SRC)
        return 2
    try:
        data = open(path, "rb").read()
    except OSError as e:
        print("BLIND: cannot read %s (%s)" % (path, e))
        return 2
    verdict, reasons = classify(data, base, require_ota)
    print("%s: %s" % (verdict, path))
    for r in reasons:
        print("  " + r)
    return {"PASS": 0, "NO-OTA": 0, "FAIL": 1, "BLIND": 2}[verdict]


def check_wiring(text):
    """Problems with where the step sits in the build job ([] = good). None = cannot find the job."""
    m = re.search(r"^  build:\n(.*?)(?=^  [a-z][a-z0-9_-]*:\n|\Z)", text, re.M | re.S)
    if not m:
        return None
    steps = re.split(r"^      - ", m.group(1), flags=re.M)[1:]
    names = [re.match(r"name:\s*(.*)", s).group(1).strip() if re.match(r"name:", s) else "" for s in steps]
    idx = [i for i, n in enumerate(names) if n.startswith(STEP_NAME)]
    problems = []
    if len(idx) != 1:
        return ["expected exactly one '%s' step in the build job, found %d" % (STEP_NAME, len(idx))]
    i = idx[0]
    if re.search(r"^        if:", steps[i], re.M):
        problems.append("the step has an `if:`; it must run for every skus.yml row")
    if "scripts/check_ota_url.py" not in steps[i]:
        problems.append("the step does not run scripts/check_ota_url.py")
    for later in ("Attach to release", "Publication receipt"):
        j = [k for k, n in enumerate(names) if n.startswith(later)]
        if len(j) != 1:
            problems.append("expected exactly one '%s' step, found %d" % (later, len(j)))
        elif j[0] < i:
            problems.append("the step runs after '%s'; a FAIL must stop the upload and the receipt" % later)
    return problems


def selftest():
    base = "https://github.com/Valar-Systems/valar-scopes/releases/latest/download"
    pin = "https://github.com/Valar-Systems/valar-scopes/releases/download/ota-preflight-v6"
    other = "https://example.invalid/ota"

    def img(*strings, anchor=True):
        return (ANCHOR + b"blipscope-s3-128\0" if anchor else b"\0") + b"".join(s.encode() + b"\0" for s in strings)

    good = (base + "/version.txt", base + "/firmware-")
    cases = [
        ("the default base", img(*good), False, "PASS"),
        ("the default base, shipping", img(*good), True, "PASS"),
        ("pinned to a bench tag", img(pin + "/version.txt", pin + "/firmware-"), False, "FAIL"),
        # only the /releases/download/ rule sees this one: the two default URLs are intact
        ("the defaults plus a bare pinned base", img(*good, pin), False, "FAIL"),
        ("another host", img(other + "/version.txt", other + "/firmware-"), False, "FAIL"),
        ("version.txt without the firmware URL", img(base + "/version.txt"), False, "FAIL"),
        ("no OTA client", img("https://scopes.valarsystems.com/api"), False, "NO-OTA"),
        ("no OTA client, shipping", img(), True, "FAIL"),
        ("no anchor", img(*good, anchor=False), False, "BLIND"),
    ]
    ok = True
    for name, data, req, want in cases:
        got, _ = classify(data, base, req)
        good_case = got == want
        ok &= good_case
        print("  %s %-40s want %-6s got %s" % ("ok  " if good_case else "FAIL", name, want, got))

    def step(name, extra=""):
        return "      - name: %s\n%s        run: x\n" % (name, extra)

    def wf(*steps):
        return "jobs:\n  build:\n    steps:\n" + "".join(steps) + "  version:\n    steps:\n"

    ours = step(STEP_NAME + " (${{ matrix.env }})").replace("run: x", "run: python3 scripts/check_ota_url.py x")
    attach, receipt = step("Attach to release"), step("Publication receipt")
    wiring = [
        ("before the upload and the receipt", wf(step("Build"), ours, attach, receipt), True),
        ("moved after the upload", wf(step("Build"), attach, ours, receipt), False),
        ("gated to slug-ful rows", wf(step("Build"), ours.replace("        run:", "        if: matrix.slug\n        run:"), attach, receipt), False),
        ("missing", wf(step("Build"), attach, receipt), False),
    ]
    for name, text, want_ok in wiring:
        problems = check_wiring(text)
        got_ok = problems == []
        good_case = got_ok == want_ok
        ok &= good_case
        print("  %s wiring: %-34s want %-5s got %s" % ("ok  " if good_case else "FAIL", name,
                                                      "ok" if want_ok else "refused", "ok" if got_ok else "refused"))
    real = default_base()
    print("  %s the default base reads from OtaUpdater.cpp: %s" % ("ok  " if real == base else "FAIL", real))
    ok &= real == base
    print("selftest " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv):
    if argv[:1] == ["--selftest"]:
        return selftest()
    if argv[:1] == ["--check-wiring"]:
        try:
            text = open(WORKFLOW, encoding="utf-8").read()
        except OSError as e:
            print("BLIND: cannot read %s (%s)" % (WORKFLOW, e))
            return 2
        problems = check_wiring(text)
        if problems is None:
            print("BLIND: no build job found in %s" % WORKFLOW)
            return 2
        for p in problems:
            print("FAIL: " + p)
        if not problems:
            print("ok: '%s' runs on every row, before the upload and the receipt" % STEP_NAME)
        return 1 if problems else 0
    require = "--require-ota" in argv
    paths = [a for a in argv if not a.startswith("--")]
    if len(paths) != 1:
        print(__doc__)
        return 2
    return scan(paths[0], require)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
