"""TX2 and S-TXRECON (docs/test-identity.md, item 4), then R1 and its sabotages (docs/ota-result-reader.md): reconcile-fleet.py leaves the production checks' TEST identity
out of 'KV ONLY'. The script is loaded as a module and run with its KV and CSV reads replaced by a fixture: two units
in the manufacturing record and in KV, plus the test identity in KV only (it has a fw: row, as every smoke run makes,
and no CSV line). EXAMPLE ids, built at run time, so no 16-hex literal sits in the repo; nothing touches KV.

S-TXRECON runs the same fixture through a copy of the source with the exclusion line removed (in memory; the file is
never written), and must report exactly one 'KV ONLY: unaccounted-for unit' and exit 1.

    python scripts/reconcile-fleet.selftest.py
"""
import contextlib
import io
import os
import sys
import tempfile
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = (HERE / "reconcile-fleet.py").read_text(encoding="utf-8")
A, B, T = "a1" * 8, "b2" * 8, "c3" * 8  # two units, and the test identity (EXAMPLE)
EXCLUSION = "    kv_ids = [d for d in kv_ids if d not in test_ids]  # the test identity is not a unit\n"


def run(source: str):
    mod = types.ModuleType("reconcile_fleet_under_test")
    mod.__file__ = str(HERE / "reconcile-fleet.py")
    exec(compile(source, mod.__file__, "exec"), mod.__dict__)
    mod.kv_list = lambda prefix, env, errfile: [A, B, T]  # the fixture's KV listing
    mod.read_csv = lambda path: {A: {}, B: {}}  # the fixture's manufacturing record
    mod.report_instruments = lambda *a, **k: []
    out, code = io.StringIO(), None
    argv = sys.argv
    sys.argv = ["reconcile-fleet.py", "--no-detail", "--log", "fixture.csv"]
    try:
        with contextlib.redirect_stdout(out):
            mod.main()
    except SystemExit as e:
        code = e.code
    finally:
        sys.argv = argv
    return out.getvalue(), code


with tempfile.TemporaryDirectory() as d:
    tokens = Path(d) / "tokens"
    tokens.write_text("# EXAMPLE token file\nkv-read=EXAMPLE-not-a-token\ntest-device=%s\n" % T, encoding="utf-8")
    os.environ["BLIPSCOPE_TOKEN_FILE"] = str(tokens)
    off = 0

    text, code = run(SRC)
    ok = code == 0 and "kv-only=0" in text and T not in text
    off += not ok
    print("TX2: %s -- exit %s; %s; the test id printed: %s"
          % ("as predicted" if ok else "OFF-PREDICTION", code,
             next((l.strip() for l in text.splitlines() if l.startswith("SUMMARY")), "(no summary)"), T in text))

    if SRC.count(EXCLUSION) != 1:
        off += 1
        print("S-TXRECON: BLIND -- the exclusion line was not found exactly once")
    else:
        text, code = run(SRC.replace(EXCLUSION, "    pass  # SABOTAGE S-TXRECON: the exclusion removed\n"))
        ok = code == 1 and "kv-only=1" in text
        off += not ok
        print("S-TXRECON: %s -- exit %s; %s"
              % ("as predicted" if ok else "OFF-PREDICTION", code,
                 next((l.strip() for l in text.splitlines() if l.startswith("SUMMARY")), "(no summary)")))


# --- R1 and its sabotages: A3.1's reader (docs/ota-result-reader.md) -------------------------------------------------
# The block's functions are run on a fixture: no KV, no Analytics Engine, no network. EXAMPLE ids, built at run time.
BEHIND = {n: ("b%d" % n) * 8 for n in range(1, 7)}
CUR1, CUR2, AHEAD, NOFW = "e1" * 8, "e2" * 8, "e3" * 8, "e4" * 8
ROWS = {BEHIND[1]: {"fw": 18}, BEHIND[2]: {"fw": 18}, BEHIND[3]: {"fw": 17}, BEHIND[4]: {"fw": 18}, BEHIND[5]: {"fw": 16},
        BEHIND[6]: {"fw": 18}, CUR1: {"fw": 19}, CUR2: {"fw": 19}, AHEAD: {"fw": 20}, NOFW: {}}
POINTS = [(BEHIND[1], "ok", "2026-11-01 10:00:00"), (BEHIND[2], "refused-foreign", "2026-11-01 10:00:00"),
          (BEHIND[3], "fail--106", "2026-11-01 10:00:00"), (BEHIND[5], "weird-token", "2026-11-01 10:00:00"),
          (BEHIND[6], "refused-no-record", "2026-10-01 10:00:00"), (BEHIND[6], "ok", "2026-11-02 10:00:00"),
          (CUR1, "refused-bad-record", "2026-11-01 10:00:00")]
WANT = {"ok": 2, "refused-foreign": 1, "fail--106": 1, "unrecognised": 1, "no ota point": 1}


def load(source: str):
    mod = types.ModuleType("reconcile_fleet_r1")
    mod.__file__ = str(HERE / "reconcile-fleet.py")
    exec(compile(source, mod.__file__, "exec"), mod.__dict__)
    return mod


def r1(mod) -> dict:
    """Every R1 row, graded. True = as predicted."""
    import re as _re
    res = {}
    b = mod.ota_breakdown(19, ROWS, POINTS)
    lines, warn = mod.ota_lines(19, ROWS, POINTS)
    text = "\n".join(lines)
    res["R1a"] = (b["behind"] == 6 and b["total"] == 10 and b["buckets"] == WANT and "sum 6 = behind 6" in text
                  and not warn)
    res["R1b"] = "refused-foreign 1" in text
    only6 = mod.ota_breakdown(19, {BEHIND[6]: ROWS[BEHIND[6]]}, [x for x in POINTS if x[0] == BEHIND[6]])
    res["R1c"] = only6["buckets"] == {"ok": 1}
    d_ok = True
    for v in ("", "v19", "0"):
        lv, wv = mod.ota_lines(mod.parse_version(v), ROWS, POINTS)
        d_ok &= bool(wv) and not any(_re.search(r"published \d+: behind", x) for x in lv)
    res["R1d"] = d_ok
    le, we = mod.ota_lines(19, ROWS, None, "EXAMPLE reason")
    te = "\n".join(le)
    res["R1e"] = "behind 6" in te and "results unread" in te and bool(we)
    every = text + "\n" + te
    res["R1f"] = not any(d in every for d in ROWS) and "behind 6" in text   # CONTROL: the block is there
    res["R1g"] = "refused-bad-record" not in b["buckets"] and "refused-bad-record" not in text
    import urllib.error as _ue

    def refuse(*a, **k):
        raise _ue.HTTPError("https://example.invalid", 422, "EXAMPLE", {}, io.BytesIO(b"EXAMPLE: unable to find type"))
    real = mod.urllib.request.urlopen
    mod.urllib.request.urlopen = refuse
    try:
        pts, why = mod.read_ota_points("production", "EXAMPLE-not-a-token")
    finally:
        mod.urllib.request.urlopen = real
    res["R1h"] = pts is None and "422" in why and "EXAMPLE: unable to find type" in why
    return res


R1_PLANTS = [
    ("S-R1drop  tokens of no known shape dropped", ["R1a"],
     "                tok = UNRECOGNISED\n", "                continue  # SABOTAGE S-R1drop\n", "SUM MISMATCH"),
    ("S-R1first the oldest point wins", ["R1a", "R1c"],
     "        if dev not in newest or t > newest[dev][1]:\n", "        if dev not in newest or t < newest[dev][1]:  # SABOTAGE\n", None),
    ("S-R1zero  an unreadable version.txt reads as 0", ["R1d"],
     '    return int(s) if re.fullmatch(r"[1-9]\\d{0,5}", s) else None\n',
     '    return int(s) if re.fullmatch(r"[1-9]\\d{0,5}", s) else 0  # SABOTAGE\n', None),
    ("S-R1body  an error's body is thrown away", ["R1h"],
     '        return None, "Analytics Engine answered %d: %s" % (e.code, e.read().decode("utf-8", "replace").strip()[:300])\n',
     '        return None, "Analytics Engine answered %d" % e.code  # SABOTAGE S-R1body\n', None),
    ("S-R1id    the block prints the behind devices' ids", ["R1f"],
     '    out.append("    covers radar devices on the cloud feed only (A3.2)',
     '    out.append("    " + " ".join(sorted(rows)))  # SABOTAGE S-R1id\n    out.append("    covers radar devices on the cloud feed only (A3.2)', None),
]

graded = r1(load(SRC))
red = sorted(k for k, v in graded.items() if not v)
ok = not red
off += not ok
print("R1 (the real source): %s -- %d rows, red %s" % ("as predicted" if ok else "OFF-PREDICTION", len(graded), red or "none"))
for name, want, old, new, must in R1_PLANTS:
    if SRC.count(old) != 1:
        off += 1
        print("%s: BLIND -- its anchor was found %d times" % (name, SRC.count(old)))
        continue
    planted = SRC.replace(old, new)
    assert planted != SRC, name
    pm = load(planted)
    got = sorted(k for k, v in r1(pm).items() if not v)
    shown = "\n".join(pm.ota_lines(19, ROWS, POINTS)[0])
    ok = got == sorted(want) and (must is None or must in shown)
    off += not ok
    print("%s: %s -- red %s (want %s)%s" % (name, "as predicted" if ok else "OFF-PREDICTION", got or "none", sorted(want),
                                            "; %s shown: %s" % (must, must in shown) if must else ""))

# U1: every text-mode subprocess call decodes as UTF-8, strictly. wrangler prints UTF-8. On a Windows machine whose locale
# is cp1252, a text=True call with no encoding= crashed subprocess's reader thread, and the script read no KV row at all
# and exited 2 (v19's step 8, 2026-11-12). CI decodes UTF-8 whatever the call says, so a behaviour test could not fail
# there. This one reads the call sites themselves, and fails on any OS. errors="strict": a byte that isn't UTF-8 is an
# error to see, never a character to invent.
import ast  # noqa: E402


def utf8_violations(source: str) -> list:
    bad = []
    for node in ast.walk(ast.parse(source)):
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                and isinstance(node.func.value, ast.Name) and node.func.value.id == "subprocess"):
            continue
        kw = {k.arg: k.value for k in node.keywords if k.arg}
        text = any(isinstance(kw.get(a), ast.Constant) and kw[a].value is True for a in ("text", "universal_newlines"))
        if not text:
            continue
        enc = kw.get("encoding")
        err = kw.get("errors")
        if not (isinstance(enc, ast.Constant) and enc.value == "utf-8" and isinstance(err, ast.Constant)
                and err.value == "strict"):
            bad.append(node.lineno)
    return bad


u1 = utf8_violations(SRC)
off += bool(u1)
print("U1 (the real source): %s -- text-mode subprocess calls without encoding=\"utf-8\", errors=\"strict\": %s"
      % ("as predicted" if not u1 else "OFF-PREDICTION", u1 or "none"))
# U1-ctl: the same reader must see one call lose its encoding, or a green U1 proves nothing.
U1_ANCHOR = 'text=True, encoding="utf-8", errors="strict", timeout=180)'
if SRC.count(U1_ANCHOR) != 1:
    off += 1
    print("U1-ctl: BLIND -- its anchor was found %d times" % SRC.count(U1_ANCHOR))
else:
    u1c = utf8_violations(SRC.replace(U1_ANCHOR, "text=True, timeout=180)"))
    ok = len(u1c) == 1
    off += not ok
    print("U1-ctl: %s -- one call planted without encoding=, the reader sees %d" % ("as predicted" if ok else "OFF-PREDICTION", len(u1c)))

# BX1-BX4 and S-BX (docs/fleet-bench-boards.md): a bench board is not a fleet unit. The bench file is written to a temp
# dir and named by BLIPSCOPE_BENCH_DEVICES; EXAMPLE ids only. The token-file fixture from TX2 still holds the test id.
BCH = "d4" * 8   # EXAMPLE bench id
BENCH_EXCLUSION = "    kv_ids = [d for d in kv_ids if d not in bench_ids]  # bench boards are not fleet units\n"


def run_bench(source: str, bench_text, git_dir: bool = False, kv=None):
    """(stdout, stderr, exit) with KV = the two units + the test id + BCH (or `kv`). bench_text None = no file."""
    kv = kv if kv is not None else [A, B, T, BCH]
    with tempfile.TemporaryDirectory() as bd:
        root = Path(bd)
        if git_dir:
            (root / ".git").mkdir()
        bench = root / "bench-devices"
        if bench_text is not None:
            bench.write_text(bench_text, encoding="utf-8")
        os.environ["BLIPSCOPE_BENCH_DEVICES"] = str(bench)
        mod = types.ModuleType("reconcile_fleet_bench")
        mod.__file__ = str(HERE / "reconcile-fleet.py")
        exec(compile(source, mod.__file__, "exec"), mod.__dict__)
        mod.kv_list = lambda prefix, env, errfile: list(kv)
        mod.read_csv = lambda path: {A: {}, B: {}}
        mod.report_instruments = lambda *a, **k: []
        out, err, code = io.StringIO(), io.StringIO(), None
        argv = sys.argv
        sys.argv = ["reconcile-fleet.py", "--no-detail", "--log", "fixture.csv"]
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                mod.main()
        except SystemExit as e:
            code = e.code
        finally:
            sys.argv = argv
            del os.environ["BLIPSCOPE_BENCH_DEVICES"]
        return out.getvalue(), err.getvalue(), code


def summary(text):
    return next((l.strip() for l in text.splitlines() if l.startswith("SUMMARY")), "(no summary)")


with tempfile.TemporaryDirectory() as d:
    (Path(d) / "tokens").write_text("# EXAMPLE token file\nkv-read=EXAMPLE-not-a-token\ntest-device=%s\n" % T,
                                    encoding="utf-8")
    os.environ["BLIPSCOPE_TOKEN_FILE"] = str(Path(d) / "tokens")

    o, e, c = run_bench(SRC, "# EXAMPLE bench file\n%s\n" % BCH)
    ok = c in (0, None) and "kv-only=0" in o and "1 row(s) left out as bench" in o and BCH not in o + e
    off += not ok
    print("BX1: %s -- exit %s; %s; bench line present: %s; the bench id printed: %s"
          % ("as predicted" if ok else "OFF-PREDICTION", c, summary(o), "left out as bench" in o, BCH in o + e))

    if SRC.count(BENCH_EXCLUSION) != 1:
        off += 1
        print("S-BX: BLIND -- the bench exclusion line was found %d times" % SRC.count(BENCH_EXCLUSION))
    else:
        o, e, c = run_bench(SRC.replace(BENCH_EXCLUSION, "    pass  # SABOTAGE S-BX: the bench exclusion removed\n"),
                            "%s\n" % BCH)
        ok = c == 1 and "kv-only=1" in o
        off += not ok
        print("S-BX: %s -- exit %s; %s" % ("as predicted" if ok else "OFF-PREDICTION", c, summary(o)))

    o, e, c = run_bench(SRC, "%s\n" % BCH, git_dir=True)
    ok = c == 2 and "SUMMARY" not in o
    off += not ok
    print("BX2: %s -- a bench file inside a git tree: exit %s" % ("as predicted" if ok else "OFF-PREDICTION", c))

    o, e, c = run_bench(SRC, "%s\nnot-an-id\n" % BCH)
    ok = c == 2 and "SUMMARY" not in o
    off += not ok
    print("BX3: %s -- a malformed bench line: exit %s" % ("as predicted" if ok else "OFF-PREDICTION", c))

    o, e, c = run_bench(SRC, None, kv=[A, B, T])
    ok = c in (0, None) and "kv-only=0" in o and "bench boards: none configured" in o
    off += not ok
    print("BX4: %s -- no bench file, TX2's KV: exit %s; %s; 'none configured': %s"
          % ("as predicted" if ok else "OFF-PREDICTION", c, summary(o), "bench boards: none configured" in o))

# --- RC1-RC4 and RC-S1: the route mirror's age line (docs/route-mirror-age.md, section 3) ----------------------------
# route_mirror_lines is run on /healthz bodies built here: no network. Whole lines are compared, never fragments.
RB = "2026-08-26T17:25:54.476Z"


def rc(mod) -> dict:
    body = lambda routes: json.dumps({"ok": True, "commit": "EXAMPLE", **({"routes": routes} if routes is not None else {})})
    fresh = {"builtAt": RB, "ageDays": 3, "rows": 619103, "stale": False, "maxAgeDays": 14}
    res = {}
    lines, warn = mod.route_mirror_lines(body(fresh))
    res["RC1"] = lines == ["  Route mirror (rt:): built %s, 3 d old, 619103 rows (limit 14 d)" % RB] and warn == []
    lines, warn = mod.route_mirror_lines(body(dict(fresh, ageDays=79, stale=True)))
    res["RC2"] = (lines == ["  Route mirror (rt:): built %s, 79 d old, 619103 rows (limit 14 d)  <== STALE" % RB]
                  and warn == ["route mirror: 79 d old, past its 14-day limit"])
    lines, warn = mod.route_mirror_lines(body(None))
    res["RC3"] = warn == ["route mirror: /healthz has no routes field (a Worker before docs/route-mirror-age.md)"]
    lines, warn = mod.route_mirror_lines(None)
    res["RC4"] = warn == ["route mirror: could not read /healthz"]
    return res


import json  # noqa: E402
got = rc(load(SRC))
red = sorted(k for k, v in got.items() if not v)
off += bool(red)
print("RC (the real source): %s -- red %s" % ("as predicted" if not red else "OFF-PREDICTION", red or "none"))
RC_ANCHOR = '    if r.get("stale") is not False:\n'
if SRC.count(RC_ANCHOR) != 1:
    off += 1
    print("RC-S1: BLIND -- its anchor was found %d times" % SRC.count(RC_ANCHOR))
else:
    # RC-S1: the warning branch removed (a stale age reads as fresh), and every early warning silenced.
    planted = SRC.replace(RC_ANCHOR, "    if False:  # SABOTAGE RC-S1\n").replace(
        '["route mirror: could not read /healthz"]', "[]").replace(
        '["route mirror: /healthz has no routes field (a Worker before docs/route-mirror-age.md)"]', "[]")
    assert planted != SRC
    got = sorted(k for k, v in rc(load(planted)).items() if not v)
    ok = got == ["RC2", "RC3", "RC4"]
    off += not ok
    print("RC-S1: %s -- red %s (want ['RC2', 'RC3', 'RC4'])" % ("as predicted" if ok else "OFF-PREDICTION", got))

# RH1: read_healthz's own request carries a User-Agent. Cloudflare's edge answers the default `Python-urllib/3.x` with 403
# before the Worker sees the request (docs/reconcile-fleet-healthz-ua.md), and the RC rows above hand route_mirror_lines a
# fixture BODY, so nothing exercised the request itself: the route-mirror block read nothing on every real run. The stub
# records what read_healthz hands urlopen; no network.
def rh(source: str) -> dict:
    mod = load(source)
    seen = []

    class _Resp(io.BytesIO):
        def __enter__(self):
            return self

        def __exit__(self, *exc):
            return False

    def stub(req, timeout=None):
        seen.append(req)
        return _Resp(b'{"ok": true}')

    real = mod.urllib.request.urlopen
    mod.urllib.request.urlopen = stub
    try:
        mod.read_healthz("production")
    finally:
        mod.urllib.request.urlopen = real
    url = mod.HEALTHZ_URL["production"]
    full = [getattr(r, "full_url", r) for r in seen]
    ua = [r.get_header("User-agent") if hasattr(r, "get_header") else None for r in seen]
    return {"RH1": len(seen) == 1 and full == [url] and bool(ua[0]) and "support@valarsystems.com" in ua[0]
                   and "github.com/Valar-Systems/valar-scopes" in ua[0],
            "RH1-ctl": len(seen) == 1 and full == [url]}


got = rh(SRC)
if not got["RH1-ctl"]:
    off += 1
    print("RH1: BLIND -- the stub was not called once with the production /healthz URL")
else:
    off += not got["RH1"]
    print("RH1: %s -- read_healthz's request %s the User-Agent" % ("as predicted" if got["RH1"] else "OFF-PREDICTION",
                                                                  "carries" if got["RH1"] else "does NOT carry"))
RH_ANCHOR = 'headers={"User-Agent": UA})'
if SRC.count(RH_ANCHOR) != 1:
    off += 1
    print("RH-S1: BLIND -- its anchor was found %d times" % SRC.count(RH_ANCHOR))
else:
    # RH-S1: the User-Agent removed from read_healthz's request. RH1 must go red while RH1-ctl still sees the call.
    planted = SRC.replace(RH_ANCHOR, "headers={})  # SABOTAGE RH-S1")
    assert planted != SRC
    g = rh(planted)
    ok = (not g["RH1"]) and g["RH1-ctl"]
    off += not ok
    print("RH-S1: %s -- the User-Agent removed: RH1 %s, RH1-ctl %s (want RH1 red, RH1-ctl as predicted)"
          % ("as predicted" if ok else "OFF-PREDICTION", "red" if not g["RH1"] else "GREEN", g["RH1-ctl"]))

# LV: were the behind-but-"ok" devices awake at their 03:00? (docs/ota-liveness-read.md) Fixtures only, EXAMPLE ids, no
# network. A (every hour), B (20 of 24), C (boot points only, which are not presence) are behind with "ok". D is present
# every hour but is not in the ok set, and one of A's points falls outside the window.
LA, LB, LC, LD = "d4" * 8, "e5" * 8, "f6" * 8, "a7" * 8
LV_HOURS = ["2026-11-13 %02d:00:00" % h for h in range(24)]
LV_WANT1 = ("    of the 3 behind with last result ok: present every hour of the last 24 h 1, with gaps 1, no request 1"
            " (AE sample interval max 1)")
LV_HEX16 = r"(?<![0-9a-fA-F])[0-9a-fA-F]{16}(?![0-9a-fA-F])"


def lv_rows(si=1):
    rows = [(LA, h, "/v1/blips", si) for h in LV_HOURS]
    rows += [(LB, h, "/v1/blips", si) for h in LV_HOURS[:20]]
    rows += [(LD, h, "/v1/blips", si) for h in LV_HOURS]
    rows += [(LC, h, "boot", si) for h in LV_HOURS]
    rows += [(LA, "2026-11-12 23:00:00", "/v1/blips", si)]
    return rows


def lv(mod) -> dict:
    import re as _re
    res = {}
    l1, w1 = mod.liveness_lines([LA, LB, LC], lv_rows(), "", LV_HOURS)
    res["LV1"] = l1 == [LV_WANT1] and w1 == []
    res["LV2"] = not _re.search(LV_HEX16, "\n".join(l1))
    res["LV2-ctl"] = (bool(l1) and l1[0].startswith("    of the 3 behind with last result ok: ")
                      and "present every hour of the last 24 h 1, with gaps 1, no request 1 (AE sample interval max 1)"
                      in l1[0])
    l3, w3 = mod.liveness_lines([LA, LB, LC], None, "EXAMPLE reason", LV_HOURS)
    res["LV3"] = l3 == ["    liveness unread: EXAMPLE reason"] and len(w3) == 1
    called = []
    l4, w4 = mod.liveness_report([], lambda: called.append(1) or ([], ""), LV_HOURS)
    res["LV4"] = l4 == ["    of the 0 behind with last result ok: nothing to read"] and w4 == [] and not called
    l5, w5 = mod.liveness_lines([LA, LB, LC], lv_rows(si=10), "", LV_HOURS)
    res["LV5"] = l5 == [LV_WANT1.replace("max 1)", "max 10)")] and len(w5) == 1 and "sampl" in w5[0]
    # LV6: the copied error path keeps Analytics Engine's own reason, as R1h proves for read_ota_points (a second path
    # proves nothing by the first one's row).
    import urllib.error as _ue

    def refuse(*a, **k):
        raise _ue.HTTPError("https://example.invalid", 422, "EXAMPLE", {}, io.BytesIO(b"EXAMPLE: unable to find type"))
    real = mod.urllib.request.urlopen
    mod.urllib.request.urlopen = refuse
    try:
        rows6, why6 = mod.read_liveness_rows("production", "EXAMPLE-not-a-token")
    finally:
        mod.urllib.request.urlopen = real
    res["LV6"] = rows6 is None and "422" in why6 and "EXAMPLE: unable to find type" in why6
    return res


_lvmod = load(SRC)
if not all(hasattr(_lvmod, n) for n in ("liveness_lines", "liveness_report")):
    off += 1
    for name in ("LV1", "LV2", "LV2-ctl", "LV3", "LV4", "LV5", "LV-S1"):
        print("%s: BLIND -- liveness_lines / liveness_report do not exist" % name)
else:
    got = lv(_lvmod)
    red = sorted(k for k, v in got.items() if not v)
    off += bool(red)
    print("LV (the real source): %s -- red %s" % ("as predicted" if not red else "OFF-PREDICTION", red or "none"))
    LV_ANCHOR = '" (AE sample interval max %s)" % (k, LIVE_HOURS, x, y, z, s_txt))'
    if SRC.count(LV_ANCHOR) != 1:
        off += 1
        print("LV-S1: BLIND -- its anchor was found %d times" % SRC.count(LV_ANCHOR))
    else:
        # LV-S1: the line's format gains the ids. LV2 must go red; LV2-ctl still sees the counts, so LV2 is not
        # passing on an empty or truncated line.
        planted = SRC.replace(LV_ANCHOR,
                              '" (AE sample interval max %s) %s" % (k, LIVE_HOURS, x, y, z, s_txt, ",".join(sorted(okset))))')
        assert planted != SRC
        g = lv(load(planted))
        ok = (not g["LV2"]) and g["LV2-ctl"]
        off += not ok
        print("LV-S1: %s -- the ids planted into the line: LV2 %s, LV2-ctl %s (want LV2 red, LV2-ctl as predicted)"
              % ("as predicted" if ok else "OFF-PREDICTION", "red" if not g["LV2"] else "GREEN", g["LV2-ctl"]))
    LVB_ANCHOR = ('        return None, "Analytics Engine refused the liveness query (%d): %s" % (e.code, e.read().decode('
                  '"utf-8", "replace").strip()[:300])\n')
    if SRC.count(LVB_ANCHOR) != 1:
        off += 1
        print("S-R1body-lv: BLIND -- its anchor was found %d times" % SRC.count(LVB_ANCHOR))
    else:
        # S-R1body-lv: the liveness reader's error drops Analytics Engine's body. Exactly LV6 must go red.
        planted = SRC.replace(LVB_ANCHOR, '        return None, "Analytics Engine refused the liveness query (%d)" % e.code'
                                          '  # SABOTAGE S-R1body-lv\n')
        assert planted != SRC
        g = sorted(k for k, v in lv(load(planted)).items() if not v)
        ok = g == ["LV6"]
        off += not ok
        print("S-R1body-lv: %s -- red %s (want ['LV6'])" % ("as predicted" if ok else "OFF-PREDICTION", g))

print("SELFTEST PASSED" if not off else "SELFTEST: %d row(s) off" % off)
sys.exit(1 if off else 0)
